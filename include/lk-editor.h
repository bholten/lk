#ifndef LK_EDITOR_H
#define LK_EDITOR_H

/*
 * lk-editor.h -- editor view + command layer + UIK_EDITOR widget
 * (editor track, stage B2; docs/editor.md sections 6, 7, 9).
 *
 * lk_editor is ONE view over an application-owned lk_document:
 * cursor byte offset (always codepoint-boundary-aligned), selection
 * anchor, sticky x-pixel for vertical motion, anchored viewport
 * {top_byte, y_offset} over visual rows, wrap mode + cache and
 * horizontal scroll (docs/editor-wrap.md), drag state, tab settings,
 * and a transient per-frame geometry block filled by the widget's
 * layout hook.
 *
 * The editor lives in the application environment (like the document
 * and history), never in lk_state or the intern pool.  The tree
 * references it through a typed resource ref carried by the
 * UIP_EDITOR prop; a missing, stale, or wrong-typed ref degrades to
 * background-only rendering and bubbled events.
 *
 * Editing semantics live in the command layer (section 6.1), not in
 * the widget event hook: input events translate to lk_editor_command
 * calls, each EDITING command brackets exactly one document
 * transaction (origin = 16 + command id), and one command is one
 * undo step.
 */

#include "lk-document.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct lk_editor lk_editor;

/**
 ** Anchored viewport (docs/editor.md section 9)
 **/

/* Not an absolute pixel offset: top_byte anchors the first (possibly
 * partially) visible VISUAL ROW -- it is a row-start byte offset (a
 * line start when wrapping is off), y_offset is the pixel offset into
 * that row, kept in [0, line_h).  Nothing ever multiplies
 * row_count * line_height into an lk_i32.  Edits transform the
 * anchor with RIGHT affinity: an insertion exactly at top_byte
 * shifts the anchor past the inserted bytes, so the content being
 * read stays at the top of the viewport. */
typedef struct lk_editor_viewport {
  lk_u32 top_byte;
  lk_i32 y_offset;
} lk_editor_viewport;

/**
 ** Lifecycle
 **/

/* NULL alloc/dealloc fall back to the system allocator.  doc is
 * required; hist may be NULL (undo/redo commands then no-op).  The
 * editor subscribes to the document and references both objects; it
 * owns neither. */
lk_editor *lk_editor_new(void *(*alloc)(void *, lk_u32),
                         void (*dealloc)(void *, void *), void *ud,
                         lk_document *doc, lk_edit_history *hist);

/* Unsubscribes from the document and frees the editor's own buffers.
 * Destroy the editor before the document it views. */
void lk_editor_destroy(lk_editor *e);

/**
 ** Accessors
 **/

lk_document *lk_editor_doc(const lk_editor *e);

/* Cursor byte offset (always on a codepoint boundary). */
lk_u32 lk_editor_cursor(const lk_editor *e);

/* Clamp pos to the document length, snap it down to a codepoint
 * boundary, and place the cursor there.  Clears the selection and
 * requests scroll-to-cursor (effective at the next layout). */
void lk_editor_set_cursor(lk_editor *e, lk_u32 pos);

/* 1 if a selection is active (writes the normalized [start, end)
 * range), 0 otherwise (out params untouched).  Out pointers may be
 * NULL. */
int lk_editor_selection(const lk_editor *e, lk_u32 *out_start,
                        lk_u32 *out_end);

/* The anchored viewport as last clamped by layout. */
lk_editor_viewport lk_editor_get_viewport(const lk_editor *e);

/* Request that the next layout scroll the viewport so the cursor is
 * visible.  (Cursor-moving commands set this themselves.) */
void lk_editor_scroll_to_cursor(lk_editor *e);

/* Tab settings (v1 pinned: TAB inserts spaces, tab_size = 4; literal
 * \t bytes render via segment-wise tab-stop expansion). */
lk_u32 lk_editor_tab_size(const lk_editor *e);

