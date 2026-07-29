/*
 * lk-present-test.c -- interior presentations (weft-surface track,
 * stage S1; docs/weft-surface.md sections 1 and 5).
 *
 * Blocks: (a) the annot-store presentation carrier (set_present,
 * release hook, query-all precedence), (b) the one translator matcher
 * (lk_translate_presentations, button + mods discrimination at both
 * interior and node level), (c) the editor offering (candidates
 * routed by gesture, pinned no-focus/cursor/capture, byte-identical
 * fallback), (d) the command dispatch arena (UIV_TEXT lifetime, log
 * copies), (e) lk_editor_pos_at's pinned contract, (f) stale-locus
 * detectability.
 *
 * All geometry runs against the stub text backend (8 px per
 * codepoint, line height 16), so pixel assertions are exact.
 */

#include <stdio.h>
#include <string.h>

#include <lk-annot-store.h>
#include <lk-editor.h>
#include <lk.h>

#include "lk-test-harness.h"

/* ---- fixture: ui + doc + editor node + annot store ---- */

typedef struct pres_fix {
  lk_ui *ui;
  lk_document *doc;
  lk_editor *ed;
  lk_annot_store *store;
  lk_resource_ref ref;
  lk_ix node;
  lk_node_id nid;
  lk_rect rects[8];
  lk_layout_cfg cfg;
} pres_fix;

/* Frame: window "w" > editor "ed" (+ optional focusable button "b"
 * so focus can live elsewhere). */
static void pfix_frame(pres_fix *f, int with_button) {
  lk_tree *t = lk_ui_begin_frame(f->ui);
  lk_ix w = lk_tree_add_node_c(t, "w", UIK_WINDOW);
  lk_ix ed = lk_tree_add_node_c(t, "ed", UIK_EDITOR);

  lk_tree_set_root(t, w);
  lk_tree_append_child(t, w, ed);
  lk_tree_add_prop(t, ed, UIP_FOCUSABLE, lk_v_bool(1));
  lk_tree_add_prop(t, ed, UIP_EDITOR, lk_v_resource(f->ref));

  if (with_button) {
    lk_ix b = lk_tree_add_node_c(t, "b", UIK_BUTTON);

    lk_tree_append_child(t, w, b);
    lk_tree_add_prop(t, b, UIP_FOCUSABLE, lk_v_bool(1));
    lk_tree_add_prop(t, b, UIP_TEXT, lk_v_cstr(f->ui->intern, "btn"));
  }

  lk_ui_end_frame(f->ui);
  f->node = lk_tree_find_by_id(lk_ui_tree(f->ui),
                               lk_intern_cid(lk_ui_intern(f->ui), "ed"));
}

static void pfix_init(pres_fix *f, const char *text, int with_button,
                      int with_source) {
  memset(f, 0, sizeof(*f));
  f->doc = lk_doc_from_str(NULL, NULL, NULL, text, (lk_u32)strlen(text));
  f->ed = lk_editor_new(NULL, NULL, NULL, f->doc, NULL);
  f->store = lk_annot_store_new(NULL, NULL, NULL);
  lk_annot_store_attach(f->store, f->doc);
  f->ui = lk_ui_create(NULL);
  f->ref = lk_resource_register(lk_ui_resources(f->ui), lk_editor_type(),
                                f->ed, "ed");
  lk_ui_set_text_backend(f->ui, lk_text_backend_stub());
  f->cfg.text = lk_text_backend_stub();
  f->cfg.viewport_w = 640;
  f->cfg.viewport_h = 480;
  f->cfg.state = lk_ui_state(f->ui);

  if (with_source) {
    lk_presentation_source src = lk_annot_presentation_source(f->store);

    lk_editor_set_presentation_source(f->ed, &src);
  }

  pfix_frame(f, with_button);
  lk_layout(lk_ui_tree(f->ui), &f->cfg, f->rects);
  f->nid = lk_intern_cid(lk_ui_intern(f->ui), "ed");
}

static void pfix_destroy(pres_fix *f) {
  lk_ui_destroy(f->ui);
  lk_editor_destroy(f->ed);
  lk_annot_store_destroy(f->store);
  lk_doc_destroy(f->doc);
}

