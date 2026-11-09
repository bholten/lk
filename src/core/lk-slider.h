#ifndef LK_SLIDER_H
#define LK_SLIDER_H

#include <lk.h>

lk_widget_def lk_slider_widget_def(void);

lk_i32 lk_slider_effective(const lk_tree *t, lk_ix n, const lk_state *state);

void lk_slider_range(const lk_tree *t, lk_ix n, lk_i32 *out_min,
                     lk_i32 *out_max, lk_i32 *out_step);

#endif
