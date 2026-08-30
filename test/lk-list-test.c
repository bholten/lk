/*
 * lk-list-test.c -- UIK_LIST, the virtualized list (docs/table.md).
 *
 * Pinned here: rows are placed by index inside a rows x row_h extent
 * and unplaced without one; the visible window in the geometry
 * scratch (and lk_list_range) tracks the scroll offset; wheel, bar
 * drag / track paging and keys scroll and move the cursor with
 * VALUE_CHANGED; a click bubbling from a row moves the cursor without
 * consuming; CONTROLLED suppresses the state write; scroll_to_row.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <lk.h>

#include "lk-test-harness.h"

static lk_style *g_styles;

static lk_ix find(lk_ui *ui, const char *id) {
  return lk_tree_find_by_id(lk_ui_tree(ui), lk_intern_cid(ui->intern, id));
}

/* window > column(start, padding 10) > list "l" (rows, row_h 20, h
 * 100, w 200, focusable) > materialized rows [from, to) as buttons
 * "r<i>" presenting item (i), plus a rowless child "stray". */
static void build_frame(lk_ui *ui, lk_i32 rows, lk_i32 from, lk_i32 to,
                        int controlled, lk_i32 value) {
  lk_tree *t = lk_ui_begin_frame(ui);
  lk_ix w = lk_tree_add_node_c(t, "w", UIK_WINDOW);
  lk_ix col = lk_tree_add_node_c(t, "col", UIK_COLUMN);
  lk_ix l = lk_tree_add_node_c(t, "l", UIK_LIST);
  lk_ix stray = lk_tree_add_node_c(t, "stray", UIK_LABEL);
  lk_i32 i;

  lk_tree_set_root(t, w);
  lk_tree_append_child(t, w, col);
  lk_tree_append_child(t, col, l);
  lk_tree_add_prop(t, col, UIP_ALIGN, lk_v_i32(LK_ALIGN_START));
  lk_tree_add_prop(t, col, UIP_PADDING, lk_v_i32(10));
  lk_tree_add_prop(t, l, UIP_ROWS, lk_v_i32(rows));
  lk_tree_add_prop(t, l, UIP_ROW_H, lk_v_i32(20));
  lk_tree_add_prop(t, l, UIP_W, lk_v_i32(200));
  lk_tree_add_prop(t, l, UIP_H, lk_v_i32(100));
  lk_tree_add_prop(t, l, UIP_PADDING, lk_v_i32(0));
  lk_tree_add_prop(t, l, UIP_FOCUSABLE, lk_v_bool(1));

  if (controlled) {
    lk_tree_add_prop(t, l, UIP_CONTROLLED, lk_v_i32(1));
  }

  if (value >= 0) {
    lk_tree_add_prop(t, l, UIP_VALUE, lk_v_i32(value));
  }

  for (i = from; i < to; i++) {
    char id[16];
    lk_ix r;
    lk_value pv[1];

    sprintf(id, "r%d", (int)i);
    r = lk_tree_add_node_c(t, id, UIK_BUTTON);
    lk_tree_append_child(t, l, r);
    lk_tree_add_prop(t, r, UIP_ROW, lk_v_i32(i));
    lk_tree_add_prop(t, r, UIP_TEXT, lk_v_cstr(t->intern, id));
    pv[0] = lk_v_i32(i);
    lk_tree_add_presentation_sv(t, r, "item", pv, 1);
  }

  lk_tree_append_child(t, l, stray);
  lk_tree_add_prop(t, stray, UIP_TEXT, lk_v_cstr(t->intern, "stray"));
  lk_ui_end_frame(ui);
}

