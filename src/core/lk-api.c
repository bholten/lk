/*
 * lk-api.c — Binding-safe accessor functions.
 *
 * Trivial one-liner wrappers that expose struct fields through function
 * calls so language bindings never reach into struct internals.
 * All null-safe with bounds checking.
 */

#include <string.h>

#include <lk.h>

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

lk_u8 lk_command_arg_tag(const lk_command *cmd, lk_u8 idx) {
  if (!cmd || idx >= cmd->arg_count || idx >= LK_CMD_MAX_ARGS) {
    return UIV_NONE;
  }

  return (lk_u8)cmd->args[idx].tag;
}

lk_i32 lk_command_arg_i32(const lk_command *cmd, lk_u8 idx) {
  if (!cmd || idx >= cmd->arg_count || idx >= LK_CMD_MAX_ARGS) {
    return 0;
  }

  if (cmd->args[idx].tag != UIV_I32) {
    return 0;
  }

  return (lk_i32)cmd->args[idx].as.i;
}

lk_u32 lk_command_arg_str_id(const lk_command *cmd, lk_u8 idx) {
  if (!cmd || idx >= cmd->arg_count || idx >= LK_CMD_MAX_ARGS) {
    return 0;
  }

  if (cmd->args[idx].tag != UIV_STR) {
    return 0;
  }

  return cmd->args[idx].as.str_id;
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

lk_value lk_command_source_value(const lk_command *cmd) {
  if (!cmd) {
    return lk_v_none();
  }

  return cmd->source_value;
}

/* ---- UI accessors ---- */

lk_intern *lk_ui_intern(const lk_ui *ui) {
  if (!ui) {
    return NULL;
  }

  return ui->intern;
}

/* ---- Binding-friendly const char* accessors ---- */

lk_ix lk_tree_add_node_c(lk_tree *t, const char *id_str, lk_kind kind) {
  if (!id_str) {
    return 0;
  }

  return lk_tree_add_node_s(t, lk_str_c(id_str), kind);
}

const char *lk_node_text_cstr(const lk_tree *t, lk_ix n) {
  lk_u32 sid;

  if (!t || n == 0 || n >= t->node_count) {
    return "";
  }

  sid = lk_node_text_id(t, n);

  if (sid == 0) {
    return "";
  }

  return lk_intern_cstr(t->intern, sid);
}

/* ---- Event init helpers ---- */

void lk_event_init_pointer(lk_event *ev, lk_u8 type, lk_i32 x, lk_i32 y,
                           lk_u8 button) {
  if (!ev) {
    return;
  }

  memset(ev, 0, sizeof(*ev));
  ev->type = type;
  ev->data.pointer.x = x;
  ev->data.pointer.y = y;
  ev->data.pointer.button = button;
}

void lk_event_init_key(lk_event *ev, lk_u8 type, lk_u16 keycode, lk_u8 mods) {
  if (!ev) {
    return;
  }

  memset(ev, 0, sizeof(*ev));
  ev->type = type;
  ev->data.key.keycode = keycode;
  ev->mods = mods;
}

/* ---- Layout convenience ---- */

int lk_layout_simple(const lk_tree *t, lk_i32 viewport_w, lk_i32 viewport_h,
                     lk_rect *rects) {
  lk_layout_cfg cfg;
  memset(&cfg, 0, sizeof(cfg));
  cfg.measure_text = lk_measure_text_stub;
  cfg.viewport_w = viewport_w;
  cfg.viewport_h = viewport_h;
  cfg.styles = NULL;

  return lk_layout(t, &cfg, rects);
}
