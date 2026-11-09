#ifndef LK_OVERLAY_H
#define LK_OVERLAY_H

#include <lk.h>

int lk_render_build_from(const lk_tree *t, lk_ix start, const lk_rect *rects,
                         const lk_style *styles, const lk_state *state,
                         const lk_widget_geom *geom, lk_render_list *out);

#endif
