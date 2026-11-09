

#include <string.h>

#include "core/lk-memory.h"
#include "core/lk-utf8.h"
#include <lk-editor.h>

#ifdef LK_EDITOR_DEBUG_ASSERTS
#include <assert.h>
#define LK_ED_ASSERT(x) assert(x)
#else
#define LK_ED_ASSERT(x) ((void)0)
#endif

#define ED_NO_ANCHOR 0xFFFFFFFFu
#define ED_STICKY_NONE (-1)
#define ED_MAX_CARETS 1024
#define ED_INITIAL_CARET_CAP 4
#define ED_SNAP_RING 64
#define ED_FALLBACK_LINE_H 16
#define ED_FALLBACK_ADVANCE 8
#define ED_FALLBACK_BASELINE 12
#define ED_FALLBACK_PAGE_LINES 20
#define ED_TAB_SIZE 4
#define ED_SCROLL_BAR_W 6
#define ED_SCROLL_THUMB_MIN 8

typedef struct ed_line {
  lk_u32 line;
  lk_u32 row;
  lk_u32 doc_start;
  lk_u32 doc_len;
  lk_u32 off;
} ed_line;

typedef struct ed_line_wrap {
  lk_u32 *breaks;
  lk_u32 break_count;
  lk_u32 break_cap;
  lk_u32 generation;
} ed_line_wrap;

typedef struct ed_seg {
  lk_u32 off;
  lk_u32 len;
  lk_i32 x, y, w;
  lk_u8 flags;
  lk_color fg, bg;
} ed_seg;

typedef struct ed_geom {
  int valid;
  const void *tree;
  lk_node_id node_id;
  lk_rect rect;
  lk_i32 line_h;
  lk_i32 baseline;
  lk_u32 first_line;
  lk_u32 vis_count;
  lk_i32 scroll_x;

  lk_u32 cursor_count;
  lk_u32 sel_count;

  lk_u32 sb_total_rows;
  lk_u32 sb_top_px;
  lk_u16 font_id, font_size;
  const lk_text_backend *tb;

} ed_geom;

typedef struct ed_caret {
  lk_u32 cursor;
  lk_u32 anchor;
  lk_i32 sticky_x;
} ed_caret;

typedef struct ed_caret_snap {
  lk_u32 serial;
  ed_caret *before;
  lk_u32 before_count, before_primary;
  ed_caret *after;
  lk_u32 after_count, after_primary;
} ed_caret_snap;

struct lk_editor {
  void *(*alloc)(void *, lk_u32);
  void (*dealloc)(void *, void *);
  void *ud;

  lk_document *doc;
  lk_edit_history *hist;
  lk_u32 sub_id;

  ed_caret *carets;
  lk_u32 caret_count, caret_cap;
  lk_u32 caret_primary;

  ed_caret_snap snaps[ED_SNAP_RING];
  lk_u32 snap_head;
  lk_u32 last_adopted_serial;

  lk_editor_viewport vp;
  int drag;
  int sb_drag;
  lk_i32 sb_grab;

  int box;
  lk_u32 box_line, box_row;
  lk_i32 box_x;
  int editable;
  lk_u32 tab_size;
  int in_command;
  int in_replay;

  int pending_scroll;
  lk_i32 scroll_x;

  lk_u32 wrap_mode;
  ed_line_wrap *wrap;
  lk_u32 wrap_count, wrap_cap;
  lk_u32 wrap_generation;
  lk_i32 wrap_w;
  const void *wrap_tb;
  lk_u16 wrap_font_id, wrap_font_size;
  lk_u32 wrap_tab;

  lk_u32 est_px, est_bytes;

  lk_i32 line_h;
  lk_i32 space_adv;
  lk_i32 page_lines;
  lk_u16 font_id, font_size;

  char *vis;
  lk_u32 vis_len, vis_cap;
  ed_line *lines;
  lk_u32 lines_cap;
  ed_seg *segs;
  lk_u32 seg_count, seg_cap;

  lk_i32 *cursor_xy;
  lk_u32 cursor_xy_cap;
  lk_rect *sel_rects;
  lk_u32 sel_rects_cap;

  char *line_buf;
  lk_u32 line_buf_cap;

  char *wrap_buf;
  lk_u32 wrap_buf_cap;

  lk_edit_span *spans;
  lk_u32 span_count, span_cap;
  lk_revision span_rev;
  lk_u32 span_range_start, span_range_end;

  lk_presentation_source psrc;

  ed_geom geom;
};

static void *ed_grow_buf(lk_editor *e, void *old, lk_u32 old_bytes,
                         lk_u32 new_bytes) {
  void *nb = e->alloc(e->ud, new_bytes);

  if (!nb) {
    return NULL;
  }

  if (old) {
    if (old_bytes) {
      memcpy(nb, old, old_bytes);
    }

    e->dealloc(e->ud, old);
  }

  return nb;
}

static lk_u32 ed_grow_cap(lk_u32 cap, lk_u32 needed, lk_u32 initial) {
  lk_u32 nc = cap ? cap : initial;

  while (nc < needed) {
    nc *= 2;
  }

  return nc;
}

static int ed_vis_reserve(lk_editor *e, lk_u32 needed) {
  lk_u32 nc;
  char *nb;

  if (needed <= e->vis_cap) {
    return 1;
  }

  nc = ed_grow_cap(e->vis_cap, needed, 256);
  nb = (char *)ed_grow_buf(e, e->vis, e->vis_len, nc);

  if (!nb) {
    return 0;
  }

  e->vis = nb;
  e->vis_cap = nc;

  return 1;
}

static int ed_lines_reserve(lk_editor *e, lk_u32 needed) {
  lk_u32 nc;
  ed_line *nb;

  if (needed <= e->lines_cap) {
    return 1;
  }

  nc = ed_grow_cap(e->lines_cap, needed, 16);
  nb = (ed_line *)ed_grow_buf(e, e->lines, 0, nc * (lk_u32)sizeof(ed_line));

  if (!nb) {
    return 0;
  }

  e->lines = nb;
  e->lines_cap = nc;

  return 1;
}

static int ed_cursor_xy_reserve(lk_editor *e, lk_u32 pairs) {
  lk_u32 nc;
  lk_i32 *nb;

  if (pairs <= e->cursor_xy_cap) {
    return 1;
  }

  nc = ed_grow_cap(e->cursor_xy_cap, pairs, 4);
  nb = (lk_i32 *)ed_grow_buf(e, e->cursor_xy,
                             e->geom.cursor_count * 2u * (lk_u32)sizeof(lk_i32),
                             nc * 2u * (lk_u32)sizeof(lk_i32));

  if (!nb) {
    return 0;
  }

  e->cursor_xy = nb;
  e->cursor_xy_cap = nc;

  return 1;
}

static int ed_sel_rects_reserve(lk_editor *e, lk_u32 needed) {
  lk_u32 nc;
  lk_rect *nb;

  if (needed <= e->sel_rects_cap) {
    return 1;
  }

  nc = ed_grow_cap(e->sel_rects_cap, needed, 8);
  nb = (lk_rect *)ed_grow_buf(e, e->sel_rects,
                              e->geom.sel_count * (lk_u32)sizeof(lk_rect),
                              nc * (lk_u32)sizeof(lk_rect));

  if (!nb) {
    return 0;
  }

  e->sel_rects = nb;
  e->sel_rects_cap = nc;

  return 1;
}

static void ed_sel_rect_push(lk_editor *e, lk_i32 x, lk_i32 y, lk_i32 w,
                             lk_i32 h) {
  lk_rect *sr;

  if (!ed_sel_rects_reserve(e, e->geom.sel_count + 1)) {
    return;
  }

  sr = &e->sel_rects[e->geom.sel_count++];
  sr->x = x;
  sr->y = y;
  sr->w = w;
  sr->h = h;
}

static int ed_segs_reserve(lk_editor *e, lk_u32 needed) {
  lk_u32 nc;
  ed_seg *nb;

  if (needed <= e->seg_cap) {
    return 1;
  }

  nc = ed_grow_cap(e->seg_cap, needed, 16);
  nb = (ed_seg *)ed_grow_buf(e, e->segs, e->seg_count * (lk_u32)sizeof(ed_seg),
                             nc * (lk_u32)sizeof(ed_seg));

  if (!nb) {
    return 0;
  }

  e->segs = nb;
  e->seg_cap = nc;

  return 1;
}

static int ed_line_buf_reserve(lk_editor *e, lk_u32 needed) {
  lk_u32 nc;
  char *nb;

  if (needed <= e->line_buf_cap) {
    return 1;
  }

  nc = ed_grow_cap(e->line_buf_cap, needed, 128);
  nb = (char *)ed_grow_buf(e, e->line_buf, 0, nc);

  if (!nb) {
    return 0;
  }

  e->line_buf = nb;
  e->line_buf_cap = nc;

  return 1;
}

static int ed_is_cont(unsigned char b) {
  return (b & 0xC0u) == 0x80u;
}

static lk_u32 ed_snap(const lk_document *d, lk_u32 pos) {
  lk_u32 len = lk_doc_len(d);

  if (pos > len) {
    pos = len;
  }

  while (pos > 0 && pos < len && ed_is_cont(lk_doc_get_byte(d, pos))) {
    pos--;
  }

  return pos;
}

static lk_u32 ed_prev_cp(const lk_document *d, lk_u32 pos) {
  if (pos == 0) {
    return 0;
  }

  pos--;

  while (pos > 0 && ed_is_cont(lk_doc_get_byte(d, pos))) {
    pos--;
  }

  return pos;
}

static lk_u32 ed_next_cp(const lk_document *d, lk_u32 pos) {
  lk_u32 len = lk_doc_len(d);

  if (pos >= len) {
    return len;
  }

  pos += lk_utf8_cp_len(lk_doc_get_byte(d, pos));

  if (pos > len) {
    pos = len;
  }

  return pos;
}

static lk_u32 ed_cp_at(const lk_document *d, lk_u32 pos, lk_u32 doc_len) {
  unsigned char b;
  lk_u32 n;
  lk_u32 cp;

  if (pos >= doc_len) {
    return 0;
  }

  b = lk_doc_get_byte(d, pos);
  n = lk_utf8_cp_len(b);

  if (pos + n > doc_len) {
    n = 1;
  }

  switch (n) {
  case 2:
    cp = ((lk_u32)b & 0x1Fu) << 6;
    cp |= (lk_u32)lk_doc_get_byte(d, pos + 1) & 0x3Fu;
    break;

  case 3:
    cp = ((lk_u32)b & 0x0Fu) << 12;
    cp |= ((lk_u32)lk_doc_get_byte(d, pos + 1) & 0x3Fu) << 6;
    cp |= (lk_u32)lk_doc_get_byte(d, pos + 2) & 0x3Fu;
    break;

  case 4:
    cp = ((lk_u32)b & 0x07u) << 18;
    cp |= ((lk_u32)lk_doc_get_byte(d, pos + 1) & 0x3Fu) << 12;
    cp |= ((lk_u32)lk_doc_get_byte(d, pos + 2) & 0x3Fu) << 6;
    cp |= (lk_u32)lk_doc_get_byte(d, pos + 3) & 0x3Fu;
    break;

  default: cp = b; break;
  }

  return cp;
}

static int ed_is_word_cp(lk_u32 cp) {
  if ((cp >= 'a' && cp <= 'z') || (cp >= 'A' && cp <= 'Z')) {
    return 1;
  }

  if (cp >= '0' && cp <= '9') {
    return 1;
  }

  if (cp == '_') {
    return 1;
  }

  if (cp >= 0x00C0u && cp <= 0x024Fu) {
    return 1;
  }

  if (cp >= 0x0300u && cp <= 0x036Fu) {
    return 1;
  }

  if (cp >= 0x0370u && cp <= 0x03FFu) {
    return 1;
  }

  if (cp >= 0x0400u && cp <= 0x04FFu) {
    return 1;
  }

  if (cp >= 0x3040u && cp <= 0x30FFu) {
    return 1;
  }

  if (cp >= 0x4E00u && cp <= 0x9FFFu) {
    return 1;
  }

  if (cp >= 0xAC00u && cp <= 0xD7AFu) {
    return 1;
  }

  return 0;
}

static lk_u32 ed_word_left(const lk_document *d, lk_u32 pos) {
  lk_u32 doc_len = lk_doc_len(d);

  if (pos == 0) {
    return 0;
  }

  pos = ed_prev_cp(d, pos);

  while (pos > 0) {
    if (ed_is_word_cp(ed_cp_at(d, pos, doc_len))) {
      break;
    }

    pos = ed_prev_cp(d, pos);
  }

  while (pos > 0) {
    lk_u32 prev = ed_prev_cp(d, pos);

    if (!ed_is_word_cp(ed_cp_at(d, prev, doc_len))) {
      break;
    }

    pos = prev;
  }

  return pos;
}

static lk_u32 ed_word_right(const lk_document *d, lk_u32 pos) {
  lk_u32 doc_len = lk_doc_len(d);

  while (pos < doc_len) {
    if (!ed_is_word_cp(ed_cp_at(d, pos, doc_len))) {
      break;
    }

    pos = ed_next_cp(d, pos);
  }

  while (pos < doc_len) {
    if (ed_is_word_cp(ed_cp_at(d, pos, doc_len))) {
      break;
    }

    pos = ed_next_cp(d, pos);
  }

  return pos;
}

static int ed_word_at(const lk_document *d, lk_u32 pos, lk_u32 *out_lo,
                      lk_u32 *out_hi) {
  lk_u32 doc_len = lk_doc_len(d);
  lk_u32 lo;
  lk_u32 hi;

  if (!ed_is_word_cp(ed_cp_at(d, pos, doc_len))) {
    if (pos == 0 || !ed_is_word_cp(ed_cp_at(d, ed_prev_cp(d, pos), doc_len))) {
      return 0;
    }

    pos = ed_prev_cp(d, pos);
  }

  lo = pos;
  hi = pos;

  while (lo > 0) {
    lk_u32 prev = ed_prev_cp(d, lo);

    if (!ed_is_word_cp(ed_cp_at(d, prev, doc_len))) {
      break;
    }

    lo = prev;
  }

  while (hi < doc_len && ed_is_word_cp(ed_cp_at(d, hi, doc_len))) {
    hi = ed_next_cp(d, hi);
  }

  *out_lo = lo;
  *out_hi = hi;

  return hi > lo;
}

static lk_i32 ed_advance(const lk_editor *e) {
  return e->space_adv > 0 ? e->space_adv : ED_FALLBACK_ADVANCE;
}

