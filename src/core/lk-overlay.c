/*
 * lk-overlay.c — Generalized overlay stack: registration, anchor
 * resolution with viewport clamping, and the overlay render /
 * hit-test / dismiss passes.
 *
 * Overlays live on a small stack owned by lk_ui (topmost = last).
 * Nodes are referenced by stable lk_node_id and resolved to tree
 * indices per pass.  Two content models:
 *
 *   - Procedural (content_root_id == 0): per-kind code emits render
 *     commands directly.  Currently only LK_OVERLAY_DROPDOWN_POPUP,
 *     which delegates to lk-dropdown.c.
 *   - Subtree (content_root_id != 0): a UIP_HIDDEN subtree in the main
 *     tree is measured and laid out on demand at the resolved anchor
 *     via lk_layout_subtree.  Subtree rects are computed into a
 *     transient scratch array (NOT the shared rects[] from lk_layout),
 *     so the main-pass rects stay untouched and const.
 *
 * See docs/overlays.md.
 */

#include <string.h>

#include "lk-dropdown.h"
#include "lk-memory.h"
#include "lk-overlay.h"
#include "lk-tooltip.h"
#include <lk.h>

/* Scrim alpha for modal overlays (black at this opacity over the
 * whole viewport).  A theme-driven scrim color is future work; the
 * constant follows the SCROLL_BAR_W / SPLIT_DIVIDER_W precedent. */
#define LK_MODAL_SCRIM_ALPHA 150

/* ---- Stack manipulation ---- */

int lk_overlay_push(lk_ui *ui, const lk_overlay *ov) {
  if (!ui || !ov) {
    return 0;
  }

  if (ui->overlay_count >= ui->overlay_cap) {
    lk_u32 new_cap = ui->overlay_cap ? ui->overlay_cap * 2 : 8;
    lk_overlay *no = (lk_overlay *)ui->alloc(
        ui->alloc_ud, (lk_u32)(sizeof(lk_overlay) * new_cap));

    if (!no) {
      return 0;
    }

    if (ui->overlays && ui->overlay_count) {
      memcpy(no, ui->overlays, sizeof(lk_overlay) * ui->overlay_count);
    }

    if (ui->overlays) {
      ui->dealloc(ui->alloc_ud, ui->overlays);
    }

    ui->overlays = no;
    ui->overlay_cap = new_cap;
  }

  ui->overlays[ui->overlay_count++] = *ov;
  return 1;
}

void lk_overlay_pop(lk_ui *ui) {
  if (ui && ui->overlay_count > 0) {
    ui->overlay_count--;
  }
}

int lk_overlay_pop_owner(lk_ui *ui, lk_node_id owner_id) {
  lk_u32 i;
  int removed = 0;

  if (!ui) {
    return 0;
  }

  i = ui->overlay_count;

  while (i > 0) {
    i--;

    if (ui->overlays[i].owner_id == owner_id) {
      lk_u32 k;

      for (k = i; k + 1 < ui->overlay_count; k++) {
        ui->overlays[k] = ui->overlays[k + 1];
      }

      ui->overlay_count--;
      removed = 1;
    }
  }

  return removed;
}

const lk_overlay *lk_overlay_top(const lk_ui *ui) {
  if (!ui || ui->overlay_count == 0) {
    return NULL;
  }

  return &ui->overlays[ui->overlay_count - 1];
}

lk_u32 lk_overlay_count(const lk_ui *ui) {
  return ui ? ui->overlay_count : 0;
}

/* ---- Anchor resolution ---- */

