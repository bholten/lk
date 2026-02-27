/*
 * lk-menu-test.c -- the context-menu producer and popup
 * (docs/context-menu.md).
 *
 * Pinned here: the producer lists gesture translators in the click's
 * order (interior hits, node walk, then the global key translators
 * after a separator) with DSL-spelled accelerators; activation emits
 * the same command a real gesture produces; disabled subtrees
 * suppress; the popup lays out at the cursor, answers hit-tests as
 * its owner, walks with the keys, and dies to ESC, outside clicks and
 * owner GC; explicit menus emit their own commands.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <lk.h>

#include "lk-test-harness.h"

/* ---- helpers ---- */

static lk_ix find(lk_ui *ui, const char *id) {
  return lk_tree_find_by_id(lk_ui_tree(ui), lk_intern_cid(ui->intern, id));
}

static lk_str name_of(lk_ui *ui, lk_u32 sid) {
  return lk_intern_str(ui->intern, sid);
}

static int is(lk_ui *ui, lk_u32 sid, const char *s) {
  lk_str v = name_of(ui, sid);
  return v.len == strlen(s) && memcmp(v.ptr, s, v.len) == 0;
}

/* window > column(start, padding 10) > [button "cell" (present cell
 * (3 4)), button "plain" (no presentation)] */
static void build_frame(lk_ui *ui, int disable_cell, int with_cell) {
  lk_tree *t = lk_ui_begin_frame(ui);
  lk_ix w = lk_tree_add_node_c(t, "w", UIK_WINDOW);
  lk_ix col = lk_tree_add_node_c(t, "col", UIK_COLUMN);
  lk_ix plain = lk_tree_add_node_c(t, "plain", UIK_BUTTON);

  lk_tree_set_root(t, w);
  lk_tree_append_child(t, w, col);
  lk_tree_add_prop(t, col, UIP_ALIGN, lk_v_i32(LK_ALIGN_START));
  lk_tree_add_prop(t, col, UIP_PADDING, lk_v_i32(10));

  if (with_cell) {
    lk_ix cell = lk_tree_add_node_c(t, "cell", UIK_BUTTON);
    lk_value pv[2];

    lk_tree_append_child(t, col, cell);
    lk_tree_add_prop(t, cell, UIP_TEXT, lk_v_cstr(t->intern, "cell"));
    lk_tree_add_prop(t, cell, UIP_W, lk_v_i32(100));
    lk_tree_add_prop(t, cell, UIP_H, lk_v_i32(30));
    lk_tree_add_prop(t, cell, UIP_FOCUSABLE, lk_v_bool(1));

    if (disable_cell) {
      lk_tree_add_prop(t, cell, UIP_DISABLED, lk_v_bool(1));
    }

    pv[0] = lk_v_i32(3);
    pv[1] = lk_v_i32(4);
    lk_tree_add_presentation_sv(t, cell, "cell", pv, 2);
  }

  lk_tree_append_child(t, col, plain);
  lk_tree_add_prop(t, plain, UIP_TEXT, lk_v_cstr(t->intern, "plain"));
  lk_tree_add_prop(t, plain, UIP_W, lk_v_i32(100));
  lk_tree_add_prop(t, plain, UIP_H, lk_v_i32(30));
  lk_ui_end_frame(ui);
}

