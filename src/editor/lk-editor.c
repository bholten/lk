/*
 * lk-editor.c -- editor view state + command implementations (editor
 * track, stage B2; docs/editor.md sections 6 and 9).
 *
 * State: cursor byte offset (codepoint-boundary-aligned), selection
 * anchor (sentinel = none), sticky x-pixel for vertical motion
 * (resolved lazily on the first UP/DOWN after a horizontal move),
 * anchored viewport {top_byte, y_offset} over VISUAL ROWS, the wrap
 * cache + horizontal scroll (docs/editor-wrap.md), drag flag, tab
 * settings, growable scratch for visible-row extraction, and a
 * transient per-frame geometry block written by
 * lk_editor_layout_node.
 *
 * The editor subscribes to its own document: LK_ORIGIN_UNDO/REDO
 * transactions derive the cursor from the deltas (end of the last
 * inserted range, start of the last deleted one); its own command
 * transactions are skipped (commands set the cursor directly); any
 * foreign transaction clamps cursor/selection/viewport to the new
 * document shape (full foreign-edit adjustment is deferred).
 *
 * Motion/edit semantics are ported from weft's editor.c (word rules,
 * boundary behavior of vertical motion); layout/wrap/blink stayed
 * behind.  weft's byte target_column generalizes to a sticky x-PIXEL
 * resolved per line via index_from_x -- identical under the monospace
 * stub, proportional-correct for free.
 */

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
#define ED_FALLBACK_LINE_H 16
#define ED_FALLBACK_ADVANCE 8
#define ED_FALLBACK_BASELINE 12
#define ED_FALLBACK_PAGE_LINES 20
#define ED_TAB_SIZE 4
#define ED_MAX_SEL_RECTS 3
#define ED_SCROLL_BAR_W 6   /* the lk-scroll.c SCROLL_BAR_W convention */
#define ED_SCROLL_THUMB_MIN 8

/* One visible VISUAL ROW extracted into the vis scratch (a whole
 * document line when wrapping is off). */
typedef struct ed_line {
  lk_u32 line;      /* document line index */
  lk_u32 row;       /* visual row within that line (0 when unwrapped) */
  lk_u32 doc_start; /* byte offset of the row start in the document */
  lk_u32 doc_len;   /* row length in bytes, excluding the \n */
  lk_u32 off;       /* offset of the row's bytes in e->vis */
} ed_line;

/* Per-document-line wrap cache entry (docs/editor-wrap.md section 1).
 * breaks[] holds the 2nd..Nth row starts RELATIVE to the line start
 * (row_count == break_count + 1); an unwrapped line allocates
 * nothing.  An entry is valid iff generation == e->wrap_generation
 * (generation 0 is reserved invalid; e->wrap_generation starts at
 * 1). */
typedef struct ed_line_wrap {
  lk_u32 *breaks;
  lk_u32 break_count;
  lk_u32 break_cap;
  lk_u32 generation;
} ed_line_wrap;

/* One tab-split (and, when spans are active, span-split) run segment,
 * in absolute pixels.  flags/fg/bg are copied out of the matching
 * span at layout time so segments never dangle into a replaced span
 * array; flags == 0 means "style->fg, no bg, no underline". */
typedef struct ed_seg {
  lk_u32 off; /* into e->vis */
  lk_u32 len;
  lk_i32 x, y, w;
  lk_u8 flags; /* LK_SPAN_* */
  lk_color fg, bg;
} ed_seg;

/* Transient per-frame geometry (docs/editor.md section 7): stashed by
 * the layout hook, read by render/hit-testing.  Application-owned --
 * the editor adds zero entries to lk_state. */
typedef struct ed_geom {
  int valid;
  const void *tree; /* claim stamp (see lk_editor_layout_node) */
  lk_node_id node_id;
  lk_rect rect; /* content rect */
  lk_i32 line_h;
  lk_i32 baseline; /* top of line -> text baseline, for underlines */
  lk_u32 first_line;
  lk_u32 vis_count;
  lk_i32 scroll_x; /* horizontal scroll this geometry was built with */
  int cursor_vis;
  lk_i32 cursor_x, cursor_y;
  lk_rect sel_rects[ED_MAX_SEL_RECTS];
  lk_u32 sel_count;
  /* Scrollbar extent (docs/editor-wrap.md section 6 model): total
   * visual rows over the whole document and the pixel offset of the
   * viewport top within that extent — exact rows for wrap-measured
   * lines, estimator rows otherwise (arithmetic only, no backend
   * calls).  Consumed by the overlay scrollbar in render. */
  lk_u32 sb_total_rows;
  lk_u32 sb_top_px;
  lk_u16 font_id, font_size;
  const lk_text_backend *tb; /* backend this snapshot was built with
                                (lk_editor_pos_at resolves against the
                                last completed layout, backend and
                                all) */
} ed_geom;

struct lk_editor {
  void *(*alloc)(void *, lk_u32);
  void (*dealloc)(void *, void *);
  void *ud;

  lk_document *doc;
  lk_edit_history *hist;
  lk_u32 sub_id;

  lk_u32 cursor;
  lk_u32 anchor;   /* ED_NO_ANCHOR = no selection */
  lk_i32 sticky_x; /* ED_STICKY_NONE = unset; x within the VISUAL ROW */
  lk_editor_viewport vp;
  int drag;
  int editable; /* read-only policy gates USER mutations only */
  lk_u32 tab_size;
  int in_command; /* inside one of our own doc transactions */
  int in_replay;  /* inside a history replay THIS editor invoked
                     (LK_ED_UNDO/REDO) -- ed_on_doc jumps the cursor
                     to the replay site only for the invoking view;
                     every other view treats the replay as a foreign
                     edit and transforms */
  int pending_scroll;
  lk_i32 scroll_x; /* horizontal scroll px; forced 0 while wrapping */

  /* wrap engine (docs/editor-wrap.md).  wrap[] is one entry per
   * document line, spliced in lockstep with the line index by
   * ed_on_doc; it is materialized only while wrap_mode != NONE.
   * The wrap key (width/font/tab/backend POINTER) is stamped by
   * layout; any change bumps wrap_generation -- one integer write,
   * never a sweep. */
  lk_u32 wrap_mode; /* lk_editor_wrap_mode */
  ed_line_wrap *wrap;
  lk_u32 wrap_count, wrap_cap;
  lk_u32 wrap_generation;
  lk_i32 wrap_w; /* wrap key: content width px (0 = no layout yet) */
  const void *wrap_tb;
  lk_u16 wrap_font_id, wrap_font_size;
  lk_u32 wrap_tab;

  /* scroll-extent estimator (docs/editor-wrap.md section 6): running
   * pixel/byte totals over measured lines; consumed ONLY at the
   * scroll-extent edge, never by the wrap engine itself. */
  lk_u32 est_px, est_bytes;

  /* last-known metrics (survive geometry invalidation) */
  lk_i32 line_h;
  lk_i32 space_adv;
  lk_i32 page_lines; /* 0 = no layout yet */
  lk_u16 font_id, font_size;

  /* layout scratch: visible line bytes + per-line/per-segment info */
  char *vis;
  lk_u32 vis_len, vis_cap;
  ed_line *lines;
  lk_u32 lines_cap;
  ed_seg *segs;
  lk_u32 seg_count, seg_cap;

