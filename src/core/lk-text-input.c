/*
 * lk-text-input.c — Single-line text input widget.
 *
 * Text buffer is stored as an interned string in lk_state (LKS_TEXT_BUF).
 * Cursor position (byte index, codepoint-boundary-aligned) in
 * LKS_CURSOR_POS.  Selection range in LKS_SELECTION_START /
 * LKS_SELECTION_END.  Cursor motion and deletion are codepoint-wise
 * (lk-utf8.h).  Cursor/selection pixel x-offsets are computed during
 * measure via the text backend's x_from_index and stored in
 * LKS_CURSOR_X / LKS_SEL_X0 / LKS_SEL_X1.  POINTER_DOWN positions the
 * cursor via index_from_x (requires lk_ui_set_text_backend).
 *
 * The host feeds the edited text back as UIP_TEXT each frame for rendering.
 */

#include <string.h>

#include "lk-memory.h"
#include "lk-utf8.h"
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

/* Measure a run through cfg->text with the node's resolved font
 * (0/0 defaults when no styles).  Zero metrics when no backend. */
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

/* Pixel x-offset of byte_ix in run, via the backend's x_from_index
 * (0 when no backend). */
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

  /* Minimum width when text is empty */
  if (m.w < 100) {
    m.w = 100;
  }

  *out_w = m.w + inset * 2;
  *out_h = m.h + inset * 2;

  /* Compute cursor and selection pixel offsets via x_from_index and
   * store them in state for render (which has no text backend).
   * Derived geometry in retained state (LKS_CURSOR_X / LKS_SEL_X0/X1)
   * is on the design-coherence list (docs/TODO.md): it belongs in
   * per-frame scratch parallel to rects[]. */
  if (cfg->state) {
    nid = t->nodes[n].id;
    {
      lk_i32 cursor_pos = get_cursor(cfg->state, nid, (lk_i32)text.len);
      lk_i32 sel_s = get_sel_start(cfg->state, nid, (lk_i32)text.len);
      lk_i32 sel_e = get_sel_end(cfg->state, nid, (lk_i32)text.len);
      lk_i32 lo = sel_s < sel_e ? sel_s : sel_e;
      lk_i32 hi = sel_s < sel_e ? sel_e : sel_s;

      lk_state_set(cfg->state, nid, LKS_CURSOR_X,
                   lk_v_i32(run_x_from_ix(cfg, n, text, (lk_u32)cursor_pos)));
      lk_state_set(cfg->state, nid, LKS_SEL_X0,
                   lk_v_i32(run_x_from_ix(cfg, n, text, (lk_u32)lo)));
      lk_state_set(cfg->state, nid, LKS_SEL_X1,
                   lk_v_i32(run_x_from_ix(cfg, n, text, (lk_u32)hi)));
    }
  }
}

/* Stash per-node geometry the event handler needs but cannot compute
 * (no rects, no styles at event time): the text content origin x and
 * the resolved font.  Called by lk_layout after rects are final —
 * same coherence-debt pattern as lk_dropdown_store_trigger_rects
 * (derived geometry in retained state; see docs/TODO.md). */
