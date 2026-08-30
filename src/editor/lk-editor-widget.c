/*
 * lk-editor-widget.c -- UIK_EDITOR vtable (editor track, stage B2;
 * docs/editor.md section 7).
 *
 * The widget is PURE translation: it resolves the attached lk_editor
 * through the UIP_EDITOR resource ref and forwards to the command
 * layer (events) or the editor's layout/render bodies (geometry).  No
 * editing logic lives here.
 *
 * Degradation: a node with no ref, a stale ref, or a wrong-typed ref
 * renders background only and lets every event bubble (the split-pane
 * degradation convention).
 */

#include <string.h>

#include <lk-editor.h>

#define ED_WHEEL_LINES 3
#define ED_TAB_SPACES_MAX 16

/* ---- Measure ---- */

/* Greedy leaf: intrinsic 0x0, sized by its container.  W/H props are
 * honored here (and again by the engine's generic override). */
static void measure_editor(const lk_tree *t, lk_ix n, const lk_size *sizes,
                           const lk_layout_cfg *cfg, lk_i32 *out_w,
                           lk_i32 *out_h) {
  (void)sizes;
  (void)cfg;
  *out_w = lk_node_prop_i32(t, n, UIP_W, 0);
  *out_h = lk_node_prop_i32(t, n, UIP_H, 0);
}

/* ---- Layout ---- */

/* Leaf with a non-NULL hook: the engine calls layout whenever the
 * pointer is set, children or not, and this hook is where the final
 * content rect and cfg->text meet -- all backend-dependent geometry
 * (viewport resolve, visible-line extraction, tab segments, cursor
 * and selection pixels) is stashed in the editor's transient block
 * here.  Returns 0: no children to recurse into. */
static int layout_editor(const lk_tree *t, lk_ix n, const lk_size *sizes,
                         const lk_rect *content, const lk_layout_cfg *cfg,
                         lk_rect *rects) {
  lk_editor *e = lk_editor_from_node(t->resources, t, n);

  (void)sizes;
  (void)rects;

  if (e) {
    lk_editor_layout_node(e, t, n, content, cfg);
  }

  return 0;
}

/* ---- Render ---- */

static void render_editor(const lk_tree *t, lk_ix n, const lk_rect *rect,
                          const lk_style *style, const lk_state *state,
                          const lk_widget_geom *geom, lk_render_list *out) {
  const lk_editor *e;
  lk_render_cmd cmd;

  (void)geom; /* the editor owns its transient geometry block */

  /* Background fill (also the degraded rendering for a missing or
   * stale editor ref). */
  memset(&cmd, 0, sizeof(cmd));
  cmd.op = LK_ROP_FILL_RECT;
  cmd.rect = *rect;
  cmd.color = style->bg;
  lk_render_list_push(out, cmd);

  e = lk_editor_from_node(t->resources, t, n);

  if (!e) {
    return;
  }

  lk_editor_render_node(e, t, n, rect, style, state, out);
}

/* ---- Event translation ---- */

static int ed_cmd(lk_editor *e, lk_ui *ui, lk_editor_cmd_id cmd,
                  const lk_editor_cmd_arg *arg) {
  return lk_editor_command(e, ui, cmd, arg);
}

static int ed_motion(lk_editor *e, lk_ui *ui, lk_editor_cmd_id cmd,
                     int select) {
  lk_editor_cmd_arg arg;

  memset(&arg, 0, sizeof(arg));
  arg.select = select;

  return ed_cmd(e, ui, cmd, &arg);
}

static int ed_insert_bytes(lk_editor *e, lk_ui *ui, const char *ptr,
                           lk_u32 len) {
  lk_editor_cmd_arg arg;

  memset(&arg, 0, sizeof(arg));
  arg.text.ptr = ptr;
  arg.text.len = len;

  return ed_cmd(e, ui, LK_ED_INSERT_TEXT, &arg);
}

static int ed_set_cursor(lk_editor *e, lk_ui *ui, lk_u32 pos, int extend) {
  lk_editor_cmd_arg arg;

  memset(&arg, 0, sizeof(arg));
  arg.set_cursor.pos = pos;
  arg.set_cursor.extend = extend;

  return ed_cmd(e, ui, LK_ED_SET_CURSOR, &arg);
}

