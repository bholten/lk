/*
 * lk-forms-test.c -- the forms widgets round (docs/forms-widgets.md):
 * UIK_CHECKBOX, UIK_RADIO, UIK_SLIDER, UIK_TABS/UIK_TAB, UIK_GRID,
 * plus the generic UIP_CONTROLLED flag and the accent style field.
 *
 * All geometry runs against the stub text backend (8 px per
 * codepoint, line height 16) with NULL styles (padding/gap 0, so the
 * pixel arithmetic in the comments is exact); the render smoke tests
 * resolve the default theme.  VALUE_CHANGED emissions are observed
 * the way apps see them: presentation + value_changed translator ->
 * command whose source_value carries the payload.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <lk.h>

#include "core/lk-check.h"
#include "core/lk-slider.h"
#include "core/lk-tabs.h"
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

static void click(lk_ui *ui, lk_ix target, lk_i32 x, lk_i32 y, lk_u8 button,
                  lk_event *out) {
  memset(out, 0, sizeof(*out));
  out->type = LK_EVENT_POINTER_DOWN;
  out->target = target;
  out->data.pointer.x = x;
  out->data.pointer.y = y;
  out->data.pointer.button = button;
  lk_event_route(ui, out);
}

static void key(lk_ui *ui, lk_ix target, lk_u16 keycode, lk_u8 mods,
                lk_event *out) {
  memset(out, 0, sizeof(*out));
  out->type = LK_EVENT_KEY_DOWN;
  out->target = target;
  out->mods = mods;
  out->data.key.keycode = keycode;
  lk_event_route(ui, out);
}

/* Last command's source_value as a C string ("" when none / not a
 * string).  buf must hold >= 32 bytes. */
static const char *last_value(lk_ui *ui, char *buf) {
  const lk_command_queue *q = lk_ui_commands(ui);

  buf[0] = 0;

  if (q->count > 0) {
    const lk_command *cmd = &q->cmds[q->count - 1];

    if (cmd->source_value.tag == UIV_STR) {
      lk_str s = lk_intern_str(ui->intern, cmd->source_value.as.str_id);
      size_t n = s.len < 31 ? s.len : 31;

      memcpy(buf, s.ptr, n);
      buf[n] = 0;
    }
  }

  return buf;
}

static int state_i32(lk_ui *ui, const char *id, lk_state_key k, int def) {
  lk_value v = lk_state_get(lk_ui_state(ui), lk_intern_cid(ui->intern, id), k);

  return v.tag == UIV_I32 ? (int)v.as.i : def;
}

static int has_state(lk_ui *ui, const char *id, lk_state_key k) {
  lk_value v = lk_state_get(lk_ui_state(ui), lk_intern_cid(ui->intern, id), k);

  return v.tag != UIV_NONE;
}

/* Count DRAW_TEXT commands for a given interned string. */
static int count_text(lk_ui *ui, const lk_render_list *rl, const char *s) {
  lk_u32 sid = lk_intern_cid(ui->intern, s);
  lk_u32 i;
  int n = 0;

  for (i = 0; i < rl->count; i++) {
    if (rl->cmds[i].op == LK_ROP_DRAW_TEXT && rl->cmds[i].str_id == sid) {
      n++;
    }
  }

  return n;
}

/* ============================================================
 * Checkbox
 * ============================================================ */

/* window > checkbox "cb" (text "On", focusable, presentation "flag") */
static lk_ui *make_checkbox_ui(int controlled, int checked_prop, int disabled) {
  lk_ui *ui = lk_ui_create(NULL);
  lk_tree *t = lk_ui_begin_frame(ui);
  lk_ix w = lk_tree_add_node_c(t, "w", UIK_WINDOW);
  lk_ix cb = lk_tree_add_node_c(t, "cb", UIK_CHECKBOX);

  lk_tree_set_root(t, w);
  lk_tree_append_child(t, w, cb);
  lk_tree_add_prop(t, cb, UIP_TEXT, lk_v_cstr(ui->intern, "On"));
  lk_tree_add_prop(t, cb, UIP_FOCUSABLE, lk_v_bool(1));

  if (controlled) {
    lk_tree_add_prop(t, cb, UIP_CONTROLLED, lk_v_i32(1));
  }

  if (checked_prop >= 0) {
    lk_tree_add_prop(t, cb, UIP_CHECKED, lk_v_bool(checked_prop));
  }

  if (disabled) {
    lk_tree_add_prop(t, cb, UIP_DISABLED, lk_v_bool(1));
  }

  lk_tree_add_presentation_s(t, cb, "flag", lk_v_i32(0));
  lk_ui_end_frame(ui);
  lk_ui_add_translator_s(ui, LK_EVENT_VALUE_CHANGED, "flag", 0, 0, 0, 0,
                         "Flag");

  return ui;
}

static void test_checkbox_measure(void) {
  /* stub: "On" = 16x16; box = text height 16; no styles: gap 0,
   * inset 0 -> 32 x 16.  Under a column the checkbox keeps its
   * measured size on the main axis. */
  lk_ui *ui = lk_ui_create(NULL);
  lk_tree *t = lk_ui_begin_frame(ui);
  lk_ix w = lk_tree_add_node_c(t, "w", UIK_WINDOW);
  lk_ix col = lk_tree_add_node_c(t, "col", UIK_COLUMN);
  lk_ix cb = lk_tree_add_node_c(t, "cb", UIK_CHECKBOX);
  lk_ix cb2 = lk_tree_add_node_c(t, "cb2", UIK_CHECKBOX);
  lk_rect *r;

  BEGIN_TEST("checkbox: measure = box + text");

  lk_tree_set_root(t, w);
  lk_tree_append_child(t, w, col);
  lk_tree_append_child(t, col, cb);
  lk_tree_append_child(t, col, cb2);
  lk_tree_add_prop(t, cb, UIP_TEXT, lk_v_cstr(ui->intern, "On"));
  lk_ui_end_frame(ui);

  r = layout_ui(ui, 400, 300);
  CHECK(r != NULL);

  if (r) {
    lk_ix c1 = find(ui, "cb");
    lk_ix c2 = find(ui, "cb2");

    CHECK_EQ(r[c1].h, 16);
    /* no text: box = the backend's line height (BOX_MIN only without
     * a backend) */
    CHECK_EQ(r[c2].h, 16);
    CHECK_EQ(r[c2].y, 16);
    free(r);
  }

  END_TEST();
  lk_ui_destroy(ui);
}

