/*
 * lk-image-test.c -- lk_image resource + UIK_IMAGE widget
 * (docs/image-widget.md).
 *
 * Geometry runs against the stub text backend with NULL styles; the
 * render tests resolve the default theme (image bg 20,20,22).  The
 * two-generation split is pinned here: the render cmd carries the
 * RESOURCE ref's generation (stale-handle detection), while
 * lk_image_mark_dirty bumps only the pixel generation queried off the
 * resolved image -- a dirty image must NOT change the emitted ref.
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

/* window > column (align start) > image "img" [+ optional UIP_IMAGE
 * ref, optional UIP_W/UIP_H].  The align-start column keeps the image
 * at its measured size on both axes (styleless stacks stretch). */
static void build_image_frame(lk_ui *ui, const lk_resource_ref *ref, lk_i32 w,
                              lk_i32 h) {
  lk_tree *t = lk_ui_begin_frame(ui);
  lk_ix win = lk_tree_add_node_c(t, "w", UIK_WINDOW);
  lk_ix col = lk_tree_add_node_c(t, "col", UIK_COLUMN);
  lk_ix img = lk_tree_add_node_c(t, "img", UIK_IMAGE);

  lk_tree_set_root(t, win);
  lk_tree_append_child(t, win, col);
  lk_tree_append_child(t, col, img);
  lk_tree_add_prop(t, col, UIP_ALIGN, lk_v_i32(LK_ALIGN_START));

  if (ref) {
    lk_tree_add_prop(t, img, UIP_IMAGE, lk_v_resource(*ref));
  }

  if (w > 0) {
    lk_tree_add_prop(t, img, UIP_W, lk_v_i32(w));
  }

  if (h > 0) {
    lk_tree_add_prop(t, img, UIP_H, lk_v_i32(h));
  }

  lk_ui_end_frame(ui);
}

/* Render the current frame with the default theme resolved; returns
 * the built list (destroy after).  rects freed here. */
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

/* Find the first DRAW_IMAGE cmd; returns count. */
static int find_draw_image(const lk_render_list *rl, lk_render_cmd *out) {
  lk_u32 i;
  int n = 0;

  for (i = 0; i < rl->count; i++) {
    if (rl->cmds[i].op == LK_ROP_DRAW_IMAGE) {
      if (n == 0 && out) {
        *out = rl->cmds[i];
      }

      n++;
    }
  }

  return n;
}

/* ---- object basics ---- */

static void test_image_basics(void) {
  lk_image *img;
  lk_u32 w = 0;
  lk_u32 h = 0;
  lk_u8 *px;
  lk_u32 i;

  BEGIN_TEST("image: new / size / pixels / dirty");

  img = lk_image_new(4, 3, NULL, NULL, NULL);
  CHECK(img != NULL);

  lk_image_size(img, &w, &h);
  CHECK_EQ(w, 4);
  CHECK_EQ(h, 3);
  CHECK_EQ(lk_image_generation(img), 1);

  px = lk_image_pixels(img);
  CHECK(px != NULL);

  for (i = 0; i < 4 * 3 * 4; i++) {
    if (px[i] != 0) {
      break;
    }
  }
  CHECK_EQ(i, 4 * 3 * 4); /* transparent black */

  px[0] = 200; /* poke a pixel, round-trip */
  CHECK_EQ(lk_image_pixels(img)[0], 200);

  lk_image_mark_dirty(img);
  CHECK_EQ(lk_image_generation(img), 2);

  lk_image_destroy(img);

  END_TEST();
}

static void test_image_bounds(void) {
  BEGIN_TEST("image: dimension bounds");

  CHECK(lk_image_new(0, 5, NULL, NULL, NULL) == NULL);
  CHECK(lk_image_new(5, 0, NULL, NULL, NULL) == NULL);
  CHECK(lk_image_new(16385, 1, NULL, NULL, NULL) == NULL);
  CHECK(lk_image_new(1, 16385, NULL, NULL, NULL) == NULL);

  /* NULL-tolerant accessors */
  CHECK(lk_image_pixels(NULL) == NULL);
  CHECK_EQ(lk_image_generation(NULL), 0);
  lk_image_destroy(NULL);

  /* type descriptor is a stable singleton */
  CHECK(lk_image_type() == lk_image_type());

  END_TEST();
}

/* ---- resolution ---- */

