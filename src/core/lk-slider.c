/*
 * lk-slider.c -- UIK_SLIDER (docs/forms-widgets.md).
 *
 * Horizontal integer slider over [UIP_MIN, UIP_MAX] in UIP_STEP
 * increments (lk_value has no float; apps scale).  Leaf node.
 *
 * Effective value = LKS_SLIDER_VALUE state (drag / keys) > UIP_VALUE
 * (i32) > UIP_MIN, always snapped to MIN + k*STEP and clamped.  Every
 * change enqueues LK_EVENT_VALUE_CHANGED with the new value as a
 * decimal string; UIP_CONTROLLED nonzero suppresses the state write
 * (the app re-supplies UIP_VALUE each frame).
 *
 * Geometry: the track (thumb travel area) is the node's content rect
 * (rect inset by padding + border_width); the layout hook stashes it
 * in the per-frame geometry scratch (geom->track) because the event
 * handler sees neither rects nor styles.  Thumb x for value v is
 * track.x + (v - min) * (track.w - THUMB_W) / (max - min).  Render:
 * bg (if visible), TRACK_H-tall rail in style->border_color, the
 * filled part and the THUMB_W-wide thumb in style->accent.
 *
 * Events: POINTER_DOWN (primary/any) in the track jumps the value to
 * the pointer, takes focus (if focusable) and the pointer capture,
 * and sets LKS_SLIDER_DRAGGING; MOVE while dragging tracks the
 * pointer; UP releases.  Keys (when focused): LEFT/DOWN -step,
 * RIGHT/UP +step, PAGEUP/PAGEDOWN +-10 steps, HOME/END min/max.
 * No geom stashed (or no layout yet): pointer events bubble.
 */

#include <stdio.h>
#include <string.h>

#include "lk-memory.h"
#include "lk-slider.h"
#include <lk.h>

#define SLIDER_DEFAULT_W 120
#define THUMB_W 10
#define THUMB_H 14
#define TRACK_H 4
#define PAGE_STEPS 10

void lk_slider_range(const lk_tree *t, lk_ix n, lk_i32 *out_min,
                     lk_i32 *out_max, lk_i32 *out_step) {
  lk_i32 mn = lk_node_prop_i32(t, n, UIP_MIN, 0);
  lk_i32 mx = lk_node_prop_i32(t, n, UIP_MAX, 100);
  lk_i32 st = lk_node_prop_i32(t, n, UIP_STEP, 1);

  if (mx < mn) {
    mx = mn;
  }

  if (st < 1) {
    st = 1;
  }

  *out_min = mn;
  *out_max = mx;
  *out_step = st;
}

static lk_i32 snap(lk_i32 v, lk_i32 mn, lk_i32 mx, lk_i32 step) {
  lk_i32 k;

  if (v < mn) {
    v = mn;
  }

  if (v > mx) {
    v = mx;
  }

  k = (v - mn + step / 2) / step;
  v = mn + k * step;

  if (v > mx) {
    v -= step;
  }

  if (v < mn) {
    v = mn;
  }

  return v;
}

lk_i32 lk_slider_effective(const lk_tree *t, lk_ix n, const lk_state *state) {
  lk_i32 mn, mx, step;
  lk_i32 v;

  lk_slider_range(t, n, &mn, &mx, &step);

  v = lk_node_prop_i32(t, n, UIP_VALUE, mn);

  if (state) {
    lk_value sv = lk_state_get(state, t->nodes[n].id, LKS_SLIDER_VALUE);

    if (sv.tag == UIV_I32) {
      v = sv.as.i;
    }
  }

  return snap(v, mn, mx, step);
}

/* ---- Measure / layout ---- */

static void measure_slider(const lk_tree *t, lk_ix n, const lk_size *sizes,
                           const lk_layout_cfg *cfg, lk_i32 *out_w,
                           lk_i32 *out_h) {
  lk_i32 pad = cfg->styles ? cfg->styles[n].padding
                           : lk_node_prop_i32(t, n, UIP_PADDING, 0);
  lk_i32 bw = cfg->styles ? cfg->styles[n].border_width : 0;
  lk_i32 inset = pad + bw;

  (void)sizes;
  *out_w = SLIDER_DEFAULT_W + inset * 2;
  *out_h = THUMB_H + inset * 2;
}

