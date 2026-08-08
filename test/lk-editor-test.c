/*
 * lk-editor-test.c -- editor track stage B2: lk_editor view state,
 * the command layer, and the UIK_EDITOR widget (docs/editor.md
 * sections 6, 7, 9).
 *
 * All geometry runs against the deterministic stub text backend
 * (8 px per codepoint, line height 16), so pixel assertions are
 * exact.  Blocks: (a) command layer driven directly (no events),
 * (b) event-tier key/text translation through lk_event_route,
 * (c) pointer click/drag with capture, (d) anchored viewport +
 * virtualization (DRAW_RUN commands read back from the byte arena),
 * (e) render details (cursor, selection rects, tab expansion),
 * (f) degradation (missing/stale/wrong-typed refs), (g) UTF-8
 * integrity under editing.
 */

#include <stdio.h>
#include <string.h>

#include <lk-editor.h>
#include <lk.h>

#include "lk-test-harness.h"

/* ---- fake clipboard ---- */

static char g_clip[256];

static const char *clip_get(void *ud) {
  (void)ud;

  return g_clip;
}

static void clip_set(void *ud, const char *text) {
  (void)ud;
  strncpy(g_clip, text, sizeof(g_clip) - 1);
  g_clip[sizeof(g_clip) - 1] = '\0';
}

/* ---- fixture ---- */

typedef struct ed_fix {
  lk_ui *ui;
  lk_document *doc;
  lk_edit_history *hist;
  lk_editor *ed;
  lk_resource_ref ref;
  lk_ix node;     /* editor node index in the current tree */
  lk_node_id nid; /* stable node id */
  lk_rect rects[8];
  lk_layout_cfg cfg;
} ed_fix;

/* Build the standard frame: window "w" > editor "ed" (FOCUSABLE,
 * UIP_EDITOR ref when with_prop). */
static void fix_frame(ed_fix *f, int with_prop) {
  lk_tree *t = lk_ui_begin_frame(f->ui);
  lk_ix w = lk_tree_add_node_c(t, "w", UIK_WINDOW);
  lk_ix ed = lk_tree_add_node_c(t, "ed", UIK_EDITOR);

  lk_tree_set_root(t, w);
  lk_tree_append_child(t, w, ed);
  lk_tree_add_prop(t, ed, UIP_FOCUSABLE, lk_v_bool(1));

  if (with_prop) {
    lk_tree_add_prop(t, ed, UIP_EDITOR, lk_v_resource(f->ref));
  }

  lk_ui_end_frame(f->ui);
  f->node = lk_tree_find_by_id(lk_ui_tree(f->ui),
                               lk_intern_cid(lk_ui_intern(f->ui), "ed"));
}

static int fix_layout(ed_fix *f) {
  return lk_layout(lk_ui_tree(f->ui), &f->cfg, f->rects);
}

/* Anchor line of the viewport (top_byte is a row-start byte since
 * the W1 viewport change; these tests assert line numbers). */
static lk_u32 vp_top_line(ed_fix *f) {
  return lk_doc_pos_to_line(f->doc, lk_editor_get_viewport(f->ed).top_byte);
}

static void fix_init_ex(ed_fix *f, const char *text, lk_i32 vw, lk_i32 vh,
                        int with_prop) {
  memset(f, 0, sizeof(*f));
  f->doc = lk_doc_from_str(NULL, NULL, NULL, text, (lk_u32)strlen(text));
  f->hist = lk_history_new(NULL, NULL, NULL);
  lk_history_attach(f->hist, f->doc);
  f->ed = lk_editor_new(NULL, NULL, NULL, f->doc, f->hist);
  f->ui = lk_ui_create(NULL);
  f->ref = lk_resource_register(lk_ui_resources(f->ui), lk_editor_type(),
                                f->ed, "ed");
  lk_ui_set_text_backend(f->ui, lk_text_backend_stub());
  f->cfg.text = lk_text_backend_stub();
  f->cfg.viewport_w = vw;
  f->cfg.viewport_h = vh;
  f->cfg.state = lk_ui_state(f->ui);
  fix_frame(f, with_prop);
  fix_layout(f);
  f->nid = lk_intern_cid(lk_ui_intern(f->ui), "ed");
}

static void fix_init(ed_fix *f, const char *text, lk_i32 vw, lk_i32 vh) {
  fix_init_ex(f, text, vw, vh, 1);
}

static void fix_destroy(ed_fix *f) {
  lk_ui_destroy(f->ui);
  lk_editor_destroy(f->ed);
  lk_history_destroy(f->hist);
  lk_doc_destroy(f->doc);
}

/* ---- document content check ---- */

static int doc_is(const lk_document *d, const char *expect) {
  char buf[512];
  lk_u32 n = lk_doc_len(d);

  if (n >= sizeof(buf)) {
    return 0;
  }

  lk_doc_get_text(d, 0, buf, n);
  buf[n] = '\0';

  return strcmp(buf, expect) == 0;
}

/* ---- command helpers ---- */

static int cmd0(ed_fix *f, lk_editor_cmd_id c) {
  return lk_editor_command(f->ed, f->ui, c, NULL);
}

static int cmd_move(ed_fix *f, lk_editor_cmd_id c, int select) {
  lk_editor_cmd_arg a;

  memset(&a, 0, sizeof(a));
  a.select = select;

  return lk_editor_command(f->ed, f->ui, c, &a);
}

static int cmd_ins(ed_fix *f, const char *s) {
  lk_editor_cmd_arg a;

  memset(&a, 0, sizeof(a));
  a.text.ptr = s;
  a.text.len = (lk_u32)strlen(s);

  return lk_editor_command(f->ed, f->ui, LK_ED_INSERT_TEXT, &a);
}

static int cmd_setcur(ed_fix *f, lk_u32 pos, int extend) {
  lk_editor_cmd_arg a;

  memset(&a, 0, sizeof(a));
  a.set_cursor.pos = pos;
  a.set_cursor.extend = extend;

  return lk_editor_command(f->ed, f->ui, LK_ED_SET_CURSOR, &a);
}

static int cmd_scroll(ed_fix *f, lk_i32 lines) {
  lk_editor_cmd_arg a;

  memset(&a, 0, sizeof(a));
  a.lines = lines;

  return lk_editor_command(f->ed, f->ui, LK_ED_SCROLL_LINES, &a);
}

/* ---- event helpers ---- */

static int send_key(ed_fix *f, lk_u16 kc, lk_u8 mods) {
  lk_event ev;

  memset(&ev, 0, sizeof(ev));
  ev.type = LK_EVENT_KEY_DOWN;
  ev.target = f->node;
  ev.mods = mods;
  ev.data.key.keycode = kc;
  lk_event_route(f->ui, &ev);

  return ev.handled;
}

static int send_text(ed_fix *f, const char *s) {
  lk_event ev;
  lk_u32 n = (lk_u32)strlen(s);

  memset(&ev, 0, sizeof(ev));
  ev.type = LK_EVENT_TEXT;
  ev.target = f->node;
  memcpy(ev.data.text.buf, s, n);
  ev.data.text.len = (lk_u8)n;
  lk_event_route(f->ui, &ev);

  return ev.handled;
}

static int send_pointer(ed_fix *f, lk_u8 type, lk_i32 x, lk_i32 y) {
  lk_event ev;

  memset(&ev, 0, sizeof(ev));
  ev.type = type;
  ev.target = f->node;
  ev.data.pointer.x = x;
  ev.data.pointer.y = y;
  lk_event_route(f->ui, &ev);

  return ev.handled;
}

static int send_wheel(ed_fix *f, lk_i32 dy) {
  lk_event ev;

  memset(&ev, 0, sizeof(ev));
  ev.type = LK_EVENT_WHEEL;
  ev.target = f->node;
  ev.data.wheel.dy = dy;
  lk_event_route(f->ui, &ev);

  return ev.handled;
}

static int send_wheel_ex(ed_fix *f, lk_i32 dx, lk_i32 dy, lk_u8 mods) {
  lk_event ev;

  memset(&ev, 0, sizeof(ev));
  ev.type = LK_EVENT_WHEEL;
  ev.target = f->node;
  ev.mods = mods;
  ev.data.wheel.dx = dx;
  ev.data.wheel.dy = dy;
  lk_event_route(f->ui, &ev);

  return ev.handled;
}

/* Switch to character wrap and re-run layout (the wrap key is
 * stamped by the layout hook). */
static void fix_wrap(ed_fix *f) {
  CHECK(lk_editor_set_wrap_mode(f->ed, LK_EDITOR_WRAP_CHARACTER) == 1);
  fix_layout(f);
}

/* Same, word wrap. */
static void fix_wrap_word(ed_fix *f) {
  CHECK(lk_editor_set_wrap_mode(f->ed, LK_EDITOR_WRAP_WORD) == 1);
  fix_layout(f);
}

/* ---- render-list query helpers ---- */

static lk_u32 count_runs(const lk_render_list *rl) {
  lk_u32 i;
  lk_u32 n = 0;

  for (i = 0; i < rl->count; i++) {
    if (rl->cmds[i].op == LK_ROP_DRAW_RUN) {
      n++;
    }
  }

  return n;
}

static const lk_render_cmd *nth_run(const lk_render_list *rl, lk_u32 idx) {
  lk_u32 i;
  lk_u32 n = 0;

  for (i = 0; i < rl->count; i++) {
    if (rl->cmds[i].op == LK_ROP_DRAW_RUN) {
      if (n == idx) {
        return &rl->cmds[i];
      }

      n++;
    }
  }

  return NULL;
}

/* 1 if the run's arena bytes equal s. */
static int run_is(const lk_render_list *rl, const lk_render_cmd *c,
                  const char *s) {
  lk_u32 n = (lk_u32)strlen(s);

  if (!c || c->run_len != n) {
    return 0;
  }

  return memcmp(rl->bytes + c->run_off, s, n) == 0;
}

static int is_sel_fill(const lk_render_cmd *c) {
  return c->op == LK_ROP_FILL_RECT && c->color.r == 80 &&
         c->color.g == 120 && c->color.b == 200 && c->color.a == 128;
}

static lk_u32 count_sel_fills(const lk_render_list *rl) {
  lk_u32 i;
  lk_u32 n = 0;

  for (i = 0; i < rl->count; i++) {
    if (is_sel_fill(&rl->cmds[i])) {
      n++;
    }
  }

  return n;
}

static const lk_render_cmd *nth_sel_fill(const lk_render_list *rl,
                                         lk_u32 idx) {
  lk_u32 i;
  lk_u32 n = 0;

  for (i = 0; i < rl->count; i++) {
    if (is_sel_fill(&rl->cmds[i])) {
      if (n == idx) {
        return &rl->cmds[i];
      }

      n++;
    }
  }

  return NULL;
}

/* The cursor bar is the only 1-px-wide FILL_RECT the editor emits. */
static const lk_render_cmd *find_cursor_fill(const lk_render_list *rl) {
  lk_u32 i;

  for (i = 0; i < rl->count; i++) {
    if (rl->cmds[i].op == LK_ROP_FILL_RECT && rl->cmds[i].rect.w == 1) {
      return &rl->cmds[i];
    }
  }

  return NULL;
}

static int fix_render(ed_fix *f, lk_render_list *rl) {
  return lk_render_build(lk_ui_tree(f->ui), f->rects, NULL,
                         lk_ui_state(f->ui), NULL, rl);
}

/* Build "line 0\nline 1\n...\nline N-1" (no trailing newline). */
static void make_lines(char *buf, lk_u32 cap, int nlines) {
  int i;
  lk_u32 off = 0;

  buf[0] = '\0';

  for (i = 0; i < nlines; i++) {
    off += (lk_u32)sprintf(buf + off, i + 1 < nlines ? "line %d\n" : "line %d",
                           i);

    if (off + 16 > cap) {
      break;
    }
  }
}

/* ================================================================
 * (a) command layer, driven directly
 * ================================================================ */

static void test_cmd_insert_text(void) {
  ed_fix f;

  BEGIN_TEST("ed cmd: insert text moves cursor");

  fix_init(&f, "", 400, 80);

  CHECK(cmd_ins(&f, "hello") == 1);
  CHECK(doc_is(f.doc, "hello"));
  CHECK_EQ(lk_editor_cursor(f.ed), 5);

  CHECK(cmd_ins(&f, " world") == 1);
  CHECK(doc_is(f.doc, "hello world"));
  CHECK_EQ(lk_editor_cursor(f.ed), 11);

  /* empty insert does nothing */
  CHECK(cmd_ins(&f, "") == 0);
  CHECK(lk_editor_command(f.ed, f.ui, LK_ED_INSERT_TEXT, NULL) == 0);

  fix_destroy(&f);
  END_TEST();
}

static void test_cmd_delete_utf8(void) {
  ed_fix f;

  BEGIN_TEST("ed cmd: delete removes whole codepoints");

  /* "a" + e-acute (2 bytes) + CJK sun (3 bytes) */
  fix_init(&f, "a\xC3\xA9\xE6\x97\xA5", 400, 80);

  cmd_setcur(&f, 6, 0);

  CHECK(cmd0(&f, LK_ED_DELETE_BACKWARD) == 1);
  CHECK(doc_is(f.doc, "a\xC3\xA9"));
  CHECK_EQ(lk_editor_cursor(f.ed), 3);

  CHECK(cmd0(&f, LK_ED_DELETE_BACKWARD) == 1);
  CHECK(doc_is(f.doc, "a"));
  CHECK_EQ(lk_editor_cursor(f.ed), 1);

  cmd_setcur(&f, 0, 0);

  CHECK(cmd0(&f, LK_ED_DELETE_FORWARD) == 1);
  CHECK(doc_is(f.doc, ""));

  /* nothing left */
  CHECK(cmd0(&f, LK_ED_DELETE_FORWARD) == 0);
  CHECK(cmd0(&f, LK_ED_DELETE_BACKWARD) == 0);

  fix_destroy(&f);
  END_TEST();
}

static void test_cmd_move_codepoints(void) {
  ed_fix f;

  BEGIN_TEST("ed cmd: left/right move by codepoint");

  fix_init(&f, "a\xC3\xA9\xE6\x97\xA5", 400, 80);

  cmd_setcur(&f, 0, 0);

  CHECK(cmd_move(&f, LK_ED_MOVE_RIGHT, 0) == 1);
  CHECK_EQ(lk_editor_cursor(f.ed), 1);
  CHECK(cmd_move(&f, LK_ED_MOVE_RIGHT, 0) == 1);
  CHECK_EQ(lk_editor_cursor(f.ed), 3);
  CHECK(cmd_move(&f, LK_ED_MOVE_RIGHT, 0) == 1);
  CHECK_EQ(lk_editor_cursor(f.ed), 6);
  CHECK(cmd_move(&f, LK_ED_MOVE_RIGHT, 0) == 0); /* at end */

  CHECK(cmd_move(&f, LK_ED_MOVE_LEFT, 0) == 1);
  CHECK_EQ(lk_editor_cursor(f.ed), 3);
  CHECK(cmd_move(&f, LK_ED_MOVE_LEFT, 0) == 1);
  CHECK_EQ(lk_editor_cursor(f.ed), 1);
  CHECK(cmd_move(&f, LK_ED_MOVE_LEFT, 0) == 1);
  CHECK_EQ(lk_editor_cursor(f.ed), 0);
  CHECK(cmd_move(&f, LK_ED_MOVE_LEFT, 0) == 0); /* at start */

  fix_destroy(&f);
  END_TEST();
}

