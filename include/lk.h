#ifndef LK_H
#define LK_H

/* Library version.  CMake's project(VERSION) must agree -- the build
 * fails at configure time if it does not (the header is the truth). */
#define LK_VERSION_MAJOR 0
#define LK_VERSION_MINOR 1
#define LK_VERSION_PATCH 0
#define LK_VERSION_STRING "0.1.0"

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
 ** Geometry (used by overlays, layout, render)
 **/
typedef struct lk_rect {
  lk_i32 x, y, w, h;
} lk_rect;
typedef struct lk_size {
  lk_i32 w, h;
} lk_size;

/**
 ** lk_node_id is an interned string -> u32
 **/
typedef lk_u32 lk_node_id;

typedef struct lk_intern lk_intern;
typedef struct lk_state lk_state;
typedef struct lk_resources lk_resources;

lk_intern *lk_intern_new(void *(*alloc)(void *, lk_u32),
                         void (*dealloc)(void *, void *), void *alloc_ud);
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
  UIK_SPLIT_H,  /* two children side-by-side with a draggable vertical
                   divider between them.  Ratio is per-mille (0..1000):
                   LKS_SPLIT_RATIO state (set by dragging) overrides the
                   UIP_SPLIT_RATIO prop (initial value), default 500.
                   With one child it behaves as a plain container;
                   children beyond the first two are ignored (zero
                   rects). */
  UIK_SPLIT_V,  /* same, stacked vertically (horizontal divider). */
  UIK_EDITOR,   /* multi-line text editor view over an application-owned
                   lk_document, attached via the UIP_EDITOR resource
                   ref.  Vtable in src/editor/lk-editor-widget.c; see
                   include/lk-editor.h and docs/editor.md section 7. */
  UIK_CHECKBOX, /* toggle with a text label (docs/forms-widgets.md).
                   Effective checked = LKS_CHECKED state (written by
                   clicks / SPACE) > UIP_CHECKED prop > 0.  Toggling
                   emits LK_EVENT_VALUE_CHANGED with "1"/"0". */
  UIK_RADIO,    /* like CHECKBOX but exclusive among sibling RADIO
                   nodes under the same parent: checking one clears
                   the LKS_CHECKED state of its sibling radios.  Only
                   the newly checked radio emits VALUE_CHANGED ("1"). */
  UIK_SLIDER,   /* horizontal integer slider over [UIP_MIN, UIP_MAX]
                   in UIP_STEP increments.  Effective value =
                   LKS_SLIDER_VALUE state (drag / keys) > UIP_VALUE
                   (i32) > UIP_MIN.  Emits VALUE_CHANGED with the new
                   value as a decimal string. */
  UIK_TABS,     /* tab strip + one visible page.  Children are UIK_TAB
                   nodes; the strip is owned by the TABS node (like the
                   dropdown trigger), the selected TAB's subtree fills
                   the area below it, unselected TAB subtrees are
                   skipped by layout/render/focus (their rects zero).
                   Effective selection = LKS_SELECTED_INDEX state >
                   UIP_VALUE (a TAB child's node id string) > 0.
                   Selecting emits VALUE_CHANGED with the TAB's id. */
  UIK_TAB,      /* one page of a TABS: UIP_TEXT is the strip title;
                   children lay out as a column inside the page. */
  UIK_GRID,     /* row-major cell grid with UIP_COLUMNS columns; every
                   column is as wide as its widest cell, every row as
                   tall as its tallest, children fill their cell. */
  UIK_IMAGE,    /* leaf view over an application-owned lk_image pixel
                   buffer, attached via the UIP_IMAGE resource ref.
                   Measures at the image's intrinsic pixel size (UIP_W /
                   UIP_H override); renders the image stretched to the
                   node rect.  See docs/image-widget.md. */
  UIK_CANVAS,   /* leaf replaying an application-owned lk_canvas display
                   list (UIP_CANVAS resource ref): lines, polylines,
                   rects, text in canvas-local pixels, clipped to the
                   node rect.  Measures at the canvas's size hint (UIP_W /
                   UIP_H override).  See docs/canvas.md. */
  UIK_STYLED_TEXT, /* read-only wrapping, per-range styled text: UIP_TEXT
                      for the bytes, UIP_SPANS (an app-owned lk_spans)
                      for colour / underline / presentations, UIP_WRAP
                      for the mode.  Sized by its content at the width
                      it is given (the fit_height hook).  Leaf, not
                      focusable.  See docs/styled-text.md. */
  UIK_LIST,        /* virtualized list: UIP_ROWS x UIP_ROW_H px of extent,
                      scrolled like a SCROLL; children are the rows the
                      app chose to build, placed by their UIP_ROW index;
                      geom->list reports the visible window so the next
                      frame builds exactly those.  Keeps a cursor row
                      (LKS_CURSOR_ROW; VALUE initial, CONTROLLED).  See
                      docs/table.md. */
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

  UIP_HIDDEN,  /* bool: subtree is skipped by the main measure, layout,
                  render, hit-test, and focus passes.  Overlay content
                  subtrees are built hidden and laid out on demand at
                  their resolved anchor (see lk_layout_subtree and
                  docs/overlays.md). */

  UIP_TOOLTIP, /* string (interned, like TEXT): hover help text.  When
                  the pointer moves onto a node carrying this prop, the
                  core pushes an LK_OVERLAY_TOOLTIP overlay anchored
                  below it (hover-transition hook in lk_hover_set /
                  lk_hover_clear; producer in lk-tooltip.c).  Tooltips
                  are passive: never hit-testable, never consume
                  clicks.  See docs/overlays.md. */

  UIP_SPLIT_RATIO, /* i32 per-mille (0..1000): initial divider position
                      for UIK_SPLIT_H/V.  Host-settable initial value;
                      once the user drags, LKS_SPLIT_RATIO state takes
                      priority.  Default 500. */

  UIP_EDITOR, /* UIV_RESOURCE: typed ref to an application-owned
                 lk_editor (registered under lk_editor_type()).  Set by
                 the app every frame like any prop; a missing, stale,
                 or wrong-typed ref makes the UIK_EDITOR node render
                 background only and ignore events.  See
                 docs/editor.md sections 5 and 7. */

  UIP_GROW, /* i32 >= 0: weighted growth in a ROW/COLUMN stack.  The
               child's share of leftover main-axis space, distributed
               largest-remainder (docs/grow-layout.md).  Presence
               matters: absent != 0 -- a bare unsized SPACER keeps its
               legacy weight of 1, grow 0 pins it.  An explicit
               main-axis size is the basis; the child still grows.
               Clamped to [0, 4096] at distribution time. */

  UIP_VALUE, /* string (interned, like TEXT): dropdown initial /
                controlled selection by option text.  Priority:
                LKS_SELECTED_INDEX state (user interaction) > this
                prop (first option whose TEXT matches) > index 0 --
                the same state > prop > default pattern as
                UIP_SPLIT_RATIO.  An unmatched value falls back to
                index 0. */

  UIP_CONTROLLED, /* i32 0/1 (default 0): app-controlled value.  When
                     nonzero, user interaction never writes the
                     widget's value state (LKS_SPLIT_RATIO,
                     LKS_CHECKED, LKS_SLIDER_VALUE, a TABS'
                     LKS_SELECTED_INDEX, a TEXT_INPUT's LKS_TEXT_BUF)
                     -- the widget still emits LK_EVENT_VALUE_CHANGED
                     and the app re-supplies the value prop
                     (UIP_SPLIT_RATIO / UIP_CHECKED / UIP_VALUE /
                     UIP_TEXT) each frame from its own model
                     (one-frame lag accepted).  Uncontrolled widgets
                     keep the state write AND emit the same event.
                     A controlled TEXT_INPUT reads UIP_TEXT as its
                     whole text every frame; each edit's event carries
                     the candidate string, and cursor/selection stay
                     widget-owned interaction state.
                     Splits: this was UIP_SPLIT_CONTROLLED, kept as an
                     alias below. */
  UIP_CHECKED,  /* bool: initial / controlled checked state of a
                   CHECKBOX or RADIO (LKS_CHECKED state wins when
                   present, the SPLIT_RATIO pattern). */
  UIP_MIN,      /* i32: slider range minimum (default 0) */
  UIP_MAX,      /* i32: slider range maximum (default 100; a MAX
                   below MIN is treated as MIN) */
  UIP_STEP,     /* i32 >= 1: slider increment (default 1); values snap
                   to MIN + k*STEP */
  UIP_COLUMNS,  /* i32 >= 1: column count of a GRID (default 1) */

  UIP_IMAGE, /* UIV_RESOURCE: typed ref to an application-owned
                lk_image (registered under lk_image_type()).  Set by
                the app every frame like any prop; a missing, stale,
                or wrong-typed ref makes the UIK_IMAGE node render
                background only.  See docs/image-widget.md. */
  UIP_FILTER, /* i32 lk_image_filter: how a UIK_IMAGE is resampled when
                 its rect differs from the pixel size.  LINEAR (0,
                 default) for photos; NEAREST (1) keeps pixel art crisp
                 at integer zooms.  Carried on the DRAW_IMAGE render cmd
                 (img_filter) for the consumer. */
  UIP_CANVAS, /* UIV_RESOURCE: typed ref to an application-owned
                 lk_canvas display list (registered under
                 lk_canvas_type()).  A missing, stale, or wrong-typed
                 ref makes the UIK_CANVAS node render background only.
                 See docs/canvas.md. */

  UIP_TEXT_ALIGN,  /* i32 lk_align: where a leaf widget places its text
                      run inside its content box, horizontally (LABEL,
                      BUTTON, TEXT_INPUT; START default, STRETCH reads
                      as START).  Presence-gated override of the style
                      field of the same name -- theme rules set it
                      per kind/tag, the prop per node. */
  UIP_TEXT_VALIGN, /* i32 lk_align: the vertical counterpart */

  UIP_SPANS, /* UIV_RESOURCE: typed ref to an application-owned lk_spans
                (registered under lk_spans_type()) styling a
                STYLED_TEXT's bytes.  Missing / stale / wrong-typed =
                unstyled, never an error. */
  UIP_WRAP,  /* i32 lk_wrap_mode (default WORD on a STYLED_TEXT): how
                the text breaks into rows at the node's width. */

  UIP_ROWS,  /* i32 >= 0: a LIST's virtual row count */
  UIP_ROW_H, /* i32 >= 1: a LIST's uniform row height in px (default 24) */
  UIP_ROW,   /* i32 >= 0: a LIST child's row index (a child without one
                is not placed) */

  UIP__COUNT
} lk_prop_key;