/**
 ** Wrap modes (docs/editor-wrap.md section 5)
 **/

typedef enum lk_editor_wrap_mode {
  LK_EDITOR_WRAP_NONE = 0,
  LK_EDITOR_WRAP_CHARACTER,
  LK_EDITOR_WRAP_WORD /* rejected until implemented */
} lk_editor_wrap_mode;

/* Set the wrap mode.  Returns 1 on success, 0 when the mode is
 * unsupported (WORD, for now).  Default: LK_EDITOR_WRAP_NONE. */
int lk_editor_set_wrap_mode(lk_editor *e, lk_editor_wrap_mode m);
lk_editor_wrap_mode lk_editor_wrap_mode_get(const lk_editor *e);

/* Invalidate all cached wrap layout (one generation bump, no sweep).
 * The escape hatch for backends that mutate their metrics in place
 * (font hot-reload, DPI change) -- such changes are invisible to the
 * wrap key, which only compares width/font/tab/backend-pointer. */
void lk_editor_invalidate_layout(lk_editor *e);

/**
 ** Styled spans (docs/editor.md section 10, stage C)
 **/

#define LK_SPAN_FG (1u << 0)
#define LK_SPAN_BG (1u << 1)
#define LK_SPAN_UNDERLINE (1u << 2)

/* One styled byte range [start, end).  Appearance only -- semantic
 * identity stays in the annotation layer (decorations are not
 * presentations). */
typedef struct lk_edit_span {
  lk_u32 start, end;
  lk_color fg, bg;
  lk_u8 flags; /* LK_SPAN_FG | LK_SPAN_BG | LK_SPAN_UNDERLINE */
} lk_edit_span;

/* Viewport-scoped span delivery: coordinates are valid at exactly one
 * document revision, and the producer says which range it resolved. */
typedef struct lk_edit_span_snapshot {
  lk_revision revision;           /* coordinates valid at this revision */
  lk_u32 range_start, range_end;  /* range the producer resolved */
  const lk_edit_span *spans;      /* sorted by start, non-overlapping */
  lk_u32 count;
} lk_edit_span_snapshot;

/* Deep-copy the snapshot into the editor, replacing any previous one
 * (NULL snap or count 0 clears).  Producer contract: spans sorted by
 * start and non-overlapping -- asserted under LK_EDITOR_DEBUG_ASSERTS,
 * trusted in release (sub-segment guards keep sloppy input from
 * emitting negative-width geometry, not from looking right).
 *
 * Staleness policy (pinned): a snapshot whose revision does not match
 * the document at geometry time is ignored entirely -- unstyled text
 * for a frame beats misplaced styling.  A snapshot covering only part
 * of the visible range styles what it covers.  Span boundaries that
 * land mid-codepoint are clamped down to a boundary. */
void lk_editor_set_spans(lk_editor *e, const lk_edit_span_snapshot *snap);

/**
 ** Interior presentations — the editor offering (weft-surface track
 ** S1; docs/weft-surface.md §1.5)
 **/

/* Install (copy by value) the presentation source consulted on
 * POINTER_DOWN.  NULL or a zeroed source clears.  The source's ud is
 * borrowed: the provider (e.g. the annot store behind
 * lk_annot_presentation_source) must outlive the editor's use of it.
 *
 * On POINTER_DOWN (any button), before its normal behavior, the
 * widget maps (x, y) -> byte position, queries the source, stamps
 * each hit's locus (below), and offers the candidates via
 * lk_translate_presentations.  A fired translation consumes the event
 * with NO focus change, NO cursor move, NO selection mutation, and NO
 * pointer capture.  No source, no candidates, or no matching
 * translator leaves the editor's behavior unchanged.
 *
 * "editor-range" locus packing (lk_presentation_hit.locus, with
 * locus_kind = the interned string "editor-range"):
 *   locus[0] = annotation id        (source-filled)
 *   locus[1] = range start byte     (source-filled)
 *   locus[2] = range end byte       (source-filled)
 *   locus[3] = hit byte position    (editor-filled)
 *   locus[4] = document revision hi (source-filled)
 *   locus[5] = document revision lo (source-filled)
 * The revision pair is the staleness evidence: a handler running
 * after further edits can compare it against the document's current
 * revision. */
