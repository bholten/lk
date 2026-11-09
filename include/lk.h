#ifndef LK_H
#define LK_H

#define LK_VERSION_MAJOR 0
#define LK_VERSION_MINOR 1
#define LK_VERSION_PATCH 0
#define LK_VERSION_STRING "0.1.0"

#include <limits.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef unsigned char lk_u8;
typedef unsigned short lk_u16;
typedef unsigned int lk_u32;
typedef int lk_i32;

#if defined(__GNUC__)
#define LK_EXTENSION __extension__
#else
#define LK_EXTENSION
#endif

#if LONG_MAX > 0x7FFFFFFFL
typedef long lk_i64;
typedef unsigned long lk_u64;
#else
LK_EXTENSION typedef long long lk_i64;
LK_EXTENSION typedef unsigned long long lk_u64;
#endif

typedef char lk_i64_must_be_8_bytes[sizeof(lk_i64) == 8 ? 1 : -1];
typedef char lk_u64_must_be_8_bytes[sizeof(lk_u64) == 8 ? 1 : -1];

typedef struct lk_str {
  const char *ptr;
  lk_u32 len;
} lk_str;

lk_str lk_str_c(const char *cstr);
int lk_str_cmp(lk_str a, lk_str b);

typedef struct lk_rect {
  lk_i32 x, y, w, h;
} lk_rect;
typedef struct lk_size {
  lk_i32 w, h;
} lk_size;

typedef lk_u32 lk_node_id;

typedef struct lk_intern lk_intern;
typedef struct lk_state lk_state;
typedef struct lk_resources lk_resources;

lk_intern *lk_intern_new(void *(*alloc)(void *, lk_u32),
                         void (*dealloc)(void *, void *), void *alloc_ud);
void lk_intern_destroy(lk_intern *it);

lk_node_id lk_intern_id(lk_intern *it, lk_str s);
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
  UIK_DROPDOWN,
  UIK_OPTION,
  UIK_SPLIT_H,
  UIK_SPLIT_V,
  UIK_EDITOR,
  UIK_CHECKBOX,
  UIK_RADIO,
  UIK_SLIDER,
  UIK_TABS,
  UIK_TAB,
  UIK_GRID,
  UIK_IMAGE,
  UIK_CANVAS,
  UIK_STYLED_TEXT,
  UIK_LIST,
  UIK__COUNT
} lk_kind;

typedef enum lk_prop_key {
  UIP_TEXT = 1,
  UIP_FOCUSABLE,
  UIP_DISABLED,
  UIP_W,
  UIP_H,
  UIP_PADDING,
  UIP_GAP,
  UIP_ALIGN,
  UIP_JUSTIFY,
  UIP_HIDDEN,
  UIP_TOOLTIP,
  UIP_SPLIT_RATIO,
  UIP_EDITOR,
  UIP_GROW,
  UIP_VALUE,
  UIP_CONTROLLED,
  UIP_CHECKED,
  UIP_MIN,
  UIP_MAX,
  UIP_STEP,
  UIP_COLUMNS,
  UIP_IMAGE,
  UIP_FILTER,
  UIP_CANVAS,
  UIP_TEXT_ALIGN,
  UIP_TEXT_VALIGN,
  UIP_SPANS,
  UIP_WRAP,
  UIP_ROWS,
  UIP_ROW_H,
  UIP_ROW,
  UIP__COUNT
} lk_prop_key;

typedef enum lk_image_filter {
  LK_FILTER_LINEAR = 0,
  LK_FILTER_NEAREST = 1
} lk_image_filter;

#define UIP_SPLIT_CONTROLLED UIP_CONTROLLED

typedef enum lk_state_key {
  LKS_SCROLL_X = 1,
  LKS_SCROLL_Y,
  LKS_CURSOR_POS,
  LKS_SELECTION_START,
  LKS_SELECTION_END,
  LKS_EXPANDED,
  LKS_TEXT_BUF,
  LKS_SELECTED_INDEX,
  LKS_HOVER_INDEX,
  LKS_FOCUSED,
  LKS_SPLIT_RATIO,
  LKS_SPLIT_DRAGGING,
  LKS_POPUP_SCROLL,
  LKS_CHECKED,
  LKS_SLIDER_VALUE,
  LKS_CURSOR_ROW,
  LKS_LIST_DRAGGING,
  LKS_SLIDER_DRAGGING,
  LKS__BUILTIN_COUNT,
  LKS_USER = 256
} lk_state_key;

typedef enum lk_align {
  LK_ALIGN_START = 0,
  LK_ALIGN_CENTER,
  LK_ALIGN_END,
  LK_ALIGN_STRETCH
} lk_align;

