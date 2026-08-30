/*
 * lk-styled-text-test.c -- lk_spans + UIK_STYLED_TEXT + the fit_height
 * engine hook (docs/styled-text.md).
 *
 * Geometry runs against the stub text backend (8 px per codepoint,
 * 16 px line height, baseline 12).  Pinned here: the span set's
 * ordering / overlap contract and release hook; the row breaker's
 * policy (character fit, word break after whitespace, tabs, the
 * progress guarantee); height-for-width propagating through columns
 * and scroll; runs split at span boundaries with bg before and
 * underline after; the pointer -> position -> presentation path.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <lk.h>

#include "core/lk-styled-text.h"
#include "lk-test-harness.h"

/* ---- helpers ---- */

static lk_rect *layout_ui(lk_ui *ui, lk_i32 vw, lk_i32 vh,
                          lk_style **out_styles) {
  lk_layout_cfg cfg;
  lk_rect *rects;
  lk_style *styles;
  const lk_tree *cur = lk_ui_tree(ui);

  styles = (lk_style *)malloc(sizeof(lk_style) * cur->node_count);
  rects = (lk_rect *)malloc(sizeof(lk_rect) * cur->node_count);

  if (!rects || !styles) {
    free(rects);
    free(styles);
    return NULL;
  }

  lk_style_resolve(lk_ui_theme(ui), cur, NULL, styles);
  memset(&cfg, 0, sizeof(cfg));
  cfg.text = lk_text_backend_stub();
  cfg.viewport_w = vw;
  cfg.viewport_h = vh;
  cfg.styles = styles;
  cfg.state = lk_ui_state(ui);
  cfg.geom = lk_ui_geom(ui);

  if (!lk_layout(cur, &cfg, rects)) {
    free(rects);
    free(styles);
    return NULL;
  }

  *out_styles = styles;
  return rects;
}

static lk_ix find(lk_ui *ui, const char *id) {
  return lk_tree_find_by_id(lk_ui_tree(ui), lk_intern_cid(ui->intern, id));
}

static lk_color rgb(lk_u8 r, lk_u8 g, lk_u8 b) {
  lk_color c;
  c.r = r;
  c.g = g;
  c.b = b;
  c.a = 255;
  return c;
}

static int count_op(const lk_render_list *rl, int op) {
  lk_u32 i;
  int n = 0;

  for (i = 0; i < rl->count; i++) {
    if (rl->cmds[i].op == op) {
      n++;
    }
  }

  return n;
}

static const lk_render_cmd *nth_op(const lk_render_list *rl, int op, int k) {
  lk_u32 i;

  for (i = 0; i < rl->count; i++) {
    if (rl->cmds[i].op == op) {
      if (k == 0) {
        return &rl->cmds[i];
      }

      k--;
    }
  }

  return NULL;
}

static int run_is(const lk_render_list *rl, const lk_render_cmd *c,
                  const char *s) {
  return c && c->run_len == strlen(s) &&
         memcmp(rl->bytes + c->run_off, s, c->run_len) == 0;
}

/* Rows of `text` at `width` in `mode` as "a|b|c" (row texts joined). */
static void rows_str(const char *text, lk_i32 width, lk_wrap_mode mode,
                     char *out, size_t cap) {
  lk_u32 k = 0;
  lk_u32 s, e;
  size_t n = 0;

  out[0] = 0;

  while (lk_styled_text_row(lk_text_backend_stub(), 0, 0, lk_str_c(text), width,
                            mode, k, &s, &e)) {
    if (k > 0 && n + 1 < cap) {
      out[n++] = '|';
    }

    if (n + (e - s) + 1 < cap) {
      memcpy(out + n, text + s, e - s);
      n += e - s;
    }

    out[n] = 0;
    k++;
  }
}

static int rows_eq(const char *text, lk_i32 width, lk_wrap_mode mode,
                   const char *want) {
  char buf[256];

  rows_str(text, width, mode, buf, sizeof(buf));

  if (strcmp(buf, want) != 0) {
    printf("\n    rows(\"%s\", %d) = \"%s\", want \"%s\"\n", text, (int)width,
           buf, want);
    return 0;
  }

  return 1;
}

/* ---- spans object ---- */

static int g_released;

static void count_release(void *ud, lk_value v) {
  (void)v;
  (*(int *)ud)++;
}

