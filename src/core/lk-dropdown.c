/*
 * lk-dropdown.c — Selection dropdown (UIK_DROPDOWN) and option
 * (UIK_OPTION) widgets, plus the Lean overlay machinery.
 *
 * UIK_DROPDOWN is a leaf in the main layout pass: its children
 * (UIK_OPTIONs) are NOT laid out or rendered by the main pass.
 * Instead, when LKS_EXPANDED == 1, the overlay pass walks the tree,
 * positions each option in a popup below the trigger, and emits fill
 * + text commands on top of the main render list.
 *
 * Hit-test is similarly two-phase: lk_hit_test_overlay() first checks
 * any expanded dropdown's popup, then falls back to lk_hit_test().
 *
 * This is deliberately dropdown-specific Lean scaffolding.  The path
 * toward a generalized overlay system (tooltips, modals, menus) is
 * documented in docs/overlays.md.
 */

#include <string.h>

#include "lk-dropdown.h"
#include "lk-memory.h"
#include <lk.h>

/* ---- Tunable constants (kept local until proven to need configurability) */

#define DROPDOWN_CHEVRON_W 14 /* reserved space on trigger's right edge */
#define DROPDOWN_OPTION_PAD_Y 4
#define DROPDOWN_MIN_OPTION_H 20
#define DROPDOWN_POPUP_MAX_HEIGHT 240 /* clamps overly long menus */

/* ---- State helpers ---- */

static lk_i32 get_i32(const lk_state *state, lk_node_id nid, lk_u16 key) {
  lk_value v;

  if (!state) {
    return 0;
  }

  v = lk_state_get(state, nid, key);

  if (v.tag == UIV_I32) {
    return (lk_i32)v.as.i;
  }

  return 0;
}

static int is_expanded(const lk_state *state, lk_node_id nid) {
  return get_i32(state, nid, LKS_EXPANDED) ? 1 : 0;
}

/* Count options under a dropdown node (children with kind UIK_OPTION). */
static lk_u32 count_options(const lk_tree *t, lk_ix n) {
  lk_ix ch = t->nodes[n].first_child;
  lk_u32 c = 0;

  while (ch) {
    if (t->nodes[ch].kind == UIK_OPTION) {
      c++;
    }

    ch = t->nodes[ch].next_sibling;
  }

  return c;
}

/* Walk to the i-th option child (0-based).  Returns 0 if out of range. */
static lk_ix nth_option(const lk_tree *t, lk_ix n, lk_u32 i) {
  lk_ix ch = t->nodes[n].first_child;
  lk_u32 c = 0;

  while (ch) {
    if (t->nodes[ch].kind == UIK_OPTION) {
      if (c == i) {
        return ch;
      }

      c++;
    }

    ch = t->nodes[ch].next_sibling;
  }

  return 0;
}

/* Return the currently-selected option's text, or the dropdown's own
 * UIP_TEXT as a fallback (e.g. "Select…"). */
static lk_u32 selected_text_id(const lk_tree *t, lk_ix n,
                                const lk_state *state) {
  lk_node_id nid = t->nodes[n].id;
  lk_i32 sel = get_i32(state, nid, LKS_SELECTED_INDEX);
  lk_ix opt;

  if (sel < 0) {
    sel = 0;
  }

  opt = nth_option(t, n, (lk_u32)sel);

  if (opt) {
    return lk_node_text_id(t, opt);
  }

  return lk_node_text_id(t, n);
}

/* Row-height of an option: max(measured-text-height, min) + pad*2. */
static lk_i32 option_row_height(const lk_layout_cfg *cfg) {
  lk_i32 th = 0;
  lk_i32 tw = 0;
  lk_str probe;

  probe.ptr = "Xg";
  probe.len = 2;

  if (cfg && cfg->measure_text) {
    cfg->measure_text(cfg->measure_ud, probe, &tw, &th);
  }

  if (th < DROPDOWN_MIN_OPTION_H - DROPDOWN_OPTION_PAD_Y * 2) {
    th = DROPDOWN_MIN_OPTION_H - DROPDOWN_OPTION_PAD_Y * 2;
  }

  return th + DROPDOWN_OPTION_PAD_Y * 2;
}

/* ---- Measure ---- */

