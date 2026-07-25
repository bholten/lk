#include <string.h>

#include "lk-memory.h"
#include <lk.h>

/* ---- Command queue operations ---- */

static int cmd_queue_push(lk_command_queue *q, void *(*alloc)(void *, lk_u32),
                          void *alloc_ud, void (*dealloc)(void *, void *),
                          const lk_command *cmd) {
  if (q->count >= q->cap) {
    lk_u32 new_cap = q->cap ? q->cap * 2 : 8;
    lk_command *nc =
        (lk_command *)alloc(alloc_ud, (lk_u32)(sizeof(lk_command) * new_cap));

    if (!nc) {
      return 0;
    }

    if (q->cmds && q->count) {
      memcpy(nc, q->cmds, sizeof(lk_command) * q->count);
    }

    if (q->cmds) {
      dealloc(alloc_ud, q->cmds);
    }

    q->cmds = nc;
    q->cap = new_cap;
  }

  q->cmds[q->count++] = *cmd;
  return 1;
}

static int log_push(lk_ui *ui, const lk_command *cmd) {
  if (ui->cmd_log_count >= ui->cmd_log_cap) {
    lk_u32 new_cap = ui->cmd_log_cap ? ui->cmd_log_cap * 2 : 16;
    lk_command *nc = (lk_command *)ui->alloc(
        ui->alloc_ud, (lk_u32)(sizeof(lk_command) * new_cap));

    if (!nc) {
      return 0;
    }

    if (ui->cmd_log && ui->cmd_log_count) {
      memcpy(nc, ui->cmd_log, sizeof(lk_command) * ui->cmd_log_count);
    }

    if (ui->cmd_log) {
      ui->dealloc(ui->alloc_ud, ui->cmd_log);
    }

    ui->cmd_log = nc;
    ui->cmd_log_cap = new_cap;
  }

  ui->cmd_log[ui->cmd_log_count++] = *cmd;
  return 1;
}

/* ---- Translator management ---- */

void lk_ui_add_translator(lk_ui *ui, lk_u8 event_type, lk_u32 ptype,
                          lk_u16 node_kind, lk_u16 keycode, lk_u8 mods,
                          lk_u32 command_name) {
  lk_translator *tr;

  if (!ui) {
    return;
  }

  if (ui->translator_count >= ui->translator_cap) {
    lk_u32 new_cap = ui->translator_cap ? ui->translator_cap * 2 : 8;
    lk_translator *nt = (lk_translator *)ui->alloc(
        ui->alloc_ud, (lk_u32)(sizeof(lk_translator) * new_cap));

    if (!nt) {
      return;
    }

    if (ui->translators && ui->translator_count) {
      memcpy(nt, ui->translators, sizeof(lk_translator) * ui->translator_count);
    }

    if (ui->translators) {
      ui->dealloc(ui->alloc_ud, ui->translators);
    }

    ui->translators = nt;
    ui->translator_cap = new_cap;
  }

  tr = &ui->translators[ui->translator_count++];
  tr->event_type = event_type;
  tr->ptype = ptype;
  tr->node_kind = node_kind;
  tr->keycode = keycode;
  tr->mods = mods;
  tr->command_name = command_name;
}

void lk_ui_add_translator_s(lk_ui *ui, lk_u8 event_type, const char *ptype,
                            lk_u16 node_kind, lk_u16 keycode, lk_u8 mods,
                            const char *command_name) {
  lk_u32 pt;
  lk_u32 cn;

  if (!ui || !ui->intern || !command_name) {
    return;
  }

  /* Treat NULL or empty-string ptype as "any" (matches Lcl-binding
   * convention, so both layers agree). */
  pt = (ptype && ptype[0] != '\0')
           ? lk_intern_id(ui->intern, lk_str_c(ptype))
           : 0;
  cn = lk_intern_id(ui->intern, lk_str_c(command_name));

  lk_ui_add_translator(ui, event_type, pt, node_kind, keycode, mods, cn);
}

void lk_ui_clear_translators(lk_ui *ui) {
  if (ui) {
    ui->translator_count = 0;
  }
}

/* ---- Command handler ---- */

void lk_ui_set_command_handler(lk_ui *ui, lk_command_handler_fn fn, void *ud) {
  if (!ui) {
    return;
  }

  ui->cmd_handler = fn;
  ui->cmd_handler_ud = ud;
}

/* ---- Command queue access ---- */

const lk_command_queue *lk_ui_commands(const lk_ui *ui) {
  if (!ui) {
    return NULL;
  }

  return &ui->cmd_queue;
}

void lk_ui_clear_commands(lk_ui *ui) {
  if (ui) {
    ui->cmd_queue.count = 0;
  }
}

/* ---- Introspection ---- */

static void wr_cstr(lk_write_fn wr, void *ud, const char *s) {
  if (!wr || !s) {
    return;
  }

  wr(ud, s, (lk_u32)strlen(s));
}

static void wr_u32(lk_write_fn wr, void *ud, lk_u32 x) {
  char buf[32];
  char tmp[32];
  lk_u32 i = 0;
  int n;

  if (x == 0) {
    buf[0] = '0';
    buf[1] = 0;
    wr_cstr(wr, ud, buf);
    return;
  }

  while (x > 0 && i < sizeof(tmp)) {
    tmp[i++] = (char)('0' + (x % 10));
    x /= 10;
  }

  n = 0;

  while (i > 0) {
    buf[n++] = tmp[--i];
  }

  buf[n] = 0;
  wr_cstr(wr, ud, buf);
}