static lk_i32 ed_tab_px(const lk_editor *e) {
  lk_i32 px = (lk_i32)e->tab_size * ed_advance(e);

  return px > 0 ? px : 1;
}

static lk_u32 ed_count_cp(const char *p, lk_u32 len, lk_u32 ix) {
  lk_u32 i = 0;
  lk_u32 n = 0;

  if (ix > len) {
    ix = len;
  }

  while (i < ix) {
    i = lk_utf8_next(p, len, i);
    n++;
  }

  return n;
}

static lk_i32 ed_run_x(const lk_editor *e, const lk_text_backend *tb,
                       const char *p, lk_u32 len, lk_u32 ix) {
  lk_str run;

  if (ix > len) {
    ix = len;
  }

  if (tb) {
    run.ptr = p;
    run.len = len;

    return tb->x_from_index(tb->ud, run, e->font_id, e->font_size, ix);
  }

  return (lk_i32)ed_count_cp(p, len, ix) * ed_advance(e);
}

static lk_u32 ed_run_ix(const lk_editor *e, const lk_text_backend *tb,
                        const char *p, lk_u32 len, lk_i32 x) {
  lk_str run;
  lk_i32 adv;
  lk_u32 boundary;
  lk_u32 ncp;
  lk_u32 i;
  lk_u32 seen;

  if (tb) {
    run.ptr = p;
    run.len = len;

    return tb->index_from_x(tb->ud, run, e->font_id, e->font_size, x);
  }

  if (x <= 0) {
    return 0;
  }

  adv = ed_advance(e);
  boundary = (lk_u32)(x + adv / 2) / (lk_u32)adv;
  ncp = ed_count_cp(p, len, len);

  if (boundary > ncp) {
    boundary = ncp;
  }

  i = 0;
  seen = 0;

  while (i < len && seen < boundary) {
    i = lk_utf8_next(p, len, i);
    seen++;
  }

  return i;
}

static lk_i32 ed_line_x_from_ix(const lk_editor *e, const lk_text_backend *tb,
                                const char *p, lk_u32 len, lk_u32 ix) {
  lk_i32 x = 0;
  lk_u32 i = 0;
  lk_i32 tabpx = ed_tab_px(e);

  if (ix > len) {
    ix = len;
  }

  while (i < len) {
    lk_u32 j = i;

    while (j < len && p[j] != '\t') {
      j++;
    }

    if (ix <= j) {
      return x + ed_run_x(e, tb, p + i, j - i, ix - i);
    }

    x += ed_run_x(e, tb, p + i, j - i, j - i);

    if (j < len) {
      x = (x / tabpx + 1) * tabpx;
      i = j + 1;
    } else {
      i = j;
    }
  }

  return x;
}

static lk_u32 ed_line_ix_from_x(const lk_editor *e, const lk_text_backend *tb,
                                const char *p, lk_u32 len, lk_i32 x) {
  lk_i32 seg_x = 0;
  lk_u32 i = 0;
  lk_i32 tabpx = ed_tab_px(e);

  if (x <= 0) {
    return 0;
  }

  while (i < len) {
    lk_u32 j = i;
    lk_i32 seg_w;
    lk_i32 seg_end;
    lk_i32 stop;

    while (j < len && p[j] != '\t') {
      j++;
    }

    seg_w = ed_run_x(e, tb, p + i, j - i, j - i);
    seg_end = seg_x + seg_w;

    if (x <= seg_end || j >= len) {
      return i + ed_run_ix(e, tb, p + i, j - i, x - seg_x);
    }

    stop = (seg_end / tabpx + 1) * tabpx;

    if (x < stop) {
      return (x - seg_end < stop - x) ? j : j + 1;
    }

    seg_x = stop;
    i = j + 1;
  }

  return len;
}

static int ed_push_seg(lk_editor *e, lk_u32 off, lk_u32 len, lk_i32 x, lk_i32 y,
                       lk_i32 w, lk_u8 flags, lk_color fg, lk_color bg) {
  ed_seg seg;

  if (!ed_segs_reserve(e, e->seg_count + 1)) {
    return 0;
  }

  seg.off = off;
  seg.len = len;
  seg.x = x;
  seg.y = y;
  seg.w = w;
  seg.flags = flags;
  seg.fg = fg;
  seg.bg = bg;
  e->segs[e->seg_count++] = seg;

  return 1;
}

static lk_u32 ed_snap_run_ix(const char *run, lk_u32 len, lk_u32 ix) {
  if (ix > len) {
    ix = len;
  }

  while (ix > 0 && !lk_utf8_is_boundary(run, len, ix)) {
    ix--;
  }

  return ix;
}

static int ed_emit_run(lk_editor *e, const lk_text_backend *tb, const char *run,
                       lk_u32 run_len, lk_u32 vis_off, lk_u32 doc_off, lk_i32 x,
                       lk_i32 y, lk_i32 w, int spans_on) {
  lk_color none;
  lk_u32 pos;
  lk_u32 si;

  memset(&none, 0, sizeof(none));

  if (!spans_on) {
    return ed_push_seg(e, vis_off, run_len, x, y, w, 0, none, none);
  }

  pos = 0;

  for (si = 0; si < e->span_count; si++) {
    const lk_edit_span *sp = &e->spans[si];
    lk_u32 a;
    lk_u32 b;

    if (sp->end <= doc_off || sp->start >= doc_off + run_len) {
      continue;
    }

    a = sp->start > doc_off ? sp->start - doc_off : 0;
    b = sp->end < doc_off + run_len ? sp->end - doc_off : run_len;
    a = ed_snap_run_ix(run, run_len, a);
    b = ed_snap_run_ix(run, run_len, b);

    if (a < pos) {
      a = pos;
    }

    if (b <= a) {
      continue;
    }

    if (a > pos) {
      lk_i32 x0 = x + ed_run_x(e, tb, run, run_len, pos);
      lk_i32 x1 = x + ed_run_x(e, tb, run, run_len, a);

      if (!ed_push_seg(e, vis_off + pos, a - pos, x0, y, x1 - x0, 0, none,
                       none)) {
        return 0;
      }
    }

    {
      lk_i32 x0 = x + ed_run_x(e, tb, run, run_len, a);
      lk_i32 x1 = x + ed_run_x(e, tb, run, run_len, b);

      if (!ed_push_seg(e, vis_off + a, b - a, x0, y, x1 - x0, sp->flags, sp->fg,
                       sp->bg)) {
        return 0;
      }
    }

    pos = b;
  }

  if (pos < run_len) {
    lk_i32 x0 = x + ed_run_x(e, tb, run, run_len, pos);

    if (!ed_push_seg(e, vis_off + pos, run_len - pos, x0, y, x + w - x0, 0,
                     none, none)) {
      return 0;
    }
  }

  return 1;
}

static const char *ed_line_text(lk_editor *e, lk_u32 line, lk_u32 *out_len,
                                lk_u32 *out_start) {
  lk_u32 ls = lk_doc_line_start(e->doc, line);
  lk_u32 le = lk_doc_line_end(e->doc, line);
  lk_u32 len = le - ls;

  *out_len = len;
  *out_start = ls;

  if (len == 0) {
    return e->line_buf ? e->line_buf : "";
  }

  if (!ed_line_buf_reserve(e, len)) {
    *out_len = 0;

    return NULL;
  }

  lk_doc_get_text(e->doc, ls, e->line_buf, len);

  return e->line_buf;
}

static int ed_wrap_buf_reserve(lk_editor *e, lk_u32 needed) {
  lk_u32 nc;
  char *nb;

  if (needed <= e->wrap_buf_cap) {
    return 1;
  }

  nc = ed_grow_cap(e->wrap_buf_cap, needed, 128);
  nb = (char *)ed_grow_buf(e, e->wrap_buf, 0, nc);

  if (!nb) {
    return 0;
  }

  e->wrap_buf = nb;
  e->wrap_buf_cap = nc;

  return 1;
}

static int ed_wrapping(const lk_editor *e) {
  return e->wrap_mode != LK_EDITOR_WRAP_NONE && e->wrap != NULL &&
         e->wrap_w > 0;
}

static int ed_break_push(lk_editor *e, ed_line_wrap *w, lk_u32 rel) {
  if (w->break_count >= w->break_cap) {
    lk_u32 nc = ed_grow_cap(w->break_cap, w->break_count + 1, 8);
    lk_u32 *nb = (lk_u32 *)ed_grow_buf(e, w->breaks,
                                       w->break_count * (lk_u32)sizeof(lk_u32),
                                       nc * (lk_u32)sizeof(lk_u32));

    if (!nb) {
      return 0;
    }

    w->breaks = nb;
    w->break_cap = nc;
  }

  w->breaks[w->break_count++] = rel;

  return 1;
}

static lk_u32 ed_fit_prefix(const lk_editor *e, const lk_text_backend *tb,
                            const char *seg, lk_u32 len, lk_i32 avail) {
  lk_u32 ix;

  if (avail <= 0) {
    return 0;
  }

  ix = ed_run_ix(e, tb, seg, len, avail);

  if (ix > len) {
    ix = len;
  }

  if (ix > 0 && ed_run_x(e, tb, seg, len, ix) > avail) {
    ix = lk_utf8_prev(seg, len, ix);
  }

  return ix;
}

static int ed_measure_breaks(lk_editor *e, const lk_text_backend *tb,
                             ed_line_wrap *w, const char *p, lk_u32 len,
                             lk_i32 *out_last_x) {
  lk_i32 width = e->wrap_w;
  lk_i32 tabpx = ed_tab_px(e);
  lk_i32 x = 0;
  lk_u32 i = 0;
  lk_u32 row_start = 0;

  w->break_count = 0;

  while (i < len) {
    if (p[i] == '\t') {
      lk_i32 stop = (x / tabpx + 1) * tabpx;

      if (stop > width && i > row_start) {
        if (!ed_break_push(e, w, i)) {
          return 0;
        }

        row_start = i;
        x = 0;
        continue;
      }

      x = stop;
      i++;
      continue;
    }

    {
      lk_u32 j = i;
      lk_i32 seg_w;
      lk_u32 fit;

      while (j < len && p[j] != '\t') {
        j++;
      }

      seg_w = ed_run_x(e, tb, p + i, j - i, j - i);

      if (x + seg_w <= width) {
        x += seg_w;
        i = j;
        continue;
      }

      fit = ed_fit_prefix(e, tb, p + i, j - i, width - x);

      if (fit == 0 && i == row_start) {

        fit = lk_utf8_next(p, len, i) - i;
      }

      if (i + fit >= len) {
        x += ed_run_x(e, tb, p + i, j - i, fit);
        i += fit;
        continue;
      }

      {
        lk_u32 brk = i + fit;

        if (e->wrap_mode == LK_EDITOR_WRAP_WORD) {

          lk_u32 b = brk;

          while (b < j && p[b] == ' ') {
            b++;
          }

          if (b == brk) {
            while (b > row_start && p[b - 1] != ' ' && p[b - 1] != '\t') {
              b--;
            }
          }

          if (b >= len) {
            x = width;
            i = len;
            continue;
          }

          if (b > row_start) {
            brk = b;
          }
        }

        if (!ed_break_push(e, w, brk)) {
          return 0;
        }

        i = brk;
        row_start = i;
        x = 0;
      }
    }
  }

  *out_last_x = x;

  return 1;
}

static const ed_line_wrap *ed_wrap_line(lk_editor *e, const lk_text_backend *tb,
                                        lk_u32 line) {
  ed_line_wrap *w;
  lk_u32 ls;
  lk_u32 le;
  lk_u32 len;
  lk_i32 last_x = 0;

  if (!ed_wrapping(e) || line >= e->wrap_count) {
    return NULL;
  }

  w = &e->wrap[line];

  if (w->generation == e->wrap_generation) {
    return w;
  }

  ls = lk_doc_line_start(e->doc, line);
  le = lk_doc_line_end(e->doc, line);
  len = le - ls;
  w->break_count = 0;

  if (len > 0) {
    if (!ed_wrap_buf_reserve(e, len)) {
      return NULL;
    }

    lk_doc_get_text(e->doc, ls, e->wrap_buf, len);

    if (!ed_measure_breaks(e, tb, w, e->wrap_buf, len, &last_x)) {
      return NULL;
    }

    e->est_px += w->break_count * (lk_u32)e->wrap_w + (lk_u32)last_x;
    e->est_bytes += len;

    if (e->est_px > 0x40000000u) {
      e->est_px /= 2;
      e->est_bytes = e->est_bytes / 2 ? e->est_bytes / 2 : 1;
    }
  }

  w->generation = e->wrap_generation;

  return w;
}

static lk_u32 ed_row_count_of(const ed_line_wrap *w) {
  return w ? w->break_count + 1 : 1;
}

static lk_u32 ed_row_start_rel(const ed_line_wrap *w, lk_u32 row) {
  return (w && row > 0) ? w->breaks[row - 1] : 0;
}

static lk_u32 ed_row_end_rel(const ed_line_wrap *w, lk_u32 row,
                             lk_u32 line_len) {
  return (w && row < w->break_count) ? w->breaks[row] : line_len;
}

static void ed_pos_to_row(lk_editor *e, const lk_text_backend *tb, lk_u32 pos,
                          lk_u32 *out_line, lk_u32 *out_row) {
  lk_u32 line = lk_doc_pos_to_line(e->doc, pos);
  const ed_line_wrap *w = ed_wrap_line(e, tb, line);
  lk_u32 rel = pos - lk_doc_line_start(e->doc, line);
  lk_u32 row = 0;

  if (w) {
    while (row < w->break_count && rel >= w->breaks[row]) {
      row++;
    }
  }

  *out_line = line;
  *out_row = row;
}

static int ed_row_next(lk_editor *e, const lk_text_backend *tb, lk_u32 *line,
                       lk_u32 *row) {
  if (*row + 1 < ed_row_count_of(ed_wrap_line(e, tb, *line))) {
    (*row)++;

    return 1;
  }

  if (*line + 1 >= lk_doc_line_count(e->doc)) {
    return 0;
  }

  (*line)++;
  *row = 0;

  return 1;
}

static int ed_row_prev(lk_editor *e, const lk_text_backend *tb, lk_u32 *line,
                       lk_u32 *row) {
  if (*row > 0) {
    (*row)--;

    return 1;
  }

  if (*line == 0) {
    return 0;
  }

  (*line)--;
  *row = ed_row_count_of(ed_wrap_line(e, tb, *line)) - 1;

  return 1;
}

