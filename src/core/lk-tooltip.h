#ifndef LK_TOOLTIP_H
#define LK_TOOLTIP_H

#include <lk.h>

/* Internal: hover-transition hook called by lk_hover_set /
 * lk_hover_clear (only on actual transitions).  Pops any existing
 * LK_OVERLAY_TOOLTIP overlay; if the newly hovered node carries
 * UIP_TOOLTIP, pushes a fresh tooltip overlay owned by it.
 * hovered_id == 0 means "nothing hovered" (pop only). */
void lk_tooltip_hover_changed(lk_ui *ui, lk_node_id hovered_id);

/* Internal: final on-screen rect of the tooltip for owner node n —
 * measured text plus padding, anchored through lk_anchor_resolve
 * (BELOW; flips above near the bottom edge, clamps into the
 * viewport).  Exposed for geometry tests. */
lk_rect lk_tooltip_rect(const lk_tree *t, lk_ix n, const lk_overlay *ov,
                        const lk_rect *rects, const lk_layout_cfg *cfg);

/* Internal: render dispatch target called by lk-overlay.c for
 * LK_OVERLAY_TOOLTIP overlays (procedural content).  Emits bg, 1 px
 * border, and the tooltip text using the owner's resolved style with
 * fg/bg swapped. */
void lk_tooltip_render(const lk_tree *t, lk_ix n, const lk_overlay *ov,
                       const lk_rect *rects, const lk_layout_cfg *cfg,
                       lk_render_list *out);

#endif /* LK_TOOLTIP_H */
