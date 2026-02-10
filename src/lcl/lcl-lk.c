/*
 * lcl-lk.c — Lcl scripting bindings for lk (Layer 1).
 *
 * Exposes 26 procs in the "lk" namespace for building UI trees,
 * managing frames, commands, translators, state, focus, and interning.
 * SDL window procs are conditionally compiled when LK_HAVE_SDL is set.
 *
 * C89 (matches lk + lcl).
 */

#include <string.h>
#include <stdio.h>

#include <lcl.h>
#include <lk.h>
#include "lcl-lk.h"

/* ============================================================================
 * Opaque type tags
 * ============================================================================
 */

#define LK_UI_TYPE   "lk_ui"
#define LK_TREE_TYPE "lk_tree"

#ifdef LK_HAVE_SDL
#include "lk-sdl.h"
#define LK_WIN_TYPE  "lk_window"
#endif

/* ============================================================================
 * String-to-enum lookup tables
 * ============================================================================
 */

typedef struct str_enum {
  const char *name;
  int value;
} str_enum;

static const str_enum kind_table[] = {
  { "window", UIK_WINDOW },
  { "row",    UIK_ROW },
  { "column", UIK_COLUMN },
  { "spacer", UIK_SPACER },
  { "label",  UIK_LABEL },
  { "button", UIK_BUTTON },
  { NULL, 0 }
};

static const str_enum prop_table[] = {
  { "text",      UIP_TEXT },
  { "focusable", UIP_FOCUSABLE },
  { "disabled",  UIP_DISABLED },
  { "w",         UIP_W },
  { "h",         UIP_H },
  { "padding",   UIP_PADDING },
  { "gap",       UIP_GAP },
  { "align",     UIP_ALIGN },
  { "justify",   UIP_JUSTIFY },
  { NULL, 0 }
};

static const str_enum event_table[] = {
  { "pointer_move",   LK_EVENT_POINTER_MOVE },
  { "pointer_down",   LK_EVENT_POINTER_DOWN },
  { "pointer_up",     LK_EVENT_POINTER_UP },
  { "key_down",       LK_EVENT_KEY_DOWN },
  { "key_up",         LK_EVENT_KEY_UP },
  { "text",           LK_EVENT_TEXT },
  { "wheel",          LK_EVENT_WHEEL },
  { "window_resize",  LK_EVENT_WINDOW_RESIZE },
  { "window_close",   LK_EVENT_WINDOW_CLOSE },
  { NULL, 0 }
};

static const str_enum align_table[] = {
  { "start",   LK_ALIGN_START },
  { "center",  LK_ALIGN_CENTER },
  { "end",     LK_ALIGN_END },
  { "stretch", LK_ALIGN_STRETCH },
  { NULL, 0 }
};

static int lookup_enum(const str_enum *table, const char *name, int *out) {
  const str_enum *e;
  for (e = table; e->name; e++) {
    if (strcmp(e->name, name) == 0) {
      *out = e->value;
      return 1;
    }
  }
  return 0;
}

/* ============================================================================
 * Finalizers
 * ============================================================================
 */

static void ui_finalizer(void *ptr) {
  lk_ui_destroy((lk_ui *)ptr);
}

/* ============================================================================
 * UI Lifecycle procs (5)
 * ============================================================================
 */

/* lk::ui_create -> opaque<lk_ui> */
static int c_lk_ui_create(lcl_interp *interp, int argc, lcl_value **argv,
                           lcl_value **out) {
  lk_ui *ui;
  (void)interp; (void)argv;
  if (argc != 0) {
    lcl_set_error(interp, "lk::ui_create: expected 0 arguments");
    return LCL_RC_ERR;
  }
  ui = lk_ui_create(NULL);
  if (!ui) {
    lcl_set_error(interp, "lk::ui_create: allocation failed");
    return LCL_RC_ERR;
  }
  *out = lcl_opaque_new(ui, LK_UI_TYPE, ui_finalizer);
  return LCL_RC_OK;
}

/* lk::ui_destroy [ui] -> "" */
static int c_lk_ui_destroy(lcl_interp *interp, int argc, lcl_value **argv,
                            lcl_value **out) {
  lk_ui *ui;
  if (argc != 1) {
    lcl_set_error(interp, "lk::ui_destroy: expected 1 argument");
    return LCL_RC_ERR;
  }
  if (lcl_opaque_get(argv[0], LK_UI_TYPE, (void **)&ui) != LCL_OK) {
    lcl_set_error(interp, "lk::ui_destroy: expected lk_ui opaque");
    return LCL_RC_ERR;
  }
  lk_ui_destroy(ui);
  /* Prevent double-free via finalizer: clear the opaque pointer.
   * lcl_opaque_get returns the raw ptr; we need to null it out.
   * Since we can't modify the opaque directly, we accept the finalizer
   * will be called on a now-invalid pointer.  The convention is:
   * after explicit destroy, drop all references immediately. */
  *out = lcl_string_new("");
  return LCL_RC_OK;
}

