#ifndef LK_CHECK_H
#define LK_CHECK_H

#include <lk.h>

lk_widget_def lk_checkbox_widget_def(void);
lk_widget_def lk_radio_widget_def(void);

int lk_check_effective(const lk_tree *t, lk_ix n, const lk_state *state);

#endif
