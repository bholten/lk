#ifndef LK_TEXT_INPUT_H
#define LK_TEXT_INPUT_H

#include <lk.h>

lk_widget_def lk_text_input_widget_def(void);

void lk_text_input_store_geometry(const lk_tree *t, const lk_rect *rects,
                                  const lk_layout_cfg *cfg);

#endif
