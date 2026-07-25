#ifndef LK_SPLIT_H
#define LK_SPLIT_H

#include <lk.h>

/* Widget-def factories — registered by lk-widget.c into the global
 * widget table for UIK_SPLIT_H and UIK_SPLIT_V. */
lk_widget_def lk_split_h_widget_def(void);
lk_widget_def lk_split_v_widget_def(void);

/* Internal: stash each split's content rect in retained state
 * (LKS_SPLIT_C*).  Called by lk_layout after rects are final so the
 * split event handler (which has no layout rects) can hit-test the
 * divider band and map pointer position to a ratio.  Follows the
 * lk_dropdown_store_trigger_rects / lk_text_input_store_geometry
 * precedent — derived geometry in retained state (coherence debt,
 * see design-coherence list in docs/TODO.md). */
void lk_split_store_geometry(const lk_tree *t, const lk_rect *rects,
                             const lk_layout_cfg *cfg);

#endif /* LK_SPLIT_H */
