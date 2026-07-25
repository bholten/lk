/*
 * lk-utf8.c — UTF-8 codepoint boundary helpers.
 *
 * Lifted from weft's editor/src/utf8.c (C89-ified).  See lk-utf8.h.
 *
 * UTF-8 encoding:
 *   0xxxxxxx                            - 1 byte  (ASCII, 0x00-0x7F)
 *   110xxxxx 10xxxxxx                   - 2 bytes (0x80-0x7FF)
 *   1110xxxx 10xxxxxx 10xxxxxx          - 3 bytes (0x800-0xFFFF)
 *   11110xxx 10xxxxxx 10xxxxxx 10xxxxxx - 4 bytes (0x10000-0x10FFFF)
 */

#include "lk-utf8.h"

/* 1 if `byte` is a continuation byte (10xxxxxx). */
static int is_continuation(unsigned char byte) {
  return (byte & 0xC0) == 0x80;
}

lk_u32 lk_utf8_cp_len(unsigned char byte) {
  if ((byte & 0x80) == 0) {
    return 1; /* ASCII: 0xxxxxxx */
  }

  if ((byte & 0xE0) == 0xC0) {
    return 2; /* 110xxxxx */
  }

  if ((byte & 0xF0) == 0xE0) {
    return 3; /* 1110xxxx */
  }

  if ((byte & 0xF8) == 0xF0) {
    return 4; /* 11110xxx */
  }

  /* Invalid lead byte or continuation byte — treat as single byte */
  return 1;
}

lk_u32 lk_utf8_next(const char *s, lk_u32 len, lk_u32 ix) {
  lk_u32 next;

  if (ix >= len) {
    return len;
  }

  next = ix + lk_utf8_cp_len((unsigned char)s[ix]);

  /* Don't overshoot the end (truncated sequence) */
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

  /* Move back one byte, then skip over continuation bytes */
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
