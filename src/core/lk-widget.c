#include <string.h>

#include "lk-dropdown.h"
#include "lk-memory.h"
#include "lk-scroll.h"
#include "lk-text-input.h"
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

static void measure_label(const lk_tree *t, lk_ix n, const lk_size *sizes,
                          const lk_layout_cfg *cfg, lk_i32 *out_w,
                          lk_i32 *out_h) {
  lk_str text = lk_node_text(t, n);
  lk_i32 tw = 0;
  lk_i32 th = 0;

  (void)sizes;
  measure_run(cfg, n, text, &tw, &th);
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

/* Shared column/row layout parameterized by axis.
 * axis=0 -> column (main=vertical, cross=horizontal)
 * axis=1 -> row    (main=horizontal, cross=vertical)
 */
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
  /* flex prop that indicates no fixed size along main axis */
  lk_prop_key flex_key = axis ? UIP_W : UIP_H;
  /* cross-axis fixed-size prop */
  lk_prop_key cross_key = axis ? UIP_H : UIP_W;

  lk_i32 remaining = main_size;
  int spacer_count = 0;
  int child_count = 0;
  lk_i32 spacer_each;
  lk_i32 spacer_extra;
  lk_i32 pos;
  int spacer_idx;
  lk_ix child;

  child = nd->first_child;

  while (child) {
    lk_kind ck = (lk_kind)t->nodes[child].kind;
    int is_flex_spacer =
        (ck == UIK_SPACER && !lk_node_has_prop(t, child, flex_key));

    if (lk_node_prop_bool(t, child, UIP_HIDDEN)) {
      child = t->nodes[child].next_sibling;
      continue;
    }

    child_count++;

    if (is_flex_spacer) {
      spacer_count++;
    } else {
      remaining -= axis ? sizes[child].w : sizes[child].h;
    }

    child = t->nodes[child].next_sibling;
  }

  if (child_count > 1) {
    remaining -= gap * (child_count - 1);
  }

  if (remaining < 0) {
    remaining = 0;
  }

  if (spacer_count > 0) {
    spacer_each = remaining / spacer_count;
    spacer_extra = remaining % spacer_count;
  } else {
    spacer_each = 0;
    spacer_extra = 0;
  }

  /* Compute justify offset for main axis */
  pos = main_origin;
  if (spacer_count == 0 && remaining > 0) {
    if (justify == LK_ALIGN_CENTER) {
      pos = main_origin + remaining / 2;
    } else if (justify == LK_ALIGN_END) {
      pos = main_origin + remaining;
    }
  }

  spacer_idx = 0;
  child = nd->first_child;

  while (child) {
    lk_kind ck = (lk_kind)t->nodes[child].kind;
    int is_flex_spacer =
        (ck == UIK_SPACER && !lk_node_has_prop(t, child, flex_key));
    lk_i32 child_main;
    lk_i32 child_cross;
    lk_i32 child_cross_pos;

    if (lk_node_prop_bool(t, child, UIP_HIDDEN)) {
      child = t->nodes[child].next_sibling;
      continue;
    }

    if (is_flex_spacer) {
      child_main = spacer_each + (spacer_idx < spacer_extra ? 1 : 0);
      spacer_idx++;
    } else {
      child_main = axis ? sizes[child].w : sizes[child].h;
    }

    if (lk_node_has_prop(t, child, cross_key)) {
      child_cross = axis ? sizes[child].h : sizes[child].w;
    } else if (align == LK_ALIGN_STRETCH) {
      child_cross = cross_size;
    } else {
      child_cross = axis ? sizes[child].h : sizes[child].w;
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
                          lk_render_list *out) {
  lk_render_cmd cmd;

  (void)t;
  (void)n;
  (void)state;
  memset(&cmd, 0, sizeof(cmd));
  cmd.op = LK_ROP_FILL_RECT;
  cmd.rect = *rect;
  cmd.color = style->bg;
  lk_render_list_push(out, cmd);
}

static void render_label(const lk_tree *t, lk_ix n, const lk_rect *rect,
                         const lk_style *style, const lk_state *state,
                         lk_render_list *out) {
  lk_u32 sid = lk_node_text_id(t, n);
  (void)state;

  if (sid != 0) {
    lk_render_cmd cmd;
    memset(&cmd, 0, sizeof(cmd));
    cmd.op = LK_ROP_DRAW_TEXT;
    cmd.rect = *rect;
    cmd.color = style->fg;
    cmd.str_id = sid;
    cmd.font_id = (lk_u16)style->font_id;
    cmd.font_size = (lk_u16)style->font_size;
    lk_render_list_push(out, cmd);
  }
}

static void render_button(const lk_tree *t, lk_ix n, const lk_rect *rect,
                          const lk_style *style, const lk_state *state,
                          lk_render_list *out) {
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
    memset(&cmd, 0, sizeof(cmd));
    cmd.op = LK_ROP_DRAW_TEXT;
    cmd.rect.x = rect->x + inset;
    cmd.rect.y = rect->y + inset;
    cmd.rect.w = rect->w - inset * 2;
    cmd.rect.h = rect->h - inset * 2;
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
  g_widgets[UIK_COLUMN] = def;

  /* ROW */
  memset(&def, 0, sizeof(def));
  def.measure = measure_row;
  def.layout = layout_row;
  def.render = 0;
  def.clips = 0;
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
}