/* Minesweeper's table plus a reaction translator and a foreign ptype. */
static void add_translators(lk_ui *ui) {
  lk_ui_add_translator_s(ui, LK_EVENT_POINTER_DOWN, "cell", 0, 0, 0,
                          LK_POINTER_BUTTON_PRIMARY, "Reveal");
  lk_ui_add_translator_s(ui, LK_EVENT_POINTER_DOWN, "cell", 0, 0, 0,
                          LK_POINTER_BUTTON_SECONDARY, "Flag");
  lk_ui_add_translator_s(ui, LK_EVENT_POINTER_DOWN, "cell", 0, 0, 0,
                          LK_POINTER_BUTTON_MIDDLE, "Chord");
  lk_ui_add_translator_s(ui, LK_EVENT_VALUE_CHANGED, "cell", 0, 0, 0, 0,
                          "Ignore");
  lk_ui_add_translator_s(ui, LK_EVENT_POINTER_DOWN, "other", 0, 0, 0, 0,
                          "Foreign");
  lk_ui_add_translator_s(ui, LK_EVENT_KEY_DOWN, "", 0, LKK_F2, 0, 0,
                          "NewGame");
  lk_ui_add_translator_s(ui, LK_EVENT_KEY_DOWN, "", 0, LKK_Q, LK_MOD_CTRL, 0,
                          "Quit");
}

/* Lay out into the ui-owned rects with resolved styles; returns the
 * cfg used (for the overlay passes). */
static lk_style *g_styles;

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

static void route_key(lk_ui *ui, lk_u16 kc, lk_u8 mods) {
  lk_event ev;

  memset(&ev, 0, sizeof(ev));
  ev.type = LK_EVENT_KEY_DOWN;
  ev.target = lk_ui_tree(ui)->root;
  ev.data.key.keycode = kc;
  ev.mods = mods;
  lk_event_route(ui, &ev);
}

static void route_pointer(lk_ui *ui, lk_u8 type, lk_ix target, lk_i32 x,
                          lk_i32 y, lk_u8 button) {
  lk_event ev;

  memset(&ev, 0, sizeof(ev));
  ev.type = type;
  ev.target = target;
  ev.data.pointer.x = x;
  ev.data.pointer.y = y;
  ev.data.pointer.button = button;
  lk_event_route(ui, &ev);
}

/* ---- the producer ---- */

static void test_candidates_order(void) {
  lk_ui *ui = lk_ui_create(NULL);
  lk_menu_item items[16];
  lk_u32 n;

  BEGIN_TEST("menu: candidates = gestures in click order + globals");

  add_translators(ui);
  build_frame(ui, 0, 1);
  n = lk_menu_candidates(ui, lk_ui_tree(ui), find(ui, "cell"), 0, 0, items,
                         16);

  CHECK_EQ(n, 6);

  if (n == 6) {
    CHECK(is(ui, items[0].command_name, "Reveal"));
    CHECK(is(ui, items[0].accel, "click"));
    CHECK_EQ(items[0].arg_count, 2);
    CHECK(items[0].args[0].tag == UIV_I32 && items[0].args[0].as.i == 3);
    CHECK(is(ui, items[0].ptype, "cell"));
    CHECK(is(ui, items[1].command_name, "Flag"));
    CHECK(is(ui, items[1].accel, "right-click"));
    CHECK(is(ui, items[2].command_name, "Chord"));
    CHECK(is(ui, items[2].accel, "middle-click"));
    CHECK_EQ(items[3].separator, 1);
    CHECK(is(ui, items[4].command_name, "NewGame"));
    CHECK(is(ui, items[4].accel, "f2"));
    CHECK(is(ui, items[5].command_name, "Quit"));
    CHECK(is(ui, items[5].accel, "ctrl+q"));
    CHECK_EQ(items[5].node_id, lk_intern_cid(ui->intern, "w"));
    CHECK(items[0].translator_ix != LK_MENU_NO_TRANSLATOR);
  }

  /* A node with no presentation: only the globals, no separator. */
  n = lk_menu_candidates(ui, lk_ui_tree(ui), find(ui, "plain"), 0, 0, items,
                         16);
  CHECK_EQ(n, 2);
  CHECK(n == 2 && is(ui, items[0].command_name, "NewGame"));

  /* Disabled: the cell's own commands vanish, globals stay. */
  build_frame(ui, 1, 1);
  n = lk_menu_candidates(ui, lk_ui_tree(ui), find(ui, "cell"), 0, 0, items,
                         16);
  CHECK_EQ(n, 2);

  END_TEST();
  lk_ui_destroy(ui);
}