static void test_cmd_word_motion(void) {
  ed_fix f;

  BEGIN_TEST("ed cmd: word motion incl punctuation runs");

  /* f o o _ _ b a r _ b  a  z  ;  ;  q  u  x
   * 0 1 2 3 4 5 6 7 8 9 10 11 12 13 14 15 16   (len 17) */
  fix_init(&f, "foo  bar_baz;;qux", 400, 80);

  cmd_setcur(&f, 0, 0);

  CHECK(cmd_move(&f, LK_ED_MOVE_WORD_RIGHT, 0) == 1);
  CHECK_EQ(lk_editor_cursor(f.ed), 5);
  CHECK(cmd_move(&f, LK_ED_MOVE_WORD_RIGHT, 0) == 1);
  CHECK_EQ(lk_editor_cursor(f.ed), 14);
  CHECK(cmd_move(&f, LK_ED_MOVE_WORD_RIGHT, 0) == 1);
  CHECK_EQ(lk_editor_cursor(f.ed), 17);
  CHECK(cmd_move(&f, LK_ED_MOVE_WORD_RIGHT, 0) == 0); /* doc end */

  CHECK(cmd_move(&f, LK_ED_MOVE_WORD_LEFT, 0) == 1);
  CHECK_EQ(lk_editor_cursor(f.ed), 14);
  CHECK(cmd_move(&f, LK_ED_MOVE_WORD_LEFT, 0) == 1);
  CHECK_EQ(lk_editor_cursor(f.ed), 5);
  CHECK(cmd_move(&f, LK_ED_MOVE_WORD_LEFT, 0) == 1);
  CHECK_EQ(lk_editor_cursor(f.ed), 0);
  CHECK(cmd_move(&f, LK_ED_MOVE_WORD_LEFT, 0) == 0); /* doc start */

  fix_destroy(&f);
  END_TEST();
}

static void test_cmd_word_motion_multibyte(void) {
  ed_fix f;

  BEGIN_TEST("ed cmd: word motion over multi-byte word chars");

  /* "hé..." spelled precomposed: h e-acute space w o-umlaut
   * bytes: h0 e-acute(1-2) sp3 w4 o-umlaut(5-6)  (len 7) */
  fix_init(&f, "h\xC3\xA9 w\xC3\xB6", 400, 80);

  cmd_setcur(&f, 0, 0);

  CHECK(cmd_move(&f, LK_ED_MOVE_WORD_RIGHT, 0) == 1);
  CHECK_EQ(lk_editor_cursor(f.ed), 4); /* after "he'" + space */
  CHECK(cmd_move(&f, LK_ED_MOVE_WORD_RIGHT, 0) == 1);
  CHECK_EQ(lk_editor_cursor(f.ed), 7);

  CHECK(cmd_move(&f, LK_ED_MOVE_WORD_LEFT, 0) == 1);
  CHECK_EQ(lk_editor_cursor(f.ed), 4);
  CHECK(cmd_move(&f, LK_ED_MOVE_WORD_LEFT, 0) == 1);
  CHECK_EQ(lk_editor_cursor(f.ed), 0);

  fix_destroy(&f);
  END_TEST();
}

static void test_cmd_selection_extend_collapse(void) {
  ed_fix f;
  lk_u32 s;
  lk_u32 e;

  BEGIN_TEST("ed cmd: selection extend + collapse to edge");

  fix_init(&f, "abcdef", 400, 80);

  cmd_setcur(&f, 1, 0);

  CHECK(lk_editor_selection(f.ed, &s, &e) == 0);

  cmd_move(&f, LK_ED_MOVE_RIGHT, 1);
  cmd_move(&f, LK_ED_MOVE_RIGHT, 1);

  CHECK(lk_editor_selection(f.ed, &s, &e) == 1);
  CHECK_EQ(s, 1);
  CHECK_EQ(e, 3);
  CHECK_EQ(lk_editor_cursor(f.ed), 3);

  /* plain LEFT collapses to the selection's left edge */
  CHECK(cmd_move(&f, LK_ED_MOVE_LEFT, 0) == 1);
  CHECK_EQ(lk_editor_cursor(f.ed), 1);
  CHECK(lk_editor_selection(f.ed, &s, &e) == 0);

  /* and RIGHT to the right edge */
  cmd_move(&f, LK_ED_MOVE_RIGHT, 1);
  cmd_move(&f, LK_ED_MOVE_RIGHT, 1);
  CHECK(cmd_move(&f, LK_ED_MOVE_RIGHT, 0) == 1);
  CHECK_EQ(lk_editor_cursor(f.ed), 3);
  CHECK(lk_editor_selection(f.ed, &s, &e) == 0);

  fix_destroy(&f);
  END_TEST();
}

static void test_cmd_select_all(void) {
  ed_fix f;
  lk_u32 s;
  lk_u32 e;

  BEGIN_TEST("ed cmd: select all");

  fix_init(&f, "abc", 400, 80);

  CHECK(cmd0(&f, LK_ED_SELECT_ALL) == 1);
  CHECK(lk_editor_selection(f.ed, &s, &e) == 1);
  CHECK_EQ(s, 0);
  CHECK_EQ(e, 3);
  CHECK_EQ(lk_editor_cursor(f.ed), 3);

  fix_destroy(&f);
  END_TEST();
}

static void test_cmd_replace_selection_one_undo(void) {
  ed_fix f;

  BEGIN_TEST("ed cmd: replace-selection is ONE undo step");

  fix_init(&f, "hello", 400, 80);

  cmd_setcur(&f, 1, 0);
  cmd_setcur(&f, 4, 1); /* select "ell" */

  CHECK(cmd_ins(&f, "X") == 1);
  CHECK(doc_is(f.doc, "hXo"));
  CHECK_EQ(lk_editor_cursor(f.ed), 2);

  /* one undo restores the whole replace; cursor derives from the
   * undo transaction's deltas (end of the re-inserted "ell") */
  CHECK(cmd0(&f, LK_ED_UNDO) == 1);
  CHECK(doc_is(f.doc, "hello"));
  CHECK_EQ(lk_editor_cursor(f.ed), 4);

  CHECK(cmd0(&f, LK_ED_REDO) == 1);
  CHECK(doc_is(f.doc, "hXo"));
  CHECK_EQ(lk_editor_cursor(f.ed), 2);

  fix_destroy(&f);
  END_TEST();
}

static void test_cmd_word_delete(void) {
  ed_fix f;

  BEGIN_TEST("ed cmd: word delete backward/forward");

  fix_init(&f, "foo bar", 400, 80);

  cmd_setcur(&f, 7, 0);

  CHECK(cmd0(&f, LK_ED_DELETE_WORD_BACKWARD) == 1);
  CHECK(doc_is(f.doc, "foo "));
  CHECK_EQ(lk_editor_cursor(f.ed), 4);

  CHECK(cmd0(&f, LK_ED_DELETE_WORD_BACKWARD) == 1);
  CHECK(doc_is(f.doc, ""));
  CHECK_EQ(lk_editor_cursor(f.ed), 0);

  cmd_ins(&f, "foo bar");
  cmd_setcur(&f, 0, 0);

  /* forward deletes the word AND its trailing separators (weft) */
  CHECK(cmd0(&f, LK_ED_DELETE_WORD_FORWARD) == 1);
  CHECK(doc_is(f.doc, "bar"));
  CHECK_EQ(lk_editor_cursor(f.ed), 0);

  fix_destroy(&f);
  END_TEST();
}

static void test_cmd_line_doc_motion(void) {
  ed_fix f;

  BEGIN_TEST("ed cmd: line/doc start+end motion");

  /* a0 b1 \n2 c3 d4 \n5 e6 f7 */
  fix_init(&f, "ab\ncd\nef", 400, 80);

  cmd_setcur(&f, 4, 0);

  CHECK(cmd_move(&f, LK_ED_MOVE_LINE_START, 0) == 1);
  CHECK_EQ(lk_editor_cursor(f.ed), 3);
  CHECK(cmd_move(&f, LK_ED_MOVE_LINE_END, 0) == 1);
  CHECK_EQ(lk_editor_cursor(f.ed), 5);
  CHECK(cmd_move(&f, LK_ED_MOVE_DOC_END, 0) == 1);
  CHECK_EQ(lk_editor_cursor(f.ed), 8);
  CHECK(cmd_move(&f, LK_ED_MOVE_DOC_START, 0) == 1);
  CHECK_EQ(lk_editor_cursor(f.ed), 0);

  fix_destroy(&f);
  END_TEST();
}

static void test_cmd_vertical_sticky_x(void) {
  ed_fix f;

  BEGIN_TEST("ed cmd: vertical motion preserves sticky x");

  /* line0 "abcdef" (0-5), line1 "ab" (7-8), line2 "abcdef" (10-15) */
  fix_init(&f, "abcdef\nab\nabcdef", 400, 80);

  cmd_setcur(&f, 4, 0); /* x = 32 */

  CHECK(cmd_move(&f, LK_ED_MOVE_DOWN, 0) == 1);
  CHECK_EQ(lk_editor_cursor(f.ed), 9); /* clamped to "ab" end */
  CHECK(cmd_move(&f, LK_ED_MOVE_DOWN, 0) == 1);
  CHECK_EQ(lk_editor_cursor(f.ed), 14); /* sticky 32 restored */
  CHECK(cmd_move(&f, LK_ED_MOVE_UP, 0) == 1);
  CHECK_EQ(lk_editor_cursor(f.ed), 9);
  CHECK(cmd_move(&f, LK_ED_MOVE_UP, 0) == 1);
  CHECK_EQ(lk_editor_cursor(f.ed), 4);

  /* boundary behavior (weft): UP on first line -> 0, DOWN on last
   * line -> doc end */
  CHECK(cmd_move(&f, LK_ED_MOVE_UP, 0) == 1);
  CHECK_EQ(lk_editor_cursor(f.ed), 0);
  cmd_setcur(&f, 12, 0);
  CHECK(cmd_move(&f, LK_ED_MOVE_DOWN, 0) == 1);
  CHECK_EQ(lk_editor_cursor(f.ed), 16);

  fix_destroy(&f);
  END_TEST();
}

static void test_cmd_page_fallback_null_ui(void) {
  lk_document *doc;
  lk_editor *ed;
  char buf[512];
  int i;
  lk_u32 off = 0;
  lk_editor_cmd_arg a;

  BEGIN_TEST("ed cmd: page motion fallback, NULL ui");

  /* 50 lines of "x" */
  for (i = 0; i < 50; i++) {
    buf[off++] = 'x';

    if (i < 49) {
      buf[off++] = '\n';
    }
  }

  doc = lk_doc_from_str(NULL, NULL, NULL, buf, off);
  ed = lk_editor_new(NULL, NULL, NULL, doc, NULL);

  CHECK(ed != NULL);

  /* no layout ever ran, ui is NULL: page falls back to 20 lines */
  memset(&a, 0, sizeof(a));
  CHECK(lk_editor_command(ed, NULL, LK_ED_MOVE_PAGE_DOWN, &a) == 1);
  CHECK_EQ(lk_doc_pos_to_line(doc, lk_editor_cursor(ed)), 20);
  CHECK(lk_editor_command(ed, NULL, LK_ED_MOVE_PAGE_DOWN, &a) == 1);
  CHECK_EQ(lk_doc_pos_to_line(doc, lk_editor_cursor(ed)), 40);
  CHECK(lk_editor_command(ed, NULL, LK_ED_MOVE_PAGE_UP, &a) == 1);
  CHECK_EQ(lk_doc_pos_to_line(doc, lk_editor_cursor(ed)), 20);

  /* NULL-ui editing works too */
  {
    lk_editor_cmd_arg t;

    memset(&t, 0, sizeof(t));
    t.text.ptr = "yo";
    t.text.len = 2;

    CHECK(lk_editor_command(ed, NULL, LK_ED_INSERT_TEXT, &t) == 1);
  }

  /* NULL-ui clipboard degrades to a no-op */
  CHECK(lk_editor_command(ed, NULL, LK_ED_COPY, NULL) == 0);
  CHECK(lk_editor_command(ed, NULL, LK_ED_PASTE, NULL) == 0);

  /* NULL history: undo/redo no-op */
  CHECK(lk_editor_command(ed, NULL, LK_ED_UNDO, NULL) == 0);
  CHECK(lk_editor_command(ed, NULL, LK_ED_REDO, NULL) == 0);

  lk_editor_destroy(ed);
  lk_doc_destroy(doc);
  END_TEST();
}

static void test_cmd_set_cursor_snaps(void) {
  ed_fix f;

  BEGIN_TEST("ed cmd: set cursor clamps + snaps to boundary");

  fix_init(&f, "a\xC3\xA9", 400, 80);

  cmd_setcur(&f, 2, 0); /* mid e-acute */
  CHECK_EQ(lk_editor_cursor(f.ed), 1);

  cmd_setcur(&f, 99, 0); /* past end */
  CHECK_EQ(lk_editor_cursor(f.ed), 3);

  lk_editor_set_cursor(f.ed, 2); /* public accessor, same rules */
  CHECK_EQ(lk_editor_cursor(f.ed), 1);

  fix_destroy(&f);
  END_TEST();
}

static void test_cmd_clipboard(void) {
  ed_fix f;
  lk_u32 s;
  lk_u32 e;

  BEGIN_TEST("ed cmd: copy/cut/paste through ui clipboard");

  fix_init(&f, "hello", 400, 80);
  lk_ui_set_clipboard(f.ui, clip_get, clip_set, NULL);
  g_clip[0] = '\0';

  /* copy with no selection: nothing */
  CHECK(cmd0(&f, LK_ED_COPY) == 0);

  cmd_setcur(&f, 1, 0);
  cmd_setcur(&f, 4, 1); /* "ell" */

  CHECK(cmd0(&f, LK_ED_COPY) == 1);
  CHECK(strcmp(g_clip, "ell") == 0);
  CHECK(doc_is(f.doc, "hello")); /* copy does not edit */

  CHECK(cmd0(&f, LK_ED_CUT) == 1);
  CHECK(doc_is(f.doc, "ho"));
  CHECK_EQ(lk_editor_cursor(f.ed), 1);
  CHECK(lk_editor_selection(f.ed, &s, &e) == 0);

  cmd_setcur(&f, 2, 0);

  CHECK(cmd0(&f, LK_ED_PASTE) == 1);
  CHECK(doc_is(f.doc, "hoell"));
  CHECK_EQ(lk_editor_cursor(f.ed), 5);

  /* paste replaces an active selection in one step */
  cmd_setcur(&f, 0, 0);
  cmd_setcur(&f, 5, 1);
  CHECK(cmd0(&f, LK_ED_PASTE) == 1);
  CHECK(doc_is(f.doc, "ell"));
  CHECK(cmd0(&f, LK_ED_UNDO) == 1);
  CHECK(doc_is(f.doc, "hoell"));

  fix_destroy(&f);
  END_TEST();
}

