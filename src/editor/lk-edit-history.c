/*
 * lk-edit-history.c -- transactional undo/redo (editor track, stage A).
 *
 * The history is an ordinary document subscriber: attach subscribes,
 * and every committed transaction is recorded as one history entry
 * with its delta bytes copied (the notification-scoped pointers do
 * not outlive the callback).  Replays run as single
 * LK_ORIGIN_UNDO / LK_ORIGIN_REDO brackets, so they reach every
 * other observer through the same protocol as any edit; the history
 * ignores its own replays by origin and clears the redo stack on any
 * other recorded transaction.
 *
 * Stacks are unbounded, one entry per committed transaction (weft's
 * EditOp stack was also unbounded and uncoalesced; keystroke
 * coalescing is a stage-B command-layer bracketing policy, not a
 * history concern).
 */

#include <string.h>

#include "lk-document.h"
#include "core/lk-memory.h"

#define HIST_INITIAL_STACK_CAP 16

typedef struct hist_delta {
  lk_u32 start;
  lk_u32 deleted_len;
  lk_u32 inserted_len;
  char *deleted;  /* owned copy, NULL when deleted_len == 0 */
  char *inserted; /* owned copy, NULL when inserted_len == 0 */
} hist_delta;

/* One committed transaction. */
typedef struct hist_entry {
  hist_delta *deltas;
  lk_u32 count;
} hist_entry;

struct lk_edit_history {
  void *(*alloc)(void *, lk_u32);
  void (*dealloc)(void *, void *);
  void *ud;

  hist_entry *undo_stack;
  lk_u32 undo_count, undo_cap;
  hist_entry *redo_stack;
  lk_u32 redo_count, redo_cap;

  lk_document *doc;
  lk_u32 sub_id;
};

/* ---- Entry helpers ---- */

static char *hist_copy_bytes(lk_edit_history *h, const char *src,
                             lk_u32 len) {
  char *b;

  if (len == 0 || !src) {
    return NULL;
  }

  b = (char *)h->alloc(h->ud, len);

  if (!b) {
    return NULL;
  }

  memcpy(b, src, len);

  return b;
}

static void hist_entry_free(lk_edit_history *h, hist_entry *e) {
  lk_u32 i;

  for (i = 0; i < e->count; i++) {
    if (e->deltas[i].deleted) {
      h->dealloc(h->ud, e->deltas[i].deleted);
    }

    if (e->deltas[i].inserted) {
      h->dealloc(h->ud, e->deltas[i].inserted);
    }
  }

  if (e->deltas) {
    h->dealloc(h->ud, e->deltas);
  }

  e->deltas = NULL;
  e->count = 0;
}

/* Deep-copy a committed transaction's deltas.  Returns 0 (with
 * nothing retained) on allocation failure. */
static int hist_entry_copy(lk_edit_history *h, hist_entry *e,
                           const lk_doc_delta *deltas, lk_u32 n) {
  lk_u32 i;

  e->deltas = (hist_delta *)h->alloc(h->ud, n * (lk_u32)sizeof(hist_delta));
  e->count = 0;

  if (!e->deltas) {
    return 0;
  }

  memset(e->deltas, 0, n * sizeof(hist_delta));

  for (i = 0; i < n; i++) {
    hist_delta *hd = &e->deltas[i];

    hd->start = deltas[i].start;
    hd->deleted_len = deltas[i].deleted_len;
    hd->inserted_len = deltas[i].inserted_len;
    hd->deleted = hist_copy_bytes(h, deltas[i].deleted, hd->deleted_len);
    hd->inserted = hist_copy_bytes(h, deltas[i].inserted, hd->inserted_len);
    e->count = i + 1;

    if ((hd->deleted_len && !hd->deleted) ||
        (hd->inserted_len && !hd->inserted)) {
      hist_entry_free(h, e);
      return 0;
    }
  }

  return 1;
}

static int hist_push(lk_edit_history *h, hist_entry **stack, lk_u32 *count,
                     lk_u32 *cap, const hist_entry *e) {
  if (*count == *cap) {
    lk_u32 new_cap = *cap ? *cap * 2 : HIST_INITIAL_STACK_CAP;
    hist_entry *ns =
        (hist_entry *)h->alloc(h->ud, new_cap * (lk_u32)sizeof(hist_entry));

    if (!ns) {
      return 0;
    }

    if (*stack) {
      memcpy(ns, *stack, *count * sizeof(hist_entry));
      h->dealloc(h->ud, *stack);
    }

    *stack = ns;
    *cap = new_cap;
  }

  (*stack)[*count] = *e;
  (*count)++;

  return 1;
}