static void test_checkbox_click_toggles(void) {
  lk_ui *ui = make_checkbox_ui(0, -1, 0);
  lk_rect *r;
  lk_event ev;
  char buf[32];

  BEGIN_TEST("checkbox: click toggles state + emits 1/0");

  r = layout_ui(ui, 400, 300);
  CHECK(r != NULL);

  if (r) {
    lk_ix cb = find(ui, "cb");
    const lk_tree *cur = lk_ui_tree(ui);

    CHECK_EQ(lk_check_effective(cur, cb, lk_ui_state(ui)), 0);
    CHECK_EQ(lk_hit_test(cur, r, 5, 5), cb);

    click(ui, cb, 5, 5, LK_POINTER_BUTTON_PRIMARY, &ev);
    CHECK_EQ((unsigned)ev.handled, 1u);
    CHECK_EQ(state_i32(ui, "cb", LKS_CHECKED, -1), 1);
    CHECK_EQ(lk_check_effective(cur, cb, lk_ui_state(ui)), 1);
    CHECK_EQ(lk_ui_commands(ui)->count, 1u);
    CHECK(strcmp(last_value(ui, buf), "1") == 0);
    /* consumed click took focus (focusable) */
    CHECK_EQ(ui->focused_id, lk_intern_cid(ui->intern, "cb"));

    click(ui, cb, 5, 5, LK_POINTER_BUTTON_ANY, &ev);
    CHECK_EQ(state_i32(ui, "cb", LKS_CHECKED, -1), 0);
    CHECK_EQ(lk_ui_commands(ui)->count, 2u);
    CHECK(strcmp(last_value(ui, buf), "0") == 0);

    /* SPACE while focused toggles too; SPACE with a modifier bubbles */
    key(ui, cb, LKK_SPACE, 0, &ev);
    CHECK_EQ((unsigned)ev.handled, 1u);
    CHECK_EQ(state_i32(ui, "cb", LKS_CHECKED, -1), 1);
    key(ui, cb, LKK_SPACE, LK_MOD_CTRL, &ev);
    CHECK_EQ((unsigned)ev.handled, 0u);
    CHECK_EQ(state_i32(ui, "cb", LKS_CHECKED, -1), 1);

    /* secondary button bubbles (translators may want it) */
    click(ui, cb, 5, 5, LK_POINTER_BUTTON_SECONDARY, &ev);
    CHECK_EQ((unsigned)ev.handled, 0u);
    CHECK_EQ(state_i32(ui, "cb", LKS_CHECKED, -1), 1);

    free(r);
  }

  END_TEST();
  lk_ui_destroy(ui);
}

static void test_checkbox_prop_and_controlled(void) {
  lk_ui *ui = make_checkbox_ui(1, 1, 0);
  lk_rect *r;
  lk_event ev;
  char buf[32];

  BEGIN_TEST("checkbox: prop initial; controlled = no state");

  r = layout_ui(ui, 400, 300);
  CHECK(r != NULL);

  if (r) {
    lk_ix cb = find(ui, "cb");
    const lk_tree *cur = lk_ui_tree(ui);

    /* UIP_CHECKED = 1 with no state: effective 1 */
    CHECK_EQ(lk_check_effective(cur, cb, lk_ui_state(ui)), 1);

    click(ui, cb, 5, 5, LK_POINTER_BUTTON_PRIMARY, &ev);
    CHECK_EQ((unsigned)ev.handled, 1u);
    CHECK_EQ(has_state(ui, "cb", LKS_CHECKED), 0);
    CHECK(strcmp(last_value(ui, buf), "0") == 0);
    /* app didn't re-supply the prop: still 1 (one-frame lag model) */
    CHECK_EQ(lk_check_effective(cur, cb, lk_ui_state(ui)), 1);

    free(r);
  }

  END_TEST();
  lk_ui_destroy(ui);
}

static void test_checkbox_disabled(void) {
  lk_ui *ui = make_checkbox_ui(0, -1, 1);
  lk_rect *r;
  lk_event ev;

  BEGIN_TEST("checkbox: disabled ignores clicks and keys");

  r = layout_ui(ui, 400, 300);
  CHECK(r != NULL);

  if (r) {
    lk_ix cb = find(ui, "cb");

    click(ui, cb, 5, 5, LK_POINTER_BUTTON_PRIMARY, &ev);
    CHECK_EQ((unsigned)ev.handled, 0u);
    key(ui, cb, LKK_SPACE, 0, &ev);
    CHECK_EQ((unsigned)ev.handled, 0u);
    CHECK_EQ(has_state(ui, "cb", LKS_CHECKED), 0);
    CHECK_EQ(lk_ui_commands(ui)->count, 0u);
    free(r);
  }

  END_TEST();
  lk_ui_destroy(ui);
}

static void test_checkbox_render(void) {
  /* Default theme: transparent bg, 1 px outline (4 fills), the accent
   * mark when checked, then the label. */
  lk_ui *ui = make_checkbox_ui(0, 1, 0);
  lk_rect *r;

  BEGIN_TEST("checkbox: render outline + mark + text");

  r = layout_ui(ui, 400, 300);
  CHECK(r != NULL);

  if (r) {
    const lk_tree *cur = lk_ui_tree(ui);
    lk_style *styles = (lk_style *)malloc(sizeof(lk_style) * cur->node_count);
    lk_render_list rl;
    lk_u32 i;
    int fills = 0;
    int accent_fill = 0;

    memset(&rl, 0, sizeof(rl));
    lk_style_resolve(lk_ui_theme(ui), cur, NULL, styles);
    lk_render_build(cur, r, styles, lk_ui_state(ui), lk_ui_geom(ui), &rl);

    for (i = 0; i < rl.count; i++) {
      if (rl.cmds[i].op == LK_ROP_FILL_RECT) {
        fills++;

        if (rl.cmds[i].color.r == 80 && rl.cmds[i].color.g == 140 &&
            rl.cmds[i].color.b == 220) {
          accent_fill++;
        }
      }
    }

    /* window bg + 4 outline edges + mark = 6 fills */
    CHECK_EQ(fills, 6);
    CHECK_EQ(accent_fill, 1);
    CHECK_EQ(count_text(ui, &rl, "On"), 1);

    lk_render_list_destroy(&rl);
    free(styles);
    free(r);
  }

  END_TEST();
  lk_ui_destroy(ui);
}

/* ============================================================
 * Radio
 * ============================================================ */

/* window > column > radio a (checked prop), radio b, radio c; each
 * presents "src" so we can count emissions. */
static lk_ui *make_radio_ui(int controlled) {
  lk_ui *ui = lk_ui_create(NULL);
  lk_tree *t = lk_ui_begin_frame(ui);
  lk_ix w = lk_tree_add_node_c(t, "w", UIK_WINDOW);
  lk_ix col = lk_tree_add_node_c(t, "col", UIK_COLUMN);
  const char *ids[3] = {"a", "b", "c"};
  int i;

  lk_tree_set_root(t, w);
  lk_tree_append_child(t, w, col);

  for (i = 0; i < 3; i++) {
    lk_ix rd = lk_tree_add_node_c(t, ids[i], UIK_RADIO);
    lk_tree_append_child(t, col, rd);
    lk_tree_add_prop(t, rd, UIP_TEXT, lk_v_cstr(ui->intern, ids[i]));
    lk_tree_add_presentation_s(t, rd, "src", lk_v_i32(i));

    if (i == 0) {
      lk_tree_add_prop(t, rd, UIP_CHECKED, lk_v_bool(1));
    }

    if (controlled) {
      lk_tree_add_prop(t, rd, UIP_CONTROLLED, lk_v_i32(1));
    }
  }

  lk_ui_end_frame(ui);
  lk_ui_add_translator_s(ui, LK_EVENT_VALUE_CHANGED, "src", 0, 0, 0, 0, "Src");

  return ui;
}