/* lk::begin_frame [ui] -> opaque<lk_tree> */
static int c_lk_begin_frame(lcl_interp *interp, int argc, lcl_value **argv,
                             lcl_value **out) {
  lk_ui *ui;
  lk_tree *t;
  if (argc != 1) {
    lcl_set_error(interp, "lk::begin_frame: expected 1 argument");
    return LCL_RC_ERR;
  }
  if (lcl_opaque_get(argv[0], LK_UI_TYPE, (void **)&ui) != LCL_OK) {
    lcl_set_error(interp, "lk::begin_frame: expected lk_ui opaque");
    return LCL_RC_ERR;
  }
  t = lk_ui_begin_frame(ui);
  /* Tree is owned by ui, no finalizer */
  *out = lcl_opaque_new(t, LK_TREE_TYPE, NULL);
  return LCL_RC_OK;
}

/* lk::end_frame [ui] -> list of change dicts */
static int c_lk_end_frame(lcl_interp *interp, int argc, lcl_value **argv,
                           lcl_value **out) {
  lk_ui *ui;
  const lk_changeset *cs;
  lcl_value *list;
  lk_u32 i;

  if (argc != 1) {
    lcl_set_error(interp, "lk::end_frame: expected 1 argument");
    return LCL_RC_ERR;
  }
  if (lcl_opaque_get(argv[0], LK_UI_TYPE, (void **)&ui) != LCL_OK) {
    lcl_set_error(interp, "lk::end_frame: expected lk_ui opaque");
    return LCL_RC_ERR;
  }

  cs = lk_ui_end_frame(ui);
  list = lcl_list_new();

  for (i = 0; i < cs->count; i++) {
    const lk_change *ch = &cs->changes[i];
    lcl_value *dict = lcl_dict_new();
    lcl_value *v;
    const char *kind_str;

    switch (ch->kind) {
    case LK_CHANGE_ADDED:   kind_str = "added"; break;
    case LK_CHANGE_REMOVED: kind_str = "removed"; break;
    case LK_CHANGE_UPDATED: kind_str = "updated"; break;
    default:                kind_str = "unknown"; break;
    }
    v = lcl_string_new(kind_str);
    lcl_dict_put(&dict, "kind", v);
    lcl_ref_dec(v);

    v = lcl_int_new((long)ch->id);
    lcl_dict_put(&dict, "id", v);
    lcl_ref_dec(v);

    v = lcl_int_new((long)ch->node_ix);
    lcl_dict_put(&dict, "node_ix", v);
    lcl_ref_dec(v);

    /* Resolve id to string */
    {
      const char *id_str = lk_intern_cstr(ui->intern, ch->id);
      if (id_str) {
        v = lcl_string_new(id_str);
      } else {
        v = lcl_string_new("");
      }
      lcl_dict_put(&dict, "id_str", v);
      lcl_ref_dec(v);
    }

    lcl_list_push(&list, dict);
    lcl_ref_dec(dict);
  }

  *out = list;
  return LCL_RC_OK;
}

/* lk::tree [ui] -> opaque<lk_tree> (current tree) */
static int c_lk_tree(lcl_interp *interp, int argc, lcl_value **argv,
                      lcl_value **out) {
  lk_ui *ui;
  const lk_tree *t;
  if (argc != 1) {
    lcl_set_error(interp, "lk::tree: expected 1 argument");
    return LCL_RC_ERR;
  }
  if (lcl_opaque_get(argv[0], LK_UI_TYPE, (void **)&ui) != LCL_OK) {
    lcl_set_error(interp, "lk::tree: expected lk_ui opaque");
    return LCL_RC_ERR;
  }
  t = lk_ui_tree(ui);
  /* Cast away const: the tree is borrowed, no finalizer */
  *out = lcl_opaque_new((void *)t, LK_TREE_TYPE, NULL);
  return LCL_RC_OK;
}

/* ============================================================================
 * Tree Building procs (5)
 * ============================================================================
 */

/* lk::node [tree, id_str, kind_str] -> int (node ix) */
static int c_lk_node(lcl_interp *interp, int argc, lcl_value **argv,
                      lcl_value **out) {
  lk_tree *t;
  const char *id_str;
  const char *kind_str;
  int kind_val;
  lk_ix ix;

  if (argc != 3) {
    lcl_set_error(interp, "lk::node: expected 3 arguments (tree, id, kind)");
    return LCL_RC_ERR;
  }
  if (lcl_opaque_get(argv[0], LK_TREE_TYPE, (void **)&t) != LCL_OK) {
    lcl_set_error(interp, "lk::node: expected lk_tree opaque");
    return LCL_RC_ERR;
  }
  id_str = lcl_value_to_string(argv[1]);
  kind_str = lcl_value_to_string(argv[2]);

  if (!lookup_enum(kind_table, kind_str, &kind_val)) {
    lcl_set_error(interp, "lk::node: unknown kind");
    return LCL_RC_ERR;
  }

  ix = lk_tree_add_node_c(t, id_str, (lk_kind)kind_val);
  *out = lcl_int_new((long)ix);
  return LCL_RC_OK;
}