static void test_cmd_readonly_policy(void) {
  ed_fix f;

  BEGIN_TEST("ed cmd: read-only rejects user mutations");

  fix_init(&f, "hello", 400, 80);
  lk_ui_set_clipboard(f.ui, clip_get, clip_set, NULL);
  g_clip[0] = '\0';

  CHECK(lk_editor_editable(f.ed) == 1);

  lk_editor_set_editable(f.ed, 0);
  CHECK(lk_editor_editable(f.ed) == 0);

  /* every user mutation is rejected and the document is untouched */
  CHECK(cmd_ins(&f, "x") == 0);
  CHECK(cmd0(&f, LK_ED_DELETE_BACKWARD) == 0);
  CHECK(cmd0(&f, LK_ED_DELETE_FORWARD) == 0);
  CHECK(cmd0(&f, LK_ED_DELETE_WORD_BACKWARD) == 0);
  CHECK(cmd0(&f, LK_ED_DELETE_WORD_FORWARD) == 0);
  CHECK(cmd0(&f, LK_ED_CUT) == 0);
  CHECK(cmd0(&f, LK_ED_PASTE) == 0);
  CHECK(cmd0(&f, LK_ED_UNDO) == 0);
  CHECK(cmd0(&f, LK_ED_REDO) == 0);
  CHECK(doc_is(f.doc, "hello"));

  /* motion, selection, and copy still work */
  CHECK(cmd_move(&f, LK_ED_MOVE_RIGHT, 0) == 1);
  CHECK_EQ(lk_editor_cursor(f.ed), 1);
  CHECK(cmd0(&f, LK_ED_SELECT_ALL) == 1);
  CHECK(cmd0(&f, LK_ED_COPY) == 1);

  /* document-level edits are NOT gated: read-only is an editor
   * policy, so programmatic refresh keeps working */
  lk_doc_insert(f.doc, 0, "z", 1);
  CHECK(doc_is(f.doc, "zhello"));

  /* re-enable and mutate again */
  lk_editor_set_editable(f.ed, 1);
  lk_editor_set_cursor(f.ed, 0);
  CHECK(cmd_ins(&f, "a") == 1);
  CHECK(doc_is(f.doc, "azhello"));

  fix_destroy(&f);
  END_TEST();
}

/* ================================================================
 * (b) event tier
 * ================================================================ */

static void test_event_typing(void) {
  ed_fix f;

  BEGIN_TEST("ed event: LK_EVENT_TEXT inserts");

  fix_init(&f, "", 400, 80);

  CHECK(send_text(&f, "hi") == 1);
  CHECK(doc_is(f.doc, "hi"));
  CHECK(send_text(&f, "!") == 1);
  CHECK(doc_is(f.doc, "hi!"));
  CHECK_EQ(lk_editor_cursor(f.ed), 3);

  fix_destroy(&f);
  END_TEST();
}

static void test_event_return_tab_esc(void) {
  ed_fix f;

  BEGIN_TEST("ed event: RETURN/TAB consumed, ESC bubbles");

  fix_init(&f, "", 400, 80);

  CHECK(send_key(&f, LKK_RETURN, 0) == 1);
  CHECK(doc_is(f.doc, "\n"));

  /* Modified RETURN has no editor meaning: it bubbles (app chords
   * like ctrl+enter belong to translators) and inserts nothing. */
  CHECK(send_key(&f, LKK_RETURN, LK_MOD_CTRL) == 0);
  CHECK(send_key(&f, LKK_RETURN, LK_MOD_ALT) == 0);
  CHECK(doc_is(f.doc, "\n"));

  CHECK(send_key(&f, LKK_TAB, 0) == 1);
  CHECK(doc_is(f.doc, "\n    ")); /* tab_size = 4 spaces */

  CHECK(send_key(&f, LKK_TAB, LK_MOD_SHIFT) == 0); /* focus path */
  CHECK(send_key(&f, LKK_ESCAPE, 0) == 0);
  CHECK(send_key(&f, LKK_F1, 0) == 0); /* unlisted bubbles */
  CHECK(doc_is(f.doc, "\n    "));

  fix_destroy(&f);
  END_TEST();
}

static void test_event_arrows_modifiers(void) {
  ed_fix f;
  lk_u32 s;
  lk_u32 e;

  BEGIN_TEST("ed event: arrows with CTRL/SHIFT combos");

  fix_init(&f, "foo bar", 400, 80);

  CHECK(send_key(&f, LKK_END, LK_MOD_CTRL) == 1); /* doc end */
  CHECK_EQ(lk_editor_cursor(f.ed), 7);

  CHECK(send_key(&f, LKK_LEFT, 0) == 1);
  CHECK_EQ(lk_editor_cursor(f.ed), 6);

  CHECK(send_key(&f, LKK_LEFT, LK_MOD_CTRL) == 1); /* word left */
  CHECK_EQ(lk_editor_cursor(f.ed), 4);

  CHECK(send_key(&f, LKK_LEFT, LK_MOD_SHIFT) == 1); /* extend */
  CHECK(lk_editor_selection(f.ed, &s, &e) == 1);
  CHECK_EQ(s, 3);
  CHECK_EQ(e, 4);

  CHECK(send_key(&f, LKK_HOME, 0) == 1); /* line start, drop sel */
  CHECK_EQ(lk_editor_cursor(f.ed), 0);
  CHECK(lk_editor_selection(f.ed, &s, &e) == 0);

  /* SHIFT+CTRL+RIGHT: word-right extension */
  CHECK(send_key(&f, LKK_RIGHT, LK_MOD_CTRL | LK_MOD_SHIFT) == 1);
  CHECK(lk_editor_selection(f.ed, &s, &e) == 1);
  CHECK_EQ(s, 0);
  CHECK_EQ(e, 4);

  fix_destroy(&f);
  END_TEST();
}

static void test_event_page_keys(void) {
  ed_fix f;
  char buf[2048];

  BEGIN_TEST("ed event: pageup/pagedown use viewport size");

  make_lines(buf, sizeof(buf), 100);
  fix_init(&f, buf, 400, 80); /* 5 lines per page */

  CHECK(send_key(&f, LKK_PAGEDOWN, 0) == 1);
  CHECK_EQ(lk_doc_pos_to_line(f.doc, lk_editor_cursor(f.ed)), 5);
  CHECK(send_key(&f, LKK_PAGEDOWN, 0) == 1);
  CHECK_EQ(lk_doc_pos_to_line(f.doc, lk_editor_cursor(f.ed)), 10);
  CHECK(send_key(&f, LKK_PAGEUP, 0) == 1);
  CHECK_EQ(lk_doc_pos_to_line(f.doc, lk_editor_cursor(f.ed)), 5);

  fix_destroy(&f);
  END_TEST();
}

static void test_event_clipboard_keys(void) {
  ed_fix f;
  lk_u32 s;
  lk_u32 e;

  BEGIN_TEST("ed event: CTRL+A/C/X/V");

  fix_init(&f, "hello", 400, 80);
  lk_ui_set_clipboard(f.ui, clip_get, clip_set, NULL);
  g_clip[0] = '\0';

  CHECK(send_key(&f, LKK_A, LK_MOD_CTRL) == 1);
  CHECK(lk_editor_selection(f.ed, &s, &e) == 1);
  CHECK_EQ(s, 0);
  CHECK_EQ(e, 5);

  CHECK(send_key(&f, LKK_C, LK_MOD_CTRL) == 1);
  CHECK(strcmp(g_clip, "hello") == 0);

  CHECK(send_key(&f, LKK_X, LK_MOD_CTRL) == 1);
  CHECK(doc_is(f.doc, ""));

  CHECK(send_key(&f, LKK_V, LK_MOD_CTRL) == 1);
  CHECK(doc_is(f.doc, "hello"));

  /* plain letters bubble as KEY_DOWN (text arrives via LK_EVENT_TEXT) */
  CHECK(send_key(&f, LKK_A, 0) == 0);

  fix_destroy(&f);
  END_TEST();
}

static void test_event_undo_redo_keys(void) {
  ed_fix f;

  BEGIN_TEST("ed event: CTRL+Z / CTRL+SHIFT+Z");

  fix_init(&f, "", 400, 80);

  send_text(&f, "a");
  send_text(&f, "b");

  CHECK(doc_is(f.doc, "ab"));

  /* one TEXT event = one transaction = one undo step */
  CHECK(send_key(&f, LKK_Z, LK_MOD_CTRL) == 1);
  CHECK(doc_is(f.doc, "a"));
  CHECK(send_key(&f, LKK_Z, LK_MOD_CTRL) == 1);
  CHECK(doc_is(f.doc, ""));
  CHECK(send_key(&f, LKK_Z, LK_MOD_CTRL | LK_MOD_SHIFT) == 1);
  CHECK(doc_is(f.doc, "a"));
  CHECK_EQ(lk_editor_cursor(f.ed), 1);

  fix_destroy(&f);
  END_TEST();
}

/* ================================================================
 * (c) pointer
 * ================================================================ */

static void test_pointer_click_position(void) {
  ed_fix f;

  BEGIN_TEST("ed pointer: click positions, focuses, captures");

  fix_init(&f, "hello\nworld", 400, 80);

  /* x=20 on line 0: nearest boundary = (20+4)/8 = 3 */
  CHECK(send_pointer(&f, LK_EVENT_POINTER_DOWN, 20, 8) == 1);
  CHECK_EQ(lk_editor_cursor(f.ed), 3);
  CHECK_EQ(f.ui->focused_id, f.nid);
  CHECK_EQ(lk_capture_current(f.ui), f.nid);
  CHECK(lk_editor_dragging(f.ed) == 1);

  CHECK(send_pointer(&f, LK_EVENT_POINTER_UP, 20, 8) == 1);
  CHECK_EQ(lk_capture_current(f.ui), 0);
  CHECK(lk_editor_dragging(f.ed) == 0);

  /* second line: y = 20 -> line 1; x = 0 -> byte 6 */
  CHECK(send_pointer(&f, LK_EVENT_POINTER_DOWN, 0, 20) == 1);
  CHECK_EQ(lk_editor_cursor(f.ed), 6);
  send_pointer(&f, LK_EVENT_POINTER_UP, 0, 20);

  /* click below the last line clamps to it; far x clamps to its end */
  CHECK(send_pointer(&f, LK_EVENT_POINTER_DOWN, 500, 70) == 1);
  CHECK_EQ(lk_editor_cursor(f.ed), 11);
  send_pointer(&f, LK_EVENT_POINTER_UP, 500, 70);

  fix_destroy(&f);
  END_TEST();
}

static void test_pointer_click_tab_segments(void) {
  ed_fix f;

  BEGIN_TEST("ed pointer: click through tab-stop segments");

  /* "ab\tcd": seg "ab" [0,16), tab span [16,32), seg "cd" [32,48) */
  fix_init(&f, "ab\tcd", 400, 80);

  /* inside the tab span, nearer the left edge -> before the tab */
  CHECK(send_pointer(&f, LK_EVENT_POINTER_DOWN, 17, 8) == 1);
  CHECK_EQ(lk_editor_cursor(f.ed), 2);
  send_pointer(&f, LK_EVENT_POINTER_UP, 17, 8);

  /* nearer the right edge -> after the tab */
  send_pointer(&f, LK_EVENT_POINTER_DOWN, 30, 8);
  CHECK_EQ(lk_editor_cursor(f.ed), 3);
  send_pointer(&f, LK_EVENT_POINTER_UP, 30, 8);

  /* in the second segment: x=33 -> boundary 0 of "cd" -> byte 3;
   * x=37 -> boundary 1 -> byte 4 */
  send_pointer(&f, LK_EVENT_POINTER_DOWN, 33, 8);
  CHECK_EQ(lk_editor_cursor(f.ed), 3);
  send_pointer(&f, LK_EVENT_POINTER_UP, 33, 8);
  send_pointer(&f, LK_EVENT_POINTER_DOWN, 37, 8);
  CHECK_EQ(lk_editor_cursor(f.ed), 4);
  send_pointer(&f, LK_EVENT_POINTER_UP, 37, 8);

  fix_destroy(&f);
  END_TEST();
}

static void test_pointer_drag_selection(void) {
  ed_fix f;
  lk_u32 s;
  lk_u32 e;

  BEGIN_TEST("ed pointer: drag extends, release clears capture");

  fix_init(&f, "hello", 400, 80);

  /* move without drag bubbles */
  CHECK(send_pointer(&f, LK_EVENT_POINTER_MOVE, 30, 8) == 0);

  CHECK(send_pointer(&f, LK_EVENT_POINTER_DOWN, 8, 8) == 1);
  CHECK_EQ(lk_editor_cursor(f.ed), 1);

  CHECK(send_pointer(&f, LK_EVENT_POINTER_MOVE, 33, 8) == 1);
  CHECK(lk_editor_selection(f.ed, &s, &e) == 1);
  CHECK_EQ(s, 1);
  CHECK_EQ(e, 4);
  CHECK_EQ(lk_editor_cursor(f.ed), 4);
  CHECK_EQ(lk_capture_current(f.ui), f.nid);

  CHECK(send_pointer(&f, LK_EVENT_POINTER_UP, 33, 8) == 1);
  CHECK_EQ(lk_capture_current(f.ui), 0);
  CHECK(lk_editor_dragging(f.ed) == 0);

  /* selection survives the release */
  CHECK(lk_editor_selection(f.ed, &s, &e) == 1);
  CHECK_EQ(s, 1);
  CHECK_EQ(e, 4);

  fix_destroy(&f);
  END_TEST();
}

/* ================================================================
 * (d) anchored viewport + virtualization
 * ================================================================ */

static void test_viewport_virtualization(void) {
  ed_fix f;
  char buf[2048];
  lk_render_list rl;

  BEGIN_TEST("ed viewport: only visible lines emit runs");

  make_lines(buf, sizeof(buf), 100);
  fix_init(&f, buf, 400, 80); /* 5 of 100 lines visible */
  memset(&rl, 0, sizeof(rl));

  CHECK(fix_render(&f, &rl));
  CHECK_EQ(count_runs(&rl), 5);
  CHECK(run_is(&rl, nth_run(&rl, 0), "line 0"));
  CHECK(run_is(&rl, nth_run(&rl, 4), "line 4"));

  /* exact stub geometry of the first run */
  CHECK_EQ((unsigned)nth_run(&rl, 0)->rect.x, 0u);
  CHECK_EQ((unsigned)nth_run(&rl, 0)->rect.y, 0u);
  CHECK_EQ((unsigned)nth_run(&rl, 0)->rect.w, 48u); /* 6 cp * 8 */
  CHECK_EQ((unsigned)nth_run(&rl, 4)->rect.y, 64u);

  lk_render_list_destroy(&rl);
  fix_destroy(&f);
  END_TEST();
}

static void test_viewport_wheel_scrolls(void) {
  ed_fix f;
  char buf[2048];
  lk_render_list rl;

  BEGIN_TEST("ed viewport: wheel scrolls by lines");

  make_lines(buf, sizeof(buf), 100);
  fix_init(&f, buf, 400, 80);
  memset(&rl, 0, sizeof(rl));

  /* wheel toward the user (dy = -1) scrolls down 3 lines, always
   * consumed (the editor owns its viewport) */
  CHECK(send_wheel(&f, -1) == 1);
  CHECK_EQ(vp_top_line(&f), 3);

  fix_layout(&f);

  CHECK(fix_render(&f, &rl));
  CHECK_EQ(count_runs(&rl), 5);
  CHECK(run_is(&rl, nth_run(&rl, 0), "line 3"));
  CHECK(run_is(&rl, nth_run(&rl, 4), "line 7"));

  /* wheel away (dy = +1) scrolls back up */
  CHECK(send_wheel(&f, 1) == 1);
  CHECK_EQ(vp_top_line(&f), 0);

  lk_render_list_destroy(&rl);
  fix_destroy(&f);
  END_TEST();
}