static void test_spans_object(void) {
  lk_spans *s = lk_spans_new(NULL, NULL, NULL);
  lk_color red = rgb(255, 0, 0);
  lk_color none;
  const lk_text_span *sp;
  lk_u32 tid;
  lk_value v;

  BEGIN_TEST("spans: ordered insert, overlap rejected, release hook");
  memset(&none, 0, sizeof(none));
  g_released = 0;
  lk_spans_set_release(s, count_release, &g_released);

  CHECK_EQ(lk_spans_add(s, 10, 20, red, none, LK_SPAN_FG), 1);
  CHECK_EQ(lk_spans_add(s, 0, 5, red, none, LK_SPAN_FG), 1);   /* before */
  CHECK_EQ(lk_spans_add(s, 30, 40, red, none, LK_SPAN_BG), 1); /* after */
  CHECK_EQ(lk_spans_add(s, 20, 30, red, none, 0), 1);          /* touching */
  CHECK_EQ(lk_spans_count(s), 4);

  /* Overlaps in every direction are refused, list unchanged. */
  CHECK_EQ(lk_spans_add(s, 15, 25, red, none, 0), 0);
  CHECK_EQ(lk_spans_add(s, 0, 100, red, none, 0), 0);
  CHECK_EQ(lk_spans_add(s, 12, 13, red, none, 0), 0);
  CHECK_EQ(lk_spans_add(s, 5, 5, red, none, 0), 0); /* empty */
  CHECK_EQ(lk_spans_add(s, 9, 3, red, none, 0), 0); /* reversed */
  CHECK_EQ(lk_spans_count(s), 4);

  sp = lk_spans_get(s, 0);
  CHECK(sp && sp->start == 0 && sp->end == 5);
  sp = lk_spans_get(s, 1);
  CHECK(sp && sp->start == 10 && sp->end == 20 && sp->fg.r == 255);
  sp = lk_spans_get(s, 3);
  CHECK(sp && sp->start == 30 && (sp->flags & LK_SPAN_BG));
  CHECK(lk_spans_get(s, 4) == NULL);

  /* Presentations: type 0 refused; a presented entry is found at a
   * position and reported by present_get; plain entries are not. */
  CHECK_EQ(lk_spans_add_present(s, 50, 60, 0, lk_v_i32(1)), 0);
  CHECK_EQ(lk_spans_add_present(s, 50, 60, 7, lk_v_i32(42)), 1);
  CHECK_EQ(lk_spans_present_get(s, 0, &tid, &v), 0);
  CHECK_EQ(lk_spans_present_get(s, 4, &tid, &v), 1);
  CHECK_EQ(tid, 7);
  CHECK(v.tag == UIV_I32 && v.as.i == 42);

  {
    lk_presentation_hit hits[4];

    CHECK_EQ(lk_spans_present_at(s, 55, hits, 4), 1);
    CHECK_EQ(hits[0].type_id, 7);
    CHECK_EQ(hits[0].locus[0], 50);
    CHECK_EQ(hits[0].locus[1], 60);
    CHECK_EQ(lk_spans_present_at(s, 60, hits, 4), 0); /* half-open */
    CHECK_EQ(lk_spans_present_at(s, 12, hits, 4), 0); /* no ptype */
  }

  /* An identical range merges: style onto a presented entry, a new
   * presentation onto it (the old value released), still one entry. */
  CHECK_EQ(lk_spans_add(s, 50, 60, red, none, LK_SPAN_UNDERLINE), 1);
  CHECK_EQ(lk_spans_count(s), 5);
  sp = lk_spans_get(s, 4);
  CHECK(sp && (sp->flags & LK_SPAN_UNDERLINE) && !(sp->flags & LK_SPAN_FG));
  CHECK_EQ(lk_spans_present_get(s, 4, &tid, &v), 1);
  CHECK_EQ(lk_spans_add_present(s, 50, 60, 8, lk_v_i32(43)), 1);
  CHECK_EQ(g_released, 1);
  CHECK_EQ(lk_spans_present_get(s, 4, &tid, &v), 1);
  CHECK(tid == 8 && v.as.i == 43);
  CHECK_EQ(lk_spans_count(s), 5);

  /* clear releases the presented value exactly once more; destroy of
   * an empty set releases nothing. */
  lk_spans_clear(s);
  CHECK_EQ(g_released, 2);
  CHECK_EQ(lk_spans_count(s), 0);
  CHECK_EQ(lk_spans_add_present(s, 1, 2, 7, lk_v_i32(1)), 1);
  lk_spans_destroy(s);
  CHECK_EQ(g_released, 3);

  END_TEST();
}