  /* command-time single-line extraction buffer */
  char *line_buf;
  lk_u32 line_buf_cap;

  /* wrap-measurement line buffer (separate from line_buf: break
   * finding runs while callers hold line_buf text) */
  char *wrap_buf;
  lk_u32 wrap_buf_cap;

  /* styled-span snapshot (deep copy; docs/editor.md section 10) */
  lk_edit_span *spans;
  lk_u32 span_count, span_cap;
  lk_revision span_rev;
  lk_u32 span_range_start, span_range_end;

  /* interior presentation source (weft-surface S1); zeroed = none */
  lk_presentation_source psrc;

  ed_geom geom;
};

/* ---- Allocation helpers ---- */

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

static int ed_segs_reserve(lk_editor *e, lk_u32 needed) {
  lk_u32 nc;
  ed_seg *nb;

  if (needed <= e->seg_cap) {
    return 1;
  }

  nc = ed_grow_cap(e->seg_cap, needed, 16);
  nb = (ed_seg *)ed_grow_buf(e, e->segs,
                             e->seg_count * (lk_u32)sizeof(ed_seg),
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

/* ---- Codepoint stepping over the document ---- */

static int ed_is_cont(unsigned char b) {
  return (b & 0xC0u) == 0x80u;
}

/* Clamp pos to the document length and snap it down to a codepoint
 * boundary. */
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

/* Decode the codepoint at pos (weft's decode_codepoint_at, over
 * lk_doc_get_byte).  Invalid sequences degrade to the raw byte. */
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

  default:
    cp = b;
    break;
  }

  return cp;
}

/* ---- Word classification (ported from weft utf8_is_word_char) ---- */

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
    return 1; /* Latin Extended */
  }

  if (cp >= 0x0300u && cp <= 0x036Fu) {
    return 1; /* combining diacritics */
  }

  if (cp >= 0x0370u && cp <= 0x03FFu) {
    return 1; /* Greek */
  }

  if (cp >= 0x0400u && cp <= 0x04FFu) {
    return 1; /* Cyrillic */
  }

  if (cp >= 0x3040u && cp <= 0x30FFu) {
    return 1; /* Hiragana + Katakana */
  }

  if (cp >= 0x4E00u && cp <= 0x9FFFu) {
    return 1; /* CJK unified */
  }

  if (cp >= 0xAC00u && cp <= 0xD7AFu) {
    return 1; /* Hangul */
  }

  return 0;
}

/* Word-left target (weft editor_move_cursor_word_left): back one
 * codepoint, skip non-word backwards, then skip word chars backwards
 * to the word start. */
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

/* Word-right target (weft editor_move_cursor_word_right): skip word
 * chars forward, then skip non-word chars to the next word start. */
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

/* ---- Tab-segment pixel geometry ----
 *
 * A literal \t splits a line into segments; x advances to the next
 * (tab_size * space-advance) stop after each segment.  The same
 * segment walk backs x_from_index, index_from_x, and the render-time
 * run positions (precomputed at layout).  With no backend, a
 * monospace fallback of space_adv pixels per codepoint keeps the
 * shape identical (matches the stub for its default metrics). */

static lk_i32 ed_advance(const lk_editor *e) {
  return e->space_adv > 0 ? e->space_adv : ED_FALLBACK_ADVANCE;
}

static lk_i32 ed_tab_px(const lk_editor *e) {
  lk_i32 px = (lk_i32)e->tab_size * ed_advance(e);

  return px > 0 ? px : 1;
}

/* Count codepoints in p[0..ix). */
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

/* Pixel x of byte ix within a single tab-free run. */
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

/* Nearest codepoint boundary to pixel x within a tab-free run. */
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

/* Pixel x of byte ix within a line, with tab expansion. */
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

/* Nearest boundary to pixel x within a line, with tab expansion.
 * Positions inside a tab span snap to the nearer of its two edges
 * (ties round forward, matching the stub's rounding). */
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

/* ---- Segment emission (tab runs, span-split into sub-pieces) ---- */

