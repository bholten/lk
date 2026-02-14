#include <string.h>

#include "lk-memory.h"
#include <lk.h>

static int node_is_focusable(const lk_tree *t, lk_ix n) {
  return lk_node_prop_bool(t, n, UIP_FOCUSABLE);
}

static int node_is_disabled(const lk_tree *t, lk_ix n) {
  return lk_node_prop_bool(t, n, UIP_DISABLED);
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
    int child_count;
    lk_ix *kids;
    int ki;

    if (!rect_contains(&rects[n], x, y)) {
      continue;
    }

    best = n;

    /* Push children in reverse order for left-to-right DFS */
    child_count = 0;
    child = nd->first_child;
    while (child) {
      child_count++;
      child = t->nodes[child].next_sibling;
    }

    if (child_count > 0) {
      kids = (lk_ix *)lk_sys_alloc(
          NULL, (lk_u32)(sizeof(lk_ix) * (lk_u32)child_count));
      if (kids) {
        ki = 0;
        child = nd->first_child;
        while (child) {
          kids[ki++] = child;
          child = t->nodes[child].next_sibling;
        }
        while (ki > 0) {
          stack[sp++] = kids[--ki];
        }
        lk_sys_dealloc(NULL, kids);
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

  ui->focused_id = id;
  return 1;
}

void lk_focus_clear(lk_ui *ui) {
  if (ui) {
    ui->focused_id = 0;
  }
}

lk_ix lk_focus_current(const lk_ui *ui, const lk_tree *t) {
  if (!ui || !t || ui->focused_id == 0) {
    return 0;
  }
  return lk_tree_find_by_id(t, ui->focused_id);
}

/* Collect all focusable+enabled nodes in DFS pre-order into buf.
 * Returns count of focusable nodes found.
 */
static lk_u32 collect_focusable(const lk_tree *t, lk_ix *buf, lk_u32 buf_cap) {
  lk_ix *stack;
  lk_u32 sp, count;

  if (!t || t->root == 0 || !buf || buf_cap == 0) {
    return 0;
  }

  stack = (lk_ix *)lk_sys_alloc(NULL, (lk_u32)(sizeof(lk_ix) * t->node_count));
  if (!stack) {
    return 0;
  }

  sp = 0;
  count = 0;
  stack[sp++] = t->root;

  while (sp > 0 && count < buf_cap) {
    lk_ix n = stack[--sp];
    const lk_node *nd = &t->nodes[n];
    lk_ix child;
    int child_count, ki;
    lk_ix *kids;

    if (node_is_focusable(t, n) && !node_is_disabled(t, n)) {
      buf[count++] = n;
    }

    /* Push children in reverse order */
    child_count = 0;
    child = nd->first_child;
    while (child) {
      child_count++;
      child = t->nodes[child].next_sibling;
    }

    if (child_count > 0) {
      kids = (lk_ix *)lk_sys_alloc(
          NULL, (lk_u32)(sizeof(lk_ix) * (lk_u32)child_count));
      if (kids) {
        ki = 0;
        child = nd->first_child;
        while (child) {
          kids[ki++] = child;
          child = t->nodes[child].next_sibling;
        }
        while (ki > 0) {
          stack[sp++] = kids[--ki];
        }
        lk_sys_dealloc(NULL, kids);
      }
    }
  }

  lk_sys_dealloc(NULL, stack);
  return count;
}

lk_node_id lk_focus_next(lk_ui *ui, const lk_tree *t) {
  lk_ix *buf;
  lk_u32 count, i;
  lk_node_id result;

  if (!ui || !t || t->root == 0) {
    return 0;
  }

  buf = (lk_ix *)lk_sys_alloc(NULL, (lk_u32)(sizeof(lk_ix) * t->node_count));
  if (!buf) {
    return 0;
  }

  count = collect_focusable(t, buf, t->node_count);
  if (count == 0) {
    lk_sys_dealloc(NULL, buf);
    return 0;
  }

  if (ui->focused_id == 0) {
    /* No current focus: pick the first */
    result = t->nodes[buf[0]].id;
    ui->focused_id = result;
    lk_sys_dealloc(NULL, buf);
    return result;
  }

  /* Find current position */
  for (i = 0; i < count; i++) {
    if (t->nodes[buf[i]].id == ui->focused_id) {
      lk_u32 next_i = (i + 1) % count;
      result = t->nodes[buf[next_i]].id;
      ui->focused_id = result;
      lk_sys_dealloc(NULL, buf);
      return result;
    }
  }

  /* Current focused not found; pick first */
  result = t->nodes[buf[0]].id;
  ui->focused_id = result;
  lk_sys_dealloc(NULL, buf);
  return result;
}

lk_node_id lk_focus_prev(lk_ui *ui, const lk_tree *t) {
  lk_ix *buf;
  lk_u32 count, i;
  lk_node_id result;

  if (!ui || !t || t->root == 0) {
    return 0;
  }

  buf = (lk_ix *)lk_sys_alloc(NULL, (lk_u32)(sizeof(lk_ix) * t->node_count));
  if (!buf) {
    return 0;
  }

  count = collect_focusable(t, buf, t->node_count);
  if (count == 0) {
    lk_sys_dealloc(NULL, buf);
    return 0;
  }

  if (ui->focused_id == 0) {
    /* No current focus: pick the last */
    result = t->nodes[buf[count - 1]].id;
    ui->focused_id = result;
    lk_sys_dealloc(NULL, buf);
    return result;
  }

  /* Find current position */
  for (i = 0; i < count; i++) {
    if (t->nodes[buf[i]].id == ui->focused_id) {
      lk_u32 prev_i = (i == 0) ? count - 1 : i - 1;
      result = t->nodes[buf[prev_i]].id;
      ui->focused_id = result;
      lk_sys_dealloc(NULL, buf);
      return result;
    }
  }

  /* Current focused not found; pick last */
  result = t->nodes[buf[count - 1]].id;
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