/* ---- activation = the gesture ---- */

static void test_activation_equals_gesture(void) {
  lk_ui *ui = lk_ui_create(NULL);
  lk_menu_item items[16];
  lk_command from_click;
  lk_ix cell;

  BEGIN_TEST("menu: activating an item emits the gesture's command");

  add_translators(ui);
  build_frame(ui, 0, 1);
  cell = find(ui, "cell");

  /* The real gesture: a secondary click on the cell. */
  route_pointer(ui, LK_EVENT_POINTER_DOWN, cell, 0, 0,
                LK_POINTER_BUTTON_SECONDARY);
  CHECK_EQ(lk_ui_commands(ui)->count, 1u);
  from_click = lk_ui_commands(ui)->cmds[0];
  CHECK(is(ui, from_click.name, "Flag"));
  lk_ui_clear_commands(ui);

  /* The menu's second item is Flag. */
  CHECK_EQ(lk_menu_candidates(ui, lk_ui_tree(ui), cell, 0, 0, items, 16), 6);
  CHECK(lk_menu_open(ui, lk_intern_cid(ui->intern, "cell"),
                     LK_ANCHOR_AT_CURSOR, 50, 50, items, 6) == 1);
  CHECK(lk_menu_is_open(ui));
  CHECK_EQ(lk_menu_count(ui), 6);
  CHECK_EQ(lk_menu_activate(ui, 1), 1);
  CHECK(!lk_menu_is_open(ui));
  CHECK_EQ(lk_ui_commands(ui)->count, 1u);

  if (lk_ui_commands(ui)->count == 1) {
    const lk_command *c = &lk_ui_commands(ui)->cmds[0];

    CHECK_EQ(c->name, from_click.name);
    CHECK_EQ(c->arg_count, from_click.arg_count);
    CHECK(c->args[0].as.i == from_click.args[0].as.i);
    CHECK(c->args[1].as.i == from_click.args[1].as.i);
    CHECK_EQ(c->source_node, from_click.source_node);
    CHECK_EQ(c->source_ptype, from_click.source_ptype);
  }

  /* A global: the key translator emits with the root as source. */
  lk_ui_clear_commands(ui);
  CHECK(lk_menu_open(ui, lk_intern_cid(ui->intern, "cell"),
                     LK_ANCHOR_AT_CURSOR, 50, 50, items, 6) == 1);
  CHECK_EQ(lk_menu_activate(ui, 3), 0); /* the separator */
  CHECK(lk_menu_is_open(ui));
  CHECK_EQ(lk_menu_activate(ui, 5), 1);
  CHECK(lk_ui_commands(ui)->count == 1 &&
        is(ui, lk_ui_commands(ui)->cmds[0].name, "Quit"));
  CHECK_EQ(lk_ui_commands(ui)->cmds[0].source_node, lk_ui_tree(ui)->root);

  /* Cleared translators under an open menu: activation refuses. */
  lk_ui_clear_commands(ui);
  CHECK(lk_menu_open(ui, lk_intern_cid(ui->intern, "cell"),
                     LK_ANCHOR_AT_CURSOR, 50, 50, items, 6) == 1);
  lk_ui_clear_translators(ui);
  CHECK_EQ(lk_menu_activate(ui, 0), 0);
  CHECK_EQ(lk_ui_commands(ui)->count, 0u);

  END_TEST();
  lk_ui_destroy(ui);
}

/* ---- the popup ---- */