static lk_u32 ed_rows_back_n(lk_editor *e, const lk_text_backend *tb,
                             lk_u32 *line, lk_u32 *row, lk_u32 n) {
  lk_u32 done = 0;

  while (done < n && ed_row_prev(e, tb, line, row)) {
    done++;
  }

  return done;
}

static void ed_bottom_anchor(lk_editor *e, const lk_text_backend *tb, lk_u32 bl,
                             lk_u32 br, lk_i32 line_h, lk_i32 view_h,
                             lk_u32 *al, lk_u32 *ar, lk_i32 *ay) {
  lk_u32 q = (lk_u32)(view_h / line_h);
  lk_i32 r = view_h % line_h;
  lk_u32 need = (r == 0) ? (q ? q - 1 : 0) : q;
  lk_u32 got;

  *al = bl;
  *ar = br;
  got = ed_rows_back_n(e, tb, al, ar, need);
  *ay = (got < need || r == 0) ? 0 : line_h - r;
}

static lk_u32 ed_est_rows(const lk_editor *e, lk_u32 line_len) {
  lk_u64 num;
  lk_u64 den;
  lk_u64 est;

  if (!ed_wrapping(e) || line_len == 0) {
    return 1;
  }

  if (e->est_bytes > 0) {
    num = (lk_u64)e->est_px;
    den = (lk_u64)e->est_bytes;
  } else {
    num = (lk_u64)ed_advance(e);
    den = 1;
  }

  den *= (lk_u64)e->wrap_w;
  est = ((lk_u64)line_len * num + den - 1) / den;

  return est ? (lk_u32)est : 1;
}

static lk_u32 ed_rows_or_est(const lk_editor *e, lk_u32 line) {
  if (e->wrap && line < e->wrap_count &&
      e->wrap[line].generation == e->wrap_generation) {
    return e->wrap[line].break_count + 1;
  }

  return ed_est_rows(e, lk_doc_line_end(e->doc, line) -
                            lk_doc_line_start(e->doc, line));
}

static void ed_wrap_release(lk_editor *e) {
  lk_u32 i;

  if (!e->wrap) {
    return;
  }

  for (i = 0; i < e->wrap_count; i++) {
    if (e->wrap[i].breaks) {
      e->dealloc(e->ud, e->wrap[i].breaks);
    }
  }

  e->dealloc(e->ud, e->wrap);
  e->wrap = NULL;
  e->wrap_count = 0;
  e->wrap_cap = 0;
}

static int ed_wrap_reserve(lk_editor *e, lk_u32 needed) {
  lk_u32 nc;
  ed_line_wrap *nb;

  if (needed <= e->wrap_cap) {
    return 1;
  }

  nc = ed_grow_cap(e->wrap_cap, needed, 64);
  nb = (ed_line_wrap *)ed_grow_buf(e, e->wrap,
                                   e->wrap_count * (lk_u32)sizeof(ed_line_wrap),
                                   nc * (lk_u32)sizeof(ed_line_wrap));

  if (!nb) {
    return 0;
  }

  e->wrap = nb;
  e->wrap_cap = nc;

  return 1;
}

static int ed_wrap_materialize(lk_editor *e) {
  lk_u32 lc = lk_doc_line_count(e->doc);

  ed_wrap_release(e);

  if (!ed_wrap_reserve(e, lc ? lc : 1)) {
    return 0;
  }

  memset(e->wrap, 0, lc * sizeof(ed_line_wrap));
  e->wrap_count = lc;

  return 1;
}

static int ed_wrap_splice_in(lk_editor *e, lk_u32 at, lk_u32 n) {
  if (at > e->wrap_count) {
    at = e->wrap_count;
  }

  if (!ed_wrap_reserve(e, e->wrap_count + n)) {
    return 0;
  }

  if (at < e->wrap_count) {
    memmove(e->wrap + at + n, e->wrap + at,
            (e->wrap_count - at) * sizeof(ed_line_wrap));
  }

  memset(e->wrap + at, 0, n * sizeof(ed_line_wrap));
  e->wrap_count += n;

  return 1;
}

static void ed_wrap_splice_out(lk_editor *e, lk_u32 at, lk_u32 n) {
  lk_u32 i;

  if (at >= e->wrap_count) {
    return;
  }

  if (n > e->wrap_count - at) {
    n = e->wrap_count - at;
  }

  for (i = at; i < at + n; i++) {
    if (e->wrap[i].breaks) {
      e->dealloc(e->ud, e->wrap[i].breaks);
    }
  }

  if (at + n < e->wrap_count) {
    memmove(e->wrap + at, e->wrap + at + n,
            (e->wrap_count - at - n) * sizeof(ed_line_wrap));
  }

  e->wrap_count -= n;
}

static lk_u32 ed_count_nl(const char *p, lk_u32 len) {
  lk_u32 i;
  lk_u32 n = 0;

  for (i = 0; i < len; i++) {
    if (p[i] == '\n') {
      n++;
    }
  }

  return n;
}

static ed_caret *ed_primary(lk_editor *e) {
  return &e->carets[e->caret_primary];
}

static const ed_caret *ed_primary_c(const lk_editor *e) {
  return &e->carets[e->caret_primary];
}

static int ed_caret_sel_range(const ed_caret *c, lk_u32 *lo, lk_u32 *hi) {
  if (c->anchor == ED_NO_ANCHOR || c->anchor == c->cursor) {
    return 0;
  }

  if (c->anchor < c->cursor) {
    *lo = c->anchor;
    *hi = c->cursor;
  } else {
    *lo = c->cursor;
    *hi = c->anchor;
  }

  return 1;
}

static void ed_caret_range(const ed_caret *c, lk_u32 *lo, lk_u32 *hi) {
  if (!ed_caret_sel_range(c, lo, hi)) {
    *lo = c->cursor;
    *hi = c->cursor;
  }
}

static void ed_motion_begin(ed_caret *c, int select) {
  if (select) {
    if (c->anchor == ED_NO_ANCHOR) {
      c->anchor = c->cursor;
    }
  } else {
    c->anchor = ED_NO_ANCHOR;
  }
}

static int ed_carets_reserve(lk_editor *e, lk_u32 needed) {
  lk_u32 new_cap;
  ed_caret *nc;

  if (needed <= e->caret_cap) {
    return 1;
  }

  new_cap = ed_grow_cap(e->caret_cap, needed, ED_INITIAL_CARET_CAP);
  nc = (ed_caret *)ed_grow_buf(e, e->carets,
                               e->caret_cap * (lk_u32)sizeof(ed_caret),
                               new_cap * (lk_u32)sizeof(ed_caret));

  if (!nc) {
    return 0;
  }

  e->carets = nc;
  e->caret_cap = new_cap;

  return 1;
}

static void ed_collapse_to(lk_editor *e, lk_u32 pos) {
  e->caret_count = 1;
  e->caret_primary = 0;
  e->carets[0].cursor = pos;
  e->carets[0].anchor = ED_NO_ANCHOR;
  e->carets[0].sticky_x = ED_STICKY_NONE;
}

static void ed_caret_merge(lk_editor *e, lk_u32 dst_ix, const ed_caret *src,
                           int src_is_primary) {
  ed_caret *dst = &e->carets[dst_ix];
  lk_u32 dlo, dhi, slo, shi, lo, hi;
  const ed_caret *orient;

  ed_caret_range(dst, &dlo, &dhi);
  ed_caret_range(src, &slo, &shi);
  lo = dlo < slo ? dlo : slo;
  hi = dhi > shi ? dhi : shi;

  orient = dst;

  if (src_is_primary && src->anchor != ED_NO_ANCHOR) {
    orient = src;
  }

  if (lo == hi) {
    dst->cursor = lo;
    dst->anchor = ED_NO_ANCHOR;
  } else if (orient->anchor != ED_NO_ANCHOR &&
             orient->cursor < orient->anchor) {
    dst->cursor = lo;
    dst->anchor = hi;
  } else {
    dst->cursor = hi;
    dst->anchor = lo;
  }

  dst->sticky_x = ED_STICKY_NONE;

  if (src_is_primary) {
    e->caret_primary = dst_ix;
  }
}

static void ed_normalize(lk_editor *e) {
  lk_u32 i;
  lk_u32 w;

  for (i = 1; i < e->caret_count; i++) {
    ed_caret key = e->carets[i];
    int key_primary = e->caret_primary == i;
    lk_u32 klo, khi, jlo, jhi;
    lk_u32 j = i;

    ed_caret_range(&key, &klo, &khi);

    while (j > 0) {
      ed_caret_range(&e->carets[j - 1], &jlo, &jhi);

      if (jlo < klo || (jlo == klo && jhi <= khi)) {
        break;
      }

      e->carets[j] = e->carets[j - 1];

      if (e->caret_primary == j - 1) {
        e->caret_primary = j;
      }

      j--;
    }

    e->carets[j] = key;

    if (key_primary) {
      e->caret_primary = j;
    }
  }

  w = 0;

  for (i = 1; i < e->caret_count; i++) {
    const ed_caret *b = &e->carets[i];
    int b_primary = e->caret_primary == i;
    lk_u32 alo, ahi, blo, bhi;
    int merge;

    ed_caret_range(&e->carets[w], &alo, &ahi);
    ed_caret_range(b, &blo, &bhi);

    if (alo < ahi && blo < bhi) {
      merge = blo < ahi;
    } else if (alo < ahi) {
      merge = blo > alo && blo < ahi;
    } else if (blo < bhi) {
      merge = 0;
    } else {
      merge = blo == alo;
    }

    if (merge) {
      ed_caret_merge(e, w, b, b_primary);
    } else {
      w++;

      if (w != i) {
        e->carets[w] = *b;

        if (b_primary) {
          e->caret_primary = w;
        }
      }
    }
  }

  e->caret_count = w + 1;
}

static void ed_snap_slot_free(lk_editor *e, ed_caret_snap *s) {
  if (s->before) {
    e->dealloc(e->ud, s->before);
  }

  if (s->after) {
    e->dealloc(e->ud, s->after);
  }

  memset(s, 0, sizeof(*s));
}

static ed_caret *ed_carets_copy(lk_editor *e, const ed_caret *src, lk_u32 n) {
  ed_caret *c = (ed_caret *)e->alloc(e->ud, n * (lk_u32)sizeof(ed_caret));

  if (c) {
    memcpy(c, src, n * sizeof(ed_caret));
  }

  return c;
}

static void ed_snap_record(lk_editor *e, lk_u32 serial, const ed_caret *before,
                           lk_u32 before_count, lk_u32 before_primary) {
  ed_caret_snap *s = &e->snaps[e->snap_head];

  ed_snap_slot_free(e, s);
  s->before = ed_carets_copy(e, before, before_count);
  s->after = ed_carets_copy(e, e->carets, e->caret_count);

  if (!s->before || !s->after) {
    ed_snap_slot_free(e, s);
    return;
  }

  s->serial = serial;
  s->before_count = before_count;
  s->before_primary = before_primary;
  s->after_count = e->caret_count;
  s->after_primary = e->caret_primary;
  e->snap_head = (e->snap_head + 1) % ED_SNAP_RING;
}

static int ed_snap_restore(lk_editor *e, lk_u32 serial, int want_after) {
  lk_u32 i;

  for (i = 0; i < ED_SNAP_RING; i++) {
    const ed_caret_snap *s = &e->snaps[i];
    const ed_caret *set;
    lk_u32 count, primary, k;

    if (s->serial != serial) {
      continue;
    }

    set = want_after ? s->after : s->before;
    count = want_after ? s->after_count : s->before_count;
    primary = want_after ? s->after_primary : s->before_primary;

    if (!ed_carets_reserve(e, count)) {
      return 0;
    }

    for (k = 0; k < count; k++) {
      ed_caret *c = &e->carets[k];

      *c = set[k];
      c->cursor = ed_snap(e->doc, c->cursor);

      if (c->anchor != ED_NO_ANCHOR) {
        c->anchor = ed_snap(e->doc, c->anchor);
      }
    }

    e->caret_count = count;
    e->caret_primary = primary < count ? primary : 0;
    ed_normalize(e);

    return 1;
  }

  return 0;
}

static void ed_snap_adopt(lk_editor *e, const ed_caret *before,
                          lk_u32 before_count, lk_u32 before_primary) {
  lk_u32 serial;

  if (!e->hist) {
    return;
  }

  serial = lk_history_peek_undo_serial(e->hist);

  if (serial == 0 || serial <= e->last_adopted_serial) {
    return;
  }

  e->last_adopted_serial = serial;
  ed_snap_record(e, serial, before, before_count, before_primary);
}

