/*
 * lk-editor.c -- editor view state + command implementations (editor
 * track, stage B2; docs/editor.md sections 6 and 9).
 *
 * State: cursor byte offset (codepoint-boundary-aligned), selection
 * anchor (sentinel = none), sticky x-pixel for vertical motion
 * (resolved lazily on the first UP/DOWN after a horizontal move),
 * anchored viewport {top_line, y_offset}, drag flag, tab settings,
 * growable scratch for visible-line extraction, and a transient
 * per-frame geometry block written by lk_editor_layout_node.
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

/* One visible line extracted into the vis scratch. */
typedef struct ed_line {
  lk_u32 doc_start; /* byte offset of the line start in the document */
  lk_u32 doc_len;   /* line length in bytes, excluding the \n */
  lk_u32 off;       /* offset of the line's bytes in e->vis */
} ed_line;

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
  int cursor_vis;
  lk_i32 cursor_x, cursor_y;
  lk_rect sel_rects[ED_MAX_SEL_RECTS];
  lk_u32 sel_count;
  lk_u16 font_id, font_size;
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
  lk_i32 sticky_x; /* ED_STICKY_NONE = unset */
  lk_editor_viewport vp;
  int drag;
  lk_u32 tab_size;
  int in_command; /* inside one of our own doc transactions */
  int pending_scroll;

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

  /* styled-span snapshot (deep copy; docs/editor.md section 10) */
  lk_edit_span *spans;
  lk_u32 span_count, span_cap;
  lk_revision span_rev;
  lk_u32 span_range_start, span_range_end;

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

  if (deltas[0].origin == LK_ORIGIN_UNDO ||
      deltas[0].origin == LK_ORIGIN_REDO) {
    /* Derive the cursor from the replay (docs/editor.md section 4):
     * end of the last inserted range, start of the last deleted
     * one. */
    const lk_doc_delta *last = &deltas[n - 1];

    e->cursor = last->inserted_len ? last->start + last->inserted_len
                                   : last->start;
    e->anchor = ED_NO_ANCHOR;
    e->sticky_x = ED_STICKY_NONE;
    e->pending_scroll = 1;
  } else if (!e->in_command) {
    /* Foreign transaction we did not initiate: clamp cursor,
     * selection, and viewport to the new document (full foreign-edit
     * position adjustment is deferred). */
    lk_u32 len = lk_doc_len(d);

    e->cursor = ed_snap(d, e->cursor);

    if (e->anchor != ED_NO_ANCHOR && e->anchor > len) {
      e->anchor = len;
    }

    e->sticky_x = ED_STICKY_NONE;
  }

  {
    lk_u32 lc = lk_doc_line_count(d);

    if (e->vp.top_line >= lc) {
      e->vp.top_line = lc - 1;
      e->vp.y_offset = 0;
    }
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
  e->tab_size = ED_TAB_SIZE;
  e->line_h = ED_FALLBACK_LINE_H;
  e->space_adv = ED_FALLBACK_ADVANCE;
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

  vp.top_line = 0;
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

/* Vertical motion by delta lines with sticky-x column resolution.
 * Boundary behavior mirrors weft: UP on the first line goes to 0,
 * DOWN on the last line goes to the end (sticky x preserved). */
static int ed_move_vert(lk_editor *e, lk_ui *ui, lk_i32 delta, int select) {
  const lk_text_backend *tb = ui ? ui->text : NULL;
  lk_u32 old_cursor = e->cursor;
  lk_u32 lo;
  lk_u32 hi;
  int had_sel = ed_sel_range(e, &lo, &hi);
  lk_u32 line = lk_doc_pos_to_line(e->doc, e->cursor);
  lk_u32 lcount = lk_doc_line_count(e->doc);

  ed_motion_begin(e, select);

  if (delta < 0 && line == 0) {
    e->cursor = 0;
  } else if (delta > 0 && line >= lcount - 1) {
    e->cursor = lk_doc_len(e->doc);
  } else {
    lk_u32 target;
    const char *text;
    lk_u32 tlen;
    lk_u32 tstart;

    if (delta < 0) {
      lk_u32 d = (lk_u32)(-delta);

      target = line > d ? line - d : 0;
    } else {
      target = line + (lk_u32)delta;

      if (target > lcount - 1) {
        target = lcount - 1;
      }
    }

    if (e->sticky_x < 0) {
      text = ed_line_text(e, line, &tlen, &tstart);

      if (!text) {
        return 0;
      }

      e->sticky_x = ed_line_x_from_ix(e, tb, text, tlen, e->cursor - tstart);
    }

    text = ed_line_text(e, target, &tlen, &tstart);

    if (!text) {
      return 0;
    }

    e->cursor = tstart + ed_line_ix_from_x(e, tb, text, tlen, e->sticky_x);
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

  case LK_ED_UNDO:
    if (!e->hist) {
      return 0;
    }

    return lk_history_undo(e->hist, e->doc);

  case LK_ED_REDO:
    if (!e->hist) {
      return 0;
    }

    return lk_history_redo(e->hist, e->doc);

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
    lk_u32 old_top = e->vp.top_line;
    lk_i32 old_off = e->vp.y_offset;
    lk_u32 lcount;

    if (!arg || arg->lines == 0) {
      return 0;
    }

    if (arg->lines < 0) {
      lk_u32 d = (lk_u32)(-arg->lines);

      if (e->vp.top_line > d) {
        e->vp.top_line -= d;
      } else {
        e->vp.top_line = 0;
        e->vp.y_offset = 0;
      }
    } else {
      e->vp.top_line += (lk_u32)arg->lines;
      lcount = lk_doc_line_count(e->doc);

      if (e->vp.top_line >= lcount) {
        e->vp.top_line = lcount - 1;
      }
    }

    /* Precise bottom clamping happens at the next layout, which knows
     * the viewport height. */
    return e->vp.top_line != old_top || e->vp.y_offset != old_off;
  }

  default:
    return 0;
  }
}

