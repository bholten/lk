/*
 * lk-styled-text.c -- UIK_STYLED_TEXT: read-only, wrapping, per-range
 * styled text (docs/styled-text.md).
 *
 * Text is the UIP_TEXT prop; appearance and presentations come from an
 * app-owned lk_spans through UIP_SPANS; UIP_WRAP picks the row policy
 * (default WORD).  The kind is sized by its content at the width the
 * parent gives it through the fit_height hook (section 2), so its
 * intrinsic width is 0 when wrapping ("take what you are given").
 *
 * Rows: one breaker (st_break_line) restates the editor's pinned
 * policy -- character fit through index_from_x with the x_from_index
 * overshoot check, WORD as a post-process preferring the most recent
 * space / tab boundary in the row (trailing whitespace stays up),
 * tab stops at 4 space advances from the row start, an empty row
 * always takes one codepoint.  It is a callback walk over the bytes
 * with no allocation; fit_height counts rows, render draws them,
 * pos_at searches them.
 *
 * Presentations (section 6): POINTER_DOWN maps the point to a byte
 * position from the ui-owned rects, asks the span set for candidates
 * at it, stamps locus_kind "text-range" + the position, and offers
 * them to THE matcher.  No match bubbles; the kind never focuses or
 * captures.
 */

#include <string.h>

#include "lk-memory.h"
#include "lk-styled-text.h"
#include "lk-text-align.h"
#include "lk-utf8.h"
#include <lk.h>

#define ST_TAB_SPACES 4
#define ST_PRES_HIT_CAP 8

/* ---- backend wrappers ---- */

typedef struct st_font {
  const lk_text_backend *tb;
  lk_u16 font_id, font_size;
} st_font;

static lk_i32 st_run_x(const st_font *f, const char *p, lk_u32 len, lk_u32 ix) {
  lk_str run;

  if (!f->tb || len == 0) {
    return 0;
  }

  if (ix > len) {
    ix = len;
  }

  run.ptr = p;
  run.len = len;

  return f->tb->x_from_index(f->tb->ud, run, f->font_id, f->font_size, ix);
}

static lk_u32 st_run_ix(const st_font *f, const char *p, lk_u32 len, lk_i32 x) {
  lk_str run;

  if (!f->tb || len == 0) {
    return 0;
  }

  run.ptr = p;
  run.len = len;

  return f->tb->index_from_x(f->tb->ud, run, f->font_id, f->font_size, x);
}

static lk_i32 st_line_height(const st_font *f) {
  lk_i32 h = 0;

  if (f->tb) {
    h = f->tb->line_height(f->tb->ud, f->font_id, f->font_size);
  }

  return h > 0 ? h : 16;
}

static lk_i32 st_tab_px(const st_font *f) {
  lk_i32 sp = st_run_x(f, " ", 1, 1);

  if (sp <= 0) {
    sp = 8;
  }

  return sp * ST_TAB_SPACES;
}

static lk_i32 st_baseline(const st_font *f) {
  lk_text_metrics m;

  m.w = 0;
  m.h = 0;
  m.baseline = 0;

  if (f->tb) {
    f->tb->measure(f->tb->ud, lk_str_c("Mg"), f->font_id, f->font_size, &m);
  }

  return m.baseline > 0 ? m.baseline : 12;
}

/* Longest prefix of seg[0..len) that fits in avail px (the editor's
 * ed_fit_prefix, verbatim in policy). */
static lk_u32 st_fit_prefix(const st_font *f, const char *seg, lk_u32 len,
                            lk_i32 avail) {
  lk_u32 ix;

  if (avail <= 0) {
    return 0;
  }

  ix = st_run_ix(f, seg, len, avail);

  if (ix > len) {
    ix = len;
  }

  if (ix > 0 && st_run_x(f, seg, len, ix) > avail) {
    ix = lk_utf8_prev(seg, len, ix);
  }

  return ix;
}

/* ---- rows ---- */

typedef int (*st_row_fn)(void *ud, lk_u32 start, lk_u32 end, lk_u32 k);

/* Break one line p[0..len) (no '\n') into rows; calls fn per row with
 * *k as the running row index.  Returns 0 when fn asked to stop. */