/* ---- Bindings as data (docs/editor-keys-reflection.md) ----
 *
 * These tables ARE the dispatch: event_editor_key walks ed_key_table,
 * the pointer/wheel cases classify through ed_pointer_table.  Rows
 * for one key are ordered most-specific first.  Docs are one line
 * and spelled in the Lk::editor_command vocabulary; keep them honest,
 * Weft prints them. */

#define EDK(key, mods, mask, cmd, act, ext, doc)                               \
  {(lk_u16)(key),                                                              \
   (lk_u8)(mods),                                                              \
   (lk_u8)(mask),                                                              \
   (lk_u8)(cmd),                                                               \
   (lk_u8)(act),                                                               \
   (lk_u8)(ext),                                                               \
   doc}

static const lk_editor_key_binding ed_key_table[] = {
    EDK(LKK_LEFT, LK_MOD_CTRL, LK_MOD_CTRL, LK_ED_MOVE_WORD_LEFT,
        LK_EDK_COMMAND, 1, "move one word left"),
    EDK(LKK_LEFT, 0, LK_MOD_CTRL, LK_ED_MOVE_LEFT, LK_EDK_COMMAND, 1,
        "move one character left"),
    EDK(LKK_RIGHT, LK_MOD_CTRL, LK_MOD_CTRL, LK_ED_MOVE_WORD_RIGHT,
        LK_EDK_COMMAND, 1, "move one word right"),
    EDK(LKK_RIGHT, 0, LK_MOD_CTRL, LK_ED_MOVE_RIGHT, LK_EDK_COMMAND, 1,
        "move one character right"),
    EDK(LKK_UP, LK_MOD_CTRL | LK_MOD_SHIFT, LK_MOD_CTRL | LK_MOD_SHIFT,
        LK_ED_ADD_CURSOR_ABOVE, LK_EDK_COMMAND, 0,
        "add a caret on the visual row above every caret"),
    EDK(LKK_UP, 0, 0, LK_ED_MOVE_UP, LK_EDK_COMMAND, 1,
        "move up one visual row"),
    EDK(LKK_DOWN, LK_MOD_CTRL | LK_MOD_SHIFT, LK_MOD_CTRL | LK_MOD_SHIFT,
        LK_ED_ADD_CURSOR_BELOW, LK_EDK_COMMAND, 0,
        "add a caret on the visual row below every caret"),
    EDK(LKK_DOWN, 0, 0, LK_ED_MOVE_DOWN, LK_EDK_COMMAND, 1,
        "move down one visual row"),
    EDK(LKK_HOME, LK_MOD_CTRL, LK_MOD_CTRL, LK_ED_MOVE_DOC_START,
        LK_EDK_COMMAND, 1, "move to the start of the document"),
    EDK(LKK_HOME, 0, LK_MOD_CTRL, LK_ED_MOVE_ROW_START, LK_EDK_COMMAND, 1,
        "move to the start of the visual row"),
    EDK(LKK_END, LK_MOD_CTRL, LK_MOD_CTRL, LK_ED_MOVE_DOC_END, LK_EDK_COMMAND,
        1, "move to the end of the document"),
    EDK(LKK_END, 0, LK_MOD_CTRL, LK_ED_MOVE_ROW_END, LK_EDK_COMMAND, 1,
        "move to the end of the visual row"),
    EDK(LKK_PAGEUP, 0, 0, LK_ED_MOVE_PAGE_UP, LK_EDK_COMMAND, 1,
        "move up one page"),
    EDK(LKK_PAGEDOWN, 0, 0, LK_ED_MOVE_PAGE_DOWN, LK_EDK_COMMAND, 1,
        "move down one page"),
    EDK(LKK_BACKSPACE, LK_MOD_CTRL, LK_MOD_CTRL, LK_ED_DELETE_WORD_BACKWARD,
        LK_EDK_COMMAND, 0, "delete the word before the caret"),
    EDK(LKK_BACKSPACE, 0, LK_MOD_CTRL, LK_ED_DELETE_BACKWARD, LK_EDK_COMMAND, 0,
        "delete the character before the caret (or the selection)"),
    EDK(LKK_DELETE, LK_MOD_CTRL, LK_MOD_CTRL, LK_ED_DELETE_WORD_FORWARD,
        LK_EDK_COMMAND, 0, "delete the word after the caret"),
    EDK(LKK_DELETE, 0, LK_MOD_CTRL, LK_ED_DELETE_FORWARD, LK_EDK_COMMAND, 0,
        "delete the character after the caret (or the selection)"),
    EDK(LKK_RETURN, 0, LK_MOD_CTRL | LK_MOD_ALT, LK_ED_INSERT_TEXT,
        LK_EDK_INSERT_NEWLINE, 0,
        "insert a newline (ctrl/alt chords bubble to the app)"),
    EDK(LKK_TAB, 0, LK_MOD_SHIFT, LK_ED_INSERT_TEXT, LK_EDK_INSERT_TAB, 0,
        "insert tab_size spaces (shift+tab bubbles for focus cycling)"),
    EDK(LKK_A, LK_MOD_CTRL, LK_MOD_CTRL, LK_ED_SELECT_ALL, LK_EDK_COMMAND, 0,
        "select the whole document"),
    EDK(LKK_C, LK_MOD_CTRL, LK_MOD_CTRL, LK_ED_COPY, LK_EDK_COMMAND, 0,
        "copy the selection(s) to the clipboard"),
    EDK(LKK_X, LK_MOD_CTRL, LK_MOD_CTRL, LK_ED_CUT, LK_EDK_COMMAND, 0,
        "cut the selection(s) to the clipboard"),
    EDK(LKK_V, LK_MOD_CTRL, LK_MOD_CTRL, LK_ED_PASTE, LK_EDK_COMMAND, 0,
        "paste the clipboard at every caret"),
    EDK(LKK_Z, LK_MOD_CTRL | LK_MOD_SHIFT, LK_MOD_CTRL | LK_MOD_SHIFT,
        LK_ED_REDO, LK_EDK_COMMAND, 0, "redo"),
    EDK(LKK_Z, LK_MOD_CTRL, LK_MOD_CTRL | LK_MOD_SHIFT, LK_ED_UNDO,
        LK_EDK_COMMAND, 0, "undo"),
    EDK(LKK_D, LK_MOD_CTRL, LK_EDITOR_MODS_ALL, LK_ED_SELECT_NEXT_MATCH,
        LK_EDK_COMMAND, 0,
        "select the next match of the primary selection, as a new caret"),
    EDK(LKK_ESCAPE, 0, 0, LK_ED_COLLAPSE_CURSORS, LK_EDK_COLLAPSE_IF_MULTI, 0,
        "collapse to the primary caret (only with more than one caret; "
        "otherwise bubbles)")};