static void test_viewport_scroll_clamps(void) {
  ed_fix f;
  char buf[2048];
  lk_render_list rl;

  BEGIN_TEST("ed viewport: scroll clamps at both ends");

  make_lines(buf, sizeof(buf), 100);
  fix_init(&f, buf, 400, 80);
  memset(&rl, 0, sizeof(rl));

  /* top clamp */
  CHECK(cmd_scroll(&f, -1000) == 0); /* already at (0,0) */
  fix_layout(&f);
  CHECK_EQ(vp_top_line(&f), 0);

  /* bottom clamp: 100 lines, 5 fit exactly -> max top = 95 */
  cmd_scroll(&f, 100000);
  fix_layout(&f);
  CHECK_EQ(vp_top_line(&f), 95);
  CHECK_EQ((unsigned)lk_editor_get_viewport(f.ed).y_offset, 0u);

  CHECK(fix_render(&f, &rl));
  CHECK_EQ(count_runs(&rl), 5);
  CHECK(run_is(&rl, nth_run(&rl, 0), "line 95"));
  CHECK(run_is(&rl, nth_run(&rl, 4), "line 99"));

  /* wheel further down stays clamped */
  send_wheel(&f, -1);
  fix_layout(&f);
  CHECK_EQ(vp_top_line(&f), 95);

  lk_render_list_destroy(&rl);
  fix_destroy(&f);
  END_TEST();
}

static void test_viewport_scroll_to_cursor(void) {
  ed_fix f;
  char buf[2048];
  lk_render_list rl;

  BEGIN_TEST("ed viewport: scroll-to-cursor after doc end/start");

  make_lines(buf, sizeof(buf), 100);
  fix_init(&f, buf, 400, 80);
  memset(&rl, 0, sizeof(rl));

  CHECK(cmd_move(&f, LK_ED_MOVE_DOC_END, 0) == 1);
  fix_layout(&f); /* pending scroll resolves here */
  CHECK_EQ(vp_top_line(&f), 95);

  CHECK(fix_render(&f, &rl));
  CHECK(run_is(&rl, nth_run(&rl, 4), "line 99"));

  CHECK(cmd_move(&f, LK_ED_MOVE_DOC_START, 0) == 1);
  fix_layout(&f);
  CHECK_EQ(vp_top_line(&f), 0);

  lk_render_list_destroy(&rl);
  fix_destroy(&f);
  END_TEST();
}

static void test_viewport_partial_line_offset(void) {
  ed_fix f;
  lk_render_list rl;
  lk_editor_viewport vp;

  BEGIN_TEST("ed viewport: y_offset for non-multiple heights");

  /* 4 lines, viewport 40 px = 2.5 lines: q=2, r=8 ->
   * max scroll = (top 1, offset 8) */
  fix_init(&f, "aa\nbb\ncc\ndd", 400, 40);
  memset(&rl, 0, sizeof(rl));

  cmd_scroll(&f, 1000);
  fix_layout(&f);
  vp = lk_editor_get_viewport(f.ed);

  CHECK_EQ(lk_doc_pos_to_line(f.doc, vp.top_byte), 1);
  CHECK_EQ((unsigned)vp.y_offset, 8u);

  CHECK(fix_render(&f, &rl));
  CHECK_EQ(count_runs(&rl), 3); /* lines 1..3 intersect */
  CHECK(run_is(&rl, nth_run(&rl, 0), "bb"));
  CHECK(nth_run(&rl, 0)->rect.y == -8); /* partially above */
  CHECK(nth_run(&rl, 2)->rect.y == 24);

  /* scroll-to-cursor lands on the same anchored maximum */
  cmd_scroll(&f, -1000);
  cmd_move(&f, LK_ED_MOVE_DOC_END, 0);
  fix_layout(&f);
  vp = lk_editor_get_viewport(f.ed);
  CHECK_EQ(lk_doc_pos_to_line(f.doc, vp.top_byte), 1);
  CHECK_EQ((unsigned)vp.y_offset, 8u);

  lk_render_list_destroy(&rl);
  fix_destroy(&f);
  END_TEST();
}

/* ================================================================
 * (e) render details
 * ================================================================ */

static void test_render_cursor_focus_only(void) {
  ed_fix f;
  lk_render_list rl;
  const lk_render_cmd *cur;

  BEGIN_TEST("ed render: cursor bar only when focused");

  fix_init(&f, "hello", 400, 80);
  memset(&rl, 0, sizeof(rl));

  cmd_setcur(&f, 3, 0);
  fix_layout(&f);

  CHECK(fix_render(&f, &rl));
  CHECK(find_cursor_fill(&rl) == NULL); /* not focused */

  CHECK(lk_focus_set(f.ui, lk_ui_tree(f.ui), f.nid) == 1);
  CHECK(fix_render(&f, &rl));
  cur = find_cursor_fill(&rl);
  CHECK(cur != NULL);

  if (cur) {
    CHECK_EQ((unsigned)cur->rect.x, 24u); /* 3 cp * 8 */
    CHECK_EQ((unsigned)cur->rect.y, 0u);
    CHECK_EQ((unsigned)cur->rect.h, 16u);
  }

  lk_focus_clear(f.ui);

  CHECK(fix_render(&f, &rl));
  CHECK(find_cursor_fill(&rl) == NULL);

  lk_render_list_destroy(&rl);
  fix_destroy(&f);
  END_TEST();
}

static void test_render_selection_rects(void) {
  ed_fix f;
  lk_render_list rl;
  const lk_render_cmd *r0;
  const lk_render_cmd *r1;
  const lk_render_cmd *r2;

  BEGIN_TEST("ed render: selection head/body/tail rects");

  /* 4 lines of 4 chars: "abcd\nefgh\nijkl\nmnop" */
  fix_init(&f, "abcd\nefgh\nijkl\nmnop", 400, 80);
  memset(&rl, 0, sizeof(rl));

  /* 1-line selection [1,3) on line 0 */
  cmd_setcur(&f, 1, 0);
  cmd_setcur(&f, 3, 1);
  fix_layout(&f);
  CHECK(fix_render(&f, &rl));
  CHECK_EQ(count_sel_fills(&rl), 1);
  r0 = nth_sel_fill(&rl, 0);
  CHECK(r0 && r0->rect.x == 8 && r0->rect.y == 0 && r0->rect.w == 16 &&
        r0->rect.h == 16);

  /* 2-line selection [2,7): head + tail, no body */
  cmd_setcur(&f, 2, 0);
  cmd_setcur(&f, 7, 1);
  fix_layout(&f);
  CHECK(fix_render(&f, &rl));
  CHECK_EQ(count_sel_fills(&rl), 2);
  r0 = nth_sel_fill(&rl, 0);
  r1 = nth_sel_fill(&rl, 1);
  CHECK(r0 && r0->rect.x == 16 && r0->rect.y == 0 && r0->rect.w == 16);
  CHECK(r1 && r1->rect.x == 0 && r1->rect.y == 16 && r1->rect.w == 16);

  /* 3-line selection [2,12): head + full-width body + tail */
  cmd_setcur(&f, 2, 0);
  cmd_setcur(&f, 12, 1);
  fix_layout(&f);
  CHECK(fix_render(&f, &rl));
  CHECK_EQ(count_sel_fills(&rl), 3);
  r0 = nth_sel_fill(&rl, 0);
  r1 = nth_sel_fill(&rl, 1);
  r2 = nth_sel_fill(&rl, 2);
  CHECK(r0 && r0->rect.x == 16 && r0->rect.y == 0 && r0->rect.w == 16);
  CHECK(r1 && r1->rect.x == 0 && r1->rect.y == 16 && r1->rect.w == 400 &&
        r1->rect.h == 16);
  CHECK(r2 && r2->rect.x == 0 && r2->rect.y == 32 && r2->rect.w == 16);

  lk_render_list_destroy(&rl);
  fix_destroy(&f);
  END_TEST();
}

static void test_render_tab_expansion(void) {
  ed_fix f;
  lk_render_list rl;
  const lk_render_cmd *r0;
  const lk_render_cmd *r1;
  const lk_render_cmd *r2;

  BEGIN_TEST("ed render: tab stops at 32 px (4 * space)");

  fix_init(&f, "ab\tcd\n\tz", 400, 80);
  memset(&rl, 0, sizeof(rl));

  CHECK(fix_render(&f, &rl));
  CHECK_EQ(count_runs(&rl), 3);

  r0 = nth_run(&rl, 0);
  r1 = nth_run(&rl, 1);
  r2 = nth_run(&rl, 2);

  CHECK(run_is(&rl, r0, "ab"));
  CHECK(r0 && r0->rect.x == 0 && r0->rect.y == 0 && r0->rect.w == 16);

  CHECK(run_is(&rl, r1, "cd"));
  CHECK(r1 && r1->rect.x == 32 && r1->rect.y == 0 && r1->rect.w == 16);

  /* leading tab on line 1: run starts at the first stop */
  CHECK(run_is(&rl, r2, "z"));
  CHECK(r2 && r2->rect.x == 32 && r2->rect.y == 16 && r2->rect.w == 8);

  /* cursor after the tab sits on the stop */
  cmd_setcur(&f, 3, 0); /* after \t on line 0 */
  lk_focus_set(f.ui, lk_ui_tree(f.ui), f.nid);
  fix_layout(&f);
  CHECK(fix_render(&f, &rl));

  {
    const lk_render_cmd *cur = find_cursor_fill(&rl);

    CHECK(cur != NULL);

    if (cur) {
      CHECK_EQ((unsigned)cur->rect.x, 32u);
    }
  }

  lk_render_list_destroy(&rl);
  fix_destroy(&f);
  END_TEST();
}

/* ================================================================
 * (e2) overlay scrollbar (polish F2)
 * ================================================================ */

#define ED_TEST_BAR_W 6

/* nth FILL_RECT sitting in the scrollbar column (x == bar_x, w ==
 * bar width): 0 = track, 1 = thumb.  NULL when absent. */
static const lk_render_cmd *nth_bar_fill(const lk_render_list *rl,
                                         lk_i32 bar_x, lk_u32 idx) {
  lk_u32 i, seen = 0;

  for (i = 0; i < rl->count; i++) {
    const lk_render_cmd *c = &rl->cmds[i];

    if (c->op == LK_ROP_FILL_RECT && c->rect.x == bar_x &&
        c->rect.w == ED_TEST_BAR_W) {
      if (seen == idx) {
        return c;
      }

      seen++;
    }
  }

  return NULL;
}

/* Build "ab\n" * n_lines (each line 2 chars + newline). */
static void many_lines(char *buf, lk_u32 cap, lk_u32 n_lines) {
  lk_u32 i, o = 0;

  for (i = 0; i < n_lines && o + 4 < cap; i++) {
    buf[o++] = 'a';
    buf[o++] = 'b';
    buf[o++] = '\n';
  }

  buf[o] = '\0';
}

static void test_scrollbar_exact_geometry(void) {
  ed_fix f;
  lk_render_list rl;
  char text[256];
  const lk_render_cmd *track;
  const lk_render_cmd *thumb;

  BEGIN_TEST("ed scrollbar: track+thumb exact stub geometry");

  /* 30 lines (the last many_lines newline yields an empty 31st line;
   * ed line count = 31... keep the arithmetic explicit below), view
   * 400x160: rows 31, line_h 16 -> total 496 px > 160. */
  many_lines(text, sizeof(text), 30);
  fix_init(&f, text, 400, 160);
  memset(&rl, 0, sizeof(rl));

  CHECK(fix_render(&f, &rl));
  track = nth_bar_fill(&rl, 400 - ED_TEST_BAR_W, 0);
  thumb = nth_bar_fill(&rl, 400 - ED_TEST_BAR_W, 1);
  CHECK(track != NULL);
  CHECK(thumb != NULL);

  if (track && thumb) {
    /* Track spans the content rect height at the right edge. */
    CHECK_EQ((unsigned)track->rect.y, 0u);
    CHECK_EQ((unsigned)track->rect.h, 160u);

    /* 31 rows * 16 = 496 total; thumb = 160*160/496 = 51 (min 8). */
    CHECK_EQ((unsigned)thumb->rect.h, 51u);
    CHECK_EQ((unsigned)thumb->rect.y, 0u); /* anchored at doc top */
  }

  /* Scroll 6 lines down: top_px = 96, max_top = 496-160 = 336,
   * thumb_y = 96 * (160-51) / 336 = 31. */
  CHECK(cmd_scroll(&f, 6));
  fix_layout(&f);
  CHECK(fix_render(&f, &rl));
  thumb = nth_bar_fill(&rl, 400 - ED_TEST_BAR_W, 1);
  CHECK(thumb != NULL);

  if (thumb) {
    CHECK_EQ((unsigned)thumb->rect.y, 31u);
    CHECK_EQ((unsigned)thumb->rect.h, 51u);
  }

  lk_render_list_destroy(&rl);
  fix_destroy(&f);
  END_TEST();
}

static void test_scrollbar_absent_when_fits(void) {
  ed_fix f;
  lk_render_list rl;

  BEGIN_TEST("ed scrollbar: absent when content fits");

  /* 3 lines in a 160 px viewport: 48 px of rows, no bar. */
  fix_init(&f, "ab\ncd\nef", 400, 160);
  memset(&rl, 0, sizeof(rl));

  CHECK(fix_render(&f, &rl));
  CHECK(nth_bar_fill(&rl, 400 - ED_TEST_BAR_W, 0) == NULL);

  lk_render_list_destroy(&rl);
  fix_destroy(&f);
  END_TEST();
}

static void test_scrollbar_tiny_viewport(void) {
  ed_fix f;
  lk_render_list rl;
  char text[256];
  const lk_render_cmd *track;
  const lk_render_cmd *thumb;

  BEGIN_TEST("ed scrollbar: tiny viewport, no negative rects");

  /* Viewport shorter than the 8 px minimum thumb: the thumb clamps
   * to the track, nothing goes negative. */
  many_lines(text, sizeof(text), 30);
  fix_init(&f, text, 400, 4);
  memset(&rl, 0, sizeof(rl));

  CHECK(fix_render(&f, &rl));
  track = nth_bar_fill(&rl, 400 - ED_TEST_BAR_W, 0);
  thumb = nth_bar_fill(&rl, 400 - ED_TEST_BAR_W, 1);
  CHECK(track != NULL);
  CHECK(thumb != NULL);

  if (track && thumb) {
    CHECK_EQ((unsigned)track->rect.h, 4u);
    CHECK_EQ((unsigned)thumb->rect.h, 4u); /* min 8 clamped to track */
    CHECK_EQ((unsigned)thumb->rect.y, 0u);
    CHECK(track->rect.h >= 0 && thumb->rect.h >= 0);
    CHECK(track->rect.w >= 0 && thumb->rect.w >= 0);
  }

  lk_render_list_destroy(&rl);
  fix_destroy(&f);
  END_TEST();
}

static void test_scrollbar_wrapped_extent(void) {
  ed_fix f;
  lk_render_list rl;
  char text[512];
  lk_u32 i, o = 0;
  const lk_render_cmd *track;
  const lk_render_cmd *thumb;

  BEGIN_TEST("ed scrollbar: wrapped rows + estimator extent");

  /* 8 lines of 30 chars in a 160 px wrap width: 20 chars per row ->
   * 2 rows per line, exact for measured lines, estimator (seeded by
   * the measured average of 8 px/byte) for the rest: 16 rows total,
   * 256 px in a 64 px viewport. */
  for (i = 0; i < 8; i++) {
    lk_u32 k;

    for (k = 0; k < 30; k++) {
      text[o++] = (char)('a' + (char)(i % 26));
    }

    if (i + 1 < 8) {
      text[o++] = '\n';
    }
  }

  text[o] = '\0';
  fix_init(&f, text, 160, 64);
  fix_wrap(&f);
  memset(&rl, 0, sizeof(rl));

  CHECK(fix_render(&f, &rl));
  track = nth_bar_fill(&rl, 160 - ED_TEST_BAR_W, 0);
  thumb = nth_bar_fill(&rl, 160 - ED_TEST_BAR_W, 1);
  CHECK(track != NULL);
  CHECK(thumb != NULL);

  if (track && thumb) {
    CHECK_EQ((unsigned)track->rect.h, 64u);
    /* thumb = 64*64/256 = 16 */
    CHECK_EQ((unsigned)thumb->rect.h, 16u);
    CHECK_EQ((unsigned)thumb->rect.y, 0u);
  }

  lk_render_list_destroy(&rl);
  fix_destroy(&f);
  END_TEST();
}

