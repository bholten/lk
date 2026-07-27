/*
 * lk-annot-store.c -- annotation store (editor track, stage C).
 *
 * Straight lift of weft's annot_store.c, C89-ized with the lk
 * allocator triple.  Anchor transform semantics are preserved
 * exactly:
 *
 *   insert at pos: anchors after pos shift forward; anchors AT pos
 *   shift only with RIGHT bias (LEFT stays); anchors before pos are
 *   unchanged.
 *
 *   delete [pos, pos+len): anchors at or past the range end shift
 *   backward by len; anchors strictly inside collapse to pos; anchors
 *   at or before pos are unchanged.  Records whose span becomes
 *   empty or inverted (start >= end) are removed in the same pass.
 *
 * The weft edit hooks are replaced by a document subscription
 * (lk_annot_store_attach): each committed delta applies its delete
 * transform, then its insert transform, both at delta->start, and the
 * store's revision follows delta->after.  Layer dirty-marking matches
 * the weft hooks: edits set LK_LAYER_DIRTY without bumping the
 * version (only clear/clear_layer/set_dirty bump it).
 */

#include <string.h>

#include "core/lk-memory.h"
#include <lk-annot-store.h>

#define ANNOT_INITIAL_ANCHOR_CAP 64
#define ANNOT_INITIAL_RECORD_CAP 32
#define ANNOT_INITIAL_LAYER_CAP 8
#define ANNOT_INITIAL_QUERY_CAP 16
#define ANNOT_IDMAP_INITIAL_CAP 64

/* ---- id -> index map (open addressing, linear probing) ---- */

static void *annot_zalloc(lk_annot_store *s, lk_u32 bytes) {
  void *p = s->alloc(s->ud, bytes);

  if (p) {
    memset(p, 0, bytes);
  }

  return p;
}

static int idmap_init(lk_annot_store *s, lk_annot_idmap *m, lk_u32 cap) {
  m->cap = cap;
  m->count = 0;
  m->keys = (lk_u32 *)annot_zalloc(s, cap * (lk_u32)sizeof(lk_u32));
  m->values = (lk_u32 *)annot_zalloc(s, cap * (lk_u32)sizeof(lk_u32));

  return m->keys != NULL && m->values != NULL;
}

static void idmap_free(lk_annot_store *s, lk_annot_idmap *m) {
  if (m->keys) {
    s->dealloc(s->ud, m->keys);
  }

  if (m->values) {
    s->dealloc(s->ud, m->values);
  }

  m->keys = NULL;
  m->values = NULL;
  m->cap = 0;
  m->count = 0;
}

static void idmap_clear(lk_annot_idmap *m) {
  if (m->keys) {
    memset(m->keys, 0, m->cap * sizeof(lk_u32));
  }

  m->count = 0;
}

/* Insert without checking load factor (used during resize). */
static void idmap_insert_raw(lk_u32 *keys, lk_u32 *values, lk_u32 cap,
                             lk_u32 key, lk_u32 value) {
  lk_u32 mask = cap - 1;
  lk_u32 idx = key & mask;

  while (keys[idx] != 0) {
    idx = (idx + 1) & mask;
  }

  keys[idx] = key;
  values[idx] = value;
}

static int idmap_grow(lk_annot_store *s, lk_annot_idmap *m) {
  lk_u32 new_cap = m->cap * 2;
  lk_u32 *new_keys = (lk_u32 *)annot_zalloc(s, new_cap * (lk_u32)sizeof(lk_u32));
  lk_u32 *new_values =
      (lk_u32 *)annot_zalloc(s, new_cap * (lk_u32)sizeof(lk_u32));
  lk_u32 i;

  if (!new_keys || !new_values) {
    if (new_keys) {
      s->dealloc(s->ud, new_keys);
    }

    if (new_values) {
      s->dealloc(s->ud, new_values);
    }

    return 0;
  }

  for (i = 0; i < m->cap; i++) {
    if (m->keys[i] != 0) {
      idmap_insert_raw(new_keys, new_values, new_cap, m->keys[i],
                       m->values[i]);
    }
  }

  s->dealloc(s->ud, m->keys);
  s->dealloc(s->ud, m->values);
  m->keys = new_keys;
  m->values = new_values;
  m->cap = new_cap;

  return 1;
}

