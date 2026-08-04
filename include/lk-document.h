#ifndef LK_DOCUMENT_H
#define LK_DOCUMENT_H

/*
 * lk-document.h -- application-owned text document (editor track,
 * stage A).
 *
 * Piece-table document (lifted from weft): immutable original buffer,
 * append-only add buffer, piece array, line index, cached total
 * length.  Mutation is transactional and observable: every committed
 * transaction notifies subscribers once with the full delta array,
 * and each delta carries the actual bytes involved (valid only for
 * the duration of the notification).
 *
 * lk_edit_history is an ordinary subscriber that records committed
 * transactions and replays their inverses as LK_ORIGIN_UNDO /
 * LK_ORIGIN_REDO transactions through the same protocol.
 *
 * These objects live in the application environment, not the lk
 * tree; nothing here touches lk_state or the intern pool.
 */

#include "lk.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 ** Revision token
 **/

/* An identity token, not arithmetic: incremented internally as a
 * pair (lo carries into hi on wrap).  Compare, never compute. */
typedef struct lk_revision {
  lk_u32 hi, lo;
} lk_revision;

int lk_revision_equal(lk_revision a, lk_revision b);
int lk_revision_before(lk_revision a, lk_revision b);

/**
 ** Transaction origins
 **/

#define LK_ORIGIN_NONE 0
#define LK_ORIGIN_UNDO 1
#define LK_ORIGIN_REDO 2
/* values >= 16 are reserved for applications and editor command ids */

/**
 ** Document
 **/

typedef struct lk_document lk_document;

/* One effective mutation inside a committed transaction.  The byte
 * pointers are valid ONLY during the notification: `inserted` points
 * into the add buffer, `deleted` into transaction-scoped scratch
 * captured before the delete applied.  Listeners that need the bytes
 * longer must copy.
 *
 * Coordinate contract (pinned, docs/editor-wrap.md section 7): delta
 * coordinates are SEQUENTIAL -- each delta's positions are expressed
 * in the document state produced by all preceding deltas of the same
 * transaction, in array order.  Replaying the deltas one by one
 * against a copy of the pre-transaction bytes reproduces the
 * post-transaction document exactly; listeners that maintain
 * position-derived structures (history, annotations, wrap caches)
 * must transform per delta, in order. */
typedef struct lk_doc_delta {
  lk_u32 start;
  lk_u32 deleted_len;
  lk_u32 inserted_len;
  const char *deleted;
  const char *inserted;
  lk_revision before, after;
  lk_u32 origin; /* committer-supplied (LK_ORIGIN_*) */
} lk_doc_delta;

typedef void (*lk_doc_listener_fn)(void *ud, const lk_document *d,
                                   const lk_doc_delta *deltas, lk_u32 n);

/* Lifecycle.  NULL alloc/dealloc fall back to the system allocator. */
lk_document *lk_doc_new(void *(*alloc)(void *, lk_u32),
                        void (*dealloc)(void *, void *), void *ud);
lk_document *lk_doc_from_str(void *(*alloc)(void *, lk_u32),
                             void (*dealloc)(void *, void *), void *ud,
                             const char *text, lk_u32 len);
void lk_doc_destroy(lk_document *d);

/* Read.  A document always has >= 1 line (the empty document is one
 * empty line).  line_end is the offset of the line's \n (exclusive),
 * or doc len for the last line; line_length includes the \n when
 * present.  get_text writes no terminator and returns bytes written;
 * pos >= len reads 0 bytes.  get_byte returns 0 out of bounds. */
lk_u32 lk_doc_len(const lk_document *d);
lk_u32 lk_doc_line_count(const lk_document *d);
lk_u32 lk_doc_get_text(const lk_document *d, lk_u32 pos, char *buf,
                       lk_u32 buf_len);