/* ================================================================
 * (f) degradation
 * ================================================================ */

static void test_degrade_missing_prop(void) {
  ed_fix f;
  lk_render_list rl;

  BEGIN_TEST("ed degrade: no UIP_EDITOR -> bg only, events bubble");

  fix_init_ex(&f, "hello", 400, 80, 0);
  memset(&rl, 0, sizeof(rl));

  CHECK(fix_render(&f, &rl));
  CHECK_EQ(count_runs(&rl), 0);

  CHECK(send_text(&f, "x") == 0);
  CHECK(send_key(&f, LKK_LEFT, 0) == 0);
  CHECK(send_pointer(&f, LK_EVENT_POINTER_DOWN, 10, 8) == 0);
  CHECK(send_wheel(&f, -1) == 0);
  CHECK(doc_is(f.doc, "hello")); /* untouched */

  lk_render_list_destroy(&rl);
  fix_destroy(&f);
  END_TEST();
}

static void test_degrade_stale_ref(void) {
  ed_fix f;
  lk_render_list rl;

  BEGIN_TEST("ed degrade: stale/wrong-typed ref -> bg only");

  fix_init(&f, "hello", 400, 80);
  memset(&rl, 0, sizeof(rl));

  /* live ref resolves */
  CHECK(lk_editor_from_node(lk_ui_resources(f.ui), lk_ui_tree(f.ui),
                            f.node) == f.ed);

  CHECK(fix_render(&f, &rl));
  CHECK_EQ(count_runs(&rl), 1);

  /* release the resource: the tree still carries the (now stale) ref */
  lk_resource_release(lk_ui_resources(f.ui), f.ref);

  CHECK(lk_editor_from_node(lk_ui_resources(f.ui), lk_ui_tree(f.ui),
                            f.node) == NULL);

  fix_layout(&f);
  CHECK(fix_render(&f, &rl));
  CHECK_EQ(count_runs(&rl), 0);

  CHECK(send_text(&f, "x") == 0);
  CHECK(send_pointer(&f, LK_EVENT_POINTER_DOWN, 10, 8) == 0);
  CHECK(doc_is(f.doc, "hello"));

  /* wrong-typed registration also fails to resolve */
  {
    static const lk_resource_type other_type = {"other", NULL};
    lk_resource_ref oref = lk_resource_register(
        lk_ui_resources(f.ui), &other_type, f.ed, "not-an-editor");
    lk_tree *t = lk_ui_begin_frame(f.ui);
    lk_ix w = lk_tree_add_node_c(t, "w", UIK_WINDOW);
    lk_ix ed = lk_tree_add_node_c(t, "ed", UIK_EDITOR);

    lk_tree_set_root(t, w);
    lk_tree_append_child(t, w, ed);
    lk_tree_add_prop(t, ed, UIP_EDITOR, lk_v_resource(oref));
    lk_ui_end_frame(f.ui);

    CHECK(lk_editor_from_node(lk_ui_resources(f.ui), lk_ui_tree(f.ui),
                              lk_tree_find_by_id(lk_ui_tree(f.ui), f.nid)) ==
          NULL);
  }

  lk_render_list_destroy(&rl);
  fix_destroy(&f);
  END_TEST();
}

/* ================================================================
 * (g) UTF-8 integrity under editing
 * ================================================================ */

static void test_utf8_typing_backspace(void) {
  ed_fix f;

  BEGIN_TEST("ed utf8: typing + backspace keep boundaries");

  fix_init(&f, "", 400, 80);

  CHECK(send_text(&f, "\xC3\xA9") == 1);       /* e-acute */
  CHECK(send_text(&f, "\xE6\x97\xA5") == 1);   /* CJK sun */
  CHECK_EQ(lk_doc_len(f.doc), 5);
  CHECK_EQ(lk_editor_cursor(f.ed), 5);

  CHECK(send_key(&f, LKK_LEFT, 0) == 1);
  CHECK_EQ(lk_editor_cursor(f.ed), 2); /* whole codepoint */
  CHECK(send_key(&f, LKK_LEFT, 0) == 1);
  CHECK_EQ(lk_editor_cursor(f.ed), 0);
  CHECK(send_key(&f, LKK_END, 0) == 1);
  CHECK_EQ(lk_editor_cursor(f.ed), 5);

  CHECK(send_key(&f, LKK_BACKSPACE, 0) == 1);
  CHECK(doc_is(f.doc, "\xC3\xA9"));
  CHECK_EQ(lk_editor_cursor(f.ed), 2);
  CHECK(send_key(&f, LKK_BACKSPACE, 0) == 1);
  CHECK(doc_is(f.doc, ""));
  CHECK_EQ(lk_editor_cursor(f.ed), 0);

  fix_destroy(&f);
  END_TEST();
}

/* ================================================================
 * (h) wrap engine (docs/editor-wrap.md, stage W1).  Stub geometry:
 * 8 px/codepoint, line height 16 -> width 80 = 10 codepoints/row.
 * ================================================================ */

/* one 20-codepoint line: rows [0,10) and [10,20] at width 80 */
#define ED20 "abcdefghijklmnopqrst"

/* Build nlines lines of ncols copies of per-line letters. */
static void make_rows(char *buf, lk_u32 cap, int nlines, int ncols) {
  int i;
  int j;
  lk_u32 off = 0;

  for (i = 0; i < nlines && off + (lk_u32)ncols + 2 < cap; i++) {
    for (j = 0; j < ncols; j++) {
      buf[off++] = (char)('a' + i % 26);
    }

    if (i + 1 < nlines) {
      buf[off++] = '\n';
    }
  }

  buf[off] = '\0';
}

static void test_wrap_mode_api(void) {
  ed_fix f;

  BEGIN_TEST("wrap: mode API, all three modes round-trip");

  fix_init(&f, "hello", 400, 80);

  CHECK(lk_editor_wrap_mode_get(f.ed) == LK_EDITOR_WRAP_NONE);
  CHECK(lk_editor_set_wrap_mode(f.ed, (lk_editor_wrap_mode)99) == 0);
  CHECK(lk_editor_wrap_mode_get(f.ed) == LK_EDITOR_WRAP_NONE);
  CHECK(lk_editor_set_wrap_mode(f.ed, LK_EDITOR_WRAP_WORD) == 1);
  CHECK(lk_editor_wrap_mode_get(f.ed) == LK_EDITOR_WRAP_WORD);
  CHECK(lk_editor_set_wrap_mode(f.ed, LK_EDITOR_WRAP_CHARACTER) == 1);
  CHECK(lk_editor_wrap_mode_get(f.ed) == LK_EDITOR_WRAP_CHARACTER);
  CHECK(lk_editor_set_wrap_mode(f.ed, LK_EDITOR_WRAP_CHARACTER) == 1);
  CHECK(lk_editor_set_wrap_mode(f.ed, LK_EDITOR_WRAP_NONE) == 1);
  CHECK(lk_editor_wrap_mode_get(f.ed) == LK_EDITOR_WRAP_NONE);

  fix_destroy(&f);
  END_TEST();
}

static void test_wrap_basic_rows(void) {
  ed_fix f;
  lk_render_list rl;

  BEGIN_TEST("wrap: 20 cp line breaks into two 10 cp rows");

  fix_init(&f, ED20, 80, 80);
  memset(&rl, 0, sizeof(rl));

  /* NONE mode first: one run, full 160 px, no wrap (degenerate) */
  CHECK(fix_render(&f, &rl));
  CHECK_EQ(count_runs(&rl), 1);
  CHECK(run_is(&rl, nth_run(&rl, 0), ED20));
  CHECK_EQ((unsigned)nth_run(&rl, 0)->rect.w, 160u);
  CHECK_EQ(lk_editor_wrap_rows(f.ed, 0), 0); /* no cache in NONE */

  fix_wrap(&f);

  CHECK_EQ(lk_editor_wrap_rows(f.ed, 0), 2);
  CHECK(fix_render(&f, &rl));
  CHECK_EQ(count_runs(&rl), 2);
  CHECK(run_is(&rl, nth_run(&rl, 0), "abcdefghij"));
  CHECK(run_is(&rl, nth_run(&rl, 1), "klmnopqrst"));
  CHECK(nth_run(&rl, 0)->rect.x == 0 && nth_run(&rl, 0)->rect.y == 0);
  CHECK_EQ((unsigned)nth_run(&rl, 0)->rect.w, 80u);
  CHECK(nth_run(&rl, 1)->rect.x == 0 && nth_run(&rl, 1)->rect.y == 16);

  /* back to NONE: the single-run shape returns */
  CHECK(lk_editor_set_wrap_mode(f.ed, LK_EDITOR_WRAP_NONE) == 1);
  fix_layout(&f);
  CHECK(fix_render(&f, &rl));
  CHECK_EQ(count_runs(&rl), 1);
  CHECK(run_is(&rl, nth_run(&rl, 0), ED20));

  lk_render_list_destroy(&rl);
  fix_destroy(&f);
  END_TEST();
}

static void test_wrap_break_utf8(void) {
  ed_fix f;
  lk_render_list rl;
  const lk_render_cmd *cur;
  char text[32];
  int i;

  BEGIN_TEST("wrap: multi-byte codepoints at the break");

  /* 12 x e-acute (2 bytes each, 24 bytes, 12 cp): break at byte 20 */
  for (i = 0; i < 12; i++) {
    text[i * 2] = '\xC3';
    text[i * 2 + 1] = '\xA9';
  }

  text[24] = '\0';
  fix_init(&f, text, 80, 80);
  fix_wrap(&f);
  memset(&rl, 0, sizeof(rl));

  CHECK_EQ(lk_editor_wrap_rows(f.ed, 0), 2);
  CHECK(fix_render(&f, &rl));
  CHECK_EQ(count_runs(&rl), 2);
  CHECK_EQ(nth_run(&rl, 0)->run_len, 20); /* 10 whole codepoints */
  CHECK_EQ(nth_run(&rl, 1)->run_len, 4);
  CHECK_EQ((unsigned)nth_run(&rl, 0)->rect.w, 80u);
  CHECK_EQ((unsigned)nth_run(&rl, 1)->rect.w, 16u);

  /* cursor at the break byte renders at the NEXT row's start */
  cmd_setcur(&f, 20, 0);
  lk_focus_set(f.ui, lk_ui_tree(f.ui), f.nid);
  fix_layout(&f);
  CHECK(fix_render(&f, &rl));
  cur = find_cursor_fill(&rl);
  CHECK(cur && cur->rect.x == 0 && cur->rect.y == 16);

  lk_render_list_destroy(&rl);
  fix_destroy(&f);
  END_TEST();
}

static void test_wrap_tab_at_edge(void) {
  ed_fix f;
  lk_render_list rl;

  BEGIN_TEST("wrap: tab that would pass the width breaks first");

  /* "abcdefghi\tx": 9 cp end at x=72; next tab stop 96 > 80 -> the
   * tab starts row 1 and advances to 32 there (row-relative stops) */
  fix_init(&f, "abcdefghi\tx", 80, 80);
  fix_wrap(&f);
  memset(&rl, 0, sizeof(rl));

  CHECK_EQ(lk_editor_wrap_rows(f.ed, 0), 2);
  CHECK(fix_render(&f, &rl));
  CHECK_EQ(count_runs(&rl), 2);
  CHECK(run_is(&rl, nth_run(&rl, 0), "abcdefghi"));
  CHECK_EQ((unsigned)nth_run(&rl, 0)->rect.w, 72u);
  CHECK(run_is(&rl, nth_run(&rl, 1), "x"));
  CHECK(nth_run(&rl, 1)->rect.x == 32 && nth_run(&rl, 1)->rect.y == 16);

  lk_render_list_destroy(&rl);
  fix_destroy(&f);
  END_TEST();
}

static void test_wrap_empty_row_progress(void) {
  ed_fix f;
  lk_render_list rl;

  BEGIN_TEST("wrap: empty-row progress guarantee");

  /* width 8 = one codepoint per row */
  fix_init(&f, "abc", 8, 80);
  fix_wrap(&f);
  memset(&rl, 0, sizeof(rl));

  CHECK_EQ(lk_editor_wrap_rows(f.ed, 0), 3);
  CHECK(fix_render(&f, &rl));
  CHECK_EQ(count_runs(&rl), 3);
  CHECK(run_is(&rl, nth_run(&rl, 0), "a"));
  CHECK(run_is(&rl, nth_run(&rl, 2), "c"));
  CHECK(nth_run(&rl, 2)->rect.y == 32);
  lk_render_list_destroy(&rl);
  fix_destroy(&f);

  /* width 4: NOTHING fits, one boundary is taken anyway */
  fix_init(&f, "abc", 4, 80);
  fix_wrap(&f);
  CHECK_EQ(lk_editor_wrap_rows(f.ed, 0), 3);
  fix_destroy(&f);

  END_TEST();
}

static void test_wrap_row_ownership(void) {
  ed_fix f;
  lk_render_list rl;
  const lk_render_cmd *cur;

  BEGIN_TEST("wrap: break belongs to next row, EOL to final");

  fix_init(&f, ED20, 80, 80);
  fix_wrap(&f);
  memset(&rl, 0, sizeof(rl));
  lk_focus_set(f.ui, lk_ui_tree(f.ui), f.nid);

  /* cursor at the break byte (10): NEXT row's start */
  cmd_setcur(&f, 10, 0);
  fix_layout(&f);
  CHECK(fix_render(&f, &rl));
  cur = find_cursor_fill(&rl);
  CHECK(cur && cur->rect.x == 0 && cur->rect.y == 16);

  /* end-of-line caret: final row, past its last codepoint */
  cmd_setcur(&f, 20, 0);
  fix_layout(&f);
  CHECK(fix_render(&f, &rl));
  cur = find_cursor_fill(&rl);
  CHECK(cur && cur->rect.x == 80 && cur->rect.y == 16);

  lk_render_list_destroy(&rl);
  fix_destroy(&f);
  END_TEST();
}