static void measure_dropdown(const lk_tree *t, lk_ix n, const lk_size *sizes,
                              const lk_layout_cfg *cfg, lk_i32 *out_w,
                              lk_i32 *out_h) {
  lk_i32 pad = cfg->styles ? cfg->styles[n].padding
                           : lk_node_prop_i32(t, n, UIP_PADDING, 0);
  lk_i32 bw = cfg->styles ? cfg->styles[n].border_width : 0;
  lk_i32 inset = pad + bw;
  lk_u32 sid;
  lk_str s;
  lk_i32 tw = 0;
  lk_i32 th = 0;

  (void)sizes;

  sid = selected_text_id(t, n, cfg->state);
  s = lk_intern_str(t->intern, sid);
  if (cfg->measure_text) {
    cfg->measure_text(cfg->measure_ud, s, &tw, &th);
  }

  if (tw < 80) {
    tw = 80; /* sensible minimum when no selection */
  }

  *out_w = tw + DROPDOWN_CHEVRON_W + inset * 2;
  *out_h = th + inset * 2;
}

static void measure_option(const lk_tree *t, lk_ix n, const lk_size *sizes,
                            const lk_layout_cfg *cfg, lk_i32 *out_w,
                            lk_i32 *out_h) {
  /* Options don't participate in the main layout pass.  Zero size is
   * fine — the dropdown places them in its overlay. */
  (void)t;
  (void)n;
  (void)sizes;
  (void)cfg;
  *out_w = 0;
  *out_h = 0;
}

/* ---- Render (main pass — just the collapsed trigger) ---- */

static void render_dropdown(const lk_tree *t, lk_ix n, const lk_rect *rect,
                             const lk_style *style, const lk_state *state,
                             lk_render_list *out) {
  lk_i32 pad = style->padding;
  lk_i32 bw = style->border_width;
  lk_i32 inset = pad + bw;
  lk_u32 sid;
  lk_render_cmd cmd;

  /* Background */
  memset(&cmd, 0, sizeof(cmd));
  cmd.op = LK_ROP_FILL_RECT;
  cmd.rect = *rect;
  cmd.color = style->bg;
  lk_render_list_push(out, cmd);

  /* Selected-option text (or placeholder) */
  sid = selected_text_id(t, n, state);
  if (sid != 0) {
    memset(&cmd, 0, sizeof(cmd));
    cmd.op = LK_ROP_DRAW_TEXT;
    cmd.rect.x = rect->x + inset;
    cmd.rect.y = rect->y + inset;
    cmd.rect.w = rect->w - inset * 2 - DROPDOWN_CHEVRON_W;
    cmd.rect.h = rect->h - inset * 2;
    cmd.color = style->fg;
    cmd.str_id = sid;
    lk_render_list_push(out, cmd);
  }

  /* Chevron — a small inverted triangle drawn as stacked rows.
   * Positioned in the reserved right strip. */
  {
    lk_i32 cx = rect->x + rect->w - DROPDOWN_CHEVRON_W / 2 - 4;
    lk_i32 cy = rect->y + rect->h / 2 - 2;
    int r;

    for (r = 0; r < 4; r++) {
      lk_i32 w = 8 - r * 2;
      if (w <= 0) {
        break;
      }
      memset(&cmd, 0, sizeof(cmd));
      cmd.op = LK_ROP_FILL_RECT;
      cmd.rect.x = cx - w / 2;
      cmd.rect.y = cy + r;
      cmd.rect.w = w;
      cmd.rect.h = 1;
      cmd.color = style->fg;
      lk_render_list_push(out, cmd);
    }
  }
}

/* OPTION doesn't render in the main pass — its paint happens in the
 * dropdown's overlay render.  Keep the function here so UIK_OPTION is
 * a well-formed widget even if someone adds it outside a dropdown
 * (useful for tests). */
static void render_option(const lk_tree *t, lk_ix n, const lk_rect *rect,
                           const lk_style *style, const lk_state *state,
                           lk_render_list *out) {
  (void)t;
  (void)n;
  (void)rect;
  (void)style;
  (void)state;
  (void)out;
}

/* ---- Event handling ---- */

static void emit_value_changed(lk_ui *ui, const lk_tree *t, lk_ix n,
                                lk_u32 new_value_id) {
  lk_event ev;
  (void)t;
  memset(&ev, 0, sizeof(ev));
  ev.type = LK_EVENT_VALUE_CHANGED;
  ev.target = n;
  ev.data.value_changed.str_id = new_value_id;
  lk_event_route(ui, &ev);
}

