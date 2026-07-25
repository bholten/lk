#ifndef LK_H
#define LK_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Fixed-width integer types for C89.
 *
 * unsigned char   is >= 8 bits on all conforming implementations.
 * unsigned short  is >= 16 bits on all conforming implementations.
 * unsigned int    is >= 16 bits by the standard, but 32 bits on every
 *                 platform we realistically target (ILP32, LP64, LLP64).
 * int             same — 32 bits everywhere relevant.
 *
 * If you ever need to port to a platform where unsigned int is not 32 bits,
 * add a check here.
 */

typedef unsigned char lk_u8;
typedef unsigned short lk_u16;
typedef unsigned int lk_u32;
typedef int lk_i32;

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
typedef struct lk_state lk_state;

lk_intern *lk_intern_new(void *(alloc)(void *, lk_u32), void *alloc_ud);
void lk_intern_destroy(lk_intern *it);

lk_node_id lk_intern_id(lk_intern *it, lk_str s); /* return stable id */
lk_str lk_intern_str(const lk_intern *it, lk_node_id id);
lk_node_id lk_intern_cid(lk_intern *it, const char *s);
const char *lk_intern_cstr(const lk_intern *it, lk_node_id id);

typedef enum lk_kind {
  UIK_WINDOW = 1,
  UIK_ROW,
  UIK_COLUMN,
  UIK_SPACER,
  UIK_LABEL,
  UIK_BUTTON,
  UIK_TEXT_INPUT,
  UIK_SCROLL,
  UIK_DROPDOWN, /* selection list — trigger renders collapsed; options
                   become a popup overlay when LKS_EXPANDED == 1. */
  UIK_OPTION,   /* option inside a dropdown.  Not laid out by the main
                   pass; rendered and hit-tested as part of the owning
                   dropdown's overlay. */
  UIK__COUNT
} lk_kind;

typedef enum lk_prop_key {
  UIP_TEXT = 1,  /* string */
  UIP_FOCUSABLE, /* bool */
  UIP_DISABLED,  /* bool */

  UIP_W,       /* int px */
  UIP_H,       /* int px */
  UIP_PADDING, /* int px */
  UIP_GAP,     /* int px */

  UIP_ALIGN,   /* i32: lk_align — cross-axis alignment */
  UIP_JUSTIFY, /* i32: lk_align — main-axis alignment */

  UIP__COUNT
} lk_prop_key;

typedef enum lk_state_key {
  LKS_SCROLL_X = 1,
  LKS_SCROLL_Y,
  LKS_CURSOR_POS,
  LKS_SELECTION_START,
  LKS_SELECTION_END,
  LKS_EXPANDED,
  LKS_TEXT_BUF,
  LKS_CURSOR_X,
  LKS_SCROLL_MAX,
  LKS_SELECTED_INDEX, /* dropdown: index of currently selected option */
  LKS_HOVER_INDEX,    /* dropdown: index of option under cursor while open */
  LKS__BUILTIN_COUNT,
  LKS_USER = 256
} lk_state_key;

typedef enum lk_align {
  LK_ALIGN_START = 0,
  LK_ALIGN_CENTER,
  LK_ALIGN_END,
  LK_ALIGN_STRETCH /* default behavior for cross-axis */
} lk_align;

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
 *   - first_child / next_sibling: adjacency (compact, no per-node dynamic
 * arrays)
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

/**
 * Presentation — attaches semantic meaning to a node.
 *
 * A presentation carries up to LK_PRES_MAX_ARGS values.  When a translator
 * matches, the presentation's pvalues are copied into the emitted command's
 * args slot.  A single-value presentation (count=1) is the common case.
 **/

#define LK_PRES_MAX_ARGS 4

typedef struct lk_presentation {
  lk_ix node;                         /* which node this is attached to */
  lk_u32 ptype;                       /* interned presentation type */
  lk_u8 pvalue_count;                 /* number of valid slots in pvalues */
  lk_value pvalues[LK_PRES_MAX_ARGS]; /* semantic value(s) */
} lk_presentation;

/**
 * Tag — attaches a named tag to a node for style matching.
 **/
