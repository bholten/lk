#include <string.h>

#include "lk-memory.h"
#include "lk-tooltip.h"
#include <lk.h>

static int node_is_focusable(const lk_tree *t, lk_ix n) {
  return lk_node_prop_bool(t, n, UIP_FOCUSABLE);
}

static int node_is_disabled(const lk_tree *t, lk_ix n) {
  return lk_node_prop_bool(t, n, UIP_DISABLED);
}

/* Keep the retained-state LKS_FOCUSED flag in sync with ui->focused_id
 * so widget render functions (which only see lk_state) can tell if
 * their node is focused.  Call BEFORE assigning the new focused_id.
 */
static void focus_flag_sync(lk_ui *ui, lk_node_id new_id) {
  lk_state *st = ui->state;

  if (!st) {
    return;
  }

  if (ui->focused_id != 0 && ui->focused_id != new_id) {
    /* Only clear an existing flag; never create an entry for a node
     * that may already be gone (would leak past state GC). */
    if (lk_state_get(st, ui->focused_id, LKS_FOCUSED).tag != UIV_NONE) {
      lk_state_set(st, ui->focused_id, LKS_FOCUSED, lk_v_i32(0));
    }
  }

  if (new_id != 0) {
    lk_state_set(st, new_id, LKS_FOCUSED, lk_v_i32(1));
  }
}

/* ---- Hit testing ---- */

static int rect_contains(const lk_rect *r, lk_i32 x, lk_i32 y) {
  return x >= r->x && x < r->x + r->w && y >= r->y && y < r->y + r->h;
}

lk_ix lk_hit_test(const lk_tree *t, const lk_rect *rects, lk_i32 x, lk_i32 y) {
  lk_ix *stack;
  lk_u32 sp;
  lk_ix best;

  if (!t || !rects || t->root == 0) {
    return 0;
  }

  stack = (lk_ix *)lk_sys_alloc(NULL, (lk_u32)(sizeof(lk_ix) * t->node_count));

  if (!stack) {
    return 0;
  }

  sp = 0;
  best = 0;
  stack[sp++] = t->root;

  while (sp > 0) {
    lk_ix n = stack[--sp];
    const lk_node *nd = &t->nodes[n];
    lk_ix child;
    lk_u32 sp_start;
    lk_u32 lo, hi;

    /* Hidden subtrees are invisible to the main-pass hit-test
     * (overlay content is hit-tested by lk_hit_test_overlay). */
    if (lk_node_prop_bool(t, n, UIP_HIDDEN)) {
      continue;
    }

    if (!rect_contains(&rects[n], x, y)) {
      continue;
    }

    best = n;

    /* Push children forward, then reverse segment for left-to-right DFS */
    sp_start = sp;
    child = nd->first_child;

    while (child) {
      stack[sp++] = child;
      child = t->nodes[child].next_sibling;
    }

    if (sp > sp_start) {
      lo = sp_start;
      hi = sp - 1;

      while (lo < hi) {
        lk_ix tmp = stack[lo];
        stack[lo] = stack[hi];
        stack[hi] = tmp;
        lo++;
        hi--;
      }
    }
  }

  lk_sys_dealloc(NULL, stack);
  return best;
}

/* ---- Focus management ---- */

void lk_ui_set_event_handler(lk_ui *ui, lk_event_handler_fn fn, void *ud) {
  if (!ui) {
    return;
  }
  ui->event_handler = fn;
  ui->event_ud = ud;
}

int lk_focus_set(lk_ui *ui, const lk_tree *t, lk_node_id id) {
  lk_ix ix;

  if (!ui || !t || id == 0) {
    return 0;
  }

  ix = lk_tree_find_by_id(t, id);
  if (ix == 0) {
    return 0;
  }

  if (!node_is_focusable(t, ix)) {
    return 0;
  }

  if (node_is_disabled(t, ix)) {
    return 0;
  }

  focus_flag_sync(ui, id);
  ui->focused_id = id;
  return 1;
}

void lk_focus_clear(lk_ui *ui) {
  if (ui) {
    focus_flag_sync(ui, 0);
    ui->focused_id = 0;
  }
}

void lk_hover_set(lk_ui *ui, lk_node_id id) {
  if (ui) {
    if (ui->hovered_id != id) {
      lk_tooltip_hover_changed(ui, id); /* hover-transition hook */
    }

    ui->hovered_id = id;
  }
}

void lk_hover_clear(lk_ui *ui) {
  if (ui) {
    if (ui->hovered_id != 0) {
      lk_tooltip_hover_changed(ui, 0);
    }

    ui->hovered_id = 0;
  }
}

/* ---- Pointer capture (see lk.h for semantics) ---- */

void lk_capture_set(lk_ui *ui, lk_node_id id) {
  if (ui) {
    ui->captured_id = id;
  }
}

void lk_capture_clear(lk_ui *ui) {
  if (ui) {
    ui->captured_id = 0;
  }
}

