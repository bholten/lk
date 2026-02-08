#ifndef LK_DATA_H
#define LK_DATA_H

#include <stddef.h>

#include "lk-int.h"

/**
 ** String view (no ownership)
 **/
typedef struct lk_str {
  const char *ptr;
  lk_u32 len;
} lk_str;

lk_str lk_str_c(const char *cstr);
int lk_str_cmp(lk_str a, lk_str b);

/**
 ** lk_node_id is an interned string -> u32
 **/
typedef lk_u32 lk_node_id;

typedef struct lk_intern lk_intern;

lk_intern *lk_intern_new(void*(alloc)(void*, lk_u32), void *alloc_ud);
void lk_intern_destroy(lk_intern *it);

lk_node_id lk_intern_id(lk_intern *it, lk_str s); /* return stable id */
lk_str lk_intern_str(const lk_intern *it, lk_node_id id);

typedef enum lk_kind {
  UIK_WINDOW = 1,
  UIK_ROW,
  UIK_COLUMN,
  UIK_SPACER,
  UIK_LABEL,
  UIK_BUTTON,
  UIK__COUNT
} lk_kind;

typedef enum lk_prop_key {
  UIP_TEXT = 1, /* string */
  UIP_FOCUSABLE, /* bool */
  UIP_DISABLED, /* bool */

  UIP_W, /* int px */
  UIP_H, /* int px */
  UIP_PADDING, /* int px */
  UIP_GAP, /* int px */

  UIP__COUNT
} lk_prop_key;

typedef enum lk_value_tag {
  UIV_NONE = 0,
  UIV_BOOL,
  UIV_I32,
  UIV_STR
} lk_value_tag;

typedef struct lk_value {
  lk_value_tag tag;
  union {
    lk_u8 b;
    lk_u32 i;
    lk_u32 str_id; /* interned string id (UIV_STR) */
  } as;
} lk_value;

/* prop is a (key, value) pair */
typedef struct lk_prop {
  lk_u16 key; /* lk_prop_key */
  lk_value value;
} lk_prop;


/* Arena storage */
/* Each node stores:
 *   - id: stable node id
 *   - kind: enum
 *   - parent: index (0 means none/root)
 *   - first_child / next_sibling: adjacency (compact, no per-node dynamic arrays)
 *   - props: slice into props arena
 */

typedef lk_u32 lk_ix;

typedef struct lk_node {
  lk_node_id id;
  lk_u16 kind;
  lk_ix parent;
  lk_ix first_child;
  lk_ix next_sibling;
  lk_ix props_off;
  lk_u16 props_len;
} lk_node;

typedef struct lk_tree {
  lk_intern *intern;
  lk_u8 owns_intern; /* 1 if tree created the intern table */
  lk_node *nodes;
  lk_u32 node_cap;
  lk_u32 node_count;
  lk_prop *props;
  lk_u32 prop_cap;
  lk_u32 prop_count;
  lk_ix root;
  void *(*alloc)(void *ud, lk_u32 bytes);
  void (*dealloc)(void *ud, void *ptr);
  void *alloc_ud;
} lk_tree;

typedef struct lk_tree_cfg {
  lk_intern *intern;
  void *(*alloc)(void *ud, lk_u32 bytes);
  void (*dealloc)(void *ud, void *ptr);
  void *ud;

  lk_u32 node_cap_hint;
  lk_u32 prop_cap_hint;
} lk_tree_cfg;

lk_tree *lk_tree_create(const lk_tree_cfg *cfg);
void lk_tree_destroy(lk_tree *t);
void lk_tree_reset(lk_tree *t); /* keep capacity, clear counts */

/* Create a node with id+kind, append to arena, return node index (1..N).
 * Caller wires parent/children via lk_tree_append_child.
 */
lk_ix lk_tree_add_node(lk_tree *t, lk_node_id id, lk_kind kind);

/* Convenience: create node by string id via intern (must not be NULL). */
lk_ix lk_tree_add_node_s(lk_tree *t, lk_str id_str, lk_kind kind);

/* Set root node index. */
void lk_tree_set_root(lk_tree *t, lk_ix root);

/* Append child to parent's child list (preserves insertion order). */
void lk_tree_append_child(lk_tree *t, lk_ix parent, lk_ix child);


/* Set/append a prop on a node.
 * Phase 0: allows duplicates; validation can flag duplicates later if desired.
 */
void lk_tree_add_prop(lk_tree *t, lk_ix node, lk_prop_key key, lk_value v);

/* Value constructors. */
lk_value lk_v_none(void);
lk_value lk_v_bool(int b);
lk_value lk_v_i32(lk_i32 i);
lk_value lk_v_str(lk_intern *it, lk_str s);
lk_value lk_v_cstr(lk_intern *it, const char *cstr);


/* Validation */
typedef enum lk_diag_kind {
  UID_NONE = 0,
  UID_ERROR,
  UID_WARN
} lk_diag_kind;

