/*
 * lk-scroll.c -- Scroll container widget.
 *
 * Clips children, scrolls vertically via wheel events, displays a
 * scroll bar indicator.  Children are stacked vertically like a column.
 *
 * Scroll offset stored in LKS_SCROLL_Y, max offset in LKS_SCROLL_MAX.
 */

#include <string.h>

#include "lk-memory.h"
#include "lk-scroll.h"

#define SCROLL_BAR_W 6
#define SCROLL_STEP 30

/* ---- Helpers ---- */

static lk_i32 clamp(lk_i32 v, lk_i32 lo, lk_i32 hi) {
  if (v < lo) {
    return lo;
  }
  if (v > hi) {
    return hi;
  }
  return v;
}

static lk_i32 get_scroll_y(const lk_state *state, lk_node_id nid) {
  lk_value v;
  if (!state) {
    return 0;
  }
  v = lk_state_get(state, nid, LKS_SCROLL_Y);
  if (v.tag == UIV_I32) {
    return (lk_i32)v.as.i;
  }
  return 0;
}

static lk_i32 get_scroll_max(const lk_state *state, lk_node_id nid) {
  lk_value v;
  if (!state) {
    return 0;
  }
  v = lk_state_get(state, nid, LKS_SCROLL_MAX);
  if (v.tag == UIV_I32) {
    return (lk_i32)v.as.i;
  }
  return 0;
}

/* ---- Measure ---- */

static void measure_scroll(const lk_tree *t, lk_ix n, const lk_size *sizes,
                           const lk_layout_cfg *cfg, lk_i32 *out_w,
                           lk_i32 *out_h) {
  const lk_node *nd = &t->nodes[n];
  lk_i32 pad = cfg->styles ? cfg->styles[n].padding
                           : lk_node_prop_i32(t, n, UIP_PADDING, 0);
  lk_i32 gap =
      cfg->styles ? cfg->styles[n].gap : lk_node_prop_i32(t, n, UIP_GAP, 0);
  lk_ix ch = nd->first_child;
  lk_i32 max_w = 0;
  lk_i32 sum_h = 0;
  int count = 0;

  while (ch) {
    if (sizes[ch].w > max_w) {
      max_w = sizes[ch].w;
    }

    sum_h += sizes[ch].h;
    count++;
    ch = t->nodes[ch].next_sibling;
  }

  if (count > 1) {
    sum_h += gap * (count - 1);
  }

  *out_w = max_w + pad * 2;
  *out_h = sum_h + pad * 2;
}

/* ---- Layout ---- */

static int layout_scroll(const lk_tree *t, lk_ix n, const lk_size *sizes,
                         const lk_rect *content, const lk_layout_cfg *cfg,
                         lk_rect *rects) {
  const lk_node *nd = &t->nodes[n];
  lk_i32 gap =
      cfg->styles ? cfg->styles[n].gap : lk_node_prop_i32(t, n, UIP_GAP, 0);
  lk_node_id nid = nd->id;
  lk_ix ch;
  lk_i32 content_h = 0;
  int count = 0;
  lk_i32 scroll_max;
  lk_i32 scroll_y;
  lk_i32 avail_w;
  lk_i32 y;

  /* Sum children heights */
  ch = nd->first_child;

  while (ch) {
    content_h += sizes[ch].h;
    count++;
    ch = t->nodes[ch].next_sibling;
  }

  if (count > 1) {
    content_h += gap * (count - 1);
  }

  /* Compute scroll bounds */
  scroll_max = content_h - content->h;

  if (scroll_max < 0) {
    scroll_max = 0;
  }

  /* Read and clamp scroll offset */
  scroll_y = get_scroll_y(cfg->state, nid);
  scroll_y = clamp(scroll_y, 0, scroll_max);

  /* Store scroll_max and clamped scroll_y in state */
  if (cfg->state) {
    lk_state_set(cfg->state, nid, LKS_SCROLL_MAX, lk_v_i32(scroll_max));
    lk_state_set(cfg->state, nid, LKS_SCROLL_Y, lk_v_i32(scroll_y));
  }

  /* Reduce available width when scrollable (scroll bar space) */
  avail_w = content->w;

  if (scroll_max > 0) {
    avail_w -= SCROLL_BAR_W;

    if (avail_w < 0) {
      avail_w = 0;
    }
  }

  /* Position children */
  y = content->y - scroll_y;
  ch = nd->first_child;

  while (ch) {
    rects[ch].x = content->x;
    rects[ch].y = y;
    rects[ch].w = avail_w;
    rects[ch].h = sizes[ch].h;
    y += sizes[ch].h + gap;
    ch = t->nodes[ch].next_sibling;
  }

  return 1;
}

