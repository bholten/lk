

#include <string.h>

#include "lk-dropdown.h"
#include "lk-memory.h"
#include <lk.h>

#define DROPDOWN_CHEVRON_W 14
#define DROPDOWN_OPTION_PAD_Y 4
#define DROPDOWN_MIN_OPTION_H 20
#define DROPDOWN_POPUP_MAX_HEIGHT                                              \
  240
#define DROPDOWN_SCROLL_BAR_W 6

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

static void dropdown_open(lk_ui *ui, lk_node_id nid, lk_i32 hover) {
  lk_state *st = lk_ui_state(ui);
  lk_overlay ov;

  lk_state_set(st, nid, LKS_EXPANDED, lk_v_i32(1));
  lk_state_set(st, nid, LKS_HOVER_INDEX, lk_v_i32(hover));
  lk_state_set(st, nid, LKS_POPUP_SCROLL, lk_v_i32(0));

  memset(&ov, 0, sizeof(ov));
  ov.kind = LK_OVERLAY_DROPDOWN_POPUP;
  ov.anchor_mode = LK_ANCHOR_BELOW;
  ov.dismiss_on_outside = 1;
  ov.traps_focus = 0;
  ov.owner_id = nid;
  ov.content_root_id = 0;

  lk_overlay_pop_owner(ui, nid);
  lk_overlay_push(ui, &ov);
}

static void dropdown_close(lk_ui *ui, lk_node_id nid) {
  lk_state *st = lk_ui_state(ui);

  lk_state_set(st, nid, LKS_EXPANDED, lk_v_i32(0));
  lk_state_set(st, nid, LKS_POPUP_SCROLL, lk_v_i32(0));
  lk_overlay_pop_owner(ui, nid);
}

static int point_in(const lk_rect *r, lk_i32 x, lk_i32 y) {
  return x >= r->x && x < r->x + r->w && y >= r->y && y < r->y + r->h;
}

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

static lk_u32 value_prop_id(const lk_tree *t, lk_ix n) {
  const lk_node *nd = &t->nodes[n];
  lk_u16 i;

  for (i = 0; i < nd->props_len; i++) {
    const lk_prop *p = &t->props[nd->props_off + i];

    if (p->key == (lk_u16)UIP_VALUE && p->value.tag == UIV_STR) {
      return p->value.as.str_id;
    }
  }

  return 0;
}

static lk_i32 dropdown_selected(const lk_tree *t, lk_ix n,
                                const lk_state *state) {
  lk_u32 vid;

  if (state) {
    lk_value v = lk_state_get(state, t->nodes[n].id, LKS_SELECTED_INDEX);

    if (v.tag == UIV_I32) {
      return (lk_i32)v.as.i;
    }
  }

  vid = value_prop_id(t, n);

  if (vid != 0) {
    lk_ix ch = t->nodes[n].first_child;
    lk_i32 i = 0;

    while (ch) {
      if (t->nodes[ch].kind == UIK_OPTION) {
        if (lk_node_text_id(t, ch) == vid) {
          return i;
        }

        i++;
      }

      ch = t->nodes[ch].next_sibling;
    }
  }

  return 0;
}

static lk_u32 selected_text_id(const lk_tree *t, lk_ix n,
                               const lk_state *state) {
  lk_i32 sel = dropdown_selected(t, n, state);
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

  if (cfg && cfg->grid_h > 0) {

    lk_i32 gh = cfg->grid_h;

    return th > 0 ? ((th + gh - 1) / gh) * gh : gh;
  }

  if (th < DROPDOWN_MIN_OPTION_H - DROPDOWN_OPTION_PAD_Y * 2) {
    th = DROPDOWN_MIN_OPTION_H - DROPDOWN_OPTION_PAD_Y * 2;
  }

  return th + DROPDOWN_OPTION_PAD_Y * 2;
}

static lk_i32 option_text_pad(const lk_layout_cfg *cfg) {
  return (cfg && cfg->grid_h > 0) ? 0 : DROPDOWN_OPTION_PAD_Y;
}