/* Click at byte position pos on line 0 (stub: 8 px per char, +3 so
 * nearest-boundary snapping lands ON pos). */
static int pfix_click(pres_fix *f, lk_u8 button, lk_u8 mods, lk_u32 pos) {
  lk_event ev;

  memset(&ev, 0, sizeof(ev));
  ev.type = LK_EVENT_POINTER_DOWN;
  ev.target = f->node;
  ev.mods = mods;
  ev.data.pointer.x = f->rects[f->node].x + (lk_i32)pos * 8 + 3;
  ev.data.pointer.y = f->rects[f->node].y + 8;
  ev.data.pointer.button = button;
  lk_event_route(f->ui, &ev);

  return ev.handled;
}

static lk_u32 intern(pres_fix *f, const char *s) {
  return lk_intern_cid(lk_ui_intern(f->ui), s);
}

/* ---- release hook counter ---- */

static int g_release_count;
static lk_value g_release_last;

static void count_release(void *ud, lk_value v) {
  (void)ud;
  g_release_count++;
  g_release_last = v;
}

/* ---- write sink for dump assertions ---- */

typedef struct pres_buf {
  char data[4096];
  lk_u32 len;
} pres_buf;

static void pres_buf_write(void *ud, const char *bytes, lk_u32 len) {
  pres_buf *b = (pres_buf *)ud;

  if (b->len + len < sizeof(b->data)) {
    memcpy(b->data + b->len, bytes, len);
    b->len += len;
    b->data[b->len] = '\0';
  }
}

/* ---- store: set_present + query-all precedence ---- */

static void test_annot_set_present_basics(void) {
  lk_annot_store *s = lk_annot_store_new(NULL, NULL, NULL);
  lk_u32 a = lk_annot_add(s, 2, 8, "base", NULL, NULL, 0);
  lk_presentation_hit hits[4];
  lk_u32 n;

  BEGIN_TEST("present: set_present + presentations_at fills hit");

  /* missing id / zero type rejected */
  CHECK_EQ(lk_annot_set_present(s, 999, 7, lk_v_i32(1)), 0);
  CHECK_EQ(lk_annot_set_present(s, a, 0, lk_v_i32(1)), 0);

  CHECK_EQ(lk_annot_set_present(s, a, 7, lk_v_i32(42)), 1);

  n = lk_annot_presentations_at(s, 4, hits, 4);
  CHECK_EQ(n, 1);
  CHECK_EQ(hits[0].type_id, 7);
  CHECK_EQ(hits[0].value.tag, UIV_I32);
  CHECK_EQ(hits[0].value.as.i, 42);
  CHECK_EQ(hits[0].locus_kind, 0); /* store leaves the kind unnamed */
  CHECK_EQ(hits[0].locus[0], a);
  CHECK_EQ(hits[0].locus[1], 2);
  CHECK_EQ(hits[0].locus[2], 8);
  CHECK_EQ(hits[0].locus[3], 0); /* hit position: editor's job */

  /* outside the range: no candidates */
  n = lk_annot_presentations_at(s, 8, hits, 4);
  CHECK_EQ(n, 0);

  /* annotations without a presentation are not candidates */
  lk_annot_add(s, 0, 50, "base", NULL, NULL, 0);
  n = lk_annot_presentations_at(s, 4, hits, 4);
  CHECK_EQ(n, 1);

  END_TEST();
  lk_annot_store_destroy(s);
}

static void test_annot_presentations_precedence(void) {
  lk_annot_store *s = lk_annot_store_new(NULL, NULL, NULL);
  lk_u32 wide = lk_annot_add(s, 0, 50, "output", NULL, NULL, 0);
  lk_u32 small = lk_annot_add(s, 10, 20, "links", NULL, NULL, 0);
  lk_presentation_hit hits[4];
  lk_u32 n;

  BEGIN_TEST("present: layer priority beats specificity");

  lk_annot_set_present(s, wide, 1, lk_v_i32(1));
  lk_annot_set_present(s, small, 2, lk_v_i32(2));

  /* Equal priority (0/0): the smaller range precedes. */
  n = lk_annot_presentations_at(s, 15, hits, 4);
  CHECK_EQ(n, 2);
  CHECK_EQ(hits[0].locus[0], small);
  CHECK_EQ(hits[1].locus[0], wide);

  /* output priority 5 > links 0: the WIDE range now precedes —
   * priority beats specificity. */
  lk_annot_layer_set_priority(s, "output", 5);
  n = lk_annot_presentations_at(s, 15, hits, 4);
  CHECK_EQ(n, 2);
  CHECK_EQ(hits[0].locus[0], wide);
  CHECK_EQ(hits[1].locus[0], small);

  END_TEST();
  lk_annot_store_destroy(s);
}