static int idmap_put(lk_annot_store *s, lk_annot_idmap *m, lk_u32 key,
                     lk_u32 value) {
  lk_u32 mask;
  lk_u32 idx;

  if (m->count * 4 >= m->cap * 3) { /* load factor > 75% */
    if (!idmap_grow(s, m)) {
      return 0;
    }
  }

  mask = m->cap - 1;
  idx = key & mask;

  while (m->keys[idx] != 0) {
    if (m->keys[idx] == key) {
      m->values[idx] = value; /* update existing */

      return 1;
    }

    idx = (idx + 1) & mask;
  }

  m->keys[idx] = key;
  m->values[idx] = value;
  m->count++;

  return 1;
}

/* Returns 1 if found, writes *out_value. */
static int idmap_get(const lk_annot_idmap *m, lk_u32 key, lk_u32 *out_value) {
  lk_u32 mask;
  lk_u32 idx;

  if (!m->keys || m->count == 0) {
    return 0;
  }

  mask = m->cap - 1;
  idx = key & mask;

  while (m->keys[idx] != 0) {
    if (m->keys[idx] == key) {
      *out_value = m->values[idx];

      return 1;
    }

    idx = (idx + 1) & mask;
  }

  return 0;
}

static void idmap_remove(lk_annot_store *s, lk_annot_idmap *m, lk_u32 key) {
  lk_u32 mask;
  lk_u32 idx;

  if (!m->keys || m->count == 0) {
    return;
  }

  mask = m->cap - 1;
  idx = key & mask;

  while (m->keys[idx] != 0) {
    if (m->keys[idx] == key) {
      /* remove and fix the probe chain (re-insertion deletion) */
      lk_u32 next;

      m->keys[idx] = 0;
      m->count--;
      next = (idx + 1) & mask;

      while (m->keys[next] != 0) {
        lk_u32 k = m->keys[next];
        lk_u32 v = m->values[next];

        m->keys[next] = 0;
        m->count--;
        idmap_put(s, m, k, v);
        next = (next + 1) & mask;
      }

      return;
    }

    idx = (idx + 1) & mask;
  }
}

/* ---- Internal helpers ---- */

static char *annot_strdup(lk_annot_store *s, const char *str) {
  lk_u32 len;
  char *dup;

  if (!str) {
    return NULL;
  }

  len = (lk_u32)strlen(str) + 1;
  dup = (char *)s->alloc(s->ud, len);

  if (dup) {
    memcpy(dup, str, len);
  }

  return dup;
}

static const lk_anchor *find_anchor_const(const lk_annot_store *s,
                                          lk_u32 id) {
  lk_u32 idx;

  if (idmap_get(&s->anchor_map, id, &idx)) {
    return &s->anchors[idx];
  }

  return NULL;
}

static const lk_annot_record *find_record_const(const lk_annot_store *s,
                                                lk_u32 id) {
  lk_u32 idx;

  if (idmap_get(&s->record_map, id, &idx)) {
    return &s->records[idx];
  }

  return NULL;
}

static lk_annot_layer *find_layer(lk_annot_store *s, const char *name) {
  lk_u32 i;

  if (!name) {
    return NULL;
  }

  for (i = 0; i < s->layer_count; i++) {
    if (s->layers[i].name && strcmp(s->layers[i].name, name) == 0) {
      return &s->layers[i];
    }
  }

  return NULL;
}

static const lk_annot_layer *find_layer_const(const lk_annot_store *s,
                                              const char *name) {
  lk_u32 i;

  if (!name) {
    return NULL;
  }

  for (i = 0; i < s->layer_count; i++) {
    if (s->layers[i].name && strcmp(s->layers[i].name, name) == 0) {
      return &s->layers[i];
    }
  }

  return NULL;
}