/* UIP_FILTER values: resampling of a UIK_IMAGE stretched to its rect. */
typedef enum lk_image_filter {
  LK_FILTER_LINEAR = 0,
  LK_FILTER_NEAREST = 1
} lk_image_filter;

/* Backwards-compatible name: the split's controlled flag is the
 * generic UIP_CONTROLLED. */
#define UIP_SPLIT_CONTROLLED UIP_CONTROLLED

/* Retained per-node state keys.
 *
 * INTERNAL WIDGET STATE -- not API.  Keys below LKS_USER belong to
 * the built-in widgets: hosts must not read or write them directly,
 * and the Lcl bindings reject them (script-visible state starts at
 * LKS_USER).  Retained keys hold user interaction state only; derived
 * per-frame geometry lives in the lk_widget_geom scratch (see
 * lk_layout_cfg.geom), never here. */
typedef enum lk_state_key {
  LKS_SCROLL_X = 1,
  LKS_SCROLL_Y,
  LKS_CURSOR_POS,
  LKS_SELECTION_START,
  LKS_SELECTION_END,
  LKS_EXPANDED,
  LKS_TEXT_BUF,
  LKS_SELECTED_INDEX, /* dropdown: index of currently selected option */
  LKS_HOVER_INDEX,    /* dropdown: index of option under cursor while open */
  LKS_FOCUSED,        /* i32 0/1: kept in sync with the UI focus by the
                         lk_focus_* functions so widget render code (which
                         only sees lk_state) can tell if its node is
                         focused */
  LKS_SPLIT_RATIO,    /* split: divider position in per-mille (0..1000),
                         written by dragging.  Overrides UIP_SPLIT_RATIO. */
  LKS_SPLIT_DRAGGING, /* split: 1 while the divider is being dragged
                         (pointer captured by the split node) */
  LKS_POPUP_SCROLL,   /* dropdown: popup scroll offset in px when the
                         option list overflows DROPDOWN_POPUP_MAX_HEIGHT.
                         User scroll position (wheel / keyboard), so it is
                         retained state; reset to 0 on open and close. */
  LKS_CHECKED,        /* checkbox / radio: 0/1, written by toggling.
                         Overrides UIP_CHECKED. */
  LKS_SLIDER_VALUE,   /* slider: current value, written by drag/keys.
                         Overrides UIP_VALUE. */
  LKS_CURSOR_ROW,     /* list: keyboard cursor row (-1 = none), written
                         by keys / row clicks unless CONTROLLED */
  LKS_LIST_DRAGGING,  /* list: grab offset while the bar thumb is being
                         dragged (NONE otherwise) */
  LKS_SLIDER_DRAGGING,/* slider: 1 while the thumb is being dragged
                         (pointer captured by the slider node) */
  LKS__BUILTIN_COUNT,
  LKS_USER = 256
} lk_state_key;

typedef enum lk_align {
  LK_ALIGN_START = 0,
  LK_ALIGN_CENTER,
  LK_ALIGN_END,
  LK_ALIGN_STRETCH /* fill the cross axis.  NOT the resolved default
                      (that is START); WINDOW gets STRETCH from the
                      default theme.  The styleless layout fallback
                      (cfg->styles == NULL) still assumes STRETCH for
                      stacks, for backwards compat. */
} lk_align;

typedef enum lk_value_tag {
  UIV_NONE = 0,
  UIV_BOOL,
  UIV_I32,
  UIV_STR,
  UIV_RESOURCE, /* typed resource reference — see lk_resource_ref */
  UIV_TEXT /* transient text in the command queue's dispatch arena
              (docs/weft-surface.md §1.2).  Command/hit scope ONLY —
              never valid in trees or retained state.  Valid from
              emission until lk_ui_clear_commands; the command log
              copies the bytes into its own arena at record time. */
} lk_value_tag;

