

#include <string.h>

#include "lk-memory.h"
#include "lk-text-align.h"
#include "lk-utf8.h"
#include <lk.h>

#include "lk-text-input.h"

static int is_controlled(const lk_tree *t, lk_ix n) {
  return lk_node_prop_i32(t, n, UIP_CONTROLLED, 0) ? 1 : 0;
}

static lk_str get_text(const lk_tree *t, lk_ix n, const lk_state *state,
                       lk_u32 *out_str_id) {
  lk_str s;
  lk_value v;

  if (state && !is_controlled(t, n)) {
    lk_node_id nid = t->nodes[n].id;
    v = lk_state_get(state, nid, LKS_TEXT_BUF);

    if (v.tag == UIV_STR) {
      if (out_str_id) {
        *out_str_id = v.as.str_id;
      }

      return lk_intern_str(t->intern, v.as.str_id);
    }
  }

  s = lk_node_text(t, n);

  if (out_str_id) {
    *out_str_id = lk_node_text_id(t, n);
  }

  return s;
}

static lk_i32 clamp_i32(lk_i32 val, lk_i32 lo, lk_i32 hi) {
  if (val < lo) {
    return lo;
  }

  if (val > hi) {
    return hi;
  }

  return val;
}

static lk_i32 get_cursor(const lk_state *state, lk_node_id nid,
                         lk_i32 text_len) {
  lk_value v;

  if (!state) {
    return 0;
  }

  v = lk_state_get(state, nid, LKS_CURSOR_POS);

  if (v.tag == UIV_I32) {
    return clamp_i32((lk_i32)v.as.i, 0, text_len);
  }

  return 0;
}

static lk_i32 get_sel_start(const lk_state *state, lk_node_id nid,
                            lk_i32 text_len) {
  lk_value v;

  if (!state) {
    return 0;
  }

  v = lk_state_get(state, nid, LKS_SELECTION_START);

  if (v.tag == UIV_I32) {
    return clamp_i32((lk_i32)v.as.i, 0, text_len);
  }

  return 0;
}

static lk_i32 get_sel_end(const lk_state *state, lk_node_id nid,
                          lk_i32 text_len) {
  lk_value v;

  if (!state) {
    return 0;
  }

  v = lk_state_get(state, nid, LKS_SELECTION_END);

  if (v.tag == UIV_I32) {
    return clamp_i32((lk_i32)v.as.i, 0, text_len);
  }
  return 0;
}

static void emit_value_changed(lk_ui *ui, lk_ix n, lk_u32 str_id) {
  lk_event ev;

  memset(&ev, 0, sizeof(ev));
  ev.type = LK_EVENT_VALUE_CHANGED;
  ev.target = n;
  ev.data.value_changed.str_id = str_id;
  lk_event_enqueue(ui, &ev);
}

static void commit_text(lk_ui *ui, const lk_tree *t, lk_ix n, const char *buf,
                        lk_i32 len) {
  lk_str s;
  lk_value v;

  s.ptr = buf;
  s.len = (lk_u32)len;
  v.tag = UIV_STR;
  v.as.str_id = lk_intern_id(t->intern, s);

  if (!is_controlled(t, n)) {
    lk_state_set(lk_ui_state(ui), t->nodes[n].id, LKS_TEXT_BUF, v);
  }

  emit_value_changed(ui, n, v.as.str_id);
}

static void ensure_text_buf(lk_ui *ui, const lk_tree *t, lk_ix n) {
  lk_node_id nid = t->nodes[n].id;
  lk_state *st = lk_ui_state(ui);
  lk_value v;
  lk_u32 sid;

  v = lk_state_get(st, nid, LKS_TEXT_BUF);

  if (v.tag != UIV_NONE) {
    return;
  }

  if (!is_controlled(t, n)) {
    sid = lk_node_text_id(t, n);
    v.tag = UIV_STR;
    v.as.str_id = sid;
    lk_state_set(st, nid, LKS_TEXT_BUF, v);
  }

  {
    lk_str text = lk_node_text(t, n);

    if (lk_state_get(st, nid, LKS_CURSOR_POS).tag == UIV_NONE) {
      lk_state_set(st, nid, LKS_CURSOR_POS, lk_v_i32((lk_i32)text.len));
    }

    if (lk_state_get(st, nid, LKS_SELECTION_START).tag == UIV_NONE) {
      lk_state_set(st, nid, LKS_SELECTION_START, lk_v_i32(0));
    }

    if (lk_state_get(st, nid, LKS_SELECTION_END).tag == UIV_NONE) {
      lk_state_set(st, nid, LKS_SELECTION_END, lk_v_i32(0));
    }
  }
}