/* ---- Layout hook body (transient geometry) ---- */

/* Decompose "pixel offset = top_line * line_h + y_offset" arithmetic
 * without ever forming the full product (docs/editor.md section 9).
 * Given viewport height V = q * line_h + r, the maximum anchored
 * scroll for a document of L lines is:
 *   L <= q          -> (0, 0)
 *   r == 0          -> (L - q, 0)
 *   otherwise       -> (L - q - 1, line_h - r)
 */
static void ed_max_scroll(lk_u32 lcount, lk_i32 line_h, lk_i32 view_h,
                          lk_u32 *out_top, lk_i32 *out_off) {
  lk_u32 q;
  lk_i32 r;

  if (view_h <= 0 || line_h <= 0) {
    *out_top = 0;
    *out_off = 0;

    return;
  }

  q = (lk_u32)(view_h / line_h);
  r = view_h % line_h;

  if (lcount <= q) {
    *out_top = 0;
    *out_off = 0;
  } else if (r == 0) {
    *out_top = lcount - q;
    *out_off = 0;
  } else {
    *out_top = lcount - q - 1;
    *out_off = line_h - r;
  }
}

void lk_editor_layout_node(lk_editor *e, const lk_tree *t, lk_ix n,
                           const lk_rect *content, const lk_layout_cfg *cfg) {
  const lk_text_backend *tb;
  lk_i32 line_h;
  lk_i32 view_h;
  lk_u32 lcount;
  lk_u32 max_top;
  lk_i32 max_off;
  lk_u32 vis_count;
  lk_u32 k;
  lk_i32 baseline;
  int spans_on;

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

  /* Clamp the anchor to the document and the viewport. */
  if (e->vp.top_line >= lcount) {
    e->vp.top_line = lcount - 1;
    e->vp.y_offset = 0;
  }

  if (e->vp.y_offset < 0) {
    e->vp.y_offset = 0;
  }

  if (e->vp.y_offset >= line_h) {
    e->vp.y_offset = line_h - 1;
  }

  ed_max_scroll(lcount, line_h, view_h, &max_top, &max_off);

  if (e->vp.top_line > max_top ||
      (e->vp.top_line == max_top && e->vp.y_offset > max_off)) {
    e->vp.top_line = max_top;
    e->vp.y_offset = max_off;
  }

  /* Pending scroll-to-cursor: bring the cursor line fully into view
   * (top-aligned when above, bottom-aligned when below). */
  if (e->pending_scroll && view_h > 0) {
    lk_u32 cl = lk_doc_pos_to_line(e->doc, e->cursor);
    lk_u32 q = (lk_u32)(view_h / line_h);
    lk_i32 r = view_h % line_h;

    if (cl < e->vp.top_line ||
        (cl == e->vp.top_line && e->vp.y_offset > 0)) {
      e->vp.top_line = cl;
      e->vp.y_offset = 0;
    } else {
      lk_u32 rel = cl - e->vp.top_line;
      int below;

      /* rel > q means the line starts past the viewport even at
       * y_offset = line_h - 1; otherwise the product is small. */
      if (rel > q) {
        below = 1;
      } else {
        below = ((lk_i32)rel * line_h - e->vp.y_offset + line_h) > view_h;
      }

      if (below && cl + 1 > q) {
        if (r == 0) {
          e->vp.top_line = cl + 1 - q;
          e->vp.y_offset = 0;
        } else {
          e->vp.top_line = cl - q;
          e->vp.y_offset = line_h - r;
        }
      } else if (below) {
        e->vp.top_line = 0;
        e->vp.y_offset = 0;
      }
    }

    e->pending_scroll = 0;
  }

  /* Visible line range: lines intersecting [0, view_h). */
  if (view_h > 0) {
    vis_count = (lk_u32)((view_h + e->vp.y_offset + line_h - 1) / line_h);

    if (vis_count > lcount - e->vp.top_line) {
      vis_count = lcount - e->vp.top_line;
    }
  } else {
    vis_count = 0;
  }

  /* Extract visible lines into scratch and precompute tab-expanded
   * run segments (virtualization: cost is viewport-, never
   * document-, proportional). */
  e->vis_len = 0;
  e->seg_count = 0;

  if (!ed_lines_reserve(e, vis_count ? vis_count : 1)) {
    return;
  }

  for (k = 0; k < vis_count; k++) {
    lk_u32 line = e->vp.top_line + k;
    lk_u32 ls = lk_doc_line_start(e->doc, line);
    lk_u32 le = lk_doc_line_end(e->doc, line);
    lk_u32 llen = le - ls;
    lk_i32 y = content->y + (lk_i32)k * line_h - e->vp.y_offset;
    const char *p;
    lk_i32 x;
    lk_u32 i;
    lk_i32 tabpx;

    if (!ed_vis_reserve(e, e->vis_len + llen)) {
      return;
    }

    if (llen) {
      lk_doc_get_text(e->doc, ls, e->vis + e->vis_len, llen);
    }

    e->lines[k].doc_start = ls;
    e->lines[k].doc_len = llen;
    e->lines[k].off = e->vis_len;

    /* Segment walk (shared shape with ed_line_x_from_ix); each
     * tab-free run is further span-split by ed_emit_run when a
     * revision-matched snapshot overlaps it. */
    p = e->vis + e->vis_len;
    x = 0;
    i = 0;
    tabpx = ed_tab_px(e);

    while (i < llen) {
      lk_u32 j = i;
      lk_i32 w;

      while (j < llen && p[j] != '\t') {
        j++;
      }

      w = ed_run_x(e, tb, p + i, j - i, j - i);

      if (j > i) {
        if (!ed_emit_run(e, tb, p + i, j - i, e->vis_len + i, ls + i,
                         content->x + x, y, w, spans_on)) {
          return;
        }
      }

      x += w;

      if (j < llen) {
        x = (x / tabpx + 1) * tabpx;
        i = j + 1;
      } else {
        i = j;
      }
    }

    e->vis_len += llen;
  }

  /* Cursor geometry (only when its line is visible). */
  {
    lk_u32 cl = lk_doc_pos_to_line(e->doc, e->cursor);

    if (cl >= e->vp.top_line && cl < e->vp.top_line + vis_count) {
      lk_u32 ck = cl - e->vp.top_line;
      const ed_line *ln = &e->lines[ck];
      lk_i32 cx = ed_line_x_from_ix(e, tb, e->vis + ln->off, ln->doc_len,
                                    e->cursor - ln->doc_start);

      e->geom.cursor_vis = 1;
      e->geom.cursor_x = content->x + cx;
      e->geom.cursor_y = content->y + (lk_i32)ck * line_h - e->vp.y_offset;
    }
  }

  /* Selection rects: head partial line, body block, tail partial
   * line, clipped to the visible range.  Zero-width pieces are
   * dropped. */
  {
    lk_u32 lo;
    lk_u32 hi;

    e->geom.sel_count = 0;

    if (vis_count > 0 && ed_sel_range(e, &lo, &hi)) {
      lk_u32 lo_line = lk_doc_pos_to_line(e->doc, lo);
      lk_u32 hi_line = lk_doc_pos_to_line(e->doc, hi);
      lk_u32 first = e->vp.top_line;
      lk_u32 last = e->vp.top_line + vis_count - 1;

      if (lo_line == hi_line) {
        if (lo_line >= first && lo_line <= last) {
          lk_u32 ck = lo_line - first;
          const ed_line *ln = &e->lines[ck];
          lk_i32 x0 = ed_line_x_from_ix(e, tb, e->vis + ln->off, ln->doc_len,
                                        lo - ln->doc_start);
          lk_i32 x1 = ed_line_x_from_ix(e, tb, e->vis + ln->off, ln->doc_len,
                                        hi - ln->doc_start);

          if (x1 > x0) {
            lk_rect *sr = &e->geom.sel_rects[e->geom.sel_count++];

            sr->x = content->x + x0;
            sr->y = content->y + (lk_i32)ck * line_h - e->vp.y_offset;
            sr->w = x1 - x0;
            sr->h = line_h;
          }
        }
      } else {
        /* head */
        if (lo_line >= first && lo_line <= last) {
          lk_u32 ck = lo_line - first;
          const ed_line *ln = &e->lines[ck];
          lk_i32 x0 = ed_line_x_from_ix(e, tb, e->vis + ln->off, ln->doc_len,
                                        lo - ln->doc_start);
          lk_i32 x1 =
              ed_line_x_from_ix(e, tb, e->vis + ln->off, ln->doc_len,
                                ln->doc_len);

          if (x1 > x0) {
            lk_rect *sr = &e->geom.sel_rects[e->geom.sel_count++];

            sr->x = content->x + x0;
            sr->y = content->y + (lk_i32)ck * line_h - e->vp.y_offset;
            sr->w = x1 - x0;
            sr->h = line_h;
          }
        }

        /* body block */
        if (hi_line > lo_line + 1) {
          lk_u32 b0 = lo_line + 1 > first ? lo_line + 1 : first;
          lk_u32 b1 = hi_line - 1 < last ? hi_line - 1 : last;

          if (b0 <= b1) {
            lk_rect *sr = &e->geom.sel_rects[e->geom.sel_count++];

            sr->x = content->x;
            sr->y = content->y + (lk_i32)(b0 - first) * line_h -
                    e->vp.y_offset;
            sr->w = content->w;
            sr->h = (lk_i32)(b1 - b0 + 1) * line_h;
          }
        }

        /* tail */
        if (hi_line >= first && hi_line <= last) {
          lk_u32 ck = hi_line - first;
          const ed_line *ln = &e->lines[ck];
          lk_i32 x1 = ed_line_x_from_ix(e, tb, e->vis + ln->off, ln->doc_len,
                                        hi - ln->doc_start);

          if (x1 > 0) {
            lk_rect *sr = &e->geom.sel_rects[e->geom.sel_count++];

            sr->x = content->x;
            sr->y = content->y + (lk_i32)ck * line_h - e->vp.y_offset;
            sr->w = x1;
            sr->h = line_h;
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
  e->geom.first_line = e->vp.top_line;
  e->geom.vis_count = vis_count;
  e->geom.font_id = e->font_id;
  e->geom.font_size = e->font_size;
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
  *out_pos = ln->doc_start + ed_line_ix_from_x(e, tb, e->vis + ln->off,
                                               ln->doc_len,
                                               x - e->geom.rect.x);

  return 1;
}
