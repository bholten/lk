#include <string.h>

#include "lk-check.h"
#include "lk-dropdown.h"
#include "lk-grid.h"
#include "lk-image.h"
#include "lk-canvas.h"
#include "lk-memory.h"
#include "lk-list.h"
#include "lk-styled-text.h"
#include "lk-text-align.h"
#include "lk-scroll.h"
#include "lk-slider.h"
#include "lk-split.h"
#include "lk-tabs.h"
#include "lk-text-input.h"
#include <lk-editor.h>
#include <lk.h>

/* ---- Registry ---- */

static lk_widget_def g_widgets[LK_KIND_MAX];
static int g_inited;

static void init_defaults(void);

void lk_widget_register(lk_kind kind, const lk_widget_def *def) {
  if ((int)kind < 0 || (int)kind >= LK_KIND_MAX || !def) {
    return;
  }

  if (!g_inited) {
    init_defaults();
  }

  g_widgets[(int)kind] = *def;
}

const lk_widget_def *lk_widget_get(lk_kind kind) {
  if (!g_inited) {
    init_defaults();
  }

  if ((int)kind < 0 || (int)kind >= LK_KIND_MAX) {
    return 0;
  }

  return &g_widgets[(int)kind];
}

/* ---- Measure functions ---- */

static void measure_window(const lk_tree *t, lk_ix n, const lk_size *sizes,
                           const lk_layout_cfg *cfg, lk_i32 *out_w,
                           lk_i32 *out_h) {
  (void)t;
  (void)n;
  (void)sizes;
  (void)cfg;
  *out_w = 0;
  *out_h = 0;
}

static void measure_column(const lk_tree *t, lk_ix n, const lk_size *sizes,
                           const lk_layout_cfg *cfg, lk_i32 *out_w,
                           lk_i32 *out_h) {
  const lk_node *nd = &t->nodes[n];
  lk_i32 pad = cfg->styles ? cfg->styles[n].padding
                           : lk_node_prop_i32(t, n, UIP_PADDING, 0);
  lk_i32 bw = cfg->styles ? cfg->styles[n].border_width : 0;
  lk_i32 inset = pad + bw;
  lk_i32 gap =
      cfg->styles ? cfg->styles[n].gap : lk_node_prop_i32(t, n, UIP_GAP, 0);
  lk_ix ch = nd->first_child;
  lk_i32 max_w = 0;
  lk_i32 sum_h = 0;
  int count = 0;

  while (ch) {
    if (!lk_node_prop_bool(t, ch, UIP_HIDDEN)) {
      if (sizes[ch].w > max_w) {
        max_w = sizes[ch].w;
      }

      sum_h += sizes[ch].h;
      count++;
    }

    ch = t->nodes[ch].next_sibling;
  }

  if (count > 1) {
    sum_h += gap * (count - 1);
  }

  *out_w = max_w + inset * 2;
  *out_h = sum_h + inset * 2;
}

static void measure_row(const lk_tree *t, lk_ix n, const lk_size *sizes,
                        const lk_layout_cfg *cfg, lk_i32 *out_w,
                        lk_i32 *out_h) {
  const lk_node *nd = &t->nodes[n];
  lk_i32 pad = cfg->styles ? cfg->styles[n].padding
                           : lk_node_prop_i32(t, n, UIP_PADDING, 0);
  lk_i32 bw = cfg->styles ? cfg->styles[n].border_width : 0;
  lk_i32 inset = pad + bw;
  lk_i32 gap =
      cfg->styles ? cfg->styles[n].gap : lk_node_prop_i32(t, n, UIP_GAP, 0);
  lk_ix ch = nd->first_child;
  lk_i32 sum_w = 0;
  lk_i32 max_h = 0;
  int count = 0;

  while (ch) {
    if (!lk_node_prop_bool(t, ch, UIP_HIDDEN)) {
      sum_w += sizes[ch].w;

      if (sizes[ch].h > max_h) {
        max_h = sizes[ch].h;
      }

      count++;
    }

    ch = t->nodes[ch].next_sibling;
  }

  if (count > 1) {
    sum_w += gap * (count - 1);
  }

  *out_w = sum_w + inset * 2;
  *out_h = max_h + inset * 2;
}

