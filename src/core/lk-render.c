#include <string.h>

#include "lk-memory.h"
#include "lk-overlay.h"
#include "lk-tabs.h"
#include <lk.h>

int lk_render_list_push(lk_render_list *rl, lk_render_cmd cmd) {
  if (rl->count >= rl->cap) {
    lk_u32 new_cap = rl->cap == 0 ? 64 : rl->cap * 2;
    lk_render_cmd *new_cmds = (lk_render_cmd *)lk_sys_alloc(
        NULL, (lk_u32)(sizeof(lk_render_cmd) * new_cap));

    if (!new_cmds) {
      return 0;
    }

    if (rl->cmds) {
      memcpy(new_cmds, rl->cmds, sizeof(lk_render_cmd) * rl->count);
      lk_sys_dealloc(NULL, rl->cmds);
    }

    rl->cmds = new_cmds;
    rl->cap = new_cap;
  }

  rl->cmds[rl->count++] = cmd;
  return 1;
}

int lk_render_list_push_run(lk_render_list *rl, const char *ptr, lk_u32 len,
                            lk_u32 *out_off) {
  if (!rl || !out_off) {
    return 0;
  }

  if (len > 0 && !ptr) {
    return 0;
  }

  if (rl->bytes_count + len > rl->bytes_cap) {
    lk_u32 new_cap = rl->bytes_cap == 0 ? 256 : rl->bytes_cap * 2;
    char *new_bytes;

    while (new_cap < rl->bytes_count + len) {
      new_cap *= 2;
    }

    new_bytes = (char *)lk_sys_alloc(NULL, new_cap);

    if (!new_bytes) {
      return 0;
    }

    if (rl->bytes) {
      memcpy(new_bytes, rl->bytes, rl->bytes_count);
      lk_sys_dealloc(NULL, rl->bytes);
    }

    rl->bytes = new_bytes;
    rl->bytes_cap = new_cap;
  }

  *out_off = rl->bytes_count;

  if (len > 0) {
    memcpy(rl->bytes + rl->bytes_count, ptr, len);
    rl->bytes_count += len;
  }

  return 1;
}

#define CLIP_END_MARKER 0x80000000u

static lk_style g_fallback_styles[LK_KIND_MAX];
static int g_fallback_inited;

static void init_fallback_styles(void) {
  lk_theme *th;
  lk_tree *t;
  lk_style *buf;
  int k;

  if (g_fallback_inited) {
    return;
  }
  g_fallback_inited = 1;
  memset(g_fallback_styles, 0, sizeof(g_fallback_styles));

  th = lk_theme_default(NULL, NULL, NULL);

  if (!th) {
    return;
  }

  t = lk_tree_create(NULL);

  if (!t) {
    lk_theme_destroy(th);
    return;
  }

  for (k = UIK_WINDOW; k < (int)UIK__COUNT; k++) {
    lk_tree_add_node(t, (lk_node_id)(k), (lk_kind)k);
  }

  lk_tree_set_root(t, 1);

  for (k = UIK_WINDOW + 1; k < (int)UIK__COUNT; k++) {
    lk_tree_append_child(t, 1, (lk_ix)k);
  }

  buf = (lk_style *)lk_sys_alloc(NULL,
                                 (lk_u32)(sizeof(lk_style) * t->node_count));
  if (buf) {
    lk_style_resolve(th, t, NULL, buf);

    for (k = UIK_WINDOW; k < (int)UIK__COUNT; k++) {
      g_fallback_styles[k] = buf[k];
    }

    lk_sys_dealloc(NULL, buf);
  }

  lk_tree_destroy(t);
  lk_theme_destroy(th);
}