static void ed_on_doc(void *ud, const lk_document *d,
                      const lk_doc_delta *deltas, lk_u32 n) {
  lk_editor *e = (lk_editor *)ud;

  if (n == 0) {
    return;
  }

  e->geom.valid = 0;

  {
    lk_u32 di;

    for (di = 0; di < n; di++) {
      lk_u32 p = deltas[di].start;
      lk_u32 dl = deltas[di].deleted_len;
      lk_u32 il = deltas[di].inserted_len;

      if (dl > 0) {
        if (e->vp.top_byte >= p + dl) {
          e->vp.top_byte -= dl;
        } else if (e->vp.top_byte > p) {
          e->vp.top_byte = p;
        }
      }

      if (il > 0 && e->vp.top_byte >= p) {
        e->vp.top_byte += il;
      }
    }
  }

  if (e->wrap) {
    lk_u32 di;

    for (di = 0; di < n; di++) {
      lk_u32 line = lk_doc_pos_to_line(d, deltas[di].start);
      lk_u32 del_nl = ed_count_nl(deltas[di].deleted, deltas[di].deleted_len);
      lk_u32 ins_nl = ed_count_nl(deltas[di].inserted, deltas[di].inserted_len);

      if (line >= e->wrap_count) {
        line = e->wrap_count ? e->wrap_count - 1 : 0;
      }

      if (line < e->wrap_count) {
        e->wrap[line].generation = 0;
        e->wrap[line].break_count = 0;
      }

      if (ins_nl > del_nl) {
        if (!ed_wrap_splice_in(e, line + 1, ins_nl - del_nl)) {
          ed_wrap_release(e);
          break;
        }
      } else if (del_nl > ins_nl) {
        ed_wrap_splice_out(e, line + 1, del_nl - ins_nl);
      }
    }

    if (e->wrap_count != lk_doc_line_count(d)) {
      ed_wrap_materialize(e);
    }
  } else if (e->wrap_mode != LK_EDITOR_WRAP_NONE) {
    ed_wrap_materialize(e);
  }

  if (e->span_count > 0 && lk_revision_equal(e->span_rev, deltas[0].before)) {
    lk_u32 di;
    lk_u32 si;
    lk_u32 w;

    for (di = 0; di < n; di++) {
      lk_u32 p = deltas[di].start;
      lk_u32 dl = deltas[di].deleted_len;
      lk_u32 il = deltas[di].inserted_len;

      for (si = 0; si < e->span_count; si++) {
        lk_edit_span *sp = &e->spans[si];

        if (dl > 0) {
          sp->start = sp->start <= p        ? sp->start
                      : sp->start >= p + dl ? sp->start - dl
                                            : p;
          sp->end = sp->end <= p        ? sp->end
                    : sp->end >= p + dl ? sp->end - dl
                                        : p;
        }

        if (il > 0) {
          if (sp->start > p) {
            sp->start += il;
          }

          if (sp->end >= p) {
            sp->end += il;
          }
        }
      }
    }

    w = 0;

    for (si = 0; si < e->span_count; si++) {
      if (e->spans[si].start < e->spans[si].end) {
        e->spans[w++] = e->spans[si];
      }
    }

    e->span_count = w;
    e->span_rev = deltas[n - 1].after;
  }

  if ((deltas[0].origin == LK_ORIGIN_UNDO ||
       deltas[0].origin == LK_ORIGIN_REDO) &&
      e->in_replay) {

    const lk_doc_delta *last = &deltas[n - 1];

    ed_collapse_to(e, last->inserted_len ? last->start + last->inserted_len
                                         : last->start);
    e->pending_scroll = 1;
  } else if (!e->in_command) {

    lk_u32 di;
    lk_u32 ci;

    for (di = 0; di < n; di++) {
      lk_u32 p = deltas[di].start;
      lk_u32 dl = deltas[di].deleted_len;
      lk_u32 il = deltas[di].inserted_len;

      for (ci = 0; ci < e->caret_count; ci++) {
        ed_caret *c = &e->carets[ci];

        if (dl > 0) {
          if (c->cursor >= p + dl) {
            c->cursor -= dl;
          } else if (c->cursor > p) {
            c->cursor = p;
          }

          if (c->anchor != ED_NO_ANCHOR) {
            if (c->anchor >= p + dl) {
              c->anchor -= dl;
            } else if (c->anchor > p) {
              c->anchor = p;
            }
          }
        }

        if (il > 0) {
          if (c->cursor >= p) {
            c->cursor += il;
          }

          if (c->anchor != ED_NO_ANCHOR && c->anchor >= p) {
            c->anchor += il;
          }
        }
      }
    }

    for (ci = 0; ci < e->caret_count; ci++) {
      ed_caret *c = &e->carets[ci];

      if (c->anchor == c->cursor) {
        c->anchor = ED_NO_ANCHOR;
      }

      c->sticky_x = ED_STICKY_NONE;

      c->cursor = ed_snap(d, c->cursor);
    }

    ed_normalize(e);
  }

  if (e->vp.top_byte > lk_doc_len(d)) {
    e->vp.top_byte = lk_doc_line_start(d, lk_doc_line_count(d) - 1);
    e->vp.y_offset = 0;
  }
}

lk_editor *lk_editor_new(void *(*alloc)(void *, lk_u32),
                         void (*dealloc)(void *, void *), void *ud,
                         lk_document *doc, lk_edit_history *hist) {
  lk_editor *e;

  if (!doc) {
    return NULL;
  }

  if (!alloc || !dealloc) {
    alloc = lk_sys_alloc;
    dealloc = lk_sys_dealloc;
    ud = NULL;
  }

  e = (lk_editor *)alloc(ud, (lk_u32)sizeof(lk_editor));

  if (!e) {
    return NULL;
  }

  memset(e, 0, sizeof(*e));
  e->alloc = alloc;
  e->dealloc = dealloc;
  e->ud = ud;
  e->doc = doc;
  e->hist = hist;
  e->carets =
      (ed_caret *)alloc(ud, ED_INITIAL_CARET_CAP * (lk_u32)sizeof(ed_caret));

  if (!e->carets) {
    dealloc(ud, e);
    return NULL;
  }

  e->caret_cap = ED_INITIAL_CARET_CAP;
  ed_collapse_to(e, 0);
  e->editable = 1;
  e->tab_size = ED_TAB_SIZE;
  e->line_h = ED_FALLBACK_LINE_H;
  e->space_adv = ED_FALLBACK_ADVANCE;
  e->wrap_generation = 1;
  e->sub_id = lk_doc_subscribe(doc, ed_on_doc, e);

  return e;
}

void lk_editor_destroy(lk_editor *e) {
  if (!e) {
    return;
  }

  if (e->doc && e->sub_id) {
    lk_doc_unsubscribe(e->doc, e->sub_id);
  }

  if (e->vis) {
    e->dealloc(e->ud, e->vis);
  }

  if (e->lines) {
    e->dealloc(e->ud, e->lines);
  }

  if (e->segs) {
    e->dealloc(e->ud, e->segs);
  }

  if (e->line_buf) {
    e->dealloc(e->ud, e->line_buf);
  }

  if (e->wrap_buf) {
    e->dealloc(e->ud, e->wrap_buf);
  }

  ed_wrap_release(e);

  if (e->spans) {
    e->dealloc(e->ud, e->spans);
  }

  if (e->carets) {
    e->dealloc(e->ud, e->carets);
  }

  if (e->cursor_xy) {
    e->dealloc(e->ud, e->cursor_xy);
  }

  if (e->sel_rects) {
    e->dealloc(e->ud, e->sel_rects);
  }

  {
    lk_u32 i;

    for (i = 0; i < ED_SNAP_RING; i++) {
      ed_snap_slot_free(e, &e->snaps[i]);
    }
  }

  e->dealloc(e->ud, e);
}

lk_document *lk_editor_doc(const lk_editor *e) {
  return e ? e->doc : NULL;
}

lk_u32 lk_editor_cursor(const lk_editor *e) {
  return e ? ed_primary_c(e)->cursor : 0;
}

void lk_editor_set_cursor(lk_editor *e, lk_u32 pos) {
  if (!e) {
    return;
  }

  ed_collapse_to(e, ed_snap(e->doc, pos));
  e->pending_scroll = 1;
}

int lk_editor_selection(const lk_editor *e, lk_u32 *out_start,
                        lk_u32 *out_end) {
  lk_u32 lo;
  lk_u32 hi;

  if (!e || !ed_caret_sel_range(ed_primary_c(e), &lo, &hi)) {
    return 0;
  }

  if (out_start) {
    *out_start = lo;
  }

  if (out_end) {
    *out_end = hi;
  }

  return 1;
}

lk_u32 lk_editor_caret_count(const lk_editor *e) {
  return e ? e->caret_count : 0;
}

int lk_editor_caret(const lk_editor *e, lk_u32 i, lk_u32 *out_cursor,
                    lk_u32 *out_start, lk_u32 *out_end) {
  const ed_caret *c;
  lk_u32 lo;
  lk_u32 hi;
  int sel;

  if (!e || i >= e->caret_count) {
    return 0;
  }

  c = &e->carets[i];
  sel = ed_caret_sel_range(c, &lo, &hi);

  if (out_cursor) {
    *out_cursor = c->cursor;
  }

  if (sel) {
    if (out_start) {
      *out_start = lo;
    }

    if (out_end) {
      *out_end = hi;
    }
  }

  return sel;
}

lk_editor_viewport lk_editor_get_viewport(const lk_editor *e) {
  lk_editor_viewport vp;

  vp.top_byte = 0;
  vp.y_offset = 0;

  return e ? e->vp : vp;
}

void lk_editor_scroll_to_cursor(lk_editor *e) {
  if (e) {
    e->pending_scroll = 1;
  }
}

lk_u32 lk_editor_tab_size(const lk_editor *e) {
  return e ? e->tab_size : ED_TAB_SIZE;
}

void lk_editor_set_editable(lk_editor *e, int on) {
  if (e) {
    e->editable = on ? 1 : 0;
  }
}

int lk_editor_editable(const lk_editor *e) {
  return e ? e->editable : 1;
}

int lk_editor_set_wrap_mode(lk_editor *e, lk_editor_wrap_mode m) {
  if (!e) {
    return 0;
  }

  if (m != LK_EDITOR_WRAP_NONE && m != LK_EDITOR_WRAP_CHARACTER &&
      m != LK_EDITOR_WRAP_WORD) {
    return 0;
  }

  if ((lk_u32)m == e->wrap_mode) {
    return 1;
  }

  if (m != LK_EDITOR_WRAP_NONE) {
    if (!ed_wrap_materialize(e)) {
      return 0;
    }

    e->scroll_x = 0;
  } else {
    ed_wrap_release(e);
  }

  e->wrap_mode = (lk_u32)m;
  e->wrap_generation++;
  e->geom.valid = 0;

  return 1;
}

lk_editor_wrap_mode lk_editor_wrap_mode_get(const lk_editor *e) {
  return e ? (lk_editor_wrap_mode)e->wrap_mode : LK_EDITOR_WRAP_NONE;
}

void lk_editor_invalidate_layout(lk_editor *e) {
  if (e) {
    e->wrap_generation++;
    e->geom.valid = 0;
  }
}

void lk_editor_scroll_x_wheel(lk_editor *e, lk_i32 ticks) {
  if (!e || ticks == 0 || e->wrap_mode != LK_EDITOR_WRAP_NONE) {
    return;
  }

  e->scroll_x += ticks * 3 * ed_advance(e);

  if (e->scroll_x < 0) {
    e->scroll_x = 0;
  }
}

lk_i32 lk_editor_scroll_x(const lk_editor *e) {
  return e ? e->scroll_x : 0;
}

lk_u32 lk_editor_wrap_rows(const lk_editor *e, lk_u32 line) {
  if (!e || !e->wrap || line >= e->wrap_count ||
      e->wrap[line].generation != e->wrap_generation) {
    return 0;
  }

  return e->wrap[line].break_count + 1;
}

void lk_editor_set_spans(lk_editor *e, const lk_edit_span_snapshot *snap) {
  if (!e) {
    return;
  }

  e->span_count = 0;

  if (!snap || snap->count == 0 || !snap->spans) {
    return;
  }

  if (snap->count > e->span_cap) {
    lk_u32 nc = ed_grow_cap(e->span_cap, snap->count, 8);
    lk_edit_span *nb = (lk_edit_span *)ed_grow_buf(
        e, e->spans, 0, nc * (lk_u32)sizeof(lk_edit_span));

    if (!nb) {
      return;
    }

    e->spans = nb;
    e->span_cap = nc;
  }

  memcpy(e->spans, snap->spans, snap->count * sizeof(lk_edit_span));
  e->span_count = snap->count;
  e->span_rev = snap->revision;
  e->span_range_start = snap->range_start;
  e->span_range_end = snap->range_end;

#ifdef LK_EDITOR_DEBUG_ASSERTS
  {
    lk_u32 i;

    for (i = 0; i < e->span_count; i++) {
      LK_ED_ASSERT(e->spans[i].start < e->spans[i].end);
      LK_ED_ASSERT(i == 0 || e->spans[i].start >= e->spans[i - 1].end);
    }
  }
#endif
}

void lk_editor_set_drag(lk_editor *e, int on) {
  if (e) {
    e->drag = on ? 1 : 0;
  }
}

int lk_editor_dragging(const lk_editor *e) {
  return e ? e->drag : 0;
}

static const lk_resource_type g_editor_type = {"editor", NULL};

const lk_resource_type *lk_editor_type(void) {
  return &g_editor_type;
}

lk_editor *lk_editor_from_node(const lk_resources *rs, const lk_tree *t,
                               lk_ix n) {
  const lk_node *nd;
  lk_u32 k;

  if (!rs || !t || n == 0 || n >= t->node_count) {
    return NULL;
  }

  nd = &t->nodes[n];

  for (k = 0; k < nd->props_len; k++) {
    const lk_prop *p = &t->props[nd->props_off + k];

    if (p->key == UIP_EDITOR && p->value.tag == UIV_RESOURCE) {
      return (lk_editor *)lk_resource_get(rs, lk_v_resource_ref(p->value),
                                          &g_editor_type);
    }
  }

  return NULL;
}

static void ed_begin(lk_editor *e, lk_editor_cmd_id cmd) {
  e->in_command = 1;
  lk_doc_begin(e->doc, 16u + (lk_u32)cmd);
}

static void ed_commit(lk_editor *e) {
  lk_doc_commit(e->doc);
  e->in_command = 0;
}

typedef struct ed_plan {
  lk_u32 lo, hi;
  int primary;
} ed_plan;

static int ed_apply_plans(lk_editor *e, lk_editor_cmd_id cmd, ed_plan *plans,
                          lk_u32 nplans, const char *ins, lk_u32 ins_len,
                          int do_insert) {
  lk_u32 i;
  lk_u32 w;
  lk_u32 del_total = 0;
  lk_u32 ins_total = 0;
  int any = 0;
  ed_caret *before = NULL;
  lk_u32 before_count = e->caret_count;
  lk_u32 before_primary = e->caret_primary;

  if (do_insert && (!ins || ins_len == 0)) {
    do_insert = 0;
  }

  for (i = 0; i < nplans; i++) {
    if (plans[i].hi > plans[i].lo || do_insert) {
      any = 1;
      break;
    }
  }

  if (!any) {
    return 0;
  }

  plans[e->caret_primary].primary = 1;

  for (i = 1; i < nplans; i++) {
    ed_plan key = plans[i];
    lk_u32 j = i;

    while (j > 0 && (plans[j - 1].lo > key.lo ||
                     (plans[j - 1].lo == key.lo && plans[j - 1].hi > key.hi))) {
      plans[j] = plans[j - 1];
      j--;
    }

    plans[j] = key;
  }

  w = 0;

  for (i = 1; i < nplans; i++) {
    const ed_plan *b = &plans[i];
    ed_plan *a = &plans[w];
    int merge;

    if (a->lo < a->hi && b->lo < b->hi) {
      merge = b->lo < a->hi;
    } else if (a->lo < a->hi) {
      merge = b->lo > a->lo && b->lo < a->hi;
    } else if (b->lo < b->hi) {
      merge = 0;
    } else {
      merge = b->lo == a->lo;
    }

    if (merge) {
      if (b->hi > a->hi) {
        a->hi = b->hi;
      }

      a->primary |= b->primary;
    } else {
      w++;
      plans[w] = *b;
    }
  }

  nplans = w + 1;

  before = ed_carets_copy(e, e->carets, e->caret_count);

  ed_begin(e, cmd);
  any = 0;

  for (i = 0; i < nplans; i++) {
    lk_u32 adj = (plans[i].lo - del_total) + ins_total;
    lk_u32 dlen = plans[i].hi - plans[i].lo;
    ed_caret *c = &e->carets[i];

    if (dlen > 0) {
      any |= lk_doc_delete(e->doc, adj, dlen);
    }

    if (do_insert) {
      any |= lk_doc_insert(e->doc, adj, ins, ins_len);
    }

    c->cursor = adj + (do_insert ? ins_len : 0);
    c->anchor = ED_NO_ANCHOR;
    c->sticky_x = ED_STICKY_NONE;

    if (plans[i].primary) {
      e->caret_primary = i;
    }

    del_total += dlen;
    ins_total += do_insert ? ins_len : 0;
  }

  ed_commit(e);
  e->caret_count = nplans;
  ed_normalize(e);
  e->pending_scroll = 1;

  if (any && before) {
    ed_snap_adopt(e, before, before_count, before_primary);
  }

  if (before) {
    e->dealloc(e->ud, before);
  }

  return any;
}