static void test_annot_presentations_serial_tiebreak(void) {
  lk_annot_store *s = lk_annot_store_new(NULL, NULL, NULL);
  lk_u32 first = lk_annot_add(s, 5, 15, "l", NULL, NULL, 0);
  lk_u32 second = lk_annot_add(s, 5, 15, "l", NULL, NULL, 0);
  lk_presentation_hit hits[4];
  lk_u32 n;

  BEGIN_TEST("present: insertion serial breaks ties stably");

  /* Set presentations in REVERSE order: serial (record id) still
   * decides, not set_present order. */
  lk_annot_set_present(s, second, 2, lk_v_i32(2));
  lk_annot_set_present(s, first, 1, lk_v_i32(1));

  n = lk_annot_presentations_at(s, 10, hits, 4);
  CHECK_EQ(n, 2);
  CHECK_EQ(hits[0].locus[0], first);
  CHECK_EQ(hits[1].locus[0], second);

  END_TEST();
  lk_annot_store_destroy(s);
}

static void test_annot_release_hook_counts(void) {
  lk_annot_store *s = lk_annot_store_new(NULL, NULL, NULL);
  lk_u32 a;

  BEGIN_TEST("present: release hook fires once per detach");

  g_release_count = 0;
  lk_annot_set_present_release(s, count_release, NULL);

  /* replace */
  a = lk_annot_add(s, 0, 10, "l", NULL, NULL, 0);
  lk_annot_set_present(s, a, 1, lk_v_i32(100));
  CHECK_EQ(g_release_count, 0);
  lk_annot_set_present(s, a, 1, lk_v_i32(200));
  CHECK_EQ(g_release_count, 1);
  CHECK_EQ(g_release_last.as.i, 100);

  /* remove */
  lk_annot_remove(s, a);
  CHECK_EQ(g_release_count, 2);
  CHECK_EQ(g_release_last.as.i, 200);

  /* layer clear */
  a = lk_annot_add(s, 0, 10, "l", NULL, NULL, 0);
  lk_annot_set_present(s, a, 1, lk_v_i32(300));
  lk_annot_store_clear_layer(s, "l");
  CHECK_EQ(g_release_count, 3);

  /* store clear */
  a = lk_annot_add(s, 0, 10, "l", NULL, NULL, 0);
  lk_annot_set_present(s, a, 1, lk_v_i32(400));
  lk_annot_store_clear(s);
  CHECK_EQ(g_release_count, 4);

  /* destroy */
  a = lk_annot_add(s, 0, 10, "l", NULL, NULL, 0);
  lk_annot_set_present(s, a, 1, lk_v_i32(500));
  lk_annot_store_destroy(s);
  CHECK_EQ(g_release_count, 5);
  CHECK_EQ(g_release_last.as.i, 500);

  END_TEST();
}

static void test_annot_release_hook_delete_sweep(void) {
  lk_document *doc = lk_doc_from_str(NULL, NULL, NULL, "hello world", 11);
  lk_annot_store *s = lk_annot_store_new(NULL, NULL, NULL);
  lk_u32 a;

  BEGIN_TEST("present: delete-transform sweep fires the hook");

  lk_annot_store_attach(s, doc);
  g_release_count = 0;
  lk_annot_set_present_release(s, count_release, NULL);

  a = lk_annot_add(s, 2, 4, "l", NULL, NULL, 0);
  lk_annot_set_present(s, a, 1, lk_v_i32(9));

  /* Delete [1, 6): the span collapses and the record is swept. */
  lk_doc_delete(doc, 1, 5);
  CHECK_EQ(g_release_count, 1);
  CHECK_EQ(g_release_last.as.i, 9);
  CHECK(lk_annot_get(s, a) == NULL);

  END_TEST();
  lk_annot_store_destroy(s);
  lk_doc_destroy(doc);
}