lk_rect lk_anchor_resolve(const lk_overlay *ov, lk_rect owner_rect,
                          lk_i32 vw, lk_i32 vh, lk_i32 content_w,
                          lk_i32 content_h) {
  lk_rect r;
  lk_i32 w = content_w;
  lk_i32 h = content_h;
  lk_i32 ox = ov ? ov->offset.x : 0;
  lk_i32 oy = ov ? ov->offset.y : 0;
  lk_u8 mode = ov ? ov->anchor_mode : (lk_u8)LK_ANCHOR_BELOW;

  if (ov && ov->offset.w > 0) {
    w = ov->offset.w;
  }

  if (ov && ov->offset.h > 0) {
    h = ov->offset.h;
  }

  r.w = w;
  r.h = h;

  switch (mode) {
  case LK_ANCHOR_ABOVE:
    r.x = owner_rect.x + ox;
    r.y = owner_rect.y - h - oy;

    /* Flip below when overflowing the top and there is room below. */
    if (r.y < 0 && vh > 0 && owner_rect.y + owner_rect.h + h <= vh) {
      r.y = owner_rect.y + owner_rect.h + oy;
    }
    break;

  case LK_ANCHOR_AT_CURSOR:
    r.x = ox;
    r.y = oy;
    break;

  case LK_ANCHOR_CENTER_VIEWPORT:
    r.x = (vw - w) / 2;
    r.y = (vh - h) / 2;
    break;

  case LK_ANCHOR_BELOW:
  default:
    r.x = owner_rect.x + ox;
    r.y = owner_rect.y + owner_rect.h + oy;

    /* Flip above when overflowing the bottom and there is room above. */
    if (vh > 0 && r.y + h > vh && owner_rect.y - h >= 0) {
      r.y = owner_rect.y - h - oy;
    }
    break;
  }

  /* Clamp into the viewport (skip an axis when its extent is 0). */
  if (vw > 0) {
    if (r.x + w > vw) {
      r.x = vw - w;
    }

    if (r.x < 0) {
      r.x = 0;
    }
  }

  if (vh > 0) {
    if (r.y + h > vh) {
      r.y = vh - h;
    }

    if (r.y < 0) {
      r.y = 0;
    }
  }

  return r;
}

/* ---- Shared helpers ---- */

static int point_in(const lk_rect *r, lk_i32 x, lk_i32 y) {
  return x >= r->x && x < r->x + r->w && y >= r->y && y < r->y + r->h;
}

/* Lay out a subtree-content overlay into scratch (node_count rects)
 * at its resolved anchor.  Fills out_content (subtree root index) and
 * out_rect (final overlay rect).  Returns 1 on success. */
static int overlay_content_geometry(const lk_tree *t, const lk_overlay *ov,
                                    const lk_rect *rects,
                                    const lk_layout_cfg *cfg,
                                    lk_rect *scratch, lk_ix *out_content,
                                    lk_rect *out_rect) {
  lk_ix cix;
  lk_ix oix;
  lk_rect owner_rect;
  lk_rect final_rect;

  cix = lk_tree_find_by_id(t, ov->content_root_id);

  if (cix == 0) {
    return 0;
  }

  /* First pass at (0,0) to learn the intrinsic content size. */
  if (!lk_layout_subtree(t, cfg, cix, 0, 0, scratch)) {
    return 0;
  }

  oix = ov->owner_id ? lk_tree_find_by_id(t, ov->owner_id) : 0;

  if (oix != 0 && rects) {
    owner_rect = rects[oix];
  } else {
    owner_rect.x = 0;
    owner_rect.y = 0;
    owner_rect.w = 0;
    owner_rect.h = 0;
  }

  final_rect =
      lk_anchor_resolve(ov, owner_rect, cfg->viewport_w, cfg->viewport_h,
                        scratch[cix].w, scratch[cix].h);

  /* Second pass at the anchored origin. */
  if (!lk_layout_subtree(t, cfg, cix, final_rect.x, final_rect.y, scratch)) {
    return 0;
  }

  *out_content = cix;
  *out_rect = final_rect;
  return 1;
}

/* Deepest node in the subtree rooted at root containing (x,y), using
 * the given rects.  Skips hidden descendants but not root itself. */
static lk_ix subtree_hit(const lk_tree *t, lk_ix root, const lk_rect *rects,
                         lk_i32 x, lk_i32 y) {
  lk_ix *stack;
  lk_u32 sp;
  lk_ix best = 0;

  stack = (lk_ix *)lk_sys_alloc(NULL, (lk_u32)(sizeof(lk_ix) * t->node_count));

  if (!stack) {
    return 0;
  }

  sp = 0;
  stack[sp++] = root;

  while (sp > 0) {
    lk_ix n = stack[--sp];
    lk_ix child;
    lk_u32 sp_start, lo, hi;

    if (n != root && lk_node_prop_bool(t, n, UIP_HIDDEN)) {
      continue;
    }

    if (!point_in(&rects[n], x, y)) {
      continue;
    }

    best = n;

    /* Push children forward, then reverse segment for left-to-right
     * DFS (later siblings win). */
    sp_start = sp;
    child = t->nodes[n].first_child;

    while (child) {
      stack[sp++] = child;
      child = t->nodes[child].next_sibling;
    }

    if (sp > sp_start) {
      lo = sp_start;
      hi = sp - 1;

      while (lo < hi) {
        lk_ix tmp = stack[lo];
        stack[lo] = stack[hi];
        stack[hi] = tmp;
        lo++;
        hi--;
      }
    }
  }

  lk_sys_dealloc(NULL, stack);
  return best;
}

