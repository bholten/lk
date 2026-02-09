#include <string.h>

#include "lk-data.h"
#include "lk-memory.h"

void lk_measure_text_stub(void *ud, lk_str text, lk_i32 *out_w, lk_i32 *out_h) {
  (void)ud;

  if (out_w) {
    *out_w = (lk_i32)text.len * 8;
  }

  if (out_h) {
    *out_h = 16;
  }
}

static lk_i32 node_prop_i32(const lk_tree *t, lk_ix n, lk_prop_key key,
                            lk_i32 def) {
  const lk_node *nd = &t->nodes[n];
  lk_u32 i;

  for (i = 0; i < nd->props_len; i++) {
    const lk_prop *p = &t->props[nd->props_off + i];

    if ((lk_prop_key)p->key == key && p->value.tag == UIV_I32) {
      return (lk_i32)p->value.as.i;
    }
  }

  return def;
}

static int node_has_prop(const lk_tree *t, lk_ix n, lk_prop_key key) {
  const lk_node *nd = &t->nodes[n];
  lk_u32 i;

  for (i = 0; i < nd->props_len; i++) {
    if ((lk_prop_key)t->props[nd->props_off + i].key == key) {
      return 1;
    }
  }

  return 0;
}

static lk_str node_text(const lk_tree *t, lk_ix n) {
  const lk_node *nd = &t->nodes[n];
  lk_str empty;
  lk_u32 i;

  empty.ptr = "";
  empty.len = 0;

  for (i = 0; i < nd->props_len; i++) {
    const lk_prop *p = &t->props[nd->props_off + i];

    if ((lk_prop_key)p->key == UIP_TEXT && p->value.tag == UIV_STR) {
      return lk_intern_str(t->intern, p->value.as.str_id);
    }
  }

  return empty;
}

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
      lk_i32 pad = node_prop_i32(t, n, UIP_PADDING, 0);
      lk_i32 gap = node_prop_i32(t, n, UIP_GAP, 0);
      lk_i32 sw = 0;
      lk_i32 sh = 0;

      switch (kind) {
      case UIK_WINDOW:
        sw = 0;
        sh = 0;
        break;

      case UIK_COLUMN: {
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

        sw = max_w + pad * 2;
        sh = sum_h + pad * 2;
        break;
      }

      case UIK_ROW: {
        lk_ix ch = nd->first_child;
        lk_i32 sum_w = 0;
        lk_i32 max_h = 0;
        int count = 0;

        while (ch) {
          sum_w += sizes[ch].w;

          if (sizes[ch].h > max_h) {
            max_h = sizes[ch].h;
          }

          count++;
          ch = t->nodes[ch].next_sibling;
        }

        if (count > 1) {
          sum_w += gap * (count - 1);
        }

        sw = sum_w + pad * 2;
        sh = max_h + pad * 2;
        break;
      }

      case UIK_SPACER: {
        sw = node_prop_i32(t, n, UIP_W, 0);
        sh = node_prop_i32(t, n, UIP_H, 0);
        break;
      }

      case UIK_LABEL: {
        lk_str text = node_text(t, n);
        lk_i32 tw = 0;
        lk_i32 th = 0;
        cfg->measure_text(cfg->measure_ud, text, &tw, &th);
        sw = tw;
        sh = th;
        break;
      }

      case UIK_BUTTON: {
        lk_str text = node_text(t, n);
        lk_i32 tw = 0;
        lk_i32 th = 0;
        cfg->measure_text(cfg->measure_ud, text, &tw, &th);
        sw = tw + pad * 2;
        sh = th + pad * 2;
        break;
      }

      default:
        sw = 0;
        sh = 0;
        break;
      }

      if (node_has_prop(t, n, UIP_W)) {
        sw = node_prop_i32(t, n, UIP_W, sw);
      }

      if (node_has_prop(t, n, UIP_H)) {
        sh = node_prop_i32(t, n, UIP_H, sh);
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
  (void)cfg;

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
    lk_i32 pad;
    lk_i32 gap;
    lk_i32 cx;
    lk_i32 cy;
    lk_i32 cw;
    lk_i32 ch;
    lk_ix child;

    if (kind != UIK_WINDOW && kind != UIK_COLUMN && kind != UIK_ROW) {
      continue;
    }

    pad = node_prop_i32(t, n, UIP_PADDING, 0);
    gap = node_prop_i32(t, n, UIP_GAP, 0);

    cx = rects[n].x + pad;
    cy = rects[n].y + pad;
    cw = rects[n].w - pad * 2;
    ch = rects[n].h - pad * 2;

    if (kind == UIK_WINDOW) {
      int child_count = 0;
      child = nd->first_child;

      while (child) {
        rects[child].x = cx;
        rects[child].y = cy;
        rects[child].w = cw;
        rects[child].h = ch;
        child_count++;
        child = t->nodes[child].next_sibling;
      }

      {
        lk_ix *kids;
        int nk = 0;
        kids = (lk_ix *)lk_sys_alloc(
            NULL,
            (lk_u32)(sizeof(lk_ix) * (child_count > 0 ? child_count : 1)));

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
    } else if (kind == UIK_COLUMN) {
      lk_i32 remaining = ch;
      int spacer_count = 0;
      int child_count = 0;
      lk_i32 spacer_each;
      lk_i32 spacer_extra;
      lk_i32 pos;
      int spacer_idx;
      lk_i32 align = node_prop_i32(t, n, UIP_ALIGN, LK_ALIGN_STRETCH);
      lk_i32 justify = node_prop_i32(t, n, UIP_JUSTIFY, LK_ALIGN_START);

      child = nd->first_child;

      while (child) {
        lk_kind ck = (lk_kind)t->nodes[child].kind;
        int is_flex_spacer =
            (ck == UIK_SPACER && !node_has_prop(t, child, UIP_H));
        child_count++;

        if (is_flex_spacer) {
          spacer_count++;
        } else {
          remaining -= sizes[child].h;
        }

        child = t->nodes[child].next_sibling;
      }

      if (child_count > 1) {
        remaining -= gap * (child_count - 1);
      }

      if (remaining < 0) {
        remaining = 0;
      }

      if (spacer_count > 0) {
        spacer_each = remaining / spacer_count;
        spacer_extra = remaining % spacer_count;
      } else {
        spacer_each = 0;
        spacer_extra = 0;
      }

      /* Compute justify offset for main axis */
      pos = cy;
      if (spacer_count == 0 && remaining > 0) {
        if (justify == LK_ALIGN_CENTER) {
          pos = cy + remaining / 2;
        } else if (justify == LK_ALIGN_END) {
          pos = cy + remaining;
        }
      }

      spacer_idx = 0;
      child = nd->first_child;

      while (child) {
        lk_kind ck = (lk_kind)t->nodes[child].kind;
        int is_flex_spacer =
            (ck == UIK_SPACER && !node_has_prop(t, child, UIP_H));
        lk_i32 child_h;
        lk_i32 child_w;
        lk_i32 child_x;

        if (is_flex_spacer) {
          child_h = spacer_each + (spacer_idx < spacer_extra ? 1 : 0);
          spacer_idx++;
        } else {
          child_h = sizes[child].h;
        }

        if (node_has_prop(t, child, UIP_W)) {
          child_w = sizes[child].w;
        } else if (align == LK_ALIGN_STRETCH) {
          child_w = cw;
        } else {
          child_w = sizes[child].w;
        }

        /* Cross-axis (horizontal) alignment */
        child_x = cx;
        if (!node_has_prop(t, child, UIP_W) && align != LK_ALIGN_STRETCH) {
          if (align == LK_ALIGN_CENTER) {
            child_x = cx + (cw - child_w) / 2;
          } else if (align == LK_ALIGN_END) {
            child_x = cx + cw - child_w;
          }
        }

        rects[child].x = child_x;
        rects[child].y = pos;
        rects[child].w = child_w;
        rects[child].h = child_h;

        pos += child_h + gap;

        child = t->nodes[child].next_sibling;
      }

      {
        lk_ix *kids;
        int nk = 0;
        kids = (lk_ix *)lk_sys_alloc(
            NULL,
            (lk_u32)(sizeof(lk_ix) * (child_count > 0 ? child_count : 1)));

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
    } else {
      lk_i32 remaining = cw;
      int spacer_count = 0;
      int child_count = 0;
      lk_i32 spacer_each;
      lk_i32 spacer_extra;
      lk_i32 pos;
      int spacer_idx;
      lk_i32 align = node_prop_i32(t, n, UIP_ALIGN, LK_ALIGN_STRETCH);
      lk_i32 justify = node_prop_i32(t, n, UIP_JUSTIFY, LK_ALIGN_START);

      child = nd->first_child;

      while (child) {
        lk_kind ck = (lk_kind)t->nodes[child].kind;
        int is_flex_spacer =
            (ck == UIK_SPACER && !node_has_prop(t, child, UIP_W));
        child_count++;

        if (is_flex_spacer) {
          spacer_count++;
        } else {
          remaining -= sizes[child].w;
        }

        child = t->nodes[child].next_sibling;
      }

      if (child_count > 1) {
        remaining -= gap * (child_count - 1);
      }

      if (remaining < 0) {
        remaining = 0;
      }

      if (spacer_count > 0) {
        spacer_each = remaining / spacer_count;
        spacer_extra = remaining % spacer_count;
      } else {
        spacer_each = 0;
        spacer_extra = 0;
      }

      /* Compute justify offset for main axis */
      pos = cx;
      if (spacer_count == 0 && remaining > 0) {
        if (justify == LK_ALIGN_CENTER) {
          pos = cx + remaining / 2;
        } else if (justify == LK_ALIGN_END) {
          pos = cx + remaining;
        }
      }

      spacer_idx = 0;
      child = nd->first_child;

      while (child) {
        lk_kind ck = (lk_kind)t->nodes[child].kind;
        int is_flex_spacer =
            (ck == UIK_SPACER && !node_has_prop(t, child, UIP_W));
        lk_i32 child_w, child_h;
        lk_i32 child_y;

        if (is_flex_spacer) {
          child_w = spacer_each + (spacer_idx < spacer_extra ? 1 : 0);
          spacer_idx++;
        } else {
          child_w = sizes[child].w;
        }

        if (node_has_prop(t, child, UIP_H)) {
          child_h = sizes[child].h;
        } else if (align == LK_ALIGN_STRETCH) {
          child_h = ch;
        } else {
          child_h = sizes[child].h;
        }

        /* Cross-axis (vertical) alignment */
        child_y = cy;
        if (!node_has_prop(t, child, UIP_H) && align != LK_ALIGN_STRETCH) {
          if (align == LK_ALIGN_CENTER) {
            child_y = cy + (ch - child_h) / 2;
          } else if (align == LK_ALIGN_END) {
            child_y = cy + ch - child_h;
          }
        }

        rects[child].x = pos;
        rects[child].y = child_y;
        rects[child].w = child_w;
        rects[child].h = child_h;

        pos += child_w + gap;

        child = t->nodes[child].next_sibling;
      }

      {
        lk_ix *kids;
        int nk = 0;
        kids = (lk_ix *)lk_sys_alloc(
            NULL,
            (lk_u32)(sizeof(lk_ix) * (child_count > 0 ? child_count : 1)));

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
  }

  lk_sys_dealloc(NULL, stack);
}

/* ---- Public API ---- */

int lk_layout(const lk_tree *t, const lk_layout_cfg *cfg, lk_rect *rects) {
  lk_size *sizes;

  if (!t || !cfg || !rects || !cfg->measure_text) {
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

  lk_sys_dealloc(NULL, sizes);
  return 1;
}
