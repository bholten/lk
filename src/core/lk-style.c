/*
 * lk-style.c — Theme storage, style resolver, default theme.
 *
 * C89 (matches lk core).
 */

#include <string.h>

#include "lk-memory.h"
#include <lk.h>

/* ---- Internal rule struct ---- */

typedef struct lk_style_rule {
  lk_u16 kind;      /* 0 = any */
  lk_u32 tag_id;    /* 0 = any */
  lk_u8 state_mask; /* 0 = any */
  lk_style style;
  lk_u32 field_mask;
} lk_style_rule;

struct lk_theme {
  lk_style_rule *rules;
  lk_u32 count, cap;
};

/* ---- Theme API ---- */

lk_theme *lk_theme_new(void) {
  lk_theme *th = (lk_theme *)lk_sys_alloc(NULL, (lk_u32)sizeof(lk_theme));
  if (!th) {
    return NULL;
  }
  memset(th, 0, sizeof(*th));
  return th;
}

void lk_theme_destroy(lk_theme *th) {
  if (!th) {
    return;
  }
  if (th->rules) {
    lk_sys_dealloc(NULL, th->rules);
  }
  lk_sys_dealloc(NULL, th);
}

void lk_theme_add_rule(lk_theme *th, lk_u16 kind, lk_u32 tag_id,
                       lk_u8 state_mask, const lk_style *style,
                       lk_u32 field_mask) {
  lk_style_rule *r;
  if (!th || !style) {
    return;
  }
  if (th->count >= th->cap) {
    lk_u32 new_cap = th->cap ? th->cap * 2 : 16;
    lk_style_rule *nr = (lk_style_rule *)lk_sys_alloc(
        NULL, (lk_u32)(sizeof(lk_style_rule) * new_cap));
    if (!nr) {
      return;
    }
    if (th->rules && th->count) {
      memcpy(nr, th->rules, sizeof(lk_style_rule) * th->count);
    }
    if (th->rules) {
      lk_sys_dealloc(NULL, th->rules);
    }
    th->rules = nr;
    th->cap = new_cap;
  }
  r = &th->rules[th->count++];
  r->kind = kind;
  r->tag_id = tag_id;
  r->state_mask = state_mask;
  r->style = *style;
  r->field_mask = field_mask;
}

/* ---- Default theme ---- */

static lk_color mk_color(lk_u8 r, lk_u8 g, lk_u8 b, lk_u8 a) {
  lk_color c;
  c.r = r;
  c.g = g;
  c.b = b;
  c.a = a;
  return c;
}

lk_theme *lk_theme_default(void) {
  lk_theme *th = lk_theme_new();
  lk_style s;
  if (!th) {
    return NULL;
  }

  /* wildcard fg for all nodes */
  memset(&s, 0, sizeof(s));
  s.fg = mk_color(220, 220, 220, 255);
  lk_theme_add_rule(th, 0, 0, 0, &s, LK_SF_FG);

  /* WINDOW */
  memset(&s, 0, sizeof(s));
  s.bg = mk_color(30, 30, 30, 255);
  s.padding = 0;
  lk_theme_add_rule(th, UIK_WINDOW, 0, 0, &s, LK_SF_BG | LK_SF_PADDING);

  /* COLUMN */
  memset(&s, 0, sizeof(s));
  s.padding = 0;
  s.gap = 0;
  lk_theme_add_rule(th, UIK_COLUMN, 0, 0, &s, LK_SF_PADDING | LK_SF_GAP);

  /* ROW */
  memset(&s, 0, sizeof(s));
  s.padding = 0;
  s.gap = 0;
  lk_theme_add_rule(th, UIK_ROW, 0, 0, &s, LK_SF_PADDING | LK_SF_GAP);

  /* BUTTON */
  memset(&s, 0, sizeof(s));
  s.bg = mk_color(60, 60, 60, 255);
  s.padding = 8;
  lk_theme_add_rule(th, UIK_BUTTON, 0, 0, &s, LK_SF_BG | LK_SF_PADDING);

  return th;
}

/* ---- Tag check (delegates to lk_tree_has_tag) ---- */

static int tree_has_tag(const lk_tree *t, lk_ix node, lk_u32 tag_id) {
  return lk_tree_has_tag(t, node, tag_id);
}

/* ---- Apply a single rule's fields to a style ---- */

static void apply_rule(lk_style *dst, lk_u32 *set_mask,
                       const lk_style_rule *rule) {
  lk_u32 fm = rule->field_mask;
  if (fm & LK_SF_FG) {
    dst->fg = rule->style.fg;
  }
  if (fm & LK_SF_BG) {
    dst->bg = rule->style.bg;
  }
  if (fm & LK_SF_FONT_ID) {
    dst->font_id = rule->style.font_id;
  }
  if (fm & LK_SF_FONT_SIZE) {
    dst->font_size = rule->style.font_size;
  }
  if (fm & LK_SF_PADDING) {
    dst->padding = rule->style.padding;
  }
  if (fm & LK_SF_GAP) {
    dst->gap = rule->style.gap;
  }
  if (fm & LK_SF_BORDER_WIDTH) {
    dst->border_width = rule->style.border_width;
  }
  if (fm & LK_SF_BORDER_COLOR) {
    dst->border_color = rule->style.border_color;
  }
  if (fm & LK_SF_BORDER_RADIUS) {
    dst->border_radius = rule->style.border_radius;
  }
  if (fm & LK_SF_ALIGN) {
    dst->align = rule->style.align;
  }
  if (fm & LK_SF_JUSTIFY) {
    dst->justify = rule->style.justify;
  }
  *set_mask |= fm;
}

