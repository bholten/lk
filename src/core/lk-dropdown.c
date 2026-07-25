/*
 * lk-dropdown.c — Selection dropdown (UIK_DROPDOWN) and option
 * (UIK_OPTION) widgets.
 *
 * UIK_DROPDOWN is a leaf in the main layout pass: its children
 * (UIK_OPTIONs) are NOT laid out or rendered by the main pass.
 * Opening the dropdown pushes an LK_OVERLAY_DROPDOWN_POPUP overlay
 * onto the ui's overlay stack (and sets LKS_EXPANDED — the two are
 * kept in sync); the overlay pass in lk-overlay.c dispatches back
 * here (lk_dropdown_render_popup / lk_dropdown_hit_popup) to draw and
 * hit-test the popup procedurally.  Popup geometry goes through
 * lk_anchor_resolve, so a dropdown near the bottom edge flips its
 * popup above the trigger instead of clipping.
 *
 * See docs/overlays.md.
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

/* Open/close keep LKS_EXPANDED (the public state-key invariant) and
 * the ui overlay stack in sync. */
static void dropdown_open(lk_ui *ui, lk_node_id nid, lk_i32 hover) {
  lk_state *st = lk_ui_state(ui);
  lk_overlay ov;

  lk_state_set(st, nid, LKS_EXPANDED, lk_v_i32(1));
  lk_state_set(st, nid, LKS_HOVER_INDEX, lk_v_i32(hover));

  memset(&ov, 0, sizeof(ov));
  ov.kind = LK_OVERLAY_DROPDOWN_POPUP;
  ov.anchor_mode = LK_ANCHOR_BELOW;
  ov.dismiss_on_outside = 1;
  ov.traps_focus = 0;
  ov.owner_id = nid;
  ov.content_root_id = 0;

  lk_overlay_pop_owner(ui, nid); /* never stack two popups per owner */
  lk_overlay_push(ui, &ov);
}

static void dropdown_close(lk_ui *ui, lk_node_id nid) {
  lk_state *st = lk_ui_state(ui);

  lk_state_set(st, nid, LKS_EXPANDED, lk_v_i32(0));
  lk_overlay_pop_owner(ui, nid);
}

static int point_in(const lk_rect *r, lk_i32 x, lk_i32 y) {
  return x >= r->x && x < r->x + r->w && y >= r->y && y < r->y + r->h;
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

/* Measure a run through cfg->text with the node's resolved font
 * (0/0 defaults when no styles).  Zero metrics when no backend. */
static void measure_run(const lk_layout_cfg *cfg, lk_ix n, lk_str run,
                        lk_i32 *out_w, lk_i32 *out_h) {
  lk_text_metrics m;

  m.w = 0;
  m.h = 0;
  m.baseline = 0;

  if (cfg && cfg->text) {
    lk_u16 font_id = cfg->styles ? (lk_u16)cfg->styles[n].font_id : 0;
    lk_u16 font_size = cfg->styles ? (lk_u16)cfg->styles[n].font_size : 0;
    cfg->text->measure(cfg->text->ud, run, font_id, font_size, &m);
  }

  *out_w = m.w;
  *out_h = m.h;
}

/* Row-height of an option: max(measured-text-height, min) + pad*2.
 * No node context here, so probes with the default face (0/0). */
static lk_i32 option_row_height(const lk_layout_cfg *cfg) {
  lk_i32 th = 0;
  lk_i32 tw = 0;
  lk_str probe;
  lk_text_metrics m;

  probe.ptr = "Xg";
  probe.len = 2;

  if (cfg && cfg->text) {
    m.w = 0;
    m.h = 0;
    m.baseline = 0;
    cfg->text->measure(cfg->text->ud, probe, 0, 0, &m);
    tw = m.w;
    th = m.h;
  }

  (void)tw;

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
  measure_run(cfg, n, s, &tw, &th);

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
    cmd.font_id = (lk_u16)style->font_id;
    cmd.font_size = (lk_u16)style->font_size;
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
      /* While expanded, a POINTER_DOWN targeting this node is either a
       * trigger click (toggle closed) or a click in the popup's padding
       * zone (the overlay hit-test resolves those to the dropdown too).
       * The trigger rect is stashed in state by the layout pass; when
       * absent (host never ran layout with state) fall back to the old
       * always-toggle behavior. */
      lk_value tw = lk_state_get(st, nid, LKS_TRIGGER_W);

      if (tw.tag == UIV_I32 && (lk_i32)tw.as.i > 0) {
        lk_rect tr;

        tr.x = get_i32(st, nid, LKS_TRIGGER_X);
        tr.y = get_i32(st, nid, LKS_TRIGGER_Y);
        tr.w = (lk_i32)tw.as.i;
        tr.h = get_i32(st, nid, LKS_TRIGGER_H);

        if (!point_in(&tr, ev->data.pointer.x, ev->data.pointer.y)) {
          /* Popup-padding click: consume without closing. */
          return 1;
        }
      }

      dropdown_close(ui, nid);
    } else {
      dropdown_open(ui, nid, sel);
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
        dropdown_open(ui, nid, sel);
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
          dropdown_close(ui, nid);

          if (opt) {
            emit_value_changed(ui, t, n, lk_node_text_id(t, opt));
          }
        }
      } else {
        dropdown_open(ui, nid, sel);
      }
      return 1;

    case LKK_ESCAPE:
      /* Normally consumed by the ESC pre-step in lk_event_route (which
       * pops the topmost overlay); kept here as a fallback for hosts
       * that dispatch widget events directly. */
      if (is_expanded(st, nid)) {
        dropdown_close(ui, nid);
        return 1;
      }
      return 0;

    default: break;
    }
  }

  return 0;
}

