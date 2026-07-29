/*
 * lk-document.c -- piece-table text document (editor track, stage A).
 *
 * Port of weft's document.c: immutable original buffer, append-only
 * add buffer, piece array, incremental line index, cached total
 * length.  The piece split/merge and line-index semantics are
 * preserved exactly; what is new around them is the change protocol:
 * transactions, per-op deltas carrying the actual bytes, and a
 * subscriber list notified once per committed transaction.
 *
 * Delta byte pointers are resolved only at notification time — the
 * add buffer and the deleted-bytes scratch may both be reallocated
 * while a transaction is open, so deltas store offsets internally.
 */

#include <string.h>

#include "lk-document.h"
#include "core/lk-memory.h"

#ifdef LK_EDITOR_DEBUG_ASSERTS
#include <assert.h>
#define LK_DOC_ASSERT(x) assert(x)
#else
#define LK_DOC_ASSERT(x) ((void)0)
#endif

#define DOC_INITIAL_ADD_CAP 1024
#define DOC_INITIAL_PIECES_CAP 16
#define DOC_INITIAL_LINES_CAP 64
#define DOC_INITIAL_DELTA_CAP 8
#define DOC_INITIAL_SCRATCH_CAP 256
#define DOC_INITIAL_SUBS_CAP 4

#define PIECE_ORIGINAL 0
#define PIECE_ADD 1

typedef struct doc_piece {
  lk_u8 source; /* PIECE_ORIGINAL or PIECE_ADD */
  lk_u32 start; /* byte offset into source buffer */
  lk_u32 length;
} doc_piece;

typedef struct doc_sub {
  lk_u32 id;
  lk_doc_listener_fn fn;
  void *ud;
} doc_sub;

struct lk_document {
  void *(*alloc)(void *, lk_u32);
  void (*dealloc)(void *, void *);
  void *ud;

  /* original buffer -- initial content, never modified */
  char *original;
  lk_u32 original_len;

  /* add buffer -- appended to on each insert */
  char *add;
  lk_u32 add_len, add_cap;

  /* piece list -- sequence describing document content */
  doc_piece *pieces;
  lk_u32 pieces_len, pieces_cap;

  lk_u32 total_len;

  /* line index -- byte offsets of line starts */
  lk_u32 *line_starts;
  lk_u32 line_count, line_cap;

  lk_revision revision;

  /* open transaction */
  lk_u32 txn_depth;
  lk_u32 txn_origin;
  lk_doc_delta *deltas;
  lk_u32 *ins_offs; /* per delta: offset into add buffer */
  lk_u32 *del_offs; /* per delta: offset into scratch */
  lk_u32 delta_count, delta_cap;
  char *scratch; /* deleted bytes, captured before applying */
  lk_u32 scratch_len, scratch_cap;
  int in_notify;

  /* subscribers */
  doc_sub *subs;
  lk_u32 sub_count, sub_cap;
  lk_u32 next_sub_id;
};

/* ---- Allocation helpers ---- */

/* Allocate new_bytes, copy old_bytes from old, free old. */
static void *doc_realloc(lk_document *d, void *old, lk_u32 old_bytes,
                         lk_u32 new_bytes) {
  void *nb = d->alloc(d->ud, new_bytes);

  if (!nb) {
    return NULL;
  }

  if (old) {
    memcpy(nb, old, old_bytes);
    d->dealloc(d->ud, old);
  }

  return nb;
}

static lk_u32 grow_cap(lk_u32 cap, lk_u32 needed, lk_u32 initial) {
  lk_u32 nc = cap ? cap : initial;

  while (nc < needed) {
    if (nc > 0x7FFFFFFFu) {
      return needed;
    }

    nc *= 2;
  }

  return nc;
}

static int ensure_add_capacity(lk_document *d, lk_u32 additional) {
  lk_u32 needed;
  lk_u32 new_cap;
  char *nb;

  if (additional > 0xFFFFFFFFu - d->add_len) {
    return 0;
  }

  needed = d->add_len + additional;

  if (needed <= d->add_cap) {
    return 1;
  }

  new_cap = grow_cap(d->add_cap, needed, DOC_INITIAL_ADD_CAP);
  nb = (char *)doc_realloc(d, d->add, d->add_len, new_cap);

  if (!nb) {
    return 0;
  }

  d->add = nb;
  d->add_cap = new_cap;

  return 1;
}