/* Create an anchor (returns 0 on failure). */
static lk_u32 create_anchor(lk_annot_store *s, lk_u32 pos,
                            lk_anchor_bias bias) {
  lk_u32 id;
  lk_u32 idx;
  lk_anchor *a;

  if (s->anchor_count >= s->anchor_cap) {
    lk_u32 new_cap = s->anchor_cap * 2;
    lk_anchor *na =
        (lk_anchor *)s->alloc(s->ud, new_cap * (lk_u32)sizeof(lk_anchor));

    if (!na) {
      return 0;
    }

    memcpy(na, s->anchors, s->anchor_count * sizeof(lk_anchor));
    s->dealloc(s->ud, s->anchors);
    s->anchors = na;
    s->anchor_cap = new_cap;
  }

  id = s->next_anchor_id++;
  idx = s->anchor_count++;
  a = &s->anchors[idx];
  a->pos = pos;
  a->bias = bias;
  a->id = id;
  idmap_put(s, &s->anchor_map, id, idx);

  return id;
}

static void record_free_meta(lk_annot_store *s, lk_annot_record *r) {
  lk_u32 i;

  if (!r) {
    return;
  }

  for (i = 0; i < r->meta_count; i++) {
    if (r->keys[i]) {
      s->dealloc(s->ud, r->keys[i]);
    }

    if (r->values[i]) {
      s->dealloc(s->ud, r->values[i]);
    }
  }

  if (r->keys) {
    s->dealloc(s->ud, r->keys);
  }

  if (r->values) {
    s->dealloc(s->ud, r->values);
  }

  if (r->layer) {
    s->dealloc(s->ud, r->layer);
  }

  r->keys = NULL;
  r->values = NULL;
  r->layer = NULL;
  r->meta_count = 0;
}

/* Remove an anchor by id (does not check if in use). */
static void remove_anchor(lk_annot_store *s, lk_u32 id) {
  lk_u32 idx;
  lk_u32 last;

  if (!idmap_get(&s->anchor_map, id, &idx)) {
    return;
  }

  idmap_remove(s, &s->anchor_map, id);

  /* move the last element into this slot */
  last = s->anchor_count - 1;

  if (idx < last) {
    s->anchors[idx] = s->anchors[last];
    idmap_put(s, &s->anchor_map, s->anchors[idx].id, idx);
  }

  s->anchor_count--;
}

static int query_add(const lk_annot_store *s, lk_annot_query *q, lk_u32 id) {
  if (q->count >= q->capacity) {
    lk_u32 new_cap =
        q->capacity == 0 ? ANNOT_INITIAL_QUERY_CAP : q->capacity * 2;
    lk_u32 *new_ids;

    /* capture the store's allocator so query_free stays store-free */
    q->alloc = s->alloc;
    q->dealloc = s->dealloc;
    q->ud = s->ud;
    new_ids = (lk_u32 *)q->alloc(q->ud, new_cap * (lk_u32)sizeof(lk_u32));

    if (!new_ids) {
      return 0;
    }

    if (q->ids) {
      memcpy(new_ids, q->ids, q->count * sizeof(lk_u32));
      q->dealloc(q->ud, q->ids);
    }

    q->ids = new_ids;
    q->capacity = new_cap;
  }

  q->ids[q->count++] = id;

  return 1;
}

/* ---- Edit transforms (weft's on_insert/on_delete bodies) ---- */

static void annot_apply_insert(lk_annot_store *s, lk_u32 pos, lk_u32 len) {
  lk_u32 i;

  if (len == 0) {
    return;
  }

  for (i = 0; i < s->anchor_count; i++) {
    lk_anchor *a = &s->anchors[i];

    if (a->pos > pos) {
      a->pos += len; /* after the insert point: shift forward */
    } else if (a->pos == pos) {
      if (a->bias == LK_ANCHOR_RIGHT) {
        a->pos += len; /* RIGHT moves with the insert; LEFT stays */
      }
    }
  }

  for (i = 0; i < s->layer_count; i++) {
    s->layers[i].state = LK_LAYER_DIRTY;
  }
}