static void test_image_from_node(void) {
  lk_ui *ui = lk_ui_create(NULL);
  lk_image *img = lk_image_new(8, 8, NULL, NULL, NULL);
  lk_resources *rs = lk_ui_resources(ui);
  lk_resource_ref ref = lk_resource_register(rs, lk_image_type(), img, "img");
  int dummy = 0;
  lk_resource_ref wrong =
      lk_resource_register(rs, lk_editor_type(), &dummy, "not-an-image");
  lk_tree *t;
  const lk_tree *cur;
  lk_ix win;
  lk_ix good;
  lk_ix bare;
  lk_ix typed;

  BEGIN_TEST("image: from_node resolve / degrade");

  CHECK(ref.id != 0);

  t = lk_ui_begin_frame(ui);
  win = lk_tree_add_node_c(t, "w", UIK_WINDOW);
  good = lk_tree_add_node_c(t, "good", UIK_IMAGE);
  bare = lk_tree_add_node_c(t, "bare", UIK_IMAGE);
  typed = lk_tree_add_node_c(t, "typed", UIK_IMAGE);
  lk_tree_set_root(t, win);
  lk_tree_append_child(t, win, good);
  lk_tree_append_child(t, win, bare);
  lk_tree_append_child(t, win, typed);
  lk_tree_add_prop(t, good, UIP_IMAGE, lk_v_resource(ref));
  lk_tree_add_prop(t, typed, UIP_IMAGE, lk_v_resource(wrong));
  lk_ui_end_frame(ui);

  cur = lk_ui_tree(ui);
  CHECK(lk_image_from_node(rs, cur, find(ui, "good")) == img);
  CHECK(lk_image_from_node(rs, cur, find(ui, "bare")) == NULL);
  CHECK(lk_image_from_node(rs, cur, find(ui, "typed")) == NULL);
  CHECK(lk_image_from_node(NULL, cur, find(ui, "good")) == NULL);
  CHECK(lk_image_from_node(rs, cur, 0) == NULL);

  /* released ref goes stale */
  lk_resource_release(rs, ref);
  CHECK(lk_image_from_node(rs, cur, find(ui, "good")) == NULL);

  END_TEST();
  lk_image_destroy(img);
  lk_ui_destroy(ui);
}

/* ---- widget geometry ---- */

static void test_image_measure(void) {
  lk_ui *ui = lk_ui_create(NULL);
  lk_image *img = lk_image_new(32, 16, NULL, NULL, NULL);
  lk_resource_ref ref =
      lk_resource_register(lk_ui_resources(ui), lk_image_type(), img, "img");
  lk_rect *r;

  BEGIN_TEST("image: intrinsic measure + W/H override");

  build_image_frame(ui, &ref, 0, 0);
  r = layout_ui(ui, 640, 480);
  CHECK(r != NULL);

  if (r) {
    lk_ix n = find(ui, "img");
    CHECK_EQ(r[n].w, 32);
    CHECK_EQ(r[n].h, 16);
    free(r);
  }

  /* explicit W/H wins over the intrinsic size */
  build_image_frame(ui, &ref, 100, 50);
  r = layout_ui(ui, 640, 480);
  CHECK(r != NULL);

  if (r) {
    lk_ix n = find(ui, "img");
    CHECK_EQ(r[n].w, 100);
    CHECK_EQ(r[n].h, 50);
    free(r);
  }

  /* unresolvable ref measures 0x0 */
  build_image_frame(ui, NULL, 0, 0);
  r = layout_ui(ui, 640, 480);
  CHECK(r != NULL);

  if (r) {
    lk_ix n = find(ui, "img");
    CHECK_EQ(r[n].w, 0);
    CHECK_EQ(r[n].h, 0);
    free(r);
  }

  END_TEST();
  lk_image_destroy(img);
  lk_ui_destroy(ui);
}

/* ---- rendering ---- */

static void test_image_render(void) {
  lk_ui *ui = lk_ui_create(NULL);
  lk_image *img = lk_image_new(32, 16, NULL, NULL, NULL);
  lk_resource_ref ref =
      lk_resource_register(lk_ui_resources(ui), lk_image_type(), img, "img");
  lk_render_list rl;
  lk_render_cmd cmd;

  BEGIN_TEST("image: render emits DRAW_IMAGE with the ref");

  memset(&cmd, 0, sizeof(cmd));
  build_image_frame(ui, &ref, 0, 0);
  CHECK(build_render(ui, &rl));

  CHECK_EQ(find_draw_image(&rl, &cmd), 1);
  CHECK_EQ(cmd.img_id, ref.id);
  CHECK_EQ(cmd.img_gen, ref.generation);
  CHECK_EQ(cmd.rect.w, 32);
  CHECK_EQ(cmd.rect.h, 16);
  CHECK_EQ(cmd.color.r, 255);
  CHECK_EQ(cmd.color.g, 255);
  CHECK_EQ(cmd.color.b, 255);
  CHECK_EQ(cmd.color.a, 255);
  lk_render_list_destroy(&rl);

  /* mark_dirty bumps the PIXEL generation only -- the emitted ref
   * must not change (the cache invalidates off the resolved image,
   * not the cmd). */
  lk_image_mark_dirty(img);
  CHECK_EQ(lk_image_generation(img), 2);

  build_image_frame(ui, &ref, 0, 0);
  CHECK(build_render(ui, &rl));
  CHECK_EQ(find_draw_image(&rl, &cmd), 1);
  CHECK_EQ(cmd.img_gen, ref.generation);
  lk_render_list_destroy(&rl);

  END_TEST();
  lk_image_destroy(img);
  lk_ui_destroy(ui);
}