typedef enum lk_value_tag {
  UIV_NONE = 0,
  UIV_BOOL,
  UIV_I32,
  UIV_STR,
  UIV_RESOURCE,
  UIV_TEXT
} lk_value_tag;

typedef struct lk_value {
  lk_value_tag tag;
  union {
    lk_u8 b;
    lk_u32 i;
    lk_u32 str_id;
    struct {
      lk_u32 id, gen;
    } res;
    struct {
      lk_u32 off, len;
    } text;
  } as;
} lk_value;

typedef struct lk_prop {
  lk_u16 key;
  lk_value value;
} lk_prop;

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

#define LK_PRES_MAX_ARGS 4

typedef struct lk_presentation {
  lk_ix node;
  lk_u32 ptype;
  lk_u8 pvalue_count;
  lk_value pvalues[LK_PRES_MAX_ARGS];
} lk_presentation;

typedef struct lk_presentation_hit {
  lk_u32 type_id;
  lk_value value;
  lk_u32 locus_kind;
  lk_u32 locus[6];
} lk_presentation_hit;

typedef struct lk_presentation_source {
  void *ud;
  lk_u32 (*query_at)(void *ud, lk_u32 pos, lk_presentation_hit *out,
                     lk_u32 cap);
} lk_presentation_source;

typedef struct lk_tag {
  lk_ix node;
  lk_u32 tag_id;
} lk_tag;

typedef struct lk_tree {
  lk_intern *intern;
  lk_u8 owns_intern;
  lk_resources *resources;
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
void lk_tree_reset(lk_tree *t);

lk_ix lk_tree_add_node(lk_tree *t, lk_node_id id, lk_kind kind);
lk_ix lk_tree_add_node_s(lk_tree *t, lk_str id_str, lk_kind kind);
lk_ix lk_tree_add_node_c(lk_tree *t, const char *id_str, lk_kind kind);

void lk_tree_set_root(lk_tree *t, lk_ix root);
void lk_tree_append_child(lk_tree *t, lk_ix parent, lk_ix child);
void lk_tree_add_prop(lk_tree *t, lk_ix node, lk_prop_key key, lk_value v);
void lk_tree_add_presentation(lk_tree *t, lk_ix node, lk_u32 ptype,
                              lk_value pvalue);

void lk_tree_add_presentation_s(lk_tree *t, lk_ix node, const char *ptype,
                                lk_value pvalue);
void lk_tree_add_presentation_v(lk_tree *t, lk_ix node, lk_u32 ptype,
                                const lk_value *pvalues, lk_u8 count);
void lk_tree_add_presentation_sv(lk_tree *t, lk_ix node, const char *ptype,
                                 const lk_value *pvalues, lk_u8 count);
const lk_presentation *lk_tree_get_presentation(const lk_tree *t, lk_ix node);

void lk_tree_add_tag(lk_tree *t, lk_ix node, lk_u32 tag_id);
void lk_tree_add_tag_s(lk_tree *t, lk_ix node, const char *tag);
int lk_tree_has_tag(const lk_tree *t, lk_ix node, lk_u32 tag_id);

typedef struct lk_resource_ref {
  lk_u32 id;
  lk_u32 generation;
} lk_resource_ref;

lk_value lk_v_none(void);
lk_value lk_v_bool(int b);
lk_value lk_v_i32(lk_i32 i);
lk_value lk_v_str(lk_intern *it, lk_str s);
lk_value lk_v_cstr(lk_intern *it, const char *cstr);
lk_value lk_v_resource(lk_resource_ref ref);

lk_resource_ref lk_v_resource_ref(lk_value v);

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

  const char *msg;
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

typedef void (*lk_write_fn)(void *ud, const char *bytes, lk_u32 len);

void lk_tree_dump(const lk_tree *t, lk_write_fn wr, void *wr_ud);

lk_ix lk_tree_find_by_id(const lk_tree *t, lk_node_id id);

typedef struct lk_resource_type {
  const char *name;

  void (*describe)(void *obj, lk_write_fn w, void *wud);
} lk_resource_type;

lk_resources *lk_resources_new(void *(*alloc)(void *, lk_u32),
                               void (*dealloc)(void *, void *), void *ud);
void lk_resources_destroy(lk_resources *rs);

lk_resource_ref lk_resource_register(lk_resources *rs,
                                     const lk_resource_type *type, void *obj,
                                     const char *debug_name);

void lk_resource_release(lk_resources *rs, lk_resource_ref ref);

void *lk_resource_get(const lk_resources *rs, lk_resource_ref ref,
                      const lk_resource_type *type);

void lk_tree_set_resources(lk_tree *t, lk_resources *rs);

typedef struct lk_image lk_image;

lk_image *lk_image_new(lk_u32 w, lk_u32 h, void *(*alloc)(void *, lk_u32),
                       void (*dealloc)(void *, void *), void *ud);
void lk_image_destroy(lk_image *img);

lk_u8 *lk_image_pixels(lk_image *img);
void lk_image_size(const lk_image *img, lk_u32 *w, lk_u32 *h);

void lk_image_mark_dirty(lk_image *img);
lk_u32 lk_image_generation(const lk_image *img);

const lk_resource_type *lk_image_type(void);

lk_image *lk_image_from_node(const lk_resources *rs, const lk_tree *t, lk_ix n);

typedef struct lk_prop_rule {
  lk_u16 key;
  lk_u8 expected_tag;
  lk_u8 required;
} lk_prop_rule;

typedef struct lk_kind_schema {
  lk_u16 kind;
  const lk_prop_rule *rules;
  lk_u16 rule_count;
} lk_kind_schema;

int lk_tree_validate_schema(const lk_tree *t, const lk_kind_schema *schema,
                            lk_u32 schema_count, lk_diag *diags,
                            lk_u32 diags_cap, lk_u32 *out_diags_len);

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

  LK_EVENT_VALUE_CHANGED,

  LK_EVENT_FOCUS_CHANGED,

  LK_EVENT_PASTE,
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

typedef enum lk_pointer_button {
  LK_POINTER_BUTTON_ANY = 0,
  LK_POINTER_BUTTON_PRIMARY,
  LK_POINTER_BUTTON_MIDDLE,
  LK_POINTER_BUTTON_SECONDARY
} lk_pointer_button;

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
  LKK_A,
  LKK_B,
  LKK_C,
  LKK_D,
  LKK_E,
  LKK_F,
  LKK_G,
  LKK_H,
  LKK_I,
  LKK_J,
  LKK_K,
  LKK_L,
  LKK_M,
  LKK_N,
  LKK_O,
  LKK_P,
  LKK_Q,
  LKK_R,
  LKK_S,
  LKK_T,
  LKK_U,
  LKK_V,
  LKK_W,
  LKK_X,
  LKK_Y,
  LKK_Z,
  LKK_0,
  LKK_1,
  LKK_2,
  LKK_3,
  LKK_4,
  LKK_5,
  LKK_6,
  LKK_7,
  LKK_8,
  LKK_9,
  LKK_PAGEUP,
  LKK_PAGEDOWN,
  LKK_F1,
  LKK_F2,
  LKK_F3,
  LKK_F4,
  LKK_F5,
  LKK_F6,
  LKK_F7,
  LKK_F8,
  LKK_F9,
  LKK_F10,
  LKK_F11,
  LKK_F12,
  LKK__COUNT
} lk_keycode;