static void measure_spacer(const lk_tree *t, lk_ix n, const lk_size *sizes,
                           const lk_layout_cfg *cfg, lk_i32 *out_w,
                           lk_i32 *out_h) {
  (void)sizes;
  (void)cfg;
  *out_w = lk_node_prop_i32(t, n, UIP_W, 0);
  *out_h = lk_node_prop_i32(t, n, UIP_H, 0);
}

/* Measure a text run through cfg->text with the node's resolved font
 * (0/0 defaults when no styles).  Zero metrics when no backend. */
static void measure_run(const lk_layout_cfg *cfg, lk_ix n, lk_str run,
                        lk_i32 *out_w, lk_i32 *out_h) {
  lk_text_metrics m;

  m.w = 0;
  m.h = 0;
  m.baseline = 0;

  if (cfg->text) {
    lk_u16 font_id = cfg->styles ? (lk_u16)cfg->styles[n].font_id : 0;
    lk_u16 font_size = cfg->styles ? (lk_u16)cfg->styles[n].font_size : 0;
    cfg->text->measure(cfg->text->ud, run, font_id, font_size, &m);
  }

  *out_w = m.w;
  *out_h = m.h;
}

/* Stash the measured run size for render's text_align placement
 * (render has no text backend).  NULL geom: text draws at START. */
static void stash_run(const lk_layout_cfg *cfg, lk_ix n, lk_i32 tw,
                      lk_i32 th) {
  if (cfg->geom) {
    cfg->geom[n].run.w = tw;
    cfg->geom[n].run.h = th;
  }
}

lk_i32 lk_text_align_offset(lk_u8 align, lk_i32 avail, lk_i32 extent) {
  lk_i32 slack = avail - extent;

  if (slack <= 0) {
    return 0;
  }

  if (align == (lk_u8)LK_ALIGN_CENTER) {
    return slack / 2;
  }

  if (align == (lk_u8)LK_ALIGN_END) {
    return slack;
  }

  return 0;
}

static void measure_label(const lk_tree *t, lk_ix n, const lk_size *sizes,
                          const lk_layout_cfg *cfg, lk_i32 *out_w,
                          lk_i32 *out_h) {
  lk_str text = lk_node_text(t, n);
  lk_i32 tw = 0;
  lk_i32 th = 0;

  (void)sizes;
  measure_run(cfg, n, text, &tw, &th);
  stash_run(cfg, n, tw, th);
  *out_w = tw;
  *out_h = th;
}

static void measure_button(const lk_tree *t, lk_ix n, const lk_size *sizes,
                           const lk_layout_cfg *cfg, lk_i32 *out_w,
                           lk_i32 *out_h) {
  lk_str text = lk_node_text(t, n);
  lk_i32 pad = cfg->styles ? cfg->styles[n].padding
                           : lk_node_prop_i32(t, n, UIP_PADDING, 0);
  lk_i32 bw = cfg->styles ? cfg->styles[n].border_width : 0;
  lk_i32 inset = pad + bw;
  lk_i32 tw = 0;
  lk_i32 th = 0;

  (void)sizes;
  measure_run(cfg, n, text, &tw, &th);
  stash_run(cfg, n, tw, th);
  *out_w = tw + inset * 2;
  *out_h = th + inset * 2;
}

/* ---- Layout functions ---- */