static int ensure_pieces_capacity(lk_document *d, lk_u32 additional) {
  lk_u32 needed = d->pieces_len + additional;
  lk_u32 new_cap;
  doc_piece *np;

  if (needed <= d->pieces_cap) {
    return 1;
  }

  new_cap = grow_cap(d->pieces_cap, needed, DOC_INITIAL_PIECES_CAP);
  np = (doc_piece *)doc_realloc(d, d->pieces,
                                d->pieces_len * (lk_u32)sizeof(doc_piece),
                                new_cap * (lk_u32)sizeof(doc_piece));

  if (!np) {
    return 0;
  }

  d->pieces = np;
  d->pieces_cap = new_cap;

  return 1;
}

static int ensure_lines_capacity(lk_document *d, lk_u32 needed) {
  lk_u32 new_cap;
  lk_u32 *nl;

  if (needed <= d->line_cap) {
    return 1;
  }

  new_cap = grow_cap(d->line_cap, needed, DOC_INITIAL_LINES_CAP);
  nl = (lk_u32 *)doc_realloc(d, d->line_starts,
                             d->line_count * (lk_u32)sizeof(lk_u32),
                             new_cap * (lk_u32)sizeof(lk_u32));

  if (!nl) {
    return 0;
  }

  d->line_starts = nl;
  d->line_cap = new_cap;

  return 1;
}

static int ensure_scratch_capacity(lk_document *d, lk_u32 additional) {
  lk_u32 needed;
  lk_u32 new_cap;
  char *nb;

  if (additional > 0xFFFFFFFFu - d->scratch_len) {
    return 0;
  }

  needed = d->scratch_len + additional;

  if (needed <= d->scratch_cap) {
    return 1;
  }

  new_cap = grow_cap(d->scratch_cap, needed, DOC_INITIAL_SCRATCH_CAP);
  nb = (char *)doc_realloc(d, d->scratch, d->scratch_len, new_cap);

  if (!nb) {
    return 0;
  }

  d->scratch = nb;
  d->scratch_cap = new_cap;

  return 1;
}

/* The three delta-side arrays share one capacity and grow together. */
static int ensure_delta_capacity(lk_document *d) {
  lk_u32 needed = d->delta_count + 1;
  lk_u32 new_cap;
  lk_doc_delta *nd;
  lk_u32 *ni;
  lk_u32 *nx;

  if (needed <= d->delta_cap) {
    return 1;
  }

  new_cap = grow_cap(d->delta_cap, needed, DOC_INITIAL_DELTA_CAP);
  nd = (lk_doc_delta *)d->alloc(d->ud,
                                new_cap * (lk_u32)sizeof(lk_doc_delta));
  ni = (lk_u32 *)d->alloc(d->ud, new_cap * (lk_u32)sizeof(lk_u32));
  nx = (lk_u32 *)d->alloc(d->ud, new_cap * (lk_u32)sizeof(lk_u32));

  if (!nd || !ni || !nx) {
    if (nd) {
      d->dealloc(d->ud, nd);
    }

    if (ni) {
      d->dealloc(d->ud, ni);
    }

    if (nx) {
      d->dealloc(d->ud, nx);
    }

    return 0;
  }

  if (d->delta_count > 0) {
    memcpy(nd, d->deltas, d->delta_count * sizeof(lk_doc_delta));
    memcpy(ni, d->ins_offs, d->delta_count * sizeof(lk_u32));
    memcpy(nx, d->del_offs, d->delta_count * sizeof(lk_u32));
  }

  if (d->deltas) {
    d->dealloc(d->ud, d->deltas);
    d->dealloc(d->ud, d->ins_offs);
    d->dealloc(d->ud, d->del_offs);
  }

  d->deltas = nd;
  d->ins_offs = ni;
  d->del_offs = nx;
  d->delta_cap = new_cap;

  return 1;
}

static int ensure_subs_capacity(lk_document *d) {
  lk_u32 needed = d->sub_count + 1;
  lk_u32 new_cap;
  doc_sub *ns;

  if (needed <= d->sub_cap) {
    return 1;
  }

  new_cap = grow_cap(d->sub_cap, needed, DOC_INITIAL_SUBS_CAP);
  ns = (doc_sub *)doc_realloc(d, d->subs,
                              d->sub_count * (lk_u32)sizeof(doc_sub),
                              new_cap * (lk_u32)sizeof(doc_sub));

  if (!ns) {
    return 0;
  }

  d->subs = ns;
  d->sub_cap = new_cap;

  return 1;
}