static lk_layout_cfg layout_into_ui(lk_ui *ui) {
  lk_layout_cfg cfg;
  const lk_tree *cur = lk_ui_tree(ui);

  free(g_styles);
  g_styles = (lk_style *)malloc(sizeof(lk_style) * cur->node_count);
  lk_style_resolve(lk_ui_theme(ui), cur, NULL, g_styles);
  memset(&cfg, 0, sizeof(cfg));
  cfg.text = lk_text_backend_stub();
  cfg.viewport_w = 640;
  cfg.viewport_h = 480;
  cfg.styles = g_styles;
  cfg.state = lk_ui_state(ui);
  cfg.geom = lk_ui_geom(ui);
  lk_ui_set_text_backend(ui, lk_text_backend_stub());
  lk_layout(cur, &cfg, lk_ui_rects(ui));

  return cfg;
}

static void route_key(lk_ui *ui, lk_ix target, lk_u16 kc) {
  lk_event ev;

  memset(&ev, 0, sizeof(ev));
  ev.type = LK_EVENT_KEY_DOWN;
  ev.target = target;
  ev.data.key.keycode = kc;
  lk_event_route(ui, &ev);
}

static void route_pointer(lk_ui *ui, lk_u8 type, lk_ix target, lk_i32 x,
                          lk_i32 y) {
  lk_event ev;

  memset(&ev, 0, sizeof(ev));
  ev.type = type;
  ev.target = target;
  ev.data.pointer.x = x;
  ev.data.pointer.y = y;
  ev.data.pointer.button = LK_POINTER_BUTTON_PRIMARY;
  lk_event_route(ui, &ev);
}

static void route_wheel(lk_ui *ui, lk_ix target, lk_i32 dy) {
  lk_event ev;

  memset(&ev, 0, sizeof(ev));
  ev.type = LK_EVENT_WHEEL;
  ev.target = target;
  ev.data.wheel.dy = dy;
  lk_event_route(ui, &ev);
}

static lk_i32 scroll_y(lk_ui *ui) {
  lk_value v = lk_state_get(lk_ui_state(ui), lk_intern_cid(ui->intern, "l"),
                            LKS_SCROLL_Y);
  return v.tag == UIV_I32 ? (lk_i32)v.as.i : 0;
}

static int last_value(lk_ui *ui, const char *want) {
  const lk_command_queue *q = lk_ui_commands(ui);
  lk_str s;

  if (q->count == 0 || q->cmds[q->count - 1].source_value.tag != UIV_STR) {
    return 0;
  }

  s = lk_intern_str(ui->intern, q->cmds[q->count - 1].source_value.as.str_id);
  return s.len == strlen(want) && memcmp(s.ptr, want, s.len) == 0;
}

/* ---- placement + window ---- */

static void test_list_placement_and_window(void) {
  lk_ui *ui = lk_ui_create(NULL);
  lk_rect *r;
  lk_i32 first = -1, count = -1;
  lk_widget_geom *g;

  BEGIN_TEST("list: rows placed by index, window reported, stray unplaced");

  /* 100 rows x 20 px in a 100 px viewport: 5 rows visible. */
  build_frame(ui, 100, 0, 8, 0, -1);
  layout_into_ui(ui);
  r = lk_ui_rects(ui);
  g = lk_ui_geom(ui);

  CHECK_EQ(r[find(ui, "l")].h, 100);
  /* the bar takes 6 px of the 200 */
  CHECK_EQ(r[find(ui, "r0")].w, 194);
  CHECK_EQ(r[find(ui, "r0")].y, r[find(ui, "l")].y);
  CHECK_EQ(r[find(ui, "r3")].y, r[find(ui, "l")].y + 60);
  CHECK_EQ(r[find(ui, "r7")].y, r[find(ui, "l")].y + 140); /* placed, clipped */
  CHECK_EQ(r[find(ui, "stray")].w, 0);
  CHECK(lk_list_range(ui, lk_intern_cid(ui->intern, "l"), &first, &count));
  CHECK_EQ(first, 0);
  CHECK_EQ(count, 5);
  CHECK_EQ(g[find(ui, "l")].list.max, 2000 - 100);
  CHECK(!lk_list_range(ui, lk_intern_cid(ui->intern, "col"), &first, &count));

  /* Scroll 30 px: rows 1..5 intersect (6 rows: 1 partial top, 5 partial
   * bottom) -> first 1, count 6. */
  lk_state_set(lk_ui_state(ui), lk_intern_cid(ui->intern, "l"), LKS_SCROLL_Y,
               lk_v_i32(30));
  build_frame(ui, 100, 0, 8, 0, -1);
  layout_into_ui(ui);
  r = lk_ui_rects(ui);
  CHECK(lk_list_range(ui, lk_intern_cid(ui->intern, "l"), &first, &count));
  CHECK_EQ(first, 1);
  CHECK_EQ(count, 6);
  CHECK_EQ(r[find(ui, "r2")].y, r[find(ui, "l")].y + 40 - 30);

  /* Fewer rows than the viewport: no bar, full width, clamped offset. */
  lk_state_set(lk_ui_state(ui), lk_intern_cid(ui->intern, "l"), LKS_SCROLL_Y,
               lk_v_i32(999));
  build_frame(ui, 3, 0, 3, 0, -1);
  layout_into_ui(ui);
  r = lk_ui_rects(ui);
  CHECK_EQ(r[find(ui, "r0")].w, 200);
  CHECK_EQ(scroll_y(ui), 0);
  CHECK(lk_list_range(ui, lk_intern_cid(ui->intern, "l"), &first, &count));
  CHECK_EQ(count, 3);

  END_TEST();
  free(g_styles);
  g_styles = NULL;
  lk_ui_destroy(ui);
}

