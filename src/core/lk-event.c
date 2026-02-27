#include <string.h>

#include "lk-memory.h"
#include "lk-tabs.h"
#include "lk-tooltip.h"
#include <lk.h>

#include "lk-menu.h"

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

/* ---- Pending synthetic-event queue ----
 *
 * Synthetic events (VALUE_CHANGED, FOCUS_CHANGED) are ENQUEUED by
 * their emitters instead of recursively calling lk_event_route
 * mid-dispatch (the design-coherence debt paid by this queue).  The
 * queue drains FIFO at the end of the OUTERMOST lk_event_route call;
 * lk_ui_flush_events drains outside routing.  Events enqueued during
 * a drain append and drain in the same loop, bounded by
 * LK_PENDING_DRAIN_MAX dispatches per drain — overflow is dropped
 * and counted in ui->pending_dropped (a debug counter lk never
 * resets), so a self-feeding handler cannot livelock the router. */

#define LK_PENDING_DRAIN_MAX 64

int lk_event_enqueue(lk_ui *ui, const lk_event *ev) {
  if (!ui || !ev) {
    return 0;
  }

  if (ui->pending_count >= ui->pending_cap) {
    lk_u32 new_cap = ui->pending_cap ? ui->pending_cap * 2 : 8;
    lk_event *np = (lk_event *)ui->alloc(
        ui->alloc_ud, (lk_u32)(sizeof(lk_event) * new_cap));

    if (!np) {
      return 0;
    }

    if (ui->pending && ui->pending_count) {
      memcpy(np, ui->pending, sizeof(lk_event) * ui->pending_count);
    }

    if (ui->pending) {
      ui->dealloc(ui->alloc_ud, ui->pending);
    }

    ui->pending = np;
    ui->pending_cap = new_cap;
  }

  ui->pending[ui->pending_count++] = *ev;
  return 1;
}

/* Forward declaration: one full tier sequence for a single event
 * (defined with the routing code below). */
static void route_one(lk_ui *ui, const lk_tree *t, lk_event *event);

/* Drain the queue FIFO against t.  route_depth is held non-zero for
 * the whole drain so tier handlers that call lk_event_route
 * re-entrantly never trigger a nested drain — their enqueues simply
 * append and are consumed by this loop. */
static void drain_pending(lk_ui *ui, const lk_tree *t) {
  lk_u32 i = 0;

  ui->route_depth++;

  while (i < ui->pending_count) {
    lk_event ev;

    if (i >= LK_PENDING_DRAIN_MAX) {
      ui->pending_dropped += ui->pending_count - i;
      break;
    }

    ev = ui->pending[i]; /* copy: the array may grow during dispatch */
    i++;
    route_one(ui, t, &ev);
  }

  ui->pending_count = 0;
  ui->route_depth--;
}

void lk_ui_flush_events(lk_ui *ui, const lk_tree *t) {
  if (!ui) {
    return;
  }

  if (ui->route_depth != 0) {
    return; /* the outermost lk_event_route call will drain */
  }

  if (ui->pending_count == 0) {
    return;
  }

  drain_pending(ui, t ? t : lk_ui_tree(ui));
}

/* Enqueue a FOCUS_CHANGED event for an effective focus change.
 * target = the newly focused node's index when resolvable (the
 * VALUE_CHANGED convention), else the root so clear-events still
 * route.  Callers only invoke this when prev != next. */
static void focus_event_enqueue(lk_ui *ui, const lk_tree *t,
                                lk_node_id prev_id, lk_node_id next_id,
                                lk_ix target) {
  lk_event ev;

  memset(&ev, 0, sizeof(ev));
  ev.type = LK_EVENT_FOCUS_CHANGED;
  ev.target = target;
  ev.data.focus.prev_id = prev_id;
  ev.data.focus.next_id = next_id;

  if (ev.target == 0 && t && t->root != 0) {
    ev.target = t->root;
  }

  lk_event_enqueue(ui, &ev);
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

  {
    lk_node_id prev = ui->focused_id;

    focus_flag_sync(ui, id);
    ui->focused_id = id;

    if (prev != id) {
      focus_event_enqueue(ui, t, prev, id, ix);
    }
  }

  /* An explicit, successful focus change supersedes a pending
   * request (the app changed its mind before the node appeared). */
  ui->focus_request_id = 0;

  return 1;
}