static ed_plan *ed_plans_new(lk_editor *e) {
  ed_plan *plans =
      (ed_plan *)e->alloc(e->ud, e->caret_count * (lk_u32)sizeof(ed_plan));
  lk_u32 i;

  if (!plans) {
    return NULL;
  }

  for (i = 0; i < e->caret_count; i++) {
    lk_u32 lo;
    lk_u32 hi;

    if (!ed_caret_sel_range(&e->carets[i], &lo, &hi)) {
      lo = e->carets[i].cursor;
      hi = lo;

      if (lo > lk_doc_len(e->doc)) {
        lo = hi = lk_doc_len(e->doc);
      }
    }

    plans[i].lo = lo;
    plans[i].hi = hi;
    plans[i].primary = 0;
  }

  return plans;
}

static int ed_edit_insert(lk_editor *e, lk_editor_cmd_id cmd, const char *ptr,
                          lk_u32 len) {
  ed_plan *plans;
  int did;

  if (!ptr || len == 0) {
    return 0;
  }

  plans = ed_plans_new(e);

  if (!plans) {
    return 0;
  }

  did = ed_apply_plans(e, cmd, plans, e->caret_count, ptr, len, 1);
  e->dealloc(e->ud, plans);

  return did;
}

static int ed_edit_delete(lk_editor *e, lk_editor_cmd_id cmd,
                          void (*derive)(const lk_document *, lk_u32, lk_u32 *,
                                         lk_u32 *)) {
  ed_plan *plans = ed_plans_new(e);
  lk_u32 i;
  int did;

  if (!plans) {
    return 0;
  }

  for (i = 0; i < e->caret_count; i++) {
    if (plans[i].lo == plans[i].hi && derive) {
      derive(e->doc, plans[i].lo, &plans[i].lo, &plans[i].hi);
    }
  }

  did = ed_apply_plans(e, cmd, plans, e->caret_count, NULL, 0, 0);
  e->dealloc(e->ud, plans);

  return did;
}

static void ed_derive_back_cp(const lk_document *d, lk_u32 pos, lk_u32 *lo,
                              lk_u32 *hi) {
  *lo = pos > 0 ? ed_prev_cp(d, pos) : pos;
  *hi = pos;
}

static void ed_derive_fwd_cp(const lk_document *d, lk_u32 pos, lk_u32 *lo,
                             lk_u32 *hi) {
  *lo = pos;
  *hi = ed_next_cp(d, pos);
}

static void ed_derive_back_word(const lk_document *d, lk_u32 pos, lk_u32 *lo,
                                lk_u32 *hi) {
  *lo = ed_word_left(d, pos);
  *hi = pos;
}

static void ed_derive_fwd_word(const lk_document *d, lk_u32 pos, lk_u32 *lo,
                               lk_u32 *hi) {
  *lo = pos;
  *hi = ed_word_right(d, pos);
}

static int ed_copy(lk_editor *e, lk_ui *ui) {
  lk_u32 lo;
  lk_u32 hi;
  lk_u32 total = 0;
  lk_u32 pieces = 0;
  lk_u32 i;
  char *buf;
  char *p;

  if (!ui || !ui->clipboard_set) {
    return 0;
  }

  for (i = 0; i < e->caret_count; i++) {
    if (ed_caret_sel_range(&e->carets[i], &lo, &hi)) {
      total += hi - lo;
      pieces++;
    }
  }

  if (pieces == 0) {
    return 0;
  }

  buf = (char *)e->alloc(e->ud, total + pieces);

  if (!buf) {
    return 0;
  }

  p = buf;

  for (i = 0; i < e->caret_count; i++) {
    if (!ed_caret_sel_range(&e->carets[i], &lo, &hi)) {
      continue;
    }

    if (p != buf) {
      *p++ = '\n';
    }

    lk_doc_get_text(e->doc, lo, p, hi - lo);
    p += hi - lo;
  }

  *p = '\0';
  ui->clipboard_set(ui->clipboard_ud, buf);
  e->dealloc(e->ud, buf);

  return 1;
}

static int ed_scrollbar_geom(const lk_editor *e, lk_i32 *track_y,
                             lk_i32 *track_h, lk_i32 *thumb_y, lk_i32 *thumb_h);

static lk_i32 ed_page_size(const lk_editor *e) {
  return e->page_lines > 0 ? e->page_lines : ED_FALLBACK_PAGE_LINES;
}

static void ed_row_bounds(lk_editor *e, const lk_text_backend *tb, lk_u32 line,
                          lk_u32 row, lk_u32 *out_start, lk_u32 *out_len,
                          int *out_final) {
  const ed_line_wrap *w = ed_wrap_line(e, tb, line);
  lk_u32 ls = lk_doc_line_start(e->doc, line);
  lk_u32 llen = lk_doc_line_end(e->doc, line) - ls;
  lk_u32 rs = ed_row_start_rel(w, row);

  *out_start = ls + rs;
  *out_len = ed_row_end_rel(w, row, llen) - rs;

  if (out_final) {
    *out_final = !w || row >= w->break_count;
  }
}

static int ed_move_vert(lk_editor *e, lk_ui *ui, ed_caret *c, lk_i32 delta,
                        int select) {
  const lk_text_backend *tb = ui ? ui->text : NULL;
  lk_u32 old_cursor = c->cursor;
  lk_u32 lo;
  lk_u32 hi;
  int had_sel = ed_caret_sel_range(c, &lo, &hi);
  lk_u32 cl;
  lk_u32 cr;
  lk_u32 lcount = lk_doc_line_count(e->doc);
  int at_last;

  ed_pos_to_row(e, tb, c->cursor, &cl, &cr);
  at_last =
      cl >= lcount - 1 && cr + 1 >= ed_row_count_of(ed_wrap_line(e, tb, cl));
  ed_motion_begin(c, select);

  if (delta < 0 && cl == 0 && cr == 0) {
    c->cursor = 0;
  } else if (delta > 0 && at_last) {
    c->cursor = lk_doc_len(e->doc);
  } else {
    lk_u32 n = (lk_u32)(delta < 0 ? -delta : delta);
    lk_u32 k;
    lk_u32 rstart;
    lk_u32 rlen;
    int final;
    const char *text;
    lk_u32 tlen;
    lk_u32 tstart;
    lk_u32 ix;

    if (c->sticky_x < 0) {
      ed_row_bounds(e, tb, cl, cr, &rstart, &rlen, NULL);
      text = ed_line_text(e, cl, &tlen, &tstart);

      if (!text) {
        return 0;
      }

      c->sticky_x = ed_line_x_from_ix(e, tb, text + (rstart - tstart), rlen,
                                      c->cursor - rstart);
    }

    for (k = 0; k < n; k++) {
      if (delta < 0 ? !ed_row_prev(e, tb, &cl, &cr)
                    : !ed_row_next(e, tb, &cl, &cr)) {
        break;
      }
    }

    ed_row_bounds(e, tb, cl, cr, &rstart, &rlen, &final);
    text = ed_line_text(e, cl, &tlen, &tstart);

    if (!text) {
      return 0;
    }

    ix = ed_line_ix_from_x(e, tb, text + (rstart - tstart), rlen, c->sticky_x);

    if (!final && ix >= rlen && rlen > 0) {
      ix = lk_utf8_prev(text + (rstart - tstart), rlen, rlen);
    }

    c->cursor = rstart + ix;
  }

  e->pending_scroll = 1;

  return c->cursor != old_cursor || (had_sel && !select);
}

static int ed_move_to(lk_editor *e, ed_caret *c, lk_u32 pos, int select) {
  lk_u32 lo;
  lk_u32 hi;
  int had_sel = ed_caret_sel_range(c, &lo, &hi);
  lk_u32 old_cursor = c->cursor;

  ed_motion_begin(c, select);
  c->cursor = pos;
  c->sticky_x = ED_STICKY_NONE;
  e->pending_scroll = 1;

  return c->cursor != old_cursor || (had_sel && !select);
}