typedef struct lk_value {
  lk_value_tag tag;
  union {
    lk_u8 b;
    lk_u32 i;
    lk_u32 str_id; /* interned string id (UIV_STR) */
    struct {
      lk_u32 id, gen;
    } res; /* resource reference (UIV_RESOURCE); no pointer in the
              value — resolution and type checking go through the
              lk_resources table (docs/editor.md §5) */
    struct {
      lk_u32 off, len;
    } text; /* UIV_TEXT: slice into the owning command queue's byte
               arena (or the command log's arena for logged
               commands).  Resolve via lk_command_arg_text. */
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
 * Interior presentations (docs/weft-surface.md §1) — sub-node
 * presentation candidates resolved at a gesture locus and offered to
 * the one translator matcher.  A hit carries a presentation type, a
 * typed application value, and a widget-specific immutable locus
 * snapshot.  Precedence travels with discovery order, not in the hit.
 **/

typedef struct lk_presentation_hit {
  lk_u32 type_id;    /* interned ptype — vocabulary is bounded */
  lk_value value;    /* the typed application value */
  lk_u32 locus_kind; /* interned, e.g. "editor-range" (0 = none) */
  lk_u32 locus[6];   /* immutable snapshot, packed by the discovering
                        widget.  For "editor-range" the packing is
                        documented in lk-editor.h. */
} lk_presentation_hit;

/* A source of interior presentation candidates at a position.  The
 * widget owns nothing here; ud is borrowed.  query_at fills out with
 * up to cap hits in precedence order and returns the number written. */
typedef struct lk_presentation_source {
  void *ud;
  lk_u32 (*query_at)(void *ud, lk_u32 pos, lk_presentation_hit *out,
                     lk_u32 cap);
} lk_presentation_source;

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
  lk_resources *resources; /* borrowed resource table (like the shared
                              intern table) — never freed by the tree.
                              lk_ui sets it on both its trees so every
                              widget vtable hook can resolve refs
                              through t alone; standalone trees have
                              NULL (attach via lk_tree_set_resources). */
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

/**
 * Resource references — typed, generation-checked handles to
 * application-owned objects (docs/editor.md §5).  The tree presents
 * views of those objects; it never owns them.
 **/

typedef struct lk_resource_ref {
  lk_u32 id;         /* stable logical identity, 0 = null ref */
  lk_u32 generation; /* stale-handle detection */
} lk_resource_ref;

/* Value constructors. */
lk_value lk_v_none(void);
lk_value lk_v_bool(int b);
lk_value lk_v_i32(lk_i32 i);
lk_value lk_v_str(lk_intern *it, lk_str s);
lk_value lk_v_cstr(lk_intern *it, const char *cstr);
lk_value lk_v_resource(lk_resource_ref ref);

/* Extract the ref from a UIV_RESOURCE value ({0,0} if not one). */
lk_resource_ref lk_v_resource_ref(lk_value v);

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

/**
 * Resource table — owned by lk_ui and borrowed by its trees (exactly
 * like the shared intern table).  Registration happens once per object
 * (id stability across frames); the table does NOT own registered
 * objects — release only invalidates the ref by bumping the slot
 * generation.  A stale or wrong-typed ref resolves to NULL, so
 * use-after-free is unrepresentable at this boundary.  See
 * docs/editor.md §5.
 **/

typedef struct lk_resource_type {
  const char *name; /* "editor", "document", ... */
  /* optional hooks, all NULL-able in v1: */
  void (*describe)(void *obj, lk_write_fn w, void *wud);
} lk_resource_type;

lk_resources *lk_resources_new(void *(*alloc)(void *, lk_u32),
                               void (*dealloc)(void *, void *), void *ud);
void lk_resources_destroy(lk_resources *rs);

/* Register obj under type; returns the new ref ({0,0} on failure).
 * debug_name is copied with the table's allocator (NULL -> empty). */
lk_resource_ref lk_resource_register(lk_resources *rs,
                                     const lk_resource_type *type, void *obj,
                                     const char *debug_name);

/* Invalidate a ref: bumps the slot generation and clears the object.
 * Releasing a stale or null ref is a no-op.  The slot is reused by a
 * later register (whose new ref works; the old ref stays dead). */
void lk_resource_release(lk_resources *rs, lk_resource_ref ref);

/* Resolve a ref.  NULL if rs is NULL, the ref is null, the id is out
 * of range, the generation mismatches (stale), or the type descriptor
 * pointer differs (type checking is descriptor-pointer equality). */
void *lk_resource_get(const lk_resources *rs, lk_resource_ref ref,
                      const lk_resource_type *type);

/* Attach a resource table to a standalone tree (borrow — the tree
 * never frees it).  lk_ui wires its own table automatically; this is
 * for headless tests and tree-driving hosts. */
void lk_tree_set_resources(lk_tree *t, lk_resources *rs);

/**
 * Images — application-owned RGBA8888 pixel buffers, shown by the
 * UIK_IMAGE widget through a UIP_IMAGE resource ref (the editor-track
 * ownership model: the tree presents a view, it never owns the
 * pixels).  Core is codec-free; file load/save lives in the SDL
 * backend (lk-sdl.h).  See docs/image-widget.md.
 **/

typedef struct lk_image lk_image;

/* Create a w x h image, pixels zeroed (transparent black).  Rejects
 * zero dimensions and anything over 16384 on either axis (keeps
 * w * h * 4 comfortably inside lk_u32).  NULL alloc/dealloc use the
 * system allocator. */
lk_image *lk_image_new(lk_u32 w, lk_u32 h, void *(*alloc)(void *, lk_u32),
                       void (*dealloc)(void *, void *), void *ud);
void lk_image_destroy(lk_image *img);

/* The mutable pixel buffer: w * h * 4 bytes, RGBA byte order, pitch =
 * w * 4 (no row padding).  Mutate freely, then lk_image_mark_dirty so
 * consumers (the SDL texture cache) re-upload. */
lk_u8 *lk_image_pixels(lk_image *img);
void lk_image_size(const lk_image *img, lk_u32 *w, lk_u32 *h);

/* Pixel-dirty generation: starts at 1, bumped by mark_dirty.  Distinct
 * from the resource ref's generation (which only changes on release /
 * re-register). */
void lk_image_mark_dirty(lk_image *img);
lk_u32 lk_image_generation(const lk_image *img);

/* Resource type descriptor for lk_resource_register/get. */
const lk_resource_type *lk_image_type(void);

/* Resolve a node's UIP_IMAGE ref to the live image, NULL when the
 * prop is absent or the ref is stale/wrong-typed (the widget's
 * degrade contract). */
lk_image *lk_image_from_node(const lk_resources *rs, const lk_tree *t,
                             lk_ix n);

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
  /* Synthetic — enqueued whenever effective keyboard focus changes
   * (lk_focus_set/clear/next/prev and the end-of-frame focus GC).
   * data.focus carries the previous and new focused node ids (0 =
   * none).  Delivered from the pending queue: at the end of the
   * outermost lk_event_route call, or via lk_ui_flush_events. */
  LK_EVENT_FOCUS_CHANGED,
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

/* lk-owned pointer button identity carried in lk_event.data.pointer.
 * button and matched by translators.  Backends map their own values
 * in (the SDL backend maps left/middle/right; anything else becomes
 * ANY).  0 doubles as "unspecified" on synthetic events and as the
 * translator wildcard. */
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
  LKK_A, LKK_B, LKK_C, LKK_D, LKK_E, LKK_F, LKK_G, LKK_H, LKK_I,
  LKK_J, LKK_K, LKK_L, LKK_M, LKK_N, LKK_O, LKK_P, LKK_Q, LKK_R,
  LKK_S, LKK_T, LKK_U, LKK_V, LKK_W, LKK_X, LKK_Y, LKK_Z,
  LKK_0, LKK_1, LKK_2, LKK_3, LKK_4, LKK_5, LKK_6, LKK_7, LKK_8,
  LKK_9,
  LKK_PAGEUP, LKK_PAGEDOWN,
  LKK_F1, LKK_F2, LKK_F3, LKK_F4, LKK_F5, LKK_F6, LKK_F7, LKK_F8,
  LKK_F9, LKK_F10, LKK_F11, LKK_F12,
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
    struct {
      lk_node_id prev_id, next_id; /* 0 = no focus */
    } focus;
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
  lk_presentation_hit hit; /* the interior-presentation hit that
                            * produced this command; all-zero when the
                            * command did not come from an interior
                            * presentation (node presentations,
                            * keybindings). */
} lk_command;

typedef struct lk_command_queue {
  lk_command *cmds;
  lk_u32 count;
  lk_u32 cap;
  char *bytes; /* dispatch arena for UIV_TEXT args (render-list byte-
                  arena pattern): the queue OWNS these bytes.  Reset
                  (capacity kept) by lk_ui_clear_commands. */
  lk_u32 bytes_count;
  lk_u32 bytes_cap;
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
  lk_u8 mods;          /* LK_MOD_* bits; exact match when keycode != 0
                          or button != 0 */
  lk_u8 button;        /* lk_pointer_button to match (0 = any).  When
                          set, only pointer events with that exact
                          button AND exact mods match — the keycode
                          discipline extended. */
  lk_u32 command_name; /* interned command name to emit */
} lk_translator;

/**
 * Overlays — popups, tooltips, context menus, modals.
 *
 * An overlay is a rectangular region that draws after (on top of) the
 * main tree, is hit-tested before it, and belongs to an "owner" node
 * in the main tree.  Overlays live on a stack owned by lk_ui and
 * persist across frames; nodes are keyed by stable lk_node_id (tree
 * indices are reassigned every frame).  See docs/overlays.md.
 **/

typedef enum lk_overlay_kind {
  LK_OVERLAY_DROPDOWN_POPUP = 1,
  LK_OVERLAY_TOOLTIP,
  LK_OVERLAY_CONTEXT_MENU,
  LK_OVERLAY_MODAL
} lk_overlay_kind;

typedef enum lk_anchor_mode {
  LK_ANCHOR_BELOW = 1,       /* below owner (flips above on overflow) */
  LK_ANCHOR_ABOVE,           /* above owner (flips below on overflow) */
  LK_ANCHOR_AT_CURSOR,       /* at (offset.x, offset.y) */
  LK_ANCHOR_CENTER_VIEWPORT  /* centered in the viewport */
} lk_anchor_mode;

typedef struct lk_overlay {
  lk_u8 kind;                 /* lk_overlay_kind */
  lk_u8 anchor_mode;          /* lk_anchor_mode */
  lk_u8 dismiss_on_outside;   /* 1 = outside pointer-down dismisses */
  lk_u8 traps_focus;          /* 1 = focus cycling scoped to content */
  lk_node_id owner_id;        /* trigger / hovered element (stable id) */
  lk_node_id content_root_id; /* root of the overlay's node subtree
                               * (stable id), or 0 if the content is
                               * procedurally generated */
  lk_rect offset;             /* relative to anchor.  For AT_CURSOR,
                               * offset.x/y is the cursor point.  w/h
                               * may be 0 to mean "intrinsic". */
} lk_overlay;

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
  lk_node_id captured_id; /* 0 = no pointer capture (see lk_capture_set) */
  lk_node_id focus_request_id; /* pending lk_focus_request (0 = none) */
  lk_state *state;       /* retained per-node state */
  lk_resources *resources; /* owned resource table, borrowed by both
                              trees (see lk_ui_resources) */

  /* Translators */
  lk_translator *translators;
  lk_u32 translator_count;
  lk_u32 translator_cap;

  /* Command queue (current frame) */
  lk_command_queue cmd_queue;
  lk_command_handler_fn cmd_handler;
  void *cmd_handler_ud;

  /* Command log (append-only, cleared explicitly).  The log outlives
   * lk_ui_clear_commands, so UIV_TEXT args are copied into the log's
   * OWN arena at record time (offsets in logged commands point into
   * cmd_log_bytes, never into the queue arena). */
  lk_command *cmd_log;
  lk_u32 cmd_log_count;
  lk_u32 cmd_log_cap;
  char *cmd_log_bytes;
  lk_u32 cmd_log_bytes_count;
  lk_u32 cmd_log_bytes_cap;

  /* Style system */
  struct lk_theme *theme;
  struct lk_style *styles;
  lk_u32 styles_cap;
  lk_u8 *node_states;
  lk_u32 nstates_cap;

  /* Per-frame widget geometry scratch (see lk_widget_geom).  Owned by
   * the ui, sized by lk_ui_geom.  Widget EVENT handlers read it, so a
   * host that routes events must pass this array (lk_ui_geom) as
   * cfg->geom when laying out; NULL until the host does. */
  union lk_widget_geom *geom;

  struct lk_menu_state *menu; /* the context-menu popup's items
                                 (docs/context-menu.md); lazily
                                 allocated, freed with the ui */
  lk_u32 geom_cap;

  /* Layout rects of the current tree, owned by the ui and sized by
   * lk_ui_rects (the geom pattern).  A host that lays out into this
   * array makes lk_node_rect answer for the frame -- the SDL run
   * loop does; tree-driving hosts may keep their own array instead,
   * in which case lk_node_rect reports nothing. */
  lk_rect *rects;
  lk_u32 rects_cap;

  /* Clipboard (optional, platform-specific) */
  lk_clipboard_get_fn clipboard_get;
  lk_clipboard_set_fn clipboard_set;
  void *clipboard_ud;

  /* Text backend (optional) — set via lk_ui_set_text_backend so widget
   * event handlers (which receive no lk_layout_cfg) can do geometry
   * queries like click-to-position.  NULL disables those behaviors. */
  const struct lk_text_backend *text;

  /* Monotonic frame time in milliseconds, stamped by the backend once
   * per frame via lk_ui_set_time_ms (the SDL run loop does).  Core
   * never reads clocks itself; 0 until a backend stamps it.  Wraps at
   * 2^32 — compare durations with subtraction (now - then), never
   * with an ordering test. */
  lk_u32 time_ms;

  /* Overlay stack — topmost is last.  Persists across frames;
   * lk_ui_end_frame pops overlays whose owner node was removed. */
  lk_overlay *overlays;
  lk_u32 overlay_count;
  lk_u32 overlay_cap;

  /* Pending synthetic-event queue (FIFO).  Synthetic emissions
   * (VALUE_CHANGED, FOCUS_CHANGED) enqueue here instead of
   * re-entering lk_event_route mid-dispatch; the queue drains at the
   * end of the OUTERMOST lk_event_route call, or explicitly via
   * lk_ui_flush_events for mutations outside any routing (host API
   * calls between frames, the end-of-frame focus GC).  pending_dropped
   * counts events discarded by the per-drain safety cap (see
   * lk_event_enqueue). */
  lk_event *pending;
  lk_u32 pending_count;
  lk_u32 pending_cap;
  lk_u32 route_depth;     /* lk_event_route re-entrancy depth */
  lk_u32 pending_dropped; /* debug counter, never reset by lk */
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

/* The ui's resource table (created at lk_ui_create, destroyed with the
 * ui; both trees borrow it).  NULL only if ui is NULL. */
lk_resources *lk_ui_resources(lk_ui *ui);

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
 *
 * (lk_rect / lk_size are defined near the top of this header so the
 * overlay types can use them.)
 **/

/**
 * Text backend contract — see docs/text-contract.md §4.1.
 *
 * Contract rules:
 * - run is a single-line UTF-8 run (no newlines; caller splits).
 * - byte_ix in/out values are always codepoint-boundary-aligned.
 *   index_from_x snaps to the NEAREST boundary (not floor) and clamps
 *   out-of-range x to 0 / run.len.
 * - font_id = 0 means the default face; font_size = 0 means the
 *   face's default size.
 * - x_from_index(run, run.len) equals measure(run).w.
 * - register_font returns a new font_id (>= 1); 0 is both "default
 *   face" and the failure sentinel.
 **/

typedef struct lk_text_metrics {
  lk_i32 w, h;     /* tight box of the run */
  lk_i32 baseline; /* top of box -> baseline, px */
} lk_text_metrics;

typedef struct lk_text_backend {
  void *ud;
  void (*measure)(void *ud, lk_str run, lk_u16 font_id, lk_u16 font_size,
                  lk_text_metrics *out);
  lk_i32 (*x_from_index)(void *ud, lk_str run, lk_u16 font_id,
                         lk_u16 font_size, lk_u32 byte_ix);
  lk_u32 (*index_from_x)(void *ud, lk_str run, lk_u16 font_id,
                         lk_u16 font_size, lk_i32 x);
  lk_i32 (*line_height)(void *ud, lk_u16 font_id, lk_u16 font_size);
  lk_u16 (*register_font)(void *ud, const char *path);
} lk_text_backend;

/**
 * Per-frame widget geometry scratch.
 *
 * Derived geometry the layout passes compute for render and event
 * handling -- never retained, never host-written.  A parallel array
 * indexed by lk_ix, allocated alongside rects[]: lk_ui owns one
 * (lk_ui_geom) that the SDL run loop wires up automatically;
 * tree-driving hosts may supply their own.  lk_layout zeroes
 * cfg->geom and widgets fill their own slots; lk_render_build passes
 * each node its slot, and widget EVENT handlers read ui->geom -- so
 * hosts that route events must pass lk_ui_geom(ui) as cfg->geom.
 *
 * cfg->geom == NULL is always safe; widgets degrade: the text-input
 * cursor bar and selection highlight do not render and
 * click-to-position bubbles, the dropdown treats every
 * expanded-trigger click as a toggle and its popup cannot
 * wheel-scroll, split dividers cannot be dragged, scroll
 * containers render no bar and ignore wheel events, and labels /
 * buttons / text inputs draw their text at START regardless of
 * text_align (the run size lives here).
 *
 * Field meaning is per-kind; each union arm is owned and documented
 * by its widget's file.
 */
typedef union lk_widget_geom {
  struct {
    lk_i32 cursor_x, sel_x0, sel_x1; /* px from text origin (measure) */
    lk_i32 origin_x;                 /* text origin x incl. the
                                        text_align offset (layout) */
    lk_i32 run_w, run_h;             /* measured text run size (measure) */
    lk_u16 font_id, font_size;       /* resolved font (layout) */
    lk_u8 placed;                    /* 1 once layout stored origin/font */
  } text;                            /* UIK_TEXT_INPUT (lk-text-input.c) */
  struct {
    lk_i32 w, h; /* measured text run size (measure); render places it
                    per style->text_align / text_valign */
  } run;         /* UIK_LABEL / UIK_BUTTON (lk-widget.c) */
  struct {
    lk_i32 x, y, w, h;   /* trigger rect; w > 0 marks validity */
    lk_u16 row_h, inset; /* popup option row height / content inset */
  } trigger;             /* UIK_DROPDOWN (lk-dropdown.c) */
  struct {
    lk_i32 x, y, w, h; /* content rect; w > 0 marks validity */
  } content;           /* UIK_SPLIT_H / UIK_SPLIT_V (lk-split.c) */
  struct {
    lk_i32 max; /* max scroll offset (0 = content fits) */
  } scroll;     /* UIK_SCROLL (lk-scroll.c) */
  struct {
    lk_i32 x, y, w, h; /* track rect (thumb travel); w > 0 = valid */
  } track;             /* UIK_SLIDER (lk-slider.c) */
  struct {
    lk_i32 x, y, w, h; /* this TAB's header cell in the parent's
                          strip; w > 0 marks validity */
  } header;            /* UIK_TAB (lk-tabs.c) */
  struct {
    lk_i32 h; /* strip (header row) height incl. separator */
  } strip;    /* UIK_TABS (lk-tabs.c) */
  struct {
    const struct lk_text_backend *tb; /* the backend measure wrapped
                                         with (measure); render re-derives
                                         the rows through it.  NULL geom
                                         = unwrapped rows. */
  } styled;                           /* UIK_STYLED_TEXT (lk-styled-text.c) */
  struct {
    lk_i32 first, count; /* visible row window (layout) */
    lk_i32 row_h, max;   /* row height; max scroll offset */
    lk_i32 x, y, w, h;   /* content rect (bar geometry, ensure-visible) */
    lk_u8 placed;
  } list;                /* UIK_LIST (lk-list.c) */
} lk_widget_geom;

typedef struct lk_layout_cfg {
  const lk_text_backend *text;   /* NULL = all text measures as 0x0 */
  lk_i32 viewport_w, viewport_h;
  const struct lk_style *styles; /* NULL = read tree props directly */
  lk_state *state;               /* NULL ok; widgets may use for cursor etc. */
  lk_widget_geom *geom;          /* NULL ok; per-frame geometry scratch,
                                    >= t->node_count entries like rects[].
                                    Pass lk_ui_geom(ui) when widget event
                                    handlers should see this layout's
                                    geometry. */
} lk_layout_cfg;

/* The ui's per-frame geometry scratch, grown to cover the CURRENT
 * tree (call after lk_ui_end_frame, before lk_layout, each frame).
 * Returns the array to pass as cfg->geom -- widget event handlers
 * read the same array through the ui, which is what makes
 * geometry-dependent events (click-to-position, divider drags, popup
 * scrolling) work.  NULL only on NULL ui or allocation failure. */
lk_widget_geom *lk_ui_geom(lk_ui *ui);

/* The ui's rects array for the CURRENT tree, grown on demand (call
 * after lk_ui_end_frame, before lk_layout, like lk_ui_geom).  Laying
 * out into it is what makes lk_node_rect work.  NULL only on NULL ui
 * or allocation failure. */
lk_rect *lk_ui_rects(lk_ui *ui);

/* The last laid-out rect of the node with stable id `id` in the
 * current tree, from the ui-owned rects array.  Returns 1 and fills
 * *out when the node exists and lk_ui_rects covers it; 0 (out
 * untouched) when the id is unknown or the host never laid out into
 * lk_ui_rects.  A node the layout skipped -- hidden subtree,
 * collapsed tab page -- reports its zero rect with 1.  Hosts that
 * re-layout after events (the SDL loop) leave the post-event rects
 * here, i.e. what the frame rendered. */
int lk_node_rect(const lk_ui *ui, lk_node_id id, lk_rect *out);

/* Compute layout rects for every node in the tree. rects[] must be
 * at least t->node_count elements (indexed by lk_ix).  Returns 1 on
 * success, 0 on failure.  Subtrees whose root carries UIP_HIDDEN are
 * skipped (their rects stay zeroed and they do not participate in
 * parent stacking).
 */
int lk_layout(const lk_tree *t, const lk_layout_cfg *cfg, lk_rect *rects);

/* Measure and lay out just the subtree rooted at subtree_root, placing
 * its root at (origin_x, origin_y).  Ignores UIP_HIDDEN on the subtree
 * root itself (hidden descendants inside the subtree are still
 * skipped), so overlay content subtrees — which are hidden from the
 * main passes — can be laid out at their resolved anchor.
 *
 * rects[] must be at least t->node_count elements; only the subtree's
 * slots are written (the rest is left untouched), so the shared
 * rects array from lk_layout may be passed directly.  Returns 1 on
 * success, 0 on failure.
 */
int lk_layout_subtree(const lk_tree *t, const lk_layout_cfg *cfg,
                      lk_ix subtree_root, lk_i32 origin_x, lk_i32 origin_y,
                      lk_rect *rects);

/* Deterministic monospace stub backend for headless tests:
 * 8 px advance per CODEPOINT (not per byte), h = 16, baseline = 12,
 * line_height = 16, independent of font_id/font_size (all faces and
 * sizes identical — documented stub behavior).  register_font hands
 * out 1, 2, 3, ... from a process-global counter. */
const lk_text_backend *lk_text_backend_stub(void);

/* Install the text backend on the UI context so widget event handlers
 * can do geometry queries (e.g. text input click-to-position via
 * index_from_x).  Hosts that drive layout/events themselves should
 * pass the same backend they put in lk_layout_cfg.text; the SDL run
 * loop does this automatically.  NULL disables click-to-position. */
void lk_ui_set_text_backend(lk_ui *ui, const lk_text_backend *text);

/* Stamp the monotonic frame time in milliseconds.  Called by the
 * backend once per frame (the SDL run loop stamps SDL_GetTicks at the
 * top of the loop, next to lk_ui_clear_commands); core never reads
 * clocks itself.  The value wraps at 2^32 — callers compare durations
 * with subtraction (now - then), never with an ordering test. */
void lk_ui_set_time_ms(lk_ui *ui, lk_u32 ms);

/* The last stamped frame time (0 before the first stamp). */
lk_u32 lk_ui_time_ms(const lk_ui *ui);

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
  lk_color accent; /* emphasis color: check marks, radio dots, slider
                      fill + thumb, the selected tab's underline */
  lk_u32 font_id;
  lk_i32 font_size, padding, gap, border_width, border_radius;
  lk_u8 align, justify;
  lk_u8 text_align, text_valign; /* lk_align: placement of a leaf's text
                                    run inside its content box (label,
                                    button, text_input); START default */
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
#define LK_SF_ACCENT (1u << 13)
#define LK_SF_TEXT_ALIGN (1u << 14)
#define LK_SF_TEXT_VALIGN (1u << 15)

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
  LK_ROP_CLIP_END,
  LK_ROP_DRAW_RUN, /* like DRAW_TEXT, but the bytes live in the render
                      list's own arena (run_off/run_len into rl->bytes)
                      — never interned.  See docs/editor.md §8. */
  LK_ROP_DRAW_IMAGE, /* blit an application-owned lk_image into rect.
                       img_id/img_gen carry the lk_resource_ref; the
                       consumer resolves it against the ui's table at
                       draw time (a stale ref draws nothing).  color is
                       a tint/alpha modulate — widgets emit opaque
                       white.  See docs/image-widget.md. */
  LK_ROP_DRAW_LINES /* stroke a polyline: run_off/run_len index packed
                       lk_i32 x,y pairs in the render list's byte arena
                       (run_len in BYTES, a multiple of 8, >= 16 —
                       at least two points), window coordinates.
                       rect = bounding box (for culling), color = the
                       stroke color, stroke = width in px (0/1 =
                       hairline).  See docs/canvas.md. */
} lk_render_op;