static lk_i32 popup_inner_h(lk_u32 count, lk_i32 row_h, lk_i32 inset) {
  lk_i32 popup_h = (lk_i32)count * row_h + inset * 2;
  lk_i32 inner;

  if (popup_h > DROPDOWN_POPUP_MAX_HEIGHT) {
    popup_h = DROPDOWN_POPUP_MAX_HEIGHT;
  }

  inner = popup_h - inset * 2;

  return inner > 0 ? inner : 0;
}

static lk_i32 popup_scroll_max(lk_u32 count, lk_i32 row_h, lk_i32 inset) {
  lk_i32 content = (lk_i32)count * row_h;
  lk_i32 inner = popup_inner_h(count, row_h, inset);

  return content > inner ? content - inner : 0;
}

static lk_i32 popup_scroll_eff(const lk_tree *t, lk_ix n, const lk_state *state,
                               lk_i32 max) {
  lk_i32 s = get_i32(state, t->nodes[n].id, LKS_POPUP_SCROLL);

  if (s < 0) {
    s = 0;
  }

  if (s > max) {
    s = max;
  }

  return s;
}

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
    tw = 80;
  }

  *out_w = tw + DROPDOWN_CHEVRON_W + inset * 2;
  *out_h = th + inset * 2;
}

static void measure_option(const lk_tree *t, lk_ix n, const lk_size *sizes,
                           const lk_layout_cfg *cfg, lk_i32 *out_w,
                           lk_i32 *out_h) {

  (void)t;
  (void)n;
  (void)sizes;
  (void)cfg;
  *out_w = 0;
  *out_h = 0;
}