/* ---- Revision ---- */

int lk_revision_equal(lk_revision a, lk_revision b) {
  return a.hi == b.hi && a.lo == b.lo;
}

int lk_revision_before(lk_revision a, lk_revision b) {
  if (a.hi != b.hi) {
    return a.hi < b.hi;
  }

  return a.lo < b.lo;
}

static void rev_advance(lk_document *d) {
  d->revision.lo++;

  if (d->revision.lo == 0) {
    d->revision.hi++;
  }
}

/* ---- Piece helpers ---- */

static const char *get_source_buffer(const lk_document *d, lk_u8 source) {
  return (source == PIECE_ORIGINAL) ? d->original : d->add;
}

/* Find the piece containing a document position.  A position on a
 * boundary lands at the end of the earlier piece; a position at the
 * very end (past all pieces) yields piece_idx == pieces_len. */
static int find_piece_at_pos(const lk_document *d, lk_u32 pos,
                             lk_u32 *piece_idx, lk_u32 *offset_in_piece) {
  lk_u32 offset = 0;
  lk_u32 p;

  if (pos > d->total_len) {
    return 0;
  }

  for (p = 0; p < d->pieces_len; p++) {
    if (pos <= offset + d->pieces[p].length) {
      *piece_idx = p;
      *offset_in_piece = pos - offset;
      return 1;
    }

    offset += d->pieces[p].length;
  }

  *piece_idx = d->pieces_len;
  *offset_in_piece = 0;

  return 1;
}

/* ---- Line index ---- */

/* Full rebuild by scanning all pieces for newlines. */
static void rebuild_line_index(lk_document *d) {
  lk_u32 offset = 0;
  lk_u32 p;

  d->line_count = 1;
  d->line_starts[0] = 0;

  for (p = 0; p < d->pieces_len; p++) {
    const doc_piece *piece = &d->pieces[p];
    const char *buf = get_source_buffer(d, piece->source);
    lk_u32 i;

    for (i = 0; i < piece->length; i++) {
      if (buf[piece->start + i] == '\n') {
        if (!ensure_lines_capacity(d, d->line_count + 1)) {
          return;
        }

        d->line_starts[d->line_count] = offset + i + 1;
        d->line_count++;
      }
    }

    offset += piece->length;
  }
}

/* First line_starts entry strictly greater than pos, searching
 * [from, line_count). */
static lk_u32 line_upper_bound(const lk_document *d, lk_u32 from,
                               lk_u32 pos) {
  lk_u32 lo = from;
  lk_u32 hi = d->line_count;

  while (lo < hi) {
    lk_u32 mid = lo + (hi - lo) / 2;

    if (d->line_starts[mid] <= pos) {
      lo = mid + 1;
    } else {
      hi = mid;
    }
  }

  return lo;
}

/* Incremental line index update after inserting text at pos. */
static void update_line_index_insert(lk_document *d, lk_u32 pos,
                                     const char *text, lk_u32 len) {
  lk_u32 new_line_count = 0;
  lk_u32 shift_from;
  lk_u32 new_total;
  lk_u32 entries_to_shift;
  lk_u32 insert_idx;
  lk_u32 i;

  for (i = 0; i < len; i++) {
    if (text[i] == '\n') {
      new_line_count++;
    }
  }

  shift_from = line_upper_bound(d, 0, pos);

  if (new_line_count == 0) {
    for (i = shift_from; i < d->line_count; i++) {
      d->line_starts[i] += len;
    }

    return;
  }

  new_total = d->line_count + new_line_count;

  if (!ensure_lines_capacity(d, new_total)) {
    rebuild_line_index(d);
    return;
  }

  entries_to_shift = d->line_count - shift_from;

  if (entries_to_shift > 0) {
    memmove(&d->line_starts[shift_from + new_line_count],
            &d->line_starts[shift_from],
            entries_to_shift * sizeof(lk_u32));
  }

  for (i = shift_from + new_line_count; i < new_total; i++) {
    d->line_starts[i] += len;
  }

  insert_idx = shift_from;

  for (i = 0; i < len; i++) {
    if (text[i] == '\n') {
      d->line_starts[insert_idx++] = pos + i + 1;
    }
  }

  d->line_count = new_total;
}