static void test_radio_exclusive(void) {
  lk_ui *ui = make_radio_ui(0);
  lk_rect *r;
  lk_event ev;
  char buf[32];

  BEGIN_TEST("radio: sibling-exclusive, prop-checked unchecks");

  r = layout_ui(ui, 400, 300);
  CHECK(r != NULL);

  if (r) {
    const lk_tree *cur = lk_ui_tree(ui);
    lk_ix a = find(ui, "a");
    lk_ix b = find(ui, "b");
    lk_ix c = find(ui, "c");

    CHECK_EQ(lk_check_effective(cur, a, lk_ui_state(ui)), 1);
    CHECK_EQ(lk_check_effective(cur, b, lk_ui_state(ui)), 0);

    /* rows stack: a at y 0..16, b at 16..32 */
    CHECK_EQ(lk_hit_test(cur, r, 4, 20), b);
    click(ui, b, 4, 20, LK_POINTER_BUTTON_PRIMARY, &ev);
    CHECK_EQ((unsigned)ev.handled, 1u);
    CHECK_EQ(lk_check_effective(cur, a, lk_ui_state(ui)), 0);
    CHECK_EQ(lk_check_effective(cur, b, lk_ui_state(ui)), 1);
    CHECK_EQ(lk_check_effective(cur, c, lk_ui_state(ui)), 0);
    CHECK_EQ(state_i32(ui, "a", LKS_CHECKED, -1), 0);
    CHECK_EQ(state_i32(ui, "b", LKS_CHECKED, -1), 1);
    /* only the newly checked one emits */
    CHECK_EQ(lk_ui_commands(ui)->count, 1u);
    CHECK(strcmp(last_value(ui, buf), "1") == 0);
    CHECK_EQ((int)lk_ui_commands(ui)->cmds[0].args[0].as.i, 1);

    /* clicking the checked one is consumed without change or emit */
    click(ui, b, 4, 20, LK_POINTER_BUTTON_PRIMARY, &ev);
    CHECK_EQ((unsigned)ev.handled, 1u);
    CHECK_EQ(lk_ui_commands(ui)->count, 1u);
    CHECK_EQ(lk_check_effective(cur, b, lk_ui_state(ui)), 1);

    /* SPACE on c moves it again */
    key(ui, c, LKK_SPACE, 0, &ev);
    CHECK_EQ(lk_check_effective(cur, b, lk_ui_state(ui)), 0);
    CHECK_EQ(lk_check_effective(cur, c, lk_ui_state(ui)), 1);
    CHECK_EQ(lk_ui_commands(ui)->count, 2u);

    free(r);
  }

  END_TEST();
  lk_ui_destroy(ui);
}

static void test_radio_controlled(void) {
  lk_ui *ui = make_radio_ui(1);
  lk_rect *r;
  lk_event ev;

  BEGIN_TEST("radio: controlled writes no state, emits");

  r = layout_ui(ui, 400, 300);
  CHECK(r != NULL);

  if (r) {
    const lk_tree *cur = lk_ui_tree(ui);
    lk_ix a = find(ui, "a");
    lk_ix b = find(ui, "b");

    click(ui, b, 4, 20, LK_POINTER_BUTTON_PRIMARY, &ev);
    CHECK_EQ((unsigned)ev.handled, 1u);
    CHECK_EQ(has_state(ui, "a", LKS_CHECKED), 0);
    CHECK_EQ(has_state(ui, "b", LKS_CHECKED), 0);
    CHECK_EQ(lk_check_effective(cur, a, lk_ui_state(ui)), 1);
    CHECK_EQ(lk_ui_commands(ui)->count, 1u);
    free(r);
  }

  END_TEST();
  lk_ui_destroy(ui);
}

/* ============================================================
 * Slider
 * ============================================================ */

/* window > slider "sl" (min/max/step/value props; presents "vol") */
static lk_ui *make_slider_ui(lk_i32 mn, lk_i32 mx, lk_i32 step, lk_i32 value,
                             int controlled) {
  lk_ui *ui = lk_ui_create(NULL);
  lk_tree *t = lk_ui_begin_frame(ui);
  lk_ix w = lk_tree_add_node_c(t, "w", UIK_WINDOW);
  lk_ix sl = lk_tree_add_node_c(t, "sl", UIK_SLIDER);

  lk_tree_set_root(t, w);
  lk_tree_append_child(t, w, sl);
  lk_tree_add_prop(t, sl, UIP_FOCUSABLE, lk_v_bool(1));

  if (mn != -9999) {
    lk_tree_add_prop(t, sl, UIP_MIN, lk_v_i32(mn));
  }

  if (mx != -9999) {
    lk_tree_add_prop(t, sl, UIP_MAX, lk_v_i32(mx));
  }

  if (step != -9999) {
    lk_tree_add_prop(t, sl, UIP_STEP, lk_v_i32(step));
  }

  if (value != -9999) {
    lk_tree_add_prop(t, sl, UIP_VALUE, lk_v_i32(value));
  }

  if (controlled) {
    lk_tree_add_prop(t, sl, UIP_CONTROLLED, lk_v_i32(1));
  }

  lk_tree_add_presentation_s(t, sl, "vol", lk_v_i32(0));
  lk_ui_end_frame(ui);
  lk_ui_add_translator_s(ui, LK_EVENT_VALUE_CHANGED, "vol", 0, 0, 0, 0, "Vol");

  return ui;
}

static void test_slider_effective(void) {
  lk_ui *ui;
  const lk_tree *cur;
  lk_ix sl;

  BEGIN_TEST("slider: effective value snaps + clamps; defaults");

  ui = make_slider_ui(-9999, -9999, -9999, -9999, 0);
  cur = lk_ui_tree(ui);
  sl = find(ui, "sl");
  /* defaults min 0 max 100 step 1, value = min */
  CHECK_EQ(lk_slider_effective(cur, sl, NULL), 0);
  lk_ui_destroy(ui);

  ui = make_slider_ui(0, 10, 5, 7, 0);
  cur = lk_ui_tree(ui);
  sl = find(ui, "sl");
  CHECK_EQ(lk_slider_effective(cur, sl, NULL), 5); /* 7 -> nearest step */
  lk_ui_destroy(ui);

  ui = make_slider_ui(0, 10, 5, 99, 0);
  cur = lk_ui_tree(ui);
  sl = find(ui, "sl");
  CHECK_EQ(lk_slider_effective(cur, sl, NULL), 10); /* clamped */
  lk_ui_destroy(ui);

  ui = make_slider_ui(-20, -30, 0, -25, 0);
  cur = lk_ui_tree(ui);
  sl = find(ui, "sl");
  /* max < min -> max = min; step < 1 -> 1 */
  CHECK_EQ(lk_slider_effective(cur, sl, NULL), (lk_i32)-20);
  lk_ui_destroy(ui);

  /* measure: 120 x 14 with no styles, inside a column */
  ui = lk_ui_create(NULL);
  {
    lk_tree *t = lk_ui_begin_frame(ui);
    lk_ix w = lk_tree_add_node_c(t, "w", UIK_WINDOW);
    lk_ix row = lk_tree_add_node_c(t, "row", UIK_ROW);
    lk_ix s = lk_tree_add_node_c(t, "sl", UIK_SLIDER);
    lk_rect *r;

    lk_tree_set_root(t, w);
    lk_tree_append_child(t, w, row);
    lk_tree_append_child(t, row, s);
    lk_ui_end_frame(ui);
    r = layout_ui(ui, 400, 300);
    CHECK(r != NULL);

    if (r) {
      CHECK_EQ(r[find(ui, "sl")].w, 120);
      free(r);
    }
  }
  lk_ui_destroy(ui);

  END_TEST();
}

