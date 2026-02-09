/*
 * lk-api.c — Binding-safe accessor functions.
 *
 * Trivial one-liner wrappers that expose struct fields through function
 * calls so language bindings never reach into struct internals.
 * All null-safe with bounds checking.
 */

#include "lk-data.h"

/* ---- Node field accessors ---- */

lk_node_id lk_node_id_get(const lk_tree *t, lk_ix n) {
  if (!t || n == 0 || n >= t->node_count) {
    return 0;
  }
  return t->nodes[n].id;
}

lk_u16 lk_node_kind_get(const lk_tree *t, lk_ix n) {
  if (!t || n == 0 || n >= t->node_count) {
    return 0;
  }
  return t->nodes[n].kind;
}

lk_ix lk_node_parent(const lk_tree *t, lk_ix n) {
  if (!t || n == 0 || n >= t->node_count) {
    return 0;
  }
  return t->nodes[n].parent;
}

lk_ix lk_node_first_child(const lk_tree *t, lk_ix n) {
  if (!t || n == 0 || n >= t->node_count) {
    return 0;
  }
  return t->nodes[n].first_child;
}

lk_ix lk_node_next_sibling(const lk_tree *t, lk_ix n) {
  if (!t || n == 0 || n >= t->node_count) {
    return 0;
  }
  return t->nodes[n].next_sibling;
}

/* ---- Tree accessors ---- */

lk_u32 lk_tree_node_count(const lk_tree *t) {
  if (!t) {
    return 0;
  }
  return t->node_count;
}

lk_ix lk_tree_root(const lk_tree *t) {
  if (!t) {
    return 0;
  }
  return t->root;
}

lk_intern *lk_tree_intern(const lk_tree *t) {
  if (!t) {
    return NULL;
  }
  return t->intern;
}

/* ---- Changeset accessors ---- */

lk_u32 lk_changeset_count(const lk_changeset *cs) {
  if (!cs) {
    return 0;
  }
  return cs->count;
}

const lk_change *lk_changeset_get(const lk_changeset *cs, lk_u32 idx) {
  if (!cs || idx >= cs->count) {
    return NULL;
  }
  return &cs->changes[idx];
}

/* ---- Command queue accessors ---- */

lk_u32 lk_command_queue_count(const lk_command_queue *q) {
  if (!q) {
    return 0;
  }
  return q->count;
}

const lk_command *lk_command_queue_get(const lk_command_queue *q, lk_u32 idx) {
  if (!q || idx >= q->count) {
    return NULL;
  }
  return &q->cmds[idx];
}

/* ---- Command field accessors ---- */

lk_u32 lk_command_name(const lk_command *cmd) {
  if (!cmd) {
    return 0;
  }
  return cmd->name;
}

lk_u8 lk_command_arg_count(const lk_command *cmd) {
  if (!cmd) {
    return 0;
  }
  return cmd->arg_count;
}

lk_value lk_command_arg(const lk_command *cmd, lk_u8 idx) {
  lk_value none;
  none.tag = UIV_NONE;
  none.as.i = 0;
  if (!cmd || idx >= cmd->arg_count || idx >= LK_CMD_MAX_ARGS) {
    return none;
  }
  return cmd->args[idx];
}

lk_ix lk_command_source_node(const lk_command *cmd) {
  if (!cmd) {
    return 0;
  }
  return cmd->source_node;
}

lk_u32 lk_command_source_ptype(const lk_command *cmd) {
  if (!cmd) {
    return 0;
  }
  return cmd->source_ptype;
}