typedef struct lk_render_cmd {
  lk_u8 op; /* lk_render_op */
  lk_rect rect;
  lk_color color;
  lk_u32 str_id;     /* for DRAW_TEXT: interned string ID */
  lk_u16 font_id;    /* for DRAW_TEXT/DRAW_RUN: face from resolved style
                        (0 = default) */
  lk_u16 font_size;  /* for DRAW_TEXT/DRAW_RUN: size from resolved style
                        (0 = default) */
  lk_u32 run_off;    /* for DRAW_RUN: byte offset into rl->bytes */
  lk_u32 run_len;    /* for DRAW_RUN: run length in bytes */
  lk_u32 img_id;     /* for DRAW_IMAGE: lk_resource_ref.id of the
                        lk_image (0 on every other op) */
  lk_u32 img_gen;    /* for DRAW_IMAGE: lk_resource_ref.generation.
                        NOTE: this is the REF generation (stale-handle
                        detection), not the pixel-dirty generation —
                        consumers compare lk_image_generation() of the
                        resolved image against their own cache. */
  lk_u8 img_filter;  /* for DRAW_IMAGE: lk_image_filter (0 = linear) */
  lk_u8 stroke;      /* for DRAW_LINES: width in px (0/1 = hairline) */
} lk_render_cmd;