void lk_editor_set_presentation_source(lk_editor *e,
                                       const lk_presentation_source *src);

/* Map window coordinates to a document byte position (pinned
 * contract, docs/weft-surface.md §1.5): (x, y) are in the same window
 * space pointer events use; the query resolves against the editor's
 * LAST COMPLETED layout snapshot (the transient geometry block, using
 * the text backend that layout ran with); returns 0 before the first
 * layout (or after an edit invalidated the snapshot) and when (x, y)
 * is outside the editor's laid-out rect — NO clamping; callers who
 * want nearest-position semantics use the widget's own click path.
 * Returns 1 and writes *out_pos otherwise. */
int lk_editor_pos_at(const lk_editor *e, lk_i32 x, lk_i32 y, lk_u32 *out_pos);

/**
 ** Resource integration (docs/editor.md section 5)
 **/

/* The type descriptor for registering editors in an lk_resources
 * table: lk_resource_register(rs, lk_editor_type(), e, "name"). */
const lk_resource_type *lk_editor_type(void);

/* Resolve the editor attached to node n: reads the UIP_EDITOR prop,
 * extracts the ref, resolves it via lk_resource_get with
 * lk_editor_type().  NULL on any failure (no prop, non-resource
 * value, stale ref, wrong type, NULL table). */
lk_editor *lk_editor_from_node(const lk_resources *rs, const lk_tree *t,
                               lk_ix n);

/**
 ** Command layer (docs/editor.md section 6.1)
 **/

typedef enum lk_editor_cmd_id {
  LK_ED_INSERT_TEXT = 0, /* arg: text */
  LK_ED_DELETE_BACKWARD,
  LK_ED_DELETE_FORWARD,
  LK_ED_DELETE_WORD_BACKWARD,
  LK_ED_DELETE_WORD_FORWARD,
  LK_ED_MOVE_LEFT, /* motion commands: arg->select extends selection */
  LK_ED_MOVE_RIGHT,
  LK_ED_MOVE_UP,
  LK_ED_MOVE_DOWN,
  LK_ED_MOVE_WORD_LEFT,
  LK_ED_MOVE_WORD_RIGHT,
  LK_ED_MOVE_LINE_START,
  LK_ED_MOVE_LINE_END,
  LK_ED_MOVE_DOC_START,
  LK_ED_MOVE_DOC_END,
  LK_ED_MOVE_PAGE_UP,
  LK_ED_MOVE_PAGE_DOWN,
  LK_ED_SELECT_ALL,
  LK_ED_CUT,
  LK_ED_COPY,
  LK_ED_PASTE,
  LK_ED_UNDO,
  LK_ED_REDO,
  LK_ED_SET_CURSOR,   /* arg: set_cursor */
  LK_ED_SCROLL_LINES, /* arg: lines (signed; does not move the cursor;
                         visual rows when wrapping) */
  /* Appended after v1 (values above are recorded as transaction
   * origins and must not shift): visual-row HOME/END.  Identical to
   * LINE_START/END when wrapping is off; the default keymap binds
   * HOME/END here. */
  LK_ED_MOVE_ROW_START,
  LK_ED_MOVE_ROW_END,
  LK_ED__COUNT
} lk_editor_cmd_id;

/* Command argument.  Only the field(s) relevant to the command are
 * read; memset to zero and fill what you need. */
typedef struct lk_editor_cmd_arg {
  struct {
    const char *ptr;
    lk_u32 len;
  } text; /* INSERT_TEXT */
  struct {
    lk_u32 pos;
    int extend;
  } set_cursor;  /* SET_CURSOR */
  lk_i32 lines;  /* SCROLL_LINES */
  int select;    /* motion commands: 1 extends the selection */
} lk_editor_cmd_arg;