/* ---- Resolver ---- */

void lk_style_resolve(const lk_theme *th, const lk_tree *t,
                      const lk_u8 *node_states, lk_style *styles) {
  /* Stack-based top-down DFS */
  lk_ix *stack;
  lk_u32 sp;

  if (!th || !t || !styles) {
    return;
  }
  if (t->root == 0 || t->root >= t->node_count) {
    return;
  }

  stack = (lk_ix *)lk_sys_alloc(NULL, (lk_u32)(sizeof(lk_ix) * t->node_count));
  if (!stack) {
    return;
  }

  sp = 0;
  stack[sp++] = t->root;

  while (sp > 0) {
    lk_ix n = stack[--sp];
    const lk_node *nd = &t->nodes[n];
    lk_u16 kind = nd->kind;
    lk_u8 nstate = node_states ? node_states[n] : 0;
    lk_u32 set_mask = 0;
    lk_u32 ri;
    lk_ix child;
    int child_count, nk;
    lk_ix *kids;

    memset(&styles[n], 0, sizeof(lk_style));

    /* Walk rules in order */
    for (ri = 0; ri < th->count; ri++) {
      const lk_style_rule *rule = &th->rules[ri];
      /* Check kind match */
      if (rule->kind != 0 && rule->kind != kind) {
        continue;
      }
      /* Check tag match */
      if (rule->tag_id != 0 && !tree_has_tag(t, n, rule->tag_id)) {
        continue;
      }
      /* Check state match */
      if (rule->state_mask != 0 &&
          (nstate & rule->state_mask) != rule->state_mask) {
        continue;
      }
      apply_rule(&styles[n], &set_mask, rule);
    }

    /* Tree prop overrides */
    if (lk_node_has_prop(t, n, UIP_PADDING)) {
      styles[n].padding = lk_node_prop_i32(t, n, UIP_PADDING, 0);
      set_mask |= LK_SF_PADDING;
    }
    if (lk_node_has_prop(t, n, UIP_GAP)) {
      styles[n].gap = lk_node_prop_i32(t, n, UIP_GAP, 0);
      set_mask |= LK_SF_GAP;
    }
    if (lk_node_has_prop(t, n, UIP_ALIGN)) {
      styles[n].align = (lk_u8)lk_node_prop_i32(t, n, UIP_ALIGN, 0);
      set_mask |= LK_SF_ALIGN;
    }
    if (lk_node_has_prop(t, n, UIP_JUSTIFY)) {
      styles[n].justify = (lk_u8)lk_node_prop_i32(t, n, UIP_JUSTIFY, 0);
      set_mask |= LK_SF_JUSTIFY;
    }

    /* Inheritance from parent */
    {
      lk_u32 inherit = LK_STYLE_INHERIT_MASK & ~set_mask;
      if (nd->parent != 0 && nd->parent < t->node_count) {
        if (inherit & LK_SF_FG) {
          styles[n].fg = styles[nd->parent].fg;
        }
        if (inherit & LK_SF_FONT_ID) {
          styles[n].font_id = styles[nd->parent].font_id;
        }
        if (inherit & LK_SF_FONT_SIZE) {
          styles[n].font_size = styles[nd->parent].font_size;
        }
      } else {
        /* Root fallbacks */
        if (inherit & LK_SF_FG) {
          styles[n].fg = mk_color(220, 220, 220, 255);
        }
      }
    }

    /* Push children (reverse for correct DFS order) */
    child_count = 0;
    child = nd->first_child;
    while (child) {
      child_count++;
      child = t->nodes[child].next_sibling;
    }

    if (child_count > 0) {
      kids = (lk_ix *)lk_sys_alloc(NULL, (lk_u32)(sizeof(lk_ix) * child_count));
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

  lk_sys_dealloc(NULL, stack);
}

/* ---- Style tracing ---- */

void lk_style_trace_node(const lk_theme *th, const lk_tree *t, lk_ix node,
                         lk_u8 node_state, lk_style_trace *out) {
  lk_u16 kind;
  lk_u32 ri;

  if (!th || !t || !out) {
    return;
  }
  if (node == 0 || node >= t->node_count) {
    return;
  }

  out->count = 0;
  kind = t->nodes[node].kind;

  for (ri = 0; ri < th->count; ri++) {
    const lk_style_rule *rule = &th->rules[ri];
    lk_style_trace_entry *e;

    if (rule->kind != 0 && rule->kind != kind) {
      continue;
    }
    if (rule->tag_id != 0 && !tree_has_tag(t, node, rule->tag_id)) {
      continue;
    }
    if (rule->state_mask != 0 &&
        (node_state & rule->state_mask) != rule->state_mask) {
      continue;
    }

    /* Grow trace array */
    if (out->count >= out->cap) {
      lk_u32 new_cap = out->cap ? out->cap * 2 : 8;
      lk_style_trace_entry *ne = (lk_style_trace_entry *)lk_sys_alloc(
          NULL, (lk_u32)(sizeof(lk_style_trace_entry) * new_cap));
      if (!ne) {
        return;
      }
      if (out->entries && out->count) {
        memcpy(ne, out->entries, sizeof(lk_style_trace_entry) * out->count);
      }
      if (out->entries) {
        lk_sys_dealloc(NULL, out->entries);
      }
      out->entries = ne;
      out->cap = new_cap;
    }

    e = &out->entries[out->count++];
    e->rule_index = ri;
    e->field_mask = rule->field_mask;
  }
}
