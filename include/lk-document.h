#ifndef LK_DOCUMENT_H
#define LK_DOCUMENT_H

#include "lk.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct lk_revision {
  lk_u32 hi, lo;
} lk_revision;

int lk_revision_equal(lk_revision a, lk_revision b);
int lk_revision_before(lk_revision a, lk_revision b);

#define LK_ORIGIN_NONE 0
#define LK_ORIGIN_UNDO 1
#define LK_ORIGIN_REDO 2

typedef struct lk_document lk_document;

typedef struct lk_doc_delta {
  lk_u32 start;
  lk_u32 deleted_len;
  lk_u32 inserted_len;
  const char *deleted;
  const char *inserted;
  lk_revision before, after;
  lk_u32 origin;
} lk_doc_delta;

typedef void (*lk_doc_listener_fn)(void *ud, const lk_document *d,
                                   const lk_doc_delta *deltas, lk_u32 n);

lk_document *lk_doc_new(void *(*alloc)(void *, lk_u32),
                        void (*dealloc)(void *, void *), void *ud);
lk_document *lk_doc_from_str(void *(*alloc)(void *, lk_u32),
                             void (*dealloc)(void *, void *), void *ud,
                             const char *text, lk_u32 len);
void lk_doc_destroy(lk_document *d);

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

int lk_doc_find(const lk_document *d, const char *needle, lk_u32 needle_len,
                lk_u32 from, lk_u32 *out_pos);

void lk_doc_begin(lk_document *d, lk_u32 origin);
int lk_doc_insert(lk_document *d, lk_u32 pos, const char *text, lk_u32 len);
int lk_doc_delete(lk_document *d, lk_u32 pos, lk_u32 len);
void lk_doc_commit(lk_document *d);

lk_u32 lk_doc_subscribe(lk_document *d, lk_doc_listener_fn fn, void *ud);
void lk_doc_unsubscribe(lk_document *d, lk_u32 subscription);

typedef struct lk_edit_history lk_edit_history;

lk_edit_history *lk_history_new(void *(*alloc)(void *, lk_u32),
                                void (*dealloc)(void *, void *), void *ud);
void lk_history_destroy(lk_edit_history *h);

void lk_history_attach(lk_edit_history *h, lk_document *d);

int lk_history_undo(lk_edit_history *h, lk_document *d);
int lk_history_redo(lk_edit_history *h, lk_document *d);
int lk_history_can_undo(const lk_edit_history *h);
int lk_history_can_redo(const lk_edit_history *h);

void lk_history_mark_saved(lk_edit_history *h);
int lk_history_at_saved(const lk_edit_history *h);

lk_u32 lk_history_peek_undo_serial(const lk_edit_history *h);
lk_u32 lk_history_peek_redo_serial(const lk_edit_history *h);

#ifdef __cplusplus
}
#endif

#endif