lk_node_id lk_capture_current(const lk_ui *ui) {
  return ui ? ui->captured_id : 0;
}

lk_ix lk_focus_current(const lk_ui *ui, const lk_tree *t) {
  if (!ui || !t || ui->focused_id == 0) {
    return 0;
  }
  return lk_tree_find_by_id(t, ui->focused_id);
}

/* Collect focusable+enabled nodes in DFS pre-order into buf, scoped
 * to the subtree rooted at start.  Hidden subtrees are skipped;
 * ignore_start_hidden exempts start itself (a focus-trapping
 * overlay's content root is UIP_HIDDEN but must stay collectable
 * while it is the active trap).  Returns count of focusable nodes.
 */
static lk_u32 collect_focusable(const lk_tree *t, lk_ix start,
                                int ignore_start_hidden, lk_ix *buf,
                                lk_u32 buf_cap) {
  lk_ix *stack;
  lk_u32 sp, count;

  if (!t || start == 0 || !buf || buf_cap == 0) {
    return 0;
  }

  stack = (lk_ix *)lk_sys_alloc(NULL, (lk_u32)(sizeof(lk_ix) * t->node_count));

  if (!stack) {
    return 0;
  }

  sp = 0;
  count = 0;
  stack[sp++] = start;

  while (sp > 0 && count < buf_cap) {
    lk_ix n = stack[--sp];
    const lk_node *nd = &t->nodes[n];
    lk_ix child;
    lk_u32 sp_start, lo, hi;

    if (lk_node_prop_bool(t, n, UIP_HIDDEN) &&
        !(ignore_start_hidden && n == start)) {
      continue;
    }

    if (node_is_focusable(t, n) && !node_is_disabled(t, n)) {
      buf[count++] = n;
    }

    /* Push children forward, then reverse segment */
    sp_start = sp;
    child = nd->first_child;

    while (child) {
      stack[sp++] = child;
      child = t->nodes[child].next_sibling;
    }

    if (sp > sp_start) {
      lo = sp_start;
      hi = sp - 1;

      while (lo < hi) {
        lk_ix tmp = stack[lo];
        stack[lo] = stack[hi];
        stack[hi] = tmp;
        lo++;
        hi--;
      }
    }
  }

  lk_sys_dealloc(NULL, stack);
  return count;
}

/* Focus scope: when the topmost focus-trapping overlay has a content
 * subtree, tab-cycling is confined to it (modal behavior).  Scans the
 * overlay stack topmost-first for a trapping overlay whose content
 * root resolves in the current tree; falls back to t->root. */
static lk_ix focus_scope_root(const lk_ui *ui, const lk_tree *t,
                              int *ignore_hidden) {
  lk_u32 i;

  *ignore_hidden = 0;

  if (!ui) {
    return t->root;
  }

  i = ui->overlay_count;

  while (i > 0) {
    const lk_overlay *ov;

    i--;
    ov = &ui->overlays[i];

    if (ov->traps_focus && ov->content_root_id != 0) {
      lk_ix c = lk_tree_find_by_id(t, ov->content_root_id);

      if (c != 0) {
        *ignore_hidden = 1;
        return c;
      }
    }
  }

  return t->root;
}

lk_node_id lk_focus_next(lk_ui *ui, const lk_tree *t) {
  lk_ix *buf;
  lk_u32 count, i;
  lk_node_id result;
  lk_ix scope;
  int ignore_hidden;

  if (!ui || !t || t->root == 0) {
    return 0;
  }

  buf = (lk_ix *)lk_sys_alloc(NULL, (lk_u32)(sizeof(lk_ix) * t->node_count));

  if (!buf) {
    return 0;
  }

  scope = focus_scope_root(ui, t, &ignore_hidden);
  count = collect_focusable(t, scope, ignore_hidden, buf, t->node_count);

  if (count == 0) {
    lk_sys_dealloc(NULL, buf);
    return 0;
  }

  if (ui->focused_id == 0) {
    /* No current focus: pick the first */
    result = t->nodes[buf[0]].id;
    focus_flag_sync(ui, result);
    ui->focused_id = result;
    lk_sys_dealloc(NULL, buf);
    return result;
  }

  /* Find current position */
  for (i = 0; i < count; i++) {
    if (t->nodes[buf[i]].id == ui->focused_id) {
      lk_u32 next_i = (i + 1) % count;
      result = t->nodes[buf[next_i]].id;
      focus_flag_sync(ui, result);
      ui->focused_id = result;
      lk_sys_dealloc(NULL, buf);
      return result;
    }
  }

  /* Current focused not found; pick first */
  result = t->nodes[buf[0]].id;
  focus_flag_sync(ui, result);
  ui->focused_id = result;
  lk_sys_dealloc(NULL, buf);
  return result;
}

