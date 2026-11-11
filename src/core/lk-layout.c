#include <string.h>

#include "lk-dropdown.h"
#include "lk-memory.h"
#include "lk-split.h"
#include "lk-tabs.h"
#include "lk-text-input.h"
#include <lk.h>

static int node_hidden(const lk_tree *t, lk_ix n) {
  return lk_node_prop_bool(t, n, UIP_HIDDEN);
}

static void measure_pass(const lk_tree *t, const lk_layout_cfg *cfg,
                         lk_size *sizes, lk_ix start) {
  lk_ix *stack;
  lk_ix *iter;
  lk_u32 sp;

  stack = (lk_ix *)lk_sys_alloc(NULL, (lk_u32)(sizeof(lk_ix) * t->node_count));
  iter = (lk_ix *)lk_sys_alloc(NULL, (lk_u32)(sizeof(lk_ix) * t->node_count));

  if (!stack || !iter) {
    if (stack) {
      lk_sys_dealloc(NULL, stack);
    }

    if (iter) {
      lk_sys_dealloc(NULL, iter);
    }

    return;
  }

  sp = 0;
  stack[sp] = start;
  iter[sp] = t->nodes[start].first_child;
  sp++;

  while (sp > 0) {
    lk_ix n = stack[sp - 1];
    lk_ix c = iter[sp - 1];

    if (c != 0) {
      iter[sp - 1] = t->nodes[c].next_sibling;

      if (node_hidden(t, c)) {
        continue;
      }

      stack[sp] = c;
      iter[sp] = t->nodes[c].first_child;
      sp++;
    } else {
      const lk_node *nd = &t->nodes[n];
      lk_kind kind = (lk_kind)nd->kind;
      const lk_widget_def *def = lk_widget_get(kind);
      lk_i32 sw = 0;
      lk_i32 sh = 0;

      if (def && def->measure) {
        def->measure(t, n, sizes, cfg, &sw, &sh);
      }

      if (lk_node_has_prop(t, n, UIP_W)) {
        sw = lk_node_prop_i32(t, n, UIP_W, sw);
      }

      if (lk_node_has_prop(t, n, UIP_H)) {
        sh = lk_node_prop_i32(t, n, UIP_H, sh);
      }

      sizes[n].w = sw;
      sizes[n].h = sh;
      sp--;
    }
  }

  lk_sys_dealloc(NULL, stack);
  lk_sys_dealloc(NULL, iter);
}

lk_i32 lk_widget_fit_height(const lk_tree *t, lk_ix n, lk_i32 width,
                            const lk_size *sizes, const lk_layout_cfg *cfg) {
  const lk_widget_def *def;

  if (!t || n == 0 || n >= t->node_count || !sizes) {
    return 0;
  }

  if (lk_node_has_prop(t, n, UIP_H)) {
    return sizes[n].h;
  }

  def = lk_widget_get((lk_kind)t->nodes[n].kind);

  if (def && def->fit_height) {
    return def->fit_height(t, n, width, sizes, cfg);
  }

  return sizes[n].h;
}

static void layout_pass(const lk_tree *t, const lk_layout_cfg *cfg,
                        const lk_size *sizes, lk_rect *rects, lk_ix start) {
  lk_ix *stack;
  lk_u32 sp;

  stack = (lk_ix *)lk_sys_alloc(NULL, (lk_u32)(sizeof(lk_ix) * t->node_count));

  if (!stack) {
    return;
  }

  sp = 0;
  stack[sp++] = start;

  while (sp > 0) {
    lk_ix n = stack[--sp];
    const lk_node *nd = &t->nodes[n];
    lk_kind kind = (lk_kind)nd->kind;
    const lk_widget_def *def = lk_widget_get(kind);
    lk_i32 pad;
    lk_i32 bw;
    lk_i32 inset;
    lk_rect content;
    lk_ix child;
    int child_count;

    if (!def || !def->layout) {
      continue;
    }

    if (lk_tabs_collapsed(t, n, cfg->state)) {
      continue;
    }

    pad = cfg->styles ? cfg->styles[n].padding
                      : lk_node_prop_i32(t, n, UIP_PADDING, 0);
    bw = cfg->styles ? cfg->styles[n].border_width : 0;
    inset = pad + bw;

    content.x = rects[n].x + inset;
    content.y = rects[n].y + inset;
    content.w = rects[n].w - inset * 2;
    content.h = rects[n].h - inset * 2;

    def->layout(t, n, sizes, &content, cfg, rects);

    child_count = 0;
    child = nd->first_child;

    while (child) {
      child_count++;
      child = t->nodes[child].next_sibling;
    }

    {
      lk_ix *kids;
      int nk = 0;
      kids = (lk_ix *)lk_sys_alloc(
          NULL, (lk_u32)(sizeof(lk_ix) * (child_count > 0 ? child_count : 1)));

      if (kids) {
        child = nd->first_child;

        while (child) {
          if (!node_hidden(t, child)) {
            kids[nk++] = child;
          }

          child = t->nodes[child].next_sibling;
        }

        while (nk > 0) {
          stack[sp++] = kids[--nk];
        }

        lk_sys_dealloc(NULL, kids);
      }
    }
  }

  lk_sys_dealloc(NULL, stack);
}