typedef struct lk_render_list {
  lk_render_cmd *cmds;
  lk_u32 count;
  lk_u32 cap;
  char *bytes; /* frame-local run arena: the list OWNS these bytes, so
                  it stays a self-contained value — inspectable,
                  capturable, consumable late.  Reset (capacity kept)
                  by lk_render_build alongside count. */
  lk_u32 bytes_count;
  lk_u32 bytes_cap;
} lk_render_list;

/* Build a render list from a laid-out tree.  rects[] indexed by lk_ix
 * (from lk_layout).  Reuses existing capacity in out; resets count.
 * state may be NULL; passed through to widget render functions.
 * geom is the per-frame geometry scratch the layout pass filled
 * (cfg->geom; NULL degrades as documented on lk_widget_geom).
 * Returns 1 on success, 0 on failure.
 */
int lk_render_build(const lk_tree *t, const lk_rect *rects,
                    const lk_style *styles, const lk_state *state,
                    const lk_widget_geom *geom, lk_render_list *out);

/* Append overlay render commands to an existing render list, iterating
 * the ui's overlay stack bottom-to-top (topmost draws last).  Call
 * after lk_render_build so overlays draw on top of the main tree.
 * cfg supplies text backend, viewport, styles, and state.
 * See docs/overlays.md. */