static void test_slider_drag_and_keys(void) {
  /* Slider fills the 400x300 window (no styles: track = whole rect,
   * travel = 400 - 10 = 390).  value(x) = round((x - 5) * 100 / 390). */
  lk_ui *ui = make_slider_ui(0, 100, 1, -9999, 0);
  lk_rect *r;
  lk_event ev;
  char buf[32];

  BEGIN_TEST("slider: click/drag/keys update value + emit");

  r = layout_ui(ui, 400, 300);
  CHECK(r != NULL);

  if (r) {
    const lk_tree *cur = lk_ui_tree(ui);
    lk_ix sl = find(ui, "sl");
    lk_node_id sl_id = lk_intern_cid(ui->intern, "sl");

    CHECK_EQ(r[sl].w, 400);

    /* DOWN at x=200: (195*100)/390 = 50 */
    click(ui, sl, 200, 10, LK_POINTER_BUTTON_PRIMARY, &ev);
    CHECK_EQ((unsigned)ev.handled, 1u);
    CHECK_EQ(state_i32(ui, "sl", LKS_SLIDER_VALUE, -1), 50);
    CHECK_EQ(lk_slider_effective(cur, sl, lk_ui_state(ui)), 50);
    CHECK_EQ(lk_capture_current(ui), sl_id);
    CHECK_EQ(state_i32(ui, "sl", LKS_SLIDER_DRAGGING, -1), 1);
    CHECK_EQ(ui->focused_id, sl_id);
    CHECK(strcmp(last_value(ui, buf), "50") == 0);

    /* MOVE far right clamps to max */
    memset(&ev, 0, sizeof(ev));
    ev.type = LK_EVENT_POINTER_MOVE;
    ev.target = sl;
    ev.data.pointer.x = 900;
    ev.data.pointer.y = 500;
    lk_event_route(ui, &ev);
    CHECK_EQ((unsigned)ev.handled, 1u);
    CHECK_EQ(state_i32(ui, "sl", LKS_SLIDER_VALUE, -1), 100);
    CHECK(strcmp(last_value(ui, buf), "100") == 0);

    /* MOVE to the same value: no new emission */
    {
      lk_u32 before = lk_ui_commands(ui)->count;
      lk_event_route(ui, &ev);
      CHECK_EQ(lk_ui_commands(ui)->count, before);
    }

    /* UP releases */
    memset(&ev, 0, sizeof(ev));
    ev.type = LK_EVENT_POINTER_UP;
    ev.target = sl;
    lk_event_route(ui, &ev);
    CHECK_EQ((unsigned)ev.handled, 1u);
    CHECK_EQ((unsigned)lk_capture_current(ui), 0u);
    CHECK_EQ(state_i32(ui, "sl", LKS_SLIDER_DRAGGING, -1), 0);

    /* MOVE when not dragging bubbles */
    memset(&ev, 0, sizeof(ev));
    ev.type = LK_EVENT_POINTER_MOVE;
    ev.target = sl;
    ev.data.pointer.x = 10;
    lk_event_route(ui, &ev);
    CHECK_EQ((unsigned)ev.handled, 0u);
    CHECK_EQ(state_i32(ui, "sl", LKS_SLIDER_VALUE, -1), 100);

    /* keys */
    key(ui, sl, LKK_LEFT, 0, &ev);
    CHECK_EQ(state_i32(ui, "sl", LKS_SLIDER_VALUE, -1), 99);
    key(ui, sl, LKK_PAGEDOWN, 0, &ev);
    CHECK_EQ(state_i32(ui, "sl", LKS_SLIDER_VALUE, -1), 89);
    key(ui, sl, LKK_HOME, 0, &ev);
    CHECK_EQ(state_i32(ui, "sl", LKS_SLIDER_VALUE, -1), 0);
    key(ui, sl, LKK_LEFT, 0, &ev); /* at min: consumed, no change */
    CHECK_EQ((unsigned)ev.handled, 1u);
    CHECK_EQ(state_i32(ui, "sl", LKS_SLIDER_VALUE, -1), 0);
    key(ui, sl, LKK_END, 0, &ev);
    CHECK_EQ(state_i32(ui, "sl", LKS_SLIDER_VALUE, -1), 100);
    key(ui, sl, LKK_RIGHT, LK_MOD_CTRL, &ev); /* modified: bubbles */
    CHECK_EQ((unsigned)ev.handled, 0u);
    CHECK(strcmp(last_value(ui, buf), "100") == 0);

    /* render: default theme -> rail, fill, thumb (+ window bg) */
    {
      lk_style *styles = (lk_style *)malloc(sizeof(lk_style) * cur->node_count);
      lk_render_list rl;
      lk_u32 i;
      int accent = 0;

      memset(&rl, 0, sizeof(rl));
      lk_style_resolve(lk_ui_theme(ui), cur, NULL, styles);
      lk_render_build(cur, r, styles, lk_ui_state(ui), lk_ui_geom(ui), &rl);

      for (i = 0; i < rl.count; i++) {
        if (rl.cmds[i].op == LK_ROP_FILL_RECT && rl.cmds[i].color.r == 80 &&
            rl.cmds[i].color.g == 140 && rl.cmds[i].color.b == 220) {
          accent++;
        }
      }

      CHECK_EQ(accent, 2);
      lk_render_list_destroy(&rl);
      free(styles);
    }

    free(r);
  }

  END_TEST();
  lk_ui_destroy(ui);
}

static void test_slider_controlled_and_step(void) {
  /* min 0 max 10 step 5, controlled: clicks emit snapped values but
   * write no state; the prop value keeps winning. */
  lk_ui *ui = make_slider_ui(0, 10, 5, 0, 1);
  lk_rect *r;
  lk_event ev;
  char buf[32];

  BEGIN_TEST("slider: controlled + step snapping");

  r = layout_ui(ui, 400, 300);
  CHECK(r != NULL);

  if (r) {
    const lk_tree *cur = lk_ui_tree(ui);
    lk_ix sl = find(ui, "sl");

    /* x=200 -> raw 5.0 -> 5 */
    click(ui, sl, 200, 10, LK_POINTER_BUTTON_PRIMARY, &ev);
    CHECK_EQ((unsigned)ev.handled, 1u);
    CHECK_EQ(has_state(ui, "sl", LKS_SLIDER_VALUE), 0);
    CHECK(strcmp(last_value(ui, buf), "5") == 0);
    CHECK_EQ(lk_slider_effective(cur, sl, lk_ui_state(ui)), 0);

    /* x=300 -> raw 7.6 -> snaps to 10 */
    memset(&ev, 0, sizeof(ev));
    ev.type = LK_EVENT_POINTER_MOVE;
    ev.target = sl;
    ev.data.pointer.x = 300;
    lk_event_route(ui, &ev);
    CHECK(strcmp(last_value(ui, buf), "10") == 0);

    /* clicking outside the track bubbles (y below the rect) */
    click(ui, sl, 200, 400, LK_POINTER_BUTTON_PRIMARY, &ev);
    CHECK_EQ((unsigned)ev.handled, 0u);

    free(r);
  }

  END_TEST();
  lk_ui_destroy(ui);
}

