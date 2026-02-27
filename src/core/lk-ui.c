#include <string.h>

#include "lk-memory.h"
#include "lk-tabs.h"
#include <lk.h>

#include "lk-menu.h"

/* lk-state.c */
lk_state *lk_state_create(void *(*)(void *, lk_u32), void (*)(void *, void *),
                          void *);
void lk_state_destroy(lk_state *st);
void lk_state_gc(lk_state *st, const lk_changeset *cs);

static void *ui_alloc(lk_ui *ui, lk_u32 bytes) {
  return ui->alloc(ui->alloc_ud, bytes);
}

static void ui_dealloc(lk_ui *ui, void *ptr) {
  if (ptr) {
    ui->dealloc(ui->alloc_ud, ptr);
  }
}

static int cs_push(lk_ui *ui, lk_u8 kind, lk_node_id id, lk_ix ix) {
  lk_changeset *cs = &ui->changeset;

  if (cs->count >= cs->cap) {
    lk_u32 new_cap = cs->cap ? cs->cap * 2 : 32;
    lk_change *nc =
        (lk_change *)ui_alloc(ui, (lk_u32)(sizeof(lk_change) * new_cap));

    if (!nc) {
      return 0;
    }

    if (cs->changes && cs->count) {
      memcpy(nc, cs->changes, sizeof(lk_change) * cs->count);
    }

    if (cs->changes) {
      ui_dealloc(ui, cs->changes);
    }

    cs->changes = nc;
    cs->cap = new_cap;
  }

  cs->changes[cs->count].kind = kind;
  cs->changes[cs->count].id = id;
  cs->changes[cs->count].node_ix = ix;
  cs->count++;

  return 1;
}

static void emit_added_subtree(lk_ui *ui, lk_ix ix) {
  lk_node *n = &ui->next->nodes[ix];
  lk_ix ch;

  cs_push(ui, LK_CHANGE_ADDED, n->id, ix);

  ch = n->first_child;

  while (ch) {
    emit_added_subtree(ui, ch);
    ch = ui->next->nodes[ch].next_sibling;
  }
}

static void emit_removed_subtree(lk_ui *ui, lk_ix ix) {
  lk_node *n = &ui->prev->nodes[ix];
  lk_ix ch;

  cs_push(ui, LK_CHANGE_REMOVED, n->id, 0);

  ch = n->first_child;

  while (ch) {
    emit_removed_subtree(ui, ch);
    ch = ui->prev->nodes[ch].next_sibling;
  }
}

static int props_equal(const lk_tree *a, const lk_node *na, const lk_tree *b,
                       const lk_node *nb) {
  lk_u32 k;

  if (na->props_len != nb->props_len) {
    return 0;
  }

  for (k = 0; k < na->props_len; k++) {
    const lk_prop *pa = &a->props[na->props_off + k];
    const lk_prop *pb = &b->props[nb->props_off + k];

    if (pa->key != pb->key) {
      return 0;
    }

    if (pa->value.tag != pb->value.tag) {
      return 0;
    }

    switch (pa->value.tag) {
    case UIV_NONE: break;
    case UIV_BOOL:
      if (pa->value.as.b != pb->value.as.b) {
        return 0;
      }
      break;
    case UIV_I32:
      if (pa->value.as.i != pb->value.as.i) {
        return 0;
      }
      break;
    case UIV_STR:
      if (pa->value.as.str_id != pb->value.as.str_id) {
        return 0;
      }
      break;
    case UIV_RESOURCE:
      if (pa->value.as.res.id != pb->value.as.res.id ||
          pa->value.as.res.gen != pb->value.as.res.gen) {
        return 0;
      }
      break;
    case UIV_TEXT: /* command/hit scope only — never valid in trees */
      if (pa->value.as.text.off != pb->value.as.text.off ||
          pa->value.as.text.len != pb->value.as.text.len) {
        return 0;
      }
      break;
    }
  }

  return 1;
}

static int value_equal(const lk_value *a, const lk_value *b) {
  if (a->tag != b->tag) {
    return 0;
  }

  switch (a->tag) {
  case UIV_NONE: return 1;
  case UIV_BOOL: return a->as.b == b->as.b;
  case UIV_I32: return a->as.i == b->as.i;
  case UIV_STR: return a->as.str_id == b->as.str_id;
  case UIV_RESOURCE:
    return a->as.res.id == b->as.res.id && a->as.res.gen == b->as.res.gen;
  case UIV_TEXT:
    return a->as.text.off == b->as.text.off && a->as.text.len == b->as.text.len;
  }

  return 0;
}