static int event_dropdown(lk_ui *ui, const lk_tree *t, lk_ix n, lk_event *ev) {
  lk_state *st = lk_ui_state(ui);
  lk_node_id nid = t->nodes[n].id;
  lk_u32 count = count_options(t, n);
  lk_i32 sel;
  lk_i32 hov;
  lk_u16 kc;

  if (count == 0) {
    return 0;
  }

  sel = get_i32(st, nid, LKS_SELECTED_INDEX);
  if (sel < 0 || (lk_u32)sel >= count) {
    sel = 0;
  }

  if (ev->type == LK_EVENT_POINTER_DOWN) {
    /* Only the trigger itself routes POINTER_DOWN through the widget
     * handler.  Option clicks are dispatched via overlay hit-test and
     * arrive as events targeting the option node; handled there. */
    if (ev->target != n) {
      return 0;
    }

    if (is_expanded(st, nid)) {
      lk_state_set(st, nid, LKS_EXPANDED, lk_v_i32(0));
    } else {
      lk_state_set(st, nid, LKS_EXPANDED, lk_v_i32(1));
      lk_state_set(st, nid, LKS_HOVER_INDEX, lk_v_i32(sel));
    }

    return 1;
  }

  if (ev->type == LK_EVENT_KEY_DOWN) {
    kc = ev->data.key.keycode;
    hov = get_i32(st, nid, LKS_HOVER_INDEX);
    if (hov < 0 || (lk_u32)hov >= count) {
      hov = sel;
    }

    switch (kc) {
    case LKK_DOWN:
      if (is_expanded(st, nid)) {
        if ((lk_u32)(hov + 1) < count) {
          lk_state_set(st, nid, LKS_HOVER_INDEX, lk_v_i32(hov + 1));
        }
      } else {
        /* Collapsed + Down opens and moves selection. */
        lk_state_set(st, nid, LKS_EXPANDED, lk_v_i32(1));
        lk_state_set(st, nid, LKS_HOVER_INDEX, lk_v_i32(sel));
      }
      return 1;

    case LKK_UP:
      if (is_expanded(st, nid) && hov > 0) {
        lk_state_set(st, nid, LKS_HOVER_INDEX, lk_v_i32(hov - 1));
      }
      return 1;

    case LKK_RETURN:
    case LKK_SPACE:
      if (is_expanded(st, nid)) {
        /* Commit: hover -> selection. */
        if (hov >= 0 && (lk_u32)hov < count) {
          lk_ix opt = nth_option(t, n, (lk_u32)hov);

          lk_state_set(st, nid, LKS_SELECTED_INDEX, lk_v_i32(hov));
          lk_state_set(st, nid, LKS_EXPANDED, lk_v_i32(0));

          if (opt) {
            emit_value_changed(ui, t, n, lk_node_text_id(t, opt));
          }
        }
      } else {
        lk_state_set(st, nid, LKS_EXPANDED, lk_v_i32(1));
        lk_state_set(st, nid, LKS_HOVER_INDEX, lk_v_i32(sel));
      }
      return 1;

    case LKK_ESCAPE:
      if (is_expanded(st, nid)) {
        lk_state_set(st, nid, LKS_EXPANDED, lk_v_i32(0));
        return 1;
      }
      return 0;

    default: break;
    }
  }

  return 0;
}

/* An OPTION node can receive POINTER_DOWN from the overlay hit-test.
 * Its widget event handler commits the selection on its parent
 * dropdown. */
static int event_option(lk_ui *ui, const lk_tree *t, lk_ix n, lk_event *ev) {
  lk_ix parent;
  lk_state *st;
  lk_node_id nid;
  lk_u32 i = 0;
  lk_ix ch;

  if (ev->type != LK_EVENT_POINTER_DOWN || ev->target != n) {
    return 0;
  }

  parent = t->nodes[n].parent;
  if (!parent || t->nodes[parent].kind != UIK_DROPDOWN) {
    return 0;
  }

  /* Find this option's index within the parent. */
  ch = t->nodes[parent].first_child;
  while (ch) {
    if (t->nodes[ch].kind == UIK_OPTION) {
      if (ch == n) {
        break;
      }
      i++;
    }
    ch = t->nodes[ch].next_sibling;
  }

  st = lk_ui_state(ui);
  nid = t->nodes[parent].id;
  lk_state_set(st, nid, LKS_SELECTED_INDEX, lk_v_i32((lk_i32)i));
  lk_state_set(st, nid, LKS_EXPANDED, lk_v_i32(0));
  emit_value_changed(ui, t, parent, lk_node_text_id(t, n));

  return 1;
}

/* ---- Overlay geometry ---- */

