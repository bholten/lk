#ifndef LK_OVERLAY_H
#define LK_OVERLAY_H

#include <lk.h>

/* Internal: append render commands for the subtree rooted at start.
 * Implemented in lk-render.c.  Ignores UIP_HIDDEN on start itself
 * (hidden descendants inside the subtree are still skipped) and does
 * NOT reset out->count, so overlay content draws on top of an already
 * built main-tree render list. */
int lk_render_build_from(const lk_tree *t, lk_ix start, const lk_rect *rects,
                         const lk_style *styles, const lk_state *state,
                         lk_render_list *out);

#endif /* LK_OVERLAY_H */