/* ---- Render ---- */

static void render_scroll(const lk_tree *t, lk_ix n, const lk_rect *rect,
                          const lk_style *style, const lk_state *state,
                          lk_render_list *out) {
  lk_node_id nid = t->nodes[n].id;
  lk_i32 scroll_max;
  lk_render_cmd cmd;

  /* Background */
  memset(&cmd, 0, sizeof(cmd));
  cmd.op = LK_ROP_FILL_RECT;
  cmd.rect = *rect;
  cmd.color = style->bg;
  lk_render_list_push(out, cmd);

  /* Scroll bar (rendered before CLIP_BEGIN so not clipped) */
  scroll_max = get_scroll_max(state, nid);
  if (scroll_max > 0) {
    lk_i32 scroll_y = get_scroll_y(state, nid);
    lk_i32 pad = style->padding;
    lk_i32 track_h = rect->h - pad * 2;
    lk_i32 viewport_h = rect->h - pad * 2;
    lk_i32 total_h = viewport_h + scroll_max;
    lk_i32 thumb_h;
    lk_i32 thumb_y;

    if (track_h <= 0 || total_h <= 0) {
      return;
    }

    thumb_h = (viewport_h * track_h) / total_h;
    if (thumb_h < 8) {
      thumb_h = 8;
    }
    thumb_y = rect->y + pad;
    if (scroll_max > 0) {
      thumb_y += (scroll_y * (track_h - thumb_h)) / scroll_max;
    }

    /* Track */
    memset(&cmd, 0, sizeof(cmd));
    cmd.op = LK_ROP_FILL_RECT;
    cmd.rect.x = rect->x + rect->w - SCROLL_BAR_W;
    cmd.rect.y = rect->y + pad;
    cmd.rect.w = SCROLL_BAR_W;
    cmd.rect.h = track_h;
    cmd.color = style->scrollbar_track;
    lk_render_list_push(out, cmd);

    /* Thumb */
    memset(&cmd, 0, sizeof(cmd));
    cmd.op = LK_ROP_FILL_RECT;
    cmd.rect.x = rect->x + rect->w - SCROLL_BAR_W;
    cmd.rect.y = thumb_y;
    cmd.rect.w = SCROLL_BAR_W;
    cmd.rect.h = thumb_h;
    cmd.color = style->scrollbar_thumb;
    lk_render_list_push(out, cmd);
  }
}

/* ---- Event handling ---- */

static int event_scroll(lk_ui *ui, const lk_tree *t, lk_ix n, lk_event *ev) {
  lk_state *st;
  lk_node_id nid;
  lk_i32 scroll_y, scroll_max;

  if (ev->type != LK_EVENT_WHEEL) {
    return 0;
  }

  st = lk_ui_state(ui);
  nid = t->nodes[n].id;
  scroll_max = get_scroll_max(st, nid);

  if (scroll_max <= 0) {
    return 0;
  }

  scroll_y = get_scroll_y(st, nid);
  scroll_y -= ev->data.wheel.dy * SCROLL_STEP;
  scroll_y = clamp(scroll_y, 0, scroll_max);
  lk_state_set(st, nid, LKS_SCROLL_Y, lk_v_i32(scroll_y));

  return 1;
}

/* ---- Registration ---- */

lk_widget_def lk_scroll_widget_def(void) {
  lk_widget_def def;
  memset(&def, 0, sizeof(def));
  def.measure = measure_scroll;
  def.layout = layout_scroll;
  def.render = render_scroll;
  def.event = event_scroll;
  def.clips = 1;
  return def;
}
