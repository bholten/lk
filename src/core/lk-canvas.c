/*
 * lk-canvas.c -- lk_canvas display list + UIK_CANVAS widget
 * (docs/canvas.md).
 *
 * An lk_canvas is an application-owned retained op buffer -- lines,
 * polylines, rects, text -- in canvas-local integer pixels.  The
 * UIK_CANVAS leaf references one through a UIP_CANVAS resource ref
 * (the image-track ownership model) and replays it every frame,
 * translated to its rect and clipped to it.  Nothing is cached and
 * nothing is interned: point and text bytes live in the canvas's own
 * arena, and the replay copies them into the render list's arena so
 * the list stays a self-contained value.
 */

#include <string.h>

#include "lk-canvas.h"
#include "lk-memory.h"
#include <lk.h>

/* Same ceiling as lk_image: keeps sizes comfortably inside lk_u32. */
#define LK_CANVAS_MAX_DIM 16384u

enum canvas_op_kind {
  CV_LINE = 1, /* a, b, c, d = x0, y0, x1, y1 */
  CV_POLYLINE, /* off/len = point count, packed lk_i32 xy in bytes */
  CV_RECT,     /* a, b, c, d = x, y, w, h (outline) */
  CV_FILL_RECT,
  CV_TEXT,       /* a, b = x, y; off/len = byte range */
  CV_CLIP_BEGIN, /* a, b, c, d = x, y, w, h */
  CV_CLIP_END
};

typedef struct canvas_op {
  lk_u8 kind;
  lk_u8 stroke;
  lk_color color;
  lk_i32 a, b, c, d;
  lk_u32 off; /* byte offset into the arena (POLYLINE / TEXT) */
  lk_u32 len; /* POLYLINE: point count; TEXT: byte length */
} canvas_op;

struct lk_canvas {
  lk_u32 w, h; /* size hint */

  canvas_op *ops;
  lk_u32 op_count, op_cap;

  char *bytes;
  lk_u32 bytes_count, bytes_cap;

  lk_u32 clip_depth; /* sub-clips currently open (append-time) */

  void *(*alloc)(void *, lk_u32);
  void (*dealloc)(void *, void *);
  void *ud;
};

lk_canvas *lk_canvas_new(lk_u32 w, lk_u32 h, void *(*alloc)(void *, lk_u32),
                         void (*dealloc)(void *, void *), void *ud) {
  lk_canvas *c;

  if (w > LK_CANVAS_MAX_DIM || h > LK_CANVAS_MAX_DIM) {
    return NULL;
  }

  if (!alloc) {
    alloc = lk_sys_alloc;
  }
  if (!dealloc) {
    dealloc = lk_sys_dealloc;
  }

  c = (lk_canvas *)alloc(ud, sizeof(lk_canvas));

  if (!c) {
    return NULL;
  }

  memset(c, 0, sizeof(*c));
  c->w = w;
  c->h = h;
  c->alloc = alloc;
  c->dealloc = dealloc;
  c->ud = ud;

  return c;
}

void lk_canvas_destroy(lk_canvas *c) {
  if (!c) {
    return;
  }

  if (c->ops) {
    c->dealloc(c->ud, c->ops);
  }
  if (c->bytes) {
    c->dealloc(c->ud, c->bytes);
  }

  c->dealloc(c->ud, c);
}

void lk_canvas_size(const lk_canvas *c, lk_u32 *w, lk_u32 *h) {
  if (w) {
    *w = c ? c->w : 0;
  }
  if (h) {
    *h = c ? c->h : 0;
  }
}

int lk_canvas_set_size(lk_canvas *c, lk_u32 w, lk_u32 h) {
  if (!c || w > LK_CANVAS_MAX_DIM || h > LK_CANVAS_MAX_DIM) {
    return 0;
  }

  c->w = w;
  c->h = h;

  return 1;
}

void lk_canvas_clear(lk_canvas *c) {
  if (c) {
    c->op_count = 0;
    c->bytes_count = 0;
    c->clip_depth = 0;
  }
}

lk_u32 lk_canvas_op_count(const lk_canvas *c) {
  return c ? c->op_count : 0;
}

/* ---- growth ---- */

static int canvas_reserve_ops(lk_canvas *c, lk_u32 extra) {
  lk_u32 need = c->op_count + extra;
  lk_u32 new_cap;
  canvas_op *grown;

  if (need <= c->op_cap) {
    return 1;
  }

  new_cap = c->op_cap ? c->op_cap : 64;

  while (new_cap < need) {
    new_cap *= 2;
  }

  grown = (canvas_op *)c->alloc(c->ud, (lk_u32)(sizeof(canvas_op) * new_cap));

  if (!grown) {
    return 0;
  }

  if (c->ops) {
    memcpy(grown, c->ops, sizeof(canvas_op) * c->op_count);
    c->dealloc(c->ud, c->ops);
  }

  c->ops = grown;
  c->op_cap = new_cap;

  return 1;
}