/* Leaf with a layout hook: nothing to place, but the content rect is
 * final here, so stash it as the track for the event handler. */
static int layout_slider(const lk_tree *t, lk_ix n, const lk_size *sizes,
                         const lk_rect *content, const lk_layout_cfg *cfg,
                         lk_rect *rects) {
  (void)t;
  (void)sizes;
  (void)rects;

  if (cfg->geom) {
    cfg->geom[n].track.x = content->x;
    cfg->geom[n].track.y = content->y;
    cfg->geom[n].track.w = content->w;
    cfg->geom[n].track.h = content->h;
  }

  return 1;
}

/* Thumb x (left edge) for value v within a track. */
static lk_i32 thumb_x(const lk_rect *track, lk_i32 v, lk_i32 mn, lk_i32 mx) {
  lk_i32 travel = track->w - THUMB_W;

  if (travel <= 0 || mx <= mn) {
    return track->x;
  }

  return track->x + (lk_i32)(((lk_i64)(v - mn) * travel) / (mx - mn));
}

/* Value for a pointer x within a track (thumb center under cursor). */
static lk_i32 value_at_x(const lk_rect *track, lk_i32 x, lk_i32 mn, lk_i32 mx,
                         lk_i32 step) {
  lk_i32 travel = track->w - THUMB_W;
  lk_i32 rel = x - track->x - THUMB_W / 2;
  lk_i64 v;

  if (travel <= 0 || mx <= mn) {
    return mn;
  }

  if (rel < 0) {
    rel = 0;
  }

  if (rel > travel) {
    rel = travel;
  }

  /* round to nearest */
  v = ((lk_i64)rel * (mx - mn) + travel / 2) / travel + mn;

  return snap((lk_i32)v, mn, mx, step);
}

/* ---- Render ---- */

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

static void render_slider(const lk_tree *t, lk_ix n, const lk_rect *rect,
                          const lk_style *style, const lk_state *state,
                          const lk_widget_geom *geom, lk_render_list *out) {
  lk_i32 inset = style->padding + style->border_width;
  lk_rect track;
  lk_i32 mn, mx, step, v, tx, ty, th;

  (void)geom;

  track.x = rect->x + inset;
  track.y = rect->y + inset;
  track.w = rect->w - inset * 2;
  track.h = rect->h - inset * 2;

  if (style->bg.a > 0) {
    push_fill(out, rect->x, rect->y, rect->w, rect->h, style->bg);
  }

  if (track.w <= 0 || track.h <= 0) {
    return;
  }

  lk_slider_range(t, n, &mn, &mx, &step);
  v = lk_slider_effective(t, n, state);
  tx = thumb_x(&track, v, mn, mx);
  ty = track.y + (track.h - TRACK_H) / 2;

  /* rail */
  push_fill(out, track.x, ty, track.w, TRACK_H, style->border_color);
  /* filled part up to the thumb center */
  push_fill(out, track.x, ty, tx + THUMB_W / 2 - track.x, TRACK_H,
            style->accent);
  /* thumb */
  th = track.h < THUMB_H ? track.h : THUMB_H;
  push_fill(out, tx, track.y + (track.h - th) / 2, THUMB_W, th, style->accent);
}

/* ---- Events ---- */

static void emit_value_changed(lk_ui *ui, lk_ix n, lk_i32 v) {
  char buf[16];
  lk_event vev;

  sprintf(buf, "%d", (int)v);
  memset(&vev, 0, sizeof(vev));
  vev.type = LK_EVENT_VALUE_CHANGED;
  vev.target = n;
  vev.data.value_changed.str_id = lk_intern_cid(ui->intern, buf);
  lk_event_enqueue(ui, &vev);
}

/* Set the value if it changed: state write (unless controlled) +
 * VALUE_CHANGED. */