static void measure_run(const lk_layout_cfg *cfg, lk_ix n, lk_str run,
                        lk_text_metrics *m) {
  m->w = 0;
  m->h = 0;
  m->baseline = 0;

  if (cfg->text) {
    lk_u16 font_id = cfg->styles ? (lk_u16)cfg->styles[n].font_id : 0;
    lk_u16 font_size = cfg->styles ? (lk_u16)cfg->styles[n].font_size : 0;
    cfg->text->measure(cfg->text->ud, run, font_id, font_size, m);
  }
}

static lk_i32 run_x_from_ix(const lk_layout_cfg *cfg, lk_ix n, lk_str run,
                            lk_u32 byte_ix) {
  lk_u16 font_id;
  lk_u16 font_size;

  if (!cfg->text) {
    return 0;
  }

  font_id = cfg->styles ? (lk_u16)cfg->styles[n].font_id : 0;
  font_size = cfg->styles ? (lk_u16)cfg->styles[n].font_size : 0;

  return cfg->text->x_from_index(cfg->text->ud, run, font_id, font_size,
                                 byte_ix);
}

static void measure_text_input(const lk_tree *t, lk_ix n, const lk_size *sizes,
                               const lk_layout_cfg *cfg, lk_i32 *out_w,
                               lk_i32 *out_h) {
  lk_str text;
  lk_text_metrics m;
  lk_i32 pad;
  lk_i32 bw;
  lk_i32 inset;
  lk_node_id nid;

  (void)sizes;

  pad = cfg->styles ? cfg->styles[n].padding
                    : lk_node_prop_i32(t, n, UIP_PADDING, 0);
  bw = cfg->styles ? cfg->styles[n].border_width : 0;
  inset = pad + bw;

  text = get_text(t, n, cfg->state, NULL);
  measure_run(cfg, n, text, &m);

  if (cfg->geom) {
    cfg->geom[n].text.run_w = m.w;
    cfg->geom[n].text.run_h = m.h;
  }

  if (m.w < 100) {
    m.w = 100;
  }

  {
    lk_text_metrics ref;

    measure_run(cfg, n, lk_str_c("Mg"), &ref);

    if (m.h < ref.h) {
      m.h = ref.h;
    }
  }

  *out_w = m.w + inset * 2;
  *out_h = m.h + inset * 2;

  if (cfg->geom) {
    lk_widget_geom *g = &cfg->geom[n];

    nid = t->nodes[n].id;
    {
      lk_i32 cursor_pos = get_cursor(cfg->state, nid, (lk_i32)text.len);
      lk_i32 sel_s = get_sel_start(cfg->state, nid, (lk_i32)text.len);
      lk_i32 sel_e = get_sel_end(cfg->state, nid, (lk_i32)text.len);
      lk_i32 lo = sel_s < sel_e ? sel_s : sel_e;
      lk_i32 hi = sel_s < sel_e ? sel_e : sel_s;

      g->text.cursor_x = run_x_from_ix(cfg, n, text, (lk_u32)cursor_pos);
      g->text.sel_x0 = run_x_from_ix(cfg, n, text, (lk_u32)lo);
      g->text.sel_x1 = run_x_from_ix(cfg, n, text, (lk_u32)hi);
    }
  }
}