/* ============================================================
 * Tabs
 * ============================================================ */

/* window > tabs "nb" (focusable, presents "page") > tab a "A" {label
 * la "aaaa" focusable}, tab b "B" {label lb "bb" focusable}, tab c "C"
 * {}.  value_id: UIP_VALUE on the tabs (NULL = none). */
static lk_ui *make_tabs_ui(const char *value_id, int controlled, int hide_b) {
  lk_ui *ui = lk_ui_create(NULL);
  lk_tree *t = lk_ui_begin_frame(ui);
  lk_ix w = lk_tree_add_node_c(t, "w", UIK_WINDOW);
  lk_ix nb = lk_tree_add_node_c(t, "nb", UIK_TABS);
  lk_ix a = lk_tree_add_node_c(t, "a", UIK_TAB);
  lk_ix b = lk_tree_add_node_c(t, "b", UIK_TAB);
  lk_ix c = lk_tree_add_node_c(t, "c", UIK_TAB);
  lk_ix la = lk_tree_add_node_c(t, "la", UIK_LABEL);
  lk_ix lb = lk_tree_add_node_c(t, "lb", UIK_LABEL);

  lk_tree_set_root(t, w);
  lk_tree_append_child(t, w, nb);
  lk_tree_append_child(t, nb, a);
  lk_tree_append_child(t, nb, b);
  lk_tree_append_child(t, nb, c);
  lk_tree_append_child(t, a, la);
  lk_tree_append_child(t, b, lb);
  /* props per node are contiguous */
  lk_tree_add_prop(t, nb, UIP_FOCUSABLE, lk_v_bool(1));

  if (value_id) {
    lk_tree_add_prop(t, nb, UIP_VALUE, lk_v_cstr(ui->intern, value_id));
  }

  if (controlled) {
    lk_tree_add_prop(t, nb, UIP_CONTROLLED, lk_v_i32(1));
  }

  lk_tree_add_prop(t, a, UIP_TEXT, lk_v_cstr(ui->intern, "A"));
  lk_tree_add_prop(t, b, UIP_TEXT, lk_v_cstr(ui->intern, "B"));

  if (hide_b) {
    lk_tree_add_prop(t, b, UIP_HIDDEN, lk_v_bool(1));
  }

  lk_tree_add_prop(t, c, UIP_TEXT, lk_v_cstr(ui->intern, "C"));
  lk_tree_add_prop(t, la, UIP_TEXT, lk_v_cstr(ui->intern, "aaaa"));
  lk_tree_add_prop(t, la, UIP_FOCUSABLE, lk_v_bool(1));
  lk_tree_add_prop(t, lb, UIP_TEXT, lk_v_cstr(ui->intern, "bb"));
  lk_tree_add_prop(t, lb, UIP_FOCUSABLE, lk_v_bool(1));

  lk_tree_add_presentation_s(t, nb, "page", lk_v_i32(0));
  lk_ui_end_frame(ui);
  lk_ui_add_translator_s(ui, LK_EVENT_VALUE_CHANGED, "page", 0, 0, 0, 0,
                         "Page");

  return ui;
}

static void test_tabs_layout(void) {
  /* Headers: 1-char titles -> 8 + 20 = 28 wide, 16 + 10 = 26 tall;
   * strip = 27 (separator).  Selected page = (0, 27, 400, 273);
   * collapsed tabs' rects are their header cells; collapsed content
   * stays zero. */
  lk_ui *ui = make_tabs_ui(NULL, 0, 0);
  lk_rect *r;

  BEGIN_TEST("tabs: strip cells, page rect, collapsed zero");

  r = layout_ui(ui, 400, 300);
  CHECK(r != NULL);

  if (r) {
    const lk_tree *cur = lk_ui_tree(ui);
    lk_ix nb = find(ui, "nb");
    lk_ix a = find(ui, "a");
    lk_ix b = find(ui, "b");
    lk_ix c = find(ui, "c");
    lk_ix la = find(ui, "la");
    lk_ix lb = find(ui, "lb");
    int idx = -1;

    CHECK_EQ(lk_tabs_selected(cur, nb, lk_ui_state(ui), &idx), a);
    CHECK_EQ(idx, 0);
    CHECK_EQ(lk_tabs_collapsed(cur, a, NULL), 0);
    CHECK_EQ(lk_tabs_collapsed(cur, b, NULL), 1);
    CHECK_EQ(lk_tabs_collapsed(cur, la, NULL), 0);

    CHECK_EQ(r[a].x, 0);
    CHECK_EQ(r[a].y, 27);
    CHECK_EQ(r[a].w, 400);
    CHECK_EQ(r[a].h, 273);
    CHECK_EQ(r[b].x, 28);
    CHECK_EQ(r[b].y, 0);
    CHECK_EQ(r[b].w, 28);
    CHECK_EQ(r[b].h, 26);
    CHECK_EQ(r[c].x, 56);

    /* page content laid out as a column; collapsed content zero */
    CHECK_EQ(r[la].y, 27);
    CHECK_EQ(r[la].w, 400);
    CHECK_EQ(r[lb].w, 0);
    CHECK_EQ(r[lb].h, 0);

    /* geometry stash */
    CHECK_EQ(lk_ui_geom(ui)[nb].strip.h, 27);
    CHECK_EQ(lk_ui_geom(ui)[a].header.x, 0);
    CHECK_EQ(lk_ui_geom(ui)[a].header.w, 28);
    CHECK_EQ(lk_ui_geom(ui)[c].header.x, 56);

    /* hit-testing: header cell -> that TAB; page -> content */
    CHECK_EQ(lk_hit_test(cur, r, 40, 10), b);
    CHECK_EQ(lk_hit_test(cur, r, 60, 10), c);
    CHECK_EQ(lk_hit_test(cur, r, 10, 10), nb); /* selected header */
    CHECK_EQ(lk_hit_test(cur, r, 10, 30), la);
    CHECK_EQ(lk_hit_test(cur, r, 10, 200), a);

    /* measured size: strip 84 wide vs page "aaaa" 32 -> 84; height 27
     * + tallest page (16) = 43 -- observable through a row parent */
    free(r);
  }

  END_TEST();
  lk_ui_destroy(ui);
}