static int st_break_line(const st_font *f, const char *p, lk_u32 len,
                         lk_u32 line_off, lk_i32 width, lk_wrap_mode mode,
                         st_row_fn fn, void *ud, lk_u32 *k) {
  lk_i32 tabpx = st_tab_px(f);
  lk_i32 x = 0;
  lk_u32 i = 0;
  lk_u32 row_start = 0;
  lk_u32 first_k = *k;

  if (mode == LK_WRAP_NONE || width <= 0) {
    return fn(ud, line_off, line_off + len, (*k)++);
  }

  while (i < len) {
    if (p[i] == '\t') {
      lk_i32 stop = (x / tabpx + 1) * tabpx;

      if (stop > width && i > row_start) {
        if (!fn(ud, line_off + row_start, line_off + i, (*k)++)) {
          return 0;
        }

        row_start = i;
        x = 0;
        continue; /* re-place the tab on the fresh row */
      }

      x = stop;
      i++;
      continue;
    }

    {
      lk_u32 j = i;
      lk_i32 seg_w;
      lk_u32 fit;
      lk_u32 brk;

      while (j < len && p[j] != '\t') {
        j++;
      }

      seg_w = st_run_x(f, p + i, j - i, j - i);

      if (x + seg_w <= width) {
        x += seg_w;
        i = j;
        continue;
      }

      fit = st_fit_prefix(f, p + i, j - i, width - x);

      if (fit == 0 && i > row_start) {
        /* nothing of this segment fits after what is already on the
         * row: break before it */
        if (!fn(ud, line_off + row_start, line_off + i, (*k)++)) {
          return 0;
        }

        row_start = i;
        x = 0;
        continue;
      }

      if (fit == 0) {
        /* progress guarantee: an empty row takes one codepoint */
        fit = lk_utf8_next(p, len, i) - i;
      }

      brk = i + fit;

      if (mode == LK_WRAP_WORD) {
        /* Whitespace at the floor hangs past the width (the break
         * lands after it); otherwise prefer the most recent boundary
         * after a space or tab in (row_start, brk]. */
        lk_u32 b = brk;

        while (b < j && p[b] == ' ') {
          b++;
        }

        if (b == brk) {
          while (b > row_start && p[b - 1] != ' ' && p[b - 1] != '\t') {
            b--;
          }
        }

        if (b >= len) {
          i = len; /* only spaces remain: they hang, the line is done */
          continue;
        }

        if (b > row_start) {
          brk = b;
        }
      }

      if (!fn(ud, line_off + row_start, line_off + brk, (*k)++)) {
        return 0;
      }

      row_start = brk;
      i = brk;
      x = 0;
    }
  }

  /* The rest of the line -- unless a break consumed it exactly and
   * the line already produced a row (no empty trailing row). */
  if (row_start < len || *k == first_k) {
    return fn(ud, line_off + row_start, line_off + len, (*k)++);
  }

  return 1;
}

/* Walk every row of text; a line always yields at least one row. */
static void st_rows(const st_font *f, lk_str text, lk_i32 width,
                    lk_wrap_mode mode, st_row_fn fn, void *ud) {
  lk_u32 i = 0;
  lk_u32 k = 0;

  for (;;) {
    lk_u32 j = i;

    while (j < text.len && text.ptr[j] != '\n') {
      j++;
    }

    if (!st_break_line(f, text.ptr + i, j - i, i, width, mode, fn, ud, &k)) {
      return;
    }

    if (j >= text.len) {
      return;
    }

    i = j + 1;
  }
}

struct st_count {
  lk_u32 n;
};

static int st_count_fn(void *ud, lk_u32 s, lk_u32 e, lk_u32 k) {
  (void)s;
  (void)e;
  (void)k;
  ((struct st_count *)ud)->n++;

  return 1;
}

struct st_find {
  lk_u32 want, start, end;
  int found;
};

static int st_find_fn(void *ud, lk_u32 s, lk_u32 e, lk_u32 k) {
  struct st_find *fd = (struct st_find *)ud;

  if (k == fd->want) {
    fd->start = s;
    fd->end = e;
    fd->found = 1;

    return 0;
  }

  return 1;
}

