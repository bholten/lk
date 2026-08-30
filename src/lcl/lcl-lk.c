/*
 * lcl-lk.c — Lcl scripting bindings for lk (Layer 1).
 *
 * Exposes 80 procs in the "Lk" namespace (74 core + 6 SDL) for
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
#define LK_IMAGE_TYPE "lk_image"
#define LK_SPANS_TYPE "lk_spans"
#define LK_CANVAS_TYPE "lk_canvas"

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
    {"checkbox",   UIK_CHECKBOX  },
    {"radio",      UIK_RADIO     },
    {"slider",     UIK_SLIDER    },
    {"tabs",       UIK_TABS      },
    {"tab",        UIK_TAB       },
    {"grid",       UIK_GRID      },
    {"image",      UIK_IMAGE     },
    {"canvas",     UIK_CANVAS    },
    {"styled_text", UIK_STYLED_TEXT},
    {"list",        UIK_LIST       },
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
    {"split_controlled", UIP_CONTROLLED}, /* legacy name */
    {"controlled",  UIP_CONTROLLED },
    {"text_align",  UIP_TEXT_ALIGN },
    {"text_valign", UIP_TEXT_VALIGN},
    {"editor",      UIP_EDITOR     },
    {"grow",        UIP_GROW       },
    {"value",       UIP_VALUE      },
    {"checked",     UIP_CHECKED    },
    {"min",         UIP_MIN        },
    {"max",         UIP_MAX        },
    {"step",        UIP_STEP       },
    {"columns",     UIP_COLUMNS    },
    {"image",       UIP_IMAGE      },
    {"filter",      UIP_FILTER     },
    {"canvas",      UIP_CANVAS     },
    {"spans",       UIP_SPANS      },
    {"wrap",        UIP_WRAP       },
    {"rows",        UIP_ROWS       },
    {"row_h",       UIP_ROW_H      },
    {"row",         UIP_ROW        },
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
    {"add_cursor_at",        LK_ED_ADD_CURSOR_AT       },
    {"add_cursor_above",     LK_ED_ADD_CURSOR_ABOVE    },
    {"add_cursor_below",     LK_ED_ADD_CURSOR_BELOW    },
    {"select_next_match",    LK_ED_SELECT_NEXT_MATCH   },
    {"collapse_cursors",     LK_ED_COLLAPSE_CURSORS    },
    {NULL,                   0                         }
};

/* Wrap-mode names, mirroring lk_editor_wrap_mode. */
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

/* UIP_WRAP values (lk_wrap_mode; the editor's own wrap_mode_table maps
 * the same names onto LK_EDITOR_WRAP_*). */
static const str_enum st_wrap_table[] = {
    {"none",      LK_WRAP_NONE     },
    {"character", LK_WRAP_CHARACTER},
    {"word",      LK_WRAP_WORD     },
    {NULL,        0                }
};

static const str_enum align_table[] = {
    {"start",   LK_ALIGN_START  },
    {"center",  LK_ALIGN_CENTER },
    {"end",     LK_ALIGN_END    },
    {"stretch", LK_ALIGN_STRETCH},
    {NULL,      0               }
};

