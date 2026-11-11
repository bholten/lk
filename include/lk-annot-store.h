#ifndef LK_ANNOT_STORE_H
#define LK_ANNOT_STORE_H

#include "lk-document.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum lk_anchor_bias {
  LK_ANCHOR_LEFT = 0,
  LK_ANCHOR_RIGHT
} lk_anchor_bias;

typedef struct lk_anchor {
  lk_u32 pos;
  lk_anchor_bias bias;
  lk_u32 id;
} lk_anchor;

typedef struct lk_annot_record {
  lk_u32 id;
  lk_u32 start_anchor;
  lk_u32 end_anchor;
  char *layer;
  char **keys;
  char **values;
  lk_u32 meta_count;
  lk_revision doc_rev;
  lk_u32 pres_type;

  lk_value pres_value;

} lk_annot_record;

typedef enum lk_layer_state {
  LK_LAYER_VALID = 0,
  LK_LAYER_DIRTY
} lk_layer_state;

typedef struct lk_annot_layer {
  char *name;
  lk_layer_state state;
  lk_u32 version;

  lk_i32 priority;

} lk_annot_layer;

typedef struct lk_annot_query {
  lk_u32 *ids;
  lk_u32 count;
  lk_u32 capacity;
  void *(*alloc)(void *, lk_u32);
  void (*dealloc)(void *, void *);
  void *ud;
} lk_annot_query;

typedef struct lk_annot_idmap {
  lk_u32 *keys;
  lk_u32 *values;
  lk_u32 cap;
  lk_u32 count;
} lk_annot_idmap;

typedef struct lk_annot_store {
  void *(*alloc)(void *, lk_u32);
  void (*dealloc)(void *, void *);
  void *ud;

  lk_anchor *anchors;
  lk_u32 anchor_count, anchor_cap;
  lk_u32 next_anchor_id;
  lk_annot_idmap anchor_map;

  lk_annot_record *records;
  lk_u32 record_count, record_cap;
  lk_u32 next_record_id;
  lk_annot_idmap record_map;

  lk_annot_layer *layers;
  lk_u32 layer_count, layer_cap;

  lk_document *doc;
  lk_u32 sub_id;
  lk_revision doc_rev;
  lk_u32 change_seq;

  void (*pres_release)(void *ud, lk_value v);
  void *pres_release_ud;
} lk_annot_store;

lk_annot_store *lk_annot_store_new(void *(*alloc)(void *, lk_u32),
                                   void (*dealloc)(void *, void *), void *ud);

void lk_annot_store_destroy(lk_annot_store *s);

void lk_annot_store_clear(lk_annot_store *s);

void lk_annot_store_clear_layer(lk_annot_store *s, const char *layer);

void lk_annot_store_attach(lk_annot_store *s, lk_document *d);

lk_revision lk_annot_store_rev(const lk_annot_store *s);

lk_u32 lk_annot_store_seq(const lk_annot_store *s);

lk_u32 lk_annot_layer_count(const lk_annot_store *s);
const char *lk_annot_layer_name(const lk_annot_store *s, lk_u32 index);

lk_u32 lk_annot_add(lk_annot_store *s, lk_u32 start, lk_u32 end,
                    const char *layer, const char **keys, const char **values,
                    lk_u32 meta_count);

int lk_annot_remove(lk_annot_store *s, lk_u32 id);

const lk_annot_record *lk_annot_get(const lk_annot_store *s, lk_u32 id);

int lk_annot_get_span(const lk_annot_store *s, lk_u32 id, lk_u32 *start,
                      lk_u32 *end);

const char *lk_annot_get_meta(const lk_annot_store *s, lk_u32 id,
                              const char *key);

void lk_annot_query_init(lk_annot_query *q);
void lk_annot_query_free(lk_annot_query *q);
void lk_annot_query_clear(lk_annot_query *q);

void lk_annot_at(const lk_annot_store *s, lk_u32 pos, const char *layer,
                 lk_annot_query *out);

void lk_annot_in_range(const lk_annot_store *s, lk_u32 start, lk_u32 end,
                       const char *layer, lk_annot_query *out);

void lk_annot_by_layer(const lk_annot_store *s, const char *layer,
                       lk_annot_query *out);

int lk_annot_set_present(lk_annot_store *s, lk_u32 id, lk_u32 type_id,
                         lk_value value);

void lk_annot_set_present_release(lk_annot_store *s,
                                  void (*fn)(void *ud, lk_value v), void *ud);

lk_u32 lk_annot_presentations_at(const lk_annot_store *s, lk_u32 pos,
                                 lk_presentation_hit *out, lk_u32 cap);

void lk_annot_layer_set_priority(lk_annot_store *s, const char *layer,
                                 lk_i32 priority);

lk_presentation_source lk_annot_presentation_source(lk_annot_store *s);

void lk_annot_register_layer(lk_annot_store *s, const char *name);
void lk_annot_set_layer_dirty(lk_annot_store *s, const char *name);
void lk_annot_set_layer_valid(lk_annot_store *s, const char *name);

lk_layer_state lk_annot_layer_state(const lk_annot_store *s, const char *name);
lk_u32 lk_annot_layer_version(const lk_annot_store *s, const char *name);

#ifdef __cplusplus
}
#endif

#endif