/* ---- Overlay render ---- */

int lk_render_build_overlays(lk_ui *ui, const lk_rect *rects,
                             const lk_layout_cfg *cfg, lk_render_list *out) {
  const lk_tree *t;
  lk_u32 i;

  if (!ui || !rects || !cfg || !out) {
    return 0;
  }

  t = lk_ui_tree(ui);

  if (!t || t->root == 0) {
    return 1;
  }

  /* Bottom to top: topmost overlay draws last (on top). */
  for (i = 0; i < ui->overlay_count; i++) {
    const lk_overlay *ov = &ui->overlays[i];

    /* Modal overlays (the same traps_focus && !dismiss_on_outside
     * definition the dismiss pass uses) dim everything beneath them:
     * a viewport-covering scrim before the content, making the
     * already-real click blocking visible.  Needs an alpha-blending
     * consumer to actually dim (the SDL backend enables
     * SDL_BLENDMODE_BLEND). */
    if (ov->traps_focus && !ov->dismiss_on_outside &&
        cfg->viewport_w > 0 && cfg->viewport_h > 0) {
      lk_render_cmd scmd;

      memset(&scmd, 0, sizeof(scmd));
      scmd.op = LK_ROP_FILL_RECT;
      scmd.color.r = 0;
      scmd.color.g = 0;
      scmd.color.b = 0;
      scmd.color.a = LK_MODAL_SCRIM_ALPHA;
      scmd.rect.x = 0;
      scmd.rect.y = 0;
      scmd.rect.w = cfg->viewport_w;
      scmd.rect.h = cfg->viewport_h;
      lk_render_list_push(out, scmd);
    }

    if (ov->content_root_id != 0) {
      lk_rect *scratch = (lk_rect *)lk_sys_alloc(
          NULL, (lk_u32)(sizeof(lk_rect) * t->node_count));
      lk_ix cix;
      lk_rect orect;

      if (!scratch) {
        continue;
      }

      if (overlay_content_geometry(t, ov, rects, cfg, scratch, &cix,
                                   &orect)) {
        lk_render_build_from(t, cix, scratch, cfg->styles, cfg->state,
                             cfg->geom, out);
      }

      lk_sys_dealloc(NULL, scratch);
    } else if (ov->kind == LK_OVERLAY_DROPDOWN_POPUP) {
      lk_ix n = lk_tree_find_by_id(t, ov->owner_id);

      if (n != 0) {
        lk_dropdown_render_popup(t, n, rects, cfg->styles, cfg->state, cfg,
                                 out);
      }
    } else if (ov->kind == LK_OVERLAY_TOOLTIP) {
      lk_ix n = lk_tree_find_by_id(t, ov->owner_id);

      if (n != 0) {
        lk_tooltip_render(t, n, ov, rects, cfg, out);
      }
    }
  }

  return 1;
}

/* ---- Overlay hit-test ---- */