typedef struct lk_event {
  lk_u8 type;
  lk_u8 phase;
  lk_u8 mods;
  lk_u8 handled;
  lk_ix target;
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
      lk_u32 str_id;
    } value_changed;
    struct {
      lk_node_id prev_id, next_id;
    } focus;
    struct {
      const char *ptr;
      lk_u32 len;
    } paste;
  } data;
} lk_event;

typedef int (*lk_event_handler_fn)(lk_event *event, lk_ix node_ix, void *ud);

typedef const char *(*lk_clipboard_get_fn)(void *ud);
typedef void (*lk_clipboard_set_fn)(void *ud, const char *text);

#define LK_CMD_MAX_ARGS 4

typedef struct lk_command {
  lk_u32 name;
  lk_value args[LK_CMD_MAX_ARGS];
  lk_u8 arg_count;
  lk_ix source_node;
  lk_u32 source_ptype;
  lk_value source_value;

  lk_presentation_hit hit;

} lk_command;

typedef struct lk_command_queue {
  lk_command *cmds;
  lk_u32 count;
  lk_u32 cap;
  char *bytes;

  lk_u32 bytes_count;
  lk_u32 bytes_cap;
} lk_command_queue;

typedef void (*lk_command_handler_fn)(const lk_command *cmd, void *ud);

typedef struct lk_translator {
  lk_u8 event_type;
  lk_u32 ptype;
  lk_u16 node_kind;
  lk_u16 keycode;
  lk_u8 mods;

  lk_u8 button;

  lk_u32 command_name;
} lk_translator;

