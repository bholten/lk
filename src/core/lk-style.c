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
  void *(*alloc)(void *, lk_u32);
  void (*dealloc)(void *, void *);
  void *ud;
  lk_style_rule *rules;
  lk_u32 count, cap;
};

/* ---- Allocator helpers ---- */

static void *th_alloc(const lk_theme *th, lk_u32 bytes) {
  if (th->alloc) {
    return th->alloc(th->ud, bytes);
  }

  return lk_sys_alloc(NULL, bytes);
}

static void th_dealloc(const lk_theme *th, void *ptr) {
  if (th->dealloc) {
    th->dealloc(th->ud, ptr);
    return;
  }

  lk_sys_dealloc(NULL, ptr);
}

/* ---- Theme API ---- */

lk_theme *lk_theme_new(void *(*alloc)(void *, lk_u32),
                       void (*dealloc)(void *, void *), void *ud) {
  lk_theme *th;

  if (alloc) {
    th = (lk_theme *)alloc(ud, (lk_u32)sizeof(lk_theme));
  } else {
    th = (lk_theme *)lk_sys_alloc(NULL, (lk_u32)sizeof(lk_theme));
  }

  if (!th) {
    return NULL;
  }

  memset(th, 0, sizeof(*th));
  th->alloc = alloc;
  th->dealloc = dealloc;
  th->ud = ud;

  return th;
}

