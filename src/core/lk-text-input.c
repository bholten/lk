/*
 * lk-text-input.c — Single-line text input widget.
 *
 * Text buffer is stored as an interned string in lk_state (LKS_TEXT_BUF).
 * Cursor position (char index) in LKS_CURSOR_POS.
 * Selection range in LKS_SELECTION_START / LKS_SELECTION_END.
 * Cursor pixel x-offset computed during measure, stored in LKS_CURSOR_X.
 *
 * The host feeds the edited text back as UIP_TEXT each frame for rendering.
 */

#include <string.h>

#include "lk-memory.h"
#include <lk.h>

#include "lk-text-input.h"

/* ---- Helpers ---- */

/* Read the current text buffer: from LKS_TEXT_BUF in state if available,
 * else from UIP_TEXT tree prop.  Returns the string and writes str_id.
 */
static lk_str get_text(const lk_tree *t, lk_ix n, const lk_state *state,
                       lk_u32 *out_str_id) {
  lk_str s;
  lk_value v;

  if (state) {
    lk_node_id nid = t->nodes[n].id;
    v = lk_state_get(state, nid, LKS_TEXT_BUF);

    if (v.tag == UIV_STR) {
      if (out_str_id) {
        *out_str_id = v.as.str_id;
      }

      return lk_intern_str(t->intern, v.as.str_id);
    }
  }

  /* Fall back to tree prop */
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

/* Read cursor pos from state, clamped to [0, text_len]. */
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

/* Emit a synthetic VALUE_CHANGED event carrying the current buffer contents.
 * Called at every buffer-mutation site so user handlers/translators can
 * react without polling state. */
static void emit_value_changed(lk_ui *ui, const lk_tree *t, lk_ix n) {
  lk_state *st = lk_ui_state(ui);
  lk_node_id nid = t->nodes[n].id;
  lk_value v = lk_state_get(st, nid, LKS_TEXT_BUF);
  lk_event ev;

  if (v.tag != UIV_STR) {
    return;
  }

  memset(&ev, 0, sizeof(ev));
  ev.type = LK_EVENT_VALUE_CHANGED;
  ev.target = n;
  ev.data.value_changed.str_id = v.as.str_id;
  lk_event_route(ui, &ev);
}

/* Ensure LKS_TEXT_BUF is initialized from UIP_TEXT on first interaction. */
static void ensure_text_buf(lk_ui *ui, const lk_tree *t, lk_ix n) {
  lk_node_id nid = t->nodes[n].id;
  lk_state *st = lk_ui_state(ui);
  lk_value v;
  lk_u32 sid;

  v = lk_state_get(st, nid, LKS_TEXT_BUF);

  if (v.tag != UIV_NONE) {
    return; /* already initialized */
  }

  sid = lk_node_text_id(t, n);
  v.tag = UIV_STR;
  v.as.str_id = sid;
  lk_state_set(st, nid, LKS_TEXT_BUF, v);

  /* Initialize cursor at end of text (only if not already set) */
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

/* ---- Measure ---- */

static void measure_text_input(const lk_tree *t, lk_ix n, const lk_size *sizes,
                               const lk_layout_cfg *cfg, lk_i32 *out_w,
                               lk_i32 *out_h) {
  lk_str text;
  lk_i32 tw = 0;
  lk_i32 th = 0;
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
  cfg->measure_text(cfg->measure_ud, text, &tw, &th);

  /* Minimum width when text is empty */
  if (tw < 100) {
    tw = 100;
  }

  *out_w = tw + inset * 2;
  *out_h = th + inset * 2;

  /* Compute cursor pixel offset and store in state */
  if (cfg->state) {
    nid = t->nodes[n].id;
    {
      lk_i32 cursor_pos = get_cursor(cfg->state, nid, (lk_i32)text.len);
      lk_i32 cx = 0;
      lk_i32 cy = 0;
      lk_str sub;
      sub.ptr = text.ptr;
      sub.len = (lk_u32)cursor_pos;
      cfg->measure_text(cfg->measure_ud, sub, &cx, &cy);
      lk_state_set(cfg->state, nid, LKS_CURSOR_X, lk_v_i32(cx));
    }
  }
}

/* ---- Render ---- */

static void render_text_input(const lk_tree *t, lk_ix n, const lk_rect *rect,
                              const lk_style *style, const lk_state *state,
                              lk_render_list *out) {
  lk_i32 pad = style->padding;
  lk_i32 bw = style->border_width;
  lk_i32 inset = pad + bw;
  lk_u32 sid = 0;
  lk_str text;
  lk_render_cmd cmd;
  lk_node_id nid = t->nodes[n].id;

  /* Background fill */
  memset(&cmd, 0, sizeof(cmd));
  cmd.op = LK_ROP_FILL_RECT;
  cmd.rect = *rect;
  cmd.color = style->bg;
  lk_render_list_push(out, cmd);

  text = get_text(t, n, state, &sid);

  /* Selection highlight */
  if (state) {
    lk_i32 sel_s = get_sel_start(state, nid, (lk_i32)text.len);
    lk_i32 sel_e = get_sel_end(state, nid, (lk_i32)text.len);

    if (sel_s != sel_e) {
      lk_i32 lo = sel_s < sel_e ? sel_s : sel_e;
      lk_i32 hi = sel_s < sel_e ? sel_e : sel_s;
      lk_i32 char_w = 8;

      if (text.len > 0) {
        lk_value cx_v = lk_state_get(state, nid, LKS_CURSOR_X);
        lk_i32 cursor_pos = get_cursor(state, nid, (lk_i32)text.len);

        if (cx_v.tag == UIV_I32 && cursor_pos > 0) {
          char_w = (lk_i32)cx_v.as.i / cursor_pos;

          if (char_w < 1) {
            char_w = 1;
          }
        }
      }

      memset(&cmd, 0, sizeof(cmd));
      cmd.op = LK_ROP_FILL_RECT;
      cmd.rect.x = rect->x + inset + lo * char_w;
      cmd.rect.y = rect->y + inset;
      cmd.rect.w = (hi - lo) * char_w;
      cmd.rect.h = rect->h - inset * 2;
      cmd.color.r = 80;
      cmd.color.g = 120;
      cmd.color.b = 200;
      cmd.color.a = 128;
      lk_render_list_push(out, cmd);
    }
  }

  /* Text */
  if (sid != 0) {
    memset(&cmd, 0, sizeof(cmd));
    cmd.op = LK_ROP_DRAW_TEXT;
    cmd.rect.x = rect->x + inset;
    cmd.rect.y = rect->y + inset;
    cmd.rect.w = rect->w - inset * 2;
    cmd.rect.h = rect->h - inset * 2;
    cmd.color = style->fg;
    cmd.str_id = sid;
    lk_render_list_push(out, cmd);
  }

  /* Cursor bar — only when this node holds keyboard focus.  The
   * LKS_FOCUSED flag is kept in sync by the lk_focus_* functions. */
  if (state) {
    lk_value f_v = lk_state_get(state, nid, LKS_FOCUSED);
    lk_value cx_v = lk_state_get(state, nid, LKS_CURSOR_X);

    if (f_v.tag == UIV_I32 && f_v.as.i != 0 && cx_v.tag == UIV_I32) {
      memset(&cmd, 0, sizeof(cmd));
      cmd.op = LK_ROP_FILL_RECT;
      cmd.rect.x = rect->x + inset + (lk_i32)cx_v.as.i;
      cmd.rect.y = rect->y + inset;
      cmd.rect.w = 1;
      cmd.rect.h = rect->h - inset * 2;
      cmd.color = style->fg;
      lk_render_list_push(out, cmd);
    }
  }
}

/* ---- Event handling ---- */

/* Delete the selection range and return the new cursor position.
 * Writes the edited text back to state.  Returns -1 if no selection.
 */
static lk_i32 delete_selection(lk_ui *ui, const lk_tree *t, lk_ix n,
                               const char *text, lk_i32 text_len) {
  lk_state *st = lk_ui_state(ui);
  lk_node_id nid = t->nodes[n].id;
  lk_i32 sel_s = get_sel_start(st, nid, text_len);
  lk_i32 sel_e = get_sel_end(st, nid, text_len);
  lk_i32 lo, hi, new_len;
  char buf[LK_TEXT_INPUT_MAX];
  lk_value v;

  if (sel_s == sel_e) {
    return -1;
  }

  lo = sel_s < sel_e ? sel_s : sel_e;
  hi = sel_s < sel_e ? sel_e : sel_s;

  /* Build new string: text[0..lo] + text[hi..text_len] */
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

  /* Intern and store */
  {
    lk_str s;
    s.ptr = buf;
    s.len = (lk_u32)new_len;
    v.tag = UIV_STR;
    v.as.str_id = lk_intern_id(t->intern, s);
    lk_state_set(st, nid, LKS_TEXT_BUF, v);
  }

  /* Clear selection, set cursor at lo */
  lk_state_set(st, nid, LKS_CURSOR_POS, lk_v_i32(lo));
  lk_state_set(st, nid, LKS_SELECTION_START, lk_v_i32(0));
  lk_state_set(st, nid, LKS_SELECTION_END, lk_v_i32(0));

  return lo;
}

static int event_text_input(lk_ui *ui, const lk_tree *t, lk_ix n,
                            lk_event *ev) {
  lk_state *st = lk_ui_state(ui);
  lk_node_id nid = t->nodes[n].id;
  lk_str text;
  lk_i32 text_len;
  lk_i32 cursor;

  /* Only handle events targeted at this node */
  if (ev->target != n) {
    return 0;
  }

  ensure_text_buf(ui, t, n);

  text = get_text(t, n, st, NULL);
  text_len = (lk_i32)text.len;
  cursor = get_cursor(st, nid, text_len);

  if (ev->type == LK_EVENT_TEXT) {
    /* Insert text at cursor (replacing selection if any) */
    lk_i32 ins_len = (lk_i32)ev->data.text.len;
    lk_i32 sel_cursor;
    char buf[LK_TEXT_INPUT_MAX];
    lk_i32 new_len;
    lk_value v;
    lk_str s;

    if (ins_len <= 0) {
      return 0;
    }

    /* Delete selection first */
    sel_cursor = delete_selection(ui, t, n, text.ptr, text_len);

    if (sel_cursor >= 0) {
      /* Re-read after deletion */
      text = get_text(t, n, st, NULL);
      text_len = (lk_i32)text.len;
      cursor = sel_cursor;
    }

    /* Check capacity */
    if (text_len + ins_len >= LK_TEXT_INPUT_MAX) {
      return 1; /* consumed but truncated */
    }

    /* Build: text[0..cursor] + insert + text[cursor..] */
    memcpy(buf, text.ptr, (size_t)cursor);
    memcpy(buf + cursor, ev->data.text.buf, (size_t)ins_len);
    memcpy(buf + cursor + ins_len, text.ptr + cursor,
           (size_t)(text_len - cursor));
    new_len = text_len + ins_len;
    buf[new_len] = '\0';

    s.ptr = buf;
    s.len = (lk_u32)new_len;
    v.tag = UIV_STR;
    v.as.str_id = lk_intern_id(t->intern, s);
    lk_state_set(st, nid, LKS_TEXT_BUF, v);
    lk_state_set(st, nid, LKS_CURSOR_POS, lk_v_i32(cursor + ins_len));
    lk_state_set(st, nid, LKS_SELECTION_START, lk_v_i32(0));
    lk_state_set(st, nid, LKS_SELECTION_END, lk_v_i32(0));

    emit_value_changed(ui, t, n);
    return 1;
  }

  if (ev->type == LK_EVENT_KEY_DOWN) {
    lk_u16 kc = ev->data.key.keycode;
    int shift = (ev->mods & LK_MOD_SHIFT) ? 1 : 0;
    int ctrl = (ev->mods & LK_MOD_CTRL) ? 1 : 0;

    switch (kc) {
    case LKK_BACKSPACE: {
      lk_i32 sel_cursor = delete_selection(ui, t, n, text.ptr, text_len);

      if (sel_cursor >= 0) {
        emit_value_changed(ui, t, n);
        return 1;
      }

      if (cursor > 0) {
        char buf[LK_TEXT_INPUT_MAX];
        lk_i32 new_len = text_len - 1;
        lk_value v;
        lk_str s;
        memcpy(buf, text.ptr, (size_t)(cursor - 1));
        memcpy(buf + cursor - 1, text.ptr + cursor,
               (size_t)(text_len - cursor));
        buf[new_len] = '\0';
        s.ptr = buf;
        s.len = (lk_u32)new_len;
        v.tag = UIV_STR;
        v.as.str_id = lk_intern_id(t->intern, s);
        lk_state_set(st, nid, LKS_TEXT_BUF, v);
        lk_state_set(st, nid, LKS_CURSOR_POS, lk_v_i32(cursor - 1));
        emit_value_changed(ui, t, n);
      }

      return 1;
    }

    case LKK_DELETE: {
      lk_i32 sel_cursor = delete_selection(ui, t, n, text.ptr, text_len);
      if (sel_cursor >= 0) {
        emit_value_changed(ui, t, n);
        return 1;
      }
      if (cursor < text_len) {
        char buf[LK_TEXT_INPUT_MAX];
        lk_i32 new_len = text_len - 1;
        lk_value v;
        lk_str s;
        memcpy(buf, text.ptr, (size_t)cursor);
        memcpy(buf + cursor, text.ptr + cursor + 1,
               (size_t)(text_len - cursor - 1));
        buf[new_len] = '\0';
        s.ptr = buf;
        s.len = (lk_u32)new_len;
        v.tag = UIV_STR;
        v.as.str_id = lk_intern_id(t->intern, s);
        lk_state_set(st, nid, LKS_TEXT_BUF, v);
        /* cursor stays at same position */
        emit_value_changed(ui, t, n);
      }
      return 1;
    }

    case LKK_LEFT:
      if (cursor > 0) {
        lk_i32 new_pos = cursor - 1;
        lk_state_set(st, nid, LKS_CURSOR_POS, lk_v_i32(new_pos));

        if (shift) {
          lk_i32 ss = get_sel_start(st, nid, text_len);
          lk_i32 se = get_sel_end(st, nid, text_len);

          if (ss == 0 && se == 0) {
            /* Start new selection from current cursor */
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
        lk_i32 new_pos = cursor + 1;
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
        /* Select all */
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
          delete_selection(ui, t, n, text.ptr, text_len);
          emit_value_changed(ui, t, n);
        }

        return 1;
      }

      return 0;

    case LKK_V:
      if (ctrl && ui->clipboard_get) {
        const char *clip = ui->clipboard_get(ui->clipboard_ud);

        if (clip && clip[0] != '\0') {
          lk_i32 ins_len = (lk_i32)strlen(clip);
          lk_i32 sel_cursor;
          char buf[LK_TEXT_INPUT_MAX];
          lk_i32 new_len;
          lk_value v;
          lk_str s;

          /* Delete selection first */
          sel_cursor = delete_selection(ui, t, n, text.ptr, text_len);

          if (sel_cursor >= 0) {
            text = get_text(t, n, st, NULL);
            text_len = (lk_i32)text.len;
            cursor = sel_cursor;
          }

          if (text_len + ins_len >= LK_TEXT_INPUT_MAX) {
            ins_len = LK_TEXT_INPUT_MAX - 1 - text_len;
          }

          if (ins_len > 0) {
            memcpy(buf, text.ptr, (size_t)cursor);
            memcpy(buf + cursor, clip, (size_t)ins_len);
            memcpy(buf + cursor + ins_len, text.ptr + cursor,
                   (size_t)(text_len - cursor));
            new_len = text_len + ins_len;
            buf[new_len] = '\0';

            s.ptr = buf;
            s.len = (lk_u32)new_len;
            v.tag = UIV_STR;
            v.as.str_id = lk_intern_id(t->intern, s);
            lk_state_set(st, nid, LKS_TEXT_BUF, v);
            lk_state_set(st, nid, LKS_CURSOR_POS,
                         lk_v_i32(cursor + ins_len));
            lk_state_set(st, nid, LKS_SELECTION_START, lk_v_i32(0));
            lk_state_set(st, nid, LKS_SELECTION_END, lk_v_i32(0));
            emit_value_changed(ui, t, n);
          }
        }

        return 1;
      }

      return 0;

    default: break;
    }

    /* TAB, RETURN, ESCAPE: let them bubble */
    return 0;
  }

  return 0;
}

/* ---- Registration ---- */

lk_widget_def lk_text_input_widget_def(void) {
  lk_widget_def def;
  memset(&def, 0, sizeof(def));
  def.measure = measure_text_input;
  def.layout = 0; /* leaf node */
  def.render = render_text_input;
  def.event = event_text_input;
  def.clips = 0;
  return def;
}