static int layout_window(const lk_tree *t, lk_ix n, const lk_size *sizes,
                         const lk_rect *content, const lk_layout_cfg *cfg,
                         lk_rect *rects) {
  const lk_node *nd = &t->nodes[n];
  lk_ix child = nd->first_child;

  (void)sizes;
  (void)cfg;

  while (child) {
    if (!lk_node_prop_bool(t, child, UIP_HIDDEN)) {
      rects[child].x = content->x;
      rects[child].y = content->y;
      rects[child].w = content->w;
      rects[child].h = content->h;
    }

    child = t->nodes[child].next_sibling;
  }

  return 1;
}

/* Growth weight cap (docs/grow-layout.md section 1): keeps
 * leftover * weight inside lk_i32 without wide integers. */
#define GROW_WEIGHT_MAX 4096

/* Effective growth weight of one stack child.  An explicit UIP_GROW
 * wins -- presence-checked, so grow 0 genuinely pins a spacer --
 * clamped to [0, GROW_WEIGHT_MAX] (the core has no assert facility;
 * negatives clamp silently, the bindings hard-error).  An unsized
 * SPACER keeps its legacy weight of 1; everything else is fixed. */
static lk_i32 grow_weight(const lk_tree *t, lk_ix child,
                          lk_prop_key main_key) {
  if (lk_node_has_prop(t, child, UIP_GROW)) {
    lk_i32 w = lk_node_prop_i32(t, child, UIP_GROW, 0);

    if (w < 0) {
      w = 0;
    }

    if (w > GROW_WEIGHT_MAX) {
      w = GROW_WEIGHT_MAX;
    }

    return w;
  }

  if ((lk_kind)t->nodes[child].kind == UIK_SPACER &&
      !lk_node_has_prop(t, child, main_key)) {
    return 1;
  }

  return 0;
}

/* Shared column/row layout parameterized by axis.
 * axis=0 -> column (main=vertical, cross=horizontal)
 * axis=1 -> row    (main=horizontal, cross=vertical)
 *
 * Leftover main-axis space (final extent minus bases minus visible
 * gaps, never negative -- intrinsic is the floor, no shrink) is
 * apportioned by grow weight, largest-remainder, ties to earlier
 * children.  Equal weights reproduce the legacy spacer share-out
 * exactly.  Growth is layout-only: measurement never sees it. */
/* Height-for-width of a stack (docs/styled-text.md section 2).
 * COLUMN: sum of the children's fits at their cross width -- the inner
 * width under stretch, else min(measured, inner), an explicit UIP_W
 * standing -- plus gaps.  ROW: the max of the children's fits at
 * their measured width (growth ignored for the estimate). */
static lk_i32 fit_stack(const lk_tree *t, lk_ix n, lk_i32 width,
                        const lk_size *sizes, const lk_layout_cfg *cfg,
                        int axis) {
  const lk_node *nd = &t->nodes[n];
  lk_i32 pad = cfg->styles ? cfg->styles[n].padding
                           : lk_node_prop_i32(t, n, UIP_PADDING, 0);
  lk_i32 bw = cfg->styles ? cfg->styles[n].border_width : 0;
  lk_i32 gap =
      cfg->styles ? cfg->styles[n].gap : lk_node_prop_i32(t, n, UIP_GAP, 0);
  lk_i32 align = cfg->styles
                     ? (lk_i32)cfg->styles[n].align
                     : lk_node_prop_i32(t, n, UIP_ALIGN, LK_ALIGN_STRETCH);
  lk_i32 inset = pad + bw;
  lk_i32 inner = width - inset * 2;
  lk_i32 total = 0;
  int count = 0;
  lk_ix child = nd->first_child;

  while (child) {
    if (!lk_node_prop_bool(t, child, UIP_HIDDEN)) {
      lk_i32 cw;
      lk_i32 ch;

      if (axis == 0) {
        if (lk_node_has_prop(t, child, UIP_W)) {
          cw = sizes[child].w;
        } else if (align == LK_ALIGN_STRETCH) {
          cw = inner;
        } else {
          cw = sizes[child].w < inner ? sizes[child].w : inner;
        }
      } else {
        cw = sizes[child].w;
      }

      ch = lk_widget_fit_height(t, child, cw, sizes, cfg);

      if (axis == 0) {
        total += ch;
      } else if (ch > total) {
        total = ch;
      }

      count++;
    }

    child = t->nodes[child].next_sibling;
  }

  if (axis == 0 && count > 1) {
    total += gap * (count - 1);
  }

  return total + inset * 2;
}