lk_node_id lk_focus_prev(lk_ui *ui, const lk_tree *t) {
  lk_ix *buf;
  lk_u32 count, i;
  lk_node_id result;
  lk_ix scope;
  int ignore_hidden;

  if (!ui || !t || t->root == 0) {
    return 0;
  }

  buf = (lk_ix *)lk_sys_alloc(NULL, (lk_u32)(sizeof(lk_ix) * t->node_count));

  if (!buf) {
    return 0;
  }

  scope = focus_scope_root(ui, t, &ignore_hidden);
  count = collect_focusable(t, scope, ignore_hidden, buf, t->node_count);

  if (count == 0) {
    lk_sys_dealloc(NULL, buf);
    return 0;
  }

  if (ui->focused_id == 0) {
    /* No current focus: pick the last */
    result = t->nodes[buf[count - 1]].id;
    focus_flag_sync(ui, result);
    ui->focused_id = result;
    lk_sys_dealloc(NULL, buf);
    return result;
  }

  /* Find current position */
  for (i = 0; i < count; i++) {
    if (t->nodes[buf[i]].id == ui->focused_id) {
      lk_u32 prev_i = (i == 0) ? count - 1 : i - 1;
      result = t->nodes[buf[prev_i]].id;
      focus_flag_sync(ui, result);
      ui->focused_id = result;
      lk_sys_dealloc(NULL, buf);
      return result;
    }
  }

  /* Current focused not found; pick last */
  result = t->nodes[buf[count - 1]].id;
  focus_flag_sync(ui, result);
  ui->focused_id = result;
  lk_sys_dealloc(NULL, buf);

  return result;
}

/* ---- Event routing ---- */

void lk_event_route(lk_ui *ui, lk_event *event) {
  const lk_tree *t;
  lk_ix path[64]; /* max tree depth */
  int depth, i;
  lk_ix n;

  if (!ui || !event) {
    return;
  }

  /* Overlay pre-step: ESC pops the topmost overlay (dropdown popup,
   * modal, ...) before any widget or user handler sees the key.  The
   * owner's public state is kept in sync (dropdowns: LKS_EXPANDED). */
  if (event->type == LK_EVENT_KEY_DOWN &&
      event->data.key.keycode == LKK_ESCAPE && ui->overlay_count > 0) {
    const lk_overlay *top = &ui->overlays[ui->overlay_count - 1];
    lk_node_id owner = top->owner_id;
    lk_u8 kind = top->kind;

    lk_overlay_pop(ui);

    if (kind == LK_OVERLAY_DROPDOWN_POPUP && ui->state) {
      lk_state_set(ui->state, owner, LKS_EXPANDED, lk_v_i32(0));
    }

    event->handled = 1;
    return;
  }

  t = lk_ui_tree(ui);

  if (!t || event->target == 0 || event->target >= t->node_count) {
    return;
  }

  /* Build path from root to target */
  depth = 0;
  n = event->target;

  while (n != 0 && depth < 64) {
    path[depth++] = n;
    n = t->nodes[n].parent;
  }

  /* Reverse: path[0] = root, path[depth-1] = target */
  for (i = 0; i < depth / 2; i++) {
    lk_ix tmp = path[i];
    path[i] = path[depth - 1 - i];
    path[depth - 1 - i] = tmp;
  }

  /* Widget-level event dispatch at target */
  {
    const lk_node *tgt_nd = &t->nodes[event->target];
    const lk_widget_def *tgt_def = lk_widget_get((lk_kind)tgt_nd->kind);

    if (tgt_def && tgt_def->event) {
      event->phase = LK_PHASE_TARGET;

      if (tgt_def->event(ui, t, event->target, event)) {
        event->handled = 1;
        return;
      }
    }
  }

  /* Widget-level event bubbling: walk ancestors from target's parent to root */
  {
    lk_ix anc = t->nodes[event->target].parent;
    event->phase = LK_PHASE_BUBBLE;

    while (anc != 0) {
      const lk_node *anc_nd = &t->nodes[anc];
      const lk_widget_def *anc_def = lk_widget_get((lk_kind)anc_nd->kind);

      if (anc_def && anc_def->event) {
        if (anc_def->event(ui, t, anc, event)) {
          event->handled = 1;
          return;
        }
      }

      anc = anc_nd->parent;
    }
  }

  /* Event handler routing (capture/target/bubble) */
  if (ui->event_handler) {
    /* Capture phase: root to target-parent */
    event->phase = LK_PHASE_CAPTURE;

    for (i = 0; i < depth - 1; i++) {
      ui->event_handler(event, path[i], ui->event_ud);

      if (event->handled) {
        return;
      }
    }

    /* Target phase */
    event->phase = LK_PHASE_TARGET;
    ui->event_handler(event, path[depth - 1], ui->event_ud);

    if (event->handled) {
      return;
    }

    /* Bubble phase: target-parent to root */
    event->phase = LK_PHASE_BUBBLE;

    for (i = depth - 2; i >= 0; i--) {
      ui->event_handler(event, path[i], ui->event_ud);

      if (event->handled) {
        return;
      }
    }
  }

  /* Translator dispatch: if no handler consumed the event */
  if (!event->handled) {
    lk_translate_event(ui, t, event);
  }
}