/* Incremental line index update after deleting len bytes at pos. */
static void update_line_index_delete(lk_document *d, lk_u32 pos, lk_u32 len) {
  lk_u32 delete_end = pos + len;
  lk_u32 first_removed = line_upper_bound(d, 0, pos);
  lk_u32 after_removed = line_upper_bound(d, first_removed, delete_end);
  lk_u32 removed_count = after_removed - first_removed;
  lk_u32 i;

  if (removed_count > 0) {
    lk_u32 remaining_after = d->line_count - after_removed;

    if (remaining_after > 0) {
      memmove(&d->line_starts[first_removed],
              &d->line_starts[after_removed],
              remaining_after * sizeof(lk_u32));
    }

    d->line_count -= removed_count;
  }

  for (i = first_removed; i < d->line_count; i++) {
    d->line_starts[i] -= len;
  }
}

/* ---- Lifecycle ---- */

lk_document *lk_doc_new(void *(*alloc)(void *, lk_u32),
                        void (*dealloc)(void *, void *), void *ud) {
  lk_document *d;

  if (!alloc || !dealloc) {
    alloc = lk_sys_alloc;
    dealloc = lk_sys_dealloc;
    ud = NULL;
  }

  d = (lk_document *)alloc(ud, (lk_u32)sizeof(lk_document));

  if (!d) {
    return NULL;
  }

  memset(d, 0, sizeof(*d));
  d->alloc = alloc;
  d->dealloc = dealloc;
  d->ud = ud;

  d->add = (char *)alloc(ud, DOC_INITIAL_ADD_CAP);
  d->pieces = (doc_piece *)alloc(
      ud, DOC_INITIAL_PIECES_CAP * (lk_u32)sizeof(doc_piece));
  d->line_starts =
      (lk_u32 *)alloc(ud, DOC_INITIAL_LINES_CAP * (lk_u32)sizeof(lk_u32));

  if (!d->add || !d->pieces || !d->line_starts) {
    lk_doc_destroy(d);
    return NULL;
  }

  d->add_cap = DOC_INITIAL_ADD_CAP;
  d->pieces_cap = DOC_INITIAL_PIECES_CAP;
  d->line_cap = DOC_INITIAL_LINES_CAP;
  d->line_starts[0] = 0;
  d->line_count = 1;
  d->revision.hi = 0;
  d->revision.lo = 1;
  d->next_sub_id = 1;

  return d;
}

lk_document *lk_doc_from_str(void *(*alloc)(void *, lk_u32),
                             void (*dealloc)(void *, void *), void *ud,
                             const char *text, lk_u32 len) {
  lk_document *d = lk_doc_new(alloc, dealloc, ud);

  if (!d) {
    return NULL;
  }

  if (len == 0 || text == NULL) {
    return d;
  }

  d->original = (char *)d->alloc(d->ud, len);

  if (!d->original) {
    lk_doc_destroy(d);
    return NULL;
  }

  memcpy(d->original, text, len);
  d->original_len = len;

  d->pieces[0].source = PIECE_ORIGINAL;
  d->pieces[0].start = 0;
  d->pieces[0].length = len;
  d->pieces_len = 1;
  d->total_len = len;

  rebuild_line_index(d);

  return d;
}

void lk_doc_destroy(lk_document *d) {
  void (*dealloc)(void *, void *);
  void *ud;

  if (!d) {
    return;
  }

  dealloc = d->dealloc;
  ud = d->ud;

  if (d->original) {
    dealloc(ud, d->original);
  }

  if (d->add) {
    dealloc(ud, d->add);
  }

  if (d->pieces) {
    dealloc(ud, d->pieces);
  }

  if (d->line_starts) {
    dealloc(ud, d->line_starts);
  }

  if (d->deltas) {
    dealloc(ud, d->deltas);
    dealloc(ud, d->ins_offs);
    dealloc(ud, d->del_offs);
  }

  if (d->scratch) {
    dealloc(ud, d->scratch);
  }

  if (d->subs) {
    dealloc(ud, d->subs);
  }

  dealloc(ud, d);
}

/* ---- Read ---- */

lk_u32 lk_doc_len(const lk_document *d) {
  if (!d) {
    return 0;
  }

  return d->total_len;
}

lk_u32 lk_doc_line_count(const lk_document *d) {
  if (!d) {
    return 0;
  }

  return d->line_count;
}