/* Copy len bytes into the arena; writes the start offset.  len 0
 * writes the current offset and copies nothing. */
static int canvas_push_bytes(lk_canvas *c, const void *ptr, lk_u32 len,
                             lk_u32 *out_off) {
  lk_u32 need = c->bytes_count + len;

  if (need > c->bytes_cap) {
    lk_u32 new_cap = c->bytes_cap ? c->bytes_cap : 256;
    char *grown;

    while (new_cap < need) {
      new_cap *= 2;
    }

    grown = (char *)c->alloc(c->ud, new_cap);

    if (!grown) {
      return 0;
    }

    if (c->bytes) {
      memcpy(grown, c->bytes, c->bytes_count);
      c->dealloc(c->ud, c->bytes);
    }

    c->bytes = grown;
    c->bytes_cap = new_cap;
  }

  *out_off = c->bytes_count;

  if (len > 0) {
    memcpy(c->bytes + c->bytes_count, ptr, len);
    c->bytes_count += len;
  }

  return 1;
}

static canvas_op *canvas_append(lk_canvas *c, lk_u8 kind, lk_color color,
                                lk_u8 stroke) {
  canvas_op *op;

  if (!canvas_reserve_ops(c, 1)) {
    return NULL;
  }

  op = &c->ops[c->op_count++];
  memset(op, 0, sizeof(*op));
  op->kind = kind;
  op->color = color;
  op->stroke = stroke;

  return op;
}

/* ---- ops ---- */

int lk_canvas_line(lk_canvas *c, lk_i32 x0, lk_i32 y0, lk_i32 x1, lk_i32 y1,
                   lk_color color, lk_u8 stroke) {
  canvas_op *op;

  if (!c) {
    return 0;
  }

  op = canvas_append(c, CV_LINE, color, stroke);

  if (!op) {
    return 0;
  }

  op->a = x0;
  op->b = y0;
  op->c = x1;
  op->d = y1;

  return 1;
}

int lk_canvas_polyline(lk_canvas *c, const lk_i32 *xy, lk_u32 n_points,
                       lk_color color, lk_u8 stroke) {
  canvas_op *op;
  lk_u32 off;
  lk_u32 saved_bytes;

  if (!c || !xy || n_points < 2 || n_points > LK_CANVAS_MAX_POINTS) {
    return 0;
  }

  /* Bytes first so an op-array failure can roll them back cheaply. */
  saved_bytes = c->bytes_count;

  if (!canvas_push_bytes(c, xy, n_points * 2u * (lk_u32)sizeof(lk_i32),
                         &off)) {
    return 0;
  }

  op = canvas_append(c, CV_POLYLINE, color, stroke);

  if (!op) {
    c->bytes_count = saved_bytes;
    return 0;
  }

  op->off = off;
  op->len = n_points;

  return 1;
}

int lk_canvas_rect(lk_canvas *c, lk_rect r, lk_color color, lk_u8 stroke) {
  canvas_op *op;

  if (!c) {
    return 0;
  }

  op = canvas_append(c, CV_RECT, color, stroke);

  if (!op) {
    return 0;
  }

  op->a = r.x;
  op->b = r.y;
  op->c = r.w;
  op->d = r.h;

  return 1;
}

int lk_canvas_fill_rect(lk_canvas *c, lk_rect r, lk_color color) {
  canvas_op *op;

  if (!c) {
    return 0;
  }

  op = canvas_append(c, CV_FILL_RECT, color, 0);

  if (!op) {
    return 0;
  }

  op->a = r.x;
  op->b = r.y;
  op->c = r.w;
  op->d = r.h;

  return 1;
}

int lk_canvas_text(lk_canvas *c, lk_i32 x, lk_i32 y, const char *ptr,
                   lk_u32 len, lk_color color) {
  canvas_op *op;
  lk_u32 off;
  lk_u32 saved_bytes;

  if (!c || (len > 0 && !ptr)) {
    return 0;
  }

  if (len == 0) {
    return 1;
  }

  saved_bytes = c->bytes_count;

  if (!canvas_push_bytes(c, ptr, len, &off)) {
    return 0;
  }

  op = canvas_append(c, CV_TEXT, color, 0);

  if (!op) {
    c->bytes_count = saved_bytes;
    return 0;
  }

  op->a = x;
  op->b = y;
  op->off = off;
  op->len = len;

  return 1;
}

/* ---- Resource integration ---- */

static const lk_resource_type g_canvas_type = {"canvas", NULL};

const lk_resource_type *lk_canvas_type(void) {
  return &g_canvas_type;
}

