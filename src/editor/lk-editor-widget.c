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

static int event_editor_key(lk_editor *e, lk_ui *ui, lk_event *ev) {
  lk_u16 kc = ev->data.key.keycode;
  int shift = (ev->mods & LK_MOD_SHIFT) ? 1 : 0;
  int ctrl = (ev->mods & LK_MOD_CTRL) ? 1 : 0;

  switch (kc) {
  case LKK_LEFT:
    ed_motion(e, ui, ctrl ? LK_ED_MOVE_WORD_LEFT : LK_ED_MOVE_LEFT, shift);

    return 1;

  case LKK_RIGHT:
    ed_motion(e, ui, ctrl ? LK_ED_MOVE_WORD_RIGHT : LK_ED_MOVE_RIGHT, shift);

    return 1;

  case LKK_UP:
    ed_motion(e, ui, LK_ED_MOVE_UP, shift);

    return 1;

  case LKK_DOWN:
    ed_motion(e, ui, LK_ED_MOVE_DOWN, shift);

    return 1;

  case LKK_HOME:
    /* Visual-row HOME (identical to logical when unwrapped); the
     * logical LINE_START command remains reachable for other
     * keymaps. */
    ed_motion(e, ui, ctrl ? LK_ED_MOVE_DOC_START : LK_ED_MOVE_ROW_START,
              shift);

    return 1;

  case LKK_END:
    ed_motion(e, ui, ctrl ? LK_ED_MOVE_DOC_END : LK_ED_MOVE_ROW_END, shift);

    return 1;

  case LKK_PAGEUP:
    ed_motion(e, ui, LK_ED_MOVE_PAGE_UP, shift);

    return 1;

  case LKK_PAGEDOWN:
    ed_motion(e, ui, LK_ED_MOVE_PAGE_DOWN, shift);

    return 1;

  case LKK_BACKSPACE:
    ed_cmd(e, ui, ctrl ? LK_ED_DELETE_WORD_BACKWARD : LK_ED_DELETE_BACKWARD,
           NULL);

    return 1;

  case LKK_DELETE:
    ed_cmd(e, ui, ctrl ? LK_ED_DELETE_WORD_FORWARD : LK_ED_DELETE_FORWARD,
           NULL);

    return 1;

  case LKK_RETURN:
    /* Plain RETURN inserts and is consumed -- unlike the single-line
     * text input it does not bubble.  A ctrl/alt-modified RETURN has
     * no editor meaning: it bubbles so app keybindings (translators)
     * can own chords like ctrl+enter. */
    if (ev->mods & (LK_MOD_CTRL | LK_MOD_ALT)) {
      return 0;
    }

    ed_insert_bytes(e, ui, "\n", 1);

    return 1;

  case LKK_TAB: {
    /* Plain TAB inserts tab_size spaces (v1 pinned policy); focus
     * escape is ESC-then-TAB.  SHIFT+TAB bubbles so host focus
     * cycling keeps a path. */
    char spaces[ED_TAB_SPACES_MAX];
    lk_u32 ts;

    if (shift) {
      return 0;
    }

    ts = lk_editor_tab_size(e);

    if (ts > ED_TAB_SPACES_MAX) {
      ts = ED_TAB_SPACES_MAX;
    }

    memset(spaces, ' ', (size_t)ts);
    ed_insert_bytes(e, ui, spaces, ts);

    return 1;
  }

  case LKK_A:
    if (ctrl) {
      ed_cmd(e, ui, LK_ED_SELECT_ALL, NULL);

      return 1;
    }

    return 0;

  case LKK_C:
    if (ctrl) {
      ed_cmd(e, ui, LK_ED_COPY, NULL);

      return 1;
    }

    return 0;

  case LKK_X:
    if (ctrl) {
      ed_cmd(e, ui, LK_ED_CUT, NULL);

      return 1;
    }

    return 0;

  case LKK_V:
    if (ctrl) {
      ed_cmd(e, ui, LK_ED_PASTE, NULL);

      return 1;
    }

    return 0;

  case LKK_Z:
    if (ctrl) {
      ed_cmd(e, ui, shift ? LK_ED_REDO : LK_ED_UNDO, NULL);

      return 1;
    }

    return 0;

  default:
    /* ESC and anything unlisted bubbles. */
    return 0;
  }
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

  case LK_EVENT_KEY_DOWN:
    return event_editor_key(e, ui, ev);

  case LK_EVENT_POINTER_DOWN: {
    lk_u32 pos;
    lk_u8 btn = ev->data.pointer.button;

    if (!lk_editor_hit_pos(e, ui ? ui->text : NULL, ev->data.pointer.x,
                           ev->data.pointer.y, &pos)) {
      return 0; /* no layout geometry yet: bubble (click-to-focus) */
    }

    /* Interior presentations FIRST (docs/weft-surface.md section 1.5):
     * offer candidates at the hit position; a fired translation
     * consumes the click with no focus change, no cursor move, no
     * selection mutation, and no pointer capture (pinned). */
    if (lk_editor_offer_presentations(e, ui, t, n, ev, pos)) {
      return 1;
    }

    /* No translation: exactly the pre-presentation behavior.  Primary
     * (or an unspecified synthetic button) places the cursor; other
     * buttons take no default action and bubble. */
    if (btn != (lk_u8)LK_POINTER_BUTTON_ANY &&
        btn != (lk_u8)LK_POINTER_BUTTON_PRIMARY) {
      return 0;
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

    /* In NONE mode, native wheel-dx and SHIFT+wheel-dy scroll
     * horizontally; vertical behavior is unchanged otherwise. */
    if (lk_editor_wrap_mode_get(e) == LK_EDITOR_WRAP_NONE) {
      hticks = ev->data.wheel.dx;

      if (ev->mods & LK_MOD_SHIFT) {
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

  default:
    return 0;
  }
}

/* ---- Registration ---- */

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

  return &def;
}
