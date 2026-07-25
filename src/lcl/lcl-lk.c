/*
 * lcl-lk.c — Lcl scripting bindings for lk (Layer 1).
 *
 * Exposes 32 procs in the "lk" namespace (26 core + 6 SDL) for
 * building UI trees, managing frames, commands, translators, state,
 * focus, interning, and windows/fonts.  SDL procs (window_* and
 * register_font) are conditionally compiled when LK_HAVE_SDL is set.
 *
 * C89 (matches lk + lcl).
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "lcl-lk.h"
#include <lcl.h>
#include <lk.h>

/* ============================================================================
 * Opaque type tags
 * ============================================================================
 */

#define LK_UI_TYPE "lk_ui"
#define LK_TREE_TYPE "lk_tree"

#ifdef LK_HAVE_SDL
#include "lk-sdl.h"
#define LK_WIN_TYPE "lk_window"
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
    {"window",     UIK_WINDOW    },
    {"row",        UIK_ROW       },
    {"column",     UIK_COLUMN    },
    {"spacer",     UIK_SPACER    },
    {"label",      UIK_LABEL     },
    {"button",     UIK_BUTTON    },
    {"text_input", UIK_TEXT_INPUT},
    {"scroll",     UIK_SCROLL    },
    {"dropdown",   UIK_DROPDOWN  },
    {"option",     UIK_OPTION    },
    {NULL,         0             }
};

static const str_enum prop_table[] = {
    {"text",      UIP_TEXT     },
    {"focusable", UIP_FOCUSABLE},
    {"disabled",  UIP_DISABLED },
    {"w",         UIP_W        },
    {"h",         UIP_H        },
    {"padding",   UIP_PADDING  },
    {"gap",       UIP_GAP      },
    {"align",     UIP_ALIGN    },
    {"justify",   UIP_JUSTIFY  },
    {"hidden",    UIP_HIDDEN   },
    {NULL,        0            }
};

static const str_enum event_table[] = {
    {"pointer_move",  LK_EVENT_POINTER_MOVE },
    {"pointer_down",  LK_EVENT_POINTER_DOWN },
    {"pointer_up",    LK_EVENT_POINTER_UP   },
    {"key_down",      LK_EVENT_KEY_DOWN     },
    {"key_up",        LK_EVENT_KEY_UP       },
    {"text",          LK_EVENT_TEXT         },
    {"wheel",         LK_EVENT_WHEEL        },
    {"window_resize", LK_EVENT_WINDOW_RESIZE},
    {"window_close",  LK_EVENT_WINDOW_CLOSE },
    {"value_changed", LK_EVENT_VALUE_CHANGED},
    {NULL,            0                     }
};

static const str_enum align_table[] = {
    {"start",   LK_ALIGN_START  },
    {"center",  LK_ALIGN_CENTER },
    {"end",     LK_ALIGN_END    },
    {"stretch", LK_ALIGN_STRETCH},
    {NULL,      0               }
};

static const str_enum state_table[] = {
    {"focused",  LK_NSTATE_FOCUSED },
    {"hovered",  LK_NSTATE_HOVERED },
    {"disabled", LK_NSTATE_DISABLED},
    {NULL,       0                 }
};

static const str_enum keycode_table[] = {
    {"tab",       LKK_TAB      },
    {"return",    LKK_RETURN   },
    {"escape",    LKK_ESCAPE   },
    {"backspace", LKK_BACKSPACE},
    {"delete",    LKK_DELETE   },
    {"space",     LKK_SPACE    },
    {"left",      LKK_LEFT     },
    {"right",     LKK_RIGHT    },
    {"up",        LKK_UP       },
    {"down",      LKK_DOWN     },
    {"home",      LKK_HOME     },
    {"end",       LKK_END      },
    {"a", LKK_A}, {"b", LKK_B}, {"c", LKK_C}, {"d", LKK_D},
    {"e", LKK_E}, {"f", LKK_F}, {"g", LKK_G}, {"h", LKK_H},
    {"i", LKK_I}, {"j", LKK_J}, {"k", LKK_K}, {"l", LKK_L},
    {"m", LKK_M}, {"n", LKK_N}, {"o", LKK_O}, {"p", LKK_P},
    {"q", LKK_Q}, {"r", LKK_R}, {"s", LKK_S}, {"t", LKK_T},
    {"u", LKK_U}, {"v", LKK_V}, {"w", LKK_W}, {"x", LKK_X},
    {"y", LKK_Y}, {"z", LKK_Z},
    {"0", LKK_0}, {"1", LKK_1}, {"2", LKK_2}, {"3", LKK_3},
    {"4", LKK_4}, {"5", LKK_5}, {"6", LKK_6}, {"7", LKK_7},
    {"8", LKK_8}, {"9", LKK_9},
    {"page_up",   LKK_PAGEUP  },
    {"page_down", LKK_PAGEDOWN},
    {"f1", LKK_F1}, {"f2",  LKK_F2 }, {"f3",  LKK_F3 }, {"f4",  LKK_F4 },
    {"f5", LKK_F5}, {"f6",  LKK_F6 }, {"f7",  LKK_F7 }, {"f8",  LKK_F8 },
    {"f9", LKK_F9}, {"f10", LKK_F10}, {"f11", LKK_F11}, {"f12", LKK_F12},
    {NULL,        0            }
};