/* ---- scrolling ---- */

static void test_list_wheel_bar_scroll_to(void) {
  lk_ui *ui = lk_ui_create(NULL);
  lk_rect *r;
  lk_ix l;
  lk_rect lr;

  BEGIN_TEST("list: wheel, bar drag / paging, scroll_to_row");

  build_frame(ui, 100, 0, 8, 0, -1);
  layout_into_ui(ui);
  r = lk_ui_rects(ui);
  l = find(ui, "l");
  lr = r[l];

  route_wheel(ui, l, -1);
  CHECK_EQ(scroll_y(ui), 30);
  route_wheel(ui, l, 5);
  CHECK_EQ(scroll_y(ui), 0);

  /* Track click below the thumb pages down by the viewport. */
  route_pointer(ui, LK_EVENT_POINTER_DOWN, l, lr.x + lr.w - 3, lr.y + 90);
  CHECK_EQ(scroll_y(ui), 100);
  CHECK_EQ(ui->focused_id, lk_intern_cid(ui->intern, "l"));

  /* Thumb drag: grab the thumb, move down 46 px of the 92 px travel
   * (100 px track, 8 px min thumb) -> half of max. */
  build_frame(ui, 100, 0, 8, 0, -1);
  layout_into_ui(ui);
  lk_state_set(lk_ui_state(ui), lk_intern_cid(ui->intern, "l"), LKS_SCROLL_Y,
               lk_v_i32(0));
  layout_into_ui(ui);
  route_pointer(ui, LK_EVENT_POINTER_DOWN, l, lr.x + lr.w - 3, lr.y + 4);
  CHECK_EQ(lk_capture_current(ui), lk_intern_cid(ui->intern, "l"));
  route_pointer(ui, LK_EVENT_POINTER_MOVE, l, lr.x + lr.w - 3, lr.y + 4 + 46);
  CHECK_EQ(scroll_y(ui), 950);
  route_pointer(ui, LK_EVENT_POINTER_UP, l, lr.x + lr.w - 3, lr.y + 50);
  CHECK_EQ(lk_capture_current(ui), 0);

  /* scroll_to_row: already visible = no change; below = bottom-aligned;
   * above = top-aligned; out of range = 0. */
  lk_state_set(lk_ui_state(ui), lk_intern_cid(ui->intern, "l"), LKS_SCROLL_Y,
               lk_v_i32(0));
  layout_into_ui(ui);
  CHECK_EQ(lk_list_scroll_to_row(ui, lk_intern_cid(ui->intern, "l"), 2), 1);
  CHECK_EQ(scroll_y(ui), 0);
  CHECK_EQ(lk_list_scroll_to_row(ui, lk_intern_cid(ui->intern, "l"), 9), 1);
  CHECK_EQ(scroll_y(ui), 100); /* row 9 bottom at 200 - 100 */
  CHECK_EQ(lk_list_scroll_to_row(ui, lk_intern_cid(ui->intern, "l"), 1), 1);
  CHECK_EQ(scroll_y(ui), 20);
  CHECK_EQ(lk_list_scroll_to_row(ui, lk_intern_cid(ui->intern, "l"), 100), 0);
  CHECK_EQ(lk_list_scroll_to_row(ui, lk_intern_cid(ui->intern, "col"), 1), 0);

  END_TEST();
  free(g_styles);
  g_styles = NULL;
  lk_ui_destroy(ui);
}