typedef enum lk_overlay_kind {
  LK_OVERLAY_DROPDOWN_POPUP = 1,
  LK_OVERLAY_TOOLTIP,
  LK_OVERLAY_CONTEXT_MENU,
  LK_OVERLAY_MODAL
} lk_overlay_kind;

typedef enum lk_anchor_mode {
  LK_ANCHOR_BELOW = 1,
  LK_ANCHOR_ABOVE,
  LK_ANCHOR_AT_CURSOR,
  LK_ANCHOR_CENTER_VIEWPORT
} lk_anchor_mode;

typedef struct lk_overlay {
  lk_u8 kind;
  lk_u8 anchor_mode;
  lk_u8 dismiss_on_outside;
  lk_u8 traps_focus;
  lk_node_id owner_id;
  lk_node_id content_root_id;

  lk_rect offset;

} lk_overlay;

typedef enum lk_change_kind {
  LK_CHANGE_ADDED = 1,
  LK_CHANGE_REMOVED,
  LK_CHANGE_UPDATED
} lk_change_kind;

typedef struct lk_change {
  lk_u8 kind;
  lk_node_id id;
  lk_ix node_ix;
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
  lk_node_id focused_id;
  lk_node_id hovered_id;
  lk_node_id captured_id;
  lk_node_id focus_request_id;
  lk_state *state;
  lk_resources *resources;

  lk_translator *translators;
  lk_u32 translator_count;
  lk_u32 translator_cap;

  lk_command_queue cmd_queue;
  lk_command_handler_fn cmd_handler;
  void *cmd_handler_ud;

  lk_command *cmd_log;
  lk_u32 cmd_log_count;
  lk_u32 cmd_log_cap;
  char *cmd_log_bytes;
  lk_u32 cmd_log_bytes_count;
  lk_u32 cmd_log_bytes_cap;

  struct lk_theme *theme;
  struct lk_style *styles;
  lk_u32 styles_cap;
  lk_u8 *node_states;
  lk_u32 nstates_cap;

  union lk_widget_geom *geom;

  struct lk_menu_state *menu;

  lk_u32 geom_cap;

  lk_rect *rects;
  lk_u32 rects_cap;

  lk_clipboard_get_fn clipboard_get;
  lk_clipboard_set_fn clipboard_set;
  void *clipboard_ud;

  const struct lk_text_backend *text;

  lk_u32 time_ms;

  lk_overlay *overlays;
  lk_u32 overlay_count;
  lk_u32 overlay_cap;

  lk_event *pending;
  lk_u32 pending_count;
  lk_u32 pending_cap;
  lk_u32 route_depth;
  lk_u32 pending_dropped;
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

lk_tree *lk_ui_begin_frame(lk_ui *ui);

const lk_changeset *lk_ui_end_frame(lk_ui *ui);

const lk_tree *lk_ui_tree(const lk_ui *ui);

void lk_ui_set_theme(lk_ui *ui, struct lk_theme *th);
struct lk_theme *lk_ui_theme(lk_ui *ui);
const struct lk_style *lk_ui_styles(const lk_ui *ui);
void lk_ui_resolve_styles(lk_ui *ui);

lk_i32 lk_node_prop_i32(const lk_tree *t, lk_ix n, lk_prop_key key, lk_i32 def);
int lk_node_has_prop(const lk_tree *t, lk_ix n, lk_prop_key key);
int lk_node_prop_bool(const lk_tree *t, lk_ix n, lk_prop_key key);
lk_str lk_node_text(const lk_tree *t, lk_ix n);
lk_u32 lk_node_text_id(const lk_tree *t, lk_ix n);
const char *lk_node_text_cstr(const lk_tree *t, lk_ix n);

lk_node_id lk_node_id_get(const lk_tree *t, lk_ix n);
lk_u16 lk_node_kind_get(const lk_tree *t, lk_ix n);
lk_ix lk_node_parent(const lk_tree *t, lk_ix n);
lk_ix lk_node_first_child(const lk_tree *t, lk_ix n);
lk_ix lk_node_next_sibling(const lk_tree *t, lk_ix n);

lk_u32 lk_tree_node_count(const lk_tree *t);
lk_ix lk_tree_root(const lk_tree *t);
lk_intern *lk_tree_intern(const lk_tree *t);

lk_intern *lk_ui_intern(const lk_ui *ui);

lk_resources *lk_ui_resources(lk_ui *ui);

lk_state *lk_ui_state(lk_ui *ui);

int lk_state_set(lk_state *st, lk_node_id node, lk_u16 key, lk_value v);
lk_value lk_state_get(const lk_state *st, lk_node_id node, lk_u16 key);
void lk_state_remove_node(lk_state *st, lk_node_id node);

lk_u32 lk_changeset_count(const lk_changeset *cs);
const lk_change *lk_changeset_get(const lk_changeset *cs, lk_u32 idx);

lk_u32 lk_command_queue_count(const lk_command_queue *q);
const lk_command *lk_command_queue_get(const lk_command_queue *q, lk_u32 idx);

lk_u32 lk_command_name(const lk_command *cmd);
lk_u8 lk_command_arg_count(const lk_command *cmd);
lk_value lk_command_arg(const lk_command *cmd, lk_u8 idx);
lk_u8 lk_command_arg_tag(const lk_command *cmd, lk_u8 idx);
lk_i32 lk_command_arg_i32(const lk_command *cmd, lk_u8 idx);
lk_u32 lk_command_arg_str_id(const lk_command *cmd, lk_u8 idx);
lk_ix lk_command_source_node(const lk_command *cmd);
lk_u32 lk_command_source_ptype(const lk_command *cmd);
lk_value lk_command_source_value(const lk_command *cmd);

typedef struct lk_text_metrics {
  lk_i32 w, h;
  lk_i32 baseline;
} lk_text_metrics;

typedef struct lk_text_backend {
  void *ud;
  void (*measure)(void *ud, lk_str run, lk_u16 font_id, lk_u16 font_size,
                  lk_text_metrics *out);
  lk_i32 (*x_from_index)(void *ud, lk_str run, lk_u16 font_id, lk_u16 font_size,
                         lk_u32 byte_ix);
  lk_u32 (*index_from_x)(void *ud, lk_str run, lk_u16 font_id, lk_u16 font_size,
                         lk_i32 x);
  lk_i32 (*line_height)(void *ud, lk_u16 font_id, lk_u16 font_size);
  lk_u16 (*register_font)(void *ud, const char *path);
} lk_text_backend;

typedef union lk_widget_geom {
  struct {
    lk_i32 cursor_x, sel_x0, sel_x1;
    lk_i32 origin_x;

    lk_i32 run_w, run_h;
    lk_u16 font_id, font_size;
    lk_u8 placed;
  } text;
  struct {
    lk_i32 w, h;

  } run;
  struct {
    lk_i32 x, y, w, h;
    lk_u16 row_h, inset;
  } trigger;
  struct {
    lk_i32 x, y, w, h;
    lk_i32 div;

  } content;
  struct {
    lk_i32 max;
  } scroll;
  struct {
    lk_i32 x, y, w, h;
  } track;
  struct {
    lk_i32 x, y, w, h;

  } header;
  struct {
    lk_i32 h;
  } strip;
  struct {
    const struct lk_text_backend *tb;

  } styled;
  struct {
    lk_i32 first, count;
    lk_i32 row_h, max;
    lk_i32 x, y, w, h;
    lk_u8 placed;
  } list;
} lk_widget_geom;

typedef struct lk_layout_cfg {
  const lk_text_backend *text;
  lk_i32 viewport_w, viewport_h;
  const struct lk_style *styles;
  lk_state *state;
  lk_widget_geom *geom;

  lk_i32 grid_w, grid_h;

} lk_layout_cfg;

lk_widget_geom *lk_ui_geom(lk_ui *ui);

lk_rect *lk_ui_rects(lk_ui *ui);

int lk_node_rect(const lk_ui *ui, lk_node_id id, lk_rect *out);

int lk_layout(const lk_tree *t, const lk_layout_cfg *cfg, lk_rect *rects);

int lk_layout_subtree(const lk_tree *t, const lk_layout_cfg *cfg,
                      lk_ix subtree_root, lk_i32 origin_x, lk_i32 origin_y,
                      lk_rect *rects);

void lk_rect_snap_grid(lk_rect *r, lk_i32 grid_w, lk_i32 grid_h);

const lk_text_backend *lk_text_backend_stub(void);

void lk_ui_set_text_backend(lk_ui *ui, const lk_text_backend *text);

void lk_ui_set_time_ms(lk_ui *ui, lk_u32 ms);

lk_u32 lk_ui_time_ms(const lk_ui *ui);

int lk_layout_simple(const lk_tree *t, lk_i32 viewport_w, lk_i32 viewport_h,
                     lk_rect *rects);

typedef struct lk_color {
  lk_u8 r, g, b, a;
} lk_color;

typedef struct lk_style {
  lk_color fg, bg, border_color;
  lk_color scrollbar_track, scrollbar_thumb;
  lk_color accent;

  lk_u32 font_id;
  lk_i32 font_size, padding, gap, border_width, border_radius;
  lk_u8 align, justify;
  lk_u8 text_align, text_valign;

} lk_style;

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
#define LK_SF_ACCENT (1u << 13)
#define LK_SF_TEXT_ALIGN (1u << 14)
#define LK_SF_TEXT_VALIGN (1u << 15)

#define LK_STYLE_INHERIT_MASK (LK_SF_FG | LK_SF_FONT_ID | LK_SF_FONT_SIZE)

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
  LK_ROP_CLIP_END,
  LK_ROP_DRAW_RUN,

  LK_ROP_DRAW_IMAGE,

  LK_ROP_DRAW_LINES,

  LK_ROP_STROKE_RECT,

  LK_ROP_CARET

} lk_render_op;

