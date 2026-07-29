#include <string.h>

#include "lk-memory.h"
#include <lk.h>

/* ---- Command queue operations ---- */

/* Copy len bytes into a byte arena (render-list pattern: doubling
 * growth, capacity reused across clears).  Writes the run's starting
 * offset to *out_off and returns 1; 0 on allocation failure. */
static int arena_push(char **bytes, lk_u32 *count, lk_u32 *cap,
                      void *(*alloc)(void *, lk_u32), void *alloc_ud,
                      void (*dealloc)(void *, void *), const char *ptr,
                      lk_u32 len, lk_u32 *out_off) {
  if (*count + len > *cap) {
    lk_u32 new_cap = *cap ? *cap : 64;
    char *nb;

    while (new_cap < *count + len) {
      new_cap *= 2;
    }

    nb = (char *)alloc(alloc_ud, new_cap);

    if (!nb) {
      return 0;
    }

    if (*bytes && *count) {
      memcpy(nb, *bytes, *count);
    }

    if (*bytes) {
      dealloc(alloc_ud, *bytes);
    }

    *bytes = nb;
    *cap = new_cap;
  }

  *out_off = *count;

  if (len) {
    memcpy(*bytes + *count, ptr, len);
    *count += len;
  }

  return 1;
}

lk_value lk_v_text(lk_ui *ui, const char *ptr, lk_u32 len) {
  lk_value v;
  lk_u32 off;

  v.tag = UIV_NONE;
  v.as.i = 0;

  if (!ui || (!ptr && len)) {
    return v;
  }

  if (!arena_push(&ui->cmd_queue.bytes, &ui->cmd_queue.bytes_count,
                  &ui->cmd_queue.bytes_cap, ui->alloc, ui->alloc_ud,
                  ui->dealloc, ptr, len, &off)) {
    return v;
  }

  v.tag = UIV_TEXT;
  v.as.text.off = off;
  v.as.text.len = len;

  return v;
}

/* 1 when cmd points at an element of the log array (logged commands
 * carry offsets into the log arena, everything else into the queue
 * arena).  Pointer equality only — C89-safe. */
static int cmd_is_logged(const lk_ui *ui, const lk_command *cmd) {
  lk_u32 i;

  for (i = 0; i < ui->cmd_log_count; i++) {
    if (&ui->cmd_log[i] == cmd) {
      return 1;
    }
  }

  return 0;
}

lk_str lk_command_text(const lk_ui *ui, const lk_command *cmd, lk_value v) {
  lk_str s;
  const char *bytes;
  lk_u32 count;

  s.ptr = NULL;
  s.len = 0;

  if (!ui || !cmd || v.tag != UIV_TEXT) {
    return s;
  }

  if (cmd_is_logged(ui, cmd)) {
    bytes = ui->cmd_log_bytes;
    count = ui->cmd_log_bytes_count;
  } else {
    bytes = ui->cmd_queue.bytes;
    count = ui->cmd_queue.bytes_count;
  }

  if (!bytes || v.as.text.off > count || v.as.text.len > count - v.as.text.off) {
    return s;
  }

  s.ptr = bytes + v.as.text.off;
  s.len = v.as.text.len;

  return s;
}

lk_str lk_command_arg_text(const lk_ui *ui, const lk_command *cmd, lk_u8 idx) {
  lk_str s;

  s.ptr = NULL;
  s.len = 0;

  if (!cmd || idx >= cmd->arg_count) {
    return s;
  }

  return lk_command_text(ui, cmd, cmd->args[idx]);
}

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

/* Rewrite one UIV_TEXT value in the log's copy of a command: the
 * bytes (currently in the queue arena) are copied into the log arena
 * so the log stays readable after lk_ui_clear_commands.  Failed
 * copies degrade to UIV_NONE rather than dangle. */
static void log_copy_text(lk_ui *ui, const lk_command *src_cmd, lk_value *v) {
  lk_str s;
  lk_u32 off;

  if (v->tag != UIV_TEXT) {
    return;
  }

  s = lk_command_text(ui, src_cmd, *v);

  if ((!s.ptr && s.len) ||
      !arena_push(&ui->cmd_log_bytes, &ui->cmd_log_bytes_count,
                  &ui->cmd_log_bytes_cap, ui->alloc, ui->alloc_ud, ui->dealloc,
                  s.ptr, s.len, &off)) {
    v->tag = UIV_NONE;
    v->as.i = 0;
    return;
  }

  v->as.text.off = off;
  v->as.text.len = s.len;
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

  ui->cmd_log[ui->cmd_log_count] = *cmd;

  /* The log retains commands past lk_ui_clear_commands: copy every
   * transient text value into the log's own arena. */
  {
    lk_command *lc = &ui->cmd_log[ui->cmd_log_count];
    lk_u8 ai;

    for (ai = 0; ai < lc->arg_count && ai < LK_CMD_MAX_ARGS; ai++) {
      log_copy_text(ui, cmd, &lc->args[ai]);
    }

    log_copy_text(ui, cmd, &lc->source_value);
    log_copy_text(ui, cmd, &lc->hit.value);
  }

  ui->cmd_log_count++;
  return 1;
}