static void test_wrap_row_commands(void) {
  ed_fix f;

  BEGIN_TEST("wrap: ROW vs LINE START/END + HOME/END keys");

  /* unwrapped: ROW variants are identical to the logical ones */
  fix_init(&f, "ab\ncd", 400, 80);
  cmd_setcur(&f, 4, 0);
  CHECK(cmd_move(&f, LK_ED_MOVE_ROW_START, 0) == 1);
  CHECK_EQ(lk_editor_cursor(f.ed), 3);
  CHECK(cmd_move(&f, LK_ED_MOVE_ROW_END, 0) == 1);
  CHECK_EQ(lk_editor_cursor(f.ed), 5);
  fix_destroy(&f);

  fix_init(&f, ED20, 80, 80);
  fix_wrap(&f);

  /* cursor on row 1: ROW_START -> the break; LINE_START -> 0 */
  cmd_setcur(&f, 15, 0);
  CHECK(cmd_move(&f, LK_ED_MOVE_ROW_START, 0) == 1);
  CHECK_EQ(lk_editor_cursor(f.ed), 10);
  CHECK(cmd_move(&f, LK_ED_MOVE_LINE_START, 0) == 1);
  CHECK_EQ(lk_editor_cursor(f.ed), 0);

  /* cursor on row 0: ROW_END -> the break byte (renders at row 1's
   * start per the ownership rule); LINE_END -> the line end */
  cmd_setcur(&f, 5, 0);
  CHECK(cmd_move(&f, LK_ED_MOVE_ROW_END, 0) == 1);
  CHECK_EQ(lk_editor_cursor(f.ed), 10);
  cmd_setcur(&f, 5, 0);
  CHECK(cmd_move(&f, LK_ED_MOVE_LINE_END, 0) == 1);
  CHECK_EQ(lk_editor_cursor(f.ed), 20);

  /* the default keymap: HOME/END are the ROW variants, CTRL+HOME/END
   * remain document motion */
  cmd_setcur(&f, 15, 0);
  CHECK(send_key(&f, LKK_HOME, 0) == 1);
  CHECK_EQ(lk_editor_cursor(f.ed), 10);
  CHECK(send_key(&f, LKK_END, 0) == 1);
  CHECK_EQ(lk_editor_cursor(f.ed), 20);
  CHECK(send_key(&f, LKK_HOME, LK_MOD_CTRL) == 1);
  CHECK_EQ(lk_editor_cursor(f.ed), 0);
  CHECK(send_key(&f, LKK_END, LK_MOD_CTRL) == 1);
  CHECK_EQ(lk_editor_cursor(f.ed), 20);

  fix_destroy(&f);
  END_TEST();
}

static void test_wrap_motion_rows_and_seams(void) {
  ed_fix f;

  BEGIN_TEST("wrap: UP/DOWN cross rows and line seams");

  /* line 0 = 20 cp (2 rows), line 1 = "xy" (starts at byte 21) */
  fix_init(&f, ED20 "\nxy", 80, 80);
  fix_wrap(&f);

  cmd_setcur(&f, 2, 0); /* row 0, x = 16 */

  CHECK(cmd_move(&f, LK_ED_MOVE_DOWN, 0) == 1);
  CHECK_EQ(lk_editor_cursor(f.ed), 12); /* row 1 of line 0 */
  CHECK(cmd_move(&f, LK_ED_MOVE_DOWN, 0) == 1);
  CHECK_EQ(lk_editor_cursor(f.ed), 23); /* line 1, clamped to "xy" end */
  CHECK(cmd_move(&f, LK_ED_MOVE_UP, 0) == 1);
  CHECK_EQ(lk_editor_cursor(f.ed), 12); /* seam back up, sticky 16 */
  CHECK(cmd_move(&f, LK_ED_MOVE_UP, 0) == 1);
  CHECK_EQ(lk_editor_cursor(f.ed), 2);

  /* UP on the first ROW goes to 0; DOWN on the last row to the end */
  CHECK(cmd_move(&f, LK_ED_MOVE_UP, 0) == 1);
  CHECK_EQ(lk_editor_cursor(f.ed), 0);
  cmd_setcur(&f, 22, 0);
  CHECK(cmd_move(&f, LK_ED_MOVE_DOWN, 0) == 1);
  CHECK_EQ(lk_editor_cursor(f.ed), 23);

  fix_destroy(&f);
  END_TEST();
}

static void test_wrap_sticky_x_rows(void) {
  ed_fix f;
  char buf[64];

  BEGIN_TEST("wrap: sticky x through wrapped rows");

  /* one 30 cp line: rows at 0/10/20 */
  make_rows(buf, sizeof(buf), 1, 30);
  fix_init(&f, buf, 80, 80);
  fix_wrap(&f);

  cmd_setcur(&f, 25, 0); /* row 2, x = 40 */
  CHECK(cmd_move(&f, LK_ED_MOVE_UP, 0) == 1);
  CHECK_EQ(lk_editor_cursor(f.ed), 15);
  CHECK(cmd_move(&f, LK_ED_MOVE_UP, 0) == 1);
  CHECK_EQ(lk_editor_cursor(f.ed), 5);
  CHECK(cmd_move(&f, LK_ED_MOVE_DOWN, 0) == 1);
  CHECK_EQ(lk_editor_cursor(f.ed), 15);
  CHECK(cmd_move(&f, LK_ED_MOVE_DOWN, 0) == 1);
  CHECK_EQ(lk_editor_cursor(f.ed), 25);

  /* sticky at the right edge clamps INSIDE a non-final target row
   * (the row-end position is the break, owned by the next row) */
  cmd_setcur(&f, 30, 0); /* EOL caret on the final row, x = 80 */
  CHECK(cmd_move(&f, LK_ED_MOVE_UP, 0) == 1);
  CHECK_EQ(lk_editor_cursor(f.ed), 19);
  CHECK(cmd_move(&f, LK_ED_MOVE_DOWN, 0) == 1);
  CHECK_EQ(lk_editor_cursor(f.ed), 30); /* sticky 80 restored */

  fix_destroy(&f);
  END_TEST();
}

static void test_wrap_click_rows(void) {
  ed_fix f;

  BEGIN_TEST("wrap: click on row 2 and at the right edge");

  fix_init(&f, ED20, 80, 80);
  fix_wrap(&f);

  /* row 1 (y=20): x=8 -> boundary 1 within the row -> byte 11 */
  CHECK(send_pointer(&f, LK_EVENT_POINTER_DOWN, 8, 20) == 1);
  CHECK_EQ(lk_editor_cursor(f.ed), 11);
  send_pointer(&f, LK_EVENT_POINTER_UP, 8, 20);

  /* right edge of row 0: the break position (renders on row 1) */
  CHECK(send_pointer(&f, LK_EVENT_POINTER_DOWN, 80, 4) == 1);
  CHECK_EQ(lk_editor_cursor(f.ed), 10);
  send_pointer(&f, LK_EVENT_POINTER_UP, 80, 4);

  /* far right of the final row clamps to the line end */
  CHECK(send_pointer(&f, LK_EVENT_POINTER_DOWN, 500, 20) == 1);
  CHECK_EQ(lk_editor_cursor(f.ed), 20);
  send_pointer(&f, LK_EVENT_POINTER_UP, 500, 20);

  fix_destroy(&f);
  END_TEST();
}

static void test_wrap_selection_rows(void) {
  ed_fix f;
  lk_render_list rl;
  const lk_render_cmd *r0;
  const lk_render_cmd *r1;
  const lk_render_cmd *r2;
  char buf[64];

  BEGIN_TEST("wrap: selection head/body/tail across rows");

  /* one 30 cp line, 3 rows */
  make_rows(buf, sizeof(buf), 1, 30);
  fix_init(&f, buf, 80, 80);
  fix_wrap(&f);
  memset(&rl, 0, sizeof(rl));

  /* [5,15): head on row 0, tail on row 1 */
  cmd_setcur(&f, 5, 0);
  cmd_setcur(&f, 15, 1);
  fix_layout(&f);
  CHECK(fix_render(&f, &rl));
  CHECK_EQ(count_sel_fills(&rl), 2);
  r0 = nth_sel_fill(&rl, 0);
  r1 = nth_sel_fill(&rl, 1);
  CHECK(r0 && r0->rect.x == 40 && r0->rect.y == 0 && r0->rect.w == 40 &&
        r0->rect.h == 16);
  CHECK(r1 && r1->rect.x == 0 && r1->rect.y == 16 && r1->rect.w == 40);

  /* [2,25): head row 0 + full-width body row 1 + tail row 2 -- all
   * within ONE document line */
  cmd_setcur(&f, 2, 0);
  cmd_setcur(&f, 25, 1);
  fix_layout(&f);
  CHECK(fix_render(&f, &rl));
  CHECK_EQ(count_sel_fills(&rl), 3);
  r0 = nth_sel_fill(&rl, 0);
  r1 = nth_sel_fill(&rl, 1);
  r2 = nth_sel_fill(&rl, 2);
  CHECK(r0 && r0->rect.x == 16 && r0->rect.y == 0 && r0->rect.w == 64);
  CHECK(r1 && r1->rect.x == 0 && r1->rect.y == 16 && r1->rect.w == 80 &&
        r1->rect.h == 16);
  CHECK(r2 && r2->rect.x == 0 && r2->rect.y == 32 && r2->rect.w == 40);

  lk_render_list_destroy(&rl);
  fix_destroy(&f);
  END_TEST();
}

static void test_wrap_splice_touched_line_only(void) {
  ed_fix f;
  char buf[64];

  BEGIN_TEST("wrap: edit re-measures only the touched line");

  /* 3 lines x 12 cp = 2 rows each, all visible (h = 96) */
  make_rows(buf, sizeof(buf), 3, 12);
  fix_init(&f, buf, 80, 96);
  fix_wrap(&f);

  CHECK_EQ(lk_editor_wrap_rows(f.ed, 0), 2);
  CHECK_EQ(lk_editor_wrap_rows(f.ed, 1), 2);
  CHECK_EQ(lk_editor_wrap_rows(f.ed, 2), 2);

  /* no-newline edit in line 1 (foreign, straight to the doc) */
  CHECK(lk_doc_insert(f.doc, lk_doc_line_start(f.doc, 1) + 2, "ZZ", 2) == 1);

  CHECK_EQ(lk_editor_wrap_rows(f.ed, 0), 2); /* untouched, still valid */
  CHECK_EQ(lk_editor_wrap_rows(f.ed, 1), 0); /* dirtied */
  CHECK_EQ(lk_editor_wrap_rows(f.ed, 2), 2); /* untouched */

  fix_layout(&f);
  CHECK_EQ(lk_editor_wrap_rows(f.ed, 1), 2); /* 14 cp: still 2 rows */

  /* delete-then-insert at one position (replace as ONE transaction
   * through the editor: two sequential deltas, same line) */
  cmd_setcur(&f, lk_doc_line_start(f.doc, 1), 0);
  cmd_setcur(&f, lk_doc_line_start(f.doc, 1) + 4, 1);
  CHECK(cmd_ins(&f, "Q") == 1);
  CHECK_EQ(lk_editor_wrap_rows(f.ed, 0), 2);
  CHECK_EQ(lk_editor_wrap_rows(f.ed, 1), 0);
  CHECK_EQ(lk_editor_wrap_rows(f.ed, 2), 2);

  fix_destroy(&f);
  END_TEST();
}

static void test_wrap_splice_newlines(void) {
  ed_fix f;
  char buf[80];
  lk_u32 l1;

  BEGIN_TEST("wrap: newline splices shift entries by index");

  /* 4 lines x 12 cp = 8 rows, all visible (h = 128) */
  make_rows(buf, sizeof(buf), 4, 12);
  fix_init(&f, buf, 80, 128);
  fix_wrap(&f);

  /* one-newline insert inside line 0: fresh invalid entry after it,
   * old lines 1-2 shift down with their shapes intact */
  CHECK(lk_doc_insert(f.doc, 6, "\n", 1) == 1);
  CHECK_EQ(lk_editor_wrap_rows(f.ed, 0), 0); /* dirtied */
  CHECK_EQ(lk_editor_wrap_rows(f.ed, 1), 0); /* fresh entry */
  CHECK_EQ(lk_editor_wrap_rows(f.ed, 2), 2); /* old line 1, shape kept */
  CHECK_EQ(lk_editor_wrap_rows(f.ed, 3), 2); /* old line 2 */
  CHECK_EQ(lk_editor_wrap_rows(f.ed, 4), 2); /* old line 3 */

  /* one-newline delete joins them back */
  CHECK(lk_doc_delete(f.doc, 6, 1) == 1);
  CHECK_EQ(lk_editor_wrap_rows(f.ed, 0), 0);
  CHECK_EQ(lk_editor_wrap_rows(f.ed, 1), 2);
  CHECK_EQ(lk_editor_wrap_rows(f.ed, 2), 2);
  CHECK_EQ(lk_editor_wrap_rows(f.ed, 3), 2);

  fix_layout(&f);
  CHECK_EQ(lk_editor_wrap_rows(f.ed, 0), 2);

  /* N-lines-replaced-with-M in one transaction: delete "\n...\n"
   * spanning two newlines (begins AND ends exactly on a newline,
   * merging lines 1-2 away), insert one newline back.  Lines within
   * the replaced range re-measure; the line BELOW it shifts by index
   * with its shape intact. */
  l1 = lk_doc_line_start(f.doc, 1);
  lk_doc_begin(f.doc, 42);
  CHECK(lk_doc_delete(f.doc, l1 - 1, 14) == 1); /* "\n" + line1 + "\n" */
  CHECK(lk_doc_insert(f.doc, l1 - 1, "\n", 1) == 1);
  lk_doc_commit(f.doc);

  CHECK_EQ(lk_doc_line_count(f.doc), 3);
  CHECK_EQ(lk_editor_wrap_rows(f.ed, 0), 0); /* replaced range */
  CHECK_EQ(lk_editor_wrap_rows(f.ed, 1), 0);
  CHECK_EQ(lk_editor_wrap_rows(f.ed, 2), 2); /* old line 3, shape kept */

  fix_layout(&f);
  CHECK_EQ(lk_editor_wrap_rows(f.ed, 0), 2);
  CHECK_EQ(lk_editor_wrap_rows(f.ed, 1), 2);
  CHECK_EQ(lk_editor_wrap_rows(f.ed, 2), 2);

  fix_destroy(&f);
  END_TEST();
}

static void test_wrap_generation_invalidation(void) {
  ed_fix f;
  lk_render_list rl;

  BEGIN_TEST("wrap: width change + invalidate_layout re-measure");

  fix_init(&f, ED20, 80, 80);
  fix_wrap(&f);
  memset(&rl, 0, sizeof(rl));

  CHECK_EQ(lk_editor_wrap_rows(f.ed, 0), 2);

  /* widen the viewport (split-divider drag): one generation bump,
   * relayout at the new width unwraps the line */
  f.cfg.viewport_w = 160;
  fix_frame(&f, 1);
  fix_layout(&f);
  CHECK_EQ(lk_editor_wrap_rows(f.ed, 0), 1);
  CHECK(fix_render(&f, &rl));
  CHECK_EQ(count_runs(&rl), 1);
  CHECK(run_is(&rl, nth_run(&rl, 0), ED20));

  /* explicit invalidation hook: stale until the next layout */
  lk_editor_invalidate_layout(f.ed);
  CHECK_EQ(lk_editor_wrap_rows(f.ed, 0), 0);
  fix_layout(&f);
  CHECK_EQ(lk_editor_wrap_rows(f.ed, 0), 1);

  lk_render_list_destroy(&rl);
  fix_destroy(&f);
  END_TEST();
}

static void test_wrap_anchor_right_affinity(void) {
  ed_fix f;
  char buf[2048];
  lk_u32 old_top;
  lk_render_list rl;

  BEGIN_TEST("wrap: insert AT top_byte keeps content anchored");

  make_lines(buf, sizeof(buf), 10);
  fix_init(&f, buf, 400, 80);
  fix_wrap(&f);

  cmd_scroll(&f, 3);
  fix_layout(&f);
  old_top = lk_editor_get_viewport(f.ed).top_byte;
  CHECK_EQ(old_top, lk_doc_line_start(f.doc, 3));

  /* insertion exactly at the anchor: RIGHT affinity shifts the
   * anchor past the inserted bytes (before any layout runs) */
  CHECK(lk_doc_insert(f.doc, old_top, "NEW\n", 4) == 1);
  CHECK_EQ(lk_editor_get_viewport(f.ed).top_byte, old_top + 4);

  /* the viewport still shows the content the user was reading */
  fix_layout(&f);
  memset(&rl, 0, sizeof(rl));
  CHECK(fix_render(&f, &rl));
  CHECK(run_is(&rl, nth_run(&rl, 0), "line 3"));

  lk_render_list_destroy(&rl);
  fix_destroy(&f);
  END_TEST();
}