lk_node_id lk_focus_request(lk_ui *ui, lk_node_id id) {
  lk_node_id prev;

  if (!ui) {
    return 0;
  }

  prev = ui->focus_request_id;
  ui->focus_request_id = id;

  return prev;
}

void lk_focus_clear(lk_ui *ui) {
  if (ui) {
    lk_node_id prev = ui->focused_id;

    focus_flag_sync(ui, 0);
    ui->focused_id = 0;

    if (prev != 0) {
      focus_event_enqueue(ui, lk_ui_tree(ui), prev, 0, 0);
    }
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
static lk_u32 collect_focusable(const lk_tree *t, const lk_state *state,
                                lk_ix start, int ignore_start_hidden,
                                lk_ix *buf, lk_u32 buf_cap) {
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

    /* An unselected TAB page's content is unreachable (lk-tabs.h). */
    if (lk_tabs_collapsed(t, n, state)) {
      continue;
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

/* Shared cycling tail: commit focus to node index chosen, enqueue
 * FOCUS_CHANGED when the effective focus actually changed, return
 * the new focused id. */
static lk_node_id focus_commit(lk_ui *ui, const lk_tree *t, lk_ix chosen) {
  lk_node_id prev = ui->focused_id;
  lk_node_id result = t->nodes[chosen].id;

  focus_flag_sync(ui, result);
  ui->focused_id = result;

  if (prev != result) {
    focus_event_enqueue(ui, t, prev, result, chosen);
  }

  return result;
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
  count = collect_focusable(t, ui->state, scope, ignore_hidden, buf,
                            t->node_count);

  if (count == 0) {
    lk_sys_dealloc(NULL, buf);
    return 0;
  }

  if (ui->focused_id == 0) {
    /* No current focus: pick the first */
    result = focus_commit(ui, t, buf[0]);
    lk_sys_dealloc(NULL, buf);
    return result;
  }

  /* Find current position */
  for (i = 0; i < count; i++) {
    if (t->nodes[buf[i]].id == ui->focused_id) {
      result = focus_commit(ui, t, buf[(i + 1) % count]);
      lk_sys_dealloc(NULL, buf);
      return result;
    }
  }

  /* Current focused not found; pick first */
  result = focus_commit(ui, t, buf[0]);
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
  count = collect_focusable(t, ui->state, scope, ignore_hidden, buf,
                            t->node_count);

  if (count == 0) {
    lk_sys_dealloc(NULL, buf);
    return 0;
  }

  if (ui->focused_id == 0) {
    /* No current focus: pick the last */
    result = focus_commit(ui, t, buf[count - 1]);
    lk_sys_dealloc(NULL, buf);
    return result;
  }

  /* Find current position */
  for (i = 0; i < count; i++) {
    if (t->nodes[buf[i]].id == ui->focused_id) {
      result = focus_commit(ui, t, buf[(i == 0) ? count - 1 : i - 1]);
      lk_sys_dealloc(NULL, buf);
      return result;
    }
  }

  /* Current focused not found; pick last */
  result = focus_commit(ui, t, buf[count - 1]);
  lk_sys_dealloc(NULL, buf);

  return result;
}

/* ---- Event routing ---- */

/* One full tier sequence (overlay pre-step, widget target+bubble,
 * user handler capture/target/bubble, translators) for a single
 * event.  Both the public entry point and the pending-queue drain
 * come through here; only the outermost lk_event_route call drains
 * the queue afterwards. */
static void route_one(lk_ui *ui, const lk_tree *t, lk_event *event) {
  lk_ix path[64]; /* max tree depth */
  int depth, i;
  lk_ix n;

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

  /* Context menu pre-step: keys while a menu is topmost, pointer
   * events inside the popup (docs/context-menu.md section 2). */
  if (lk_menu_route(ui, t, event)) {
    event->handled = 1;
    return;
  }

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

void lk_event_route(lk_ui *ui, lk_event *event) {
  if (!ui || !event) {
    return;
  }

  ui->route_depth++;
  route_one(ui, lk_ui_tree(ui), event);
  ui->route_depth--;

  /* Only the outermost call drains: synthetic events enqueued during
   * this dispatch (VALUE_CHANGED, FOCUS_CHANGED) run their own full
   * tier sequences AFTER the originating event completes. */
  if (ui->route_depth == 0 && ui->pending_count > 0) {
    drain_pending(ui, lk_ui_tree(ui));
  }
}
