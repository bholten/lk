/*
 * lk-text-align.h — internal: placing a measured text run inside a
 * leaf widget's content box per lk_style.text_align / text_valign.
 */
#ifndef LK_TEXT_ALIGN_H
#define LK_TEXT_ALIGN_H

#include <lk.h>

/* Offset of a run of `extent` px inside `avail` px for an lk_align:
 * START (and STRETCH) = 0, CENTER = half the slack, END = all of it.
 * Never negative, so an overflowing run stays START-anchored (and is
 * clipped on the right, as before alignment existed). */
lk_i32 lk_text_align_offset(lk_u8 align, lk_i32 avail, lk_i32 extent);

#endif /* LK_TEXT_ALIGN_H */