/* ---- the matcher ---- */

static void test_translate_presentations_direct(void) {
  pres_fix f;
  lk_presentation_hit hits[2];
  lk_event ev;
  const lk_command_queue *q;

  BEGIN_TEST("present: matcher takes candidates in order");
  pfix_init(&f, "hello world", 0, 0);

  memset(hits, 0, sizeof(hits));
  hits[0].type_id = intern(&f, "output");
  hits[0].value = lk_v_i32(1);
  hits[1].type_id = intern(&f, "loc");
  hits[1].value = lk_v_i32(2);

  /* Only the SECOND candidate has a translator: candidates are not
   * winner-first — the gesture routes past the first. */
  lk_ui_add_translator_s(f.ui, LK_EVENT_POINTER_DOWN, "loc", 0, 0, 0, 0,
                         "Open");

  memset(&ev, 0, sizeof(ev));
  ev.type = LK_EVENT_POINTER_DOWN;
  ev.target = f.node;
  CHECK_EQ(lk_translate_presentations(f.ui, lk_ui_tree(f.ui), f.node, &ev,
                                      hits, 2),
           1);
  CHECK_EQ(ev.handled, 1);

  q = lk_ui_commands(f.ui);
  CHECK_EQ(q->count, 1);
  CHECK_EQ(q->cmds[0].name, intern(&f, "Open"));
  CHECK_EQ(q->cmds[0].source_ptype, intern(&f, "loc"));
  CHECK_EQ(q->cmds[0].arg_count, 1);
  CHECK_EQ(q->cmds[0].args[0].as.i, 2);
  CHECK_EQ(q->cmds[0].hit.type_id, intern(&f, "loc"));

  /* No translator at all: returns 0, not handled. */
  lk_ui_clear_translators(f.ui);
  memset(&ev, 0, sizeof(ev));
  ev.type = LK_EVENT_POINTER_DOWN;
  ev.target = f.node;
  CHECK_EQ(lk_translate_presentations(f.ui, lk_ui_tree(f.ui), f.node, &ev,
                                      hits, 2),
           0);
  CHECK_EQ(ev.handled, 0);

  END_TEST();
  pfix_destroy(&f);
}

/* The review's motivating case: file-location inside evaluated
 * output.  Middle fires the output's translator, secondary skips the
 * higher-precedence output candidate (no secondary translator for it)
 * and fires the location's — candidates, not winner-first. */
static void test_review_case_gesture_routes(void) {
  pres_fix f;
  lk_u32 wide;
  lk_u32 small;
  const lk_command_queue *q;

  BEGIN_TEST("present: review case — gesture picks the candidate");
  pfix_init(&f, "0123456789012345678901234567890123456789012345678901234", 0,
            1);

  wide = lk_annot_add(f.store, 0, 50, "output", NULL, NULL, 0);
  small = lk_annot_add(f.store, 10, 20, "links", NULL, NULL, 0);
  lk_annot_layer_set_priority(f.store, "output", 5);
  lk_annot_set_present(f.store, wide, intern(&f, "output"), lk_v_i32(1));
  lk_annot_set_present(f.store, small, intern(&f, "loc"), lk_v_i32(2));

  lk_ui_add_translator_s(f.ui, LK_EVENT_POINTER_DOWN, "output", 0, 0, 0,
                         (lk_u8)LK_POINTER_BUTTON_MIDDLE, "CmdA");
  lk_ui_add_translator_s(f.ui, LK_EVENT_POINTER_DOWN, "loc", 0, 0, 0,
                         (lk_u8)LK_POINTER_BUTTON_SECONDARY, "CmdB");

  /* middle at 15: the output candidate (priority 5) precedes and its
   * middle translator fires. */
  CHECK_EQ(pfix_click(&f, (lk_u8)LK_POINTER_BUTTON_MIDDLE, 0, 15), 1);
  q = lk_ui_commands(f.ui);
  CHECK_EQ(q->count, 1);
  CHECK_EQ(q->cmds[0].name, intern(&f, "CmdA"));
  CHECK_EQ(q->cmds[0].hit.locus[0], wide);
  CHECK_EQ(q->cmds[0].hit.locus[3], 15);
  CHECK_EQ(q->cmds[0].hit.locus_kind, intern(&f, "editor-range"));
  lk_ui_clear_commands(f.ui);

  /* secondary at 15: output has no secondary translator — the links
   * candidate fires despite lower precedence. */
  CHECK_EQ(pfix_click(&f, (lk_u8)LK_POINTER_BUTTON_SECONDARY, 0, 15), 1);
  q = lk_ui_commands(f.ui);
  CHECK_EQ(q->count, 1);
  CHECK_EQ(q->cmds[0].name, intern(&f, "CmdB"));
  CHECK_EQ(q->cmds[0].hit.locus[0], small);
  CHECK_EQ(q->cmds[0].hit.locus[1], 10);
  CHECK_EQ(q->cmds[0].hit.locus[2], 20);

  END_TEST();
  pfix_destroy(&f);
}

