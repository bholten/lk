/*
 * lk-styled-text.h -- internal: the UIK_STYLED_TEXT widget def and the
 * row breaker's test hooks (docs/styled-text.md).
 */
#ifndef LK_STYLED_TEXT_H
#define LK_STYLED_TEXT_H

#include <lk.h>

lk_widget_def lk_styled_text_widget_def(void);

/* Row k (0-based) of `text` broken at `width` px in `mode` with the
 * given backend/font: 1 + [start, end) byte range, or 0 past the last
 * row.  Rows never include the '\n' that ends a line.  Test hook. */
int lk_styled_text_row(const lk_text_backend *tb, lk_u16 font_id,
                       lk_u16 font_size, lk_str text, lk_i32 width,
                       lk_wrap_mode mode, lk_u32 k, lk_u32 *start, lk_u32 *end);
lk_u32 lk_styled_text_row_count(const lk_text_backend *tb, lk_u16 font_id,
                                lk_u16 font_size, lk_str text, lk_i32 width,
                                lk_wrap_mode mode);

#endif /* LK_STYLED_TEXT_H */