/* ---- rows ---- */

static void test_rows_policy(void) {
  BEGIN_TEST("styled text: row breaker (char, word, tab, progress)");

  /* NONE: one row per line, whatever the width. */
  CHECK(rows_eq("abc def", 8, LK_WRAP_NONE, "abc def"));
  CHECK(rows_eq("ab\ncd", 8, LK_WRAP_NONE, "ab|cd"));

  /* CHARACTER: 40 px = 5 codepoints per row. */
  CHECK(rows_eq("abcdefghij", 40, LK_WRAP_CHARACTER, "abcde|fghij"));
  CHECK(rows_eq("abcdefghijk", 40, LK_WRAP_CHARACTER, "abcde|fghij|k"));
  /* UTF-8: 5 two-byte codepoints fit 40 px. */
  CHECK(rows_eq("\xc3\xa9\xc3\xa9\xc3\xa9\xc3\xa9\xc3\xa9\xc3\xa9", 40,
                LK_WRAP_CHARACTER,
                "\xc3\xa9\xc3\xa9\xc3\xa9\xc3\xa9\xc3\xa9|\xc3\xa9"));

  /* WORD: break after the last space; trailing space stays up. */
  CHECK(rows_eq("aaa bbb ccc", 56, LK_WRAP_WORD, "aaa bbb |ccc"));
  CHECK(rows_eq("aaa bbb ccc", 40, LK_WRAP_WORD, "aaa |bbb |ccc"));
  /* an unbreakable run falls back to the character floor */
  CHECK(rows_eq("abcdefghij kl", 40, LK_WRAP_WORD, "abcde|fghij |kl"));
  /* exact fit does not break */
  CHECK(rows_eq("abcde", 40, LK_WRAP_WORD, "abcde"));
  /* lines break independently; an empty line is a row */
  CHECK(rows_eq("aaa bbb\n\ncc", 40, LK_WRAP_WORD, "aaa |bbb||cc"));

  /* Progress guarantee: too narrow for one glyph still advances. */
  CHECK(rows_eq("abc", 4, LK_WRAP_CHARACTER, "a|b|c"));
  CHECK(rows_eq("abc", 4, LK_WRAP_WORD, "a|b|c"));

  /* Tabs: stops every 32 px (4 spaces).  "a\tb" = 8 + stop 32 + 8. */
  CHECK(rows_eq("a\tb", 40, LK_WRAP_CHARACTER, "a\tb"));
  CHECK(rows_eq("a\tb", 39, LK_WRAP_CHARACTER, "a\t|b"));
  /* a tab that would land past the width breaks first: at 48, "abcd"
   * (32) + a stop at 64 breaks, then the tab stops at 32 and x fits */
  CHECK(rows_eq("abcd\tx", 48, LK_WRAP_CHARACTER, "abcd|\tx"));
  /* trailing spaces hang past the width rather than wrapping alone */
  CHECK(rows_eq("aaa bbb   ", 56, LK_WRAP_WORD, "aaa bbb   "));

  CHECK_EQ(lk_styled_text_row_count(lk_text_backend_stub(), 0, 0,
                                    lk_str_c("aaa bbb ccc"), 40, LK_WRAP_WORD),
           3);
  CHECK_EQ(lk_styled_text_row_count(lk_text_backend_stub(), 0, 0, lk_str_c(""),
                                    40, LK_WRAP_WORD),
           1);

  END_TEST();
}

/* ---- fit_height through the engine ---- */

/* window > column "outer" (align start) > column "inner" (w 80,
 * stretch) > [styled_text "st" (text, mode), label "after"] */
static void build_column_frame(lk_ui *ui, const char *text, lk_i32 mode,
                               lk_i32 inner_w) {
  lk_tree *t = lk_ui_begin_frame(ui);
  lk_ix win = lk_tree_add_node_c(t, "w", UIK_WINDOW);
  lk_ix outer = lk_tree_add_node_c(t, "outer", UIK_COLUMN);
  lk_ix inner = lk_tree_add_node_c(t, "inner", UIK_COLUMN);
  lk_ix st = lk_tree_add_node_c(t, "st", UIK_STYLED_TEXT);
  lk_ix after = lk_tree_add_node_c(t, "after", UIK_LABEL);

  lk_tree_set_root(t, win);
  lk_tree_append_child(t, win, outer);
  lk_tree_append_child(t, outer, inner);
  lk_tree_append_child(t, inner, st);
  lk_tree_append_child(t, inner, after);
  lk_tree_add_prop(t, outer, UIP_ALIGN, lk_v_i32(LK_ALIGN_START));
  lk_tree_add_prop(t, inner, UIP_W, lk_v_i32(inner_w));
  lk_tree_add_prop(t, inner, UIP_ALIGN, lk_v_i32(LK_ALIGN_STRETCH));
  lk_tree_add_prop(t, st, UIP_TEXT, lk_v_cstr(t->intern, text));
  lk_tree_add_prop(t, st, UIP_WRAP, lk_v_i32(mode));
  lk_tree_add_prop(t, after, UIP_TEXT, lk_v_cstr(t->intern, "after"));
  lk_ui_end_frame(ui);
}