#define LK_TA_UNDERLINE (1u << 0)

typedef struct lk_render_cmd {
  lk_u8 op;
  lk_rect rect;
  lk_color color;
  lk_u32 str_id;
  lk_u16 font_id;

  lk_u16 font_size;

  lk_u32 run_off;
  lk_u32 run_len;
  lk_u32 img_id;

  lk_u32 img_gen;

  lk_u8 img_filter;
  lk_u8 stroke;

  lk_u8 text_attr;
} lk_render_cmd;

typedef struct lk_render_list {
  lk_render_cmd *cmds;
  lk_u32 count;
  lk_u32 cap;
  char *bytes;

  lk_u32 bytes_count;
  lk_u32 bytes_cap;
} lk_render_list;

int lk_render_build(const lk_tree *t, const lk_rect *rects,
                    const lk_style *styles, const lk_state *state,
                    const lk_widget_geom *geom, lk_render_list *out);

int lk_render_build_overlays(lk_ui *ui, const lk_rect *rects,
                             const lk_layout_cfg *cfg, lk_render_list *out);

int lk_render_list_push(lk_render_list *rl, lk_render_cmd cmd);

int lk_render_list_push_run(lk_render_list *rl, const char *ptr, lk_u32 len,
                            lk_u32 *out_off);

