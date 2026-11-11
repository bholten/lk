#ifndef LK_EDITOR_H
#define LK_EDITOR_H

#include "lk-document.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct lk_editor lk_editor;

typedef struct lk_editor_viewport {
  lk_u32 top_byte;
  lk_i32 y_offset;
} lk_editor_viewport;

lk_editor *lk_editor_new(void *(*alloc)(void *, lk_u32),
                         void (*dealloc)(void *, void *), void *ud,
                         lk_document *doc, lk_edit_history *hist);

void lk_editor_destroy(lk_editor *e);

lk_document *lk_editor_doc(const lk_editor *e);

lk_u32 lk_editor_cursor(const lk_editor *e);

void lk_editor_set_cursor(lk_editor *e, lk_u32 pos);

int lk_editor_selection(const lk_editor *e, lk_u32 *out_start, lk_u32 *out_end);

lk_u32 lk_editor_caret_count(const lk_editor *e);

int lk_editor_caret(const lk_editor *e, lk_u32 i, lk_u32 *out_cursor,
                    lk_u32 *out_start, lk_u32 *out_end);

lk_editor_viewport lk_editor_get_viewport(const lk_editor *e);

void lk_editor_scroll_to_cursor(lk_editor *e);

lk_u32 lk_editor_tab_size(const lk_editor *e);

void lk_editor_set_editable(lk_editor *e, int on);
int lk_editor_editable(const lk_editor *e);

typedef enum lk_editor_wrap_mode {
  LK_EDITOR_WRAP_NONE = 0,
  LK_EDITOR_WRAP_CHARACTER,
  LK_EDITOR_WRAP_WORD

} lk_editor_wrap_mode;

int lk_editor_set_wrap_mode(lk_editor *e, lk_editor_wrap_mode m);
lk_editor_wrap_mode lk_editor_wrap_mode_get(const lk_editor *e);

void lk_editor_invalidate_layout(lk_editor *e);

typedef lk_text_span lk_edit_span;

typedef struct lk_edit_span_snapshot {
  lk_revision revision;
  lk_u32 range_start, range_end;
  const lk_edit_span *spans;
  lk_u32 count;
} lk_edit_span_snapshot;

void lk_editor_set_spans(lk_editor *e, const lk_edit_span_snapshot *snap);

void lk_editor_set_presentation_source(lk_editor *e,
                                       const lk_presentation_source *src);

int lk_editor_pos_at(const lk_editor *e, lk_i32 x, lk_i32 y, lk_u32 *out_pos);

const lk_resource_type *lk_editor_type(void);

lk_editor *lk_editor_from_node(const lk_resources *rs, const lk_tree *t,
                               lk_ix n);

typedef enum lk_editor_cmd_id {
  LK_ED_INSERT_TEXT = 0,
  LK_ED_DELETE_BACKWARD,
  LK_ED_DELETE_FORWARD,
  LK_ED_DELETE_WORD_BACKWARD,
  LK_ED_DELETE_WORD_FORWARD,
  LK_ED_MOVE_LEFT,
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
  LK_ED_SET_CURSOR,
  LK_ED_SCROLL_LINES,

  LK_ED_MOVE_ROW_START,
  LK_ED_MOVE_ROW_END,

  LK_ED_ADD_CURSOR_AT,

  LK_ED_ADD_CURSOR_ABOVE,
  LK_ED_ADD_CURSOR_BELOW,

  LK_ED_SELECT_NEXT_MATCH,
  LK_ED_COLLAPSE_CURSORS,

  LK_ED__COUNT
} lk_editor_cmd_id;

typedef struct lk_editor_cmd_arg {
  struct {
    const char *ptr;
    lk_u32 len;
  } text;
  struct {
    lk_u32 pos;
    int extend;
  } set_cursor;
  lk_i32 lines;
  int select;
} lk_editor_cmd_arg;

int lk_editor_command(lk_editor *e, lk_ui *ui, lk_editor_cmd_id cmd,
                      const lk_editor_cmd_arg *arg);

const lk_widget_def *lk_editor_widget(void);