static void test_translated_click_pinned_behavior(void) {
  pres_fix f;
  lk_u32 a;
  lk_u32 sel_start;
  lk_u32 sel_end;

  BEGIN_TEST("present: translated click — no focus/cursor/capture");
  pfix_init(&f, "hello world here", 1, 1);

  a = lk_annot_add(f.store, 0, 16, "l", NULL, NULL, 0);
  lk_annot_set_present(f.store, a, intern(&f, "act"), lk_v_i32(1));
  lk_ui_add_translator_s(f.ui, LK_EVENT_POINTER_DOWN, "act", 0, 0, 0,
                         (lk_u8)LK_POINTER_BUTTON_MIDDLE, "Execute");

  /* focus the button, park the cursor at 3 */
  lk_focus_set(f.ui, lk_ui_tree(f.ui), intern(&f, "b"));
  lk_editor_set_cursor(f.ed, 3);

  CHECK_EQ(pfix_click(&f, (lk_u8)LK_POINTER_BUTTON_MIDDLE, 0, 10), 1);
  CHECK_EQ(lk_ui_commands(f.ui)->count, 1);

  /* pinned: nothing else moved */
  CHECK_EQ(lk_editor_cursor(f.ed), 3);
  CHECK_EQ(f.ui->focused_id, intern(&f, "b"));
  CHECK_EQ(lk_capture_current(f.ui), 0);
  CHECK_EQ(lk_editor_selection(f.ed, &sel_start, &sel_end), 0);

  END_TEST();
  pfix_destroy(&f);
}

static void test_unpresented_click_baseline(void) {
  pres_fix f;
  lk_u32 a;

  BEGIN_TEST("present: unmatched clicks keep today's behavior");
  pfix_init(&f, "hello world here", 0, 1);

  a = lk_annot_add(f.store, 0, 5, "l", NULL, NULL, 0);
  lk_annot_set_present(f.store, a, intern(&f, "act"), lk_v_i32(1));
  lk_ui_add_translator_s(f.ui, LK_EVENT_POINTER_DOWN, "act", 0, 0, 0,
                         (lk_u8)LK_POINTER_BUTTON_MIDDLE, "Execute");

  /* primary outside the annotated range: plain click-to-cursor,
   * focus + capture (then release). */
  CHECK_EQ(pfix_click(&f, (lk_u8)LK_POINTER_BUTTON_PRIMARY, 0, 10), 1);
  CHECK_EQ(lk_editor_cursor(f.ed), 10);
  CHECK_EQ(f.ui->focused_id, f.nid);
  CHECK_EQ(lk_capture_current(f.ui), f.nid);
  CHECK_EQ(lk_ui_commands(f.ui)->count, 0);

  {
    lk_event up;

    memset(&up, 0, sizeof(up));
    up.type = LK_EVENT_POINTER_UP;
    up.target = f.node;
    lk_event_route(f.ui, &up);
  }

  /* primary INSIDE the range: only a middle translator exists, so the
   * candidate does not fire and the click behaves normally. */
  CHECK_EQ(pfix_click(&f, (lk_u8)LK_POINTER_BUTTON_PRIMARY, 0, 2), 1);
  CHECK_EQ(lk_editor_cursor(f.ed), 2);
  CHECK_EQ(lk_ui_commands(f.ui)->count, 0);

  {
    lk_event up;

    memset(&up, 0, sizeof(up));
    up.type = LK_EVENT_POINTER_UP;
    up.target = f.node;
    lk_event_route(f.ui, &up);
  }

  /* middle with no match: no default editor action — bubbles. */
  CHECK_EQ(pfix_click(&f, (lk_u8)LK_POINTER_BUTTON_MIDDLE, 0, 10), 0);
  CHECK_EQ(lk_editor_cursor(f.ed), 2);
  CHECK_EQ(lk_capture_current(f.ui), 0);

  END_TEST();
  pfix_destroy(&f);
}