static void test_image_filter(void) {
  lk_ui *ui = lk_ui_create(NULL);
  lk_image *img = lk_image_new(4, 4, NULL, NULL, NULL);
  lk_resource_ref ref =
      lk_resource_register(lk_ui_resources(ui), lk_image_type(), img, "img");
  lk_render_list rl;
  lk_render_cmd cmd;
  lk_i32 filters[3];
  lk_u8 expect[3];
  int i;

  BEGIN_TEST("image: UIP_FILTER rides on DRAW_IMAGE (default linear)");

  memset(&cmd, 0, sizeof(cmd));

  /* no prop -> linear */
  build_image_frame(ui, &ref, 32, 32);
  CHECK(build_render(ui, &rl));
  CHECK_EQ(find_draw_image(&rl, &cmd), 1);
  CHECK_EQ(cmd.img_filter, (lk_u8)LK_FILTER_LINEAR);
  lk_render_list_destroy(&rl);

  /* explicit values; an out-of-range value degrades to linear */
  filters[0] = LK_FILTER_NEAREST;
  expect[0] = (lk_u8)LK_FILTER_NEAREST;
  filters[1] = LK_FILTER_LINEAR;
  expect[1] = (lk_u8)LK_FILTER_LINEAR;
  filters[2] = 7;
  expect[2] = (lk_u8)LK_FILTER_LINEAR;

  for (i = 0; i < 3; i++) {
    lk_tree *t = lk_ui_begin_frame(ui);
    lk_ix win = lk_tree_add_node_c(t, "w", UIK_WINDOW);
    lk_ix n = lk_tree_add_node_c(t, "img", UIK_IMAGE);

    lk_tree_set_root(t, win);
    lk_tree_append_child(t, win, n);
    lk_tree_add_prop(t, n, UIP_IMAGE, lk_v_resource(ref));
    lk_tree_add_prop(t, n, UIP_FILTER, lk_v_i32(filters[i]));
    lk_ui_end_frame(ui);

    CHECK(build_render(ui, &rl));
    CHECK_EQ(find_draw_image(&rl, &cmd), 1);
    CHECK_EQ(cmd.img_filter, expect[i]);
    lk_render_list_destroy(&rl);
  }

  END_TEST();
  lk_image_destroy(img);
  lk_ui_destroy(ui);
}

static void test_image_render_degrade(void) {
  lk_ui *ui = lk_ui_create(NULL);
  lk_image *img = lk_image_new(8, 8, NULL, NULL, NULL);
  lk_resources *rs = lk_ui_resources(ui);
  lk_resource_ref ref = lk_resource_register(rs, lk_image_type(), img, "img");
  lk_resource_ref ref2;
  lk_render_list rl;
  lk_render_cmd cmd;

  BEGIN_TEST("image: degrade renders bg only; re-register renews ref");

  memset(&cmd, 0, sizeof(cmd));

  /* no prop: bg fill, no DRAW_IMAGE */
  build_image_frame(ui, NULL, 40, 40);
  CHECK(build_render(ui, &rl));
  CHECK_EQ(find_draw_image(&rl, NULL), 0);
  lk_render_list_destroy(&rl);

  /* stale ref (released between frames): same */
  lk_resource_release(rs, ref);
  build_image_frame(ui, &ref, 0, 0);
  CHECK(build_render(ui, &rl));
  CHECK_EQ(find_draw_image(&rl, NULL), 0);
  lk_render_list_destroy(&rl);

  /* re-register: the slot may be reused, the new ref draws again */
  ref2 = lk_resource_register(rs, lk_image_type(), img, "img2");
  CHECK(ref2.id != 0);
  CHECK(ref2.id != ref.id || ref2.generation != ref.generation);

  build_image_frame(ui, &ref2, 0, 0);
  CHECK(build_render(ui, &rl));
  CHECK_EQ(find_draw_image(&rl, &cmd), 1);
  CHECK_EQ(cmd.img_id, ref2.id);
  CHECK_EQ(cmd.img_gen, ref2.generation);
  lk_render_list_destroy(&rl);

  END_TEST();
  lk_image_destroy(img);
  lk_ui_destroy(ui);
}

/* ============================================================ */

void lk_image_run_tests(void) {
  printf("\nlk image tests:\n");
  test_image_basics();
  test_image_bounds();
  test_image_from_node();
  test_image_measure();
  test_image_render();
  test_image_filter();
  test_image_render_degrade();
}