lk_u32 lk_styled_text_row_count(const lk_text_backend *tb, lk_u16 font_id,
                                lk_u16 font_size, lk_str text, lk_i32 width,
                                lk_wrap_mode mode) {
  st_font f;
  struct st_count c;

  f.tb = tb;
  f.font_id = font_id;
  f.font_size = font_size;
  c.n = 0;
  st_rows(&f, text, width, mode, st_count_fn, &c);

  return c.n;
}

int lk_styled_text_row(const lk_text_backend *tb, lk_u16 font_id,
                       lk_u16 font_size, lk_str text, lk_i32 width,
                       lk_wrap_mode mode, lk_u32 k, lk_u32 *start,
                       lk_u32 *end) {
  st_font f;
  struct st_find fd;

  f.tb = tb;
  f.font_id = font_id;
  f.font_size = font_size;
  fd.want = k;
  fd.found = 0;
  fd.start = 0;
  fd.end = 0;
  st_rows(&f, text, width, mode, st_find_fn, &fd);

  if (!fd.found) {
    return 0;
  }

  if (start) {
    *start = fd.start;
  }

  if (end) {
    *end = fd.end;
  }

  return 1;
}

/* Pixel x of byte ix within row p[0..len), with tab expansion
 * (relative to the row's origin). */
static lk_i32 st_row_x(const st_font *f, const char *p, lk_u32 len, lk_u32 ix) {
  lk_i32 x = 0;
  lk_u32 i = 0;
  lk_i32 tabpx = st_tab_px(f);

  if (ix > len) {
    ix = len;
  }

  while (i < len) {
    lk_u32 j = i;

    while (j < len && p[j] != '\t') {
      j++;
    }

    if (ix <= j) {
      return x + st_run_x(f, p + i, j - i, ix - i);
    }

    x += st_run_x(f, p + i, j - i, j - i);

    if (j < len) {
      x = (x / tabpx + 1) * tabpx;
      i = j + 1;

      if (ix <= i) {
        return x;
      }
    } else {
      i = j;
    }
  }

  return x;
}

/* Byte index in row p[0..len) nearest pixel x (row-relative). */
static lk_u32 st_row_ix(const st_font *f, const char *p, lk_u32 len, lk_i32 x) {
  lk_i32 cx = 0;
  lk_u32 i = 0;
  lk_i32 tabpx = st_tab_px(f);

  if (x <= 0) {
    return 0;
  }

  while (i < len) {
    lk_u32 j = i;
    lk_i32 seg_w;

    while (j < len && p[j] != '\t') {
      j++;
    }

    seg_w = st_run_x(f, p + i, j - i, j - i);

    if (x <= cx + seg_w) {
      return i + st_run_ix(f, p + i, j - i, x - cx);
    }

    cx += seg_w;

    if (j < len) {
      lk_i32 stop = (cx / tabpx + 1) * tabpx;

      if (x < (cx + stop) / 2) {
        return j;
      }

      cx = stop;
      i = j + 1;
    } else {
      i = j;
    }
  }

  return len;
}

/* ---- node parameters ---- */

static void st_font_of(st_font *f, const lk_text_backend *tb,
                       const lk_style *styles, lk_ix n) {
  f->tb = tb;
  f->font_id = styles ? (lk_u16)styles[n].font_id : 0;
  f->font_size = styles ? (lk_u16)styles[n].font_size : 0;
}

static lk_wrap_mode st_mode(const lk_tree *t, lk_ix n) {
  lk_i32 m = lk_node_prop_i32(t, n, UIP_WRAP, LK_WRAP_WORD);

  if (m < 0 || m > LK_WRAP_WORD) {
    m = LK_WRAP_WORD;
  }

  return (lk_wrap_mode)m;
}

static lk_i32 st_inset(const lk_tree *t, lk_ix n, const lk_style *styles) {
  lk_i32 pad =
      styles ? styles[n].padding : lk_node_prop_i32(t, n, UIP_PADDING, 0);
  lk_i32 bw = styles ? styles[n].border_width : 0;

  return pad + bw;
}