/* ---- sub-clip ---- */

int lk_canvas_clip_begin(lk_canvas *c, lk_rect r) {
  canvas_op *op;
  lk_color none;

  if (!c || c->clip_depth >= LK_CANVAS_MAX_CLIP_DEPTH || r.w < 0 || r.h < 0) {
    return 0;
  }

  memset(&none, 0, sizeof(none));
  op = canvas_append(c, CV_CLIP_BEGIN, none, 0);

  if (!op) {
    return 0;
  }

  op->a = r.x;
  op->b = r.y;
  op->c = r.w;
  op->d = r.h;
  c->clip_depth++;

  return 1;
}

int lk_canvas_clip_end(lk_canvas *c) {
  lk_color none;

  if (!c || c->clip_depth == 0) {
    return 0;
  }

  memset(&none, 0, sizeof(none));

  if (!canvas_append(c, CV_CLIP_END, none, 0)) {
    return 0;
  }

  c->clip_depth--;

  return 1;
}

lk_u32 lk_canvas_clip_depth(const lk_canvas *c) {
  return c ? c->clip_depth : 0;
}

static lk_resource_ref canvas_node_ref(const lk_tree *t, lk_ix n) {
  const lk_node *nd;
  lk_u32 k;
  lk_resource_ref none;

  none.id = 0;
  none.generation = 0;

  if (!t || n == 0 || n >= t->node_count) {
    return none;
  }

  nd = &t->nodes[n];

  for (k = 0; k < nd->props_len; k++) {
    const lk_prop *p = &t->props[nd->props_off + k];

    if (p->key == UIP_CANVAS && p->value.tag == UIV_RESOURCE) {
      return lk_v_resource_ref(p->value);
    }
  }

  return none;
}

lk_canvas *lk_canvas_from_node(const lk_resources *rs, const lk_tree *t,
                               lk_ix n) {
  lk_resource_ref ref;

  if (!rs) {
    return NULL;
  }

  ref = canvas_node_ref(t, n);

  if (ref.id == 0) {
    return NULL;
  }

  return (lk_canvas *)lk_resource_get(rs, ref, &g_canvas_type);
}

/* ---- Widget ---- */

static void measure_canvas(const lk_tree *t, lk_ix n, const lk_size *sizes,
                           const lk_layout_cfg *cfg, lk_i32 *out_w,
                           lk_i32 *out_h) {
  const lk_canvas *c = lk_canvas_from_node(t->resources, t, n);

  (void)sizes;
  (void)cfg;

  if (c) {
    *out_w = (lk_i32)c->w;
    *out_h = (lk_i32)c->h;
  } else {
    *out_w = 0;
    *out_h = 0;
  }
}

/* Emit one DRAW_LINES for n points, translating by (ox, oy) as the
 * points are copied through a stack window (no per-op allocation;
 * the render list's arena is the only destination). */
static int emit_lines(lk_render_list *out, const lk_i32 *xy, lk_u32 n,
                      lk_i32 ox, lk_i32 oy, lk_color color, lk_u8 stroke) {
  lk_render_cmd cmd;
  lk_i32 win[64];
  lk_u32 i;
  lk_u32 start_off = 0;
  lk_u32 off;
  lk_i32 minx, miny, maxx, maxy;
  lk_u32 pending = 0;
  int first = 1;

  if (n < 2) {
    return 1;
  }

  minx = maxx = xy[0] + ox;
  miny = maxy = xy[1] + oy;

  for (i = 0; i < n; i++) {
    lk_i32 x = xy[i * 2] + ox;
    lk_i32 y = xy[i * 2 + 1] + oy;

    if (x < minx) minx = x;
    if (x > maxx) maxx = x;
    if (y < miny) miny = y;
    if (y > maxy) maxy = y;

    win[pending * 2] = x;
    win[pending * 2 + 1] = y;
    pending++;

    if (pending * 2 == sizeof(win) / sizeof(win[0]) || i + 1 == n) {
      if (!lk_render_list_push_run(out, (const char *)win,
                                   pending * 2u * (lk_u32)sizeof(lk_i32),
                                   &off)) {
        return 0;
      }

      if (first) {
        start_off = off;
        first = 0;
      }

      pending = 0;
    }
  }

  memset(&cmd, 0, sizeof(cmd));
  cmd.op = LK_ROP_DRAW_LINES;
  cmd.run_off = start_off;
  cmd.run_len = n * 2u * (lk_u32)sizeof(lk_i32);
  cmd.color = color;
  cmd.stroke = stroke;
  cmd.rect.x = minx;
  cmd.rect.y = miny;
  cmd.rect.w = maxx - minx + 1;
  cmd.rect.h = maxy - miny + 1;

  return lk_render_list_push(out, cmd);
}

