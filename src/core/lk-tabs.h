#ifndef LK_TABS_H
#define LK_TABS_H

#include <lk.h>

/* Widget-def factories — registered by lk-widget.c for UIK_TABS and
 * UIK_TAB.  The TAB def returned here carries render + event only;
 * lk-widget.c fills in the column measure/layout (a page lays out
 * its children like a column).  See docs/forms-widgets.md. */
lk_widget_def lk_tabs_widget_def(void);
lk_widget_def lk_tab_widget_def(void);

/* The selected TAB child of the TABS node n (0 when it has no visible
 * TAB children).  Effective selection = LKS_SELECTED_INDEX state >
 * UIP_VALUE (a TAB child's node id string) > 0, clamped to the count
 * of visible (non-UIP_HIDDEN) TAB children.  *out_index (may be NULL)
 * receives the 0-based index among those children.  state may be
 * NULL. */
lk_ix lk_tabs_selected(const lk_tree *t, lk_ix n, const lk_state *state,
                       int *out_index);

/* ENGINE HOOK.  1 if node n is a child of a UIK_TABS that is not its
 * selected TAB (an unselected TAB page, or a stray non-TAB child).
 * The main passes treat such a node as a LEAF: it is measured (so
 * the tabs area sizes to its largest page) and rendered (its header
 * cell), but its layout hook is not called, its children are not
 * descended into by layout / render / focus collection, and its
 * subtree rects stay zero (so hit-testing never reaches it).  state
 * may be NULL. */
int lk_tabs_collapsed(const lk_tree *t, lk_ix n, const lk_state *state);

#endif /* LK_TABS_H */