/* ---- measure / fit ---- */

struct st_widest {
  const st_font *f;
  lk_str text;
  lk_i32 w;
};

static int st_widest_fn(void *ud, lk_u32 s, lk_u32 e, lk_u32 k) {
  struct st_widest *wd = (struct st_widest *)ud;
  lk_i32 w = st_row_x(wd->f, wd->text.ptr + s, e - s, e - s);
  (void)k;

  if (w > wd->w) {
    wd->w = w;
  }

  return 1;
}

static void measure_styled_text(const lk_tree *t, lk_ix n, const lk_size *sizes,
                                const lk_layout_cfg *cfg, lk_i32 *out_w,
                                lk_i32 *out_h) {
  st_font f;
  lk_str text = lk_node_text(t, n);
  lk_wrap_mode mode = st_mode(t, n);
  lk_i32 inset = st_inset(t, n, cfg->styles);
  lk_i32 lh;
  lk_u32 lines = 1;
  lk_u32 i;

  (void)sizes;
  st_font_of(&f, cfg->text, cfg->styles, n);
  lh = st_line_height(&f);

  /* Render has no backend of its own: stash the one the rows were
   * measured with (the text_input's origin/font stash, same idea). */
  if (cfg->geom) {
    cfg->geom[n].styled.tb = cfg->text;
  }

  for (i = 0; i < text.len; i++) {
    if (text.ptr[i] == '\n') {
      lines++;
    }
  }

  if (mode == LK_WRAP_NONE) {
    struct st_widest wd;

    wd.f = &f;
    wd.text = text;
    wd.w = 0;
    st_rows(&f, text, 0, LK_WRAP_NONE, st_widest_fn, &wd);
    *out_w = wd.w + inset * 2;
  } else {
    /* a wrapping paragraph takes the width it is given */
    *out_w = 0;
  }

  *out_h = (lk_i32)lines * lh + inset * 2; /* floor; fit adds the rows */
}

static lk_i32 fit_styled_text(const lk_tree *t, lk_ix n, lk_i32 width,
                              const lk_size *sizes, const lk_layout_cfg *cfg) {
  st_font f;
  lk_str text = lk_node_text(t, n);
  lk_wrap_mode mode = st_mode(t, n);
  lk_i32 inset = st_inset(t, n, cfg->styles);
  lk_i32 inner = width - inset * 2;
  struct st_count c;

  (void)sizes;
  st_font_of(&f, cfg->text, cfg->styles, n);
  c.n = 0;
  st_rows(&f, text, inner, mode, st_count_fn, &c);

  return (lk_i32)c.n * st_line_height(&f) + inset * 2;
}

/* ---- render ---- */

struct st_render {
  const st_font *f;
  lk_str text;
  const lk_spans *sp;
  const lk_style *style;
  lk_render_list *out;
  lk_i32 x0, y0, lh, baseline, inner_w;
  lk_u32 span_ix; /* first span that may reach this or a later row */
  lk_u8 talign;
};

static void st_fill(lk_render_list *out, lk_i32 x, lk_i32 y, lk_i32 w, lk_i32 h,
                    lk_color c) {
  lk_render_cmd cmd;

  if (w <= 0 || h <= 0) {
    return;
  }

  memset(&cmd, 0, sizeof(cmd));
  cmd.op = LK_ROP_FILL_RECT;
  cmd.rect.x = x;
  cmd.rect.y = y;
  cmd.rect.w = w;
  cmd.rect.h = h;
  cmd.color = c;
  lk_render_list_push(out, cmd);
}

static void st_run(struct st_render *r, lk_i32 x, lk_i32 y, const char *p,
                   lk_u32 len, lk_color fg) {
  lk_render_cmd cmd;
  lk_u32 off;

  if (len == 0 || !lk_render_list_push_run(r->out, p, len, &off)) {
    return;
  }

  memset(&cmd, 0, sizeof(cmd));
  cmd.op = LK_ROP_DRAW_RUN;
  cmd.rect.x = x;
  cmd.rect.y = y;
  cmd.color = fg;
  cmd.font_id = r->f->font_id;
  cmd.font_size = r->f->font_size;
  cmd.run_off = off;
  cmd.run_len = len;
  lk_render_list_push(r->out, cmd);
}