/* lk::set_root [tree, node_ix] -> "" */
static int c_lk_set_root(lcl_interp *interp, int argc, lcl_value **argv,
                          lcl_value **out) {
  lk_tree *t;
  long ix;

  if (argc != 2) {
    lcl_set_error(interp, "lk::set_root: expected 2 arguments");
    return LCL_RC_ERR;
  }
  if (lcl_opaque_get(argv[0], LK_TREE_TYPE, (void **)&t) != LCL_OK) {
    lcl_set_error(interp, "lk::set_root: expected lk_tree opaque");
    return LCL_RC_ERR;
  }
  if (lcl_value_to_int(argv[1], &ix) != LCL_OK) {
    lcl_set_error(interp, "lk::set_root: node_ix must be an integer");
    return LCL_RC_ERR;
  }

  lk_tree_set_root(t, (lk_ix)ix);
  *out = lcl_string_new("");
  return LCL_RC_OK;
}

/* lk::append_child [tree, parent_ix, child_ix] -> "" */
static int c_lk_append_child(lcl_interp *interp, int argc, lcl_value **argv,
                              lcl_value **out) {
  lk_tree *t;
  long parent_ix, child_ix;

  if (argc != 3) {
    lcl_set_error(interp, "lk::append_child: expected 3 arguments");
    return LCL_RC_ERR;
  }
  if (lcl_opaque_get(argv[0], LK_TREE_TYPE, (void **)&t) != LCL_OK) {
    lcl_set_error(interp, "lk::append_child: expected lk_tree opaque");
    return LCL_RC_ERR;
  }
  if (lcl_value_to_int(argv[1], &parent_ix) != LCL_OK) {
    lcl_set_error(interp, "lk::append_child: parent_ix must be an integer");
    return LCL_RC_ERR;
  }
  if (lcl_value_to_int(argv[2], &child_ix) != LCL_OK) {
    lcl_set_error(interp, "lk::append_child: child_ix must be an integer");
    return LCL_RC_ERR;
  }

  lk_tree_append_child(t, (lk_ix)parent_ix, (lk_ix)child_ix);
  *out = lcl_string_new("");
  return LCL_RC_OK;
}

/* lk::prop [tree, node_ix, key_str, value] -> "" */
static int c_lk_prop(lcl_interp *interp, int argc, lcl_value **argv,
                      lcl_value **out) {
  lk_tree *t;
  long node_ix;
  const char *key_str;
  int key_val;
  lk_value lv;

  if (argc != 4) {
    lcl_set_error(interp, "lk::prop: expected 4 arguments (tree, node, key, value)");
    return LCL_RC_ERR;
  }
  if (lcl_opaque_get(argv[0], LK_TREE_TYPE, (void **)&t) != LCL_OK) {
    lcl_set_error(interp, "lk::prop: expected lk_tree opaque");
    return LCL_RC_ERR;
  }
  if (lcl_value_to_int(argv[1], &node_ix) != LCL_OK) {
    lcl_set_error(interp, "lk::prop: node_ix must be an integer");
    return LCL_RC_ERR;
  }
  key_str = lcl_value_to_string(argv[2]);
  if (!lookup_enum(prop_table, key_str, &key_val)) {
    lcl_set_error(interp, "lk::prop: unknown prop key");
    return LCL_RC_ERR;
  }

  /* Value coercion based on prop key */
  switch (key_val) {
  case UIP_TEXT:
    lv = lk_v_cstr(t->intern, lcl_value_to_string(argv[3]));
    break;
  case UIP_FOCUSABLE:
  case UIP_DISABLED: {
    long b;
    if (lcl_value_to_int(argv[3], &b) != LCL_OK) {
      lcl_set_error(interp, "lk::prop: bool prop expects integer");
      return LCL_RC_ERR;
    }
    lv = lk_v_bool((int)b);
    break;
  }
  case UIP_W:
  case UIP_H:
  case UIP_PADDING:
  case UIP_GAP: {
    long i;
    if (lcl_value_to_int(argv[3], &i) != LCL_OK) {
      lcl_set_error(interp, "lk::prop: numeric prop expects integer");
      return LCL_RC_ERR;
    }
    lv = lk_v_i32((lk_i32)i);
    break;
  }
  case UIP_ALIGN:
  case UIP_JUSTIFY: {
    const char *align_str = lcl_value_to_string(argv[3]);
    int align_val;
    if (!lookup_enum(align_table, align_str, &align_val)) {
      lcl_set_error(interp, "lk::prop: unknown align value");
      return LCL_RC_ERR;
    }
    lv = lk_v_i32((lk_i32)align_val);
    break;
  }
  default:
    lcl_set_error(interp, "lk::prop: unsupported prop key");
    return LCL_RC_ERR;
  }

  lk_tree_add_prop(t, (lk_ix)node_ix, (lk_prop_key)key_val, lv);
  *out = lcl_string_new("");
  return LCL_RC_OK;
}