static void test_tabs_measure_max_page(void) {
  /* window > row > tabs: the row gives the tabs its measured width;
   * strip (84) beats the widest page ("aaaa" = 32).  Height is the
   * cross axis (stretched), so measure it via a column parent. */
  lk_ui *ui = lk_ui_create(NULL);
  lk_tree *t = lk_ui_begin_frame(ui);
  lk_ix w = lk_tree_add_node_c(t, "w", UIK_WINDOW);
  lk_ix col = lk_tree_add_node_c(t, "col", UIK_COLUMN);
  lk_ix nb = lk_tree_add_node_c(t, "nb", UIK_TABS);
  lk_ix a = lk_tree_add_node_c(t, "a", UIK_TAB);
  lk_ix b = lk_tree_add_node_c(t, "b", UIK_TAB);
  lk_ix la = lk_tree_add_node_c(t, "la", UIK_LABEL);
  lk_ix lb1 = lk_tree_add_node_c(t, "lb1", UIK_LABEL);
  lk_ix lb2 = lk_tree_add_node_c(t, "lb2", UIK_LABEL);
  lk_rect *r;

  BEGIN_TEST("tabs: measures the tallest page (hidden or not)");

  lk_tree_set_root(t, w);
  lk_tree_append_child(t, w, col);
  lk_tree_append_child(t, col, nb);
  lk_tree_append_child(t, nb, a);
  lk_tree_append_child(t, nb, b);
  lk_tree_append_child(t, a, la);
  lk_tree_append_child(t, b, lb1);
  lk_tree_append_child(t, b, lb2);
  lk_tree_add_prop(t, a, UIP_TEXT, lk_v_cstr(ui->intern, "A"));
  lk_tree_add_prop(t, b, UIP_TEXT, lk_v_cstr(ui->intern, "B"));
  lk_tree_add_prop(t, la, UIP_TEXT, lk_v_cstr(ui->intern, "x"));
  lk_tree_add_prop(t, lb1, UIP_TEXT, lk_v_cstr(ui->intern, "y"));
  lk_tree_add_prop(t, lb2, UIP_TEXT, lk_v_cstr(ui->intern, "z"));
  lk_ui_end_frame(ui);

  r = layout_ui(ui, 400, 300);
  CHECK(r != NULL);

  if (r) {
    /* page a is 16 tall, page b (unselected) is 32: tabs h = 27 + 32 */
    CHECK_EQ(r[find(ui, "nb")].h, 59);
    CHECK_EQ(r[find(ui, "a")].h, 32);
    free(r);
  }

  END_TEST();
  lk_ui_destroy(ui);
}

static void test_tabs_click_and_keys(void) {
  lk_ui *ui = make_tabs_ui(NULL, 0, 0);
  lk_rect *r;
  lk_event ev;
  char buf[32];

  BEGIN_TEST("tabs: header click + arrows select, emit tab id");

  r = layout_ui(ui, 400, 300);
  CHECK(r != NULL);

  if (r) {
    const lk_tree *cur = lk_ui_tree(ui);
    lk_ix nb = find(ui, "nb");
    lk_ix a = find(ui, "a");
    lk_ix b = find(ui, "b");
    lk_ix la = find(ui, "la");
    lk_ix lb = find(ui, "lb");
    int idx;

    /* click on the open page bubbles (not a header) */
    click(ui, a, 10, 200, LK_POINTER_BUTTON_PRIMARY, &ev);
    CHECK_EQ((unsigned)ev.handled, 0u);

    /* click header B */
    click(ui, b, 40, 10, LK_POINTER_BUTTON_PRIMARY, &ev);
    CHECK_EQ((unsigned)ev.handled, 1u);
    CHECK_EQ(state_i32(ui, "nb", LKS_SELECTED_INDEX, -1), 1);
    CHECK_EQ(lk_tabs_selected(cur, nb, lk_ui_state(ui), &idx), b);
    CHECK_EQ(lk_ui_commands(ui)->count, 1u);
    CHECK(strcmp(last_value(ui, buf), "b") == 0);
    CHECK_EQ(ui->focused_id, lk_intern_cid(ui->intern, "nb"));

    /* re-layout: b is the page now, a is a header cell */
    free(r);
    r = layout_ui(ui, 400, 300);
    CHECK(r != NULL);

    if (r) {
      CHECK_EQ(r[b].y, 27);
      CHECK_EQ(r[b].h, 273);
      CHECK_EQ(r[a].w, 28);
      CHECK_EQ(r[a].h, 26);
      CHECK_EQ(r[lb].w, 400);
      CHECK_EQ(r[la].w, 0);
      CHECK_EQ(lk_hit_test(cur, r, 10, 10), a);
    }

    /* secondary click on a header bubbles */
    click(ui, a, 10, 10, LK_POINTER_BUTTON_SECONDARY, &ev);
    CHECK_EQ((unsigned)ev.handled, 0u);
    CHECK_EQ(state_i32(ui, "nb", LKS_SELECTED_INDEX, -1), 1);

    /* keys on the focused tabs */
    key(ui, nb, LKK_RIGHT, 0, &ev);
    CHECK_EQ((unsigned)ev.handled, 1u);
    CHECK_EQ(state_i32(ui, "nb", LKS_SELECTED_INDEX, -1), 2);
    CHECK(strcmp(last_value(ui, buf), "c") == 0);
    key(ui, nb, LKK_RIGHT, 0, &ev); /* clamps: consumed, no emit */
    CHECK_EQ((unsigned)ev.handled, 1u);
    CHECK_EQ(state_i32(ui, "nb", LKS_SELECTED_INDEX, -1), 2);
    CHECK_EQ(lk_ui_commands(ui)->count, 2u);
    key(ui, nb, LKK_HOME, 0, &ev);
    CHECK_EQ(state_i32(ui, "nb", LKS_SELECTED_INDEX, -1), 0);
    key(ui, nb, LKK_END, 0, &ev);
    CHECK_EQ(state_i32(ui, "nb", LKS_SELECTED_INDEX, -1), 2);
    key(ui, nb, LKK_LEFT, 0, &ev);
    CHECK_EQ(state_i32(ui, "nb", LKS_SELECTED_INDEX, -1), 1);
    key(ui, nb, LKK_LEFT, LK_MOD_SHIFT, &ev); /* modified: bubbles */
    CHECK_EQ((unsigned)ev.handled, 0u);

    if (r) {
      free(r);
    }
  }

  END_TEST();
  lk_ui_destroy(ui);
}

static void test_tabs_value_prop_and_controlled(void) {
  lk_ui *ui = make_tabs_ui("c", 1, 0);
  lk_rect *r;
  lk_event ev;
  char buf[32];

  BEGIN_TEST("tabs: value prop initial; controlled = no state");

  r = layout_ui(ui, 400, 300);
  CHECK(r != NULL);

  if (r) {
    const lk_tree *cur = lk_ui_tree(ui);
    lk_ix nb = find(ui, "nb");
    lk_ix a = find(ui, "a");
    lk_ix c = find(ui, "c");
    int idx;

    CHECK_EQ(lk_tabs_selected(cur, nb, lk_ui_state(ui), &idx), c);
    CHECK_EQ(idx, 2);
    CHECK_EQ(r[c].h, 273);
    CHECK_EQ(r[a].w, 28);

    click(ui, a, 10, 10, LK_POINTER_BUTTON_PRIMARY, &ev);
    CHECK_EQ((unsigned)ev.handled, 1u);
    CHECK_EQ(has_state(ui, "nb", LKS_SELECTED_INDEX), 0);
    CHECK(strcmp(last_value(ui, buf), "a") == 0);
    CHECK_EQ(lk_tabs_selected(cur, nb, lk_ui_state(ui), &idx), c);

    free(r);
  }

  END_TEST();
  lk_ui_destroy(ui);

  /* unmatched value falls back to index 0; state beats prop */
  ui = make_tabs_ui("nope", 0, 0);
  {
    const lk_tree *cur = lk_ui_tree(ui);
    lk_ix nb = find(ui, "nb");
    int idx;

    CHECK_EQ(lk_tabs_selected(cur, nb, NULL, &idx), find(ui, "a"));
    lk_state_set(lk_ui_state(ui), lk_intern_cid(ui->intern, "nb"),
                 LKS_SELECTED_INDEX, lk_v_i32(7)); /* out of range: clamps */
    CHECK_EQ(lk_tabs_selected(cur, nb, lk_ui_state(ui), &idx), find(ui, "c"));
    CHECK_EQ(idx, 2);
  }
  lk_ui_destroy(ui);
}

