/*
 * lk-canvas-test.c -- lk_canvas display list + UIK_CANVAS widget
 * (docs/canvas.md).
 *
 * Geometry runs against the stub text backend with NULL styles; the
 * render tests resolve the default theme (canvas bg 24,26,30).  The
 * contract pinned here: the widget replays the list translated to its
 * rect inside a CLIP pair, polyline points travel through the render
 * list's own byte arena (never a pointer into the canvas), and a dead
 * ref renders the background only.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <lk-editor.h>
#include <lk.h>

#include "lk-test-harness.h"

/* ---- helpers ---- */

static lk_rect *layout_ui(lk_ui *ui, lk_i32 vw, lk_i32 vh) {
  lk_layout_cfg cfg;
  lk_rect *rects;
  const lk_tree *cur = lk_ui_tree(ui);

  memset(&cfg, 0, sizeof(cfg));
  cfg.text = lk_text_backend_stub();
  cfg.viewport_w = vw;
  cfg.viewport_h = vh;
  cfg.state = lk_ui_state(ui);
  cfg.geom = lk_ui_geom(ui);

  rects = (lk_rect *)malloc(sizeof(lk_rect) * cur->node_count);

  if (!rects) {
    return NULL;
  }

  if (!lk_layout(cur, &cfg, rects)) {
    free(rects);
    return NULL;
  }

  return rects;
}

static lk_ix find(lk_ui *ui, const char *id) {
  return lk_tree_find_by_id(lk_ui_tree(ui), lk_intern_cid(ui->intern, id));
}

/* window > column (align start, padding 10) > canvas "cv" [+ optional
 * UIP_CANVAS ref, optional UIP_W/UIP_H].  The padding puts the canvas
 * at a non-zero origin so translation is actually exercised. */
static void build_canvas_frame(lk_ui *ui, const lk_resource_ref *ref, lk_i32 w,
                               lk_i32 h) {
  lk_tree *t = lk_ui_begin_frame(ui);
  lk_ix win = lk_tree_add_node_c(t, "w", UIK_WINDOW);
  lk_ix col = lk_tree_add_node_c(t, "col", UIK_COLUMN);
  lk_ix cv = lk_tree_add_node_c(t, "cv", UIK_CANVAS);

  lk_tree_set_root(t, win);
  lk_tree_append_child(t, win, col);
  lk_tree_append_child(t, col, cv);
  lk_tree_add_prop(t, col, UIP_ALIGN, lk_v_i32(LK_ALIGN_START));
  lk_tree_add_prop(t, col, UIP_PADDING, lk_v_i32(10));

  if (ref) {
    lk_tree_add_prop(t, cv, UIP_CANVAS, lk_v_resource(*ref));
  }

  if (w > 0) {
    lk_tree_add_prop(t, cv, UIP_W, lk_v_i32(w));
  }

  if (h > 0) {
    lk_tree_add_prop(t, cv, UIP_H, lk_v_i32(h));
  }

  lk_ui_end_frame(ui);
}