static void hist_clear_stack(lk_edit_history *h, hist_entry *stack,
                             lk_u32 *count) {
  lk_u32 i;

  for (i = 0; i < *count; i++) {
    hist_entry_free(h, &stack[i]);
  }

  *count = 0;
}

/* ---- Document listener ---- */

static void hist_on_change(void *ud, const lk_document *d,
                           const lk_doc_delta *deltas, lk_u32 n) {
  lk_edit_history *h = (lk_edit_history *)ud;
  hist_entry entry;

  (void)d;

  if (n == 0) {
    return;
  }

  /* our own replays come back through the same protocol */
  if (deltas[0].origin == LK_ORIGIN_UNDO ||
      deltas[0].origin == LK_ORIGIN_REDO) {
    return;
  }

  if (!hist_entry_copy(h, &entry, deltas, n)) {
    return;
  }

  if (!hist_push(h, &h->undo_stack, &h->undo_count, &h->undo_cap, &entry)) {
    hist_entry_free(h, &entry);
    return;
  }

  hist_clear_stack(h, h->redo_stack, &h->redo_count);
}

/* ---- Public API ---- */

lk_edit_history *lk_history_new(void *(*alloc)(void *, lk_u32),
                                void (*dealloc)(void *, void *), void *ud) {
  lk_edit_history *h;

  if (!alloc || !dealloc) {
    alloc = lk_sys_alloc;
    dealloc = lk_sys_dealloc;
    ud = NULL;
  }

  h = (lk_edit_history *)alloc(ud, (lk_u32)sizeof(lk_edit_history));

  if (!h) {
    return NULL;
  }

  memset(h, 0, sizeof(*h));
  h->alloc = alloc;
  h->dealloc = dealloc;
  h->ud = ud;

  return h;
}

void lk_history_destroy(lk_edit_history *h) {
  if (!h) {
    return;
  }

  if (h->doc && h->sub_id) {
    lk_doc_unsubscribe(h->doc, h->sub_id);
  }

  hist_clear_stack(h, h->undo_stack, &h->undo_count);
  hist_clear_stack(h, h->redo_stack, &h->redo_count);

  if (h->undo_stack) {
    h->dealloc(h->ud, h->undo_stack);
  }

  if (h->redo_stack) {
    h->dealloc(h->ud, h->redo_stack);
  }

  h->dealloc(h->ud, h);
}

void lk_history_attach(lk_edit_history *h, lk_document *d) {
  if (!h || !d) {
    return;
  }

  h->doc = d;
  h->sub_id = lk_doc_subscribe(d, hist_on_change, h);
}

int lk_history_undo(lk_edit_history *h, lk_document *d) {
  hist_entry entry;
  lk_u32 i;

  if (!h || !d || h->undo_count == 0) {
    return 0;
  }

  entry = h->undo_stack[h->undo_count - 1];
  h->undo_count--;

  /* inverse of each delta, in reverse order, as one bracket */
  lk_doc_begin(d, LK_ORIGIN_UNDO);

  i = entry.count;

  while (i > 0) {
    const hist_delta *hd;

    i--;
    hd = &entry.deltas[i];

    if (hd->inserted_len) {
      lk_doc_delete(d, hd->start, hd->inserted_len);
    }

    if (hd->deleted_len) {
      lk_doc_insert(d, hd->start, hd->deleted, hd->deleted_len);
    }
  }

  lk_doc_commit(d);

  if (!hist_push(h, &h->redo_stack, &h->redo_count, &h->redo_cap, &entry)) {
    hist_entry_free(h, &entry);
  }

  return 1;
}

int lk_history_redo(lk_edit_history *h, lk_document *d) {
  hist_entry entry;
  lk_u32 i;

  if (!h || !d || h->redo_count == 0) {
    return 0;
  }

  entry = h->redo_stack[h->redo_count - 1];
  h->redo_count--;

  /* recorded deltas replayed forward, as one bracket */
  lk_doc_begin(d, LK_ORIGIN_REDO);

  for (i = 0; i < entry.count; i++) {
    const hist_delta *hd = &entry.deltas[i];

    if (hd->deleted_len) {
      lk_doc_delete(d, hd->start, hd->deleted_len);
    }

    if (hd->inserted_len) {
      lk_doc_insert(d, hd->start, hd->inserted, hd->inserted_len);
    }
  }

  lk_doc_commit(d);

  if (!hist_push(h, &h->undo_stack, &h->undo_count, &h->undo_cap, &entry)) {
    hist_entry_free(h, &entry);
  }

  return 1;
}

int lk_history_can_undo(const lk_edit_history *h) {
  return h && h->undo_count > 0;
}

int lk_history_can_redo(const lk_edit_history *h) {
  return h && h->redo_count > 0;
}