#define EDP(gest, btn, mods, mask, act, has, cmd, doc)                         \
  {(lk_u8)(gest), (lk_u8)(btn), (lk_u8)(mods), (lk_u8)(mask),                  \
   (lk_u8)(act),  (lk_u8)(has), (lk_u8)(cmd),  doc}

static const lk_editor_pointer_binding ed_pointer_table[] = {
    EDP(LK_EDG_DRAG, LK_POINTER_BUTTON_PRIMARY, LK_MOD_ALT, LK_MOD_ALT,
        LK_EDP_BOX_SELECT, 0, 0,
        "box selection: one caret per visual row across the dragged "
        "rectangle (skips interior presentations)"),
    EDP(LK_EDG_CLICK, LK_POINTER_BUTTON_PRIMARY, LK_MOD_CTRL,
        LK_MOD_CTRL | LK_MOD_ALT, LK_EDP_ADD_CURSOR, 1, LK_ED_ADD_CURSOR_AT,
        "toggle a caret at the clicked position; an added caret drags to "
        "extend"),
    EDP(LK_EDG_CLICK, LK_POINTER_BUTTON_PRIMARY, 0, LK_MOD_CTRL | LK_MOD_ALT,
        LK_EDP_PLACE_CURSOR, 1, LK_ED_SET_CURSOR,
        "place the caret (interior presentations get first refusal); "
        "dragging extends the selection"),
    EDP(LK_EDG_WHEEL, 0, LK_MOD_SHIFT, LK_MOD_SHIFT, LK_EDP_SCROLL_X, 0, 0,
        "scroll horizontally (unwrapped mode only)"),
    EDP(LK_EDG_WHEEL, 0, 0, LK_MOD_SHIFT, LK_EDP_SCROLL, 1, LK_ED_SCROLL_LINES,
        "scroll the viewport three lines per notch")};

