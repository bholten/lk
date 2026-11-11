

#include "lk-utf8.h"
#include <lk.h>

#define STUB_ADVANCE 8
#define STUB_HEIGHT 16
#define STUB_BASELINE 12

static lk_u32 stub_count_cp(lk_str run, lk_u32 len) {
  lk_u32 i = 0;
  lk_u32 n = 0;

  while (i < len) {
    i = lk_utf8_next(run.ptr, len, i);
    n++;
  }

  return n;
}

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