static void test_popup_open_geometry_keys(void) {
  lk_ui *ui = lk_ui_create(NULL);
  lk_layout_cfg cfg;
  lk_rect cell_r;
  lk_render_list rl;
  lk_rect mr;
  lk_ix cell;

  BEGIN_TEST("menu: open at cursor, geometry, hit, keys, activate");

  add_translators(ui);
  build_frame(ui, 0, 1);
  cfg = layout_into_ui(ui);
  cell = find(ui, "cell");
  CHECK(lk_node_rect(ui, lk_intern_cid(ui->intern, "cell"), &cell_r));

  /* Empty space is the window: the globals apply everywhere. */
  CHECK_EQ(lk_menu_open_context(ui, lk_ui_tree(ui), 600, 400), 2);
  CHECK(lk_menu_is_open(ui));
  lk_menu_close(ui);
  /* Outside the viewport hits nothing. */
  CHECK_EQ(lk_menu_open_context(ui, lk_ui_tree(ui), 700, 400), 0);
  CHECK(!lk_menu_is_open(ui));

  /* On the cell: six items, an overlay, first item hovered. */
  CHECK_EQ(lk_menu_open_context(ui, lk_ui_tree(ui), cell_r.x + 5,
                                cell_r.y + 5),
           6);
  CHECK(lk_menu_is_open(ui));
  CHECK_EQ(lk_overlay_count(ui), 1);
  CHECK_EQ((unsigned)lk_overlay_top(ui)->kind, (unsigned)LK_OVERLAY_CONTEXT_MENU);
  CHECK_EQ(lk_menu_hover(ui), 0);

  /* Rendering the overlays resolves the popup at the cursor. */
  memset(&rl, 0, sizeof(rl));
  lk_render_build_overlays(ui, lk_ui_rects(ui), &cfg, &rl);
  CHECK(rl.count > 0);
  lk_render_list_destroy(&rl);
  mr = lk_menu_rect(ui);
  CHECK_EQ(mr.x, cell_r.x + 5);
  CHECK_EQ(mr.y, cell_r.y + 5);
  /* 5 rows of 24 + a half row + padding */
  CHECK_EQ(mr.h, 5 * 24 + 12 + 8);
  CHECK(mr.w > 100);

  /* Hit-testing inside the popup answers the owner. */
  CHECK_EQ(lk_hit_test_overlay(ui, lk_ui_rects(ui), &cfg, mr.x + 5,
                               mr.y + 5),
           cell);
  CHECK_EQ(lk_hit_test_overlay(ui, lk_ui_rects(ui), &cfg, mr.x + mr.w + 5,
                               mr.y),
           0);

  /* Keys walk, skipping the separator; letters jump. */
  route_key(ui, LKK_DOWN, 0);
  CHECK_EQ(lk_menu_hover(ui), 1);
  route_key(ui, LKK_DOWN, 0);
  route_key(ui, LKK_DOWN, 0);
  CHECK_EQ(lk_menu_hover(ui), 4); /* over the separator at 3 */
  route_key(ui, LKK_END, 0);
  CHECK_EQ(lk_menu_hover(ui), 5);
  route_key(ui, LKK_DOWN, 0);
  CHECK_EQ(lk_menu_hover(ui), 0); /* wraps */
  route_key(ui, LKK_UP, 0);
  CHECK_EQ(lk_menu_hover(ui), 5);
  route_key(ui, LKK_HOME, 0);
  CHECK_EQ(lk_menu_hover(ui), 0);
  route_key(ui, LKK_C, 0);
  CHECK_EQ(lk_menu_hover(ui), 2); /* Chord */

  /* Pointer move inside picks the row; a click activates it. */
  route_pointer(ui, LK_EVENT_POINTER_MOVE, cell, mr.x + 10, mr.y + 4 + 24 + 4,
                0);
  CHECK_EQ(lk_menu_hover(ui), 1);
  lk_ui_clear_commands(ui);
  route_pointer(ui, LK_EVENT_POINTER_DOWN, cell, mr.x + 10, mr.y + 4 + 24 + 4,
                LK_POINTER_BUTTON_PRIMARY);
  CHECK(!lk_menu_is_open(ui));
  CHECK_EQ(lk_overlay_count(ui), 0);
  CHECK(lk_ui_commands(ui)->count == 1 &&
        is(ui, lk_ui_commands(ui)->cmds[0].name, "Flag"));

  /* Return activates the hovered item. */
  lk_ui_clear_commands(ui);
  CHECK_EQ(lk_menu_open_context(ui, lk_ui_tree(ui), cell_r.x + 5,
                                cell_r.y + 5),
           6);
  route_key(ui, LKK_DOWN, 0);
  route_key(ui, LKK_DOWN, 0);
  route_key(ui, LKK_RETURN, 0);
  CHECK(!lk_menu_is_open(ui));
  CHECK(lk_ui_commands(ui)->count == 1 &&
        is(ui, lk_ui_commands(ui)->cmds[0].name, "Chord"));

  /* The keyboard opener: at the focused node's centre. */
  lk_focus_set(ui, lk_ui_tree(ui), lk_intern_cid(ui->intern, "cell"));
  CHECK_EQ(lk_menu_open_context_at_focus(ui, lk_ui_tree(ui)), 6);
  CHECK(lk_menu_is_open(ui));
  lk_menu_close(ui);
  CHECK(!lk_menu_is_open(ui));

  END_TEST();
  free(g_styles);
  g_styles = NULL;
  lk_ui_destroy(ui);
}