static void test_button_mods_discrimination_node_level(void) {
  lk_ui *ui = lk_ui_create(NULL);
  lk_tree *t = lk_ui_begin_frame(ui);
  lk_ix w = lk_tree_add_node_c(t, "w", UIK_WINDOW);
  lk_ix b = lk_tree_add_node_c(t, "b", UIK_BUTTON);
  lk_ix bi;
  lk_event ev;

  BEGIN_TEST("present: node-level button + mods discrimination");

  lk_tree_set_root(t, w);
  lk_tree_append_child(t, w, b);
  lk_tree_add_presentation_s(t, b, "act", lk_v_i32(7));
  lk_ui_end_frame(ui);
  bi = lk_tree_find_by_id(lk_ui_tree(ui), lk_intern_cid(ui->intern, "b"));

  lk_ui_add_translator_s(ui, LK_EVENT_POINTER_DOWN, "act", 0, 0, LK_MOD_CTRL,
                         (lk_u8)LK_POINTER_BUTTON_MIDDLE, "Execute");

  /* primary: no match */
  lk_event_init_pointer(&ev, LK_EVENT_POINTER_DOWN, 1, 1,
                        (lk_u8)LK_POINTER_BUTTON_PRIMARY);
  ev.target = bi;
  ev.mods = LK_MOD_CTRL;
  lk_event_route(ui, &ev);
  CHECK_EQ(lk_ui_commands(ui)->count, 0);

  /* middle without ctrl: exact-mods discipline — no match */
  lk_event_init_pointer(&ev, LK_EVENT_POINTER_DOWN, 1, 1,
                        (lk_u8)LK_POINTER_BUTTON_MIDDLE);
  ev.target = bi;
  lk_event_route(ui, &ev);
  CHECK_EQ(lk_ui_commands(ui)->count, 0);

  /* middle + ctrl: fires (node presentation, hit stays zeroed) */
  lk_event_init_pointer(&ev, LK_EVENT_POINTER_DOWN, 1, 1,
                        (lk_u8)LK_POINTER_BUTTON_MIDDLE);
  ev.target = bi;
  ev.mods = LK_MOD_CTRL;
  lk_event_route(ui, &ev);
  CHECK_EQ(lk_ui_commands(ui)->count, 1);
  CHECK_EQ(lk_ui_commands(ui)->cmds[0].args[0].as.i, 7);
  CHECK_EQ(lk_ui_commands(ui)->cmds[0].hit.type_id, 0);

  END_TEST();
  lk_ui_destroy(ui);
}

/* ---- resource-valued presentations ---- */