lk_rect lk_dropdown_popup_rect(const lk_tree *t, lk_ix n,
                                const lk_rect *rects,
                                const lk_style *styles,
                                const lk_layout_cfg *cfg) {
  lk_rect trigger = rects[n];
  lk_rect out;
  lk_u32 count = count_options(t, n);
  lk_i32 row_h = option_row_height(cfg);
  lk_i32 pad = styles ? styles[n].padding : 0;
  lk_i32 bw = styles ? styles[n].border_width : 0;
  lk_i32 popup_h;

  popup_h = (lk_i32)count * row_h + (pad + bw) * 2;
  if (popup_h > DROPDOWN_POPUP_MAX_HEIGHT) {
    popup_h = DROPDOWN_POPUP_MAX_HEIGHT;
  }

  out.x = trigger.x;
  out.y = trigger.y + trigger.h;
  out.w = trigger.w;
  out.h = popup_h;
  return out;
}

lk_rect lk_dropdown_option_rect(const lk_tree *t, lk_ix n,
                                 lk_u32 opt_index,
                                 const lk_rect *rects,
                                 const lk_style *styles,
                                 const lk_layout_cfg *cfg) {
  lk_rect popup = lk_dropdown_popup_rect(t, n, rects, styles, cfg);
  lk_rect out;
  lk_i32 row_h = option_row_height(cfg);
  lk_i32 pad = styles ? styles[n].padding : 0;
  lk_i32 bw = styles ? styles[n].border_width : 0;
  lk_i32 inset = pad + bw;

  out.x = popup.x + inset;
  out.y = popup.y + inset + (lk_i32)opt_index * row_h;
  out.w = popup.w - inset * 2;
  out.h = row_h;
  return out;
}

/* ---- Overlay render ---- */

int lk_render_build_overlays(const lk_tree *t, const lk_rect *rects,
                              const lk_style *styles, const lk_state *state,
                              const lk_layout_cfg *cfg, lk_render_list *out) {
  lk_ix n;

  if (!t || !rects || !out) {
    return 0;
  }

  for (n = 1; n < (lk_ix)t->node_count; n++) {
    const lk_node *nd = &t->nodes[n];
    lk_node_id nid;
    lk_u32 count;
    lk_i32 hov;
    lk_u32 i;
    const lk_style *trig_style;
    const lk_style *opt_style;
    lk_rect popup;
    lk_render_cmd cmd;

    if (nd->kind != UIK_DROPDOWN) {
      continue;
    }

    nid = nd->id;
    if (!is_expanded(state, nid)) {
      continue;
    }

    count = count_options(t, n);
    if (count == 0) {
      continue;
    }

    trig_style = styles ? &styles[n] : NULL;
    popup = lk_dropdown_popup_rect(t, n, rects, styles, cfg);

    /* Popup background */
    memset(&cmd, 0, sizeof(cmd));
    cmd.op = LK_ROP_FILL_RECT;
    cmd.rect = popup;
    if (trig_style) {
      cmd.color = trig_style->bg;
    } else {
      cmd.color.r = 0;
      cmd.color.g = 0;
      cmd.color.b = 0;
      cmd.color.a = 255;
    }
    lk_render_list_push(out, cmd);

    /* Border (1px) */
    if (trig_style && trig_style->border_width > 0) {
      lk_color bc = trig_style->border_color;
      lk_i32 bw = trig_style->border_width;

      memset(&cmd, 0, sizeof(cmd));
      cmd.op = LK_ROP_FILL_RECT;
      cmd.color = bc;
      /* top */
      cmd.rect.x = popup.x;
      cmd.rect.y = popup.y;
      cmd.rect.w = popup.w;
      cmd.rect.h = bw;
      lk_render_list_push(out, cmd);
      /* bottom */
      cmd.rect.y = popup.y + popup.h - bw;
      lk_render_list_push(out, cmd);
      /* left */
      cmd.rect.y = popup.y;
      cmd.rect.w = bw;
      cmd.rect.h = popup.h;
      lk_render_list_push(out, cmd);
      /* right */
      cmd.rect.x = popup.x + popup.w - bw;
      lk_render_list_push(out, cmd);
    }

    hov = get_i32(state, nid, LKS_HOVER_INDEX);

    /* Options */
    for (i = 0; i < count; i++) {
      lk_ix opt = nth_option(t, n, i);
      lk_rect r;
      lk_color hl;

      if (!opt) {
        continue;
      }

      r = lk_dropdown_option_rect(t, n, i, rects, styles, cfg);
      opt_style = styles ? &styles[opt] : trig_style;

      /* Hover highlight */
      if ((lk_i32)i == hov) {
        memset(&cmd, 0, sizeof(cmd));
        cmd.op = LK_ROP_FILL_RECT;
        cmd.rect = r;
        if (trig_style) {
          hl = trig_style->border_color;
        } else {
          hl.r = 80;
          hl.g = 100;
          hl.b = 160;
          hl.a = 255;
        }
        cmd.color = hl;
        lk_render_list_push(out, cmd);
      }

      /* Option text */
      {
        lk_u32 sid = lk_node_text_id(t, opt);
        if (sid != 0) {
          memset(&cmd, 0, sizeof(cmd));
          cmd.op = LK_ROP_DRAW_TEXT;
          cmd.rect.x = r.x + DROPDOWN_OPTION_PAD_Y;
          cmd.rect.y = r.y + DROPDOWN_OPTION_PAD_Y;
          cmd.rect.w = r.w - DROPDOWN_OPTION_PAD_Y * 2;
          cmd.rect.h = r.h - DROPDOWN_OPTION_PAD_Y * 2;
          if (opt_style) {
            cmd.color = opt_style->fg;
          } else {
            cmd.color.r = 220;
            cmd.color.g = 220;
            cmd.color.b = 220;
            cmd.color.a = 255;
          }
          cmd.str_id = sid;
          lk_render_list_push(out, cmd);
        }
      }
    }
  }

  return 1;
}