static lk_i32 fit_column(const lk_tree *t, lk_ix n, lk_i32 width,
                         const lk_size *sizes, const lk_layout_cfg *cfg) {
  return fit_stack(t, n, width, sizes, cfg, 0);
}

static lk_i32 fit_row(const lk_tree *t, lk_ix n, lk_i32 width,
                      const lk_size *sizes, const lk_layout_cfg *cfg) {
  return fit_stack(t, n, width, sizes, cfg, 1);
}

static int layout_stack(const lk_tree *t, lk_ix n, const lk_size *sizes,
                        const lk_rect *content, const lk_layout_cfg *cfg,
                        lk_rect *rects, int axis) {
  const lk_node *nd = &t->nodes[n];
  lk_i32 gap =
      cfg->styles ? cfg->styles[n].gap : lk_node_prop_i32(t, n, UIP_GAP, 0);
  lk_i32 align = cfg->styles
                     ? (lk_i32)cfg->styles[n].align
                     : lk_node_prop_i32(t, n, UIP_ALIGN, LK_ALIGN_STRETCH);
  lk_i32 justify = cfg->styles
                       ? (lk_i32)cfg->styles[n].justify
                       : lk_node_prop_i32(t, n, UIP_JUSTIFY, LK_ALIGN_START);
  /* main_size = content extent along main axis
   * cross_size = content extent along cross axis */
  lk_i32 main_size = axis ? content->w : content->h;
  lk_i32 cross_size = axis ? content->h : content->w;
  lk_i32 main_origin = axis ? content->x : content->y;
  lk_i32 cross_origin = axis ? content->y : content->x;
  /* main-axis explicit-size prop (its absence keeps a spacer flexible) */
  lk_prop_key main_key = axis ? UIP_W : UIP_H;
  /* cross-axis fixed-size prop */
  lk_prop_key cross_key = axis ? UIP_H : UIP_W;

  lk_i32 leftover = main_size;
  lk_i32 total_weight = 0;
  lk_i32 residual = 0;
  int child_count = 0;
  lk_i32 pos;
  lk_ix child;
  /* Column children's bases are height-for-width at their cross
   * extent (docs/styled-text.md section 2), computed once here;
   * rows keep the measured width as the base. */
  lk_i32 *bases = NULL;
  int bi;

  child = nd->first_child;

  while (child) {
    if (lk_node_prop_bool(t, child, UIP_HIDDEN)) {
      child = t->nodes[child].next_sibling;
      continue;
    }

    child_count++;
    child = t->nodes[child].next_sibling;
  }

  if (axis == 0 && child_count > 0) {
    bases = (lk_i32 *)lk_sys_alloc(NULL,
                                   (lk_u32)(sizeof(lk_i32) * child_count));
  }

  bi = 0;
  child = nd->first_child;

  while (child) {
    lk_i32 base;

    if (lk_node_prop_bool(t, child, UIP_HIDDEN)) {
      child = t->nodes[child].next_sibling;
      continue;
    }

    if (axis) {
      base = sizes[child].w;
    } else {
      lk_i32 cw;

      if (lk_node_has_prop(t, child, cross_key) || align != LK_ALIGN_STRETCH) {
        cw = sizes[child].w < cross_size ? sizes[child].w : cross_size;

        if (lk_node_has_prop(t, child, cross_key)) {
          cw = sizes[child].w;
        }
      } else {
        cw = cross_size;
      }

      base = lk_widget_fit_height(t, child, cw, sizes, cfg);

      if (bases) {
        bases[bi] = base;
      }
    }

    bi++;
    leftover -= base;
    total_weight += grow_weight(t, child, main_key);

    child = t->nodes[child].next_sibling;
  }

  if (child_count > 1) {
    leftover -= gap * (child_count - 1);
  }

  if (leftover < 0) {
    leftover = 0;
  }

  /* Largest-remainder residual: pixels the floored shares leave
   * over (always < the number of weighted children). */
  if (total_weight > 0 && leftover > 0) {
    residual = leftover;
    child = nd->first_child;

    while (child) {
      if (!lk_node_prop_bool(t, child, UIP_HIDDEN)) {
        residual -= leftover * grow_weight(t, child, main_key) / total_weight;
      }

      child = t->nodes[child].next_sibling;
    }
  }

  /* Justify shifts the run only when nothing grows */
  pos = main_origin;
  if (total_weight == 0 && leftover > 0) {
    if (justify == LK_ALIGN_CENTER) {
      pos = main_origin + leftover / 2;
    } else if (justify == LK_ALIGN_END) {
      pos = main_origin + leftover;
    }
  }

  bi = 0;
  child = nd->first_child;

  while (child) {
    lk_i32 child_main;
    lk_i32 child_cross;
    lk_i32 child_cross_pos;

    if (lk_node_prop_bool(t, child, UIP_HIDDEN)) {
      child = t->nodes[child].next_sibling;
      continue;
    }

    if (axis) {
      child_main = sizes[child].w;
    } else if (bases) {
      child_main = bases[bi];
    } else {
      child_main = sizes[child].h;
    }

    bi++;

    if (leftover > 0) {
      lk_i32 wgt = grow_weight(t, child, main_key);

      if (wgt > 0) {
        /* Rank this child's remainder among its weighted siblings
         * (earlier child wins ties); the first `residual` ranks get
         * one extra pixel.  O(n^2) but sort-free and exact. */
        lk_i32 rem = leftover * wgt % total_weight;
        lk_i32 rank = 0;
        int before = 1;
        lk_ix sib = nd->first_child;

        while (sib) {
          if (sib == child) {
            before = 0;
          } else if (!lk_node_prop_bool(t, sib, UIP_HIDDEN)) {
            lk_i32 swgt = grow_weight(t, sib, main_key);

            if (swgt > 0) {
              lk_i32 srem = leftover * swgt % total_weight;

              if (srem > rem || (srem == rem && before)) {
                rank++;
              }
            }
          }

          sib = t->nodes[sib].next_sibling;
        }

        child_main +=
            leftover * wgt / total_weight + (rank < residual ? 1 : 0);
      }
    }

    if (lk_node_has_prop(t, child, cross_key)) {
      child_cross = axis ? sizes[child].h : sizes[child].w;
    } else if (align == LK_ALIGN_STRETCH) {
      child_cross = cross_size;
    } else if (axis) {
      /* a row child's height is height-for-width at its main extent */
      child_cross = lk_widget_fit_height(t, child, child_main, sizes, cfg);
    } else {
      child_cross = sizes[child].w;
    }

    /* Cross-axis alignment */
    child_cross_pos = cross_origin;
    if (!lk_node_has_prop(t, child, cross_key) && align != LK_ALIGN_STRETCH) {
      if (align == LK_ALIGN_CENTER) {
        child_cross_pos = cross_origin + (cross_size - child_cross) / 2;
      } else if (align == LK_ALIGN_END) {
        child_cross_pos = cross_origin + cross_size - child_cross;
      }
    }

    if (axis) {
      /* row: main = x, cross = y */
      rects[child].x = pos;
      rects[child].y = child_cross_pos;
      rects[child].w = child_main;
      rects[child].h = child_cross;
    } else {
      /* column: main = y, cross = x */
      rects[child].x = child_cross_pos;
      rects[child].y = pos;
      rects[child].w = child_cross;
      rects[child].h = child_main;
    }

    pos += child_main + gap;

    child = t->nodes[child].next_sibling;
  }


  if (bases) {
    lk_sys_dealloc(NULL, bases);
  }
  return 1;
}