static void test_fit_column(void) {
  lk_ui *ui = lk_ui_create(NULL);
  lk_rect *r;
  lk_style *styles;

  BEGIN_TEST("styled text: height-for-width through nested columns");

  /* 80 px = 10 cp: "aaaa bbbb " fills row 1 exactly, "cccc" row 2. */
  build_column_frame(ui, "aaaa bbbb cccc", LK_WRAP_WORD, 80);
  r = layout_ui(ui, 640, 480, &styles);
  CHECK(r != NULL);

  if (r) {
    lk_ix st = find(ui, "st");
    lk_ix after = find(ui, "after");
    lk_ix inner = find(ui, "inner");

    CHECK_EQ(r[st].w, 80);
    CHECK_EQ(r[st].h, 32);              /* two rows */
    CHECK_EQ(r[after].y, r[st].y + 32); /* the sibling sits below both */
    CHECK_EQ(r[inner].h, 48);           /* the column grew to fit */
    free(r);
    free(styles);
  }

  /* wrap none: one row, the label right under it; the text clips. */
  build_column_frame(ui, "aaaa bbbb cccc", LK_WRAP_NONE, 80);
  r = layout_ui(ui, 640, 480, &styles);
  CHECK(r != NULL);

  if (r) {
    lk_ix st = find(ui, "st");
    lk_ix after = find(ui, "after");

    CHECK_EQ(r[st].h, 16);
    CHECK_EQ(r[after].y, r[st].y + 16);
    free(r);
    free(styles);
  }

  /* Narrower: 40 px = 5 cp -> "aaaa |bbbb |cccc" = 3 rows. */
  build_column_frame(ui, "aaaa bbbb cccc", LK_WRAP_WORD, 40);
  r = layout_ui(ui, 640, 480, &styles);
  CHECK(r != NULL);

  if (r) {
    lk_ix st = find(ui, "st");

    CHECK_EQ(r[st].h, 48);
    free(r);
    free(styles);
  }

  END_TEST();
  lk_ui_destroy(ui);
}

static void test_fit_scroll(void) {
  lk_ui *ui = lk_ui_create(NULL);
  lk_rect *r;
  lk_style *styles;

  BEGIN_TEST("styled text: scroll lays out wrapped rows, scrolls them");

  /* window > column(start) > scroll (w 80, h 24) > styled_text: at
   * 80 px "aaaa bbbbb cccc" is 2 rows ("aaaa bbbbb " hangs its space)
   * = 32 px > 24 -> scrollable; with the bar's 6 px gone it refits at
   * 74 px -> "aaaa |bbbbb |cccc" = 3 rows = 48 px. */
  {
    lk_tree *t = lk_ui_begin_frame(ui);
    lk_ix win = lk_tree_add_node_c(t, "w", UIK_WINDOW);
    lk_ix col = lk_tree_add_node_c(t, "col", UIK_COLUMN);
    lk_ix sc = lk_tree_add_node_c(t, "sc", UIK_SCROLL);
    lk_ix st = lk_tree_add_node_c(t, "st", UIK_STYLED_TEXT);

    lk_tree_set_root(t, win);
    lk_tree_append_child(t, win, col);
    lk_tree_append_child(t, col, sc);
    lk_tree_append_child(t, sc, st);
    lk_tree_add_prop(t, col, UIP_ALIGN, lk_v_i32(LK_ALIGN_START));
    lk_tree_add_prop(t, sc, UIP_W, lk_v_i32(80));
    lk_tree_add_prop(t, sc, UIP_H, lk_v_i32(24));
    lk_tree_add_prop(t, sc, UIP_PADDING, lk_v_i32(0));
    lk_tree_add_prop(t, st, UIP_TEXT, lk_v_cstr(t->intern, "aaaa bbbbb cccc"));
    lk_ui_end_frame(ui);
  }

  r = layout_ui(ui, 640, 480, &styles);
  CHECK(r != NULL);

  if (r) {
    lk_ix sc = find(ui, "sc");
    lk_ix st = find(ui, "st");
    lk_widget_geom *g = lk_ui_geom(ui);

    CHECK_EQ(r[st].w, 74);
    CHECK_EQ(r[st].h, 48);
    CHECK(g[sc].scroll.max == 48 - 24);
    free(r);
    free(styles);
  }

  END_TEST();
  lk_ui_destroy(ui);
}

