#ifndef LK_UTF8_H
#define LK_UTF8_H

/* UTF-8 codepoint boundary helpers (internal header, like lk-memory.h).
 *
 * Lifted from weft's editor/src/utf8.c and C89-ified.  Pure functions,
 * no allocation.  All indices are byte offsets into s[0..len); len is
 * itself a valid boundary ("cursor at end").
 *
 * Invalid UTF-8 degrades gracefully: a bad lead byte or a stray
 * continuation byte is treated as a single-byte codepoint so callers
 * always make progress.
 *
 * Shared by the text-backend stub, the text input widget, and (later)
 * the editor widget — single source of truth for codepoint stepping.
 */

#include <lk.h>

/* Number of bytes in the codepoint whose lead byte is `byte`
 * (1..4; invalid lead/continuation bytes count as 1). */
lk_u32 lk_utf8_cp_len(unsigned char byte);

/* Byte index of the codepoint boundary after ix (clamped to len).
 * lk_utf8_next(s, len, len) == len. */
lk_u32 lk_utf8_next(const char *s, lk_u32 len, lk_u32 ix);

/* Byte index of the codepoint boundary before ix (0 when ix == 0). */
lk_u32 lk_utf8_prev(const char *s, lk_u32 len, lk_u32 ix);

/* 1 if ix is a codepoint boundary (ix == 0, ix == len, or s[ix] is
 * not a continuation byte), else 0. */
int lk_utf8_is_boundary(const char *s, lk_u32 len, lk_u32 ix);

#endif /* LK_UTF8_H */
