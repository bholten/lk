#include <string.h>

#include "lk-data.h"
#include "lk-memory.h"

/**Hardcoded MVP theme colors **/
static lk_color color_window_bg(void) {
  lk_color c;
  c.r = 30; c.g = 30; c.b = 30; c.a = 255;
  return c;
}

static lk_color color_button_bg(void) {
  lk_color c;
  c.r = 60; c.g = 60; c.b = 60; c.a = 255;
  return c;
}

static lk_color color_text(void) {
  lk_color c;
  c.r = 220; c.g = 220; c.b = 220; c.a = 255;
  return c;
}

static lk_i32 node_prop_i32(const lk_tree *t, lk_ix n, lk_prop_key key,
                            lk_i32 def) {
  const lk_node *nd = &t->nodes[n];
  lk_u32 i;

  for (i = 0; i < nd->props_len; i++) {
    const lk_prop *p = &t->props[nd->props_off + i];

    if ((lk_prop_key)p->key == key && p->value.tag == UIV_I32) {
      return (lk_i32)p->value.as.i;
    }
  }

  return def;
}

static lk_u32 node_text_id(const lk_tree *t, lk_ix n) {
  const lk_node *nd = &t->nodes[n];
  lk_u32 i;

  for (i = 0; i < nd->props_len; i++) {
    const lk_prop *p = &t->props[nd->props_off + i];

    if ((lk_prop_key)p->key == UIP_TEXT && p->value.tag == UIV_STR) {
      return p->value.as.str_id;
    }
  }

  return 0;
}

static int rl_push(lk_render_list *rl, lk_render_cmd cmd) {
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

int lk_render_build(const lk_tree *t, const lk_rect *rects,
                    lk_render_list *out) {
  lk_ix *stack;
  lk_u32 sp;

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

  stack = (lk_ix *)lk_sys_alloc(NULL, (lk_u32)(sizeof(lk_ix) * t->node_count));

  if (!stack) {
    return 0;
  }

  sp = 0;
  stack[sp++] = t->root;

  while (sp > 0) {
    lk_ix n = stack[--sp];
    const lk_node *nd = &t->nodes[n];
    lk_kind kind = (lk_kind)nd->kind;

    switch (kind) {
    case UIK_WINDOW: {
      lk_render_cmd cmd;
      memset(&cmd, 0, sizeof(cmd));
      cmd.op = LK_ROP_FILL_RECT;
      cmd.rect = rects[n];
      cmd.color = color_window_bg();
      rl_push(out, cmd);
      break;
    }

    case UIK_LABEL: {
      lk_u32 sid = node_text_id(t, n);

      if (sid != 0) {
        lk_render_cmd cmd;
        memset(&cmd, 0, sizeof(cmd));
        cmd.op = LK_ROP_DRAW_TEXT;
        cmd.rect = rects[n];
        cmd.color = color_text();
        cmd.str_id = sid;
        rl_push(out, cmd);
      }

      break;
    }

    case UIK_BUTTON: {
      lk_i32 pad = node_prop_i32(t, n, UIP_PADDING, 0);
      lk_u32 sid = node_text_id(t, n);
      lk_render_cmd cmd;

      memset(&cmd, 0, sizeof(cmd));
      cmd.op = LK_ROP_FILL_RECT;
      cmd.rect = rects[n];
      cmd.color = color_button_bg();
      rl_push(out, cmd);

      if (sid != 0) {
        memset(&cmd, 0, sizeof(cmd));
        cmd.op = LK_ROP_DRAW_TEXT;
        cmd.rect.x = rects[n].x + pad;
        cmd.rect.y = rects[n].y + pad;
        cmd.rect.w = rects[n].w - pad * 2;
        cmd.rect.h = rects[n].h - pad * 2;
        cmd.color = color_text();
        cmd.str_id = sid;
        rl_push(out, cmd);
      }

      break;
    }

    default:
      /* COLUMN, ROW, SPACER — invisible containers, emit nothing */
      break;
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

      if (child_count > 0) {
        kids = (lk_ix *)lk_sys_alloc(
            NULL, (lk_u32)(sizeof(lk_ix) * child_count));

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