const lk_editor_key_binding *lk_editor_key_bindings(lk_u32 *count) {
  if (count) {
    *count = (lk_u32)(sizeof(ed_key_table) / sizeof(ed_key_table[0]));
  }

  return ed_key_table;
}

const lk_editor_pointer_binding *lk_editor_pointer_bindings(lk_u32 *count) {
  if (count) {
    *count = (lk_u32)(sizeof(ed_pointer_table) / sizeof(ed_pointer_table[0]));
  }

  return ed_pointer_table;
}

const lk_editor_key_binding *lk_editor_key_lookup(lk_u16 key, lk_u8 mods) {
  lk_u32 n = (lk_u32)(sizeof(ed_key_table) / sizeof(ed_key_table[0]));
  lk_u32 i;

  for (i = 0; i < n; i++) {
    const lk_editor_key_binding *b = &ed_key_table[i];

    if (b->key == key && (mods & b->mods_mask) == b->mods) {
      return b;
    }
  }

  return NULL;
}

const lk_editor_pointer_binding *
lk_editor_pointer_lookup(lk_u8 gesture, lk_u8 button, lk_u8 mods) {
  lk_u32 n = (lk_u32)(sizeof(ed_pointer_table) / sizeof(ed_pointer_table[0]));
  lk_u32 i;

  for (i = 0; i < n; i++) {
    const lk_editor_pointer_binding *b = &ed_pointer_table[i];

    if (b->gesture != gesture) {
      continue;
    }

    if (gesture != (lk_u8)LK_EDG_WHEEL && b->button != button) {
      continue;
    }

    if ((mods & b->mods_mask) == b->mods) {
      return b;
    }
  }

  return NULL;
}

static int event_editor_key(lk_editor *e, lk_ui *ui, lk_event *ev) {
  const lk_editor_key_binding *b =
      lk_editor_key_lookup(ev->data.key.keycode, ev->mods);

  if (!b) {
    /* unlisted, or a chord the row's mask reserves for the app
     * (ctrl+shift+d, ctrl+enter, shift+tab, ...) */
    return 0;
  }

  switch (b->action) {
  case LK_EDK_INSERT_NEWLINE: ed_insert_bytes(e, ui, "\n", 1); return 1;

  case LK_EDK_INSERT_TAB: {
    /* Plain TAB inserts tab_size spaces (v1 pinned policy); focus
     * escape is ESC-then-TAB. */
    char spaces[ED_TAB_SPACES_MAX];
    lk_u32 ts = lk_editor_tab_size(e);

    if (ts > ED_TAB_SPACES_MAX) {
      ts = ED_TAB_SPACES_MAX;
    }

    memset(spaces, ' ', (size_t)ts);
    ed_insert_bytes(e, ui, spaces, ts);

    return 1;
  }

  case LK_EDK_COLLAPSE_IF_MULTI:
    /* ESC collapses a multi-caret set (consumed); with a single
     * caret it bubbles exactly as before.  (An open overlay is
     * popped by the lk_event_route pre-step BEFORE widget dispatch,
     * so the popup wins the first press -- correct priority.) */
    if (lk_editor_caret_count(e) > 1) {
      ed_cmd(e, ui, (lk_editor_cmd_id)b->cmd, NULL);

      return 1;
    }

    return 0;

  default: break;
  }

  if (b->shift_extends) {
    ed_motion(e, ui, (lk_editor_cmd_id)b->cmd,
              (ev->mods & LK_MOD_SHIFT) ? 1 : 0);
  } else {
    ed_cmd(e, ui, (lk_editor_cmd_id)b->cmd, NULL);
  }

  return 1;
}