/* ---- render ---- */

static void test_render_runs(void) {
  lk_ui *ui = lk_ui_create(NULL);
  lk_spans *sp = lk_spans_new(NULL, NULL, NULL);
  lk_resources *rs = lk_ui_resources(ui);
  lk_resource_ref ref = lk_resource_register(rs, lk_spans_type(), sp, "sp");
  lk_rect *r;
  lk_style *styles;
  lk_render_list rl;
  lk_color none;

  BEGIN_TEST("styled text: runs split at spans, bg before, underline after");
  memset(&none, 0, sizeof(none));

  /* "hello world": [0,5) red fg; [6,11) blue bg + underline. */
  lk_spans_add(sp, 0, 5, rgb(255, 0, 0), none, LK_SPAN_FG);
  lk_spans_add(sp, 6, 11, none, rgb(0, 0, 255), LK_SPAN_BG | LK_SPAN_UNDERLINE);

  {
    lk_tree *t = lk_ui_begin_frame(ui);
    lk_ix win = lk_tree_add_node_c(t, "w", UIK_WINDOW);
    lk_ix col = lk_tree_add_node_c(t, "col", UIK_COLUMN);
    lk_ix st = lk_tree_add_node_c(t, "st", UIK_STYLED_TEXT);

    lk_tree_set_root(t, win);
    lk_tree_append_child(t, win, col);
    lk_tree_append_child(t, col, st);
    lk_tree_add_prop(t, col, UIP_ALIGN, lk_v_i32(LK_ALIGN_START));
    lk_tree_add_prop(t, col, UIP_PADDING, lk_v_i32(10));
    lk_tree_add_prop(t, st, UIP_TEXT, lk_v_cstr(t->intern, "hello world"));
    lk_tree_add_prop(t, st, UIP_WRAP, lk_v_i32(LK_WRAP_NONE));
    lk_tree_add_prop(t, st, UIP_SPANS, lk_v_resource(ref));
    lk_ui_end_frame(ui);
  }

  r = layout_ui(ui, 640, 480, &styles);
  CHECK(r != NULL);

  if (r) {
    const lk_tree *cur = lk_ui_tree(ui);
    lk_ix st = find(ui, "st");
    const lk_render_cmd *c;

    CHECK_EQ(r[st].x, 10);
    CHECK_EQ(r[st].w, 88);

    memset(&rl, 0, sizeof(rl));
    lk_render_build(cur, r, styles, lk_ui_state(ui), lk_ui_geom(ui), &rl);

    /* window clip + the text's own */
    CHECK_EQ(count_op(&rl, LK_ROP_CLIP_BEGIN), 2);
    CHECK_EQ(count_op(&rl, LK_ROP_CLIP_END), 2);
    CHECK_EQ(count_op(&rl, LK_ROP_DRAW_RUN), 3);

    c = nth_op(&rl, LK_ROP_DRAW_RUN, 0);
    CHECK(run_is(&rl, c, "hello"));
    CHECK(c && c->color.r == 255 && c->color.g == 0);
    CHECK(c && c->rect.x == 10 && c->rect.y == 10);
    c = nth_op(&rl, LK_ROP_DRAW_RUN, 1);
    CHECK(run_is(&rl, c, " "));
    c = nth_op(&rl, LK_ROP_DRAW_RUN, 2);
    CHECK(run_is(&rl, c, "world"));
    CHECK(c && c->rect.x == 10 + 48);
    CHECK(c && c->color.r == styles[st].fg.r); /* no FG flag: node fg */

    /* The bg fill precedes the runs; the underline follows them. */
    {
      lk_u32 i;
      int bg_at = -1, first_run = -1, ul_at = -1;

      for (i = 0; i < rl.count; i++) {
        const lk_render_cmd *k = &rl.cmds[i];

        if (k->op == LK_ROP_FILL_RECT && k->color.b == 255 && k->rect.h == 16) {
          bg_at = (int)i;
          CHECK_EQ(k->rect.x, 58);
          CHECK_EQ(k->rect.w, 40);
        }

        if (k->op == LK_ROP_DRAW_RUN && first_run < 0) {
          first_run = (int)i;
        }

        if (k->op == LK_ROP_FILL_RECT && k->rect.h == 1) {
          ul_at = (int)i;
          CHECK_EQ(k->rect.x, 58);
          CHECK_EQ(k->rect.w, 40);
          CHECK_EQ(k->rect.y, 10 + 12 + 1);
        }
      }

      CHECK(bg_at >= 0 && first_run > bg_at);
      CHECK(ul_at > first_run);
    }

    lk_render_list_destroy(&rl);

    /* NULL geom: no backend for the rows -> one unwrapped run per
     * line, still styled (three runs), never a crash. */
    memset(&rl, 0, sizeof(rl));
    lk_render_build(cur, r, styles, lk_ui_state(ui), NULL, &rl);
    CHECK_EQ(count_op(&rl, LK_ROP_DRAW_RUN), 3);
    lk_render_list_destroy(&rl);

    free(r);
    free(styles);
  }

  END_TEST();
  lk_resource_release(rs, ref);
  lk_spans_destroy(sp);
  lk_ui_destroy(ui);
}