static int render_walk(const lk_tree *t, lk_ix start, const lk_rect *rects,
                       const lk_style *styles, const lk_state *state,
                       const lk_widget_geom *geom, lk_render_list *out,
                       int ignore_start_hidden) {
  lk_ix *stack;
  lk_u32 sp;

  lk_u32 stack_cap;

  if (!styles && !g_fallback_inited) {
    init_fallback_styles();
  }

  stack_cap = t->node_count * 2;
  stack = (lk_ix *)lk_sys_alloc(NULL, (lk_u32)(sizeof(lk_ix) * stack_cap));

  if (!stack) {
    return 0;
  }

  sp = 0;
  stack[sp++] = start;

  while (sp > 0) {
    lk_ix raw = stack[--sp];
    lk_ix n;
    const lk_node *nd;
    lk_kind kind;
    const lk_widget_def *def;
    int clips;
    const lk_style *node_style;

    if (raw & CLIP_END_MARKER) {
      lk_render_cmd cmd;
      memset(&cmd, 0, sizeof(cmd));
      cmd.op = LK_ROP_CLIP_END;
      lk_render_list_push(out, cmd);
      continue;
    }

    n = raw;

    if (lk_node_prop_bool(t, n, UIP_HIDDEN) &&
        !(ignore_start_hidden && n == start)) {
      continue;
    }

    nd = &t->nodes[n];
    kind = (lk_kind)nd->kind;
    def = lk_widget_get(kind);
    clips = (def && def->clips) ? 1 : 0;

    if (styles) {
      node_style = &styles[n];
    } else {
      node_style = ((int)kind > 0 && (int)kind < LK_KIND_MAX)
                       ? &g_fallback_styles[(int)kind]
                       : &g_fallback_styles[0];
    }

    if (def && def->render) {
      def->render(t, n, &rects[n], node_style, state, geom ? &geom[n] : NULL,
                  out);
    } else if (node_style->bg.a > 0) {

      lk_render_cmd cmd;

      memset(&cmd, 0, sizeof(cmd));
      cmd.op = LK_ROP_FILL_RECT;
      cmd.color = node_style->bg;
      cmd.rect = rects[n];
      lk_render_list_push(out, cmd);
    }

    if (node_style->border_width > 0) {
      lk_i32 bw = node_style->border_width;
      lk_render_cmd bcmd;

      memset(&bcmd, 0, sizeof(bcmd));
      bcmd.op = LK_ROP_STROKE_RECT;
      bcmd.color = node_style->border_color;
      bcmd.rect = rects[n];
      bcmd.stroke = (lk_u8)(bw > 255 ? 255 : bw);
      lk_render_list_push(out, bcmd);
    }

    if (clips) {
      lk_render_cmd cmd;
      memset(&cmd, 0, sizeof(cmd));
      cmd.op = LK_ROP_CLIP_BEGIN;
      cmd.rect = rects[n];
      lk_render_list_push(out, cmd);
    }

    {
      lk_ix child;
      int child_count = 0;
      int nk;
      lk_ix *kids;

      child = nd->first_child;

      while (child) {
        child_count++;
        child = t->nodes[child].next_sibling;
      }

      if (lk_tabs_collapsed(t, n, state)) {
        child_count = 0;
      }

      if (clips) {
        stack[sp++] = (n | CLIP_END_MARKER);
      }

      if (child_count > 0) {
        kids =
            (lk_ix *)lk_sys_alloc(NULL, (lk_u32)(sizeof(lk_ix) * child_count));

        if (kids) {
          nk = 0;
          child = nd->first_child;

          while (child) {
            kids[nk++] = child;
            child = t->nodes[child].next_sibling;
          }

          while (nk > 0) {
            stack[sp++] = kids[--nk];
          }

          lk_sys_dealloc(NULL, kids);
        }
      }
    }
  }

  lk_sys_dealloc(NULL, stack);
  return 1;
}

int lk_render_build(const lk_tree *t, const lk_rect *rects,
                    const lk_style *styles, const lk_state *state,
                    const lk_widget_geom *geom, lk_render_list *out) {
  if (!t || !out) {
    return 0;
  }

  out->count = 0;
  out->bytes_count = 0;

  if (t->root == 0 || t->root >= t->node_count) {
    return 1;
  }

  if (!rects) {
    return 0;
  }

  return render_walk(t, t->root, rects, styles, state, geom, out, 0);
}

int lk_render_build_from(const lk_tree *t, lk_ix start, const lk_rect *rects,
                         const lk_style *styles, const lk_state *state,
                         const lk_widget_geom *geom, lk_render_list *out) {
  if (!t || !out || !rects) {
    return 0;
  }

  if (start == 0 || start >= t->node_count) {
    return 0;
  }

  return render_walk(t, start, rects, styles, state, geom, out, 1);
}

void lk_render_list_destroy(lk_render_list *rl) {
  if (!rl) {
    return;
  }

  if (rl->cmds) {
    lk_sys_dealloc(NULL, rl->cmds);
  }

  if (rl->bytes) {
    lk_sys_dealloc(NULL, rl->bytes);
  }

  rl->cmds = NULL;
  rl->count = 0;
  rl->cap = 0;
  rl->bytes = NULL;
  rl->bytes_count = 0;
  rl->bytes_cap = 0;
}