int lk_render_build_overlays(lk_ui *ui, const lk_rect *rects,
                             const lk_layout_cfg *cfg, lk_render_list *out);

/* Push a command to the render list (grows as needed). Returns 1 on
 * success, 0 on allocation failure. */
int lk_render_list_push(lk_render_list *rl, lk_render_cmd cmd);

/* Copy len bytes into the render list's run arena (grows with the
 * same doubling pattern as the cmd array; capacity is reused across
 * frames).  Writes the run's starting offset to *out_off and returns
 * 1; returns 0 on allocation failure (arena unchanged).  len 0 is
 * legal: writes the current offset, copies nothing, returns 1.
 * (docs/editor.md §8 sketches "returns offset"; the out-param form is
 * a deliberate deviation so failure isn't ambiguous with offset 0.) */
int lk_render_list_push_run(lk_render_list *rl, const char *ptr, lk_u32 len,
                            lk_u32 *out_off);

/* Free the cmds array and the run arena. Safe to call on a zeroed
 * struct. */
void lk_render_list_destroy(lk_render_list *rl);

/* LK_VERSION_STRING of the linked library (not the header you compiled
 * against). */
const char *lk_version(void);

/**
 * Vector canvas (docs/canvas.md)
 *
 * lk_canvas is an APPLICATION-OWNED display list: a retained op
 * buffer (lines, polylines, rects, text) in canvas-local integer
 * pixels, replayed every frame by the UIK_CANVAS leaf that references
 * it through a UIP_CANVAS resource ref.  The image-track ownership
 * model without the pixel cache: there is no dirty generation, the
 * widget always replays what is there.  Rebuild when your data
 * changes (lk_canvas_clear keeps capacity, so a warm rebuild does
 * not allocate).
 **/

typedef struct lk_canvas lk_canvas;

/* Per-polyline point cap (lk_canvas_polyline rejects larger). */
#define LK_CANVAS_MAX_POINTS 65536u
/* Sub-clip nesting cap (lk_canvas_clip_begin rejects deeper).  The
 * SDL consumer's clip stack is 32 deep and the canvas sits inside the
 * tree's own clips, so keep the canvas's share small. */
#define LK_CANVAS_MAX_CLIP_DEPTH 8u

/* Create a canvas with a size HINT of w x h (what the widget measures
 * at; 0..16384 per axis, 0 is fine when the node is sized by grow /
 * UIP_W / UIP_H).  The hint never clips or scales content.  NULL
 * alloc/dealloc use the system allocator. */
lk_canvas *lk_canvas_new(lk_u32 w, lk_u32 h, void *(*alloc)(void *, lk_u32),
                         void (*dealloc)(void *, void *), void *ud);
void lk_canvas_destroy(lk_canvas *c);
void lk_canvas_size(const lk_canvas *c, lk_u32 *w, lk_u32 *h);
/* Returns 0 (unchanged) when a dimension exceeds 16384. */
int lk_canvas_set_size(lk_canvas *c, lk_u32 w, lk_u32 h);

/* Drop every op, keep capacity. */
void lk_canvas_clear(lk_canvas *c);
lk_u32 lk_canvas_op_count(const lk_canvas *c);

/* Append ops.  Coordinates are canvas-local pixels (origin = node
 * rect top-left); stroke is a width in pixels (0 and 1 = hairline).
 * Each returns 1 on success, 0 on allocation failure or bad args
 * (fewer than 2 polyline points, more than LK_CANVAS_MAX_POINTS, NULL
 * xy) — the list is unchanged on 0. */
int lk_canvas_line(lk_canvas *c, lk_i32 x0, lk_i32 y0, lk_i32 x1, lk_i32 y1,
                   lk_color color, lk_u8 stroke);
int lk_canvas_polyline(lk_canvas *c, const lk_i32 *xy, lk_u32 n_points,
                       lk_color color, lk_u8 stroke);
int lk_canvas_rect(lk_canvas *c, lk_rect r, lk_color color, lk_u8 stroke);
int lk_canvas_fill_rect(lk_canvas *c, lk_rect r, lk_color color);
/* Text bytes are copied into the canvas (never interned); the widget
 * draws them with the node's resolved font.  len 0 appends nothing
 * and returns 1. */
int lk_canvas_text(lk_canvas *c, lk_i32 x, lk_i32 y, const char *ptr,
                   lk_u32 len, lk_color color);
/* Sub-clip: ops appended between begin and the matching end are
 * clipped to r (canvas-local; intersected with the node's clip by the
 * consumer).  Nests to LK_CANVAS_MAX_CLIP_DEPTH; begin returns 0
 * beyond that or on allocation failure, end returns 0 when no clip is
 * open (the list is unchanged on 0).  A clip still open at render time
 * is closed automatically after the last op, so a forgotten end never
 * leaks past the canvas.  lk_canvas_clear forgets open clips. */
int lk_canvas_clip_begin(lk_canvas *c, lk_rect r);
int lk_canvas_clip_end(lk_canvas *c);
lk_u32 lk_canvas_clip_depth(const lk_canvas *c);

/* Resource type descriptor for lk_resource_register/get. */
const lk_resource_type *lk_canvas_type(void);

/* Resolve a node's UIP_CANVAS ref to the live canvas, NULL when the
 * prop is absent or the ref is stale/wrong-typed. */
lk_canvas *lk_canvas_from_node(const lk_resources *rs, const lk_tree *t,
                               lk_ix n);

/**
 * Widget definition — per-kind vtable for measure, layout, render.
 **/

#define LK_KIND_MAX 32

/**
 * Styled text (docs/styled-text.md)
 *
 * One span vocabulary for the editor's viewport snapshots and the
 * STYLED_TEXT kind's app-owned span set.
 **/

#define LK_SPAN_FG (1u << 0)
#define LK_SPAN_BG (1u << 1)
#define LK_SPAN_UNDERLINE (1u << 2)

/* One styled byte range [start, end).  Appearance only -- semantic
 * identity rides beside it (an annotation, or an lk_spans entry's
 * presentation), never inside it. */
typedef struct lk_text_span {
  lk_u32 start, end;
  lk_color fg, bg;
  lk_u8 flags; /* LK_SPAN_FG | LK_SPAN_BG | LK_SPAN_UNDERLINE */
} lk_text_span;

typedef enum lk_wrap_mode {
  LK_WRAP_NONE = 0,
  LK_WRAP_CHARACTER,
  LK_WRAP_WORD
} lk_wrap_mode;

/* lk_spans: an APPLICATION-OWNED, sorted, non-overlapping list of
 * byte-range spans over a STYLED_TEXT's UIP_TEXT.  Each entry carries
 * appearance (an lk_text_span; flags 0 = none) and optionally a
 * presentation (an interned ptype + typed value) -- the span set is
 * the presentation carrier for prop-text the way the annot store is
 * for documents.  Overlap is rejected at add time, so the renderer
 * never checks.  No revision: text and spans come from the same
 * frame; a span past the text's end is clamped at render. */
typedef struct lk_spans lk_spans;

lk_spans *lk_spans_new(void *(*alloc)(void *, lk_u32),
                       void (*dealloc)(void *, void *), void *ud);
void lk_spans_destroy(lk_spans *s);
/* Drop every entry (releasing presented values), keep capacity. */
void lk_spans_clear(lk_spans *s);
lk_u32 lk_spans_count(const lk_spans *s);
/* Insert [start, end) in order.  0 (unchanged) when start >= end, the
 * range overlaps an existing entry, or allocation fails.  An entry
 * with the IDENTICAL range merges instead: add() replaces the given
 * appearance fields, add_present() replaces the presentation
 * (releasing the old value) -- so one range can carry both. */
int lk_spans_add(lk_spans *s, lk_u32 start, lk_u32 end, lk_color fg,
                 lk_color bg, lk_u8 flags);
/* Insert a presentation-carrying entry with no appearance (flags 0);
 * type_id 0 is rejected.  Same ordering / overlap rules. */
int lk_spans_add_present(lk_spans *s, lk_u32 start, lk_u32 end,
                         lk_u32 type_id, lk_value value);
/* Entry i (0..count-1) in start order; NULL out of range. */
const lk_text_span *lk_spans_get(const lk_spans *s, lk_u32 i);
/* Entry i's presentation: 1 + fills when it carries one, else 0. */
int lk_spans_present_get(const lk_spans *s, lk_u32 i, lk_u32 *type_id,
                         lk_value *value);
