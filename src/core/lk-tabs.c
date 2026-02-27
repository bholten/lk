/*
 * lk-tabs.c -- UIK_TABS + UIK_TAB (docs/forms-widgets.md).
 *
 * A TABS node owns a header strip (like the dropdown owns its
 * trigger) and shows exactly one of its TAB children below it.
 *
 * Selection: LKS_SELECTED_INDEX state (clicks / arrow keys) >
 * UIP_VALUE (the id string of a TAB child) > 0, clamped to the count
 * of visible (non-UIP_HIDDEN) TAB children.  Selecting enqueues
 * LK_EVENT_VALUE_CHANGED whose payload is the selected TAB's node id
 * (already an intern id -- ids and prop strings share the table);
 * UIP_CONTROLLED nonzero suppresses the state write and the app
 * re-supplies UIP_VALUE each frame.
 *
 * Geometry (all in the layout hook, stashed in cfg->geom because
 * render and events see neither rects nor a text backend):
 *   - every visible TAB gets a header cell in the strip (text width +
 *     2*HDR_PAD_X by max text height + 2*HDR_PAD_Y, cells separated
 *     by style->gap), recorded in geom[tab].header;
 *   - the SELECTED tab's rect is the page (the content area below the
 *     strip and its 1 px separator); its children lay out as a column
 *     inside it (measure/layout come from lk-widget.c);
 *   - every OTHER tab's rect is its header cell and it is a leaf: the
 *     engine skips its layout hook and never descends into it
 *     (lk_tabs_collapsed), so its subtree rects stay zero and it is
 *     unreachable by hit-testing / focus traversal.  Hit-testing a
 *     header cell therefore lands on that TAB node (hover styling
 *     works per tab), and POINTER_DOWN there selects it.
 *   - geom[tabs].strip.h = strip height incl. separator.
 * Measure: max(strip width, widest page) by strip height + tallest
 * page -- unselected pages ARE measured, so switching tabs never
 * resizes the area.
 *
 * Render: TABS paints the strip bg (style->bg) and the separator
 * (style->border_color).  A TAB paints its header: text in fg, and
 * when selected a bg fill (page color, visually joining header and
 * page) plus a 2 px style->accent bar along the top; the selected TAB
 * also fills its page bg.  Engine borders behave as usual.
 *
 * Events: POINTER_DOWN (primary/any) on an unselected TAB selects it
 * (and focuses the TABS if focusable).  When the TABS is focused,
 * LEFT/RIGHT step the selection, HOME/END jump.  Clicks anywhere on
 * the selected page bubble as usual (the page IS the TAB's rect).
 */

#include <string.h>

#include "lk-memory.h"
#include "lk-tabs.h"
#include <lk.h>

#define HDR_PAD_X 10
#define HDR_PAD_Y 5
#define HDR_MIN_H 12
#define SEP_H 1
#define ACCENT_H 2

/* ---- Helpers ---- */