typedef struct lk_tag {
  lk_ix node;
  lk_u32 tag_id;
} lk_tag;

typedef struct lk_tree {
  lk_intern *intern;
  lk_u8 owns_intern; /* 1 if tree created the intern table */
  lk_node *nodes;
  lk_u32 node_cap;
  lk_u32 node_count;
  lk_prop *props;
  lk_u32 prop_cap;
  lk_u32 prop_count;
  lk_presentation *pres;
  lk_u32 pres_cap;
  lk_u32 pres_count;
  lk_tag *tags;
  lk_u32 tag_count;
  lk_u32 tag_cap;
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

/* Create a node with id+kind, append to arena, return node index
 * (1..N).  Caller wires parent/children via lk_tree_append_child.
 */
lk_ix lk_tree_add_node(lk_tree *t, lk_node_id id, lk_kind kind);

/* Convenience: create node by string id via intern (must not be
   NULL). */
lk_ix lk_tree_add_node_s(lk_tree *t, lk_str id_str, lk_kind kind);

/* Create node by C string id (no lk_str needed). */
lk_ix lk_tree_add_node_c(lk_tree *t, const char *id_str, lk_kind kind);

/* Set root node index. */
void lk_tree_set_root(lk_tree *t, lk_ix root);

/* Append child to parent's child list (preserves insertion order). */
void lk_tree_append_child(lk_tree *t, lk_ix parent, lk_ix child);

/* Set/append a prop on a node.
 *
 * Phase 0: allows duplicates; validation can flag duplicates later if
 * desired.
 */
void lk_tree_add_prop(lk_tree *t, lk_ix node, lk_prop_key key, lk_value v);

/* Presentations — attach semantic meaning to a node.  Single-value
 * forms are convenience wrappers over the multi-value (_v) forms. */
void lk_tree_add_presentation(lk_tree *t, lk_ix node, lk_u32 ptype,
                              lk_value pvalue);
void lk_tree_add_presentation_s(lk_tree *t, lk_ix node, const char *ptype,
                                lk_value pvalue);
void lk_tree_add_presentation_v(lk_tree *t, lk_ix node, lk_u32 ptype,
                                const lk_value *pvalues, lk_u8 count);
void lk_tree_add_presentation_sv(lk_tree *t, lk_ix node, const char *ptype,
                                 const lk_value *pvalues, lk_u8 count);
const lk_presentation *lk_tree_get_presentation(const lk_tree *t, lk_ix node);

/* Tags — attach named tags to nodes for style matching. */
void lk_tree_add_tag(lk_tree *t, lk_ix node, lk_u32 tag_id);
void lk_tree_add_tag_s(lk_tree *t, lk_ix node, const char *tag);
int lk_tree_has_tag(const lk_tree *t, lk_ix node, lk_u32 tag_id);

/* Value constructors. */
lk_value lk_v_none(void);
lk_value lk_v_bool(int b);
lk_value lk_v_i32(lk_i32 i);
lk_value lk_v_str(lk_intern *it, lk_str s);
lk_value lk_v_cstr(lk_intern *it, const char *cstr);

/* Validation */
typedef enum lk_diag_kind { UID_NONE = 0, UID_ERROR, UID_WARN } lk_diag_kind;

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
  lk_u16 key;         /* lk_prop_key */
  lk_u8 expected_tag; /* value tag */
  lk_u8 required;     /* 0/1 */
} lk_prop_rule;

typedef struct lk_kind_schema {
  lk_u16 kind;               /* lk_kind */
  const lk_prop_rule *rules; /* array */
  lk_u16 rule_count;
} lk_kind_schema;

/* Temporary: for MPV provide built-in default schema for MVP kinds */
lk_kind_schema lk_default_schema(lk_u32 *out_count);

/* Optionally validate against a schema table. */
int lk_tree_validate_schema(const lk_tree *t, const lk_kind_schema *schema,
                            lk_u32 schema_count, lk_diag *diags,
                            lk_u32 diags_cap, lk_u32 *out_diags_len);

/**
 * Events — types
 **/