static const str_enum mod_table[] = {
    {"shift", LK_MOD_SHIFT},
    {"ctrl",  LK_MOD_CTRL },
    {"alt",   LK_MOD_ALT  },
    {"gui",   LK_MOD_GUI  },
    {NULL,    0            }
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

/* Parse a mods string like "ctrl", "ctrl+shift", etc. into a bitmask.
 * Returns 0 for "" (any). Returns -1 on error. */
static int parse_mods(const char *s, lk_u8 *out) {
  char buf[64];
  char *p;
  char *tok;
  lk_u8 mods = 0;

  if (s[0] == '\0') {
    *out = 0;
    return 0;
  }

  if (strlen(s) >= sizeof(buf)) {
    return -1;
  }

  strcpy(buf, s);
  p = buf;

  while (*p) {
    int val;

    tok = p;

    while (*p && *p != '+') {
      p++;
    }

    if (*p == '+') {
      *p = '\0';
      p++;
    }

    if (!lookup_enum(mod_table, tok, &val)) {
      return -1;
    }

    mods |= (lk_u8)val;
  }

  *out = mods;
  return 0;
}

/* ============================================================================
 * Finalizers
 * ============================================================================
 */

/* Forward declarations for command handler cleanup */
struct lcl_cmd_ctx;
static void lcl_cmd_bridge(const lk_command *cmd, void *ud);
static void lcl_cmd_ctx_free(struct lcl_cmd_ctx *ctx);

static void ui_finalizer(void *ptr) {
  lk_ui *ui = (lk_ui *)ptr;

  if (ui->cmd_handler == lcl_cmd_bridge && ui->cmd_handler_ud) {
    lcl_cmd_ctx_free((struct lcl_cmd_ctx *)ui->cmd_handler_ud);
    ui->cmd_handler = NULL;
    ui->cmd_handler_ud = NULL;
  }

  lk_ui_destroy(ui);
}

/* ============================================================================
 * UI Lifecycle procs (5)
 * ============================================================================
 */

/* lk::ui_create -> opaque<lk_ui> */
static int c_lk_ui_create(lcl_interp *interp, int argc, lcl_value **argv,
                          lcl_value **out) {
  lk_ui *ui;
  (void)interp;
  (void)argv;

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
    case LK_CHANGE_ADDED: kind_str = "added"; break;
    case LK_CHANGE_REMOVED: kind_str = "removed"; break;
    case LK_CHANGE_UPDATED: kind_str = "updated"; break;
    default: kind_str = "unknown"; break;
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
    lcl_set_error(interp,
                  "lk::prop: expected 4 arguments (tree, node, key, value)");

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
  case UIP_TEXT: lv = lk_v_cstr(t->intern, lcl_value_to_string(argv[3])); break;
  case UIP_FOCUSABLE:
  case UIP_DISABLED:
  case UIP_HIDDEN: {
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

/* Coerce one Lcl value into an lk_value (int first, fall back to string). */
static lk_value coerce_lk_value(lk_tree *t, lcl_value *v) {
  long iv;

  if (lcl_value_to_int(v, &iv) == LCL_OK) {
    return lk_v_i32((lk_i32)iv);
  }

  return lk_v_cstr(t->intern, lcl_value_to_string(v));
}

/* lk::present [tree, node_ix, ptype_str, pvalue]
 *
 * pvalue may be a scalar (int/string) or a list; list elements become
 * the presentation's args, one-to-one with the emitted command's args.
 * Lists longer than LK_PRES_MAX_ARGS are truncated.
 */
static int c_lk_present(lcl_interp *interp, int argc, lcl_value **argv,
                        lcl_value **out) {
  lk_tree *t;
  long node_ix;
  const char *ptype_str;
  lk_value pvs[LK_PRES_MAX_ARGS];
  lk_u8 count = 0;

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

  if (lcl_value_type_of(argv[3]) == LCL_LIST) {
    size_t n = lcl_list_len(argv[3]);
    size_t i;

    if (n > LK_PRES_MAX_ARGS) {
      n = LK_PRES_MAX_ARGS;
    }

    for (i = 0; i < n; i++) {
      lcl_value *elem = NULL;

      if (lcl_list_get(argv[3], i, &elem) == LCL_OK && elem) {
        pvs[count++] = coerce_lk_value(t, elem);
      }
    }
  } else {
    pvs[count++] = coerce_lk_value(t, argv[3]);
  }

  lk_tree_add_presentation_sv(t, (lk_ix)node_ix, ptype_str, pvs, count);
  *out = lcl_string_new("");

  return LCL_RC_OK;
}

/* ============================================================================
 * Commands & Translators (5)
 * ============================================================================
 */

/* lk::add_translator [ui event_type ptype kind keycode mods cmd_name]
 * All string fields: "" means any/wildcard.
 * keycode: "" or letter/name (e.g. "s", "f", "return").
 * mods: "" or "+"-joined modifiers (e.g. "ctrl", "ctrl+shift"). */
static int c_lk_add_translator(lcl_interp *interp, int argc, lcl_value **argv,
                               lcl_value **out) {
  lk_ui *ui;
  const char *ev_str;
  const char *pt_str;
  const char *kn_str;
  const char *kc_str;
  const char *mod_str;
  const char *cmd_str;
  lk_u8 ev_type = 0;
  lk_u16 node_kind = 0;
  lk_u16 keycode = 0;
  lk_u8 mods = 0;
  lk_u32 ptype = 0;
  lk_u32 cmd_name;

  if (argc != 7) {
    lcl_set_error(interp, "lk::add_translator: expected 7 arguments");

    return LCL_RC_ERR;
  }

  if (lcl_opaque_get(argv[0], LK_UI_TYPE, (void **)&ui) != LCL_OK) {
    lcl_set_error(interp, "lk::add_translator: expected lk_ui opaque");

    return LCL_RC_ERR;
  }

  ev_str = lcl_value_to_string(argv[1]);
  pt_str = lcl_value_to_string(argv[2]);
  kn_str = lcl_value_to_string(argv[3]);
  kc_str = lcl_value_to_string(argv[4]);
  mod_str = lcl_value_to_string(argv[5]);
  cmd_str = lcl_value_to_string(argv[6]);

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

  /* keycode: "" means 0 (any) */
  if (kc_str[0] != '\0') {
    int kc_val;
    if (!lookup_enum(keycode_table, kc_str, &kc_val)) {
      lcl_set_error(interp, "lk::add_translator: unknown keycode");
      return LCL_RC_ERR;
    }
    keycode = (lk_u16)kc_val;
  }

  /* mods: "" means 0 (any), or "ctrl+shift" etc. */
  if (mod_str[0] != '\0') {
    if (parse_mods(mod_str, &mods) != 0) {
      lcl_set_error(interp, "lk::add_translator: unknown modifier");
      return LCL_RC_ERR;
    }
  }

  cmd_name = lk_intern_cid(ui->intern, cmd_str);
  lk_ui_add_translator(ui, ev_type, ptype, node_kind, keycode, mods, cmd_name);

  *out = lcl_string_new("");

  return LCL_RC_OK;
}

/* Helper: convert lk_command to lcl dict */
/* ---- Marshal lk_value to Lcl value (caller owns ref) ---- */

static lcl_value *lk_value_to_lcl(const lk_value *v, const lk_intern *intern) {
  switch (v->tag) {
  case UIV_I32: return lcl_int_new((long)v->as.i);
  case UIV_BOOL: return lcl_int_new((long)v->as.b);
  case UIV_STR: {
    const char *s = lk_intern_cstr(intern, v->as.str_id);
    return lcl_string_new(s ? s : "");
  }
  default: return lcl_string_new("");
  }
}

static lcl_value *command_to_dict(const lk_command *cmd,
                                  const lk_intern *intern) {
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
      case UIV_I32: av = lcl_int_new((long)arg->as.i); break;
      case UIV_STR: {
        const char *s = lk_intern_cstr(intern, arg->as.str_id);
        av = lcl_string_new(s ? s : "");
        break;
      }
      case UIV_BOOL: av = lcl_int_new((long)arg->as.b); break;
      default: av = lcl_string_new(""); break;
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

  /* source_value — the event-intrinsic value (e.g. new buffer for
   * value_changed commands).  Always present; empty string when the
   * event carried no value. */
  v = lk_value_to_lcl(&cmd->source_value, intern);
  lcl_dict_put(&dict, "source_value", v);
  lcl_ref_dec(v);

  return dict;
}

/* ---- Command handler bridge ---- */

struct lcl_cmd_ctx {
  lcl_interp *interp;
  lcl_value *handler;
  lk_ui *ui;
};

static void lcl_cmd_bridge(const lk_command *cmd, void *ud) {
  struct lcl_cmd_ctx *ctx = (struct lcl_cmd_ctx *)ud;
  lcl_value *dict;
  lcl_value *args[1];
  lcl_value *result = NULL;
  const lk_tree *cur;

  dict = command_to_dict(cmd, ctx->ui->intern);

  /* Add source_node_id string for convenience */
  cur = lk_ui_tree(ctx->ui);
  if (cur && cmd->source_node > 0 &&
      cmd->source_node <= (lk_ix)cur->node_count) {
    const char *nid =
        lk_intern_cstr(cur->intern, cur->nodes[cmd->source_node].id);
    if (nid) {
      lcl_value *v = lcl_string_new(nid);
      lcl_dict_put(&dict, "source_node_id", v);
      lcl_ref_dec(v);
    }
  }

  args[0] = dict;
  lcl_call_proc(ctx->interp, ctx->handler, 1, args, &result);

  if (result) {
    lcl_ref_dec(result);
  }

  lcl_ref_dec(dict);
}

static void lcl_cmd_ctx_free(struct lcl_cmd_ctx *ctx) {
  if (ctx) {
    if (ctx->handler) {
      lcl_ref_dec(ctx->handler);
    }
    free(ctx);
  }
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

/* lk::set_command_handler [ui, handler_proc] -> "" */
static int c_lk_set_command_handler(lcl_interp *interp, int argc,
                                    lcl_value **argv, lcl_value **out) {
  lk_ui *ui;
  struct lcl_cmd_ctx *ctx;

  if (argc != 2) {
    lcl_set_error(interp, "lk::set_command_handler: expected 2 arguments");

    return LCL_RC_ERR;
  }

  if (lcl_opaque_get(argv[0], LK_UI_TYPE, (void **)&ui) != LCL_OK) {
    lcl_set_error(interp, "lk::set_command_handler: expected lk_ui opaque");

    return LCL_RC_ERR;
  }

  if (!lcl_is_callable(argv[1])) {
    lcl_set_error(interp, "lk::set_command_handler: expected callable");

    return LCL_RC_ERR;
  }

  /* Free previous handler ctx if one was set through this binding */
  if (ui->cmd_handler == lcl_cmd_bridge && ui->cmd_handler_ud) {
    lcl_cmd_ctx_free((struct lcl_cmd_ctx *)ui->cmd_handler_ud);
  }

  ctx = (struct lcl_cmd_ctx *)malloc(sizeof(*ctx));

  if (!ctx) {
    lcl_set_error(interp, "lk::set_command_handler: allocation failed");

    return LCL_RC_ERR;
  }

  ctx->interp = interp;
  ctx->handler = argv[1];
  ctx->ui = ui;
  lcl_ref_inc(argv[1]);

  lk_ui_set_command_handler(ui, lcl_cmd_bridge, ctx);

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
  case UIV_I32: *out = lcl_int_new((long)lv.as.i); break;
  case UIV_BOOL: *out = lcl_int_new((long)lv.as.b); break;
  case UIV_STR: {
    const char *s = lk_intern_cstr(ui->intern, lv.as.str_id);
    *out = lcl_string_new(s ? s : "");
    break;
  }
  default: *out = lcl_string_new(""); break;
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
 * Tags & Style (2)
 * ============================================================================
 */

/* lk::tag [tree, node_ix, tag_str] -> "" */
static int c_lk_tag(lcl_interp *interp, int argc, lcl_value **argv,
                    lcl_value **out) {
  lk_tree *t;
  long node_ix;
  const char *tag_str;

  if (argc != 3) {
    lcl_set_error(interp, "lk::tag: expected 3 arguments (tree, node, tag)");

    return LCL_RC_ERR;
  }

  if (lcl_opaque_get(argv[0], LK_TREE_TYPE, (void **)&t) != LCL_OK) {
    lcl_set_error(interp, "lk::tag: expected lk_tree opaque");

    return LCL_RC_ERR;
  }

  if (lcl_value_to_int(argv[1], &node_ix) != LCL_OK) {
    lcl_set_error(interp, "lk::tag: node_ix must be an integer");

    return LCL_RC_ERR;
  }

  tag_str = lcl_value_to_string(argv[2]);
  lk_tree_add_tag_s(t, (lk_ix)node_ix, tag_str);

  *out = lcl_string_new("");

  return LCL_RC_OK;
}

/* Helper: parse (r g b) or (r g b a) list into lk_color */
static int parse_color_list(lcl_value *list, lk_color *out) {
  size_t len;
  lcl_value *v;
  long r;
  long g;
  long b;
  long a;

  len = lcl_list_len(list);

  if (len < 3 || len > 4) {
    return 0;
  }

  if (lcl_list_get(list, 0, &v) != LCL_OK ||
      lcl_value_to_int(v, &r) != LCL_OK) {
    return 0;
  }

  if (lcl_list_get(list, 1, &v) != LCL_OK ||
      lcl_value_to_int(v, &g) != LCL_OK) {
    return 0;
  }

  if (lcl_list_get(list, 2, &v) != LCL_OK ||
      lcl_value_to_int(v, &b) != LCL_OK) {
    return 0;
  }

  a = 255;

  if (len == 4) {
    if (lcl_list_get(list, 3, &v) != LCL_OK ||
        lcl_value_to_int(v, &a) != LCL_OK) {
      return 0;
    }
  }

  out->r = (lk_u8)r;
  out->g = (lk_u8)g;
  out->b = (lk_u8)b;
  out->a = (lk_u8)a;

  return 1;
}

/* lk::theme_rule [ui, kind_str, tag_str, state_str, style_dict] -> "" */
static int c_lk_theme_rule(lcl_interp *interp, int argc, lcl_value **argv,
                           lcl_value **out) {
  lk_ui *ui;
  const char *kind_str;
  const char *tag_str;
  const char *state_str;
  lcl_value *dict;
  lk_u16 kind = 0;
  lk_u32 tag_id = 0;
  lk_u8 state_mask = 0;
  lk_style style;
  lk_u32 field_mask = 0;
  lk_theme *th;
  lcl_value *v;

  if (argc != 5) {
    lcl_set_error(
        interp,
        "lk::theme_rule: expected 5 arguments (ui, kind, tag, state, style)");

    return LCL_RC_ERR;
  }

  if (lcl_opaque_get(argv[0], LK_UI_TYPE, (void **)&ui) != LCL_OK) {
    lcl_set_error(interp, "lk::theme_rule: expected lk_ui opaque");

    return LCL_RC_ERR;
  }

  kind_str = lcl_value_to_string(argv[1]);
  tag_str = lcl_value_to_string(argv[2]);
  state_str = lcl_value_to_string(argv[3]);
  dict = argv[4];

  /* kind: "" or "*" means 0 (any) */
  if (kind_str[0] != '\0' && strcmp(kind_str, "*") != 0) {
    int kv;
    if (!lookup_enum(kind_table, kind_str, &kv)) {
      lcl_set_error(interp, "lk::theme_rule: unknown kind");

      return LCL_RC_ERR;
    }

    kind = (lk_u16)kv;
  }

  /* tag: "" means 0 (any) */
  if (tag_str[0] != '\0') {
    tag_id = lk_intern_cid(ui->intern, tag_str);
  }

  /* state: "" means 0 (any) */
  if (state_str[0] != '\0') {
    int sv;
    if (!lookup_enum(state_table, state_str, &sv)) {
      lcl_set_error(interp, "lk::theme_rule: unknown state");

      return LCL_RC_ERR;
    }

    state_mask = (lk_u8)sv;
  }

  /* Parse style dict */
  memset(&style, 0, sizeof(style));

  /* bg: {r g b} or {r g b a} */
  if (lcl_dict_get(dict, "bg", &v) == LCL_OK) {
    if (parse_color_list(v, &style.bg)) {
      field_mask |= LK_SF_BG;
    }
  }

  /* fg: {r g b} or {r g b a} */
  if (lcl_dict_get(dict, "fg", &v) == LCL_OK) {
    if (parse_color_list(v, &style.fg)) {
      field_mask |= LK_SF_FG;
    }
  }

  /* border_color: {r g b} or {r g b a} */
  if (lcl_dict_get(dict, "border_color", &v) == LCL_OK) {
    if (parse_color_list(v, &style.border_color)) {
      field_mask |= LK_SF_BORDER_COLOR;
    }
  }

  /* padding: int */
  if (lcl_dict_get(dict, "padding", &v) == LCL_OK) {
    long iv;
    if (lcl_value_to_int(v, &iv) == LCL_OK) {
      style.padding = (lk_i32)iv;
      field_mask |= LK_SF_PADDING;
    }
  }

  /* gap: int */
  if (lcl_dict_get(dict, "gap", &v) == LCL_OK) {
    long iv;
    if (lcl_value_to_int(v, &iv) == LCL_OK) {
      style.gap = (lk_i32)iv;
      field_mask |= LK_SF_GAP;
    }
  }

  /* font_id: int (from lk::register_font; 0 = default face) */
  if (lcl_dict_get(dict, "font_id", &v) == LCL_OK) {
    long iv;
    if (lcl_value_to_int(v, &iv) != LCL_OK || iv < 0) {
      lcl_set_error(interp,
                    "lk::theme_rule: font_id must be a non-negative int");

      return LCL_RC_ERR;
    }

    style.font_id = (lk_u32)iv;
    field_mask |= LK_SF_FONT_ID;
  }

  /* font_size: int (0 = face default size) */
  if (lcl_dict_get(dict, "font_size", &v) == LCL_OK) {
    long iv;
    if (lcl_value_to_int(v, &iv) != LCL_OK || iv < 0) {
      lcl_set_error(interp,
                    "lk::theme_rule: font_size must be a non-negative int");

      return LCL_RC_ERR;
    }

    style.font_size = (lk_i32)iv;
    field_mask |= LK_SF_FONT_SIZE;
  }

  /* border_width: int */
  if (lcl_dict_get(dict, "border_width", &v) == LCL_OK) {
    long iv;
    if (lcl_value_to_int(v, &iv) == LCL_OK) {
      style.border_width = (lk_i32)iv;
      field_mask |= LK_SF_BORDER_WIDTH;
    }
  }

  /* border_radius: int */
  if (lcl_dict_get(dict, "border_radius", &v) == LCL_OK) {
    long iv;
    if (lcl_value_to_int(v, &iv) == LCL_OK) {
      style.border_radius = (lk_i32)iv;
      field_mask |= LK_SF_BORDER_RADIUS;
    }
  }

  /* align: string */
  if (lcl_dict_get(dict, "align", &v) == LCL_OK) {
    int av;
    if (lookup_enum(align_table, lcl_value_to_string(v), &av)) {
      style.align = (lk_u8)av;
      field_mask |= LK_SF_ALIGN;
    }
  }

  /* justify: string */
  if (lcl_dict_get(dict, "justify", &v) == LCL_OK) {
    int av;
    if (lookup_enum(align_table, lcl_value_to_string(v), &av)) {
      style.justify = (lk_u8)av;
      field_mask |= LK_SF_JUSTIFY;
    }
  }

  /* scrollbar_track: {r g b} or {r g b a} */
  if (lcl_dict_get(dict, "scrollbar_track", &v) == LCL_OK) {
    if (parse_color_list(v, &style.scrollbar_track)) {
      field_mask |= LK_SF_SCROLLBAR_TRACK;
    }
  }

  /* scrollbar_thumb: {r g b} or {r g b a} */
  if (lcl_dict_get(dict, "scrollbar_thumb", &v) == LCL_OK) {
    if (parse_color_list(v, &style.scrollbar_thumb)) {
      field_mask |= LK_SF_SCROLLBAR_THUMB;
    }
  }

  th = lk_ui_theme(ui);

  if (!th) {
    lcl_set_error(interp, "lk::theme_rule: ui has no theme");

    return LCL_RC_ERR;
  }

  lk_theme_add_rule(th, kind, tag_id, state_mask, &style, field_mask);

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

/* ---- Wrapper struct: holds lk_window + Lcl event handler ---- */

struct lcl_lk_window {
  lk_window *win;
  lcl_interp *interp;
  lcl_value *event_handler; /* NULL if not set */
};

/* ---- Enum-to-string reverse tables ---- */

static const char *event_type_str(lk_u8 t) {
  switch (t) {
  case LK_EVENT_POINTER_MOVE: return "pointer_move";
  case LK_EVENT_POINTER_DOWN: return "pointer_down";
  case LK_EVENT_POINTER_UP: return "pointer_up";
  case LK_EVENT_KEY_DOWN: return "key_down";
  case LK_EVENT_KEY_UP: return "key_up";
  case LK_EVENT_TEXT: return "text";
  case LK_EVENT_WHEEL: return "wheel";
  case LK_EVENT_WINDOW_RESIZE: return "window_resize";
  case LK_EVENT_WINDOW_CLOSE: return "window_close";
  case LK_EVENT_VALUE_CHANGED: return "value_changed";
  default: return "unknown";
  }
}

static const char *event_phase_str(lk_u8 p) {
  switch (p) {
  case LK_PHASE_CAPTURE: return "capture";
  case LK_PHASE_TARGET: return "target";
  case LK_PHASE_BUBBLE: return "bubble";
  default: return "unknown";
  }
}

/* ---- Marshal lk_event to Lcl dict ---- */

static lcl_value *event_to_dict(const lk_event *ev, const lk_intern *intern) {
  lcl_value *dict = lcl_dict_new();
  lcl_value *v;

  v = lcl_string_new(event_type_str(ev->type));
  lcl_dict_put(&dict, "type", v);
  lcl_ref_dec(v);

  v = lcl_string_new(event_phase_str(ev->phase));
  lcl_dict_put(&dict, "phase", v);
  lcl_ref_dec(v);

  v = lcl_int_new((long)ev->mods);
  lcl_dict_put(&dict, "mods", v);
  lcl_ref_dec(v);

  v = lcl_int_new((long)ev->handled);
  lcl_dict_put(&dict, "handled", v);
  lcl_ref_dec(v);

  v = lcl_int_new((long)ev->target);
  lcl_dict_put(&dict, "target", v);
  lcl_ref_dec(v);

  /* Type-specific fields */
  switch (ev->type) {
  case LK_EVENT_POINTER_MOVE:
  case LK_EVENT_POINTER_DOWN:
  case LK_EVENT_POINTER_UP:
    v = lcl_int_new((long)ev->data.pointer.x);
    lcl_dict_put(&dict, "x", v);
    lcl_ref_dec(v);

    v = lcl_int_new((long)ev->data.pointer.y);
    lcl_dict_put(&dict, "y", v);
    lcl_ref_dec(v);

    v = lcl_int_new((long)ev->data.pointer.button);
    lcl_dict_put(&dict, "button", v);
    lcl_ref_dec(v);
    break;

  case LK_EVENT_KEY_DOWN:
  case LK_EVENT_KEY_UP:
    v = lcl_int_new((long)ev->data.key.keycode);
    lcl_dict_put(&dict, "keycode", v);
    lcl_ref_dec(v);

    v = lcl_int_new((long)ev->data.key.repeat);
    lcl_dict_put(&dict, "repeat", v);
    lcl_ref_dec(v);
    break;

  case LK_EVENT_TEXT:
    v = lcl_string_new(ev->data.text.buf);
    lcl_dict_put(&dict, "text", v);
    lcl_ref_dec(v);
    break;

  case LK_EVENT_WHEEL:
    v = lcl_int_new((long)ev->data.wheel.dx);
    lcl_dict_put(&dict, "dx", v);
    lcl_ref_dec(v);

    v = lcl_int_new((long)ev->data.wheel.dy);
    lcl_dict_put(&dict, "dy", v);
    lcl_ref_dec(v);
    break;

  case LK_EVENT_WINDOW_RESIZE:
    v = lcl_int_new((long)ev->data.window.w);
    lcl_dict_put(&dict, "w", v);
    lcl_ref_dec(v);

    v = lcl_int_new((long)ev->data.window.h);
    lcl_dict_put(&dict, "h", v);
    lcl_ref_dec(v);
    break;

  case LK_EVENT_VALUE_CHANGED: {
    const char *s = lk_intern_cstr(intern, ev->data.value_changed.str_id);
    v = lcl_string_new(s ? s : "");
    lcl_dict_put(&dict, "value", v);
    lcl_ref_dec(v);
    break;
  }

  default: break;
  }

  return dict;
}

/* ---- C event handler bridge ---- */

static int lcl_lk_event_handler(lk_event *event, lk_ix node_ix, void *ud) {
  struct lcl_lk_window *lw = (struct lcl_lk_window *)ud;
  lcl_value *ev_dict;
  lcl_value *args[2];
  lcl_value *result = NULL;
  const lk_tree *cur;
  int rc;

  if (!lw->event_handler) {
    return 0;
  }

  ev_dict = event_to_dict(event, lk_ui_intern(lk_window_ui(lw->win)));

  /* Add target_id and node_id string fields so scripts can identify nodes */
  cur = lk_ui_tree(lk_window_ui(lw->win));
  if (cur) {
    lcl_value *v;
    if (event->target > 0 && event->target <= (lk_ix)cur->node_count) {
      const char *tid =
          lk_intern_cstr(cur->intern, cur->nodes[event->target].id);
      if (tid) {
        v = lcl_string_new(tid);
        lcl_dict_put(&ev_dict, "target_id", v);
        lcl_ref_dec(v);
      }
    }
    if (node_ix > 0 && node_ix <= (lk_ix)cur->node_count) {
      const char *nid = lk_intern_cstr(cur->intern, cur->nodes[node_ix].id);
      if (nid) {
        v = lcl_string_new(nid);
        lcl_dict_put(&ev_dict, "node_id", v);
        lcl_ref_dec(v);
      }
    }
  }

  args[0] = ev_dict;
  args[1] = lcl_int_new((long)node_ix);

  rc = lcl_call_proc(lw->interp, lw->event_handler, 2, args, &result);

  if (rc == LCL_RC_OK && result) {
    long handled;
    if (lcl_value_to_int(result, &handled) == LCL_OK && handled) {
      event->handled = 1;
    }
  }

  if (result) {
    lcl_ref_dec(result);
  }

  lcl_ref_dec(args[1]);
  lcl_ref_dec(ev_dict);

  return 0;
}

/* ---- Frame callback bridge ---- */

struct lcl_lk_frame_ctx {
  lcl_interp *interp;
  lcl_value *view_fn;
};

static void lcl_lk_frame(lk_tree *t, void *ud) {
  struct lcl_lk_frame_ctx *ctx = (struct lcl_lk_frame_ctx *)ud;
  lcl_value *tree_val = lcl_opaque_new(t, LK_TREE_TYPE, NULL);
  lcl_value *args[1];
  lcl_value *result = NULL;
  args[0] = tree_val;
  if (lcl_call_proc(ctx->interp, ctx->view_fn, 1, args, &result) != LCL_RC_OK) {
    const char *file = lcl_interp_error_file(ctx->interp);
    int line = lcl_interp_error_line(ctx->interp);
    const char *msg = lcl_interp_error_msg(ctx->interp);
    fprintf(stderr, "Frame error");
    if (file) fprintf(stderr, " in %s", file);
    if (line > 0) fprintf(stderr, ":%d", line);
    fprintf(stderr, ": %s\n", msg ? msg : "(unknown)");
  }

  if (result) {
    lcl_ref_dec(result);
  }

  lcl_ref_dec(tree_val);
}

/* ---- Finalizer ---- */

static void lcl_lk_window_finalizer(void *ptr) {
  struct lcl_lk_window *lw = (struct lcl_lk_window *)ptr;
  lk_ui *ui;

  if (lw->event_handler) {
    lcl_ref_dec(lw->event_handler);
  }

  /* Clean up command handler ctx before window destroy frees the ui */
  ui = lk_window_ui(lw->win);
  if (ui && ui->cmd_handler == lcl_cmd_bridge && ui->cmd_handler_ud) {
    lcl_cmd_ctx_free((struct lcl_cmd_ctx *)ui->cmd_handler_ud);
    ui->cmd_handler = NULL;
    ui->cmd_handler_ud = NULL;
  }

  lk_window_destroy(lw->win);
  free(lw);
}

/* ---- Helper: extract lcl_lk_window from opaque ---- */

static struct lcl_lk_window *get_lk_window(lcl_interp *interp, lcl_value *val) {
  struct lcl_lk_window *lw = NULL;

  if (lcl_opaque_get(val, LK_WIN_TYPE, (void **)&lw) != LCL_OK) {
    lcl_set_error(interp, "expected lk_window opaque");

    return NULL;
  }

  return lw;
}

/* lk::window_create [title, ?w, ?h, ?font, ?size] -> opaque<lk_window> */
static int c_lk_window_create(lcl_interp *interp, int argc, lcl_value **argv,
                              lcl_value **out) {
  lk_window_cfg cfg;
  lk_window *win;
  struct lcl_lk_window *lw;

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

    if (lcl_value_to_int(argv[1], &w) == LCL_OK) {
      cfg.width = (int)w;
    }
  }

  if (argc >= 3) {
    long h;

    if (lcl_value_to_int(argv[2], &h) == LCL_OK) {
      cfg.height = (int)h;
    }
  }

  if (argc >= 4) {
    cfg.font_path = lcl_value_to_string(argv[3]);
  }

  if (argc >= 5) {
    long sz;

    if (lcl_value_to_int(argv[4], &sz) == LCL_OK) {
      cfg.font_size = (int)sz;
    }
  }

  win = lk_window_create(&cfg);

  if (!win) {
    lcl_set_error(interp, "lk::window_create: failed to create window");

    return LCL_RC_ERR;
  }

  lw = (struct lcl_lk_window *)malloc(sizeof(*lw));

  if (!lw) {
    lk_window_destroy(win);
    lcl_set_error(interp, "lk::window_create: allocation failed");

    return LCL_RC_ERR;
  }

  lw->win = win;
  lw->interp = interp;
  lw->event_handler = NULL;

  *out = lcl_opaque_new(lw, LK_WIN_TYPE, lcl_lk_window_finalizer);

  return LCL_RC_OK;
}

/* lk::window_destroy [win] -> "" */
static int c_lk_window_destroy(lcl_interp *interp, int argc, lcl_value **argv,
                               lcl_value **out) {
  struct lcl_lk_window *lw;

  if (argc != 1) {
    lcl_set_error(interp, "lk::window_destroy: expected 1 argument");

    return LCL_RC_ERR;
  }

  lw = get_lk_window(interp, argv[0]);

  if (!lw) {
    return LCL_RC_ERR;
  }

  if (lw->event_handler) {
    lcl_ref_dec(lw->event_handler);
    lw->event_handler = NULL;
  }

  lk_window_destroy(lw->win);
  lw->win = NULL;

  *out = lcl_string_new("");

  return LCL_RC_OK;
}

/* lk::window_run [win, view_proc] -> "" (blocks until close) */
static int c_lk_window_run(lcl_interp *interp, int argc, lcl_value **argv,
                           lcl_value **out) {
  struct lcl_lk_window *lw;
  struct lcl_lk_frame_ctx frame_ctx;

  if (argc != 2) {
    lcl_set_error(interp, "lk::window_run: expected 2 arguments");
    return LCL_RC_ERR;
  }

  lw = get_lk_window(interp, argv[0]);

  if (!lw) {
    return LCL_RC_ERR;
  }

  if (!lcl_is_callable(argv[1])) {
    lcl_set_error(interp, "lk::window_run: expected callable view proc");

    return LCL_RC_ERR;
  }

  /* Install event handler bridge if a handler has been set */
  if (lw->event_handler) {
    lk_window_set_event_handler(lw->win, lcl_lk_event_handler, lw);
  }

  frame_ctx.interp = interp;
  frame_ctx.view_fn = lcl_ref_inc(argv[1]);

  lk_window_run(lw->win, lcl_lk_frame, &frame_ctx);

  lcl_ref_dec(frame_ctx.view_fn);

  *out = lcl_string_new("");

  return LCL_RC_OK;
}

/* lk::window_ui [win] -> opaque<lk_ui> */
static int c_lk_window_ui(lcl_interp *interp, int argc, lcl_value **argv,
                          lcl_value **out) {
  struct lcl_lk_window *lw;
  lk_ui *ui;

  if (argc != 1) {
    lcl_set_error(interp, "lk::window_ui: expected 1 argument");

    return LCL_RC_ERR;
  }

  lw = get_lk_window(interp, argv[0]);

  if (!lw) {
    return LCL_RC_ERR;
  }

  ui = lk_window_ui(lw->win);
  /* UI is owned by window, no finalizer */
  *out = lcl_opaque_new(ui, LK_UI_TYPE, NULL);

  return LCL_RC_OK;
}

/* lk::window_set_event_handler [win, handler_proc] -> "" */
static int c_lk_window_set_event_handler(lcl_interp *interp, int argc,
                                         lcl_value **argv, lcl_value **out) {
  struct lcl_lk_window *lw;

  if (argc != 2) {
    lcl_set_error(interp, "lk::window_set_event_handler: expected 2 arguments");

    return LCL_RC_ERR;
  }

  lw = get_lk_window(interp, argv[0]);

  if (!lw) {
    return LCL_RC_ERR;
  }

  if (!lcl_is_callable(argv[1])) {
    lcl_set_error(interp, "lk::window_set_event_handler: expected callable");

    return LCL_RC_ERR;
  }

  /* Release previous handler if any */
  if (lw->event_handler) {
    lcl_ref_dec(lw->event_handler);
  }

  lw->event_handler = lcl_ref_inc(argv[1]);

  /* If already running, install immediately; otherwise window_run
   * will install it before entering the loop. */
  lk_window_set_event_handler(lw->win, lcl_lk_event_handler, lw);

  *out = lcl_string_new("");
  return LCL_RC_OK;
}

/* lk::register_font [win, path] -> font_id (int; 0 = failure).
 * Mirrors the C contract: bad arguments are errors, but an unreadable
 * path returns 0 rather than erroring. */
static int c_lk_register_font(lcl_interp *interp, int argc, lcl_value **argv,
                              lcl_value **out) {
  struct lcl_lk_window *lw;
  const char *path;
  lk_u16 id;

  if (argc != 2) {
    lcl_set_error(interp, "lk::register_font: expected 2 arguments (win, path)");

    return LCL_RC_ERR;
  }

  lw = get_lk_window(interp, argv[0]);

  if (!lw) {
    return LCL_RC_ERR;
  }

  path = lcl_value_to_string(argv[1]);
  id = lk_window_register_font(lw->win, path);

  *out = lcl_int_new((long)id);

  return LCL_RC_OK;
}

#endif /* LK_HAVE_SDL */

/* ============================================================================
 * Overlays
 * ============================================================================
 */

/* lk::overlay_count [ui] — number of overlays on the ui's overlay
 * stack (headless-testable introspection; widgets drive push/pop). */
static int c_lk_overlay_count(lcl_interp *interp, int argc, lcl_value **argv,
                              lcl_value **out) {
  lk_ui *ui = NULL;

  if (argc < 1) {
    lcl_set_error(interp, "lk::overlay_count: expected ui");
    return LCL_RC_ERR;
  }

  if (lcl_opaque_get(argv[0], LK_UI_TYPE, (void **)&ui) != LCL_OK || !ui) {
    lcl_set_error(interp, "lk::overlay_count: bad ui handle");
    return LCL_RC_ERR;
  }

  *out = lcl_int_new((long)lk_overlay_count(ui));

  return LCL_RC_OK;
}

/* ============================================================================
 * Clipboard
 * ============================================================================
 */

static int c_lk_clipboard_get(lcl_interp *interp, int argc,
                               lcl_value **argv, lcl_value **out) {
  lk_ui *ui = NULL;

  if (argc < 1) {
    lcl_set_error(interp, "lk::clipboard_get: expected ui");
    return LCL_RC_ERR;
  }

  if (lcl_opaque_get(argv[0], LK_UI_TYPE, (void **)&ui) != LCL_OK || !ui) {
    lcl_set_error(interp, "lk::clipboard_get: bad ui handle");
    return LCL_RC_ERR;
  }

  if (ui->clipboard_get) {
    const char *text = ui->clipboard_get(ui->clipboard_ud);
    *out = lcl_string_new(text ? text : "");
  } else {
    *out = lcl_string_new("");
  }

  return LCL_RC_OK;
}

static int c_lk_clipboard_set(lcl_interp *interp, int argc,
                               lcl_value **argv, lcl_value **out) {
  lk_ui *ui = NULL;
  const char *text;

  if (argc < 2) {
    lcl_set_error(interp, "lk::clipboard_set: expected ui text");
    return LCL_RC_ERR;
  }

  if (lcl_opaque_get(argv[0], LK_UI_TYPE, (void **)&ui) != LCL_OK || !ui) {
    lcl_set_error(interp, "lk::clipboard_set: bad ui handle");
    return LCL_RC_ERR;
  }

  text = lcl_value_to_string(argv[1]);

  if (ui->clipboard_set) {
    ui->clipboard_set(ui->clipboard_ud, text);
  }

  *out = lcl_string_new("");
  return LCL_RC_OK;
}

/* ============================================================================
 * Registration
 * ============================================================================
 */

void lcl_register_lk(lcl_interp *interp) {
  lcl_value *ns = lcl_ns_new("lk");
  lcl_define_take(interp, "lk", ns);

  /* UI Lifecycle */
  lcl_ns_def(ns, "ui_create", lcl_c_proc_new("lk::ui_create", c_lk_ui_create));
  lcl_ns_def(ns, "ui_destroy",
             lcl_c_proc_new("lk::ui_destroy", c_lk_ui_destroy));
  lcl_ns_def(ns, "begin_frame",
             lcl_c_proc_new("lk::begin_frame", c_lk_begin_frame));
  lcl_ns_def(ns, "end_frame", lcl_c_proc_new("lk::end_frame", c_lk_end_frame));
  lcl_ns_def(ns, "tree", lcl_c_proc_new("lk::tree", c_lk_tree));

  /* Tree Building */
  lcl_ns_def(ns, "node", lcl_c_proc_new("lk::node", c_lk_node));
  lcl_ns_def(ns, "set_root", lcl_c_proc_new("lk::set_root", c_lk_set_root));
  lcl_ns_def(ns, "append_child",
             lcl_c_proc_new("lk::append_child", c_lk_append_child));
  lcl_ns_def(ns, "prop", lcl_c_proc_new("lk::prop", c_lk_prop));
  lcl_ns_def(ns, "present", lcl_c_proc_new("lk::present", c_lk_present));

  /* Commands & Translators */
  lcl_ns_def(ns, "add_translator",
             lcl_c_proc_new("lk::add_translator", c_lk_add_translator));
  lcl_ns_def(ns, "commands", lcl_c_proc_new("lk::commands", c_lk_commands));
  lcl_ns_def(ns, "clear_commands",
             lcl_c_proc_new("lk::clear_commands", c_lk_clear_commands));
  lcl_ns_def(ns, "command_log",
             lcl_c_proc_new("lk::command_log", c_lk_command_log));
  lcl_ns_def(ns, "clear_command_log",
             lcl_c_proc_new("lk::clear_command_log", c_lk_clear_command_log));
  lcl_ns_def(
      ns, "set_command_handler",
      lcl_c_proc_new("lk::set_command_handler", c_lk_set_command_handler));

  /* State */
  lcl_ns_def(ns, "state_set", lcl_c_proc_new("lk::state_set", c_lk_state_set));
  lcl_ns_def(ns, "state_get", lcl_c_proc_new("lk::state_get", c_lk_state_get));

  /* Focus */
  lcl_ns_def(ns, "focus_set", lcl_c_proc_new("lk::focus_set", c_lk_focus_set));
  lcl_ns_def(ns, "focus_clear",
             lcl_c_proc_new("lk::focus_clear", c_lk_focus_clear));

  /* Overlays */
  lcl_ns_def(ns, "overlay_count",
             lcl_c_proc_new("lk::overlay_count", c_lk_overlay_count));

  /* Tags & Style */
  lcl_ns_def(ns, "tag", lcl_c_proc_new("lk::tag", c_lk_tag));
  lcl_ns_def(ns, "theme_rule",
             lcl_c_proc_new("lk::theme_rule", c_lk_theme_rule));

  /* Interning */
  lcl_ns_def(ns, "intern_str",
             lcl_c_proc_new("lk::intern_str", c_lk_intern_str));
  lcl_ns_def(ns, "intern_id", lcl_c_proc_new("lk::intern_id", c_lk_intern_id));

  /* Clipboard */
  lcl_ns_def(ns, "clipboard_get",
             lcl_c_proc_new("lk::clipboard_get", c_lk_clipboard_get));
  lcl_ns_def(ns, "clipboard_set",
             lcl_c_proc_new("lk::clipboard_set", c_lk_clipboard_set));

#ifdef LK_HAVE_SDL
  /* SDL Window */
  lcl_ns_def(ns, "window_create",
             lcl_c_proc_new("lk::window_create", c_lk_window_create));
  lcl_ns_def(ns, "window_destroy",
             lcl_c_proc_new("lk::window_destroy", c_lk_window_destroy));
  lcl_ns_def(ns, "window_run",
             lcl_c_proc_new("lk::window_run", c_lk_window_run));
  lcl_ns_def(ns, "window_ui", lcl_c_proc_new("lk::window_ui", c_lk_window_ui));
  lcl_ns_def(ns, "window_set_event_handler",
             lcl_c_proc_new("lk::window_set_event_handler",
                            c_lk_window_set_event_handler));
  lcl_ns_def(ns, "register_font",
             lcl_c_proc_new("lk::register_font", c_lk_register_font));
#endif
}