static void render_dropdown(const lk_tree *t, lk_ix n, const lk_rect *rect,
                            const lk_style *style, const lk_state *state,
                            const lk_widget_geom *geom, lk_render_list *out) {
  lk_i32 pad = style->padding;
  lk_i32 bw = style->border_width;
  lk_i32 inset = pad + bw;
  lk_u32 sid;
  lk_render_cmd cmd;

  (void)geom;

  memset(&cmd, 0, sizeof(cmd));
  cmd.op = LK_ROP_FILL_RECT;
  cmd.rect = *rect;
  cmd.color = style->bg;
  lk_render_list_push(out, cmd);

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

static void render_option(const lk_tree *t, lk_ix n, const lk_rect *rect,
                          const lk_style *style, const lk_state *state,
                          const lk_widget_geom *geom, lk_render_list *out) {
  (void)t;
  (void)n;
  (void)rect;
  (void)style;
  (void)state;
  (void)geom;
  (void)out;
}

static void emit_value_changed(lk_ui *ui, const lk_tree *t, lk_ix n,
                               lk_u32 new_value_id) {
  lk_event ev;
  (void)t;
  memset(&ev, 0, sizeof(ev));
  ev.type = LK_EVENT_VALUE_CHANGED;
  ev.target = n;
  ev.data.value_changed.str_id = new_value_id;
  lk_event_enqueue(ui, &ev);
}

static const lk_widget_geom *dropdown_geom(const lk_ui *ui, lk_ix n) {
  if (!ui->geom || (lk_u32)n >= ui->geom_cap) {
    return NULL;
  }

  return &ui->geom[n];
}

static void dropdown_scroll_to_hover(lk_ui *ui, const lk_tree *t, lk_ix n,
                                     lk_i32 hov, lk_u32 count) {
  lk_state *st = lk_ui_state(ui);
  lk_node_id nid = t->nodes[n].id;
  const lk_widget_geom *g = dropdown_geom(ui, n);
  lk_i32 row_h;
  lk_i32 inset;
  lk_i32 max;
  lk_i32 inner;
  lk_i32 s;
  lk_i32 top;
  lk_i32 bot;

  if (!g || g->trigger.row_h == 0 || hov < 0) {
    return;
  }

  row_h = (lk_i32)g->trigger.row_h;
  inset = (lk_i32)g->trigger.inset;
  max = popup_scroll_max(count, row_h, inset);

  if (max <= 0) {
    return;
  }

  inner = popup_inner_h(count, row_h, inset);
  s = popup_scroll_eff(t, n, st, max);
  top = hov * row_h;
  bot = top + row_h;

  if (bot - s > inner) {
    s = bot - inner;
  }

  if (top < s) {
    s = top;
  }

  if (s < 0) {
    s = 0;
  }

  if (s > max) {
    s = max;
  }

  lk_state_set(st, nid, LKS_POPUP_SCROLL, lk_v_i32(s));
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

  sel = dropdown_selected(t, n, st);

  if (sel < 0 || (lk_u32)sel >= count) {
    sel = 0;
  }

  if (ev->type == LK_EVENT_POINTER_DOWN) {

    if (ev->target != n) {
      return 0;
    }

    if (is_expanded(st, nid)) {

      const lk_widget_geom *g = dropdown_geom(ui, n);

      if (g && g->trigger.w > 0) {
        lk_rect tr;

        tr.x = g->trigger.x;
        tr.y = g->trigger.y;
        tr.w = g->trigger.w;
        tr.h = g->trigger.h;

        if (!point_in(&tr, ev->data.pointer.x, ev->data.pointer.y)) {

          return 1;
        }
      }

      dropdown_close(ui, nid);
    } else {
      dropdown_open(ui, nid, sel);
      dropdown_scroll_to_hover(ui, t, n, sel, count);
    }

    return 1;
  }

  if (ev->type == LK_EVENT_WHEEL) {

    const lk_widget_geom *g;
    lk_i32 row_h;
    lk_i32 inset;
    lk_i32 max;
    lk_i32 s;

    if (!is_expanded(st, nid)) {
      return 0;
    }

    g = dropdown_geom(ui, n);

    if (!g || g->trigger.row_h == 0) {
      return 1;
    }

    row_h = (lk_i32)g->trigger.row_h;
    inset = (lk_i32)g->trigger.inset;
    max = popup_scroll_max(count, row_h, inset);

    if (max <= 0) {
      return 1;
    }

    s = popup_scroll_eff(t, n, st, max) - ev->data.wheel.dy * row_h;

    if (s < 0) {
      s = 0;
    }

    if (s > max) {
      s = max;
    }

    lk_state_set(st, nid, LKS_POPUP_SCROLL, lk_v_i32(s));

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
          dropdown_scroll_to_hover(ui, t, n, hov + 1, count);
        }
      } else {

        dropdown_open(ui, nid, sel);
        dropdown_scroll_to_hover(ui, t, n, sel, count);
      }
      return 1;

    case LKK_UP:
      if (is_expanded(st, nid) && hov > 0) {
        lk_state_set(st, nid, LKS_HOVER_INDEX, lk_v_i32(hov - 1));
        dropdown_scroll_to_hover(ui, t, n, hov - 1, count);
      }
      return 1;

    case LKK_RETURN:
    case LKK_SPACE:
      if (is_expanded(st, nid)) {

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
        dropdown_scroll_to_hover(ui, t, n, sel, count);
      }
      return 1;

    case LKK_ESCAPE:

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

static int event_option(lk_ui *ui, const lk_tree *t, lk_ix n, lk_event *ev) {
  lk_ix parent;
  lk_state *st;
  lk_node_id nid;
  lk_u32 i = 0;
  lk_ix ch;

  if (ev->target != n) {
    return 0;
  }

  if (ev->type != LK_EVENT_POINTER_DOWN && ev->type != LK_EVENT_POINTER_MOVE) {
    return 0;
  }

  parent = t->nodes[n].parent;

  if (!parent || t->nodes[parent].kind != UIK_DROPDOWN) {
    return 0;
  }

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

    lk_state_set(st, nid, LKS_HOVER_INDEX, lk_v_i32((lk_i32)i));
    return 1;
  }

  lk_state_set(st, nid, LKS_SELECTED_INDEX, lk_v_i32((lk_i32)i));
  dropdown_close(ui, nid);
  emit_value_changed(ui, t, parent, lk_node_text_id(t, n));

  return 1;
}

