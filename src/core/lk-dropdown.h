#ifndef LK_DROPDOWN_H
#define LK_DROPDOWN_H

#include <lk.h>

/* Widget-def factories — registered by lk-widget.c into the global
 * widget table for UIK_DROPDOWN and UIK_OPTION. */
lk_widget_def lk_dropdown_widget_def(void);
lk_widget_def lk_option_widget_def(void);

/* Internal: compute the popup rect for an expanded dropdown at index n.
 * Uses the trigger rect in rects[n] as the anchor.  cfg->measure_text
 * is used to measure option heights. */
lk_rect lk_dropdown_popup_rect(const lk_tree *t, lk_ix n,
                               const lk_rect *rects,
                               const lk_style *styles,
                               const lk_layout_cfg *cfg);

/* Internal: compute the rect of option index `opt_ix_in_parent`
 * within the popup.  (0 = first option.)  Returns a zero-rect for
 * out-of-range indices. */
lk_rect lk_dropdown_option_rect(const lk_tree *t, lk_ix dd,
                                 lk_u32 opt_index,
                                 const lk_rect *rects,
                                 const lk_style *styles,
                                 const lk_layout_cfg *cfg);

#endif /* LK_DROPDOWN_H */
