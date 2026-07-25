#include <string.h>

#include "lk-dropdown.h"
#include "lk-memory.h"
#include <lk.h>

static void measure_pass(const lk_tree *t, const lk_layout_cfg *cfg,
                         lk_size *sizes) {
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
  stack[sp] = t->root;
  iter[sp] = t->nodes[t->root].first_child;
  sp++;

  while (sp > 0) {
    lk_ix n = stack[sp - 1];
    lk_ix c = iter[sp - 1];

    if (c != 0) {
      iter[sp - 1] = t->nodes[c].next_sibling;
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

static void layout_pass(const lk_tree *t, const lk_layout_cfg *cfg,
                        const lk_size *sizes, lk_rect *rects) {
  lk_ix *stack;
  lk_u32 sp;

  stack = (lk_ix *)lk_sys_alloc(NULL, (lk_u32)(sizeof(lk_ix) * t->node_count));

  if (!stack) {
    return;
  }

  sp = 0;
  stack[sp++] = t->root;

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
          kids[nk++] = child;
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

  sizes =
      (lk_size *)lk_sys_alloc(NULL, (lk_u32)(sizeof(lk_size) * t->node_count));

  if (!sizes) {
    return 0;
  }

  memset(sizes, 0, sizeof(lk_size) * t->node_count);

  measure_pass(t, cfg, sizes);

  rects[t->root].x = 0;
  rects[t->root].y = 0;
  rects[t->root].w = cfg->viewport_w;
  rects[t->root].h = cfg->viewport_h;

  layout_pass(t, cfg, sizes, rects);

  /* Lean overlay support: stash dropdown trigger rects in retained
   * state so dropdown event handling can reason about geometry.
   * See docs/overlays.md. */
  if (cfg->state) {
    lk_dropdown_store_trigger_rects(t, rects, cfg->state);
  }

  lk_sys_dealloc(NULL, sizes);
  return 1;
}