static int pres_equal(const lk_tree *prev, lk_ix prev_ix, const lk_tree *next,
                      lk_ix next_ix) {
  lk_u32 pi, ni;
  lk_u32 prev_count = 0;
  lk_u32 next_count = 0;

  for (pi = 0; pi < prev->pres_count; pi++) {
    if (prev->pres[pi].node == prev_ix) {
      prev_count++;
    }
  }

  for (ni = 0; ni < next->pres_count; ni++) {
    if (next->pres[ni].node == next_ix) {
      next_count++;
    }
  }

  if (prev_count != next_count) {
    return 0;
  }

  if (prev_count == 0) {
    return 1;
  }

  /* Compare presentations in order of appearance */
  pi = 0;
  ni = 0;

  while (pi < prev->pres_count && ni < next->pres_count) {
    /* Find next pres for prev_ix */
    while (pi < prev->pres_count && prev->pres[pi].node != prev_ix) {
      pi++;
    }

    /* Find next pres for next_ix */
    while (ni < next->pres_count && next->pres[ni].node != next_ix) {
      ni++;
    }

    if (pi >= prev->pres_count || ni >= next->pres_count) {
      break;
    }

    if (prev->pres[pi].ptype != next->pres[ni].ptype) {
      return 0;
    }

    if (prev->pres[pi].pvalue_count != next->pres[ni].pvalue_count) {
      return 0;
    }

    {
      lk_u8 ai;
      for (ai = 0; ai < prev->pres[pi].pvalue_count; ai++) {
        if (!value_equal(&prev->pres[pi].pvalues[ai],
                         &next->pres[ni].pvalues[ai])) {
          return 0;
        }
      }
    }

    pi++;
    ni++;
  }

  return 1;
}

static int tags_equal(const lk_tree *prev, lk_ix prev_ix, const lk_tree *next,
                      lk_ix next_ix) {
  lk_u32 pi, ni;
  lk_u32 prev_count = 0;
  lk_u32 next_count = 0;

  for (pi = 0; pi < prev->tag_count; pi++) {
    if (prev->tags[pi].node == prev_ix) {
      prev_count++;
    }
  }

  for (ni = 0; ni < next->tag_count; ni++) {
    if (next->tags[ni].node == next_ix) {
      next_count++;
    }
  }

  if (prev_count != next_count) {
    return 0;
  }

  if (prev_count == 0) {
    return 1;
  }

  /* Compare tags in order of appearance */
  pi = 0;
  ni = 0;

  while (pi < prev->tag_count && ni < next->tag_count) {
    while (pi < prev->tag_count && prev->tags[pi].node != prev_ix) {
      pi++;
    }

    while (ni < next->tag_count && next->tags[ni].node != next_ix) {
      ni++;
    }

    if (pi >= prev->tag_count || ni >= next->tag_count) {
      break;
    }

    if (prev->tags[pi].tag_id != next->tags[ni].tag_id) {
      return 0;
    }

    pi++;
    ni++;
  }

  return 1;
}