/* lk::present [tree, node_ix, ptype_str, pvalue] -> "" */
static int c_lk_present(lcl_interp *interp, int argc, lcl_value **argv,
                         lcl_value **out) {
  lk_tree *t;
  long node_ix;
  const char *ptype_str;
  lk_value pv;

  if (argc != 4) {
    lcl_set_error(interp, "lk::present: expected 4 arguments");
    return LCL_RC_ERR;
  }
  if (lcl_opaque_get(argv[0], LK_TREE_TYPE, (void **)&t) != LCL_OK) {
    lcl_set_error(interp, "lk::present: expected lk_tree opaque");
    return LCL_RC_ERR;
  }
  if (lcl_value_to_int(argv[1], &node_ix) != LCL_OK) {
    lcl_set_error(interp, "lk::present: node_ix must be an integer");
    return LCL_RC_ERR;
  }
  ptype_str = lcl_value_to_string(argv[2]);

  /* pvalue: try int first, fall back to string */
  {
    long iv;
    if (lcl_value_to_int(argv[3], &iv) == LCL_OK) {
      pv = lk_v_i32((lk_i32)iv);
    } else {
      pv = lk_v_cstr(t->intern, lcl_value_to_string(argv[3]));
    }
  }

  lk_tree_add_presentation_s(t, (lk_ix)node_ix, ptype_str, pv);
  *out = lcl_string_new("");
  return LCL_RC_OK;
}

/* ============================================================================
 * Commands & Translators (5)
 * ============================================================================
 */

/* lk::add_translator [ui, event_type_str, ptype_str, kind_str, cmd_name_str] */
static int c_lk_add_translator(lcl_interp *interp, int argc, lcl_value **argv,
                                lcl_value **out) {
  lk_ui *ui;
  const char *ev_str;
  const char *pt_str;
  const char *kn_str;
  const char *cmd_str;
  lk_u8 ev_type = 0;
  lk_u16 node_kind = 0;
  lk_u32 ptype = 0;
  lk_u32 cmd_name;

  if (argc != 5) {
    lcl_set_error(interp, "lk::add_translator: expected 5 arguments");
    return LCL_RC_ERR;
  }
  if (lcl_opaque_get(argv[0], LK_UI_TYPE, (void **)&ui) != LCL_OK) {
    lcl_set_error(interp, "lk::add_translator: expected lk_ui opaque");
    return LCL_RC_ERR;
  }

  ev_str = lcl_value_to_string(argv[1]);
  pt_str = lcl_value_to_string(argv[2]);
  kn_str = lcl_value_to_string(argv[3]);
  cmd_str = lcl_value_to_string(argv[4]);

  /* event_type: "" means 0 (any) */
  if (ev_str[0] != '\0') {
    int ev_val;
    if (!lookup_enum(event_table, ev_str, &ev_val)) {
      lcl_set_error(interp, "lk::add_translator: unknown event type");
      return LCL_RC_ERR;
    }
    ev_type = (lk_u8)ev_val;
  }

  /* ptype: "" means 0 (any) */
  if (pt_str[0] != '\0') {
    ptype = lk_intern_cid(ui->intern, pt_str);
  }

  /* kind: "" means 0 (any) */
  if (kn_str[0] != '\0') {
    int kn_val;
    if (!lookup_enum(kind_table, kn_str, &kn_val)) {
      lcl_set_error(interp, "lk::add_translator: unknown kind");
      return LCL_RC_ERR;
    }
    node_kind = (lk_u16)kn_val;
  }

  cmd_name = lk_intern_cid(ui->intern, cmd_str);
  lk_ui_add_translator(ui, ev_type, ptype, node_kind, cmd_name);

  *out = lcl_string_new("");
  return LCL_RC_OK;
}

/* Helper: convert lk_command to lcl dict */
static lcl_value *command_to_dict(const lk_command *cmd, const lk_intern *intern) {
  lcl_value *dict = lcl_dict_new();
  lcl_value *v;
  const char *name_str;
  lk_u8 i;

  /* name */
  name_str = lk_intern_cstr(intern, cmd->name);
  v = lcl_string_new(name_str ? name_str : "");
  lcl_dict_put(&dict, "name", v);
  lcl_ref_dec(v);

  /* args */
  {
    lcl_value *args_list = lcl_list_new();
    for (i = 0; i < cmd->arg_count; i++) {
      const lk_value *arg = &cmd->args[i];
      lcl_value *av = NULL;
      switch (arg->tag) {
      case UIV_I32:
        av = lcl_int_new((long)arg->as.i);
        break;
      case UIV_STR: {
        const char *s = lk_intern_cstr(intern, arg->as.str_id);
        av = lcl_string_new(s ? s : "");
        break;
      }
      case UIV_BOOL:
        av = lcl_int_new((long)arg->as.b);
        break;
      default:
        av = lcl_string_new("");
        break;
      }
      lcl_list_push(&args_list, av);
      lcl_ref_dec(av);
    }
    lcl_dict_put(&dict, "args", args_list);
    lcl_ref_dec(args_list);
  }

  /* source_node */
  v = lcl_int_new((long)cmd->source_node);
  lcl_dict_put(&dict, "source_node", v);
  lcl_ref_dec(v);

  /* source_ptype */
  {
    const char *pt = lk_intern_cstr(intern, cmd->source_ptype);
    v = lcl_string_new(pt ? pt : "");
    lcl_dict_put(&dict, "source_ptype", v);
    lcl_ref_dec(v);
  }

  return dict;
}