void lk_render_list_destroy(lk_render_list *rl);

const char *lk_version(void);

typedef struct lk_canvas lk_canvas;

#define LK_CANVAS_MAX_POINTS 65536u

#define LK_CANVAS_MAX_CLIP_DEPTH 8u

lk_canvas *lk_canvas_new(lk_u32 w, lk_u32 h, void *(*alloc)(void *, lk_u32),
                         void (*dealloc)(void *, void *), void *ud);
void lk_canvas_destroy(lk_canvas *c);
void lk_canvas_size(const lk_canvas *c, lk_u32 *w, lk_u32 *h);

int lk_canvas_set_size(lk_canvas *c, lk_u32 w, lk_u32 h);

void lk_canvas_clear(lk_canvas *c);
lk_u32 lk_canvas_op_count(const lk_canvas *c);

int lk_canvas_line(lk_canvas *c, lk_i32 x0, lk_i32 y0, lk_i32 x1, lk_i32 y1,
                   lk_color color, lk_u8 stroke);
int lk_canvas_polyline(lk_canvas *c, const lk_i32 *xy, lk_u32 n_points,
                       lk_color color, lk_u8 stroke);
int lk_canvas_rect(lk_canvas *c, lk_rect r, lk_color color, lk_u8 stroke);
int lk_canvas_fill_rect(lk_canvas *c, lk_rect r, lk_color color);

int lk_canvas_text(lk_canvas *c, lk_i32 x, lk_i32 y, const char *ptr,
                   lk_u32 len, lk_color color);

int lk_canvas_clip_begin(lk_canvas *c, lk_rect r);
int lk_canvas_clip_end(lk_canvas *c);
lk_u32 lk_canvas_clip_depth(const lk_canvas *c);

const lk_resource_type *lk_canvas_type(void);

lk_canvas *lk_canvas_from_node(const lk_resources *rs, const lk_tree *t,
                               lk_ix n);

#define LK_KIND_MAX 32

#define LK_SPAN_FG (1u << 0)
#define LK_SPAN_BG (1u << 1)
#define LK_SPAN_UNDERLINE (1u << 2)

typedef struct lk_text_span {
  lk_u32 start, end;
  lk_color fg, bg;
  lk_u8 flags;
} lk_text_span;

typedef enum lk_wrap_mode {
  LK_WRAP_NONE = 0,
  LK_WRAP_CHARACTER,
  LK_WRAP_WORD
} lk_wrap_mode;

typedef struct lk_spans lk_spans;

lk_spans *lk_spans_new(void *(*alloc)(void *, lk_u32),
                       void (*dealloc)(void *, void *), void *ud);