/* UIP_FILTER values (lk_image_filter). */
static const str_enum filter_table[] = {
    {"linear",  LK_FILTER_LINEAR },
    {"nearest", LK_FILTER_NEAREST},
    {NULL,      0                }
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

/* Human-readable lcl_type name for error messages. */
static const char *lcl_type_name(lcl_type t) {
  switch (t) {
  case LCL_STRING:    return "string";
  case LCL_INT:       return "int";
  case LCL_FLOAT:     return "float";
  case LCL_LIST:      return "list";
  case LCL_DICT:      return "dict";
  case LCL_CELL:      return "cell";
  case LCL_PROC:      return "proc";
  case LCL_CPROC:     return "proc";
  case LCL_NAMESPACE: return "namespace";
  case LCL_OPAQUE:    return "opaque";
  default:            return "value";
  }
}

/* Fetch a name-shaped argument (node id, tag, ptype, command name,
 * layer, meta key).  Strings pass through and typed numbers render (a
 * counter is a fine id), but structured values — list, dict, proc,
 * opaque — are hard errors: rendering those would silently turn a
 * wrong-variable bug into a misnamed node.  On error sets the interp
 * error ("<what> must be a string or number, got <type>") and returns
 * NULL.  `what` names the proc and operand, e.g. "Lk::node: id". */
static const char *arg_name(lcl_interp *interp, lcl_value *v,
                            const char *what) {
  lcl_type ty = lcl_value_type_of(v);
  const char *s;

  if (ty == LCL_STRING || ty == LCL_INT || ty == LCL_FLOAT) {
    s = lcl_value_to_string(v);

    if (!s) {
      lcl_set_error(interp, "out of memory");
    }

    return s;
  }

  {
    char err[128];

    sprintf(err, "%.64s must be a string or number, got %s", what,
            lcl_type_name(ty));
    lcl_set_error(interp, err);
  }

  return NULL;
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

/* One script-side document subscription (Lk::doc_subscribe). */
struct lcl_doc_sub {
  lk_u32 id;
  lcl_interp *interp;
  lcl_value *handler; /* retained */
  struct lcl_doc_sub *next;
};

struct lcl_lk_doc {
  lk_document *doc;
  struct lcl_doc_sub *subs; /* live Lk::doc_subscribe bridges */
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
                           (Lk::editor_presentations), NULL before */
};

struct lcl_lk_annot {
  lk_annot_store *store;
  lcl_value *doc_val; /* retained once attached, NULL before */
  lk_ui *pres_ui;     /* ui whose resource table holds this store's
                         presentation values; set by the first
                         Lk::annot_present, NULL before */
  lcl_value *ui_val;  /* retained alongside pres_ui */
};

/* Image track: Lk::image_new / image_load register the lk_image in
 * the ui's resource table (the editor wrapper scheme). */
struct lcl_lk_image {
  lk_image *img;
  lk_ui *ui;           /* for resource release */
  lk_resource_ref ref; /* registration in the ui's resource table */
  lcl_value *ui_val;   /* retained */
};

/* Vector canvas (docs/canvas.md): Lk::canvas_new registers the
 * display list in the ui's resource table -- the image wrapper scheme
 * verbatim. */
/* Span set (docs/styled-text.md): Lk::spans_new registers the set in
 * the ui's resource table -- the image wrapper scheme -- and installs
 * the release hook that frees the lcl-value boxes its presentations
 * carry. */
struct lcl_lk_spans {
  lk_spans *sp;
  lk_ui *ui;
  lk_resource_ref ref;
  lcl_value *ui_val; /* retained */
};

struct lcl_lk_canvas {
  lk_canvas *cv;
  lk_ui *ui;
  lk_resource_ref ref;
  lcl_value *ui_val; /* retained */
};

/* ---- "lcl-value" resources ----
 *
 * Lk::annot_present wraps ANY Lcl value as a presentation value: the
 * value is retained in a box registered in the ui's resource table
 * under this type, and the annotation carries the UIV_RESOURCE ref.
 * command_to_dict unwraps refs of this type back to the retained Lcl
 * value, so handlers receive dicts/closures/whatever intact.  The
 * store's release hook releases the registration + retain when the
 * presentation detaches.
 *
 * The resource table does not own the boxes, so the binding keeps its
 * own list of every live box: when a ui is torn down before the
 * store's finalizer runs (the DSL's `app` destroys its window
 * explicitly), the release hook can no longer reach the table and the
 * boxes would leak -- ui teardown sweeps them instead. */

static const lk_resource_type g_lcl_value_type = {"lcl-value", NULL};

struct lcl_pres_box {
  lcl_value *val; /* retained */
  lk_ui *ui;      /* the table this box is registered in */
  struct lcl_pres_box *prev;
  struct lcl_pres_box *next;
};

static struct lcl_pres_box *g_pres_boxes = NULL;

/* Test hook (not in lcl-lk.h; lcl_lk_test declares it extern): the
 * number of live boxes.  The list above makes the boxes reachable, so
 * LSan cannot see a missed sweep -- the count can. */
int lcl_lk_debug_pres_boxes(void) {
  struct lcl_pres_box *box = g_pres_boxes;
  int n = 0;

  while (box) {
    n++;
    box = box->next;
  }

  return n;
}

static void pres_box_link(struct lcl_pres_box *box) {
  box->prev = NULL;
  box->next = g_pres_boxes;

  if (g_pres_boxes) {
    g_pres_boxes->prev = box;
  }

  g_pres_boxes = box;
}

/* Unlink, drop the Lcl retain, free.  Never touches the resource
 * table -- callers release the registration while the ui is live. */
static void pres_box_free(struct lcl_pres_box *box) {
  if (box->prev) {
    box->prev->next = box->next;
  } else {
    g_pres_boxes = box->next;
  }

  if (box->next) {
    box->next->prev = box->prev;
  }

  lcl_ref_dec(box->val);
  free(box);
}

/* Free every box registered in `ui`'s table (the table dies with the
 * ui; the store's later release hook sees a dead ui and skips). */
static void pres_boxes_sweep(lk_ui *ui) {
  struct lcl_pres_box *box = g_pres_boxes;

  while (box) {
    struct lcl_pres_box *next = box->next;

    if (box->ui == ui) {
      pres_box_free(box);
    }

    box = next;
  }
}

/* ---- Live-ui registry ----
 *
 * Value retention cannot cover one case: a ui destroyed explicitly
 * (Lk::ui_destroy, Lk::window_destroy, or the window finalizer for
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

/* Everything the binding hung on `ui` that must go before the ui
 * dies: the command-handler ctx (retained handler value + record),
 * the lcl-value presentation boxes registered in its resource table,
 * and the live-registry entry.  Every teardown path -- the ui/window
 * finalizers AND the explicit Lk::ui_destroy / Lk::window_destroy
 * procs -- calls this; the DSL's `app` destroys its window
 * explicitly, so a finalizer-only release leaked the handler lambda
 * and every presented value of every app. */
static void ui_teardown_bindings(lk_ui *ui) {
  if (!ui) {
    return;
  }

  if (ui->cmd_handler == lcl_cmd_bridge && ui->cmd_handler_ud) {
    lcl_cmd_ctx_free((struct lcl_cmd_ctx *)ui->cmd_handler_ud);
    ui->cmd_handler = NULL;
    ui->cmd_handler_ud = NULL;
  }

  pres_boxes_sweep(ui);
  live_ui_remove(ui);
}

static void ui_finalizer(void *ptr) {
  lk_ui *ui = (lk_ui *)ptr;

  /* Explicit Lk::ui_destroy / window teardown already removed the ui
   * from the live registry and freed it; touching it again here would
   * be a use-after-free. */
  if (!live_ui_check(ui)) {
    return;
  }

  ui_teardown_bindings(ui);
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

static void image_finalizer(void *ptr) {
  struct lcl_lk_image *iw = (struct lcl_lk_image *)ptr;

  if (live_ui_check(iw->ui)) {
    lk_resource_release(lk_ui_resources(iw->ui), iw->ref);
  }

  lk_image_destroy(iw->img);
  lcl_ref_dec(iw->ui_val);
  free(iw);
}

static void spans_finalizer(void *ptr) {
  struct lcl_lk_spans *sw = (struct lcl_lk_spans *)ptr;

  /* destroy first: its release hook frees the presentation boxes while
   * the ui (if still alive) can still be consulted */
  lk_spans_destroy(sw->sp);

  if (live_ui_check(sw->ui)) {
    lk_resource_release(lk_ui_resources(sw->ui), sw->ref);
  }

  lcl_ref_dec(sw->ui_val);
  free(sw);
}

static void canvas_finalizer(void *ptr) {
  struct lcl_lk_canvas *cw = (struct lcl_lk_canvas *)ptr;

  if (live_ui_check(cw->ui)) {
    lk_resource_release(lk_ui_resources(cw->ui), cw->ref);
  }

  lk_canvas_destroy(cw->cv);
  lcl_ref_dec(cw->ui_val);
  free(cw);
}

/* ============================================================================
 * UI Lifecycle procs (5)
 * ============================================================================
 */

/* Lk::ui_create -> opaque<lk_ui> */
static lcl_return_code c_lk_ui_create(lcl_interp *interp, int argc,
                                      lcl_value **argv, lcl_value **out) {
  lk_ui *ui;
  (void)interp;
  (void)argv;

  if (argc != 0) {
    lcl_set_error(interp, "Lk::ui_create: expected 0 arguments");

    return LCL_RC_ERR;
  }

  ui = lk_ui_create(NULL);

  if (!ui) {
    lcl_set_error(interp, "Lk::ui_create: allocation failed");

    return LCL_RC_ERR;
  }

  live_ui_add(ui);
  *out = lcl_opaque_new(ui, LK_UI_TYPE, ui_finalizer);

  return LCL_RC_OK;
}

/* Lk::ui_destroy [ui] -> "" */
static lcl_return_code c_lk_ui_destroy(lcl_interp *interp, int argc,
                                       lcl_value **argv, lcl_value **out) {
  lk_ui *ui;

  if (argc != 1) {
    lcl_set_error(interp, "Lk::ui_destroy: expected 1 argument");

    return LCL_RC_ERR;
  }

  if (lcl_opaque_get(argv[0], LK_UI_TYPE, (void **)&ui) != LCL_OK) {
    lcl_set_error(interp, "Lk::ui_destroy: expected lk_ui opaque");

    return LCL_RC_ERR;
  }

  ui_teardown_bindings(ui);
  lk_ui_destroy(ui);

  /* The opaque still points at the freed ui, but its finalizer checks
   * the live registry and no-ops for uis already destroyed here. */
  *out = lcl_string_new("");

  return LCL_RC_OK;
}

/* Lk::begin_frame [ui] -> opaque<lk_tree> */
static lcl_return_code c_lk_begin_frame(lcl_interp *interp, int argc,
                                        lcl_value **argv, lcl_value **out) {
  lk_ui *ui;
  lk_tree *t;

  if (argc != 1) {
    lcl_set_error(interp, "Lk::begin_frame: expected 1 argument");

    return LCL_RC_ERR;
  }

  if (lcl_opaque_get(argv[0], LK_UI_TYPE, (void **)&ui) != LCL_OK) {
    lcl_set_error(interp, "Lk::begin_frame: expected lk_ui opaque");

    return LCL_RC_ERR;
  }

  t = lk_ui_begin_frame(ui);

  /* Tree is owned by ui, no finalizer */
  *out = lcl_opaque_new(t, LK_TREE_TYPE, NULL);

  return LCL_RC_OK;
}

/* Lk::end_frame [ui] -> list of change dicts */
static lcl_return_code c_lk_end_frame(lcl_interp *interp, int argc,
                                      lcl_value **argv, lcl_value **out) {
  lk_ui *ui;
  const lk_changeset *cs;
  lcl_value *list;
  lk_u32 i;

  if (argc != 1) {
    lcl_set_error(interp, "Lk::end_frame: expected 1 argument");

    return LCL_RC_ERR;
  }

  if (lcl_opaque_get(argv[0], LK_UI_TYPE, (void **)&ui) != LCL_OK) {
    lcl_set_error(interp, "Lk::end_frame: expected lk_ui opaque");

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

    v = lcl_int_new((lcl_int)ch->id);
    lcl_dict_put(&dict, "id", v);
    lcl_ref_dec(v);

    v = lcl_int_new((lcl_int)ch->node_ix);
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

/* Lk::tree [ui] -> opaque<lk_tree> (current tree) */
static lcl_return_code c_lk_tree(lcl_interp *interp, int argc, lcl_value **argv,
                                 lcl_value **out) {
  lk_ui *ui;
  const lk_tree *t;

  if (argc != 1) {
    lcl_set_error(interp, "Lk::tree: expected 1 argument");

    return LCL_RC_ERR;
  }

  if (lcl_opaque_get(argv[0], LK_UI_TYPE, (void **)&ui) != LCL_OK) {
    lcl_set_error(interp, "Lk::tree: expected lk_ui opaque");

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

/* Lk::node [tree, id_str, kind_str] -> int (node ix) */
static lcl_return_code c_lk_node(lcl_interp *interp, int argc, lcl_value **argv,
                                 lcl_value **out) {
  lk_tree *t;
  const char *id_str;
  const char *kind_str;
  int kind_val;
  lk_ix ix;

  if (argc != 3) {
    lcl_set_error(interp, "Lk::node: expected 3 arguments (tree, id, kind)");

    return LCL_RC_ERR;
  }

  if (lcl_opaque_get(argv[0], LK_TREE_TYPE, (void **)&t) != LCL_OK) {
    lcl_set_error(interp, "Lk::node: expected lk_tree opaque");

    return LCL_RC_ERR;
  }

  id_str = arg_name(interp, argv[1], "Lk::node: id");

  if (!id_str) {
    return LCL_RC_ERR;
  }

  kind_str = lcl_value_to_string(argv[2]);

  if (!lookup_enum(kind_table, kind_str, &kind_val)) {
    lcl_set_error(interp, "Lk::node: unknown kind");

    return LCL_RC_ERR;
  }

  ix = lk_tree_add_node_c(t, id_str, (lk_kind)kind_val);

  *out = lcl_int_new((lcl_int)ix);

  return LCL_RC_OK;
}

/* Lk::set_root [tree, node_ix] -> "" */
static lcl_return_code c_lk_set_root(lcl_interp *interp, int argc,
                                     lcl_value **argv, lcl_value **out) {
  lk_tree *t;
  lcl_int ix;

  if (argc != 2) {
    lcl_set_error(interp, "Lk::set_root: expected 2 arguments");

    return LCL_RC_ERR;
  }

  if (lcl_opaque_get(argv[0], LK_TREE_TYPE, (void **)&t) != LCL_OK) {
    lcl_set_error(interp, "Lk::set_root: expected lk_tree opaque");

    return LCL_RC_ERR;
  }

  if (lcl_value_to_int(argv[1], &ix) != LCL_OK) {
    lcl_set_error(interp, "Lk::set_root: node_ix must be an integer");

    return LCL_RC_ERR;
  }

  lk_tree_set_root(t, (lk_ix)ix);

  *out = lcl_string_new("");

  return LCL_RC_OK;
}

/* Lk::append_child [tree, parent_ix, child_ix] -> "" */
static lcl_return_code c_lk_append_child(lcl_interp *interp, int argc,
                                         lcl_value **argv, lcl_value **out) {
  lk_tree *t;
  lcl_int parent_ix, child_ix;

  if (argc != 3) {
    lcl_set_error(interp, "Lk::append_child: expected 3 arguments");

    return LCL_RC_ERR;
  }

  if (lcl_opaque_get(argv[0], LK_TREE_TYPE, (void **)&t) != LCL_OK) {
    lcl_set_error(interp, "Lk::append_child: expected lk_tree opaque");

    return LCL_RC_ERR;
  }

  if (lcl_value_to_int(argv[1], &parent_ix) != LCL_OK) {
    lcl_set_error(interp, "Lk::append_child: parent_ix must be an integer");

    return LCL_RC_ERR;
  }

  if (lcl_value_to_int(argv[2], &child_ix) != LCL_OK) {
    lcl_set_error(interp, "Lk::append_child: child_ix must be an integer");

    return LCL_RC_ERR;
  }

  lk_tree_append_child(t, (lk_ix)parent_ix, (lk_ix)child_ix);

  *out = lcl_string_new("");

  return LCL_RC_OK;
}

/* Lk::prop [tree, node_ix, key_str, value] -> "" */
static lcl_return_code c_lk_prop(lcl_interp *interp, int argc, lcl_value **argv,
                                 lcl_value **out) {
  lk_tree *t;
  lcl_int node_ix;
  const char *key_str;
  int key_val;
  lk_value lv;

  if (argc != 4) {
    lcl_set_error(interp,
                  "Lk::prop: expected 4 arguments (tree, node, key, value)");

    return LCL_RC_ERR;
  }

  if (lcl_opaque_get(argv[0], LK_TREE_TYPE, (void **)&t) != LCL_OK) {
    lcl_set_error(interp, "Lk::prop: expected lk_tree opaque");

    return LCL_RC_ERR;
  }

  if (lcl_value_to_int(argv[1], &node_ix) != LCL_OK) {
    lcl_set_error(interp, "Lk::prop: node_ix must be an integer");

    return LCL_RC_ERR;
  }

  key_str = lcl_value_to_string(argv[2]);

  if (!lookup_enum(prop_table, key_str, &key_val)) {
    lcl_set_error(interp, "Lk::prop: unknown prop key");

    return LCL_RC_ERR;
  }

  /* Value coercion based on prop key */
  switch (key_val) {
  case UIP_TEXT:
  case UIP_TOOLTIP:
    lv = lk_v_cstr(t->intern, lcl_value_to_string(argv[3]));
    break;
  case UIP_VALUE:
    /* Kind-dispatched: a slider's value and a list's value (its
     * cursor row) are integers (numeric text accepted); every other
     * kind's value is a string (dropdown option text, tabs tab id). */
    if (node_ix > 0 && (lk_u32)node_ix < t->node_count &&
        ((lk_kind)t->nodes[node_ix].kind == UIK_SLIDER ||
         (lk_kind)t->nodes[node_ix].kind == UIK_LIST)) {
      lcl_int i;
      if (lcl_value_to_int(argv[3], &i) != LCL_OK) {
        lcl_set_error(interp,
                      (lk_kind)t->nodes[node_ix].kind == UIK_SLIDER
                          ? "Lk::prop: slider value expects an integer"
                          : "Lk::prop: list value expects an integer");
        return LCL_RC_ERR;
      }
      lv = lk_v_i32((lk_i32)i);
    } else {
      lv = lk_v_cstr(t->intern, lcl_value_to_string(argv[3]));
    }
    break;
  case UIP_FOCUSABLE:
  case UIP_DISABLED:
  case UIP_CHECKED:
  case UIP_HIDDEN: {
    lcl_int b;
    if (lcl_value_to_int(argv[3], &b) != LCL_OK) {
      lcl_set_error(interp, "Lk::prop: bool prop expects integer");
      return LCL_RC_ERR;
    }
    lv = lk_v_bool((int)b);
    break;
  }
  case UIP_W:
  case UIP_H:
  case UIP_PADDING:
  case UIP_GAP:
  case UIP_SPLIT_RATIO:
  case UIP_CONTROLLED:
  case UIP_MIN:
  case UIP_MAX: {
    lcl_int i;
    if (lcl_value_to_int(argv[3], &i) != LCL_OK) {
      lcl_set_error(interp, "Lk::prop: numeric prop expects integer");
      return LCL_RC_ERR;
    }
    lv = lk_v_i32((lk_i32)i);
    break;
  }
  case UIP_ROWS:
  case UIP_ROW: {
    lcl_int i;
    if (lcl_value_to_int(argv[3], &i) != LCL_OK || i < 0) {
      lcl_set_error(interp, key_val == UIP_ROWS
                                ? "Lk::prop: rows expects an integer >= 0"
                                : "Lk::prop: row expects an integer >= 0");
      return LCL_RC_ERR;
    }
    lv = lk_v_i32((lk_i32)i);
    break;
  }
  case UIP_ROW_H:
  case UIP_STEP:
  case UIP_COLUMNS: {
    lcl_int i;
    if (lcl_value_to_int(argv[3], &i) != LCL_OK || i < 1) {
      lcl_set_error(interp, key_val == UIP_STEP
                                ? "Lk::prop: step expects an integer >= 1"
                            : key_val == UIP_ROW_H
                                ? "Lk::prop: row_h expects an integer >= 1"
                                : "Lk::prop: columns expects an integer >= 1");
      return LCL_RC_ERR;
    }
    lv = lk_v_i32((lk_i32)i);
    break;
  }
  case UIP_GROW: {
    lcl_int i;
    if (lcl_value_to_int(argv[3], &i) != LCL_OK) {
      lcl_set_error(interp, "Lk::prop: grow expects an integer >= 0");
      return LCL_RC_ERR;
    }
    if (i < 0) {
      lcl_set_error(interp, "Lk::prop: grow must be >= 0 (weighted growth "
                            "has no negative weights)");
      return LCL_RC_ERR;
    }
    lv = lk_v_i32((lk_i32)i);
    break;
  }
  case UIP_ALIGN:
  case UIP_JUSTIFY:
  case UIP_TEXT_ALIGN:
  case UIP_TEXT_VALIGN: {
    const char *align_str = lcl_value_to_string(argv[3]);
    int align_val;
    if (!lookup_enum(align_table, align_str, &align_val)) {
      lcl_set_error(interp, "Lk::prop: unknown align value");
      return LCL_RC_ERR;
    }
    lv = lk_v_i32((lk_i32)align_val);
    break;
  }
  case UIP_FILTER: {
    const char *filter_str = lcl_value_to_string(argv[3]);
    int filter_val;
    if (!lookup_enum(filter_table, filter_str, &filter_val)) {
      lcl_set_error(interp, "Lk::prop: filter expects linear or nearest");
      return LCL_RC_ERR;
    }
    lv = lk_v_i32((lk_i32)filter_val);
    break;
  }
  case UIP_EDITOR: {
    struct lcl_lk_editor *ew = NULL;

    if (lcl_opaque_get(argv[3], LK_EDITOR_TYPE, (void **)&ew) != LCL_OK) {
      lcl_set_error(interp, "Lk::prop: editor prop expects an lk_editor opaque");
      return LCL_RC_ERR;
    }

    lv = lk_v_resource(ew->ref);
    break;
  }
  case UIP_IMAGE: {
    struct lcl_lk_image *iw = NULL;

    if (lcl_opaque_get(argv[3], LK_IMAGE_TYPE, (void **)&iw) != LCL_OK) {
      lcl_set_error(interp, "Lk::prop: image prop expects an lk_image opaque");
      return LCL_RC_ERR;
    }

    lv = lk_v_resource(iw->ref);
    break;
  }
  case UIP_SPANS: {
    struct lcl_lk_spans *sw = NULL;

    if (lcl_opaque_get(argv[3], LK_SPANS_TYPE, (void **)&sw) != LCL_OK) {
      lcl_set_error(interp, "Lk::prop: spans prop expects an lk_spans opaque");
      return LCL_RC_ERR;
    }

    lv = lk_v_resource(sw->ref);
    break;
  }
  case UIP_WRAP: {
    const char *ws = lcl_value_to_string(argv[3]);
    int wv;

    if (!lookup_enum(st_wrap_table, ws, &wv)) {
      lcl_set_error(interp,
                    "Lk::prop: wrap must be one of none, character, word");
      return LCL_RC_ERR;
    }

    lv = lk_v_i32((lk_i32)wv);
    break;
  }
  case UIP_CANVAS: {
    struct lcl_lk_canvas *cw = NULL;

    if (lcl_opaque_get(argv[3], LK_CANVAS_TYPE, (void **)&cw) != LCL_OK) {
      lcl_set_error(interp,
                    "Lk::prop: canvas prop expects an lk_canvas opaque");
      return LCL_RC_ERR;
    }

    lv = lk_v_resource(cw->ref);
    break;
  }
  default:
    lcl_set_error(interp, "Lk::prop: unsupported prop key");
    return LCL_RC_ERR;
  }

  lk_tree_add_prop(t, (lk_ix)node_ix, (lk_prop_key)key_val, lv);
  *out = lcl_string_new("");

  return LCL_RC_OK;
}

/* Coerce one Lcl value into an lk_value by tag: a string stays a
 * string (numeric text like "007" is data, not a number), a typed
 * number becomes i32, anything else renders to a string. */
static lk_value coerce_lk_value(lk_tree *t, lcl_value *v) {
  const char *sv;
  lcl_int iv;

  if (lcl_value_get_string(v, &sv) == LCL_OK) {
    return lk_v_cstr(t->intern, sv);
  }

  if (lcl_value_to_int(v, &iv) == LCL_OK) {
    return lk_v_i32((lk_i32)iv);
  }

  return lk_v_cstr(t->intern, lcl_value_to_string(v));
}

/* Lk::present [tree, node_ix, ptype_str, pvalue]
 *
 * pvalue may be a scalar (int/string) or a list; list elements become
 * the presentation's args, one-to-one with the emitted command's args.
 * Lists longer than LK_PRES_MAX_ARGS are truncated.
 */
static lcl_return_code c_lk_present(lcl_interp *interp, int argc,
                                    lcl_value **argv, lcl_value **out) {
  lk_tree *t;
  lcl_int node_ix;
  const char *ptype_str;
  lk_value pvs[LK_PRES_MAX_ARGS];
  lk_u8 count = 0;

  if (argc != 4) {
    lcl_set_error(interp, "Lk::present: expected 4 arguments");

    return LCL_RC_ERR;
  }

  if (lcl_opaque_get(argv[0], LK_TREE_TYPE, (void **)&t) != LCL_OK) {
    lcl_set_error(interp, "Lk::present: expected lk_tree opaque");

    return LCL_RC_ERR;
  }

  if (lcl_value_to_int(argv[1], &node_ix) != LCL_OK) {
    lcl_set_error(interp, "Lk::present: node_ix must be an integer");

    return LCL_RC_ERR;
  }

  ptype_str = arg_name(interp, argv[2], "Lk::present: ptype");

  if (!ptype_str) {
    return LCL_RC_ERR;
  }

  if (lcl_value_type_of(argv[3]) == LCL_LIST) {
    size_t n = lcl_list_len(argv[3]);
    size_t i;

    if (n > LK_PRES_MAX_ARGS) {
      n = LK_PRES_MAX_ARGS;
    }

    for (i = 0; i < n; i++) {
      lcl_value *elem = lcl_list_peek(argv[3], i);

      if (elem) {
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

/* Lk::add_translator [ui event_type ptype kind keycode mods cmd_name
 *                     ?button?]
 * All string fields: "" means any/wildcard.
 * keycode: "" or letter/name (e.g. "s", "f", "return").
 * mods: "" or "+"-joined modifiers (e.g. "ctrl", "ctrl+shift").
 * button (optional 8th arg): "primary" | "middle" | "secondary", or
 * "" / "0" for any.  When set, only pointer events with that button
 * and exact mods match. */
/* Lk::clear_translators [ui] -> "": drop every translator registered
 * on the ui (lk_ui_clear_translators).  The clear-and-re-register idiom
 * for keymaps that change at runtime.  Safe wherever script runs --
 * frame body, event handler, or inside a command handler: emission
 * copies the matched row into the command before the handler runs and
 * every matcher loop returns right after emitting, so neither a clear
 * nor a re-add (which may reallocate the table) is observed
 * mid-iteration.  Registrations made before the clear on the same
 * frame's events are simply gone for the next event. */
static lcl_return_code c_lk_clear_translators(lcl_interp *interp, int argc,
                                              lcl_value **argv,
                                              lcl_value **out) {
  lk_ui *ui;

  if (argc != 1) {
    lcl_set_error(interp, "Lk::clear_translators: expected 1 argument (ui)");

    return LCL_RC_ERR;
  }

  if (lcl_opaque_get(argv[0], LK_UI_TYPE, (void **)&ui) != LCL_OK) {
    lcl_set_error(interp, "Lk::clear_translators: expected lk_ui opaque");

    return LCL_RC_ERR;
  }

  lk_ui_clear_translators(ui);
  *out = lcl_string_new("");

  return LCL_RC_OK;
}

static lcl_return_code c_lk_add_translator(lcl_interp *interp, int argc,
                                           lcl_value **argv, lcl_value **out) {
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
    lcl_set_error(interp, "Lk::add_translator: expected 7 or 8 arguments");

    return LCL_RC_ERR;
  }

  if (lcl_opaque_get(argv[0], LK_UI_TYPE, (void **)&ui) != LCL_OK) {
    lcl_set_error(interp, "Lk::add_translator: expected lk_ui opaque");

    return LCL_RC_ERR;
  }

  ev_str = lcl_value_to_string(argv[1]);
  pt_str = arg_name(interp, argv[2], "Lk::add_translator: ptype");
  kn_str = lcl_value_to_string(argv[3]);
  kc_str = lcl_value_to_string(argv[4]);
  mod_str = lcl_value_to_string(argv[5]);
  cmd_str = arg_name(interp, argv[6], "Lk::add_translator: command name");

  if (!pt_str || !cmd_str) {
    return LCL_RC_ERR;
  }

  /* event_type: "" means 0 (any) */
  if (ev_str[0] != '\0') {
    int ev_val;
    if (!lookup_enum(event_table, ev_str, &ev_val)) {
      lcl_set_error(interp, "Lk::add_translator: unknown event type");

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
      lcl_set_error(interp, "Lk::add_translator: unknown kind");
      return LCL_RC_ERR;
    }
    node_kind = (lk_u16)kn_val;
  }

  /* keycode: "" means 0 (any) */
  if (kc_str[0] != '\0') {
    int kc_val;
    if (!lookup_enum(keycode_table, kc_str, &kc_val)) {
      lcl_set_error(interp, "Lk::add_translator: unknown keycode");
      return LCL_RC_ERR;
    }
    keycode = (lk_u16)kc_val;
  }

  /* mods: "" means 0 (any), or "ctrl+shift" etc. */
  if (mod_str[0] != '\0') {
    if (parse_mods(mod_str, &mods) != 0) {
      lcl_set_error(interp, "Lk::add_translator: unknown modifier");
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
                      "Lk::add_translator: unknown button (known: primary, "
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
  case UIV_I32: return lcl_int_new((lcl_int)v->as.i);
  case UIV_BOOL: return lcl_int_new((lcl_int)v->as.b);
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

    v = lcl_int_new((lcl_int)hit->locus[0]);
    lcl_dict_put(&locus, "annot_id", v);
    lcl_ref_dec(v);

    v = lcl_int_new((lcl_int)hit->locus[1]);
    lcl_dict_put(&locus, "start", v);
    lcl_ref_dec(v);

    v = lcl_int_new((lcl_int)hit->locus[2]);
    lcl_dict_put(&locus, "end", v);
    lcl_ref_dec(v);

    v = lcl_int_new((lcl_int)hit->locus[3]);
    lcl_dict_put(&locus, "pos", v);
    lcl_ref_dec(v);

    /* Same "hi:lo" shape as Lk::doc_revision, so scripts can compare
     * for staleness with a string equality. */
    sprintf(rev, "%lu:%lu", (unsigned long)hit->locus[4],
            (unsigned long)hit->locus[5]);
    v = lcl_string_new(rev);
    lcl_dict_put(&locus, "rev", v);
    lcl_ref_dec(v);

    lcl_dict_put(&dict, "locus", locus);
    lcl_ref_dec(locus);
  } else if (lk_name && strcmp(lk_name, "text-range") == 0) {
    /* a STYLED_TEXT span: #{start end pos} */
    lcl_value *locus = lcl_dict_new();

    v = lcl_int_new((lcl_int)hit->locus[0]);
    lcl_dict_put(&locus, "start", v);
    lcl_ref_dec(v);
    v = lcl_int_new((lcl_int)hit->locus[1]);
    lcl_dict_put(&locus, "end", v);
    lcl_ref_dec(v);
    v = lcl_int_new((lcl_int)hit->locus[2]);
    lcl_dict_put(&locus, "pos", v);
    lcl_ref_dec(v);
    lcl_dict_put(&dict, "locus", locus);
    lcl_ref_dec(locus);
  } else {
    lcl_value *locus = lcl_list_new();
    int i;

    for (i = 0; i < 6; i++) {
      v = lcl_int_new((lcl_int)hit->locus[i]);
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
  v = lcl_int_new((lcl_int)cmd->source_node);
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

/* ---- Script-callback error reporting ---- */

/* Host->script callbacks (command handlers, event handlers, doc
 * subscribers, the frame proc) run outside any script eval, so a
 * script error raised inside one has no caller to propagate to.
 * Report it on stderr — swallowing it silently makes handler bugs
 * undiagnosable (a broken handler just "does nothing"). */
static void lcl_lk_report_callback_error(lcl_interp *interp,
                                         const char *where) {
  const char *file = lcl_interp_error_file(interp);
  int line = lcl_interp_error_line(interp);
  const char *msg = lcl_interp_error_msg(interp);

  fprintf(stderr, "%s error", where);

  if (file) {
    fprintf(stderr, " in %s", file);
  }

  if (line > 0) {
    fprintf(stderr, ":%d", line);
  }

  fprintf(stderr, ": %s\n", msg ? msg : "(unknown)");
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

  if (lcl_call_proc(ctx->interp, ctx->handler, 1, args, &result) !=
      LCL_RC_OK) {
    lcl_lk_report_callback_error(ctx->interp, "Command handler");
  }

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

/* Lk::commands [ui] -> list of command dicts */
static lcl_return_code c_lk_commands(lcl_interp *interp, int argc,
                                     lcl_value **argv, lcl_value **out) {
  lk_ui *ui;
  const lk_command_queue *q;
  lcl_value *list;
  lk_u32 i;

  if (argc != 1) {
    lcl_set_error(interp, "Lk::commands: expected 1 argument");

    return LCL_RC_ERR;
  }

  if (lcl_opaque_get(argv[0], LK_UI_TYPE, (void **)&ui) != LCL_OK) {
    lcl_set_error(interp, "Lk::commands: expected lk_ui opaque");

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

/* Lk::clear_commands [ui] -> "" */
static lcl_return_code c_lk_clear_commands(lcl_interp *interp, int argc,
                                           lcl_value **argv, lcl_value **out) {
  lk_ui *ui;

  if (argc != 1) {
    lcl_set_error(interp, "Lk::clear_commands: expected 1 argument");

    return LCL_RC_ERR;
  }

  if (lcl_opaque_get(argv[0], LK_UI_TYPE, (void **)&ui) != LCL_OK) {
    lcl_set_error(interp, "Lk::clear_commands: expected lk_ui opaque");

    return LCL_RC_ERR;
  }

  lk_ui_clear_commands(ui);
  *out = lcl_string_new("");

  return LCL_RC_OK;
}

/* Lk::command_log [ui] -> list of command dicts */
static lcl_return_code c_lk_command_log(lcl_interp *interp, int argc,
                                        lcl_value **argv, lcl_value **out) {
  lk_ui *ui;
  lk_u32 log_count;
  const lk_command *log;
  lcl_value *list;
  lk_u32 i;

  if (argc != 1) {
    lcl_set_error(interp, "Lk::command_log: expected 1 argument");

    return LCL_RC_ERR;
  }

  if (lcl_opaque_get(argv[0], LK_UI_TYPE, (void **)&ui) != LCL_OK) {
    lcl_set_error(interp, "Lk::command_log: expected lk_ui opaque");

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

/* Lk::clear_command_log [ui] -> "" */
static lcl_return_code c_lk_clear_command_log(lcl_interp *interp, int argc,
                                              lcl_value **argv,
                                              lcl_value **out) {
  lk_ui *ui;

  if (argc != 1) {
    lcl_set_error(interp, "Lk::clear_command_log: expected 1 argument");

    return LCL_RC_ERR;
  }

  if (lcl_opaque_get(argv[0], LK_UI_TYPE, (void **)&ui) != LCL_OK) {
    lcl_set_error(interp, "Lk::clear_command_log: expected lk_ui opaque");

    return LCL_RC_ERR;
  }

  lk_ui_clear_command_log(ui);
  *out = lcl_string_new("");

  return LCL_RC_OK;
}

/* Lk::set_command_handler [ui, handler_proc] -> "" */
static lcl_return_code c_lk_set_command_handler(lcl_interp *interp, int argc,
                                                lcl_value **argv,
                                                lcl_value **out) {
  lk_ui *ui;
  struct lcl_cmd_ctx *ctx;

  if (argc != 2) {
    lcl_set_error(interp, "Lk::set_command_handler: expected 2 arguments");

    return LCL_RC_ERR;
  }

  if (lcl_opaque_get(argv[0], LK_UI_TYPE, (void **)&ui) != LCL_OK) {
    lcl_set_error(interp, "Lk::set_command_handler: expected lk_ui opaque");

    return LCL_RC_ERR;
  }

  if (!lcl_is_callable(argv[1])) {
    lcl_set_error(interp, "Lk::set_command_handler: expected callable");

    return LCL_RC_ERR;
  }

  /* Free previous handler ctx if one was set through this binding */
  if (ui->cmd_handler == lcl_cmd_bridge && ui->cmd_handler_ud) {
    lcl_cmd_ctx_free((struct lcl_cmd_ctx *)ui->cmd_handler_ud);
  }

  ctx = (struct lcl_cmd_ctx *)malloc(sizeof(*ctx));

  if (!ctx) {
    lcl_set_error(interp, "Lk::set_command_handler: allocation failed");

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

/* Lk::state_set [ui, node_id_str, key_int, value] -> "" */
static lcl_return_code c_lk_state_set(lcl_interp *interp, int argc,
                                      lcl_value **argv, lcl_value **out) {
  lk_ui *ui;
  const char *node_str;
  lcl_int key;
  lk_node_id nid;
  lk_value lv;
  lk_state *st;

  if (argc != 4) {
    lcl_set_error(interp, "Lk::state_set: expected 4 arguments");

    return LCL_RC_ERR;
  }

  if (lcl_opaque_get(argv[0], LK_UI_TYPE, (void **)&ui) != LCL_OK) {
    lcl_set_error(interp, "Lk::state_set: expected lk_ui opaque");

    return LCL_RC_ERR;
  }

  node_str = arg_name(interp, argv[1], "Lk::state_set: node id");

  if (!node_str) {
    return LCL_RC_ERR;
  }

  if (lcl_value_to_int(argv[2], &key) != LCL_OK) {
    lcl_set_error(interp, "Lk::state_set: key must be an integer");

    return LCL_RC_ERR;
  }

  /* Keys below LKS_USER are internal widget state — scripts never
   * poke widget state (initial values are props, e.g. the dropdown's
   * `value`).  App-owned state starts at LKS_USER. */
  if (key < (lcl_int)LKS_USER) {
    lcl_set_error(interp,
                  "Lk::state_set: keys below 256 (LKS_USER) are internal "
                  "widget state");

    return LCL_RC_ERR;
  }

  nid = lk_intern_cid(ui->intern, node_str);

  /* Value by tag: strings stay strings, typed numbers become i32,
   * anything else renders (mirrors coerce_lk_value). */
  {
    const char *sv;
    lcl_int iv;

    if (lcl_value_get_string(argv[3], &sv) == LCL_OK) {
      lv = lk_v_cstr(ui->intern, sv);
    } else if (lcl_value_to_int(argv[3], &iv) == LCL_OK) {
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

/* Lk::state_get [ui, node_id_str, key_int] -> value */
static lcl_return_code c_lk_state_get(lcl_interp *interp, int argc,
                                      lcl_value **argv, lcl_value **out) {
  lk_ui *ui;
  const char *node_str;
  lcl_int key;
  lk_node_id nid;
  lk_value lv;
  lk_state *st;

  if (argc != 3) {
    lcl_set_error(interp, "Lk::state_get: expected 3 arguments");

    return LCL_RC_ERR;
  }

  if (lcl_opaque_get(argv[0], LK_UI_TYPE, (void **)&ui) != LCL_OK) {
    lcl_set_error(interp, "Lk::state_get: expected lk_ui opaque");

    return LCL_RC_ERR;
  }

  node_str = arg_name(interp, argv[1], "Lk::state_get: node id");

  if (!node_str) {
    return LCL_RC_ERR;
  }

  if (lcl_value_to_int(argv[2], &key) != LCL_OK) {
    lcl_set_error(interp, "Lk::state_get: key must be an integer");

    return LCL_RC_ERR;
  }

  /* Same barrier as Lk::state_set: internal widget state is not
   * script-visible. */
  if (key < (lcl_int)LKS_USER) {
    lcl_set_error(interp,
                  "Lk::state_get: keys below 256 (LKS_USER) are internal "
                  "widget state");

    return LCL_RC_ERR;
  }

  nid = lk_intern_cid(ui->intern, node_str);
  st = lk_ui_state(ui);
  lv = lk_state_get(st, nid, (lk_u16)key);

  switch (lv.tag) {
  case UIV_I32: *out = lcl_int_new((lcl_int)lv.as.i); break;
  case UIV_BOOL: *out = lcl_int_new((lcl_int)lv.as.b); break;
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

/* Lk::focus_set [ui, node_id_str] -> "" */
static lcl_return_code c_lk_focus_set(lcl_interp *interp, int argc,
                                      lcl_value **argv, lcl_value **out) {
  lk_ui *ui;
  const char *id_str;
  lk_node_id nid;
  const lk_tree *t;

  if (argc != 2) {
    lcl_set_error(interp, "Lk::focus_set: expected 2 arguments");

    return LCL_RC_ERR;
  }

  if (lcl_opaque_get(argv[0], LK_UI_TYPE, (void **)&ui) != LCL_OK) {
    lcl_set_error(interp, "Lk::focus_set: expected lk_ui opaque");

    return LCL_RC_ERR;
  }

  id_str = arg_name(interp, argv[1], "Lk::focus_set: node id");

  if (!id_str) {
    return LCL_RC_ERR;
  }

  nid = lk_intern_cid(ui->intern, id_str);
  t = lk_ui_tree(ui);
  lk_focus_set(ui, t, nid);

  *out = lcl_string_new("");

  return LCL_RC_OK;
}

/* Lk::focus_request [ui id] -> "" -- deferred focus, applied at the
 * next end_frame that commits the node ("" cancels). */
static lcl_return_code c_lk_focus_request(lcl_interp *interp, int argc,
                                          lcl_value **argv,
                                          lcl_value **out) {
  lk_ui *ui;
  const char *id_str;

  if (argc != 2) {
    lcl_set_error(interp, "Lk::focus_request: expected 2 arguments (ui id)");

    return LCL_RC_ERR;
  }

  if (lcl_opaque_get(argv[0], LK_UI_TYPE, (void **)&ui) != LCL_OK) {
    lcl_set_error(interp, "Lk::focus_request: expected lk_ui opaque");

    return LCL_RC_ERR;
  }

  id_str = arg_name(interp, argv[1], "Lk::focus_request: node id");

  if (!id_str) {
    return LCL_RC_ERR;
  }

  lk_focus_request(ui, id_str[0] ? lk_intern_cid(ui->intern, id_str) : 0);

  *out = lcl_string_new("");

  return LCL_RC_OK;
}

/* Lk::focus_clear [ui] -> "" */
static lcl_return_code c_lk_focus_clear(lcl_interp *interp, int argc,
                                        lcl_value **argv, lcl_value **out) {
  lk_ui *ui;

  if (argc != 1) {
    lcl_set_error(interp, "Lk::focus_clear: expected 1 argument");

    return LCL_RC_ERR;
  }

  if (lcl_opaque_get(argv[0], LK_UI_TYPE, (void **)&ui) != LCL_OK) {
    lcl_set_error(interp, "Lk::focus_clear: expected lk_ui opaque");

    return LCL_RC_ERR;
  }

  lk_focus_clear(ui);

  *out = lcl_string_new("");

  return LCL_RC_OK;
}

/* Lk::capture_set [ui id] -> "" -- pointer capture for a node: the run
 * loop targets MOVE/UP at it and suppresses hover until cleared. */
static lcl_return_code c_lk_capture_set(lcl_interp *interp, int argc,
                                        lcl_value **argv, lcl_value **out) {
  lk_ui *ui;
  const char *id_str;

  if (argc != 2) {
    lcl_set_error(interp, "Lk::capture_set: expected 2 arguments");

    return LCL_RC_ERR;
  }

  if (lcl_opaque_get(argv[0], LK_UI_TYPE, (void **)&ui) != LCL_OK) {
    lcl_set_error(interp, "Lk::capture_set: expected lk_ui opaque");

    return LCL_RC_ERR;
  }

  id_str = arg_name(interp, argv[1], "Lk::capture_set: node id");

  if (!id_str) {
    return LCL_RC_ERR;
  }

  lk_capture_set(ui, lk_intern_cid(ui->intern, id_str));
  *out = lcl_string_new("");

  return LCL_RC_OK;
}

/* Lk::capture_clear [ui] -> "" */
static lcl_return_code c_lk_capture_clear(lcl_interp *interp, int argc,
                                          lcl_value **argv, lcl_value **out) {
  lk_ui *ui;

  if (argc != 1) {
    lcl_set_error(interp, "Lk::capture_clear: expected 1 argument");

    return LCL_RC_ERR;
  }

  if (lcl_opaque_get(argv[0], LK_UI_TYPE, (void **)&ui) != LCL_OK) {
    lcl_set_error(interp, "Lk::capture_clear: expected lk_ui opaque");

    return LCL_RC_ERR;
  }

  lk_capture_clear(ui);
  *out = lcl_string_new("");

  return LCL_RC_OK;
}

/* Lk::capture_get [ui] -> node id string, "" when none */
static lcl_return_code c_lk_capture_get(lcl_interp *interp, int argc,
                                        lcl_value **argv, lcl_value **out) {
  lk_ui *ui;
  lk_node_id id;
  const char *id_str = NULL;

  if (argc != 1) {
    lcl_set_error(interp, "Lk::capture_get: expected 1 argument");

    return LCL_RC_ERR;
  }

  if (lcl_opaque_get(argv[0], LK_UI_TYPE, (void **)&ui) != LCL_OK) {
    lcl_set_error(interp, "Lk::capture_get: expected lk_ui opaque");

    return LCL_RC_ERR;
  }

  id = lk_capture_current(ui);

  if (id != 0) {
    id_str = lk_intern_cstr(ui->intern, id);
  }

  *out = lcl_string_new(id_str ? id_str : "");

  return LCL_RC_OK;
}

/* Lk::text_size [ui text ?font_id ?font_size] -> (w h), measured by the
 * ui's text backend (a window installs its own; the stub headless). */
static lcl_return_code c_lk_text_size(lcl_interp *interp, int argc,
                                      lcl_value **argv, lcl_value **out) {
  lk_ui *ui;
  const char *text;
  lcl_int font_id = 0;
  lcl_int font_size = 0;
  const lk_text_backend *tb;
  lk_text_metrics m;
  lcl_value *list;

  if (argc < 2 || argc > 4) {
    lcl_set_error(interp,
                  "Lk::text_size: expected 2 to 4 arguments "
                  "(ui, text, ?font_id, ?font_size)");

    return LCL_RC_ERR;
  }

  if (lcl_opaque_get(argv[0], LK_UI_TYPE, (void **)&ui) != LCL_OK) {
    lcl_set_error(interp, "Lk::text_size: expected lk_ui opaque");

    return LCL_RC_ERR;
  }

  text = lcl_value_to_string(argv[1]);

  if (argc >= 3 && (lcl_value_to_int(argv[2], &font_id) != LCL_OK ||
                    font_id < 0 || font_id > 65535)) {
    lcl_set_error(interp, "Lk::text_size: font_id must be an int 0..65535");

    return LCL_RC_ERR;
  }

  if (argc >= 4 && (lcl_value_to_int(argv[3], &font_size) != LCL_OK ||
                    font_size < 0 || font_size > 65535)) {
    lcl_set_error(interp,
                  "Lk::text_size: font_size must be an int 0..65535");

    return LCL_RC_ERR;
  }

  tb = ui->text ? ui->text : lk_text_backend_stub();
  memset(&m, 0, sizeof(m));
  tb->measure(tb->ud, lk_str_c(text ? text : ""), (lk_u16)font_id,
              (lk_u16)font_size, &m);

  list = lcl_list_new();
  {
    lcl_value *v;

    v = lcl_int_new((lcl_int)m.w);
    lcl_list_push(&list, v);
    lcl_ref_dec(v);
    v = lcl_int_new((lcl_int)m.h);
    lcl_list_push(&list, v);
    lcl_ref_dec(v);
  }
  *out = list;

  return LCL_RC_OK;
}

/* Lk::focus_get [ui] -> focused node's id string, "" when none */
static lcl_return_code c_lk_focus_get(lcl_interp *interp, int argc,
                                      lcl_value **argv, lcl_value **out) {
  lk_ui *ui;
  const char *id_str = NULL;

  if (argc != 1) {
    lcl_set_error(interp, "Lk::focus_get: expected 1 argument");

    return LCL_RC_ERR;
  }

  if (lcl_opaque_get(argv[0], LK_UI_TYPE, (void **)&ui) != LCL_OK) {
    lcl_set_error(interp, "Lk::focus_get: expected lk_ui opaque");

    return LCL_RC_ERR;
  }

  if (ui->focused_id != 0) {
    id_str = lk_intern_cstr(ui->intern, ui->focused_id);
  }

  *out = lcl_string_new(id_str ? id_str : "");

  return LCL_RC_OK;
}

/* Lk::node_rect [ui id] -> (x y w h) from the last layout into the
 * ui-owned rects array, or () when the node is unknown or the host
 * never laid out into lk_ui_rects (headless script, tree-driving
 * host with its own array).  A node the layout skipped -- hidden
 * subtree, collapsed tab page -- reports (0 0 0 0). */
/* Lk::version -> the linked lk's LK_VERSION_STRING. */
static lcl_return_code c_lk_version(lcl_interp *interp, int argc,
                                    lcl_value **argv, lcl_value **out) {
  (void)argv;

  if (argc != 0) {
    lcl_set_error(interp, "Lk::version: expected no arguments");

    return LCL_RC_ERR;
  }

  *out = lcl_string_new(lk_version());

  return LCL_RC_OK;
}

/* ---- Lk::args: script arguments (lcl_lk_set_args) ---- */

static int g_lk_argc = 0;
static char **g_lk_argv = NULL;

void lcl_lk_set_args(int argc, char **argv) {
  g_lk_argc = (argc > 0 && argv) ? argc : 0;
  g_lk_argv = argv;
}

/* Lk::args -> list of the strings after the script path (() when the
 * host set none). */
static lcl_return_code c_lk_args(lcl_interp *interp, int argc,
                                 lcl_value **argv, lcl_value **out) {
  lcl_value *list;
  int i;

  (void)argv;

  if (argc != 0) {
    lcl_set_error(interp, "Lk::args: expected no arguments");

    return LCL_RC_ERR;
  }

  list = lcl_list_new();

  for (i = 0; i < g_lk_argc; i++) {
    lcl_value *v = lcl_string_new(g_lk_argv[i] ? g_lk_argv[i] : "");

    lcl_list_push(&list, v);
    lcl_ref_dec(v);
  }

  *out = list;

  return LCL_RC_OK;
}

static lcl_return_code c_lk_node_rect(lcl_interp *interp, int argc,
                                      lcl_value **argv, lcl_value **out) {
  lk_ui *ui;
  const char *id_str;
  lk_node_id id;
  lk_rect r;
  lcl_value *list;
  lcl_value *v;
  lk_i32 parts[4];
  int i;

  if (argc != 2) {
    lcl_set_error(interp, "Lk::node_rect: expected 2 arguments (ui id)");

    return LCL_RC_ERR;
  }

  if (lcl_opaque_get(argv[0], LK_UI_TYPE, (void **)&ui) != LCL_OK) {
    lcl_set_error(interp, "Lk::node_rect: expected lk_ui opaque");

    return LCL_RC_ERR;
  }

  id_str = arg_name(interp, argv[1], "Lk::node_rect: id");

  if (!id_str) {
    return LCL_RC_ERR;
  }

  list = lcl_list_new();
  id = lk_intern_cid(ui->intern, id_str);

  if (lk_node_rect(ui, id, &r)) {
    parts[0] = r.x;
    parts[1] = r.y;
    parts[2] = r.w;
    parts[3] = r.h;

    for (i = 0; i < 4; i++) {
      v = lcl_int_new((lcl_int)parts[i]);
      lcl_list_push(&list, v);
      lcl_ref_dec(v);
    }
  }

  *out = list;

  return LCL_RC_OK;
}

/* ---- Lk::editor_keys: the widget's bindings as data ---- */

static const char *enum_name(const str_enum *table, int value) {
  int i;

  for (i = 0; table[i].name; i++) {
    if (table[i].value == value) {
      return table[i].name;
    }
  }

  return "";
}

/* "ctrl+alt+shift" spelling shared with the DSL `keybinding` parser
 * (parse_mods accepts any order; this is the canonical one). */
static void mods_to_string(lk_u8 mods, char *buf, size_t cap) {
  static const struct {
    lk_u8 bit;
    const char *name;
  } order[] = {
      {LK_MOD_CTRL, "ctrl"}, {LK_MOD_ALT, "alt"},
      {LK_MOD_SHIFT, "shift"}, {LK_MOD_GUI, "gui"}};
  size_t i;
  size_t len = 0;

  buf[0] = '\0';

  for (i = 0; i < sizeof(order) / sizeof(order[0]); i++) {
    size_t nl;

    if (!(mods & order[i].bit)) {
      continue;
    }

    nl = strlen(order[i].name);

    if (len + nl + 2 > cap) {
      break;
    }

    if (len > 0) {
      buf[len++] = '+';
    }

    memcpy(buf + len, order[i].name, nl);
    len += nl;
    buf[len] = '\0';
  }
}

/* "<mods>+<name>" or just "<name>"; buf is always terminated. */
static void join_chord(char *buf, size_t cap, const char *mods,
                       const char *name) {
  size_t ml = strlen(mods);
  size_t nl = strlen(name);

  buf[0] = '\0';

  if (ml + nl + 2 > cap) {
    return;
  }

  if (ml > 0) {
    strcpy(buf, mods);
    strcat(buf, "+");
  }

  strcat(buf, name);
}

static void dict_put_cstr(lcl_value **dict, const char *key, const char *s) {
  lcl_value *v = lcl_string_new(s);

  lcl_dict_put(dict, key, v);
  lcl_ref_dec(v);
}

static void dict_put_int(lcl_value **dict, const char *key, lcl_int i) {
  lcl_value *v = lcl_int_new(i);

  lcl_dict_put(dict, key, v);
  lcl_ref_dec(v);
}

/* Lk::editor_keys -> list of binding records (docs/editor-keys-
 * reflection.md).  Key rows: #{kind key chord "ctrl+d" key d mods
 * "ctrl" mods_exact 1 extend_with_shift 0 command select_next_match
 * doc ..}; pointer rows: #{kind pointer gesture "ctrl+click" button
 * primary mods "ctrl" mods_exact 0 action add_cursor command
 * add_cursor_at doc ..} (command "" when the action is not one).
 * Order is the widget's dispatch order (stable across a release, so
 * apps can diff it). */
static lcl_return_code c_lk_editor_keys(lcl_interp *interp, int argc,
                                        lcl_value **argv, lcl_value **out) {
  static const char *gesture_names[] = {"click", "drag", "wheel"};
  static const char *action_names[] = {"place_cursor", "add_cursor",
                                       "box_select", "scroll", "scroll_x"};
  const lk_editor_key_binding *keys;
  const lk_editor_pointer_binding *ptrs;
  lk_u32 nk;
  lk_u32 np;
  lk_u32 i;
  lcl_value *list;
  char mods[32];
  char chord[64];

  (void)argv;

  if (argc != 0) {
    lcl_set_error(interp, "Lk::editor_keys: expected no arguments");

    return LCL_RC_ERR;
  }

  list = lcl_list_new();
  keys = lk_editor_key_bindings(&nk);
  ptrs = lk_editor_pointer_bindings(&np);

  for (i = 0; i < nk; i++) {
    const lk_editor_key_binding *b = &keys[i];
    lcl_value *rec = lcl_dict_new();
    const char *key_name = enum_name(keycode_table, (int)b->key);
    lk_u8 exact_mask = (lk_u8)(LK_EDITOR_MODS_ALL &
                               ~(b->shift_extends ? LK_MOD_SHIFT : 0));

    mods_to_string(b->mods, mods, sizeof(mods));

    join_chord(chord, sizeof(chord), mods, key_name);

    dict_put_cstr(&rec, "kind", "key");
    dict_put_cstr(&rec, "chord", chord);
    dict_put_cstr(&rec, "key", key_name);
    dict_put_cstr(&rec, "mods", mods);
    dict_put_int(&rec, "mods_exact",
                 (b->mods_mask & exact_mask) == exact_mask ? 1 : 0);
    dict_put_int(&rec, "extend_with_shift", b->shift_extends ? 1 : 0);
    dict_put_cstr(&rec, "command", enum_name(ed_cmd_table, (int)b->cmd));
    dict_put_cstr(&rec, "doc", b->doc);
    lcl_list_push(&list, rec);
    lcl_ref_dec(rec);
  }

  for (i = 0; i < np; i++) {
    const lk_editor_pointer_binding *b = &ptrs[i];
    lcl_value *rec = lcl_dict_new();
    const char *gest = b->gesture < 3 ? gesture_names[b->gesture] : "";
    const char *btn = b->gesture == (lk_u8)LK_EDG_WHEEL
                          ? ""
                          : enum_name(button_table, (int)b->button);

    mods_to_string(b->mods, mods, sizeof(mods));

    join_chord(chord, sizeof(chord), mods, gest);

    dict_put_cstr(&rec, "kind", "pointer");
    dict_put_cstr(&rec, "gesture", chord);
    dict_put_cstr(&rec, "button", btn);
    dict_put_cstr(&rec, "mods", mods);
    dict_put_int(&rec, "mods_exact",
                 (b->mods_mask & LK_EDITOR_MODS_ALL) == LK_EDITOR_MODS_ALL
                     ? 1
                     : 0);
    dict_put_cstr(&rec, "action",
                  b->action < 5 ? action_names[b->action] : "");
    dict_put_cstr(&rec, "command",
                  b->has_cmd ? enum_name(ed_cmd_table, (int)b->cmd) : "");
    dict_put_cstr(&rec, "doc", b->doc);
    lcl_list_push(&list, rec);
    lcl_ref_dec(rec);
  }

  *out = list;

  return LCL_RC_OK;
}

/* Lk::time_ms [ui] -> int: monotonic frame time in ms, stamped by the
 * backend once per frame (0 until a backend stamps it; wraps at
 * 2^32). */
static lcl_return_code c_lk_time_ms(lcl_interp *interp, int argc,
                                    lcl_value **argv, lcl_value **out) {
  lk_ui *ui;

  if (argc != 1) {
    lcl_set_error(interp, "Lk::time_ms: expected 1 argument");

    return LCL_RC_ERR;
  }

  if (lcl_opaque_get(argv[0], LK_UI_TYPE, (void **)&ui) != LCL_OK) {
    lcl_set_error(interp, "Lk::time_ms: expected lk_ui opaque");

    return LCL_RC_ERR;
  }

  *out = lcl_int_new((lcl_int)lk_ui_time_ms(ui));

  return LCL_RC_OK;
}

/* ============================================================================
 * Tags & Style (2)
 * ============================================================================
 */

/* Lk::tag [tree, node_ix, tag_str] -> "" */
static lcl_return_code c_lk_tag(lcl_interp *interp, int argc, lcl_value **argv,
                                lcl_value **out) {
  lk_tree *t;
  lcl_int node_ix;
  const char *tag_str;

  if (argc != 3) {
    lcl_set_error(interp, "Lk::tag: expected 3 arguments (tree, node, tag)");

    return LCL_RC_ERR;
  }

  if (lcl_opaque_get(argv[0], LK_TREE_TYPE, (void **)&t) != LCL_OK) {
    lcl_set_error(interp, "Lk::tag: expected lk_tree opaque");

    return LCL_RC_ERR;
  }

  if (lcl_value_to_int(argv[1], &node_ix) != LCL_OK) {
    lcl_set_error(interp, "Lk::tag: node_ix must be an integer");

    return LCL_RC_ERR;
  }

  tag_str = arg_name(interp, argv[2], "Lk::tag: tag");

  if (!tag_str) {
    return LCL_RC_ERR;
  }

  lk_tree_add_tag_s(t, (lk_ix)node_ix, tag_str);

  *out = lcl_string_new("");

  return LCL_RC_OK;
}

/* Helper: parse (r g b) or (r g b a) list into lk_color */
static int parse_color_list(lcl_value *list, lk_color *out) {
  size_t len;
  lcl_value *v;
  lcl_int r;
  lcl_int g;
  lcl_int b;
  lcl_int a;

  len = lcl_list_len(list);

  if (len < 3 || len > 4) {
    return 0;
  }

  v = lcl_list_peek(list, 0);

  if (!v || lcl_value_to_int(v, &r) != LCL_OK) {
    return 0;
  }

  v = lcl_list_peek(list, 1);

  if (!v || lcl_value_to_int(v, &g) != LCL_OK) {
    return 0;
  }

  v = lcl_list_peek(list, 2);

  if (!v || lcl_value_to_int(v, &b) != LCL_OK) {
    return 0;
  }

  a = 255;

  if (len == 4) {
    v = lcl_list_peek(list, 3);

    if (!v || lcl_value_to_int(v, &a) != LCL_OK) {
      return 0;
    }
  }

  out->r = (lk_u8)r;
  out->g = (lk_u8)g;
  out->b = (lk_u8)b;
  out->a = (lk_u8)a;

  return 1;
}

/* Lk::theme_rule [ui, kind_str, tag_str, state_str, style_dict] -> "" */
static lcl_return_code c_lk_theme_rule(lcl_interp *interp, int argc,
                                       lcl_value **argv, lcl_value **out) {
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
        "Lk::theme_rule: expected 5 arguments (ui, kind, tag, state, style)");

    return LCL_RC_ERR;
  }

  if (lcl_opaque_get(argv[0], LK_UI_TYPE, (void **)&ui) != LCL_OK) {
    lcl_set_error(interp, "Lk::theme_rule: expected lk_ui opaque");

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
      lcl_set_error(interp, "Lk::theme_rule: unknown kind");

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
      lcl_set_error(interp, "Lk::theme_rule: unknown state");

      return LCL_RC_ERR;
    }

    state_mask = (lk_u8)sv;
  }

  /* Parse style dict */
  memset(&style, 0, sizeof(style));

  /* bg: {r g b} or {r g b a} */
  v = lcl_dict_peek(dict, "bg");

  if (v) {
    if (parse_color_list(v, &style.bg)) {
      field_mask |= LK_SF_BG;
    }
  }

  /* fg: {r g b} or {r g b a} */
  v = lcl_dict_peek(dict, "fg");

  if (v) {
    if (parse_color_list(v, &style.fg)) {
      field_mask |= LK_SF_FG;
    }
  }

  /* border_color: {r g b} or {r g b a} */
  v = lcl_dict_peek(dict, "border_color");

  if (v) {
    if (parse_color_list(v, &style.border_color)) {
      field_mask |= LK_SF_BORDER_COLOR;
    }
  }

  /* padding: int */
  v = lcl_dict_peek(dict, "padding");

  if (v) {
    lcl_int iv;
    if (lcl_value_to_int(v, &iv) == LCL_OK) {
      style.padding = (lk_i32)iv;
      field_mask |= LK_SF_PADDING;
    }
  }

  /* gap: int */
  v = lcl_dict_peek(dict, "gap");

  if (v) {
    lcl_int iv;
    if (lcl_value_to_int(v, &iv) == LCL_OK) {
      style.gap = (lk_i32)iv;
      field_mask |= LK_SF_GAP;
    }
  }

  /* font_id: int (from Lk::register_font; 0 = default face) */
  v = lcl_dict_peek(dict, "font_id");

  if (v) {
    lcl_int iv;
    if (lcl_value_to_int(v, &iv) != LCL_OK || iv < 0) {
      lcl_set_error(interp,
                    "Lk::theme_rule: font_id must be a non-negative int");

      return LCL_RC_ERR;
    }

    style.font_id = (lk_u32)iv;
    field_mask |= LK_SF_FONT_ID;
  }

  /* font_size: int (0 = face default size) */
  v = lcl_dict_peek(dict, "font_size");

  if (v) {
    lcl_int iv;
    if (lcl_value_to_int(v, &iv) != LCL_OK || iv < 0) {
      lcl_set_error(interp,
                    "Lk::theme_rule: font_size must be a non-negative int");

      return LCL_RC_ERR;
    }

    style.font_size = (lk_i32)iv;
    field_mask |= LK_SF_FONT_SIZE;
  }

  /* border_width: int */
  v = lcl_dict_peek(dict, "border_width");

  if (v) {
    lcl_int iv;
    if (lcl_value_to_int(v, &iv) == LCL_OK) {
      style.border_width = (lk_i32)iv;
      field_mask |= LK_SF_BORDER_WIDTH;
    }
  }

  /* border_radius: int */
  v = lcl_dict_peek(dict, "border_radius");

  if (v) {
    lcl_int iv;
    if (lcl_value_to_int(v, &iv) == LCL_OK) {
      style.border_radius = (lk_i32)iv;
      field_mask |= LK_SF_BORDER_RADIUS;
    }
  }

  /* align: string */
  v = lcl_dict_peek(dict, "align");

  if (v) {
    int av;
    if (lookup_enum(align_table, lcl_value_to_string(v), &av)) {
      style.align = (lk_u8)av;
      field_mask |= LK_SF_ALIGN;
    }
  }

  /* justify: string */
  v = lcl_dict_peek(dict, "justify");

  if (v) {
    int av;
    if (lookup_enum(align_table, lcl_value_to_string(v), &av)) {
      style.justify = (lk_u8)av;
      field_mask |= LK_SF_JUSTIFY;
    }
  }

  /* text_align / text_valign: string (a leaf's text run placement) */
  v = lcl_dict_peek(dict, "text_align");

  if (v) {
    int av;
    if (lookup_enum(align_table, lcl_value_to_string(v), &av)) {
      style.text_align = (lk_u8)av;
      field_mask |= LK_SF_TEXT_ALIGN;
    }
  }

  v = lcl_dict_peek(dict, "text_valign");

  if (v) {
    int av;
    if (lookup_enum(align_table, lcl_value_to_string(v), &av)) {
      style.text_valign = (lk_u8)av;
      field_mask |= LK_SF_TEXT_VALIGN;
    }
  }

  /* scrollbar_track: {r g b} or {r g b a} */
  v = lcl_dict_peek(dict, "scrollbar_track");

  if (v) {
    if (parse_color_list(v, &style.scrollbar_track)) {
      field_mask |= LK_SF_SCROLLBAR_TRACK;
    }
  }

  /* scrollbar_thumb: {r g b} or {r g b a} */
  v = lcl_dict_peek(dict, "scrollbar_thumb");

  if (v) {
    if (parse_color_list(v, &style.scrollbar_thumb)) {
      field_mask |= LK_SF_SCROLLBAR_THUMB;
    }
  }

  /* accent: {r g b} or {r g b a} -- check marks, radio dots, slider
   * fill + thumb, selected-tab bar */
  v = lcl_dict_peek(dict, "accent");

  if (v) {
    if (parse_color_list(v, &style.accent)) {
      field_mask |= LK_SF_ACCENT;
    }
  }

  th = lk_ui_theme(ui);

  if (!th) {
    lcl_set_error(interp, "Lk::theme_rule: ui has no theme");

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

/* Lk::intern_str [ui, id_int] -> string */
static lcl_return_code c_lk_intern_str(lcl_interp *interp, int argc,
                                       lcl_value **argv, lcl_value **out) {
  lk_ui *ui;
  lcl_int id;
  const char *s;

  if (argc != 2) {
    lcl_set_error(interp, "Lk::intern_str: expected 2 arguments");

    return LCL_RC_ERR;
  }

  if (lcl_opaque_get(argv[0], LK_UI_TYPE, (void **)&ui) != LCL_OK) {
    lcl_set_error(interp, "Lk::intern_str: expected lk_ui opaque");

    return LCL_RC_ERR;
  }

  if (lcl_value_to_int(argv[1], &id) != LCL_OK) {
    lcl_set_error(interp, "Lk::intern_str: id must be an integer");

    return LCL_RC_ERR;
  }

  s = lk_intern_cstr(ui->intern, (lk_node_id)id);
  *out = lcl_string_new(s ? s : "");

  return LCL_RC_OK;
}

/* Lk::intern_id [ui, string] -> int */
static lcl_return_code c_lk_intern_id(lcl_interp *interp, int argc,
                                      lcl_value **argv, lcl_value **out) {
  lk_ui *ui;
  const char *s;
  lk_node_id id;

  if (argc != 2) {
    lcl_set_error(interp, "Lk::intern_id: expected 2 arguments");

    return LCL_RC_ERR;
  }

  if (lcl_opaque_get(argv[0], LK_UI_TYPE, (void **)&ui) != LCL_OK) {
    lcl_set_error(interp, "Lk::intern_id: expected lk_ui opaque");

    return LCL_RC_ERR;
  }

  s = arg_name(interp, argv[1], "Lk::intern_id: name");

  if (!s) {
    return LCL_RC_ERR;
  }

  id = lk_intern_cid(ui->intern, s);
  *out = lcl_int_new((lcl_int)id);

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

  v = lcl_int_new((lcl_int)ev->mods);
  lcl_dict_put(&dict, "mods", v);
  lcl_ref_dec(v);

  v = lcl_int_new((lcl_int)ev->handled);
  lcl_dict_put(&dict, "handled", v);
  lcl_ref_dec(v);

  v = lcl_int_new((lcl_int)ev->target);
  lcl_dict_put(&dict, "target", v);
  lcl_ref_dec(v);

  /* Type-specific fields */
  switch (ev->type) {
  case LK_EVENT_POINTER_MOVE:
  case LK_EVENT_POINTER_DOWN:
  case LK_EVENT_POINTER_UP:
    v = lcl_int_new((lcl_int)ev->data.pointer.x);
    lcl_dict_put(&dict, "x", v);
    lcl_ref_dec(v);

    v = lcl_int_new((lcl_int)ev->data.pointer.y);
    lcl_dict_put(&dict, "y", v);
    lcl_ref_dec(v);

    v = lcl_int_new((lcl_int)ev->data.pointer.button);
    lcl_dict_put(&dict, "button", v);
    lcl_ref_dec(v);
    break;

  case LK_EVENT_KEY_DOWN:
  case LK_EVENT_KEY_UP:
    v = lcl_int_new((lcl_int)ev->data.key.keycode);
    lcl_dict_put(&dict, "keycode", v);
    lcl_ref_dec(v);

    v = lcl_int_new((lcl_int)ev->data.key.repeat);
    lcl_dict_put(&dict, "repeat", v);
    lcl_ref_dec(v);
    break;

  case LK_EVENT_TEXT:
    v = lcl_string_new(ev->data.text.buf);
    lcl_dict_put(&dict, "text", v);
    lcl_ref_dec(v);
    break;

  case LK_EVENT_WHEEL:
    v = lcl_int_new((lcl_int)ev->data.wheel.dx);
    lcl_dict_put(&dict, "dx", v);
    lcl_ref_dec(v);

    v = lcl_int_new((lcl_int)ev->data.wheel.dy);
    lcl_dict_put(&dict, "dy", v);
    lcl_ref_dec(v);
    break;

  case LK_EVENT_WINDOW_RESIZE:
    v = lcl_int_new((lcl_int)ev->data.window.w);
    lcl_dict_put(&dict, "w", v);
    lcl_ref_dec(v);

    v = lcl_int_new((lcl_int)ev->data.window.h);
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
 * Hex payload helpers — shared by Lk::window_icon_hex and the image
 * byte accessors.  Lcl strings are NUL-terminated, so raw bytes ride
 * as hex text.
 * ============================================================================
 */

static int hex_nibble(char c) {
  if (c >= '0' && c <= '9') {
    return c - '0';
  }

  if (c >= 'a' && c <= 'f') {
    return c - 'a' + 10;
  }

  if (c >= 'A' && c <= 'F') {
    return c - 'A' + 10;
  }

  return -1;
}

/* Decode a hex payload (the Lk::window_icon_hex contract: whitespace
 * between digits ignored; any other character, an odd digit count, or
 * an empty payload is a hard error, prefixed with proc).  Returns a
 * malloc'd buffer the caller frees, or NULL with the interp error
 * set. */
static unsigned char *hex_decode_arg(lcl_interp *interp, const char *proc,
                                     const char *hex, lk_u32 *out_n) {
  unsigned char *bytes;
  lk_u32 n = 0;
  int hi = -1;
  const char *p;
  char err[96];

  /* Decoded size <= strlen/2; over-allocate by the string length. */
  bytes = (unsigned char *)malloc(strlen(hex) / 2 + 1);

  if (!bytes) {
    sprintf(err, "%.40s: allocation failed", proc);
    lcl_set_error(interp, err);

    return NULL;
  }

  for (p = hex; *p; p++) {
    int v;

    if (*p == ' ' || *p == '\n' || *p == '\t' || *p == '\r') {
      continue;
    }

    v = hex_nibble(*p);

    if (v < 0) {
      free(bytes);
      sprintf(err, "%.40s: invalid hex character", proc);
      lcl_set_error(interp, err);

      return NULL;
    }

    if (hi < 0) {
      hi = v;
    } else {
      bytes[n++] = (unsigned char)((hi << 4) | v);
      hi = -1;
    }
  }

  if (hi >= 0 || n == 0) {
    free(bytes);
    sprintf(err, n == 0 ? "%.40s: empty payload"
                        : "%.40s: odd number of hex digits",
            proc);
    lcl_set_error(interp, err);

    return NULL;
  }

  *out_n = n;

  return bytes;
}

/* Encode n bytes as lowercase hex into dst (2n + 1 chars incl. the
 * NUL).  Round-trips through hex_decode_arg. */
static void hex_encode(const unsigned char *bytes, lk_u32 n, char *dst) {
  static const char digits[] = "0123456789abcdef";
  lk_u32 i;

  for (i = 0; i < n; i++) {
    dst[i * 2] = digits[bytes[i] >> 4];
    dst[i * 2 + 1] = digits[bytes[i] & 0x0f];
  }

  dst[n * 2] = '\0';
}

/* ============================================================================
 * Image procs (6) — core, headless.  File load/save live in the SDL
 * set; the pixel/byte accessors here are the script-side substrate
 * for pixel work (steganography!): bytes travel as hex, the
 * window_icon_hex convention.
 * ============================================================================
 */

static struct lcl_lk_image *get_image(lcl_interp *interp, lcl_value *val) {
  struct lcl_lk_image *iw = NULL;

  if (lcl_opaque_get(val, LK_IMAGE_TYPE, (void **)&iw) != LCL_OK) {
    lcl_set_error(interp, "expected lk_image opaque");

    return NULL;
  }

  return iw;
}

/* Register img in ui's table and wrap it as an Lcl opaque.  Consumes
 * img on failure.  Shared by Lk::image_new and (SDL) Lk::image_load. */
static lcl_return_code image_wrap_register(lcl_interp *interp,
                                           const char *proc, lk_ui *ui,
                                           lcl_value *ui_val, lk_image *img,
                                           lcl_value **out) {
  struct lcl_lk_image *iw;
  lk_resource_ref ref;
  char err[96];

  ref = lk_resource_register(lk_ui_resources(ui), lk_image_type(), img,
                             "lcl-image");

  if (ref.id == 0) {
    lk_image_destroy(img);
    sprintf(err, "%.40s: resource registration failed", proc);
    lcl_set_error(interp, err);

    return LCL_RC_ERR;
  }

  iw = (struct lcl_lk_image *)malloc(sizeof(*iw));

  if (!iw) {
    lk_resource_release(lk_ui_resources(ui), ref);
    lk_image_destroy(img);
    sprintf(err, "%.40s: allocation failed", proc);
    lcl_set_error(interp, err);

    return LCL_RC_ERR;
  }

  iw->img = img;
  iw->ui = ui;
  iw->ref = ref;
  iw->ui_val = lcl_ref_inc(ui_val);

  *out = lcl_opaque_new(iw, LK_IMAGE_TYPE, image_finalizer);

  return LCL_RC_OK;
}

/* Lk::image_new [ui w h] -> opaque<lk_image>.  Pixels start
 * transparent black. */
static lcl_return_code c_lk_image_new(lcl_interp *interp, int argc,
                                      lcl_value **argv, lcl_value **out) {
  lk_ui *ui = NULL;
  lcl_int w;
  lcl_int h;
  lk_image *img;

  if (argc != 3) {
    lcl_set_error(interp, "Lk::image_new: expected 3 arguments (ui, w, h)");

    return LCL_RC_ERR;
  }

  if (lcl_opaque_get(argv[0], LK_UI_TYPE, (void **)&ui) != LCL_OK || !ui) {
    lcl_set_error(interp, "Lk::image_new: expected lk_ui opaque");

    return LCL_RC_ERR;
  }

  if (lcl_value_to_int(argv[1], &w) != LCL_OK ||
      lcl_value_to_int(argv[2], &h) != LCL_OK || w < 1 || h < 1) {
    lcl_set_error(interp, "Lk::image_new: w and h must be integers >= 1");

    return LCL_RC_ERR;
  }

  img = lk_image_new((lk_u32)w, (lk_u32)h, NULL, NULL, NULL);

  if (!img) {
    /* > 16384 on an axis, or OOM (lk_image_new's contract) */
    lcl_set_error(interp,
                  "Lk::image_new: invalid dimensions (max 16384 per axis) "
                  "or allocation failed");

    return LCL_RC_ERR;
  }

  return image_wrap_register(interp, "Lk::image_new", ui, argv[0], img, out);
}

/* Lk::image_size [img] -> (w h) */
static lcl_return_code c_lk_image_size(lcl_interp *interp, int argc,
                                       lcl_value **argv, lcl_value **out) {
  struct lcl_lk_image *iw;
  lk_u32 w = 0;
  lk_u32 h = 0;
  lcl_value *list;
  lcl_value *v;

  if (argc != 1) {
    lcl_set_error(interp, "Lk::image_size: expected 1 argument");

    return LCL_RC_ERR;
  }

  iw = get_image(interp, argv[0]);

  if (!iw) {
    return LCL_RC_ERR;
  }

  lk_image_size(iw->img, &w, &h);
  list = lcl_list_new();

  v = lcl_int_new((lcl_int)w);
  lcl_list_push(&list, v);
  lcl_ref_dec(v);

  v = lcl_int_new((lcl_int)h);
  lcl_list_push(&list, v);
  lcl_ref_dec(v);

  *out = list;

  return LCL_RC_OK;
}

/* Shared (x, y) validation for the pixel accessors.  Returns the
 * byte offset of the pixel, or sets the error and returns -1. */
static lcl_int image_px_offset(lcl_interp *interp, const char *proc,
                            struct lcl_lk_image *iw, lcl_value *xv,
                            lcl_value *yv) {
  lk_u32 w = 0;
  lk_u32 h = 0;
  lcl_int x;
  lcl_int y;
  char err[96];

  lk_image_size(iw->img, &w, &h);

  if (lcl_value_to_int(xv, &x) != LCL_OK ||
      lcl_value_to_int(yv, &y) != LCL_OK) {
    sprintf(err, "%.40s: x and y must be integers", proc);
    lcl_set_error(interp, err);

    return -1;
  }

  if (x < 0 || y < 0 || x >= (lcl_int)w || y >= (lcl_int)h) {
    sprintf(err, "%.40s: pixel out of range", proc);
    lcl_set_error(interp, err);

    return -1;
  }

  return (y * (lcl_int)w + x) * 4;
}

/* Lk::image_get_px [img x y] -> (r g b a) */
static lcl_return_code c_lk_image_get_px(lcl_interp *interp, int argc,
                                         lcl_value **argv, lcl_value **out) {
  struct lcl_lk_image *iw;
  lcl_int off;
  const lk_u8 *px;
  lcl_value *list;
  int i;

  if (argc != 3) {
    lcl_set_error(interp, "Lk::image_get_px: expected 3 arguments (img, x, y)");

    return LCL_RC_ERR;
  }

  iw = get_image(interp, argv[0]);

  if (!iw) {
    return LCL_RC_ERR;
  }

  off = image_px_offset(interp, "Lk::image_get_px", iw, argv[1], argv[2]);

  if (off < 0) {
    return LCL_RC_ERR;
  }

  px = lk_image_pixels(iw->img) + off;
  list = lcl_list_new();

  for (i = 0; i < 4; i++) {
    lcl_value *v = lcl_int_new((lcl_int)px[i]);

    lcl_list_push(&list, v);
    lcl_ref_dec(v);
  }

  *out = list;

  return LCL_RC_OK;
}

/* Lk::image_set_px [img x y (r g b ?a?)] — alpha defaults to 255;
 * marks the image dirty. */
static lcl_return_code c_lk_image_set_px(lcl_interp *interp, int argc,
                                         lcl_value **argv, lcl_value **out) {
  struct lcl_lk_image *iw;
  lcl_int off;
  lk_color c;
  lk_u8 *px;

  if (argc != 4) {
    lcl_set_error(
        interp, "Lk::image_set_px: expected 4 arguments (img, x, y, rgba)");

    return LCL_RC_ERR;
  }

  iw = get_image(interp, argv[0]);

  if (!iw) {
    return LCL_RC_ERR;
  }

  off = image_px_offset(interp, "Lk::image_set_px", iw, argv[1], argv[2]);

  if (off < 0) {
    return LCL_RC_ERR;
  }

  if (!parse_color_list(argv[3], &c)) {
    lcl_set_error(interp,
                  "Lk::image_set_px: expected an (r g b ?a?) color list");

    return LCL_RC_ERR;
  }

  px = lk_image_pixels(iw->img) + off;
  px[0] = c.r;
  px[1] = c.g;
  px[2] = c.b;
  px[3] = c.a;
  lk_image_mark_dirty(iw->img);

  *out = lcl_string_new("");

  return LCL_RC_OK;
}

/* Lk::image_bytes [img off len] -> lowercase hex over the raw
 * w*h*4 RGBA byte stream. */
static lcl_return_code c_lk_image_bytes(lcl_interp *interp, int argc,
                                        lcl_value **argv, lcl_value **out) {
  struct lcl_lk_image *iw;
  lk_u32 w = 0;
  lk_u32 h = 0;
  lcl_int off;
  lcl_int len;
  lcl_int total;
  char *hex;

  if (argc != 3) {
    lcl_set_error(interp,
                  "Lk::image_bytes: expected 3 arguments (img, off, len)");

    return LCL_RC_ERR;
  }

  iw = get_image(interp, argv[0]);

  if (!iw) {
    return LCL_RC_ERR;
  }

  if (lcl_value_to_int(argv[1], &off) != LCL_OK ||
      lcl_value_to_int(argv[2], &len) != LCL_OK) {
    lcl_set_error(interp, "Lk::image_bytes: off and len must be integers");

    return LCL_RC_ERR;
  }

  lk_image_size(iw->img, &w, &h);
  total = (lcl_int)w * (lcl_int)h * 4;

  if (off < 0 || len < 1 || off > total - len) {
    lcl_set_error(interp, "Lk::image_bytes: range out of bounds");

    return LCL_RC_ERR;
  }

  hex = (char *)malloc((size_t)len * 2 + 1);

  if (!hex) {
    lcl_set_error(interp, "Lk::image_bytes: allocation failed");

    return LCL_RC_ERR;
  }

  hex_encode(lk_image_pixels(iw->img) + off, (lk_u32)len, hex);
  *out = lcl_string_new(hex);
  free(hex);

  return LCL_RC_OK;
}

/* Lk::image_set_bytes [img off hex] — the window_icon_hex hex
 * contract; marks the image dirty. */
static lcl_return_code c_lk_image_set_bytes(lcl_interp *interp, int argc,
                                            lcl_value **argv,
                                            lcl_value **out) {
  struct lcl_lk_image *iw;
  lk_u32 w = 0;
  lk_u32 h = 0;
  lcl_int off;
  lcl_int total;
  const char *hex;
  unsigned char *bytes;
  lk_u32 n = 0;

  if (argc != 3) {
    lcl_set_error(interp,
                  "Lk::image_set_bytes: expected 3 arguments (img, off, hex)");

    return LCL_RC_ERR;
  }

  iw = get_image(interp, argv[0]);

  if (!iw) {
    return LCL_RC_ERR;
  }

  if (lcl_value_to_int(argv[1], &off) != LCL_OK) {
    lcl_set_error(interp, "Lk::image_set_bytes: off must be an integer");

    return LCL_RC_ERR;
  }

  hex = lcl_value_to_string(argv[2]);

  if (!hex) {
    lcl_set_error(interp, "Lk::image_set_bytes: hex must be a string");

    return LCL_RC_ERR;
  }

  bytes = hex_decode_arg(interp, "Lk::image_set_bytes", hex, &n);

  if (!bytes) {
    return LCL_RC_ERR;
  }

  lk_image_size(iw->img, &w, &h);
  total = (lcl_int)w * (lcl_int)h * 4;

  if (off < 0 || off > total - (lcl_int)n) {
    free(bytes);
    lcl_set_error(interp, "Lk::image_set_bytes: range out of bounds");

    return LCL_RC_ERR;
  }

  memcpy(lk_image_pixels(iw->img) + off, bytes, (size_t)n);
  free(bytes);
  lk_image_mark_dirty(iw->img);

  *out = lcl_string_new("");

  return LCL_RC_OK;
}

/* ============================================================================
 * Vector canvas procs (docs/canvas.md)
 *
 * An lk_canvas is an application-owned display list shown by the
 * `canvas` widget kind through the `canvas` prop.  Coordinates go
 * through lcl_value_to_int so typed floats are accepted (a plotter
 * computes in floats); colors are the theme's (r g b ?a?) lists.
 * ============================================================================
 */

static struct lcl_lk_canvas *get_canvas(lcl_interp *interp, lcl_value *val) {
  struct lcl_lk_canvas *cw = NULL;

  if (lcl_opaque_get(val, LK_CANVAS_TYPE, (void **)&cw) != LCL_OK) {
    lcl_set_error(interp, "expected lk_canvas opaque");

    return NULL;
  }

  return cw;
}

/* One coordinate: ints pass through, floats round half away from
 * zero (lcl_value_to_int would truncate).  Returns 0 when not numeric
 * or out of i32 range. */
static int canvas_coord(lcl_value *v, lk_i32 *out) {
  double f;

  if (lcl_value_to_float(v, &f) != LCL_OK) {
    return 0;
  }

  if (f > 2147483647.0 || f < -2147483648.0) {
    return 0;
  }

  *out = (lk_i32)(f >= 0.0 ? f + 0.5 : f - 0.5);

  return 1;
}

/* Read n coordinates from argv[from..from+n) into out.  Sets the
 * error and returns 0 when one is not numeric. */
static int canvas_coords(lcl_interp *interp, const char *proc,
                         lcl_value **argv, int from, int n, lk_i32 *out) {
  int i;
  char err[96];

  for (i = 0; i < n; i++) {
    if (!canvas_coord(argv[from + i], &out[i])) {
      sprintf(err, "%.40s: coordinates must be numbers", proc);
      lcl_set_error(interp, err);

      return 0;
    }
  }

  return 1;
}

static int canvas_color(lcl_interp *interp, const char *proc, lcl_value *v,
                        lk_color *out) {
  char err[96];

  if (!parse_color_list(v, out)) {
    sprintf(err, "%.40s: expected an (r g b ?a?) color list", proc);
    lcl_set_error(interp, err);

    return 0;
  }

  return 1;
}

/* Optional trailing stroke width: absent -> 1; else an integer
 * 0..255 (hard error otherwise). */
static int canvas_stroke(lcl_interp *interp, const char *proc, int argc,
                         lcl_value **argv, int idx, lk_u8 *out) {
  lcl_int v;
  char err[96];

  *out = 1;

  if (argc <= idx) {
    return 1;
  }

  if (lcl_value_to_int(argv[idx], &v) != LCL_OK || v < 0 || v > 255) {
    sprintf(err, "%.40s: stroke must be an integer 0..255", proc);
    lcl_set_error(interp, err);

    return 0;
  }

  *out = (lk_u8)v;

  return 1;
}

/* Lk::canvas_new [ui ?w ?h] -> opaque<lk_canvas>.  w/h are the size
 * hint the widget measures at (default 0x0: let grow / w / h size
 * the node). */
static lcl_return_code c_lk_canvas_new(lcl_interp *interp, int argc,
                                       lcl_value **argv, lcl_value **out) {
  lk_ui *ui = NULL;
  lcl_int w = 0;
  lcl_int h = 0;
  lk_canvas *cv;
  struct lcl_lk_canvas *cw;
  lk_resource_ref ref;

  if (argc != 1 && argc != 3) {
    lcl_set_error(interp,
                  "Lk::canvas_new: expected 1 or 3 arguments (ui ?w h?)");

    return LCL_RC_ERR;
  }

  if (lcl_opaque_get(argv[0], LK_UI_TYPE, (void **)&ui) != LCL_OK || !ui) {
    lcl_set_error(interp, "Lk::canvas_new: expected lk_ui opaque");

    return LCL_RC_ERR;
  }

  if (argc == 3 &&
      (lcl_value_to_int(argv[1], &w) != LCL_OK ||
       lcl_value_to_int(argv[2], &h) != LCL_OK || w < 0 || h < 0)) {
    lcl_set_error(interp, "Lk::canvas_new: w and h must be integers >= 0");

    return LCL_RC_ERR;
  }

  cv = lk_canvas_new((lk_u32)w, (lk_u32)h, NULL, NULL, NULL);

  if (!cv) {
    lcl_set_error(interp,
                  "Lk::canvas_new: invalid size hint (max 16384 per axis) "
                  "or allocation failed");

    return LCL_RC_ERR;
  }

  ref = lk_resource_register(lk_ui_resources(ui), lk_canvas_type(), cv,
                             "lcl-canvas");

  if (ref.id == 0) {
    lk_canvas_destroy(cv);
    lcl_set_error(interp, "Lk::canvas_new: resource registration failed");

    return LCL_RC_ERR;
  }

  cw = (struct lcl_lk_canvas *)malloc(sizeof(*cw));

  if (!cw) {
    lk_resource_release(lk_ui_resources(ui), ref);
    lk_canvas_destroy(cv);
    lcl_set_error(interp, "Lk::canvas_new: allocation failed");

    return LCL_RC_ERR;
  }

  cw->cv = cv;
  cw->ui = ui;
  cw->ref = ref;
  cw->ui_val = lcl_ref_inc(argv[0]);

  *out = lcl_opaque_new(cw, LK_CANVAS_TYPE, canvas_finalizer);

  return LCL_RC_OK;
}

/* Lk::canvas_size [c] -> (w h) */
static lcl_return_code c_lk_canvas_size(lcl_interp *interp, int argc,
                                        lcl_value **argv, lcl_value **out) {
  struct lcl_lk_canvas *cw;
  lk_u32 w = 0;
  lk_u32 h = 0;
  lcl_value *list;
  lcl_value *v;

  if (argc != 1) {
    lcl_set_error(interp, "Lk::canvas_size: expected 1 argument");

    return LCL_RC_ERR;
  }

  cw = get_canvas(interp, argv[0]);

  if (!cw) {
    return LCL_RC_ERR;
  }

  lk_canvas_size(cw->cv, &w, &h);

  list = lcl_list_new();
  v = lcl_int_new((lcl_int)w);
  lcl_list_push(&list, v);
  lcl_ref_dec(v);
  v = lcl_int_new((lcl_int)h);
  lcl_list_push(&list, v);
  lcl_ref_dec(v);

  *out = list;

  return LCL_RC_OK;
}

/* Lk::canvas_set_size [c w h] */
static lcl_return_code c_lk_canvas_set_size(lcl_interp *interp, int argc,
                                            lcl_value **argv,
                                            lcl_value **out) {
  struct lcl_lk_canvas *cw;
  lcl_int w;
  lcl_int h;

  if (argc != 3) {
    lcl_set_error(interp,
                  "Lk::canvas_set_size: expected 3 arguments (c, w, h)");

    return LCL_RC_ERR;
  }

  cw = get_canvas(interp, argv[0]);

  if (!cw) {
    return LCL_RC_ERR;
  }

  if (lcl_value_to_int(argv[1], &w) != LCL_OK ||
      lcl_value_to_int(argv[2], &h) != LCL_OK || w < 0 || h < 0 ||
      !lk_canvas_set_size(cw->cv, (lk_u32)w, (lk_u32)h)) {
    lcl_set_error(interp,
                  "Lk::canvas_set_size: w and h must be integers 0..16384");

    return LCL_RC_ERR;
  }

  *out = lcl_string_new("");

  return LCL_RC_OK;
}

/* Lk::canvas_clear [c] */
static lcl_return_code c_lk_canvas_clear(lcl_interp *interp, int argc,
                                         lcl_value **argv, lcl_value **out) {
  struct lcl_lk_canvas *cw;

  if (argc != 1) {
    lcl_set_error(interp, "Lk::canvas_clear: expected 1 argument");

    return LCL_RC_ERR;
  }

  cw = get_canvas(interp, argv[0]);

  if (!cw) {
    return LCL_RC_ERR;
  }

  lk_canvas_clear(cw->cv);
  *out = lcl_string_new("");

  return LCL_RC_OK;
}

/* Lk::canvas_clip [c x y w h] -> "" -- open a sub-clip (nests to
 * LK_CANVAS_MAX_CLIP_DEPTH; deeper is a hard error). */
static lcl_return_code c_lk_canvas_clip(lcl_interp *interp, int argc,
                                        lcl_value **argv, lcl_value **out) {
  struct lcl_lk_canvas *cw;
  lk_i32 p[4];
  lk_rect r;

  if (argc != 5) {
    lcl_set_error(interp,
                  "Lk::canvas_clip: expected 5 arguments (c, x, y, w, h)");

    return LCL_RC_ERR;
  }

  cw = get_canvas(interp, argv[0]);

  if (!cw) {
    return LCL_RC_ERR;
  }

  if (!canvas_coords(interp, "Lk::canvas_clip", argv, 1, 4, p)) {
    return LCL_RC_ERR;
  }

  if (p[2] < 0 || p[3] < 0) {
    lcl_set_error(interp, "Lk::canvas_clip: w and h must be >= 0");

    return LCL_RC_ERR;
  }

  r.x = p[0];
  r.y = p[1];
  r.w = p[2];
  r.h = p[3];

  if (!lk_canvas_clip_begin(cw->cv, r)) {
    if (lk_canvas_clip_depth(cw->cv) >= LK_CANVAS_MAX_CLIP_DEPTH) {
      lcl_set_error(interp, "Lk::canvas_clip: clips nest at most 8 deep");
    } else {
      lcl_set_error(interp, "Lk::canvas_clip: out of memory");
    }

    return LCL_RC_ERR;
  }

  *out = lcl_string_new("");

  return LCL_RC_OK;
}

/* Lk::canvas_clip_end [c] -> "" -- close the innermost sub-clip; none
 * open is a hard error. */
static lcl_return_code c_lk_canvas_clip_end(lcl_interp *interp, int argc,
                                            lcl_value **argv,
                                            lcl_value **out) {
  struct lcl_lk_canvas *cw;

  if (argc != 1) {
    lcl_set_error(interp, "Lk::canvas_clip_end: expected 1 argument");

    return LCL_RC_ERR;
  }

  cw = get_canvas(interp, argv[0]);

  if (!cw) {
    return LCL_RC_ERR;
  }

  if (lk_canvas_clip_depth(cw->cv) == 0) {
    lcl_set_error(interp, "Lk::canvas_clip_end: no sub-clip is open");

    return LCL_RC_ERR;
  }

  if (!lk_canvas_clip_end(cw->cv)) {
    lcl_set_error(interp, "Lk::canvas_clip_end: out of memory");

    return LCL_RC_ERR;
  }

  *out = lcl_string_new("");

  return LCL_RC_OK;
}

/* ============================================================================
 * Span sets + styled text (docs/styled-text.md)
 * ============================================================================
 */

static struct lcl_lk_spans *get_spans(lcl_interp *interp, lcl_value *val) {
  struct lcl_lk_spans *sw = NULL;

  if (lcl_opaque_get(val, LK_SPANS_TYPE, (void **)&sw) != LCL_OK || !sw) {
    lcl_set_error(interp, "expected lk_spans opaque");
    return NULL;
  }

  return sw;
}

/* The span set's release hook: a presented value is an lcl-value box
 * registered in the wrapper's ui; free it once (annot store pattern). */
static void lcl_spans_pres_release(void *ud, lk_value v) {
  struct lcl_lk_spans *sw = (struct lcl_lk_spans *)ud;
  lk_resource_ref ref = lk_v_resource_ref(v);
  struct lcl_pres_box *box;

  if (ref.id == 0 || !live_ui_check(sw->ui)) {
    return;
  }

  box = (struct lcl_pres_box *)lk_resource_get(lk_ui_resources(sw->ui), ref,
                                               &g_lcl_value_type);

  if (!box) {
    return;
  }

  lk_resource_release(lk_ui_resources(sw->ui), ref);
  pres_box_free(box);
}

/* Lk::spans_new [ui] -> opaque<lk_spans> */
static lcl_return_code c_lk_spans_new(lcl_interp *interp, int argc,
                                      lcl_value **argv, lcl_value **out) {
  lk_ui *ui = NULL;
  lk_spans *sp;
  struct lcl_lk_spans *sw;
  lk_resource_ref ref;

  if (argc != 1) {
    lcl_set_error(interp, "Lk::spans_new: expected 1 argument (ui)");
    return LCL_RC_ERR;
  }

  if (lcl_opaque_get(argv[0], LK_UI_TYPE, (void **)&ui) != LCL_OK || !ui) {
    lcl_set_error(interp, "Lk::spans_new: expected lk_ui opaque");
    return LCL_RC_ERR;
  }

  sp = lk_spans_new(NULL, NULL, NULL);

  if (!sp) {
    lcl_set_error(interp, "Lk::spans_new: allocation failed");
    return LCL_RC_ERR;
  }

  ref = lk_resource_register(lk_ui_resources(ui), lk_spans_type(), sp,
                             "lcl-spans");

  if (ref.id == 0) {
    lk_spans_destroy(sp);
    lcl_set_error(interp, "Lk::spans_new: resource registration failed");
    return LCL_RC_ERR;
  }

  sw = (struct lcl_lk_spans *)malloc(sizeof(*sw));

  if (!sw) {
    lk_resource_release(lk_ui_resources(ui), ref);
    lk_spans_destroy(sp);
    lcl_set_error(interp, "Lk::spans_new: allocation failed");
    return LCL_RC_ERR;
  }

  sw->sp = sp;
  sw->ui = ui;
  sw->ref = ref;
  sw->ui_val = lcl_ref_inc(argv[0]);
  lk_spans_set_release(sp, lcl_spans_pres_release, sw);
  *out = lcl_opaque_new(sw, LK_SPANS_TYPE, spans_finalizer);

  return LCL_RC_OK;
}

/* Lk::spans_clear [s] -> "" */
static lcl_return_code c_lk_spans_clear(lcl_interp *interp, int argc,
                                        lcl_value **argv, lcl_value **out) {
  struct lcl_lk_spans *sw;

  if (argc != 1) {
    lcl_set_error(interp, "Lk::spans_clear: expected 1 argument");
    return LCL_RC_ERR;
  }

  sw = get_spans(interp, argv[0]);

  if (!sw) {
    return LCL_RC_ERR;
  }

  lk_spans_clear(sw->sp);
  *out = lcl_string_new("");

  return LCL_RC_OK;
}

/* Lk::spans_count [s] -> int */
static lcl_return_code c_lk_spans_count(lcl_interp *interp, int argc,
                                        lcl_value **argv, lcl_value **out) {
  struct lcl_lk_spans *sw;

  if (argc != 1) {
    lcl_set_error(interp, "Lk::spans_count: expected 1 argument");
    return LCL_RC_ERR;
  }

  sw = get_spans(interp, argv[0]);

  if (!sw) {
    return LCL_RC_ERR;
  }

  *out = lcl_int_new((lcl_int)lk_spans_count(sw->sp));

  return LCL_RC_OK;
}

/* start / end args shared by spans_add and spans_present */
static int spans_range_args(lcl_interp *interp, const char *proc,
                            lcl_value *a, lcl_value *b, lk_u32 *start,
                            lk_u32 *end) {
  lcl_int s, e;
  char err[96];

  if (lcl_value_to_int(a, &s) != LCL_OK || lcl_value_to_int(b, &e) != LCL_OK ||
      s < 0 || e < 0) {
    sprintf(err, "%.40s: start and end must be integers >= 0", proc);
    lcl_set_error(interp, err);
    return 0;
  }

  if (s >= e) {
    sprintf(err, "%.40s: start must be before end", proc);
    lcl_set_error(interp, err);
    return 0;
  }

  *start = (lk_u32)s;
  *end = (lk_u32)e;

  return 1;
}

/* Lk::spans_add [s start end style-dict] -> "" -- style keys fg / bg
 * ((r g b ?a) lists) and underline (0/1); at least one; unknown keys
 * and overlaps are hard errors. */
static lcl_return_code c_lk_spans_add(lcl_interp *interp, int argc,
                                      lcl_value **argv, lcl_value **out) {
  struct lcl_lk_spans *sw;
  lk_u32 start, end;
  lk_color fg, bg;
  lk_u8 flags = 0;
  lcl_value *v;
  lcl_value *keys = NULL;
  size_t i, nkeys;

  if (argc != 4) {
    lcl_set_error(interp,
                  "Lk::spans_add: expected 4 arguments "
                  "(s, start, end, style-dict)");
    return LCL_RC_ERR;
  }

  sw = get_spans(interp, argv[0]);

  if (!sw) {
    return LCL_RC_ERR;
  }

  if (!spans_range_args(interp, "Lk::spans_add", argv[1], argv[2], &start,
                        &end)) {
    return LCL_RC_ERR;
  }

  if (lcl_value_type_of(argv[3]) != LCL_DICT) {
    lcl_set_error(interp, "Lk::spans_add: style must be a dict");
    return LCL_RC_ERR;
  }

  memset(&fg, 0, sizeof(fg));
  memset(&bg, 0, sizeof(bg));

  if (lcl_dict_keys(argv[3], &keys) != LCL_OK) {
    keys = NULL;
  }

  nkeys = keys ? lcl_list_len(keys) : 0;

  for (i = 0; i < nkeys; i++) {
    const char *k = lcl_value_to_string(lcl_list_peek(keys, i));

    if (k && (strcmp(k, "fg") == 0 || strcmp(k, "bg") == 0 ||
              strcmp(k, "underline") == 0)) {
      continue;
    }

    lcl_ref_dec(keys);
    lcl_set_error(interp,
                  "Lk::spans_add: unknown style key (known: fg, bg, "
                  "underline)");
    return LCL_RC_ERR;
  }

  if (keys) {
    lcl_ref_dec(keys);
  }

  v = lcl_dict_peek(argv[3], "fg");

  if (v) {
    if (!canvas_color(interp, "Lk::spans_add", v, &fg)) {
      return LCL_RC_ERR;
    }

    flags |= LK_SPAN_FG;
  }

  v = lcl_dict_peek(argv[3], "bg");

  if (v) {
    if (!canvas_color(interp, "Lk::spans_add", v, &bg)) {
      return LCL_RC_ERR;
    }

    flags |= LK_SPAN_BG;
  }

  v = lcl_dict_peek(argv[3], "underline");

  if (v) {
    lcl_int u;

    if (lcl_value_to_int(v, &u) != LCL_OK) {
      lcl_set_error(interp, "Lk::spans_add: underline must be 0 or 1");
      return LCL_RC_ERR;
    }

    if (u) {
      flags |= LK_SPAN_UNDERLINE;
    }
  }

  if (flags == 0) {
    lcl_set_error(interp,
                  "Lk::spans_add: the style dict needs fg, bg or "
                  "underline (use Lk::spans_present for a bare range)");
    return LCL_RC_ERR;
  }

  if (!lk_spans_add(sw->sp, start, end, fg, bg, flags)) {
    lcl_set_error(interp,
                  "Lk::spans_add: range overlaps an existing span "
                  "(an identical range would merge)");
    return LCL_RC_ERR;
  }

  *out = lcl_string_new("");

  return LCL_RC_OK;
}

/* Lk::spans_present [s start end ptype value] -> "" -- a presentation
 * on a byte range: value may be ANY Lcl value (retained in an
 * lcl-value box in the set's ui, unwrapped in the command dict). */
static lcl_return_code c_lk_spans_present(lcl_interp *interp, int argc,
                                          lcl_value **argv, lcl_value **out) {
  struct lcl_lk_spans *sw;
  lk_u32 start, end;
  const char *ptype_str;
  struct lcl_pres_box *box;
  lk_resource_ref ref;
  lk_u32 type_id;

  if (argc != 5) {
    lcl_set_error(interp,
                  "Lk::spans_present: expected 5 arguments "
                  "(s, start, end, ptype, value)");
    return LCL_RC_ERR;
  }

  sw = get_spans(interp, argv[0]);

  if (!sw) {
    return LCL_RC_ERR;
  }

  if (!spans_range_args(interp, "Lk::spans_present", argv[1], argv[2], &start,
                        &end)) {
    return LCL_RC_ERR;
  }

  ptype_str = arg_name(interp, argv[3], "Lk::spans_present: ptype");

  if (!ptype_str) {
    return LCL_RC_ERR;
  }

  if (ptype_str[0] == '\0') {
    lcl_set_error(interp, "Lk::spans_present: ptype must be non-empty");
    return LCL_RC_ERR;
  }

  if (!live_ui_check(sw->ui)) {
    lcl_set_error(interp, "Lk::spans_present: the set's ui was destroyed");
    return LCL_RC_ERR;
  }

  box = (struct lcl_pres_box *)malloc(sizeof(*box));

  if (!box) {
    lcl_set_error(interp, "Lk::spans_present: allocation failed");
    return LCL_RC_ERR;
  }

  box->val = lcl_ref_inc(argv[4]);
  box->ui = sw->ui;
  ref = lk_resource_register(lk_ui_resources(sw->ui), &g_lcl_value_type, box,
                             "lcl-value");

  if (ref.id == 0) {
    lcl_ref_dec(box->val);
    free(box);
    lcl_set_error(interp, "Lk::spans_present: resource registration failed");
    return LCL_RC_ERR;
  }

  type_id = lk_intern_cid(sw->ui->intern, ptype_str);

  if (!lk_spans_add_present(sw->sp, start, end, type_id, lk_v_resource(ref))) {
    lk_resource_release(lk_ui_resources(sw->ui), ref);
    lcl_ref_dec(box->val);
    free(box);
    lcl_set_error(interp,
                  "Lk::spans_present: range overlaps an existing span");
    return LCL_RC_ERR;
  }

  pres_box_link(box);
  *out = lcl_string_new("");

  return LCL_RC_OK;
}

/* Lk::styled_text_pos_at [ui id x y] -> byte position, or -1 when the
 * node is unknown / not laid out / the point is outside it. */
static lcl_return_code c_lk_styled_text_pos_at(lcl_interp *interp, int argc,
                                               lcl_value **argv,
                                               lcl_value **out) {
  lk_ui *ui = NULL;
  const char *id_str;
  lcl_int x, y;
  lk_u32 pos = 0;

  if (argc != 4) {
    lcl_set_error(interp,
                  "Lk::styled_text_pos_at: expected 4 arguments "
                  "(ui, id, x, y)");
    return LCL_RC_ERR;
  }

  if (lcl_opaque_get(argv[0], LK_UI_TYPE, (void **)&ui) != LCL_OK || !ui) {
    lcl_set_error(interp, "Lk::styled_text_pos_at: expected lk_ui opaque");
    return LCL_RC_ERR;
  }

  id_str = arg_name(interp, argv[1], "Lk::styled_text_pos_at: node id");

  if (!id_str) {
    return LCL_RC_ERR;
  }

  if (lcl_value_to_int(argv[2], &x) != LCL_OK ||
      lcl_value_to_int(argv[3], &y) != LCL_OK) {
    lcl_set_error(interp, "Lk::styled_text_pos_at: x and y must be integers");
    return LCL_RC_ERR;
  }

  if (lk_styled_text_pos_at(ui, lk_intern_cid(ui->intern, id_str), (lk_i32)x,
                            (lk_i32)y, &pos)) {
    *out = lcl_int_new((lcl_int)pos);
  } else {
    *out = lcl_int_new(-1);
  }

  return LCL_RC_OK;
}

/* ============================================================================
 * Context menus (docs/context-menu.md)
 * ============================================================================
 */

/* Lk::context_menu [ui x y] -> item count (0 = nothing applicable, no
 * menu).  Hit-tests the ui-owned rects, runs the producer, opens at
 * the cursor. */
static lcl_return_code c_lk_context_menu(lcl_interp *interp, int argc,
                                         lcl_value **argv, lcl_value **out) {
  lk_ui *ui = NULL;
  lcl_int x, y;

  if (argc != 3) {
    lcl_set_error(interp, "Lk::context_menu: expected 3 arguments (ui, x, y)");
    return LCL_RC_ERR;
  }

  if (lcl_opaque_get(argv[0], LK_UI_TYPE, (void **)&ui) != LCL_OK || !ui) {
    lcl_set_error(interp, "Lk::context_menu: expected lk_ui opaque");
    return LCL_RC_ERR;
  }

  if (lcl_value_to_int(argv[1], &x) != LCL_OK ||
      lcl_value_to_int(argv[2], &y) != LCL_OK) {
    lcl_set_error(interp, "Lk::context_menu: x and y must be integers");
    return LCL_RC_ERR;
  }

  *out = lcl_int_new(
      (lcl_int)lk_menu_open_context(ui, lk_ui_tree(ui), (lk_i32)x, (lk_i32)y));

  return LCL_RC_OK;
}

/* Lk::context_menu_focus [ui] -> item count -- the keyboard opener. */
static lcl_return_code c_lk_context_menu_focus(lcl_interp *interp, int argc,
                                               lcl_value **argv,
                                               lcl_value **out) {
  lk_ui *ui = NULL;

  if (argc != 1) {
    lcl_set_error(interp, "Lk::context_menu_focus: expected 1 argument");
    return LCL_RC_ERR;
  }

  if (lcl_opaque_get(argv[0], LK_UI_TYPE, (void **)&ui) != LCL_OK || !ui) {
    lcl_set_error(interp, "Lk::context_menu_focus: expected lk_ui opaque");
    return LCL_RC_ERR;
  }

  *out = lcl_int_new(
      (lcl_int)lk_menu_open_context_at_focus(ui, lk_ui_tree(ui)));

  return LCL_RC_OK;
}

/* One explicit item from a list `(label Cmd ?arg..)`, `(---)`, or a
 * dict #{label command args enabled}. */
static int menu_item_from_lcl(lcl_interp *interp, lk_ui *ui, lcl_value *v,
                              lk_node_id owner, lk_menu_item *it) {
  lk_tree *t = (lk_tree *)lk_ui_tree(ui); /* interning only */

  memset(it, 0, sizeof(*it));
  it->enabled = 1;
  it->translator_ix = LK_MENU_NO_TRANSLATOR;
  it->node_id = owner;

  if (lcl_value_type_of(v) == LCL_LIST) {
    size_t n = lcl_list_len(v);
    size_t i;
    const char *first;

    if (n == 0) {
      lcl_set_error(interp, "Lk::menu_open: an item list must not be empty");
      return 0;
    }

    first = lcl_value_to_string(lcl_list_peek(v, 0));

    if (first && strcmp(first, "---") == 0) {
      it->separator = 1;
      it->enabled = 0;
      return 1;
    }

    if (n < 2) {
      lcl_set_error(interp,
                    "Lk::menu_open: an item is (label command ?args..) "
                    "or (---)");
      return 0;
    }

    it->label = lk_intern_cid(ui->intern, first ? first : "");
    it->command_name = lk_intern_cid(
        ui->intern, lcl_value_to_string(lcl_list_peek(v, 1)));

    for (i = 2; i < n && it->arg_count < LK_PRES_MAX_ARGS; i++) {
      it->args[it->arg_count++] = coerce_lk_value(t, lcl_list_peek(v, i));
    }

    return 1;
  }

  if (lcl_value_type_of(v) == LCL_DICT) {
    lcl_value *keys = NULL;
    lcl_value *e;
    size_t nk, i;

    if (lcl_dict_keys(v, &keys) != LCL_OK) {
      keys = NULL;
    }

    nk = keys ? lcl_list_len(keys) : 0;

    for (i = 0; i < nk; i++) {
      const char *k = lcl_value_to_string(lcl_list_peek(keys, i));

      if (k && (strcmp(k, "label") == 0 || strcmp(k, "command") == 0 ||
                strcmp(k, "args") == 0 || strcmp(k, "enabled") == 0)) {
        continue;
      }

      lcl_ref_dec(keys);
      lcl_set_error(interp,
                    "Lk::menu_open: unknown item key (known: label, "
                    "command, args, enabled)");
      return 0;
    }

    if (keys) {
      lcl_ref_dec(keys);
    }

    e = lcl_dict_peek(v, "command");

    if (!e) {
      lcl_set_error(interp, "Lk::menu_open: an item dict needs `command`");
      return 0;
    }

    it->command_name = lk_intern_cid(ui->intern, lcl_value_to_string(e));
    e = lcl_dict_peek(v, "label");
    it->label = e ? lk_intern_cid(ui->intern, lcl_value_to_string(e))
                  : it->command_name;
    e = lcl_dict_peek(v, "enabled");

    if (e) {
      lcl_int en;

      if (lcl_value_to_int(e, &en) != LCL_OK) {
        lcl_set_error(interp, "Lk::menu_open: enabled must be 0 or 1");
        return 0;
      }

      it->enabled = en ? 1 : 0;
    }

    e = lcl_dict_peek(v, "args");

    if (e) {
      if (lcl_value_type_of(e) == LCL_LIST) {
        size_t n = lcl_list_len(e);

        for (i = 0; i < n && it->arg_count < LK_PRES_MAX_ARGS; i++) {
          it->args[it->arg_count++] = coerce_lk_value(t, lcl_list_peek(e, i));
        }
      } else {
        it->args[it->arg_count++] = coerce_lk_value(t, e);
      }
    }

    return 1;
  }

  lcl_set_error(interp, "Lk::menu_open: items must be lists or dicts");
  return 0;
}

/* Lk::menu_open [ui spec] -> 1/0.  spec: #{items (...) ?owner id
 * ?anchor below|above|at_cursor|center ?x N ?y N}. */
static lcl_return_code c_lk_menu_open(lcl_interp *interp, int argc,
                                      lcl_value **argv, lcl_value **out) {
  lk_ui *ui = NULL;
  lcl_value *v;
  lcl_value *items;
  lk_menu_item buf[LK_MENU_MAX_ITEMS];
  size_t n, i;
  lk_node_id owner = 0;
  int anchor = -1;
  lcl_int x = 0, y = 0;

  if (argc != 2) {
    lcl_set_error(interp, "Lk::menu_open: expected 2 arguments (ui, spec)");
    return LCL_RC_ERR;
  }

  if (lcl_opaque_get(argv[0], LK_UI_TYPE, (void **)&ui) != LCL_OK || !ui) {
    lcl_set_error(interp, "Lk::menu_open: expected lk_ui opaque");
    return LCL_RC_ERR;
  }

  if (lcl_value_type_of(argv[1]) != LCL_DICT) {
    lcl_set_error(interp, "Lk::menu_open: spec must be a dict");
    return LCL_RC_ERR;
  }

  items = lcl_dict_peek(argv[1], "items");

  if (!items || lcl_value_type_of(items) != LCL_LIST ||
      lcl_list_len(items) == 0) {
    lcl_set_error(interp, "Lk::menu_open: spec needs a non-empty `items` list");
    return LCL_RC_ERR;
  }

  v = lcl_dict_peek(argv[1], "owner");

  if (v) {
    const char *os = arg_name(interp, v, "Lk::menu_open: owner");

    if (!os) {
      return LCL_RC_ERR;
    }

    if (os[0] != '\0') {
      owner = lk_intern_cid(ui->intern, os);
    }
  }

  v = lcl_dict_peek(argv[1], "anchor");

  if (v) {
    if (!lookup_enum(anchor_table, lcl_value_to_string(v), &anchor)) {
      lcl_set_error(interp,
                    "Lk::menu_open: anchor must be below, above, at_cursor "
                    "or center");
      return LCL_RC_ERR;
    }
  } else {
    anchor = owner ? LK_ANCHOR_BELOW : LK_ANCHOR_AT_CURSOR;
  }

  v = lcl_dict_peek(argv[1], "x");

  if (v && lcl_value_to_int(v, &x) != LCL_OK) {
    lcl_set_error(interp, "Lk::menu_open: x must be an integer");
    return LCL_RC_ERR;
  }

  v = lcl_dict_peek(argv[1], "y");

  if (v && lcl_value_to_int(v, &y) != LCL_OK) {
    lcl_set_error(interp, "Lk::menu_open: y must be an integer");
    return LCL_RC_ERR;
  }

  n = lcl_list_len(items);

  if (n > LK_MENU_MAX_ITEMS) {
    lcl_set_error(interp, "Lk::menu_open: at most 64 items");
    return LCL_RC_ERR;
  }

  for (i = 0; i < n; i++) {
    if (!menu_item_from_lcl(interp, ui, lcl_list_peek(items, i), owner,
                            &buf[i])) {
      return LCL_RC_ERR;
    }
  }

  /* An item's node: the owner when given, else the root (so a menu
   * with no owner still has a source node). */
  if (owner == 0) {
    const lk_tree *t = lk_ui_tree(ui);
    lk_node_id root_id = (t && t->root) ? t->nodes[t->root].id : 0;

    for (i = 0; i < n; i++) {
      buf[i].node_id = root_id;
    }
  }

  *out = lcl_int_new((lcl_int)lk_menu_open(ui, owner, (lk_u8)anchor, (lk_i32)x,
                                        (lk_i32)y, buf, (lk_u32)n));

  return LCL_RC_OK;
}

/* Lk::menu_items [ui] -> list of #{label command accel enabled
 * separator}, () when no menu is open. */
static lcl_return_code c_lk_menu_items(lcl_interp *interp, int argc,
                                       lcl_value **argv, lcl_value **out) {
  lk_ui *ui = NULL;
  lcl_value *list;
  lk_u32 i, n;

  if (argc != 1) {
    lcl_set_error(interp, "Lk::menu_items: expected 1 argument");
    return LCL_RC_ERR;
  }

  if (lcl_opaque_get(argv[0], LK_UI_TYPE, (void **)&ui) != LCL_OK || !ui) {
    lcl_set_error(interp, "Lk::menu_items: expected lk_ui opaque");
    return LCL_RC_ERR;
  }

  list = lcl_list_new();
  n = lk_menu_count(ui);

  for (i = 0; i < n; i++) {
    const lk_menu_item *it = lk_menu_item_get(ui, i);
    lcl_value *d = lcl_dict_new();
    lcl_value *v;
    const char *s;

    s = it->label ? lk_intern_cstr(ui->intern, it->label) : "";
    v = lcl_string_new(s ? s : "");
    lcl_dict_put(&d, "label", v);
    lcl_ref_dec(v);
    s = it->command_name ? lk_intern_cstr(ui->intern, it->command_name) : "";
    v = lcl_string_new(s ? s : "");
    lcl_dict_put(&d, "command", v);
    lcl_ref_dec(v);
    s = it->accel ? lk_intern_cstr(ui->intern, it->accel) : "";
    v = lcl_string_new(s ? s : "");
    lcl_dict_put(&d, "accel", v);
    lcl_ref_dec(v);
    v = lcl_int_new((lcl_int)it->enabled);
    lcl_dict_put(&d, "enabled", v);
    lcl_ref_dec(v);
    v = lcl_int_new((lcl_int)it->separator);
    lcl_dict_put(&d, "separator", v);
    lcl_ref_dec(v);
    lcl_list_push(&list, d);
    lcl_ref_dec(d);
  }

  *out = list;

  return LCL_RC_OK;
}

/* Lk::menu_hover [ui] -> hovered index, -1 when none / closed */
static lcl_return_code c_lk_menu_hover(lcl_interp *interp, int argc,
                                       lcl_value **argv, lcl_value **out) {
  lk_ui *ui = NULL;

  if (argc != 1) {
    lcl_set_error(interp, "Lk::menu_hover: expected 1 argument");
    return LCL_RC_ERR;
  }

  if (lcl_opaque_get(argv[0], LK_UI_TYPE, (void **)&ui) != LCL_OK || !ui) {
    lcl_set_error(interp, "Lk::menu_hover: expected lk_ui opaque");
    return LCL_RC_ERR;
  }

  *out = lcl_int_new((lcl_int)lk_menu_hover(ui));

  return LCL_RC_OK;
}

/* Lk::menu_activate [ui index] -> 1/0 */
static lcl_return_code c_lk_menu_activate(lcl_interp *interp, int argc,
                                          lcl_value **argv, lcl_value **out) {
  lk_ui *ui = NULL;
  lcl_int i;

  if (argc != 2) {
    lcl_set_error(interp, "Lk::menu_activate: expected 2 arguments (ui, index)");
    return LCL_RC_ERR;
  }

  if (lcl_opaque_get(argv[0], LK_UI_TYPE, (void **)&ui) != LCL_OK || !ui) {
    lcl_set_error(interp, "Lk::menu_activate: expected lk_ui opaque");
    return LCL_RC_ERR;
  }

  if (lcl_value_to_int(argv[1], &i) != LCL_OK || i < 0) {
    lcl_set_error(interp, "Lk::menu_activate: index must be an integer >= 0");
    return LCL_RC_ERR;
  }

  *out = lcl_int_new((lcl_int)lk_menu_activate(ui, (lk_u32)i));

  return LCL_RC_OK;
}

/* Lk::menu_close [ui] -> "" */
static lcl_return_code c_lk_menu_close(lcl_interp *interp, int argc,
                                       lcl_value **argv, lcl_value **out) {
  lk_ui *ui = NULL;

  if (argc != 1) {
    lcl_set_error(interp, "Lk::menu_close: expected 1 argument");
    return LCL_RC_ERR;
  }

  if (lcl_opaque_get(argv[0], LK_UI_TYPE, (void **)&ui) != LCL_OK || !ui) {
    lcl_set_error(interp, "Lk::menu_close: expected lk_ui opaque");
    return LCL_RC_ERR;
  }

  lk_menu_close(ui);
  *out = lcl_string_new("");

  return LCL_RC_OK;
}

/* ============================================================================
 * Virtualized list (docs/table.md)
 * ============================================================================
 */

/* Lk::list_range [ui id] -> (first count) or () before any layout */
static lcl_return_code c_lk_list_range(lcl_interp *interp, int argc,
                                       lcl_value **argv, lcl_value **out) {
  lk_ui *ui = NULL;
  const char *id_str;
  lk_i32 first = 0, count = 0;
  lcl_value *list;

  if (argc != 2) {
    lcl_set_error(interp, "Lk::list_range: expected 2 arguments (ui, id)");
    return LCL_RC_ERR;
  }

  if (lcl_opaque_get(argv[0], LK_UI_TYPE, (void **)&ui) != LCL_OK || !ui) {
    lcl_set_error(interp, "Lk::list_range: expected lk_ui opaque");
    return LCL_RC_ERR;
  }

  id_str = arg_name(interp, argv[1], "Lk::list_range: node id");

  if (!id_str) {
    return LCL_RC_ERR;
  }

  list = lcl_list_new();

  if (lk_list_range(ui, lk_intern_cid(ui->intern, id_str), &first, &count)) {
    lcl_value *v;

    v = lcl_int_new((lcl_int)first);
    lcl_list_push(&list, v);
    lcl_ref_dec(v);
    v = lcl_int_new((lcl_int)count);
    lcl_list_push(&list, v);
    lcl_ref_dec(v);
  }

  *out = list;

  return LCL_RC_OK;
}

/* Lk::list_scroll_to [ui id row] -> 1/0 */
static lcl_return_code c_lk_list_scroll_to(lcl_interp *interp, int argc,
                                           lcl_value **argv, lcl_value **out) {
  lk_ui *ui = NULL;
  const char *id_str;
  lcl_int row;

  if (argc != 3) {
    lcl_set_error(interp,
                  "Lk::list_scroll_to: expected 3 arguments (ui, id, row)");
    return LCL_RC_ERR;
  }

  if (lcl_opaque_get(argv[0], LK_UI_TYPE, (void **)&ui) != LCL_OK || !ui) {
    lcl_set_error(interp, "Lk::list_scroll_to: expected lk_ui opaque");
    return LCL_RC_ERR;
  }

  id_str = arg_name(interp, argv[1], "Lk::list_scroll_to: node id");

  if (!id_str) {
    return LCL_RC_ERR;
  }

  if (lcl_value_to_int(argv[2], &row) != LCL_OK || row < 0) {
    lcl_set_error(interp, "Lk::list_scroll_to: row must be an integer >= 0");
    return LCL_RC_ERR;
  }

  *out = lcl_int_new((lcl_int)lk_list_scroll_to_row(
      ui, lk_intern_cid(ui->intern, id_str), (lk_i32)row));

  return LCL_RC_OK;
}

/* Lk::canvas_op_count [c] -> int */
static lcl_return_code c_lk_canvas_op_count(lcl_interp *interp, int argc,
                                            lcl_value **argv,
                                            lcl_value **out) {
  struct lcl_lk_canvas *cw;

  if (argc != 1) {
    lcl_set_error(interp, "Lk::canvas_op_count: expected 1 argument");

    return LCL_RC_ERR;
  }

  cw = get_canvas(interp, argv[0]);

  if (!cw) {
    return LCL_RC_ERR;
  }

  *out = lcl_int_new((lcl_int)lk_canvas_op_count(cw->cv));

  return LCL_RC_OK;
}

/* Lk::canvas_line [c x0 y0 x1 y1 color ?stroke] */
static lcl_return_code c_lk_canvas_line(lcl_interp *interp, int argc,
                                        lcl_value **argv, lcl_value **out) {
  struct lcl_lk_canvas *cw;
  lk_i32 p[4];
  lk_color color;
  lk_u8 stroke;

  if (argc != 6 && argc != 7) {
    lcl_set_error(interp,
                  "Lk::canvas_line: expected 6 or 7 arguments "
                  "(c, x0, y0, x1, y1, color, ?stroke)");

    return LCL_RC_ERR;
  }

  cw = get_canvas(interp, argv[0]);

  if (!cw) {
    return LCL_RC_ERR;
  }

  if (!canvas_coords(interp, "Lk::canvas_line", argv, 1, 4, p) ||
      !canvas_color(interp, "Lk::canvas_line", argv[5], &color) ||
      !canvas_stroke(interp, "Lk::canvas_line", argc, argv, 6, &stroke)) {
    return LCL_RC_ERR;
  }

  if (!lk_canvas_line(cw->cv, p[0], p[1], p[2], p[3], color, stroke)) {
    lcl_set_error(interp, "Lk::canvas_line: allocation failed");

    return LCL_RC_ERR;
  }

  *out = lcl_string_new("");

  return LCL_RC_OK;
}

/* Lk::canvas_polyline [c points color ?stroke] -- points is a FLAT
 * (x0 y0 x1 y1 ...) list, even length >= 4. */
static lcl_return_code c_lk_canvas_polyline(lcl_interp *interp, int argc,
                                            lcl_value **argv,
                                            lcl_value **out) {
  struct lcl_lk_canvas *cw;
  lk_color color;
  lk_u8 stroke;
  size_t len;
  size_t i;
  lk_i32 *xy;
  int ok;

  if (argc != 3 && argc != 4) {
    lcl_set_error(interp,
                  "Lk::canvas_polyline: expected 3 or 4 arguments "
                  "(c, points, color, ?stroke)");

    return LCL_RC_ERR;
  }

  cw = get_canvas(interp, argv[0]);

  if (!cw) {
    return LCL_RC_ERR;
  }

  if (lcl_value_type_of(argv[1]) != LCL_LIST) {
    lcl_set_error(interp,
                  "Lk::canvas_polyline: points must be a flat (x0 y0 x1 y1 "
                  "...) list");

    return LCL_RC_ERR;
  }

  len = lcl_list_len(argv[1]);

  if (len < 4 || (len % 2) != 0 || len / 2 > LK_CANVAS_MAX_POINTS) {
    lcl_set_error(interp,
                  "Lk::canvas_polyline: points must hold an even number of "
                  "coordinates, at least 2 points, at most 65536");

    return LCL_RC_ERR;
  }

  if (!canvas_color(interp, "Lk::canvas_polyline", argv[2], &color) ||
      !canvas_stroke(interp, "Lk::canvas_polyline", argc, argv, 3, &stroke)) {
    return LCL_RC_ERR;
  }

  xy = (lk_i32 *)malloc(sizeof(lk_i32) * len);

  if (!xy) {
    lcl_set_error(interp, "Lk::canvas_polyline: allocation failed");

    return LCL_RC_ERR;
  }

  for (i = 0; i < len; i++) {
    lcl_value *v = lcl_list_peek(argv[1], i);

    if (!v || !canvas_coord(v, &xy[i])) {
      free(xy);
      lcl_set_error(interp, "Lk::canvas_polyline: coordinates must be numbers");

      return LCL_RC_ERR;
    }
  }

  ok = lk_canvas_polyline(cw->cv, xy, (lk_u32)(len / 2), color, stroke);
  free(xy);

  if (!ok) {
    lcl_set_error(interp, "Lk::canvas_polyline: allocation failed");

    return LCL_RC_ERR;
  }

  *out = lcl_string_new("");

  return LCL_RC_OK;
}

/* Shared body of canvas_rect / canvas_fill_rect. */
static lcl_return_code canvas_rect_common(lcl_interp *interp, int argc,
                                          lcl_value **argv, lcl_value **out,
                                          int fill) {
  const char *proc = fill ? "Lk::canvas_fill_rect" : "Lk::canvas_rect";
  struct lcl_lk_canvas *cw;
  lk_i32 p[4];
  lk_rect r;
  lk_color color;
  lk_u8 stroke = 1;
  int ok;

  if ((fill && argc != 6) || (!fill && argc != 6 && argc != 7)) {
    lcl_set_error(interp,
                  fill ? "Lk::canvas_fill_rect: expected 6 arguments "
                         "(c, x, y, w, h, color)"
                       : "Lk::canvas_rect: expected 6 or 7 arguments "
                         "(c, x, y, w, h, color, ?stroke)");

    return LCL_RC_ERR;
  }

  cw = get_canvas(interp, argv[0]);

  if (!cw) {
    return LCL_RC_ERR;
  }

  if (!canvas_coords(interp, proc, argv, 1, 4, p) ||
      !canvas_color(interp, proc, argv[5], &color)) {
    return LCL_RC_ERR;
  }

  if (!fill && !canvas_stroke(interp, proc, argc, argv, 6, &stroke)) {
    return LCL_RC_ERR;
  }

  r.x = p[0];
  r.y = p[1];
  r.w = p[2];
  r.h = p[3];

  ok = fill ? lk_canvas_fill_rect(cw->cv, r, color)
            : lk_canvas_rect(cw->cv, r, color, stroke);

  if (!ok) {
    char err[96];

    sprintf(err, "%.40s: allocation failed", proc);
    lcl_set_error(interp, err);

    return LCL_RC_ERR;
  }

  *out = lcl_string_new("");

  return LCL_RC_OK;
}

/* Lk::canvas_rect [c x y w h color ?stroke] (outline) */
static lcl_return_code c_lk_canvas_rect(lcl_interp *interp, int argc,
                                        lcl_value **argv, lcl_value **out) {
  return canvas_rect_common(interp, argc, argv, out, 0);
}

/* Lk::canvas_fill_rect [c x y w h color] */
static lcl_return_code c_lk_canvas_fill_rect(lcl_interp *interp, int argc,
                                             lcl_value **argv,
                                             lcl_value **out) {
  return canvas_rect_common(interp, argc, argv, out, 1);
}

/* Lk::canvas_text [c x y text color] -- drawn with the node's font. */
static lcl_return_code c_lk_canvas_text(lcl_interp *interp, int argc,
                                        lcl_value **argv, lcl_value **out) {
  struct lcl_lk_canvas *cw;
  lk_i32 p[2];
  lk_color color;
  const char *text;

  if (argc != 5) {
    lcl_set_error(interp,
                  "Lk::canvas_text: expected 5 arguments "
                  "(c, x, y, text, color)");

    return LCL_RC_ERR;
  }

  cw = get_canvas(interp, argv[0]);

  if (!cw) {
    return LCL_RC_ERR;
  }

  if (!canvas_coords(interp, "Lk::canvas_text", argv, 1, 2, p) ||
      !canvas_color(interp, "Lk::canvas_text", argv[4], &color)) {
    return LCL_RC_ERR;
  }

  text = lcl_value_to_string(argv[3]);

  if (!text) {
    lcl_set_error(interp, "Lk::canvas_text: text must be a string");

    return LCL_RC_ERR;
  }

  if (!lk_canvas_text(cw->cv, p[0], p[1], text, (lk_u32)strlen(text),
                      color)) {
    lcl_set_error(interp, "Lk::canvas_text: allocation failed");

    return LCL_RC_ERR;
  }

  *out = lcl_string_new("");

  return LCL_RC_OK;
}

/* ============================================================================
 * SDL Window procs (8) — compiled only when LK_HAVE_SDL is set
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
  args[1] = lcl_int_new((lcl_int)node_ix);

  rc = lcl_call_proc(lw->interp, lw->event_handler, 2, args, &result);

  if (rc != LCL_RC_OK) {
    lcl_lk_report_callback_error(lw->interp, "Event handler");
  }

  if (rc == LCL_RC_OK && result) {
    lcl_int handled;
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
    lcl_lk_report_callback_error(ctx->interp, "Frame");
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

  /* Release what the binding hung on the ui before window destroy
   * frees it (lw->win is NULL after an explicit Lk::window_destroy). */
  ui = lk_window_ui(lw->win);
  ui_teardown_bindings(ui);

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

/* Lk::window_create [title, ?w, ?h, ?font, ?size] -> opaque<lk_window> */
static lcl_return_code c_lk_window_create(lcl_interp *interp, int argc,
                                          lcl_value **argv, lcl_value **out) {
  lk_window_cfg cfg;
  lk_window *win;
  struct lcl_lk_window *lw;

  if (argc < 1 || argc > 5) {
    lcl_set_error(interp, "Lk::window_create: expected 1-5 arguments");

    return LCL_RC_ERR;
  }

  memset(&cfg, 0, sizeof(cfg));
  cfg.title = lcl_value_to_string(argv[0]);
  cfg.width = 800;
  cfg.height = 600;
  cfg.font_size = 0;
  cfg.font_path = NULL;

  if (argc >= 2) {
    lcl_int w;

    if (lcl_value_to_int(argv[1], &w) == LCL_OK) {
      cfg.width = (int)w;
    }
  }

  if (argc >= 3) {
    lcl_int h;

    if (lcl_value_to_int(argv[2], &h) == LCL_OK) {
      cfg.height = (int)h;
    }
  }

  if (argc >= 4) {
    cfg.font_path = lcl_value_to_string(argv[3]);
  }

  if (argc >= 5) {
    lcl_int sz;

    if (lcl_value_to_int(argv[4], &sz) == LCL_OK) {
      cfg.font_size = (int)sz;
    }
  }

  win = lk_window_create(&cfg);

  if (!win) {
    lcl_set_error(interp, "Lk::window_create: failed to create window");

    return LCL_RC_ERR;
  }

  lw = (struct lcl_lk_window *)malloc(sizeof(*lw));

  if (!lw) {
    lk_window_destroy(win);
    lcl_set_error(interp, "Lk::window_create: allocation failed");

    return LCL_RC_ERR;
  }

  lw->win = win;
  lw->interp = interp;
  lw->event_handler = NULL;

  live_ui_add(lk_window_ui(win));
  *out = lcl_opaque_new(lw, LK_WIN_TYPE, lcl_lk_window_finalizer);

  return LCL_RC_OK;
}

/* Lk::window_destroy [win] -> "" */
static lcl_return_code c_lk_window_destroy(lcl_interp *interp, int argc,
                                           lcl_value **argv, lcl_value **out) {
  struct lcl_lk_window *lw;

  if (argc != 1) {
    lcl_set_error(interp, "Lk::window_destroy: expected 1 argument");

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

  ui_teardown_bindings(lk_window_ui(lw->win));
  lk_window_destroy(lw->win);
  lw->win = NULL;

  *out = lcl_string_new("");

  return LCL_RC_OK;
}

/* Lk::window_run [win, view_proc] -> "" (blocks until close) */
static lcl_return_code c_lk_window_run(lcl_interp *interp, int argc,
                                       lcl_value **argv, lcl_value **out) {
  struct lcl_lk_window *lw;
  struct lcl_lk_frame_ctx frame_ctx;

  if (argc != 2) {
    lcl_set_error(interp, "Lk::window_run: expected 2 arguments");
    return LCL_RC_ERR;
  }

  lw = get_lk_window(interp, argv[0]);

  if (!lw) {
    return LCL_RC_ERR;
  }

  if (!lcl_is_callable(argv[1])) {
    lcl_set_error(interp, "Lk::window_run: expected callable view proc");

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

/* Lk::window_ui [win] -> opaque<lk_ui> */
static lcl_return_code c_lk_window_ui(lcl_interp *interp, int argc,
                                      lcl_value **argv, lcl_value **out) {
  struct lcl_lk_window *lw;
  lk_ui *ui;

  if (argc != 1) {
    lcl_set_error(interp, "Lk::window_ui: expected 1 argument");

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

/* Lk::window_set_event_handler [win, handler_proc] -> "" */
static lcl_return_code c_lk_window_set_event_handler(lcl_interp *interp,
                                                     int argc, lcl_value **argv,
                                                     lcl_value **out) {
  struct lcl_lk_window *lw;

  if (argc != 2) {
    lcl_set_error(interp, "Lk::window_set_event_handler: expected 2 arguments");

    return LCL_RC_ERR;
  }

  lw = get_lk_window(interp, argv[0]);

  if (!lw) {
    return LCL_RC_ERR;
  }

  if (!lcl_is_callable(argv[1])) {
    lcl_set_error(interp, "Lk::window_set_event_handler: expected callable");

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

/* Lk::register_font [win, path] -> font_id (int; 0 = failure).
 * Mirrors the C contract: bad arguments are errors, but an unreadable
 * path returns 0 rather than erroring. */
static lcl_return_code c_lk_register_font(lcl_interp *interp, int argc,
                                          lcl_value **argv, lcl_value **out) {
  struct lcl_lk_window *lw;
  const char *path;
  lk_u16 id;

  if (argc != 2) {
    lcl_set_error(interp, "Lk::register_font: expected 2 arguments (win, path)");

    return LCL_RC_ERR;
  }

  lw = get_lk_window(interp, argv[0]);

  if (!lw) {
    return LCL_RC_ERR;
  }

  path = lcl_value_to_string(argv[1]);
  id = lk_window_register_font(lw->win, path);

  *out = lcl_int_new((lcl_int)id);

  return LCL_RC_OK;
}

/* Lk::window_icon [win, path] -> 1 | 0.
 * Sets the window icon from an image file (PNG with SDL3_image; BMP
 * always).  Mirrors lk_window_set_icon: bad arguments are errors, an
 * unreadable/undecodable file returns 0 -- the window keeps its
 * previous icon. */
static lcl_return_code c_lk_window_icon(lcl_interp *interp, int argc,
                                        lcl_value **argv, lcl_value **out) {
  struct lcl_lk_window *lw;
  const char *path;

  if (argc != 2) {
    lcl_set_error(interp, "Lk::window_icon: expected 2 arguments (win, path)");

    return LCL_RC_ERR;
  }

  lw = get_lk_window(interp, argv[0]);

  if (!lw) {
    return LCL_RC_ERR;
  }

  path = lcl_value_to_string(argv[1]);

  *out = lcl_int_new((lcl_int)lk_window_set_icon(lw->win, path));

  return LCL_RC_OK;
}

/* Lk::window_screenshot [win path] -> 1 | 0.  Queues a screenshot
 * saved at the end of the current run-loop iteration (see
 * lk_window_request_screenshot); .png via SDL3_image else BMP. */
static lcl_return_code c_lk_window_screenshot(lcl_interp *interp, int argc,
                                              lcl_value **argv,
                                              lcl_value **out) {
  struct lcl_lk_window *lw;
  const char *path;

  if (argc != 2) {
    lcl_set_error(interp,
                  "Lk::window_screenshot: expected 2 arguments (win, path)");

    return LCL_RC_ERR;
  }

  lw = get_lk_window(interp, argv[0]);

  if (!lw) {
    return LCL_RC_ERR;
  }

  path = lcl_value_to_string(argv[1]);

  if (!path || !path[0]) {
    lcl_set_error(interp, "Lk::window_screenshot: path must be a non-empty "
                          "string");

    return LCL_RC_ERR;
  }

  *out = lcl_int_new((lcl_int)lk_window_request_screenshot(lw->win, path));

  return LCL_RC_OK;
}

/* Lk::window_stop [win] -> "".  Leaves Lk::window_run after the
 * current frame. */
static lcl_return_code c_lk_window_stop(lcl_interp *interp, int argc,
                                        lcl_value **argv, lcl_value **out) {
  struct lcl_lk_window *lw;

  if (argc != 1) {
    lcl_set_error(interp, "Lk::window_stop: expected 1 argument (win)");

    return LCL_RC_ERR;
  }

  lw = get_lk_window(interp, argv[0]);

  if (!lw) {
    return LCL_RC_ERR;
  }

  lk_window_stop(lw->win);
  *out = lcl_string_new("");

  return LCL_RC_OK;
}

/* Lk::window_icon_hex [win, hex] -> 1 | 0.
 * Sets the window icon from an encoded image (PNG/BMP, same decoders
 * as Lk::window_icon) given as a hex string -- the script-side route
 * for embedding an icon in a single-file script or static binary,
 * since Lcl strings cannot carry raw bytes.  Whitespace between hex
 * digits is ignored; any other character, an odd digit count, or an
 * empty payload is a hard error.  Returns 0 only when decoding fails. */
static lcl_return_code c_lk_window_icon_hex(lcl_interp *interp, int argc,
                                            lcl_value **argv, lcl_value **out) {
  struct lcl_lk_window *lw;
  const char *hex;
  unsigned char *bytes;
  lk_u32 n = 0;
  int ok;

  if (argc != 2) {
    lcl_set_error(interp,
                  "Lk::window_icon_hex: expected 2 arguments (win, hex)");

    return LCL_RC_ERR;
  }

  lw = get_lk_window(interp, argv[0]);

  if (!lw) {
    return LCL_RC_ERR;
  }

  hex = lcl_value_to_string(argv[1]);

  if (!hex) {
    lcl_set_error(interp, "Lk::window_icon_hex: hex must be a string");

    return LCL_RC_ERR;
  }

  bytes = hex_decode_arg(interp, "Lk::window_icon_hex", hex, &n);

  if (!bytes) {
    return LCL_RC_ERR;
  }

  ok = lk_window_set_icon_mem(lw->win, bytes, n);
  free(bytes);

  *out = lcl_int_new((lcl_int)ok);

  return LCL_RC_OK;
}

/* ---- Image file IO + native file dialogs (image track I6) ---- */

/* Lk::image_load [ui path] -> opaque<lk_image>.
 * Decodes BMP always, PNG & co. under SDL3_image; the result is a
 * fresh app-owned image registered like Lk::image_new's.  Unlike
 * Lk::window_icon, an unreadable/undecodable file is a HARD error —
 * an opaque-returning proc has no clean 0 return; catch it. */
static lcl_return_code c_lk_image_load(lcl_interp *interp, int argc,
                                       lcl_value **argv, lcl_value **out) {
  lk_ui *ui = NULL;
  const char *path;
  lk_image *img;
  char err[512];

  if (argc != 2) {
    lcl_set_error(interp, "Lk::image_load: expected 2 arguments (ui, path)");

    return LCL_RC_ERR;
  }

  if (lcl_opaque_get(argv[0], LK_UI_TYPE, (void **)&ui) != LCL_OK || !ui) {
    lcl_set_error(interp, "Lk::image_load: expected lk_ui opaque");

    return LCL_RC_ERR;
  }

  path = lcl_value_to_string(argv[1]);

  if (!path) {
    lcl_set_error(interp, "Lk::image_load: path must be a string");

    return LCL_RC_ERR;
  }

  img = lk_image_load_file(path);

  if (!img) {
    sprintf(err, "Lk::image_load: could not load image: %.400s", path);
    lcl_set_error(interp, err);

    return LCL_RC_ERR;
  }

  return image_wrap_register(interp, "Lk::image_load", ui, argv[0], img, out);
}

static int str_ieq(const char *a, const char *b) {
  while (*a && *b) {
    char ca = *a;
    char cb = *b;

    if (ca >= 'A' && ca <= 'Z') {
      ca = (char)(ca + 32);
    }
    if (cb >= 'A' && cb <= 'Z') {
      cb = (char)(cb + 32);
    }

    if (ca != cb) {
      return 0;
    }

    a++;
    b++;
  }

  return *a == '\0' && *b == '\0';
}

/* Lk::image_save [img path] -> 1 | 0.
 * Format from the extension (case-insensitive .bmp / .png — the
 * extension IS the format, a mismatch would lie on disk).  An
 * unknown extension is a hard error; an IO failure (or .png without
 * SDL3_image) returns 0, the Lk::window_icon contract. */
static lcl_return_code c_lk_image_save(lcl_interp *interp, int argc,
                                       lcl_value **argv, lcl_value **out) {
  struct lcl_lk_image *iw;
  const char *path;
  const char *dot;
  int ok;

  if (argc != 2) {
    lcl_set_error(interp, "Lk::image_save: expected 2 arguments (img, path)");

    return LCL_RC_ERR;
  }

  iw = get_image(interp, argv[0]);

  if (!iw) {
    return LCL_RC_ERR;
  }

  path = lcl_value_to_string(argv[1]);

  if (!path) {
    lcl_set_error(interp, "Lk::image_save: path must be a string");

    return LCL_RC_ERR;
  }

  dot = strrchr(path, '.');

  if (dot && str_ieq(dot, ".bmp")) {
    ok = lk_image_save_bmp(iw->img, path);
  } else if (dot && str_ieq(dot, ".png")) {
    ok = lk_image_save_png(iw->img, path);
  } else {
    lcl_set_error(interp,
                  "Lk::image_save: unknown extension (supported: .bmp, .png)");

    return LCL_RC_ERR;
  }

  *out = lcl_int_new((lcl_int)ok);

  return LCL_RC_OK;
}

/* ---- File dialog bridge ----
 *
 * The C completion runs on the main thread from inside the window's
 * run loop (lk-sdl.h contract); this bridge builds the path list and
 * calls the retained script handler.  Known limitation: a window
 * destroyed while its dialog is pending never gets the completion,
 * so that ctx (and its retained handler) lives until process exit. */

struct lcl_dialog_ctx {
  lcl_interp *interp;
  lcl_value *handler; /* retained */
};

static void lcl_dialog_done(void *ud, const char *const *paths, int npaths) {
  struct lcl_dialog_ctx *ctx = (struct lcl_dialog_ctx *)ud;
  lcl_value *list = lcl_list_new();
  lcl_value *args[1];
  lcl_value *result = NULL;
  int i;

  for (i = 0; i < npaths; i++) {
    lcl_value *v = lcl_string_new(paths[i]);

    lcl_list_push(&list, v);
    lcl_ref_dec(v);
  }

  args[0] = list;

  if (lcl_call_proc(ctx->interp, ctx->handler, 1, args, &result) !=
      LCL_RC_OK) {
    lcl_lk_report_callback_error(ctx->interp, "File dialog handler");
  }

  if (result) {
    lcl_ref_dec(result);
  }

  lcl_ref_dec(list);
  lcl_ref_dec(ctx->handler);
  free(ctx);
}

#define LCL_DIALOG_MAX_FILTERS 16

/* Shared body of Lk::open_file_dialog / Lk::save_file_dialog:
 * [win proc ?filters ?default] where filters is a list of
 * (name pattern) 2-lists (SDL ';'-separated extension patterns,
 * e.g. ("Images" "bmp;png")).  Returns immediately; proc is called
 * later with a list of path strings (empty = cancelled). */
static lcl_return_code dialog_common(lcl_interp *interp, const char *proc,
                                     int save, int argc, lcl_value **argv,
                                     lcl_value **out) {
  struct lcl_lk_window *lw;
  struct lcl_dialog_ctx *ctx;
  lk_file_dialog_filter filters[LCL_DIALOG_MAX_FILTERS];
  int nfilters = 0;
  const char *def_loc = NULL;
  char err[128];

  if (argc < 2 || argc > 4) {
    sprintf(err,
            "%.40s: expected 2 to 4 arguments (win, proc, ?filters, ?default)",
            proc);
    lcl_set_error(interp, err);

    return LCL_RC_ERR;
  }

  lw = get_lk_window(interp, argv[0]);

  if (!lw) {
    return LCL_RC_ERR;
  }

  if (!lcl_is_callable(argv[1])) {
    sprintf(err, "%.40s: expected callable", proc);
    lcl_set_error(interp, err);

    return LCL_RC_ERR;
  }

  if (argc >= 3) {
    size_t len;
    size_t i;

    if (lcl_value_type_of(argv[2]) != LCL_LIST) {
      sprintf(err, "%.40s: filters must be a list of (name pattern) 2-lists",
              proc);
      lcl_set_error(interp, err);

      return LCL_RC_ERR;
    }

    len = lcl_list_len(argv[2]);

    if (len > LCL_DIALOG_MAX_FILTERS) {
      sprintf(err, "%.40s: too many filters (max 16)", proc);
      lcl_set_error(interp, err);

      return LCL_RC_ERR;
    }

    for (i = 0; i < len; i++) {
      lcl_value *elem = lcl_list_peek(argv[2], i);

      if (!elem || lcl_value_type_of(elem) != LCL_LIST ||
          lcl_list_len(elem) != 2) {
        sprintf(err, "%.40s: filters must be a list of (name pattern) 2-lists",
                proc);
        lcl_set_error(interp, err);

        return LCL_RC_ERR;
      }

      /* Strings borrowed from the argument list — valid for this
       * call; lk-sdl deep-copies before returning. */
      filters[nfilters].name = lcl_value_to_string(lcl_list_peek(elem, 0));
      filters[nfilters].pattern = lcl_value_to_string(lcl_list_peek(elem, 1));
      nfilters++;
    }
  }

  if (argc == 4) {
    def_loc = lcl_value_to_string(argv[3]);

    if (def_loc && !*def_loc) {
      def_loc = NULL;
    }
  }

  ctx = (struct lcl_dialog_ctx *)malloc(sizeof(*ctx));

  if (!ctx) {
    sprintf(err, "%.40s: allocation failed", proc);
    lcl_set_error(interp, err);

    return LCL_RC_ERR;
  }

  ctx->interp = interp;
  ctx->handler = lcl_ref_inc(argv[1]);

  if (save) {
    lk_window_save_file_dialog(lw->win, filters, nfilters, def_loc,
                               lcl_dialog_done, ctx);
  } else {
    lk_window_open_file_dialog(lw->win, filters, nfilters, def_loc, 0,
                               lcl_dialog_done, ctx);
  }

  *out = lcl_string_new("");

  return LCL_RC_OK;
}

static lcl_return_code c_lk_open_file_dialog(lcl_interp *interp, int argc,
                                             lcl_value **argv,
                                             lcl_value **out) {
  return dialog_common(interp, "Lk::open_file_dialog", 0, argc, argv, out);
}

static lcl_return_code c_lk_save_file_dialog(lcl_interp *interp, int argc,
                                             lcl_value **argv,
                                             lcl_value **out) {
  return dialog_common(interp, "Lk::save_file_dialog", 1, argc, argv, out);
}

#endif /* LK_HAVE_SDL */

/* ============================================================================
 * Overlays
 * ============================================================================
 */

/* Lk::overlay_count [ui] — number of overlays on the ui's overlay
 * stack (headless-testable introspection; widgets drive push/pop). */
static lcl_return_code c_lk_overlay_count(lcl_interp *interp, int argc,
                                          lcl_value **argv, lcl_value **out) {
  lk_ui *ui = NULL;

  if (argc < 1) {
    lcl_set_error(interp, "Lk::overlay_count: expected ui");
    return LCL_RC_ERR;
  }

  if (lcl_opaque_get(argv[0], LK_UI_TYPE, (void **)&ui) != LCL_OK || !ui) {
    lcl_set_error(interp, "Lk::overlay_count: bad ui handle");
    return LCL_RC_ERR;
  }

  *out = lcl_int_new((lcl_int)lk_overlay_count(ui));

  return LCL_RC_OK;
}

/* Lk::overlay_push [ui dict] — push an overlay from app code.
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
static lcl_return_code c_lk_overlay_push(lcl_interp *interp, int argc,
                                         lcl_value **argv, lcl_value **out) {
  lk_ui *ui = NULL;
  lcl_value *dict;
  lcl_value *v;
  lk_overlay ov;
  int kind_val;
  int is_modal;

  if (argc != 2) {
    lcl_set_error(interp, "Lk::overlay_push: expected 2 arguments (ui, dict)");
    return LCL_RC_ERR;
  }

  if (lcl_opaque_get(argv[0], LK_UI_TYPE, (void **)&ui) != LCL_OK || !ui) {
    lcl_set_error(interp, "Lk::overlay_push: bad ui handle");
    return LCL_RC_ERR;
  }

  dict = argv[1];

  if (lcl_value_type_of(dict) != LCL_DICT) {
    lcl_set_error(interp, "Lk::overlay_push: expected dict");
    return LCL_RC_ERR;
  }

  v = lcl_dict_peek(dict, "kind");

  if (!v) {
    lcl_set_error(interp, "Lk::overlay_push: missing kind");
    return LCL_RC_ERR;
  }

  if (!lookup_enum(overlay_kind_table, lcl_value_to_string(v), &kind_val)) {
    lcl_set_error(interp, "Lk::overlay_push: unknown overlay kind");
    return LCL_RC_ERR;
  }

  is_modal = (kind_val == LK_OVERLAY_MODAL);

  memset(&ov, 0, sizeof(ov));
  ov.kind = (lk_u8)kind_val;
  ov.anchor_mode =
      (lk_u8)(is_modal ? LK_ANCHOR_CENTER_VIEWPORT : LK_ANCHOR_BELOW);
  ov.dismiss_on_outside = (lk_u8)(is_modal ? 0 : 1);
  ov.traps_focus = (lk_u8)(is_modal ? 1 : 0);

  v = lcl_dict_peek(dict, "anchor");

  if (v) {
    int anchor_val;

    if (!lookup_enum(anchor_table, lcl_value_to_string(v), &anchor_val)) {
      lcl_set_error(interp, "Lk::overlay_push: unknown anchor");
      return LCL_RC_ERR;
    }

    ov.anchor_mode = (lk_u8)anchor_val;
  }

  v = lcl_dict_peek(dict, "owner_id");

  if (v) {
    const char *s = arg_name(interp, v, "Lk::overlay_push: owner_id");

    if (!s) {
      return LCL_RC_ERR;
    }

    ov.owner_id = lk_intern_cid(ui->intern, s);
  }

  v = lcl_dict_peek(dict, "content_root_id");

  if (v) {
    const char *s = arg_name(interp, v, "Lk::overlay_push: content_root_id");

    if (!s) {
      return LCL_RC_ERR;
    }

    ov.content_root_id = lk_intern_cid(ui->intern, s);
  }

  v = lcl_dict_peek(dict, "dismiss_on_outside");

  if (v) {
    lcl_int b;

    if (lcl_value_to_int(v, &b) != LCL_OK) {
      lcl_set_error(interp, "Lk::overlay_push: dismiss_on_outside expects int");
      return LCL_RC_ERR;
    }

    ov.dismiss_on_outside = (lk_u8)(b ? 1 : 0);
  }

  v = lcl_dict_peek(dict, "traps_focus");

  if (v) {
    lcl_int b;

    if (lcl_value_to_int(v, &b) != LCL_OK) {
      lcl_set_error(interp, "Lk::overlay_push: traps_focus expects int");
      return LCL_RC_ERR;
    }

    ov.traps_focus = (lk_u8)(b ? 1 : 0);
  }

  if (!lk_overlay_push(ui, &ov)) {
    lcl_set_error(interp, "Lk::overlay_push: push failed");
    return LCL_RC_ERR;
  }

  *out = lcl_string_new("");

  return LCL_RC_OK;
}

/* Lk::overlay_pop [ui] — pop the topmost overlay (no-op when empty). */
static lcl_return_code c_lk_overlay_pop(lcl_interp *interp, int argc,
                                        lcl_value **argv, lcl_value **out) {
  lk_ui *ui = NULL;

  if (argc != 1) {
    lcl_set_error(interp, "Lk::overlay_pop: expected 1 argument");
    return LCL_RC_ERR;
  }

  if (lcl_opaque_get(argv[0], LK_UI_TYPE, (void **)&ui) != LCL_OK || !ui) {
    lcl_set_error(interp, "Lk::overlay_pop: bad ui handle");
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

static lcl_return_code c_lk_clipboard_get(lcl_interp *interp, int argc,
                                          lcl_value **argv, lcl_value **out) {
  lk_ui *ui = NULL;

  if (argc < 1) {
    lcl_set_error(interp, "Lk::clipboard_get: expected ui");
    return LCL_RC_ERR;
  }

  if (lcl_opaque_get(argv[0], LK_UI_TYPE, (void **)&ui) != LCL_OK || !ui) {
    lcl_set_error(interp, "Lk::clipboard_get: bad ui handle");
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

static lcl_return_code c_lk_clipboard_set(lcl_interp *interp, int argc,
                                          lcl_value **argv, lcl_value **out) {
  lk_ui *ui = NULL;
  const char *text;

  if (argc < 2) {
    lcl_set_error(interp, "Lk::clipboard_set: expected ui text");
    return LCL_RC_ERR;
  }

  if (lcl_opaque_get(argv[0], LK_UI_TYPE, (void **)&ui) != LCL_OK || !ui) {
    lcl_set_error(interp, "Lk::clipboard_set: bad ui handle");
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

/* Lk::doc_new [?text] -> opaque<lk_document> */
static lcl_return_code c_lk_doc_new(lcl_interp *interp, int argc,
                                    lcl_value **argv, lcl_value **out) {
  struct lcl_lk_doc *dw;
  lk_document *doc;

  if (argc > 1) {
    lcl_set_error(interp, "Lk::doc_new: expected 0 or 1 arguments (?text)");

    return LCL_RC_ERR;
  }

  if (argc == 1) {
    const char *text = lcl_value_to_string(argv[0]);
    doc = lk_doc_from_str(NULL, NULL, NULL, text, (lk_u32)strlen(text));
  } else {
    doc = lk_doc_new(NULL, NULL, NULL);
  }

  if (!doc) {
    lcl_set_error(interp, "Lk::doc_new: allocation failed");

    return LCL_RC_ERR;
  }

  dw = (struct lcl_lk_doc *)malloc(sizeof(*dw));

  if (!dw) {
    lk_doc_destroy(doc);
    lcl_set_error(interp, "Lk::doc_new: allocation failed");

    return LCL_RC_ERR;
  }

  dw->doc = doc;
  dw->subs = NULL;

  *out = lcl_opaque_new(dw, LK_DOC_TYPE, doc_finalizer);

  return LCL_RC_OK;
}

/* Lk::doc_text [doc] -> string (the whole contents) */
static lcl_return_code c_lk_doc_text(lcl_interp *interp, int argc,
                                     lcl_value **argv, lcl_value **out) {
  struct lcl_lk_doc *dw;
  lk_u32 n;
  char *buf;

  if (argc != 1) {
    lcl_set_error(interp, "Lk::doc_text: expected 1 argument");

    return LCL_RC_ERR;
  }

  dw = get_doc(interp, argv[0]);

  if (!dw) {
    return LCL_RC_ERR;
  }

  n = lk_doc_len(dw->doc);
  buf = (char *)malloc(n + 1);

  if (!buf) {
    lcl_set_error(interp, "Lk::doc_text: allocation failed");

    return LCL_RC_ERR;
  }

  lk_doc_get_text(dw->doc, 0, buf, n);
  buf[n] = '\0';
  *out = lcl_string_new(buf);
  free(buf);

  return LCL_RC_OK;
}

/* Lk::doc_len [doc] -> int */
static lcl_return_code c_lk_doc_len(lcl_interp *interp, int argc,
                                    lcl_value **argv, lcl_value **out) {
  struct lcl_lk_doc *dw;

  if (argc != 1) {
    lcl_set_error(interp, "Lk::doc_len: expected 1 argument");

    return LCL_RC_ERR;
  }

  dw = get_doc(interp, argv[0]);

  if (!dw) {
    return LCL_RC_ERR;
  }

  *out = lcl_int_new((lcl_int)lk_doc_len(dw->doc));

  return LCL_RC_OK;
}

/* Lk::doc_line_count [doc] -> int */
static lcl_return_code c_lk_doc_line_count(lcl_interp *interp, int argc,
                                           lcl_value **argv, lcl_value **out) {
  struct lcl_lk_doc *dw;

  if (argc != 1) {
    lcl_set_error(interp, "Lk::doc_line_count: expected 1 argument");

    return LCL_RC_ERR;
  }

  dw = get_doc(interp, argv[0]);

  if (!dw) {
    return LCL_RC_ERR;
  }

  *out = lcl_int_new((lcl_int)lk_doc_line_count(dw->doc));

  return LCL_RC_OK;
}

/* Lk::doc_pos_to_line [doc, pos] -> 0-based line index.
 *
 * pos at or past the document end resolves to the last line
 * (mirroring the C API); the 1-based display line is script-side
 * arithmetic. */
static lcl_return_code c_lk_doc_pos_to_line(lcl_interp *interp, int argc,
                                            lcl_value **argv, lcl_value **out) {
  struct lcl_lk_doc *dw;
  lcl_int pos;

  if (argc != 2) {
    lcl_set_error(interp,
                  "Lk::doc_pos_to_line: expected 2 arguments (doc, pos)");

    return LCL_RC_ERR;
  }

  dw = get_doc(interp, argv[0]);

  if (!dw) {
    return LCL_RC_ERR;
  }

  if (lcl_value_to_int(argv[1], &pos) != LCL_OK || pos < 0) {
    lcl_set_error(interp,
                  "Lk::doc_pos_to_line: pos must be a non-negative integer");

    return LCL_RC_ERR;
  }

  *out = lcl_int_new((lcl_int)lk_doc_pos_to_line(dw->doc, (lk_u32)pos));

  return LCL_RC_OK;
}

/* Lk::doc_line_start [doc, line] -> byte offset of the line's start.
 *
 * line is 0-based.  Out-of-range lines are hard errors (the C API's
 * silent 0 is indistinguishable from line 0's start). */
static lcl_return_code c_lk_doc_line_start(lcl_interp *interp, int argc,
                                           lcl_value **argv, lcl_value **out) {
  struct lcl_lk_doc *dw;
  lcl_int line;

  if (argc != 2) {
    lcl_set_error(interp,
                  "Lk::doc_line_start: expected 2 arguments (doc, line)");

    return LCL_RC_ERR;
  }

  dw = get_doc(interp, argv[0]);

  if (!dw) {
    return LCL_RC_ERR;
  }

  if (lcl_value_to_int(argv[1], &line) != LCL_OK || line < 0) {
    lcl_set_error(interp,
                  "Lk::doc_line_start: line must be a non-negative integer");

    return LCL_RC_ERR;
  }

  if ((lk_u32)line >= lk_doc_line_count(dw->doc)) {
    lcl_set_error(interp, "Lk::doc_line_start: line out of range");

    return LCL_RC_ERR;
  }

  *out = lcl_int_new((lcl_int)lk_doc_line_start(dw->doc, (lk_u32)line));

  return LCL_RC_OK;
}

/* Lk::doc_line_end [doc, line] -> byte offset of the line's end (its
 * \n, exclusive; the document length for the last line).
 *
 * line is 0-based; out-of-range lines are hard errors. */
static lcl_return_code c_lk_doc_line_end(lcl_interp *interp, int argc,
                                         lcl_value **argv, lcl_value **out) {
  struct lcl_lk_doc *dw;
  lcl_int line;

  if (argc != 2) {
    lcl_set_error(interp, "Lk::doc_line_end: expected 2 arguments (doc, line)");

    return LCL_RC_ERR;
  }

  dw = get_doc(interp, argv[0]);

  if (!dw) {
    return LCL_RC_ERR;
  }

  if (lcl_value_to_int(argv[1], &line) != LCL_OK || line < 0) {
    lcl_set_error(interp,
                  "Lk::doc_line_end: line must be a non-negative integer");

    return LCL_RC_ERR;
  }

  if ((lk_u32)line >= lk_doc_line_count(dw->doc)) {
    lcl_set_error(interp, "Lk::doc_line_end: line out of range");

    return LCL_RC_ERR;
  }

  *out = lcl_int_new((lcl_int)lk_doc_line_end(dw->doc, (lk_u32)line));

  return LCL_RC_OK;
}

/* Lk::doc_char_col [doc, pos] -> 1-based CHARACTER column.
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
static lcl_return_code c_lk_doc_char_col(lcl_interp *interp, int argc,
                                         lcl_value **argv, lcl_value **out) {
  struct lcl_lk_doc *dw;
  lcl_int pos;
  lk_u32 at;
  lk_u32 col;
  char buf[256];

  if (argc != 2) {
    lcl_set_error(interp, "Lk::doc_char_col: expected 2 arguments (doc, pos)");

    return LCL_RC_ERR;
  }

  dw = get_doc(interp, argv[0]);

  if (!dw) {
    return LCL_RC_ERR;
  }

  if (lcl_value_to_int(argv[1], &pos) != LCL_OK || pos < 0) {
    lcl_set_error(interp,
                  "Lk::doc_char_col: pos must be a non-negative integer");

    return LCL_RC_ERR;
  }

  if ((lk_u32)pos > lk_doc_len(dw->doc)) {
    lcl_set_error(interp, "Lk::doc_char_col: pos out of range");

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

  *out = lcl_int_new((lcl_int)col);

  return LCL_RC_OK;
}

/* Lk::doc_find [doc, needle, ?from] -> first match position >= from,
 * or -1 when not found.
 *
 * Literal forward byte search (no patterns); from defaults to 0.  A
 * from past the document end is simply not-found (-1), so the
 * search-next idiom -- search again from hit + 1 -- never errors at
 * the end of the document.  An empty needle is a hard error (the C
 * API's silent 0 would be indistinguishable from not-found). */
static lcl_return_code c_lk_doc_find(lcl_interp *interp, int argc,
                                     lcl_value **argv, lcl_value **out) {
  struct lcl_lk_doc *dw;
  const char *needle;
  lcl_int from = 0;
  lk_u32 pos = 0;

  if (argc != 2 && argc != 3) {
    lcl_set_error(interp,
                  "Lk::doc_find: expected 2 or 3 arguments (doc, needle, "
                  "?from)");

    return LCL_RC_ERR;
  }

  dw = get_doc(interp, argv[0]);

  if (!dw) {
    return LCL_RC_ERR;
  }

  needle = lcl_value_to_string(argv[1]);

  if (!needle || needle[0] == '\0') {
    lcl_set_error(interp, "Lk::doc_find: needle must be non-empty");

    return LCL_RC_ERR;
  }

  if (argc == 3) {
    if (lcl_value_to_int(argv[2], &from) != LCL_OK || from < 0) {
      lcl_set_error(interp,
                    "Lk::doc_find: from must be a non-negative integer");

      return LCL_RC_ERR;
    }
  }

  if (lk_doc_find(dw->doc, needle, (lk_u32)strlen(needle), (lk_u32)from,
                  &pos)) {
    *out = lcl_int_new((lcl_int)pos);
  } else {
    *out = lcl_int_new(-1);
  }

  return LCL_RC_OK;
}

/* Lk::doc_revision [doc] -> "hi:lo" (e.g. "0:42").
 *
 * The lk_revision {hi,lo} pair encoded as a deterministic string —
 * equality-comparable from script ([== $r1 $r2]); nothing more.  Do
 * not do arithmetic on it. */
static lcl_return_code c_lk_doc_revision(lcl_interp *interp, int argc,
                                         lcl_value **argv, lcl_value **out) {
  struct lcl_lk_doc *dw;
  lk_revision rev;
  char buf[32];

  if (argc != 1) {
    lcl_set_error(interp, "Lk::doc_revision: expected 1 argument");

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

/* Lk::doc_insert [doc, pos, text] -> "" */
static lcl_return_code c_lk_doc_insert(lcl_interp *interp, int argc,
                                       lcl_value **argv, lcl_value **out) {
  struct lcl_lk_doc *dw;
  lcl_int pos;
  const char *text;

  if (argc != 3) {
    lcl_set_error(interp, "Lk::doc_insert: expected 3 arguments (doc, pos, text)");

    return LCL_RC_ERR;
  }

  dw = get_doc(interp, argv[0]);

  if (!dw) {
    return LCL_RC_ERR;
  }

  if (lcl_value_to_int(argv[1], &pos) != LCL_OK || pos < 0) {
    lcl_set_error(interp, "Lk::doc_insert: pos must be a non-negative integer");

    return LCL_RC_ERR;
  }

  text = lcl_value_to_string(argv[2]);

  if (!lk_doc_insert(dw->doc, (lk_u32)pos, text, (lk_u32)strlen(text))) {
    lcl_set_error(interp,
                  "Lk::doc_insert: rejected (pos out of range, empty text, "
                  "or mutation from inside a notification)");

    return LCL_RC_ERR;
  }

  *out = lcl_string_new("");

  return LCL_RC_OK;
}

/* Lk::doc_delete [doc, pos, len] -> "" */
static lcl_return_code c_lk_doc_delete(lcl_interp *interp, int argc,
                                       lcl_value **argv, lcl_value **out) {
  struct lcl_lk_doc *dw;
  lcl_int pos;
  lcl_int len;

  if (argc != 3) {
    lcl_set_error(interp, "Lk::doc_delete: expected 3 arguments (doc, pos, len)");

    return LCL_RC_ERR;
  }

  dw = get_doc(interp, argv[0]);

  if (!dw) {
    return LCL_RC_ERR;
  }

  if (lcl_value_to_int(argv[1], &pos) != LCL_OK || pos < 0) {
    lcl_set_error(interp, "Lk::doc_delete: pos must be a non-negative integer");

    return LCL_RC_ERR;
  }

  if (lcl_value_to_int(argv[2], &len) != LCL_OK || len < 0) {
    lcl_set_error(interp, "Lk::doc_delete: len must be a non-negative integer");

    return LCL_RC_ERR;
  }

  if (!lk_doc_delete(dw->doc, (lk_u32)pos, (lk_u32)len)) {
    lcl_set_error(interp,
                  "Lk::doc_delete: rejected (pos out of range, zero length, "
                  "or mutation from inside a notification)");

    return LCL_RC_ERR;
  }

  *out = lcl_string_new("");

  return LCL_RC_OK;
}

/* Lk::doc_transact [doc, body] -> ""
 *
 * Brackets the body in one lk_doc_begin/lk_doc_commit transaction:
 * every Lk::doc_insert/Lk::doc_delete inside becomes one committed
 * transaction — one notification, one undo step.  The commit runs
 * even if the body errors (edits made before the error stay applied,
 * as one transaction), then the body's error propagates.  Nesting
 * doc_transact on the same document is a programming error (the C
 * layer debug-asserts nested begin). */
static lcl_return_code c_lk_doc_transact(lcl_interp *interp, int argc,
                                         lcl_value **argv, lcl_value **out) {
  struct lcl_lk_doc *dw;
  const char *body;
  lcl_value *r = NULL;
  int rc;

  if (argc != 2) {
    lcl_set_error(interp, "Lk::doc_transact: expected 2 arguments (doc, body)");

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

    v = lcl_int_new((lcl_int)dl->start);
    lcl_dict_put(&dict, "start", v);
    lcl_ref_dec(v);

    v = lcl_int_new((lcl_int)dl->deleted_len);
    lcl_dict_put(&dict, "deleted_len", v);
    lcl_ref_dec(v);

    v = lcl_int_new((lcl_int)dl->inserted_len);
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

    v = lcl_int_new((lcl_int)dl->origin);
    lcl_dict_put(&dict, "origin", v);
    lcl_ref_dec(v);

    lcl_list_push(&list, dict);
    lcl_ref_dec(dict);
  }

  args[0] = list;

  if (lcl_call_proc(sub->interp, sub->handler, 1, args, &result) !=
      LCL_RC_OK) {
    lcl_lk_report_callback_error(sub->interp, "Doc subscriber");
  }

  if (result) {
    lcl_ref_dec(result);
  }

  lcl_ref_dec(list);
}

/* Lk::doc_subscribe [doc, proc] -> subscription id (int).
 * The proc receives ONE argument: a list of delta dicts, each with
 * start, deleted_len, inserted_len, deleted, inserted, origin. */
static lcl_return_code c_lk_doc_subscribe(lcl_interp *interp, int argc,
                                          lcl_value **argv, lcl_value **out) {
  struct lcl_lk_doc *dw;
  struct lcl_doc_sub *sub;

  if (argc != 2) {
    lcl_set_error(interp, "Lk::doc_subscribe: expected 2 arguments (doc, proc)");

    return LCL_RC_ERR;
  }

  dw = get_doc(interp, argv[0]);

  if (!dw) {
    return LCL_RC_ERR;
  }

  if (!lcl_is_callable(argv[1])) {
    lcl_set_error(interp, "Lk::doc_subscribe: expected callable");

    return LCL_RC_ERR;
  }

  sub = (struct lcl_doc_sub *)malloc(sizeof(*sub));

  if (!sub) {
    lcl_set_error(interp, "Lk::doc_subscribe: allocation failed");

    return LCL_RC_ERR;
  }

  sub->interp = interp;
  sub->handler = lcl_ref_inc(argv[1]);
  sub->id = lk_doc_subscribe(dw->doc, lcl_doc_sub_bridge, sub);

  if (sub->id == 0) {
    lcl_ref_dec(sub->handler);
    free(sub);
    lcl_set_error(interp, "Lk::doc_subscribe: subscribe failed");

    return LCL_RC_ERR;
  }

  sub->next = dw->subs;
  dw->subs = sub;

  *out = lcl_int_new((lcl_int)sub->id);

  return LCL_RC_OK;
}

/* Lk::doc_unsubscribe [doc, id] -> "" */
static lcl_return_code c_lk_doc_unsubscribe(lcl_interp *interp, int argc,
                                            lcl_value **argv, lcl_value **out) {
  struct lcl_lk_doc *dw;
  struct lcl_doc_sub **link;
  lcl_int id;

  if (argc != 2) {
    lcl_set_error(interp, "Lk::doc_unsubscribe: expected 2 arguments (doc, id)");

    return LCL_RC_ERR;
  }

  dw = get_doc(interp, argv[0]);

  if (!dw) {
    return LCL_RC_ERR;
  }

  if (lcl_value_to_int(argv[1], &id) != LCL_OK) {
    lcl_set_error(interp, "Lk::doc_unsubscribe: id must be an integer");

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

  lcl_set_error(interp, "Lk::doc_unsubscribe: unknown subscription id");

  return LCL_RC_ERR;
}

/* ============================================================================
 * Editor track: edit history (5)
 * ============================================================================
 */

/* Lk::history_new [?doc] -> opaque<lk_edit_history>.
 * With a doc argument the history attaches immediately (records every
 * committed transaction).  Without one it stays detached until an
 * Lk::editor_new call wires it to that editor's document. */
static lcl_return_code c_lk_history_new(lcl_interp *interp, int argc,
                                        lcl_value **argv, lcl_value **out) {
  struct lcl_lk_history *hw;
  struct lcl_lk_doc *dw = NULL;
  lk_edit_history *hist;

  if (argc > 1) {
    lcl_set_error(interp, "Lk::history_new: expected 0 or 1 arguments (?doc)");

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
    lcl_set_error(interp, "Lk::history_new: allocation failed");

    return LCL_RC_ERR;
  }

  hw = (struct lcl_lk_history *)malloc(sizeof(*hw));

  if (!hw) {
    lk_history_destroy(hist);
    lcl_set_error(interp, "Lk::history_new: allocation failed");

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

/* Lk::history_undo [hist, doc] -> 1 if a step was undone, else 0 */
static lcl_return_code c_lk_history_undo(lcl_interp *interp, int argc,
                                         lcl_value **argv, lcl_value **out) {
  struct lcl_lk_history *hw;
  struct lcl_lk_doc *dw;

  if (argc != 2) {
    lcl_set_error(interp, "Lk::history_undo: expected 2 arguments (hist, doc)");

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

  *out = lcl_int_new((lcl_int)lk_history_undo(hw->hist, dw->doc));

  return LCL_RC_OK;
}

/* Lk::history_redo [hist, doc] -> 1 if a step was redone, else 0 */
static lcl_return_code c_lk_history_redo(lcl_interp *interp, int argc,
                                         lcl_value **argv, lcl_value **out) {
  struct lcl_lk_history *hw;
  struct lcl_lk_doc *dw;

  if (argc != 2) {
    lcl_set_error(interp, "Lk::history_redo: expected 2 arguments (hist, doc)");

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

  *out = lcl_int_new((lcl_int)lk_history_redo(hw->hist, dw->doc));

  return LCL_RC_OK;
}

/* Lk::history_can_undo [hist] -> 0/1 */
static lcl_return_code c_lk_history_can_undo(lcl_interp *interp, int argc,
                                             lcl_value **argv,
                                             lcl_value **out) {
  struct lcl_lk_history *hw;

  if (argc != 1) {
    lcl_set_error(interp, "Lk::history_can_undo: expected 1 argument");

    return LCL_RC_ERR;
  }

  hw = get_history(interp, argv[0]);

  if (!hw) {
    return LCL_RC_ERR;
  }

  *out = lcl_int_new((lcl_int)lk_history_can_undo(hw->hist));

  return LCL_RC_OK;
}

/* Lk::history_can_redo [hist] -> 0/1 */
static lcl_return_code c_lk_history_can_redo(lcl_interp *interp, int argc,
                                             lcl_value **argv,
                                             lcl_value **out) {
  struct lcl_lk_history *hw;

  if (argc != 1) {
    lcl_set_error(interp, "Lk::history_can_redo: expected 1 argument");

    return LCL_RC_ERR;
  }

  hw = get_history(interp, argv[0]);

  if (!hw) {
    return LCL_RC_ERR;
  }

  *out = lcl_int_new((lcl_int)lk_history_can_redo(hw->hist));

  return LCL_RC_OK;
}

/* Lk::history_mark_saved [hist] -> "" */
static lcl_return_code c_lk_history_mark_saved(lcl_interp *interp, int argc,
                                               lcl_value **argv,
                                               lcl_value **out) {
  struct lcl_lk_history *hw;

  if (argc != 1) {
    lcl_set_error(interp, "Lk::history_mark_saved: expected 1 argument");

    return LCL_RC_ERR;
  }

  hw = get_history(interp, argv[0]);

  if (!hw) {
    return LCL_RC_ERR;
  }

  lk_history_mark_saved(hw->hist);
  *out = lcl_string_new("");

  return LCL_RC_OK;
}

/* Lk::history_at_saved [hist] -> 0/1 */
static lcl_return_code c_lk_history_at_saved(lcl_interp *interp, int argc,
                                             lcl_value **argv,
                                             lcl_value **out) {
  struct lcl_lk_history *hw;

  if (argc != 1) {
    lcl_set_error(interp, "Lk::history_at_saved: expected 1 argument");

    return LCL_RC_ERR;
  }

  hw = get_history(interp, argv[0]);

  if (!hw) {
    return LCL_RC_ERR;
  }

  *out = lcl_int_new((lcl_int)lk_history_at_saved(hw->hist));

  return LCL_RC_OK;
}

/* ============================================================================
 * Editor track: editors (8)
 * ============================================================================
 */

/* Lk::editor_new [ui, doc, ?hist] -> opaque<lk_editor>.
 * Registers the editor in the ui's resource table; the wrapper's
 * finalizer releases the registration before destroying the editor.
 * A detached history is attached to the document here; a history
 * already attached to a DIFFERENT document is an error. */
static lcl_return_code c_lk_editor_new(lcl_interp *interp, int argc,
                                       lcl_value **argv, lcl_value **out) {
  lk_ui *ui = NULL;
  struct lcl_lk_doc *dw;
  struct lcl_lk_history *hw = NULL;
  struct lcl_lk_editor *ew;
  lk_editor *ed;
  lk_resource_ref ref;

  if (argc < 2 || argc > 3) {
    lcl_set_error(interp,
                  "Lk::editor_new: expected 2 or 3 arguments (ui, doc, ?hist)");

    return LCL_RC_ERR;
  }

  if (lcl_opaque_get(argv[0], LK_UI_TYPE, (void **)&ui) != LCL_OK || !ui) {
    lcl_set_error(interp, "Lk::editor_new: expected lk_ui opaque");

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
                    "Lk::editor_new: history is attached to a different "
                    "document");

      return LCL_RC_ERR;
    }
  }

  ed = lk_editor_new(NULL, NULL, NULL, dw->doc, hw ? hw->hist : NULL);

  if (!ed) {
    lcl_set_error(interp, "Lk::editor_new: allocation failed");

    return LCL_RC_ERR;
  }

  ref = lk_resource_register(lk_ui_resources(ui), lk_editor_type(), ed,
                             "lcl-editor");

  if (ref.id == 0) {
    lk_editor_destroy(ed);
    lcl_set_error(interp, "Lk::editor_new: resource registration failed");

    return LCL_RC_ERR;
  }

  ew = (struct lcl_lk_editor *)malloc(sizeof(*ew));

  if (!ew) {
    lk_resource_release(lk_ui_resources(ui), ref);
    lk_editor_destroy(ed);
    lcl_set_error(interp, "Lk::editor_new: allocation failed");

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

/* Lk::editor_cursor [editor] -> byte offset (int) */
static lcl_return_code c_lk_editor_cursor(lcl_interp *interp, int argc,
                                          lcl_value **argv, lcl_value **out) {
  struct lcl_lk_editor *ew;

  if (argc != 1) {
    lcl_set_error(interp, "Lk::editor_cursor: expected 1 argument");

    return LCL_RC_ERR;
  }

  ew = get_editor(interp, argv[0]);

  if (!ew) {
    return LCL_RC_ERR;
  }

  *out = lcl_int_new((lcl_int)lk_editor_cursor(ew->ed));

  return LCL_RC_OK;
}

/* Lk::editor_set_cursor [editor, pos] -> "" */
static lcl_return_code c_lk_editor_set_cursor(lcl_interp *interp, int argc,
                                              lcl_value **argv,
                                              lcl_value **out) {
  struct lcl_lk_editor *ew;
  lcl_int pos;

  if (argc != 2) {
    lcl_set_error(interp,
                  "Lk::editor_set_cursor: expected 2 arguments (editor, pos)");

    return LCL_RC_ERR;
  }

  ew = get_editor(interp, argv[0]);

  if (!ew) {
    return LCL_RC_ERR;
  }

  if (lcl_value_to_int(argv[1], &pos) != LCL_OK || pos < 0) {
    lcl_set_error(interp,
                  "Lk::editor_set_cursor: pos must be a non-negative integer");

    return LCL_RC_ERR;
  }

  lk_editor_set_cursor(ew->ed, (lk_u32)pos);
  *out = lcl_string_new("");

  return LCL_RC_OK;
}

/* Lk::editor_scroll_to_cursor [editor] -> "" -- request that the next
 * render bring the cursor row into view, without moving the cursor.
 * A foreign transaction (a script append, another view's edit) carries
 * the cursor along but deliberately does NOT scroll: a document
 * changing elsewhere must not yank the reader's viewport.  A caller
 * that WANTS to follow -- a log tailing its own output -- asks here. */
static lcl_return_code c_lk_editor_scroll_to_cursor(lcl_interp *interp,
                                                    int argc, lcl_value **argv,
                                                    lcl_value **out) {
  struct lcl_lk_editor *ew;

  if (argc != 1) {
    lcl_set_error(interp, "Lk::editor_scroll_to_cursor: expected 1 argument");

    return LCL_RC_ERR;
  }

  ew = get_editor(interp, argv[0]);

  if (!ew) {
    return LCL_RC_ERR;
  }

  lk_editor_scroll_to_cursor(ew->ed);
  *out = lcl_string_new("");

  return LCL_RC_OK;
}

/* Lk::editor_selection [editor] -> (start end) or () when none */
static lcl_return_code c_lk_editor_selection(lcl_interp *interp, int argc,
                                             lcl_value **argv,
                                             lcl_value **out) {
  struct lcl_lk_editor *ew;
  lk_u32 start;
  lk_u32 end;
  lcl_value *list;

  if (argc != 1) {
    lcl_set_error(interp, "Lk::editor_selection: expected 1 argument");

    return LCL_RC_ERR;
  }

  ew = get_editor(interp, argv[0]);

  if (!ew) {
    return LCL_RC_ERR;
  }

  list = lcl_list_new();

  if (lk_editor_selection(ew->ed, &start, &end)) {
    lcl_value *v;

    v = lcl_int_new((lcl_int)start);
    lcl_list_push(&list, v);
    lcl_ref_dec(v);

    v = lcl_int_new((lcl_int)end);
    lcl_list_push(&list, v);
    lcl_ref_dec(v);
  }

  *out = list;

  return LCL_RC_OK;
}

/* Lk::editor_carets [editor] -> list of cursor positions (ints), one
 * per caret in document order (multi-cursor stage E; always >= 1). */
static lcl_return_code c_lk_editor_carets(lcl_interp *interp, int argc,
                                          lcl_value **argv, lcl_value **out) {
  struct lcl_lk_editor *ew;
  lcl_value *list;
  lk_u32 i;
  lk_u32 n;

  if (argc != 1) {
    lcl_set_error(interp, "Lk::editor_carets: expected 1 argument");

    return LCL_RC_ERR;
  }

  ew = get_editor(interp, argv[0]);

  if (!ew) {
    return LCL_RC_ERR;
  }

  list = lcl_list_new();
  n = lk_editor_caret_count(ew->ed);

  for (i = 0; i < n; i++) {
    lk_u32 cur = 0;
    lcl_value *v;

    lk_editor_caret(ew->ed, i, &cur, NULL, NULL);
    v = lcl_int_new((lcl_int)cur);
    lcl_list_push(&list, v);
    lcl_ref_dec(v);
  }

  *out = list;

  return LCL_RC_OK;
}

/* Lk::editor_selections [editor] -> one entry PER CARET, index-
 * parallel to Lk::editor_carets: a (start end) 2-list, or the empty
 * list () for a caret with no selection. */
static lcl_return_code c_lk_editor_selections(lcl_interp *interp, int argc,
                                              lcl_value **argv,
                                              lcl_value **out) {
  struct lcl_lk_editor *ew;
  lcl_value *list;
  lk_u32 i;
  lk_u32 n;

  if (argc != 1) {
    lcl_set_error(interp, "Lk::editor_selections: expected 1 argument");

    return LCL_RC_ERR;
  }

  ew = get_editor(interp, argv[0]);

  if (!ew) {
    return LCL_RC_ERR;
  }

  list = lcl_list_new();
  n = lk_editor_caret_count(ew->ed);

  for (i = 0; i < n; i++) {
    lk_u32 s = 0;
    lk_u32 en = 0;
    lcl_value *entry = lcl_list_new();

    if (lk_editor_caret(ew->ed, i, NULL, &s, &en)) {
      lcl_value *v;

      v = lcl_int_new((lcl_int)s);
      lcl_list_push(&entry, v);
      lcl_ref_dec(v);

      v = lcl_int_new((lcl_int)en);
      lcl_list_push(&entry, v);
      lcl_ref_dec(v);
    }

    lcl_list_push(&list, entry);
    lcl_ref_dec(entry);
  }

  *out = list;

  return LCL_RC_OK;
}

/* Lk::editor_wrap [editor, mode] -> ""
 *
 * mode is "none" | "character" | "word" (docs/editor-wrap.md section
 * 5).  An unknown name is a hard error listing the supported modes,
 * DSL-v2 style; an engine rejection (allocation failure) is a hard
 * error too. */
/* Lk::editor_set_editable [editor] [0/1] -- read-only policy: 0
 * rejects USER mutations (insert/delete/cut/paste/undo/redo) at the
 * editor; document-level edits stay unaffected. */
static lcl_return_code c_lk_editor_set_editable(lcl_interp *interp, int argc,
                                                lcl_value **argv,
                                                lcl_value **out) {
  struct lcl_lk_editor *ew;
  lcl_int on;

  if (argc != 2) {
    lcl_set_error(
        interp, "Lk::editor_set_editable: expected 2 arguments (editor, 0/1)");

    return LCL_RC_ERR;
  }

  ew = get_editor(interp, argv[0]);

  if (!ew) {
    return LCL_RC_ERR;
  }

  if (lcl_value_to_int(argv[1], &on) != LCL_OK) {
    lcl_set_error(interp, "Lk::editor_set_editable: flag must be 0 or 1");

    return LCL_RC_ERR;
  }

  lk_editor_set_editable(ew->ed, on ? 1 : 0);
  *out = lcl_string_new("");

  return LCL_RC_OK;
}

/* Lk::editor_editable [editor] -> 0/1 */
static lcl_return_code c_lk_editor_editable(lcl_interp *interp, int argc,
                                            lcl_value **argv, lcl_value **out) {
  struct lcl_lk_editor *ew;

  if (argc != 1) {
    lcl_set_error(interp, "Lk::editor_editable: expected 1 argument");

    return LCL_RC_ERR;
  }

  ew = get_editor(interp, argv[0]);

  if (!ew) {
    return LCL_RC_ERR;
  }

  *out = lcl_int_new(lk_editor_editable(ew->ed));

  return LCL_RC_OK;
}

static lcl_return_code c_lk_editor_wrap(lcl_interp *interp, int argc,
                                        lcl_value **argv, lcl_value **out) {
  struct lcl_lk_editor *ew;
  const char *mode_str;
  int mode_val;

  if (argc != 2) {
    lcl_set_error(interp, "Lk::editor_wrap: expected 2 arguments (editor, mode)");

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
            "Lk::editor_wrap: unknown mode '%.48s' "
            "(supported: none, character, word)",
            mode_str);
    lcl_set_error(interp, err);

    return LCL_RC_ERR;
  }

  if (!lk_editor_set_wrap_mode(ew->ed, (lk_editor_wrap_mode)mode_val)) {
    static char err[160];

    sprintf(err,
            "Lk::editor_wrap: the engine rejected mode '%.48s' "
            "(supported: none, character, word)",
            mode_str);
    lcl_set_error(interp, err);

    return LCL_RC_ERR;
  }

  *out = lcl_string_new("");

  return LCL_RC_OK;
}

/* Lk::editor_wrap_get [editor] -> the mode name ("none" | "character"
 * | "word") */
static lcl_return_code c_lk_editor_wrap_get(lcl_interp *interp, int argc,
                                            lcl_value **argv, lcl_value **out) {
  struct lcl_lk_editor *ew;
  int mode;
  const str_enum *e;

  if (argc != 1) {
    lcl_set_error(interp, "Lk::editor_wrap_get: expected 1 argument");

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

  lcl_set_error(interp, "Lk::editor_wrap_get: unmapped wrap mode");

  return LCL_RC_ERR;
}

/* Joined command-name list for Lk::editor_command error messages. */
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

/* Lk::editor_command [editor, cmd, ?args...] -> 1 if the command did
 * anything, else 0.
 *
 * cmd is an LK_ED_* name with the prefix stripped and lowercased
 * ("insert_text", "move_left", "select_all", "undo", ...).  Per-
 * command args: insert_text takes the text; set_cursor takes pos and
 * an optional literal "extend"; scroll_lines takes a signed line
 * count; add_cursor_at takes a position (toggle -- a caret already
 * exactly there is removed, never below one); motion commands accept
 * an optional literal "select" flag (extends the selection);
 * everything else -- including add_cursor_above/below,
 * select_next_match, collapse_cursors (multi-cursor stage E) --
 * takes no args.  Unknown commands and malformed args are hard
 * errors. */
static lcl_return_code c_lk_editor_command(lcl_interp *interp, int argc,
                                           lcl_value **argv, lcl_value **out) {
  struct lcl_lk_editor *ew;
  const char *cmd_str;
  int cmd_val;
  int extra;
  lk_editor_cmd_arg arg;

  if (argc < 2) {
    lcl_set_error(
        interp, "Lk::editor_command: expected (editor, command, ?args...?)");

    return LCL_RC_ERR;
  }

  ew = get_editor(interp, argv[0]);

  if (!ew) {
    return LCL_RC_ERR;
  }

  cmd_str = lcl_value_to_string(argv[1]);

  if (!lookup_enum(ed_cmd_table, cmd_str, &cmd_val)) {
    static char err[640];

    sprintf(err, "Lk::editor_command: unknown command '%.48s' (known: %s)",
            cmd_str, ed_cmd_known());
    lcl_set_error(interp, err);

    return LCL_RC_ERR;
  }

  memset(&arg, 0, sizeof(arg));
  extra = argc - 2;

  switch (cmd_val) {
  case LK_ED_INSERT_TEXT:
    if (extra != 1) {
      lcl_set_error(interp, "Lk::editor_command: insert_text expects the text");

      return LCL_RC_ERR;
    }

    arg.text.ptr = lcl_value_to_string(argv[2]);
    arg.text.len = (lk_u32)strlen(arg.text.ptr);
    break;

  case LK_ED_SET_CURSOR: {
    lcl_int pos;

    if (extra < 1 || extra > 2) {
      lcl_set_error(interp,
                    "Lk::editor_command: set_cursor expects pos ?\"extend\"?");

      return LCL_RC_ERR;
    }

    if (lcl_value_to_int(argv[2], &pos) != LCL_OK || pos < 0) {
      lcl_set_error(
          interp,
          "Lk::editor_command: set_cursor pos must be a non-negative integer");

      return LCL_RC_ERR;
    }

    arg.set_cursor.pos = (lk_u32)pos;

    if (extra == 2) {
      if (strcmp(lcl_value_to_string(argv[3]), "extend") != 0) {
        lcl_set_error(
            interp,
            "Lk::editor_command: set_cursor trailing flag must be \"extend\"");

        return LCL_RC_ERR;
      }

      arg.set_cursor.extend = 1;
    }
    break;
  }

  case LK_ED_SCROLL_LINES: {
    lcl_int lines;

    if (extra != 1 || lcl_value_to_int(argv[2], &lines) != LCL_OK) {
      lcl_set_error(
          interp,
          "Lk::editor_command: scroll_lines expects a signed line count");

      return LCL_RC_ERR;
    }

    arg.lines = (lk_i32)lines;
    break;
  }

  case LK_ED_ADD_CURSOR_AT: {
    lcl_int pos;

    if (extra != 1 || lcl_value_to_int(argv[2], &pos) != LCL_OK || pos < 0) {
      lcl_set_error(interp,
                    "Lk::editor_command: add_cursor_at expects a "
                    "non-negative position");

      return LCL_RC_ERR;
    }

    arg.set_cursor.pos = (lk_u32)pos;
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
                      "Lk::editor_command: motion commands take at most a "
                      "\"select\" flag");

        return LCL_RC_ERR;
      }

      if (extra == 1) {
        arg.select = 1;
      }
    } else if (extra != 0) {
      lcl_set_error(interp, "Lk::editor_command: command takes no arguments");

      return LCL_RC_ERR;
    }
    break;
  }

  *out = lcl_int_new(
      (lcl_int)lk_editor_command(ew->ed, ew->ui, (lk_editor_cmd_id)cmd_val,
                                 &arg));

  return LCL_RC_OK;
}

/* Parse one span dict (#{start N end N ?fg (r g b ?a?)? ?bg ...?
 * ?underline 1?}) into *sp.  Returns 1 on success, 0 with the interp
 * error set. */
static int span_from_dict(lcl_interp *interp, lcl_value *dict,
                          lk_edit_span *sp) {
  lcl_value *v;
  lcl_int a;
  lcl_int b;

  if (lcl_value_type_of(dict) != LCL_DICT) {
    lcl_set_error(interp,
                  "Lk::editor_set_spans: each span must be a dict "
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
        lcl_value *kv = lcl_list_peek(keys, i);

        if (kv) {
          const char *k = lcl_value_to_string(kv);

          if (strcmp(k, "start") != 0 && strcmp(k, "end") != 0 &&
              strcmp(k, "fg") != 0 && strcmp(k, "bg") != 0 &&
              strcmp(k, "underline") != 0) {
            lcl_ref_dec(keys);
            lcl_set_error(interp,
                          "Lk::editor_set_spans: unknown span key (known: "
                          "start, end, fg, bg, underline)");

            return 0;
          }
        }
      }

      lcl_ref_dec(keys);
    }
  }

  v = lcl_dict_peek(dict, "start");

  if (!v) {
    lcl_set_error(interp, "Lk::editor_set_spans: span is missing 'start'");

    return 0;
  }

  if (lcl_value_to_int(v, &a) != LCL_OK || a < 0) {
    lcl_set_error(interp,
                  "Lk::editor_set_spans: span start must be a non-negative "
                  "integer");

    return 0;
  }

  v = lcl_dict_peek(dict, "end");

  if (!v) {
    lcl_set_error(interp, "Lk::editor_set_spans: span is missing 'end'");

    return 0;
  }

  if (lcl_value_to_int(v, &b) != LCL_OK || b <= a) {
    lcl_set_error(interp,
                  "Lk::editor_set_spans: span end must be an integer > start");

    return 0;
  }

  sp->start = (lk_u32)a;
  sp->end = (lk_u32)b;
  sp->flags = 0;

  v = lcl_dict_peek(dict, "fg");

  if (v) {
    if (!parse_color_list(v, &sp->fg)) {
      lcl_set_error(interp,
                    "Lk::editor_set_spans: fg must be (r g b) or (r g b a)");

      return 0;
    }

    sp->flags |= LK_SPAN_FG;
  }

  v = lcl_dict_peek(dict, "bg");

  if (v) {
    if (!parse_color_list(v, &sp->bg)) {
      lcl_set_error(interp,
                    "Lk::editor_set_spans: bg must be (r g b) or (r g b a)");

      return 0;
    }

    sp->flags |= LK_SPAN_BG;
  }

  v = lcl_dict_peek(dict, "underline");

  if (v) {
    lcl_int u;

    if (lcl_value_to_int(v, &u) != LCL_OK) {
      lcl_set_error(interp, "Lk::editor_set_spans: underline expects an int");

      return 0;
    }

    if (u) {
      sp->flags |= LK_SPAN_UNDERLINE;
    }
  }

  return 1;
}

/* Lk::editor_set_spans [editor, doc, spans] -> ""
 *
 * spans is a list of span dicts (see span_from_dict); an empty list
 * clears.  The snapshot is stamped with the document's CURRENT
 * revision and the spans' min/max range — a simplification of the
 * docs/editor.md section 11 snapshot dict that is equivalent for
 * synchronous script producers (the producer runs and delivers within
 * one frame, so stamp-at-call == stamp-at-produce).  Spans must be
 * sorted by start and non-overlapping. */
static lcl_return_code c_lk_editor_set_spans(lcl_interp *interp, int argc,
                                             lcl_value **argv,
                                             lcl_value **out) {
  struct lcl_lk_editor *ew;
  struct lcl_lk_doc *dw;
  lk_edit_span *spans;
  lk_edit_span_snapshot snap;
  size_t n;
  size_t i;

  if (argc != 3) {
    lcl_set_error(
        interp, "Lk::editor_set_spans: expected 3 arguments (editor, doc, spans)");

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
                  "Lk::editor_set_spans: spans must be a list of span dicts");

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
    lcl_set_error(interp, "Lk::editor_set_spans: allocation failed");

    return LCL_RC_ERR;
  }

  for (i = 0; i < n; i++) {
    lcl_value *sv = lcl_list_peek(argv[2], i);

    memset(&spans[i], 0, sizeof(spans[i]));

    if (!sv) {
      free(spans);
      lcl_set_error(interp, "Lk::editor_set_spans: bad spans list");

      return LCL_RC_ERR;
    }

    if (!span_from_dict(interp, sv, &spans[i])) {
      free(spans);

      return LCL_RC_ERR; /* error set by span_from_dict */
    }

    if (i > 0 && spans[i].start < spans[i - 1].end) {
      free(spans);
      lcl_set_error(
          interp,
          "Lk::editor_set_spans: spans must be sorted and non-overlapping");

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

/* Lk::annot_store_new -> opaque<lk_annot_store> */
static lcl_return_code c_lk_annot_store_new(lcl_interp *interp, int argc,
                                            lcl_value **argv, lcl_value **out) {
  struct lcl_lk_annot *aw;
  lk_annot_store *store;
  (void)argv;

  if (argc != 0) {
    lcl_set_error(interp, "Lk::annot_store_new: expected 0 arguments");

    return LCL_RC_ERR;
  }

  store = lk_annot_store_new(NULL, NULL, NULL);

  if (!store) {
    lcl_set_error(interp, "Lk::annot_store_new: allocation failed");

    return LCL_RC_ERR;
  }

  aw = (struct lcl_lk_annot *)malloc(sizeof(*aw));

  if (!aw) {
    lk_annot_store_destroy(store);
    lcl_set_error(interp, "Lk::annot_store_new: allocation failed");

    return LCL_RC_ERR;
  }

  aw->store = store;
  aw->doc_val = NULL;
  aw->pres_ui = NULL;
  aw->ui_val = NULL;

  *out = lcl_opaque_new(aw, LK_ANNOT_TYPE, annot_finalizer);

  return LCL_RC_OK;
}

/* Lk::annot_attach [store, doc] -> "" (subscribes; anchors then track
 * every committed transaction).  Re-attaching is an error. */
static lcl_return_code c_lk_annot_attach(lcl_interp *interp, int argc,
                                         lcl_value **argv, lcl_value **out) {
  struct lcl_lk_annot *aw;
  struct lcl_lk_doc *dw;

  if (argc != 2) {
    lcl_set_error(interp, "Lk::annot_attach: expected 2 arguments (store, doc)");

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
    lcl_set_error(interp, "Lk::annot_attach: store is already attached");

    return LCL_RC_ERR;
  }

  lk_annot_store_attach(aw->store, dw->doc);
  aw->doc_val = lcl_ref_inc(argv[1]);

  *out = lcl_string_new("");

  return LCL_RC_OK;
}

/* Lk::annot_add [store, start, end, layer, ?meta-dict] -> record id.
 * meta-dict keys/values become the record's metadata (all strings). */
static lcl_return_code c_lk_annot_add(lcl_interp *interp, int argc,
                                      lcl_value **argv, lcl_value **out) {
  struct lcl_lk_annot *aw;
  lcl_int start;
  lcl_int end;
  const char *layer;
  lk_u32 id;
  const char **keys = NULL;
  const char **values = NULL;
  lcl_value *keys_list = NULL;
  lk_u32 meta_count = 0;

  if (argc < 4 || argc > 5) {
    lcl_set_error(interp,
                  "Lk::annot_add: expected 4 or 5 arguments "
                  "(store, start, end, layer, ?meta-dict)");

    return LCL_RC_ERR;
  }

  aw = get_annot(interp, argv[0]);

  if (!aw) {
    return LCL_RC_ERR;
  }

  if (lcl_value_to_int(argv[1], &start) != LCL_OK || start < 0) {
    lcl_set_error(interp, "Lk::annot_add: start must be a non-negative integer");

    return LCL_RC_ERR;
  }

  if (lcl_value_to_int(argv[2], &end) != LCL_OK || end <= start) {
    lcl_set_error(interp, "Lk::annot_add: end must be an integer > start");

    return LCL_RC_ERR;
  }

  layer = arg_name(interp, argv[3], "Lk::annot_add: layer");

  if (!layer) {
    return LCL_RC_ERR;
  }

  if (argc == 5) {
    size_t nk;
    size_t i;

    if (lcl_value_type_of(argv[4]) != LCL_DICT) {
      lcl_set_error(interp, "Lk::annot_add: meta must be a dict");

      return LCL_RC_ERR;
    }

    if (lcl_dict_keys(argv[4], &keys_list) != LCL_OK || !keys_list) {
      lcl_set_error(interp, "Lk::annot_add: bad meta dict");

      return LCL_RC_ERR;
    }

    nk = lcl_list_len(keys_list);

    if (nk > 0) {
      keys = (const char **)malloc(nk * sizeof(*keys));
      values = (const char **)malloc(nk * sizeof(*values));

      if (!keys || !values) {
        free((void *)keys);
        free((void *)values);
        lcl_ref_dec(keys_list);
        lcl_set_error(interp, "Lk::annot_add: allocation failed");

        return LCL_RC_ERR;
      }

      /* Borrowed from the dict (peek): valid for this call, and
       * lk_annot_add copies every string before returning. */
      for (i = 0; i < nk; i++) {
        lcl_value *kv = lcl_list_peek(keys_list, i);
        lcl_value *vv = kv ? lcl_dict_peek(argv[4], lcl_value_to_string(kv))
                           : NULL;

        if (!vv) {
          continue;
        }

        keys[meta_count] = lcl_value_to_string(kv);
        values[meta_count] = lcl_value_to_string(vv);
        meta_count++;
      }
    }
  }

  id = lk_annot_add(aw->store, (lk_u32)start, (lk_u32)end, layer, keys, values,
                    meta_count);

  free((void *)keys);
  free((void *)values);

  if (keys_list) {
    lcl_ref_dec(keys_list);
  }

  if (id == 0) {
    lcl_set_error(interp, "Lk::annot_add: add failed");

    return LCL_RC_ERR;
  }

  *out = lcl_int_new((lcl_int)id);

  return LCL_RC_OK;
}

/* Lk::annot_remove [store, id] -> 1 if removed, 0 if not found */
static lcl_return_code c_lk_annot_remove(lcl_interp *interp, int argc,
                                         lcl_value **argv, lcl_value **out) {
  struct lcl_lk_annot *aw;
  lcl_int id;

  if (argc != 2) {
    lcl_set_error(interp, "Lk::annot_remove: expected 2 arguments (store, id)");

    return LCL_RC_ERR;
  }

  aw = get_annot(interp, argv[0]);

  if (!aw) {
    return LCL_RC_ERR;
  }

  if (lcl_value_to_int(argv[1], &id) != LCL_OK) {
    lcl_set_error(interp, "Lk::annot_remove: id must be an integer");

    return LCL_RC_ERR;
  }

  *out = lcl_int_new((lcl_int)lk_annot_remove(aw->store, (lk_u32)id));

  return LCL_RC_OK;
}

/* Lk::annot_span [store, id] -> (start end); error if the record is
 * gone. */
static lcl_return_code c_lk_annot_span(lcl_interp *interp, int argc,
                                       lcl_value **argv, lcl_value **out) {
  struct lcl_lk_annot *aw;
  lcl_int id;
  lk_u32 start;
  lk_u32 end;
  lcl_value *list;
  lcl_value *v;

  if (argc != 2) {
    lcl_set_error(interp, "Lk::annot_span: expected 2 arguments (store, id)");

    return LCL_RC_ERR;
  }

  aw = get_annot(interp, argv[0]);

  if (!aw) {
    return LCL_RC_ERR;
  }

  if (lcl_value_to_int(argv[1], &id) != LCL_OK) {
    lcl_set_error(interp, "Lk::annot_span: id must be an integer");

    return LCL_RC_ERR;
  }

  if (!lk_annot_get_span(aw->store, (lk_u32)id, &start, &end)) {
    lcl_set_error(interp, "Lk::annot_span: no such annotation");

    return LCL_RC_ERR;
  }

  list = lcl_list_new();

  v = lcl_int_new((lcl_int)start);
  lcl_list_push(&list, v);
  lcl_ref_dec(v);

  v = lcl_int_new((lcl_int)end);
  lcl_list_push(&list, v);
  lcl_ref_dec(v);

  *out = list;

  return LCL_RC_OK;
}

/* Lk::annot_meta [store, id, key] -> value string ("" when the key is
 * absent); error if the record is gone. */
static lcl_return_code c_lk_annot_meta(lcl_interp *interp, int argc,
                                       lcl_value **argv, lcl_value **out) {
  struct lcl_lk_annot *aw;
  lcl_int id;
  const char *val;

  if (argc != 3) {
    lcl_set_error(interp,
                  "Lk::annot_meta: expected 3 arguments (store, id, key)");

    return LCL_RC_ERR;
  }

  aw = get_annot(interp, argv[0]);

  if (!aw) {
    return LCL_RC_ERR;
  }

  if (lcl_value_to_int(argv[1], &id) != LCL_OK) {
    lcl_set_error(interp, "Lk::annot_meta: id must be an integer");

    return LCL_RC_ERR;
  }

  if (!lk_annot_get(aw->store, (lk_u32)id)) {
    lcl_set_error(interp, "Lk::annot_meta: no such annotation");

    return LCL_RC_ERR;
  }

  {
    const char *key = arg_name(interp, argv[2], "Lk::annot_meta: key");

    if (!key) {
      return LCL_RC_ERR;
    }

    val = lk_annot_get_meta(aw->store, (lk_u32)id, key);
  }

  *out = lcl_string_new(val ? val : "");

  return LCL_RC_OK;
}

/* Lk::annot_layer [store, id] -> the record's layer name; error if
 * the record is gone. */
static lcl_return_code c_lk_annot_layer(lcl_interp *interp, int argc,
                                        lcl_value **argv, lcl_value **out) {
  struct lcl_lk_annot *aw;
  lcl_int id;
  const lk_annot_record *rec;

  if (argc != 2) {
    lcl_set_error(interp,
                  "Lk::annot_layer: expected 2 arguments (store, id)");

    return LCL_RC_ERR;
  }

  aw = get_annot(interp, argv[0]);

  if (!aw) {
    return LCL_RC_ERR;
  }

  if (lcl_value_to_int(argv[1], &id) != LCL_OK) {
    lcl_set_error(interp, "Lk::annot_layer: id must be an integer");

    return LCL_RC_ERR;
  }

  rec = lk_annot_get(aw->store, (lk_u32)id);

  if (!rec) {
    lcl_set_error(interp, "Lk::annot_layer: no such annotation");

    return LCL_RC_ERR;
  }

  *out = lcl_string_new(rec->layer);

  return LCL_RC_OK;
}

/* Lk::annot_meta_all [store, id] -> dict of every metadata key/value
 * on the record (empty dict when it has none); error if the record is
 * gone.  The single-key Lk::annot_meta stays for point reads. */
static lcl_return_code c_lk_annot_meta_all(lcl_interp *interp, int argc,
                                           lcl_value **argv, lcl_value **out) {
  struct lcl_lk_annot *aw;
  lcl_int id;
  const lk_annot_record *rec;
  lcl_value *d;
  lk_u32 i;

  if (argc != 2) {
    lcl_set_error(interp,
                  "Lk::annot_meta_all: expected 2 arguments (store, id)");

    return LCL_RC_ERR;
  }

  aw = get_annot(interp, argv[0]);

  if (!aw) {
    return LCL_RC_ERR;
  }

  if (lcl_value_to_int(argv[1], &id) != LCL_OK) {
    lcl_set_error(interp, "Lk::annot_meta_all: id must be an integer");

    return LCL_RC_ERR;
  }

  rec = lk_annot_get(aw->store, (lk_u32)id);

  if (!rec) {
    lcl_set_error(interp, "Lk::annot_meta_all: no such annotation");

    return LCL_RC_ERR;
  }

  d = lcl_dict_new();

  for (i = 0; i < rec->meta_count; i++) {
    lcl_value *v = lcl_string_new(rec->values[i]);

    lcl_dict_put(&d, rec->keys[i], v);
    lcl_ref_dec(v);
  }

  *out = d;

  return LCL_RC_OK;
}

/* Marshal a query's ids into an Lcl list (frees nothing). */
static lcl_value *annot_query_to_list(const lk_annot_query *q) {
  lcl_value *list = lcl_list_new();
  lk_u32 i;

  for (i = 0; i < q->count; i++) {
    lcl_value *v = lcl_int_new((lcl_int)q->ids[i]);

    lcl_list_push(&list, v);
    lcl_ref_dec(v);
  }

  return list;
}

/* Lk::annot_in_range [store, start, end, ?layer] -> list of ids */
static lcl_return_code c_lk_annot_in_range(lcl_interp *interp, int argc,
                                           lcl_value **argv, lcl_value **out) {
  struct lcl_lk_annot *aw;
  lcl_int start;
  lcl_int end;
  const char *layer = NULL;
  lk_annot_query q;

  if (argc < 3 || argc > 4) {
    lcl_set_error(interp,
                  "Lk::annot_in_range: expected 3 or 4 arguments "
                  "(store, start, end, ?layer)");

    return LCL_RC_ERR;
  }

  aw = get_annot(interp, argv[0]);

  if (!aw) {
    return LCL_RC_ERR;
  }

  if (lcl_value_to_int(argv[1], &start) != LCL_OK ||
      lcl_value_to_int(argv[2], &end) != LCL_OK || start < 0 || end < start) {
    lcl_set_error(interp, "Lk::annot_in_range: bad range");

    return LCL_RC_ERR;
  }

  if (argc == 4) {
    layer = arg_name(interp, argv[3], "Lk::annot_in_range: layer");

    if (!layer) {
      return LCL_RC_ERR;
    }
  }

  lk_annot_query_init(&q);
  lk_annot_in_range(aw->store, (lk_u32)start, (lk_u32)end, layer, &q);
  *out = annot_query_to_list(&q);
  lk_annot_query_free(&q);

  return LCL_RC_OK;
}

/* Lk::annot_at [store, pos, ?layer] -> list of ids */
static lcl_return_code c_lk_annot_at(lcl_interp *interp, int argc,
                                     lcl_value **argv, lcl_value **out) {
  struct lcl_lk_annot *aw;
  lcl_int pos;
  const char *layer = NULL;
  lk_annot_query q;

  if (argc < 2 || argc > 3) {
    lcl_set_error(
        interp, "Lk::annot_at: expected 2 or 3 arguments (store, pos, ?layer)");

    return LCL_RC_ERR;
  }

  aw = get_annot(interp, argv[0]);

  if (!aw) {
    return LCL_RC_ERR;
  }

  if (lcl_value_to_int(argv[1], &pos) != LCL_OK || pos < 0) {
    lcl_set_error(interp, "Lk::annot_at: pos must be a non-negative integer");

    return LCL_RC_ERR;
  }

  if (argc == 3) {
    layer = arg_name(interp, argv[2], "Lk::annot_at: layer");

    if (!layer) {
      return LCL_RC_ERR;
    }
  }

  lk_annot_query_init(&q);
  lk_annot_at(aw->store, (lk_u32)pos, layer, &q);
  *out = annot_query_to_list(&q);
  lk_annot_query_free(&q);

  return LCL_RC_OK;
}

/* Lk::annot_by_layer [store, layer] -> list of ids */
static lcl_return_code c_lk_annot_by_layer(lcl_interp *interp, int argc,
                                           lcl_value **argv, lcl_value **out) {
  struct lcl_lk_annot *aw;
  lk_annot_query q;

  if (argc != 2) {
    lcl_set_error(interp,
                  "Lk::annot_by_layer: expected 2 arguments (store, layer)");

    return LCL_RC_ERR;
  }

  aw = get_annot(interp, argv[0]);

  if (!aw) {
    return LCL_RC_ERR;
  }

  {
    const char *layer = arg_name(interp, argv[1], "Lk::annot_by_layer: layer");

    if (!layer) {
      return LCL_RC_ERR;
    }

    lk_annot_query_init(&q);
    lk_annot_by_layer(aw->store, layer, &q);
  }
  *out = annot_query_to_list(&q);
  lk_annot_query_free(&q);

  return LCL_RC_OK;
}

/* Lk::annot_layer_register [store, name] -> "" */
static lcl_return_code c_lk_annot_layer_register(lcl_interp *interp, int argc,
                                                 lcl_value **argv,
                                                 lcl_value **out) {
  struct lcl_lk_annot *aw;

  if (argc != 2) {
    lcl_set_error(
        interp, "Lk::annot_layer_register: expected 2 arguments (store, name)");

    return LCL_RC_ERR;
  }

  aw = get_annot(interp, argv[0]);

  if (!aw) {
    return LCL_RC_ERR;
  }

  {
    const char *layer =
        arg_name(interp, argv[1], "Lk::annot_layer_register: layer");

    if (!layer) {
      return LCL_RC_ERR;
    }

    lk_annot_register_layer(aw->store, layer);
  }

  *out = lcl_string_new("");

  return LCL_RC_OK;
}

/* Lk::annot_layer_priority [store, layer, priority] -> ""
 * Presentation precedence for a layer (default 0; higher wins).
 * Auto-registers the layer. */
static lcl_return_code c_lk_annot_layer_priority(lcl_interp *interp, int argc,
                                                 lcl_value **argv,
                                                 lcl_value **out) {
  struct lcl_lk_annot *aw;
  lcl_int prio;

  if (argc != 3) {
    lcl_set_error(interp,
                  "Lk::annot_layer_priority: expected 3 arguments "
                  "(store, layer, priority)");

    return LCL_RC_ERR;
  }

  aw = get_annot(interp, argv[0]);

  if (!aw) {
    return LCL_RC_ERR;
  }

  if (lcl_value_to_int(argv[2], &prio) != LCL_OK) {
    lcl_set_error(interp,
                  "Lk::annot_layer_priority: priority must be an integer");

    return LCL_RC_ERR;
  }

  {
    const char *layer =
        arg_name(interp, argv[1], "Lk::annot_layer_priority: layer");

    if (!layer) {
      return LCL_RC_ERR;
    }

    lk_annot_layer_set_priority(aw->store, layer, (lk_i32)prio);
  }

  *out = lcl_string_new("");

  return LCL_RC_OK;
}

/* Lk::annot_store_seq [store] -> int
 * Monotonic count of record-set mutations (add/remove/clear/present,
 * delete-sweep).  Compare for change. */
static lcl_return_code c_lk_annot_store_seq(lcl_interp *interp, int argc,
                                            lcl_value **argv, lcl_value **out) {
  struct lcl_lk_annot *aw;

  if (argc != 1) {
    lcl_set_error(interp, "Lk::annot_store_seq: expected 1 argument (store)");

    return LCL_RC_ERR;
  }

  aw = get_annot(interp, argv[0]);

  if (!aw) {
    return LCL_RC_ERR;
  }

  *out = lcl_int_new((lcl_int)lk_annot_store_seq(aw->store));

  return LCL_RC_OK;
}

/* Lk::annot_layers [store] -> (name ...)
 * Registered layer names in registration order. */
static lcl_return_code c_lk_annot_layers(lcl_interp *interp, int argc,
                                         lcl_value **argv, lcl_value **out) {
  struct lcl_lk_annot *aw;
  lcl_value *list;
  lk_u32 i;
  lk_u32 n;

  if (argc != 1) {
    lcl_set_error(interp, "Lk::annot_layers: expected 1 argument (store)");

    return LCL_RC_ERR;
  }

  aw = get_annot(interp, argv[0]);

  if (!aw) {
    return LCL_RC_ERR;
  }

  list = lcl_list_new();
  n = lk_annot_layer_count(aw->store);

  for (i = 0; i < n; i++) {
    lcl_value *name = lcl_string_new(lk_annot_layer_name(aw->store, i));

    lcl_list_push(&list, name);
    lcl_ref_dec(name);
  }

  *out = list;

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
  pres_box_free(box);
}

/* Lk::annot_present [ui, store, id, ptype, value] -> ""
 *
 * Attaches a presentation to annotation id: ptype is interned in the
 * ui's table; value may be ANY Lcl value (dicts, closures, ...) — it
 * is retained and registered in the ui's resource table under the
 * "lcl-value" type, and the record carries the UIV_RESOURCE ref.
 * Handlers receive the value back intact (command_to_dict unwraps).
 * A store's presentations are bound to ONE ui (the first call's);
 * replacing / removing / clearing / destroying releases the retain
 * through the store's release hook, installed here on first use. */
static lcl_return_code c_lk_annot_present(lcl_interp *interp, int argc,
                                          lcl_value **argv, lcl_value **out) {
  lk_ui *ui = NULL;
  struct lcl_lk_annot *aw;
  lcl_int id;
  const char *ptype_str;
  struct lcl_pres_box *box;
  lk_resource_ref ref;
  lk_u32 type_id;

  if (argc != 5) {
    lcl_set_error(interp,
                  "Lk::annot_present: expected 5 arguments "
                  "(ui, store, id, ptype, value)");

    return LCL_RC_ERR;
  }

  if (lcl_opaque_get(argv[0], LK_UI_TYPE, (void **)&ui) != LCL_OK || !ui) {
    lcl_set_error(interp, "Lk::annot_present: expected lk_ui opaque");

    return LCL_RC_ERR;
  }

  aw = get_annot(interp, argv[1]);

  if (!aw) {
    return LCL_RC_ERR;
  }

  if (aw->pres_ui && aw->pres_ui != ui) {
    lcl_set_error(interp,
                  "Lk::annot_present: this store's presentations are bound "
                  "to a different ui");

    return LCL_RC_ERR;
  }

  if (lcl_value_to_int(argv[2], &id) != LCL_OK || id <= 0) {
    lcl_set_error(interp, "Lk::annot_present: id must be a positive integer");

    return LCL_RC_ERR;
  }

  ptype_str = arg_name(interp, argv[3], "Lk::annot_present: ptype");

  if (!ptype_str) {
    return LCL_RC_ERR;
  }

  if (ptype_str[0] == '\0') {
    lcl_set_error(interp, "Lk::annot_present: ptype must be non-empty");

    return LCL_RC_ERR;
  }

  if (!lk_annot_get(aw->store, (lk_u32)id)) {
    lcl_set_error(interp, "Lk::annot_present: no such annotation");

    return LCL_RC_ERR;
  }

  box = (struct lcl_pres_box *)malloc(sizeof(*box));

  if (!box) {
    lcl_set_error(interp, "Lk::annot_present: allocation failed");

    return LCL_RC_ERR;
  }

  box->val = lcl_ref_inc(argv[4]);
  box->ui = ui;
  ref = lk_resource_register(lk_ui_resources(ui), &g_lcl_value_type, box,
                             "lcl-value");

  if (ref.id == 0) {
    lcl_ref_dec(box->val);
    free(box);
    lcl_set_error(interp, "Lk::annot_present: resource registration failed");

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
    lcl_set_error(interp, "Lk::annot_present: set_present failed");

    return LCL_RC_ERR;
  }

  pres_box_link(box);
  *out = lcl_string_new("");

  return LCL_RC_OK;
}

/* Lk::editor_pos_at [editor, x, y] -> byte position, or -1 when the
 * point is outside the editor's laid-out rect or no layout snapshot
 * exists (docs/weft-surface.md section 1.5 pinned contract). */
static lcl_return_code c_lk_editor_pos_at(lcl_interp *interp, int argc,
                                          lcl_value **argv, lcl_value **out) {
  struct lcl_lk_editor *ew;
  lcl_int x;
  lcl_int y;
  lk_u32 pos = 0;

  if (argc != 3) {
    lcl_set_error(interp,
                  "Lk::editor_pos_at: expected 3 arguments (editor, x, y)");

    return LCL_RC_ERR;
  }

  ew = get_editor(interp, argv[0]);

  if (!ew) {
    return LCL_RC_ERR;
  }

  if (lcl_value_to_int(argv[1], &x) != LCL_OK ||
      lcl_value_to_int(argv[2], &y) != LCL_OK) {
    lcl_set_error(interp, "Lk::editor_pos_at: x and y must be integers");

    return LCL_RC_ERR;
  }

  if (lk_editor_pos_at(ew->ed, (lk_i32)x, (lk_i32)y, &pos)) {
    *out = lcl_int_new((lcl_int)pos);
  } else {
    *out = lcl_int_new(-1);
  }

  return LCL_RC_OK;
}

/* Lk::editor_presentations [editor, store] -> ""
 * Installs the store's presentation source on the editor (the annot
 * adapter).  The editor wrapper retains the store value so the source
 * can never dangle. */
static lcl_return_code c_lk_editor_presentations(lcl_interp *interp, int argc,
                                                 lcl_value **argv,
                                                 lcl_value **out) {
  struct lcl_lk_editor *ew;
  struct lcl_lk_annot *aw;
  lk_presentation_source src;

  if (argc != 2) {
    lcl_set_error(interp,
                  "Lk::editor_presentations: expected 2 arguments "
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
  lcl_value *ns = lcl_ns_new("Lk");
  lcl_define_take(interp, "Lk", ns);

  /* UI Lifecycle */
  lcl_ns_def_take(ns, "ui_create",
                  lcl_c_proc_new("Lk::ui_create", c_lk_ui_create));
  lcl_ns_def_take(ns, "ui_destroy",
                  lcl_c_proc_new("Lk::ui_destroy", c_lk_ui_destroy));
  lcl_ns_def_take(ns, "begin_frame",
                  lcl_c_proc_new("Lk::begin_frame", c_lk_begin_frame));
  lcl_ns_def_take(ns, "end_frame",
                  lcl_c_proc_new("Lk::end_frame", c_lk_end_frame));
  lcl_ns_def_take(ns, "tree", lcl_c_proc_new("Lk::tree", c_lk_tree));

  /* Tree Building */
  lcl_ns_def_take(ns, "node", lcl_c_proc_new("Lk::node", c_lk_node));
  lcl_ns_def_take(ns, "set_root",
                  lcl_c_proc_new("Lk::set_root", c_lk_set_root));
  lcl_ns_def_take(ns, "append_child",
                  lcl_c_proc_new("Lk::append_child", c_lk_append_child));
  lcl_ns_def_take(ns, "prop", lcl_c_proc_new("Lk::prop", c_lk_prop));
  lcl_ns_def_take(ns, "present", lcl_c_proc_new("Lk::present", c_lk_present));

  /* Commands & Translators */
  lcl_ns_def_take(ns, "add_translator",
                  lcl_c_proc_new("Lk::add_translator", c_lk_add_translator));
  lcl_ns_def_take(ns, "clear_translators",
                  lcl_c_proc_new("Lk::clear_translators",
                                 c_lk_clear_translators));
  lcl_ns_def_take(ns, "commands",
                  lcl_c_proc_new("Lk::commands", c_lk_commands));
  lcl_ns_def_take(ns, "clear_commands",
                  lcl_c_proc_new("Lk::clear_commands", c_lk_clear_commands));
  lcl_ns_def_take(ns, "command_log",
                  lcl_c_proc_new("Lk::command_log", c_lk_command_log));
  lcl_ns_def_take(
      ns, "clear_command_log",
      lcl_c_proc_new("Lk::clear_command_log", c_lk_clear_command_log));
  lcl_ns_def_take(
      ns, "set_command_handler",
      lcl_c_proc_new("Lk::set_command_handler", c_lk_set_command_handler));

  /* State */
  lcl_ns_def_take(ns, "state_set",
                  lcl_c_proc_new("Lk::state_set", c_lk_state_set));
  lcl_ns_def_take(ns, "state_get",
                  lcl_c_proc_new("Lk::state_get", c_lk_state_get));

  /* Focus */
  lcl_ns_def_take(ns, "focus_set",
                  lcl_c_proc_new("Lk::focus_set", c_lk_focus_set));
  lcl_ns_def_take(ns, "focus_request",
                  lcl_c_proc_new("Lk::focus_request", c_lk_focus_request));
  lcl_ns_def_take(ns, "focus_clear",
                  lcl_c_proc_new("Lk::focus_clear", c_lk_focus_clear));
  lcl_ns_def_take(ns, "focus_get",
                  lcl_c_proc_new("Lk::focus_get", c_lk_focus_get));
  lcl_ns_def_take(ns, "capture_set",
                  lcl_c_proc_new("Lk::capture_set", c_lk_capture_set));
  lcl_ns_def_take(ns, "capture_clear",
                  lcl_c_proc_new("Lk::capture_clear", c_lk_capture_clear));
  lcl_ns_def_take(ns, "capture_get",
                  lcl_c_proc_new("Lk::capture_get", c_lk_capture_get));
  lcl_ns_def_take(ns, "text_size",
                  lcl_c_proc_new("Lk::text_size", c_lk_text_size));

  /* Geometry */
  lcl_ns_def_take(ns, "node_rect",
                  lcl_c_proc_new("Lk::node_rect", c_lk_node_rect));
  lcl_ns_def_take(ns, "args", lcl_c_proc_new("Lk::args", c_lk_args));
  lcl_ns_def_take(ns, "version", lcl_c_proc_new("Lk::version", c_lk_version));

  /* Time */
  lcl_ns_def_take(ns, "time_ms", lcl_c_proc_new("Lk::time_ms", c_lk_time_ms));

  /* Overlays */
  lcl_ns_def_take(ns, "overlay_count",
                  lcl_c_proc_new("Lk::overlay_count", c_lk_overlay_count));
  lcl_ns_def_take(ns, "overlay_push",
                  lcl_c_proc_new("Lk::overlay_push", c_lk_overlay_push));
  lcl_ns_def_take(ns, "overlay_pop",
                  lcl_c_proc_new("Lk::overlay_pop", c_lk_overlay_pop));

  /* Tags & Style */
  lcl_ns_def_take(ns, "tag", lcl_c_proc_new("Lk::tag", c_lk_tag));
  lcl_ns_def_take(ns, "theme_rule",
                  lcl_c_proc_new("Lk::theme_rule", c_lk_theme_rule));

  /* Interning */
  lcl_ns_def_take(ns, "intern_str",
                  lcl_c_proc_new("Lk::intern_str", c_lk_intern_str));
  lcl_ns_def_take(ns, "intern_id",
                  lcl_c_proc_new("Lk::intern_id", c_lk_intern_id));

  /* Clipboard */
  lcl_ns_def_take(ns, "clipboard_get",
                  lcl_c_proc_new("Lk::clipboard_get", c_lk_clipboard_get));
  lcl_ns_def_take(ns, "clipboard_set",
                  lcl_c_proc_new("Lk::clipboard_set", c_lk_clipboard_set));

  /* Documents (editor track) */
  lcl_ns_def_take(ns, "doc_new", lcl_c_proc_new("Lk::doc_new", c_lk_doc_new));
  lcl_ns_def_take(ns, "doc_text",
                  lcl_c_proc_new("Lk::doc_text", c_lk_doc_text));
  lcl_ns_def_take(ns, "doc_len", lcl_c_proc_new("Lk::doc_len", c_lk_doc_len));
  lcl_ns_def_take(ns, "doc_line_count",
                  lcl_c_proc_new("Lk::doc_line_count", c_lk_doc_line_count));
  lcl_ns_def_take(ns, "doc_pos_to_line",
                  lcl_c_proc_new("Lk::doc_pos_to_line", c_lk_doc_pos_to_line));
  lcl_ns_def_take(ns, "doc_line_start",
                  lcl_c_proc_new("Lk::doc_line_start", c_lk_doc_line_start));
  lcl_ns_def_take(ns, "doc_line_end",
                  lcl_c_proc_new("Lk::doc_line_end", c_lk_doc_line_end));
  lcl_ns_def_take(ns, "doc_char_col",
                  lcl_c_proc_new("Lk::doc_char_col", c_lk_doc_char_col));
  lcl_ns_def_take(ns, "doc_find",
                  lcl_c_proc_new("Lk::doc_find", c_lk_doc_find));
  lcl_ns_def_take(ns, "doc_revision",
                  lcl_c_proc_new("Lk::doc_revision", c_lk_doc_revision));
  lcl_ns_def_take(ns, "doc_insert",
                  lcl_c_proc_new("Lk::doc_insert", c_lk_doc_insert));
  lcl_ns_def_take(ns, "doc_delete",
                  lcl_c_proc_new("Lk::doc_delete", c_lk_doc_delete));
  lcl_ns_def_take(ns, "doc_transact",
                  lcl_c_proc_new("Lk::doc_transact", c_lk_doc_transact));
  lcl_ns_def_take(ns, "doc_subscribe",
                  lcl_c_proc_new("Lk::doc_subscribe", c_lk_doc_subscribe));
  lcl_ns_def_take(ns, "doc_unsubscribe",
                  lcl_c_proc_new("Lk::doc_unsubscribe", c_lk_doc_unsubscribe));

  /* Edit history */
  lcl_ns_def_take(ns, "history_new",
                  lcl_c_proc_new("Lk::history_new", c_lk_history_new));
  lcl_ns_def_take(ns, "history_undo",
                  lcl_c_proc_new("Lk::history_undo", c_lk_history_undo));
  lcl_ns_def_take(ns, "history_redo",
                  lcl_c_proc_new("Lk::history_redo", c_lk_history_redo));
  lcl_ns_def_take(
      ns, "history_can_undo",
      lcl_c_proc_new("Lk::history_can_undo", c_lk_history_can_undo));
  lcl_ns_def_take(
      ns, "history_can_redo",
      lcl_c_proc_new("Lk::history_can_redo", c_lk_history_can_redo));
  lcl_ns_def_take(
      ns, "history_mark_saved",
      lcl_c_proc_new("Lk::history_mark_saved", c_lk_history_mark_saved));
  lcl_ns_def_take(
      ns, "history_at_saved",
      lcl_c_proc_new("Lk::history_at_saved", c_lk_history_at_saved));

  /* Editors */
  lcl_ns_def_take(ns, "editor_new",
                  lcl_c_proc_new("Lk::editor_new", c_lk_editor_new));
  lcl_ns_def_take(ns, "editor_cursor",
                  lcl_c_proc_new("Lk::editor_cursor", c_lk_editor_cursor));
  lcl_ns_def_take(
      ns, "editor_set_cursor",
      lcl_c_proc_new("Lk::editor_set_cursor", c_lk_editor_set_cursor));
  lcl_ns_def_take(ns, "editor_scroll_to_cursor",
                  lcl_c_proc_new("Lk::editor_scroll_to_cursor",
                                 c_lk_editor_scroll_to_cursor));
  lcl_ns_def_take(
      ns, "editor_selection",
      lcl_c_proc_new("Lk::editor_selection", c_lk_editor_selection));
  lcl_ns_def_take(ns, "editor_keys",
                  lcl_c_proc_new("Lk::editor_keys", c_lk_editor_keys));
  lcl_ns_def_take(ns, "editor_carets",
                  lcl_c_proc_new("Lk::editor_carets", c_lk_editor_carets));
  lcl_ns_def_take(
      ns, "editor_selections",
      lcl_c_proc_new("Lk::editor_selections", c_lk_editor_selections));
  lcl_ns_def_take(
      ns, "editor_set_editable",
      lcl_c_proc_new("Lk::editor_set_editable", c_lk_editor_set_editable));
  lcl_ns_def_take(ns, "editor_editable",
                  lcl_c_proc_new("Lk::editor_editable", c_lk_editor_editable));
  lcl_ns_def_take(ns, "editor_wrap",
                  lcl_c_proc_new("Lk::editor_wrap", c_lk_editor_wrap));
  lcl_ns_def_take(ns, "editor_wrap_get",
                  lcl_c_proc_new("Lk::editor_wrap_get", c_lk_editor_wrap_get));
  lcl_ns_def_take(ns, "editor_command",
                  lcl_c_proc_new("Lk::editor_command", c_lk_editor_command));
  lcl_ns_def_take(
      ns, "editor_set_spans",
      lcl_c_proc_new("Lk::editor_set_spans", c_lk_editor_set_spans));

  /* Annotation stores */
  lcl_ns_def_take(ns, "annot_store_new",
                  lcl_c_proc_new("Lk::annot_store_new", c_lk_annot_store_new));
  lcl_ns_def_take(ns, "annot_attach",
                  lcl_c_proc_new("Lk::annot_attach", c_lk_annot_attach));
  lcl_ns_def_take(ns, "annot_add",
                  lcl_c_proc_new("Lk::annot_add", c_lk_annot_add));
  lcl_ns_def_take(ns, "annot_remove",
                  lcl_c_proc_new("Lk::annot_remove", c_lk_annot_remove));
  lcl_ns_def_take(ns, "annot_span",
                  lcl_c_proc_new("Lk::annot_span", c_lk_annot_span));
  lcl_ns_def_take(ns, "annot_meta",
                  lcl_c_proc_new("Lk::annot_meta", c_lk_annot_meta));
  lcl_ns_def_take(ns, "annot_meta_all",
                  lcl_c_proc_new("Lk::annot_meta_all", c_lk_annot_meta_all));
  lcl_ns_def_take(ns, "annot_layer",
                  lcl_c_proc_new("Lk::annot_layer", c_lk_annot_layer));
  lcl_ns_def_take(ns, "annot_in_range",
                  lcl_c_proc_new("Lk::annot_in_range", c_lk_annot_in_range));
  lcl_ns_def_take(ns, "annot_at",
                  lcl_c_proc_new("Lk::annot_at", c_lk_annot_at));
  lcl_ns_def_take(ns, "annot_by_layer",
                  lcl_c_proc_new("Lk::annot_by_layer", c_lk_annot_by_layer));
  lcl_ns_def_take(
      ns, "annot_layer_register",
      lcl_c_proc_new("Lk::annot_layer_register", c_lk_annot_layer_register));
  lcl_ns_def_take(ns, "annot_store_seq",
                  lcl_c_proc_new("Lk::annot_store_seq", c_lk_annot_store_seq));
  lcl_ns_def_take(ns, "annot_layers",
                  lcl_c_proc_new("Lk::annot_layers", c_lk_annot_layers));
  lcl_ns_def_take(
      ns, "annot_layer_priority",
      lcl_c_proc_new("Lk::annot_layer_priority", c_lk_annot_layer_priority));

  /* Range presentations (weft-surface S1) */
  lcl_ns_def_take(ns, "annot_present",
                  lcl_c_proc_new("Lk::annot_present", c_lk_annot_present));
  lcl_ns_def_take(ns, "editor_pos_at",
                  lcl_c_proc_new("Lk::editor_pos_at", c_lk_editor_pos_at));
  lcl_ns_def_take(
      ns, "editor_presentations",
      lcl_c_proc_new("Lk::editor_presentations", c_lk_editor_presentations));

  /* Images */
  lcl_ns_def_take(ns, "image_new",
                  lcl_c_proc_new("Lk::image_new", c_lk_image_new));
  lcl_ns_def_take(ns, "image_size",
                  lcl_c_proc_new("Lk::image_size", c_lk_image_size));
  lcl_ns_def_take(ns, "image_get_px",
                  lcl_c_proc_new("Lk::image_get_px", c_lk_image_get_px));
  lcl_ns_def_take(ns, "image_set_px",
                  lcl_c_proc_new("Lk::image_set_px", c_lk_image_set_px));
  lcl_ns_def_take(ns, "image_bytes",
                  lcl_c_proc_new("Lk::image_bytes", c_lk_image_bytes));
  lcl_ns_def_take(ns, "image_set_bytes",
                  lcl_c_proc_new("Lk::image_set_bytes", c_lk_image_set_bytes));

  /* Vector canvas */
  lcl_ns_def_take(ns, "canvas_new",
                  lcl_c_proc_new("Lk::canvas_new", c_lk_canvas_new));
  lcl_ns_def_take(ns, "canvas_size",
                  lcl_c_proc_new("Lk::canvas_size", c_lk_canvas_size));
  lcl_ns_def_take(ns, "canvas_set_size",
                  lcl_c_proc_new("Lk::canvas_set_size", c_lk_canvas_set_size));
  lcl_ns_def_take(ns, "canvas_clear",
                  lcl_c_proc_new("Lk::canvas_clear", c_lk_canvas_clear));
  lcl_ns_def_take(ns, "list_range",
                  lcl_c_proc_new("Lk::list_range", c_lk_list_range));
  lcl_ns_def_take(ns, "list_scroll_to",
                  lcl_c_proc_new("Lk::list_scroll_to", c_lk_list_scroll_to));
  lcl_ns_def_take(ns, "context_menu",
                  lcl_c_proc_new("Lk::context_menu", c_lk_context_menu));
  lcl_ns_def_take(ns, "context_menu_focus",
                  lcl_c_proc_new("Lk::context_menu_focus", c_lk_context_menu_focus));
  lcl_ns_def_take(ns, "menu_open",
                  lcl_c_proc_new("Lk::menu_open", c_lk_menu_open));
  lcl_ns_def_take(ns, "menu_items",
                  lcl_c_proc_new("Lk::menu_items", c_lk_menu_items));
  lcl_ns_def_take(ns, "menu_hover",
                  lcl_c_proc_new("Lk::menu_hover", c_lk_menu_hover));
  lcl_ns_def_take(ns, "menu_activate",
                  lcl_c_proc_new("Lk::menu_activate", c_lk_menu_activate));
  lcl_ns_def_take(ns, "menu_close",
                  lcl_c_proc_new("Lk::menu_close", c_lk_menu_close));
  lcl_ns_def_take(ns, "spans_new",
                  lcl_c_proc_new("Lk::spans_new", c_lk_spans_new));
  lcl_ns_def_take(ns, "spans_clear",
                  lcl_c_proc_new("Lk::spans_clear", c_lk_spans_clear));
  lcl_ns_def_take(ns, "spans_count",
                  lcl_c_proc_new("Lk::spans_count", c_lk_spans_count));
  lcl_ns_def_take(ns, "spans_add",
                  lcl_c_proc_new("Lk::spans_add", c_lk_spans_add));
  lcl_ns_def_take(ns, "spans_present",
                  lcl_c_proc_new("Lk::spans_present", c_lk_spans_present));
  lcl_ns_def_take(ns, "styled_text_pos_at",
                  lcl_c_proc_new("Lk::styled_text_pos_at", c_lk_styled_text_pos_at));
  lcl_ns_def_take(ns, "canvas_clip",
                  lcl_c_proc_new("Lk::canvas_clip", c_lk_canvas_clip));
  lcl_ns_def_take(ns, "canvas_clip_end",
                  lcl_c_proc_new("Lk::canvas_clip_end", c_lk_canvas_clip_end));
  lcl_ns_def_take(ns, "canvas_op_count",
                  lcl_c_proc_new("Lk::canvas_op_count", c_lk_canvas_op_count));
  lcl_ns_def_take(ns, "canvas_line",
                  lcl_c_proc_new("Lk::canvas_line", c_lk_canvas_line));
  lcl_ns_def_take(ns, "canvas_polyline",
                  lcl_c_proc_new("Lk::canvas_polyline", c_lk_canvas_polyline));
  lcl_ns_def_take(ns, "canvas_rect",
                  lcl_c_proc_new("Lk::canvas_rect", c_lk_canvas_rect));
  lcl_ns_def_take(ns, "canvas_fill_rect",
                  lcl_c_proc_new("Lk::canvas_fill_rect", c_lk_canvas_fill_rect));
  lcl_ns_def_take(ns, "canvas_text",
                  lcl_c_proc_new("Lk::canvas_text", c_lk_canvas_text));

#ifdef LK_HAVE_SDL
  /* SDL Window */
  lcl_ns_def_take(ns, "window_create",
                  lcl_c_proc_new("Lk::window_create", c_lk_window_create));
  lcl_ns_def_take(ns, "window_destroy",
                  lcl_c_proc_new("Lk::window_destroy", c_lk_window_destroy));
  lcl_ns_def_take(ns, "window_run",
                  lcl_c_proc_new("Lk::window_run", c_lk_window_run));
  lcl_ns_def_take(ns, "window_ui",
                  lcl_c_proc_new("Lk::window_ui", c_lk_window_ui));
  lcl_ns_def_take(ns, "window_set_event_handler",
                  lcl_c_proc_new("Lk::window_set_event_handler",
                                 c_lk_window_set_event_handler));
  lcl_ns_def_take(ns, "register_font",
                  lcl_c_proc_new("Lk::register_font", c_lk_register_font));
  lcl_ns_def_take(ns, "window_icon",
                  lcl_c_proc_new("Lk::window_icon", c_lk_window_icon));
  lcl_ns_def_take(ns, "window_icon_hex",
                  lcl_c_proc_new("Lk::window_icon_hex", c_lk_window_icon_hex));
  lcl_ns_def_take(ns, "window_screenshot",
                  lcl_c_proc_new("Lk::window_screenshot",
                                 c_lk_window_screenshot));
  lcl_ns_def_take(ns, "window_stop",
                  lcl_c_proc_new("Lk::window_stop", c_lk_window_stop));

  /* Image file IO + file dialogs */
  lcl_ns_def_take(ns, "image_load",
                  lcl_c_proc_new("Lk::image_load", c_lk_image_load));
  lcl_ns_def_take(ns, "image_save",
                  lcl_c_proc_new("Lk::image_save", c_lk_image_save));
  lcl_ns_def_take(
      ns, "open_file_dialog",
      lcl_c_proc_new("Lk::open_file_dialog", c_lk_open_file_dialog));
  lcl_ns_def_take(
      ns, "save_file_dialog",
      lcl_c_proc_new("Lk::save_file_dialog", c_lk_save_file_dialog));
#endif
}