static void diff_node(lk_ui *ui, lk_ix prev_ix, lk_ix next_ix) {
  lk_node *pn = &ui->prev->nodes[prev_ix];
  lk_node *nn = &ui->next->nodes[next_ix];
  lk_ix pch, nch;
  lk_u32 pc, nc, i, j;
  lk_ix *prev_arr = NULL;
  lk_ix *next_arr = NULL;
  lk_u8 *matched = NULL;

  if (pn->kind != nn->kind || !props_equal(ui->prev, pn, ui->next, nn) ||
      !pres_equal(ui->prev, prev_ix, ui->next, next_ix) ||
      !tags_equal(ui->prev, prev_ix, ui->next, next_ix)) {
    cs_push(ui, LK_CHANGE_UPDATED, nn->id, next_ix);
  }

  pc = 0;
  pch = pn->first_child;

  while (pch) {
    pc++;
    pch = ui->prev->nodes[pch].next_sibling;
  }

  nc = 0;
  nch = nn->first_child;

  while (nch) {
    nc++;
    nch = ui->next->nodes[nch].next_sibling;
  }

  if (pc == 0 && nc == 0) {
    return;
  }

  if (pc == 0) {
    nch = nn->first_child;

    while (nch) {
      emit_added_subtree(ui, nch);
      nch = ui->next->nodes[nch].next_sibling;
    }

    return;
  }

  if (nc == 0) {
    pch = pn->first_child;

    while (pch) {
      emit_removed_subtree(ui, pch);
      pch = ui->prev->nodes[pch].next_sibling;
    }

    return;
  }

  prev_arr = (lk_ix *)ui_alloc(ui, (lk_u32)(sizeof(lk_ix) * pc));
  matched = (lk_u8 *)ui_alloc(ui, (lk_u32)(sizeof(lk_u8) * pc));
  next_arr = (lk_ix *)ui_alloc(ui, (lk_u32)(sizeof(lk_ix) * nc));

  if (!prev_arr || !matched || !next_arr) {
    goto cleanup;
  }

  i = 0;
  pch = pn->first_child;

  while (pch) {
    prev_arr[i] = pch;
    matched[i] = 0;
    i++;
    pch = ui->prev->nodes[pch].next_sibling;
  }

  i = 0;
  nch = nn->first_child;

  while (nch) {
    next_arr[i] = nch;
    i++;
    nch = ui->next->nodes[nch].next_sibling;
  }

  for (i = 0; i < nc; i++) {
    lk_node_id nid = ui->next->nodes[next_arr[i]].id;
    int found = 0;

    for (j = 0; j < pc; j++) {
      if (!matched[j] && ui->prev->nodes[prev_arr[j]].id == nid) {
        matched[j] = 1;
        found = 1;
        diff_node(ui, prev_arr[j], next_arr[i]);
        break;
      }
    }

    if (!found) {
      emit_added_subtree(ui, next_arr[i]);
    }
  }

  for (j = 0; j < pc; j++) {
    if (!matched[j]) {
      emit_removed_subtree(ui, prev_arr[j]);
    }
  }

cleanup:
  ui_dealloc(ui, prev_arr);
  ui_dealloc(ui, matched);
  ui_dealloc(ui, next_arr);
}

const char *lk_version(void) {
  return LK_VERSION_STRING;
}

lk_ui *lk_ui_create(const lk_ui_cfg *cfg) {
  lk_ui *ui;
  lk_tree_cfg tcfg;
  void *(*al)(void *, lk_u32);
  void (*de)(void *, void *);
  void *ud;

  al = (cfg && cfg->alloc) ? cfg->alloc : lk_sys_alloc;
  de = (cfg && cfg->dealloc) ? cfg->dealloc : lk_sys_dealloc;
  ud = cfg ? cfg->ud : NULL;

  ui = (lk_ui *)al(ud, (lk_u32)sizeof(lk_ui));

  if (!ui) {
    return NULL;
  }

  memset(ui, 0, sizeof(*ui));
  ui->alloc = al;
  ui->dealloc = de;
  ui->alloc_ud = ud;

  ui->intern = lk_intern_new(al, de, ud);

  if (!ui->intern) {
    de(ud, ui);
    return NULL;
  }

  memset(&tcfg, 0, sizeof(tcfg));

  tcfg.intern = ui->intern;
  tcfg.alloc = al;
  tcfg.dealloc = de;
  tcfg.ud = ud;
  tcfg.node_cap_hint = cfg ? cfg->node_cap_hint : 0;
  tcfg.prop_cap_hint = cfg ? cfg->prop_cap_hint : 0;

  ui->prev = lk_tree_create(&tcfg);
  ui->next = lk_tree_create(&tcfg);

  if (!ui->prev || !ui->next) {
    lk_ui_destroy(ui);
    return NULL;
  }

  ui->state = lk_state_create(al, de, ud);
  if (!ui->state) {
    lk_ui_destroy(ui);
    return NULL;
  }

  /* Resource table — eager, matching the rest of lk_ui's init.  Both
   * trees borrow it so widget vtable hooks can resolve refs through
   * the tree alone (docs/editor.md §5). */
  ui->resources = lk_resources_new(al, de, ud);

  if (!ui->resources) {
    lk_ui_destroy(ui);
    return NULL;
  }

  ui->prev->resources = ui->resources;
  ui->next->resources = ui->resources;

  ui->theme = lk_theme_default(al, de, ud);

  return ui;
}

