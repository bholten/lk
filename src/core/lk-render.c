#include <string.h>

#include "lk-memory.h"
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

/* High bit marks a CLIP_END sentinel on the DFS stack.  Node indices
 * never reach 2^31 so this is safe.
 */
#define CLIP_END_MARKER 0x80000000u

int lk_render_build(const lk_tree *t, const lk_rect *rects,
                    lk_render_list *out) {
  lk_ix *stack;
  lk_u32 sp;
  /* Stack needs room for each node plus a CLIP_END marker per clipping
   * node.  2x node_count is a safe upper bound.
   */
  lk_u32 stack_cap;

  if (!t || !out) {
    return 0;
  }

  out->count = 0;

  if (t->root == 0 || t->root >= t->node_count) {
    return 1;
  }

  if (!rects) {
    return 0;
  }

  stack_cap = t->node_count * 2;
  stack = (lk_ix *)lk_sys_alloc(NULL, (lk_u32)(sizeof(lk_ix) * stack_cap));

  if (!stack) {
    return 0;
  }

  sp = 0;
  stack[sp++] = t->root;

  while (sp > 0) {
    lk_ix raw = stack[--sp];
    lk_ix n;
    const lk_node *nd;
    lk_kind kind;
    const lk_widget_def *def;
    int clips;

    /* Check for CLIP_END sentinel */
    if (raw & CLIP_END_MARKER) {
      lk_render_cmd cmd;
      memset(&cmd, 0, sizeof(cmd));
      cmd.op = LK_ROP_CLIP_END;
      lk_render_list_push(out, cmd);
      continue;
    }

    n = raw;
    nd = &t->nodes[n];
    kind = (lk_kind)nd->kind;
    def = lk_widget_get(kind);
    clips = (def && def->clips) ? 1 : 0;

    if (def && def->render) {
      def->render(t, n, &rects[n], out);
    }

    /* Emit CLIP_BEGIN after own render commands, before children */
    if (clips) {
      lk_render_cmd cmd;
      memset(&cmd, 0, sizeof(cmd));
      cmd.op = LK_ROP_CLIP_BEGIN;
      cmd.rect = rects[n];
      lk_render_list_push(out, cmd);
    }

    /* Push children (and CLIP_END marker if clipping) */
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

      /* Push CLIP_END marker first so it pops AFTER all children */
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

void lk_render_list_destroy(lk_render_list *rl) {
  if (!rl) {
    return;
  }

  if (rl->cmds) {
    lk_sys_dealloc(NULL, rl->cmds);
  }

  rl->cmds = NULL;
  rl->count = 0;
  rl->cap = 0;
}