int lk_editor_command(lk_editor *e, lk_ui *ui, lk_editor_cmd_id cmd,
                      const lk_editor_cmd_arg *arg) {
  int select;
  lk_u32 lo;
  lk_u32 hi;
  lk_u32 i;
  int changed;

  if (!e) {
    return 0;
  }

  if (!e->editable) {
    switch (cmd) {
    case LK_ED_INSERT_TEXT:
    case LK_ED_DELETE_BACKWARD:
    case LK_ED_DELETE_FORWARD:
    case LK_ED_DELETE_WORD_BACKWARD:
    case LK_ED_DELETE_WORD_FORWARD:
    case LK_ED_CUT:
    case LK_ED_PASTE:
    case LK_ED_UNDO:
    case LK_ED_REDO: return 0;
    default: break;
    }
  }

  select = arg ? arg->select : 0;
  changed = 0;

  switch (cmd) {
  case LK_ED_INSERT_TEXT:
    if (!arg) {
      return 0;
    }

    return ed_edit_insert(e, cmd, arg->text.ptr, arg->text.len);

  case LK_ED_DELETE_BACKWARD: return ed_edit_delete(e, cmd, ed_derive_back_cp);

  case LK_ED_DELETE_FORWARD: return ed_edit_delete(e, cmd, ed_derive_fwd_cp);

  case LK_ED_DELETE_WORD_BACKWARD:
    return ed_edit_delete(e, cmd, ed_derive_back_word);

  case LK_ED_DELETE_WORD_FORWARD:
    return ed_edit_delete(e, cmd, ed_derive_fwd_word);

  case LK_ED_MOVE_LEFT:
    for (i = 0; i < e->caret_count; i++) {
      ed_caret *c = &e->carets[i];

      if (!select && ed_caret_sel_range(c, &lo, &hi)) {

        c->cursor = lo;
        c->anchor = ED_NO_ANCHOR;
        c->sticky_x = ED_STICKY_NONE;
        changed = 1;
        continue;
      }

      ed_motion_begin(c, select);

      if (c->cursor > 0) {
        c->cursor = ed_prev_cp(e->doc, c->cursor);
        c->sticky_x = ED_STICKY_NONE;
        changed = 1;
      }
    }

    ed_normalize(e);

    if (changed) {
      e->pending_scroll = 1;
    }

    return changed;

  case LK_ED_MOVE_RIGHT:
    for (i = 0; i < e->caret_count; i++) {
      ed_caret *c = &e->carets[i];

      if (!select && ed_caret_sel_range(c, &lo, &hi)) {
        c->cursor = hi;
        c->anchor = ED_NO_ANCHOR;
        c->sticky_x = ED_STICKY_NONE;
        changed = 1;
        continue;
      }

      ed_motion_begin(c, select);

      if (c->cursor < lk_doc_len(e->doc)) {
        c->cursor = ed_next_cp(e->doc, c->cursor);
        c->sticky_x = ED_STICKY_NONE;
        changed = 1;
      }
    }

    ed_normalize(e);

    if (changed) {
      e->pending_scroll = 1;
    }

    return changed;

  case LK_ED_MOVE_UP:
  case LK_ED_MOVE_DOWN: {
    lk_i32 d = cmd == LK_ED_MOVE_UP ? -1 : 1;

    for (i = 0; i < e->caret_count; i++) {
      changed |= ed_move_vert(e, ui, &e->carets[i], d, select);
    }

    ed_normalize(e);

    return changed;
  }

  case LK_ED_MOVE_WORD_LEFT:
  case LK_ED_MOVE_WORD_RIGHT:
    for (i = 0; i < e->caret_count; i++) {
      ed_caret *c = &e->carets[i];
      lk_u32 target = cmd == LK_ED_MOVE_WORD_LEFT
                          ? ed_word_left(e->doc, c->cursor)
                          : ed_word_right(e->doc, c->cursor);
      int had_sel = ed_caret_sel_range(c, &lo, &hi);

      ed_motion_begin(c, select);

      if (target != c->cursor || (had_sel && !select)) {
        c->cursor = target;
        c->sticky_x = ED_STICKY_NONE;
        changed = 1;
      }
    }

    ed_normalize(e);

    if (changed) {
      e->pending_scroll = 1;
    }

    return changed;

  case LK_ED_MOVE_LINE_START:
    for (i = 0; i < e->caret_count; i++) {
      ed_caret *c = &e->carets[i];

      changed |= ed_move_to(
          e, c,
          lk_doc_line_start(e->doc, lk_doc_pos_to_line(e->doc, c->cursor)),
          select);
    }

    ed_normalize(e);

    return changed;

  case LK_ED_MOVE_LINE_END:
    for (i = 0; i < e->caret_count; i++) {
      ed_caret *c = &e->carets[i];

      changed |= ed_move_to(
          e, c, lk_doc_line_end(e->doc, lk_doc_pos_to_line(e->doc, c->cursor)),
          select);
    }

    ed_normalize(e);

    return changed;

  case LK_ED_MOVE_ROW_START:
  case LK_ED_MOVE_ROW_END:
    for (i = 0; i < e->caret_count; i++) {
      ed_caret *c = &e->carets[i];
      lk_u32 cl;
      lk_u32 cr;
      lk_u32 rstart;
      lk_u32 rlen;

      ed_pos_to_row(e, ui ? ui->text : NULL, c->cursor, &cl, &cr);
      ed_row_bounds(e, ui ? ui->text : NULL, cl, cr, &rstart, &rlen, NULL);

      changed |= ed_move_to(
          e, c, cmd == LK_ED_MOVE_ROW_START ? rstart : rstart + rlen, select);
    }

    ed_normalize(e);

    return changed;

  case LK_ED_MOVE_DOC_START:
  case LK_ED_MOVE_DOC_END: {
    lk_u32 pos = cmd == LK_ED_MOVE_DOC_START ? 0 : lk_doc_len(e->doc);

    for (i = 0; i < e->caret_count; i++) {
      changed |= ed_move_to(e, &e->carets[i], pos, select);
    }

    ed_normalize(e);

    return changed;
  }

  case LK_ED_MOVE_PAGE_UP:
  case LK_ED_MOVE_PAGE_DOWN: {
    lk_i32 d = cmd == LK_ED_MOVE_PAGE_UP ? -ed_page_size(e) : ed_page_size(e);

    for (i = 0; i < e->caret_count; i++) {
      changed |= ed_move_vert(e, ui, &e->carets[i], d, select);
    }

    ed_normalize(e);

    return changed;
  }

  case LK_ED_SELECT_ALL:
    if (lk_doc_len(e->doc) == 0) {
      return 0;
    }

    ed_collapse_to(e, lk_doc_len(e->doc));
    e->carets[0].anchor = 0;

    return 1;

  case LK_ED_COPY: return ed_copy(e, ui);

  case LK_ED_CUT:
    if (!ed_copy(e, ui)) {
      return 0;
    }

    return ed_edit_delete(e, cmd, NULL);

  case LK_ED_PASTE: {
    const char *clip;

    if (!ui || !ui->clipboard_get) {
      return 0;
    }

    clip = ui->clipboard_get(ui->clipboard_ud);

    if (!clip || clip[0] == '\0') {
      return 0;
    }

    return ed_edit_insert(e, cmd, clip, (lk_u32)strlen(clip));
  }

  case LK_ED_UNDO: {
    int ok;
    lk_u32 serial;

    if (!e->hist) {
      return 0;
    }

    serial = lk_history_peek_undo_serial(e->hist);
    e->in_replay = 1;
    ok = lk_history_undo(e->hist, e->doc);
    e->in_replay = 0;

    if (ok && serial != 0) {
      ed_snap_restore(e, serial, 0);
    }

    return ok;
  }

  case LK_ED_REDO: {
    int ok;
    lk_u32 serial;

    if (!e->hist) {
      return 0;
    }

    serial = lk_history_peek_redo_serial(e->hist);
    e->in_replay = 1;
    ok = lk_history_redo(e->hist, e->doc);
    e->in_replay = 0;

    if (ok && serial != 0) {
      ed_snap_restore(e, serial, 1);
    }

    return ok;
  }

  case LK_ED_SET_CURSOR: {
    lk_u32 pos;

    if (!arg) {
      return 0;
    }

    pos = ed_snap(e->doc, arg->set_cursor.pos);

    if (arg->set_cursor.extend) {

      ed_caret *c = ed_primary(e);

      if (c->anchor == ED_NO_ANCHOR) {
        c->anchor = c->cursor;
      }

      c->cursor = pos;
      c->sticky_x = ED_STICKY_NONE;
      ed_normalize(e);
    } else {
      ed_collapse_to(e, pos);
    }

    e->pending_scroll = 1;

    return 1;
  }

  case LK_ED_ADD_CURSOR_AT: {
    lk_u32 pos;

    if (!arg) {
      return 0;
    }

    pos = ed_snap(e->doc, arg->set_cursor.pos);

    for (i = 0; i < e->caret_count; i++) {
      if (e->carets[i].cursor == pos) {
        lk_u32 r = i;

        if (e->caret_count == 1) {
          return 0;
        }

        for (; i + 1 < e->caret_count; i++) {
          e->carets[i] = e->carets[i + 1];
        }

        e->caret_count--;

        if (e->caret_primary == r) {
          e->caret_primary = r < e->caret_count ? r : e->caret_count - 1;
        } else if (e->caret_primary > r) {
          e->caret_primary--;
        }

        return 1;
      }
    }

    if (e->caret_count >= ED_MAX_CARETS ||
        !ed_carets_reserve(e, e->caret_count + 1)) {
      return 0;
    }

    e->carets[e->caret_count].cursor = pos;
    e->carets[e->caret_count].anchor = ED_NO_ANCHOR;
    e->carets[e->caret_count].sticky_x = ED_STICKY_NONE;
    e->caret_primary = e->caret_count;
    e->caret_count++;
    ed_normalize(e);
    e->pending_scroll = 1;

    return 1;
  }

  case LK_ED_ADD_CURSOR_ABOVE:
  case LK_ED_ADD_CURSOR_BELOW: {

    lk_i32 d = cmd == LK_ED_ADD_CURSOR_ABOVE ? -1 : 1;
    lk_u32 n0 = e->caret_count;
    lk_u32 old_primary = e->caret_primary;
    lk_u32 child = 0xFFFFFFFFu;
    const lk_text_backend *tb = ui ? ui->text : NULL;
    lk_u32 want = n0 * 2u <= ED_MAX_CARETS ? n0 * 2u : ED_MAX_CARETS;

    if (!ed_carets_reserve(e, want)) {
      return 0;
    }

    for (i = 0; i < n0; i++) {
      ed_caret clone;
      lk_u32 l0;
      lk_u32 r0;
      lk_u32 l1;
      lk_u32 r1;

      if (e->caret_count >= ED_MAX_CARETS) {
        break;
      }

      clone = e->carets[i];
      ed_pos_to_row(e, tb, clone.cursor, &l0, &r0);
      ed_move_vert(e, ui, &clone, d, 0);
      ed_pos_to_row(e, tb, clone.cursor, &l1, &r1);

      if (l1 == l0 && r1 == r0) {
        continue;
      }

      e->carets[e->caret_count] = clone;

      if (i == old_primary) {
        child = e->caret_count;
      }

      e->caret_count++;
      changed = 1;
    }

    if (child != 0xFFFFFFFFu) {
      e->caret_primary = child;
    }

    ed_normalize(e);

    if (changed) {
      e->pending_scroll = 1;
    }

    return changed;
  }

  case LK_ED_SELECT_NEXT_MATCH: {
    lk_u32 plo;
    lk_u32 phi;

    if (!ed_caret_sel_range(ed_primary(e), &plo, &phi)) {

      lk_u32 wlo;
      lk_u32 whi;

      if (!ed_word_at(e->doc, ed_primary(e)->cursor, &wlo, &whi)) {
        return 0;
      }

      for (i = 0; i < e->caret_count; i++) {
        ed_caret *c = &e->carets[i];
        lk_u32 clo;
        lk_u32 chi;

        if (ed_caret_sel_range(c, &clo, &chi)) {
          continue;
        }

        if (i == e->caret_primary) {
          clo = wlo;
          chi = whi;
        } else if (!ed_word_at(e->doc, c->cursor, &clo, &chi)) {
          continue;
        }

        c->anchor = clo;
        c->cursor = chi;
        c->sticky_x = ED_STICKY_NONE;
        changed = 1;
      }

      ed_normalize(e);
      e->pending_scroll = 1;

      return changed;
    }

    {
      lk_u32 nlen = phi - plo;
      char *needle;
      lk_u32 from;
      lk_u32 mpos = 0;
      int wrapped = 0;
      int found = 0;

      if (e->caret_count >= ED_MAX_CARETS ||
          !ed_carets_reserve(e, e->caret_count + 1)) {
        return 0;
      }

      needle = (char *)e->alloc(e->ud, nlen);

      if (!needle) {
        return 0;
      }

      lk_doc_get_text(e->doc, plo, needle, nlen);
      from = phi;

      for (;;) {
        if (!lk_doc_find(e->doc, needle, nlen, from, &mpos)) {
          if (wrapped) {
            break;
          }

          wrapped = 1;
          from = 0;
          continue;
        }

        if (wrapped && mpos >= plo) {
          break;
        }

        {
          int overlaps = 0;

          for (i = 0; i < e->caret_count; i++) {
            lk_u32 clo;
            lk_u32 chi;

            if (ed_caret_sel_range(&e->carets[i], &clo, &chi) && mpos < chi &&
                clo < mpos + nlen) {
              overlaps = 1;
              break;
            }
          }

          if (!overlaps) {
            found = 1;
            break;
          }
        }

        from = mpos + 1;
      }

      e->dealloc(e->ud, needle);

      if (!found) {
        return 0;
      }

      e->carets[e->caret_count].anchor = mpos;
      e->carets[e->caret_count].cursor = mpos + nlen;
      e->carets[e->caret_count].sticky_x = ED_STICKY_NONE;
      e->caret_primary = e->caret_count;
      e->caret_count++;
      ed_normalize(e);
      e->pending_scroll = 1;

      return 1;
    }
  }

  case LK_ED_COLLAPSE_CURSORS: {
    ed_caret keep;

    if (e->caret_count <= 1) {
      return 0;
    }

    keep = *ed_primary(e);
    e->carets[0] = keep;
    e->caret_count = 1;
    e->caret_primary = 0;

    return 1;
  }

  case LK_ED_SCROLL_LINES: {
    lk_u32 old_top = e->vp.top_byte;
    lk_i32 old_off = e->vp.y_offset;
    lk_u32 lcount = lk_doc_line_count(e->doc);
    const lk_text_backend *tb = ui ? ui->text : NULL;

    if (!arg || arg->lines == 0) {
      return 0;
    }

    if (!ed_wrapping(e)) {
      lk_u32 line = lk_doc_pos_to_line(e->doc, e->vp.top_byte);

      if (arg->lines < 0) {
        lk_u32 d = (lk_u32)(-arg->lines);

        if (line > d) {
          line -= d;
        } else {
          line = 0;
          e->vp.y_offset = 0;
        }
      } else {
        line += (lk_u32)arg->lines;

        if (line >= lcount) {
          line = lcount - 1;
        }
      }

      e->vp.top_byte = lk_doc_line_start(e->doc, line);
    } else {
      lk_u32 al;
      lk_u32 ar;
      lk_u32 n = (lk_u32)(arg->lines < 0 ? -arg->lines : arg->lines);
      lk_u32 distant = (lk_u32)ed_page_size(e) * 4u + 8u;

      ed_pos_to_row(e, tb, e->vp.top_byte, &al, &ar);

      if (n <= distant) {

        lk_u32 k;

        for (k = 0; k < n; k++) {
          if (arg->lines < 0 ? !ed_row_prev(e, tb, &al, &ar)
                             : !ed_row_next(e, tb, &al, &ar)) {
            if (arg->lines < 0) {
              e->vp.y_offset = 0;
            }

            break;
          }
        }
      } else if (arg->lines > 0) {

        lk_u32 skip = n;
        lk_u32 avail = ed_rows_or_est(e, al) - ar;

        while (avail <= skip && al + 1 < lcount) {
          skip -= avail;
          al++;
          ar = 0;
          avail = ed_rows_or_est(e, al);
        }

        {
          lk_u32 rc = ed_row_count_of(ed_wrap_line(e, tb, al));

          ar = (al + 1 < lcount || avail > skip) ? ar + skip : rc;

          if (ar >= rc) {
            ar = rc - 1;
          }
        }
      } else {

        lk_u32 skip = n;
        lk_u32 avail = ar;

        while (avail < skip && al > 0) {
          skip -= avail;
          al--;
          avail = ed_rows_or_est(e, al);
          ar = avail;
        }

        if (avail >= skip) {
          lk_u32 rc = ed_row_count_of(ed_wrap_line(e, tb, al));

          ar = avail - skip;

          if (ar >= rc) {
            ar = rc - 1;
          }
        } else {
          al = 0;
          ar = 0;
          e->vp.y_offset = 0;
        }
      }

      e->vp.top_byte = lk_doc_line_start(e->doc, al) +
                       ed_row_start_rel(ed_wrap_line(e, tb, al), ar);
    }

    return e->vp.top_byte != old_top || e->vp.y_offset != old_off;
  }

  default: return 0;
  }
}

static int ed_rowpos_cmp(lk_u32 l1, lk_u32 r1, lk_u32 l2, lk_u32 r2) {
  if (l1 != l2) {
    return l1 < l2 ? -1 : 1;
  }

  if (r1 != r2) {
    return r1 < r2 ? -1 : 1;
  }

  return 0;
}