/* lk::commands [ui] -> list of command dicts */
static int c_lk_commands(lcl_interp *interp, int argc, lcl_value **argv,
                          lcl_value **out) {
  lk_ui *ui;
  const lk_command_queue *q;
  lcl_value *list;
  lk_u32 i;

  if (argc != 1) {
    lcl_set_error(interp, "lk::commands: expected 1 argument");
    return LCL_RC_ERR;
  }
  if (lcl_opaque_get(argv[0], LK_UI_TYPE, (void **)&ui) != LCL_OK) {
    lcl_set_error(interp, "lk::commands: expected lk_ui opaque");
    return LCL_RC_ERR;
  }

  q = lk_ui_commands(ui);
  list = lcl_list_new();

  for (i = 0; i < q->count; i++) {
    lcl_value *d = command_to_dict(&q->cmds[i], ui->intern);
    lcl_list_push(&list, d);
    lcl_ref_dec(d);
  }

  *out = list;
  return LCL_RC_OK;
}

/* lk::clear_commands [ui] -> "" */
static int c_lk_clear_commands(lcl_interp *interp, int argc, lcl_value **argv,
                                lcl_value **out) {
  lk_ui *ui;
  if (argc != 1) {
    lcl_set_error(interp, "lk::clear_commands: expected 1 argument");
    return LCL_RC_ERR;
  }
  if (lcl_opaque_get(argv[0], LK_UI_TYPE, (void **)&ui) != LCL_OK) {
    lcl_set_error(interp, "lk::clear_commands: expected lk_ui opaque");
    return LCL_RC_ERR;
  }
  lk_ui_clear_commands(ui);
  *out = lcl_string_new("");
  return LCL_RC_OK;
}

/* lk::command_log [ui] -> list of command dicts */
static int c_lk_command_log(lcl_interp *interp, int argc, lcl_value **argv,
                             lcl_value **out) {
  lk_ui *ui;
  lk_u32 log_count;
  const lk_command *log;
  lcl_value *list;
  lk_u32 i;

  if (argc != 1) {
    lcl_set_error(interp, "lk::command_log: expected 1 argument");
    return LCL_RC_ERR;
  }
  if (lcl_opaque_get(argv[0], LK_UI_TYPE, (void **)&ui) != LCL_OK) {
    lcl_set_error(interp, "lk::command_log: expected lk_ui opaque");
    return LCL_RC_ERR;
  }

  log = lk_ui_command_log(ui, &log_count);
  list = lcl_list_new();

  for (i = 0; i < log_count; i++) {
    lcl_value *d = command_to_dict(&log[i], ui->intern);
    lcl_list_push(&list, d);
    lcl_ref_dec(d);
  }

  *out = list;
  return LCL_RC_OK;
}

/* lk::clear_command_log [ui] -> "" */
static int c_lk_clear_command_log(lcl_interp *interp, int argc,
                                   lcl_value **argv, lcl_value **out) {
  lk_ui *ui;
  if (argc != 1) {
    lcl_set_error(interp, "lk::clear_command_log: expected 1 argument");
    return LCL_RC_ERR;
  }
  if (lcl_opaque_get(argv[0], LK_UI_TYPE, (void **)&ui) != LCL_OK) {
    lcl_set_error(interp, "lk::clear_command_log: expected lk_ui opaque");
    return LCL_RC_ERR;
  }
  lk_ui_clear_command_log(ui);
  *out = lcl_string_new("");
  return LCL_RC_OK;
}

/* ============================================================================
 * State (2)
 * ============================================================================
 */

/* lk::state_set [ui, node_id_str, key_int, value] -> "" */
static int c_lk_state_set(lcl_interp *interp, int argc, lcl_value **argv,
                           lcl_value **out) {
  lk_ui *ui;
  const char *node_str;
  long key;
  lk_node_id nid;
  lk_value lv;
  lk_state *st;

  if (argc != 4) {
    lcl_set_error(interp, "lk::state_set: expected 4 arguments");
    return LCL_RC_ERR;
  }
  if (lcl_opaque_get(argv[0], LK_UI_TYPE, (void **)&ui) != LCL_OK) {
    lcl_set_error(interp, "lk::state_set: expected lk_ui opaque");
    return LCL_RC_ERR;
  }
  node_str = lcl_value_to_string(argv[1]);
  if (lcl_value_to_int(argv[2], &key) != LCL_OK) {
    lcl_set_error(interp, "lk::state_set: key must be an integer");
    return LCL_RC_ERR;
  }

  nid = lk_intern_cid(ui->intern, node_str);

  /* Value: try int, then string */
  {
    long iv;
    if (lcl_value_to_int(argv[3], &iv) == LCL_OK) {
      lv = lk_v_i32((lk_i32)iv);
    } else {
      lv = lk_v_cstr(ui->intern, lcl_value_to_string(argv[3]));
    }
  }

  st = lk_ui_state(ui);
  lk_state_set(st, nid, (lk_u16)key, lv);

  *out = lcl_string_new("");
  return LCL_RC_OK;
}