/* An OPTION node can receive POINTER_DOWN and POINTER_MOVE from the
 * overlay hit-test.  Its widget event handler commits the selection
 * (down) or updates the hover highlight (move) on its parent dropdown. */
static int event_option(lk_ui *ui, const lk_tree *t, lk_ix n, lk_event *ev) {
  lk_ix parent;
  lk_state *st;
  lk_node_id nid;
  lk_u32 i = 0;
  lk_ix ch;

  if (ev->target != n) {
    return 0;
  }

  if (ev->type != LK_EVENT_POINTER_DOWN &&
      ev->type != LK_EVENT_POINTER_MOVE) {
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

  if (ev->type == LK_EVENT_POINTER_MOVE) {
    /* Hover highlight follows the mouse while the popup is open. */
    lk_state_set(st, nid, LKS_HOVER_INDEX, lk_v_i32((lk_i32)i));
    return 1;
  }

  lk_state_set(st, nid, LKS_SELECTED_INDEX, lk_v_i32((lk_i32)i));
  dropdown_close(ui, nid);
  emit_value_changed(ui, t, parent, lk_node_text_id(t, n));

  return 1;
}

/* ---- Overlay geometry ---- */

/* Stash each dropdown's trigger rect in retained state.  Called by
 * lk_layout after rects are final so event handling (which has no
 * access to layout rects) can distinguish trigger clicks from
 * popup-padding clicks.  Lean overlay scaffolding — see
 * docs/overlays.md. */
void lk_dropdown_store_trigger_rects(const lk_tree *t, const lk_rect *rects,
                                     lk_state *state) {
  lk_ix n;

  if (!t || !rects || !state) {
    return;
  }

  for (n = 1; n < (lk_ix)t->node_count; n++) {
    lk_node_id nid;

    if (t->nodes[n].kind != UIK_DROPDOWN) {
      continue;
    }

    nid = t->nodes[n].id;
    lk_state_set(state, nid, LKS_TRIGGER_X, lk_v_i32(rects[n].x));
    lk_state_set(state, nid, LKS_TRIGGER_Y, lk_v_i32(rects[n].y));
    lk_state_set(state, nid, LKS_TRIGGER_W, lk_v_i32(rects[n].w));
    lk_state_set(state, nid, LKS_TRIGGER_H, lk_v_i32(rects[n].h));
  }
}

lk_rect lk_dropdown_popup_rect(const lk_tree *t, lk_ix n,
                                const lk_rect *rects,
                                const lk_style *styles,
                                const lk_layout_cfg *cfg) {
  lk_rect trigger = rects[n];
  lk_u32 count = count_options(t, n);
  lk_i32 row_h = option_row_height(cfg);
  lk_i32 pad = styles ? styles[n].padding : 0;
  lk_i32 bw = styles ? styles[n].border_width : 0;
  lk_i32 popup_h;
  lk_overlay ov;

  popup_h = (lk_i32)count * row_h + (pad + bw) * 2;
  if (popup_h > DROPDOWN_POPUP_MAX_HEIGHT) {
    popup_h = DROPDOWN_POPUP_MAX_HEIGHT;
  }

  /* Anchor below the trigger, same width; flips above / clamps at the
   * viewport edges (0/0 viewport skips clamping — old behavior). */
  memset(&ov, 0, sizeof(ov));
  ov.kind = LK_OVERLAY_DROPDOWN_POPUP;
  ov.anchor_mode = LK_ANCHOR_BELOW;

  return lk_anchor_resolve(&ov, trigger, cfg ? cfg->viewport_w : 0,
                           cfg ? cfg->viewport_h : 0, trigger.w, popup_h);
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

/* ---- Overlay render (dispatch target for lk-overlay.c) ---- */

void lk_dropdown_render_popup(const lk_tree *t, lk_ix n,
                              const lk_rect *rects, const lk_style *styles,
                              const lk_state *state,
                              const lk_layout_cfg *cfg, lk_render_list *out) {
  lk_node_id nid;
  lk_u32 count;
  lk_i32 hov;
  lk_u32 i;
  const lk_style *trig_style;
  const lk_style *opt_style;
  lk_rect popup;
  lk_render_cmd cmd;

  if (!t || !rects || !out || n == 0 || n >= (lk_ix)t->node_count) {
    return;
  }

  if (t->nodes[n].kind != UIK_DROPDOWN) {
    return;
  }

  nid = t->nodes[n].id;
  if (!is_expanded(state, nid)) {
    return;
  }

  count = count_options(t, n);
  if (count == 0) {
    return;
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
          cmd.font_id = (lk_u16)opt_style->font_id;
          cmd.font_size = (lk_u16)opt_style->font_size;
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

/* ---- Overlay hit-test (dispatch target for lk-overlay.c) ---- */

lk_ix lk_dropdown_hit_popup(const lk_tree *t, lk_ix n, const lk_rect *rects,
                            const lk_style *styles, const lk_state *state,
                            const lk_layout_cfg *cfg, lk_i32 x, lk_i32 y) {
  lk_rect popup;
  lk_u32 count;
  lk_u32 i;

  if (!t || !rects || n == 0 || n >= (lk_ix)t->node_count) {
    return 0;
  }

  if (t->nodes[n].kind != UIK_DROPDOWN) {
    return 0;
  }

  if (!is_expanded(state, t->nodes[n].id)) {
    return 0;
  }

  popup = lk_dropdown_popup_rect(t, n, rects, styles, cfg);
  if (!point_in(&popup, x, y)) {
    return 0;
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