void lk_editor_layout_node(lk_editor *e, const lk_tree *t, lk_ix n,
                           const lk_rect *content, const lk_layout_cfg *cfg) {
  const lk_text_backend *tb;
  lk_i32 line_h;
  lk_i32 view_h;
  lk_u32 lcount;
  lk_u32 q;
  lk_u32 al;
  lk_u32 ar;
  lk_u32 vis_count;
  lk_u32 vis_needed;
  lk_u32 k;
  lk_i32 baseline;
  int spans_on;
  int want_scroll;

  if (!e || !t || !content) {
    return;
  }

  LK_ED_ASSERT(!(e->geom.valid && e->geom.tree == (const void *)t &&
                 e->geom.node_id != t->nodes[n].id));

  memset(&e->geom, 0, sizeof(e->geom));

  tb = cfg ? cfg->text : NULL;
  e->font_id = (cfg && cfg->styles) ? (lk_u16)cfg->styles[n].font_id : 0;
  e->font_size = (cfg && cfg->styles) ? (lk_u16)cfg->styles[n].font_size : 0;

  line_h = tb ? tb->line_height(tb->ud, e->font_id, e->font_size)
              : ED_FALLBACK_LINE_H;

  if (line_h <= 0) {
    line_h = ED_FALLBACK_LINE_H;
  }

  e->line_h = line_h;

  if (tb) {
    lk_text_metrics m;
    lk_str sp;

    sp.ptr = " ";
    sp.len = 1;
    m.w = 0;
    m.h = 0;
    m.baseline = 0;
    tb->measure(tb->ud, sp, e->font_id, e->font_size, &m);
    e->space_adv = m.w > 0 ? m.w : ED_FALLBACK_ADVANCE;
    baseline = m.baseline > 0 ? m.baseline : ED_FALLBACK_BASELINE;
  } else {
    e->space_adv = ED_FALLBACK_ADVANCE;
    baseline = ED_FALLBACK_BASELINE;
  }

  spans_on = e->span_count > 0 &&
             lk_revision_equal(e->span_rev, lk_doc_revision(e->doc));

  view_h = content->h;
  e->page_lines = view_h > 0 ? view_h / line_h : 0;
  lcount = lk_doc_line_count(e->doc);
  q = view_h > 0 ? (lk_u32)(view_h / line_h) : 0;
  want_scroll = e->pending_scroll;

  if (e->wrap_mode != LK_EDITOR_WRAP_NONE) {
    lk_i32 ww = content->w > 0 ? content->w : 0;

    if (e->wrap_w != ww || e->wrap_tb != (const void *)tb ||
        e->wrap_font_id != e->font_id || e->wrap_font_size != e->font_size ||
        e->wrap_tab != e->tab_size) {
      e->wrap_w = ww;
      e->wrap_tb = (const void *)tb;
      e->wrap_font_id = e->font_id;
      e->wrap_font_size = e->font_size;
      e->wrap_tab = e->tab_size;
      e->wrap_generation++;
      e->est_px = 0;
      e->est_bytes = 0;
    }

    e->scroll_x = 0;
  }

  if (e->vp.top_byte > lk_doc_len(e->doc)) {
    e->vp.top_byte = lk_doc_len(e->doc);
  }

  ed_pos_to_row(e, tb, e->vp.top_byte, &al, &ar);

  if (e->vp.y_offset < 0) {
    e->vp.y_offset = 0;
  }

  if (e->vp.y_offset >= line_h) {
    e->vp.y_offset = line_h - 1;
  }

  if (view_h <= 0) {
    al = 0;
    ar = 0;
    e->vp.y_offset = 0;
  } else if (lcount - 1 - al <= q + 1) {
    lk_u32 bl = lcount - 1;
    lk_u32 br = ed_row_count_of(ed_wrap_line(e, tb, bl)) - 1;
    lk_u32 ml;
    lk_u32 mr;
    lk_i32 my;

    ed_bottom_anchor(e, tb, bl, br, line_h, view_h, &ml, &mr, &my);

    if (ed_rowpos_cmp(al, ar, ml, mr) > 0 ||
        (ed_rowpos_cmp(al, ar, ml, mr) == 0 && e->vp.y_offset > my)) {
      al = ml;
      ar = mr;
      e->vp.y_offset = my;
    }
  }

  if (want_scroll && view_h > 0) {
    lk_u32 cl;
    lk_u32 cr;

    ed_pos_to_row(e, tb, ed_primary(e)->cursor, &cl, &cr);

    if (ed_rowpos_cmp(cl, cr, al, ar) < 0 ||
        (ed_rowpos_cmp(cl, cr, al, ar) == 0 && e->vp.y_offset > 0)) {
      al = cl;
      ar = cr;
      e->vp.y_offset = 0;
    } else if (ed_rowpos_cmp(cl, cr, al, ar) > 0) {
      int below;

      if (cl - al > q + 1) {
        below = 1;
      } else {
        lk_u32 l = al;
        lk_u32 r = ar;
        lk_u32 rel = 0;

        while (ed_rowpos_cmp(l, r, cl, cr) < 0 && rel <= q + 1) {
          if (!ed_row_next(e, tb, &l, &r)) {
            break;
          }

          rel++;
        }

        below = ed_rowpos_cmp(l, r, cl, cr) == 0
                    ? ((lk_i32)rel * line_h - e->vp.y_offset + line_h) > view_h
                    : 1;
      }

      if (below) {
        ed_bottom_anchor(e, tb, cl, cr, line_h, view_h, &al, &ar,
                         &e->vp.y_offset);
      }
    }

    e->pending_scroll = 0;
  }

  e->vp.top_byte = lk_doc_line_start(e->doc, al) +
                   ed_row_start_rel(ed_wrap_line(e, tb, al), ar);

  {
    lk_u32 li;
    lk_u32 sb_total = 0;
    lk_u64 sb_rows_before = 0;
    lk_u64 sb_top;

    for (li = 0; li < lcount; li++) {
      lk_u32 rows_n = ed_rows_or_est(e, li);

      if (li < al) {
        sb_rows_before += (lk_u64)rows_n;
      }

      sb_total += rows_n;
    }

    sb_top =
        (sb_rows_before + (lk_u64)ar) * (lk_u64)line_h + (lk_u64)e->vp.y_offset;
    e->geom.sb_total_rows = sb_total;
    e->geom.sb_top_px = (lk_u32)sb_top;
  }

  if (e->wrap_mode == LK_EDITOR_WRAP_NONE) {
    lk_i32 margin = 2 * ed_advance(e);

    if (want_scroll && view_h > 0) {
      lk_u32 cl = lk_doc_pos_to_line(e->doc, ed_primary(e)->cursor);
      const char *text;
      lk_u32 tlen;
      lk_u32 tstart;

      text = ed_line_text(e, cl, &tlen, &tstart);

      if (text) {
        lk_i32 cx = ed_line_x_from_ix(e, tb, text, tlen,
                                      ed_primary(e)->cursor - tstart);

        if (cx < e->scroll_x + margin) {
          e->scroll_x = cx > margin ? cx - margin : 0;
        } else if (content->w > 0 && cx > e->scroll_x + content->w - margin) {
          e->scroll_x = cx - content->w + margin;
        }
      }
    }

    if (e->scroll_x > 0 && view_h > 0) {
      lk_i32 widest = 0;
      lk_i32 cap;
      lk_u32 nvis = (lk_u32)((view_h + e->vp.y_offset + line_h - 1) / line_h);

      if (nvis > lcount - al) {
        nvis = lcount - al;
      }

      for (k = 0; k < nvis; k++) {
        const char *text;
        lk_u32 tlen;
        lk_u32 tstart;
        lk_i32 w;

        text = ed_line_text(e, al + k, &tlen, &tstart);

        if (!text) {
          continue;
        }

        w = ed_line_x_from_ix(e, tb, text, tlen, tlen);

        if (w > widest) {
          widest = w;
        }
      }

      cap = widest - content->w + margin;

      if (cap < 0) {
        cap = 0;
      }

      if (e->scroll_x > cap) {
        e->scroll_x = cap;
      }
    }
  }

  vis_needed = view_h > 0
                   ? (lk_u32)((view_h + e->vp.y_offset + line_h - 1) / line_h)
                   : 0;
  e->vis_len = 0;
  e->seg_count = 0;
  vis_count = 0;

  if (!ed_lines_reserve(e, vis_needed ? vis_needed : 1)) {
    return;
  }

  {
    lk_u32 l = al;
    lk_u32 r = ar;

    while (vis_count < vis_needed) {
      const ed_line_wrap *w = ed_wrap_line(e, tb, l);
      lk_u32 ls = lk_doc_line_start(e->doc, l);
      lk_u32 llen = lk_doc_line_end(e->doc, l) - ls;
      lk_u32 rs = ed_row_start_rel(w, r);
      lk_u32 rlen = ed_row_end_rel(w, r, llen) - rs;
      lk_i32 y = content->y + (lk_i32)vis_count * line_h - e->vp.y_offset;
      const char *p;
      lk_i32 x;
      lk_u32 i;
      lk_i32 tabpx;

      if (!ed_vis_reserve(e, e->vis_len + rlen)) {
        return;
      }

      if (rlen) {
        lk_doc_get_text(e->doc, ls + rs, e->vis + e->vis_len, rlen);
      }

      e->lines[vis_count].line = l;
      e->lines[vis_count].row = r;
      e->lines[vis_count].doc_start = ls + rs;
      e->lines[vis_count].doc_len = rlen;
      e->lines[vis_count].off = e->vis_len;

      p = e->vis + e->vis_len;
      x = 0;
      i = 0;
      tabpx = ed_tab_px(e);

      while (i < rlen) {
        lk_u32 j = i;
        lk_i32 w2;

        while (j < rlen && p[j] != '\t') {
          j++;
        }

        w2 = ed_run_x(e, tb, p + i, j - i, j - i);

        if (j > i) {
          if (!ed_emit_run(e, tb, p + i, j - i, e->vis_len + i, ls + rs + i,
                           content->x + x - e->scroll_x, y, w2, spans_on)) {
            return;
          }
        }

        x += w2;

        if (j < rlen) {
          x = (x / tabpx + 1) * tabpx;
          i = j + 1;
        } else {
          i = j;
        }
      }

      e->vis_len += rlen;
      vis_count++;

      if (!ed_row_next(e, tb, &l, &r)) {
        break;
      }
    }
  }

  {
    lk_u32 ci;

    for (ci = 0; ci < e->caret_count; ci++) {
      lk_u32 cl;
      lk_u32 cr;

      ed_pos_to_row(e, tb, e->carets[ci].cursor, &cl, &cr);

      for (k = 0; k < vis_count; k++) {
        const ed_line *ln = &e->lines[k];

        if (ln->line == cl && ln->row == cr) {
          lk_i32 cx = ed_line_x_from_ix(e, tb, e->vis + ln->off, ln->doc_len,
                                        e->carets[ci].cursor - ln->doc_start);

          if (ed_cursor_xy_reserve(e, e->geom.cursor_count + 1)) {
            e->cursor_xy[e->geom.cursor_count * 2] =
                content->x + cx - e->scroll_x;
            e->cursor_xy[e->geom.cursor_count * 2 + 1] =
                content->y + (lk_i32)k * line_h - e->vp.y_offset;
            e->geom.cursor_count++;
          }

          break;
        }
      }
    }
  }

  if (vis_count > 0) {
    lk_u32 ci;

    for (ci = 0; ci < e->caret_count; ci++) {
      lk_u32 lo;
      lk_u32 hi;
      lk_u32 ll;
      lk_u32 lr;
      lk_u32 hl;
      lk_u32 hr;

      if (!ed_caret_sel_range(&e->carets[ci], &lo, &hi)) {
        continue;
      }

      ed_pos_to_row(e, tb, lo, &ll, &lr);
      ed_pos_to_row(e, tb, hi, &hl, &hr);

      if (ll == hl && lr == hr) {
        for (k = 0; k < vis_count; k++) {
          const ed_line *ln = &e->lines[k];

          if (ln->line == ll && ln->row == lr) {
            lk_i32 x0 = ed_line_x_from_ix(e, tb, e->vis + ln->off, ln->doc_len,
                                          lo - ln->doc_start);
            lk_i32 x1 = ed_line_x_from_ix(e, tb, e->vis + ln->off, ln->doc_len,
                                          hi - ln->doc_start);

            if (x1 > x0) {
              ed_sel_rect_push(e, content->x + x0 - e->scroll_x,
                               content->y + (lk_i32)k * line_h - e->vp.y_offset,
                               x1 - x0, line_h);
            }

            break;
          }
        }
      } else {
        lk_u32 b0 = 0;
        lk_u32 b1 = 0;
        int have_body = 0;

        for (k = 0; k < vis_count; k++) {
          const ed_line *ln = &e->lines[k];
          int c_lo = ed_rowpos_cmp(ln->line, ln->row, ll, lr);
          int c_hi = ed_rowpos_cmp(ln->line, ln->row, hl, hr);

          if (c_lo == 0) {

            lk_i32 x0 = ed_line_x_from_ix(e, tb, e->vis + ln->off, ln->doc_len,
                                          lo - ln->doc_start);
            lk_i32 x1 = ed_line_x_from_ix(e, tb, e->vis + ln->off, ln->doc_len,
                                          ln->doc_len);

            if (x1 > x0) {
              ed_sel_rect_push(e, content->x + x0 - e->scroll_x,
                               content->y + (lk_i32)k * line_h - e->vp.y_offset,
                               x1 - x0, line_h);
            }
          } else if (c_lo > 0 && c_hi < 0) {

            if (!have_body) {
              b0 = k;
              have_body = 1;
            }

            b1 = k;
          }
        }

        if (have_body) {
          ed_sel_rect_push(e, content->x,
                           content->y + (lk_i32)b0 * line_h - e->vp.y_offset,
                           content->w, (lk_i32)(b1 - b0 + 1) * line_h);
        }

        for (k = 0; k < vis_count; k++) {
          const ed_line *ln = &e->lines[k];

          if (ln->line == hl && ln->row == hr) {

            lk_i32 x1 = ed_line_x_from_ix(e, tb, e->vis + ln->off, ln->doc_len,
                                          hi - ln->doc_start);

            if (x1 > 0) {
              ed_sel_rect_push(e, content->x - e->scroll_x,
                               content->y + (lk_i32)k * line_h - e->vp.y_offset,
                               x1, line_h);
            }

            break;
          }
        }
      }
    }
  }

  e->geom.valid = 1;
  e->geom.tree = (const void *)t;
  e->geom.node_id = t->nodes[n].id;
  e->geom.rect = *content;
  e->geom.line_h = line_h;
  e->geom.baseline = baseline;
  e->geom.first_line = al;
  e->geom.vis_count = vis_count;
  e->geom.scroll_x = e->scroll_x;
  e->geom.font_id = e->font_id;
  e->geom.font_size = e->font_size;
  e->geom.tb = cfg ? cfg->text : NULL;
}

