#ifndef LK_CHECK_H
#define LK_CHECK_H

#include <lk.h>

/* Widget-def factories — registered by lk-widget.c into the global
 * widget table for UIK_CHECKBOX and UIK_RADIO.  See docs/forms-widgets.md. */
lk_widget_def lk_checkbox_widget_def(void);
lk_widget_def lk_radio_widget_def(void);

/* Effective checked state (0/1): LKS_CHECKED state when present, else
 * the UIP_CHECKED prop, else 0.  state may be NULL. */
int lk_check_effective(const lk_tree *t, lk_ix n, const lk_state *state);

#endif /* LK_CHECK_H */