static void test_resource_presentation_roundtrip(void) {
  pres_fix f;
  static int payload = 1234;
  lk_resource_type ptype_desc;
  lk_resource_ref pref;
  lk_u32 a;
  const lk_command_queue *q;

  BEGIN_TEST("present: resource value round-trips through the command");
  pfix_init(&f, "hello world", 0, 1);

  memset(&ptype_desc, 0, sizeof(ptype_desc));
  ptype_desc.name = "payload";
  pref = lk_resource_register(lk_ui_resources(f.ui), &ptype_desc, &payload,
                              "p");

  a = lk_annot_add(f.store, 0, 11, "l", NULL, NULL, 0);
  lk_annot_set_present(f.store, a, intern(&f, "obj"), lk_v_resource(pref));
  lk_ui_add_translator_s(f.ui, LK_EVENT_POINTER_DOWN, "obj", 0, 0, 0, 0,
                         "Take");

  CHECK_EQ(pfix_click(&f, 0, 0, 5), 1);
  q = lk_ui_commands(f.ui);
  CHECK_EQ(q->count, 1);
  CHECK_EQ(q->cmds[0].args[0].tag, UIV_RESOURCE);
  CHECK(lk_resource_get(lk_ui_resources(f.ui),
                        lk_v_resource_ref(q->cmds[0].args[0]),
                        &ptype_desc) == &payload);

  END_TEST();
  pfix_destroy(&f);
}

/* ---- command dispatch arena ---- */

/* Synthetic source: one hit whose value is transient text pushed into
 * the queue arena at query time. */
static lk_ui *g_text_src_ui;

static lk_u32 text_source_query(void *ud, lk_u32 pos,
                                lk_presentation_hit *out, lk_u32 cap) {
  (void)ud;
  (void)pos;

  if (cap == 0) {
    return 0;
  }

  memset(&out[0], 0, sizeof(out[0]));
  out[0].type_id = lk_intern_cid(lk_ui_intern(g_text_src_ui), "path");
  out[0].value = lk_v_text(g_text_src_ui, "src/main.c:42", 13);

  return 1;
}

static int g_handler_text_ok;
static lk_ui *g_handler_ui;

static void text_cmd_handler(const lk_command *cmd, void *ud) {
  lk_str s = lk_command_arg_text(g_handler_ui, cmd, 0);

  (void)ud;
  g_handler_text_ok =
      (s.ptr != NULL && s.len == 13 && memcmp(s.ptr, "src/main.c:42", 13) == 0);
}

static void test_command_arena_text_lifetime(void) {
  pres_fix f;
  lk_presentation_source src;
  const lk_command_queue *q;
  lk_str s;
  pres_buf buf;

  BEGIN_TEST("present: UIV_TEXT arena — handler, clear, log dump");
  pfix_init(&f, "hello world", 0, 0);

  g_text_src_ui = f.ui;
  src.ud = NULL;
  src.query_at = text_source_query;
  lk_editor_set_presentation_source(f.ed, &src);

  lk_ui_add_translator_s(f.ui, LK_EVENT_POINTER_DOWN, "path", 0, 0, 0, 0,
                         "Plumb");
  g_handler_text_ok = 0;
  g_handler_ui = f.ui;
  lk_ui_set_command_handler(f.ui, text_cmd_handler, NULL);

  CHECK_EQ(pfix_click(&f, 0, 0, 3), 1);
  CHECK_EQ(g_handler_text_ok, 1); /* readable at handler time */

  /* still readable from the queue before clear */
  q = lk_ui_commands(f.ui);
  CHECK_EQ(q->count, 1);
  CHECK_EQ(q->cmds[0].args[0].tag, UIV_TEXT);
  s = lk_command_arg_text(f.ui, &q->cmds[0], 0);
  CHECK(s.ptr != NULL && s.len == 13);

  /* clear resets the queue arena... */
  lk_ui_clear_commands(f.ui);
  CHECK_EQ(f.ui->cmd_queue.bytes_count, 0);
  CHECK_EQ(f.ui->cmd_queue.count, 0);

  /* ...but the LOG kept its own copy: the dump still prints it. */
  memset(&buf, 0, sizeof(buf));
  lk_ui_dump_commands(f.ui, pres_buf_write, &buf);
  CHECK(strstr(buf.data, "src/main.c:42") != NULL);
  CHECK(strstr(buf.data, "Plumb") != NULL);

  /* and the log command resolves through the log arena */
  {
    lk_u32 nlog = 0;
    const lk_command *log = lk_ui_command_log(f.ui, &nlog);

    CHECK_EQ(nlog, 1);
    s = lk_command_arg_text(f.ui, &log[0], 0);
    CHECK(s.ptr != NULL && s.len == 13 &&
          memcmp(s.ptr, "src/main.c:42", 13) == 0);
  }

  END_TEST();
  pfix_destroy(&f);
}