/* ---- Overlay hit-test ---- */

static int point_in(const lk_rect *r, lk_i32 x, lk_i32 y) {
  return x >= r->x && x < r->x + r->w && y >= r->y && y < r->y + r->h;
}

lk_ix lk_hit_test_overlay(const lk_tree *t, const lk_rect *rects,
                           const lk_style *styles, const lk_state *state,
                           const lk_layout_cfg *cfg, lk_i32 x, lk_i32 y) {
  lk_ix n;

  if (!t || !rects) {
    return 0;
  }

  for (n = 1; n < (lk_ix)t->node_count; n++) {
    const lk_node *nd = &t->nodes[n];
    lk_rect popup;
    lk_u32 count;
    lk_u32 i;

    if (nd->kind != UIK_DROPDOWN) {
      continue;
    }

    if (!is_expanded(state, nd->id)) {
      continue;
    }

    popup = lk_dropdown_popup_rect(t, n, rects, styles, cfg);
    if (!point_in(&popup, x, y)) {
      continue;
    }

    count = count_options(t, n);
    for (i = 0; i < count; i++) {
      lk_ix opt = nth_option(t, n, i);
      lk_rect r = lk_dropdown_option_rect(t, n, i, rects, styles, cfg);

      if (opt && point_in(&r, x, y)) {
        return opt;
      }
    }

    /* Click inside popup but outside any option (padding zone) —
     * consume as a hit on the dropdown itself so the click doesn't
     * close the overlay. */
    return n;
  }

  return 0;
}

/* ---- Overlay dismissal ---- */

int lk_overlay_dismiss_outside(lk_ui *ui, const lk_rect *rects,
                                const lk_style *styles,
                                const lk_layout_cfg *cfg,
                                lk_i32 x, lk_i32 y) {
  const lk_tree *t;
  lk_state *st;
  lk_ix n;
  int dismissed = 0;

  if (!ui) {
    return 0;
  }

  t = lk_ui_tree(ui);
  st = lk_ui_state(ui);
  if (!t || !st) {
    return 0;
  }

  for (n = 1; n < (lk_ix)t->node_count; n++) {
    const lk_node *nd = &t->nodes[n];
    lk_rect popup;
    lk_rect trigger;

    if (nd->kind != UIK_DROPDOWN) {
      continue;
    }

    if (!is_expanded(st, nd->id)) {
      continue;
    }

    popup = lk_dropdown_popup_rect(t, n, rects, styles, cfg);
    trigger = rects[n];

    if (!point_in(&popup, x, y) && !point_in(&trigger, x, y)) {
      lk_state_set(st, nd->id, LKS_EXPANDED, lk_v_i32(0));
      dismissed = 1;
    }
  }

  return dismissed;
}

/* ---- Registration ---- */

lk_widget_def lk_dropdown_widget_def(void) {
  lk_widget_def def;
  memset(&def, 0, sizeof(def));
  def.measure = measure_dropdown;
  def.layout = 0; /* leaf — options live in the overlay, not the tree
                   * layout.  See docs/overlays.md. */
  def.render = render_dropdown;
  def.event = event_dropdown;
  def.clips = 0;
  return def;
}

lk_widget_def lk_option_widget_def(void) {
  lk_widget_def def;
  memset(&def, 0, sizeof(def));
  def.measure = measure_option;
  def.layout = 0;
  def.render = render_option;
  def.event = event_option;
  def.clips = 0;
  return def;
}