void lk_ui_destroy(lk_ui *ui) {
  if (!ui) {
    return;
  }

  if (ui->prev) {
    lk_tree_destroy(ui->prev);
  }

  if (ui->next) {
    lk_tree_destroy(ui->next);
  }

  if (ui->intern) {
    lk_intern_destroy(ui->intern);
  }

  if (ui->state) {
    lk_state_destroy(ui->state);
  }

  if (ui->resources) {
    lk_resources_destroy(ui->resources);
  }

  if (ui->changeset.changes) {
    ui_dealloc(ui, ui->changeset.changes);
  }

  if (ui->translators) {
    ui_dealloc(ui, ui->translators);
  }

  if (ui->cmd_queue.cmds) {
    ui_dealloc(ui, ui->cmd_queue.cmds);
  }

  if (ui->cmd_queue.bytes) {
    ui_dealloc(ui, ui->cmd_queue.bytes);
  }

  if (ui->cmd_log) {
    ui_dealloc(ui, ui->cmd_log);
  }

  if (ui->cmd_log_bytes) {
    ui_dealloc(ui, ui->cmd_log_bytes);
  }

  if (ui->theme) {
    lk_theme_destroy(ui->theme);
  }

  if (ui->styles) {
    ui_dealloc(ui, ui->styles);
  }

  if (ui->node_states) {
    ui_dealloc(ui, ui->node_states);
  }

  if (ui->geom) {
    ui_dealloc(ui, ui->geom);
  }

  if (ui->rects) {
    ui_dealloc(ui, ui->rects);
  }

  lk_menu_ui_destroy(ui);

  if (ui->overlays) {
    ui_dealloc(ui, ui->overlays);
  }

  if (ui->pending) {
    ui_dealloc(ui, ui->pending);
  }

  ui->dealloc(ui->alloc_ud, ui);
}

lk_tree *lk_ui_begin_frame(lk_ui *ui) {
  lk_tree_reset(ui->next);
  return ui->next;
}

/* 1 when id was REMOVED in cs and not re-ADDED (i.e. truly gone, not
 * merely moved to a new parent).  Shared by the focus-clear and
 * overlay GC in lk_ui_end_frame; lk_state_gc applies the same rule. */
static int cs_id_removed_not_readded(const lk_changeset *cs, lk_node_id id) {
  lk_u32 i;
  int removed = 0;

  for (i = 0; i < cs->count; i++) {
    if (cs->changes[i].kind == LK_CHANGE_REMOVED && cs->changes[i].id == id) {
      removed = 1;
      break;
    }
  }

  if (!removed) {
    return 0;
  }

  for (i = 0; i < cs->count; i++) {
    if (cs->changes[i].kind == LK_CHANGE_ADDED && cs->changes[i].id == id) {
      return 0;
    }
  }

  return 1;
}