void lk_spans_destroy(lk_spans *s);

void lk_spans_clear(lk_spans *s);
lk_u32 lk_spans_count(const lk_spans *s);

int lk_spans_add(lk_spans *s, lk_u32 start, lk_u32 end, lk_color fg,
                 lk_color bg, lk_u8 flags);

int lk_spans_add_present(lk_spans *s, lk_u32 start, lk_u32 end, lk_u32 type_id,
                         lk_value value);

const lk_text_span *lk_spans_get(const lk_spans *s, lk_u32 i);

int lk_spans_present_get(const lk_spans *s, lk_u32 i, lk_u32 *type_id,
                         lk_value *value);

lk_u32 lk_spans_present_at(const lk_spans *s, lk_u32 pos,
                           struct lk_presentation_hit *out, lk_u32 cap);

void lk_spans_set_release(lk_spans *s, void (*fn)(void *ud, lk_value v),
                          void *ud);
const lk_resource_type *lk_spans_type(void);
lk_spans *lk_spans_from_node(const lk_resources *rs, const lk_tree *t, lk_ix n);

int lk_styled_text_pos_at(const lk_ui *ui, lk_node_id id, lk_i32 x, lk_i32 y,
                          lk_u32 *out_pos);

#define LK_MENU_NO_TRANSLATOR 0xFFFFFFFFu
#define LK_MENU_MAX_ITEMS 64u

typedef struct lk_menu_item {
  lk_u32 label;
  lk_u32 command_name;
  lk_u32 accel;
  lk_u8 enabled;
  lk_u8 separator;

  lk_u32 translator_ix;

  lk_node_id node_id;

  lk_u32 ptype;
  lk_value args[LK_PRES_MAX_ARGS];
  lk_u8 arg_count;
  lk_presentation_hit hit;
} lk_menu_item;

lk_u32 lk_menu_candidates(lk_ui *ui, const lk_tree *t, lk_ix target, lk_i32 x,
                          lk_i32 y, lk_menu_item *out, lk_u32 cap);

int lk_menu_open(lk_ui *ui, lk_node_id owner, lk_u8 anchor, lk_i32 x, lk_i32 y,
                 const lk_menu_item *items, lk_u32 n);

lk_u32 lk_menu_open_context(lk_ui *ui, const lk_tree *t, lk_i32 x, lk_i32 y);

lk_u32 lk_menu_open_context_at_focus(lk_ui *ui, const lk_tree *t);
void lk_menu_close(lk_ui *ui);
int lk_menu_is_open(const lk_ui *ui);
lk_u32 lk_menu_count(const lk_ui *ui);
const lk_menu_item *lk_menu_item_get(const lk_ui *ui, lk_u32 i);
lk_i32 lk_menu_hover(const lk_ui *ui);
void lk_menu_set_hover(lk_ui *ui, lk_i32 i);

int lk_menu_activate(lk_ui *ui, lk_u32 i);

lk_rect lk_menu_rect(const lk_ui *ui);

int lk_list_range(const lk_ui *ui, lk_node_id id, lk_i32 *first, lk_i32 *count);

int lk_list_scroll_to_row(lk_ui *ui, lk_node_id id, lk_i32 row);

lk_i32 lk_list_cursor(const lk_tree *t, lk_ix n, const lk_state *state);

lk_i32 lk_widget_fit_height(const lk_tree *t, lk_ix n, lk_i32 width,
                            const lk_size *sizes,
                            const struct lk_layout_cfg *cfg);

#define LK_TEXT_INPUT_MAX 1024

typedef struct lk_widget_def {
  void (*measure)(const lk_tree *t, lk_ix n, const lk_size *sizes,
                  const lk_layout_cfg *cfg, lk_i32 *out_w, lk_i32 *out_h);

  int (*layout)(const lk_tree *t, lk_ix n, const lk_size *sizes,
                const lk_rect *content, const lk_layout_cfg *cfg,
                lk_rect *rects);

  void (*render)(const lk_tree *t, lk_ix n, const lk_rect *rect,
                 const lk_style *style, const lk_state *state,
                 const lk_widget_geom *geom, lk_render_list *out);

  int (*event)(lk_ui *ui, const lk_tree *t, lk_ix n, lk_event *ev);

  lk_u8 clips;

  lk_i32 (*fit_height)(const lk_tree *t, lk_ix n, lk_i32 width,
                       const lk_size *sizes, const lk_layout_cfg *cfg);

  lk_u32 (*presentations_at)(lk_ui *ui, const lk_tree *t, lk_ix n, lk_i32 x,
                             lk_i32 y, lk_presentation_hit *out, lk_u32 cap);
} lk_widget_def;