typedef enum lk_diag_code {
  UIDC_OK = 0,

  UIDC_NO_ROOT,
  UIDC_INVALID_NODE_INDEX,
  UIDC_CYCLE_DETECTED,
  UIDC_DUPLICATE_NODE_ID,

  UIDC_INVALID_KIND,
  UIDC_INVALID_PROP_KEY,
  UIDC_PROP_TYPE_MISMATCH,
  UIDC_PROP_NOT_ALLOWED_FOR_KIND,
  UIDC_MISSING_REQUIRED_PROP,

  UIDC_PARENT_MISMATCH,
  UIDC_MULTIPLE_PARENTS,

  UIDC__COUNT
} lk_diag_code;

typedef struct lk_diag {
  lk_diag_kind kind;
  lk_diag_code code;

  lk_ix node;
  lk_u16 key;
  lk_u16 kind_u16;

  const char *msg; /* Optionmal short message owned by caller */
} lk_diag;

typedef struct lk_validate_opts {
  int require_root;
  int forbid_duplicate_ids;
  int forbid_cycles;
  int forbid_multiple_parents;
  
  int check_prop_schema;
} lk_validate_opts;

int lk_tree_validate(const lk_tree *t, const lk_validate_opts *opts,
                     lk_diag *diags, lk_u32 diags_cap, lk_u32 *out_diags_len);

/* Dump / query helpers for testing (maybe remove later) */
typedef void (*lk_write_fn)(void *ud, const char *bytes, lk_u32 len);

/* dump tree as readable s-expression-ish format */
void lk_tree_dump(const lk_tree *t, lk_write_fn wr, void *wr_ud);

/* very tiny selected for version 0: find node by exact id */
lk_ix lk_tree_find_by_id(const lk_tree *t, lk_node_id id);

/* Prop schema rules
 *
 * For now, keep schema rules as data tables
 * - for each kind: allowed keys + required keys + expected types
 *
 * TODO better later
 */

typedef struct lk_prop_rule {
  lk_u16 key; /* lk_prop_key */
  lk_u8 expected_tag; /* value tag */
  lk_u8 required; /* 0/1 */
} lk_prop_rule;


typedef struct lk_kind_schema {
  lk_u16 kind; /* lk_kind */
  const lk_prop_rule *rules; /* array */
  lk_u16 rule_count;
} lk_kind_schema;

/* Temporary: for MPV provide built-in default schema for MVP kinds */
lk_kind_schema lk_default_schema(lk_u32 *out_count);

/* Optionally validate against a schema table. */
int lk_tree_validate_schema(const lk_tree *t,
                            const lk_kind_schema* schema,
                            lk_u32 schema_count,
                            lk_diag* diags,
                            lk_u32 diags_cap,
                            lk_u32* out_diags_len);



/**
 * lk_ui — Double-buffered UI context with tree diffing.
 *
 * Owns two trees (prev, next) sharing a single intern table.
 * Each frame: begin_frame resets `next`, host builds into it,
 * end_frame diffs prev vs next and swaps.
 **/

typedef enum lk_change_kind {
  LK_CHANGE_ADDED = 1,
  LK_CHANGE_REMOVED,
  LK_CHANGE_UPDATED
} lk_change_kind;

typedef struct lk_change {
  lk_u8 kind; /* lk_change_kind */
  lk_node_id id; /* node identity */
  lk_ix node_ix; /* index in current tree (0 for REMOVED) */
} lk_change;

typedef struct lk_changeset {
  lk_change *changes;
  lk_u32 count;
  lk_u32 cap;
} lk_changeset;

typedef struct lk_ui {
  lk_intern *intern;
  lk_tree *prev;
  lk_tree *next;
  lk_changeset changeset;
  void *(*alloc)(void *ud, lk_u32 bytes);
  void (*dealloc)(void *ud, void *ptr);
  void *alloc_ud;
} lk_ui;

typedef struct lk_ui_cfg {
  void *(*alloc)(void *ud, lk_u32 bytes);
  void (*dealloc)(void *ud, void *ptr);
  void *ud;
  lk_u32 node_cap_hint;
  lk_u32 prop_cap_hint;
} lk_ui_cfg;

lk_ui *lk_ui_create(const lk_ui_cfg *cfg);
void lk_ui_destroy(lk_ui *ui);

/* Reset next tree and return it for building. */
lk_tree *lk_ui_begin_frame(lk_ui *ui);

/* Diff prev vs next, swap, return changeset.
 * Changeset is valid until the next lk_ui_end_frame call.
 * ADDED/UPDATED node_ix values are indices into the current tree.
 * REMOVED entries have node_ix = 0.
 */
const lk_changeset *lk_ui_end_frame(lk_ui *ui);

/* Return the current tree (valid after end_frame, before next begin_frame). */
const lk_tree *lk_ui_tree(const lk_ui *ui);


/**
 * lk_ht - Hash Table
 **/
typedef struct lk_ht lk_ht;

lk_ht *lk_ht_new(size_t elem_size);
int lk_ht_delete(lk_ht *ht);
int lk_ht_get(lk_ht *ht, const char *key, void **out);
const char *lk_ht_set(lk_ht *ht, const char *key, void *value);
size_t lk_ht_length(lk_ht *ht);

/**
 * lk_hti
 **/

typedef struct lk_hti {
  const char *key;
  void *value;
  lk_ht *ht;
  size_t index;
} lk_hti;

lk_hti lk_hti_iterator(lk_ht *ht);
int lk_hti_next(lk_hti *hti);

#endif
