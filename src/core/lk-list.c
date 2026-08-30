/*
 * lk-list.c -- UIK_LIST: the virtualized list (docs/table.md).
 *
 * A scroll container that positions by index instead of by stacking.
 * The application builds only the rows it wants shown, each carrying
 * its index in UIP_ROW; the list places them at row * row_h inside an
 * extent of UIP_ROWS * UIP_ROW_H pixels, scrolls that extent (wheel,
 * a draggable bar, keys), and reports the visible window through the
 * geometry scratch (geom->list) so the next frame can build exactly
 * those rows -- the frame callback is the row source.
 *
 * State: LKS_SCROLL_Y (px, the scroll contract), LKS_CURSOR_ROW (the
 * keyboard position, -1 = none; VALUE prop is the initial one,
 * CONTROLLED suppresses the write), LKS_LIST_DRAGGING (thumb drag).
 * Every cursor change enqueues VALUE_CHANGED with the row as a decimal
 * string.  The cursor is position, not selection: selection is the
 * application's, expressed as tags on the rows it builds.
 */

#include <stdio.h>
#include <string.h>

#include "lk-list.h"
#include "lk-memory.h"
#include <lk.h>

/* ---- helpers ---- */

static lk_i32 clamp(lk_i32 v, lk_i32 lo, lk_i32 hi) {
  if (v < lo) {
    return lo;
  }

  if (v > hi) {
    return hi;
  }

  return v;
}

static lk_i32 get_i32(const lk_state *st, lk_node_id nid, lk_state_key k,
                      lk_i32 dflt) {
  lk_value v;

  if (!st) {
    return dflt;
  }

  v = lk_state_get(st, nid, k);

  return v.tag == UIV_I32 ? (lk_i32)v.as.i : dflt;
}

static lk_i32 list_rows(const lk_tree *t, lk_ix n) {
  lk_i32 r = lk_node_prop_i32(t, n, UIP_ROWS, 0);

  return r < 0 ? 0 : r;
}

static lk_i32 list_row_h(const lk_tree *t, lk_ix n) {
  lk_i32 h = lk_node_prop_i32(t, n, UIP_ROW_H, LK_LIST_DEFAULT_ROW_H);

  return h < 1 ? 1 : h;
}

lk_i32 lk_list_cursor(const lk_tree *t, lk_ix n, const lk_state *state) {
  lk_i32 rows = list_rows(t, n);
  lk_i32 c;
  lk_value v;

  v = state ? lk_state_get(state, t->nodes[n].id, LKS_CURSOR_ROW)
            : lk_v_none();

  if (v.tag == UIV_I32) {
    c = (lk_i32)v.as.i;
  } else {
    c = lk_node_prop_i32(t, n, UIP_VALUE, -1);
  }

  if (c < -1) {
    c = -1;
  }

  if (c >= rows) {
    c = rows - 1;
  }

  return c;
}

static int inset_of(const lk_tree *t, lk_ix n, const lk_style *styles) {
  lk_i32 pad =
      styles ? styles[n].padding : lk_node_prop_i32(t, n, UIP_PADDING, 0);
  lk_i32 bw = styles ? styles[n].border_width : 0;

  return pad + bw;
}

/* ---- measure / layout ---- */

static void measure_list(const lk_tree *t, lk_ix n, const lk_size *sizes,
                         const lk_layout_cfg *cfg, lk_i32 *out_w,
                         lk_i32 *out_h) {
  const lk_node *nd = &t->nodes[n];
  lk_i32 inset = inset_of(t, n, cfg->styles);
  lk_i32 max_w = 0;
  lk_i32 rows = list_rows(t, n);
  lk_i32 floor_rows = rows < LK_LIST_FLOOR_ROWS ? rows : LK_LIST_FLOOR_ROWS;
  lk_ix ch = nd->first_child;

  while (ch) {
    if (!lk_node_prop_bool(t, ch, UIP_HIDDEN) && sizes[ch].w > max_w) {
      max_w = sizes[ch].w;
    }

    ch = t->nodes[ch].next_sibling;
  }

  *out_w = max_w + inset * 2;
  *out_h = floor_rows * list_row_h(t, n) + inset * 2;
}