static void test_render_wrapped_span(void) {
  lk_ui *ui = lk_ui_create(NULL);
  lk_spans *sp = lk_spans_new(NULL, NULL, NULL);
  lk_resources *rs = lk_ui_resources(ui);
  lk_resource_ref ref = lk_resource_register(rs, lk_spans_type(), sp, "sp");
  lk_rect *r;
  lk_style *styles;
  lk_render_list rl;
  lk_color none;

  BEGIN_TEST("styled text: a span crossing a row break styles both rows");
  memset(&none, 0, sizeof(none));

  /* 40 px rows: "aaaa |bbbb |cccc"; span [2, 8) covers "aa " + "bb". */
  lk_spans_add(sp, 2, 8, rgb(1, 2, 3), none, LK_SPAN_FG);
  build_column_frame(ui, "aaaa bbbb cccc", LK_WRAP_WORD, 40);
  {
    lk_tree *t = lk_ui_begin_frame(ui);
    lk_ix win = lk_tree_add_node_c(t, "w", UIK_WINDOW);
    lk_ix col = lk_tree_add_node_c(t, "col", UIK_COLUMN);
    lk_ix st = lk_tree_add_node_c(t, "st", UIK_STYLED_TEXT);

    lk_tree_set_root(t, win);
    lk_tree_append_child(t, win, col);
    lk_tree_append_child(t, col, st);
    lk_tree_add_prop(t, col, UIP_ALIGN, lk_v_i32(LK_ALIGN_START));
    lk_tree_add_prop(t, st, UIP_W, lk_v_i32(40));
    lk_tree_add_prop(t, st, UIP_TEXT, lk_v_cstr(t->intern, "aaaa bbbb cccc"));
    lk_tree_add_prop(t, st, UIP_SPANS, lk_v_resource(ref));
    lk_ui_end_frame(ui);
  }

  r = layout_ui(ui, 640, 480, &styles);
  CHECK(r != NULL);

  if (r) {
    const lk_tree *cur = lk_ui_tree(ui);
    lk_ix st = find(ui, "st");
    const lk_render_cmd *c;

    CHECK_EQ(r[st].h, 48);
    memset(&rl, 0, sizeof(rl));
    lk_render_build(cur, r, styles, lk_ui_state(ui), lk_ui_geom(ui), &rl);

    /* [2, 8) = "aa " + "bbb": row 1: "aa" + "aa "; row 2: "bbb" +
     * "b "; row 3: "cccc" */
    CHECK_EQ(count_op(&rl, LK_ROP_DRAW_RUN), 5);
    c = nth_op(&rl, LK_ROP_DRAW_RUN, 0);
    CHECK(run_is(&rl, c, "aa") && c->color.r == styles[st].fg.r);
    c = nth_op(&rl, LK_ROP_DRAW_RUN, 1);
    CHECK(run_is(&rl, c, "aa ") && c->color.r == 1 && c->color.b == 3);
    c = nth_op(&rl, LK_ROP_DRAW_RUN, 2);
    CHECK(run_is(&rl, c, "bbb") && c->color.r == 1);
    CHECK(c && c->rect.y == r[st].y + 16);
    c = nth_op(&rl, LK_ROP_DRAW_RUN, 3);
    CHECK(run_is(&rl, c, "b ") && c->color.r == styles[st].fg.r);
    c = nth_op(&rl, LK_ROP_DRAW_RUN, 4);
    CHECK(run_is(&rl, c, "cccc"));
    lk_render_list_destroy(&rl);
    free(r);
    free(styles);
  }

  END_TEST();
  lk_resource_release(rs, ref);
  lk_spans_destroy(sp);
  lk_ui_destroy(ui);
}