void lk_text_input_store_geometry(const lk_tree *t, const lk_rect *rects,
                                  const lk_layout_cfg *cfg) {
  lk_ix n;

  if (!t || !rects || !cfg || !cfg->geom) {
    return;
  }

  for (n = 1; n < (lk_ix)t->node_count; n++) {
    lk_widget_geom *g;
    lk_i32 pad;
    lk_i32 bw;

    if (t->nodes[n].kind != UIK_TEXT_INPUT) {
      continue;
    }

    pad = cfg->styles ? cfg->styles[n].padding
                      : lk_node_prop_i32(t, n, UIP_PADDING, 0);
    bw = cfg->styles ? cfg->styles[n].border_width : 0;
    g = &cfg->geom[n];

    g->text.origin_x =
        rects[n].x + pad + bw +
        lk_text_align_offset(
            cfg->styles ? cfg->styles[n].text_align
                        : (lk_u8)lk_node_prop_i32(t, n, UIP_TEXT_ALIGN, 0),
            rects[n].w - (pad + bw) * 2, g->text.run_w);
    g->text.font_id = cfg->styles ? (lk_u16)cfg->styles[n].font_id : 0;
    g->text.font_size = cfg->styles ? (lk_u16)cfg->styles[n].font_size : 0;
    g->text.placed = 1;
  }
}

static void render_text_input(const lk_tree *t, lk_ix n, const lk_rect *rect,
                              const lk_style *style, const lk_state *state,
                              const lk_widget_geom *geom, lk_render_list *out) {
  lk_i32 pad = style->padding;
  lk_i32 bw = style->border_width;
  lk_i32 inset = pad + bw;
  lk_u32 sid = 0;
  lk_str text;
  lk_render_cmd cmd;
  lk_node_id nid = t->nodes[n].id;
  lk_i32 ox = 0;
  lk_i32 oy = 0;

  memset(&cmd, 0, sizeof(cmd));
  cmd.op = LK_ROP_FILL_RECT;
  cmd.rect = *rect;
  cmd.color = style->bg;
  lk_render_list_push(out, cmd);

  text = get_text(t, n, state, &sid);

  if (geom) {
    ox = lk_text_align_offset(style->text_align, rect->w - inset * 2,
                              geom->text.run_w);
    oy = lk_text_align_offset(style->text_valign, rect->h - inset * 2,
                              geom->text.run_h);
  }

  if (state && geom) {
    lk_i32 sel_s = get_sel_start(state, nid, (lk_i32)text.len);
    lk_i32 sel_e = get_sel_end(state, nid, (lk_i32)text.len);

    if (sel_s != sel_e && geom->text.sel_x1 > geom->text.sel_x0) {
      memset(&cmd, 0, sizeof(cmd));
      cmd.op = LK_ROP_FILL_RECT;
      cmd.rect.x = rect->x + inset + ox + geom->text.sel_x0;
      cmd.rect.y = rect->y + inset;
      cmd.rect.w = geom->text.sel_x1 - geom->text.sel_x0;
      cmd.rect.h = rect->h - inset * 2;
      cmd.color.r = 80;
      cmd.color.g = 120;
      cmd.color.b = 200;
      cmd.color.a = 128;
      lk_render_list_push(out, cmd);
    }
  }

  if (sid != 0) {
    memset(&cmd, 0, sizeof(cmd));
    cmd.op = LK_ROP_DRAW_TEXT;
    cmd.rect.x = rect->x + inset + ox;
    cmd.rect.y = rect->y + inset + oy;
    cmd.rect.w = rect->w - inset * 2 - ox;
    cmd.rect.h = rect->h - inset * 2 - oy;
    cmd.color = style->fg;
    cmd.str_id = sid;
    cmd.font_id = (lk_u16)style->font_id;
    cmd.font_size = (lk_u16)style->font_size;
    lk_render_list_push(out, cmd);
  }

  if (state && geom) {
    lk_value f_v = lk_state_get(state, nid, LKS_FOCUSED);

    if (f_v.tag == UIV_I32 && f_v.as.i != 0) {
      memset(&cmd, 0, sizeof(cmd));
      cmd.op = LK_ROP_CARET;
      cmd.rect.x = rect->x + inset + ox + geom->text.cursor_x;
      cmd.rect.y = rect->y + inset;
      cmd.rect.w = 1;
      cmd.rect.h = rect->h - inset * 2;
      cmd.color = style->fg;
      lk_render_list_push(out, cmd);
    }
  }
}