void lk_widget_register(lk_kind kind, const lk_widget_def *def);
const lk_widget_def *lk_widget_get(lk_kind kind);

lk_ix lk_hit_test(const lk_tree *t, const lk_rect *rects, lk_i32 x, lk_i32 y);

lk_ix lk_hit_test_overlay(lk_ui *ui, const lk_rect *rects,
                          const lk_layout_cfg *cfg, lk_i32 x, lk_i32 y);

#define LK_DISMISS_NONE 0

#define LK_DISMISS_DISMISSED 1

#define LK_DISMISS_BLOCKED 2

int lk_overlay_dismiss_outside(lk_ui *ui, const lk_rect *rects,
                               const lk_layout_cfg *cfg, lk_i32 x, lk_i32 y);

int lk_overlay_push(lk_ui *ui, const lk_overlay *ov);
void lk_overlay_pop(lk_ui *ui);
int lk_overlay_pop_owner(lk_ui *ui, lk_node_id owner_id);
const lk_overlay *lk_overlay_top(const lk_ui *ui);
lk_u32 lk_overlay_count(const lk_ui *ui);

lk_rect lk_anchor_resolve(const lk_overlay *ov, lk_rect owner_rect, lk_i32 vw,
                          lk_i32 vh, lk_i32 content_w, lk_i32 content_h);

void lk_event_init_pointer(lk_event *ev, lk_u8 type, lk_i32 x, lk_i32 y,
                           lk_u8 button);
void lk_event_init_key(lk_event *ev, lk_u8 type, lk_u16 keycode, lk_u8 mods);

void lk_event_route(lk_ui *ui, lk_event *event);

int lk_event_enqueue(lk_ui *ui, const lk_event *ev);

void lk_ui_flush_events(lk_ui *ui, const lk_tree *t);

void lk_ui_set_event_handler(lk_ui *ui, lk_event_handler_fn fn, void *ud);

void lk_ui_set_clipboard(lk_ui *ui, lk_clipboard_get_fn get_fn,
                         lk_clipboard_set_fn set_fn, void *ud);

int lk_focus_set(lk_ui *ui, const lk_tree *t, lk_node_id id);
void lk_focus_clear(lk_ui *ui);

lk_node_id lk_focus_request(lk_ui *ui, lk_node_id id);
lk_node_id lk_focus_next(lk_ui *ui, const lk_tree *t);
lk_node_id lk_focus_prev(lk_ui *ui, const lk_tree *t);
lk_ix lk_focus_current(const lk_ui *ui, const lk_tree *t);

void lk_hover_set(lk_ui *ui, lk_node_id id);
void lk_hover_clear(lk_ui *ui);

void lk_capture_set(lk_ui *ui, lk_node_id id);
void lk_capture_clear(lk_ui *ui);
lk_node_id lk_capture_current(const lk_ui *ui);

void lk_ui_add_translator(lk_ui *ui, lk_u8 event_type, lk_u32 ptype,
                          lk_u16 node_kind, lk_u16 keycode, lk_u8 mods,
                          lk_u8 button, lk_u32 command_name);
void lk_ui_add_translator_s(lk_ui *ui, lk_u8 event_type, const char *ptype,
                            lk_u16 node_kind, lk_u16 keycode, lk_u8 mods,
                            lk_u8 button, const char *command_name);
void lk_ui_clear_translators(lk_ui *ui);

void lk_ui_set_command_handler(lk_ui *ui, lk_command_handler_fn fn, void *ud);

const lk_command_queue *lk_ui_commands(const lk_ui *ui);

void lk_ui_clear_commands(lk_ui *ui);

lk_value lk_v_text(lk_ui *ui, const char *ptr, lk_u32 len);

lk_str lk_command_text(const lk_ui *ui, const lk_command *cmd, lk_value v);

lk_str lk_command_arg_text(const lk_ui *ui, const lk_command *cmd, lk_u8 idx);

void lk_ui_dump_commands(const lk_ui *ui, lk_write_fn wr, void *wr_ud);
const lk_command *lk_ui_command_log(const lk_ui *ui, lk_u32 *out_count);
void lk_ui_clear_command_log(lk_ui *ui);

void lk_translate_event(lk_ui *ui, const lk_tree *t, lk_event *event);

int lk_translate_presentations(lk_ui *ui, const lk_tree *t, lk_ix node,
                               lk_event *ev, const lk_presentation_hit *hits,
                               lk_u32 n);

#ifdef __cplusplus
}
#endif

#endif
