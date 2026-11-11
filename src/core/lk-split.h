#ifndef LK_SPLIT_H
#define LK_SPLIT_H

#include <lk.h>

lk_widget_def lk_split_h_widget_def(void);
lk_widget_def lk_split_v_widget_def(void);

void lk_split_store_geometry(const lk_tree *t, const lk_rect *rects,
                             const lk_layout_cfg *cfg);

#endif