static void test_wrap_distant_reanchor(void) {
  ed_fix f;
  char buf[2200];
  lk_render_list rl;

  BEGIN_TEST("wrap: distant jump measures only the target region");

  /* 100 lines x 20 cp = 2 rows each; 5 rows visible */
  make_rows(buf, sizeof(buf), 100, 20);
  fix_init(&f, buf, 80, 80);
  fix_wrap(&f);

  CHECK_EQ(lk_editor_wrap_rows(f.ed, 0), 2); /* viewport measured */
  CHECK_EQ(lk_editor_wrap_rows(f.ed, 50), 0);

  /* jump far outside the materialized window */
  lk_editor_set_cursor(f.ed, lk_doc_line_start(f.doc, 90));
  fix_layout(&f);

  /* bottom-placed: anchor backs up 4 rows from (line 90, row 0) */
  CHECK_EQ(lk_editor_get_viewport(f.ed).top_byte,
           lk_doc_line_start(f.doc, 88));
  CHECK_EQ(lk_editor_wrap_rows(f.ed, 88), 2);
  CHECK_EQ(lk_editor_wrap_rows(f.ed, 90), 2);

  /* the intervening lines were NEVER measured (no full walk), and
   * neither were the doc-end lines (bottom clamp skipped) */
  CHECK_EQ(lk_editor_wrap_rows(f.ed, 20), 0);
  CHECK_EQ(lk_editor_wrap_rows(f.ed, 50), 0);
  CHECK_EQ(lk_editor_wrap_rows(f.ed, 87), 0);
  CHECK_EQ(lk_editor_wrap_rows(f.ed, 95), 0);

  /* cursor is visible on the last row */
  memset(&rl, 0, sizeof(rl));
  lk_focus_set(f.ui, lk_ui_tree(f.ui), f.nid);
  CHECK(fix_render(&f, &rl));

  {
    const lk_render_cmd *cur = find_cursor_fill(&rl);

    CHECK(cur && cur->rect.x == 0 && cur->rect.y == 64);
  }

  lk_render_list_destroy(&rl);
  fix_destroy(&f);
  END_TEST();
}

static void test_wrap_scroll_rows_and_page(void) {
  ed_fix f;
  char buf[2200];

  BEGIN_TEST("wrap: wheel scrolls rows, PAGE moves by rows");

  make_rows(buf, sizeof(buf), 100, 20);
  fix_init(&f, buf, 80, 80);
  fix_wrap(&f);

  /* wheel down 3 VISUAL rows: line 1, row 1 */
  CHECK(send_wheel(&f, -1) == 1);
  CHECK_EQ(lk_editor_get_viewport(f.ed).top_byte,
           lk_doc_line_start(f.doc, 1) + 10);
  CHECK(send_wheel(&f, 1) == 1);
  CHECK_EQ(lk_editor_get_viewport(f.ed).top_byte, 0);

  /* PAGE_DOWN: 5 visible rows -> (line 2, row 1) */
  cmd_setcur(&f, 0, 0);
  CHECK(cmd_move(&f, LK_ED_MOVE_PAGE_DOWN, 0) == 1);
  CHECK_EQ(lk_editor_cursor(f.ed), lk_doc_line_start(f.doc, 2) + 10);
  CHECK(cmd_move(&f, LK_ED_MOVE_PAGE_UP, 0) == 1);
  CHECK_EQ(lk_editor_cursor(f.ed), 0);

  fix_destroy(&f);
  END_TEST();
}

static void test_wrap_scroll_extent_estimator(void) {
  ed_fix f;
  char buf[600];
  lk_u32 l5;
  lk_u32 off = 0;
  int i;
  lk_render_list rl;

  BEGIN_TEST("wrap: unmeasured long line counts >1 row in extent");

  /* 5 short lines, one 400-byte line (40 rows at width 80), one
   * short line; viewport = 3 rows */
  for (i = 0; i < 5; i++) {
    memset(buf + off, (int)('a' + i), 10);
    off += 10;
    buf[off++] = '\n';
  }

  memset(buf + off, 'c', 400);
  off += 400;
  buf[off++] = '\n';
  memset(buf + off, 'd', 10);
  off += 10;
  buf[off] = '\0';

  fix_init(&f, buf, 80, 48);
  fix_wrap(&f);
  l5 = lk_doc_line_start(f.doc, 5);

  CHECK_EQ(lk_editor_wrap_rows(f.ed, 5), 0); /* long line unmeasured */

  /* distant scroll by 25 rows: the ESTIMATOR must credit the long
   * line ~40 rows so the target lands INSIDE it (row 20); a
   * 1-row-per-line lie would run past it to the document end */
  CHECK(cmd_scroll(&f, 25) == 1);
  CHECK_EQ(lk_editor_get_viewport(f.ed).top_byte, l5 + 200);
  fix_layout(&f);
  CHECK_EQ(lk_editor_get_viewport(f.ed).top_byte, l5 + 200);

  memset(&rl, 0, sizeof(rl));
  CHECK(fix_render(&f, &rl));
  CHECK_EQ(count_runs(&rl), 3);
  CHECK(run_is(&rl, nth_run(&rl, 0), "cccccccccc"));

  /* overscroll clamps exactly: the last viewport-full is rows 38-39
   * of the long line plus the final line */
  cmd_scroll(&f, 100000);
  fix_layout(&f);
  CHECK_EQ(lk_editor_get_viewport(f.ed).top_byte, l5 + 380);
  CHECK(fix_render(&f, &rl));
  CHECK_EQ(count_runs(&rl), 3);
  CHECK(run_is(&rl, nth_run(&rl, 2), "dddddddddd"));

  /* and all the way back up */
  cmd_scroll(&f, -100000);
  fix_layout(&f);
  CHECK_EQ(lk_editor_get_viewport(f.ed).top_byte, 0);

  lk_render_list_destroy(&rl);
  fix_destroy(&f);
  END_TEST();
}

/* ---- word wrap (docs/editor-wrap.md section 2 word policy) ---- */

static void test_wrap_word_break_basic(void) {
  ed_fix f;
  lk_render_list rl;
  const lk_render_cmd *cur;

  BEGIN_TEST("wrap word: break after the last space that fits");

  /* "hello world foo": the char-fit floor is byte 10 (mid-"world");
   * the word policy backs up to byte 6, AFTER the space */
  fix_init(&f, "hello world foo", 80, 80);
  fix_wrap_word(&f);
  memset(&rl, 0, sizeof(rl));

  CHECK_EQ(lk_editor_wrap_rows(f.ed, 0), 2);
  CHECK(fix_render(&f, &rl));
  CHECK_EQ(count_runs(&rl), 2);
  CHECK(run_is(&rl, nth_run(&rl, 0), "hello "));
  CHECK_EQ((unsigned)nth_run(&rl, 0)->rect.w, 48u);
  CHECK(run_is(&rl, nth_run(&rl, 1), "world foo"));
  CHECK(nth_run(&rl, 1)->rect.x == 0 && nth_run(&rl, 1)->rect.y == 16);

  /* row ownership holds at the word break: the break byte renders at
   * the NEXT row's start */
  lk_focus_set(f.ui, lk_ui_tree(f.ui), f.nid);
  cmd_setcur(&f, 6, 0);
  fix_layout(&f);
  CHECK(fix_render(&f, &rl));
  cur = find_cursor_fill(&rl);
  CHECK(cur && cur->rect.x == 0 && cur->rect.y == 16);

  /* motion over the boundary: DOWN from byte 2 (x = 16) lands at
   * sticky x on row 1 -> byte 8; UP returns */
  cmd_setcur(&f, 2, 0);
  CHECK(cmd_move(&f, LK_ED_MOVE_DOWN, 0) == 1);
  CHECK_EQ(lk_editor_cursor(f.ed), 8);
  CHECK(cmd_move(&f, LK_ED_MOVE_UP, 0) == 1);
  CHECK_EQ(lk_editor_cursor(f.ed), 2);

  /* hit-testing: row 1 x=8 -> byte 7; past row 0's short content
   * clamps to the break byte */
  CHECK(send_pointer(&f, LK_EVENT_POINTER_DOWN, 8, 20) == 1);
  CHECK_EQ(lk_editor_cursor(f.ed), 7);
  send_pointer(&f, LK_EVENT_POINTER_UP, 8, 20);
  CHECK(send_pointer(&f, LK_EVENT_POINTER_DOWN, 80, 4) == 1);
  CHECK_EQ(lk_editor_cursor(f.ed), 6);
  send_pointer(&f, LK_EVENT_POINTER_UP, 80, 4);

  lk_render_list_destroy(&rl);
  fix_destroy(&f);
  END_TEST();
}

static void test_wrap_word_unbreakable_fallback(void) {
  ed_fix f;
  lk_render_list rl;

  BEGIN_TEST("wrap word: unbreakable run falls back to char breaks");

  /* 12 codepoints, no whitespace: identical to character wrap */
  fix_init(&f, "aaaaaaaaaaaa", 80, 80);
  fix_wrap_word(&f);
  memset(&rl, 0, sizeof(rl));

  CHECK_EQ(lk_editor_wrap_rows(f.ed, 0), 2);
  CHECK(fix_render(&f, &rl));
  CHECK_EQ(count_runs(&rl), 2);
  CHECK(run_is(&rl, nth_run(&rl, 0), "aaaaaaaaaa"));
  CHECK(run_is(&rl, nth_run(&rl, 1), "aa"));
  lk_render_list_destroy(&rl);
  fix_destroy(&f);

  /* progress guarantee survives the word policy: width 4 fits
   * NOTHING, one boundary per row is taken anyway */
  fix_init(&f, "abc", 4, 80);
  fix_wrap_word(&f);
  CHECK_EQ(lk_editor_wrap_rows(f.ed, 0), 3);
  fix_destroy(&f);

  END_TEST();
}

static void test_wrap_word_trailing_spaces(void) {
  ed_fix f;
  lk_render_list rl;

  BEGIN_TEST("wrap word: a space run stays on the upper row");

  /* "hello   world": the break lands AFTER the last space (byte 8);
   * all three spaces stay on row 0 */
  fix_init(&f, "hello   world", 80, 80);
  fix_wrap_word(&f);
  memset(&rl, 0, sizeof(rl));

  CHECK_EQ(lk_editor_wrap_rows(f.ed, 0), 2);
  CHECK(fix_render(&f, &rl));
  CHECK_EQ(count_runs(&rl), 2);
  CHECK(run_is(&rl, nth_run(&rl, 0), "hello   "));
  CHECK_EQ((unsigned)nth_run(&rl, 0)->rect.w, 64u);
  CHECK(run_is(&rl, nth_run(&rl, 1), "world"));
  CHECK(nth_run(&rl, 1)->rect.x == 0 && nth_run(&rl, 1)->rect.y == 16);

  lk_render_list_destroy(&rl);
  fix_destroy(&f);
  END_TEST();
}

static void test_wrap_word_tab_boundary(void) {
  ed_fix f;
  lk_render_list rl;

  BEGIN_TEST("wrap word: a tab boundary is breakable");

  /* "ab\tcdefghijk": tab stop at 32, segment 72 px overflows; the
   * char-fit floor is byte 9 (mid-word) and the scan backs up to the
   * tab boundary (byte 3) -- the whole word moves to row 1 */
  fix_init(&f, "ab\tcdefghijk", 80, 80);
  fix_wrap_word(&f);
  memset(&rl, 0, sizeof(rl));

  CHECK_EQ(lk_editor_wrap_rows(f.ed, 0), 2);
  CHECK(fix_render(&f, &rl));
  CHECK_EQ(count_runs(&rl), 2);
  CHECK(run_is(&rl, nth_run(&rl, 0), "ab"));
  CHECK(run_is(&rl, nth_run(&rl, 1), "cdefghijk"));
  CHECK(nth_run(&rl, 1)->rect.x == 0 && nth_run(&rl, 1)->rect.y == 16);
  CHECK_EQ((unsigned)nth_run(&rl, 1)->rect.w, 72u);

  lk_render_list_destroy(&rl);
  fix_destroy(&f);
  END_TEST();
}

static void test_wrap_word_mode_switch(void) {
  ed_fix f;
  lk_render_list rl;

  BEGIN_TEST("wrap word: character <-> word <-> none rewraps");

  /* "aaaa bbbbbb cccc": character = 2 rows (break at 10), word = 3
   * rows (breaks at 5 and 12) */
  fix_init(&f, "aaaa bbbbbb cccc", 80, 80);
  fix_wrap(&f);
  memset(&rl, 0, sizeof(rl));

  CHECK_EQ(lk_editor_wrap_rows(f.ed, 0), 2);
  CHECK(fix_render(&f, &rl));
  CHECK(run_is(&rl, nth_run(&rl, 0), "aaaa bbbbb"));

  /* switch: one generation bump, stale until the next layout */
  CHECK(lk_editor_set_wrap_mode(f.ed, LK_EDITOR_WRAP_WORD) == 1);
  CHECK(lk_editor_wrap_mode_get(f.ed) == LK_EDITOR_WRAP_WORD);
  CHECK_EQ(lk_editor_wrap_rows(f.ed, 0), 0);
  fix_layout(&f);
  CHECK_EQ(lk_editor_wrap_rows(f.ed, 0), 3);
  CHECK(fix_render(&f, &rl));
  CHECK_EQ(count_runs(&rl), 3);
  CHECK(run_is(&rl, nth_run(&rl, 0), "aaaa "));
  CHECK(run_is(&rl, nth_run(&rl, 1), "bbbbbb "));
  CHECK(run_is(&rl, nth_run(&rl, 2), "cccc"));

  /* word -> none: the single-run shape returns */
  CHECK(lk_editor_set_wrap_mode(f.ed, LK_EDITOR_WRAP_NONE) == 1);
  CHECK(lk_editor_wrap_mode_get(f.ed) == LK_EDITOR_WRAP_NONE);
  fix_layout(&f);
  CHECK_EQ(lk_editor_wrap_rows(f.ed, 0), 0); /* no cache in NONE */
  CHECK(fix_render(&f, &rl));
  CHECK_EQ(count_runs(&rl), 1);
  CHECK(run_is(&rl, nth_run(&rl, 0), "aaaa bbbbbb cccc"));

  lk_render_list_destroy(&rl);
  fix_destroy(&f);
  END_TEST();
}

static void test_wrap_word_splice(void) {
  ed_fix f;

  BEGIN_TEST("wrap word: edit splice re-measures the touched line");

  /* 3 x "hello world foo" = 2 word rows each, all visible (h = 96) */
  fix_init(&f, "hello world foo\nhello world foo\nhello world foo", 80,
           96);
  fix_wrap_word(&f);

  CHECK_EQ(lk_editor_wrap_rows(f.ed, 0), 2);
  CHECK_EQ(lk_editor_wrap_rows(f.ed, 1), 2);
  CHECK_EQ(lk_editor_wrap_rows(f.ed, 2), 2);

  /* no-newline edit in line 1 (foreign, straight to the doc): only
   * that line is dirtied, and it re-wraps to 3 word rows ("wide " /
   * "hello " / "world foo" -> breaks at 5 and 11) */
  CHECK(lk_doc_insert(f.doc, lk_doc_line_start(f.doc, 1), "wide ", 5) == 1);

  CHECK_EQ(lk_editor_wrap_rows(f.ed, 0), 2); /* untouched, still valid */
  CHECK_EQ(lk_editor_wrap_rows(f.ed, 1), 0); /* dirtied */
  CHECK_EQ(lk_editor_wrap_rows(f.ed, 2), 2); /* untouched */

  fix_layout(&f);
  CHECK_EQ(lk_editor_wrap_rows(f.ed, 0), 2);
  CHECK_EQ(lk_editor_wrap_rows(f.ed, 1), 3);

  fix_destroy(&f);
  END_TEST();
}