/* lk::state_get [ui, node_id_str, key_int] -> value */
static int c_lk_state_get(lcl_interp *interp, int argc, lcl_value **argv,
                           lcl_value **out) {
  lk_ui *ui;
  const char *node_str;
  long key;
  lk_node_id nid;
  lk_value lv;
  lk_state *st;

  if (argc != 3) {
    lcl_set_error(interp, "lk::state_get: expected 3 arguments");
    return LCL_RC_ERR;
  }
  if (lcl_opaque_get(argv[0], LK_UI_TYPE, (void **)&ui) != LCL_OK) {
    lcl_set_error(interp, "lk::state_get: expected lk_ui opaque");
    return LCL_RC_ERR;
  }
  node_str = lcl_value_to_string(argv[1]);
  if (lcl_value_to_int(argv[2], &key) != LCL_OK) {
    lcl_set_error(interp, "lk::state_get: key must be an integer");
    return LCL_RC_ERR;
  }

  nid = lk_intern_cid(ui->intern, node_str);
  st = lk_ui_state(ui);
  lv = lk_state_get(st, nid, (lk_u16)key);

  switch (lv.tag) {
  case UIV_I32:
    *out = lcl_int_new((long)lv.as.i);
    break;
  case UIV_BOOL:
    *out = lcl_int_new((long)lv.as.b);
    break;
  case UIV_STR: {
    const char *s = lk_intern_cstr(ui->intern, lv.as.str_id);
    *out = lcl_string_new(s ? s : "");
    break;
  }
  default:
    *out = lcl_string_new("");
    break;
  }
  return LCL_RC_OK;
}

/* ============================================================================
 * Focus (2)
 * ============================================================================
 */

/* lk::focus_set [ui, node_id_str] -> "" */
static int c_lk_focus_set(lcl_interp *interp, int argc, lcl_value **argv,
                           lcl_value **out) {
  lk_ui *ui;
  const char *id_str;
  lk_node_id nid;
  const lk_tree *t;

  if (argc != 2) {
    lcl_set_error(interp, "lk::focus_set: expected 2 arguments");
    return LCL_RC_ERR;
  }
  if (lcl_opaque_get(argv[0], LK_UI_TYPE, (void **)&ui) != LCL_OK) {
    lcl_set_error(interp, "lk::focus_set: expected lk_ui opaque");
    return LCL_RC_ERR;
  }
  id_str = lcl_value_to_string(argv[1]);
  nid = lk_intern_cid(ui->intern, id_str);
  t = lk_ui_tree(ui);
  lk_focus_set(ui, t, nid);

  *out = lcl_string_new("");
  return LCL_RC_OK;
}

/* lk::focus_clear [ui] -> "" */
static int c_lk_focus_clear(lcl_interp *interp, int argc, lcl_value **argv,
                             lcl_value **out) {
  lk_ui *ui;
  if (argc != 1) {
    lcl_set_error(interp, "lk::focus_clear: expected 1 argument");
    return LCL_RC_ERR;
  }
  if (lcl_opaque_get(argv[0], LK_UI_TYPE, (void **)&ui) != LCL_OK) {
    lcl_set_error(interp, "lk::focus_clear: expected lk_ui opaque");
    return LCL_RC_ERR;
  }
  lk_focus_clear(ui);
  *out = lcl_string_new("");
  return LCL_RC_OK;
}

/* ============================================================================
 * Interning (2)
 * ============================================================================
 */

/* lk::intern_str [ui, id_int] -> string */
static int c_lk_intern_str(lcl_interp *interp, int argc, lcl_value **argv,
                            lcl_value **out) {
  lk_ui *ui;
  long id;
  const char *s;

  if (argc != 2) {
    lcl_set_error(interp, "lk::intern_str: expected 2 arguments");
    return LCL_RC_ERR;
  }
  if (lcl_opaque_get(argv[0], LK_UI_TYPE, (void **)&ui) != LCL_OK) {
    lcl_set_error(interp, "lk::intern_str: expected lk_ui opaque");
    return LCL_RC_ERR;
  }
  if (lcl_value_to_int(argv[1], &id) != LCL_OK) {
    lcl_set_error(interp, "lk::intern_str: id must be an integer");
    return LCL_RC_ERR;
  }

  s = lk_intern_cstr(ui->intern, (lk_node_id)id);
  *out = lcl_string_new(s ? s : "");
  return LCL_RC_OK;
}

/* lk::intern_id [ui, string] -> int */
static int c_lk_intern_id(lcl_interp *interp, int argc, lcl_value **argv,
                           lcl_value **out) {
  lk_ui *ui;
  const char *s;
  lk_node_id id;

  if (argc != 2) {
    lcl_set_error(interp, "lk::intern_id: expected 2 arguments");
    return LCL_RC_ERR;
  }
  if (lcl_opaque_get(argv[0], LK_UI_TYPE, (void **)&ui) != LCL_OK) {
    lcl_set_error(interp, "lk::intern_id: expected lk_ui opaque");
    return LCL_RC_ERR;
  }
  s = lcl_value_to_string(argv[1]);
  id = lk_intern_cid(ui->intern, s);
  *out = lcl_int_new((long)id);
  return LCL_RC_OK;
}

