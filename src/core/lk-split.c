/*
 * lk-split.c -- Resizable split-pane containers (UIK_SPLIT_H /
 * UIK_SPLIT_V).
 *
 * A split holds exactly two panes separated by a DIVIDER_W-pixel
 * divider band.  SPLIT_H places them side-by-side (vertical divider);
 * SPLIT_V stacks them (horizontal divider).  The divider band belongs
 * to the split node itself — it is not a separate node, so a
 * hit-test in the band lands on the split (no child covers it).
 *
 * Ratio is per-mille (0..1000; lk_value has no float).  Priority:
 *   LKS_SPLIT_RATIO state (written by dragging)
 *   > UIP_SPLIT_RATIO prop (host-set initial value)
 *   > 500.
 * The first pane is clamped to >= MIN_PANE px on the split axis
 * whenever the axis is big enough for both panes to keep it.
 *
 * Degradation: one visible child fills the whole content rect; zero
 * children renders background only; children beyond the first two are
 * ignored (their rects are zeroed).
 *
 * Dragging uses the pointer-capture facility (lk_capture_set): the
 * divider keeps receiving POINTER_MOVE/UP while the cursor is outside
 * the band.  Divider geometry is always derived from the split's own
 * laid-out rect (render) or its stashed content rect (events) — never
 * recomputed from ancestors.
 */

#include <string.h>

#include "lk-memory.h"
#include "lk-split.h"

#define SPLIT_DIVIDER_W 5
#define SPLIT_MIN_PANE 40
#define SPLIT_DEFAULT_RATIO 500

/* ---- Helpers ---- */

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

static int point_in(const lk_rect *r, lk_i32 x, lk_i32 y) {
  return x >= r->x && x < r->x + r->w && y >= r->y && y < r->y + r->h;
}

/* Collect up to two visible (non-hidden) children.  Returns the
 * visible-pane count clamped to 2; extras are ignored. */
static lk_u32 split_panes(const lk_tree *t, lk_ix n, lk_ix out[2]) {
  lk_ix ch = t->nodes[n].first_child;
  lk_u32 c = 0;

  out[0] = 0;
  out[1] = 0;

  while (ch) {
    if (!lk_node_prop_bool(t, ch, UIP_HIDDEN)) {
      if (c < 2) {
        out[c] = ch;
      }

      c++;
    }

    ch = t->nodes[ch].next_sibling;
  }

  return c > 2 ? 2 : c;
}

/* Effective ratio: state > prop > default, clamped to 0..1000. */
static lk_i32 split_ratio_eff(const lk_tree *t, lk_ix n,
                              const lk_state *state) {
  lk_i32 r = lk_node_prop_i32(t, n, UIP_SPLIT_RATIO, SPLIT_DEFAULT_RATIO);

  if (state) {
    lk_value v = lk_state_get(state, t->nodes[n].id, LKS_SPLIT_RATIO);

    if (v.tag == UIV_I32) {
      r = (lk_i32)v.as.i;
    }
  }

  if (r < 0) {
    r = 0;
  }

  if (r > 1000) {
    r = 1000;
  }

  return r;
}

/* First pane's extent along the split axis for a given available
 * extent (content minus divider).  Keeps both panes >= MIN_PANE when
 * the axis is big enough. */
static lk_i32 split_first_main(lk_i32 avail, lk_i32 ratio) {
  lk_i32 first;

  if (avail <= 0) {
    return 0;
  }

  first = (avail * ratio) / 1000;

  if (avail >= SPLIT_MIN_PANE * 2) {
    if (first < SPLIT_MIN_PANE) {
      first = SPLIT_MIN_PANE;
    }

    if (first > avail - SPLIT_MIN_PANE) {
      first = avail - SPLIT_MIN_PANE;
    }
  } else {
    if (first < 0) {
      first = 0;
    }

    if (first > avail) {
      first = avail;
    }
  }

  return first;
}

/* Divider band rect within a content rect (only meaningful with two
 * visible panes; caller checks).  horiz=1 for SPLIT_H (vertical band),
 * 0 for SPLIT_V (horizontal band). */
static lk_rect split_band_rect(const lk_rect *content, lk_i32 first,
                               int horiz) {
  lk_rect band;

  if (horiz) {
    band.x = content->x + first;
    band.y = content->y;
    band.w = SPLIT_DIVIDER_W;
    band.h = content->h;
  } else {
    band.x = content->x;
    band.y = content->y + first;
    band.w = content->w;
    band.h = SPLIT_DIVIDER_W;
  }

  return band;
}