void lk_dropdown_store_trigger_rects(const lk_tree *t, const lk_rect *rects,
                                     const lk_layout_cfg *cfg) {
  lk_ix n;
  lk_i32 row_h;

  if (!t || !rects || !cfg || !cfg->geom) {
    return;
  }

  row_h = option_row_height(cfg);

  for (n = 1; n < (lk_ix)t->node_count; n++) {
    lk_widget_geom *g;
    lk_i32 pad;
    lk_i32 bw;

    if (t->nodes[n].kind != UIK_DROPDOWN) {
      continue;
    }

    pad = cfg->styles ? cfg->styles[n].padding : 0;
    bw = cfg->styles ? cfg->styles[n].border_width : 0;

    g = &cfg->geom[n];
    g->trigger.x = rects[n].x;
    g->trigger.y = rects[n].y;
    g->trigger.w = rects[n].w;
    g->trigger.h = rects[n].h;
    g->trigger.row_h = (lk_u16)row_h;
    g->trigger.inset = (lk_u16)(pad + bw);
  }
}

lk_rect lk_dropdown_popup_rect(const lk_tree *t, lk_ix n, const lk_rect *rects,
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

  memset(&ov, 0, sizeof(ov));
  ov.kind = LK_OVERLAY_DROPDOWN_POPUP;
  ov.anchor_mode = LK_ANCHOR_BELOW;

  {
    lk_rect r =
        lk_anchor_resolve(&ov, trigger, cfg ? cfg->viewport_w : 0,
                          cfg ? cfg->viewport_h : 0, trigger.w, popup_h);

    if (cfg) {
      lk_rect_snap_grid(&r, cfg->grid_w, cfg->grid_h);
    }

    return r;
  }
}

lk_rect lk_dropdown_option_rect(const lk_tree *t, lk_ix n, lk_u32 opt_index,
                                const lk_rect *rects, const lk_style *styles,
                                const lk_layout_cfg *cfg) {
  lk_rect popup = lk_dropdown_popup_rect(t, n, rects, styles, cfg);
  lk_rect out;
  lk_i32 row_h = option_row_height(cfg);
  lk_i32 pad = styles ? styles[n].padding : 0;
  lk_i32 bw = styles ? styles[n].border_width : 0;
  lk_i32 inset = pad + bw;
  lk_i32 scroll =
      popup_scroll_eff(t, n, cfg ? cfg->state : NULL,
                       popup_scroll_max(count_options(t, n), row_h, inset));

  out.x = popup.x + inset;
  out.y = popup.y + inset + (lk_i32)opt_index * row_h - scroll;
  out.w = popup.w - inset * 2;
  out.h = row_h;
  return out;
}