/* The span covering pos, or NULL; advances r->span_ix past spans that
 * end at or before pos (rows and spans both ascend). */
static const lk_text_span *st_span_at(struct st_render *r, lk_u32 pos,
                                      lk_u32 *next_start) {
  lk_u32 count = lk_spans_count(r->sp);

  *next_start = 0xFFFFFFFFu;

  while (r->span_ix < count) {
    const lk_text_span *s = lk_spans_get(r->sp, r->span_ix);

    if (s->end <= pos) {
      r->span_ix++;
      continue;
    }

    if (s->start <= pos) {
      return s;
    }

    *next_start = s->start;

    return NULL;
  }

  return NULL;
}

static int st_render_row(void *ud, lk_u32 rs, lk_u32 re, lk_u32 k) {
  struct st_render *r = (struct st_render *)ud;
  const char *p = r->text.ptr + rs;
  lk_u32 len = re - rs;
  lk_i32 row_w = st_row_x(r->f, p, len, len);
  lk_i32 x0 = r->x0 + lk_text_align_offset(r->talign, r->inner_w, row_w);
  lk_i32 y = r->y0 + (lk_i32)k * r->lh;
  lk_u32 save_ix = r->span_ix;
  lk_u32 cur;
  lk_u32 next_start;
  lk_u32 i;

  /* 1. span backgrounds */
  for (i = save_ix; i < lk_spans_count(r->sp); i++) {
    const lk_text_span *s = lk_spans_get(r->sp, i);
    lk_u32 a, b;

    if (s->start >= re) {
      break;
    }

    if (s->end <= rs || !(s->flags & LK_SPAN_BG)) {
      continue;
    }

    a = s->start > rs ? s->start - rs : 0;
    b = s->end < re ? s->end - rs : len;
    st_fill(r->out, x0 + st_row_x(r->f, p, len, a), y,
            st_row_x(r->f, p, len, b) - st_row_x(r->f, p, len, a), r->lh,
            s->bg);
  }

  /* 2. runs: split at span boundaries and tabs */
  cur = 0;

  while (cur < len) {
    const lk_text_span *s = st_span_at(r, rs + cur, &next_start);
    lk_u32 stop = len;
    lk_color fg = r->style->fg;
    lk_u32 seg_end;

    if (s) {
      if (s->end - rs < stop) {
        stop = s->end - rs;
      }

      if (s->flags & LK_SPAN_FG) {
        fg = s->fg;
      }
    } else if (next_start != 0xFFFFFFFFu && next_start - rs < stop) {
      stop = next_start - rs;
    }

    if (p[cur] == '\t') {
      cur++;
      continue;
    }

    seg_end = cur;

    while (seg_end < stop && p[seg_end] != '\t') {
      seg_end++;
    }

    st_run(r, x0 + st_row_x(r->f, p, len, cur), y, p + cur, seg_end - cur, fg);
    cur = seg_end;
  }

  /* 3. underlines (baseline + 1) */
  for (i = save_ix; i < lk_spans_count(r->sp); i++) {
    const lk_text_span *s = lk_spans_get(r->sp, i);
    lk_u32 a, b;

    if (s->start >= re) {
      break;
    }

    if (s->end <= rs || !(s->flags & LK_SPAN_UNDERLINE)) {
      continue;
    }

    a = s->start > rs ? s->start - rs : 0;
    b = s->end < re ? s->end - rs : len;
    st_fill(r->out, x0 + st_row_x(r->f, p, len, a), y + r->baseline + 1,
            st_row_x(r->f, p, len, b) - st_row_x(r->f, p, len, a), 1,
            (s->flags & LK_SPAN_FG) ? s->fg : r->style->fg);
  }

  /* spans that continue onto the next row must stay visible to it */
  r->span_ix = save_ix;

  while (r->span_ix < lk_spans_count(r->sp) &&
         lk_spans_get(r->sp, r->span_ix)->end <= re) {
    r->span_ix++;
  }

  return 1;
}