/* ---- Translator management ---- */

void lk_ui_add_translator(lk_ui *ui, lk_u8 event_type, lk_u32 ptype,
                          lk_u16 node_kind, lk_u16 keycode, lk_u8 mods,
                          lk_u8 button, lk_u32 command_name) {
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
  tr->button = button;
  tr->command_name = command_name;
}

void lk_ui_add_translator_s(lk_ui *ui, lk_u8 event_type, const char *ptype,
                            lk_u16 node_kind, lk_u16 keycode, lk_u8 mods,
                            lk_u8 button, const char *command_name) {
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

  lk_ui_add_translator(ui, event_type, pt, node_kind, keycode, mods, button,
                       cn);
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
    ui->cmd_queue.bytes_count = 0; /* dispatch arena dies with the queue */
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
    wr_cstr(wr, wr_ud, " :button ");
    wr_u32(wr, wr_ud, (lk_u32)tr->button);
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

  /* Command log.  UIV_TEXT args are printed from the LOG's copy —
   * never from the (possibly reset) queue arena. */
  wr_cstr(wr, wr_ud, "(commands");

  for (i = 0; i < ui->cmd_log_count; i++) {
    const lk_command *cmd = &ui->cmd_log[i];
    lk_u8 ai;

    wr_cstr(wr, wr_ud, "\n  (command :name ");

    if (ui->intern && cmd->name) {
      lk_str s = lk_intern_str(ui->intern, cmd->name);

      if (s.ptr && s.len) {
        wr_cstr(wr, wr_ud, "\"");
        wr(wr_ud, s.ptr, s.len);
        wr_cstr(wr, wr_ud, "\"");
      } else {
        wr_u32(wr, wr_ud, cmd->name);
      }
    } else {
      wr_u32(wr, wr_ud, cmd->name);
    }

    wr_cstr(wr, wr_ud, " :args (");

    for (ai = 0; ai < cmd->arg_count; ai++) {
      const lk_value *v = &cmd->args[ai];

      if (ai > 0) {
        wr_cstr(wr, wr_ud, " ");
      }

      switch (v->tag) {
      case UIV_BOOL: wr_cstr(wr, wr_ud, v->as.b ? "true" : "false"); break;
      case UIV_I32: wr_u32(wr, wr_ud, (lk_u32)v->as.i); break;
      case UIV_STR: {
        lk_str s = ui->intern ? lk_intern_str(ui->intern, v->as.str_id)
                              : lk_str_c("");

        wr_cstr(wr, wr_ud, "\"");

        if (s.ptr && s.len) {
          wr(wr_ud, s.ptr, s.len);
        }

        wr_cstr(wr, wr_ud, "\"");
        break;
      }
      case UIV_RESOURCE:
        wr_cstr(wr, wr_ud, "resource#");
        wr_u32(wr, wr_ud, v->as.res.id);
        break;
      case UIV_TEXT: {
        lk_str s = lk_command_text(ui, cmd, *v);

        wr_cstr(wr, wr_ud, "<text \"");

        if (s.ptr && s.len) {
          wr(wr_ud, s.ptr, s.len);
        }

        wr_cstr(wr, wr_ud, "\">");
        break;
      }
      default: wr_cstr(wr, wr_ud, "null"); break;
      }
    }

    wr_cstr(wr, wr_ud, "))");
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
    ui->cmd_log_bytes_count = 0;
  }
}

/* ---- Translator dispatch ----
 *
 * There is ONE translator matcher (docs/weft-surface.md §1).  Node-
 * presentation routing (lk_translate_event) and interior-presentation
 * routing (lk_translate_presentations) both converge on the shared
 * translator_match + emit_translated internals below; they differ
 * only in candidate discovery. */

/* Does tr match (event, ptype, node)?  keycode != 0 and button != 0
 * both demand exact mods (the same discipline). */