static void test_popup_dismissal(void) {
  lk_ui *ui = lk_ui_create(NULL);
  lk_layout_cfg cfg;
  lk_rect cell_r;
  lk_render_list rl;
  lk_rect mr;

  BEGIN_TEST("menu: ESC, outside click, owner GC all close it");

  add_translators(ui);
  build_frame(ui, 0, 1);
  cfg = layout_into_ui(ui);
  lk_node_rect(ui, lk_intern_cid(ui->intern, "cell"), &cell_r);

  /* ESC (the generic overlay pre-step). */
  CHECK_EQ(lk_menu_open_context(ui, lk_ui_tree(ui), cell_r.x + 5,
                                cell_r.y + 5),
           6);
  route_key(ui, LKK_ESCAPE, 0);
  CHECK(!lk_menu_is_open(ui));
  CHECK_EQ(lk_overlay_count(ui), 0);
  /* keys after that reach nobody special: no crash, not consumed */
  route_key(ui, LKK_DOWN, 0);

  /* Outside click through the host's dismiss pass. */
  CHECK_EQ(lk_menu_open_context(ui, lk_ui_tree(ui), cell_r.x + 5,
                                cell_r.y + 5),
           6);
  memset(&rl, 0, sizeof(rl));
  lk_render_build_overlays(ui, lk_ui_rects(ui), &cfg, &rl);
  lk_render_list_destroy(&rl);
  mr = lk_menu_rect(ui);
  CHECK_EQ(lk_overlay_dismiss_outside(ui, lk_ui_rects(ui), &cfg, mr.x + 2,
                                      mr.y + 2),
           LK_DISMISS_NONE); /* inside: nothing dismissed */
  CHECK(lk_menu_is_open(ui));
  CHECK_EQ(lk_overlay_dismiss_outside(ui, lk_ui_rects(ui), &cfg, 600, 400),
           LK_DISMISS_DISMISSED);
  CHECK(!lk_menu_is_open(ui));

  /* Owner leaves the tree: end_frame pops the overlay. */
  CHECK_EQ(lk_menu_open_context(ui, lk_ui_tree(ui), cell_r.x + 5,
                                cell_r.y + 5),
           6);
  build_frame(ui, 0, 0);
  CHECK(!lk_menu_is_open(ui));
  CHECK_EQ(lk_overlay_count(ui), 0);

  END_TEST();
  free(g_styles);
  g_styles = NULL;
  lk_ui_destroy(ui);
}

/* ---- explicit menus ---- */

