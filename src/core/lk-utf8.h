#ifndef LK_UTF8_H
#define LK_UTF8_H

#include <lk.h>

lk_u32 lk_utf8_cp_len(unsigned char byte);

lk_u32 lk_utf8_next(const char *s, lk_u32 len, lk_u32 ix);

lk_u32 lk_utf8_prev(const char *s, lk_u32 len, lk_u32 ix);

int lk_utf8_is_boundary(const char *s, lk_u32 len, lk_u32 ix);

#endif