static int build_render(lk_ui *ui, lk_render_list *rl) {
  lk_rect *r = layout_ui(ui, 640, 480);
  const lk_tree *cur = lk_ui_tree(ui);
  lk_style *styles;

  if (!r) {
    return 0;
  }

  styles = (lk_style *)malloc(sizeof(lk_style) * cur->node_count);

  if (!styles) {
    free(r);
    return 0;
  }

  memset(rl, 0, sizeof(*rl));
  lk_style_resolve(lk_ui_theme(ui), cur, NULL, styles);
  lk_render_build(cur, r, styles, lk_ui_state(ui), lk_ui_geom(ui), rl);
  free(styles);
  free(r);

  return 1;
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

/* The k-th (0-based) cmd with the given op, or NULL. */
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

static lk_color rgb(lk_u8 r, lk_u8 g, lk_u8 b) {
  lk_color c;

  c.r = r;
  c.g = g;
  c.b = b;
  c.a = 255;

  return c;
}

static lk_rect mkrect(lk_i32 x, lk_i32 y, lk_i32 w, lk_i32 h) {
  lk_rect r;

  r.x = x;
  r.y = y;
  r.w = w;
  r.h = h;

  return r;
}

struct dump_buf {
  char buf[1024];
  lk_u32 len;
};

static void dump_writer(void *ud, const char *ptr, lk_u32 len) {
  struct dump_buf *db = (struct dump_buf *)ud;

  if (db->len + len + 1 > sizeof(db->buf)) {
    len = (lk_u32)(sizeof(db->buf) - 1 - db->len);
  }

  memcpy(db->buf + db->len, ptr, len);
  db->len += len;
  db->buf[db->len] = 0;
}

/* ---- object basics ---- */

static void test_canvas_basics(void) {
  lk_canvas *c;
  lk_u32 w = 0;
  lk_u32 h = 0;
  lk_i32 pts[6];

  BEGIN_TEST("canvas: new / size / ops / clear");

  c = lk_canvas_new(300, 200, NULL, NULL, NULL);
  CHECK(c != NULL);

  lk_canvas_size(c, &w, &h);
  CHECK_EQ(w, 300);
  CHECK_EQ(h, 200);
  CHECK_EQ(lk_canvas_op_count(c), 0);

  CHECK(lk_canvas_set_size(c, 10, 20) == 1);
  lk_canvas_size(c, &w, &h);
  CHECK_EQ(w, 10);
  CHECK_EQ(h, 20);
  CHECK(lk_canvas_set_size(c, 16385, 1) == 0);
  lk_canvas_size(c, &w, &h);
  CHECK_EQ(w, 10); /* unchanged on rejection */

  CHECK(lk_canvas_line(c, 0, 0, 10, 10, rgb(1, 2, 3), 1) == 1);
  CHECK(lk_canvas_rect(c, mkrect(1, 1, 5, 5), rgb(1, 2, 3), 2) == 1);
  CHECK(lk_canvas_fill_rect(c, mkrect(1, 1, 5, 5), rgb(1, 2, 3)) == 1);
  CHECK(lk_canvas_text(c, 3, 4, "hi", 2, rgb(1, 2, 3)) == 1);

  pts[0] = 0;
  pts[1] = 0;
  pts[2] = 5;
  pts[3] = 5;
  pts[4] = 10;
  pts[5] = 0;
  CHECK(lk_canvas_polyline(c, pts, 3, rgb(1, 2, 3), 1) == 1);
  CHECK_EQ(lk_canvas_op_count(c), 5);

  /* Rejections leave the list unchanged. */
  CHECK(lk_canvas_polyline(c, pts, 1, rgb(1, 2, 3), 1) == 0);
  CHECK(lk_canvas_polyline(c, NULL, 3, rgb(1, 2, 3), 1) == 0);
  CHECK(lk_canvas_polyline(c, pts, LK_CANVAS_MAX_POINTS + 1, rgb(1, 2, 3), 1) ==
        0);
  CHECK(lk_canvas_text(c, 0, 0, NULL, 3, rgb(1, 2, 3)) == 0);
  CHECK(lk_canvas_text(c, 0, 0, "", 0, rgb(1, 2, 3)) == 1); /* no-op ok */
  CHECK_EQ(lk_canvas_op_count(c), 5);

  lk_canvas_clear(c);
  CHECK_EQ(lk_canvas_op_count(c), 0);
  CHECK(lk_canvas_line(c, 0, 0, 1, 1, rgb(1, 2, 3), 1) == 1);
  CHECK_EQ(lk_canvas_op_count(c), 1);

  lk_canvas_destroy(c);

  END_TEST();
}

static void test_canvas_bounds(void) {
  lk_canvas *c;
  lk_u32 i;

  BEGIN_TEST("canvas: bounds, NULL tolerance, growth");

  CHECK(lk_canvas_new(16385, 1, NULL, NULL, NULL) == NULL);
  CHECK(lk_canvas_new(1, 16385, NULL, NULL, NULL) == NULL);

  /* 0x0 hint is legal (grow / W / H size the node). */
  c = lk_canvas_new(0, 0, NULL, NULL, NULL);
  CHECK(c != NULL);

  CHECK_EQ(lk_canvas_op_count(NULL), 0);
  CHECK(lk_canvas_line(NULL, 0, 0, 1, 1, rgb(0, 0, 0), 1) == 0);
  CHECK(lk_canvas_set_size(NULL, 1, 1) == 0);
  lk_canvas_clear(NULL);
  lk_canvas_destroy(NULL);

  /* Past the initial op capacity and the initial arena. */
  for (i = 0; i < 1000; i++) {
    CHECK(lk_canvas_text(c, 0, 0, "0123456789abcdef", 16, rgb(0, 0, 0)) == 1);
  }
  CHECK_EQ(lk_canvas_op_count(c), 1000);

  CHECK(lk_canvas_type() == lk_canvas_type());

  lk_canvas_destroy(c);

  END_TEST();
}

/* ---- resolution + measure ---- */

static void test_canvas_from_node_and_measure(void) {
  lk_ui *ui = lk_ui_create(NULL);
  lk_canvas *c = lk_canvas_new(120, 80, NULL, NULL, NULL);
  lk_resources *rs = lk_ui_resources(ui);
  lk_resource_ref ref = lk_resource_register(rs, lk_canvas_type(), c, "cv");
  int dummy = 0;
  lk_resource_ref wrong =
      lk_resource_register(rs, lk_editor_type(), &dummy, "not-a-canvas");
  lk_rect *r;

  BEGIN_TEST("canvas: from_node + measure (hint, override, degrade)");

  /* Resolves through the prop. */
  build_canvas_frame(ui, &ref, 0, 0);
  CHECK(lk_canvas_from_node(rs, lk_ui_tree(ui), find(ui, "cv")) == c);
  CHECK(lk_canvas_from_node(NULL, lk_ui_tree(ui), find(ui, "cv")) == NULL);

  r = layout_ui(ui, 640, 480);
  CHECK(r != NULL);
  if (r) {
    lk_rect cr = r[find(ui, "cv")];

    CHECK_EQ(cr.x, 10);
    CHECK_EQ(cr.y, 10);
    CHECK_EQ(cr.w, 120);
    CHECK_EQ(cr.h, 80);
    free(r);
  }

  /* Explicit size overrides the hint. */
  build_canvas_frame(ui, &ref, 300, 0);
  r = layout_ui(ui, 640, 480);
  CHECK(r != NULL);
  if (r) {
    lk_rect cr = r[find(ui, "cv")];

    CHECK_EQ(cr.w, 300);
    CHECK_EQ(cr.h, 80);
    free(r);
  }

  /* Wrong-typed ref: NULL, and the node measures 0x0. */
  build_canvas_frame(ui, &wrong, 0, 0);
  CHECK(lk_canvas_from_node(rs, lk_ui_tree(ui), find(ui, "cv")) == NULL);
  r = layout_ui(ui, 640, 480);
  CHECK(r != NULL);
  if (r) {
    lk_rect cr = r[find(ui, "cv")];

    CHECK_EQ(cr.w, 0);
    CHECK_EQ(cr.h, 0);
    free(r);
  }

  /* No prop at all. */
  build_canvas_frame(ui, NULL, 0, 0);
  CHECK(lk_canvas_from_node(rs, lk_ui_tree(ui), find(ui, "cv")) == NULL);

  /* Released ref: stale. */
  build_canvas_frame(ui, &ref, 0, 0);
  lk_resource_release(rs, ref);
  CHECK(lk_canvas_from_node(rs, lk_ui_tree(ui), find(ui, "cv")) == NULL);

  lk_canvas_destroy(c);
  lk_ui_destroy(ui);

  END_TEST();
}

/* ---- render ---- */

static void test_canvas_render_replay(void) {
  lk_ui *ui = lk_ui_create(NULL);
  lk_canvas *c = lk_canvas_new(100, 100, NULL, NULL, NULL);
  lk_resources *rs = lk_ui_resources(ui);
  lk_resource_ref ref = lk_resource_register(rs, lk_canvas_type(), c, "cv");
  lk_render_list rl;
  lk_i32 pts[6];
  const lk_render_cmd *cmd;
  const lk_i32 *xy;

  BEGIN_TEST("canvas: render replays ops translated + clipped");

  lk_canvas_line(c, 0, 0, 10, 20, rgb(255, 0, 0), 1);
  pts[0] = 1;
  pts[1] = 2;
  pts[2] = 30;
  pts[3] = 40;
  pts[4] = 50;
  pts[5] = 60;
  lk_canvas_polyline(c, pts, 3, rgb(0, 255, 0), 3);
  lk_canvas_rect(c, mkrect(5, 5, 10, 10), rgb(0, 0, 255), 1);
  lk_canvas_fill_rect(c, mkrect(7, 8, 9, 10), rgb(9, 9, 9));
  lk_canvas_text(c, 3, 4, "hello", 5, rgb(1, 1, 1));

  build_canvas_frame(ui, &ref, 0, 0);
  CHECK(build_render(ui, &rl) == 1);

  /* The window clips too (one pair), the canvas adds the second. */
  CHECK_EQ(count_op(&rl, LK_ROP_CLIP_BEGIN), 2);
  CHECK_EQ(count_op(&rl, LK_ROP_CLIP_END), 2);
  CHECK_EQ(count_op(&rl, LK_ROP_DRAW_LINES), 3); /* line, polyline, rect */
  CHECK_EQ(count_op(&rl, LK_ROP_DRAW_RUN), 1);

  /* The canvas's clip is its rect (10,10 100x100). */
  cmd = nth_op(&rl, LK_ROP_CLIP_BEGIN, 1);
  CHECK(cmd != NULL);
  if (cmd) {
    CHECK_EQ(cmd->rect.x, 10);
    CHECK_EQ(cmd->rect.y, 10);
    CHECK_EQ(cmd->rect.w, 100);
    CHECK_EQ(cmd->rect.h, 100);
  }

  /* Line: two points translated by (10, 10); bounding rect; stroke. */
  cmd = nth_op(&rl, LK_ROP_DRAW_LINES, 0);
  CHECK(cmd != NULL);
  if (cmd) {
    CHECK_EQ(cmd->run_len, 16);
    xy = (const lk_i32 *)(rl.bytes + cmd->run_off);
    CHECK_EQ(xy[0], 10);
    CHECK_EQ(xy[1], 10);
    CHECK_EQ(xy[2], 20);
    CHECK_EQ(xy[3], 30);
    CHECK_EQ(cmd->color.r, 255);
    CHECK_EQ(cmd->stroke, 1);
    CHECK_EQ(cmd->rect.x, 10);
    CHECK_EQ(cmd->rect.y, 10);
    CHECK_EQ(cmd->rect.w, 11);
    CHECK_EQ(cmd->rect.h, 21);
  }

  /* Polyline: 3 points, stroke 3, bytes copied (not the canvas's). */
  cmd = nth_op(&rl, LK_ROP_DRAW_LINES, 1);
  CHECK(cmd != NULL);
  if (cmd) {
    CHECK_EQ(cmd->run_len, 24);
    xy = (const lk_i32 *)(rl.bytes + cmd->run_off);
    CHECK_EQ(xy[0], 11);
    CHECK_EQ(xy[1], 12);
    CHECK_EQ(xy[4], 60);
    CHECK_EQ(xy[5], 70);
    CHECK_EQ(cmd->stroke, 3);
    CHECK_EQ(cmd->color.g, 255);
  }

  /* Rect outline: closed 5-point polyline over the edge pixels. */
  cmd = nth_op(&rl, LK_ROP_DRAW_LINES, 2);
  CHECK(cmd != NULL);
  if (cmd) {
    CHECK_EQ(cmd->run_len, 40);
    xy = (const lk_i32 *)(rl.bytes + cmd->run_off);
    CHECK_EQ(xy[0], 15);
    CHECK_EQ(xy[1], 15);
    CHECK_EQ(xy[2], 24);
    CHECK_EQ(xy[3], 15);
    CHECK_EQ(xy[4], 24);
    CHECK_EQ(xy[5], 24);
    CHECK_EQ(xy[6], 15);
    CHECK_EQ(xy[7], 24);
    CHECK_EQ(xy[8], 15);
    CHECK_EQ(xy[9], 15);
    CHECK_EQ(cmd->rect.w, 10);
    CHECK_EQ(cmd->rect.h, 10);
  }

  /* Text: a DRAW_RUN at the translated origin with the op's color and
   * the resolved font; bytes in the list arena. */
  cmd = nth_op(&rl, LK_ROP_DRAW_RUN, 0);
  CHECK(cmd != NULL);
  if (cmd) {
    CHECK_EQ(cmd->rect.x, 13);
    CHECK_EQ(cmd->rect.y, 14);
    CHECK_EQ(cmd->run_len, 5);
    CHECK(memcmp(rl.bytes + cmd->run_off, "hello", 5) == 0);
    CHECK_EQ(cmd->color.r, 1);
  }

  /* fill_rect translated: find the FILL_RECT with color 9,9,9. */
  {
    lk_u32 i;
    int found = 0;

    for (i = 0; i < rl.count; i++) {
      const lk_render_cmd *f = &rl.cmds[i];

      if (f->op == LK_ROP_FILL_RECT && f->color.r == 9 && f->color.g == 9) {
        found = 1;
        CHECK_EQ(f->rect.x, 17);
        CHECK_EQ(f->rect.y, 18);
        CHECK_EQ(f->rect.w, 9);
        CHECK_EQ(f->rect.h, 10);
      }
    }

    CHECK(found == 1);
  }

  /* Mutating the canvas after the build must not change the list —
   * the bytes were copied. */
  lk_canvas_clear(c);
  cmd = nth_op(&rl, LK_ROP_DRAW_LINES, 0);
  CHECK(cmd != NULL);
  if (cmd) {
    xy = (const lk_i32 *)(rl.bytes + cmd->run_off);
    CHECK_EQ(xy[2], 20);
  }

  lk_render_list_destroy(&rl);
  lk_canvas_destroy(c);
  lk_ui_destroy(ui);

  END_TEST();
}

static void test_canvas_render_long_polyline(void) {
  lk_ui *ui = lk_ui_create(NULL);
  lk_canvas *c = lk_canvas_new(100, 100, NULL, NULL, NULL);
  lk_resources *rs = lk_ui_resources(ui);
  lk_resource_ref ref = lk_resource_register(rs, lk_canvas_type(), c, "cv");
  lk_render_list rl;
  lk_i32 pts[2 * 200];
  lk_u32 i;
  const lk_render_cmd *cmd;
  const lk_i32 *xy;

  /* 200 points crosses the emitter's 32-point staging window several
   * times; the run must still be one contiguous byte range. */
  BEGIN_TEST("canvas: long polyline is one contiguous run");

  for (i = 0; i < 200; i++) {
    pts[i * 2] = (lk_i32)i;
    pts[i * 2 + 1] = (lk_i32)(i * 2);
  }

  lk_canvas_polyline(c, pts, 200, rgb(1, 2, 3), 1);
  build_canvas_frame(ui, &ref, 0, 0);
  CHECK(build_render(ui, &rl) == 1);
  CHECK_EQ(count_op(&rl, LK_ROP_DRAW_LINES), 1);

  cmd = nth_op(&rl, LK_ROP_DRAW_LINES, 0);
  CHECK(cmd != NULL);
  if (cmd) {
    int ok = 1;

    CHECK_EQ(cmd->run_len, 200 * 8);
    xy = (const lk_i32 *)(rl.bytes + cmd->run_off);

    for (i = 0; i < 200; i++) {
      if (xy[i * 2] != (lk_i32)i + 10 ||
          xy[i * 2 + 1] != (lk_i32)(i * 2) + 10) {
        ok = 0;
      }
    }

    CHECK(ok == 1);
    CHECK_EQ(cmd->rect.w, 200);
    CHECK_EQ(cmd->rect.h, 399);
  }

  lk_render_list_destroy(&rl);
  lk_canvas_destroy(c);
  lk_ui_destroy(ui);

  END_TEST();
}

static void test_canvas_subclip(void) {
  lk_ui *ui = lk_ui_create(NULL);
  lk_canvas *c = lk_canvas_new(100, 100, NULL, NULL, NULL);
  lk_resources *rs = lk_ui_resources(ui);
  lk_resource_ref ref = lk_resource_register(rs, lk_canvas_type(), c, "cv");
  lk_render_list rl;
  const lk_render_cmd *cmd;
  lk_u32 i;

  BEGIN_TEST("canvas: sub-clip ops, depth cap, auto-close at render");

  /* End with nothing open is refused and appends nothing. */
  CHECK_EQ(lk_canvas_clip_end(c), 0);
  CHECK_EQ(lk_canvas_op_count(c), 0);
  CHECK_EQ(lk_canvas_clip_depth(c), 0);

  /* Negative extents are refused. */
  CHECK_EQ(lk_canvas_clip_begin(c, mkrect(0, 0, -1, 5)), 0);

  CHECK_EQ(lk_canvas_clip_begin(c, mkrect(10, 20, 30, 40)), 1);
  CHECK_EQ(lk_canvas_clip_depth(c), 1);
  lk_canvas_line(c, 0, 0, 99, 99, rgb(255, 0, 0), 1);
  CHECK_EQ(lk_canvas_clip_end(c), 1);
  CHECK_EQ(lk_canvas_clip_depth(c), 0);
  /* A second clip left OPEN on purpose. */
  CHECK_EQ(lk_canvas_clip_begin(c, mkrect(1, 2, 3, 4)), 1);
  lk_canvas_fill_rect(c, mkrect(0, 0, 5, 5), rgb(9, 9, 9));
  CHECK_EQ(lk_canvas_op_count(c), 5);

  build_canvas_frame(ui, &ref, 0, 0);
  CHECK(build_render(ui, &rl) == 1);

  /* window + canvas + two sub-clips = 4 begins; the open one is closed
   * by the replay, so the ends balance. */
  CHECK_EQ(count_op(&rl, LK_ROP_CLIP_BEGIN), 4);
  CHECK_EQ(count_op(&rl, LK_ROP_CLIP_END), 4);

  /* First sub-clip translated by the canvas origin (10, 10). */
  cmd = nth_op(&rl, LK_ROP_CLIP_BEGIN, 2);
  CHECK(cmd != NULL);
  if (cmd) {
    CHECK_EQ(cmd->rect.x, 20);
    CHECK_EQ(cmd->rect.y, 30);
    CHECK_EQ(cmd->rect.w, 30);
    CHECK_EQ(cmd->rect.h, 40);
  }

  /* The list ends with the two auto/outer CLIP_ENDs, and the line sits
   * between the first sub-clip's begin and end. */
  CHECK(rl.count >= 2);
  if (rl.count >= 2) {
    CHECK_EQ((unsigned)rl.cmds[rl.count - 1].op, (unsigned)LK_ROP_CLIP_END);
    CHECK_EQ((unsigned)rl.cmds[rl.count - 2].op, (unsigned)LK_ROP_CLIP_END);
  }

  {
    int seen_begin = 0, line_inside = 0;

    for (i = 0; i < rl.count; i++) {
      if (rl.cmds[i].op == LK_ROP_CLIP_BEGIN && rl.cmds[i].rect.w == 30) {
        seen_begin = 1;
      } else if (rl.cmds[i].op == LK_ROP_DRAW_LINES && seen_begin) {
        line_inside = 1;
      } else if (rl.cmds[i].op == LK_ROP_CLIP_END && seen_begin) {
        break;
      }
    }

    CHECK(line_inside);
  }

  lk_render_list_destroy(&rl);

  /* clear forgets open clips; the depth cap holds. */
  lk_canvas_clear(c);
  CHECK_EQ(lk_canvas_clip_depth(c), 0);

  for (i = 0; i < LK_CANVAS_MAX_CLIP_DEPTH; i++) {
    CHECK_EQ(lk_canvas_clip_begin(c, mkrect(0, 0, 1, 1)), 1);
  }

  CHECK_EQ(lk_canvas_clip_begin(c, mkrect(0, 0, 1, 1)), 0);
  CHECK_EQ(lk_canvas_clip_depth(c), LK_CANVAS_MAX_CLIP_DEPTH);

  END_TEST();
  lk_resource_release(rs, ref);
  lk_canvas_destroy(c);
  lk_ui_destroy(ui);
}

static void test_canvas_render_degrade(void) {
  lk_ui *ui = lk_ui_create(NULL);
  lk_canvas *c = lk_canvas_new(50, 50, NULL, NULL, NULL);
  lk_resources *rs = lk_ui_resources(ui);
  lk_resource_ref ref = lk_resource_register(rs, lk_canvas_type(), c, "cv");
  lk_render_list rl;

  BEGIN_TEST("canvas: empty list / dead ref render bg only");

  /* Empty canvas: bg, no clip pair of its own (the window's remains),
   * no lines. */
  build_canvas_frame(ui, &ref, 0, 0);
  CHECK(build_render(ui, &rl) == 1);
  CHECK_EQ(count_op(&rl, LK_ROP_CLIP_BEGIN), 1);
  CHECK_EQ(count_op(&rl, LK_ROP_DRAW_LINES), 0);
  {
    lk_u32 i;
    int found = 0;

    for (i = 0; i < rl.count; i++) {
      const lk_render_cmd *f = &rl.cmds[i];

      if (f->op == LK_ROP_FILL_RECT && f->color.r == 24 && f->color.g == 26 &&
          f->color.b == 30) {
        found = 1;
        CHECK_EQ(f->rect.x, 10);
        CHECK_EQ(f->rect.w, 50);
      }
    }

    CHECK(found == 1); /* the theme plate */
  }
  lk_render_list_destroy(&rl);

  /* Dead ref with ops on the (still live) canvas: bg only. */
  lk_canvas_line(c, 0, 0, 5, 5, rgb(1, 1, 1), 1);
  lk_resource_release(rs, ref);
  build_canvas_frame(ui, &ref, 40, 40);
  CHECK(build_render(ui, &rl) == 1);
  CHECK_EQ(count_op(&rl, LK_ROP_DRAW_LINES), 0);
  CHECK_EQ(count_op(&rl, LK_ROP_CLIP_BEGIN), 1);
  lk_render_list_destroy(&rl);

  lk_canvas_destroy(c);
  lk_ui_destroy(ui);

  END_TEST();
}

static void test_canvas_dump_and_registry(void) {
  lk_ui *ui = lk_ui_create(NULL);
  const lk_widget_def *def;

  BEGIN_TEST("canvas: widget registered as a leaf; dump names it");

  def = lk_widget_get(UIK_CANVAS);
  CHECK(def != NULL);
  if (def) {
    CHECK(def->measure != NULL);
    CHECK(def->render != NULL);
    CHECK(def->layout == NULL); /* leaf */
    CHECK(def->event == NULL);
  }

  build_canvas_frame(ui, NULL, 0, 0);
  {
    struct dump_buf db;

    db.len = 0;
    db.buf[0] = 0;
    lk_tree_dump(lk_ui_tree(ui), dump_writer, &db);
    CHECK(db.len > 0);
    CHECK(strstr(db.buf, "canvas") != NULL);
  }

  lk_ui_destroy(ui);

  END_TEST();
}

void lk_canvas_run_tests(void) {
  printf("\nlk canvas tests:\n");
  test_canvas_basics();
  test_canvas_bounds();
  test_canvas_from_node_and_measure();
  test_canvas_render_replay();
  test_canvas_render_long_polyline();
  test_canvas_render_degrade();
  test_canvas_subclip();
  test_canvas_dump_and_registry();
}
