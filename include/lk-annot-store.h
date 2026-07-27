#ifndef LK_ANNOT_STORE_H
#define LK_ANNOT_STORE_H

/*
 * lk-annot-store.h -- annotation store (editor track, stage C;
 * docs/editor.md section 10).
 *
 * Lifted from weft's annot_store: anchors with stable ids and an
 * insert bias, annotation records tying two anchors to a layer name
 * and metadata key/value pairs, dirty-tracked layers with a version
 * counter, and position/range/layer queries.
 *
 * The one architectural change from weft: the public edit hooks
 * (annot_store_on_insert/on_delete) are gone.  The store attaches to
 * an lk_document as an ordinary subscriber -- one listener among
 * peers, exactly like lk_edit_history -- and transforms its anchors
 * from the committed deltas (delete transform first, then insert, per
 * delta, in order).  Undo/redo transactions arrive through the same
 * subscription and transform anchors again; that is correct and free,
 * no origin special-casing.
 *
 * The store lives in the application environment, never in lk_state.
 * Layer names and metadata strings are OWNED copies made with the
 * store's allocator -- never interned (the intern pool is immortal).
 */

#include "lk-document.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 ** Anchors
 **/

/* How an anchor behaves when text is inserted exactly at its
 * position: LEFT stays put, RIGHT moves with the insert. */
typedef enum lk_anchor_bias {
  LK_ANCHOR_LEFT = 0,
  LK_ANCHOR_RIGHT
} lk_anchor_bias;

/* A byte position that survives document edits.  Ids are stable and
 * start at 1 (0 = invalid). */
typedef struct lk_anchor {
  lk_u32 pos;
  lk_anchor_bias bias;
  lk_u32 id;
} lk_anchor;

/**
 ** Records, layers, queries
 **/

/* A span [start, end) with a layer name and metadata.  All strings
 * are owned by the store. */
typedef struct lk_annot_record {
  lk_u32 id;
  lk_u32 start_anchor; /* anchor id, LK_ANCHOR_LEFT bias */
  lk_u32 end_anchor;   /* anchor id, LK_ANCHOR_RIGHT bias */
  char *layer;
  char **keys; /* "namespace:key" strings */
  char **values;
  lk_u32 meta_count;
  lk_revision doc_rev; /* store revision when the record was created */
} lk_annot_record;

typedef enum lk_layer_state {
  LK_LAYER_VALID = 0,
  LK_LAYER_DIRTY
} lk_layer_state;

typedef struct lk_annot_layer {
  char *name; /* owned */
  lk_layer_state state;
  lk_u32 version; /* bumped by clear/clear_layer/set_dirty (weft:
                     an ephemeral session counter, lk_u32 here) */
} lk_annot_layer;

/* Query result: annotation ids.  init zeroes the struct; the array
 * grows with the queried store's allocator, captured into the struct
 * so free keeps weft's one-argument shape. */
typedef struct lk_annot_query {
  lk_u32 *ids;
  lk_u32 count;
  lk_u32 capacity;
  void *(*alloc)(void *, lk_u32);
  void (*dealloc)(void *, void *);
  void *ud;
} lk_annot_query;

/* id -> array index map (open addressing, linear probing, power-of-
 * two capacity; key 0 = empty slot).  Internal shape, public only so
 * the store struct can embed it. */
typedef struct lk_annot_idmap {
  lk_u32 *keys;
  lk_u32 *values;
  lk_u32 cap;
  lk_u32 count;
} lk_annot_idmap;

/**
 ** Store
 **/

typedef struct lk_annot_store {
  void *(*alloc)(void *, lk_u32);
  void (*dealloc)(void *, void *);
  void *ud;

  lk_anchor *anchors;
  lk_u32 anchor_count, anchor_cap;
  lk_u32 next_anchor_id;
  lk_annot_idmap anchor_map; /* anchor id -> index in anchors[] */

  lk_annot_record *records;
  lk_u32 record_count, record_cap;
  lk_u32 next_record_id;
  lk_annot_idmap record_map; /* record id -> index in records[] */

  lk_annot_layer *layers;
  lk_u32 layer_count, layer_cap;

  lk_document *doc; /* attached document, NULL when detached */
  lk_u32 sub_id;
  lk_revision doc_rev; /* follows delta->after */
} lk_annot_store;