const lk_changeset *lk_ui_end_frame(lk_ui *ui) {
  lk_tree *tmp;

  ui->changeset.count = 0;

  if (ui->prev->root == 0 && ui->next->root == 0) {
    /* Both empty: nothing to do */
  } else if (ui->prev->root == 0) {
    emit_added_subtree(ui, ui->next->root);
  } else if (ui->next->root == 0) {
    emit_removed_subtree(ui, ui->prev->root);
  } else if (ui->prev->nodes[ui->prev->root].id !=
             ui->next->nodes[ui->next->root].id) {
    emit_removed_subtree(ui, ui->prev->root);
    emit_added_subtree(ui, ui->next->root);
  } else {
    diff_node(ui, ui->prev->root, ui->next->root);
  }

  tmp = ui->prev;
  ui->prev = ui->next;
  ui->next = tmp;

  /* Clear focus if the focused node was removed.  A node that is
   * REMOVED and ADDED in the same changeset merely moved to a new
   * parent — keep focus in that case.  The clear is an effective
   * focus change, so it enqueues FOCUS_CHANGED like the lk_focus_*
   * functions; the host drains it via lk_ui_flush_events (end_frame
   * itself never routes). */
  if (ui->focused_id != 0 &&
      cs_id_removed_not_readded(&ui->changeset, ui->focused_id)) {
    lk_event fev;

    memset(&fev, 0, sizeof(fev));
    fev.type = LK_EVENT_FOCUS_CHANGED;
    fev.target = ui->prev->root; /* clear: no focused node to target */
    fev.data.focus.prev_id = ui->focused_id;
    fev.data.focus.next_id = 0;
    lk_event_enqueue(ui, &fev);

    ui->focused_id = 0;
  }

  /* A focused node inside a collapsed TAB page (the tabs selection
   * moved -- by a click, or by an app-controlled `value` change --
   * while focus lived in the old page) is unreachable: not laid out,
   * not rendered, skipped by focus traversal.  Clear focus rather than
   * route keystrokes into an invisible widget.  UIP_HIDDEN subtrees
   * are deliberately NOT swept here: modal overlay content is a
   * hidden subtree that legitimately holds focus (focus trap). */
  if (ui->focused_id != 0) {
    lk_ix fx = lk_tree_find_by_id(ui->prev, ui->focused_id);
    int collapsed = 0;

    /* ancestors only: a collapsed TAB itself is its (visible) header */
    if (fx != 0) {
      fx = ui->prev->nodes[fx].parent;
    }

    while (fx != 0) {
      if (lk_tabs_collapsed(ui->prev, fx, ui->state)) {
        collapsed = 1;
        break;
      }

      fx = ui->prev->nodes[fx].parent;
    }

    if (collapsed) {
      lk_event fev;

      memset(&fev, 0, sizeof(fev));
      fev.type = LK_EVENT_FOCUS_CHANGED;
      fev.target = ui->prev->root;
      fev.data.focus.prev_id = ui->focused_id;
      fev.data.focus.next_id = 0;
      lk_event_enqueue(ui, &fev);

      ui->focused_id = 0;
    }
  }

  /* Same rule for pointer capture: a captured node that is truly gone
   * releases the capture; a move (REMOVED+ADDED) keeps it. */
  if (ui->captured_id != 0 &&
      cs_id_removed_not_readded(&ui->changeset, ui->captured_id)) {
    ui->captured_id = 0;
  }

  /* Deferred focus (lk_focus_request): the committed tree is the
   * first place the requested node can exist.  lk_focus_set applies
   * its own focusable/enabled rules and clears the request on
   * success; an unsatisfied request simply stays pending. */
  if (ui->focus_request_id != 0) {
    lk_focus_set(ui, ui->prev, ui->focus_request_id);
  }

  /* Pop overlays whose owner node was removed (same move filter:
   * REMOVED + ADDED in one changeset is a reparent, overlay stays). */
  {
    lk_u32 oi = ui->overlay_count;

    while (oi > 0) {
      oi--;

      if (cs_id_removed_not_readded(&ui->changeset,
                                    ui->overlays[oi].owner_id)) {
        lk_u32 k;

        for (k = oi; k + 1 < ui->overlay_count; k++) {
          ui->overlays[k] = ui->overlays[k + 1];
        }

        ui->overlay_count--;
      }
    }
  }

  /* GC retained state for removed nodes */
  if (ui->state) {
    lk_state_gc(ui->state, &ui->changeset);
  }

  return &ui->changeset;
}

const lk_tree *lk_ui_tree(const lk_ui *ui) {
  return ui->prev;
}

lk_state *lk_ui_state(lk_ui *ui) {
  return ui ? ui->state : NULL;
}

lk_resources *lk_ui_resources(lk_ui *ui) {
  return ui ? ui->resources : NULL;
}

void lk_ui_set_theme(lk_ui *ui, lk_theme *th) {
  if (!ui) {
    return;
  }

  if (ui->theme) {
    lk_theme_destroy(ui->theme);
  }

  ui->theme = th;
}

lk_theme *lk_ui_theme(lk_ui *ui) {
  return ui ? ui->theme : NULL;
}

const lk_style *lk_ui_styles(const lk_ui *ui) {
  return ui ? ui->styles : NULL;
}

lk_widget_geom *lk_ui_geom(lk_ui *ui) {
  const lk_tree *t;
  lk_u32 nc;

  if (!ui) {
    return NULL;
  }

  t = ui->prev; /* current tree (mirrors lk_ui_resolve_styles) */
  nc = t ? t->node_count : 0;

  if (nc > ui->geom_cap) {
    if (ui->geom) {
      ui_dealloc(ui, ui->geom);
    }

    ui->geom =
        (lk_widget_geom *)ui_alloc(ui, (lk_u32)(sizeof(lk_widget_geom) * nc));

    if (!ui->geom) {
      ui->geom_cap = 0;
      return NULL;
    }

    ui->geom_cap = nc;
  }

  return ui->geom;
}