/* ---- pointer -> position -> presentation ---- */

static void test_pos_and_presentation(void) {
  lk_ui *ui = lk_ui_create(NULL);
  lk_spans *sp = lk_spans_new(NULL, NULL, NULL);
  lk_resources *rs = lk_ui_resources(ui);
  lk_resource_ref ref = lk_resource_register(rs, lk_spans_type(), sp, "sp");
  lk_layout_cfg cfg;
  lk_style *styles;
  const lk_tree *cur;
  lk_u32 pos = 999;
  lk_event ev;
  lk_node_id st_id;

  BEGIN_TEST("styled text: pos_at + presented range -> command via matcher");

  lk_ui_add_translator_s(ui, LK_EVENT_POINTER_DOWN, "word", 0, 0, 0, 0, "Look");

  {
    lk_tree *t = lk_ui_begin_frame(ui);
    lk_ix win = lk_tree_add_node_c(t, "w", UIK_WINDOW);
    lk_ix col = lk_tree_add_node_c(t, "col", UIK_COLUMN);
    lk_ix st = lk_tree_add_node_c(t, "st", UIK_STYLED_TEXT);

    lk_tree_set_root(t, win);
    lk_tree_append_child(t, win, col);
    lk_tree_append_child(t, col, st);
    lk_tree_add_prop(t, col, UIP_ALIGN, lk_v_i32(LK_ALIGN_START));
    lk_tree_add_prop(t, col, UIP_PADDING, lk_v_i32(10));
    lk_tree_add_prop(t, st, UIP_W, lk_v_i32(40)); /* rows: "aaaa |bbbb |cccc" */
    lk_tree_add_prop(t, st, UIP_TEXT, lk_v_cstr(t->intern, "aaaa bbbb cccc"));
    lk_tree_add_prop(t, st, UIP_SPANS, lk_v_resource(ref));
    lk_ui_end_frame(ui);
  }

  cur = lk_ui_tree(ui);
  st_id = lk_intern_cid(ui->intern, "st");
  /* "bbbb" = [5, 9) presents word -> the interned value "bbbb" */
  lk_spans_add_present(sp, 5, 9, lk_intern_cid(ui->intern, "word"),
                       lk_v_cstr(ui->intern, "bbbb"));

  /* Lay out into the ui-owned rects so lk_node_rect answers. */
  styles = (lk_style *)malloc(sizeof(lk_style) * cur->node_count);
  lk_style_resolve(lk_ui_theme(ui), cur, NULL, styles);
  memset(&cfg, 0, sizeof(cfg));
  cfg.text = lk_text_backend_stub();
  cfg.viewport_w = 640;
  cfg.viewport_h = 480;
  cfg.styles = styles;
  cfg.state = lk_ui_state(ui);
  cfg.geom = lk_ui_geom(ui);
  lk_ui_set_text_backend(ui, lk_text_backend_stub());
  CHECK(lk_layout(cur, &cfg, lk_ui_rects(ui)) == 1);

  /* Row 2 starts at y 26; x 10 + 2 cp = 26 -> byte 5 + 2 = 7. */
  CHECK_EQ(lk_styled_text_pos_at(ui, st_id, 27, 30, &pos), 1);
  CHECK_EQ(pos, 7);
  CHECK_EQ(lk_styled_text_pos_at(ui, st_id, 11, 12, &pos), 1);
  CHECK_EQ(pos, 0);
  CHECK_EQ(lk_styled_text_pos_at(ui, st_id, 5, 5, &pos), 0); /* outside */
  CHECK_EQ(
      lk_styled_text_pos_at(ui, lk_intern_cid(ui->intern, "col"), 27, 30, &pos),
      0); /* not a styled text */

  /* Click on "bbbb": the presentation translates to Look with the
   * value as arg 0 and a text-range locus. */
  memset(&ev, 0, sizeof(ev));
  ev.type = LK_EVENT_POINTER_DOWN;
  ev.target = find(ui, "st");
  ev.data.pointer.x = 27;
  ev.data.pointer.y = 30;
  ev.data.pointer.button = LK_POINTER_BUTTON_PRIMARY;
  lk_event_route(ui, &ev);
  CHECK_EQ((unsigned)ev.handled, 1u);
  CHECK_EQ(lk_ui_commands(ui)->count, 1u);

  if (lk_ui_commands(ui)->count == 1) {
    const lk_command *cmd = &lk_ui_commands(ui)->cmds[0];
    lk_str kind = lk_intern_str(ui->intern, cmd->hit.locus_kind);

    CHECK_EQ(cmd->name, lk_intern_cid(ui->intern, "Look"));
    CHECK(kind.len == 10 && memcmp(kind.ptr, "text-range", 10) == 0);
    CHECK_EQ(cmd->hit.locus[0], 5);
    CHECK_EQ(cmd->hit.locus[1], 9);
    CHECK_EQ(cmd->hit.locus[2], 7);
    CHECK(cmd->arg_count >= 1 && cmd->args[0].tag == UIV_STR);
  }

  /* Click on "cccc" (no presentation): bubbles, no command. */
  lk_ui_clear_commands(ui);
  memset(&ev, 0, sizeof(ev));
  ev.type = LK_EVENT_POINTER_DOWN;
  ev.target = find(ui, "st");
  ev.data.pointer.x = 20;
  ev.data.pointer.y = 46;
  ev.data.pointer.button = LK_POINTER_BUTTON_PRIMARY;
  lk_event_route(ui, &ev);
  CHECK_EQ((unsigned)ev.handled, 0u);
  CHECK_EQ(lk_ui_commands(ui)->count, 0u);

  free(styles);
  END_TEST();
  lk_resource_release(rs, ref);
  lk_spans_destroy(sp);
  lk_ui_destroy(ui);
}