void lk_ui_dump_commands(const lk_ui *ui, lk_write_fn wr, void *wr_ud) {
  lk_u32 i;

  if (!ui || !wr) {
    return;
  }

  wr_cstr(wr, wr_ud, "(translators");

  for (i = 0; i < ui->translator_count; i++) {
    const lk_translator *tr = &ui->translators[i];

    wr_cstr(wr, wr_ud, "\n  (translator :event ");
    wr_u32(wr, wr_ud, (lk_u32)tr->event_type);
    wr_cstr(wr, wr_ud, " :ptype ");
    wr_u32(wr, wr_ud, tr->ptype);
    wr_cstr(wr, wr_ud, " :kind ");
    wr_u32(wr, wr_ud, (lk_u32)tr->node_kind);
    wr_cstr(wr, wr_ud, " :key ");
    wr_u32(wr, wr_ud, (lk_u32)tr->keycode);
    wr_cstr(wr, wr_ud, " :mods ");
    wr_u32(wr, wr_ud, (lk_u32)tr->mods);
    wr_cstr(wr, wr_ud, " :cmd ");

    if (ui->intern && tr->command_name) {
      lk_str s = lk_intern_str(ui->intern, tr->command_name);

      if (s.ptr && s.len) {
        wr_cstr(wr, wr_ud, "\"");
        wr(wr_ud, s.ptr, s.len);
        wr_cstr(wr, wr_ud, "\"");
      } else {
        wr_u32(wr, wr_ud, tr->command_name);
      }
    } else {
      wr_u32(wr, wr_ud, tr->command_name);
    }

    wr_cstr(wr, wr_ud, ")");
  }

  wr_cstr(wr, wr_ud, ")\n");
}

const lk_command *lk_ui_command_log(const lk_ui *ui, lk_u32 *out_count) {
  if (!ui) {
    if (out_count) {
      *out_count = 0;
    }

    return NULL;
  }

  if (out_count) {
    *out_count = ui->cmd_log_count;
  }

  return ui->cmd_log;
}

void lk_ui_clear_command_log(lk_ui *ui) {
  if (ui) {
    ui->cmd_log_count = 0;
  }
}

/* ---- Translator dispatch ---- */

void lk_translate_event(lk_ui *ui, const lk_tree *t, lk_event *event) {
  lk_ix node;
  lk_ix top_disabled;
  int suppressed;

  if (!ui || !t || !event || event->handled) {
    return;
  }

  if (ui->translator_count == 0) {
    return;
  }

  /* Disabled suppression: find the topmost (closest-to-root) disabled
   * node on the target->root path.  That node and everything below it
   * must not emit commands; ancestors above it still may. */
  top_disabled = 0;
  node = event->target;

  while (node != 0 && node < t->node_count) {
    if (lk_node_prop_bool(t, node, UIP_DISABLED)) {
      top_disabled = node;
    }

    node = t->nodes[node].parent;
  }

  suppressed = (top_disabled != 0);

  /* Walk from target up to root, looking for presentations */
  node = event->target;

  while (node != 0 && node < t->node_count) {
    const lk_presentation *pres;
    lk_u32 pi;

    /* Check all presentations on this node (skipped while inside a
     * disabled subtree) */
    for (pi = 0; suppressed == 0 && pi < t->pres_count; pi++) {
      if (t->pres[pi].node != node) {
        continue;
      }

      pres = &t->pres[pi];

      /* Scan translators for a match */
      {
        lk_u32 ti;

        for (ti = 0; ti < ui->translator_count; ti++) {
          const lk_translator *tr = &ui->translators[ti];

          if (tr->event_type != 0 && tr->event_type != event->type) {
            continue;
          }

          if (tr->ptype != 0 && tr->ptype != pres->ptype) {
            continue;
          }

          if (tr->node_kind != 0 && tr->node_kind != t->nodes[node].kind) {
            continue;
          }

          /* Key+mods matching: when keycode is set, require exact match */
          if (tr->keycode != 0) {
            if (event->type != LK_EVENT_KEY_DOWN &&
                event->type != LK_EVENT_KEY_UP) {
              continue;
            }

            if (event->data.key.keycode != tr->keycode ||
                event->mods != tr->mods) {
              continue;
            }
          }

          /* Match found — build and push command */
          {
            lk_command cmd;
            lk_u8 ai;

            memset(&cmd, 0, sizeof(cmd));
            cmd.name = tr->command_name;
            cmd.arg_count = pres->pvalue_count;
            if (cmd.arg_count > LK_CMD_MAX_ARGS) {
              cmd.arg_count = LK_CMD_MAX_ARGS;
            }
            for (ai = 0; ai < cmd.arg_count; ai++) {
              cmd.args[ai] = pres->pvalues[ai];
            }
            cmd.source_node = node;
            cmd.source_ptype = pres->ptype;

            /* Carry the event-intrinsic value onto the command so handlers
             * get (presentation args) + (new value) in one delivery. */
            if (event->type == LK_EVENT_VALUE_CHANGED) {
              cmd.source_value.tag = UIV_STR;
              cmd.source_value.as.str_id = event->data.value_changed.str_id;
            }

            cmd_queue_push(&ui->cmd_queue, ui->alloc, ui->alloc_ud, ui->dealloc,
                           &cmd);
            log_push(ui, &cmd);

            if (ui->cmd_handler) {
              ui->cmd_handler(&cmd, ui->cmd_handler_ud);
            }

            event->handled = 1;
            return;
          }
        }
      }
    }

    /* Once we step above the topmost disabled node, matching resumes. */
    if (node == top_disabled) {
      suppressed = 0;
    }

    node = t->nodes[node].parent;
  }
}