typedef enum lk_event_type {
  LK_EVENT_NONE = 0,
  LK_EVENT_POINTER_MOVE,
  LK_EVENT_POINTER_DOWN,
  LK_EVENT_POINTER_UP,
  LK_EVENT_KEY_DOWN,
  LK_EVENT_KEY_UP,
  LK_EVENT_TEXT,
  LK_EVENT_WHEEL,
  LK_EVENT_WINDOW_RESIZE,
  LK_EVENT_WINDOW_CLOSE,
  /* Synthetic — emitted by widgets when their model value changes
   * (text input buffer mutation, dropdown selection change, ...).
   * data.value_changed.str_id carries the new value as an interned string. */
  LK_EVENT_VALUE_CHANGED,
  LK_EVENT__COUNT
} lk_event_type;

typedef enum lk_event_phase {
  LK_PHASE_CAPTURE = 1,
  LK_PHASE_TARGET,
  LK_PHASE_BUBBLE
} lk_event_phase;

#define LK_MOD_SHIFT 0x01u
#define LK_MOD_CTRL 0x02u
#define LK_MOD_ALT 0x04u
#define LK_MOD_GUI 0x08u

typedef enum lk_keycode {
  LKK_UNKNOWN = 0,
  LKK_TAB,
  LKK_RETURN,
  LKK_ESCAPE,
  LKK_BACKSPACE,
  LKK_DELETE,
  LKK_SPACE,
  LKK_LEFT,
  LKK_RIGHT,
  LKK_UP,
  LKK_DOWN,
  LKK_HOME,
  LKK_END,
  LKK_A, LKK_B, LKK_C, LKK_D, LKK_E, LKK_F, LKK_G, LKK_H, LKK_I,
  LKK_J, LKK_K, LKK_L, LKK_M, LKK_N, LKK_O, LKK_P, LKK_Q, LKK_R,
  LKK_S, LKK_T, LKK_U, LKK_V, LKK_W, LKK_X, LKK_Y, LKK_Z,
  LKK__COUNT
} lk_keycode;

typedef struct lk_event {
  lk_u8 type;    /* lk_event_type */
  lk_u8 phase;   /* lk_event_phase (set during routing) */
  lk_u8 mods;    /* LK_MOD_* bit flags */
  lk_u8 handled; /* set to 1 to stop propagation */
  lk_ix target;  /* target node (from hit-test or focus) */
  union {
    struct {
      lk_i32 x, y;
      lk_u8 button;
    } pointer;
    struct {
      lk_u16 keycode;
      lk_u8 repeat;
    } key;
    struct {
      char buf[32];
      lk_u8 len;
    } text;
    struct {
      lk_i32 dx, dy;
    } wheel;
    struct {
      lk_i32 w, h;
    } window;
    struct {
      lk_u32 str_id; /* interned new value */
    } value_changed;
  } data;
} lk_event;

typedef int (*lk_event_handler_fn)(lk_event *event, lk_ix node_ix, void *ud);

/**
 * Clipboard callbacks — optional, platform-specific.
 * Widget code checks for NULL before calling.
 **/
typedef const char *(*lk_clipboard_get_fn)(void *ud);
typedef void (*lk_clipboard_set_fn)(void *ud, const char *text);

/**
 * Commands — named actions emitted by translators.
 **/

#define LK_CMD_MAX_ARGS 4

typedef struct lk_command {
  lk_u32 name; /* interned command name */
  lk_value args[LK_CMD_MAX_ARGS];
  lk_u8 arg_count;
  lk_ix source_node;   /* node that triggered this */
  lk_u32 source_ptype; /* matched presentation type (0 if none) */
  lk_value source_value; /* event-carried value (e.g. new text for
                          * value_changed); UIV_NONE for events with
                          * no intrinsic value. */
} lk_command;

typedef struct lk_command_queue {
  lk_command *cmds;
  lk_u32 count;
  lk_u32 cap;
} lk_command_queue;

typedef void (*lk_command_handler_fn)(const lk_command *cmd, void *ud);