static void test_from_node_degrade(void) {
  lk_ui *ui = lk_ui_create(NULL);
  lk_spans *sp = lk_spans_new(NULL, NULL, NULL);
  lk_resources *rs = lk_ui_resources(ui);
  lk_resource_ref ref = lk_resource_register(rs, lk_spans_type(), sp, "sp");
  lk_resource_ref bad;

  BEGIN_TEST("styled text: dead / wrong-typed spans ref is unstyled");

  {
    lk_tree *t = lk_ui_begin_frame(ui);
    lk_ix win = lk_tree_add_node_c(t, "w", UIK_WINDOW);
    lk_ix st = lk_tree_add_node_c(t, "st", UIK_STYLED_TEXT);

    lk_tree_set_root(t, win);
    lk_tree_append_child(t, win, st);
    lk_tree_add_prop(t, st, UIP_TEXT, lk_v_cstr(t->intern, "x"));
    lk_tree_add_prop(t, st, UIP_SPANS, lk_v_resource(ref));
    lk_ui_end_frame(ui);
  }

  CHECK(lk_spans_from_node(rs, lk_ui_tree(ui), find(ui, "st")) == sp);
  lk_resource_release(rs, ref);
  CHECK(lk_spans_from_node(rs, lk_ui_tree(ui), find(ui, "st")) == NULL);

  bad = lk_resource_register(rs, lk_canvas_type(), sp, "not-spans");
  {
    lk_tree *t = lk_ui_begin_frame(ui);
    lk_ix win = lk_tree_add_node_c(t, "w", UIK_WINDOW);
    lk_ix st = lk_tree_add_node_c(t, "st", UIK_STYLED_TEXT);

    lk_tree_set_root(t, win);
    lk_tree_append_child(t, win, st);
    lk_tree_add_prop(t, st, UIP_TEXT, lk_v_cstr(t->intern, "x"));
    lk_tree_add_prop(t, st, UIP_SPANS, lk_v_resource(bad));
    lk_ui_end_frame(ui);
  }
  CHECK(lk_spans_from_node(rs, lk_ui_tree(ui), find(ui, "st")) == NULL);
  lk_resource_release(rs, bad);

  END_TEST();
  lk_spans_destroy(sp);
  lk_ui_destroy(ui);
}

void lk_styled_text_run_tests(void) {
  printf("\nstyled text tests:\n");
  test_spans_object();
  test_rows_policy();
  test_fit_column();
  test_fit_scroll();
  test_render_runs();
  test_render_wrapped_span();
  test_pos_and_presentation();
  test_from_node_degrade();
}