/* ---- stale locus ---- */

static void test_stale_locus_detectable(void) {
  pres_fix f;
  lk_u32 a;
  lk_command captured;
  lk_revision now;

  BEGIN_TEST("present: locus revision goes stale detectably");
  pfix_init(&f, "hello world", 0, 1);

  a = lk_annot_add(f.store, 0, 11, "l", NULL, NULL, 0);
  lk_annot_set_present(f.store, a, intern(&f, "act"), lk_v_i32(1));
  lk_ui_add_translator_s(f.ui, LK_EVENT_POINTER_DOWN, "act", 0, 0, 0, 0,
                         "Do");

  CHECK_EQ(pfix_click(&f, 0, 0, 5), 1);
  captured = lk_ui_commands(f.ui)->cmds[0];

  /* the hit carried the revision at dispatch */
  now = lk_doc_revision(f.doc);
  CHECK_EQ(captured.hit.locus[4], now.hi);
  CHECK_EQ(captured.hit.locus[5], now.lo);

  /* edit the document: the hit's words no longer match */
  lk_doc_insert(f.doc, 0, "x", 1);
  now = lk_doc_revision(f.doc);
  CHECK(captured.hit.locus[4] != now.hi || captured.hit.locus[5] != now.lo);

  END_TEST();
  pfix_destroy(&f);
}

/* ---- lk_editor_pos_at ---- */

static void test_editor_pos_at_contract(void) {
  pres_fix f;
  lk_u32 pos = 777;
  lk_rect r;

  BEGIN_TEST("present: lk_editor_pos_at pinned contract");

  /* before any layout: 0 */
  {
    lk_document *doc = lk_doc_from_str(NULL, NULL, NULL, "ab", 2);
    lk_editor *ed = lk_editor_new(NULL, NULL, NULL, doc, NULL);

    CHECK_EQ(lk_editor_pos_at(ed, 0, 0, &pos), 0);
    lk_editor_destroy(ed);
    lk_doc_destroy(doc);
  }

  pfix_init(&f, "hello\nworld wide", 0, 0);
  r = f.rects[f.node];

  /* exact stub geometry: 8 px per char, 16 px lines */
  CHECK_EQ(lk_editor_pos_at(f.ed, r.x + 3, r.y + 8, &pos), 1);
  CHECK_EQ(pos, 0);
  CHECK_EQ(lk_editor_pos_at(f.ed, r.x + 3 * 8 + 3, r.y + 8, &pos), 1);
  CHECK_EQ(pos, 3);
  CHECK_EQ(lk_editor_pos_at(f.ed, r.x + 2 * 8 + 3, r.y + 16 + 8, &pos), 1);
  CHECK_EQ(pos, 8); /* line 1 starts at byte 6: 6 + 2 */

  /* outside the laid-out rect: 0, NO clamping */
  CHECK_EQ(lk_editor_pos_at(f.ed, r.x - 1, r.y + 8, &pos), 0);
  CHECK_EQ(lk_editor_pos_at(f.ed, r.x + r.w, r.y + 8, &pos), 0);
  CHECK_EQ(lk_editor_pos_at(f.ed, r.x + 3, r.y - 1, &pos), 0);
  CHECK_EQ(lk_editor_pos_at(f.ed, r.x + 3, r.y + r.h, &pos), 0);

  END_TEST();
  pfix_destroy(&f);
}

/* ---- runner ---- */

void lk_present_run_tests(void) {
  printf("\nlk interior presentation tests (weft-surface S1):\n");
  test_annot_set_present_basics();
  test_annot_presentations_precedence();
  test_annot_presentations_serial_tiebreak();
  test_annot_release_hook_counts();
  test_annot_release_hook_delete_sweep();
  test_translate_presentations_direct();
  test_review_case_gesture_routes();
  test_translated_click_pinned_behavior();
  test_unpresented_click_baseline();
  test_button_mods_discrimination_node_level();
  test_resource_presentation_roundtrip();
  test_command_arena_text_lifetime();
  test_stale_locus_detectable();
  test_editor_pos_at_contract();
}