/**
 * Translators — map (event_type, ptype?, node_kind?, keycode?, mods?) to
 * command name.  When keycode != 0, the translator only matches key events
 * with that exact keycode and modifier combination.
 **/

typedef struct lk_translator {
  lk_u8 event_type;    /* lk_event_type to match (0 = any) */
  lk_u32 ptype;        /* presentation type to match (0 = any) */
  lk_u16 node_kind;    /* lk_kind to match (0 = any) */
  lk_u16 keycode;      /* lk_keycode to match (0 = any) */
  lk_u8 mods;          /* LK_MOD_* bits; exact match when keycode != 0 */
  lk_u32 command_name; /* interned command name to emit */
} lk_translator;

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
  lk_u8 kind;    /* lk_change_kind */
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
  lk_event_handler_fn event_handler;
  void *event_ud;
  lk_node_id focused_id; /* 0 = no focus */
  lk_node_id hovered_id; /* 0 = nothing hovered */
  lk_state *state;       /* retained per-node state */

  /* Translators */
  lk_translator *translators;
  lk_u32 translator_count;
  lk_u32 translator_cap;

  /* Command queue (current frame) */
  lk_command_queue cmd_queue;
  lk_command_handler_fn cmd_handler;
  void *cmd_handler_ud;

  /* Command log (append-only, cleared explicitly) */
  lk_command *cmd_log;
  lk_u32 cmd_log_count;
  lk_u32 cmd_log_cap;

  /* Style system */
  struct lk_theme *theme;
  struct lk_style *styles;
  lk_u32 styles_cap;
  lk_u8 *node_states;
  lk_u32 nstates_cap;

  /* Clipboard (optional, platform-specific) */
  lk_clipboard_get_fn clipboard_get;
  lk_clipboard_set_fn clipboard_set;
  void *clipboard_ud;
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
 *
 * Changeset is valid until the next lk_ui_end_frame call.
 * ADDED/UPDATED node_ix values are indices into the current tree.
 * REMOVED entries have node_ix = 0.
 */
const lk_changeset *lk_ui_end_frame(lk_ui *ui);

/* Return the current tree (valid after end_frame, before next
   begin_frame). */
const lk_tree *lk_ui_tree(const lk_ui *ui);

/* Style system integration */
void lk_ui_set_theme(lk_ui *ui, struct lk_theme *th);
struct lk_theme *lk_ui_theme(lk_ui *ui);
const struct lk_style *lk_ui_styles(const lk_ui *ui);
void lk_ui_resolve_styles(lk_ui *ui);

/**
 * Node prop query helpers
 **/

lk_i32 lk_node_prop_i32(const lk_tree *t, lk_ix n, lk_prop_key key, lk_i32 def);
int lk_node_has_prop(const lk_tree *t, lk_ix n, lk_prop_key key);
int lk_node_prop_bool(const lk_tree *t, lk_ix n, lk_prop_key key);
lk_str lk_node_text(const lk_tree *t, lk_ix n);
lk_u32 lk_node_text_id(const lk_tree *t, lk_ix n);
const char *lk_node_text_cstr(const lk_tree *t, lk_ix n);

/**
 * Binding-safe accessors — expose struct fields through function calls.
 * All null-safe with bounds checking; return 0/NULL on failure.
 **/

/* Node field accessors (tree + index -> field) */
lk_node_id lk_node_id_get(const lk_tree *t, lk_ix n);
lk_u16 lk_node_kind_get(const lk_tree *t, lk_ix n);
lk_ix lk_node_parent(const lk_tree *t, lk_ix n);
lk_ix lk_node_first_child(const lk_tree *t, lk_ix n);
lk_ix lk_node_next_sibling(const lk_tree *t, lk_ix n);

/* Tree accessors */
lk_u32 lk_tree_node_count(const lk_tree *t);
lk_ix lk_tree_root(const lk_tree *t);
lk_intern *lk_tree_intern(const lk_tree *t);

/* UI accessors */
lk_intern *lk_ui_intern(const lk_ui *ui);

/**
 * Retained state store — per-node state that persists across frames.
 * Keyed by (lk_node_id, lk_u16 state_key).
 * Automatically garbage-collects entries when nodes are REMOVED.
 **/