static void test_explicit_menu(void) {
  lk_ui *ui = lk_ui_create(NULL);
  lk_menu_item items[4];

  BEGIN_TEST("menu: explicit items emit their own commands");

  build_frame(ui, 0, 1);
  memset(items, 0, sizeof(items));
  items[0].label = lk_intern_cid(ui->intern, "Open...");
  items[0].command_name = lk_intern_cid(ui->intern, "Open");
  items[0].enabled = 1;
  items[0].translator_ix = LK_MENU_NO_TRANSLATOR;
  items[0].node_id = lk_intern_cid(ui->intern, "plain");
  items[0].args[0] = lk_v_cstr(ui->intern, "recent");
  items[0].arg_count = 1;
  items[1].separator = 1;
  items[2].label = lk_intern_cid(ui->intern, "Save");
  items[2].command_name = lk_intern_cid(ui->intern, "Save");
  items[2].enabled = 0;
  items[2].translator_ix = LK_MENU_NO_TRANSLATOR;
  items[2].node_id = items[0].node_id;
  items[3].label = lk_intern_cid(ui->intern, "Quit");
  items[3].command_name = lk_intern_cid(ui->intern, "Quit");
  items[3].enabled = 1;
  items[3].translator_ix = LK_MENU_NO_TRANSLATOR;
  items[3].node_id = items[0].node_id;

  CHECK(lk_menu_open(ui, items[0].node_id, LK_ANCHOR_BELOW, 0, 0, items, 4));
  CHECK_EQ(lk_menu_count(ui), 4);
  CHECK_EQ(lk_menu_hover(ui), 0);
  CHECK_EQ(lk_menu_activate(ui, 2), 0); /* disabled */
  CHECK(lk_menu_is_open(ui));
  route_key(ui, LKK_DOWN, 0);
  CHECK_EQ(lk_menu_hover(ui), 3); /* over the separator and Save */
  route_key(ui, LKK_UP, 0);
  CHECK_EQ(lk_menu_hover(ui), 0);
  CHECK_EQ(lk_menu_activate(ui, 0), 1);
  CHECK(!lk_menu_is_open(ui));
  CHECK(lk_ui_commands(ui)->count == 1 &&
        is(ui, lk_ui_commands(ui)->cmds[0].name, "Open"));
  CHECK_EQ(lk_ui_commands(ui)->cmds[0].source_node, find(ui, "plain"));
  CHECK(lk_ui_commands(ui)->cmds[0].arg_count == 1 &&
        is(ui, lk_ui_commands(ui)->cmds[0].args[0].as.str_id, "recent"));

  /* Opening replaces: two opens leave one overlay. */
  CHECK(lk_menu_open(ui, items[0].node_id, LK_ANCHOR_BELOW, 0, 0, items, 4));
  CHECK(lk_menu_open(ui, items[0].node_id, LK_ANCHOR_BELOW, 0, 0, items, 2));
  CHECK_EQ(lk_overlay_count(ui), 1);
  CHECK_EQ(lk_menu_count(ui), 2);

  END_TEST();
  lk_ui_destroy(ui);
}

/* ---- interior presentations ---- */