static lk_i32 delete_selection(lk_ui *ui, const lk_tree *t, lk_ix n,
                               const char *text, lk_i32 text_len, char *buf,
                               lk_i32 *out_len) {
  lk_state *st = lk_ui_state(ui);
  lk_node_id nid = t->nodes[n].id;
  lk_i32 sel_s = get_sel_start(st, nid, text_len);
  lk_i32 sel_e = get_sel_end(st, nid, text_len);
  lk_i32 lo, hi, new_len;

  if (sel_s == sel_e) {
    return -1;
  }

  lo = sel_s < sel_e ? sel_s : sel_e;
  hi = sel_s < sel_e ? sel_e : sel_s;

  new_len = text_len - (hi - lo);

  if (new_len < 0) {
    new_len = 0;
  }

  if (new_len > LK_TEXT_INPUT_MAX - 1) {
    new_len = LK_TEXT_INPUT_MAX - 1;
  }

  memcpy(buf, text, (size_t)lo);
  memcpy(buf + lo, text + hi, (size_t)(text_len - hi));
  buf[new_len] = '\0';
  *out_len = new_len;

  lk_state_set(st, nid, LKS_CURSOR_POS, lk_v_i32(lo));
  lk_state_set(st, nid, LKS_SELECTION_START, lk_v_i32(0));
  lk_state_set(st, nid, LKS_SELECTION_END, lk_v_i32(0));

  return lo;
}

static void paste_into(lk_ui *ui, const lk_tree *t, lk_ix n, lk_str text,
                       lk_i32 text_len, lk_i32 cursor, const char *clip,
                       lk_i32 ins_len) {
  lk_state *st = lk_ui_state(ui);
  lk_node_id nid = t->nodes[n].id;
  lk_i32 sel_cursor;
  char base[LK_TEXT_INPUT_MAX];
  char buf[LK_TEXT_INPUT_MAX];
  lk_i32 new_len;

  sel_cursor = delete_selection(ui, t, n, text.ptr, text_len, base, &text_len);

  if (sel_cursor >= 0) {
    text.ptr = base;
    text.len = (lk_u32)text_len;
    cursor = sel_cursor;
  }

  if (text_len + ins_len >= LK_TEXT_INPUT_MAX) {
    lk_i32 full_len = ins_len;
    ins_len = LK_TEXT_INPUT_MAX - 1 - text_len;

    if (ins_len < 0) {
      ins_len = 0;
    }

    while (ins_len > 0 &&
           !lk_utf8_is_boundary(clip, (lk_u32)full_len, (lk_u32)ins_len)) {
      ins_len--;
    }
  }

  if (ins_len > 0) {
    memcpy(buf, text.ptr, (size_t)cursor);
    memcpy(buf + cursor, clip, (size_t)ins_len);
    memcpy(buf + cursor + ins_len, text.ptr + cursor,
           (size_t)(text_len - cursor));
    new_len = text_len + ins_len;
    buf[new_len] = '\0';

    lk_state_set(st, nid, LKS_CURSOR_POS, lk_v_i32(cursor + ins_len));
    lk_state_set(st, nid, LKS_SELECTION_START, lk_v_i32(0));
    lk_state_set(st, nid, LKS_SELECTION_END, lk_v_i32(0));
    commit_text(ui, t, n, buf, new_len);
  } else if (sel_cursor >= 0) {

    commit_text(ui, t, n, base, text_len);
  }
}

