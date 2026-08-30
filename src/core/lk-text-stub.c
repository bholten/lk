/* Deterministic monospace text backend for headless tests.
 *
 * Metrics (chosen to match the old lk_measure_text_stub so ASCII
 * layout assertions carry over unchanged):
 *   advance   = 8 px per CODEPOINT (old stub was 8 px per byte —
 *               identical for ASCII, exact for UTF-8 now)
 *   height    = 16 px
 *   baseline  = 12 px
 *   line_height = 16 px
 *
 * All metrics are independent of font_id/font_size: every face and
 * size is identical.  This is documented stub behavior — it makes
 * font selection testable (ids flow through) without affecting
 * geometry assertions.
 *
 * register_font hands out 1, 2, 3, ... from a process-global static
 * counter, so stub font ids are process-global (fine for tests).
 *
 * Codepoint stepping comes from lk-utf8.c (single source of truth).
 */

#include "lk-utf8.h"
#include <lk.h>

#define STUB_ADVANCE 8
#define STUB_HEIGHT 16
#define STUB_BASELINE 12

/* Count codepoints in run.ptr[0..len). */
static lk_u32 stub_count_cp(lk_str run, lk_u32 len) {
  lk_u32 i = 0;
  lk_u32 n = 0;

  while (i < len) {
    i = lk_utf8_next(run.ptr, len, i);
    n++;
  }

  return n;
}

/* Byte index of the boundary after cp_count codepoints (clamped to
 * run.len). */
static lk_u32 stub_cp_to_byte(lk_str run, lk_u32 cp_count) {
  lk_u32 i = 0;
  lk_u32 seen = 0;

  while (i < run.len && seen < cp_count) {
    i = lk_utf8_next(run.ptr, run.len, i);
    seen++;
  }

  return i;
}

static void stub_measure(void *ud, lk_str run, lk_u16 font_id, lk_u16 font_size,
                         lk_text_metrics *out) {
  (void)ud;
  (void)font_id;
  (void)font_size;

  if (!out) {
    return;
  }

  out->w = (lk_i32)(stub_count_cp(run, run.len) * STUB_ADVANCE);
  out->h = STUB_HEIGHT;
  out->baseline = STUB_BASELINE;
}

static lk_i32 stub_x_from_index(void *ud, lk_str run, lk_u16 font_id,
                                lk_u16 font_size, lk_u32 byte_ix) {
  lk_u32 ix = byte_ix;

  (void)ud;
  (void)font_id;
  (void)font_size;

  if (ix > run.len) {
    ix = run.len;
  }

  /* Snap down to a codepoint boundary. */
  while (ix > 0 && !lk_utf8_is_boundary(run.ptr, run.len, ix)) {
    ix--;
  }

  return (lk_i32)(stub_count_cp(run, ix) * STUB_ADVANCE);
}

static lk_u32 stub_index_from_x(void *ud, lk_str run, lk_u16 font_id,
                                lk_u16 font_size, lk_i32 x) {
  lk_u32 ncp;
  lk_u32 boundary;

  (void)ud;
  (void)font_id;
  (void)font_size;

  if (x <= 0) {
    return 0;
  }

  /* Boundary i sits at x = 8*i.  Nearest boundary, ties round up:
   * i = floor((x + advance/2) / advance). */
  boundary = (lk_u32)(x + STUB_ADVANCE / 2) / STUB_ADVANCE;
  ncp = stub_count_cp(run, run.len);

  if (boundary > ncp) {
    boundary = ncp;
  }

  return stub_cp_to_byte(run, boundary);
}

static lk_i32 stub_line_height(void *ud, lk_u16 font_id, lk_u16 font_size) {
  (void)ud;
  (void)font_id;
  (void)font_size;
  return STUB_HEIGHT;
}

static lk_u16 stub_register_font(void *ud, const char *path) {
  /* Process-global counter: successive calls return 1, 2, 3, ...
   * regardless of which stub "instance" is used (there is only one). */
  static lk_u16 g_next_font = 1;

  (void)ud;
  (void)path;

  return g_next_font++;
}

static const lk_text_backend g_stub_backend = {NULL,
                                               stub_measure,
                                               stub_x_from_index,
                                               stub_index_from_x,
                                               stub_line_height,
                                               stub_register_font};

const lk_text_backend *lk_text_backend_stub(void) {
  return &g_stub_backend;
}