static int event_editor(lk_ui *ui, const lk_tree *t, lk_ix n, lk_event *ev) {
  lk_editor *e;
  lk_node_id nid;

  if (ev->target != n) {
    return 0;
  }

  e = lk_editor_from_node(t->resources, t, n);

  if (!e) {
    return 0; /* unresolvable editor: everything bubbles */
  }

  nid = t->nodes[n].id;

  switch (ev->type) {
  case LK_EVENT_TEXT:
    if (ev->data.text.len == 0) {
      return 0;
    }

    ed_insert_bytes(e, ui, ev->data.text.buf, ev->data.text.len);

    return 1;

  case LK_EVENT_KEY_DOWN: return event_editor_key(e, ui, ev);

  case LK_EVENT_POINTER_DOWN: {
    lk_u32 pos;
    lk_u8 btn = ev->data.pointer.button;
    const lk_editor_pointer_binding *pb;

    /* Overlay scrollbar first: a primary click on the bar never
     * reaches presentations or cursor placement.  Thumb = capture
     * drag (like a selection drag, but no focus/cursor change); track
     * = page toward the pointer. */
    if (btn == (lk_u8)LK_POINTER_BUTTON_ANY ||
        btn == (lk_u8)LK_POINTER_BUTTON_PRIMARY) {
      int sb = lk_editor_scrollbar_down(e, ui, ev->data.pointer.x,
                                        ev->data.pointer.y);

      if (sb) {
        if (sb == 2) {
          lk_capture_set(ui, nid);
        }

        return 1;
      }
    }

    if (!lk_editor_hit_pos(e, ui ? ui->text : NULL, ev->data.pointer.x,
                           ev->data.pointer.y, &pos)) {
      return 0; /* no layout geometry yet: bubble (click-to-focus) */
    }

    /* From here the gesture tables decide (an unspecified synthetic
     * button counts as primary). */
    if (btn == (lk_u8)LK_POINTER_BUTTON_ANY) {
      btn = (lk_u8)LK_POINTER_BUTTON_PRIMARY;
    }

    /* Alt+primary starts a box selection (stage E3) and deliberately
     * SKIPS the presentation offer: an explicit editing gesture, not
     * a click (docs/editor-multicursor.md section 9.3). */
    pb = lk_editor_pointer_lookup((lk_u8)LK_EDG_DRAG, btn, ev->mods);

    if (pb && pb->action == (lk_u8)LK_EDP_BOX_SELECT) {
      if (!lk_editor_box_down(e, ui, ev->data.pointer.x, ev->data.pointer.y)) {
        return 0;
      }

      lk_focus_set(ui, t, nid);
      lk_capture_set(ui, nid);

      return 1;
    }

    /* Interior presentations FIRST (docs/weft-surface.md section 1.5):
     * offer candidates at the hit position; a fired translation
     * consumes the click with no focus change, no cursor move, no
     * selection mutation, and no pointer capture (pinned).  This
     * outranks ctrl+click add-caret: an app translator bound to
     * ctrl+click keeps right-of-way. */
    if (lk_editor_offer_presentations(e, ui, t, n, ev, pos)) {
      return 1;
    }

    /* No translation: the click table decides; a button/chord with no
     * row (middle, secondary) takes no default action and bubbles. */
    pb = lk_editor_pointer_lookup((lk_u8)LK_EDG_CLICK, btn, ev->mods);

    if (!pb) {
      return 0;
    }

    /* Ctrl+primary toggles a caret at the hit position (stage E3).
     * An ADD starts a drag so the new PRIMARY caret extends while
     * the rest stay put; a remove (or a refused add) just consumes
     * the click. */
    if (pb->action == (lk_u8)LK_EDP_ADD_CURSOR) {
      lk_u32 before = lk_editor_caret_count(e);
      lk_editor_cmd_arg arg;

      memset(&arg, 0, sizeof(arg));
      arg.set_cursor.pos = pos;
      ed_cmd(e, ui, LK_ED_ADD_CURSOR_AT, &arg);
      lk_focus_set(ui, t, nid);

      if (lk_editor_caret_count(e) > before) {
        lk_capture_set(ui, nid);
        lk_editor_set_drag(e, 1);
      }

      return 1;
    }

    ed_set_cursor(e, ui, pos, 0);

    /* We consume the click, so the host's built-in click-to-focus
     * never sees it -- take focus and the pointer capture here. */
    lk_focus_set(ui, t, nid);
    lk_capture_set(ui, nid);
    lk_editor_set_drag(e, 1);

    return 1;
  }

  case LK_EVENT_POINTER_MOVE: {
    lk_u32 pos;

    if (lk_editor_scrollbar_dragging(e)) {
      lk_editor_scrollbar_move(e, ui, ev->data.pointer.y);

      return 1;
    }

    if (lk_editor_box_active(e)) {
      lk_editor_box_move(e, ui, ev->data.pointer.x, ev->data.pointer.y);

      return 1;
    }

    if (!lk_editor_dragging(e)) {
      return 0;
    }

    if (lk_editor_hit_pos(e, ui ? ui->text : NULL, ev->data.pointer.x,
                          ev->data.pointer.y, &pos)) {
      ed_set_cursor(e, ui, pos, 1);
    }

    return 1;
  }

  case LK_EVENT_POINTER_UP:
    if (lk_editor_scrollbar_dragging(e)) {
      lk_capture_clear(ui);
      lk_editor_scrollbar_end(e);

      return 1;
    }

    if (lk_editor_box_active(e)) {
      lk_capture_clear(ui);
      lk_editor_box_end(e);

      return 1;
    }

    if (!lk_editor_dragging(e)) {
      return 0;
    }

    lk_capture_clear(ui);
    lk_editor_set_drag(e, 0);

    return 1;

  case LK_EVENT_WHEEL: {
    lk_editor_cmd_arg arg;
    lk_i32 vy = ev->data.wheel.dy;
    lk_i32 hticks = 0;
    const lk_editor_pointer_binding *pb =
        lk_editor_pointer_lookup((lk_u8)LK_EDG_WHEEL, 0, ev->mods);

    /* In NONE mode, native wheel-dx and SHIFT+wheel-dy (the SCROLL_X
     * row) scroll horizontally; vertical behavior is unchanged
     * otherwise. */
    if (lk_editor_wrap_mode_get(e) == LK_EDITOR_WRAP_NONE) {
      hticks = ev->data.wheel.dx;

      if (pb && pb->action == (lk_u8)LK_EDP_SCROLL_X) {
        hticks -= vy;
        vy = 0;
      }
    }

    if (hticks != 0) {
      lk_editor_scroll_x_wheel(e, hticks);
    }

    if (vy != 0) {
      memset(&arg, 0, sizeof(arg));
      arg.lines = -vy * ED_WHEEL_LINES;
      ed_cmd(e, ui, LK_ED_SCROLL_LINES, &arg);
    }

    /* Always consumed: the editor owns its viewport and never leaks
     * wheel events to an ancestor UIK_SCROLL. */
    return 1;
  }

  default: return 0;
  }
}

/* ---- Registration ---- */

/* Context-menu discovery: byte under the point, then the source's
 * candidates (docs/context-menu.md section 1). */
static lk_u32 presentations_at_editor(lk_ui *ui, const lk_tree *t, lk_ix n,
                                      lk_i32 x, lk_i32 y,
                                      lk_presentation_hit *out, lk_u32 cap) {
  lk_editor *e = lk_editor_from_node(t->resources, t, n);
  lk_u32 pos;

  (void)ui;

  if (!e || !lk_editor_pos_at(e, x, y, &pos)) {
    return 0;
  }

  return lk_editor_presentations_at(e, t, pos, out, cap);
}

const lk_widget_def *lk_editor_widget(void) {
  static lk_widget_def def;
  static int inited = 0;

  if (!inited) {
    memset(&def, 0, sizeof(def));
    def.measure = measure_editor;
    def.layout = layout_editor;
    def.render = render_editor;
    def.event = event_editor;
    def.clips = 1;
    inited = 1;
  }

  def.presentations_at = presentations_at_editor;
  return &def;
}