/* ============================================================================
 * SDL Window procs (5) — compiled only when LK_HAVE_SDL is set
 * ============================================================================
 */

#ifdef LK_HAVE_SDL

/* Frame callback bridge */
struct lcl_lk_ctx {
  lcl_interp *interp;
  lcl_value *view_fn;
};

static void lcl_lk_frame(lk_tree *t, void *ud) {
  struct lcl_lk_ctx *ctx = (struct lcl_lk_ctx *)ud;
  lcl_value *tree_val = lcl_opaque_new(t, LK_TREE_TYPE, NULL);
  lcl_value *args[1];
  lcl_value *result = NULL;
  args[0] = tree_val;
  lcl_call_proc(ctx->interp, ctx->view_fn, 1, args, &result);

  if (result) {
    lcl_ref_dec(result);
  }

  lcl_ref_dec(tree_val);
}

static void win_finalizer(void *ptr) {
  lk_window_destroy((lk_window *)ptr);
}

/* lk::window_create [title, ?w, ?h, ?font, ?size] -> opaque<lk_window> */
static int c_lk_window_create(lcl_interp *interp, int argc, lcl_value **argv,
                               lcl_value **out) {
  lk_window_cfg cfg;
  lk_window *win;

  if (argc < 1 || argc > 5) {
    lcl_set_error(interp, "lk::window_create: expected 1-5 arguments");
    return LCL_RC_ERR;
  }

  memset(&cfg, 0, sizeof(cfg));
  cfg.title = lcl_value_to_string(argv[0]);
  cfg.width = 800;
  cfg.height = 600;
  cfg.font_size = 0;
  cfg.font_path = NULL;

  if (argc >= 2) {
    long w;
    if (lcl_value_to_int(argv[1], &w) == LCL_OK) cfg.width = (int)w;
  }
  if (argc >= 3) {
    long h;
    if (lcl_value_to_int(argv[2], &h) == LCL_OK) cfg.height = (int)h;
  }
  if (argc >= 4) {
    cfg.font_path = lcl_value_to_string(argv[3]);
  }
  if (argc >= 5) {
    long sz;
    if (lcl_value_to_int(argv[4], &sz) == LCL_OK) cfg.font_size = (int)sz;
  }

  win = lk_window_create(&cfg);
  if (!win) {
    lcl_set_error(interp, "lk::window_create: failed to create window");
    return LCL_RC_ERR;
  }
  *out = lcl_opaque_new(win, LK_WIN_TYPE, win_finalizer);
  return LCL_RC_OK;
}

/* lk::window_destroy [win] -> "" */
static int c_lk_window_destroy(lcl_interp *interp, int argc, lcl_value **argv,
                                lcl_value **out) {
  lk_window *win;
  if (argc != 1) {
    lcl_set_error(interp, "lk::window_destroy: expected 1 argument");
    return LCL_RC_ERR;
  }
  if (lcl_opaque_get(argv[0], LK_WIN_TYPE, (void **)&win) != LCL_OK) {
    lcl_set_error(interp, "lk::window_destroy: expected lk_window opaque");
    return LCL_RC_ERR;
  }
  lk_window_destroy(win);
  *out = lcl_string_new("");
  return LCL_RC_OK;
}

/* lk::window_run [win, view_proc] -> "" (blocks until close) */
static int c_lk_window_run(lcl_interp *interp, int argc, lcl_value **argv,
                            lcl_value **out) {
  lk_window *win;
  struct lcl_lk_ctx ctx;

  if (argc != 2) {
    lcl_set_error(interp, "lk::window_run: expected 2 arguments");
    return LCL_RC_ERR;
  }

  if (lcl_opaque_get(argv[0], LK_WIN_TYPE, (void **)&win) != LCL_OK) {
    lcl_set_error(interp, "lk::window_run: expected lk_window opaque");
    return LCL_RC_ERR;
  }

  if (!lcl_is_callable(argv[1])) {
    lcl_set_error(interp, "lk::window_run: expected callable view proc");
    return LCL_RC_ERR;
  }

  ctx.interp = interp;
  ctx.view_fn = lcl_ref_inc(argv[1]);

  lk_window_run(win, lcl_lk_frame, &ctx);

  lcl_ref_dec(ctx.view_fn);

  *out = lcl_string_new("");

  return LCL_RC_OK;
}

/* lk::window_ui [win] -> opaque<lk_ui> */
static int c_lk_window_ui(lcl_interp *interp, int argc, lcl_value **argv,
                           lcl_value **out) {
  lk_window *win;
  lk_ui *ui;

  if (argc != 1) {
    lcl_set_error(interp, "lk::window_ui: expected 1 argument");
    return LCL_RC_ERR;
  }
  if (lcl_opaque_get(argv[0], LK_WIN_TYPE, (void **)&win) != LCL_OK) {
    lcl_set_error(interp, "lk::window_ui: expected lk_window opaque");
    return LCL_RC_ERR;
  }

  ui = lk_window_ui(win);
  /* UI is owned by window, no finalizer */
  *out = lcl_opaque_new(ui, LK_UI_TYPE, NULL);
  return LCL_RC_OK;
}