/* Presentation candidates covering byte pos, shortest range first
 * (insertion order breaks ties), as hits with type_id / value / locus
 * {start, end, 0, 0, 0, 0} and locus_kind 0 (the widget names it). */
lk_u32 lk_spans_present_at(const lk_spans *s, lk_u32 pos,
                           struct lk_presentation_hit *out, lk_u32 cap);
/* Fires once per presented value as it is detached (clear, destroy). */
void lk_spans_set_release(lk_spans *s, void (*fn)(void *ud, lk_value v),
                          void *ud);
const lk_resource_type *lk_spans_type(void);
lk_spans *lk_spans_from_node(const lk_resources *rs, const lk_tree *t,
                             lk_ix n);

/* Byte position under a window point in a STYLED_TEXT node, from the
 * ui-owned rects (lk_ui_rects) and text backend: 1 + pos, or 0 when
 * the node is unknown, not laid out into the ui array, or the point
 * is outside its rect. */
int lk_styled_text_pos_at(const lk_ui *ui, lk_node_id id, lk_i32 x,
                          lk_i32 y, lk_u32 *out_pos);

/**
 * Context menus (docs/context-menu.md)
 *
 * "What can I do with this?": the producer lists every command the
 * translator table would emit for a gesture on the thing under a
 * point -- interior presentations first, then the node walk, then
 * the global key translators -- and the popup shows them; choosing
 * one emits the command exactly as the gesture would have.
 **/

#define LK_MENU_NO_TRANSLATOR 0xFFFFFFFFu
#define LK_MENU_MAX_ITEMS 64u /* lk_menu_open copies at most this many */

typedef struct lk_menu_item {
  lk_u32 label;        /* interned (v1: the command name) */
  lk_u32 command_name; /* interned */
  lk_u32 accel;        /* interned chord / gesture text, 0 = none */
  lk_u8 enabled;
  lk_u8 separator;     /* a rule, not a choice */
  /* activation record: */
  lk_u32 translator_ix; /* into the ui's table, or LK_MENU_NO_TRANSLATOR
                           for an explicit item */
  lk_node_id node_id;   /* the node it applies to (re-resolved when
                           chosen; gone = cancelled) */
  lk_u32 ptype;
  lk_value args[LK_PRES_MAX_ARGS];
  lk_u8 arg_count;
  lk_presentation_hit hit; /* zeroed for node presentations */
} lk_menu_item;

/* The producer: candidates for a gesture at (x, y) on target, in the
 * order a click would consider them.  Returns the count written. */
lk_u32 lk_menu_candidates(lk_ui *ui, const lk_tree *t, lk_ix target,
                          lk_i32 x, lk_i32 y, lk_menu_item *out, lk_u32 cap);

/* Open the popup with explicit items (copied; at most 64), anchored per
 * `anchor` (LK_ANCHOR_AT_CURSOR at (x, y); BELOW/ABOVE the owner).
 * Replaces an open menu.  1 on success. */
int lk_menu_open(lk_ui *ui, lk_node_id owner, lk_u8 anchor, lk_i32 x,
                 lk_i32 y, const lk_menu_item *items, lk_u32 n);
/* Hit-test (x, y) in the ui-owned rects, run the producer, open at the
 * cursor.  Returns the item count; 0 = nothing applicable, no menu. */
lk_u32 lk_menu_open_context(lk_ui *ui, const lk_tree *t, lk_i32 x, lk_i32 y);
/* The keyboard opener: the focused node's rect centre. */
lk_u32 lk_menu_open_context_at_focus(lk_ui *ui, const lk_tree *t);
void lk_menu_close(lk_ui *ui);
int lk_menu_is_open(const lk_ui *ui);
lk_u32 lk_menu_count(const lk_ui *ui);
const lk_menu_item *lk_menu_item_get(const lk_ui *ui, lk_u32 i);
lk_i32 lk_menu_hover(const lk_ui *ui);       /* -1 = none */
void lk_menu_set_hover(lk_ui *ui, lk_i32 i); /* out of range / not
                                                choosable = none */
/* Choose item i: closes the menu and emits its command.  0 when the
 * menu is closed, i is out of range / a separator / disabled, or the
 * item's node has left the tree. */
int lk_menu_activate(lk_ui *ui, lk_u32 i);
/* The popup rect as last resolved by the overlay passes (zero when
 * no menu is open or it has not been laid out yet). */
lk_rect lk_menu_rect(const lk_ui *ui);

/**
 * Virtualized list (docs/table.md)
 **/

/* The visible row window of a LIST as of its last layout into the
 * ui-owned geometry: 1 + first/count, 0 when the node is unknown, not
 * a list, or not laid out yet.  The next frame builds those rows
 * (plus a margin) as children carrying UIP_ROW. */
int lk_list_range(const lk_ui *ui, lk_node_id id, lk_i32 *first,
                  lk_i32 *count);
/* Scroll so that `row` is inside the viewport (no-op when it is);
 * 0 for an unknown / unlaid-out list or a row out of range. */
int lk_list_scroll_to_row(lk_ui *ui, lk_node_id id, lk_i32 row);
/* Effective cursor row: LKS_CURSOR_ROW state > UIP_VALUE > -1. */
lk_i32 lk_list_cursor(const lk_tree *t, lk_ix n, const lk_state *state);

/* Height-for-width: the kind's fit_height hook when it has one (and
 * no UIP_H), else the measured sizes[n].h.  Containers propagate it
 * (COLUMN sums, ROW maxes, SCROLL lays out with it); layout_stack
 * uses it for column children's bases.  docs/styled-text.md section 2. */
lk_i32 lk_widget_fit_height(const lk_tree *t, lk_ix n, lk_i32 width,
                            const lk_size *sizes,
                            const struct lk_layout_cfg *cfg);

#define LK_TEXT_INPUT_MAX 1024

typedef struct lk_widget_def {
  void (*measure)(const lk_tree *t, lk_ix n, const lk_size *sizes,
                  const lk_layout_cfg *cfg, lk_i32 *out_w, lk_i32 *out_h);

  /* Position children within content rect (parent rect minus padding).
   * Returns 1 if children need recursive layout, 0 for leaves. */
  int (*layout)(const lk_tree *t, lk_ix n, const lk_size *sizes,
                const lk_rect *content, const lk_layout_cfg *cfg,
                lk_rect *rects);

  /* geom is THIS node's per-frame geometry slot (&cfg->geom[n]) or
   * NULL when the host laid out without a geometry scratch. */
  void (*render)(const lk_tree *t, lk_ix n, const lk_rect *rect,
                 const lk_style *style, const lk_state *state,
                 const lk_widget_geom *geom, lk_render_list *out);

  /* Widget-level event handler. Called at TARGET phase before the global
   * handler. Return 1 if handled (stops propagation), 0 to continue. */
  int (*event)(lk_ui *ui, const lk_tree *t, lk_ix n, lk_event *ev);

  lk_u8 clips; /* 1 if this node clips children */

  /* Optional height-for-width (docs/styled-text.md section 2): the
   * node's height when laid out `width` wide.  NULL = the measured
   * height stands.  Reached through lk_widget_fit_height. */
  lk_i32 (*fit_height)(const lk_tree *t, lk_ix n, lk_i32 width,
                       const lk_size *sizes, const lk_layout_cfg *cfg);

  /* Optional interior-presentation discovery (docs/context-menu.md
   * section 1): the sub-node candidates under window point (x, y),
   * in precedence order, with locus_kind stamped -- the query half of
   * what the widget's own POINTER_DOWN path offers to the matcher.
   * The context-menu producer calls it; NULL = none. */
  lk_u32 (*presentations_at)(lk_ui *ui, const lk_tree *t, lk_ix n, lk_i32 x,
                             lk_i32 y, lk_presentation_hit *out, lk_u32 cap);
} lk_widget_def;

void lk_widget_register(lk_kind kind, const lk_widget_def *def);
const lk_widget_def *lk_widget_get(lk_kind kind);

/**
 * Events — function declarations
 **/

lk_ix lk_hit_test(const lk_tree *t, const lk_rect *rects, lk_i32 x, lk_i32 y);

/* Overlay-aware hit-test: checks the ui's overlay stack topmost-first
 * before the caller falls back to the main tree.  Returns the
 * option/overlay-content node when an overlay is hit, or 0 when no
 * overlay contains the point.  cfg supplies text backend, viewport,
 * styles, and state. */