static void annot_apply_delete(lk_annot_store *s, lk_u32 pos, lk_u32 len) {
  lk_u32 end = pos + len;
  lk_u32 i;
  lk_u32 write_idx;

  if (len == 0) {
    return;
  }

  for (i = 0; i < s->anchor_count; i++) {
    lk_anchor *a = &s->anchors[i];

    if (a->pos >= end) {
      a->pos -= len; /* past the range: shift backward */
    } else if (a->pos > pos) {
      a->pos = pos; /* inside the range: collapse to its start */
    }
  }

  /* remove records whose span became empty or inverted */
  write_idx = 0;

  for (i = 0; i < s->record_count; i++) {
    lk_annot_record *r = &s->records[i];
    const lk_anchor *start_a = find_anchor_const(s, r->start_anchor);
    const lk_anchor *end_a = find_anchor_const(s, r->end_anchor);
    int do_remove = 0;

    if (!start_a || !end_a) {
      do_remove = 1;
    } else if (start_a->pos >= end_a->pos) {
      do_remove = 1;
    }

    if (do_remove) {
      idmap_remove(s, &s->record_map, r->id);
      remove_anchor(s, r->start_anchor);
      remove_anchor(s, r->end_anchor);
      record_free_meta(s, r);
    } else {
      if (write_idx != i) {
        s->records[write_idx] = s->records[i];
        idmap_put(s, &s->record_map, s->records[write_idx].id, write_idx);
      }

      write_idx++;
    }
  }

  s->record_count = write_idx;

  for (i = 0; i < s->layer_count; i++) {
    s->layers[i].state = LK_LAYER_DIRTY;
  }
}

/* ---- Document listener ---- */

static void annot_on_doc(void *ud, const lk_document *d,
                         const lk_doc_delta *deltas, lk_u32 n) {
  lk_annot_store *s = (lk_annot_store *)ud;
  lk_u32 i;

  (void)d;

  for (i = 0; i < n; i++) {
    if (deltas[i].deleted_len) {
      annot_apply_delete(s, deltas[i].start, deltas[i].deleted_len);
    }

    if (deltas[i].inserted_len) {
      annot_apply_insert(s, deltas[i].start, deltas[i].inserted_len);
    }

    s->doc_rev = deltas[i].after;
  }
}

/* ---- Lifecycle ---- */

lk_annot_store *lk_annot_store_new(void *(*alloc)(void *, lk_u32),
                                   void (*dealloc)(void *, void *),
                                   void *ud) {
  lk_annot_store *s;

  if (!alloc || !dealloc) {
    alloc = lk_sys_alloc;
    dealloc = lk_sys_dealloc;
    ud = NULL;
  }

  s = (lk_annot_store *)alloc(ud, (lk_u32)sizeof(lk_annot_store));

  if (!s) {
    return NULL;
  }

  memset(s, 0, sizeof(*s));
  s->alloc = alloc;
  s->dealloc = dealloc;
  s->ud = ud;

  s->anchors = (lk_anchor *)alloc(
      ud, ANNOT_INITIAL_ANCHOR_CAP * (lk_u32)sizeof(lk_anchor));
  s->anchor_cap = ANNOT_INITIAL_ANCHOR_CAP;
  s->next_anchor_id = 1; /* 0 reserved as invalid */

  s->records = (lk_annot_record *)alloc(
      ud, ANNOT_INITIAL_RECORD_CAP * (lk_u32)sizeof(lk_annot_record));
  s->record_cap = ANNOT_INITIAL_RECORD_CAP;
  s->next_record_id = 1;

  s->layers = (lk_annot_layer *)alloc(
      ud, ANNOT_INITIAL_LAYER_CAP * (lk_u32)sizeof(lk_annot_layer));
  s->layer_cap = ANNOT_INITIAL_LAYER_CAP;

  if (!s->anchors || !s->records || !s->layers ||
      !idmap_init(s, &s->anchor_map, ANNOT_IDMAP_INITIAL_CAP) ||
      !idmap_init(s, &s->record_map, ANNOT_IDMAP_INITIAL_CAP)) {
    lk_annot_store_destroy(s);

    return NULL;
  }

  return s;
}

void lk_annot_store_destroy(lk_annot_store *s) {
  lk_u32 i;

  if (!s) {
    return;
  }

  if (s->doc && s->sub_id) {
    lk_doc_unsubscribe(s->doc, s->sub_id);
  }

  for (i = 0; i < s->record_count; i++) {
    record_free_meta(s, &s->records[i]);
  }

  if (s->records) {
    s->dealloc(s->ud, s->records);
  }

  for (i = 0; i < s->layer_count; i++) {
    if (s->layers[i].name) {
      s->dealloc(s->ud, s->layers[i].name);
    }
  }

  if (s->layers) {
    s->dealloc(s->ud, s->layers);
  }

  if (s->anchors) {
    s->dealloc(s->ud, s->anchors);
  }

  idmap_free(s, &s->anchor_map);
  idmap_free(s, &s->record_map);
  s->dealloc(s->ud, s);
}