lk_state *lk_ui_state(lk_ui *ui);

int lk_state_set(lk_state *st, lk_node_id node, lk_u16 key, lk_value v);
lk_value lk_state_get(const lk_state *st, lk_node_id node, lk_u16 key);
void lk_state_remove_node(lk_state *st, lk_node_id node);

/* Changeset accessors */
lk_u32 lk_changeset_count(const lk_changeset *cs);
const lk_change *lk_changeset_get(const lk_changeset *cs, lk_u32 idx);

/* Command queue accessors */
lk_u32 lk_command_queue_count(const lk_command_queue *q);
const lk_command *lk_command_queue_get(const lk_command_queue *q, lk_u32 idx);

/* Command field accessors */
lk_u32 lk_command_name(const lk_command *cmd);
lk_u8 lk_command_arg_count(const lk_command *cmd);
lk_value lk_command_arg(const lk_command *cmd, lk_u8 idx);
lk_u8 lk_command_arg_tag(const lk_command *cmd, lk_u8 idx);
lk_i32 lk_command_arg_i32(const lk_command *cmd, lk_u8 idx);
lk_u32 lk_command_arg_str_id(const lk_command *cmd, lk_u8 idx);
lk_ix lk_command_source_node(const lk_command *cmd);
lk_u32 lk_command_source_ptype(const lk_command *cmd);
lk_value lk_command_source_value(const lk_command *cmd);

/**
 * Layout
 **/

typedef struct lk_rect {
  lk_i32 x, y, w, h;
} lk_rect;
typedef struct lk_size {
  lk_i32 w, h;
} lk_size;

typedef void (*lk_measure_text_fn)(void *ud, lk_str text, lk_i32 *out_w,
                                   lk_i32 *out_h);

typedef struct lk_layout_cfg {
  lk_measure_text_fn measure_text;
  void *measure_ud;
  lk_i32 viewport_w, viewport_h;
  const struct lk_style *styles; /* NULL = read tree props directly */
  lk_state *state;               /* NULL ok; widgets may use for cursor etc. */
} lk_layout_cfg;

/* Compute layout rects for every node in the tree. rects[] must be
 * at least t->node_count elements (indexed by lk_ix).  Returns 1 on
 * success, 0 on failure.
 */
int lk_layout(const lk_tree *t, const lk_layout_cfg *cfg, lk_rect *rects);

/* Stub text measurer: 8px per char, 16px tall. */
void lk_measure_text_stub(void *ud, lk_str text, lk_i32 *out_w, lk_i32 *out_h);

/* Convenience: layout with stub text measurer (for bindings). */
int lk_layout_simple(const lk_tree *t, lk_i32 viewport_w, lk_i32 viewport_h,
                     lk_rect *rects);

/**
 * Render list — flat display list for renderer consumption.
 **/

typedef struct lk_color {
  lk_u8 r, g, b, a;
} lk_color;

/**
 * Style system — resolved appearance for a node.
 **/

typedef struct lk_style {
  lk_color fg, bg, border_color;
  lk_color scrollbar_track, scrollbar_thumb;
  lk_u32 font_id;
  lk_i32 font_size, padding, gap, border_width, border_radius;
  lk_u8 align, justify;
} lk_style;

/* Field mask bits */
#define LK_SF_FG (1u << 0)
#define LK_SF_BG (1u << 1)
#define LK_SF_FONT_ID (1u << 2)
#define LK_SF_FONT_SIZE (1u << 3)
#define LK_SF_PADDING (1u << 4)
#define LK_SF_GAP (1u << 5)
#define LK_SF_BORDER_WIDTH (1u << 6)
#define LK_SF_BORDER_COLOR (1u << 7)
#define LK_SF_BORDER_RADIUS (1u << 8)
#define LK_SF_ALIGN (1u << 9)
#define LK_SF_JUSTIFY (1u << 10)
#define LK_SF_SCROLLBAR_TRACK (1u << 11)
#define LK_SF_SCROLLBAR_THUMB (1u << 12)