lk_ix lk_hit_test_overlay(lk_ui *ui, const lk_rect *rects,
                          const lk_layout_cfg *cfg, lk_i32 x, lk_i32 y);

/* Outcome of lk_overlay_dismiss_outside. */
#define LK_DISMISS_NONE 0      /* no overlay affected */
#define LK_DISMISS_DISMISSED 1 /* at least one overlay was dismissed */
#define LK_DISMISS_BLOCKED 2   /* a modal overlay consumed the click:
                                * caller must NOT route the event */

/* Handle a pointer-down at (x,y) with respect to the overlay stack.
 * Topmost-first: an overlay containing the point (or whose owner rect
 * contains it) stops processing; a dismissible overlay not containing
 * it is popped (dropdowns also get LKS_EXPANDED cleared); a
 * focus-trapping, non-dismissible overlay (modal) consumes the click
 * without dismissing.  Call before routing a pointer-down event.
 * Returns one of the LK_DISMISS_* codes above. */
int lk_overlay_dismiss_outside(lk_ui *ui, const lk_rect *rects,
                               const lk_layout_cfg *cfg, lk_i32 x, lk_i32 y);

/* Overlay stack manipulation.  push copies *ov onto the stack (returns
 * 1 on success, 0 on allocation failure or bad args); pop removes the
 * topmost overlay; pop_owner removes any overlay owned by owner_id
 * (returns 1 if one was removed).  top returns the topmost overlay or
 * NULL when the stack is empty. */
int lk_overlay_push(lk_ui *ui, const lk_overlay *ov);
void lk_overlay_pop(lk_ui *ui);
int lk_overlay_pop_owner(lk_ui *ui, lk_node_id owner_id);
const lk_overlay *lk_overlay_top(const lk_ui *ui);
lk_u32 lk_overlay_count(const lk_ui *ui);

/* Compute the final on-screen rect of an overlay of size
 * (content_w, content_h) anchored to owner_rect in a (vw, vh)
 * viewport.  BELOW flips above when it would overflow the bottom and
 * there is room above (ABOVE flips symmetrically); the result is then
 * clamped into the viewport on both axes.  offset.w/h override the
 * content size when non-zero.  Deterministic; pass vw/vh = 0 to skip
 * clamping on that axis. */
lk_rect lk_anchor_resolve(const lk_overlay *ov, lk_rect owner_rect,
                          lk_i32 vw, lk_i32 vh, lk_i32 content_w,
                          lk_i32 content_h);

void lk_event_init_pointer(lk_event *ev, lk_u8 type, lk_i32 x, lk_i32 y,
                           lk_u8 button);
void lk_event_init_key(lk_event *ev, lk_u8 type, lk_u16 keycode, lk_u8 mods);

void lk_event_route(lk_ui *ui, lk_event *event);

/* Append a synthetic event to the pending queue (copied by value).
 * Queued events run the normal tier sequence when the queue drains:
 * at the end of the outermost lk_event_route call, or via
 * lk_ui_flush_events.  Events enqueued DURING a drain append and
 * drain in the same loop, bounded by a safety cap of 64 dispatches
 * per drain — overflow is dropped and counted in ui->pending_dropped.
 * Returns 1 on success, 0 on allocation failure or NULL args. */
int lk_event_enqueue(lk_ui *ui, const lk_event *ev);

/* Drain the pending synthetic-event queue outside of routing — for
 * mutations that happen outside any event (focus changes from the
 * end_frame GC or host API calls between frames).  t is the tree to
 * route against (NULL falls back to lk_ui_tree).  A no-op while an
 * lk_event_route call is in progress (the outermost route drains). */
void lk_ui_flush_events(lk_ui *ui, const lk_tree *t);

void lk_ui_set_event_handler(lk_ui *ui, lk_event_handler_fn fn, void *ud);

/* Install clipboard callbacks (e.g. from SDL backend). */
void lk_ui_set_clipboard(lk_ui *ui, lk_clipboard_get_fn get_fn,
                         lk_clipboard_set_fn set_fn, void *ud);

int lk_focus_set(lk_ui *ui, const lk_tree *t, lk_node_id id);
void lk_focus_clear(lk_ui *ui);

/* Deferred focus: remember id and focus it from lk_ui_end_frame as
 * soon as a committed frame contains it as a focusable, enabled node
 * (lk_focus_set's rules).  The request stays pending until it is
 * satisfied, replaced by another request, or cancelled with id 0;
 * a successful explicit lk_focus_set also cancels it.  This is how an
 * app focuses a node it is about to build ("first-frame focus") --
 * without it, hosts had to retry lk_focus_set every frame until the
 * node existed.  Returns the previous pending id. */
lk_node_id lk_focus_request(lk_ui *ui, lk_node_id id);
lk_node_id lk_focus_next(lk_ui *ui, const lk_tree *t);
lk_node_id lk_focus_prev(lk_ui *ui, const lk_tree *t);
lk_ix lk_focus_current(const lk_ui *ui, const lk_tree *t);

void lk_hover_set(lk_ui *ui, lk_node_id id);
void lk_hover_clear(lk_ui *ui);

/* Pointer capture — while set, the host event loop targets
 * POINTER_MOVE/UP events at the captured node (bypassing hit-test)
 * and suppresses hover updates, so drag interactions (split dividers,
 * future sliders) keep receiving pointer events after the cursor
 * leaves the widget.  Tracked by stable lk_node_id like focus;
 * lk_ui_end_frame clears it when the captured node is removed (a
 * REMOVED+ADDED move in one changeset keeps the capture, mirroring
 * focus).  The SDL run loop wires the targeting automatically; hosts
 * that drive events themselves must honor lk_capture_current. */
void lk_capture_set(lk_ui *ui, lk_node_id id);
void lk_capture_clear(lk_ui *ui);
lk_node_id lk_capture_current(const lk_ui *ui);

/**
 * Translator + command API
 **/

void lk_ui_add_translator(lk_ui *ui, lk_u8 event_type, lk_u32 ptype,
                          lk_u16 node_kind, lk_u16 keycode, lk_u8 mods,
                          lk_u8 button, lk_u32 command_name);
void lk_ui_add_translator_s(lk_ui *ui, lk_u8 event_type, const char *ptype,
                            lk_u16 node_kind, lk_u16 keycode, lk_u8 mods,
                            lk_u8 button, const char *command_name);
void lk_ui_clear_translators(lk_ui *ui);

void lk_ui_set_command_handler(lk_ui *ui, lk_command_handler_fn fn, void *ud);

const lk_command_queue *lk_ui_commands(const lk_ui *ui);

/* Reset the queue: count and the UIV_TEXT dispatch arena (capacity
 * kept).  Any UIV_TEXT value referencing the queue arena dies here;
 * logged copies live on in the log's own arena. */
void lk_ui_clear_commands(lk_ui *ui);

/* Copy len bytes into the command queue's dispatch arena and return a
 * UIV_TEXT value referencing them (valid until lk_ui_clear_commands).
 * Returns a UIV_NONE value on allocation failure or NULL args.  len 0
 * is legal (empty text). */
lk_value lk_v_text(lk_ui *ui, const char *ptr, lk_u32 len);

/* Resolve a UIV_TEXT value carried by cmd against the right arena
 * (queue, or the log's copy when cmd is a logged command).  Returns
 * {NULL, 0} unless v is a valid in-bounds UIV_TEXT. */
lk_str lk_command_text(const lk_ui *ui, const lk_command *cmd, lk_value v);

/* Convenience: lk_command_text over cmd->args[idx]. */
lk_str lk_command_arg_text(const lk_ui *ui, const lk_command *cmd, lk_u8 idx);

void lk_ui_dump_commands(const lk_ui *ui, lk_write_fn wr, void *wr_ud);
const lk_command *lk_ui_command_log(const lk_ui *ui, lk_u32 *out_count);
void lk_ui_clear_command_log(lk_ui *ui);

/* Internal: translator dispatch (called from event routing) */
void lk_translate_event(lk_ui *ui, const lk_tree *t, lk_event *event);

/* THE interior-presentation matcher (docs/weft-surface.md §1.3):
 * consider ev against the candidate hits in the given order and emit
 * the first applicable translator's command — hit attached, hit value
 * as arg 0.  Returns 1 on the first emission (ev marked handled), 0
 * when no (hit, translator) pair matched.  Node-level presentation
 * routing (lk_translate_event) shares the same matching and emission
 * internals after its own single-candidate discovery. */
int lk_translate_presentations(lk_ui *ui, const lk_tree *t, lk_ix node,
                               lk_event *ev, const lk_presentation_hit *hits,
                               lk_u32 n);

#ifdef __cplusplus
}
#endif

#endif /* LK_H */
