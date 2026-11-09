

#ifndef LK_STYLED_TEXT_H
#define LK_STYLED_TEXT_H

#include <lk.h>

lk_widget_def lk_styled_text_widget_def(void);

int lk_styled_text_row(const lk_text_backend *tb, lk_u16 font_id,
                       lk_u16 font_size, lk_str text, lk_i32 width,
                       lk_wrap_mode mode, lk_u32 k, lk_u32 *start, lk_u32 *end);
lk_u32 lk_styled_text_row_count(const lk_text_backend *tb, lk_u16 font_id,
                                lk_u16 font_size, lk_str text, lk_i32 width,
                                lk_wrap_mode mode);

#endif