void lk_theme_destroy(lk_theme *th) {
  if (!th) {
    return;
  }

  if (th->rules) {
    th_dealloc(th, th->rules);
  }

  if (th->dealloc) {
    th->dealloc(th->ud, th);
  } else {
    lk_sys_dealloc(NULL, th);
  }
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
    lk_style_rule *nr = (lk_style_rule *)th_alloc(
        th, (lk_u32)(sizeof(lk_style_rule) * new_cap));

    if (!nr) {
      return;
    }

    if (th->rules && th->count) {
      memcpy(nr, th->rules, sizeof(lk_style_rule) * th->count);
    }

    if (th->rules) {
      th_dealloc(th, th->rules);
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

lk_theme *lk_theme_default(void *(*alloc)(void *, lk_u32),
                           void (*dealloc)(void *, void *), void *ud) {
  lk_theme *th = lk_theme_new(alloc, dealloc, ud);
  lk_style s;

  if (!th) {
    return NULL;
  }

  /* wildcard fg + accent for all nodes (accent: check marks, radio
   * dots, slider fill/thumb, selected-tab underline) */
  memset(&s, 0, sizeof(s));
  s.fg = mk_color(220, 220, 220, 255);
  s.accent = mk_color(80, 140, 220, 255);
  lk_theme_add_rule(th, 0, 0, 0, &s, LK_SF_FG | LK_SF_ACCENT);

  /* WINDOW -- align STRETCH: a root's child should fill the client
   * area (docs/grow-layout.md section 4).  ROW/COLUMN keep the
   * resolved default of START and opt in per node. */
  memset(&s, 0, sizeof(s));
  s.bg = mk_color(30, 30, 30, 255);
  s.padding = 0;
  s.align = (lk_u8)LK_ALIGN_STRETCH;
  lk_theme_add_rule(th, UIK_WINDOW, 0, 0, &s,
                    LK_SF_BG | LK_SF_PADDING | LK_SF_ALIGN);

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

  /* TEXT_INPUT */
  memset(&s, 0, sizeof(s));
  s.bg = mk_color(45, 45, 45, 255);
  s.padding = 4;
  lk_theme_add_rule(th, UIK_TEXT_INPUT, 0, 0, &s, LK_SF_BG | LK_SF_PADDING);

  /* SCROLL */
  memset(&s, 0, sizeof(s));
  s.bg = mk_color(35, 35, 35, 255);
  s.padding = 0;
  s.gap = 0;
  s.scrollbar_track = mk_color(50, 50, 50, 128);
  s.scrollbar_thumb = mk_color(120, 120, 120, 200);
  lk_theme_add_rule(th, UIK_SCROLL, 0, 0, &s,
                    LK_SF_BG | LK_SF_PADDING | LK_SF_GAP |
                        LK_SF_SCROLLBAR_TRACK | LK_SF_SCROLLBAR_THUMB);

  /* LIST (the scroll rule: same plate + bar colours) */
  memset(&s, 0, sizeof(s));
  s.bg = mk_color(35, 35, 35, 255);
  s.padding = 0;
  s.gap = 0;
  s.scrollbar_track = mk_color(50, 50, 50, 128);
  s.scrollbar_thumb = mk_color(120, 120, 120, 200);
  lk_theme_add_rule(th, UIK_LIST, 0, 0, &s,
                    LK_SF_BG | LK_SF_PADDING | LK_SF_GAP |
                        LK_SF_SCROLLBAR_TRACK | LK_SF_SCROLLBAR_THUMB);

  /* TEXT_INPUT focused */
  memset(&s, 0, sizeof(s));
  s.border_width = 1;
  s.border_color = mk_color(80, 140, 220, 255);
  lk_theme_add_rule(th, UIK_TEXT_INPUT, 0, LK_NSTATE_FOCUSED, &s,
                    LK_SF_BORDER_WIDTH | LK_SF_BORDER_COLOR);

  /* DROPDOWN */
  memset(&s, 0, sizeof(s));
  s.bg = mk_color(45, 45, 45, 255);
  s.padding = 6;
  s.border_width = 1;
  s.border_color = mk_color(80, 80, 100, 255);
  lk_theme_add_rule(th, UIK_DROPDOWN, 0, 0, &s,
                    LK_SF_BG | LK_SF_PADDING | LK_SF_BORDER_WIDTH |
                        LK_SF_BORDER_COLOR);

  /* DROPDOWN hovered */
  memset(&s, 0, sizeof(s));
  s.border_color = mk_color(120, 120, 150, 255);
  lk_theme_add_rule(th, UIK_DROPDOWN, 0, LK_NSTATE_HOVERED, &s,
                    LK_SF_BORDER_COLOR);

  /* DROPDOWN focused */
  memset(&s, 0, sizeof(s));
  s.border_color = mk_color(80, 140, 220, 255);
  lk_theme_add_rule(th, UIK_DROPDOWN, 0, LK_NSTATE_FOCUSED, &s,
                    LK_SF_BORDER_COLOR);

  /* OPTION */
  memset(&s, 0, sizeof(s));
  s.bg = mk_color(45, 45, 45, 255);
  s.padding = 4;
  lk_theme_add_rule(th, UIK_OPTION, 0, 0, &s, LK_SF_BG | LK_SF_PADDING);

  /* SPLIT_H / SPLIT_V — near-transparent bg (panes paint themselves);
   * border_color doubles as the divider band color. */
  memset(&s, 0, sizeof(s));
  s.bg = mk_color(30, 30, 30, 0);
  s.padding = 0;
  s.border_color = mk_color(70, 70, 80, 255);
  lk_theme_add_rule(th, UIK_SPLIT_H, 0, 0, &s,
                    LK_SF_BG | LK_SF_PADDING | LK_SF_BORDER_COLOR);
  lk_theme_add_rule(th, UIK_SPLIT_V, 0, 0, &s,
                    LK_SF_BG | LK_SF_PADDING | LK_SF_BORDER_COLOR);

  /* EDITOR -- dark bg slightly distinct from the window bg; fg
   * inherits from the wildcard rule.  Padding 0 keeps line geometry
   * aligned with the node rect (apps can set UIP_PADDING).
   * Scrollbar colors mirror the UIK_SCROLL rule (that rule is
   * kind-scoped, so the editor needs its own). */
  memset(&s, 0, sizeof(s));
  s.bg = mk_color(24, 25, 29, 255);
  s.padding = 0;
  s.scrollbar_track = mk_color(50, 50, 50, 128);
  s.scrollbar_thumb = mk_color(120, 120, 120, 200);
  lk_theme_add_rule(th, UIK_EDITOR, 0, 0, &s,
                    LK_SF_BG | LK_SF_PADDING | LK_SF_SCROLLBAR_TRACK |
                        LK_SF_SCROLLBAR_THUMB);

  /* CHECKBOX / RADIO -- transparent bg (the box is drawn from
   * border_color, the mark from accent); padding 2, gap 6 between the
   * box and its label. */
  memset(&s, 0, sizeof(s));
  s.bg = mk_color(0, 0, 0, 0);
  s.padding = 2;
  s.gap = 6;
  s.border_color = mk_color(120, 120, 140, 255);
  lk_theme_add_rule(th, UIK_CHECKBOX, 0, 0, &s,
                    LK_SF_BG | LK_SF_PADDING | LK_SF_GAP | LK_SF_BORDER_COLOR);
  lk_theme_add_rule(th, UIK_RADIO, 0, 0, &s,
                    LK_SF_BG | LK_SF_PADDING | LK_SF_GAP | LK_SF_BORDER_COLOR);

  /* CHECKBOX / RADIO focused -- accent-colored box outline */
  memset(&s, 0, sizeof(s));
  s.border_color = mk_color(80, 140, 220, 255);
  lk_theme_add_rule(th, UIK_CHECKBOX, 0, LK_NSTATE_FOCUSED, &s,
                    LK_SF_BORDER_COLOR);
  lk_theme_add_rule(th, UIK_RADIO, 0, LK_NSTATE_FOCUSED, &s,
                    LK_SF_BORDER_COLOR);

  /* SLIDER -- transparent bg; border_color is the empty track, accent
   * the filled track + thumb. */
  memset(&s, 0, sizeof(s));
  s.bg = mk_color(0, 0, 0, 0);
  s.padding = 4;
  s.border_color = mk_color(70, 70, 80, 255);
  lk_theme_add_rule(th, UIK_SLIDER, 0, 0, &s,
                    LK_SF_BG | LK_SF_PADDING | LK_SF_BORDER_COLOR);

  memset(&s, 0, sizeof(s));
  s.border_color = mk_color(110, 110, 130, 255);
  lk_theme_add_rule(th, UIK_SLIDER, 0, LK_NSTATE_FOCUSED, &s,
                    LK_SF_BORDER_COLOR);

  /* TABS -- bg paints the strip background; padding is the header
   * cell inset; border_color separates the strip from the page. */
  memset(&s, 0, sizeof(s));
  s.bg = mk_color(38, 38, 42, 255);
  s.padding = 6;
  s.gap = 2;
  s.border_color = mk_color(70, 70, 80, 255);
  lk_theme_add_rule(th, UIK_TABS, 0, 0, &s,
                    LK_SF_BG | LK_SF_PADDING | LK_SF_GAP | LK_SF_BORDER_COLOR);

  /* TAB -- the page: a column-like container */
  memset(&s, 0, sizeof(s));
  s.bg = mk_color(30, 30, 30, 255);
  s.padding = 8;
  s.gap = 4;
  lk_theme_add_rule(th, UIK_TAB, 0, 0, &s,
                    LK_SF_BG | LK_SF_PADDING | LK_SF_GAP);

  /* GRID -- container: gap between cells */
  memset(&s, 0, sizeof(s));
  s.padding = 0;
  s.gap = 4;
  lk_theme_add_rule(th, UIK_GRID, 0, 0, &s, LK_SF_PADDING | LK_SF_GAP);

  /* IMAGE -- dark plate behind the pixels: shows through transparent
   * regions and IS the degraded rendering for a missing/stale ref.
   * Padding 0 keeps the blit aligned with the node rect. */
  memset(&s, 0, sizeof(s));
  s.bg = mk_color(20, 20, 22, 255);
  s.padding = 0;
  lk_theme_add_rule(th, UIK_IMAGE, 0, 0, &s, LK_SF_BG | LK_SF_PADDING);

  /* CANVAS -- a slightly lifted plate under the display list (also
   * the degrade for a dead ref); padding 0 so canvas-local (0, 0) is
   * the node's top-left. */
  memset(&s, 0, sizeof(s));
  s.bg = mk_color(24, 26, 30, 255);
  s.padding = 0;
  lk_theme_add_rule(th, UIK_CANVAS, 0, 0, &s, LK_SF_BG | LK_SF_PADDING);

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

  if (fm & LK_SF_TEXT_ALIGN) {
    dst->text_align = rule->style.text_align;
  }

  if (fm & LK_SF_TEXT_VALIGN) {
    dst->text_valign = rule->style.text_valign;
  }

  if (fm & LK_SF_SCROLLBAR_TRACK) {
    dst->scrollbar_track = rule->style.scrollbar_track;
  }

  if (fm & LK_SF_SCROLLBAR_THUMB) {
    dst->scrollbar_thumb = rule->style.scrollbar_thumb;
  }

  if (fm & LK_SF_ACCENT) {
    dst->accent = rule->style.accent;
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
    lk_u32 sp_start, lo, hi;

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

    /* Tree prop overrides -- presence-gated (lk_node_has_prop), so an
     * absent prop never clobbers a theme-sourced value and an explicit
     * zero (e.g. align start) still beats the theme.  Resolution order:
     * explicit prop > theme rule > engine fallback (zeroed style). */
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

    if (lk_node_has_prop(t, n, UIP_TEXT_ALIGN)) {
      styles[n].text_align =
          (lk_u8)lk_node_prop_i32(t, n, UIP_TEXT_ALIGN, 0);
      set_mask |= LK_SF_TEXT_ALIGN;
    }

    if (lk_node_has_prop(t, n, UIP_TEXT_VALIGN)) {
      styles[n].text_valign =
          (lk_u8)lk_node_prop_i32(t, n, UIP_TEXT_VALIGN, 0);
      set_mask |= LK_SF_TEXT_VALIGN;
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

    /* Push children forward, then reverse segment for correct DFS order */
    sp_start = sp;
    child = nd->first_child;

    while (child) {
      stack[sp++] = child;
      child = t->nodes[child].next_sibling;
    }

    if (sp > sp_start) {
      lo = sp_start;
      hi = sp - 1;

      while (lo < hi) {
        lk_ix tmp = stack[lo];
        stack[lo] = stack[hi];
        stack[hi] = tmp;
        lo++;
        hi--;
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