static void test_interior_candidates(void) {
  lk_ui *ui = lk_ui_create(NULL);
  lk_spans *sp = lk_spans_new(NULL, NULL, NULL);
  lk_resources *rs = lk_ui_resources(ui);
  lk_resource_ref ref = lk_resource_register(rs, lk_spans_type(), sp, "sp");
  lk_menu_item items[16];
  lk_u32 n;
  lk_ix st;

  BEGIN_TEST("menu: interior (styled text) candidates come first, with hits");

  lk_ui_add_translator_s(ui, LK_EVENT_POINTER_DOWN, "loc", 0, 0, 0,
                          LK_POINTER_BUTTON_PRIMARY, "Open");
  lk_ui_add_translator_s(ui, LK_EVENT_POINTER_DOWN, "loc", 0, 0, 0,
                          LK_POINTER_BUTTON_MIDDLE, "Copy");
  lk_ui_add_translator_s(ui, LK_EVENT_POINTER_DOWN, "para", 0, 0, 0, 0,
                          "Select");
  lk_ui_add_translator_s(ui, LK_EVENT_KEY_DOWN, "", 0, LKK_Q, LK_MOD_CTRL, 0,
                          "Quit");

  {
    lk_tree *t = lk_ui_begin_frame(ui);
    lk_ix w = lk_tree_add_node_c(t, "w", UIK_WINDOW);
    lk_ix col = lk_tree_add_node_c(t, "col", UIK_COLUMN);
    lk_ix p = lk_tree_add_node_c(t, "st", UIK_STYLED_TEXT);
    lk_value pv[1];

    lk_tree_set_root(t, w);
    lk_tree_append_child(t, w, col);
    lk_tree_append_child(t, col, p);
    lk_tree_add_prop(t, col, UIP_ALIGN, lk_v_i32(LK_ALIGN_START));
    lk_tree_add_prop(t, col, UIP_PADDING, lk_v_i32(10));
    lk_tree_add_prop(t, p, UIP_W, lk_v_i32(200));
    lk_tree_add_prop(t, p, UIP_TEXT, lk_v_cstr(t->intern, "see a.c:7 now"));
    lk_tree_add_prop(t, p, UIP_SPANS, lk_v_resource(ref));
    pv[0] = lk_v_cstr(t->intern, "p1");
    lk_tree_add_presentation_sv(t, p, "para", pv, 1);
    lk_ui_end_frame(ui);
  }

  /* "a.c:7" = [4, 9) presents loc */
  lk_spans_add_present(sp, 4, 9, lk_intern_cid(ui->intern, "loc"),
                       lk_v_cstr(ui->intern, "a.c:7"));
  layout_into_ui(ui);
  st = find(ui, "st");

  /* Over the location: Open, Copy (interior), Select (node), sep, Quit. */
  n = lk_menu_candidates(ui, lk_ui_tree(ui), st, 10 + 5 * 8, 10 + 4, items,
                         16);
  CHECK_EQ(n, 5);

  if (n == 5) {
    CHECK(is(ui, items[0].command_name, "Open"));
    CHECK(is(ui, items[0].hit.locus_kind, "text-range"));
    CHECK_EQ(items[0].hit.locus[0], 4);
    CHECK_EQ(items[0].hit.locus[1], 9);
    CHECK(is(ui, items[1].command_name, "Copy"));
    CHECK(is(ui, items[2].command_name, "Select"));
    CHECK_EQ(items[3].separator, 1);
    CHECK(is(ui, items[4].command_name, "Quit"));
  }

  /* Over plain text: no interior items. */
  n = lk_menu_candidates(ui, lk_ui_tree(ui), st, 10 + 1 * 8, 10 + 4, items,
                         16);
  CHECK_EQ(n, 3);
  CHECK(n == 3 && is(ui, items[0].command_name, "Select"));

  /* Activating the interior item carries the hit into the command. */
  CHECK_EQ(lk_menu_open_context(ui, lk_ui_tree(ui), 10 + 5 * 8, 10 + 4), 5);
  lk_ui_clear_commands(ui);
  CHECK_EQ(lk_menu_activate(ui, 1), 1);
  CHECK(lk_ui_commands(ui)->count == 1);

  if (lk_ui_commands(ui)->count == 1) {
    const lk_command *c = &lk_ui_commands(ui)->cmds[0];

    CHECK(is(ui, c->name, "Copy"));
    CHECK_EQ(c->hit.locus[0], 4);
    CHECK_EQ(c->hit.locus[1], 9);
    CHECK(c->arg_count == 1 && is(ui, c->args[0].as.str_id, "a.c:7"));
  }

  END_TEST();
  free(g_styles);
  g_styles = NULL;
  lk_resource_release(rs, ref);
  lk_spans_destroy(sp);
  lk_ui_destroy(ui);
}

void lk_menu_run_tests(void) {
  printf("\nmenu tests:\n");
  test_candidates_order();
  test_activation_equals_gesture();
  test_popup_open_geometry_keys();
  test_popup_dismissal();
  test_explicit_menu();
  test_interior_candidates();
}
