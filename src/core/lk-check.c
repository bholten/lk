

#include <string.h>

#include "lk-check.h"
#include "lk-memory.h"
#include <lk.h>

#define BOX_MIN 14
#define CHECK_INSET 3

static void measure_text(const lk_layout_cfg *cfg, lk_ix n, lk_str run,
                         lk_i32 *out_w, lk_i32 *out_h) {
  lk_text_metrics m;

  m.w = 0;
  m.h = 0;
  m.baseline = 0;

  if (cfg->text) {
    lk_u16 font_id = cfg->styles ? (lk_u16)cfg->styles[n].font_id : 0;
    lk_u16 font_size = cfg->styles ? (lk_u16)cfg->styles[n].font_size : 0;
    cfg->text->measure(cfg->text->ud, run, font_id, font_size, &m);
  }

  *out_w = m.w;
  *out_h = m.h;
}

int lk_check_effective(const lk_tree *t, lk_ix n, const lk_state *state) {
  if (state) {
    lk_value v = lk_state_get(state, t->nodes[n].id, LKS_CHECKED);

    if (v.tag == UIV_I32) {
      return v.as.i ? 1 : 0;
    }
  }

  return lk_node_prop_bool(t, n, UIP_CHECKED);
}

static void measure_check(const lk_tree *t, lk_ix n, const lk_size *sizes,
                          const lk_layout_cfg *cfg, lk_i32 *out_w,
                          lk_i32 *out_h) {
  lk_str text = lk_node_text(t, n);
  lk_i32 pad = cfg->styles ? cfg->styles[n].padding
                           : lk_node_prop_i32(t, n, UIP_PADDING, 0);
  lk_i32 bw = cfg->styles ? cfg->styles[n].border_width : 0;
  lk_i32 gap =
      cfg->styles ? cfg->styles[n].gap : lk_node_prop_i32(t, n, UIP_GAP, 0);
  lk_i32 inset = pad + bw;
  lk_i32 tw = 0;
  lk_i32 th = 0;
  lk_i32 box;

  (void)sizes;
  measure_text(cfg, n, text, &tw, &th);
  box = th > 0 ? th : BOX_MIN;
  *out_w = box + (text.len ? gap + tw : 0) + inset * 2;
  *out_h = (th > box ? th : box) + inset * 2;
}

static void push_fill(lk_render_list *out, lk_i32 x, lk_i32 y, lk_i32 w,
                      lk_i32 h, lk_color c) {
  lk_render_cmd cmd;

  memset(&cmd, 0, sizeof(cmd));
  cmd.op = LK_ROP_FILL_RECT;
  cmd.rect.x = x;
  cmd.rect.y = y;
  cmd.rect.w = w;
  cmd.rect.h = h;
  cmd.color = c;
  lk_render_list_push(out, cmd);
}

static void render_check(const lk_tree *t, lk_ix n, const lk_rect *rect,
                         const lk_style *style, const lk_state *state,
                         const lk_widget_geom *geom, lk_render_list *out,
                         int radio) {
  lk_i32 inset = style->padding + style->border_width;
  lk_i32 box = rect->h - inset * 2;
  lk_i32 bx = rect->x + inset;
  lk_i32 by;
  lk_u32 sid = lk_node_text_id(t, n);
  int checked = lk_check_effective(t, n, state);

  (void)geom;

  if (box < 4) {
    box = 4;
  }

  by = rect->y + (rect->h - box) / 2;

  if (style->bg.a > 0) {
    push_fill(out, rect->x, rect->y, rect->w, rect->h, style->bg);
  }

  {
    lk_render_cmd cmd;

    memset(&cmd, 0, sizeof(cmd));
    cmd.op = LK_ROP_STROKE_RECT;
    cmd.rect.x = bx;
    cmd.rect.y = by;
    cmd.rect.w = box;
    cmd.rect.h = box;
    cmd.color = style->border_color;
    cmd.stroke = 1;
    lk_render_list_push(out, cmd);
  }

  if (checked) {
    if (radio) {
      lk_i32 d = box / 2;
      lk_i32 off = (box - d) / 2;
      push_fill(out, bx + off, by + off, d, d, style->accent);
    } else {
      lk_i32 ci = box > CHECK_INSET * 2 + 2 ? CHECK_INSET : 1;
      push_fill(out, bx + ci, by + ci, box - ci * 2, box - ci * 2,
                style->accent);
    }
  }

  if (sid != 0) {
    lk_render_cmd cmd;

    memset(&cmd, 0, sizeof(cmd));
    cmd.op = LK_ROP_DRAW_TEXT;
    cmd.rect.x = bx + box + style->gap;
    cmd.rect.y = rect->y + inset;
    cmd.rect.w = rect->w - (box + style->gap) - inset * 2;
    cmd.rect.h = rect->h - inset * 2;
    cmd.color = style->fg;
    cmd.str_id = sid;
    cmd.font_id = (lk_u16)style->font_id;
    cmd.font_size = (lk_u16)style->font_size;
    lk_render_list_push(out, cmd);
  }
}

