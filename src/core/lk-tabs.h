#ifndef LK_TABS_H
#define LK_TABS_H

#include <lk.h>

lk_widget_def lk_tabs_widget_def(void);
lk_widget_def lk_tab_widget_def(void);

lk_ix lk_tabs_selected(const lk_tree *t, lk_ix n, const lk_state *state,
                       int *out_index);

int lk_tabs_collapsed(const lk_tree *t, lk_ix n, const lk_state *state);

#endif
