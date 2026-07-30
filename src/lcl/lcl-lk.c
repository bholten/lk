/*
 * lcl-lk.c — Lcl scripting bindings for lk (Layer 1).
 *
 * Exposes 76 procs in the "lk" namespace (70 core + 6 SDL) for
 * building UI trees, managing frames, commands, translators, state,
 * focus, overlays, interning, windows/fonts, the editor track
 * (documents, edit histories, editors, annotation stores), and range
 * presentations (weft-surface S1: annot_present, annot_layer_priority,
 * editor_presentations, editor_pos_at).  SDL procs (window_* and
 * register_font) are conditionally compiled when LK_HAVE_SDL is set.
 *
 * C89 (matches lk + lcl).
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "lcl-lk.h"
#include <lcl.h>
#include <lk-annot-store.h>
#include <lk-editor.h>
#include <lk.h>

/* ============================================================================
 * Opaque type tags
 * ============================================================================
 */

#define LK_UI_TYPE "lk_ui"
#define LK_TREE_TYPE "lk_tree"
#define LK_DOC_TYPE "lk_document"
#define LK_HIST_TYPE "lk_edit_history"
#define LK_EDITOR_TYPE "lk_editor"
#define LK_ANNOT_TYPE "lk_annot_store"

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
    {"split_h",    UIK_SPLIT_H   },
    {"split_v",    UIK_SPLIT_V   },
    {"editor",     UIK_EDITOR    },
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
    {"hidden",      UIP_HIDDEN     },
    {"tooltip",     UIP_TOOLTIP    },
    {"split_ratio", UIP_SPLIT_RATIO},
    {"editor",      UIP_EDITOR     },
    {"grow",        UIP_GROW       },
    {"value",       UIP_VALUE      },
    {NULL,          0              }
};

/* Editor command names, mirroring lk_editor_cmd_id (LK_ED_* with the
 * prefix stripped and lowercased).  Kept in enum order so the joined
 * known-commands list reads naturally in error messages. */
static const str_enum ed_cmd_table[] = {
    {"insert_text",          LK_ED_INSERT_TEXT         },
    {"delete_backward",      LK_ED_DELETE_BACKWARD     },
    {"delete_forward",       LK_ED_DELETE_FORWARD      },
    {"delete_word_backward", LK_ED_DELETE_WORD_BACKWARD},
    {"delete_word_forward",  LK_ED_DELETE_WORD_FORWARD },
    {"move_left",            LK_ED_MOVE_LEFT           },
    {"move_right",           LK_ED_MOVE_RIGHT          },
    {"move_up",              LK_ED_MOVE_UP             },
    {"move_down",            LK_ED_MOVE_DOWN           },
    {"move_word_left",       LK_ED_MOVE_WORD_LEFT      },
    {"move_word_right",      LK_ED_MOVE_WORD_RIGHT     },
    {"move_line_start",      LK_ED_MOVE_LINE_START     },
    {"move_line_end",        LK_ED_MOVE_LINE_END       },
    {"move_doc_start",       LK_ED_MOVE_DOC_START      },
    {"move_doc_end",         LK_ED_MOVE_DOC_END        },
    {"move_page_up",         LK_ED_MOVE_PAGE_UP        },
    {"move_page_down",       LK_ED_MOVE_PAGE_DOWN      },
    {"select_all",           LK_ED_SELECT_ALL          },
    {"cut",                  LK_ED_CUT                 },
    {"copy",                 LK_ED_COPY                },
    {"paste",                LK_ED_PASTE               },
    {"undo",                 LK_ED_UNDO                },
    {"redo",                 LK_ED_REDO                },
    {"set_cursor",           LK_ED_SET_CURSOR          },
    {"scroll_lines",         LK_ED_SCROLL_LINES        },
    {"move_row_start",       LK_ED_MOVE_ROW_START      },
    {"move_row_end",         LK_ED_MOVE_ROW_END        },
    {NULL,                   0                         }
};

/* Wrap-mode names, mirroring lk_editor_wrap_mode.  "word" is a valid
 * name but the engine rejects it until word wrap is implemented. */
static const str_enum wrap_mode_table[] = {
    {"none",      LK_EDITOR_WRAP_NONE     },
    {"character", LK_EDITOR_WRAP_CHARACTER},
    {"word",      LK_EDITOR_WRAP_WORD     },
    {NULL,        0                       }
};

static const str_enum overlay_kind_table[] = {
    {"dropdown_popup", LK_OVERLAY_DROPDOWN_POPUP},
    {"tooltip",        LK_OVERLAY_TOOLTIP       },
    {"context_menu",   LK_OVERLAY_CONTEXT_MENU  },
    {"modal",          LK_OVERLAY_MODAL         },
    {NULL,             0                        }
};