#define LK_EDITOR_MODS_ALL (LK_MOD_SHIFT | LK_MOD_CTRL | LK_MOD_ALT)

typedef enum lk_editor_key_action {
  LK_EDK_COMMAND = 0,

  LK_EDK_INSERT_NEWLINE,
  LK_EDK_INSERT_TAB,
  LK_EDK_COLLAPSE_IF_MULTI

} lk_editor_key_action;

typedef struct lk_editor_key_binding {
  lk_u16 key;
  lk_u8 mods;
  lk_u8 mods_mask;
  lk_u8 cmd;
  lk_u8 action;
  lk_u8 shift_extends;

  const char *doc;
} lk_editor_key_binding;

typedef enum lk_editor_gesture {
  LK_EDG_CLICK = 0,
  LK_EDG_DRAG,
  LK_EDG_WHEEL
} lk_editor_gesture;

typedef enum lk_editor_pointer_action {
  LK_EDP_PLACE_CURSOR = 0,
  LK_EDP_ADD_CURSOR,
  LK_EDP_BOX_SELECT,
  LK_EDP_SCROLL,
  LK_EDP_SCROLL_X
} lk_editor_pointer_action;

typedef struct lk_editor_pointer_binding {
  lk_u8 gesture;
  lk_u8 button;
  lk_u8 mods;
  lk_u8 mods_mask;
  lk_u8 action;
  lk_u8 has_cmd;
  lk_u8 cmd;
  const char *doc;
} lk_editor_pointer_binding;

const lk_editor_key_binding *lk_editor_key_bindings(lk_u32 *count);
const lk_editor_pointer_binding *lk_editor_pointer_bindings(lk_u32 *count);

const lk_editor_key_binding *lk_editor_key_lookup(lk_u16 key, lk_u8 mods);
const lk_editor_pointer_binding *
lk_editor_pointer_lookup(lk_u8 gesture, lk_u8 button, lk_u8 mods);

void lk_editor_layout_node(lk_editor *e, const lk_tree *t, lk_ix n,
                           const lk_rect *content, const lk_layout_cfg *cfg);

void lk_editor_render_node(const lk_editor *e, const lk_tree *t, lk_ix n,
                           const lk_rect *rect, const lk_style *style,
                           const lk_state *state, lk_render_list *out);

int lk_editor_hit_pos(const lk_editor *e, const lk_text_backend *tb, lk_i32 x,
                      lk_i32 y, lk_u32 *out_pos);

lk_u32 lk_editor_presentations_at(lk_editor *e, const lk_tree *t, lk_u32 pos,
                                  lk_presentation_hit *out, lk_u32 cap);
int lk_editor_offer_presentations(lk_editor *e, lk_ui *ui, const lk_tree *t,
                                  lk_ix n, lk_event *ev, lk_u32 pos);

void lk_editor_set_drag(lk_editor *e, int on);
int lk_editor_dragging(const lk_editor *e);

int lk_editor_box_down(lk_editor *e, lk_ui *ui, lk_i32 x, lk_i32 y);
void lk_editor_box_move(lk_editor *e, lk_ui *ui, lk_i32 x, lk_i32 y);
int lk_editor_box_active(const lk_editor *e);
void lk_editor_box_end(lk_editor *e);

int lk_editor_scrollbar_down(lk_editor *e, lk_ui *ui, lk_i32 x, lk_i32 y);
void lk_editor_scrollbar_move(lk_editor *e, lk_ui *ui, lk_i32 y);
int lk_editor_scrollbar_dragging(const lk_editor *e);
void lk_editor_scrollbar_end(lk_editor *e);

void lk_editor_scroll_to_row(lk_editor *e, lk_ui *ui, lk_u32 row);

void lk_editor_scroll_x_wheel(lk_editor *e, lk_i32 ticks);

lk_i32 lk_editor_scroll_x(const lk_editor *e);

lk_u32 lk_editor_wrap_rows(const lk_editor *e, lk_u32 line);

#ifdef __cplusplus
}
#endif

#endif