/* Inheritable fields: fg, font_id, font_size */
#define LK_STYLE_INHERIT_MASK (LK_SF_FG | LK_SF_FONT_ID | LK_SF_FONT_SIZE)

/* Node interaction state bits */
#define LK_NSTATE_FOCUSED (1u << 0)
#define LK_NSTATE_HOVERED (1u << 1)
#define LK_NSTATE_DISABLED (1u << 2)

typedef struct lk_theme lk_theme;

lk_theme *lk_theme_new(void *(*alloc)(void *, lk_u32),
                       void (*dealloc)(void *, void *), void *ud);
void lk_theme_destroy(lk_theme *th);
void lk_theme_add_rule(lk_theme *th, lk_u16 kind, lk_u32 tag_id,
                       lk_u8 state_mask, const lk_style *style,
                       lk_u32 field_mask);
lk_theme *lk_theme_default(void *(*alloc)(void *, lk_u32),
                            void (*dealloc)(void *, void *), void *ud);

void lk_style_resolve(const lk_theme *th, const lk_tree *t,
                      const lk_u8 *node_states, lk_style *styles);

/* Style tracing */
typedef struct lk_style_trace_entry {
  lk_u32 rule_index, field_mask;
} lk_style_trace_entry;
typedef struct lk_style_trace {
  lk_style_trace_entry *entries;
  lk_u32 count, cap;
} lk_style_trace;
void lk_style_trace_node(const lk_theme *th, const lk_tree *t, lk_ix node,
                         lk_u8 node_state, lk_style_trace *out);

typedef enum lk_render_op {
  LK_ROP_FILL_RECT = 1,
  LK_ROP_DRAW_TEXT,
  LK_ROP_CLIP_BEGIN,
  LK_ROP_CLIP_END
} lk_render_op;

typedef struct lk_render_cmd {
  lk_u8 op; /* lk_render_op */
  lk_rect rect;
  lk_color color;
  lk_u32 str_id; /* for DRAW_TEXT: interned string ID */
} lk_render_cmd;

typedef struct lk_render_list {
  lk_render_cmd *cmds;
  lk_u32 count;
  lk_u32 cap;
} lk_render_list;

/* Build a render list from a laid-out tree.  rects[] indexed by lk_ix
 * (from lk_layout).  Reuses existing capacity in out; resets count.
 * state may be NULL; passed through to widget render functions.
 * Returns 1 on success, 0 on failure.
 */
int lk_render_build(const lk_tree *t, const lk_rect *rects,
                    const lk_style *styles, const lk_state *state,
                    lk_render_list *out);

/* Append overlay render commands (currently: expanded dropdowns) to
 * an existing render list.  Call after lk_render_build so overlays
 * draw on top of the main tree.  See docs/overlays.md for the design
 * roadmap toward a general overlay system. */
int lk_render_build_overlays(const lk_tree *t, const lk_rect *rects,
                             const lk_style *styles, const lk_state *state,
                             const lk_layout_cfg *cfg, lk_render_list *out);

/* Push a command to the render list (grows as needed). Returns 1 on
 * success, 0 on allocation failure. */
int lk_render_list_push(lk_render_list *rl, lk_render_cmd cmd);

/* Free the cmds array. Safe to call on a zeroed struct. */
void lk_render_list_destroy(lk_render_list *rl);

/**
 * Widget definition — per-kind vtable for measure, layout, render.
 **/

#define LK_KIND_MAX 32

#define LK_TEXT_INPUT_MAX 1024

typedef struct lk_widget_def {
  void (*measure)(const lk_tree *t, lk_ix n, const lk_size *sizes,
                  const lk_layout_cfg *cfg, lk_i32 *out_w, lk_i32 *out_h);

  /* Position children within content rect (parent rect minus padding).
   * Returns 1 if children need recursive layout, 0 for leaves. */
  int (*layout)(const lk_tree *t, lk_ix n, const lk_size *sizes,
                const lk_rect *content, const lk_layout_cfg *cfg,
                lk_rect *rects);

  void (*render)(const lk_tree *t, lk_ix n, const lk_rect *rect,
                 const lk_style *style, const lk_state *state,
                 lk_render_list *out);

  /* Widget-level event handler. Called at TARGET phase before the global
   * handler. Return 1 if handled (stops propagation), 0 to continue. */
  int (*event)(lk_ui *ui, const lk_tree *t, lk_ix n, lk_event *ev);

  lk_u8 clips; /* 1 if this node clips children */
} lk_widget_def;