static void render_styled_text(const lk_tree *t, lk_ix n, const lk_rect *rect,
                               const lk_style *style, const lk_state *state,
                               const lk_widget_geom *geom,
                               lk_render_list *out) {
  lk_render_cmd cmd;
  struct st_render r;
  st_font f;
  lk_str text = lk_node_text(t, n);
  lk_i32 inset = style->padding + style->border_width;
  lk_wrap_mode mode = st_mode(t, n);
  struct st_count c;
  lk_i32 block_h;

  (void)state;

  if (style->bg.a > 0) {
    st_fill(out, rect->x, rect->y, rect->w, rect->h, style->bg);
  }

  if (text.len == 0) {
    return;
  }

  /* The rows are re-derived with the backend measure stashed in the
   * geometry scratch; no geom (no backend) = every line is one
   * unwrapped row, still styled and clipped. */
  f.tb = geom ? geom->styled.tb : NULL;
  f.font_id = (lk_u16)style->font_id;
  f.font_size = (lk_u16)style->font_size;

  memset(&cmd, 0, sizeof(cmd));
  cmd.op = LK_ROP_CLIP_BEGIN;
  cmd.rect = *rect;
  lk_render_list_push(out, cmd);

  r.f = &f;
  r.text = text;
  r.sp = lk_spans_from_node(t->resources, t, n);
  r.style = style;
  r.out = out;
  r.lh = st_line_height(&f);
  r.baseline = st_baseline(&f);
  r.inner_w = rect->w - inset * 2;
  r.span_ix = 0;
  r.talign = style->text_align;

  c.n = 0;
  st_rows(&f, text, r.inner_w, mode, st_count_fn, &c);
  block_h = (lk_i32)c.n * r.lh;

  r.x0 = rect->x + inset;
  r.y0 = rect->y + inset +
         lk_text_align_offset(style->text_valign, rect->h - inset * 2, block_h);

  st_rows(&f, text, r.inner_w, mode, st_render_row, &r);

  memset(&cmd, 0, sizeof(cmd));
  cmd.op = LK_ROP_CLIP_END;
  lk_render_list_push(out, cmd);
}

/* ---- position under a point ---- */

struct st_hit {
  const st_font *f;
  lk_str text;
  lk_i32 x, y; /* relative to the text origin */
  lk_i32 lh;
  lk_i32 inner_w;
  lk_u8 talign;
  lk_u32 pos;
  int found;
};

static int st_hit_fn(void *ud, lk_u32 rs, lk_u32 re, lk_u32 k) {
  struct st_hit *h = (struct st_hit *)ud;
  lk_i32 top = (lk_i32)k * h->lh;

  if (h->y < top) {
    return 0;
  }

  if (h->y < top + h->lh) {
    const char *p = h->text.ptr + rs;
    lk_u32 len = re - rs;
    lk_i32 row_w = st_row_x(h->f, p, len, len);
    lk_i32 ox = lk_text_align_offset(h->talign, h->inner_w, row_w);

    h->pos = rs + st_row_ix(h->f, p, len, h->x - ox);
    h->found = 1;

    return 0;
  }

  return 1;
}

/* Shared by the public query and the event handler: the byte under
 * (x, y) in window coords for node n laid out at rect. */
static int st_pos_at(const lk_ui *ui, const lk_tree *t, lk_ix n,
                     const lk_rect *rect, lk_i32 x, lk_i32 y, lk_u32 *out_pos) {
  const lk_style *styles = lk_ui_styles(ui);
  st_font f;
  lk_str text = lk_node_text(t, n);
  lk_i32 inset = st_inset(t, n, styles);
  struct st_hit h;
  struct st_count c;
  lk_i32 block_h;
  lk_wrap_mode mode = st_mode(t, n);

  if (rect->w <= 0 || rect->h <= 0 || x < rect->x || y < rect->y ||
      x >= rect->x + rect->w || y >= rect->y + rect->h) {
    return 0;
  }

  st_font_of(&f, ui->text, styles, n);
  h.f = &f;
  h.text = text;
  h.lh = st_line_height(&f);
  h.inner_w = rect->w - inset * 2;
  h.talign = styles ? styles[n].text_align : 0;
  c.n = 0;
  st_rows(&f, text, h.inner_w, mode, st_count_fn, &c);
  block_h = (lk_i32)c.n * h.lh;
  h.x = x - (rect->x + inset);
  h.y = y - (rect->y + inset +
             lk_text_align_offset(styles ? styles[n].text_valign : 0,
                                  rect->h - inset * 2, block_h));
  h.found = 0;
  h.pos = 0;

  if (h.y < 0) {
    return 0;
  }

  st_rows(&f, text, h.inner_w, mode, st_hit_fn, &h);

  if (!h.found) {
    return 0;
  }

  *out_pos = h.pos;

  return 1;
}