static int layout_list(const lk_tree *t, lk_ix n, const lk_size *sizes,
                       const lk_rect *content, const lk_layout_cfg *cfg,
                       lk_rect *rects) {
  const lk_node *nd = &t->nodes[n];
  lk_node_id nid = nd->id;
  lk_i32 rows = list_rows(t, n);
  lk_i32 row_h = list_row_h(t, n);
  lk_i32 extent = rows * row_h;
  lk_i32 max = extent - content->h;
  lk_i32 scroll_y;
  lk_i32 avail_w = content->w;
  lk_ix ch;

  (void)sizes;

  if (max < 0) {
    max = 0;
  }

  scroll_y = clamp(get_i32(cfg->state, nid, LKS_SCROLL_Y, 0), 0, max);

  if (cfg->state) {
    lk_state_set(cfg->state, nid, LKS_SCROLL_Y, lk_v_i32(scroll_y));
  }

  if (max > 0) {
    avail_w -= LK_LIST_BAR_W;

    if (avail_w < 0) {
      avail_w = 0;
    }
  }

  ch = nd->first_child;

  while (ch) {
    lk_i32 r = lk_node_prop_i32(t, ch, UIP_ROW, -1);

    if (lk_node_prop_bool(t, ch, UIP_HIDDEN) || r < 0 || r >= rows) {
      memset(&rects[ch], 0, sizeof(rects[ch]));
    } else {
      rects[ch].x = content->x;
      rects[ch].y = content->y - scroll_y + r * row_h;
      rects[ch].w = avail_w;
      rects[ch].h = row_h;
    }

    ch = t->nodes[ch].next_sibling;
  }

  /* The visible window + the content rect, for the next frame's build
   * and for the event handlers (bar drag, ensure-visible). */
  if (cfg->geom) {
    lk_widget_geom *g = &cfg->geom[n];
    lk_i32 first = scroll_y / row_h;
    lk_i32 last = (scroll_y + content->h - 1) / row_h; /* inclusive */
    lk_i32 count;

    if (content->h <= 0 || rows == 0) {
      first = 0;
      count = 0;
    } else {
      if (last >= rows) {
        last = rows - 1;
      }

      count = last - first + 1;

      if (count < 0) {
        count = 0;
      }
    }

    g->list.first = first;
    g->list.count = count;
    g->list.row_h = row_h;
    g->list.max = max;
    g->list.x = content->x;
    g->list.y = content->y;
    g->list.w = content->w;
    g->list.h = content->h;
    g->list.placed = 1;
  }

  return 1;
}

int lk_list_range(const lk_ui *ui, lk_node_id id, lk_i32 *first,
                  lk_i32 *count) {
  const lk_tree *t;
  lk_ix n;

  if (!ui || !ui->geom) {
    return 0;
  }

  t = lk_ui_tree(ui);
  n = lk_tree_find_by_id(t, id);

  if (n == 0 || t->nodes[n].kind != UIK_LIST || (lk_u32)n >= ui->geom_cap ||
      !ui->geom[n].list.placed) {
    return 0;
  }

  if (first) {
    *first = ui->geom[n].list.first;
  }

  if (count) {
    *count = ui->geom[n].list.count;
  }

  return 1;
}

/* ---- render ---- */