static int layout_column(const lk_tree *t, lk_ix n, const lk_size *sizes,
                         const lk_rect *content, const lk_layout_cfg *cfg,
                         lk_rect *rects) {
  return layout_stack(t, n, sizes, content, cfg, rects, 0);
}

static int layout_row(const lk_tree *t, lk_ix n, const lk_size *sizes,
                      const lk_rect *content, const lk_layout_cfg *cfg,
                      lk_rect *rects) {
  return layout_stack(t, n, sizes, content, cfg, rects, 1);
}

/* ---- Render functions ---- */

static void render_window(const lk_tree *t, lk_ix n, const lk_rect *rect,
                          const lk_style *style, const lk_state *state,
                          const lk_widget_geom *geom, lk_render_list *out) {
  lk_render_cmd cmd;

  (void)t;
  (void)n;
  (void)state;
  (void)geom;
  memset(&cmd, 0, sizeof(cmd));
  cmd.op = LK_ROP_FILL_RECT;
  cmd.rect = *rect;
  cmd.color = style->bg;
  lk_render_list_push(out, cmd);
}

static void render_label(const lk_tree *t, lk_ix n, const lk_rect *rect,
                         const lk_style *style, const lk_state *state,
                         const lk_widget_geom *geom, lk_render_list *out) {
  lk_u32 sid = lk_node_text_id(t, n);
  (void)state;

  if (sid != 0) {
    lk_render_cmd cmd;
    lk_i32 ox = 0;
    lk_i32 oy = 0;

    if (geom) {
      ox = lk_text_align_offset(style->text_align, rect->w, geom->run.w);
      oy = lk_text_align_offset(style->text_valign, rect->h, geom->run.h);
    }

    memset(&cmd, 0, sizeof(cmd));
    cmd.op = LK_ROP_DRAW_TEXT;
    cmd.rect.x = rect->x + ox;
    cmd.rect.y = rect->y + oy;
    cmd.rect.w = rect->w - ox;
    cmd.rect.h = rect->h - oy;
    cmd.color = style->fg;
    cmd.str_id = sid;
    cmd.font_id = (lk_u16)style->font_id;
    cmd.font_size = (lk_u16)style->font_size;
    lk_render_list_push(out, cmd);
  }
}