/**
 ** Lifecycle
 **/

/* NULL alloc/dealloc fall back to the system allocator. */
lk_annot_store *lk_annot_store_new(void *(*alloc)(void *, lk_u32),
                                   void (*dealloc)(void *, void *), void *ud);

/* Unsubscribes from the attached document (if any) and frees
 * everything.  Destroy the store before the document it is attached
 * to (same order contract as lk_edit_history). */
void lk_annot_store_destroy(lk_annot_store *s);

/* Remove all records and anchors; registered layers survive, marked
 * dirty with their version bumped. */
void lk_annot_store_clear(lk_annot_store *s);

/* Remove all records in one layer; the layer stays registered,
 * marked dirty with its version bumped. */
void lk_annot_store_clear_layer(lk_annot_store *s, const char *layer);

/* Attach subscribes to the document: every committed transaction's
 * deltas transform the anchors in order, and the store's revision
 * follows delta->after.  One store per document is the v1
 * configuration. */
void lk_annot_store_attach(lk_annot_store *s, lk_document *d);

/* The store's current revision (the attached document's revision as
 * of the last notification; the attach point's revision before any). */
lk_revision lk_annot_store_rev(const lk_annot_store *s);

/**
 ** Annotation CRUD
 **/

/* Add an annotation spanning [start, end) with meta_count key/value
 * pairs (all strings copied).  The layer is auto-registered.  Returns
 * the record id, 0 on failure (start >= end, or allocation). */
lk_u32 lk_annot_add(lk_annot_store *s, lk_u32 start, lk_u32 end,
                    const char *layer, const char **keys,
                    const char **values, lk_u32 meta_count);

/* Remove by id.  Returns 1 if removed, 0 if not found. */
int lk_annot_remove(lk_annot_store *s, lk_u32 id);

/* Record by id (NULL if not found).  Borrowed pointer: invalidated by
 * any mutation of the store. */
const lk_annot_record *lk_annot_get(const lk_annot_store *s, lk_u32 id);

/* Current span of an annotation (resolves anchors to positions).
 * Returns 1 and writes both out params, or 0 if not found. */
int lk_annot_get_span(const lk_annot_store *s, lk_u32 id, lk_u32 *start,
                      lk_u32 *end);

/* Metadata value for a key (NULL if the record or key is missing). */
const char *lk_annot_get_meta(const lk_annot_store *s, lk_u32 id,
                              const char *key);

/**
 ** Queries.  layer == NULL matches any layer; results append to out
 ** in record order.
 **/

void lk_annot_query_init(lk_annot_query *q);
void lk_annot_query_free(lk_annot_query *q);
void lk_annot_query_clear(lk_annot_query *q);

/* Annotations containing pos (start <= pos < end). */
void lk_annot_at(const lk_annot_store *s, lk_u32 pos, const char *layer,
                 lk_annot_query *out);

/* Annotations overlapping [start, end). */
void lk_annot_in_range(const lk_annot_store *s, lk_u32 start, lk_u32 end,
                       const char *layer, lk_annot_query *out);

/* All annotations in a layer. */
void lk_annot_by_layer(const lk_annot_store *s, const char *layer,
                       lk_annot_query *out);

/**
 ** Layer management
 **/

void lk_annot_register_layer(lk_annot_store *s, const char *name);
void lk_annot_set_layer_dirty(lk_annot_store *s, const char *name);
void lk_annot_set_layer_valid(lk_annot_store *s, const char *name);

/* Unknown layers read as DIRTY / version 0. */
lk_layer_state lk_annot_layer_state(const lk_annot_store *s,
                                    const char *name);
lk_u32 lk_annot_layer_version(const lk_annot_store *s, const char *name);

#ifdef __cplusplus
}
#endif

#endif /* LK_ANNOT_STORE_H */