static void render_list(const lk_tree *t, lk_ix n, const lk_rect *rect,
                        const lk_style *style, const lk_state *state,
                        const lk_widget_geom *geom, lk_render_list *out) {
  lk_node_id nid = t->nodes[n].id;
  lk_render_cmd cmd;
  lk_i32 max;

  memset(&cmd, 0, sizeof(cmd));
  cmd.op = LK_ROP_FILL_RECT;
  cmd.rect = *rect;
  cmd.color = style->bg;
  lk_render_list_push(out, cmd);

  max = geom ? geom->list.max : 0;

  if (max > 0 && geom) {
    lk_i32 scroll_y = clamp(get_i32(state, nid, LKS_SCROLL_Y, 0), 0, max);
    lk_i32 track_h = geom->list.h;
    lk_i32 total_h = geom->list.h + max;
    lk_i32 thumb_h, thumb_y;

    if (track_h <= 0 || total_h <= 0) {
      return;
    }

    thumb_h = (geom->list.h * track_h) / total_h;

    if (thumb_h < 8) {
      thumb_h = 8;
    }

    thumb_y = geom->list.y + (scroll_y * (track_h - thumb_h)) / max;

    memset(&cmd, 0, sizeof(cmd));
    cmd.op = LK_ROP_FILL_RECT;
    cmd.rect.x = geom->list.x + geom->list.w - LK_LIST_BAR_W;
    cmd.rect.y = geom->list.y;
    cmd.rect.w = LK_LIST_BAR_W;
    cmd.rect.h = track_h;
    cmd.color = style->scrollbar_track;
    lk_render_list_push(out, cmd);

    cmd.rect.y = thumb_y;
    cmd.rect.h = thumb_h;
    cmd.color = style->scrollbar_thumb;
    lk_render_list_push(out, cmd);
  }
}

/* ---- events ---- */

static void emit_cursor(lk_ui *ui, lk_ix n, lk_i32 row) {
  char buf[16];
  lk_event vev;

  sprintf(buf, "%d", (int)row);
  memset(&vev, 0, sizeof(vev));
  vev.type = LK_EVENT_VALUE_CHANGED;
  vev.target = n;
  vev.data.value_changed.str_id = lk_intern_cid(ui->intern, buf);
  lk_event_enqueue(ui, &vev);
}

static const lk_widget_geom *list_geom(const lk_ui *ui, lk_ix n) {
  if (!ui->geom || (lk_u32)n >= ui->geom_cap || !ui->geom[n].list.placed) {
    return NULL;
  }

  return &ui->geom[n];
}

/* Scroll so that `row` is inside the viewport (no-op when it is). */
static void ensure_visible(lk_ui *ui, const lk_tree *t, lk_ix n, lk_i32 row) {
  const lk_widget_geom *g = list_geom(ui, n);
  lk_state *st = lk_ui_state(ui);
  lk_node_id nid = t->nodes[n].id;
  lk_i32 scroll_y;
  lk_i32 top, bottom;

  if (!g || row < 0) {
    return;
  }

  scroll_y = clamp(get_i32(st, nid, LKS_SCROLL_Y, 0), 0, g->list.max);
  top = row * g->list.row_h;
  bottom = top + g->list.row_h;

  if (top < scroll_y) {
    scroll_y = top;
  } else if (bottom > scroll_y + g->list.h) {
    scroll_y = bottom - g->list.h;
  }

  lk_state_set(st, nid, LKS_SCROLL_Y,
               lk_v_i32(clamp(scroll_y, 0, g->list.max)));
}

int lk_list_scroll_to_row(lk_ui *ui, lk_node_id id, lk_i32 row) {
  const lk_tree *t;
  lk_ix n;

  if (!ui) {
    return 0;
  }

  t = lk_ui_tree(ui);
  n = lk_tree_find_by_id(t, id);

  if (n == 0 || t->nodes[n].kind != UIK_LIST || !list_geom(ui, n) ||
      row < 0 || row >= list_rows(t, n)) {
    return 0;
  }

  ensure_visible(ui, t, n, row);

  return 1;
}

/* Set the cursor if it changed: state write (unless controlled),
 * ensure visible, VALUE_CHANGED. */
static void cursor_set(lk_ui *ui, const lk_tree *t, lk_ix n, lk_i32 row) {
  lk_state *st = lk_ui_state(ui);
  lk_i32 rows = list_rows(t, n);
  lk_i32 cur = lk_list_cursor(t, n, st);

  if (rows == 0) {
    return;
  }

  row = clamp(row, 0, rows - 1);

  if (row == cur) {
    ensure_visible(ui, t, n, row);
    return;
  }

  if (st && !lk_node_prop_i32(t, n, UIP_CONTROLLED, 0)) {
    lk_state_set(st, t->nodes[n].id, LKS_CURSOR_ROW, lk_v_i32(row));
  }

  ensure_visible(ui, t, n, row);
  emit_cursor(ui, n, row);
}