static void render_button(const lk_tree *t, lk_ix n, const lk_rect *rect,
                          const lk_style *style, const lk_state *state,
                          const lk_widget_geom *geom, lk_render_list *out) {
  lk_i32 pad = style->padding;
  lk_i32 bw = style->border_width;
  lk_i32 inset = pad + bw;
  lk_u32 sid = lk_node_text_id(t, n);
  lk_render_cmd cmd;
  (void)state;

  memset(&cmd, 0, sizeof(cmd));
  cmd.op = LK_ROP_FILL_RECT;
  cmd.rect = *rect;
  cmd.color = style->bg;
  lk_render_list_push(out, cmd);

  if (sid != 0) {
    lk_i32 ox = 0;
    lk_i32 oy = 0;

    if (geom) {
      ox = lk_text_align_offset(style->text_align, rect->w - inset * 2,
                                geom->run.w);
      oy = lk_text_align_offset(style->text_valign, rect->h - inset * 2,
                                geom->run.h);
    }

    memset(&cmd, 0, sizeof(cmd));
    cmd.op = LK_ROP_DRAW_TEXT;
    cmd.rect.x = rect->x + inset + ox;
    cmd.rect.y = rect->y + inset + oy;
    cmd.rect.w = rect->w - inset * 2 - ox;
    cmd.rect.h = rect->h - inset * 2 - oy;
    cmd.color = style->fg;
    cmd.str_id = sid;
    cmd.font_id = (lk_u16)style->font_id;
    cmd.font_size = (lk_u16)style->font_size;
    lk_render_list_push(out, cmd);
  }
}