int lk_styled_text_pos_at(const lk_ui *ui, lk_node_id id, lk_i32 x, lk_i32 y,
                          lk_u32 *out_pos) {
  const lk_tree *t;
  lk_ix n;
  lk_rect rect;

  if (!ui || !out_pos) {
    return 0;
  }

  t = lk_ui_tree(ui);
  n = lk_tree_find_by_id(t, id);

  if (n == 0 || t->nodes[n].kind != UIK_STYLED_TEXT ||
      !lk_node_rect(ui, id, &rect)) {
    return 0;
  }

  return st_pos_at(ui, t, n, &rect, x, y, out_pos);
}

/* ---- events ---- */

static int event_styled_text(lk_ui *ui, const lk_tree *t, lk_ix n,
                             lk_event *ev) {
  lk_rect rect;
  lk_u32 pos;
  const lk_spans *sp;
  lk_presentation_hit hits[ST_PRES_HIT_CAP];
  lk_u32 count;
  lk_u32 i;
  lk_u32 locus_kind;

  if (ev->target != n || ev->type != LK_EVENT_POINTER_DOWN) {
    return 0;
  }

  sp = lk_spans_from_node(t->resources, t, n);

  if (!sp || lk_spans_count(sp) == 0) {
    return 0;
  }

  if (!lk_node_rect(ui, t->nodes[n].id, &rect) ||
      !st_pos_at(ui, t, n, &rect, ev->data.pointer.x, ev->data.pointer.y,
                 &pos)) {
    return 0;
  }

  count = lk_spans_present_at(sp, pos, hits, ST_PRES_HIT_CAP);

  if (count == 0) {
    return 0;
  }

  locus_kind = t->intern ? lk_intern_cid(t->intern, "text-range") : 0;

  for (i = 0; i < count; i++) {
    hits[i].locus_kind = locus_kind;
    hits[i].locus[2] = pos;
  }

  return lk_translate_presentations(ui, t, n, ev, hits, count);
}

/* Interior-presentation discovery for the context-menu producer: the
 * click path's query half, same locus stamping. */
static lk_u32 presentations_at_styled_text(lk_ui *ui, const lk_tree *t, lk_ix n,
                                           lk_i32 x, lk_i32 y,
                                           lk_presentation_hit *out,
                                           lk_u32 cap) {
  lk_rect rect;
  lk_u32 pos;
  const lk_spans *sp = lk_spans_from_node(t->resources, t, n);
  lk_u32 count;
  lk_u32 i;
  lk_u32 locus_kind;

  if (!sp || !lk_node_rect(ui, t->nodes[n].id, &rect) ||
      !st_pos_at(ui, t, n, &rect, x, y, &pos)) {
    return 0;
  }

  count = lk_spans_present_at(sp, pos, out, cap);
  locus_kind = t->intern ? lk_intern_cid(t->intern, "text-range") : 0;

  for (i = 0; i < count; i++) {
    out[i].locus_kind = locus_kind;
    out[i].locus[2] = pos;
  }

  return count;
}

/* ---- registration ---- */

lk_widget_def lk_styled_text_widget_def(void) {
  lk_widget_def def;

  memset(&def, 0, sizeof(def));
  def.measure = measure_styled_text;
  def.layout = 0; /* leaf */
  def.render = render_styled_text;
  def.event = event_styled_text;
  def.fit_height = fit_styled_text;
  def.presentations_at = presentations_at_styled_text;
  def.clips = 0; /* clips its own rows itself */

  return def;
}
