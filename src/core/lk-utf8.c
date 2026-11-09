

#include "lk-utf8.h"

static int is_continuation(unsigned char byte) {
  return (byte & 0xC0) == 0x80;
}

lk_u32 lk_utf8_cp_len(unsigned char byte) {
  if ((byte & 0x80) == 0) {
    return 1;
  }

  if ((byte & 0xE0) == 0xC0) {
    return 2;
  }

  if ((byte & 0xF0) == 0xE0) {
    return 3;
  }

  if ((byte & 0xF8) == 0xF0) {
    return 4;
  }

  return 1;
}

lk_u32 lk_utf8_next(const char *s, lk_u32 len, lk_u32 ix) {
  lk_u32 next;

  if (ix >= len) {
    return len;
  }

  next = ix + lk_utf8_cp_len((unsigned char)s[ix]);

  if (next > len) {
    next = len;
  }

  return next;
}

lk_u32 lk_utf8_prev(const char *s, lk_u32 len, lk_u32 ix) {
  lk_u32 prev;

  if (ix == 0) {
    return 0;
  }

  if (ix > len) {
    ix = len;
  }

  prev = ix - 1;

  while (prev > 0 && is_continuation((unsigned char)s[prev])) {
    prev--;
  }

  return prev;
}

int lk_utf8_is_boundary(const char *s, lk_u32 len, lk_u32 ix) {
  if (ix == 0 || ix >= len) {
    return 1;
  }

  return !is_continuation((unsigned char)s[ix]);
}
