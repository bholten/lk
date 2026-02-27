/*
 * lk-grid.c -- UIK_GRID (docs/forms-widgets.md).
 *
 * Row-major cell grid: visible (non-UIP_HIDDEN) children fill cells
 * left-to-right, top-to-bottom, UIP_COLUMNS (default 1, clamped >= 1)
 * per row.  Every column is as wide as its widest cell, every row as
 * tall as its tallest -- the Tk `grid` feel without spans or weights
 * (v1).  Cells are separated by style->gap on both axes; the whole
 * thing is inset by padding + border_width.  Each child fills its
 * cell (stretch on both axes).  Extra space the parent hands the grid
 * beyond its measured size is left unused (the grid stays top-left
 * anchored inside its rect).
 *
 * Column/row extents are recomputed in layout from the measured child
 * sizes -- the same arithmetic as measure -- so no scratch survives
 * between the passes.  Render-less: the engine paints bg + border.
 */

#include <string.h>

#include "lk-grid.h"
#include "lk-memory.h"
#include <lk.h>

static int grid_columns(const lk_tree *t, lk_ix n) {
  lk_i32 c = lk_node_prop_i32(t, n, UIP_COLUMNS, 1);

  return c < 1 ? 1 : (int)c;
}

/* Fill col_w[cols] / row_h[rows] with the max cell extents; returns the
 * number of rows used.  Either array may be NULL when only the row
 * count is wanted (col_w must be sized cols, row_h at least
 * ceil(children / cols)). */
static int grid_extents(const lk_tree *t, lk_ix n, const lk_size *sizes,
                        int cols, lk_i32 *col_w, lk_i32 *row_h) {
  lk_ix c = t->nodes[n].first_child;
  int i = 0;

  while (c) {
    if (!lk_node_prop_bool(t, c, UIP_HIDDEN)) {
      int col = i % cols;
      int row = i / cols;

      if (col_w && sizes[c].w > col_w[col]) {
        col_w[col] = sizes[c].w;
      }

      if (row_h && sizes[c].h > row_h[row]) {
        row_h[row] = sizes[c].h;
      }

      i++;
    }

    c = t->nodes[c].next_sibling;
  }

  return (i + cols - 1) / cols;
}

static int visible_count(const lk_tree *t, lk_ix n) {
  lk_ix c = t->nodes[n].first_child;
  int i = 0;

  while (c) {
    if (!lk_node_prop_bool(t, c, UIP_HIDDEN)) {
      i++;
    }

    c = t->nodes[c].next_sibling;
  }

  return i;
}

static void measure_grid(const lk_tree *t, lk_ix n, const lk_size *sizes,
                         const lk_layout_cfg *cfg, lk_i32 *out_w,
                         lk_i32 *out_h) {
  lk_i32 pad = cfg->styles ? cfg->styles[n].padding
                           : lk_node_prop_i32(t, n, UIP_PADDING, 0);
  lk_i32 bw = cfg->styles ? cfg->styles[n].border_width : 0;
  lk_i32 gap =
      cfg->styles ? cfg->styles[n].gap : lk_node_prop_i32(t, n, UIP_GAP, 0);
  lk_i32 inset = pad + bw;
  int cols = grid_columns(t, n);
  int count = visible_count(t, n);
  int rows = (count + cols - 1) / cols;
  lk_i32 *col_w;
  lk_i32 *row_h;
  lk_i32 w = 0;
  lk_i32 h = 0;
  int i;

  if (count == 0) {
    *out_w = inset * 2;
    *out_h = inset * 2;
    return;
  }

  col_w = (lk_i32 *)lk_sys_alloc(NULL, (lk_u32)(sizeof(lk_i32) * (lk_u32)cols));
  row_h = (lk_i32 *)lk_sys_alloc(NULL, (lk_u32)(sizeof(lk_i32) * (lk_u32)rows));

  if (!col_w || !row_h) {
    if (col_w) {
      lk_sys_dealloc(NULL, col_w);
    }

    if (row_h) {
      lk_sys_dealloc(NULL, row_h);
    }

    *out_w = inset * 2;
    *out_h = inset * 2;
    return;
  }

  memset(col_w, 0, sizeof(lk_i32) * (lk_u32)cols);
  memset(row_h, 0, sizeof(lk_i32) * (lk_u32)rows);
  grid_extents(t, n, sizes, cols, col_w, row_h);

  for (i = 0; i < cols; i++) {
    w += col_w[i];
  }

  for (i = 0; i < rows; i++) {
    h += row_h[i];
  }

  /* only columns that actually hold a cell get a gap: with fewer
   * children than columns the trailing empty columns are zero-width
   * and their gaps would still show, so count used columns */
  {
    int used_cols = count < cols ? count : cols;

    if (used_cols > 1) {
      w += gap * (used_cols - 1);
    }
  }

  if (rows > 1) {
    h += gap * (rows - 1);
  }

  lk_sys_dealloc(NULL, col_w);
  lk_sys_dealloc(NULL, row_h);

  *out_w = w + inset * 2;
  *out_h = h + inset * 2;
}