lk_u32 lk_doc_get_text(const lk_document *d, lk_u32 pos, char *buf,
                       lk_u32 buf_len) {
  lk_u32 piece_idx;
  lk_u32 offset_in_piece;
  lk_u32 bytes_written = 0;
  lk_u32 remaining = buf_len;
  lk_u32 p;

  if (!d || !buf || buf_len == 0) {
    return 0;
  }

  if (pos >= d->total_len) {
    return 0;
  }

  if (!find_piece_at_pos(d, pos, &piece_idx, &offset_in_piece)) {
    return 0;
  }

  for (p = piece_idx; p < d->pieces_len && remaining > 0; p++) {
    const doc_piece *piece = &d->pieces[p];
    const char *src = get_source_buffer(d, piece->source);
    lk_u32 piece_start = (p == piece_idx) ? offset_in_piece : 0;
    lk_u32 piece_remaining = piece->length - piece_start;
    lk_u32 to_copy = (piece_remaining < remaining) ? piece_remaining
                                                   : remaining;

    memcpy(buf + bytes_written, src + piece->start + piece_start, to_copy);
    bytes_written += to_copy;
    remaining -= to_copy;
  }

  return bytes_written;
}

unsigned char lk_doc_get_byte(const lk_document *d, lk_u32 pos) {
  lk_u32 piece_idx;
  lk_u32 offset_in_piece;
  const doc_piece *piece;
  const char *src;

  if (!d || pos >= d->total_len) {
    return 0;
  }

  if (!find_piece_at_pos(d, pos, &piece_idx, &offset_in_piece)) {
    return 0;
  }

  piece = &d->pieces[piece_idx];
  src = get_source_buffer(d, piece->source);

  return (unsigned char)src[piece->start + offset_in_piece];
}

lk_u32 lk_doc_line_start(const lk_document *d, lk_u32 line) {
  if (!d || line >= d->line_count) {
    return 0;
  }

  return d->line_starts[line];
}

lk_u32 lk_doc_line_end(const lk_document *d, lk_u32 line) {
  if (!d || line >= d->line_count) {
    return 0;
  }

  if (line + 1 < d->line_count) {
    /* the line ends at its \n, one before the next line's start */
    return d->line_starts[line + 1] - 1;
  }

  return d->total_len;
}

lk_u32 lk_doc_line_length(const lk_document *d, lk_u32 line) {
  lk_u32 start;
  lk_u32 end;

  if (!d || line >= d->line_count) {
    return 0;
  }

  start = lk_doc_line_start(d, line);
  end = lk_doc_line_end(d, line);

  if (line + 1 < d->line_count) {
    return end - start + 1; /* include the \n */
  }

  return end - start;
}

lk_u32 lk_doc_pos_to_line(const lk_document *d, lk_u32 pos) {
  lk_u32 lo;

  if (!d || d->line_count == 0) {
    return 0;
  }

  if (pos >= d->total_len) {
    return d->line_count - 1;
  }

  lo = line_upper_bound(d, 0, pos);

  return (lo > 0) ? lo - 1 : 0;
}

lk_revision lk_doc_revision(const lk_document *d) {
  lk_revision r;

  r.hi = 0;
  r.lo = 0;

  if (!d) {
    return r;
  }

  return d->revision;
}

/* ---- Search ---- */

/* Window size for lk_doc_find's chunked reconstruction (bytes). */
#define DOC_FIND_WINDOW 1024

/* Literal forward byte search, piece-table aware.
 *
 * Approach: chunked reconstruction into a sliding window.  The
 * document is a piece list over two buffers, so a match may span any
 * number of piece boundaries; rather than matching across pieces
 * in-place, each round copies a contiguous window of the document
 * (via lk_doc_get_text, which already walks pieces) into a stack
 * buffer and scans it with memcmp.  The window then slides forward
 * keeping needle_len - 1 bytes of overlap, so a match straddling a
 * window edge is fully contained in the next fill.  Needles larger
 * than the stack window get a heap window (document allocator) of
 * needle_len + DOC_FIND_WINDOW, preserving the invariant that every
 * fill can hold a whole match and still make forward progress. */