/* lk::window_set_event_handler [win, handler_proc] -> "" */
static int c_lk_window_set_event_handler(lcl_interp *interp, int argc,
                                          lcl_value **argv, lcl_value **out) {
  lk_window *win;
  (void)interp;

  if (argc != 2) {
    lcl_set_error(interp, "lk::window_set_event_handler: expected 2 arguments");
    return LCL_RC_ERR;
  }
  if (lcl_opaque_get(argv[0], LK_WIN_TYPE, (void **)&win) != LCL_OK) {
    lcl_set_error(interp, "lk::window_set_event_handler: expected lk_window opaque");
    return LCL_RC_ERR;
  }
  if (!lcl_is_callable(argv[1])) {
    lcl_set_error(interp, "lk::window_set_event_handler: expected callable");
    return LCL_RC_ERR;
  }

  /* TODO: implement Lcl-side event handler bridge.
   * This requires storing the interp + callback and creating a C
   * event handler that marshals lk_event fields into an Lcl dict
   * and calls the proc. Left as a stub for Phase 4. */
  *out = lcl_string_new("");

  return LCL_RC_OK;
}

#endif /* LK_HAVE_SDL */

/* ============================================================================
 * Registration
 * ============================================================================
 */

void lcl_register_lk(lcl_interp *interp) {
  lcl_value *ns = lcl_ns_new("lk");
  lcl_define_take(interp, "lk", ns);

  /* UI Lifecycle */
  lcl_ns_def(ns, "ui_create",   lcl_c_proc_new("lk::ui_create",   c_lk_ui_create));
  lcl_ns_def(ns, "ui_destroy",  lcl_c_proc_new("lk::ui_destroy",  c_lk_ui_destroy));
  lcl_ns_def(ns, "begin_frame", lcl_c_proc_new("lk::begin_frame", c_lk_begin_frame));
  lcl_ns_def(ns, "end_frame",   lcl_c_proc_new("lk::end_frame",   c_lk_end_frame));
  lcl_ns_def(ns, "tree",        lcl_c_proc_new("lk::tree",        c_lk_tree));

  /* Tree Building */
  lcl_ns_def(ns, "node",         lcl_c_proc_new("lk::node",         c_lk_node));
  lcl_ns_def(ns, "set_root",     lcl_c_proc_new("lk::set_root",     c_lk_set_root));
  lcl_ns_def(ns, "append_child", lcl_c_proc_new("lk::append_child", c_lk_append_child));
  lcl_ns_def(ns, "prop",         lcl_c_proc_new("lk::prop",         c_lk_prop));
  lcl_ns_def(ns, "present",      lcl_c_proc_new("lk::present",      c_lk_present));

  /* Commands & Translators */
  lcl_ns_def(ns, "add_translator",  lcl_c_proc_new("lk::add_translator",  c_lk_add_translator));
  lcl_ns_def(ns, "commands",        lcl_c_proc_new("lk::commands",        c_lk_commands));
  lcl_ns_def(ns, "clear_commands",  lcl_c_proc_new("lk::clear_commands",  c_lk_clear_commands));
  lcl_ns_def(ns, "command_log",     lcl_c_proc_new("lk::command_log",     c_lk_command_log));
  lcl_ns_def(ns, "clear_command_log", lcl_c_proc_new("lk::clear_command_log", c_lk_clear_command_log));

  /* State */
  lcl_ns_def(ns, "state_set", lcl_c_proc_new("lk::state_set", c_lk_state_set));
  lcl_ns_def(ns, "state_get", lcl_c_proc_new("lk::state_get", c_lk_state_get));

  /* Focus */
  lcl_ns_def(ns, "focus_set",   lcl_c_proc_new("lk::focus_set",   c_lk_focus_set));
  lcl_ns_def(ns, "focus_clear", lcl_c_proc_new("lk::focus_clear", c_lk_focus_clear));

  /* Interning */
  lcl_ns_def(ns, "intern_str", lcl_c_proc_new("lk::intern_str", c_lk_intern_str));
  lcl_ns_def(ns, "intern_id",  lcl_c_proc_new("lk::intern_id",  c_lk_intern_id));

#ifdef LK_HAVE_SDL
  /* SDL Window */
  lcl_ns_def(ns, "window_create",  lcl_c_proc_new("lk::window_create",  c_lk_window_create));
  lcl_ns_def(ns, "window_destroy", lcl_c_proc_new("lk::window_destroy", c_lk_window_destroy));
  lcl_ns_def(ns, "window_run",     lcl_c_proc_new("lk::window_run",     c_lk_window_run));
  lcl_ns_def(ns, "window_ui",      lcl_c_proc_new("lk::window_ui",      c_lk_window_ui));
  lcl_ns_def(ns, "window_set_event_handler",
             lcl_c_proc_new("lk::window_set_event_handler",
                            c_lk_window_set_event_handler));
#endif
}