/* ---- the cursor row ---- */

static void test_list_cursor_keys_and_click(void) {
  lk_ui *ui = lk_ui_create(NULL);
  lk_ix l;
  lk_ix r3;

  BEGIN_TEST("list: keys move the cursor + VALUE_CHANGED, row click sets it");

  lk_ui_add_translator_s(ui, LK_EVENT_VALUE_CHANGED, "rows", 0, 0, 0, 0,
                         "Cursor");
  lk_ui_add_translator_s(ui, LK_EVENT_POINTER_DOWN, "item", 0, 0, 0, 0, "Pick");
  build_frame(ui, 100, 0, 8, 0, -1);
  {
    /* present `rows` on the list so its value_changed becomes Cursor */
    lk_tree *t = lk_ui_begin_frame(ui);
    lk_ix w = lk_tree_add_node_c(t, "w", UIK_WINDOW);
    lk_ix col = lk_tree_add_node_c(t, "col", UIK_COLUMN);
    lk_ix li = lk_tree_add_node_c(t, "l", UIK_LIST);
    lk_i32 i;
    lk_value pv[1];

    lk_tree_set_root(t, w);
    lk_tree_append_child(t, w, col);
    lk_tree_append_child(t, col, li);
    lk_tree_add_prop(t, col, UIP_ALIGN, lk_v_i32(LK_ALIGN_START));
    lk_tree_add_prop(t, col, UIP_PADDING, lk_v_i32(10));
    lk_tree_add_prop(t, li, UIP_ROWS, lk_v_i32(100));
    lk_tree_add_prop(t, li, UIP_ROW_H, lk_v_i32(20));
    lk_tree_add_prop(t, li, UIP_W, lk_v_i32(200));
    lk_tree_add_prop(t, li, UIP_H, lk_v_i32(100));
    lk_tree_add_prop(t, li, UIP_PADDING, lk_v_i32(0));
    lk_tree_add_prop(t, li, UIP_FOCUSABLE, lk_v_bool(1));
    pv[0] = lk_v_cstr(t->intern, "l");
    lk_tree_add_presentation_sv(t, li, "rows", pv, 1);

    for (i = 0; i < 8; i++) {
      char id[16];
      lk_ix rr;
      lk_value rv[1];

      sprintf(id, "r%d", (int)i);
      rr = lk_tree_add_node_c(t, id, UIK_BUTTON);
      lk_tree_append_child(t, li, rr);
      lk_tree_add_prop(t, rr, UIP_ROW, lk_v_i32(i));
      lk_tree_add_prop(t, rr, UIP_TEXT, lk_v_cstr(t->intern, id));
      rv[0] = lk_v_i32(i);
      lk_tree_add_presentation_sv(t, rr, "item", rv, 1);
    }

    lk_ui_end_frame(ui);
  }
  layout_into_ui(ui);
  l = find(ui, "l");
  r3 = find(ui, "r3");
  lk_focus_set(ui, lk_ui_tree(ui), lk_intern_cid(ui->intern, "l"));
  lk_ui_flush_events(ui, lk_ui_tree(ui));
  lk_ui_clear_commands(ui);

  CHECK_EQ(lk_list_cursor(lk_ui_tree(ui), l, lk_ui_state(ui)), -1);
  route_key(ui, l, LKK_DOWN);
  CHECK_EQ(lk_list_cursor(lk_ui_tree(ui), l, lk_ui_state(ui)), 0);
  CHECK(last_value(ui, "0"));
  route_key(ui, l, LKK_DOWN);
  route_key(ui, l, LKK_DOWN);
  CHECK_EQ(lk_list_cursor(lk_ui_tree(ui), l, lk_ui_state(ui)), 2);
  CHECK(last_value(ui, "2"));
  route_key(ui, l, LKK_PAGEDOWN); /* 5 rows per page */
  CHECK_EQ(lk_list_cursor(lk_ui_tree(ui), l, lk_ui_state(ui)), 7);
  CHECK_EQ(scroll_y(ui), 60); /* row 7 bottom (160) - 100 */
  route_key(ui, l, LKK_END);
  CHECK_EQ(lk_list_cursor(lk_ui_tree(ui), l, lk_ui_state(ui)), 99);
  CHECK_EQ(scroll_y(ui), 1900);
  route_key(ui, l, LKK_HOME);
  CHECK_EQ(lk_list_cursor(lk_ui_tree(ui), l, lk_ui_state(ui)), 0);
  CHECK_EQ(scroll_y(ui), 0);
  route_key(ui, l, LKK_UP); /* clamps */
  CHECK_EQ(lk_list_cursor(lk_ui_tree(ui), l, lk_ui_state(ui)), 0);

  /* Click on row 3: the cursor moves AND the row's Pick still fires. */
  lk_ui_clear_commands(ui);
  route_pointer(ui, LK_EVENT_POINTER_DOWN, r3, 50, lk_ui_rects(ui)[r3].y + 5);
  CHECK_EQ(lk_list_cursor(lk_ui_tree(ui), l, lk_ui_state(ui)), 3);
  {
    const lk_command_queue *q = lk_ui_commands(ui);
    int pick = 0, cursor = 0;
    lk_u32 i;

    for (i = 0; i < q->count; i++) {
      if (q->cmds[i].name == lk_intern_cid(ui->intern, "Pick")) {
        pick = 1;
      }

      if (q->cmds[i].name == lk_intern_cid(ui->intern, "Cursor")) {
        cursor = 1;
      }
    }

    CHECK(pick && cursor);
  }

  END_TEST();
  free(g_styles);
  g_styles = NULL;
  lk_ui_destroy(ui);
}