static void test_tabs_hidden_tab_and_render(void) {
  /* b hidden: strip = a, c (index 1 = c); render draws the selected
   * page's text and every header title, never collapsed content. */
  lk_ui *ui = make_tabs_ui(NULL, 0, 1);
  lk_rect *r;

  BEGIN_TEST("tabs: hidden tab skipped; render headers only");

  r = layout_ui(ui, 400, 300);
  CHECK(r != NULL);

  if (r) {
    const lk_tree *cur = lk_ui_tree(ui);
    lk_ix nb = find(ui, "nb");
    lk_ix b = find(ui, "b");
    lk_ix c = find(ui, "c");
    lk_style *styles = (lk_style *)malloc(sizeof(lk_style) * cur->node_count);
    lk_render_list rl;

    CHECK_EQ(r[c].x, 28); /* right after a's cell */
    CHECK_EQ(r[b].w, 0);
    CHECK_EQ(lk_hit_test(cur, r, 40, 10), c);

    memset(&rl, 0, sizeof(rl));
    lk_style_resolve(lk_ui_theme(ui), cur, NULL, styles);
    lk_render_build(cur, r, styles, lk_ui_state(ui), lk_ui_geom(ui), &rl);
    CHECK_EQ(count_text(ui, &rl, "A"), 1);
    CHECK_EQ(count_text(ui, &rl, "C"), 1);
    CHECK_EQ(count_text(ui, &rl, "B"), 0);
    CHECK_EQ(count_text(ui, &rl, "aaaa"), 1);
    CHECK_EQ(count_text(ui, &rl, "bb"), 0);
    lk_render_list_destroy(&rl);
    free(styles);

    /* select c: its (empty) page shows, "aaaa" disappears */
    lk_state_set(lk_ui_state(ui), lk_intern_cid(ui->intern, "nb"),
                 LKS_SELECTED_INDEX, lk_v_i32(1));
    free(r);
    r = layout_ui(ui, 400, 300);
    CHECK(r != NULL);

    if (r) {
      styles = (lk_style *)malloc(sizeof(lk_style) * cur->node_count);
      memset(&rl, 0, sizeof(rl));
      lk_style_resolve(lk_ui_theme(ui), cur, NULL, styles);
      lk_render_build(cur, r, styles, lk_ui_state(ui), lk_ui_geom(ui), &rl);
      CHECK_EQ(count_text(ui, &rl, "aaaa"), 0);
      CHECK_EQ(count_text(ui, &rl, "A"), 1);
      CHECK_EQ(lk_tabs_selected(cur, nb, lk_ui_state(ui), NULL), c);
      lk_render_list_destroy(&rl);
      free(styles);
      free(r);
    }
  }

  END_TEST();
  lk_ui_destroy(ui);
}

static void test_tabs_focus_skips_collapsed(void) {
  /* Tab cycling never lands inside an unselected page. */
  lk_ui *ui = make_tabs_ui(NULL, 0, 0);
  const lk_tree *cur = lk_ui_tree(ui);
  lk_node_id nb_id = lk_intern_cid(ui->intern, "nb");
  lk_node_id la_id = lk_intern_cid(ui->intern, "la");
  lk_node_id lb_id = lk_intern_cid(ui->intern, "lb");
  lk_rect *r;

  BEGIN_TEST("tabs: focus traversal skips collapsed pages");

  r = layout_ui(ui, 400, 300);
  CHECK(r != NULL);

  CHECK_EQ(lk_focus_next(ui, cur), nb_id);
  CHECK_EQ(lk_focus_next(ui, cur), la_id);
  CHECK_EQ(lk_focus_next(ui, cur), nb_id); /* wrapped: lb skipped */

  /* select b: now lb is reachable and la is not */
  lk_state_set(lk_ui_state(ui), nb_id, LKS_SELECTED_INDEX, lk_v_i32(1));
  CHECK_EQ(lk_focus_next(ui, cur), lb_id);
  CHECK_EQ(lk_focus_next(ui, cur), nb_id);
  CHECK_EQ(lk_focus_prev(ui, cur), lb_id);

  if (r) {
    free(r);
  }

  END_TEST();
  lk_ui_destroy(ui);
}

static void test_tabs_end_frame_clears_trapped_focus(void) {
  /* Focus in page a; the app (controlled) selects b next frame: the
   * focused node is now inside a collapsed page, so end_frame clears
   * focus and enqueues FOCUS_CHANGED.  Focus on the TABS itself or a
   * collapsed TAB (its header) is left alone. */
  lk_ui *ui = lk_ui_create(NULL);
  lk_node_id la_id, nb_id, a_id;
  int i;

  BEGIN_TEST("tabs: end_frame clears focus in a collapsed page");

  la_id = lk_intern_cid(ui->intern, "la");
  nb_id = lk_intern_cid(ui->intern, "nb");
  a_id = lk_intern_cid(ui->intern, "a");

  for (i = 0; i < 3; i++) {
    lk_tree *t = lk_ui_begin_frame(ui);
    lk_ix w = lk_tree_add_node_c(t, "w", UIK_WINDOW);
    lk_ix nb = lk_tree_add_node_c(t, "nb", UIK_TABS);
    lk_ix a = lk_tree_add_node_c(t, "a", UIK_TAB);
    lk_ix b = lk_tree_add_node_c(t, "b", UIK_TAB);
    lk_ix la = lk_tree_add_node_c(t, "la", UIK_LABEL);

    lk_tree_set_root(t, w);
    lk_tree_append_child(t, w, nb);
    lk_tree_append_child(t, nb, a);
    lk_tree_append_child(t, nb, b);
    lk_tree_append_child(t, a, la);
    lk_tree_add_prop(t, nb, UIP_CONTROLLED, lk_v_i32(1));
    lk_tree_add_prop(t, nb, UIP_FOCUSABLE, lk_v_bool(1));
    lk_tree_add_prop(t, nb, UIP_VALUE,
                     lk_v_cstr(ui->intern, i == 0 ? "a" : "b"));
    lk_tree_add_prop(t, a, UIP_FOCUSABLE, lk_v_bool(1));
    lk_tree_add_prop(t, la, UIP_FOCUSABLE, lk_v_bool(1));
    lk_ui_end_frame(ui);

    if (i == 0) {
      CHECK(lk_focus_set(ui, lk_ui_tree(ui), la_id) == 1);
      CHECK_EQ(ui->focused_id, la_id);
      lk_ui_clear_commands(ui);
    } else if (i == 1) {
      /* page a collapsed: focus cleared */
      CHECK_EQ((unsigned)ui->focused_id, 0u);
      /* focus on the collapsed TAB a itself (its header is visible)
       * and on the tabs survive */
      CHECK(lk_focus_set(ui, lk_ui_tree(ui), a_id) == 1);
    } else {
      CHECK_EQ(ui->focused_id, a_id);
      lk_focus_set(ui, lk_ui_tree(ui), nb_id);
    }
  }

  CHECK_EQ(ui->focused_id, nb_id);

  END_TEST();
  lk_ui_destroy(ui);
}