void lk_annot_store_clear(lk_annot_store *s) {
  lk_u32 i;

  if (!s) {
    return;
  }

  for (i = 0; i < s->record_count; i++) {
    record_free_meta(s, &s->records[i]);
  }

  s->record_count = 0;
  s->anchor_count = 0;
  idmap_clear(&s->anchor_map);
  idmap_clear(&s->record_map);

  for (i = 0; i < s->layer_count; i++) {
    s->layers[i].state = LK_LAYER_DIRTY;
    s->layers[i].version++;
  }
}

void lk_annot_store_clear_layer(lk_annot_store *s, const char *layer) {
  lk_u32 i;
  lk_u32 write_idx;
  lk_annot_layer *l;

  if (!s || !layer) {
    return;
  }

  write_idx = 0;

  for (i = 0; i < s->record_count; i++) {
    if (s->records[i].layer && strcmp(s->records[i].layer, layer) == 0) {
      idmap_remove(s, &s->record_map, s->records[i].id);
      remove_anchor(s, s->records[i].start_anchor);
      remove_anchor(s, s->records[i].end_anchor);
      record_free_meta(s, &s->records[i]);
    } else {
      if (write_idx != i) {
        s->records[write_idx] = s->records[i];
        idmap_put(s, &s->record_map, s->records[write_idx].id, write_idx);
      }

      write_idx++;
    }
  }

  s->record_count = write_idx;
  l = find_layer(s, layer);

  if (l) {
    l->state = LK_LAYER_DIRTY;
    l->version++;
  }
}

void lk_annot_store_attach(lk_annot_store *s, lk_document *d) {
  if (!s || !d) {
    return;
  }

  s->doc = d;
  s->sub_id = lk_doc_subscribe(d, annot_on_doc, s);
  s->doc_rev = lk_doc_revision(d);
}

lk_revision lk_annot_store_rev(const lk_annot_store *s) {
  lk_revision zero;

  zero.hi = 0;
  zero.lo = 0;

  return s ? s->doc_rev : zero;
}

/* ---- Annotation CRUD ---- */

lk_u32 lk_annot_add(lk_annot_store *s, lk_u32 start, lk_u32 end,
                    const char *layer, const char **keys,
                    const char **values, lk_u32 meta_count) {
  lk_u32 start_anchor;
  lk_u32 end_anchor;
  char **new_keys = NULL;
  char **new_values = NULL;
  lk_u32 i;
  lk_u32 id;
  lk_u32 rec_idx;
  lk_annot_record *r;

  if (!s || start >= end) {
    return 0;
  }

  if (s->record_count >= s->record_cap) {
    lk_u32 new_cap = s->record_cap * 2;
    lk_annot_record *nr = (lk_annot_record *)s->alloc(
        s->ud, new_cap * (lk_u32)sizeof(lk_annot_record));

    if (!nr) {
      return 0;
    }

    memcpy(nr, s->records, s->record_count * sizeof(lk_annot_record));
    s->dealloc(s->ud, s->records);
    s->records = nr;
    s->record_cap = new_cap;
  }

  start_anchor = create_anchor(s, start, LK_ANCHOR_LEFT);

  if (start_anchor == 0) {
    return 0;
  }

  end_anchor = create_anchor(s, end, LK_ANCHOR_RIGHT);

  if (end_anchor == 0) {
    remove_anchor(s, start_anchor);

    return 0;
  }

  if (meta_count > 0) {
    new_keys = (char **)annot_zalloc(s, meta_count * (lk_u32)sizeof(char *));
    new_values =
        (char **)annot_zalloc(s, meta_count * (lk_u32)sizeof(char *));

    if (!new_keys || !new_values) {
      goto cleanup_meta;
    }

    for (i = 0; i < meta_count; i++) {
      if (keys && keys[i]) {
        new_keys[i] = annot_strdup(s, keys[i]);

        if (!new_keys[i]) {
          goto cleanup_meta;
        }
      }

      if (values && values[i]) {
        new_values[i] = annot_strdup(s, values[i]);

        if (!new_values[i]) {
          goto cleanup_meta;
        }
      }
    }
  }

  id = s->next_record_id++;
  rec_idx = s->record_count++;
  r = &s->records[rec_idx];
  r->id = id;
  r->start_anchor = start_anchor;
  r->end_anchor = end_anchor;
  r->layer = annot_strdup(s, layer);
  r->keys = new_keys;
  r->values = new_values;
  r->meta_count = meta_count;
  r->doc_rev = s->doc_rev;
  idmap_put(s, &s->record_map, id, rec_idx);

  /* auto-register the layer */
  if (layer && !find_layer(s, layer)) {
    lk_annot_register_layer(s, layer);
  }

  return id;

cleanup_meta:
  if (new_keys) {
    for (i = 0; i < meta_count; i++) {
      if (new_keys[i]) {
        s->dealloc(s->ud, new_keys[i]);
      }
    }

    s->dealloc(s->ud, new_keys);
  }

  if (new_values) {
    for (i = 0; i < meta_count; i++) {
      if (new_values[i]) {
        s->dealloc(s->ud, new_values[i]);
      }
    }

    s->dealloc(s->ud, new_values);
  }

  remove_anchor(s, start_anchor);
  remove_anchor(s, end_anchor);

  return 0;
}