void lk_text_input_store_geometry(const lk_tree *t, const lk_rect *rects,
                                  const lk_layout_cfg *cfg) {
  lk_ix n;

  if (!t || !rects || !cfg || !cfg->state) {
    return;
  }

  for (n = 1; n < (lk_ix)t->node_count; n++) {
    lk_node_id nid;
    lk_i32 pad;
    lk_i32 bw;

    if (t->nodes[n].kind != UIK_TEXT_INPUT) {
      continue;
    }

    pad = cfg->styles ? cfg->styles[n].padding
                      : lk_node_prop_i32(t, n, UIP_PADDING, 0);
    bw = cfg->styles ? cfg->styles[n].border_width : 0;
    nid = t->nodes[n].id;

    lk_state_set(cfg->state, nid, LKS_TEXT_ORIGIN_X,
                 lk_v_i32(rects[n].x + pad + bw));
    lk_state_set(cfg->state, nid, LKS_FONT_ID,
                 lk_v_i32(cfg->styles ? (lk_i32)cfg->styles[n].font_id : 0));
    lk_state_set(cfg->state, nid, LKS_FONT_SIZE,
                 lk_v_i32(cfg->styles ? (lk_i32)cfg->styles[n].font_size : 0));
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

  /* Selection highlight — endpoint x-offsets were computed via
   * x_from_index during measure and stashed in LKS_SEL_X0/X1
   * (render has no text backend). */
  if (state) {
    lk_i32 sel_s = get_sel_start(state, nid, (lk_i32)text.len);
    lk_i32 sel_e = get_sel_end(state, nid, (lk_i32)text.len);

    if (sel_s != sel_e) {
      lk_value x0_v = lk_state_get(state, nid, LKS_SEL_X0);
      lk_value x1_v = lk_state_get(state, nid, LKS_SEL_X1);

      if (x0_v.tag == UIV_I32 && x1_v.tag == UIV_I32 &&
          (lk_i32)x1_v.as.i > (lk_i32)x0_v.as.i) {
        memset(&cmd, 0, sizeof(cmd));
        cmd.op = LK_ROP_FILL_RECT;
        cmd.rect.x = rect->x + inset + (lk_i32)x0_v.as.i;
        cmd.rect.y = rect->y + inset;
        cmd.rect.w = (lk_i32)x1_v.as.i - (lk_i32)x0_v.as.i;
        cmd.rect.h = rect->h - inset * 2;
        cmd.color.r = 80;
        cmd.color.g = 120;
        cmd.color.b = 200;
        cmd.color.a = 128;
        lk_render_list_push(out, cmd);
      }
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
    cmd.font_id = (lk_u16)style->font_id;
    cmd.font_size = (lk_u16)style->font_size;
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

    /* Check capacity — truncate at a codepoint boundary so a UTF-8
     * sequence is never split at the buffer cap. */
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
        return 1; /* consumed but nothing fits */
      }
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

  if (ev->type == LK_EVENT_POINTER_DOWN) {
    /* Click-to-position: map pointer x to a byte index via the UI's
     * text backend (installed by lk_ui_set_text_backend; NULL means
     * the feature is off and the event bubbles so the host's built-in
     * click-to-focus still runs).  Text origin and resolved font come
     * from state, stashed by lk_text_input_store_geometry during
     * layout — event handlers see neither rects nor styles. */
    lk_value v;
    lk_i32 origin_x;
    lk_u16 font_id;
    lk_u16 font_size;
    lk_u32 ix;

    if (!ui->text) {
      return 0;
    }

    v = lk_state_get(st, nid, LKS_TEXT_ORIGIN_X);

    if (v.tag != UIV_I32) {
      return 0; /* no layout pass has stashed geometry yet */
    }

    origin_x = (lk_i32)v.as.i;

    v = lk_state_get(st, nid, LKS_FONT_ID);
    font_id = (v.tag == UIV_I32) ? (lk_u16)v.as.i : 0;
    v = lk_state_get(st, nid, LKS_FONT_SIZE);
    font_size = (v.tag == UIV_I32) ? (lk_u16)v.as.i : 0;

    /* index_from_x snaps to the nearest boundary and clamps to
     * [0, len] per the contract; re-clamp defensively. */
    ix = ui->text->index_from_x(ui->text->ud, text, font_id, font_size,
                                ev->data.pointer.x - origin_x);

    if (ix > text.len) {
      ix = text.len;
    }

    /* We consume the event, so the host's built-in click-to-focus
     * never sees it — take focus here. */
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
      lk_i32 sel_cursor = delete_selection(ui, t, n, text.ptr, text_len);

      if (sel_cursor >= 0) {
        emit_value_changed(ui, t, n);
        return 1;
      }

      if (cursor > 0) {
        /* Delete the whole codepoint before the cursor. */
        char buf[LK_TEXT_INPUT_MAX];
        lk_i32 del_from =
            (lk_i32)lk_utf8_prev(text.ptr, text.len, (lk_u32)cursor);
        lk_i32 new_len = text_len - (cursor - del_from);
        lk_value v;
        lk_str s;
        memcpy(buf, text.ptr, (size_t)del_from);
        memcpy(buf + del_from, text.ptr + cursor,
               (size_t)(text_len - cursor));
        buf[new_len] = '\0';
        s.ptr = buf;
        s.len = (lk_u32)new_len;
        v.tag = UIV_STR;
        v.as.str_id = lk_intern_id(t->intern, s);
        lk_state_set(st, nid, LKS_TEXT_BUF, v);
        lk_state_set(st, nid, LKS_CURSOR_POS, lk_v_i32(del_from));
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
        /* Delete the whole codepoint after the cursor. */
        char buf[LK_TEXT_INPUT_MAX];
        lk_i32 del_to =
            (lk_i32)lk_utf8_next(text.ptr, text.len, (lk_u32)cursor);
        lk_i32 new_len = text_len - (del_to - cursor);
        lk_value v;
        lk_str s;
        memcpy(buf, text.ptr, (size_t)cursor);
        memcpy(buf + cursor, text.ptr + del_to,
               (size_t)(text_len - del_to));
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
        lk_i32 new_pos =
            (lk_i32)lk_utf8_prev(text.ptr, text.len, (lk_u32)cursor);
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
            lk_i32 full_len = ins_len;
            ins_len = LK_TEXT_INPUT_MAX - 1 - text_len;

            if (ins_len < 0) {
              ins_len = 0;
            }

            /* Truncate at a codepoint boundary — never split a
             * UTF-8 sequence at the buffer cap. */
            while (ins_len > 0 &&
                   !lk_utf8_is_boundary(clip, (lk_u32)full_len,
                                        (lk_u32)ins_len)) {
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