static int ed_push_seg(lk_editor *e, lk_u32 off, lk_u32 len, lk_i32 x,
                       lk_i32 y, lk_i32 w, lk_u8 flags, lk_color fg,
                       lk_color bg) {
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

/* Snap a byte offset within run[0..len) down to a codepoint boundary
 * (sloppy producers can hand span edges that split a sequence). */
static lk_u32 ed_snap_run_ix(const char *run, lk_u32 len, lk_u32 ix) {
  if (ix > len) {
    ix = len;
  }

  while (ix > 0 && !lk_utf8_is_boundary(run, len, ix)) {
    ix--;
  }

  return ix;
}

/* Emit one tab-free run as segments: a single segment when no
 * revision-matched spans overlap it, otherwise span-split sub-pieces
 * whose x positions come from the same x_from_index walk the plain
 * segments use.  run/run_len are the run's bytes (already in e->vis
 * at vis_off), doc_off its document byte offset, x/y/w its absolute
 * pixel geometry, spans_on the layout-time staleness verdict.
 * Returns 0 only on allocation failure. */
static int ed_emit_run(lk_editor *e, const lk_text_backend *tb,
                       const char *run, lk_u32 run_len, lk_u32 vis_off,
                       lk_u32 doc_off, lk_i32 x, lk_i32 y, lk_i32 w,
                       int spans_on) {
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
      a = pos; /* overlap guard for sloppy producers */
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

      if (!ed_push_seg(e, vis_off + a, b - a, x0, y, x1 - x0, sp->flags,
                       sp->fg, sp->bg)) {
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

/* ---- Line extraction (command-time, single line) ---- */

/* Extract document line `line` into e->line_buf.  Writes length and
 * line start; returns the byte pointer (NULL on allocation failure,
 * with *out_len forced to 0). */
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

/* ---- Wrap cache (docs/editor-wrap.md sections 1-3) ---- */

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

/* Wrapping is effective only once layout has stamped a width. */
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

/* THE character-fit floor: byte length of the longest prefix of the
 * tab-free segment seg[0..len) whose pixel extent fits in avail.
 * One backend fit query (nearest boundary) plus one floor-correction
 * -- never per-codepoint accumulation.  Word wrap post-processes
 * this result in ed_measure_breaks (which holds the row-local bytes
 * the backward whitespace scan needs); nothing outside the break
 * finder changes. */
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

/* Find the wrap breaks of one line (p[0..len), no \n).  Tab stops are
 * relative to the CURRENT VISUAL ROW's x = 0; a tab that would land
 * past the width breaks first.  Empty-row progress guarantee: when
 * nothing fits on an empty row, one boundary is taken anyway.
 * WORD mode is a post-processing of the character-fit floor: prefer
 * the most recent breakable boundary at-or-before it within the
 * current row -- a boundary whose preceding codepoint is an ASCII
 * space (0x20) or a tab (a tab boundary is inherently breakable; no
 * other Unicode is special-cased, so NBSP never breaks).  The break
 * lands AFTER the whitespace: trailing spaces stay on the upper row.
 * With no breakable boundary in the row (an unbreakable run wider
 * than the viewport) the char-fit floor stands unchanged, progress
 * guarantee included.  The scan is over row-local bytes already in
 * hand -- no extra backend queries.
 * Writes the final row's end x to *out_last_x (for the estimator).
 * Returns 0 only on allocation failure. */
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
        continue; /* re-place the tab on the fresh row */
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
        /* progress guarantee */
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

          while (b > row_start && p[b - 1] != ' ' && p[b - 1] != '\t') {
            b--;
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

/* Ensure the line's wrap entry is valid at the current generation,
 * measuring lazily on demand.  NULL when wrapping is off (every line
 * is one row). */
static const ed_line_wrap *ed_wrap_line(lk_editor *e,
                                        const lk_text_backend *tb,
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

    /* estimator update: rows before the last are full-ish */
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

/* Half-open row ownership (pinned, docs/editor-wrap.md section 3):
 * row i owns caret positions [row_start_i, row_start_i+1); a position
 * exactly at a wrap break belongs to the NEXT row; the final row
 * additionally owns the end-of-line caret.  EVERY consumer -- cursor
 * xy, vertical motion, ROW commands, selection rects, clicks,
 * scroll-to-cursor -- routes through here. */
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

/* Step one visual row forward/backward; 0 at the document edge. */
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

/* Anchor that bottom-aligns row (bl, br) in a view_h viewport: back
 * up q-1 rows (r == 0) or q rows with y_offset = line_h - r.  Only
 * the walked rows' lines get measured -- this is both the bottom
 * scroll clamp and the distant scroll-to-cursor placement, so it is
 * viewport-bounded by construction. */
static void ed_bottom_anchor(lk_editor *e, const lk_text_backend *tb,
                             lk_u32 bl, lk_u32 br, lk_i32 line_h,
                             lk_i32 view_h, lk_u32 *al, lk_u32 *ar,
                             lk_i32 *ay) {
  lk_u32 q = (lk_u32)(view_h / line_h);
  lk_i32 r = view_h % line_h;
  lk_u32 need = (r == 0) ? (q ? q - 1 : 0) : q;
  lk_u32 got;

  *al = bl;
  *ar = br;
  got = ed_rows_back_n(e, tb, al, ar, need);
  *ay = (got < need || r == 0) ? 0 : line_h - r;
}

/* Estimated rows of an unmeasured line (docs/editor-wrap.md section
 * 6): max(1, ceil(len * avg_px_per_byte / wrap_width)), the average
 * running over measured lines with the space advance as seed. */
static lk_u32 ed_est_rows(const lk_editor *e, lk_u32 line_len) {
  unsigned long num;
  unsigned long den;
  unsigned long est;

  if (!ed_wrapping(e) || line_len == 0) {
    return 1;
  }

  if (e->est_bytes > 0) {
    num = (unsigned long)e->est_px;
    den = (unsigned long)e->est_bytes;
  } else {
    num = (unsigned long)ed_advance(e);
    den = 1;
  }

  den *= (unsigned long)e->wrap_w;
  est = ((unsigned long)line_len * num + den - 1) / den;

  return est ? (lk_u32)est : 1;
}

/* Exact rows for a valid entry, estimate otherwise (scroll-extent
 * edge only). */
static lk_u32 ed_rows_or_est(const lk_editor *e, lk_u32 line) {
  if (e->wrap && line < e->wrap_count &&
      e->wrap[line].generation == e->wrap_generation) {
    return e->wrap[line].break_count + 1;
  }

  return ed_est_rows(e, lk_doc_line_end(e->doc, line) -
                            lk_doc_line_start(e->doc, line));
}

/* ---- Wrap array lifecycle + delta splicing (section 7) ---- */

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
                                   e->wrap_count *
                                       (lk_u32)sizeof(ed_line_wrap),
                                   nc * (lk_u32)sizeof(ed_line_wrap));

  if (!nb) {
    return 0;
  }

  e->wrap = nb;
  e->wrap_cap = nc;

  return 1;
}

/* Fresh all-invalid array sized to the current document. */
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

/* Insert n fresh invalid entries at index at (entries below shift by
 * index only -- relative offsets make that free). */
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

/* Remove n entries at index at, freeing their breaks arrays. */
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

/* ---- Selection helpers ---- */

static int ed_sel_range(const lk_editor *e, lk_u32 *lo, lk_u32 *hi) {
  if (e->anchor == ED_NO_ANCHOR || e->anchor == e->cursor) {
    return 0;
  }

  if (e->anchor < e->cursor) {
    *lo = e->anchor;
    *hi = e->cursor;
  } else {
    *lo = e->cursor;
    *hi = e->anchor;
  }

  return 1;
}

/* Standard motion prologue: extending creates the anchor at the old
 * cursor if none; plain motion clears the selection. */
static void ed_motion_begin(lk_editor *e, int select) {
  if (select) {
    if (e->anchor == ED_NO_ANCHOR) {
      e->anchor = e->cursor;
    }
  } else {
    e->anchor = ED_NO_ANCHOR;
  }
}

/* ---- Document listener ---- */

static void ed_on_doc(void *ud, const lk_document *d,
                      const lk_doc_delta *deltas, lk_u32 n) {
  lk_editor *e = (lk_editor *)ud;

  if (n == 0) {
    return;
  }

  /* Geometry is stale after any committed change; the next layout
   * recomputes it. */
  e->geom.valid = 0;

  /* Viewport anchor transform, per delta in sequential order.
   * Affinity is pinned RIGHT: an insertion exactly at top_byte
   * (top_byte == p, hence the >=) shifts the anchor past the
   * inserted bytes, so the content being read stays at the top of
   * the viewport. */
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
        e->vp.top_byte += il; /* RIGHT affinity */
      }
    }
  }

  /* Wrap-cache splice (docs/editor-wrap.md section 7), per delta in
   * sequential order: dirty the touched line, add fresh invalid
   * entries for inserted newlines, drop entries for deleted ones.
   * Entries below shift by index only (relative break offsets).
   * pos_to_line queries the post-transaction document; the line
   * index AT delta.start is unchanged by the delta itself and by
   * later deltas at or after it, which covers every transaction the
   * editor, history, or an ascending multi-edit producer emits.  The
   * count check below resyncs anything exotic. */
  if (e->wrap) {
    lk_u32 di;

    for (di = 0; di < n; di++) {
      lk_u32 line = lk_doc_pos_to_line(d, deltas[di].start);
      lk_u32 del_nl = ed_count_nl(deltas[di].deleted,
                                  deltas[di].deleted_len);
      lk_u32 ins_nl = ed_count_nl(deltas[di].inserted,
                                  deltas[di].inserted_len);

      if (line >= e->wrap_count) {
        line = e->wrap_count ? e->wrap_count - 1 : 0;
      }

      if (line < e->wrap_count) {
        e->wrap[line].generation = 0;
        e->wrap[line].break_count = 0;
      }

      if (ins_nl > del_nl) {
        if (!ed_wrap_splice_in(e, line + 1, ins_nl - del_nl)) {
          ed_wrap_release(e); /* degrade; rebuilt by the count check */
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
    ed_wrap_materialize(e); /* recover from a failed splice */
  }

  /* Keep the styled-span copy usable through this transaction: if it
   * was current when the transaction began, forward-transform it per
   * delta — the same position rules the annot store applies to its
   * default anchors (start stays at an insert point, end moves) — and
   * restamp, so the frame between the edit and the producer's next
   * run stays styled instead of blinking unstyled.  The producer
   * still re-stamps truth next frame.  A copy that was already stale
   * stays stale (transforming from a wrong base would be wrong). */
  if (e->span_count > 0 &&
      lk_revision_equal(e->span_rev, deltas[0].before)) {
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
          sp->start = sp->start <= p     ? sp->start
                      : sp->start >= p + dl ? sp->start - dl
                                            : p;
          sp->end = sp->end <= p     ? sp->end
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
    /* The replay THIS editor invoked: derive the cursor from it
     * (docs/editor.md section 4): end of the last inserted range,
     * start of the last deleted one.  Other views over the same
     * document take the foreign-transform branch below. */
    const lk_doc_delta *last = &deltas[n - 1];

    e->cursor = last->inserted_len ? last->start + last->inserted_len
                                   : last->start;
    e->anchor = ED_NO_ANCHOR;
    e->sticky_x = ED_STICKY_NONE;
    e->pending_scroll = 1;
  } else if (!e->in_command) {
    /* Foreign transaction we did not initiate (including undo/redo
     * invoked from another view or from script): transform cursor
     * and anchor per delta in sequential order — delete first, then
     * insert, the annot-store position rules with RIGHT bias on
     * insert (matching the viewport affinity pinned in
     * include/lk-editor.h). */
    lk_u32 di;

    for (di = 0; di < n; di++) {
      lk_u32 p = deltas[di].start;
      lk_u32 dl = deltas[di].deleted_len;
      lk_u32 il = deltas[di].inserted_len;

      if (dl > 0) {
        if (e->cursor >= p + dl) {
          e->cursor -= dl;
        } else if (e->cursor > p) {
          e->cursor = p;
        }

        if (e->anchor != ED_NO_ANCHOR) {
          if (e->anchor >= p + dl) {
            e->anchor -= dl;
          } else if (e->anchor > p) {
            e->anchor = p;
          }
        }
      }

      if (il > 0) {
        if (e->cursor >= p) {
          e->cursor += il; /* RIGHT bias */
        }

        if (e->anchor != ED_NO_ANCHOR && e->anchor >= p) {
          e->anchor += il;
        }
      }
    }

    /* A selection collapsed by the transform is no selection. */
    if (e->anchor == e->cursor) {
      e->anchor = ED_NO_ANCHOR;
    }

    e->sticky_x = ED_STICKY_NONE;

    /* Positions from the transform are byte-exact, but the codepoint
     * boundary snap is a cheap invariant worth keeping. */
    e->cursor = ed_snap(d, e->cursor);
  }

  if (e->vp.top_byte > lk_doc_len(d)) {
    e->vp.top_byte = lk_doc_line_start(d, lk_doc_line_count(d) - 1);
    e->vp.y_offset = 0;
  }
}

/* ---- Lifecycle ---- */

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
  e->anchor = ED_NO_ANCHOR;
  e->sticky_x = ED_STICKY_NONE;
  e->editable = 1;
  e->tab_size = ED_TAB_SIZE;
  e->line_h = ED_FALLBACK_LINE_H;
  e->space_adv = ED_FALLBACK_ADVANCE;
  e->wrap_generation = 1; /* entry generation 0 is reserved invalid */
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

  e->dealloc(e->ud, e);
}

/* ---- Accessors ---- */

lk_document *lk_editor_doc(const lk_editor *e) {
  return e ? e->doc : NULL;
}

lk_u32 lk_editor_cursor(const lk_editor *e) {
  return e ? e->cursor : 0;
}

void lk_editor_set_cursor(lk_editor *e, lk_u32 pos) {
  if (!e) {
    return;
  }

  e->cursor = ed_snap(e->doc, pos);
  e->anchor = ED_NO_ANCHOR;
  e->sticky_x = ED_STICKY_NONE;
  e->pending_scroll = 1;
}

int lk_editor_selection(const lk_editor *e, lk_u32 *out_start,
                        lk_u32 *out_end) {
  lk_u32 lo;
  lk_u32 hi;

  if (!e || !ed_sel_range(e, &lo, &hi)) {
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

/* ---- Wrap modes (docs/editor-wrap.md section 5) ---- */

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

/* ---- Styled spans (docs/editor.md section 10) ---- */

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
    lk_edit_span *nb =
        (lk_edit_span *)ed_grow_buf(e, e->spans, 0,
                                    nc * (lk_u32)sizeof(lk_edit_span));

    if (!nb) {
      return; /* allocation failure degrades to "no spans" */
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

/* ---- Resource integration ---- */

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

/* ---- Command layer ---- */

/* One editing transaction bracket.  origin = 16 + cmd id, so every
 * observer (history, annot store, other views) can tell which command
 * produced a change. */
static void ed_begin(lk_editor *e, lk_editor_cmd_id cmd) {
  e->in_command = 1;
  lk_doc_begin(e->doc, 16u + (lk_u32)cmd);
}

static void ed_commit(lk_editor *e) {
  lk_doc_commit(e->doc);
  e->in_command = 0;
}

/* Shared editing epilogue: place the cursor, drop selection/sticky,
 * request scroll-to-cursor. */
static void ed_after_edit(lk_editor *e, lk_u32 cursor) {
  e->cursor = cursor;
  e->anchor = ED_NO_ANCHOR;
  e->sticky_x = ED_STICKY_NONE;
  e->pending_scroll = 1;
}

/* Insert bytes, replacing the selection when active, as ONE
 * transaction (replace = delete + insert inside one bracket). */
static int ed_insert(lk_editor *e, lk_editor_cmd_id cmd, const char *ptr,
                     lk_u32 len) {
  lk_u32 lo;
  lk_u32 hi;
  lk_u32 pos;

  if (!ptr || len == 0) {
    return 0;
  }

  ed_begin(e, cmd);

  if (ed_sel_range(e, &lo, &hi)) {
    lk_doc_delete(e->doc, lo, hi - lo);
    pos = lo;
  } else {
    pos = e->cursor;

    if (pos > lk_doc_len(e->doc)) {
      pos = lk_doc_len(e->doc);
    }
  }

  lk_doc_insert(e->doc, pos, ptr, len);
  ed_commit(e);
  ed_after_edit(e, pos + len);

  return 1;
}

/* Delete [lo, hi) as one transaction. */
static int ed_delete_range(lk_editor *e, lk_editor_cmd_id cmd, lk_u32 lo,
                           lk_u32 hi) {
  if (hi <= lo) {
    return 0;
  }

  ed_begin(e, cmd);
  lk_doc_delete(e->doc, lo, hi - lo);
  ed_commit(e);
  ed_after_edit(e, lo);

  return 1;
}

/* Copy the selection to the clipboard.  0 when there is no selection
 * or no clipboard (NULL ui degrades to a no-op). */
static int ed_copy(lk_editor *e, lk_ui *ui) {
  lk_u32 lo;
  lk_u32 hi;
  lk_u32 n;
  char *buf;

  if (!ui || !ui->clipboard_set) {
    return 0;
  }

  if (!ed_sel_range(e, &lo, &hi)) {
    return 0;
  }

  n = hi - lo;
  buf = (char *)e->alloc(e->ud, n + 1);

  if (!buf) {
    return 0;
  }

  lk_doc_get_text(e->doc, lo, buf, n);
  buf[n] = '\0';
  ui->clipboard_set(ui->clipboard_ud, buf);
  e->dealloc(e->ud, buf);

  return 1;
}

/* Lines per page from the last layout; 20 when no layout ran yet. */
static lk_i32 ed_page_size(const lk_editor *e) {
  return e->page_lines > 0 ? e->page_lines : ED_FALLBACK_PAGE_LINES;
}

/* Bounds of visual row (line, row): absolute start byte, byte length
 * (no \n), and whether it is the line's final row. */
static void ed_row_bounds(lk_editor *e, const lk_text_backend *tb,
                          lk_u32 line, lk_u32 row, lk_u32 *out_start,
                          lk_u32 *out_len, int *out_final) {
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

/* Vertical motion by delta VISUAL ROWS with sticky-x resolution in
 * the target row (a row is a whole line when wrapping is off, so the
 * pre-wrap behavior is the degenerate case).  Boundary behavior
 * mirrors weft: UP on the first row goes to 0, DOWN on the last row
 * goes to the end (sticky x preserved). */
static int ed_move_vert(lk_editor *e, lk_ui *ui, lk_i32 delta, int select) {
  const lk_text_backend *tb = ui ? ui->text : NULL;
  lk_u32 old_cursor = e->cursor;
  lk_u32 lo;
  lk_u32 hi;
  int had_sel = ed_sel_range(e, &lo, &hi);
  lk_u32 cl;
  lk_u32 cr;
  lk_u32 lcount = lk_doc_line_count(e->doc);
  int at_last;

  ed_pos_to_row(e, tb, e->cursor, &cl, &cr);
  at_last = cl >= lcount - 1 &&
            cr + 1 >= ed_row_count_of(ed_wrap_line(e, tb, cl));
  ed_motion_begin(e, select);

  if (delta < 0 && cl == 0 && cr == 0) {
    e->cursor = 0;
  } else if (delta > 0 && at_last) {
    e->cursor = lk_doc_len(e->doc);
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

    if (e->sticky_x < 0) {
      ed_row_bounds(e, tb, cl, cr, &rstart, &rlen, NULL);
      text = ed_line_text(e, cl, &tlen, &tstart);

      if (!text) {
        return 0;
      }

      e->sticky_x = ed_line_x_from_ix(e, tb, text + (rstart - tstart), rlen,
                                      e->cursor - rstart);
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

    ix = ed_line_ix_from_x(e, tb, text + (rstart - tstart), rlen,
                           e->sticky_x);

    /* A non-final row's end position IS the wrap break, which the
     * NEXT row owns -- clamp inside the row so the caret lands where
     * the motion aimed. */
    if (!final && ix >= rlen && rlen > 0) {
      ix = lk_utf8_prev(text + (rstart - tstart), rlen, rlen);
    }

    e->cursor = rstart + ix;
  }

  e->pending_scroll = 1;

  return e->cursor != old_cursor || (had_sel && !select);
}

/* Plain cursor placement shared by LINE/DOC start/end motion. */
static int ed_move_to(lk_editor *e, lk_u32 pos, int select) {
  lk_u32 lo;
  lk_u32 hi;
  int had_sel = ed_sel_range(e, &lo, &hi);
  lk_u32 old_cursor = e->cursor;

  ed_motion_begin(e, select);
  e->cursor = pos;
  e->sticky_x = ED_STICKY_NONE;
  e->pending_scroll = 1;

  return e->cursor != old_cursor || (had_sel && !select);
}

int lk_editor_command(lk_editor *e, lk_ui *ui, lk_editor_cmd_id cmd,
                      const lk_editor_cmd_arg *arg) {
  int select;
  lk_u32 lo;
  lk_u32 hi;

  if (!e) {
    return 0;
  }

  /* Read-only policy: user mutations are rejected here, the one
   * choke point every input path funnels through.  Motion,
   * selection, copy, and scrolling fall through untouched. */
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
    case LK_ED_REDO:
      return 0;
    default:
      break;
    }
  }

  select = arg ? arg->select : 0;

  switch (cmd) {
  case LK_ED_INSERT_TEXT:
    if (!arg) {
      return 0;
    }

    return ed_insert(e, cmd, arg->text.ptr, arg->text.len);

  case LK_ED_DELETE_BACKWARD:
    if (ed_sel_range(e, &lo, &hi)) {
      return ed_delete_range(e, cmd, lo, hi);
    }

    if (e->cursor == 0) {
      return 0;
    }

    return ed_delete_range(e, cmd, ed_prev_cp(e->doc, e->cursor), e->cursor);

  case LK_ED_DELETE_FORWARD:
    if (ed_sel_range(e, &lo, &hi)) {
      return ed_delete_range(e, cmd, lo, hi);
    }

    return ed_delete_range(e, cmd, e->cursor, ed_next_cp(e->doc, e->cursor));

  case LK_ED_DELETE_WORD_BACKWARD:
    if (ed_sel_range(e, &lo, &hi)) {
      return ed_delete_range(e, cmd, lo, hi);
    }

    return ed_delete_range(e, cmd, ed_word_left(e->doc, e->cursor),
                           e->cursor);

  case LK_ED_DELETE_WORD_FORWARD:
    if (ed_sel_range(e, &lo, &hi)) {
      return ed_delete_range(e, cmd, lo, hi);
    }

    return ed_delete_range(e, cmd, e->cursor,
                           ed_word_right(e->doc, e->cursor));

  case LK_ED_MOVE_LEFT:
    if (!select && ed_sel_range(e, &lo, &hi)) {
      /* Collapse to the selection's left edge (standard). */
      e->cursor = lo;
      e->anchor = ED_NO_ANCHOR;
      e->sticky_x = ED_STICKY_NONE;
      e->pending_scroll = 1;

      return 1;
    }

    ed_motion_begin(e, select);

    if (e->cursor == 0) {
      return 0;
    }

    e->cursor = ed_prev_cp(e->doc, e->cursor);
    e->sticky_x = ED_STICKY_NONE;
    e->pending_scroll = 1;

    return 1;

  case LK_ED_MOVE_RIGHT:
    if (!select && ed_sel_range(e, &lo, &hi)) {
      e->cursor = hi;
      e->anchor = ED_NO_ANCHOR;
      e->sticky_x = ED_STICKY_NONE;
      e->pending_scroll = 1;

      return 1;
    }

    ed_motion_begin(e, select);

    if (e->cursor >= lk_doc_len(e->doc)) {
      return 0;
    }

    e->cursor = ed_next_cp(e->doc, e->cursor);
    e->sticky_x = ED_STICKY_NONE;
    e->pending_scroll = 1;

    return 1;

  case LK_ED_MOVE_UP:
    return ed_move_vert(e, ui, -1, select);

  case LK_ED_MOVE_DOWN:
    return ed_move_vert(e, ui, 1, select);

  case LK_ED_MOVE_WORD_LEFT: {
    lk_u32 target = ed_word_left(e->doc, e->cursor);
    int had_sel = ed_sel_range(e, &lo, &hi);

    ed_motion_begin(e, select);

    if (target == e->cursor && !(had_sel && !select)) {
      return 0;
    }

    e->cursor = target;
    e->sticky_x = ED_STICKY_NONE;
    e->pending_scroll = 1;

    return 1;
  }

  case LK_ED_MOVE_WORD_RIGHT: {
    lk_u32 target = ed_word_right(e->doc, e->cursor);
    int had_sel = ed_sel_range(e, &lo, &hi);

    ed_motion_begin(e, select);

    if (target == e->cursor && !(had_sel && !select)) {
      return 0;
    }

    e->cursor = target;
    e->sticky_x = ED_STICKY_NONE;
    e->pending_scroll = 1;

    return 1;
  }

  case LK_ED_MOVE_LINE_START:
    return ed_move_to(
        e, lk_doc_line_start(e->doc, lk_doc_pos_to_line(e->doc, e->cursor)),
        select);

  case LK_ED_MOVE_LINE_END:
    return ed_move_to(
        e, lk_doc_line_end(e->doc, lk_doc_pos_to_line(e->doc, e->cursor)),
        select);

  case LK_ED_MOVE_ROW_START: {
    lk_u32 cl;
    lk_u32 cr;
    lk_u32 rstart;
    lk_u32 rlen;

    ed_pos_to_row(e, ui ? ui->text : NULL, e->cursor, &cl, &cr);
    ed_row_bounds(e, ui ? ui->text : NULL, cl, cr, &rstart, &rlen, NULL);

    return ed_move_to(e, rstart, select);
  }

  case LK_ED_MOVE_ROW_END: {
    lk_u32 cl;
    lk_u32 cr;
    lk_u32 rstart;
    lk_u32 rlen;

    ed_pos_to_row(e, ui ? ui->text : NULL, e->cursor, &cl, &cr);
    ed_row_bounds(e, ui ? ui->text : NULL, cl, cr, &rstart, &rlen, NULL);

    /* Row end: the wrap break for a non-final row (owned by the NEXT
     * row per the pinned rule -- the caret renders at its start), the
     * line end for the final row. */
    return ed_move_to(e, rstart + rlen, select);
  }

  case LK_ED_MOVE_DOC_START:
    return ed_move_to(e, 0, select);

  case LK_ED_MOVE_DOC_END:
    return ed_move_to(e, lk_doc_len(e->doc), select);

  case LK_ED_MOVE_PAGE_UP:
    return ed_move_vert(e, ui, -ed_page_size(e), select);

  case LK_ED_MOVE_PAGE_DOWN:
    return ed_move_vert(e, ui, ed_page_size(e), select);

  case LK_ED_SELECT_ALL:
    if (lk_doc_len(e->doc) == 0) {
      return 0;
    }

    e->anchor = 0;
    e->cursor = lk_doc_len(e->doc);
    e->sticky_x = ED_STICKY_NONE;

    return 1;

  case LK_ED_COPY:
    return ed_copy(e, ui);

  case LK_ED_CUT:
    if (!ed_copy(e, ui)) {
      return 0;
    }

    if (!ed_sel_range(e, &lo, &hi)) {
      return 0; /* unreachable: copy implies a selection */
    }

    return ed_delete_range(e, cmd, lo, hi);

  case LK_ED_PASTE: {
    const char *clip;

    if (!ui || !ui->clipboard_get) {
      return 0;
    }

    clip = ui->clipboard_get(ui->clipboard_ud);

    if (!clip || clip[0] == '\0') {
      return 0;
    }

    return ed_insert(e, cmd, clip, (lk_u32)strlen(clip));
  }

  case LK_ED_UNDO: {
    int ok;

    if (!e->hist) {
      return 0;
    }

    /* in_replay marks this editor as the invoking view: its ed_on_doc
     * jumps to the replay site; other views transform (foreign). */
    e->in_replay = 1;
    ok = lk_history_undo(e->hist, e->doc);
    e->in_replay = 0;

    return ok;
  }

  case LK_ED_REDO: {
    int ok;

    if (!e->hist) {
      return 0;
    }

    e->in_replay = 1;
    ok = lk_history_redo(e->hist, e->doc);
    e->in_replay = 0;

    return ok;
  }

  case LK_ED_SET_CURSOR: {
    lk_u32 pos;

    if (!arg) {
      return 0;
    }

    pos = ed_snap(e->doc, arg->set_cursor.pos);

    if (arg->set_cursor.extend) {
      if (e->anchor == ED_NO_ANCHOR) {
        e->anchor = e->cursor;
      }
    } else {
      e->anchor = ED_NO_ANCHOR;
    }

    e->cursor = pos;
    e->sticky_x = ED_STICKY_NONE;
    e->pending_scroll = 1;

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
        /* near: walk visual rows, measuring only the walked lines */
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
        /* distant down: consume exact-or-ESTIMATED rows per line
         * (cheap arithmetic, no backend calls) to pick the target
         * line, then measure just that line -- the scroll-extent
         * edge is the one sanctioned estimate consumer. */
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
        /* distant up: same estimate consumption backwards */
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

      e->vp.top_byte =
          lk_doc_line_start(e->doc, al) +
          ed_row_start_rel(ed_wrap_line(e, tb, al), ar);
    }

    /* Precise bottom clamping happens at the next layout, which knows
     * the viewport height. */
    return e->vp.top_byte != old_top || e->vp.y_offset != old_off;
  }

  default:
    return 0;
  }
}

/* ---- Layout hook body (transient geometry) ---- */

/* Lexicographic visual-row order. */
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

  /* One-editor-one-node claim check.  lk_ui alternates its two trees
   * between frames, so a still-valid stamp with the SAME tree pointer
   * but a different node id means a second claimant inside the same
   * layout pass (debug-only heuristic; an intermittently attached
   * editor whose node id changes can false-positive).  Release: last
   * claimant wins -- the block simply gets restamped. */
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

  /* Staleness policy (pinned, docs/editor.md section 10): the span
   * snapshot participates only when its revision matches the document
   * NOW -- and any later edit invalidates this whole geometry block,
   * so a frame can never draw misplaced styling. */
  spans_on = e->span_count > 0 &&
             lk_revision_equal(e->span_rev, lk_doc_revision(e->doc));

  view_h = content->h;
  e->page_lines = view_h > 0 ? view_h / line_h : 0;
  lcount = lk_doc_line_count(e->doc);
  q = view_h > 0 ? (lk_u32)(view_h / line_h) : 0;
  want_scroll = e->pending_scroll;

  /* Wrap key check (docs/editor-wrap.md section 1): any change in
   * width/font/tab/backend-pointer bumps the generation -- one
   * integer write, never a sweep. */
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

    e->scroll_x = 0; /* horizontal scroll is a NONE-mode feature */
  }

  /* Resolve the anchor: clamp into the document, snap top_byte to
   * the visual-row start at-or-before it (local resolution: only the
   * anchor's own line is measured). */
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

  /* Bottom clamp.  Skipped when there are more than q+1 whole LINES
   * below the anchor (rows >= lines, so the anchor is provably above
   * the maximum) -- this keeps a mid-document viewport from ever
   * measuring doc-end lines.  Otherwise back-walk from the last row,
   * measuring at most a viewport's worth of lines. */
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

  /* Pending scroll-to-cursor: bring the cursor ROW fully into view
   * (top-aligned when above, bottom-aligned when below).  Two paths
   * by construction: near targets walk at most q+1 rows; distant
   * targets (outside the materialized window by line count alone)
   * re-anchor directly at the cursor row and back up a viewport --
   * never a walk over the intervening lines. */
  if (want_scroll && view_h > 0) {
    lk_u32 cl;
    lk_u32 cr;

    ed_pos_to_row(e, tb, e->cursor, &cl, &cr);

    if (ed_rowpos_cmp(cl, cr, al, ar) < 0 ||
        (ed_rowpos_cmp(cl, cr, al, ar) == 0 && e->vp.y_offset > 0)) {
      al = cl;
      ar = cr;
      e->vp.y_offset = 0;
    } else if (ed_rowpos_cmp(cl, cr, al, ar) > 0) {
      int below;

      if (cl - al > q + 1) {
        below = 1; /* distant: provably past the viewport */
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
                    ? ((lk_i32)rel * line_h - e->vp.y_offset + line_h) >
                          view_h
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

  /* Scrollbar extent: total visual rows and the rows above the
   * resolved anchor, exact-or-estimated per line (the scroll-extent
   * edge is the one sanctioned estimate consumer; cheap arithmetic,
   * no backend calls). */
  {
    lk_u32 li;
    lk_u32 sb_total = 0;
    unsigned long sb_rows_before = 0;
    unsigned long sb_top;

    for (li = 0; li < lcount; li++) {
      lk_u32 rows_n = ed_rows_or_est(e, li);

      if (li < al) {
        sb_rows_before += (unsigned long)rows_n;
      }

      sb_total += rows_n;
    }

    sb_top = (sb_rows_before + (unsigned long)ar) * (unsigned long)line_h +
             (unsigned long)e->vp.y_offset;
    e->geom.sb_total_rows = sb_total;
    e->geom.sb_top_px = (lk_u32)sb_top;
  }

  /* Horizontal scroll (NONE mode only, docs/editor-wrap.md section
   * 4): follow the cursor with a ~2-space margin, then soft-clamp
   * against the widest MEASURED visible line (documented: lines
   * outside the viewport do not extend the range). */
  if (e->wrap_mode == LK_EDITOR_WRAP_NONE) {
    lk_i32 margin = 2 * ed_advance(e);

    if (want_scroll && view_h > 0) {
      lk_u32 cl = lk_doc_pos_to_line(e->doc, e->cursor);
      const char *text;
      lk_u32 tlen;
      lk_u32 tstart;

      text = ed_line_text(e, cl, &tlen, &tstart);

      if (text) {
        lk_i32 cx = ed_line_x_from_ix(e, tb, text, tlen,
                                      e->cursor - tstart);

        if (cx < e->scroll_x + margin) {
          e->scroll_x = cx > margin ? cx - margin : 0;
        } else if (content->w > 0 &&
                   cx > e->scroll_x + content->w - margin) {
          e->scroll_x = cx - content->w + margin;
        }
      }
    }

    if (e->scroll_x > 0 && view_h > 0) {
      lk_i32 widest = 0;
      lk_i32 cap;
      lk_u32 nvis = (lk_u32)((view_h + e->vp.y_offset + line_h - 1) /
                             line_h);

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

  /* Visible rows: rows intersecting [0, view_h), walked forward from
   * the anchor (virtualization: cost is viewport-, never document-,
   * proportional). */
  vis_needed =
      view_h > 0
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

      /* Segment walk (shared shape with ed_line_x_from_ix); tab
       * stops are relative to the ROW's x = 0, matching the break
       * finder; each tab-free run is further span-split by
       * ed_emit_run when a revision-matched snapshot overlaps it. */
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

  /* Cursor geometry (only when its row is visible; ownership of a
   * position at a wrap break comes from ed_pos_to_row: the NEXT
   * row). */
  {
    lk_u32 cl;
    lk_u32 cr;

    ed_pos_to_row(e, tb, e->cursor, &cl, &cr);

    for (k = 0; k < vis_count; k++) {
      const ed_line *ln = &e->lines[k];

      if (ln->line == cl && ln->row == cr) {
        lk_i32 cx = ed_line_x_from_ix(e, tb, e->vis + ln->off, ln->doc_len,
                                      e->cursor - ln->doc_start);

        e->geom.cursor_vis = 1;
        e->geom.cursor_x = content->x + cx - e->scroll_x;
        e->geom.cursor_y = content->y + (lk_i32)k * line_h - e->vp.y_offset;
        break;
      }
    }
  }

  /* Selection rects: head partial row, body block, tail partial row,
   * clipped to the visible range.  Zero-width pieces are dropped.
   * Row membership of the endpoints comes from ed_pos_to_row, so
   * selection geometry can never disagree with the cursor by a
   * row. */
  {
    lk_u32 lo;
    lk_u32 hi;

    e->geom.sel_count = 0;

    if (vis_count > 0 && ed_sel_range(e, &lo, &hi)) {
      lk_u32 ll;
      lk_u32 lr;
      lk_u32 hl;
      lk_u32 hr;

      ed_pos_to_row(e, tb, lo, &ll, &lr);
      ed_pos_to_row(e, tb, hi, &hl, &hr);

      if (ll == hl && lr == hr) {
        for (k = 0; k < vis_count; k++) {
          const ed_line *ln = &e->lines[k];

          if (ln->line == ll && ln->row == lr) {
            lk_i32 x0 = ed_line_x_from_ix(e, tb, e->vis + ln->off,
                                          ln->doc_len, lo - ln->doc_start);
            lk_i32 x1 = ed_line_x_from_ix(e, tb, e->vis + ln->off,
                                          ln->doc_len, hi - ln->doc_start);

            if (x1 > x0) {
              lk_rect *sr = &e->geom.sel_rects[e->geom.sel_count++];

              sr->x = content->x + x0 - e->scroll_x;
              sr->y = content->y + (lk_i32)k * line_h - e->vp.y_offset;
              sr->w = x1 - x0;
              sr->h = line_h;
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
            /* head */
            lk_i32 x0 = ed_line_x_from_ix(e, tb, e->vis + ln->off,
                                          ln->doc_len, lo - ln->doc_start);
            lk_i32 x1 = ed_line_x_from_ix(e, tb, e->vis + ln->off,
                                          ln->doc_len, ln->doc_len);

            if (x1 > x0) {
              lk_rect *sr = &e->geom.sel_rects[e->geom.sel_count++];

              sr->x = content->x + x0 - e->scroll_x;
              sr->y = content->y + (lk_i32)k * line_h - e->vp.y_offset;
              sr->w = x1 - x0;
              sr->h = line_h;
            }
          } else if (c_lo > 0 && c_hi < 0) {
            /* body row (fully covered) */
            if (!have_body) {
              b0 = k;
              have_body = 1;
            }

            b1 = k;
          }
        }

        if (have_body) {
          lk_rect *sr = &e->geom.sel_rects[e->geom.sel_count++];

          sr->x = content->x;
          sr->y = content->y + (lk_i32)b0 * line_h - e->vp.y_offset;
          sr->w = content->w;
          sr->h = (lk_i32)(b1 - b0 + 1) * line_h;
        }

        for (k = 0; k < vis_count; k++) {
          const ed_line *ln = &e->lines[k];

          if (ln->line == hl && ln->row == hr) {
            /* tail */
            lk_i32 x1 = ed_line_x_from_ix(e, tb, e->vis + ln->off,
                                          ln->doc_len, hi - ln->doc_start);

            if (x1 > 0) {
              lk_rect *sr = &e->geom.sel_rects[e->geom.sel_count++];

              sr->x = content->x - e->scroll_x;
              sr->y = content->y + (lk_i32)k * line_h - e->vp.y_offset;
              sr->w = x1;
              sr->h = line_h;
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

/* ---- Render hook body (pure geometry, no backend) ---- */

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
    return; /* no layout yet, or another node claimed the editor */
  }

  /* Clip our own text to the node rect (the engine's clip for
   * clips = 1 kinds only wraps children, and the editor is a leaf). */
  memset(&cmd, 0, sizeof(cmd));
  cmd.op = LK_ROP_CLIP_BEGIN;
  cmd.rect = *rect;
  lk_render_list_push(out, cmd);

  /* Span backgrounds first: selection rects and the cursor render
   * above them, exactly as they did before spans existed. */
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

  /* Selection highlight (same color as the text input widget; a
   * dedicated style field is deliberately not added in this stage). */
  for (i = 0; i < e->geom.sel_count; i++) {
    memset(&cmd, 0, sizeof(cmd));
    cmd.op = LK_ROP_FILL_RECT;
    cmd.rect = e->geom.sel_rects[i];
    cmd.color.r = 80;
    cmd.color.g = 120;
    cmd.color.b = 200;
    cmd.color.a = 128;
    lk_render_list_push(out, cmd);
  }

  /* One DRAW_RUN per visible line segment through the byte arena
   * (span sub-segments carry their own fg; underlines are 1-px fills
   * at the text baseline + 1). */
  for (i = 0; i < e->seg_count; i++) {
    const ed_seg *seg = &e->segs[i];
    lk_u32 run_off;
    lk_color run_fg;

    if (seg->len == 0) {
      continue;
    }

    if (!lk_render_list_push_run(out, e->vis + seg->off, seg->len,
                                 &run_off)) {
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
    lk_render_list_push(out, cmd);

    if (seg->flags & LK_SPAN_UNDERLINE) {
      memset(&cmd, 0, sizeof(cmd));
      cmd.op = LK_ROP_FILL_RECT;
      cmd.rect.x = seg->x;
      cmd.rect.y = seg->y + e->geom.baseline + 1;
      cmd.rect.w = seg->w;
      cmd.rect.h = 1;
      cmd.color = run_fg;
      lk_render_list_push(out, cmd);
    }
  }

  /* Cursor bar -- only when this node holds keyboard focus (the
   * LKS_FOCUSED flag is kept in sync by the lk_focus_* functions,
   * same mechanism as the text input widget). */
  if (state && e->geom.cursor_vis) {
    lk_value f = lk_state_get(state, nid, LKS_FOCUSED);

    if (f.tag == UIV_I32 && f.as.i != 0) {
      memset(&cmd, 0, sizeof(cmd));
      cmd.op = LK_ROP_FILL_RECT;
      cmd.rect.x = e->geom.cursor_x;
      cmd.rect.y = e->geom.cursor_y;
      cmd.rect.w = 1;
      cmd.rect.h = e->geom.line_h;
      cmd.color = style->fg;
      lk_render_list_push(out, cmd);
    }
  }

  /* Overlay scrollbar (v1, non-interactive — wheel scrolls; the
   * scroll widget's bar does not drag either).  Drawn last inside the
   * node clip so it overlays text and cursor (the lk-scroll.c feel,
   * where the bar paints over the content edge), and reserving NO
   * width: a bar that reserved width would change the wrap width,
   * changing the row count, changing the overflow that decides
   * whether the bar exists — a feedback loop this overlay style
   * deliberately avoids.  Geometry comes from the section-6
   * scroll-extent model stamped by layout (sb_total_rows /
   * sb_top_px: exact rows for measured lines, estimator rows
   * otherwise).  Hidden when the content fits; wrap NONE horizontal
   * overflow gets no horizontal bar in v1. */
  {
    lk_i32 line_h = e->geom.line_h;
    lk_i32 track_h = e->geom.rect.h;
    unsigned long total_px =
        (unsigned long)e->geom.sb_total_rows * (unsigned long)line_h;

    if (track_h > 0 && line_h > 0 && total_px > (unsigned long)track_h) {
      unsigned long top_px = (unsigned long)e->geom.sb_top_px;
      unsigned long max_top = total_px - (unsigned long)track_h;
      lk_i32 bar_x = rect->x + rect->w - ED_SCROLL_BAR_W;
      lk_i32 thumb_h = (lk_i32)(((unsigned long)track_h *
                                 (unsigned long)track_h) /
                                total_px);
      lk_i32 thumb_y;

      if (thumb_h < ED_SCROLL_THUMB_MIN) {
        thumb_h = ED_SCROLL_THUMB_MIN;
      }

      if (thumb_h > track_h) {
        thumb_h = track_h;
      }

      /* Estimator drift can put the anchor past the derived extent;
       * clamp so the thumb never leaves the track. */
      if (top_px > max_top) {
        top_px = max_top;
      }

      thumb_y = e->geom.rect.y +
                (lk_i32)((top_px * (unsigned long)(track_h - thumb_h)) /
                         max_top);

      /* Track */
      memset(&cmd, 0, sizeof(cmd));
      cmd.op = LK_ROP_FILL_RECT;
      cmd.rect.x = bar_x;
      cmd.rect.y = e->geom.rect.y;
      cmd.rect.w = ED_SCROLL_BAR_W;
      cmd.rect.h = track_h;
      cmd.color = style->scrollbar_track;
      lk_render_list_push(out, cmd);

      /* Thumb */
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

/* ---- Pointer hit mapping ---- */

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
  *out_pos = ln->doc_start +
             ed_line_ix_from_x(e, tb, e->vis + ln->off, ln->doc_len,
                               x - e->geom.rect.x + e->geom.scroll_x);

  return 1;
}

int lk_editor_pos_at(const lk_editor *e, lk_i32 x, lk_i32 y, lk_u32 *out_pos) {
  if (!e || !out_pos || !e->geom.valid) {
    return 0; /* before first layout / snapshot invalidated by an edit */
  }

  /* Outside the laid-out rect: no clamping (pinned contract). */
  if (x < e->geom.rect.x || x >= e->geom.rect.x + e->geom.rect.w ||
      y < e->geom.rect.y || y >= e->geom.rect.y + e->geom.rect.h) {
    return 0;
  }

  return lk_editor_hit_pos(e, e->geom.tb, x, y, out_pos);
}

/* ---- Interior presentations (weft-surface S1) ---- */

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

  /* Stamp the editor-owned locus words: the kind vocabulary and the
   * hit position (the source filled annot id / range / revision). */
  locus_kind = t->intern ? lk_intern_cid(t->intern, "editor-range") : 0;

  for (i = 0; i < count; i++) {
    hits[i].locus_kind = locus_kind;
    hits[i].locus[3] = pos;
  }

  return lk_translate_presentations(ui, t, n, ev, hits, count);
}