/* ---- Default registration ---- */

static void init_defaults(void) {
  lk_widget_def def;

  if (g_inited) {
    return;
  }

  g_inited = 1;
  memset(g_widgets, 0, sizeof(g_widgets));

  /* WINDOW */
  memset(&def, 0, sizeof(def));
  def.measure = measure_window;
  def.layout = layout_window;
  def.render = render_window;
  def.clips = 1;
  g_widgets[UIK_WINDOW] = def;

  /* COLUMN */
  memset(&def, 0, sizeof(def));
  def.measure = measure_column;
  def.layout = layout_column;
  def.render = 0;
  def.clips = 0;
  def.fit_height = fit_column;
  g_widgets[UIK_COLUMN] = def;

  /* ROW */
  memset(&def, 0, sizeof(def));
  def.measure = measure_row;
  def.layout = layout_row;
  def.render = 0;
  def.clips = 0;
  def.fit_height = fit_row;
  g_widgets[UIK_ROW] = def;

  /* SPACER */
  memset(&def, 0, sizeof(def));
  def.measure = measure_spacer;
  def.layout = 0;
  def.render = 0;
  def.clips = 0;
  g_widgets[UIK_SPACER] = def;

  /* LABEL */
  memset(&def, 0, sizeof(def));
  def.measure = measure_label;
  def.layout = 0;
  def.render = render_label;
  def.clips = 0;
  g_widgets[UIK_LABEL] = def;

  /* BUTTON */
  memset(&def, 0, sizeof(def));
  def.measure = measure_button;
  def.layout = 0;
  def.render = render_button;
  def.clips = 0;
  g_widgets[UIK_BUTTON] = def;

  /* TEXT_INPUT */
  g_widgets[UIK_TEXT_INPUT] = lk_text_input_widget_def();

  /* SCROLL */
  g_widgets[UIK_SCROLL] = lk_scroll_widget_def();

  /* DROPDOWN */
  g_widgets[UIK_DROPDOWN] = lk_dropdown_widget_def();

  /* OPTION */
  g_widgets[UIK_OPTION] = lk_option_widget_def();

  /* SPLIT_H / SPLIT_V */
  g_widgets[UIK_SPLIT_H] = lk_split_h_widget_def();
  g_widgets[UIK_SPLIT_V] = lk_split_v_widget_def();

  /* EDITOR (src/editor/lk-editor-widget.c) */
  g_widgets[UIK_EDITOR] = *lk_editor_widget();

  /* CHECKBOX / RADIO (lk-check.c) */
  g_widgets[UIK_CHECKBOX] = lk_checkbox_widget_def();
  g_widgets[UIK_RADIO] = lk_radio_widget_def();

  /* SLIDER (lk-slider.c) */
  g_widgets[UIK_SLIDER] = lk_slider_widget_def();

  /* TABS / TAB (lk-tabs.c) -- a TAB page measures and lays out its
   * children like a column; lk-tabs.c supplies render + event. */
  g_widgets[UIK_TABS] = lk_tabs_widget_def();
  def = lk_tab_widget_def();
  def.measure = measure_column;
  def.layout = layout_column;
  g_widgets[UIK_TAB] = def;

  /* GRID (lk-grid.c) */
  g_widgets[UIK_GRID] = lk_grid_widget_def();

  /* IMAGE (lk-image.c) */
  g_widgets[UIK_IMAGE] = lk_image_widget_def();

  /* CANVAS (lk-canvas.c) */
  g_widgets[UIK_CANVAS] = lk_canvas_widget_def();
  g_widgets[UIK_STYLED_TEXT] = lk_styled_text_widget_def();
  g_widgets[UIK_LIST] = lk_list_widget_def();
}
