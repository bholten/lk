#ifndef LK_TEXT_INPUT_H
#define LK_TEXT_INPUT_H

#include <lk.h>

/* Returns the widget definition for UIK_TEXT_INPUT. */
lk_widget_def lk_text_input_widget_def(void);

/* Stash text-input geometry (content origin x, resolved font) in
 * retained state for the event handler (click-to-position).  Called
 * by lk_layout after rects are final; no-op when cfg->state is NULL.
 * Same coherence-debt pattern as lk_dropdown_store_trigger_rects. */
void lk_text_input_store_geometry(const lk_tree *t, const lk_rect *rects,
                                  const lk_layout_cfg *cfg);

#endif /* LK_TEXT_INPUT_H */
