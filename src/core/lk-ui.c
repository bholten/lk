#include <string.h>

#include "lk-data.h"
#include "lk-memory.h"

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
    }
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

  if (pn->kind != nn->kind || !props_equal(ui->prev, pn, ui->next, nn)) {
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

  ui->intern = lk_intern_new(al, ud);

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

  if (ui->changeset.changes) {
    ui_dealloc(ui, ui->changeset.changes);
  }

  ui->dealloc(ui->alloc_ud, ui);
}

lk_tree *lk_ui_begin_frame(lk_ui *ui) {
  lk_tree_reset(ui->next);
  return ui->next;
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

  return &ui->changeset;
}

const lk_tree *lk_ui_tree(const lk_ui *ui) {
  return ui->prev;
}