static lk_i32 snap_edge(lk_i32 v, lk_i32 g) {
  lk_i32 q = v + g / 2;

  if (q >= 0) {
    return (q / g) * g;
  }

  return -(((-q) + g - 1) / g) * g;
}

void lk_rect_snap_grid(lk_rect *r, lk_i32 grid_w, lk_i32 grid_h) {
  if (grid_w > 0) {
    lk_i32 x0 = snap_edge(r->x, grid_w);
    lk_i32 x1 = snap_edge(r->x + r->w, grid_w);

    r->x = x0;
    r->w = x1 > x0 ? x1 - x0 : 0;
  }

  if (grid_h > 0) {
    lk_i32 y0 = snap_edge(r->y, grid_h);
    lk_i32 y1 = snap_edge(r->y + r->h, grid_h);

    r->y = y0;
    r->h = y1 > y0 ? y1 - y0 : 0;
  }
}

static void snap_rects(const lk_tree *t, const lk_layout_cfg *cfg,
                       lk_rect *rects) {
  lk_u32 i;

  if (cfg->grid_w <= 0 && cfg->grid_h <= 0) {
    return;
  }

  for (i = 0; i < t->node_count; i++) {
    if (rects[i].w > 0 || rects[i].h > 0) {
      lk_rect_snap_grid(&rects[i], cfg->grid_w, cfg->grid_h);
    }
  }
}

int lk_layout(const lk_tree *t, const lk_layout_cfg *cfg, lk_rect *rects) {
  lk_size *sizes;

  if (!t || !cfg || !rects) {
    return 0;
  }

  if (t->root == 0 || t->root >= t->node_count) {
    return 0;
  }

  memset(rects, 0, sizeof(lk_rect) * t->node_count);

  if (cfg->geom) {
    memset(cfg->geom, 0, sizeof(lk_widget_geom) * t->node_count);
  }

  sizes =
      (lk_size *)lk_sys_alloc(NULL, (lk_u32)(sizeof(lk_size) * t->node_count));

  if (!sizes) {
    return 0;
  }

  memset(sizes, 0, sizeof(lk_size) * t->node_count);

  measure_pass(t, cfg, sizes, t->root);

  rects[t->root].x = 0;
  rects[t->root].y = 0;
  rects[t->root].w = cfg->viewport_w;
  rects[t->root].h = cfg->viewport_h;

  layout_pass(t, cfg, sizes, rects, t->root);
  snap_rects(t, cfg, rects);

  if (cfg->geom) {
    lk_dropdown_store_trigger_rects(t, rects, cfg);
    lk_text_input_store_geometry(t, rects, cfg);
    lk_split_store_geometry(t, rects, cfg);
  }

  lk_sys_dealloc(NULL, sizes);
  return 1;
}

static void zero_subtree_rects(const lk_tree *t, lk_ix start, lk_rect *rects) {
  lk_ix *stack;
  lk_u32 sp;

  stack = (lk_ix *)lk_sys_alloc(NULL, (lk_u32)(sizeof(lk_ix) * t->node_count));

  if (!stack) {
    return;
  }

  sp = 0;
  stack[sp++] = start;

  while (sp > 0) {
    lk_ix n = stack[--sp];
    lk_ix child = t->nodes[n].first_child;

    rects[n].x = 0;
    rects[n].y = 0;
    rects[n].w = 0;
    rects[n].h = 0;

    while (child) {
      stack[sp++] = child;
      child = t->nodes[child].next_sibling;
    }
  }

  lk_sys_dealloc(NULL, stack);
}

int lk_layout_subtree(const lk_tree *t, const lk_layout_cfg *cfg,
                      lk_ix subtree_root, lk_i32 origin_x, lk_i32 origin_y,
                      lk_rect *rects) {
  lk_size *sizes;

  if (!t || !cfg || !rects) {
    return 0;
  }

  if (subtree_root == 0 || subtree_root >= t->node_count) {
    return 0;
  }

  sizes =
      (lk_size *)lk_sys_alloc(NULL, (lk_u32)(sizeof(lk_size) * t->node_count));

  if (!sizes) {
    return 0;
  }

  memset(sizes, 0, sizeof(lk_size) * t->node_count);
  zero_subtree_rects(t, subtree_root, rects);

  measure_pass(t, cfg, sizes, subtree_root);

  rects[subtree_root].x = origin_x;
  rects[subtree_root].y = origin_y;
  rects[subtree_root].w = sizes[subtree_root].w;
  rects[subtree_root].h = sizes[subtree_root].h;

  layout_pass(t, cfg, sizes, rects, subtree_root);
  snap_rects(t, cfg, rects);

  lk_sys_dealloc(NULL, sizes);
  return 1;
}