int lk_doc_find(const lk_document *d, const char *needle, lk_u32 needle_len,
                lk_u32 from, lk_u32 *out_pos) {
  char stack_win[DOC_FIND_WINDOW];
  char *win = stack_win;
  lk_u32 win_cap = DOC_FIND_WINDOW;
  lk_u32 base;
  int found = 0;

  if (!d || !needle || needle_len == 0 || !out_pos) {
    return 0;
  }

  if (from > d->total_len || needle_len > d->total_len - from) {
    return 0; /* covers the empty document and over-long needles */
  }

  if (needle_len > win_cap) {
    if (needle_len > 0xFFFFFFFFu - DOC_FIND_WINDOW) {
      win_cap = needle_len;
    } else {
      win_cap = needle_len + DOC_FIND_WINDOW;
    }

    win = (char *)d->alloc(d->ud, win_cap);

    if (!win) {
      return 0;
    }
  }

  base = from;

  while (!found && base <= d->total_len - needle_len) {
    lk_u32 want = d->total_len - base;
    lk_u32 got;
    lk_u32 last;
    lk_u32 i;

    if (want > win_cap) {
      want = win_cap;
    }

    got = lk_doc_get_text(d, base, win, want);

    if (got < needle_len) {
      break; /* defensive: cannot happen for an in-range base */
    }

    last = got - needle_len;

    for (i = 0; i <= last; i++) {
      if (win[i] == needle[0] &&
          memcmp(win + i, needle, needle_len) == 0) {
        *out_pos = base + i;
        found = 1;
        break;
      }
    }

    /* slide, keeping needle_len - 1 bytes of overlap; got >=
     * needle_len guarantees forward progress */
    base += got - (needle_len - 1);
  }

  if (win != stack_win) {
    d->dealloc(d->ud, win);
  }

  return found;
}

/* ---- Transactions ---- */

/* Resolve delta byte pointers and notify every subscriber once. */
static void doc_notify(lk_document *d) {
  lk_u32 i;

  if (d->delta_count == 0) {
    return;
  }

  for (i = 0; i < d->delta_count; i++) {
    d->deltas[i].inserted =
        d->deltas[i].inserted_len ? d->add + d->ins_offs[i] : NULL;
    d->deltas[i].deleted =
        d->deltas[i].deleted_len ? d->scratch + d->del_offs[i] : NULL;
  }

  d->in_notify = 1;

  for (i = 0; i < d->sub_count; i++) {
    d->subs[i].fn(d->subs[i].ud, d, d->deltas, d->delta_count);
  }

  d->in_notify = 0;
  d->delta_count = 0;
  d->scratch_len = 0;
}

void lk_doc_begin(lk_document *d, lk_u32 origin) {
  if (!d) {
    return;
  }

  if (d->in_notify) {
    LK_DOC_ASSERT(!"lk_doc_begin during notification");
    return;
  }

  if (d->txn_depth == 0) {
    d->txn_origin = origin;
    d->delta_count = 0;
    d->scratch_len = 0;
  } else {
    LK_DOC_ASSERT(!"nested lk_doc_begin");
  }

  d->txn_depth++;
}

void lk_doc_commit(lk_document *d) {
  if (!d) {
    return;
  }

  if (d->in_notify) {
    LK_DOC_ASSERT(!"lk_doc_commit during notification");
    return;
  }

  if (d->txn_depth == 0) {
    LK_DOC_ASSERT(!"lk_doc_commit without begin");
    return;
  }

  d->txn_depth--;

  if (d->txn_depth == 0) {
    doc_notify(d);
  }
}

/* Record one effective op.  Delta capacity is guaranteed by the
 * caller before the op applies, so recording cannot fail. */
static void record_delta(lk_document *d, lk_u32 start, lk_u32 deleted_len,
                         lk_u32 inserted_len, lk_u32 ins_off, lk_u32 del_off,
                         lk_revision before) {
  lk_doc_delta *dd = &d->deltas[d->delta_count];

  dd->start = start;
  dd->deleted_len = deleted_len;
  dd->inserted_len = inserted_len;
  dd->deleted = NULL;
  dd->inserted = NULL;
  dd->before = before;
  dd->after = d->revision;
  dd->origin = (d->txn_depth > 0) ? d->txn_origin : LK_ORIGIN_NONE;

  d->ins_offs[d->delta_count] = ins_off;
  d->del_offs[d->delta_count] = del_off;
  d->delta_count++;
}

/* ---- Mutation ---- */