int lk_annot_remove(lk_annot_store *s, lk_u32 id) {
  lk_u32 idx;
  lk_u32 last;

  if (!s || id == 0) {
    return 0;
  }

  if (!idmap_get(&s->record_map, id, &idx)) {
    return 0;
  }

  remove_anchor(s, s->records[idx].start_anchor);
  remove_anchor(s, s->records[idx].end_anchor);
  record_free_meta(s, &s->records[idx]);
  idmap_remove(s, &s->record_map, id);

  /* move the last record into this slot */
  last = s->record_count - 1;

  if (idx < last) {
    s->records[idx] = s->records[last];
    idmap_put(s, &s->record_map, s->records[idx].id, idx);
  }

  s->record_count--;

  return 1;
}

const lk_annot_record *lk_annot_get(const lk_annot_store *s, lk_u32 id) {
  if (!s || id == 0) {
    return NULL;
  }

  return find_record_const(s, id);
}

int lk_annot_get_span(const lk_annot_store *s, lk_u32 id, lk_u32 *start,
                      lk_u32 *end) {
  const lk_annot_record *r;
  const lk_anchor *start_a;
  const lk_anchor *end_a;

  if (!s || id == 0 || !start || !end) {
    return 0;
  }

  r = find_record_const(s, id);

  if (!r) {
    return 0;
  }

  start_a = find_anchor_const(s, r->start_anchor);
  end_a = find_anchor_const(s, r->end_anchor);

  if (!start_a || !end_a) {
    return 0;
  }

  *start = start_a->pos;
  *end = end_a->pos;

  return 1;
}

const char *lk_annot_get_meta(const lk_annot_store *s, lk_u32 id,
                              const char *key) {
  const lk_annot_record *r;
  lk_u32 i;

  if (!s || id == 0 || !key) {
    return NULL;
  }

  r = find_record_const(s, id);

  if (!r) {
    return NULL;
  }

  for (i = 0; i < r->meta_count; i++) {
    if (r->keys[i] && strcmp(r->keys[i], key) == 0) {
      return r->values[i];
    }
  }

  return NULL;
}

/* ---- Queries ---- */

void lk_annot_query_init(lk_annot_query *q) {
  if (q) {
    memset(q, 0, sizeof(*q));
  }
}

void lk_annot_query_free(lk_annot_query *q) {
  if (!q) {
    return;
  }

  if (q->ids && q->dealloc) {
    q->dealloc(q->ud, q->ids);
  }

  q->ids = NULL;
  q->count = 0;
  q->capacity = 0;
}

void lk_annot_query_clear(lk_annot_query *q) {
  if (q) {
    q->count = 0;
  }
}

