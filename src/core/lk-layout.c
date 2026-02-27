#include <string.h>

#include "lk-dropdown.h"
#include "lk-memory.h"
#include "lk-split.h"
#include "lk-tabs.h"
#include "lk-text-input.h"
#include <lk.h>

/* Hidden subtrees (UIP_HIDDEN on the root of the subtree) are skipped
 * by the main passes.  The subtree entry points (lk_layout_subtree)
 * ignore the flag on their start node only, so overlay content —
 * hidden from the main tree — can still be measured and laid out. */
static int node_hidden(const lk_tree *t, lk_ix n) {
  return lk_node_prop_bool(t, n, UIP_HIDDEN);
}

/* Post-order measure of the subtree rooted at start.  Hidden
 * descendants are skipped (their sizes stay zero); start itself is
 * always measured. */
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
        continue; /* skip hidden subtree */
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

/* Top-down layout of the subtree rooted at start.  rects[start] must
 * already hold the subtree root's final rect.  Hidden descendants are
 * not descended into (their rects are left as-is). */
lk_i32 lk_widget_fit_height(const lk_tree *t, lk_ix n, lk_i32 width,
                            const lk_size *sizes, const lk_layout_cfg *cfg) {
  const lk_widget_def *def;

  if (!t || n == 0 || n >= t->node_count || !sizes) {
    return 0;
  }

  /* An explicit height is final (measure_pass applied it to sizes). */
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

    /* An unselected TAB page is a leaf for this pass: no layout hook,
     * no descent (its subtree rects stay zero).  See lk-tabs.h. */
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

    /* Push children in reverse order for correct DFS traversal */
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

/* ---- Public API ---- */

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

  /* Per-frame geometry scratch: widgets stash derived geometry the
   * event handlers need but cannot compute (trigger rects, text
   * origin, split content rects).  NULL geom skips the stash; the
   * degraded behaviors are documented on lk_widget_geom. */
  if (cfg->geom) {
    lk_dropdown_store_trigger_rects(t, rects, cfg);
    lk_text_input_store_geometry(t, rects, cfg);
    lk_split_store_geometry(t, rects, cfg);
  }

  lk_sys_dealloc(NULL, sizes);
  return 1;
}

/* Zero the rect slots of every node in the subtree rooted at start
 * (including hidden descendants), so stale values from a previous
 * subtree layout can't leak through. */
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

  /* UIP_HIDDEN on subtree_root itself is deliberately ignored: the
   * whole point is laying out subtrees the main pass skipped. */
  measure_pass(t, cfg, sizes, subtree_root);

  rects[subtree_root].x = origin_x;
  rects[subtree_root].y = origin_y;
  rects[subtree_root].w = sizes[subtree_root].w;
  rects[subtree_root].h = sizes[subtree_root].h;

  layout_pass(t, cfg, sizes, rects, subtree_root);

  lk_sys_dealloc(NULL, sizes);
  return 1;
}