static int translator_match(const lk_translator *tr, const lk_tree *t,
                            lk_ix node, const lk_event *event, lk_u32 ptype) {
  if (tr->event_type != 0 && tr->event_type != event->type) {
    return 0;
  }

  if (tr->ptype != 0 && tr->ptype != ptype) {
    return 0;
  }

  if (tr->node_kind != 0 && tr->node_kind != t->nodes[node].kind) {
    return 0;
  }

  if (tr->keycode != 0) {
    if (event->type != LK_EVENT_KEY_DOWN && event->type != LK_EVENT_KEY_UP) {
      return 0;
    }

    if (event->data.key.keycode != tr->keycode || event->mods != tr->mods) {
      return 0;
    }
  }

  if (tr->button != 0) {
    if (event->type != LK_EVENT_POINTER_DOWN &&
        event->type != LK_EVENT_POINTER_UP &&
        event->type != LK_EVENT_POINTER_MOVE) {
      return 0;
    }

    if (event->data.pointer.button != tr->button || event->mods != tr->mods) {
      return 0;
    }
  }

  return 1;
}

/* Build the command for a matched translator and dispatch it (queue,
 * log, handler).  hit may be NULL (node presentations, keybindings) —
 * the command's hit stays zeroed then. */
static void emit_translated(lk_ui *ui, const lk_translator *tr, lk_ix node,
                            const lk_event *event, lk_u32 ptype,
                            const lk_value *args, lk_u8 arg_count,
                            const lk_presentation_hit *hit) {
  lk_command cmd;
  lk_u8 ai;

  memset(&cmd, 0, sizeof(cmd));
  cmd.name = tr->command_name;
  cmd.arg_count = arg_count;

  if (cmd.arg_count > LK_CMD_MAX_ARGS) {
    cmd.arg_count = LK_CMD_MAX_ARGS;
  }

  for (ai = 0; ai < cmd.arg_count; ai++) {
    cmd.args[ai] = args[ai];
  }

  cmd.source_node = node;
  cmd.source_ptype = ptype;

  /* Carry the event-intrinsic value onto the command so handlers
   * get (presentation args) + (new value) in one delivery. */
  if (event->type == LK_EVENT_VALUE_CHANGED) {
    cmd.source_value.tag = UIV_STR;
    cmd.source_value.as.str_id = event->data.value_changed.str_id;
  }

  if (hit) {
    cmd.hit = *hit;
  }

  cmd_queue_push(&ui->cmd_queue, ui->alloc, ui->alloc_ud, ui->dealloc, &cmd);
  log_push(ui, &cmd);

  if (ui->cmd_handler) {
    ui->cmd_handler(&cmd, ui->cmd_handler_ud);
  }
}

int lk_translate_presentations(lk_ui *ui, const lk_tree *t, lk_ix node,
                               lk_event *ev, const lk_presentation_hit *hits,
                               lk_u32 n) {
  lk_u32 hi, ti;

  if (!ui || !t || !ev || !hits || ev->handled || node == 0 ||
      node >= t->node_count) {
    return 0;
  }

  /* Candidates in the given order; the gesture picks which candidate
   * is relevant — the first (hit, translator) pair wins. */
  for (hi = 0; hi < n; hi++) {
    for (ti = 0; ti < ui->translator_count; ti++) {
      const lk_translator *tr = &ui->translators[ti];

      if (!translator_match(tr, t, node, ev, hits[hi].type_id)) {
        continue;
      }

      emit_translated(ui, tr, node, ev, hits[hi].type_id, &hits[hi].value, 1,
                      &hits[hi]);
      ev->handled = 1;

      return 1;
    }
  }

  return 0;
}

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
    lk_u32 pi;

    /* Check all presentations on this node (skipped while inside a
     * disabled subtree) */
    for (pi = 0; suppressed == 0 && pi < t->pres_count; pi++) {
      const lk_presentation *pres;
      lk_u32 ti;

      if (t->pres[pi].node != node) {
        continue;
      }

      pres = &t->pres[pi];

      for (ti = 0; ti < ui->translator_count; ti++) {
        const lk_translator *tr = &ui->translators[ti];

        if (!translator_match(tr, t, node, event, pres->ptype)) {
          continue;
        }

        /* Node presentations are not interior presentations: no hit
         * attached, pvalues flow into the args unchanged. */
        emit_translated(ui, tr, node, event, pres->ptype, pres->pvalues,
                        pres->pvalue_count, NULL);
        event->handled = 1;

        return;
      }
    }

    /* Once we step above the topmost disabled node, matching resumes. */
    if (node == top_disabled) {
      suppressed = 0;
    }

    node = t->nodes[node].parent;
  }
}