/* ============================================================
 * Grid
 * ============================================================ */

static lk_ui *make_grid_ui(int cols, int gap, int hide_second) {
  lk_ui *ui = lk_ui_create(NULL);
  lk_tree *t = lk_ui_begin_frame(ui);
  lk_ix w = lk_tree_add_node_c(t, "w", UIK_WINDOW);
  lk_ix row = lk_tree_add_node_c(t, "row", UIK_ROW);
  lk_ix g = lk_tree_add_node_c(t, "g", UIK_GRID);
  lk_ix l1 = lk_tree_add_node_c(t, "l1", UIK_LABEL);
  lk_ix l2 = lk_tree_add_node_c(t, "l2", UIK_LABEL);
  lk_ix l3 = lk_tree_add_node_c(t, "l3", UIK_LABEL);

  lk_tree_set_root(t, w);
  lk_tree_append_child(t, w, row);
  lk_tree_append_child(t, row, g);
  lk_tree_append_child(t, g, l1);
  lk_tree_append_child(t, g, l2);
  lk_tree_append_child(t, g, l3);
  if (cols > 0) {
    lk_tree_add_prop(t, g, UIP_COLUMNS, lk_v_i32(cols));
  }

  if (gap > 0) {
    lk_tree_add_prop(t, g, UIP_GAP, lk_v_i32(gap));
  }

  lk_tree_add_prop(t, l1, UIP_TEXT, lk_v_cstr(ui->intern, "aa"));
  lk_tree_add_prop(t, l2, UIP_TEXT, lk_v_cstr(ui->intern, "bbbb"));

  if (hide_second) {
    lk_tree_add_prop(t, l2, UIP_HIDDEN, lk_v_bool(1));
  }

  lk_tree_add_prop(t, l3, UIP_TEXT, lk_v_cstr(ui->intern, "c"));

  lk_ui_end_frame(ui);

  return ui;
}

static void test_grid_cells(void) {
  /* 2 columns, gap 4: col widths 16 / 32, row heights 16 / 16.
   * measured w = 16 + 32 + 4 = 52 (via the row parent). */
  lk_ui *ui = make_grid_ui(2, 4, 0);
  lk_rect *r;

  BEGIN_TEST("grid: column-aligned cells + gap");

  r = layout_ui(ui, 400, 300);
  CHECK(r != NULL);

  if (r) {
    lk_ix g = find(ui, "g");
    lk_ix l1 = find(ui, "l1");
    lk_ix l2 = find(ui, "l2");
    lk_ix l3 = find(ui, "l3");

    CHECK_EQ(r[g].w, 52);
    CHECK_EQ(r[l1].x, 0);
    CHECK_EQ(r[l1].y, 0);
    CHECK_EQ(r[l1].w, 16);
    CHECK_EQ(r[l1].h, 16);
    CHECK_EQ(r[l2].x, 20);
    CHECK_EQ(r[l2].w, 32);
    CHECK_EQ(r[l3].x, 0);
    CHECK_EQ(r[l3].y, 20);
    CHECK_EQ(r[l3].w, 16); /* fills the column width */
    free(r);
  }

  END_TEST();
  lk_ui_destroy(ui);

  /* default 1 column: stacked; hidden child skipped (no cell) */
  ui = make_grid_ui(0, 0, 1);
  r = layout_ui(ui, 400, 300);
  CHECK(r != NULL);

  if (r) {
    lk_ix g = find(ui, "g");
    lk_ix l1 = find(ui, "l1");
    lk_ix l3 = find(ui, "l3");

    CHECK_EQ(r[g].w, 16);
    CHECK_EQ(r[l1].y, 0);
    CHECK_EQ(r[l3].y, 16);
    CHECK_EQ(r[l3].w, 16);
    free(r);
  }

  lk_ui_destroy(ui);

  /* more columns than children: no trailing gaps in the width */
  ui = make_grid_ui(5, 4, 0);
  r = layout_ui(ui, 400, 300);
  CHECK(r != NULL);

  if (r) {
    lk_ix g = find(ui, "g");
    lk_ix l3 = find(ui, "l3");

    CHECK_EQ(r[g].w, 16 + 32 + 8 + 4 * 2);
    CHECK_EQ(r[l3].x, 16 + 4 + 32 + 4);
    CHECK_EQ(r[l3].y, 0);
    free(r);
  }

  lk_ui_destroy(ui);
}

/* ============================================================
 * Cross-cutting: UIP_CONTROLLED alias, accent style
 * ============================================================ */

static void test_controlled_alias_and_accent(void) {
  lk_ui *ui = lk_ui_create(NULL);
  lk_tree *t = lk_ui_begin_frame(ui);
  lk_ix w = lk_tree_add_node_c(t, "w", UIK_WINDOW);
  lk_ix cb = lk_tree_add_node_c(t, "cb", UIK_CHECKBOX);
  lk_style *styles;
  lk_style s;

  BEGIN_TEST("controlled alias + accent style field");

  CHECK_EQ((int)UIP_SPLIT_CONTROLLED, (int)UIP_CONTROLLED);

  lk_tree_set_root(t, w);
  lk_tree_append_child(t, w, cb);
  lk_tree_add_tag_s(t, cb, "hot");
  lk_ui_end_frame(ui);

  /* user rule overrides the accent for the tagged node */
  memset(&s, 0, sizeof(s));
  s.accent.r = 1;
  s.accent.g = 2;
  s.accent.b = 3;
  s.accent.a = 255;
  lk_theme_add_rule(lk_ui_theme(ui), UIK_CHECKBOX,
                    lk_intern_cid(ui->intern, "hot"), 0, &s, LK_SF_ACCENT);

  styles = (lk_style *)malloc(sizeof(lk_style) * lk_ui_tree(ui)->node_count);
  lk_style_resolve(lk_ui_theme(ui), lk_ui_tree(ui), NULL, styles);
  CHECK_EQ(styles[find(ui, "cb")].accent.r, 1);
  CHECK_EQ(styles[find(ui, "cb")].accent.b, 3);
  /* default accent from the wildcard rule elsewhere */
  CHECK_EQ(styles[find(ui, "w")].accent.r, 80);
  CHECK_EQ(styles[find(ui, "w")].accent.b, 220);
  free(styles);

  END_TEST();
  lk_ui_destroy(ui);
}

/* ============================================================ */

void lk_forms_run_tests(void) {
  printf("\nlk forms widgets tests:\n");
  test_checkbox_measure();
  test_checkbox_click_toggles();
  test_checkbox_prop_and_controlled();
  test_checkbox_disabled();
  test_checkbox_render();
  test_radio_exclusive();
  test_radio_controlled();
  test_slider_effective();
  test_slider_drag_and_keys();
  test_slider_controlled_and_step();
  test_tabs_layout();
  test_tabs_measure_max_page();
  test_tabs_click_and_keys();
  test_tabs_value_prop_and_controlled();
  test_tabs_hidden_tab_and_render();
  test_tabs_focus_skips_collapsed();
  test_tabs_end_frame_clears_trapped_focus();
  test_grid_cells();
  test_controlled_alias_and_accent();
}