static void test_list_controlled_and_value(void) {
  lk_ui *ui = lk_ui_create(NULL);
  lk_ix l;

  BEGIN_TEST("list: VALUE initial cursor; CONTROLLED writes no state");

  build_frame(ui, 100, 0, 8, 1, 4);
  layout_into_ui(ui);
  l = find(ui, "l");
  CHECK_EQ(lk_list_cursor(lk_ui_tree(ui), l, lk_ui_state(ui)), 4);
  route_key(ui, l, LKK_DOWN);
  /* proposed 5, state untouched: still 4 from the prop */
  CHECK_EQ(lk_list_cursor(lk_ui_tree(ui), l, lk_ui_state(ui)), 4);
  CHECK(lk_state_get(lk_ui_state(ui), lk_intern_cid(ui->intern, "l"),
                     LKS_CURSOR_ROW)
            .tag == UIV_NONE);

  /* Uncontrolled: the value prop seeds, keys move on top of it. */
  build_frame(ui, 100, 0, 8, 0, 4);
  layout_into_ui(ui);
  l = find(ui, "l");
  route_key(ui, l, LKK_DOWN);
  CHECK_EQ(lk_list_cursor(lk_ui_tree(ui), l, lk_ui_state(ui)), 5);

  END_TEST();
  free(g_styles);
  g_styles = NULL;
  lk_ui_destroy(ui);
}

void lk_list_run_tests(void) {
  printf("\nlist tests:\n");
  test_list_placement_and_window();
  test_list_wheel_bar_scroll_to();
  test_list_cursor_keys_and_click();
  test_list_controlled_and_value();
}