static int event_text_input(lk_ui *ui, const lk_tree *t, lk_ix n,
                            lk_event *ev) {
  lk_state *st = lk_ui_state(ui);
  lk_node_id nid = t->nodes[n].id;
  lk_str text;
  lk_i32 text_len;
  lk_i32 cursor;

  if (ev->target != n) {
    return 0;
  }

  ensure_text_buf(ui, t, n);

  text = get_text(t, n, st, NULL);
  text_len = (lk_i32)text.len;
  cursor = get_cursor(st, nid, text_len);

  if (ev->type == LK_EVENT_PASTE) {
    if (ev->data.paste.len > 0 && ev->data.paste.ptr) {
      paste_into(ui, t, n, text, text_len, cursor, ev->data.paste.ptr,
                 (lk_i32)ev->data.paste.len);
    }

    return 1;
  }

  if (ev->type == LK_EVENT_TEXT) {

    lk_i32 ins_len = (lk_i32)ev->data.text.len;
    lk_i32 sel_cursor;
    char base[LK_TEXT_INPUT_MAX];
    char buf[LK_TEXT_INPUT_MAX];
    lk_i32 new_len;

    if (ins_len <= 0) {
      return 0;
    }

    sel_cursor =
        delete_selection(ui, t, n, text.ptr, text_len, base, &text_len);

    if (sel_cursor >= 0) {
      text.ptr = base;
      text.len = (lk_u32)text_len;
      cursor = sel_cursor;
    }

    if (text_len + ins_len >= LK_TEXT_INPUT_MAX) {
      lk_i32 full_len = ins_len;
      ins_len = LK_TEXT_INPUT_MAX - 1 - text_len;

      if (ins_len < 0) {
        ins_len = 0;
      }

      while (ins_len > 0 &&
             !lk_utf8_is_boundary(ev->data.text.buf, (lk_u32)full_len,
                                  (lk_u32)ins_len)) {
        ins_len--;
      }

      if (ins_len == 0) {

        if (sel_cursor >= 0) {
          commit_text(ui, t, n, base, text_len);
        }

        return 1;
      }
    }

    memcpy(buf, text.ptr, (size_t)cursor);
    memcpy(buf + cursor, ev->data.text.buf, (size_t)ins_len);
    memcpy(buf + cursor + ins_len, text.ptr + cursor,
           (size_t)(text_len - cursor));
    new_len = text_len + ins_len;
    buf[new_len] = '\0';

    lk_state_set(st, nid, LKS_CURSOR_POS, lk_v_i32(cursor + ins_len));
    lk_state_set(st, nid, LKS_SELECTION_START, lk_v_i32(0));
    lk_state_set(st, nid, LKS_SELECTION_END, lk_v_i32(0));
    commit_text(ui, t, n, buf, new_len);
    return 1;
  }

  if (ev->type == LK_EVENT_POINTER_DOWN) {

    const lk_widget_geom *g;
    lk_i32 origin_x;
    lk_u16 font_id;
    lk_u16 font_size;
    lk_u32 ix;

    if (!ui->text || !ui->geom || (lk_u32)n >= ui->geom_cap) {
      return 0;
    }

    g = &ui->geom[n];

    if (!g->text.placed) {
      return 0;
    }

    origin_x = g->text.origin_x;
    font_id = g->text.font_id;
    font_size = g->text.font_size;

    ix = ui->text->index_from_x(ui->text->ud, text, font_id, font_size,
                                ev->data.pointer.x - origin_x);

    if (ix > text.len) {
      ix = text.len;
    }

    lk_focus_set(ui, t, nid);

    lk_state_set(st, nid, LKS_CURSOR_POS, lk_v_i32((lk_i32)ix));
    lk_state_set(st, nid, LKS_SELECTION_START, lk_v_i32(0));
    lk_state_set(st, nid, LKS_SELECTION_END, lk_v_i32(0));

    return 1;
  }

  if (ev->type == LK_EVENT_KEY_DOWN) {
    lk_u16 kc = ev->data.key.keycode;
    int shift = (ev->mods & LK_MOD_SHIFT) ? 1 : 0;
    int ctrl = (ev->mods & LK_MOD_CTRL) ? 1 : 0;

    switch (kc) {
    case LKK_BACKSPACE: {
      char cut[LK_TEXT_INPUT_MAX];
      lk_i32 cut_len;
      lk_i32 sel_cursor =
          delete_selection(ui, t, n, text.ptr, text_len, cut, &cut_len);

      if (sel_cursor >= 0) {
        commit_text(ui, t, n, cut, cut_len);
        return 1;
      }

      if (cursor > 0) {

        char buf[LK_TEXT_INPUT_MAX];
        lk_i32 del_from =
            (lk_i32)lk_utf8_prev(text.ptr, text.len, (lk_u32)cursor);
        lk_i32 new_len = text_len - (cursor - del_from);
        memcpy(buf, text.ptr, (size_t)del_from);
        memcpy(buf + del_from, text.ptr + cursor, (size_t)(text_len - cursor));
        buf[new_len] = '\0';
        lk_state_set(st, nid, LKS_CURSOR_POS, lk_v_i32(del_from));
        commit_text(ui, t, n, buf, new_len);
      }

      return 1;
    }

    case LKK_DELETE: {
      char cut[LK_TEXT_INPUT_MAX];
      lk_i32 cut_len;
      lk_i32 sel_cursor =
          delete_selection(ui, t, n, text.ptr, text_len, cut, &cut_len);

      if (sel_cursor >= 0) {
        commit_text(ui, t, n, cut, cut_len);
        return 1;
      }

      if (cursor < text_len) {

        char buf[LK_TEXT_INPUT_MAX];
        lk_i32 del_to =
            (lk_i32)lk_utf8_next(text.ptr, text.len, (lk_u32)cursor);
        lk_i32 new_len = text_len - (del_to - cursor);
        memcpy(buf, text.ptr, (size_t)cursor);
        memcpy(buf + cursor, text.ptr + del_to, (size_t)(text_len - del_to));
        buf[new_len] = '\0';

        commit_text(ui, t, n, buf, new_len);
      }

      return 1;
    }

    case LKK_LEFT:
      if (cursor > 0) {
        lk_i32 new_pos =
            (lk_i32)lk_utf8_prev(text.ptr, text.len, (lk_u32)cursor);
        lk_state_set(st, nid, LKS_CURSOR_POS, lk_v_i32(new_pos));

        if (shift) {
          lk_i32 ss = get_sel_start(st, nid, text_len);
          lk_i32 se = get_sel_end(st, nid, text_len);

          if (ss == 0 && se == 0) {

            lk_state_set(st, nid, LKS_SELECTION_START, lk_v_i32(cursor));
          }

          lk_state_set(st, nid, LKS_SELECTION_END, lk_v_i32(new_pos));
        } else {
          lk_state_set(st, nid, LKS_SELECTION_START, lk_v_i32(0));
          lk_state_set(st, nid, LKS_SELECTION_END, lk_v_i32(0));
        }
      }

      return 1;

    case LKK_RIGHT:
      if (cursor < text_len) {
        lk_i32 new_pos =
            (lk_i32)lk_utf8_next(text.ptr, text.len, (lk_u32)cursor);
        lk_state_set(st, nid, LKS_CURSOR_POS, lk_v_i32(new_pos));

        if (shift) {
          lk_i32 ss = get_sel_start(st, nid, text_len);
          lk_i32 se = get_sel_end(st, nid, text_len);

          if (ss == 0 && se == 0) {
            lk_state_set(st, nid, LKS_SELECTION_START, lk_v_i32(cursor));
          }

          lk_state_set(st, nid, LKS_SELECTION_END, lk_v_i32(new_pos));
        } else {
          lk_state_set(st, nid, LKS_SELECTION_START, lk_v_i32(0));
          lk_state_set(st, nid, LKS_SELECTION_END, lk_v_i32(0));
        }
      }

      return 1;

    case LKK_HOME:
      lk_state_set(st, nid, LKS_CURSOR_POS, lk_v_i32(0));

      if (shift) {
        lk_i32 ss = get_sel_start(st, nid, text_len);
        lk_i32 se = get_sel_end(st, nid, text_len);

        if (ss == 0 && se == 0) {
          lk_state_set(st, nid, LKS_SELECTION_START, lk_v_i32(cursor));
        }

        lk_state_set(st, nid, LKS_SELECTION_END, lk_v_i32(0));
      } else {
        lk_state_set(st, nid, LKS_SELECTION_START, lk_v_i32(0));
        lk_state_set(st, nid, LKS_SELECTION_END, lk_v_i32(0));
      }

      return 1;

    case LKK_END:
      lk_state_set(st, nid, LKS_CURSOR_POS, lk_v_i32(text_len));

      if (shift) {
        lk_i32 ss = get_sel_start(st, nid, text_len);
        lk_i32 se = get_sel_end(st, nid, text_len);

        if (ss == 0 && se == 0) {
          lk_state_set(st, nid, LKS_SELECTION_START, lk_v_i32(cursor));
        }

        lk_state_set(st, nid, LKS_SELECTION_END, lk_v_i32(text_len));
      } else {
        lk_state_set(st, nid, LKS_SELECTION_START, lk_v_i32(0));
        lk_state_set(st, nid, LKS_SELECTION_END, lk_v_i32(0));
      }

      return 1;

    case LKK_A:
      if (ctrl) {

        lk_state_set(st, nid, LKS_SELECTION_START, lk_v_i32(0));
        lk_state_set(st, nid, LKS_SELECTION_END, lk_v_i32(text_len));
        lk_state_set(st, nid, LKS_CURSOR_POS, lk_v_i32(text_len));
        return 1;
      }

      return 0;

    case LKK_C:
      if (ctrl && ui->clipboard_set) {
        lk_i32 sel_s = get_sel_start(st, nid, text_len);
        lk_i32 sel_e = get_sel_end(st, nid, text_len);

        if (sel_s != sel_e) {
          lk_i32 lo = sel_s < sel_e ? sel_s : sel_e;
          lk_i32 hi = sel_s < sel_e ? sel_e : sel_s;
          char buf[LK_TEXT_INPUT_MAX];
          lk_i32 copy_len = hi - lo;

          if (copy_len > LK_TEXT_INPUT_MAX - 1) {
            copy_len = LK_TEXT_INPUT_MAX - 1;
          }

          memcpy(buf, text.ptr + lo, (size_t)copy_len);
          buf[copy_len] = '\0';
          ui->clipboard_set(ui->clipboard_ud, buf);
        }

        return 1;
      }

      return 0;

    case LKK_X:
      if (ctrl && ui->clipboard_set) {
        lk_i32 sel_s = get_sel_start(st, nid, text_len);
        lk_i32 sel_e = get_sel_end(st, nid, text_len);

        if (sel_s != sel_e) {
          lk_i32 lo = sel_s < sel_e ? sel_s : sel_e;
          lk_i32 hi = sel_s < sel_e ? sel_e : sel_s;
          char buf[LK_TEXT_INPUT_MAX];
          lk_i32 copy_len = hi - lo;

          if (copy_len > LK_TEXT_INPUT_MAX - 1) {
            copy_len = LK_TEXT_INPUT_MAX - 1;
          }

          memcpy(buf, text.ptr + lo, (size_t)copy_len);
          buf[copy_len] = '\0';
          ui->clipboard_set(ui->clipboard_ud, buf);

          {
            char cut[LK_TEXT_INPUT_MAX];
            lk_i32 cut_len;

            if (delete_selection(ui, t, n, text.ptr, text_len, cut, &cut_len) >=
                0) {
              commit_text(ui, t, n, cut, cut_len);
            }
          }
        }

        return 1;
      }

      return 0;

    case LKK_V:
      if (ctrl && ui->clipboard_get) {
        const char *clip = ui->clipboard_get(ui->clipboard_ud);

        if (clip && clip[0] != '\0') {
          paste_into(ui, t, n, text, text_len, cursor, clip,
                     (lk_i32)strlen(clip));
        }

        return 1;
      }

      return 0;

    default: break;
    }

    return 0;
  }

  return 0;
}

lk_widget_def lk_text_input_widget_def(void) {
  lk_widget_def def;
  memset(&def, 0, sizeof(def));
  def.measure = measure_text_input;
  def.layout = 0;
  def.render = render_text_input;
  def.event = event_text_input;
  def.clips = 0;
  return def;
}