void lk_dropdown_render_popup(const lk_tree *t, lk_ix n, const lk_rect *rects,
                              const lk_style *styles, const lk_state *state,
                              const lk_layout_cfg *cfg, lk_render_list *out) {
  lk_node_id nid;
  lk_u32 count;
  lk_i32 hov;
  lk_u32 i;
  const lk_style *trig_style;
  const lk_style *opt_style;
  lk_rect popup;
  lk_render_cmd cmd;
  lk_i32 inset;
  lk_i32 row_h;
  lk_i32 max_scroll;
  lk_i32 scroll;
  lk_i32 inner_h;

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

  if (trig_style && trig_style->border_width > 0) {
    lk_i32 bw = trig_style->border_width;

    memset(&cmd, 0, sizeof(cmd));
    cmd.op = LK_ROP_STROKE_RECT;
    cmd.color = trig_style->border_color;
    cmd.rect = popup;
    cmd.stroke = (lk_u8)(bw > 255 ? 255 : bw);
    lk_render_list_push(out, cmd);
  }

  hov = get_i32(state, nid, LKS_HOVER_INDEX);

  {
    lk_i32 pad = trig_style ? trig_style->padding : 0;
    lk_i32 bw = trig_style ? trig_style->border_width : 0;

    inset = pad + bw;
  }

  row_h = option_row_height(cfg);
  max_scroll = popup_scroll_max(count, row_h, inset);
  scroll = popup_scroll_eff(t, n, cfg ? cfg->state : NULL, max_scroll);
  inner_h = popup_inner_h(count, row_h, inset);

  if (max_scroll > 0) {
    memset(&cmd, 0, sizeof(cmd));
    cmd.op = LK_ROP_CLIP_BEGIN;
    cmd.rect.x = popup.x + inset;
    cmd.rect.y = popup.y + inset;
    cmd.rect.w = popup.w - inset * 2;
    cmd.rect.h = inner_h;
    lk_render_list_push(out, cmd);
  }

  for (i = 0; i < count; i++) {
    lk_ix opt = nth_option(t, n, i);
    lk_rect r;
    lk_color hl;

    if (!opt) {
      continue;
    }

    r = lk_dropdown_option_rect(t, n, i, rects, styles, cfg);

    if (max_scroll > 0 &&
        (r.y + r.h <= popup.y + inset || r.y >= popup.y + inset + inner_h)) {
      continue;
    }

    opt_style = styles ? &styles[opt] : trig_style;

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

    {
      lk_u32 sid = lk_node_text_id(t, opt);
      if (sid != 0) {
        memset(&cmd, 0, sizeof(cmd));
        cmd.op = LK_ROP_DRAW_TEXT;
        {
          lk_i32 tp = option_text_pad(cfg);

          cmd.rect.x = r.x + tp;
          cmd.rect.y = r.y + tp;
          cmd.rect.w = r.w - tp * 2;
          cmd.rect.h = r.h - tp * 2;
        }
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

  if (max_scroll > 0) {
    lk_i32 track_h = inner_h;
    lk_i32 content_h = (lk_i32)count * row_h;
    lk_i32 thumb_h;
    lk_i32 thumb_y;

    memset(&cmd, 0, sizeof(cmd));
    cmd.op = LK_ROP_CLIP_END;
    lk_render_list_push(out, cmd);

    if (track_h > 0 && content_h > 0) {
      thumb_h = (inner_h * track_h) / content_h;

      if (thumb_h < 8) {
        thumb_h = 8;
      }

      thumb_y = popup.y + inset;
      thumb_y += (scroll * (track_h - thumb_h)) / max_scroll;

      memset(&cmd, 0, sizeof(cmd));
      cmd.op = LK_ROP_FILL_RECT;
      cmd.rect.x = popup.x + popup.w - inset - DROPDOWN_SCROLL_BAR_W;
      cmd.rect.y = popup.y + inset;
      cmd.rect.w = DROPDOWN_SCROLL_BAR_W;
      cmd.rect.h = track_h;
      if (trig_style) {
        cmd.color = trig_style->scrollbar_track;
      } else {
        cmd.color.r = 40;
        cmd.color.g = 40;
        cmd.color.b = 48;
        cmd.color.a = 255;
      }
      lk_render_list_push(out, cmd);

      memset(&cmd, 0, sizeof(cmd));
      cmd.op = LK_ROP_FILL_RECT;
      cmd.rect.x = popup.x + popup.w - inset - DROPDOWN_SCROLL_BAR_W;
      cmd.rect.y = thumb_y;
      cmd.rect.w = DROPDOWN_SCROLL_BAR_W;
      cmd.rect.h = thumb_h;
      if (trig_style) {
        cmd.color = trig_style->scrollbar_thumb;
      } else {
        cmd.color.r = 110;
        cmd.color.g = 110;
        cmd.color.b = 125;
        cmd.color.a = 255;
      }
      lk_render_list_push(out, cmd);
    }
  }
}

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

  {
    lk_i32 row_h = option_row_height(cfg);
    lk_i32 pad = styles ? styles[n].padding : 0;
    lk_i32 bw = styles ? styles[n].border_width : 0;
    lk_i32 inset = pad + bw;

    if (popup_scroll_max(count, row_h, inset) > 0 &&
        (y < popup.y + inset ||
         y >= popup.y + inset + popup_inner_h(count, row_h, inset))) {
      return n;
    }
  }

  for (i = 0; i < count; i++) {
    lk_ix opt = nth_option(t, n, i);
    lk_rect r = lk_dropdown_option_rect(t, n, i, rects, styles, cfg);

    if (opt && point_in(&r, x, y)) {
      return opt;
    }
  }

  return n;
}

lk_widget_def lk_dropdown_widget_def(void) {
  lk_widget_def def;
  memset(&def, 0, sizeof(def));
  def.measure = measure_dropdown;
  def.layout = 0;

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