static void test_hscroll_follow_cursor(void) {
  ed_fix f;
  lk_render_list rl;
  const lk_render_cmd *cur;

  BEGIN_TEST("hscroll: NONE mode follows the cursor");

  fix_init(&f, ED20, 80, 80); /* 160 px line in an 80 px view */
  memset(&rl, 0, sizeof(rl));
  lk_focus_set(f.ui, lk_ui_tree(f.ui), f.nid);

  CHECK_EQ(lk_editor_scroll_x(f.ed), 0);

  /* cursor to EOL: scroll right so it sits margin (16 px) inside */
  cmd_setcur(&f, 20, 0);
  fix_layout(&f);
  CHECK_EQ((unsigned)lk_editor_scroll_x(f.ed), 96u);
  CHECK(fix_render(&f, &rl));
  cur = find_cursor_fill(&rl);
  CHECK(cur && cur->rect.x == 64 && cur->rect.y == 0);
  CHECK(nth_run(&rl, 0)->rect.x == -96);

  /* hit-testing rides the same origin: x=64 is the line end */
  CHECK(send_pointer(&f, LK_EVENT_POINTER_DOWN, 64, 4) == 1);
  CHECK_EQ(lk_editor_cursor(f.ed), 20);
  send_pointer(&f, LK_EVENT_POINTER_UP, 64, 4);

  /* back to the start: scroll returns to 0 */
  cmd_setcur(&f, 0, 0);
  fix_layout(&f);
  CHECK_EQ(lk_editor_scroll_x(f.ed), 0);
  CHECK(fix_render(&f, &rl));
  CHECK(nth_run(&rl, 0)->rect.x == 0);

  lk_render_list_destroy(&rl);
  fix_destroy(&f);
  END_TEST();
}

static void test_hscroll_wheel(void) {
  ed_fix f;
  lk_render_list rl;

  BEGIN_TEST("hscroll: SHIFT+wheel and wheel-dx in NONE mode");

  fix_init(&f, ED20, 80, 80);
  memset(&rl, 0, sizeof(rl));

  /* SHIFT + wheel toward the user scrolls right by 3 advances */
  CHECK(send_wheel_ex(&f, 0, -1, LK_MOD_SHIFT) == 1);
  CHECK_EQ((unsigned)lk_editor_scroll_x(f.ed), 24u);
  fix_layout(&f);
  CHECK_EQ((unsigned)lk_editor_scroll_x(f.ed), 24u);
  CHECK(fix_render(&f, &rl));
  CHECK(nth_run(&rl, 0)->rect.x == -24);

  /* native dx maps too */
  CHECK(send_wheel_ex(&f, 2, 0, 0) == 1);
  CHECK_EQ((unsigned)lk_editor_scroll_x(f.ed), 72u);

  /* SHIFT + wheel away scrolls back left */
  CHECK(send_wheel_ex(&f, 0, 1, LK_MOD_SHIFT) == 1);
  CHECK_EQ((unsigned)lk_editor_scroll_x(f.ed), 48u);

  /* layout soft-clamps against the widest measured line */
  lk_editor_scroll_x_wheel(f.ed, 100);
  fix_layout(&f);
  CHECK_EQ((unsigned)lk_editor_scroll_x(f.ed), 96u); /* 160-80+16 */

  /* wrapping forces horizontal scroll off; SHIFT+wheel goes back to
   * the vertical path */
  fix_wrap(&f);
  CHECK_EQ(lk_editor_scroll_x(f.ed), 0);
  CHECK(send_wheel_ex(&f, 0, -1, LK_MOD_SHIFT) == 1);
  CHECK_EQ(lk_editor_scroll_x(f.ed), 0);
  CHECK_EQ(lk_editor_get_viewport(f.ed).top_byte, 10); /* 3 rows down,
                                                          clamped */

  lk_render_list_destroy(&rl);
  fix_destroy(&f);
  END_TEST();
}

/* ================================================================
 * (h) multi-view: two editors over one document.  Foreign
 * transactions (anything a view did not initiate, including undo /
 * redo invoked elsewhere) transform the other view's cursor and
 * selection per delta — delete first, then insert, RIGHT bias on
 * insert — instead of clamping.  Only the INVOKING editor's
 * LK_ED_UNDO/REDO jumps to the replay site.
 * ================================================================ */

typedef struct ed2_fix {
  lk_document *doc;
  lk_edit_history *hist;
  lk_editor *ea, *eb;
} ed2_fix;

static void ed2_init(ed2_fix *f, const char *text) {
  f->doc = lk_doc_from_str(NULL, NULL, NULL, text, (lk_u32)strlen(text));
  f->hist = lk_history_new(NULL, NULL, NULL);
  lk_history_attach(f->hist, f->doc);
  f->ea = lk_editor_new(NULL, NULL, NULL, f->doc, f->hist);
  f->eb = lk_editor_new(NULL, NULL, NULL, f->doc, f->hist);
}

static void ed2_destroy(ed2_fix *f) {
  lk_editor_destroy(f->eb);
  lk_editor_destroy(f->ea);
  lk_history_destroy(f->hist);
  lk_doc_destroy(f->doc);
}

static void ed2_setcur(lk_editor *e, lk_u32 pos, int extend) {
  lk_editor_cmd_arg a;

  memset(&a, 0, sizeof(a));
  a.set_cursor.pos = pos;
  a.set_cursor.extend = extend;
  lk_editor_command(e, NULL, LK_ED_SET_CURSOR, &a);
}

static void ed2_insert(lk_editor *e, const char *text) {
  lk_editor_cmd_arg a;

  memset(&a, 0, sizeof(a));
  a.text.ptr = text;
  a.text.len = (lk_u32)strlen(text);
  lk_editor_command(e, NULL, LK_ED_INSERT_TEXT, &a);
}

static void test_multiview_foreign_insert(void) {
  ed2_fix f;

  BEGIN_TEST("multiview: foreign insert before/at/after cursor");

  ed2_init(&f, "hello world");
  ed2_setcur(f.ea, 3, 0);
  ed2_setcur(f.eb, 8, 0);

  /* insert between the cursors: only the one after shifts */
  CHECK(lk_doc_insert(f.doc, 5, "XX", 2));
  CHECK_EQ(lk_editor_cursor(f.ea), 3);
  CHECK_EQ(lk_editor_cursor(f.eb), 10);

  /* insert exactly AT a cursor: RIGHT bias shifts it past the
   * inserted bytes */
  CHECK(lk_doc_insert(f.doc, 10, "Z", 1));
  CHECK_EQ(lk_editor_cursor(f.ea), 3);
  CHECK_EQ(lk_editor_cursor(f.eb), 11);

  /* insert after both: neither moves */
  CHECK(lk_doc_insert(f.doc, 14, "!", 1));
  CHECK_EQ(lk_editor_cursor(f.ea), 3);
  CHECK_EQ(lk_editor_cursor(f.eb), 11);

  ed2_destroy(&f);
  END_TEST();
}

static void test_multiview_foreign_delete(void) {
  ed2_fix f;

  BEGIN_TEST("multiview: foreign delete shifts/collapses cursor");

  ed2_init(&f, "abcdefghij");
  ed2_setcur(f.ea, 2, 0);
  ed2_setcur(f.eb, 8, 0);

  /* delete [4,7): before-range cursor unchanged, after-range shifts */
  CHECK(lk_doc_delete(f.doc, 4, 3));
  CHECK_EQ(lk_editor_cursor(f.ea), 2);
  CHECK_EQ(lk_editor_cursor(f.eb), 5);

  /* delete [4,6) spanning eb's cursor (5): collapses to the delete
   * start */
  CHECK(lk_doc_delete(f.doc, 4, 2));
  CHECK_EQ(lk_editor_cursor(f.ea), 2);
  CHECK_EQ(lk_editor_cursor(f.eb), 4);

  ed2_destroy(&f);
  END_TEST();
}

static void test_multiview_selection_transform(void) {
  ed2_fix f;
  lk_u32 s, e;

  BEGIN_TEST("multiview: selection transforms and collapses");

  ed2_init(&f, "hello world");

  /* eb selects "world" [6,11) */
  ed2_setcur(f.eb, 6, 0);
  ed2_setcur(f.eb, 11, 1);
  CHECK(lk_editor_selection(f.eb, &s, &e) == 1);
  CHECK_EQ(s, 6);
  CHECK_EQ(e, 11);

  /* foreign insert before the selection shifts both ends */
  CHECK(lk_doc_insert(f.doc, 0, "AB", 2));
  CHECK(lk_editor_selection(f.eb, &s, &e) == 1);
  CHECK_EQ(s, 8);
  CHECK_EQ(e, 13);

  /* foreign delete of exactly the selected range collapses the
   * selection (anchor == cursor -> no selection) */
  CHECK(lk_doc_delete(f.doc, 8, 5));
  CHECK(lk_editor_selection(f.eb, &s, &e) == 0);
  CHECK_EQ(lk_editor_cursor(f.eb), 8);

  ed2_destroy(&f);
  END_TEST();
}

static void test_multiview_undo_invoker_jumps(void) {
  ed2_fix f;

  BEGIN_TEST("multiview: LK_ED_UNDO jumps invoker, B transforms");

  ed2_init(&f, "abcdef");

  /* ea appends "XYZ" at 6 */
  ed2_setcur(f.ea, 6, 0);
  ed2_insert(f.ea, "XYZ");
  CHECK_EQ(lk_editor_cursor(f.ea), 9);

  ed2_setcur(f.eb, 2, 0);

  /* undo through ea's command verb: ea jumps to the replay site (the
   * delete's start, 6); eb sees a foreign delete after its cursor and
   * stays put (the old v1 rule would have yanked it to 6 too). */
  CHECK(lk_editor_command(f.ea, NULL, LK_ED_UNDO, NULL) == 1);
  CHECK_EQ(lk_editor_cursor(f.ea), 6);
  CHECK_EQ(lk_editor_cursor(f.eb), 2);

  /* redo through ea: ea jumps to end of re-inserted range; eb still
   * untouched */
  CHECK(lk_editor_command(f.ea, NULL, LK_ED_REDO, NULL) == 1);
  CHECK_EQ(lk_editor_cursor(f.ea), 9);
  CHECK_EQ(lk_editor_cursor(f.eb), 2);

  ed2_destroy(&f);
  END_TEST();
}

static void test_multiview_direct_undo_transforms_both(void) {
  ed2_fix f;

  BEGIN_TEST("multiview: direct history undo transforms both");

  ed2_init(&f, "abcdef");

  ed2_setcur(f.ea, 6, 0);
  ed2_insert(f.ea, "XYZ"); /* doc "abcdefXYZ", ea cursor 9 */
  ed2_setcur(f.ea, 0, 0);  /* move ea away from the edit site */
  ed2_setcur(f.eb, 2, 0);

  /* lk_history_undo with no invoking editor (script path): NO view
   * jumps — the replay is a foreign delete [6,9) to both, and both
   * cursors are before it. */
  CHECK_EQ(lk_history_undo(f.hist, f.doc), 1);
  CHECK_EQ(lk_editor_cursor(f.ea), 0);
  CHECK_EQ(lk_editor_cursor(f.eb), 2);

  ed2_destroy(&f);
  END_TEST();
}

static void test_multiview_viewport_transform_intact(void) {
  ed2_fix f;

  BEGIN_TEST("multiview: viewport anchor transform intact");

  ed2_init(&f, "aaa\nbbb\nccc\n");

  /* Fresh viewports anchor at 0; a foreign insert at 0 shifts the
   * anchor past the inserted bytes (pinned RIGHT affinity) in BOTH
   * views — the viewport transform runs before (and independent of)
   * the cursor branch. */
  CHECK(lk_doc_insert(f.doc, 0, "zz\n", 3));
  CHECK_EQ(lk_editor_get_viewport(f.ea).top_byte, 3);
  CHECK_EQ(lk_editor_get_viewport(f.eb).top_byte, 3);

  /* cursors (both at 0) also shifted right */
  CHECK_EQ(lk_editor_cursor(f.ea), 3);
  CHECK_EQ(lk_editor_cursor(f.eb), 3);

  ed2_destroy(&f);
  END_TEST();
}

/* ---- runner ---- */

void lk_editor_run_tests(void) {
  printf("\nlk editor command-layer tests:\n");
  test_cmd_insert_text();
  test_cmd_delete_utf8();
  test_cmd_move_codepoints();
  test_cmd_word_motion();
  test_cmd_word_motion_multibyte();
  test_cmd_selection_extend_collapse();
  test_cmd_select_all();
  test_cmd_replace_selection_one_undo();
  test_cmd_word_delete();
  test_cmd_line_doc_motion();
  test_cmd_vertical_sticky_x();
  test_cmd_page_fallback_null_ui();
  test_cmd_set_cursor_snaps();
  test_cmd_clipboard();
  test_cmd_readonly_policy();

  printf("\nlk editor event-tier tests:\n");
  test_event_typing();
  test_event_return_tab_esc();
  test_event_arrows_modifiers();
  test_event_page_keys();
  test_event_clipboard_keys();
  test_event_undo_redo_keys();

  printf("\nlk editor pointer tests:\n");
  test_pointer_click_position();
  test_pointer_click_tab_segments();
  test_pointer_drag_selection();

  printf("\nlk editor viewport tests:\n");
  test_viewport_virtualization();
  test_viewport_wheel_scrolls();
  test_viewport_scroll_clamps();
  test_viewport_scroll_to_cursor();
  test_viewport_partial_line_offset();

  printf("\nlk editor render tests:\n");
  test_render_cursor_focus_only();
  test_render_selection_rects();
  test_render_tab_expansion();
  test_scrollbar_exact_geometry();
  test_scrollbar_absent_when_fits();
  test_scrollbar_tiny_viewport();
  test_scrollbar_wrapped_extent();

  printf("\nlk editor degradation tests:\n");
  test_degrade_missing_prop();
  test_degrade_stale_ref();

  printf("\nlk editor utf8 tests:\n");
  test_utf8_typing_backspace();

  printf("\nlk editor wrap tests (stage W1):\n");
  test_wrap_mode_api();
  test_wrap_basic_rows();
  test_wrap_break_utf8();
  test_wrap_tab_at_edge();
  test_wrap_empty_row_progress();
  test_wrap_row_ownership();
  test_wrap_row_commands();
  test_wrap_motion_rows_and_seams();
  test_wrap_sticky_x_rows();
  test_wrap_click_rows();
  test_wrap_selection_rows();
  test_wrap_splice_touched_line_only();
  test_wrap_splice_newlines();
  test_wrap_generation_invalidation();
  test_wrap_anchor_right_affinity();
  test_wrap_distant_reanchor();
  test_wrap_scroll_rows_and_page();
  test_wrap_scroll_extent_estimator();
  test_wrap_word_break_basic();
  test_wrap_word_unbreakable_fallback();
  test_wrap_word_trailing_spaces();
  test_wrap_word_tab_boundary();
  test_wrap_word_mode_switch();
  test_wrap_word_splice();
  test_hscroll_follow_cursor();
  test_hscroll_wheel();

  printf("\nlk editor multi-view tests:\n");
  test_multiview_foreign_insert();
  test_multiview_foreign_delete();
  test_multiview_selection_transform();
  test_multiview_undo_invoker_jumps();
  test_multiview_direct_undo_transforms_both();
  test_multiview_viewport_transform_intact();
}