void lk_annot_at(const lk_annot_store *s, lk_u32 pos, const char *layer,
                 lk_annot_query *out) {
  lk_u32 i;

  if (!s || !out) {
    return;
  }

  for (i = 0; i < s->record_count; i++) {
    const lk_annot_record *r = &s->records[i];
    const lk_anchor *start_a;
    const lk_anchor *end_a;

    if (layer) {
      if (!r->layer || strcmp(r->layer, layer) != 0) {
        continue;
      }
    }

    start_a = find_anchor_const(s, r->start_anchor);
    end_a = find_anchor_const(s, r->end_anchor);

    if (!start_a || !end_a) {
      continue;
    }

    if (pos >= start_a->pos && pos < end_a->pos) {
      query_add(s, out, r->id);
    }
  }
}

void lk_annot_in_range(const lk_annot_store *s, lk_u32 start, lk_u32 end,
                       const char *layer, lk_annot_query *out) {
  lk_u32 i;

  if (!s || !out || start >= end) {
    return;
  }

  for (i = 0; i < s->record_count; i++) {
    const lk_annot_record *r = &s->records[i];
    const lk_anchor *start_a;
    const lk_anchor *end_a;

    if (layer) {
      if (!r->layer || strcmp(r->layer, layer) != 0) {
        continue;
      }
    }

    start_a = find_anchor_const(s, r->start_anchor);
    end_a = find_anchor_const(s, r->end_anchor);

    if (!start_a || !end_a) {
      continue;
    }

    /* overlap: span.start < end && span.end > start */
    if (start_a->pos < end && end_a->pos > start) {
      query_add(s, out, r->id);
    }
  }
}

void lk_annot_by_layer(const lk_annot_store *s, const char *layer,
                       lk_annot_query *out) {
  lk_u32 i;

  if (!s || !layer || !out) {
    return;
  }

  for (i = 0; i < s->record_count; i++) {
    const lk_annot_record *r = &s->records[i];

    if (r->layer && strcmp(r->layer, layer) == 0) {
      query_add(s, out, r->id);
    }
  }
}

/* ---- Layer management ---- */

void lk_annot_register_layer(lk_annot_store *s, const char *name) {
  lk_annot_layer *l;

  if (!s || !name) {
    return;
  }

  if (find_layer(s, name)) {
    return;
  }

  if (s->layer_count >= s->layer_cap) {
    lk_u32 new_cap = s->layer_cap * 2;
    lk_annot_layer *nl = (lk_annot_layer *)s->alloc(
        s->ud, new_cap * (lk_u32)sizeof(lk_annot_layer));

    if (!nl) {
      return;
    }

    memcpy(nl, s->layers, s->layer_count * sizeof(lk_annot_layer));
    s->dealloc(s->ud, s->layers);
    s->layers = nl;
    s->layer_cap = new_cap;
  }

  l = &s->layers[s->layer_count++];
  l->name = annot_strdup(s, name);
  l->state = LK_LAYER_VALID;
  l->version = 1;
}

void lk_annot_set_layer_dirty(lk_annot_store *s, const char *name) {
  lk_annot_layer *l;

  if (!s || !name) {
    return;
  }

  l = find_layer(s, name);

  if (l) {
    l->state = LK_LAYER_DIRTY;
    l->version++;
  }
}

void lk_annot_set_layer_valid(lk_annot_store *s, const char *name) {
  lk_annot_layer *l;

  if (!s || !name) {
    return;
  }

  l = find_layer(s, name);

  if (l) {
    l->state = LK_LAYER_VALID;
  }
}

lk_layer_state lk_annot_layer_state(const lk_annot_store *s,
                                    const char *name) {
  const lk_annot_layer *l;

  if (!s || !name) {
    return LK_LAYER_DIRTY;
  }

  l = find_layer_const(s, name);

  if (l) {
    return l->state;
  }

  return LK_LAYER_DIRTY;
}

lk_u32 lk_annot_layer_version(const lk_annot_store *s, const char *name) {
  const lk_annot_layer *l;

  if (!s || !name) {
    return 0;
  }

  l = find_layer_const(s, name);

  if (l) {
    return l->version;
  }

  return 0;
}