unsigned char lk_doc_get_byte(const lk_document *d, lk_u32 pos);
lk_u32 lk_doc_line_start(const lk_document *d, lk_u32 line);
lk_u32 lk_doc_line_end(const lk_document *d, lk_u32 line);
lk_u32 lk_doc_line_length(const lk_document *d, lk_u32 line);
lk_u32 lk_doc_pos_to_line(const lk_document *d, lk_u32 pos);
lk_revision lk_doc_revision(const lk_document *d);

/* Literal forward byte search.  Finds the first occurrence of needle
 * at a position >= from and stores it in *out_pos.  Returns 1 on a
 * match, 0 when not found or on bad arguments (NULL/empty needle,
 * from > document length).  Bytes are compared verbatim -- a UTF-8
 * needle matches its exact byte sequence -- and matches may span
 * piece boundaries.  Search-next is lk_doc_find(d, n, nl, hit + 1,
 * &hit); backward search waits for a concrete consumer. */
int lk_doc_find(const lk_document *d, const char *needle, lk_u32 needle_len,
                lk_u32 from, lk_u32 *out_pos);

/* Mutation.  insert/delete outside a begin/commit bracket form an
 * implicit single-op transaction (subscribers notified immediately).
 * Nested begin is a programming error (debug-asserted; the outer
 * bracket wins in release).  Return 1 on success, 0 on rejection:
 * zero length, insert pos > len, delete pos >= len, pos + len
 * overflowing lk_u32, or mutation from inside a notification.
 * Failed mutations do not advance the revision. */
void lk_doc_begin(lk_document *d, lk_u32 origin);
int lk_doc_insert(lk_document *d, lk_u32 pos, const char *text, lk_u32 len);
int lk_doc_delete(lk_document *d, lk_u32 pos, lk_u32 len);
void lk_doc_commit(lk_document *d);

/* Subscriptions.  Listeners are notified once per committed
 * transaction, in subscribe order, and must not mutate the document
 * during notification.  subscribe returns an id (0 on failure). */
lk_u32 lk_doc_subscribe(lk_document *d, lk_doc_listener_fn fn, void *ud);
void lk_doc_unsubscribe(lk_document *d, lk_u32 subscription);

/**
 ** Edit history
 **/

typedef struct lk_edit_history lk_edit_history;

lk_edit_history *lk_history_new(void *(*alloc)(void *, lk_u32),
                                void (*dealloc)(void *, void *), void *ud);
void lk_history_destroy(lk_edit_history *h);

/* Attach subscribes to the document: every committed transaction is
 * recorded (bytes copied) except the history's own replays.  One
 * history per document is the v1 configuration.  Destroy the history
 * before the document it is attached to. */
void lk_history_attach(lk_edit_history *h, lk_document *d);

/* Replay the inverse (undo) or the recorded forward form (redo) of
 * one transaction as a single LK_ORIGIN_UNDO / LK_ORIGIN_REDO
 * bracket.  Return 1 on success, 0 when the stack is empty. */
int lk_history_undo(lk_edit_history *h, lk_document *d);
int lk_history_redo(lk_edit_history *h, lk_document *d);
int lk_history_can_undo(const lk_edit_history *h);
int lk_history_can_redo(const lk_edit_history *h);

/* Savepoint: remember the current history position as "saved" (e.g.
 * when the buffer is written to disk) and ask whether the document is
 * back at exactly that position.  at_saved returns 1 only while a
 * savepoint is set and the undo position equals it (undoing back to
 * the savepoint or redoing forward onto it both count).  Recording a
 * new transaction while the position is BELOW the savepoint destroys
 * the redo path back to the saved state, so the savepoint is then
 * invalidated forever; recording at or above it keeps it reachable
 * via undo.  A fresh history has no savepoint (at_saved is 0). */
void lk_history_mark_saved(lk_edit_history *h);
int lk_history_at_saved(const lk_edit_history *h);

#ifdef __cplusplus
}
#endif

#endif /* LK_DOCUMENT_H */