/* ---- Measure ---- */

/* Intrinsics: sum of the two panes plus the divider on the split
 * axis, max on the cross axis. */
static void measure_split(const lk_tree *t, lk_ix n, const lk_size *sizes,
                          const lk_layout_cfg *cfg, lk_i32 *out_w,
                          lk_i32 *out_h, int horiz) {
  lk_i32 pad = cfg->styles ? cfg->styles[n].padding
                           : lk_node_prop_i32(t, n, UIP_PADDING, 0);
  lk_i32 bw = cfg->styles ? cfg->styles[n].border_width : 0;
  lk_i32 inset = pad + bw;
  lk_ix panes[2];
  lk_u32 count = split_panes(t, n, panes);
  lk_i32 sum_main = 0;
  lk_i32 max_cross = 0;
  lk_u32 i;

  for (i = 0; i < count; i++) {
    lk_i32 cm = horiz ? sizes[panes[i]].w : sizes[panes[i]].h;
    lk_i32 cc = horiz ? sizes[panes[i]].h : sizes[panes[i]].w;

    sum_main += cm;

    if (cc > max_cross) {
      max_cross = cc;
    }
  }

  if (count == 2) {
    sum_main += SPLIT_DIVIDER_W;
  }

  if (horiz) {
    *out_w = sum_main + inset * 2;
    *out_h = max_cross + inset * 2;
  } else {
    *out_w = max_cross + inset * 2;
    *out_h = sum_main + inset * 2;
  }
}

static void measure_split_h(const lk_tree *t, lk_ix n, const lk_size *sizes,
                            const lk_layout_cfg *cfg, lk_i32 *out_w,
                            lk_i32 *out_h) {
  measure_split(t, n, sizes, cfg, out_w, out_h, 1);
}

static void measure_split_v(const lk_tree *t, lk_ix n, const lk_size *sizes,
                            const lk_layout_cfg *cfg, lk_i32 *out_w,
                            lk_i32 *out_h) {
  measure_split(t, n, sizes, cfg, out_w, out_h, 0);
}

/* ---- Layout ---- */

static int layout_split(const lk_tree *t, lk_ix n, const lk_size *sizes,
                        const lk_rect *content, const lk_layout_cfg *cfg,
                        lk_rect *rects, int horiz) {
  lk_ix panes[2];
  lk_u32 count = split_panes(t, n, panes);
  lk_i32 main_size = horiz ? content->w : content->h;
  lk_i32 avail;
  lk_i32 first;
  lk_i32 second;
  lk_ix ch;
  lk_u32 seen = 0;

  (void)sizes;

  /* Extras beyond the first two visible children are ignored: zero
   * their rects so they render nowhere and never hit-test. */
  ch = t->nodes[n].first_child;

  while (ch) {
    if (!lk_node_prop_bool(t, ch, UIP_HIDDEN)) {
      if (seen >= 2) {
        rects[ch].x = 0;
        rects[ch].y = 0;
        rects[ch].w = 0;
        rects[ch].h = 0;
      }

      seen++;
    }

    ch = t->nodes[ch].next_sibling;
  }

  if (count == 0) {
    return 1;
  }

  if (count == 1) {
    /* Single pane fills the whole content rect. */
    rects[panes[0]] = *content;
    return 1;
  }

  avail = main_size - SPLIT_DIVIDER_W;

  if (avail < 0) {
    avail = 0;
  }

  first = split_first_main(avail, split_ratio_eff(t, n, cfg->state));
  second = avail - first;

  if (horiz) {
    rects[panes[0]].x = content->x;
    rects[panes[0]].y = content->y;
    rects[panes[0]].w = first;
    rects[panes[0]].h = content->h;

    rects[panes[1]].x = content->x + first + SPLIT_DIVIDER_W;
    rects[panes[1]].y = content->y;
    rects[panes[1]].w = second;
    rects[panes[1]].h = content->h;
  } else {
    rects[panes[0]].x = content->x;
    rects[panes[0]].y = content->y;
    rects[panes[0]].w = content->w;
    rects[panes[0]].h = first;

    rects[panes[1]].x = content->x;
    rects[panes[1]].y = content->y + first + SPLIT_DIVIDER_W;
    rects[panes[1]].w = content->w;
    rects[panes[1]].h = second;
  }

  return 1;
}