/* The materialized row (direct child of n) on the path from target. */
static lk_ix row_child_of(const lk_tree *t, lk_ix n, lk_ix target) {
  lk_ix c = target;

  while (c != 0 && c < t->node_count && t->nodes[c].parent != n) {
    c = t->nodes[c].parent;
  }

  return (c != 0 && t->nodes[c].parent == n) ? c : 0;
}

/* Bar geometry from the stashed content rect. */
static int bar_geom(const lk_ui *ui, const lk_tree *t, lk_ix n,
                    lk_rect *track, lk_rect *thumb) {
  const lk_widget_geom *g = list_geom(ui, n);
  lk_i32 scroll_y, total_h, thumb_h;

  if (!g || g->list.max <= 0 || g->list.h <= 0) {
    return 0;
  }

  scroll_y = clamp(get_i32(lk_ui_state((lk_ui *)ui), t->nodes[n].id,
                           LKS_SCROLL_Y, 0),
                   0, g->list.max);
  total_h = g->list.h + g->list.max;
  thumb_h = (g->list.h * g->list.h) / total_h;

  if (thumb_h < 8) {
    thumb_h = 8;
  }

  track->x = g->list.x + g->list.w - LK_LIST_BAR_W;
  track->y = g->list.y;
  track->w = LK_LIST_BAR_W;
  track->h = g->list.h;
  *thumb = *track;
  thumb->y = g->list.y + (scroll_y * (g->list.h - thumb_h)) / g->list.max;
  thumb->h = thumb_h;

  return 1;
}

static int in_rect(const lk_rect *r, lk_i32 x, lk_i32 y) {
  return x >= r->x && y >= r->y && x < r->x + r->w && y < r->y + r->h;
}