static void render_canvas(const lk_tree *t, lk_ix n, const lk_rect *rect,
                          const lk_style *style, const lk_state *state,
                          const lk_widget_geom *geom, lk_render_list *out) {
  const lk_canvas *c;
  lk_render_cmd cmd;
  lk_u32 i;
  lk_u32 open = 0; /* sub-clips opened by the replay, closed below */

  (void)state;
  (void)geom;

  /* Background (also the degrade for a missing/stale ref). */
  memset(&cmd, 0, sizeof(cmd));
  cmd.op = LK_ROP_FILL_RECT;
  cmd.rect = *rect;
  cmd.color = style->bg;
  lk_render_list_push(out, cmd);

  c = lk_canvas_from_node(t->resources, t, n);

  if (!c || c->op_count == 0) {
    return;
  }

  memset(&cmd, 0, sizeof(cmd));
  cmd.op = LK_ROP_CLIP_BEGIN;
  cmd.rect = *rect;
  lk_render_list_push(out, cmd);

  for (i = 0; i < c->op_count; i++) {
    const canvas_op *op = &c->ops[i];

    switch (op->kind) {
    case CV_LINE: {
      lk_i32 xy[4];

      xy[0] = op->a;
      xy[1] = op->b;
      xy[2] = op->c;
      xy[3] = op->d;
      emit_lines(out, xy, 2, rect->x, rect->y, op->color, op->stroke);
      break;
    }

    case CV_POLYLINE:
      emit_lines(out, (const lk_i32 *)(c->bytes + op->off), op->len, rect->x,
                 rect->y, op->color, op->stroke);
      break;

    case CV_RECT: {
      /* Outline as a closed 5-point polyline over the rect's edge
       * pixels (w/h are extents, so the far edge is at x + w - 1). */
      lk_i32 xy[10];
      lk_i32 x1 = op->a + (op->c > 0 ? op->c - 1 : 0);
      lk_i32 y1 = op->b + (op->d > 0 ? op->d - 1 : 0);

      xy[0] = op->a;
      xy[1] = op->b;
      xy[2] = x1;
      xy[3] = op->b;
      xy[4] = x1;
      xy[5] = y1;
      xy[6] = op->a;
      xy[7] = y1;
      xy[8] = op->a;
      xy[9] = op->b;
      emit_lines(out, xy, 5, rect->x, rect->y, op->color, op->stroke);
      break;
    }

    case CV_FILL_RECT:
      memset(&cmd, 0, sizeof(cmd));
      cmd.op = LK_ROP_FILL_RECT;
      cmd.rect.x = rect->x + op->a;
      cmd.rect.y = rect->y + op->b;
      cmd.rect.w = op->c;
      cmd.rect.h = op->d;
      cmd.color = op->color;
      lk_render_list_push(out, cmd);
      break;

    case CV_CLIP_BEGIN:
      memset(&cmd, 0, sizeof(cmd));
      cmd.op = LK_ROP_CLIP_BEGIN;
      cmd.rect.x = rect->x + op->a;
      cmd.rect.y = rect->y + op->b;
      cmd.rect.w = op->c;
      cmd.rect.h = op->d;
      lk_render_list_push(out, cmd);
      open++;
      break;

    case CV_CLIP_END:
      if (open > 0) {
        memset(&cmd, 0, sizeof(cmd));
        cmd.op = LK_ROP_CLIP_END;
        lk_render_list_push(out, cmd);
        open--;
      }
      break;

    case CV_TEXT: {
      lk_u32 off;

      if (!lk_render_list_push_run(out, c->bytes + op->off, op->len, &off)) {
        break;
      }

      memset(&cmd, 0, sizeof(cmd));
      cmd.op = LK_ROP_DRAW_RUN;
      cmd.rect.x = rect->x + op->a;
      cmd.rect.y = rect->y + op->b;
      cmd.rect.w = 0;
      cmd.rect.h = 0;
      cmd.color = op->color;
      cmd.font_id = style->font_id;
      cmd.font_size = style->font_size;
      cmd.run_off = off;
      cmd.run_len = op->len;
      lk_render_list_push(out, cmd);
      break;
    }

    default: break;
    }
  }

  /* Close sub-clips the list left open, then the canvas's own. */
  while (open > 0) {
    memset(&cmd, 0, sizeof(cmd));
    cmd.op = LK_ROP_CLIP_END;
    lk_render_list_push(out, cmd);
    open--;
  }

  memset(&cmd, 0, sizeof(cmd));
  cmd.op = LK_ROP_CLIP_END;
  lk_render_list_push(out, cmd);
}

lk_widget_def lk_canvas_widget_def(void) {
  lk_widget_def def;

  memset(&def, 0, sizeof(def));
  def.measure = measure_canvas;
  def.render = render_canvas;

  return def;
}