static void slider_set(lk_ui *ui, const lk_tree *t, lk_ix n, lk_i32 v) {
  lk_state *st = lk_ui_state(ui);
  lk_i32 cur = lk_slider_effective(t, n, st);

  if (v == cur) {
    return;
  }

  if (st && !lk_node_prop_i32(t, n, UIP_CONTROLLED, 0)) {
    lk_state_set(st, t->nodes[n].id, LKS_SLIDER_VALUE, lk_v_i32(v));
  }

  emit_value_changed(ui, n, v);
}

static int stashed_track(const lk_ui *ui, lk_ix n, lk_rect *out) {
  if (!ui->geom || (lk_u32)n >= ui->geom_cap) {
    return 0;
  }

  if (ui->geom[n].track.w <= 0) {
    return 0;
  }

  out->x = ui->geom[n].track.x;
  out->y = ui->geom[n].track.y;
  out->w = ui->geom[n].track.w;
  out->h = ui->geom[n].track.h;

  return 1;
}

static int event_slider(lk_ui *ui, const lk_tree *t, lk_ix n, lk_event *ev) {
  lk_state *st = lk_ui_state(ui);
  lk_node_id nid = t->nodes[n].id;
  lk_i32 mn, mx, step;

  if (lk_node_prop_bool(t, n, UIP_DISABLED)) {
    return 0;
  }

  lk_slider_range(t, n, &mn, &mx, &step);

  if (ev->type == LK_EVENT_KEY_DOWN) {
    lk_i32 cur = lk_slider_effective(t, n, st);
    lk_i32 v = cur;

    if (ev->mods != 0) {
      return 0;
    }

    switch (ev->data.key.keycode) {
    case LKK_LEFT:
    case LKK_DOWN: v = cur - step; break;
    case LKK_RIGHT:
    case LKK_UP: v = cur + step; break;
    case LKK_PAGEDOWN: v = cur - step * PAGE_STEPS; break;
    case LKK_PAGEUP: v = cur + step * PAGE_STEPS; break;
    case LKK_HOME: v = mn; break;
    case LKK_END: v = mx; break;
    default: return 0;
    }

    slider_set(ui, t, n, snap(v, mn, mx, step));

    return 1;
  }

  if (ev->type == LK_EVENT_POINTER_DOWN || ev->type == LK_EVENT_POINTER_MOVE ||
      ev->type == LK_EVENT_POINTER_UP) {
    lk_rect track;
    int dragging;

    if (!st || !stashed_track(ui, n, &track)) {
      return 0;
    }

    dragging = lk_state_get(st, nid, LKS_SLIDER_DRAGGING).tag == UIV_I32 &&
               lk_state_get(st, nid, LKS_SLIDER_DRAGGING).as.i;

    if (ev->type == LK_EVENT_POINTER_DOWN) {
      lk_u8 b = ev->data.pointer.button;

      if (b != LK_POINTER_BUTTON_ANY && b != LK_POINTER_BUTTON_PRIMARY) {
        return 0;
      }

      if (ev->data.pointer.x < track.x ||
          ev->data.pointer.x >= track.x + track.w ||
          ev->data.pointer.y < track.y ||
          ev->data.pointer.y >= track.y + track.h) {
        return 0;
      }

      lk_focus_set(ui, t, nid);
      lk_capture_set(ui, nid);
      lk_state_set(st, nid, LKS_SLIDER_DRAGGING, lk_v_i32(1));
      slider_set(ui, t, n,
                 value_at_x(&track, ev->data.pointer.x, mn, mx, step));

      return 1;
    }

    if (!dragging) {
      return 0;
    }

    if (ev->type == LK_EVENT_POINTER_MOVE) {
      slider_set(ui, t, n,
                 value_at_x(&track, ev->data.pointer.x, mn, mx, step));

      return 1;
    }

    /* POINTER_UP while dragging */
    lk_state_set(st, nid, LKS_SLIDER_DRAGGING, lk_v_i32(0));
    lk_capture_clear(ui);

    return 1;
  }

  return 0;
}

/* ---- Registration ---- */

lk_widget_def lk_slider_widget_def(void) {
  lk_widget_def def;
  memset(&def, 0, sizeof(def));
  def.measure = measure_slider;
  def.layout = layout_slider;
  def.render = render_slider;
  def.event = event_slider;
  def.clips = 0;
  return def;
}