static int event_list(lk_ui *ui, const lk_tree *t, lk_ix n, lk_event *ev) {
  lk_state *st = lk_ui_state(ui);
  lk_node_id nid = t->nodes[n].id;
  const lk_widget_geom *g;

  if (lk_node_prop_bool(t, n, UIP_DISABLED)) {
    return 0;
  }

  /* A primary click bubbling up from a materialized row moves the
   * cursor there and focuses the list -- and does NOT consume: the
   * row's own presentations / translators still fire. */
  if (ev->type == LK_EVENT_POINTER_DOWN && ev->target != n) {
    lk_ix row = row_child_of(t, n, ev->target);

    if (row != 0 && (ev->data.pointer.button == LK_POINTER_BUTTON_PRIMARY ||
                     ev->data.pointer.button == LK_POINTER_BUTTON_ANY)) {
      lk_i32 r = lk_node_prop_i32(t, row, UIP_ROW, -1);

      if (r >= 0) {
        if (lk_node_prop_bool(t, n, UIP_FOCUSABLE)) {
          lk_focus_set(ui, t, nid);
        }

        cursor_set(ui, t, n, r);
      }
    }

    return 0;
  }

  if (ev->type == LK_EVENT_WHEEL) {
    lk_i32 scroll_y;

    g = list_geom(ui, n);

    if (!g || g->list.max <= 0) {
      return 0;
    }

    scroll_y = get_i32(st, nid, LKS_SCROLL_Y, 0);
    scroll_y -= ev->data.wheel.dy * LK_LIST_STEP;
    lk_state_set(st, nid, LKS_SCROLL_Y,
                 lk_v_i32(clamp(scroll_y, 0, g->list.max)));

    return 1;
  }

  if (ev->target != n) {
    return 0;
  }

  /* The bar: thumb drag with capture, track click pages. */
  if (ev->type == LK_EVENT_POINTER_DOWN) {
    lk_rect track, thumb;

    if (ev->data.pointer.button != LK_POINTER_BUTTON_PRIMARY &&
        ev->data.pointer.button != LK_POINTER_BUTTON_ANY) {
      return 0;
    }

    if (bar_geom(ui, t, n, &track, &thumb) &&
        in_rect(&track, ev->data.pointer.x, ev->data.pointer.y)) {
      g = list_geom(ui, n);

      if (in_rect(&thumb, ev->data.pointer.x, ev->data.pointer.y)) {
        lk_capture_set(ui, nid);
        lk_state_set(st, nid, LKS_LIST_DRAGGING,
                     lk_v_i32(ev->data.pointer.y - thumb.y));
      } else {
        lk_i32 scroll_y = get_i32(st, nid, LKS_SCROLL_Y, 0);
        lk_i32 page = g->list.h;

        scroll_y += ev->data.pointer.y < thumb.y ? -page : page;
        lk_state_set(st, nid, LKS_SCROLL_Y,
                     lk_v_i32(clamp(scroll_y, 0, g->list.max)));
      }

      if (lk_node_prop_bool(t, n, UIP_FOCUSABLE)) {
        lk_focus_set(ui, t, nid);
      }

      return 1;
    }

    /* Empty space inside the list: take focus, nothing else. */
    if (lk_node_prop_bool(t, n, UIP_FOCUSABLE)) {
      lk_focus_set(ui, t, nid);
      return 1;
    }

    return 0;
  }

  if (ev->type == LK_EVENT_POINTER_MOVE) {
    lk_value dv = st ? lk_state_get(st, nid, LKS_LIST_DRAGGING) : lk_v_none();
    lk_rect track, thumb;

    if (dv.tag != UIV_I32 || lk_capture_current(ui) != nid) {
      return 0;
    }

    if (bar_geom(ui, t, n, &track, &thumb)) {
      lk_i32 travel = track.h - thumb.h;
      lk_i32 top = ev->data.pointer.y - (lk_i32)dv.as.i - track.y;

      g = list_geom(ui, n);

      if (travel > 0) {
        lk_i32 scroll_y = (lk_i32)((lk_i64)clamp(top, 0, travel) *
                                   (lk_i64)g->list.max / (lk_i64)travel);

        lk_state_set(st, nid, LKS_SCROLL_Y,
                     lk_v_i32(clamp(scroll_y, 0, g->list.max)));
      }
    }

    return 1;
  }

  if (ev->type == LK_EVENT_POINTER_UP) {
    lk_value dv = st ? lk_state_get(st, nid, LKS_LIST_DRAGGING) : lk_v_none();

    if (dv.tag == UIV_I32) {
      lk_state_set(st, nid, LKS_LIST_DRAGGING, lk_v_none());
      lk_capture_clear(ui);
      return 1;
    }

    return 0;
  }

  if (ev->type == LK_EVENT_KEY_DOWN) {
    lk_i32 rows = list_rows(t, n);
    lk_i32 cur = lk_list_cursor(t, n, st);
    lk_i32 page;

    g = list_geom(ui, n);
    page = g ? g->list.h / g->list.row_h : 1;

    if (page < 1) {
      page = 1;
    }

    if (rows == 0) {
      return 0;
    }

    switch (ev->data.key.keycode) {
    case LKK_DOWN:
      cursor_set(ui, t, n, cur < 0 ? 0 : cur + 1);
      return 1;
    case LKK_UP:
      cursor_set(ui, t, n, cur < 0 ? 0 : cur - 1);
      return 1;
    case LKK_PAGEDOWN:
      cursor_set(ui, t, n, cur < 0 ? page - 1 : cur + page);
      return 1;
    case LKK_PAGEUP:
      cursor_set(ui, t, n, cur < 0 ? 0 : cur - page);
      return 1;
    case LKK_HOME:
      cursor_set(ui, t, n, 0);
      return 1;
    case LKK_END:
      cursor_set(ui, t, n, rows - 1);
      return 1;
    default:
      return 0;
    }
  }

  return 0;
}

/* ---- registration ---- */

lk_widget_def lk_list_widget_def(void) {
  lk_widget_def def;

  memset(&def, 0, sizeof(def));
  def.measure = measure_list;
  def.layout = layout_list;
  def.render = render_list;
  def.event = event_list;
  def.clips = 1;

  return def;
}