static int is_visible_tab(const lk_tree *t, lk_ix c) {
  return (lk_kind)t->nodes[c].kind == UIK_TAB &&
         !lk_node_prop_bool(t, c, UIP_HIDDEN);
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

lk_ix lk_tabs_selected(const lk_tree *t, lk_ix n, const lk_state *state,
                       int *out_index) {
  lk_ix c;
  int count = 0;
  int want = -1;
  int i;

  if (out_index) {
    *out_index = -1;
  }

  if (!t || n == 0 || (lk_kind)t->nodes[n].kind != UIK_TABS) {
    return 0;
  }

  c = t->nodes[n].first_child;

  while (c) {
    if (is_visible_tab(t, c)) {
      count++;
    }

    c = t->nodes[c].next_sibling;
  }

  if (count == 0) {
    return 0;
  }

  if (state) {
    lk_value v = lk_state_get(state, t->nodes[n].id, LKS_SELECTED_INDEX);

    if (v.tag == UIV_I32) {
      want = (int)v.as.i;
    }
  }

  if (want < 0) {
    lk_u32 vid = value_prop_id(t, n);

    want = 0;

    if (vid) {
      i = 0;
      c = t->nodes[n].first_child;

      while (c) {
        if (is_visible_tab(t, c)) {
          if (t->nodes[c].id == vid) {
            want = i;
            break;
          }

          i++;
        }

        c = t->nodes[c].next_sibling;
      }
    }
  }

  if (want >= count) {
    want = count - 1;
  }

  i = 0;
  c = t->nodes[n].first_child;

  while (c) {
    if (is_visible_tab(t, c)) {
      if (i == want) {
        if (out_index) {
          *out_index = i;
        }

        return c;
      }

      i++;
    }

    c = t->nodes[c].next_sibling;
  }

  return 0;
}

int lk_tabs_collapsed(const lk_tree *t, lk_ix n, const lk_state *state) {
  lk_ix parent;

  if (!t || n == 0) {
    return 0;
  }

  parent = t->nodes[n].parent;

  if (parent == 0 || (lk_kind)t->nodes[parent].kind != UIK_TABS) {
    return 0;
  }

  return lk_tabs_selected(t, parent, state, NULL) != n;
}

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

/* Strip metrics: total header width (cells + gaps) and strip height
 * (max cell height + separator).  Optionally writes each visible tab's
 * header cell at (x0, y0) into geom. */
static void strip_metrics(const lk_tree *t, lk_ix n, const lk_layout_cfg *cfg,
                          lk_i32 x0, lk_i32 y0, lk_widget_geom *geom,
                          lk_i32 *out_w, lk_i32 *out_h) {
  lk_i32 gap =
      cfg->styles ? cfg->styles[n].gap : lk_node_prop_i32(t, n, UIP_GAP, 0);
  lk_ix c = t->nodes[n].first_child;
  lk_i32 x = x0;
  lk_i32 max_h = HDR_MIN_H;
  int count = 0;

  /* pass 1: heights */
  while (c) {
    if (is_visible_tab(t, c)) {
      lk_i32 tw, th;
      measure_text(cfg, c, lk_node_text(t, c), &tw, &th);

      if (th + HDR_PAD_Y * 2 > max_h) {
        max_h = th + HDR_PAD_Y * 2;
      }
    }

    c = t->nodes[c].next_sibling;
  }

  /* pass 2: cells */
  c = t->nodes[n].first_child;

  while (c) {
    if (is_visible_tab(t, c)) {
      lk_i32 tw, th, cw;
      measure_text(cfg, c, lk_node_text(t, c), &tw, &th);
      cw = tw + HDR_PAD_X * 2;

      if (count > 0) {
        x += gap;
      }

      if (geom) {
        geom[c].header.x = x;
        geom[c].header.y = y0;
        geom[c].header.w = cw;
        geom[c].header.h = max_h;
      }

      x += cw;
      count++;
    }

    c = t->nodes[c].next_sibling;
  }

  *out_w = x - x0;
  *out_h = max_h + SEP_H;
}

/* ---- Measure ---- */

static void measure_tabs(const lk_tree *t, lk_ix n, const lk_size *sizes,
                         const lk_layout_cfg *cfg, lk_i32 *out_w,
                         lk_i32 *out_h) {
  lk_i32 pad = cfg->styles ? cfg->styles[n].padding
                           : lk_node_prop_i32(t, n, UIP_PADDING, 0);
  lk_i32 bw = cfg->styles ? cfg->styles[n].border_width : 0;
  lk_i32 inset = pad + bw;
  lk_i32 sw, sh;
  lk_i32 page_w = 0;
  lk_i32 page_h = 0;
  lk_ix c = t->nodes[n].first_child;

  strip_metrics(t, n, cfg, 0, 0, NULL, &sw, &sh);

  while (c) {
    if (is_visible_tab(t, c)) {
      if (sizes[c].w > page_w) {
        page_w = sizes[c].w;
      }

      if (sizes[c].h > page_h) {
        page_h = sizes[c].h;
      }
    }

    c = t->nodes[c].next_sibling;
  }

  *out_w = (sw > page_w ? sw : page_w) + inset * 2;
  *out_h = sh + page_h + inset * 2;
}

/* ---- Layout ---- */

static int layout_tabs(const lk_tree *t, lk_ix n, const lk_size *sizes,
                       const lk_rect *content, const lk_layout_cfg *cfg,
                       lk_rect *rects) {
  lk_i32 sw, sh;
  lk_ix sel = lk_tabs_selected(t, n, cfg->state, NULL);
  lk_ix c;

  (void)sizes;
  strip_metrics(t, n, cfg, content->x, content->y, cfg->geom, &sw, &sh);

  if (cfg->geom) {
    cfg->geom[n].strip.h = sh;
  }

  c = t->nodes[n].first_child;

  while (c) {
    if (c == sel) {
      rects[c].x = content->x;
      rects[c].y = content->y + sh;
      rects[c].w = content->w;
      rects[c].h = content->h - sh;

      if (rects[c].h < 0) {
        rects[c].h = 0;
      }
    } else if (is_visible_tab(t, c) && cfg->geom) {
      /* collapsed tab: its rect IS its header cell (hit-testable) */
      rects[c].x = cfg->geom[c].header.x;
      rects[c].y = cfg->geom[c].header.y;
      rects[c].w = cfg->geom[c].header.w;
      rects[c].h = cfg->geom[c].header.h;
    } else {
      rects[c].x = 0;
      rects[c].y = 0;
      rects[c].w = 0;
      rects[c].h = 0;
    }

    c = t->nodes[c].next_sibling;
  }

  return 1;
}

/* ---- Render ---- */

static void push_fill(lk_render_list *out, lk_i32 x, lk_i32 y, lk_i32 w,
                      lk_i32 h, lk_color c) {
  lk_render_cmd cmd;

  if (w <= 0 || h <= 0) {
    return;
  }

  memset(&cmd, 0, sizeof(cmd));
  cmd.op = LK_ROP_FILL_RECT;
  cmd.rect.x = x;
  cmd.rect.y = y;
  cmd.rect.w = w;
  cmd.rect.h = h;
  cmd.color = c;
  lk_render_list_push(out, cmd);
}

static void render_tabs(const lk_tree *t, lk_ix n, const lk_rect *rect,
                        const lk_style *style, const lk_state *state,
                        const lk_widget_geom *geom, lk_render_list *out) {
  lk_i32 inset = style->padding + style->border_width;
  lk_i32 sh = geom ? geom->strip.h : 0;

  (void)t;
  (void)n;
  (void)state;

  if (sh <= 0) {
    return;
  }

  if (style->bg.a > 0) {
    push_fill(out, rect->x + inset, rect->y + inset, rect->w - inset * 2,
              sh - SEP_H, style->bg);
  }

  push_fill(out, rect->x + inset, rect->y + inset + sh - SEP_H,
            rect->w - inset * 2, SEP_H, style->border_color);
}

static void render_tab(const lk_tree *t, lk_ix n, const lk_rect *rect,
                       const lk_style *style, const lk_state *state,
                       const lk_widget_geom *geom, lk_render_list *out) {
  int selected = !lk_tabs_collapsed(t, n, state);
  lk_rect hdr;
  lk_u32 sid = lk_node_text_id(t, n);

  if (selected) {
    if (!geom || geom->header.w <= 0) {
      /* no geometry stashed: page bg only */
      if (style->bg.a > 0) {
        push_fill(out, rect->x, rect->y, rect->w, rect->h, style->bg);
      }

      return;
    }

    hdr.x = geom->header.x;
    hdr.y = geom->header.y;
    hdr.w = geom->header.w;
    hdr.h = geom->header.h;

    /* page bg, then the header joins it in the same color */
    if (style->bg.a > 0) {
      push_fill(out, rect->x, rect->y, rect->w, rect->h, style->bg);
      push_fill(out, hdr.x, hdr.y, hdr.w, hdr.h + SEP_H, style->bg);
    }

    push_fill(out, hdr.x, hdr.y, hdr.w, ACCENT_H, style->accent);
  } else {
    hdr = *rect;
  }

  if (sid != 0 && hdr.w > 0) {
    lk_render_cmd cmd;

    memset(&cmd, 0, sizeof(cmd));
    cmd.op = LK_ROP_DRAW_TEXT;
    cmd.rect.x = hdr.x + HDR_PAD_X;
    cmd.rect.y = hdr.y + HDR_PAD_Y;
    cmd.rect.w = hdr.w - HDR_PAD_X * 2;
    cmd.rect.h = hdr.h - HDR_PAD_Y * 2;
    cmd.color = style->fg;
    cmd.str_id = sid;
    cmd.font_id = (lk_u16)style->font_id;
    cmd.font_size = (lk_u16)style->font_size;
    lk_render_list_push(out, cmd);
  }
}

/* ---- Events ---- */

/* Select the visible TAB child at index idx of TABS node n. */
static void tabs_select(lk_ui *ui, const lk_tree *t, lk_ix n, int idx) {
  lk_state *st = lk_ui_state(ui);
  lk_ix c = t->nodes[n].first_child;
  lk_ix target = 0;
  int i = 0;
  int cur_idx = -1;
  lk_event vev;

  lk_tabs_selected(t, n, st, &cur_idx);

  while (c) {
    if (is_visible_tab(t, c)) {
      if (i == idx) {
        target = c;
        break;
      }

      i++;
    }

    c = t->nodes[c].next_sibling;
  }

  if (!target || idx == cur_idx) {
    return;
  }

  if (st && !lk_node_prop_i32(t, n, UIP_CONTROLLED, 0)) {
    lk_state_set(st, t->nodes[n].id, LKS_SELECTED_INDEX, lk_v_i32(idx));
  }

  memset(&vev, 0, sizeof(vev));
  vev.type = LK_EVENT_VALUE_CHANGED;
  vev.target = n;
  vev.data.value_changed.str_id = t->nodes[target].id;
  lk_event_enqueue(ui, &vev);
}

static int visible_tab_count(const lk_tree *t, lk_ix n) {
  lk_ix c = t->nodes[n].first_child;
  int count = 0;

  while (c) {
    if (is_visible_tab(t, c)) {
      count++;
    }

    c = t->nodes[c].next_sibling;
  }

  return count;
}

static int event_tabs(lk_ui *ui, const lk_tree *t, lk_ix n, lk_event *ev) {
  int idx;
  int count;
  int next;

  if (ev->type != LK_EVENT_KEY_DOWN || ev->mods != 0) {
    return 0;
  }

  if (lk_node_prop_bool(t, n, UIP_DISABLED)) {
    return 0;
  }

  count = visible_tab_count(t, n);

  if (count == 0) {
    return 0;
  }

  lk_tabs_selected(t, n, lk_ui_state(ui), &idx);
  next = idx;

  switch (ev->data.key.keycode) {
  case LKK_LEFT: next = idx - 1; break;
  case LKK_RIGHT: next = idx + 1; break;
  case LKK_HOME: next = 0; break;
  case LKK_END: next = count - 1; break;
  default: return 0;
  }

  if (next < 0) {
    next = 0;
  }

  if (next >= count) {
    next = count - 1;
  }

  tabs_select(ui, t, n, next);

  return 1;
}

static int event_tab(lk_ui *ui, const lk_tree *t, lk_ix n, lk_event *ev) {
  lk_ix parent = t->nodes[n].parent;
  lk_ix c;
  int idx = 0;
  lk_u8 b;

  if (ev->type != LK_EVENT_POINTER_DOWN) {
    return 0;
  }

  b = ev->data.pointer.button;

  if (b != LK_POINTER_BUTTON_ANY && b != LK_POINTER_BUTTON_PRIMARY) {
    return 0;
  }

  if (parent == 0 || (lk_kind)t->nodes[parent].kind != UIK_TABS) {
    return 0;
  }

  if (!lk_tabs_collapsed(t, n, lk_ui_state(ui))) {
    return 0; /* click inside the open page: not ours */
  }

  if (lk_node_prop_bool(t, parent, UIP_DISABLED) ||
      lk_node_prop_bool(t, n, UIP_DISABLED)) {
    return 0;
  }

  c = t->nodes[parent].first_child;

  while (c && c != n) {
    if (is_visible_tab(t, c)) {
      idx++;
    }

    c = t->nodes[c].next_sibling;
  }

  lk_focus_set(ui, t, t->nodes[parent].id);
  tabs_select(ui, t, parent, idx);

  return 1;
}

/* ---- Registration ---- */

lk_widget_def lk_tabs_widget_def(void) {
  lk_widget_def def;
  memset(&def, 0, sizeof(def));
  def.measure = measure_tabs;
  def.layout = layout_tabs;
  def.render = render_tabs;
  def.event = event_tabs;
  def.clips = 0;
  return def;
}

lk_widget_def lk_tab_widget_def(void) {
  lk_widget_def def;
  memset(&def, 0, sizeof(def));
  def.measure = 0; /* filled by lk-widget.c (column) */
  def.layout = 0;  /* filled by lk-widget.c (column) */
  def.render = render_tab;
  def.event = event_tab;
  def.clips = 0;
  return def;
}