static void render_checkbox(const lk_tree *t, lk_ix n, const lk_rect *rect,
                            const lk_style *style, const lk_state *state,
                            const lk_widget_geom *geom, lk_render_list *out) {
  render_check(t, n, rect, style, state, geom, out, 0);
}

static void render_radio(const lk_tree *t, lk_ix n, const lk_rect *rect,
                         const lk_style *style, const lk_state *state,
                         const lk_widget_geom *geom, lk_render_list *out) {
  render_check(t, n, rect, style, state, geom, out, 1);
}

static void emit_value_changed(lk_ui *ui, lk_ix n, int checked) {
  lk_event vev;

  memset(&vev, 0, sizeof(vev));
  vev.type = LK_EVENT_VALUE_CHANGED;
  vev.target = n;
  vev.data.value_changed.str_id =
      lk_intern_cid(ui->intern, checked ? "1" : "0");
  lk_event_enqueue(ui, &vev);
}

static int check_activate(lk_ui *ui, const lk_tree *t, lk_ix n, int radio) {
  lk_state *st = lk_ui_state(ui);
  lk_node_id nid = t->nodes[n].id;
  int controlled = lk_node_prop_i32(t, n, UIP_CONTROLLED, 0) ? 1 : 0;
  int cur = lk_check_effective(t, n, st);
  int next;

  if (radio) {
    if (cur) {
      return 1;
    }

    next = 1;
  } else {
    next = cur ? 0 : 1;
  }

  if (st && !controlled) {
    if (radio) {
      lk_ix parent = t->nodes[n].parent;

      if (parent) {
        lk_ix sib = t->nodes[parent].first_child;

        while (sib) {
          if (sib != n && (lk_kind)t->nodes[sib].kind == UIK_RADIO) {
            lk_state_set(st, t->nodes[sib].id, LKS_CHECKED, lk_v_i32(0));
          }

          sib = t->nodes[sib].next_sibling;
        }
      }
    }

    lk_state_set(st, nid, LKS_CHECKED, lk_v_i32(next));
  }

  emit_value_changed(ui, n, next);

  return 1;
}

static int event_check(lk_ui *ui, const lk_tree *t, lk_ix n, lk_event *ev,
                       int radio) {
  if (lk_node_prop_bool(t, n, UIP_DISABLED)) {
    return 0;
  }

  if (ev->type == LK_EVENT_POINTER_DOWN) {
    lk_u8 b = ev->data.pointer.button;

    if (b != LK_POINTER_BUTTON_ANY && b != LK_POINTER_BUTTON_PRIMARY) {
      return 0;
    }

    lk_focus_set(ui, t, t->nodes[n].id);

    return check_activate(ui, t, n, radio);
  }

  if (ev->type == LK_EVENT_KEY_DOWN && ev->data.key.keycode == LKK_SPACE &&
      ev->mods == 0) {
    return check_activate(ui, t, n, radio);
  }

  return 0;
}

static int event_checkbox(lk_ui *ui, const lk_tree *t, lk_ix n, lk_event *ev) {
  return event_check(ui, t, n, ev, 0);
}

static int event_radio(lk_ui *ui, const lk_tree *t, lk_ix n, lk_event *ev) {
  return event_check(ui, t, n, ev, 1);
}

lk_widget_def lk_checkbox_widget_def(void) {
  lk_widget_def def;
  memset(&def, 0, sizeof(def));
  def.measure = measure_check;
  def.layout = 0;
  def.render = render_checkbox;
  def.event = event_checkbox;
  def.clips = 0;
  return def;
}

lk_widget_def lk_radio_widget_def(void) {
  lk_widget_def def;
  memset(&def, 0, sizeof(def));
  def.measure = measure_check;
  def.layout = 0;
  def.render = render_radio;
  def.event = event_radio;
  def.clips = 0;
  return def;
}