void lk_widget_register(lk_kind kind, const lk_widget_def *def);
const lk_widget_def *lk_widget_get(lk_kind kind);

/**
 * Events — function declarations
 **/

lk_ix lk_hit_test(const lk_tree *t, const lk_rect *rects, lk_i32 x, lk_i32 y);

/* Overlay-aware hit-test: checks any active overlays (e.g. expanded
 * dropdown popups) before falling back to the main tree.  Returns the
 * option/overlay-content node when a popup is hit, or the normal
 * hit-test result otherwise.  Pass state so the hit-tester knows which
 * overlays are open; pass cfg so it knows how to measure option heights.
 */
lk_ix lk_hit_test_overlay(const lk_tree *t, const lk_rect *rects,
                          const lk_style *styles, const lk_state *state,
                          const lk_layout_cfg *cfg, lk_i32 x, lk_i32 y);

/* Close any expanded overlay whose popup does NOT contain (x,y).
 * Call before routing a pointer-down event so clicks outside an open
 * dropdown dismiss it.  Returns 1 if any overlay was dismissed. */
int lk_overlay_dismiss_outside(lk_ui *ui, const lk_rect *rects,
                                const lk_style *styles,
                                const lk_layout_cfg *cfg, lk_i32 x, lk_i32 y);

void lk_event_init_pointer(lk_event *ev, lk_u8 type, lk_i32 x, lk_i32 y,
                           lk_u8 button);
void lk_event_init_key(lk_event *ev, lk_u8 type, lk_u16 keycode, lk_u8 mods);

void lk_event_route(lk_ui *ui, lk_event *event);

void lk_ui_set_event_handler(lk_ui *ui, lk_event_handler_fn fn, void *ud);

/* Install clipboard callbacks (e.g. from SDL backend). */
void lk_ui_set_clipboard(lk_ui *ui, lk_clipboard_get_fn get_fn,
                         lk_clipboard_set_fn set_fn, void *ud);

int lk_focus_set(lk_ui *ui, const lk_tree *t, lk_node_id id);
void lk_focus_clear(lk_ui *ui);
lk_node_id lk_focus_next(lk_ui *ui, const lk_tree *t);
lk_node_id lk_focus_prev(lk_ui *ui, const lk_tree *t);
lk_ix lk_focus_current(const lk_ui *ui, const lk_tree *t);

void lk_hover_set(lk_ui *ui, lk_node_id id);
void lk_hover_clear(lk_ui *ui);

/**
 * Translator + command API
 **/

void lk_ui_add_translator(lk_ui *ui, lk_u8 event_type, lk_u32 ptype,
                          lk_u16 node_kind, lk_u16 keycode, lk_u8 mods,
                          lk_u32 command_name);
void lk_ui_add_translator_s(lk_ui *ui, lk_u8 event_type, const char *ptype,
                            lk_u16 node_kind, lk_u16 keycode, lk_u8 mods,
                            const char *command_name);
void lk_ui_clear_translators(lk_ui *ui);

void lk_ui_set_command_handler(lk_ui *ui, lk_command_handler_fn fn, void *ud);

const lk_command_queue *lk_ui_commands(const lk_ui *ui);
void lk_ui_clear_commands(lk_ui *ui);

void lk_ui_dump_commands(const lk_ui *ui, lk_write_fn wr, void *wr_ud);
const lk_command *lk_ui_command_log(const lk_ui *ui, lk_u32 *out_count);
void lk_ui_clear_command_log(lk_ui *ui);

/* Internal: translator dispatch (called from event routing) */
void lk_translate_event(lk_ui *ui, const lk_tree *t, lk_event *event);

#ifdef __cplusplus
}
#endif

#endif /* LK_H */