lk_rect *lk_ui_rects(lk_ui *ui) {
  const lk_tree *t;
  lk_u32 nc;

  if (!ui) {
    return NULL;
  }

  t = ui->prev; /* current tree (mirrors lk_ui_geom) */
  nc = t ? t->node_count : 0;

  if (nc > ui->rects_cap) {
    if (ui->rects) {
      ui_dealloc(ui, ui->rects);
    }

    ui->rects = (lk_rect *)ui_alloc(ui, (lk_u32)(sizeof(lk_rect) * nc));

    if (!ui->rects) {
      ui->rects_cap = 0;
      return NULL;
    }

    ui->rects_cap = nc;
  }

  return ui->rects;
}

int lk_node_rect(const lk_ui *ui, lk_node_id id, lk_rect *out) {
  const lk_tree *t;
  lk_ix n;

  if (!ui || !out || id == 0 || !ui->rects) {
    return 0;
  }

  t = ui->prev;
  n = t ? lk_tree_find_by_id(t, id) : 0;

  if (n == 0 || n >= ui->rects_cap) {
    return 0;
  }

  *out = ui->rects[n];

  return 1;
}

void lk_ui_set_clipboard(lk_ui *ui, lk_clipboard_get_fn get_fn,
                         lk_clipboard_set_fn set_fn, void *ud) {
  if (!ui) {
    return;
  }

  ui->clipboard_get = get_fn;
  ui->clipboard_set = set_fn;
  ui->clipboard_ud = ud;
}

void lk_ui_set_text_backend(lk_ui *ui, const lk_text_backend *text) {
  if (!ui) {
    return;
  }

  ui->text = text;
}

void lk_ui_set_time_ms(lk_ui *ui, lk_u32 ms) {
  if (!ui) {
    return;
  }

  ui->time_ms = ms;
}

lk_u32 lk_ui_time_ms(const lk_ui *ui) {
  if (!ui) {
    return 0;
  }

  return ui->time_ms;
}

void lk_ui_resolve_styles(lk_ui *ui) {
  const lk_tree *t;
  lk_u32 nc;

  if (!ui || !ui->theme) {
    return;
  }

  t = ui->prev;
  if (!t || t->root == 0 || t->root >= t->node_count) {
    return;
  }

  nc = t->node_count;

  /* Ensure styles array is large enough */
  if (nc > ui->styles_cap) {
    if (ui->styles) {
      ui_dealloc(ui, ui->styles);
    }

    ui->styles = (lk_style *)ui_alloc(ui, (lk_u32)(sizeof(lk_style) * nc));

    if (!ui->styles) {
      ui->styles_cap = 0;
      return;
    }

    ui->styles_cap = nc;
  }

  /* Ensure node_states array is large enough */
  if (nc > ui->nstates_cap) {
    if (ui->node_states) {
      ui_dealloc(ui, ui->node_states);
    }

    ui->node_states = (lk_u8 *)ui_alloc(ui, (lk_u32)(sizeof(lk_u8) * nc));

    if (!ui->node_states) {
      ui->nstates_cap = 0;
      return;
    }

    ui->nstates_cap = nc;
  }

  /* Compute node states */
  memset(ui->node_states, 0, sizeof(lk_u8) * nc);

  {
    lk_ix fi;

    /* Mark focused node */
    if (ui->focused_id != 0) {
      for (fi = 1; fi < (lk_ix)nc; fi++) {
        if (t->nodes[fi].id == ui->focused_id) {
          ui->node_states[fi] |= LK_NSTATE_FOCUSED;
          break;
        }
      }
    }

    /* Mark hovered node */
    if (ui->hovered_id != 0) {
      for (fi = 1; fi < (lk_ix)nc; fi++) {
        if (t->nodes[fi].id == ui->hovered_id) {
          ui->node_states[fi] |= LK_NSTATE_HOVERED;
          break;
        }
      }
    }

    /* Mark disabled nodes */
    for (fi = 1; fi < (lk_ix)nc; fi++) {
      if (lk_node_prop_bool(t, fi, UIP_DISABLED)) {
        ui->node_states[fi] |= LK_NSTATE_DISABLED;
      }
    }
  }

  lk_style_resolve(ui->theme, t, ui->node_states, ui->styles);
}