static int layout_split_h(const lk_tree *t, lk_ix n, const lk_size *sizes,
                          const lk_rect *content, const lk_layout_cfg *cfg,
                          lk_rect *rects) {
  return layout_split(t, n, sizes, content, cfg, rects, 1);
}

static int layout_split_v(const lk_tree *t, lk_ix n, const lk_size *sizes,
                          const lk_rect *content, const lk_layout_cfg *cfg,
                          lk_rect *rects) {
  return layout_split(t, n, sizes, content, cfg, rects, 0);
}

/* ---- Render ---- */

/* Background plus the divider band (style->border_color).  Divider
 * geometry is derived from the node's OWN laid-out rect — never
 * recomputed from ancestors. */
static void render_split(const lk_tree *t, lk_ix n, const lk_rect *rect,
                         const lk_style *style, const lk_state *state,
                         lk_render_list *out, int horiz) {
  lk_render_cmd cmd;
  lk_ix panes[2];
  lk_u32 count;

  /* Background */
  memset(&cmd, 0, sizeof(cmd));
  cmd.op = LK_ROP_FILL_RECT;
  cmd.rect = *rect;
  cmd.color = style->bg;
  lk_render_list_push(out, cmd);

  count = split_panes(t, n, panes);

  if (count == 2) {
    lk_i32 inset = style->padding + style->border_width;
    lk_rect content;
    lk_i32 avail;
    lk_i32 first;

    content.x = rect->x + inset;
    content.y = rect->y + inset;
    content.w = rect->w - inset * 2;
    content.h = rect->h - inset * 2;

    avail = (horiz ? content.w : content.h) - SPLIT_DIVIDER_W;

    if (avail >= 0) {
      first = split_first_main(avail, split_ratio_eff(t, n, state));

      memset(&cmd, 0, sizeof(cmd));
      cmd.op = LK_ROP_FILL_RECT;
      cmd.rect = split_band_rect(&content, first, horiz);
      cmd.color = style->border_color;
      lk_render_list_push(out, cmd);
    }
  }
}

static void render_split_h(const lk_tree *t, lk_ix n, const lk_rect *rect,
                           const lk_style *style, const lk_state *state,
                           lk_render_list *out) {
  render_split(t, n, rect, style, state, out, 1);
}

static void render_split_v(const lk_tree *t, lk_ix n, const lk_rect *rect,
                           const lk_style *style, const lk_state *state,
                           lk_render_list *out) {
  render_split(t, n, rect, style, state, out, 0);
}

/* ---- Event handling ---- */

/* Fetch the content rect stashed by the layout pass.  Returns 0 when
 * absent (layout never ran with state). */
static int split_stashed_content(const lk_state *state, lk_node_id nid,
                                 lk_rect *out) {
  lk_value w;

  if (!state) {
    return 0;
  }

  w = lk_state_get(state, nid, LKS_SPLIT_CW);

  if (w.tag != UIV_I32 || (lk_i32)w.as.i <= 0) {
    return 0;
  }

  out->x = get_i32(state, nid, LKS_SPLIT_CX);
  out->y = get_i32(state, nid, LKS_SPLIT_CY);
  out->w = (lk_i32)w.as.i;
  out->h = get_i32(state, nid, LKS_SPLIT_CH);

  return 1;
}