int lk_doc_insert(lk_document *d, lk_u32 pos, const char *text, lk_u32 len) {
  lk_u32 add_start;
  lk_u32 piece_idx;
  lk_u32 offset_in_piece;
  doc_piece new_piece;
  lk_revision before;
  int implicit;

  if (!d || !text || len == 0) {
    return 0;
  }

  if (d->in_notify) {
    LK_DOC_ASSERT(!"lk_doc_insert during notification");
    return 0;
  }

  if (pos > d->total_len) {
    return 0;
  }

  if (len > 0xFFFFFFFFu - d->total_len) {
    return 0;
  }

  if (!ensure_delta_capacity(d)) {
    return 0;
  }

  if (!ensure_add_capacity(d, len)) {
    return 0;
  }

  add_start = d->add_len;
  memcpy(d->add + add_start, text, len);
  d->add_len += len;

  new_piece.source = PIECE_ADD;
  new_piece.start = add_start;
  new_piece.length = len;

  if (!find_piece_at_pos(d, pos, &piece_idx, &offset_in_piece)) {
    d->add_len = add_start;
    return 0;
  }

  if (piece_idx >= d->pieces_len) {
    /* insert at the end, past all pieces */
    if (!ensure_pieces_capacity(d, 1)) {
      d->add_len = add_start;
      return 0;
    }

    d->pieces[d->pieces_len] = new_piece;
    d->pieces_len++;
  } else if (offset_in_piece == 0) {
    /* insert at the start of a piece -- new piece goes before it */
    if (!ensure_pieces_capacity(d, 1)) {
      d->add_len = add_start;
      return 0;
    }

    memmove(&d->pieces[piece_idx + 1], &d->pieces[piece_idx],
            (d->pieces_len - piece_idx) * sizeof(doc_piece));
    d->pieces[piece_idx] = new_piece;
    d->pieces_len++;
  } else if (offset_in_piece == d->pieces[piece_idx].length) {
    /* insert at the end of a piece -- new piece goes after it */
    if (!ensure_pieces_capacity(d, 1)) {
      d->add_len = add_start;
      return 0;
    }

    memmove(&d->pieces[piece_idx + 2], &d->pieces[piece_idx + 1],
            (d->pieces_len - piece_idx - 1) * sizeof(doc_piece));
    d->pieces[piece_idx + 1] = new_piece;
    d->pieces_len++;
  } else {
    /* split the piece: before, new, after replace one */
    doc_piece original;
    doc_piece piece_before;
    doc_piece piece_after;

    if (!ensure_pieces_capacity(d, 2)) {
      d->add_len = add_start;
      return 0;
    }

    original = d->pieces[piece_idx];

    piece_before.source = original.source;
    piece_before.start = original.start;
    piece_before.length = offset_in_piece;

    piece_after.source = original.source;
    piece_after.start = original.start + offset_in_piece;
    piece_after.length = original.length - offset_in_piece;

    memmove(&d->pieces[piece_idx + 3], &d->pieces[piece_idx + 1],
            (d->pieces_len - piece_idx - 1) * sizeof(doc_piece));

    d->pieces[piece_idx] = piece_before;
    d->pieces[piece_idx + 1] = new_piece;
    d->pieces[piece_idx + 2] = piece_after;
    d->pieces_len += 2;
  }

  before = d->revision;
  d->total_len += len;
  rev_advance(d);
  update_line_index_insert(d, pos, text, len);

  implicit = (d->txn_depth == 0);
  record_delta(d, pos, 0, len, add_start, 0, before);

  if (implicit) {
    doc_notify(d);
  }

  return 1;
}