/* Execute a command.  arg may be NULL for arg-less commands.  ui
 * supplies the clipboard vtable and the text backend for geometry
 * (word/vertical motion, page size); NULL ui degrades: clipboard
 * commands no-op, vertical motion falls back to the last-known line
 * height and a monospace column approximation.  Every EDITING command
 * wraps its document mutations in ONE lk_doc_begin/lk_doc_commit
 * bracket with origin 16 + cmd (one command = one transaction = one
 * undo step; no keystroke coalescing, matching weft).  Returns 1 if
 * the command did anything. */
int lk_editor_command(lk_editor *e, lk_ui *ui, lk_editor_cmd_id cmd,
                      const lk_editor_cmd_arg *arg);

/**
 ** Widget (UIK_EDITOR vtable; registered by lk_widget_init)
 **/

const lk_widget_def *lk_editor_widget(void);

/**
 ** Internal: widget integration (called by the UIK_EDITOR vtable in
 ** lk-editor-widget.c; not application API -- same precedent as
 ** lk_translate_event in lk.h).
 **/

/* Layout-hook body: resolve the anchored viewport against the content
 * rect, apply pending scroll-to-cursor, extract visible lines into
 * editor scratch, precompute tab-expanded run segments, cursor x/y,
 * and up-to-3 selection rects, and stamp the transient geometry
 * block.  All backend-dependent geometry happens here; render reads
 * pure geometry. */
void lk_editor_layout_node(lk_editor *e, const lk_tree *t, lk_ix n,
                           const lk_rect *content, const lk_layout_cfg *cfg);

/* Render-hook body (after the background fill): selection rects, one
 * DRAW_RUN per visible line segment through the render list's byte
 * arena, cursor bar when focused.  No backend access. */
void lk_editor_render_node(const lk_editor *e, const lk_tree *t, lk_ix n,
                           const lk_rect *rect, const lk_style *style,
                           const lk_state *state, lk_render_list *out);

/* Map a pointer position to a document byte offset using the
 * transient geometry (line from the viewport, byte via the segment-
 * aware index_from_x).  Returns 1 and writes *out_pos, or 0 when no
 * valid geometry exists (event should bubble). */
int lk_editor_hit_pos(const lk_editor *e, const lk_text_backend *tb, lk_i32 x,
                      lk_i32 y, lk_u32 *out_pos);

/* Offer interior presentation candidates at byte position pos to the
 * translator matcher: queries the installed source (up to 8 hits),
 * stamps locus_kind = interned "editor-range" and the hit position
 * into each, and calls lk_translate_presentations.  Returns 1 when a
 * translation fired (the widget must consume the event untouched),
 * 0 otherwise (no source, no candidates, or no match). */
int lk_editor_offer_presentations(lk_editor *e, lk_ui *ui, const lk_tree *t,
                                  lk_ix n, lk_event *ev, lk_u32 pos);

/* Drag flag (pointer-capture selection drags). */
void lk_editor_set_drag(lk_editor *e, int on);
int lk_editor_dragging(const lk_editor *e);

/* Horizontal wheel input in wheel ticks (one tick = 3 space
 * advances); positive scrolls right.  No-op while wrapping is
 * active.  The upper clamp (against the widest measured visible
 * line) is applied at the next layout. */
void lk_editor_scroll_x_wheel(lk_editor *e, lk_i32 ticks);

/* Current horizontal scroll in px (0 while wrapping). */
lk_i32 lk_editor_scroll_x(const lk_editor *e);

/* Debug/test observability: row count of the line's wrap-cache entry,
 * 0 when the line is unmeasured or stale at the current wrap
 * generation (or wrapping is off).  Lets tests assert WHICH lines a
 * pass measured without timing. */
lk_u32 lk_editor_wrap_rows(const lk_editor *e, lk_u32 line);

#ifdef __cplusplus
}
#endif

#endif /* LK_EDITOR_H */