static int event_split(lk_ui *ui, const lk_tree *t, lk_ix n, lk_event *ev,
                       int horiz) {
  lk_state *st = lk_ui_state(ui);
  lk_node_id nid = t->nodes[n].id;
  lk_rect content;
  lk_i32 avail;
  int dragging;

  if (ev->type != LK_EVENT_POINTER_DOWN &&
      ev->type != LK_EVENT_POINTER_MOVE && ev->type != LK_EVENT_POINTER_UP) {
    return 0;
  }

  if (!st || !split_stashed_content(st, nid, &content)) {
    return 0;
  }

  dragging = get_i32(st, nid, LKS_SPLIT_DRAGGING) ? 1 : 0;

  if (ev->type == LK_EVENT_POINTER_DOWN) {
    lk_ix panes[2];
    lk_i32 first;
    lk_rect band;

    if (split_panes(t, n, panes) != 2) {
      return 0;
    }

    avail = (horiz ? content.w : content.h) - SPLIT_DIVIDER_W;

    if (avail < 0) {
      return 0;
    }

    first = split_first_main(avail, split_ratio_eff(t, n, st));
    band = split_band_rect(&content, first, horiz);

    if (!point_in(&band, ev->data.pointer.x, ev->data.pointer.y)) {
      /* Pane click bubbling through — not ours. */
      return 0;
    }

    lk_capture_set(ui, nid);
    lk_state_set(st, nid, LKS_SPLIT_DRAGGING, lk_v_i32(1));

    return 1;
  }

  if (!dragging) {
    return 0;
  }

  if (ev->type == LK_EVENT_POINTER_MOVE) {
    /* New ratio from pointer position along the split axis, anchoring
     * the divider's center under the cursor.  Clamped so the first
     * pane keeps >= MIN_PANE (per-mille min rounds up so the pixel
     * result lands exactly on MIN_PANE). */
    lk_i32 pos = horiz ? ev->data.pointer.x : ev->data.pointer.y;
    lk_i32 origin = horiz ? content.x : content.y;
    lk_i32 rel = pos - origin - SPLIT_DIVIDER_W / 2;
    lk_i32 ratio;

    avail = (horiz ? content.w : content.h) - SPLIT_DIVIDER_W;

    if (avail <= 0) {
      return 1; /* still consume while dragging */
    }

    ratio = (rel * 1000) / avail;

    if (avail >= SPLIT_MIN_PANE * 2) {
      lk_i32 min_r = (SPLIT_MIN_PANE * 1000 + avail - 1) / avail;
      lk_i32 max_r = ((avail - SPLIT_MIN_PANE) * 1000) / avail;

      if (ratio < min_r) {
        ratio = min_r;
      }

      if (ratio > max_r) {
        ratio = max_r;
      }
    }

    if (ratio < 0) {
      ratio = 0;
    }

    if (ratio > 1000) {
      ratio = 1000;
    }

    lk_state_set(st, nid, LKS_SPLIT_RATIO, lk_v_i32(ratio));

    return 1;
  }

  /* POINTER_UP while dragging: end the drag. */
  lk_state_set(st, nid, LKS_SPLIT_DRAGGING, lk_v_i32(0));
  lk_capture_clear(ui);

  return 1;
}

static int event_split_h(lk_ui *ui, const lk_tree *t, lk_ix n, lk_event *ev) {
  return event_split(ui, t, n, ev, 1);
}

static int event_split_v(lk_ui *ui, const lk_tree *t, lk_ix n, lk_event *ev) {
  return event_split(ui, t, n, ev, 0);
}

/* ---- Layout-pass geometry stash ---- */

void lk_split_store_geometry(const lk_tree *t, const lk_rect *rects,
                             const lk_layout_cfg *cfg) {
  lk_ix n;

  if (!t || !rects || !cfg || !cfg->state) {
    return;
  }

  for (n = 1; n < (lk_ix)t->node_count; n++) {
    lk_u16 kind = t->nodes[n].kind;
    lk_i32 pad;
    lk_i32 bw;
    lk_i32 inset;
    lk_node_id nid;

    if (kind != (lk_u16)UIK_SPLIT_H && kind != (lk_u16)UIK_SPLIT_V) {
      continue;
    }

    pad = cfg->styles ? cfg->styles[n].padding
                      : lk_node_prop_i32(t, n, UIP_PADDING, 0);
    bw = cfg->styles ? cfg->styles[n].border_width : 0;
    inset = pad + bw;

    nid = t->nodes[n].id;
    lk_state_set(cfg->state, nid, LKS_SPLIT_CX, lk_v_i32(rects[n].x + inset));
    lk_state_set(cfg->state, nid, LKS_SPLIT_CY, lk_v_i32(rects[n].y + inset));
    lk_state_set(cfg->state, nid, LKS_SPLIT_CW,
                 lk_v_i32(rects[n].w - inset * 2));
    lk_state_set(cfg->state, nid, LKS_SPLIT_CH,
                 lk_v_i32(rects[n].h - inset * 2));
  }
}

/* ---- Registration ---- */

lk_widget_def lk_split_h_widget_def(void) {
  lk_widget_def def;
  memset(&def, 0, sizeof(def));
  def.measure = measure_split_h;
  def.layout = layout_split_h;
  def.render = render_split_h;
  def.event = event_split_h;
  def.clips = 0;
  return def;
}

lk_widget_def lk_split_v_widget_def(void) {
  lk_widget_def def;
  memset(&def, 0, sizeof(def));
  def.measure = measure_split_v;
  def.layout = layout_split_v;
  def.render = render_split_v;
  def.event = event_split_v;
  def.clips = 0;
  return def;
}