static const str_enum anchor_table[] = {
    {"below",     LK_ANCHOR_BELOW          },
    {"above",     LK_ANCHOR_ABOVE          },
    {"at_cursor", LK_ANCHOR_AT_CURSOR      },
    {"center",    LK_ANCHOR_CENTER_VIEWPORT},
    {NULL,        0                        }
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
    {"focus_changed", LK_EVENT_FOCUS_CHANGED},
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

static const str_enum button_table[] = {
    {"primary",   LK_POINTER_BUTTON_PRIMARY  },
    {"middle",    LK_POINTER_BUTTON_MIDDLE   },
    {"secondary", LK_POINTER_BUTTON_SECONDARY},
    {NULL,        0                          }
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
 * Editor-track wrappers (documents, histories, editors, annot stores)
 *
 * Lifetime scheme: dependents retain their dependencies' Lcl values.
 * A history/editor/annot-store wrapper holds an lcl_ref_inc'd
 * reference to the document value it uses (and the editor to the ui
 * and history values too), so the referenced opaque can never be
 * finalized while a dependent is alive — GC order becomes refcount
 * order, and the C contract "destroy dependents before the document"
 * holds no matter when the script drops its own handles.  Dependent
 * finalizers destroy their own C object (which unsubscribes) and only
 * then release the retained values.
 * ============================================================================
 */

/* One script-side document subscription (lk::doc_subscribe). */
struct lcl_doc_sub {
  lk_u32 id;
  lcl_interp *interp;
  lcl_value *handler; /* retained */
  struct lcl_doc_sub *next;
};

struct lcl_lk_doc {
  lk_document *doc;
  struct lcl_doc_sub *subs; /* live lk::doc_subscribe bridges */
};

struct lcl_lk_history {
  lk_edit_history *hist;
  lk_document *doc;   /* attached document, NULL until attached */
  lcl_value *doc_val; /* retained once attached */
};

struct lcl_lk_editor {
  lk_editor *ed;
  lk_ui *ui;           /* for lk_editor_command + resource release */
  lk_resource_ref ref; /* registration in the ui's resource table */
  lcl_value *ui_val;   /* retained */
  lcl_value *doc_val;  /* retained */
  lcl_value *hist_val; /* retained, NULL when no history was given */
  lcl_value *annot_val; /* retained annot-store value while its
                           presentation source is installed
                           (lk::editor_presentations), NULL before */
};

struct lcl_lk_annot {
  lk_annot_store *store;
  lcl_value *doc_val; /* retained once attached, NULL before */
  lk_ui *pres_ui;     /* ui whose resource table holds this store's
                         presentation values; set by the first
                         lk::annot_present, NULL before */
  lcl_value *ui_val;  /* retained alongside pres_ui */
};

/* ---- "lcl-value" resources ----
 *
 * lk::annot_present wraps ANY Lcl value as a presentation value: the
 * value is retained in a box registered in the ui's resource table
 * under this type, and the annotation carries the UIV_RESOURCE ref.
 * command_to_dict unwraps refs of this type back to the retained Lcl
 * value, so handlers receive dicts/closures/whatever intact.  The
 * store's release hook releases the registration + retain when the
 * presentation detaches. */

static const lk_resource_type g_lcl_value_type = {"lcl-value", NULL};

struct lcl_pres_box {
  lcl_value *val; /* retained */
};

/* ---- Live-ui registry ----
 *
 * Value retention cannot cover one case: a ui destroyed explicitly
 * (lk::ui_destroy, lk::window_destroy, or the window finalizer for
 * window-owned uis) while an editor value still exists.  The editor
 * finalizer must then skip lk_resource_release (the table died with
 * the ui).  This registry tracks every lk_ui* the bindings handed
 * out ownership-wise; editors consult it before touching their ui. */

static lk_ui **g_live_uis = NULL;
static int g_live_ui_count = 0;
static int g_live_ui_cap = 0;

static void live_ui_add(lk_ui *ui) {
  if (!ui) {
    return;
  }

  if (g_live_ui_count == g_live_ui_cap) {
    int cap = g_live_ui_cap ? g_live_ui_cap * 2 : 8;
    lk_ui **grown = (lk_ui **)realloc(g_live_uis, sizeof(*grown) * cap);

    if (!grown) {
      return; /* untracked ui: editors will conservatively skip release */
    }

    g_live_uis = grown;
    g_live_ui_cap = cap;
  }

  g_live_uis[g_live_ui_count++] = ui;
}

static void live_ui_remove(lk_ui *ui) {
  int i;

  for (i = 0; i < g_live_ui_count; i++) {
    if (g_live_uis[i] == ui) {
      g_live_uis[i] = g_live_uis[--g_live_ui_count];

      return;
    }
  }
}

static int live_ui_check(const lk_ui *ui) {
  int i;

  for (i = 0; i < g_live_ui_count; i++) {
    if (g_live_uis[i] == ui) {
      return 1;
    }
  }

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

  live_ui_remove(ui);
  lk_ui_destroy(ui);
}

static void doc_finalizer(void *ptr) {
  struct lcl_lk_doc *dw = (struct lcl_lk_doc *)ptr;
  struct lcl_doc_sub *s = dw->subs;

  /* No dependent can still be subscribed here (they retain the doc
   * value), so only script subscriptions remain; the listener slots
   * die with the document. */
  while (s) {
    struct lcl_doc_sub *next = s->next;

    lcl_ref_dec(s->handler);
    free(s);
    s = next;
  }

  lk_doc_destroy(dw->doc);
  free(dw);
}

static void history_finalizer(void *ptr) {
  struct lcl_lk_history *hw = (struct lcl_lk_history *)ptr;

  /* Destroy unsubscribes; the doc is still alive because we retain
   * its value.  Release the retained value only afterwards. */
  lk_history_destroy(hw->hist);

  if (hw->doc_val) {
    lcl_ref_dec(hw->doc_val);
  }

  free(hw);
}

static void editor_finalizer(void *ptr) {
  struct lcl_lk_editor *ew = (struct lcl_lk_editor *)ptr;

  if (live_ui_check(ew->ui)) {
    lk_resource_release(lk_ui_resources(ew->ui), ew->ref);
  }

  lk_editor_destroy(ew->ed); /* unsubscribes from the (still live) doc */

  if (ew->hist_val) {
    lcl_ref_dec(ew->hist_val);
  }

  if (ew->annot_val) {
    lcl_ref_dec(ew->annot_val);
  }

  lcl_ref_dec(ew->doc_val);
  lcl_ref_dec(ew->ui_val);
  free(ew);
}

static void annot_finalizer(void *ptr) {
  struct lcl_lk_annot *aw = (struct lcl_lk_annot *)ptr;

  /* Destroy first: the release hook fires for every live
   * presentation while aw->pres_ui is still retained/checkable. */
  lk_annot_store_destroy(aw->store); /* unsubscribes if attached */

  if (aw->doc_val) {
    lcl_ref_dec(aw->doc_val);
  }

  if (aw->ui_val) {
    lcl_ref_dec(aw->ui_val);
  }

  free(aw);
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

  live_ui_add(ui);
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

  live_ui_remove(ui);
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
  case UIP_TEXT:
  case UIP_TOOLTIP:
  case UIP_VALUE:
    lv = lk_v_cstr(t->intern, lcl_value_to_string(argv[3]));
    break;
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
  case UIP_GAP:
  case UIP_SPLIT_RATIO: {
    long i;
    if (lcl_value_to_int(argv[3], &i) != LCL_OK) {
      lcl_set_error(interp, "lk::prop: numeric prop expects integer");
      return LCL_RC_ERR;
    }
    lv = lk_v_i32((lk_i32)i);
    break;
  }
  case UIP_GROW: {
    long i;
    if (lcl_value_to_int(argv[3], &i) != LCL_OK) {
      lcl_set_error(interp, "lk::prop: grow expects an integer >= 0");
      return LCL_RC_ERR;
    }
    if (i < 0) {
      lcl_set_error(interp, "lk::prop: grow must be >= 0 (weighted growth "
                            "has no negative weights)");
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
  case UIP_EDITOR: {
    struct lcl_lk_editor *ew = NULL;

    if (lcl_opaque_get(argv[3], LK_EDITOR_TYPE, (void **)&ew) != LCL_OK) {
      lcl_set_error(interp, "lk::prop: editor prop expects an lk_editor opaque");
      return LCL_RC_ERR;
    }

    lv = lk_v_resource(ew->ref);
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

/* lk::add_translator [ui event_type ptype kind keycode mods cmd_name
 *                     ?button?]
 * All string fields: "" means any/wildcard.
 * keycode: "" or letter/name (e.g. "s", "f", "return").
 * mods: "" or "+"-joined modifiers (e.g. "ctrl", "ctrl+shift").
 * button (optional 8th arg): "primary" | "middle" | "secondary", or
 * "" / "0" for any.  When set, only pointer events with that button
 * and exact mods match. */
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
  lk_u8 button = 0;
  lk_u32 ptype = 0;
  lk_u32 cmd_name;

  if (argc != 7 && argc != 8) {
    lcl_set_error(interp, "lk::add_translator: expected 7 or 8 arguments");

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

  /* button: absent, "" or "0" means 0 (any) */
  if (argc == 8) {
    const char *btn_str = lcl_value_to_string(argv[7]);

    if (btn_str[0] != '\0' && strcmp(btn_str, "0") != 0) {
      int btn_val;

      if (!lookup_enum(button_table, btn_str, &btn_val)) {
        lcl_set_error(interp,
                      "lk::add_translator: unknown button (known: primary, "
                      "middle, secondary)");
        return LCL_RC_ERR;
      }

      button = (lk_u8)btn_val;
    }
  }

  cmd_name = lk_intern_cid(ui->intern, cmd_str);
  lk_ui_add_translator(ui, ev_type, ptype, node_kind, keycode, mods, button,
                       cmd_name);

  *out = lcl_string_new("");

  return LCL_RC_OK;
}

/* Helper: convert lk_command to lcl dict */
/* ---- Marshal lk_value to Lcl value (caller owns ref) ---- */

/* Context-free marshal: UIV_TEXT and non-lcl-value resources become
 * placeholders (command args go through cmd_value_to_lcl below, which
 * has the ui for arena + resource-table access). */
static lcl_value *lk_value_to_lcl(const lk_value *v, const lk_intern *intern) {
  switch (v->tag) {
  case UIV_I32: return lcl_int_new((long)v->as.i);
  case UIV_BOOL: return lcl_int_new((long)v->as.b);
  case UIV_STR: {
    const char *s = lk_intern_cstr(intern, v->as.str_id);
    return lcl_string_new(s ? s : "");
  }
  case UIV_RESOURCE: return lcl_string_new("<resource>");
  case UIV_TEXT: return lcl_string_new("<text>");
  default: return lcl_string_new("");
  }
}

/* Command-scope marshal: UIV_TEXT resolves through the right arena
 * (queue or log copy); UIV_RESOURCE refs of the lcl-value type unwrap
 * to the retained Lcl value itself. */
static lcl_value *cmd_value_to_lcl(lk_ui *ui, const lk_command *cmd,
                                   const lk_value *v) {
  switch (v->tag) {
  case UIV_TEXT: {
    lk_str s = lk_command_text(ui, cmd, *v);
    char *buf;
    lcl_value *r;

    if (!s.ptr) {
      return lcl_string_new("");
    }

    buf = (char *)malloc((size_t)s.len + 1);

    if (!buf) {
      return lcl_string_new("");
    }

    memcpy(buf, s.ptr, s.len);
    buf[s.len] = '\0';
    r = lcl_string_new(buf);
    free(buf);

    return r;
  }
  case UIV_RESOURCE: {
    struct lcl_pres_box *box = (struct lcl_pres_box *)lk_resource_get(
        ui->resources, lk_v_resource_ref(*v), &g_lcl_value_type);

    if (box && box->val) {
      return lcl_ref_inc(box->val);
    }

    return lcl_string_new("<resource>");
  }
  default: return lk_value_to_lcl(v, ui->intern);
  }
}

/* The hit sub-dict: #{ptype <name> value <unwrapped> locus_kind
 * <name> locus <...>}.  For locus_kind "editor-range" the locus is
 * decoded into #{annot_id start end pos rev "hi:lo"}; any other kind
 * gets the raw 6-int list. */
static lcl_value *hit_to_dict(lk_ui *ui, const lk_command *cmd) {
  const lk_presentation_hit *hit = &cmd->hit;
  lcl_value *dict = lcl_dict_new();
  lcl_value *v;
  const char *pt = lk_intern_cstr(ui->intern, hit->type_id);
  const char *lk_name =
      hit->locus_kind ? lk_intern_cstr(ui->intern, hit->locus_kind) : NULL;

  v = lcl_string_new(pt ? pt : "");
  lcl_dict_put(&dict, "ptype", v);
  lcl_ref_dec(v);

  v = cmd_value_to_lcl(ui, cmd, &hit->value);
  lcl_dict_put(&dict, "value", v);
  lcl_ref_dec(v);

  v = lcl_string_new(lk_name ? lk_name : "");
  lcl_dict_put(&dict, "locus_kind", v);
  lcl_ref_dec(v);

  if (lk_name && strcmp(lk_name, "editor-range") == 0) {
    lcl_value *locus = lcl_dict_new();
    char rev[32];

    v = lcl_int_new((long)hit->locus[0]);
    lcl_dict_put(&locus, "annot_id", v);
    lcl_ref_dec(v);

    v = lcl_int_new((long)hit->locus[1]);
    lcl_dict_put(&locus, "start", v);
    lcl_ref_dec(v);

    v = lcl_int_new((long)hit->locus[2]);
    lcl_dict_put(&locus, "end", v);
    lcl_ref_dec(v);

    v = lcl_int_new((long)hit->locus[3]);
    lcl_dict_put(&locus, "pos", v);
    lcl_ref_dec(v);

    /* Same "hi:lo" shape as lk::doc_revision, so scripts can compare
     * for staleness with a string equality. */
    sprintf(rev, "%lu:%lu", (unsigned long)hit->locus[4],
            (unsigned long)hit->locus[5]);
    v = lcl_string_new(rev);
    lcl_dict_put(&locus, "rev", v);
    lcl_ref_dec(v);

    lcl_dict_put(&dict, "locus", locus);
    lcl_ref_dec(locus);
  } else {
    lcl_value *locus = lcl_list_new();
    int i;

    for (i = 0; i < 6; i++) {
      v = lcl_int_new((long)hit->locus[i]);
      lcl_list_push(&locus, v);
      lcl_ref_dec(v);
    }

    lcl_dict_put(&dict, "locus", locus);
    lcl_ref_dec(locus);
  }

  return dict;
}

static lcl_value *command_to_dict(const lk_command *cmd, lk_ui *ui) {
  const lk_intern *intern = ui->intern;
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
      lcl_value *av = cmd_value_to_lcl(ui, cmd, &cmd->args[i]);

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

  /* hit — only when the command came from an interior presentation
   * (scripts test with `has?`). */
  if (cmd->hit.type_id != 0) {
    v = hit_to_dict(ui, cmd);
    lcl_dict_put(&dict, "hit", v);
    lcl_ref_dec(v);
  }

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

  dict = command_to_dict(cmd, ctx->ui);

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
    lcl_value *d = command_to_dict(&q->cmds[i], ui);
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
    lcl_value *d = command_to_dict(&log[i], ui);
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

  /* Keys below LKS_USER are internal widget state — scripts never
   * poke widget state (initial values are props, e.g. the dropdown's
   * `value`).  App-owned state starts at LKS_USER. */
  if (key < (long)LKS_USER) {
    lcl_set_error(interp,
                  "lk::state_set: keys below 256 (LKS_USER) are internal "
                  "widget state");

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

  /* Same barrier as lk::state_set: internal widget state is not
   * script-visible. */
  if (key < (long)LKS_USER) {
    lcl_set_error(interp,
                  "lk::state_get: keys below 256 (LKS_USER) are internal "
                  "widget state");

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
  case UIV_RESOURCE: *out = lcl_string_new("<resource>"); break;
  case UIV_TEXT: *out = lcl_string_new("<text>"); break; /* command scope only */
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
 * Event marshaling — shared by the SDL event-handler bridge and the
 * headless tests (lcl_lk_event_to_dict is deliberately non-static so
 * a host driving lk_event_route itself can reuse the marshal).
 * ============================================================================
 */

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
  case LK_EVENT_FOCUS_CHANGED: return "focus_changed";
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

lcl_value *lcl_lk_event_to_dict(const lk_event *ev, const lk_intern *intern);

lcl_value *lcl_lk_event_to_dict(const lk_event *ev, const lk_intern *intern) {
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

  case LK_EVENT_FOCUS_CHANGED: {
    /* Node-id STRINGS resolved from the intern table (the
     * target_id/node_id convention); 0 = no focus -> empty string. */
    const char *ps =
        ev->data.focus.prev_id
            ? lk_intern_cstr(intern, ev->data.focus.prev_id)
            : NULL;
    const char *ns =
        ev->data.focus.next_id
            ? lk_intern_cstr(intern, ev->data.focus.next_id)
            : NULL;

    v = lcl_string_new(ps ? ps : "");
    lcl_dict_put(&dict, "prev_id", v);
    lcl_ref_dec(v);

    v = lcl_string_new(ns ? ns : "");
    lcl_dict_put(&dict, "next_id", v);
    lcl_ref_dec(v);
    break;
  }

  default: break;
  }

  return dict;
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

  ev_dict = lcl_lk_event_to_dict(event, lk_ui_intern(lk_window_ui(lw->win)));

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

  if (ui) {
    live_ui_remove(ui);
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

  live_ui_add(lk_window_ui(win));
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

  live_ui_remove(lk_window_ui(lw->win));
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

/* lk::overlay_push [ui dict] — push an overlay from app code.
 *
 * Dict keys:
 *   kind               required: "dropdown_popup"|"tooltip"|
 *                      "context_menu"|"modal"
 *   anchor             optional: "below"|"above"|"at_cursor"|"center"
 *                      (default: "center" for modal, else "below")
 *   owner_id           optional node id string (interned)
 *   content_root_id    optional node id string (interned) — a
 *                      UIP_HIDDEN subtree laid out at the anchor
 *   dismiss_on_outside optional bool (default: modal 0, else 1)
 *   traps_focus        optional bool (default: modal 1, else 0)
 */
static int c_lk_overlay_push(lcl_interp *interp, int argc, lcl_value **argv,
                             lcl_value **out) {
  lk_ui *ui = NULL;
  lcl_value *dict;
  lcl_value *v;
  lk_overlay ov;
  int kind_val;
  int is_modal;

  if (argc != 2) {
    lcl_set_error(interp, "lk::overlay_push: expected 2 arguments (ui, dict)");
    return LCL_RC_ERR;
  }

  if (lcl_opaque_get(argv[0], LK_UI_TYPE, (void **)&ui) != LCL_OK || !ui) {
    lcl_set_error(interp, "lk::overlay_push: bad ui handle");
    return LCL_RC_ERR;
  }

  dict = argv[1];

  if (lcl_value_type_of(dict) != LCL_DICT) {
    lcl_set_error(interp, "lk::overlay_push: expected dict");
    return LCL_RC_ERR;
  }

  if (lcl_dict_get(dict, "kind", &v) != LCL_OK) {
    lcl_set_error(interp, "lk::overlay_push: missing kind");
    return LCL_RC_ERR;
  }

  if (!lookup_enum(overlay_kind_table, lcl_value_to_string(v), &kind_val)) {
    lcl_set_error(interp, "lk::overlay_push: unknown overlay kind");
    return LCL_RC_ERR;
  }

  is_modal = (kind_val == LK_OVERLAY_MODAL);

  memset(&ov, 0, sizeof(ov));
  ov.kind = (lk_u8)kind_val;
  ov.anchor_mode =
      (lk_u8)(is_modal ? LK_ANCHOR_CENTER_VIEWPORT : LK_ANCHOR_BELOW);
  ov.dismiss_on_outside = (lk_u8)(is_modal ? 0 : 1);
  ov.traps_focus = (lk_u8)(is_modal ? 1 : 0);

  if (lcl_dict_get(dict, "anchor", &v) == LCL_OK) {
    int anchor_val;

    if (!lookup_enum(anchor_table, lcl_value_to_string(v), &anchor_val)) {
      lcl_set_error(interp, "lk::overlay_push: unknown anchor");
      return LCL_RC_ERR;
    }

    ov.anchor_mode = (lk_u8)anchor_val;
  }

  if (lcl_dict_get(dict, "owner_id", &v) == LCL_OK) {
    ov.owner_id = lk_intern_cid(ui->intern, lcl_value_to_string(v));
  }

  if (lcl_dict_get(dict, "content_root_id", &v) == LCL_OK) {
    ov.content_root_id = lk_intern_cid(ui->intern, lcl_value_to_string(v));
  }

  if (lcl_dict_get(dict, "dismiss_on_outside", &v) == LCL_OK) {
    long b;

    if (lcl_value_to_int(v, &b) != LCL_OK) {
      lcl_set_error(interp, "lk::overlay_push: dismiss_on_outside expects int");
      return LCL_RC_ERR;
    }

    ov.dismiss_on_outside = (lk_u8)(b ? 1 : 0);
  }

  if (lcl_dict_get(dict, "traps_focus", &v) == LCL_OK) {
    long b;

    if (lcl_value_to_int(v, &b) != LCL_OK) {
      lcl_set_error(interp, "lk::overlay_push: traps_focus expects int");
      return LCL_RC_ERR;
    }

    ov.traps_focus = (lk_u8)(b ? 1 : 0);
  }

  if (!lk_overlay_push(ui, &ov)) {
    lcl_set_error(interp, "lk::overlay_push: push failed");
    return LCL_RC_ERR;
  }

  *out = lcl_string_new("");

  return LCL_RC_OK;
}

/* lk::overlay_pop [ui] — pop the topmost overlay (no-op when empty). */
static int c_lk_overlay_pop(lcl_interp *interp, int argc, lcl_value **argv,
                            lcl_value **out) {
  lk_ui *ui = NULL;

  if (argc != 1) {
    lcl_set_error(interp, "lk::overlay_pop: expected 1 argument");
    return LCL_RC_ERR;
  }

  if (lcl_opaque_get(argv[0], LK_UI_TYPE, (void **)&ui) != LCL_OK || !ui) {
    lcl_set_error(interp, "lk::overlay_pop: bad ui handle");
    return LCL_RC_ERR;
  }

  lk_overlay_pop(ui);

  *out = lcl_string_new("");

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
 * Editor track: documents (14)
 * ============================================================================
 */

/* ---- Typed wrapper getters (generic messages, get_lk_window style) ---- */

static struct lcl_lk_doc *get_doc(lcl_interp *interp, lcl_value *val) {
  struct lcl_lk_doc *dw = NULL;

  if (lcl_opaque_get(val, LK_DOC_TYPE, (void **)&dw) != LCL_OK) {
    lcl_set_error(interp, "expected lk_document opaque");

    return NULL;
  }

  return dw;
}

static struct lcl_lk_history *get_history(lcl_interp *interp, lcl_value *val) {
  struct lcl_lk_history *hw = NULL;

  if (lcl_opaque_get(val, LK_HIST_TYPE, (void **)&hw) != LCL_OK) {
    lcl_set_error(interp, "expected lk_edit_history opaque");

    return NULL;
  }

  return hw;
}

static struct lcl_lk_editor *get_editor(lcl_interp *interp, lcl_value *val) {
  struct lcl_lk_editor *ew = NULL;

  if (lcl_opaque_get(val, LK_EDITOR_TYPE, (void **)&ew) != LCL_OK) {
    lcl_set_error(interp, "expected lk_editor opaque");

    return NULL;
  }

  return ew;
}

static struct lcl_lk_annot *get_annot(lcl_interp *interp, lcl_value *val) {
  struct lcl_lk_annot *aw = NULL;

  if (lcl_opaque_get(val, LK_ANNOT_TYPE, (void **)&aw) != LCL_OK) {
    lcl_set_error(interp, "expected lk_annot_store opaque");

    return NULL;
  }

  return aw;
}

/* Lcl string from a non-terminated byte run (delta bytes are only
 * valid during the notification; this copies them into the value). */
static lcl_value *lcl_string_from_bytes(const char *p, lk_u32 len) {
  char *buf;
  lcl_value *v;

  if (!p || len == 0) {
    return lcl_string_new("");
  }

  buf = (char *)malloc(len + 1);

  if (!buf) {
    return lcl_string_new("");
  }

  memcpy(buf, p, len);
  buf[len] = '\0';
  v = lcl_string_new(buf);
  free(buf);

  return v;
}

/* lk::doc_new [?text] -> opaque<lk_document> */
static int c_lk_doc_new(lcl_interp *interp, int argc, lcl_value **argv,
                        lcl_value **out) {
  struct lcl_lk_doc *dw;
  lk_document *doc;

  if (argc > 1) {
    lcl_set_error(interp, "lk::doc_new: expected 0 or 1 arguments (?text)");

    return LCL_RC_ERR;
  }

  if (argc == 1) {
    const char *text = lcl_value_to_string(argv[0]);
    doc = lk_doc_from_str(NULL, NULL, NULL, text, (lk_u32)strlen(text));
  } else {
    doc = lk_doc_new(NULL, NULL, NULL);
  }

  if (!doc) {
    lcl_set_error(interp, "lk::doc_new: allocation failed");

    return LCL_RC_ERR;
  }

  dw = (struct lcl_lk_doc *)malloc(sizeof(*dw));

  if (!dw) {
    lk_doc_destroy(doc);
    lcl_set_error(interp, "lk::doc_new: allocation failed");

    return LCL_RC_ERR;
  }

  dw->doc = doc;
  dw->subs = NULL;

  *out = lcl_opaque_new(dw, LK_DOC_TYPE, doc_finalizer);

  return LCL_RC_OK;
}

/* lk::doc_text [doc] -> string (the whole contents) */
static int c_lk_doc_text(lcl_interp *interp, int argc, lcl_value **argv,
                         lcl_value **out) {
  struct lcl_lk_doc *dw;
  lk_u32 n;
  char *buf;

  if (argc != 1) {
    lcl_set_error(interp, "lk::doc_text: expected 1 argument");

    return LCL_RC_ERR;
  }

  dw = get_doc(interp, argv[0]);

  if (!dw) {
    return LCL_RC_ERR;
  }

  n = lk_doc_len(dw->doc);
  buf = (char *)malloc(n + 1);

  if (!buf) {
    lcl_set_error(interp, "lk::doc_text: allocation failed");

    return LCL_RC_ERR;
  }

  lk_doc_get_text(dw->doc, 0, buf, n);
  buf[n] = '\0';
  *out = lcl_string_new(buf);
  free(buf);

  return LCL_RC_OK;
}

/* lk::doc_len [doc] -> int */
static int c_lk_doc_len(lcl_interp *interp, int argc, lcl_value **argv,
                        lcl_value **out) {
  struct lcl_lk_doc *dw;

  if (argc != 1) {
    lcl_set_error(interp, "lk::doc_len: expected 1 argument");

    return LCL_RC_ERR;
  }

  dw = get_doc(interp, argv[0]);

  if (!dw) {
    return LCL_RC_ERR;
  }

  *out = lcl_int_new((long)lk_doc_len(dw->doc));

  return LCL_RC_OK;
}

/* lk::doc_line_count [doc] -> int */
static int c_lk_doc_line_count(lcl_interp *interp, int argc, lcl_value **argv,
                               lcl_value **out) {
  struct lcl_lk_doc *dw;

  if (argc != 1) {
    lcl_set_error(interp, "lk::doc_line_count: expected 1 argument");

    return LCL_RC_ERR;
  }

  dw = get_doc(interp, argv[0]);

  if (!dw) {
    return LCL_RC_ERR;
  }

  *out = lcl_int_new((long)lk_doc_line_count(dw->doc));

  return LCL_RC_OK;
}

/* lk::doc_pos_to_line [doc, pos] -> 0-based line index.
 *
 * pos at or past the document end resolves to the last line
 * (mirroring the C API); the 1-based display line is script-side
 * arithmetic. */
static int c_lk_doc_pos_to_line(lcl_interp *interp, int argc, lcl_value **argv,
                                lcl_value **out) {
  struct lcl_lk_doc *dw;
  long pos;

  if (argc != 2) {
    lcl_set_error(interp,
                  "lk::doc_pos_to_line: expected 2 arguments (doc, pos)");

    return LCL_RC_ERR;
  }

  dw = get_doc(interp, argv[0]);

  if (!dw) {
    return LCL_RC_ERR;
  }

  if (lcl_value_to_int(argv[1], &pos) != LCL_OK || pos < 0) {
    lcl_set_error(interp,
                  "lk::doc_pos_to_line: pos must be a non-negative integer");

    return LCL_RC_ERR;
  }

  *out = lcl_int_new((long)lk_doc_pos_to_line(dw->doc, (lk_u32)pos));

  return LCL_RC_OK;
}

/* lk::doc_line_start [doc, line] -> byte offset of the line's start.
 *
 * line is 0-based.  Out-of-range lines are hard errors (the C API's
 * silent 0 is indistinguishable from line 0's start). */
static int c_lk_doc_line_start(lcl_interp *interp, int argc, lcl_value **argv,
                               lcl_value **out) {
  struct lcl_lk_doc *dw;
  long line;

  if (argc != 2) {
    lcl_set_error(interp,
                  "lk::doc_line_start: expected 2 arguments (doc, line)");

    return LCL_RC_ERR;
  }

  dw = get_doc(interp, argv[0]);

  if (!dw) {
    return LCL_RC_ERR;
  }

  if (lcl_value_to_int(argv[1], &line) != LCL_OK || line < 0) {
    lcl_set_error(interp,
                  "lk::doc_line_start: line must be a non-negative integer");

    return LCL_RC_ERR;
  }

  if ((lk_u32)line >= lk_doc_line_count(dw->doc)) {
    lcl_set_error(interp, "lk::doc_line_start: line out of range");

    return LCL_RC_ERR;
  }

  *out = lcl_int_new((long)lk_doc_line_start(dw->doc, (lk_u32)line));

  return LCL_RC_OK;
}

/* lk::doc_line_end [doc, line] -> byte offset of the line's end (its
 * \n, exclusive; the document length for the last line).
 *
 * line is 0-based; out-of-range lines are hard errors. */
static int c_lk_doc_line_end(lcl_interp *interp, int argc, lcl_value **argv,
                             lcl_value **out) {
  struct lcl_lk_doc *dw;
  long line;

  if (argc != 2) {
    lcl_set_error(interp, "lk::doc_line_end: expected 2 arguments (doc, line)");

    return LCL_RC_ERR;
  }

  dw = get_doc(interp, argv[0]);

  if (!dw) {
    return LCL_RC_ERR;
  }

  if (lcl_value_to_int(argv[1], &line) != LCL_OK || line < 0) {
    lcl_set_error(interp,
                  "lk::doc_line_end: line must be a non-negative integer");

    return LCL_RC_ERR;
  }

  if ((lk_u32)line >= lk_doc_line_count(dw->doc)) {
    lcl_set_error(interp, "lk::doc_line_end: line out of range");

    return LCL_RC_ERR;
  }

  *out = lcl_int_new((long)lk_doc_line_end(dw->doc, (lk_u32)line));

  return LCL_RC_OK;
}

/* lk::doc_char_col [doc, pos] -> 1-based CHARACTER column.
 *
 * Column definition (docs/editor-wrap.md section 8, pinned): the
 * 1-based codepoint count from the line start to pos.  A tab counts
 * as ONE character; this is never a byte column and never a
 * visual/tab-expanded column (that is a different, future notion).
 * Script-side counting cannot be exact (String::length is
 * byte-based), hence this proc.
 *
 * Implementation: count UTF-8 lead bytes ((b & 0xC0) != 0x80)
 * between lk_doc_line_start(pos's line) and pos via chunked
 * lk_doc_get_text reads -- public API only, no core internals.
 * pos must be in [0, doc len] (codepoint-boundary alignment is the
 * caller's business; the cursor always is). */
static int c_lk_doc_char_col(lcl_interp *interp, int argc, lcl_value **argv,
                             lcl_value **out) {
  struct lcl_lk_doc *dw;
  long pos;
  lk_u32 at;
  lk_u32 col;
  char buf[256];

  if (argc != 2) {
    lcl_set_error(interp, "lk::doc_char_col: expected 2 arguments (doc, pos)");

    return LCL_RC_ERR;
  }

  dw = get_doc(interp, argv[0]);

  if (!dw) {
    return LCL_RC_ERR;
  }

  if (lcl_value_to_int(argv[1], &pos) != LCL_OK || pos < 0) {
    lcl_set_error(interp,
                  "lk::doc_char_col: pos must be a non-negative integer");

    return LCL_RC_ERR;
  }

  if ((lk_u32)pos > lk_doc_len(dw->doc)) {
    lcl_set_error(interp, "lk::doc_char_col: pos out of range");

    return LCL_RC_ERR;
  }

  at = lk_doc_line_start(dw->doc, lk_doc_pos_to_line(dw->doc, (lk_u32)pos));
  col = 1;

  while (at < (lk_u32)pos) {
    lk_u32 want = (lk_u32)pos - at;
    lk_u32 got;
    lk_u32 i;

    if (want > (lk_u32)sizeof(buf)) {
      want = (lk_u32)sizeof(buf);
    }

    got = lk_doc_get_text(dw->doc, at, buf, want);

    if (got == 0) {
      break; /* defensive: cannot happen for in-range pos */
    }

    for (i = 0; i < got; i++) {
      if (((unsigned char)buf[i] & 0xC0u) != 0x80u) {
        col++;
      }
    }

    at += got;
  }

  *out = lcl_int_new((long)col);

  return LCL_RC_OK;
}

/* lk::doc_find [doc, needle, ?from] -> first match position >= from,
 * or -1 when not found.
 *
 * Literal forward byte search (no patterns); from defaults to 0.  A
 * from past the document end is simply not-found (-1), so the
 * search-next idiom -- search again from hit + 1 -- never errors at
 * the end of the document.  An empty needle is a hard error (the C
 * API's silent 0 would be indistinguishable from not-found). */
static int c_lk_doc_find(lcl_interp *interp, int argc, lcl_value **argv,
                         lcl_value **out) {
  struct lcl_lk_doc *dw;
  const char *needle;
  long from = 0;
  lk_u32 pos = 0;

  if (argc != 2 && argc != 3) {
    lcl_set_error(interp,
                  "lk::doc_find: expected 2 or 3 arguments (doc, needle, "
                  "?from)");

    return LCL_RC_ERR;
  }

  dw = get_doc(interp, argv[0]);

  if (!dw) {
    return LCL_RC_ERR;
  }

  needle = lcl_value_to_string(argv[1]);

  if (!needle || needle[0] == '\0') {
    lcl_set_error(interp, "lk::doc_find: needle must be non-empty");

    return LCL_RC_ERR;
  }

  if (argc == 3) {
    if (lcl_value_to_int(argv[2], &from) != LCL_OK || from < 0) {
      lcl_set_error(interp,
                    "lk::doc_find: from must be a non-negative integer");

      return LCL_RC_ERR;
    }
  }

  if (lk_doc_find(dw->doc, needle, (lk_u32)strlen(needle), (lk_u32)from,
                  &pos)) {
    *out = lcl_int_new((long)pos);
  } else {
    *out = lcl_int_new(-1);
  }

  return LCL_RC_OK;
}

/* lk::doc_revision [doc] -> "hi:lo" (e.g. "0:42").
 *
 * The lk_revision {hi,lo} pair encoded as a deterministic string —
 * equality-comparable from script ([== $r1 $r2]); nothing more.  Do
 * not do arithmetic on it. */
static int c_lk_doc_revision(lcl_interp *interp, int argc, lcl_value **argv,
                             lcl_value **out) {
  struct lcl_lk_doc *dw;
  lk_revision rev;
  char buf[32];

  if (argc != 1) {
    lcl_set_error(interp, "lk::doc_revision: expected 1 argument");

    return LCL_RC_ERR;
  }

  dw = get_doc(interp, argv[0]);

  if (!dw) {
    return LCL_RC_ERR;
  }

  rev = lk_doc_revision(dw->doc);
  sprintf(buf, "%lu:%lu", (unsigned long)rev.hi, (unsigned long)rev.lo);
  *out = lcl_string_new(buf);

  return LCL_RC_OK;
}

/* lk::doc_insert [doc, pos, text] -> "" */
static int c_lk_doc_insert(lcl_interp *interp, int argc, lcl_value **argv,
                           lcl_value **out) {
  struct lcl_lk_doc *dw;
  long pos;
  const char *text;

  if (argc != 3) {
    lcl_set_error(interp, "lk::doc_insert: expected 3 arguments (doc, pos, text)");

    return LCL_RC_ERR;
  }

  dw = get_doc(interp, argv[0]);

  if (!dw) {
    return LCL_RC_ERR;
  }

  if (lcl_value_to_int(argv[1], &pos) != LCL_OK || pos < 0) {
    lcl_set_error(interp, "lk::doc_insert: pos must be a non-negative integer");

    return LCL_RC_ERR;
  }

  text = lcl_value_to_string(argv[2]);

  if (!lk_doc_insert(dw->doc, (lk_u32)pos, text, (lk_u32)strlen(text))) {
    lcl_set_error(interp,
                  "lk::doc_insert: rejected (pos out of range, empty text, "
                  "or mutation from inside a notification)");

    return LCL_RC_ERR;
  }

  *out = lcl_string_new("");

  return LCL_RC_OK;
}

/* lk::doc_delete [doc, pos, len] -> "" */
static int c_lk_doc_delete(lcl_interp *interp, int argc, lcl_value **argv,
                           lcl_value **out) {
  struct lcl_lk_doc *dw;
  long pos;
  long len;

  if (argc != 3) {
    lcl_set_error(interp, "lk::doc_delete: expected 3 arguments (doc, pos, len)");

    return LCL_RC_ERR;
  }

  dw = get_doc(interp, argv[0]);

  if (!dw) {
    return LCL_RC_ERR;
  }

  if (lcl_value_to_int(argv[1], &pos) != LCL_OK || pos < 0) {
    lcl_set_error(interp, "lk::doc_delete: pos must be a non-negative integer");

    return LCL_RC_ERR;
  }

  if (lcl_value_to_int(argv[2], &len) != LCL_OK || len < 0) {
    lcl_set_error(interp, "lk::doc_delete: len must be a non-negative integer");

    return LCL_RC_ERR;
  }

  if (!lk_doc_delete(dw->doc, (lk_u32)pos, (lk_u32)len)) {
    lcl_set_error(interp,
                  "lk::doc_delete: rejected (pos out of range, zero length, "
                  "or mutation from inside a notification)");

    return LCL_RC_ERR;
  }

  *out = lcl_string_new("");

  return LCL_RC_OK;
}

/* lk::doc_transact [doc, body] -> ""
 *
 * Brackets the body in one lk_doc_begin/lk_doc_commit transaction:
 * every lk::doc_insert/lk::doc_delete inside becomes one committed
 * transaction — one notification, one undo step.  The commit runs
 * even if the body errors (edits made before the error stay applied,
 * as one transaction), then the body's error propagates.  Nesting
 * doc_transact on the same document is a programming error (the C
 * layer debug-asserts nested begin). */
static int c_lk_doc_transact(lcl_interp *interp, int argc, lcl_value **argv,
                             lcl_value **out) {
  struct lcl_lk_doc *dw;
  const char *body;
  lcl_value *r = NULL;
  int rc;

  if (argc != 2) {
    lcl_set_error(interp, "lk::doc_transact: expected 2 arguments (doc, body)");

    return LCL_RC_ERR;
  }

  dw = get_doc(interp, argv[0]);

  if (!dw) {
    return LCL_RC_ERR;
  }

  body = lcl_value_to_string(argv[1]);

  lk_doc_begin(dw->doc, LK_ORIGIN_NONE);
  rc = lcl_eval_string(interp, body, &r);
  lk_doc_commit(dw->doc);

  if (r) {
    lcl_ref_dec(r);
  }

  if (rc != LCL_RC_OK) {
    return LCL_RC_ERR; /* interp error already set by the body */
  }

  *out = lcl_string_new("");

  return LCL_RC_OK;
}

/* ---- Document subscription bridge ---- */

static void lcl_doc_sub_bridge(void *ud, const lk_document *d,
                               const lk_doc_delta *deltas, lk_u32 n) {
  struct lcl_doc_sub *sub = (struct lcl_doc_sub *)ud;
  lcl_value *list = lcl_list_new();
  lcl_value *args[1];
  lcl_value *result = NULL;
  lk_u32 i;

  (void)d;

  for (i = 0; i < n; i++) {
    const lk_doc_delta *dl = &deltas[i];
    lcl_value *dict = lcl_dict_new();
    lcl_value *v;

    v = lcl_int_new((long)dl->start);
    lcl_dict_put(&dict, "start", v);
    lcl_ref_dec(v);

    v = lcl_int_new((long)dl->deleted_len);
    lcl_dict_put(&dict, "deleted_len", v);
    lcl_ref_dec(v);

    v = lcl_int_new((long)dl->inserted_len);
    lcl_dict_put(&dict, "inserted_len", v);
    lcl_ref_dec(v);

    /* Delta byte pointers are valid only during this notification —
     * copied into Lcl strings here, so the script may keep them. */
    v = lcl_string_from_bytes(dl->deleted, dl->deleted_len);
    lcl_dict_put(&dict, "deleted", v);
    lcl_ref_dec(v);

    v = lcl_string_from_bytes(dl->inserted, dl->inserted_len);
    lcl_dict_put(&dict, "inserted", v);
    lcl_ref_dec(v);

    v = lcl_int_new((long)dl->origin);
    lcl_dict_put(&dict, "origin", v);
    lcl_ref_dec(v);

    lcl_list_push(&list, dict);
    lcl_ref_dec(dict);
  }

  args[0] = list;
  lcl_call_proc(sub->interp, sub->handler, 1, args, &result);

  if (result) {
    lcl_ref_dec(result);
  }

  lcl_ref_dec(list);
}

/* lk::doc_subscribe [doc, proc] -> subscription id (int).
 * The proc receives ONE argument: a list of delta dicts, each with
 * start, deleted_len, inserted_len, deleted, inserted, origin. */
static int c_lk_doc_subscribe(lcl_interp *interp, int argc, lcl_value **argv,
                              lcl_value **out) {
  struct lcl_lk_doc *dw;
  struct lcl_doc_sub *sub;

  if (argc != 2) {
    lcl_set_error(interp, "lk::doc_subscribe: expected 2 arguments (doc, proc)");

    return LCL_RC_ERR;
  }

  dw = get_doc(interp, argv[0]);

  if (!dw) {
    return LCL_RC_ERR;
  }

  if (!lcl_is_callable(argv[1])) {
    lcl_set_error(interp, "lk::doc_subscribe: expected callable");

    return LCL_RC_ERR;
  }

  sub = (struct lcl_doc_sub *)malloc(sizeof(*sub));

  if (!sub) {
    lcl_set_error(interp, "lk::doc_subscribe: allocation failed");

    return LCL_RC_ERR;
  }

  sub->interp = interp;
  sub->handler = lcl_ref_inc(argv[1]);
  sub->id = lk_doc_subscribe(dw->doc, lcl_doc_sub_bridge, sub);

  if (sub->id == 0) {
    lcl_ref_dec(sub->handler);
    free(sub);
    lcl_set_error(interp, "lk::doc_subscribe: subscribe failed");

    return LCL_RC_ERR;
  }

  sub->next = dw->subs;
  dw->subs = sub;

  *out = lcl_int_new((long)sub->id);

  return LCL_RC_OK;
}

/* lk::doc_unsubscribe [doc, id] -> "" */
static int c_lk_doc_unsubscribe(lcl_interp *interp, int argc, lcl_value **argv,
                                lcl_value **out) {
  struct lcl_lk_doc *dw;
  struct lcl_doc_sub **link;
  long id;

  if (argc != 2) {
    lcl_set_error(interp, "lk::doc_unsubscribe: expected 2 arguments (doc, id)");

    return LCL_RC_ERR;
  }

  dw = get_doc(interp, argv[0]);

  if (!dw) {
    return LCL_RC_ERR;
  }

  if (lcl_value_to_int(argv[1], &id) != LCL_OK) {
    lcl_set_error(interp, "lk::doc_unsubscribe: id must be an integer");

    return LCL_RC_ERR;
  }

  for (link = &dw->subs; *link; link = &(*link)->next) {
    if ((*link)->id == (lk_u32)id) {
      struct lcl_doc_sub *sub = *link;

      *link = sub->next;
      lk_doc_unsubscribe(dw->doc, sub->id);
      lcl_ref_dec(sub->handler);
      free(sub);

      *out = lcl_string_new("");

      return LCL_RC_OK;
    }
  }

  lcl_set_error(interp, "lk::doc_unsubscribe: unknown subscription id");

  return LCL_RC_ERR;
}

/* ============================================================================
 * Editor track: edit history (5)
 * ============================================================================
 */

/* lk::history_new [?doc] -> opaque<lk_edit_history>.
 * With a doc argument the history attaches immediately (records every
 * committed transaction).  Without one it stays detached until an
 * lk::editor_new call wires it to that editor's document. */
static int c_lk_history_new(lcl_interp *interp, int argc, lcl_value **argv,
                            lcl_value **out) {
  struct lcl_lk_history *hw;
  struct lcl_lk_doc *dw = NULL;
  lk_edit_history *hist;

  if (argc > 1) {
    lcl_set_error(interp, "lk::history_new: expected 0 or 1 arguments (?doc)");

    return LCL_RC_ERR;
  }

  if (argc == 1) {
    dw = get_doc(interp, argv[0]);

    if (!dw) {
      return LCL_RC_ERR;
    }
  }

  hist = lk_history_new(NULL, NULL, NULL);

  if (!hist) {
    lcl_set_error(interp, "lk::history_new: allocation failed");

    return LCL_RC_ERR;
  }

  hw = (struct lcl_lk_history *)malloc(sizeof(*hw));

  if (!hw) {
    lk_history_destroy(hist);
    lcl_set_error(interp, "lk::history_new: allocation failed");

    return LCL_RC_ERR;
  }

  hw->hist = hist;
  hw->doc = NULL;
  hw->doc_val = NULL;

  if (dw) {
    lk_history_attach(hist, dw->doc);
    hw->doc = dw->doc;
    hw->doc_val = lcl_ref_inc(argv[0]);
  }

  *out = lcl_opaque_new(hw, LK_HIST_TYPE, history_finalizer);

  return LCL_RC_OK;
}

/* lk::history_undo [hist, doc] -> 1 if a step was undone, else 0 */
static int c_lk_history_undo(lcl_interp *interp, int argc, lcl_value **argv,
                             lcl_value **out) {
  struct lcl_lk_history *hw;
  struct lcl_lk_doc *dw;

  if (argc != 2) {
    lcl_set_error(interp, "lk::history_undo: expected 2 arguments (hist, doc)");

    return LCL_RC_ERR;
  }

  hw = get_history(interp, argv[0]);

  if (!hw) {
    return LCL_RC_ERR;
  }

  dw = get_doc(interp, argv[1]);

  if (!dw) {
    return LCL_RC_ERR;
  }

  *out = lcl_int_new((long)lk_history_undo(hw->hist, dw->doc));

  return LCL_RC_OK;
}

/* lk::history_redo [hist, doc] -> 1 if a step was redone, else 0 */
static int c_lk_history_redo(lcl_interp *interp, int argc, lcl_value **argv,
                             lcl_value **out) {
  struct lcl_lk_history *hw;
  struct lcl_lk_doc *dw;

  if (argc != 2) {
    lcl_set_error(interp, "lk::history_redo: expected 2 arguments (hist, doc)");

    return LCL_RC_ERR;
  }

  hw = get_history(interp, argv[0]);

  if (!hw) {
    return LCL_RC_ERR;
  }

  dw = get_doc(interp, argv[1]);

  if (!dw) {
    return LCL_RC_ERR;
  }

  *out = lcl_int_new((long)lk_history_redo(hw->hist, dw->doc));

  return LCL_RC_OK;
}

/* lk::history_can_undo [hist] -> 0/1 */
static int c_lk_history_can_undo(lcl_interp *interp, int argc, lcl_value **argv,
                                 lcl_value **out) {
  struct lcl_lk_history *hw;

  if (argc != 1) {
    lcl_set_error(interp, "lk::history_can_undo: expected 1 argument");

    return LCL_RC_ERR;
  }

  hw = get_history(interp, argv[0]);

  if (!hw) {
    return LCL_RC_ERR;
  }

  *out = lcl_int_new((long)lk_history_can_undo(hw->hist));

  return LCL_RC_OK;
}

/* lk::history_can_redo [hist] -> 0/1 */
static int c_lk_history_can_redo(lcl_interp *interp, int argc, lcl_value **argv,
                                 lcl_value **out) {
  struct lcl_lk_history *hw;

  if (argc != 1) {
    lcl_set_error(interp, "lk::history_can_redo: expected 1 argument");

    return LCL_RC_ERR;
  }

  hw = get_history(interp, argv[0]);

  if (!hw) {
    return LCL_RC_ERR;
  }

  *out = lcl_int_new((long)lk_history_can_redo(hw->hist));

  return LCL_RC_OK;
}

/* ============================================================================
 * Editor track: editors (8)
 * ============================================================================
 */

/* lk::editor_new [ui, doc, ?hist] -> opaque<lk_editor>.
 * Registers the editor in the ui's resource table; the wrapper's
 * finalizer releases the registration before destroying the editor.
 * A detached history is attached to the document here; a history
 * already attached to a DIFFERENT document is an error. */
static int c_lk_editor_new(lcl_interp *interp, int argc, lcl_value **argv,
                           lcl_value **out) {
  lk_ui *ui = NULL;
  struct lcl_lk_doc *dw;
  struct lcl_lk_history *hw = NULL;
  struct lcl_lk_editor *ew;
  lk_editor *ed;
  lk_resource_ref ref;

  if (argc < 2 || argc > 3) {
    lcl_set_error(interp,
                  "lk::editor_new: expected 2 or 3 arguments (ui, doc, ?hist)");

    return LCL_RC_ERR;
  }

  if (lcl_opaque_get(argv[0], LK_UI_TYPE, (void **)&ui) != LCL_OK || !ui) {
    lcl_set_error(interp, "lk::editor_new: expected lk_ui opaque");

    return LCL_RC_ERR;
  }

  dw = get_doc(interp, argv[1]);

  if (!dw) {
    return LCL_RC_ERR;
  }

  if (argc == 3) {
    hw = get_history(interp, argv[2]);

    if (!hw) {
      return LCL_RC_ERR;
    }

    if (hw->doc && hw->doc != dw->doc) {
      lcl_set_error(interp,
                    "lk::editor_new: history is attached to a different "
                    "document");

      return LCL_RC_ERR;
    }
  }

  ed = lk_editor_new(NULL, NULL, NULL, dw->doc, hw ? hw->hist : NULL);

  if (!ed) {
    lcl_set_error(interp, "lk::editor_new: allocation failed");

    return LCL_RC_ERR;
  }

  ref = lk_resource_register(lk_ui_resources(ui), lk_editor_type(), ed,
                             "lcl-editor");

  if (ref.id == 0) {
    lk_editor_destroy(ed);
    lcl_set_error(interp, "lk::editor_new: resource registration failed");

    return LCL_RC_ERR;
  }

  ew = (struct lcl_lk_editor *)malloc(sizeof(*ew));

  if (!ew) {
    lk_resource_release(lk_ui_resources(ui), ref);
    lk_editor_destroy(ed);
    lcl_set_error(interp, "lk::editor_new: allocation failed");

    return LCL_RC_ERR;
  }

  /* Attach a still-detached history to this document (v1: one history
   * per document; the attach is recorded on the history wrapper so a
   * second editor over the same doc+hist does not re-subscribe). */
  if (hw && !hw->doc) {
    lk_history_attach(hw->hist, dw->doc);
    hw->doc = dw->doc;
    hw->doc_val = lcl_ref_inc(argv[1]);
  }

  ew->ed = ed;
  ew->ui = ui;
  ew->ref = ref;
  ew->ui_val = lcl_ref_inc(argv[0]);
  ew->doc_val = lcl_ref_inc(argv[1]);
  ew->hist_val = hw ? lcl_ref_inc(argv[2]) : NULL;
  ew->annot_val = NULL;

  *out = lcl_opaque_new(ew, LK_EDITOR_TYPE, editor_finalizer);

  return LCL_RC_OK;
}

/* lk::editor_cursor [editor] -> byte offset (int) */
static int c_lk_editor_cursor(lcl_interp *interp, int argc, lcl_value **argv,
                              lcl_value **out) {
  struct lcl_lk_editor *ew;

  if (argc != 1) {
    lcl_set_error(interp, "lk::editor_cursor: expected 1 argument");

    return LCL_RC_ERR;
  }

  ew = get_editor(interp, argv[0]);

  if (!ew) {
    return LCL_RC_ERR;
  }

  *out = lcl_int_new((long)lk_editor_cursor(ew->ed));

  return LCL_RC_OK;
}

/* lk::editor_set_cursor [editor, pos] -> "" */
static int c_lk_editor_set_cursor(lcl_interp *interp, int argc,
                                  lcl_value **argv, lcl_value **out) {
  struct lcl_lk_editor *ew;
  long pos;

  if (argc != 2) {
    lcl_set_error(interp,
                  "lk::editor_set_cursor: expected 2 arguments (editor, pos)");

    return LCL_RC_ERR;
  }

  ew = get_editor(interp, argv[0]);

  if (!ew) {
    return LCL_RC_ERR;
  }

  if (lcl_value_to_int(argv[1], &pos) != LCL_OK || pos < 0) {
    lcl_set_error(interp,
                  "lk::editor_set_cursor: pos must be a non-negative integer");

    return LCL_RC_ERR;
  }

  lk_editor_set_cursor(ew->ed, (lk_u32)pos);
  *out = lcl_string_new("");

  return LCL_RC_OK;
}

/* lk::editor_selection [editor] -> (start end) or () when none */
static int c_lk_editor_selection(lcl_interp *interp, int argc,
                                 lcl_value **argv, lcl_value **out) {
  struct lcl_lk_editor *ew;
  lk_u32 start;
  lk_u32 end;
  lcl_value *list;

  if (argc != 1) {
    lcl_set_error(interp, "lk::editor_selection: expected 1 argument");

    return LCL_RC_ERR;
  }

  ew = get_editor(interp, argv[0]);

  if (!ew) {
    return LCL_RC_ERR;
  }

  list = lcl_list_new();

  if (lk_editor_selection(ew->ed, &start, &end)) {
    lcl_value *v;

    v = lcl_int_new((long)start);
    lcl_list_push(&list, v);
    lcl_ref_dec(v);

    v = lcl_int_new((long)end);
    lcl_list_push(&list, v);
    lcl_ref_dec(v);
  }

  *out = list;

  return LCL_RC_OK;
}

/* lk::editor_wrap [editor, mode] -> ""
 *
 * mode is "none" | "character" | "word" (docs/editor-wrap.md section
 * 5).  An unknown name and a mode the engine rejects (word, until
 * word wrap is implemented) are both hard errors listing the
 * supported modes, DSL-v2 style. */
static int c_lk_editor_wrap(lcl_interp *interp, int argc, lcl_value **argv,
                            lcl_value **out) {
  struct lcl_lk_editor *ew;
  const char *mode_str;
  int mode_val;

  if (argc != 2) {
    lcl_set_error(interp, "lk::editor_wrap: expected 2 arguments (editor, mode)");

    return LCL_RC_ERR;
  }

  ew = get_editor(interp, argv[0]);

  if (!ew) {
    return LCL_RC_ERR;
  }

  mode_str = lcl_value_to_string(argv[1]);

  if (!lookup_enum(wrap_mode_table, mode_str, &mode_val)) {
    static char err[160];

    sprintf(err,
            "lk::editor_wrap: unknown mode '%.48s' "
            "(supported: none, character)",
            mode_str);
    lcl_set_error(interp, err);

    return LCL_RC_ERR;
  }

  if (!lk_editor_set_wrap_mode(ew->ed, (lk_editor_wrap_mode)mode_val)) {
    static char err[160];

    sprintf(err,
            "lk::editor_wrap: mode '%.48s' is not implemented yet "
            "(supported: none, character)",
            mode_str);
    lcl_set_error(interp, err);

    return LCL_RC_ERR;
  }

  *out = lcl_string_new("");

  return LCL_RC_OK;
}

/* lk::editor_wrap_get [editor] -> the mode name ("none" | "character"
 * | "word") */
static int c_lk_editor_wrap_get(lcl_interp *interp, int argc,
                                lcl_value **argv, lcl_value **out) {
  struct lcl_lk_editor *ew;
  int mode;
  const str_enum *e;

  if (argc != 1) {
    lcl_set_error(interp, "lk::editor_wrap_get: expected 1 argument");

    return LCL_RC_ERR;
  }

  ew = get_editor(interp, argv[0]);

  if (!ew) {
    return LCL_RC_ERR;
  }

  mode = (int)lk_editor_wrap_mode_get(ew->ed);

  for (e = wrap_mode_table; e->name; e++) {
    if (e->value == mode) {
      *out = lcl_string_new(e->name);

      return LCL_RC_OK;
    }
  }

  lcl_set_error(interp, "lk::editor_wrap_get: unmapped wrap mode");

  return LCL_RC_ERR;
}

/* Joined command-name list for lk::editor_command error messages. */
static const char *ed_cmd_known(void) {
  static char buf[512];
  static int built = 0;

  if (!built) {
    const str_enum *e;
    char *p = buf;

    for (e = ed_cmd_table; e->name; e++) {
      if (e != ed_cmd_table) {
        strcpy(p, ", ");
        p += 2;
      }

      strcpy(p, e->name);
      p += strlen(e->name);
    }

    *p = '\0';
    built = 1;
  }

  return buf;
}

/* lk::editor_command [editor, cmd, ?args...] -> 1 if the command did
 * anything, else 0.
 *
 * cmd is an LK_ED_* name with the prefix stripped and lowercased
 * ("insert_text", "move_left", "select_all", "undo", ...).  Per-
 * command args: insert_text takes the text; set_cursor takes pos and
 * an optional literal "extend"; scroll_lines takes a signed line
 * count; motion commands accept an optional literal "select" flag
 * (extends the selection); everything else takes no args.  Unknown
 * commands and malformed args are hard errors. */
static int c_lk_editor_command(lcl_interp *interp, int argc, lcl_value **argv,
                               lcl_value **out) {
  struct lcl_lk_editor *ew;
  const char *cmd_str;
  int cmd_val;
  int extra;
  lk_editor_cmd_arg arg;

  if (argc < 2) {
    lcl_set_error(
        interp, "lk::editor_command: expected (editor, command, ?args...?)");

    return LCL_RC_ERR;
  }

  ew = get_editor(interp, argv[0]);

  if (!ew) {
    return LCL_RC_ERR;
  }

  cmd_str = lcl_value_to_string(argv[1]);

  if (!lookup_enum(ed_cmd_table, cmd_str, &cmd_val)) {
    static char err[640];

    sprintf(err, "lk::editor_command: unknown command '%.48s' (known: %s)",
            cmd_str, ed_cmd_known());
    lcl_set_error(interp, err);

    return LCL_RC_ERR;
  }

  memset(&arg, 0, sizeof(arg));
  extra = argc - 2;

  switch (cmd_val) {
  case LK_ED_INSERT_TEXT:
    if (extra != 1) {
      lcl_set_error(interp, "lk::editor_command: insert_text expects the text");

      return LCL_RC_ERR;
    }

    arg.text.ptr = lcl_value_to_string(argv[2]);
    arg.text.len = (lk_u32)strlen(arg.text.ptr);
    break;

  case LK_ED_SET_CURSOR: {
    long pos;

    if (extra < 1 || extra > 2) {
      lcl_set_error(interp,
                    "lk::editor_command: set_cursor expects pos ?\"extend\"?");

      return LCL_RC_ERR;
    }

    if (lcl_value_to_int(argv[2], &pos) != LCL_OK || pos < 0) {
      lcl_set_error(
          interp,
          "lk::editor_command: set_cursor pos must be a non-negative integer");

      return LCL_RC_ERR;
    }

    arg.set_cursor.pos = (lk_u32)pos;

    if (extra == 2) {
      if (strcmp(lcl_value_to_string(argv[3]), "extend") != 0) {
        lcl_set_error(
            interp,
            "lk::editor_command: set_cursor trailing flag must be \"extend\"");

        return LCL_RC_ERR;
      }

      arg.set_cursor.extend = 1;
    }
    break;
  }

  case LK_ED_SCROLL_LINES: {
    long lines;

    if (extra != 1 || lcl_value_to_int(argv[2], &lines) != LCL_OK) {
      lcl_set_error(
          interp,
          "lk::editor_command: scroll_lines expects a signed line count");

      return LCL_RC_ERR;
    }

    arg.lines = (lk_i32)lines;
    break;
  }

  default:
    /* Motion range: LK_ED_MOVE_LEFT..MOVE_PAGE_DOWN are contiguous;
     * the visual-row pair was appended after the v1 enum tail
     * (recorded transaction origins must not shift), so it sits
     * outside that range and is matched explicitly. */
    if ((cmd_val >= LK_ED_MOVE_LEFT && cmd_val <= LK_ED_MOVE_PAGE_DOWN) ||
        cmd_val == LK_ED_MOVE_ROW_START || cmd_val == LK_ED_MOVE_ROW_END) {
      /* Motion command: optional literal "select" flag. */
      if (extra > 1 ||
          (extra == 1 && strcmp(lcl_value_to_string(argv[2]), "select") != 0)) {
        lcl_set_error(interp,
                      "lk::editor_command: motion commands take at most a "
                      "\"select\" flag");

        return LCL_RC_ERR;
      }

      if (extra == 1) {
        arg.select = 1;
      }
    } else if (extra != 0) {
      lcl_set_error(interp, "lk::editor_command: command takes no arguments");

      return LCL_RC_ERR;
    }
    break;
  }

  *out = lcl_int_new(
      (long)lk_editor_command(ew->ed, ew->ui, (lk_editor_cmd_id)cmd_val, &arg));

  return LCL_RC_OK;
}

/* Parse one span dict (#{start N end N ?fg (r g b ?a?)? ?bg ...?
 * ?underline 1?}) into *sp.  Returns 1 on success, 0 with the interp
 * error set. */
static int span_from_dict(lcl_interp *interp, lcl_value *dict,
                          lk_edit_span *sp) {
  lcl_value *v;
  long a;
  long b;

  if (lcl_value_type_of(dict) != LCL_DICT) {
    lcl_set_error(interp,
                  "lk::editor_set_spans: each span must be a dict "
                  "(#{start N end N ?fg (r g b)? ?bg (r g b)? ?underline 1?})");

    return 0;
  }

  /* Unknown keys are hard errors (DSL v2 philosophy). */
  {
    lcl_value *keys = NULL;

    if (lcl_dict_keys(dict, &keys) == LCL_OK && keys) {
      size_t i;
      size_t nk = lcl_list_len(keys);

      for (i = 0; i < nk; i++) {
        lcl_value *kv = NULL;

        if (lcl_list_get(keys, i, &kv) == LCL_OK && kv) {
          const char *k = lcl_value_to_string(kv);

          if (strcmp(k, "start") != 0 && strcmp(k, "end") != 0 &&
              strcmp(k, "fg") != 0 && strcmp(k, "bg") != 0 &&
              strcmp(k, "underline") != 0) {
            lcl_ref_dec(kv);
            lcl_ref_dec(keys);
            lcl_set_error(interp,
                          "lk::editor_set_spans: unknown span key (known: "
                          "start, end, fg, bg, underline)");

            return 0;
          }

          lcl_ref_dec(kv);
        }
      }

      lcl_ref_dec(keys);
    }
  }

  if (lcl_dict_get(dict, "start", &v) != LCL_OK) {
    lcl_set_error(interp, "lk::editor_set_spans: span is missing 'start'");

    return 0;
  }

  if (lcl_value_to_int(v, &a) != LCL_OK || a < 0) {
    lcl_ref_dec(v);
    lcl_set_error(interp,
                  "lk::editor_set_spans: span start must be a non-negative "
                  "integer");

    return 0;
  }

  lcl_ref_dec(v);

  if (lcl_dict_get(dict, "end", &v) != LCL_OK) {
    lcl_set_error(interp, "lk::editor_set_spans: span is missing 'end'");

    return 0;
  }

  if (lcl_value_to_int(v, &b) != LCL_OK || b <= a) {
    lcl_ref_dec(v);
    lcl_set_error(interp,
                  "lk::editor_set_spans: span end must be an integer > start");

    return 0;
  }

  lcl_ref_dec(v);

  sp->start = (lk_u32)a;
  sp->end = (lk_u32)b;
  sp->flags = 0;

  if (lcl_dict_get(dict, "fg", &v) == LCL_OK) {
    int ok = parse_color_list(v, &sp->fg);

    lcl_ref_dec(v);

    if (!ok) {
      lcl_set_error(interp,
                    "lk::editor_set_spans: fg must be (r g b) or (r g b a)");

      return 0;
    }

    sp->flags |= LK_SPAN_FG;
  }

  if (lcl_dict_get(dict, "bg", &v) == LCL_OK) {
    int ok = parse_color_list(v, &sp->bg);

    lcl_ref_dec(v);

    if (!ok) {
      lcl_set_error(interp,
                    "lk::editor_set_spans: bg must be (r g b) or (r g b a)");

      return 0;
    }

    sp->flags |= LK_SPAN_BG;
  }

  if (lcl_dict_get(dict, "underline", &v) == LCL_OK) {
    long u;
    int ok = (lcl_value_to_int(v, &u) == LCL_OK);

    lcl_ref_dec(v);

    if (!ok) {
      lcl_set_error(interp, "lk::editor_set_spans: underline expects an int");

      return 0;
    }

    if (u) {
      sp->flags |= LK_SPAN_UNDERLINE;
    }
  }

  return 1;
}

/* lk::editor_set_spans [editor, doc, spans] -> ""
 *
 * spans is a list of span dicts (see span_from_dict); an empty list
 * clears.  The snapshot is stamped with the document's CURRENT
 * revision and the spans' min/max range — a simplification of the
 * docs/editor.md section 11 snapshot dict that is equivalent for
 * synchronous script producers (the producer runs and delivers within
 * one frame, so stamp-at-call == stamp-at-produce).  Spans must be
 * sorted by start and non-overlapping. */
static int c_lk_editor_set_spans(lcl_interp *interp, int argc,
                                 lcl_value **argv, lcl_value **out) {
  struct lcl_lk_editor *ew;
  struct lcl_lk_doc *dw;
  lk_edit_span *spans;
  lk_edit_span_snapshot snap;
  size_t n;
  size_t i;

  if (argc != 3) {
    lcl_set_error(
        interp, "lk::editor_set_spans: expected 3 arguments (editor, doc, spans)");

    return LCL_RC_ERR;
  }

  ew = get_editor(interp, argv[0]);

  if (!ew) {
    return LCL_RC_ERR;
  }

  dw = get_doc(interp, argv[1]);

  if (!dw) {
    return LCL_RC_ERR;
  }

  if (lcl_value_type_of(argv[2]) != LCL_LIST) {
    lcl_set_error(interp,
                  "lk::editor_set_spans: spans must be a list of span dicts");

    return LCL_RC_ERR;
  }

  n = lcl_list_len(argv[2]);

  if (n == 0) {
    lk_editor_set_spans(ew->ed, NULL);
    *out = lcl_string_new("");

    return LCL_RC_OK;
  }

  spans = (lk_edit_span *)malloc(n * sizeof(*spans));

  if (!spans) {
    lcl_set_error(interp, "lk::editor_set_spans: allocation failed");

    return LCL_RC_ERR;
  }

  for (i = 0; i < n; i++) {
    lcl_value *sv = NULL;
    int ok;

    memset(&spans[i], 0, sizeof(spans[i]));

    if (lcl_list_get(argv[2], i, &sv) != LCL_OK || !sv) {
      free(spans);
      lcl_set_error(interp, "lk::editor_set_spans: bad spans list");

      return LCL_RC_ERR;
    }

    ok = span_from_dict(interp, sv, &spans[i]);
    lcl_ref_dec(sv);

    if (!ok) {
      free(spans);

      return LCL_RC_ERR; /* error set by span_from_dict */
    }

    if (i > 0 && spans[i].start < spans[i - 1].end) {
      free(spans);
      lcl_set_error(
          interp,
          "lk::editor_set_spans: spans must be sorted and non-overlapping");

      return LCL_RC_ERR;
    }
  }

  snap.revision = lk_doc_revision(dw->doc);
  snap.range_start = spans[0].start;
  snap.range_end = spans[n - 1].end;
  snap.spans = spans;
  snap.count = (lk_u32)n;

  lk_editor_set_spans(ew->ed, &snap);
  free(spans);

  *out = lcl_string_new("");

  return LCL_RC_OK;
}

/* ============================================================================
 * Editor track: annotation stores (10)
 * ============================================================================
 */

/* lk::annot_store_new -> opaque<lk_annot_store> */
static int c_lk_annot_store_new(lcl_interp *interp, int argc, lcl_value **argv,
                                lcl_value **out) {
  struct lcl_lk_annot *aw;
  lk_annot_store *store;
  (void)argv;

  if (argc != 0) {
    lcl_set_error(interp, "lk::annot_store_new: expected 0 arguments");

    return LCL_RC_ERR;
  }

  store = lk_annot_store_new(NULL, NULL, NULL);

  if (!store) {
    lcl_set_error(interp, "lk::annot_store_new: allocation failed");

    return LCL_RC_ERR;
  }

  aw = (struct lcl_lk_annot *)malloc(sizeof(*aw));

  if (!aw) {
    lk_annot_store_destroy(store);
    lcl_set_error(interp, "lk::annot_store_new: allocation failed");

    return LCL_RC_ERR;
  }

  aw->store = store;
  aw->doc_val = NULL;
  aw->pres_ui = NULL;
  aw->ui_val = NULL;

  *out = lcl_opaque_new(aw, LK_ANNOT_TYPE, annot_finalizer);

  return LCL_RC_OK;
}

/* lk::annot_attach [store, doc] -> "" (subscribes; anchors then track
 * every committed transaction).  Re-attaching is an error. */
static int c_lk_annot_attach(lcl_interp *interp, int argc, lcl_value **argv,
                             lcl_value **out) {
  struct lcl_lk_annot *aw;
  struct lcl_lk_doc *dw;

  if (argc != 2) {
    lcl_set_error(interp, "lk::annot_attach: expected 2 arguments (store, doc)");

    return LCL_RC_ERR;
  }

  aw = get_annot(interp, argv[0]);

  if (!aw) {
    return LCL_RC_ERR;
  }

  dw = get_doc(interp, argv[1]);

  if (!dw) {
    return LCL_RC_ERR;
  }

  if (aw->doc_val) {
    lcl_set_error(interp, "lk::annot_attach: store is already attached");

    return LCL_RC_ERR;
  }

  lk_annot_store_attach(aw->store, dw->doc);
  aw->doc_val = lcl_ref_inc(argv[1]);

  *out = lcl_string_new("");

  return LCL_RC_OK;
}

/* lk::annot_add [store, start, end, layer, ?meta-dict] -> record id.
 * meta-dict keys/values become the record's metadata (all strings). */
static int c_lk_annot_add(lcl_interp *interp, int argc, lcl_value **argv,
                          lcl_value **out) {
  struct lcl_lk_annot *aw;
  long start;
  long end;
  const char *layer;
  lk_u32 id;
  const char **keys = NULL;
  const char **values = NULL;
  lcl_value **kvals = NULL;
  lcl_value **vvals = NULL;
  lcl_value *keys_list = NULL;
  lk_u32 meta_count = 0;

  if (argc < 4 || argc > 5) {
    lcl_set_error(interp,
                  "lk::annot_add: expected 4 or 5 arguments "
                  "(store, start, end, layer, ?meta-dict)");

    return LCL_RC_ERR;
  }

  aw = get_annot(interp, argv[0]);

  if (!aw) {
    return LCL_RC_ERR;
  }

  if (lcl_value_to_int(argv[1], &start) != LCL_OK || start < 0) {
    lcl_set_error(interp, "lk::annot_add: start must be a non-negative integer");

    return LCL_RC_ERR;
  }

  if (lcl_value_to_int(argv[2], &end) != LCL_OK || end <= start) {
    lcl_set_error(interp, "lk::annot_add: end must be an integer > start");

    return LCL_RC_ERR;
  }

  layer = lcl_value_to_string(argv[3]);

  if (argc == 5) {
    size_t nk;
    size_t i;

    if (lcl_value_type_of(argv[4]) != LCL_DICT) {
      lcl_set_error(interp, "lk::annot_add: meta must be a dict");

      return LCL_RC_ERR;
    }

    if (lcl_dict_keys(argv[4], &keys_list) != LCL_OK || !keys_list) {
      lcl_set_error(interp, "lk::annot_add: bad meta dict");

      return LCL_RC_ERR;
    }

    nk = lcl_list_len(keys_list);

    if (nk > 0) {
      keys = (const char **)malloc(nk * sizeof(*keys));
      values = (const char **)malloc(nk * sizeof(*values));
      kvals = (lcl_value **)malloc(nk * sizeof(*kvals));
      vvals = (lcl_value **)malloc(nk * sizeof(*vvals));

      if (!keys || !values || !kvals || !vvals) {
        free((void *)keys);
        free((void *)values);
        free(kvals);
        free(vvals);
        lcl_ref_dec(keys_list);
        lcl_set_error(interp, "lk::annot_add: allocation failed");

        return LCL_RC_ERR;
      }

      for (i = 0; i < nk; i++) {
        lcl_value *kv = NULL;
        lcl_value *vv = NULL;

        if (lcl_list_get(keys_list, i, &kv) != LCL_OK || !kv ||
            lcl_dict_get(argv[4], lcl_value_to_string(kv), &vv) != LCL_OK) {
          if (kv) {
            lcl_ref_dec(kv);
          }

          continue;
        }

        kvals[meta_count] = kv;
        vvals[meta_count] = vv;
        keys[meta_count] = lcl_value_to_string(kv);
        values[meta_count] = lcl_value_to_string(vv);
        meta_count++;
      }
    }
  }

  id = lk_annot_add(aw->store, (lk_u32)start, (lk_u32)end, layer, keys, values,
                    meta_count);

  /* lk_annot_add copied everything; drop the marshalling refs. */
  {
    lk_u32 i;

    for (i = 0; i < meta_count; i++) {
      lcl_ref_dec(kvals[i]);
      lcl_ref_dec(vvals[i]);
    }
  }

  free((void *)keys);
  free((void *)values);
  free(kvals);
  free(vvals);

  if (keys_list) {
    lcl_ref_dec(keys_list);
  }

  if (id == 0) {
    lcl_set_error(interp, "lk::annot_add: add failed");

    return LCL_RC_ERR;
  }

  *out = lcl_int_new((long)id);

  return LCL_RC_OK;
}

/* lk::annot_remove [store, id] -> 1 if removed, 0 if not found */
static int c_lk_annot_remove(lcl_interp *interp, int argc, lcl_value **argv,
                             lcl_value **out) {
  struct lcl_lk_annot *aw;
  long id;

  if (argc != 2) {
    lcl_set_error(interp, "lk::annot_remove: expected 2 arguments (store, id)");

    return LCL_RC_ERR;
  }

  aw = get_annot(interp, argv[0]);

  if (!aw) {
    return LCL_RC_ERR;
  }

  if (lcl_value_to_int(argv[1], &id) != LCL_OK) {
    lcl_set_error(interp, "lk::annot_remove: id must be an integer");

    return LCL_RC_ERR;
  }

  *out = lcl_int_new((long)lk_annot_remove(aw->store, (lk_u32)id));

  return LCL_RC_OK;
}

/* lk::annot_span [store, id] -> (start end); error if the record is
 * gone. */
static int c_lk_annot_span(lcl_interp *interp, int argc, lcl_value **argv,
                           lcl_value **out) {
  struct lcl_lk_annot *aw;
  long id;
  lk_u32 start;
  lk_u32 end;
  lcl_value *list;
  lcl_value *v;

  if (argc != 2) {
    lcl_set_error(interp, "lk::annot_span: expected 2 arguments (store, id)");

    return LCL_RC_ERR;
  }

  aw = get_annot(interp, argv[0]);

  if (!aw) {
    return LCL_RC_ERR;
  }

  if (lcl_value_to_int(argv[1], &id) != LCL_OK) {
    lcl_set_error(interp, "lk::annot_span: id must be an integer");

    return LCL_RC_ERR;
  }

  if (!lk_annot_get_span(aw->store, (lk_u32)id, &start, &end)) {
    lcl_set_error(interp, "lk::annot_span: no such annotation");

    return LCL_RC_ERR;
  }

  list = lcl_list_new();

  v = lcl_int_new((long)start);
  lcl_list_push(&list, v);
  lcl_ref_dec(v);

  v = lcl_int_new((long)end);
  lcl_list_push(&list, v);
  lcl_ref_dec(v);

  *out = list;

  return LCL_RC_OK;
}

/* lk::annot_meta [store, id, key] -> value string ("" when the key is
 * absent); error if the record is gone. */
static int c_lk_annot_meta(lcl_interp *interp, int argc, lcl_value **argv,
                           lcl_value **out) {
  struct lcl_lk_annot *aw;
  long id;
  const char *val;

  if (argc != 3) {
    lcl_set_error(interp,
                  "lk::annot_meta: expected 3 arguments (store, id, key)");

    return LCL_RC_ERR;
  }

  aw = get_annot(interp, argv[0]);

  if (!aw) {
    return LCL_RC_ERR;
  }

  if (lcl_value_to_int(argv[1], &id) != LCL_OK) {
    lcl_set_error(interp, "lk::annot_meta: id must be an integer");

    return LCL_RC_ERR;
  }

  if (!lk_annot_get(aw->store, (lk_u32)id)) {
    lcl_set_error(interp, "lk::annot_meta: no such annotation");

    return LCL_RC_ERR;
  }

  val = lk_annot_get_meta(aw->store, (lk_u32)id, lcl_value_to_string(argv[2]));
  *out = lcl_string_new(val ? val : "");

  return LCL_RC_OK;
}

/* Marshal a query's ids into an Lcl list (frees nothing). */
static lcl_value *annot_query_to_list(const lk_annot_query *q) {
  lcl_value *list = lcl_list_new();
  lk_u32 i;

  for (i = 0; i < q->count; i++) {
    lcl_value *v = lcl_int_new((long)q->ids[i]);

    lcl_list_push(&list, v);
    lcl_ref_dec(v);
  }

  return list;
}

/* lk::annot_in_range [store, start, end, ?layer] -> list of ids */
static int c_lk_annot_in_range(lcl_interp *interp, int argc, lcl_value **argv,
                               lcl_value **out) {
  struct lcl_lk_annot *aw;
  long start;
  long end;
  const char *layer = NULL;
  lk_annot_query q;

  if (argc < 3 || argc > 4) {
    lcl_set_error(interp,
                  "lk::annot_in_range: expected 3 or 4 arguments "
                  "(store, start, end, ?layer)");

    return LCL_RC_ERR;
  }

  aw = get_annot(interp, argv[0]);

  if (!aw) {
    return LCL_RC_ERR;
  }

  if (lcl_value_to_int(argv[1], &start) != LCL_OK ||
      lcl_value_to_int(argv[2], &end) != LCL_OK || start < 0 || end < start) {
    lcl_set_error(interp, "lk::annot_in_range: bad range");

    return LCL_RC_ERR;
  }

  if (argc == 4) {
    layer = lcl_value_to_string(argv[3]);
  }

  lk_annot_query_init(&q);
  lk_annot_in_range(aw->store, (lk_u32)start, (lk_u32)end, layer, &q);
  *out = annot_query_to_list(&q);
  lk_annot_query_free(&q);

  return LCL_RC_OK;
}

/* lk::annot_at [store, pos, ?layer] -> list of ids */
static int c_lk_annot_at(lcl_interp *interp, int argc, lcl_value **argv,
                         lcl_value **out) {
  struct lcl_lk_annot *aw;
  long pos;
  const char *layer = NULL;
  lk_annot_query q;

  if (argc < 2 || argc > 3) {
    lcl_set_error(
        interp, "lk::annot_at: expected 2 or 3 arguments (store, pos, ?layer)");

    return LCL_RC_ERR;
  }

  aw = get_annot(interp, argv[0]);

  if (!aw) {
    return LCL_RC_ERR;
  }

  if (lcl_value_to_int(argv[1], &pos) != LCL_OK || pos < 0) {
    lcl_set_error(interp, "lk::annot_at: pos must be a non-negative integer");

    return LCL_RC_ERR;
  }

  if (argc == 3) {
    layer = lcl_value_to_string(argv[2]);
  }

  lk_annot_query_init(&q);
  lk_annot_at(aw->store, (lk_u32)pos, layer, &q);
  *out = annot_query_to_list(&q);
  lk_annot_query_free(&q);

  return LCL_RC_OK;
}

/* lk::annot_by_layer [store, layer] -> list of ids */
static int c_lk_annot_by_layer(lcl_interp *interp, int argc, lcl_value **argv,
                               lcl_value **out) {
  struct lcl_lk_annot *aw;
  lk_annot_query q;

  if (argc != 2) {
    lcl_set_error(interp,
                  "lk::annot_by_layer: expected 2 arguments (store, layer)");

    return LCL_RC_ERR;
  }

  aw = get_annot(interp, argv[0]);

  if (!aw) {
    return LCL_RC_ERR;
  }

  lk_annot_query_init(&q);
  lk_annot_by_layer(aw->store, lcl_value_to_string(argv[1]), &q);
  *out = annot_query_to_list(&q);
  lk_annot_query_free(&q);

  return LCL_RC_OK;
}

/* lk::annot_layer_register [store, name] -> "" */
static int c_lk_annot_layer_register(lcl_interp *interp, int argc,
                                     lcl_value **argv, lcl_value **out) {
  struct lcl_lk_annot *aw;

  if (argc != 2) {
    lcl_set_error(
        interp, "lk::annot_layer_register: expected 2 arguments (store, name)");

    return LCL_RC_ERR;
  }

  aw = get_annot(interp, argv[0]);

  if (!aw) {
    return LCL_RC_ERR;
  }

  lk_annot_register_layer(aw->store, lcl_value_to_string(argv[1]));
  *out = lcl_string_new("");

  return LCL_RC_OK;
}

/* lk::annot_layer_priority [store, layer, priority] -> ""
 * Presentation precedence for a layer (default 0; higher wins).
 * Auto-registers the layer. */
static int c_lk_annot_layer_priority(lcl_interp *interp, int argc,
                                     lcl_value **argv, lcl_value **out) {
  struct lcl_lk_annot *aw;
  long prio;

  if (argc != 3) {
    lcl_set_error(interp,
                  "lk::annot_layer_priority: expected 3 arguments "
                  "(store, layer, priority)");

    return LCL_RC_ERR;
  }

  aw = get_annot(interp, argv[0]);

  if (!aw) {
    return LCL_RC_ERR;
  }

  if (lcl_value_to_int(argv[2], &prio) != LCL_OK) {
    lcl_set_error(interp,
                  "lk::annot_layer_priority: priority must be an integer");

    return LCL_RC_ERR;
  }

  lk_annot_layer_set_priority(aw->store, lcl_value_to_string(argv[1]),
                              (lk_i32)prio);
  *out = lcl_string_new("");

  return LCL_RC_OK;
}

/* ============================================================================
 * Range presentations (weft-surface track, S1)
 * ============================================================================
 */

/* The store's release hook: fires when a presentation value detaches.
 * For lcl-value resources this releases the registration and the Lcl
 * retain; a ui that was already destroyed explicitly is skipped (the
 * table died with it — same live_ui discipline as editors). */
static void lcl_annot_pres_release(void *ud, lk_value v) {
  struct lcl_lk_annot *aw = (struct lcl_lk_annot *)ud;
  lk_resource_ref ref = lk_v_resource_ref(v);
  struct lcl_pres_box *box;

  if (ref.id == 0 || !aw->pres_ui || !live_ui_check(aw->pres_ui)) {
    return;
  }

  box = (struct lcl_pres_box *)lk_resource_get(lk_ui_resources(aw->pres_ui),
                                               ref, &g_lcl_value_type);

  if (!box) {
    return;
  }

  lk_resource_release(lk_ui_resources(aw->pres_ui), ref);
  lcl_ref_dec(box->val);
  free(box);
}

/* lk::annot_present [ui, store, id, ptype, value] -> ""
 *
 * Attaches a presentation to annotation id: ptype is interned in the
 * ui's table; value may be ANY Lcl value (dicts, closures, ...) — it
 * is retained and registered in the ui's resource table under the
 * "lcl-value" type, and the record carries the UIV_RESOURCE ref.
 * Handlers receive the value back intact (command_to_dict unwraps).
 * A store's presentations are bound to ONE ui (the first call's);
 * replacing / removing / clearing / destroying releases the retain
 * through the store's release hook, installed here on first use. */
static int c_lk_annot_present(lcl_interp *interp, int argc, lcl_value **argv,
                              lcl_value **out) {
  lk_ui *ui = NULL;
  struct lcl_lk_annot *aw;
  long id;
  const char *ptype_str;
  struct lcl_pres_box *box;
  lk_resource_ref ref;
  lk_u32 type_id;

  if (argc != 5) {
    lcl_set_error(interp,
                  "lk::annot_present: expected 5 arguments "
                  "(ui, store, id, ptype, value)");

    return LCL_RC_ERR;
  }

  if (lcl_opaque_get(argv[0], LK_UI_TYPE, (void **)&ui) != LCL_OK || !ui) {
    lcl_set_error(interp, "lk::annot_present: expected lk_ui opaque");

    return LCL_RC_ERR;
  }

  aw = get_annot(interp, argv[1]);

  if (!aw) {
    return LCL_RC_ERR;
  }

  if (aw->pres_ui && aw->pres_ui != ui) {
    lcl_set_error(interp,
                  "lk::annot_present: this store's presentations are bound "
                  "to a different ui");

    return LCL_RC_ERR;
  }

  if (lcl_value_to_int(argv[2], &id) != LCL_OK || id <= 0) {
    lcl_set_error(interp, "lk::annot_present: id must be a positive integer");

    return LCL_RC_ERR;
  }

  ptype_str = lcl_value_to_string(argv[3]);

  if (ptype_str[0] == '\0') {
    lcl_set_error(interp, "lk::annot_present: ptype must be non-empty");

    return LCL_RC_ERR;
  }

  if (!lk_annot_get(aw->store, (lk_u32)id)) {
    lcl_set_error(interp, "lk::annot_present: no such annotation");

    return LCL_RC_ERR;
  }

  box = (struct lcl_pres_box *)malloc(sizeof(*box));

  if (!box) {
    lcl_set_error(interp, "lk::annot_present: allocation failed");

    return LCL_RC_ERR;
  }

  box->val = lcl_ref_inc(argv[4]);
  ref = lk_resource_register(lk_ui_resources(ui), &g_lcl_value_type, box,
                             "lcl-value");

  if (ref.id == 0) {
    lcl_ref_dec(box->val);
    free(box);
    lcl_set_error(interp, "lk::annot_present: resource registration failed");

    return LCL_RC_ERR;
  }

  /* First use binds the store to this ui and installs the hook. */
  if (!aw->pres_ui) {
    aw->pres_ui = ui;
    aw->ui_val = lcl_ref_inc(argv[0]);
    lk_annot_set_present_release(aw->store, lcl_annot_pres_release, aw);
  }

  type_id = lk_intern_cid(ui->intern, ptype_str);

  if (!lk_annot_set_present(aw->store, (lk_u32)id, type_id,
                            lk_v_resource(ref))) {
    lk_resource_release(lk_ui_resources(ui), ref);
    lcl_ref_dec(box->val);
    free(box);
    lcl_set_error(interp, "lk::annot_present: set_present failed");

    return LCL_RC_ERR;
  }

  *out = lcl_string_new("");

  return LCL_RC_OK;
}

/* lk::editor_pos_at [editor, x, y] -> byte position, or -1 when the
 * point is outside the editor's laid-out rect or no layout snapshot
 * exists (docs/weft-surface.md section 1.5 pinned contract). */
static int c_lk_editor_pos_at(lcl_interp *interp, int argc, lcl_value **argv,
                              lcl_value **out) {
  struct lcl_lk_editor *ew;
  long x;
  long y;
  lk_u32 pos = 0;

  if (argc != 3) {
    lcl_set_error(interp,
                  "lk::editor_pos_at: expected 3 arguments (editor, x, y)");

    return LCL_RC_ERR;
  }

  ew = get_editor(interp, argv[0]);

  if (!ew) {
    return LCL_RC_ERR;
  }

  if (lcl_value_to_int(argv[1], &x) != LCL_OK ||
      lcl_value_to_int(argv[2], &y) != LCL_OK) {
    lcl_set_error(interp, "lk::editor_pos_at: x and y must be integers");

    return LCL_RC_ERR;
  }

  if (lk_editor_pos_at(ew->ed, (lk_i32)x, (lk_i32)y, &pos)) {
    *out = lcl_int_new((long)pos);
  } else {
    *out = lcl_int_new(-1);
  }

  return LCL_RC_OK;
}

/* lk::editor_presentations [editor, store] -> ""
 * Installs the store's presentation source on the editor (the annot
 * adapter).  The editor wrapper retains the store value so the source
 * can never dangle. */
static int c_lk_editor_presentations(lcl_interp *interp, int argc,
                                     lcl_value **argv, lcl_value **out) {
  struct lcl_lk_editor *ew;
  struct lcl_lk_annot *aw;
  lk_presentation_source src;

  if (argc != 2) {
    lcl_set_error(interp,
                  "lk::editor_presentations: expected 2 arguments "
                  "(editor, store)");

    return LCL_RC_ERR;
  }

  ew = get_editor(interp, argv[0]);

  if (!ew) {
    return LCL_RC_ERR;
  }

  aw = get_annot(interp, argv[1]);

  if (!aw) {
    return LCL_RC_ERR;
  }

  src = lk_annot_presentation_source(aw->store);
  lk_editor_set_presentation_source(ew->ed, &src);

  if (ew->annot_val) {
    lcl_ref_dec(ew->annot_val);
  }

  ew->annot_val = lcl_ref_inc(argv[1]);

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
  lcl_ns_def(ns, "overlay_push",
             lcl_c_proc_new("lk::overlay_push", c_lk_overlay_push));
  lcl_ns_def(ns, "overlay_pop",
             lcl_c_proc_new("lk::overlay_pop", c_lk_overlay_pop));

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

  /* Documents (editor track) */
  lcl_ns_def(ns, "doc_new", lcl_c_proc_new("lk::doc_new", c_lk_doc_new));
  lcl_ns_def(ns, "doc_text", lcl_c_proc_new("lk::doc_text", c_lk_doc_text));
  lcl_ns_def(ns, "doc_len", lcl_c_proc_new("lk::doc_len", c_lk_doc_len));
  lcl_ns_def(ns, "doc_line_count",
             lcl_c_proc_new("lk::doc_line_count", c_lk_doc_line_count));
  lcl_ns_def(ns, "doc_pos_to_line",
             lcl_c_proc_new("lk::doc_pos_to_line", c_lk_doc_pos_to_line));
  lcl_ns_def(ns, "doc_line_start",
             lcl_c_proc_new("lk::doc_line_start", c_lk_doc_line_start));
  lcl_ns_def(ns, "doc_line_end",
             lcl_c_proc_new("lk::doc_line_end", c_lk_doc_line_end));
  lcl_ns_def(ns, "doc_char_col",
             lcl_c_proc_new("lk::doc_char_col", c_lk_doc_char_col));
  lcl_ns_def(ns, "doc_find", lcl_c_proc_new("lk::doc_find", c_lk_doc_find));
  lcl_ns_def(ns, "doc_revision",
             lcl_c_proc_new("lk::doc_revision", c_lk_doc_revision));
  lcl_ns_def(ns, "doc_insert",
             lcl_c_proc_new("lk::doc_insert", c_lk_doc_insert));
  lcl_ns_def(ns, "doc_delete",
             lcl_c_proc_new("lk::doc_delete", c_lk_doc_delete));
  lcl_ns_def(ns, "doc_transact",
             lcl_c_proc_new("lk::doc_transact", c_lk_doc_transact));
  lcl_ns_def(ns, "doc_subscribe",
             lcl_c_proc_new("lk::doc_subscribe", c_lk_doc_subscribe));
  lcl_ns_def(ns, "doc_unsubscribe",
             lcl_c_proc_new("lk::doc_unsubscribe", c_lk_doc_unsubscribe));

  /* Edit history */
  lcl_ns_def(ns, "history_new",
             lcl_c_proc_new("lk::history_new", c_lk_history_new));
  lcl_ns_def(ns, "history_undo",
             lcl_c_proc_new("lk::history_undo", c_lk_history_undo));
  lcl_ns_def(ns, "history_redo",
             lcl_c_proc_new("lk::history_redo", c_lk_history_redo));
  lcl_ns_def(ns, "history_can_undo",
             lcl_c_proc_new("lk::history_can_undo", c_lk_history_can_undo));
  lcl_ns_def(ns, "history_can_redo",
             lcl_c_proc_new("lk::history_can_redo", c_lk_history_can_redo));

  /* Editors */
  lcl_ns_def(ns, "editor_new",
             lcl_c_proc_new("lk::editor_new", c_lk_editor_new));
  lcl_ns_def(ns, "editor_cursor",
             lcl_c_proc_new("lk::editor_cursor", c_lk_editor_cursor));
  lcl_ns_def(ns, "editor_set_cursor",
             lcl_c_proc_new("lk::editor_set_cursor", c_lk_editor_set_cursor));
  lcl_ns_def(ns, "editor_selection",
             lcl_c_proc_new("lk::editor_selection", c_lk_editor_selection));
  lcl_ns_def(ns, "editor_wrap",
             lcl_c_proc_new("lk::editor_wrap", c_lk_editor_wrap));
  lcl_ns_def(ns, "editor_wrap_get",
             lcl_c_proc_new("lk::editor_wrap_get", c_lk_editor_wrap_get));
  lcl_ns_def(ns, "editor_command",
             lcl_c_proc_new("lk::editor_command", c_lk_editor_command));
  lcl_ns_def(ns, "editor_set_spans",
             lcl_c_proc_new("lk::editor_set_spans", c_lk_editor_set_spans));

  /* Annotation stores */
  lcl_ns_def(ns, "annot_store_new",
             lcl_c_proc_new("lk::annot_store_new", c_lk_annot_store_new));
  lcl_ns_def(ns, "annot_attach",
             lcl_c_proc_new("lk::annot_attach", c_lk_annot_attach));
  lcl_ns_def(ns, "annot_add", lcl_c_proc_new("lk::annot_add", c_lk_annot_add));
  lcl_ns_def(ns, "annot_remove",
             lcl_c_proc_new("lk::annot_remove", c_lk_annot_remove));
  lcl_ns_def(ns, "annot_span",
             lcl_c_proc_new("lk::annot_span", c_lk_annot_span));
  lcl_ns_def(ns, "annot_meta",
             lcl_c_proc_new("lk::annot_meta", c_lk_annot_meta));
  lcl_ns_def(ns, "annot_in_range",
             lcl_c_proc_new("lk::annot_in_range", c_lk_annot_in_range));
  lcl_ns_def(ns, "annot_at", lcl_c_proc_new("lk::annot_at", c_lk_annot_at));
  lcl_ns_def(ns, "annot_by_layer",
             lcl_c_proc_new("lk::annot_by_layer", c_lk_annot_by_layer));
  lcl_ns_def(ns, "annot_layer_register",
             lcl_c_proc_new("lk::annot_layer_register",
                            c_lk_annot_layer_register));
  lcl_ns_def(ns, "annot_layer_priority",
             lcl_c_proc_new("lk::annot_layer_priority",
                            c_lk_annot_layer_priority));

  /* Range presentations (weft-surface S1) */
  lcl_ns_def(ns, "annot_present",
             lcl_c_proc_new("lk::annot_present", c_lk_annot_present));
  lcl_ns_def(ns, "editor_pos_at",
             lcl_c_proc_new("lk::editor_pos_at", c_lk_editor_pos_at));
  lcl_ns_def(ns, "editor_presentations",
             lcl_c_proc_new("lk::editor_presentations",
                            c_lk_editor_presentations));

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
