#ifndef LK_SLIDER_H
#define LK_SLIDER_H

#include <lk.h>

/* Widget-def factory — registered by lk-widget.c for UIK_SLIDER.
 * See docs/forms-widgets.md. */
lk_widget_def lk_slider_widget_def(void);

/* Effective slider value: LKS_SLIDER_VALUE state when present, else the
 * i32 UIP_VALUE prop, else UIP_MIN; snapped to UIP_STEP and clamped to
 * [UIP_MIN, UIP_MAX].  state may be NULL. */
lk_i32 lk_slider_effective(const lk_tree *t, lk_ix n, const lk_state *state);

/* Range accessors with defaults applied (min 0, max 100 -- never
 * below min, step >= 1). */
void lk_slider_range(const lk_tree *t, lk_ix n, lk_i32 *out_min,
                     lk_i32 *out_max, lk_i32 *out_step);

#endif /* LK_SLIDER_H */