int lk_doc_delete(lk_document *d, lk_u32 pos, lk_u32 len) {
  lk_u32 delete_end;
  lk_u32 start_piece_idx;
  lk_u32 start_offset;
  lk_u32 end_piece_idx;
  lk_u32 end_offset;
  lk_u32 del_off;
  lk_revision before;
  int implicit;

  if (!d || len == 0) {
    return 0;
  }

  if (d->in_notify) {
    LK_DOC_ASSERT(!"lk_doc_delete during notification");
    return 0;
  }

  if (pos >= d->total_len) {
    return 0;
  }

  if (len > 0xFFFFFFFFu - pos) {
    return 0;
  }

  if (pos + len > d->total_len) {
    len = d->total_len - pos;
  }

  delete_end = pos + len;

  if (!find_piece_at_pos(d, pos, &start_piece_idx, &start_offset)) {
    return 0;
  }

  if (!find_piece_at_pos(d, delete_end, &end_piece_idx, &end_offset)) {
    return 0;
  }

  if (!ensure_delta_capacity(d)) {
    return 0;
  }

  /* capture the doomed bytes before the piece surgery */
  if (!ensure_scratch_capacity(d, len)) {
    return 0;
  }

  del_off = d->scratch_len;
  lk_doc_get_text(d, pos, d->scratch + del_off, len);

  if (start_piece_idx == end_piece_idx) {
    doc_piece *piece = &d->pieces[start_piece_idx];

    if (start_offset == 0 && end_offset == piece->length) {
      /* delete the entire piece */
      memmove(&d->pieces[start_piece_idx], &d->pieces[start_piece_idx + 1],
              (d->pieces_len - start_piece_idx - 1) * sizeof(doc_piece));
      d->pieces_len--;
    } else if (start_offset == 0) {
      piece->start += len;
      piece->length -= len;
    } else if (end_offset == piece->length) {
      piece->length = start_offset;
    } else {
      /* delete from the middle -- split into two pieces */
      doc_piece piece_before;
      doc_piece piece_after;

      if (!ensure_pieces_capacity(d, 1)) {
        return 0;
      }

      piece = &d->pieces[start_piece_idx]; /* may have moved */

      piece_before.source = piece->source;
      piece_before.start = piece->start;
      piece_before.length = start_offset;

      piece_after.source = piece->source;
      piece_after.start = piece->start + end_offset;
      piece_after.length = piece->length - end_offset;

      memmove(&d->pieces[start_piece_idx + 2],
              &d->pieces[start_piece_idx + 1],
              (d->pieces_len - start_piece_idx - 1) * sizeof(doc_piece));

      d->pieces[start_piece_idx] = piece_before;
      d->pieces[start_piece_idx + 1] = piece_after;
      d->pieces_len++;
    }
  } else {
    /* deletion spans multiple pieces */
    lk_u32 pieces_to_remove;
    lk_u32 write_idx;
    lk_u32 read_idx;

    d->pieces[start_piece_idx].length = start_offset;

    if (end_piece_idx < d->pieces_len) {
      doc_piece *last = &d->pieces[end_piece_idx];

      last->start += end_offset;
      last->length -= end_offset;
    }

    pieces_to_remove = end_piece_idx - start_piece_idx - 1;

    if (pieces_to_remove > 0) {
      memmove(&d->pieces[start_piece_idx + 1], &d->pieces[end_piece_idx],
              (d->pieces_len - end_piece_idx) * sizeof(doc_piece));
      d->pieces_len -= pieces_to_remove;
    }

    /* compact away empty pieces */
    write_idx = 0;

    for (read_idx = 0; read_idx < d->pieces_len; read_idx++) {
      if (d->pieces[read_idx].length > 0) {
        if (write_idx != read_idx) {
          d->pieces[write_idx] = d->pieces[read_idx];
        }

        write_idx++;
      }
    }

    d->pieces_len = write_idx;
  }

  before = d->revision;
  d->total_len -= len;
  d->scratch_len += len;
  rev_advance(d);
  update_line_index_delete(d, pos, len);

  implicit = (d->txn_depth == 0);
  record_delta(d, pos, len, 0, 0, del_off, before);

  if (implicit) {
    doc_notify(d);
  }

  return 1;
}

/* ---- Subscriptions ---- */

lk_u32 lk_doc_subscribe(lk_document *d, lk_doc_listener_fn fn, void *ud) {
  lk_u32 id;

  if (!d || !fn) {
    return 0;
  }

  if (!ensure_subs_capacity(d)) {
    return 0;
  }

  id = d->next_sub_id++;
  d->subs[d->sub_count].id = id;
  d->subs[d->sub_count].fn = fn;
  d->subs[d->sub_count].ud = ud;
  d->sub_count++;

  return id;
}

void lk_doc_unsubscribe(lk_document *d, lk_u32 subscription) {
  lk_u32 i;

  if (!d || subscription == 0) {
    return;
  }

  for (i = 0; i < d->sub_count; i++) {
    if (d->subs[i].id == subscription) {
      memmove(&d->subs[i], &d->subs[i + 1],
              (d->sub_count - i - 1) * sizeof(doc_sub));
      d->sub_count--;
      return;
    }
  }
}