static int layout_grid(const lk_tree *t, lk_ix n, const lk_size *sizes,
                       const lk_rect *content, const lk_layout_cfg *cfg,
                       lk_rect *rects) {
  lk_i32 gap =
      cfg->styles ? cfg->styles[n].gap : lk_node_prop_i32(t, n, UIP_GAP, 0);
  int cols = grid_columns(t, n);
  int count = visible_count(t, n);
  int rows = (count + cols - 1) / cols;
  lk_i32 *col_w;
  lk_i32 *row_h;
  lk_i32 *col_x;
  lk_ix c;
  int i;
  lk_i32 y;

  if (count == 0) {
    return 1;
  }

  col_w = (lk_i32 *)lk_sys_alloc(NULL, (lk_u32)(sizeof(lk_i32) * (lk_u32)cols));
  col_x = (lk_i32 *)lk_sys_alloc(NULL, (lk_u32)(sizeof(lk_i32) * (lk_u32)cols));
  row_h = (lk_i32 *)lk_sys_alloc(NULL, (lk_u32)(sizeof(lk_i32) * (lk_u32)rows));

  if (!col_w || !row_h || !col_x) {
    if (col_w) {
      lk_sys_dealloc(NULL, col_w);
    }

    if (col_x) {
      lk_sys_dealloc(NULL, col_x);
    }

    if (row_h) {
      lk_sys_dealloc(NULL, row_h);
    }

    return 0;
  }

  memset(col_w, 0, sizeof(lk_i32) * (lk_u32)cols);
  memset(row_h, 0, sizeof(lk_i32) * (lk_u32)rows);
  grid_extents(t, n, sizes, cols, col_w, row_h);

  col_x[0] = content->x;

  for (i = 1; i < cols; i++) {
    col_x[i] = col_x[i - 1] + col_w[i - 1] + gap;
  }

  i = 0;
  y = content->y;
  c = t->nodes[n].first_child;

  while (c) {
    if (!lk_node_prop_bool(t, c, UIP_HIDDEN)) {
      int col = i % cols;
      int row = i / cols;

      if (col == 0 && row > 0) {
        y += row_h[row - 1] + gap;
      }

      rects[c].x = col_x[col];
      rects[c].y = y;
      rects[c].w = col_w[col];
      rects[c].h = row_h[row];
      i++;
    }

    c = t->nodes[c].next_sibling;
  }

  lk_sys_dealloc(NULL, col_w);
  lk_sys_dealloc(NULL, col_x);
  lk_sys_dealloc(NULL, row_h);

  return 1;
}

lk_widget_def lk_grid_widget_def(void) {
  lk_widget_def def;
  memset(&def, 0, sizeof(def));
  def.measure = measure_grid;
  def.layout = layout_grid;
  def.render = 0;
  def.event = 0;
  def.clips = 0;
  return def;
}
