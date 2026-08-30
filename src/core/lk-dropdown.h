#ifndef LK_DROPDOWN_H
#define LK_DROPDOWN_H

#include <lk.h>

/* Widget-def factories — registered by lk-widget.c into the global
 * widget table for UIK_DROPDOWN and UIK_OPTION. */
lk_widget_def lk_dropdown_widget_def(void);
lk_widget_def lk_option_widget_def(void);

/* Internal: stash each dropdown's trigger rect + popup row metrics in
 * the per-frame geometry scratch (cfg->geom, trigger arm).  Called by
 * lk_layout after rects are final so the dropdown event handler can
 * distinguish trigger clicks from clicks in the popup's padding zone
 * and clamp popup scrolling.  No-op when cfg->geom is NULL. */
void lk_dropdown_store_trigger_rects(const lk_tree *t, const lk_rect *rects,
                                     const lk_layout_cfg *cfg);

/* Internal: compute the popup rect for an expanded dropdown at index n.
 * Uses the trigger rect in rects[n] as the anchor, resolved through
 * lk_anchor_resolve (BELOW, flipping above near the bottom edge and
 * clamping to cfg->viewport_w/h).  cfg->text is used to measure
 * option heights. */
lk_rect lk_dropdown_popup_rect(const lk_tree *t, lk_ix n, const lk_rect *rects,
                               const lk_style *styles,
                               const lk_layout_cfg *cfg);

/* Internal: compute the rect of option index `opt_ix_in_parent`
 * within the popup.  (0 = first option.)  Returns a zero-rect for
 * out-of-range indices. */
lk_rect lk_dropdown_option_rect(const lk_tree *t, lk_ix dd, lk_u32 opt_index,
                                const lk_rect *rects, const lk_style *styles,
                                const lk_layout_cfg *cfg);

/* Internal: per-overlay dispatch targets called by lk-overlay.c for
 * LK_OVERLAY_DROPDOWN_POPUP overlays (procedural content). */

/* Emit the popup (background, border, hover highlight, option text)
 * for the expanded dropdown at index n. */
void lk_dropdown_render_popup(const lk_tree *t, lk_ix n, const lk_rect *rects,
                              const lk_style *styles, const lk_state *state,
                              const lk_layout_cfg *cfg, lk_render_list *out);

/* Hit-test the expanded dropdown's popup.  Returns the option index
 * under (x,y), n itself for the popup's padding zone, or 0 when the
 * point is outside the popup (or the dropdown is not expanded). */
lk_ix lk_dropdown_hit_popup(const lk_tree *t, lk_ix n, const lk_rect *rects,
                            const lk_style *styles, const lk_state *state,
                            const lk_layout_cfg *cfg, lk_i32 x, lk_i32 y);

#endif /* LK_DROPDOWN_H */