void lk_editor_render_node(const lk_editor *e, const lk_tree *t, lk_ix n,
                           const lk_rect *rect, const lk_style *style,
                           const lk_state *state, lk_render_list *out) {
  lk_render_cmd cmd;
  lk_u32 i;
  lk_node_id nid;

  if (!e || !t || !rect || !style || !out) {
    return;
  }

  nid = t->nodes[n].id;

  if (!e->geom.valid || e->geom.node_id != nid) {
    return;
  }

  memset(&cmd, 0, sizeof(cmd));
  cmd.op = LK_ROP_CLIP_BEGIN;
  cmd.rect = *rect;
  lk_render_list_push(out, cmd);

  for (i = 0; i < e->seg_count; i++) {
    const ed_seg *seg = &e->segs[i];

    if (!(seg->flags & LK_SPAN_BG)) {
      continue;
    }

    memset(&cmd, 0, sizeof(cmd));
    cmd.op = LK_ROP_FILL_RECT;
    cmd.rect.x = seg->x;
    cmd.rect.y = seg->y;
    cmd.rect.w = seg->w;
    cmd.rect.h = e->geom.line_h;
    cmd.color = seg->bg;
    lk_render_list_push(out, cmd);
  }

  for (i = 0; i < e->geom.sel_count; i++) {
    memset(&cmd, 0, sizeof(cmd));
    cmd.op = LK_ROP_FILL_RECT;
    cmd.rect = e->sel_rects[i];
    cmd.color.r = 80;
    cmd.color.g = 120;
    cmd.color.b = 200;
    cmd.color.a = 128;
    lk_render_list_push(out, cmd);
  }

  for (i = 0; i < e->seg_count; i++) {
    const ed_seg *seg = &e->segs[i];
    lk_u32 run_off;
    lk_color run_fg;

    if (seg->len == 0) {
      continue;
    }

    if (!lk_render_list_push_run(out, e->vis + seg->off, seg->len, &run_off)) {
      continue;
    }

    run_fg = (seg->flags & LK_SPAN_FG) ? seg->fg : style->fg;

    memset(&cmd, 0, sizeof(cmd));
    cmd.op = LK_ROP_DRAW_RUN;
    cmd.rect.x = seg->x;
    cmd.rect.y = seg->y;
    cmd.rect.w = seg->w;
    cmd.rect.h = e->geom.line_h;
    cmd.color = run_fg;
    cmd.font_id = e->geom.font_id;
    cmd.font_size = e->geom.font_size;
    cmd.run_off = run_off;
    cmd.run_len = seg->len;

    if (seg->flags & LK_SPAN_UNDERLINE) {
      cmd.text_attr |= LK_TA_UNDERLINE;
    }

    lk_render_list_push(out, cmd);
  }

  if (state && e->geom.cursor_count > 0) {
    lk_value f = lk_state_get(state, nid, LKS_FOCUSED);

    if (f.tag == UIV_I32 && f.as.i != 0) {
      for (i = 0; i < e->geom.cursor_count; i++) {
        memset(&cmd, 0, sizeof(cmd));
        cmd.op = LK_ROP_CARET;
        cmd.rect.x = e->cursor_xy[i * 2];
        cmd.rect.y = e->cursor_xy[i * 2 + 1];
        cmd.rect.w = 1;
        cmd.rect.h = e->geom.line_h;
        cmd.color = style->fg;
        lk_render_list_push(out, cmd);
      }
    }
  }

  {
    lk_i32 track_y;
    lk_i32 track_h;
    lk_i32 thumb_y;
    lk_i32 thumb_h;

    if (ed_scrollbar_geom(e, &track_y, &track_h, &thumb_y, &thumb_h)) {
      lk_i32 bar_x = rect->x + rect->w - ED_SCROLL_BAR_W;

      memset(&cmd, 0, sizeof(cmd));
      cmd.op = LK_ROP_FILL_RECT;
      cmd.rect.x = bar_x;
      cmd.rect.y = track_y;
      cmd.rect.w = ED_SCROLL_BAR_W;
      cmd.rect.h = track_h;
      cmd.color = style->scrollbar_track;
      lk_render_list_push(out, cmd);

      memset(&cmd, 0, sizeof(cmd));
      cmd.op = LK_ROP_FILL_RECT;
      cmd.rect.x = bar_x;
      cmd.rect.y = thumb_y;
      cmd.rect.w = ED_SCROLL_BAR_W;
      cmd.rect.h = thumb_h;
      cmd.color = style->scrollbar_thumb;
      lk_render_list_push(out, cmd);
    }
  }

  memset(&cmd, 0, sizeof(cmd));
  cmd.op = LK_ROP_CLIP_END;
  lk_render_list_push(out, cmd);
}

static int ed_scrollbar_geom(const lk_editor *e, lk_i32 *track_y,
                             lk_i32 *track_h, lk_i32 *thumb_y,
                             lk_i32 *thumb_h) {
  lk_i32 line_h = e->geom.line_h;
  lk_i32 th = e->geom.rect.h;
  lk_u64 total_px = (lk_u64)e->geom.sb_total_rows * (lk_u64)line_h;
  lk_u64 top_px;
  lk_u64 max_top;
  lk_i32 h;

  if (th <= 0 || line_h <= 0 || total_px <= (lk_u64)th) {
    return 0;
  }

  top_px = (lk_u64)e->geom.sb_top_px;
  max_top = total_px - (lk_u64)th;
  h = (lk_i32)(((lk_u64)th * (lk_u64)th) / total_px);

  if (h < ED_SCROLL_THUMB_MIN) {
    h = ED_SCROLL_THUMB_MIN;
  }

  if (h > th) {
    h = th;
  }

  if (top_px > max_top) {
    top_px = max_top;
  }

  *track_y = e->geom.rect.y;
  *track_h = th;
  *thumb_h = h;
  *thumb_y = e->geom.rect.y + (lk_i32)((top_px * (lk_u64)(th - h)) / max_top);

  return 1;
}

static void ed_scroll_extent(lk_editor *e, const lk_text_backend *tb,
                             lk_u32 *out_top, lk_u32 *out_total) {
  lk_u32 lcount = lk_doc_line_count(e->doc);
  lk_u32 al;
  lk_u32 ar;
  lk_u32 li;
  lk_u32 total = 0;
  lk_u32 before = 0;

  ed_pos_to_row(e, tb, e->vp.top_byte, &al, &ar);

  for (li = 0; li < lcount; li++) {
    lk_u32 rows_n = ed_rows_or_est(e, li);

    if (li < al) {
      before += rows_n;
    }

    total += rows_n;
  }

  *out_top = before + ar;
  *out_total = total;
}

void lk_editor_scroll_to_row(lk_editor *e, lk_ui *ui, lk_u32 row) {
  lk_u32 top;
  lk_u32 total;
  lk_editor_cmd_arg arg;

  if (!e) {
    return;
  }

  ed_scroll_extent(e, ui ? ui->text : NULL, &top, &total);

  if (total == 0) {
    return;
  }

  if (row >= total) {
    row = total - 1;
  }

  if (row == top) {
    return;
  }

  memset(&arg, 0, sizeof(arg));
  arg.lines = row > top ? (lk_i32)(row - top) : -(lk_i32)(top - row);
  lk_editor_command(e, ui, LK_ED_SCROLL_LINES, &arg);
}

int lk_editor_scrollbar_down(lk_editor *e, lk_ui *ui, lk_i32 x, lk_i32 y) {
  lk_i32 track_y;
  lk_i32 track_h;
  lk_i32 thumb_y;
  lk_i32 thumb_h;
  lk_editor_cmd_arg arg;

  if (!e || !ed_scrollbar_geom(e, &track_y, &track_h, &thumb_y, &thumb_h)) {
    return 0;
  }

  if (x < e->geom.rect.x + e->geom.rect.w - ED_SCROLL_BAR_W || y < track_y ||
      y >= track_y + track_h) {
    return 0;
  }

  if (y >= thumb_y && y < thumb_y + thumb_h) {
    e->sb_drag = 1;
    e->sb_grab = y - thumb_y;

    return 2;
  }

  memset(&arg, 0, sizeof(arg));
  arg.lines = y < thumb_y ? -ed_page_size(e) : ed_page_size(e);
  lk_editor_command(e, ui, LK_ED_SCROLL_LINES, &arg);

  return 1;
}

void lk_editor_scrollbar_move(lk_editor *e, lk_ui *ui, lk_i32 y) {
  lk_i32 track_y;
  lk_i32 track_h;
  lk_i32 thumb_y;
  lk_i32 thumb_h;
  lk_i32 range;
  lk_i32 rel;
  lk_u64 total_px;
  lk_u64 max_top;
  lk_u64 top_px;

  if (!e || !e->sb_drag ||
      !ed_scrollbar_geom(e, &track_y, &track_h, &thumb_y, &thumb_h)) {
    return;
  }

  range = track_h - thumb_h;

  if (range <= 0) {
    return;
  }

  rel = y - e->sb_grab - track_y;

  if (rel < 0) {
    rel = 0;
  }

  if (rel > range) {
    rel = range;
  }

  total_px = (lk_u64)e->geom.sb_total_rows * (lk_u64)e->geom.line_h;
  max_top = total_px - (lk_u64)track_h;
  top_px = ((lk_u64)rel * max_top) / (lk_u64)range;
  lk_editor_scroll_to_row(e, ui, (lk_u32)(top_px / (lk_u64)e->geom.line_h));
}

int lk_editor_scrollbar_dragging(const lk_editor *e) {
  return e ? e->sb_drag : 0;
}

void lk_editor_scrollbar_end(lk_editor *e) {
  if (e) {
    e->sb_drag = 0;
  }
}

int lk_editor_hit_pos(const lk_editor *e, const lk_text_backend *tb, lk_i32 x,
                      lk_i32 y, lk_u32 *out_pos) {
  lk_i32 rel;
  lk_u32 k;
  const ed_line *ln;

  if (!e || !out_pos || !e->geom.valid || e->geom.vis_count == 0 ||
      e->geom.line_h <= 0) {
    return 0;
  }

  rel = y - e->geom.rect.y + e->vp.y_offset;

  if (rel < 0) {
    rel = 0;
  }

  k = (lk_u32)(rel / e->geom.line_h);

  if (k >= e->geom.vis_count) {
    k = e->geom.vis_count - 1;
  }

  ln = &e->lines[k];
  *out_pos =
      ln->doc_start + ed_line_ix_from_x(e, tb, e->vis + ln->off, ln->doc_len,
                                        x - e->geom.rect.x + e->geom.scroll_x);

  return 1;
}

static int ed_box_point(const lk_editor *e, lk_i32 x, lk_i32 y,
                        lk_u32 *out_line, lk_u32 *out_row, lk_i32 *out_x) {
  lk_i32 rel;
  lk_u32 k;

  if (!e->geom.valid || e->geom.vis_count == 0 || e->geom.line_h <= 0) {
    return 0;
  }

  rel = y - e->geom.rect.y + e->vp.y_offset;

  if (rel < 0) {
    rel = 0;
  }

  k = (lk_u32)(rel / e->geom.line_h);

  if (k >= e->geom.vis_count) {
    k = e->geom.vis_count - 1;
  }

  *out_line = e->lines[k].line;
  *out_row = e->lines[k].row;
  *out_x = x - e->geom.rect.x + e->geom.scroll_x;

  return 1;
}

static void ed_box_rebuild(lk_editor *e, const lk_text_backend *tb, lk_u32 cl,
                           lk_u32 cr, lk_i32 cx) {
  lk_u32 l = cl;
  lk_u32 r = cr;
  int toward_next = ed_rowpos_cmp(cl, cr, e->box_line, e->box_row) < 0;
  lk_u32 n = 0;

  for (;;) {
    lk_u32 rstart;
    lk_u32 rlen;
    const char *text;
    lk_u32 tlen;
    lk_u32 tstart;
    lk_u32 a_ix;
    lk_u32 c_ix;

    if (n >= ED_MAX_CARETS || !ed_carets_reserve(e, n + 1)) {
      break;
    }

    ed_row_bounds(e, tb, l, r, &rstart, &rlen, NULL);
    text = ed_line_text(e, l, &tlen, &tstart);

    if (!text) {
      break;
    }

    a_ix = ed_line_ix_from_x(e, tb, text + (rstart - tstart), rlen, e->box_x);
    c_ix = ed_line_ix_from_x(e, tb, text + (rstart - tstart), rlen, cx);

    e->carets[n].cursor = rstart + c_ix;
    e->carets[n].anchor = a_ix == c_ix ? ED_NO_ANCHOR : rstart + a_ix;
    e->carets[n].sticky_x = ED_STICKY_NONE;
    n++;

    if (l == e->box_line && r == e->box_row) {
      break;
    }

    if (toward_next ? !ed_row_next(e, tb, &l, &r)
                    : !ed_row_prev(e, tb, &l, &r)) {
      break;
    }
  }

  if (n == 0) {
    return;
  }

  e->caret_count = n;
  e->caret_primary = 0;
  ed_normalize(e);
}

int lk_editor_box_down(lk_editor *e, lk_ui *ui, lk_i32 x, lk_i32 y) {
  lk_u32 l;
  lk_u32 r;
  lk_i32 rx;

  if (!e || !ui || !ui->text) {
    return 0;
  }

  if (!ed_box_point(e, x, y, &l, &r, &rx)) {
    return 0;
  }

  e->box = 1;
  e->box_line = l;
  e->box_row = r;
  e->box_x = rx;
  ed_box_rebuild(e, ui->text, l, r, rx);

  return 1;
}

void lk_editor_box_move(lk_editor *e, lk_ui *ui, lk_i32 x, lk_i32 y) {
  lk_u32 l;
  lk_u32 r;
  lk_i32 rx;

  if (!e || !e->box || !ui || !ui->text) {
    return;
  }

  if (!ed_box_point(e, x, y, &l, &r, &rx)) {
    return;
  }

  ed_box_rebuild(e, ui->text, l, r, rx);
}

int lk_editor_box_active(const lk_editor *e) {
  return e ? e->box : 0;
}

void lk_editor_box_end(lk_editor *e) {
  if (e) {
    e->box = 0;
  }
}

int lk_editor_pos_at(const lk_editor *e, lk_i32 x, lk_i32 y, lk_u32 *out_pos) {
  if (!e || !out_pos || !e->geom.valid) {
    return 0;
  }

  if (x < e->geom.rect.x || x >= e->geom.rect.x + e->geom.rect.w ||
      y < e->geom.rect.y || y >= e->geom.rect.y + e->geom.rect.h) {
    return 0;
  }

  return lk_editor_hit_pos(e, e->geom.tb, x, y, out_pos);
}

#define ED_PRES_HIT_CAP 8

void lk_editor_set_presentation_source(lk_editor *e,
                                       const lk_presentation_source *src) {
  if (!e) {
    return;
  }

  if (src) {
    e->psrc = *src;
  } else {
    memset(&e->psrc, 0, sizeof(e->psrc));
  }
}

lk_u32 lk_editor_presentations_at(lk_editor *e, const lk_tree *t, lk_u32 pos,
                                  lk_presentation_hit *out, lk_u32 cap) {
  lk_u32 count;
  lk_u32 i;
  lk_u32 locus_kind;

  if (!e || !e->psrc.query_at || !t || !out || cap == 0) {
    return 0;
  }

  count = e->psrc.query_at(e->psrc.ud, pos, out, cap);

  if (count > cap) {
    count = cap;
  }

  locus_kind = t->intern ? lk_intern_cid(t->intern, "editor-range") : 0;

  for (i = 0; i < count; i++) {
    out[i].locus_kind = locus_kind;
    out[i].locus[3] = pos;
  }

  return count;
}

int lk_editor_offer_presentations(lk_editor *e, lk_ui *ui, const lk_tree *t,
                                  lk_ix n, lk_event *ev, lk_u32 pos) {
  lk_presentation_hit hits[ED_PRES_HIT_CAP];
  lk_u32 count;
  lk_u32 i;
  lk_u32 locus_kind;

  if (!e || !e->psrc.query_at || !ui || !t) {
    return 0;
  }

  count = e->psrc.query_at(e->psrc.ud, pos, hits, ED_PRES_HIT_CAP);

  if (count == 0) {
    return 0;
  }

  if (count > ED_PRES_HIT_CAP) {
    count = ED_PRES_HIT_CAP;
  }

  locus_kind = t->intern ? lk_intern_cid(t->intern, "editor-range") : 0;

  for (i = 0; i < count; i++) {
    hits[i].locus_kind = locus_kind;
    hits[i].locus[3] = pos;
  }

  return lk_translate_presentations(ui, t, n, ev, hits, count);
}