lk_ix lk_hit_test_overlay(lk_ui *ui, const lk_rect *rects,
                          const lk_layout_cfg *cfg, lk_i32 x, lk_i32 y) {
  const lk_tree *t;
  lk_u32 i;

  if (!ui || !rects || !cfg) {
    return 0;
  }

  t = lk_ui_tree(ui);

  if (!t || t->root == 0) {
    return 0;
  }

  /* Top to bottom: topmost overlay is hit-tested first.  Kinds with
   * no case below (LK_OVERLAY_TOOLTIP) are never hit-testable — the
   * pointer sees straight through them. */
  i = ui->overlay_count;

  while (i > 0) {
    const lk_overlay *ov;

    i--;
    ov = &ui->overlays[i];

    if (ov->content_root_id != 0) {
      lk_rect *scratch = (lk_rect *)lk_sys_alloc(
          NULL, (lk_u32)(sizeof(lk_rect) * t->node_count));
      lk_ix cix;
      lk_rect orect;

      if (!scratch) {
        continue;
      }

      if (overlay_content_geometry(t, ov, rects, cfg, scratch, &cix,
                                   &orect) &&
          point_in(&orect, x, y)) {
        lk_ix hit = subtree_hit(t, cix, scratch, x, y);

        lk_sys_dealloc(NULL, scratch);
        return hit ? hit : cix;
      }

      lk_sys_dealloc(NULL, scratch);
    } else if (ov->kind == LK_OVERLAY_DROPDOWN_POPUP) {
      lk_ix n = lk_tree_find_by_id(t, ov->owner_id);

      if (n != 0) {
        lk_ix hit = lk_dropdown_hit_popup(t, n, rects, cfg->styles,
                                          cfg->state, cfg, x, y);

        if (hit != 0) {
          return hit;
        }
      }
    }
  }

  return 0;
}

/* ---- Overlay dismissal / modal blocking ---- */

/* Overlay rect + owner rect containment for the topmost overlay. */
static int overlay_contains(lk_ui *ui, const lk_overlay *ov,
                            const lk_rect *rects, const lk_layout_cfg *cfg,
                            lk_i32 x, lk_i32 y) {
  const lk_tree *t = lk_ui_tree(ui);
  lk_ix oix;

  if (!t) {
    return 0;
  }

  oix = ov->owner_id ? lk_tree_find_by_id(t, ov->owner_id) : 0;

  /* Owner (trigger) rect counts as inside: clicking the trigger is
   * the widget's own toggle, not an outside dismissal. */
  if (oix != 0 && rects && point_in(&rects[oix], x, y)) {
    return 1;
  }

  if (ov->content_root_id != 0) {
    lk_rect *scratch = (lk_rect *)lk_sys_alloc(
        NULL, (lk_u32)(sizeof(lk_rect) * t->node_count));
    lk_ix cix;
    lk_rect orect;
    int inside = 0;

    if (!scratch) {
      return 0;
    }

    if (overlay_content_geometry(t, ov, rects, cfg, scratch, &cix, &orect)) {
      inside = point_in(&orect, x, y);
    }

    lk_sys_dealloc(NULL, scratch);
    return inside;
  }

  if (ov->kind == LK_OVERLAY_DROPDOWN_POPUP && oix != 0) {
    lk_rect popup = lk_dropdown_popup_rect(t, oix, rects, cfg->styles, cfg);
    return point_in(&popup, x, y);
  }

  return 0;
}

int lk_overlay_dismiss_outside(lk_ui *ui, const lk_rect *rects,
                               const lk_layout_cfg *cfg, lk_i32 x, lk_i32 y) {
  int dismissed = 0;
  lk_u32 i;

  if (!ui || !rects || !cfg) {
    return LK_DISMISS_NONE;
  }

  /* Top to bottom.  Passive overlays (tooltips) are transparent: the
   * scan skips over them so a dismissible overlay underneath still
   * sees the outside click. */
  i = ui->overlay_count;

  while (i > 0) {
    const lk_overlay *ov = &ui->overlays[i - 1];
    lk_node_id owner;
    lk_u8 kind;

    if (overlay_contains(ui, ov, rects, cfg, x, y)) {
      break;
    }

    /* Modal: consume the outside click without dismissing. */
    if (ov->traps_focus && !ov->dismiss_on_outside) {
      return LK_DISMISS_BLOCKED;
    }

    /* Passive overlay (e.g. tooltip): outside clicks pass through. */
    if (!ov->dismiss_on_outside) {
      i--;
      continue;
    }

    owner = ov->owner_id;
    kind = ov->kind;

    /* Remove entry i-1 (may not be topmost when a passive overlay
     * sits above it). */
    {
      lk_u32 k;

      for (k = i - 1; k + 1 < ui->overlay_count; k++) {
        ui->overlays[k] = ui->overlays[k + 1];
      }

      ui->overlay_count--;
    }

    if (kind == LK_OVERLAY_DROPDOWN_POPUP && ui->state) {
      lk_state_set(ui->state, owner, LKS_EXPANDED, lk_v_i32(0));
    }

    dismissed = 1;
    i--;
  }

  return dismissed ? LK_DISMISS_DISMISSED : LK_DISMISS_NONE;
}
