#define _POSIX_C_SOURCE 200809L
#define _DEFAULT_SOURCE

#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>

#include "core/lk-memory.h"
#include "core/lk-utf8.h"
#include <lk-window.h>

#define TUI_CLIP_MAX 32
#define TUI_DIALOG_MAX 4

#define TUI_ATTR_UNDERLINE 1u
#define TUI_ATTR_REVERSE 2u
#define TUI_ATTR_FRAME 4u

typedef struct tui_cell {
  lk_u32 cp;
  lk_color fg;
  lk_color bg;
  lk_u8 attr;
} tui_cell;

typedef struct tui_dialog {
  lk_file_dialog_fn fn;
  void *ud;
} tui_dialog;

struct lk_window {
  lk_ui *ui;
  lk_rect *rects;
  lk_style *styles;
  lk_u32 styles_cap;
  lk_render_list rl;

  int cw;
  int ch;
  int cols;
  int rows;
  int width;
  int height;

  tui_cell *cells;

  lk_rect clip[TUI_CLIP_MAX];
  int clip_sp;

  int caret_cx;
  int caret_cy;
  int snap;
  char *clipboard;
  size_t clipboard_cap;

  lk_text_backend tb;
  lk_u16 next_font;

  int running;
  lk_u32 frame_serial;
  char *shot_path;

  tui_dialog dialogs[TUI_DIALOG_MAX];
  int dialog_count;

  struct tui_term *term;
};

static lk_u32 tui_decode(const char *s, lk_u32 len, lk_u32 ix, lk_u32 *out) {
  lk_u32 next = lk_utf8_next(s, len, ix);
  lk_u32 n = next - ix;
  const unsigned char *p = (const unsigned char *)s + ix;
  lk_u32 cp;

  switch (n) {
  case 1: cp = p[0] < 0x80 ? p[0] : 0xFFFDu; break;
  case 2: cp = ((lk_u32)(p[0] & 0x1Fu) << 6) | (p[1] & 0x3Fu); break;
  case 3:
    cp = ((lk_u32)(p[0] & 0x0Fu) << 12) | ((lk_u32)(p[1] & 0x3Fu) << 6) |
         (p[2] & 0x3Fu);
    break;
  case 4:
    cp = ((lk_u32)(p[0] & 0x07u) << 18) | ((lk_u32)(p[1] & 0x3Fu) << 12) |
         ((lk_u32)(p[2] & 0x3Fu) << 6) | (p[3] & 0x3Fu);
    break;
  default: cp = 0xFFFDu; break;
  }

  *out = cp;

  return next;
}

static int tui_cp_width(lk_u32 cp) {
  if (cp == 0) {
    return 0;
  }

  if ((cp >= 0x0300 && cp <= 0x036F) || (cp >= 0x0483 && cp <= 0x0489) ||
      (cp >= 0x0591 && cp <= 0x05BD) || (cp >= 0x0610 && cp <= 0x061A) ||
      (cp >= 0x064B && cp <= 0x065F) || (cp >= 0x1AB0 && cp <= 0x1AFF) ||
      (cp >= 0x1DC0 && cp <= 0x1DFF) || (cp >= 0x200B && cp <= 0x200F) ||
      (cp >= 0x202A && cp <= 0x202E) || (cp >= 0x2060 && cp <= 0x2064) ||
      (cp >= 0x20D0 && cp <= 0x20FF) || (cp >= 0xFE00 && cp <= 0xFE0F) ||
      (cp >= 0xFE20 && cp <= 0xFE2F) || cp == 0xFEFF ||
      (cp >= 0xE0100 && cp <= 0xE01EF)) {
    return 0;
  }

  if ((cp >= 0x1100 && cp <= 0x115F) || (cp >= 0x2E80 && cp <= 0x303E) ||
      (cp >= 0x3041 && cp <= 0x33FF) || (cp >= 0x3400 && cp <= 0x4DBF) ||
      (cp >= 0x4E00 && cp <= 0x9FFF) || (cp >= 0xA000 && cp <= 0xA4CF) ||
      (cp >= 0xAC00 && cp <= 0xD7A3) || (cp >= 0xF900 && cp <= 0xFAFF) ||
      (cp >= 0xFE30 && cp <= 0xFE4F) || (cp >= 0xFF00 && cp <= 0xFF60) ||
      (cp >= 0xFFE0 && cp <= 0xFFE6) || (cp >= 0x1F300 && cp <= 0x1F64F) ||
      (cp >= 0x1F680 && cp <= 0x1F6FF) || (cp >= 0x1F900 && cp <= 0x1F9FF) ||
      (cp >= 0x20000 && cp <= 0x3FFFD)) {
    return 2;
  }

  return 1;
}

static lk_u32 tui_displayable(lk_u32 cp) {
  if (cp < 0x20 || (cp >= 0x7F && cp <= 0x9F)) {
    return 0x00B7u;
  }

  return cp;
}

static lk_i32 tui_run_cols(lk_str run, lk_u32 len) {
  lk_u32 i = 0;
  lk_i32 cols = 0;
  lk_u32 cp;

  while (i < len) {
    i = tui_decode(run.ptr, len, i, &cp);
    cols += tui_cp_width(cp);
  }

  return cols;
}

static void tui_measure(void *ud, lk_str run, lk_u16 font_id, lk_u16 font_size,
                        lk_text_metrics *out) {
  lk_window *win = (lk_window *)ud;

  (void)font_id;
  (void)font_size;

  if (!out) {
    return;
  }

  out->w = tui_run_cols(run, run.len) * win->cw;
  out->h = win->ch;
  out->baseline = win->ch * 3 / 4;
}

static lk_i32 tui_x_from_index(void *ud, lk_str run, lk_u16 font_id,
                               lk_u16 font_size, lk_u32 byte_ix) {
  lk_window *win = (lk_window *)ud;
  lk_u32 ix = byte_ix;

  (void)font_id;
  (void)font_size;

  if (ix > run.len) {
    ix = run.len;
  }

  while (ix > 0 && !lk_utf8_is_boundary(run.ptr, run.len, ix)) {
    ix--;
  }

  return tui_run_cols(run, ix) * win->cw;
}

static lk_u32 tui_index_from_x(void *ud, lk_str run, lk_u16 font_id,
                               lk_u16 font_size, lk_i32 x) {
  lk_window *win = (lk_window *)ud;
  lk_u32 i = 0;
  lk_i32 px = 0;

  (void)font_id;
  (void)font_size;

  if (x <= 0) {
    return 0;
  }

  while (i < run.len) {
    lk_u32 cp;
    lk_u32 next = tui_decode(run.ptr, run.len, i, &cp);
    lk_i32 w = tui_cp_width(cp) * win->cw;

    if (x < px + w / 2) {
      return i;
    }

    px += w;
    i = next;
  }

  return run.len;
}

static lk_i32 tui_line_height(void *ud, lk_u16 font_id, lk_u16 font_size) {
  lk_window *win = (lk_window *)ud;

  (void)font_id;
  (void)font_size;

  return win->ch;
}

static lk_u16 tui_register_font(void *ud, const char *path) {
  lk_window *win = (lk_window *)ud;

  (void)path;

  return win->next_font++;
}

static void tui_clear(lk_window *win) {
  int i;
  int n = win->cols * win->rows;
  tui_cell blank;

  blank.cp = 0x20;
  blank.fg.r = blank.fg.g = blank.fg.b = 200;
  blank.fg.a = 255;
  blank.bg.r = blank.bg.g = blank.bg.b = 0;
  blank.bg.a = 255;
  blank.attr = 0;

  for (i = 0; i < n; i++) {
    win->cells[i] = blank;
  }
}

static tui_cell *tui_at(lk_window *win, int cx, int cy) {
  if (cx < 0 || cy < 0 || cx >= win->cols || cy >= win->rows) {
    return NULL;
  }

  return &win->cells[cy * win->cols + cx];
}

static int tui_in_clip(const lk_window *win, lk_i32 px, lk_i32 py) {
  const lk_rect *c;

  if (win->clip_sp == 0) {
    return 1;
  }

  c = &win->clip[win->clip_sp - 1];

  return px >= c->x && py >= c->y && px < c->x + c->w && py < c->y + c->h;
}

static int tui_rect_covers_cell(const lk_window *win, const lk_rect *r, int cx,
                                int cy) {
  lk_i32 x0 = (lk_i32)cx * win->cw;
  lk_i32 y0 = (lk_i32)cy * win->ch;

  return r->x <= x0 && r->y <= y0 && r->x + r->w >= x0 + win->cw &&
         r->y + r->h >= y0 + win->ch;
}

static int tui_cell_has_text(const tui_cell *c) {
  return c->cp != 0x20 && !(c->attr & TUI_ATTR_FRAME);
}

static int tui_cell_visible(const lk_window *win, int cx, int cy) {
  return tui_in_clip(win, (lk_i32)cx * win->cw + win->cw / 2,
                     (lk_i32)cy * win->ch + win->ch / 2);
}

static void tui_clear_glyph(lk_window *win, int cx, int cy) {
  tui_cell *c = tui_at(win, cx, cy);
  tui_cell *o;

  if (!c) {
    return;
  }

  if (c->cp == 0) {
    o = tui_at(win, cx - 1, cy);

    if (o) {
      o->cp = 0x20;
      o->attr = 0;
    }
  } else if (tui_cp_width(c->cp) == 2) {
    o = tui_at(win, cx + 1, cy);

    if (o && o->cp == 0) {
      o->cp = 0x20;
      o->attr = 0;
    }
  }

  c->cp = 0x20;
  c->attr = 0;
}

static lk_u8 tui_mix(lk_u8 dst, lk_u8 src, lk_u8 a) {
  return (lk_u8)((dst * (255 - a) + src * a) / 255);
}

static lk_color tui_blend(lk_color dst, lk_color src) {
  lk_color out;

  out.r = tui_mix(dst.r, src.r, src.a);
  out.g = tui_mix(dst.g, src.g, src.a);
  out.b = tui_mix(dst.b, src.b, src.a);
  out.a = 255;

  return out;
}

static int tui_floor_div(lk_i32 a, int b) {
  return (int)((a >= 0) ? a / b : -((-a + b - 1) / b));
}

static void tui_span_cells(lk_i32 p0, lk_i32 len, int size, int limit, int *c0,
                           int *c1) {
  lk_i32 half = size / 2;
  int a = tui_floor_div(p0 - half + size - 1, size);
  int b = tui_floor_div(p0 + len - half - 1, size);

  if (a < 0) {
    a = 0;
  }

  if (b > limit - 1) {
    b = limit - 1;
  }

  *c0 = a;
  *c1 = b;
}

static int tui_fill_as_rule(lk_window *win, const lk_render_cmd *cmd) {
  const lk_rect *r = &cmd->rect;
  int horiz = r->h < win->ch && r->w >= win->cw && r->w >= 2 * r->h;
  int vert = r->w < win->cw && r->h >= win->ch && r->h >= 2 * r->w;
  int c0, c1, i, fixed;
  lk_u32 cp;

  if (horiz == vert) {
    return 0;
  }

  if (horiz) {
    fixed = tui_floor_div(r->y + r->h / 2, win->ch);
    tui_span_cells(r->x, r->w, win->cw, win->cols, &c0, &c1);
    cp = r->h * 2 >= win->ch ? 0x2501 : 0x2500;
  } else {
    fixed = tui_floor_div(r->x + r->w / 2, win->cw);
    tui_span_cells(r->y, r->h, win->ch, win->rows, &c0, &c1);
    cp = r->w * 2 >= win->cw ? 0x2503 : 0x2502;
  }

  for (i = c0; i <= c1; i++) {
    int cx = horiz ? i : fixed;
    int cy = horiz ? fixed : i;
    tui_cell *c;

    if (!tui_cell_visible(win, cx, cy)) {
      continue;
    }

    c = tui_at(win, cx, cy);

    if (!c || tui_cell_has_text(c) || c->cp == 0) {
      continue;
    }

    c->cp = cp;
    c->attr = TUI_ATTR_FRAME;
    c->fg = cmd->color.a == 255 ? cmd->color : tui_blend(c->bg, cmd->color);
  }

  return 1;
}

static void tui_fill_rect(lk_window *win, const lk_render_cmd *cmd) {
  int cx0;
  int cx1;
  int cy0;
  int cy1;
  int cx;
  int cy;

  if (cmd->rect.w <= 0 || cmd->rect.h <= 0 || cmd->color.a == 0) {
    return;
  }

  if (tui_fill_as_rule(win, cmd)) {
    return;
  }

  tui_span_cells(cmd->rect.x, cmd->rect.w, win->cw, win->cols, &cx0, &cx1);
  tui_span_cells(cmd->rect.y, cmd->rect.h, win->ch, win->rows, &cy0, &cy1);

  for (cy = cy0; cy <= cy1; cy++) {
    for (cx = cx0; cx <= cx1; cx++) {
      tui_cell *c;

      if (!tui_cell_visible(win, cx, cy)) {
        continue;
      }

      c = tui_at(win, cx, cy);

      if (cmd->color.a == 255) {
        if ((c->attr & TUI_ATTR_FRAME) &&
            !tui_rect_covers_cell(win, &cmd->rect, cx, cy)) {
          c->bg = cmd->color;
        } else {
          tui_clear_glyph(win, cx, cy);
          c->bg = cmd->color;
        }
      } else {
        c->bg = tui_blend(c->bg, cmd->color);
      }
    }
  }
}

static void tui_draw_run(lk_window *win, const char *s, lk_u32 len, lk_i32 x,
                         lk_i32 y, lk_color fg, lk_u8 attr) {
  int cx = tui_floor_div(x + win->cw / 2, win->cw);
  int cy = tui_floor_div(y + win->ch / 2, win->ch);
  lk_u32 i = 0;

  if (cy < 0 || cy >= win->rows) {
    return;
  }

  while (i < len && cx < win->cols) {
    lk_u32 cp;
    int w;

    i = tui_decode(s, len, i, &cp);
    cp = tui_displayable(cp);
    w = tui_cp_width(cp);

    if (w == 0) {
      continue;
    }

    if (cx >= 0 && tui_cell_visible(win, cx, cy)) {
      tui_cell *c = tui_at(win, cx, cy);
      tui_cell *n = (w == 2) ? tui_at(win, cx + 1, cy) : NULL;

      if (w == 2 && (!n || !tui_cell_visible(win, cx + 1, cy))) {
        tui_clear_glyph(win, cx, cy);
        c->cp = 0x20;
      } else {
        tui_clear_glyph(win, cx, cy);
        c->cp = cp;
        c->fg = fg;
        c->attr = attr;

        if (n) {
          tui_clear_glyph(win, cx + 1, cy);
          n->cp = 0;
          n->fg = fg;
          n->attr = attr;
        }
      }
    }

    cx += w;
  }
}

static const lk_u8 tui_braille_bit[2][4] = {
    {0x01, 0x02, 0x04, 0x40},
    {0x08, 0x10, 0x20, 0x80}
};

static void tui_plot(lk_window *win, lk_i32 px, lk_i32 py, lk_color fg) {
  int cx = tui_floor_div(px, win->cw);
  int cy = tui_floor_div(py, win->ch);
  tui_cell *c;
  int dx, dy;

  if (!tui_in_clip(win, px, py)) {
    return;
  }

  c = tui_at(win, cx, cy);

  if (!c) {
    return;
  }

  dx = (int)((px - (lk_i32)cx * win->cw) * 2 / win->cw);
  dy = (int)((py - (lk_i32)cy * win->ch) * 4 / win->ch);

  if (dx < 0 || dx > 1 || dy < 0 || dy > 3) {
    return;
  }

  if (c->cp < 0x2800 || c->cp > 0x28FF) {
    if (c->cp != 0x20) {
      return;
    }

    c->cp = 0x2800;
  }

  c->cp |= tui_braille_bit[dx][dy];
  c->fg = fg;
}

static void tui_line(lk_window *win, lk_i32 x0, lk_i32 y0, lk_i32 x1, lk_i32 y1,
                     lk_color fg) {
  lk_i32 dx = x1 > x0 ? x1 - x0 : x0 - x1;
  lk_i32 dy = y1 > y0 ? y0 - y1 : y1 - y0;
  lk_i32 sx = x0 < x1 ? 1 : -1;
  lk_i32 sy = y0 < y1 ? 1 : -1;
  lk_i32 err = dx + dy;

  for (;;) {
    lk_i32 e2;

    tui_plot(win, x0, y0, fg);

    if (x0 == x1 && y0 == y1) {
      break;
    }

    e2 = 2 * err;

    if (e2 >= dy) {
      err += dy;
      x0 += sx;
    }

    if (e2 <= dx) {
      err += dx;
      y0 += sy;
    }
  }
}

static void tui_draw_lines(lk_window *win, const lk_render_cmd *cmd) {
  const lk_i32 *xy;
  lk_u32 n;
  lk_u32 j;

  if (cmd->run_len < 16 || !win->rl.bytes) {
    return;
  }

  xy = (const lk_i32 *)(win->rl.bytes + cmd->run_off);
  n = cmd->run_len / 8;

  for (j = 0; j + 1 < n; j++) {
    tui_line(win, xy[j * 2], xy[j * 2 + 1], xy[j * 2 + 2], xy[j * 2 + 3],
             cmd->color);
  }
}

static void tui_draw_image(lk_window *win, const lk_render_cmd *cmd) {
  lk_resource_ref ref;
  lk_image *img;
  lk_u32 iw;
  lk_u32 ih;
  const lk_u8 *px;
  int cx0;
  int cx1;
  int cy0;
  int cy1;
  int cx;
  int cy;

  ref.id = cmd->img_id;
  ref.generation = cmd->img_gen;
  img = (lk_image *)lk_resource_get(lk_ui_resources(win->ui), ref,
                                    lk_image_type());

  if (!img || cmd->rect.w <= 0 || cmd->rect.h <= 0) {
    return;
  }

  lk_image_size(img, &iw, &ih);
  px = lk_image_pixels(img);

  if (!px || iw == 0 || ih == 0) {
    return;
  }

  tui_span_cells(cmd->rect.x, cmd->rect.w, win->cw, win->cols, &cx0, &cx1);
  tui_span_cells(cmd->rect.y, cmd->rect.h, win->ch, win->rows, &cy0, &cy1);

  for (cy = cy0; cy <= cy1; cy++) {
    for (cx = cx0; cx <= cx1; cx++) {
      tui_cell *c;
      lk_color top, bot;

      top.r = top.g = top.b = top.a = 0;
      bot = top;
      int half;

      if (!tui_cell_visible(win, cx, cy)) {
        continue;
      }

      c = tui_at(win, cx, cy);

      for (half = 0; half < 2; half++) {
        lk_i32 sx = (lk_i32)cx * win->cw + win->cw / 2 - cmd->rect.x;
        lk_i32 sy =
            (lk_i32)cy * win->ch + (half ? 3 : 1) * win->ch / 4 - cmd->rect.y;
        lk_u32 ix, iy;
        const lk_u8 *p;
        lk_color s;

        if (sx < 0) {
          sx = 0;
        }

        if (sy < 0) {
          sy = 0;
        }

        ix = (lk_u32)sx * iw / (lk_u32)cmd->rect.w;
        iy = (lk_u32)sy * ih / (lk_u32)cmd->rect.h;

        if (ix >= iw) {
          ix = iw - 1;
        }

        if (iy >= ih) {
          iy = ih - 1;
        }

        p = px + ((lk_u32)iy * iw + ix) * 4;
        s.r = p[0];
        s.g = p[1];
        s.b = p[2];
        s.a = (lk_u8)(p[3] * cmd->color.a / 255);

        if (half == 0) {
          top = tui_blend(c->bg, s);
        } else {
          bot = tui_blend(c->bg, s);
        }
      }

      tui_clear_glyph(win, cx, cy);
      c->cp = 0x2580;
      c->fg = top;
      c->bg = bot;
    }
  }
}

static const lk_u32 tui_box[2][6] = {
    {0x2500, 0x2502, 0x250C, 0x2510, 0x2514, 0x2518},
    {0x2550, 0x2551, 0x2554, 0x2557, 0x255A, 0x255D}
};

static void tui_stroke_rect(lk_window *win, const lk_render_cmd *cmd) {
  const lk_rect *r = &cmd->rect;
  const lk_u32 *g = tui_box[cmd->stroke >= 2 ? 1 : 0];
  int x0;
  int x1;
  int y0;
  int y1;
  int cx;
  int cy;

  if (r->w <= 0 || r->h <= 0 || cmd->color.a == 0) {
    return;
  }

  tui_span_cells(r->x, r->w, win->cw, win->cols, &x0, &x1);
  tui_span_cells(r->y, r->h, win->ch, win->rows, &y0, &y1);

  if (x1 < x0) {
    x0 = x1 = tui_floor_div(r->x + r->w / 2, win->cw);
  }

  if (y1 < y0) {
    y0 = y1 = tui_floor_div(r->y + r->h / 2, win->ch);
  }

  for (cy = y0; cy <= y1; cy++) {
    for (cx = x0; cx <= x1; cx++) {
      int top = cy == y0, bot = cy == y1, left = cx == x0, right = cx == x1;
      lk_u32 cp;
      tui_cell *c;

      if (!top && !bot && !left && !right) {
        cx = x1 - 1;
        continue;
      }

      if (!tui_cell_visible(win, cx, cy)) {
        continue;
      }

      c = tui_at(win, cx, cy);

      if (!c) {
        continue;
      }

      if (x0 == x1 && y0 == y1) {
        cp = 0x25A1;
      } else if (x0 == x1) {
        cp = g[1];
      } else if (y0 == y1) {
        if (!left && !right) {
          continue;
        }

        cp = left ? 0x5B : 0x5D;
      } else if (top && left) {
        cp = g[2];
      } else if (top && right) {
        cp = g[3];
      } else if (bot && left) {
        cp = g[4];
      } else if (bot && right) {
        cp = g[5];
      } else if (top || bot) {
        cp = g[0];
      } else {
        cp = g[1];
      }

      if (tui_cell_has_text(c) || c->cp == 0) {
        continue;
      }

      c->cp = cp;
      c->attr = TUI_ATTR_FRAME;
      c->fg = cmd->color.a == 255 ? cmd->color : tui_blend(c->bg, cmd->color);
    }
  }
}

static void tui_caret(lk_window *win, const lk_render_cmd *cmd) {
  int cx = tui_floor_div(cmd->rect.x + win->cw / 2, win->cw);
  int cy = tui_floor_div(cmd->rect.y + cmd->rect.h / 2, win->ch);
  tui_cell *c;

  if (!tui_cell_visible(win, cx, cy)) {
    return;
  }

  c = tui_at(win, cx, cy);

  if (!c) {
    return;
  }

  if (c->cp == 0) {
    c = tui_at(win, cx - 1, cy);
    cx--;
  }

  c->attr |= TUI_ATTR_REVERSE;

  if (win->caret_cx < 0) {
    win->caret_cx = cx;
    win->caret_cy = cy;
  }
}

static void tui_clip_begin(lk_window *win, lk_rect r) {
  if (win->clip_sp > 0) {
    const lk_rect *o = &win->clip[win->clip_sp - 1];
    lk_i32 x0 = r.x > o->x ? r.x : o->x;
    lk_i32 y0 = r.y > o->y ? r.y : o->y;
    lk_i32 x1 = (r.x + r.w) < (o->x + o->w) ? (r.x + r.w) : (o->x + o->w);
    lk_i32 y1 = (r.y + r.h) < (o->y + o->h) ? (r.y + r.h) : (o->y + o->h);

    r.x = x0;
    r.y = y0;
    r.w = x1 > x0 ? x1 - x0 : 0;
    r.h = y1 > y0 ? y1 - y0 : 0;
  }

  if (win->clip_sp < TUI_CLIP_MAX) {
    win->clip[win->clip_sp++] = r;
  }
}

static void tui_clip_end(lk_window *win) {
  if (win->clip_sp > 0) {
    win->clip_sp--;
  }
}

static void tui_dump_render_list(const lk_window *win, const lk_tree *cur) {
  static const char *names[] = {
      "?",        "FILL_RECT",  "DRAW_TEXT",  "CLIP_BEGIN",  "CLIP_END",
      "DRAW_RUN", "DRAW_IMAGE", "DRAW_LINES", "STROKE_RECT", "CARET"};
  lk_u32 i;

  fprintf(stderr, "-- frame %u: %u cmds\n", (unsigned)win->frame_serial,
          (unsigned)win->rl.count);

  for (i = 0; i < win->rl.count; i++) {
    const lk_render_cmd *c = &win->rl.cmds[i];
    const char *nm =
        c->op < sizeof(names) / sizeof(names[0]) ? names[c->op] : "?";

    fprintf(stderr, "%4u %-11s %5d %5d %5d %5d  #%02x%02x%02x/%02x",
            (unsigned)i, nm, (int)c->rect.x, (int)c->rect.y, (int)c->rect.w,
            (int)c->rect.h, c->color.r, c->color.g, c->color.b, c->color.a);

    if (c->op == LK_ROP_DRAW_TEXT && c->str_id) {
      lk_str t = lk_intern_str(cur->intern, c->str_id);

      fprintf(stderr, "  \"%.*s\"", (int)(t.len > 40 ? 40 : t.len), t.ptr);
    } else if (c->op == LK_ROP_DRAW_RUN && win->rl.bytes) {
      fprintf(stderr, "  \"%.*s\"", (int)(c->run_len > 40 ? 40 : c->run_len),
              win->rl.bytes + c->run_off);
    } else if (c->op == LK_ROP_STROKE_RECT) {
      fprintf(stderr, "  stroke %u", (unsigned)c->stroke);
    }

    if (c->text_attr) {
      fprintf(stderr, "  attr %u", (unsigned)c->text_attr);
    }

    fputc('\n', stderr);
  }
}

static void tui_paint(lk_window *win, const lk_tree *cur) {
  lk_u32 i;

  win->clip_sp = 0;
  win->caret_cx = -1;
  win->caret_cy = -1;
  tui_clear(win);

  for (i = 0; i < win->rl.count; i++) {
    const lk_render_cmd *cmd = &win->rl.cmds[i];

    switch (cmd->op) {
    case LK_ROP_FILL_RECT: tui_fill_rect(win, cmd); break;
    case LK_ROP_STROKE_RECT: tui_stroke_rect(win, cmd); break;
    case LK_ROP_CARET: tui_caret(win, cmd); break;

    case LK_ROP_DRAW_TEXT:
      if (cmd->str_id != 0) {
        lk_str text = lk_intern_str(cur->intern, cmd->str_id);

        if (text.ptr && text.len > 0) {
          tui_draw_run(win, text.ptr, text.len, cmd->rect.x, cmd->rect.y,
                       cmd->color, cmd->text_attr);
        }
      }

      break;

    case LK_ROP_DRAW_RUN:
      if (cmd->run_len > 0 && win->rl.bytes) {
        tui_draw_run(win, win->rl.bytes + cmd->run_off, cmd->run_len,
                     cmd->rect.x, cmd->rect.y, cmd->color, cmd->text_attr);
      }

      break;

    case LK_ROP_CLIP_BEGIN: tui_clip_begin(win, cmd->rect); break;
    case LK_ROP_CLIP_END: tui_clip_end(win); break;
    case LK_ROP_DRAW_IMAGE: tui_draw_image(win, cmd); break;
    case LK_ROP_DRAW_LINES: tui_draw_lines(win, cmd); break;
    default: break;
    }
  }

  win->clip_sp = 0;

  if (win->term && win->caret_cx >= 0) {
    tui_cell *c = tui_at(win, win->caret_cx, win->caret_cy);

    if (c) {
      c->attr &= (lk_u8)~TUI_ATTR_REVERSE;
    }
  }
}

static void tui_put_utf8(FILE *f, lk_u32 cp) {
  unsigned char b[4];
  int n;

  if (cp < 0x80) {
    b[0] = (unsigned char)cp;
    n = 1;
  } else if (cp < 0x800) {
    b[0] = (unsigned char)(0xC0 | (cp >> 6));
    b[1] = (unsigned char)(0x80 | (cp & 0x3F));
    n = 2;
  } else if (cp < 0x10000) {
    b[0] = (unsigned char)(0xE0 | (cp >> 12));
    b[1] = (unsigned char)(0x80 | ((cp >> 6) & 0x3F));
    b[2] = (unsigned char)(0x80 | (cp & 0x3F));
    n = 3;
  } else {
    b[0] = (unsigned char)(0xF0 | (cp >> 18));
    b[1] = (unsigned char)(0x80 | ((cp >> 12) & 0x3F));
    b[2] = (unsigned char)(0x80 | ((cp >> 6) & 0x3F));
    b[3] = (unsigned char)(0x80 | (cp & 0x3F));
    n = 4;
  }

  fwrite(b, 1, (size_t)n, f);
}

static int tui_color_eq(lk_color a, lk_color b) {
  return a.r == b.r && a.g == b.g && a.b == b.b;
}

static void tui_write_txt(const lk_window *win, FILE *f) {
  int cx;
  int cy;

  for (cy = 0; cy < win->rows; cy++) {
    for (cx = 0; cx < win->cols; cx++) {
      const tui_cell *c = &win->cells[cy * win->cols + cx];

      if (c->cp != 0) {
        tui_put_utf8(f, c->cp);
      }
    }

    fputc('\n', f);
  }
}

static void tui_write_ans(const lk_window *win, FILE *f) {
  int cx;
  int cy;

  for (cy = 0; cy < win->rows; cy++) {
    lk_color fg;
    lk_color bg;
    lk_u8 attr = 0;
    int have = 0;

    for (cx = 0; cx < win->cols; cx++) {
      const tui_cell *c = &win->cells[cy * win->cols + cx];

      if (c->cp == 0) {
        continue;
      }

      if (have && c->attr != attr) {
        fputs("\033[0m", f);
        have = 0;
      }

      if (!have) {
        attr = c->attr;

        if (attr & TUI_ATTR_UNDERLINE) {
          fputs("\033[4m", f);
        }

        if (attr & TUI_ATTR_REVERSE) {
          fputs("\033[7m", f);
        }
      }

      if (!have || !tui_color_eq(fg, c->fg)) {
        fprintf(f, "\033[38;2;%d;%d;%dm", c->fg.r, c->fg.g, c->fg.b);
        fg = c->fg;
      }

      if (!have || !tui_color_eq(bg, c->bg)) {
        fprintf(f, "\033[48;2;%d;%d;%dm", c->bg.r, c->bg.g, c->bg.b);
        bg = c->bg;
      }

      have = 1;
      tui_put_utf8(f, c->cp);
    }

    fputs("\033[0m\n", f);
  }
}

static void tui_service_screenshot(lk_window *win) {
  char *path = win->shot_path;
  size_t n, stem;
  char *buf;
  FILE *f;

  if (!path) {
    return;
  }

  win->shot_path = NULL;
  n = strlen(path);
  stem = n;

  while (stem > 0 && path[stem - 1] != '/' && path[stem - 1] != '.') {
    stem--;
  }

  if (stem == 0 || path[stem - 1] == '/') {
    stem = n;
  } else {
    stem--;
  }

  buf = (char *)malloc(stem + 5);

  if (!buf) {
    free(path);
    return;
  }

  memcpy(buf, path, stem);
  memcpy(buf + stem, ".txt", 5);
  f = fopen(buf, "wb");

  if (f) {
    tui_write_txt(win, f);
    fclose(f);
  }

  memcpy(buf + stem, ".ans", 5);
  f = fopen(buf, "wb");

  if (f) {
    tui_write_ans(win, f);
    fclose(f);
  }

  free(buf);
  free(path);
}

static lk_u32 tui_ticks_ms(void) {
  struct timespec ts;

  if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0) {
    return 0;
  }

  return (lk_u32)(ts.tv_sec * 1000 + ts.tv_nsec / 1000000);
}

static int tui_env_pair(const char *name, int *a, int *b) {
  const char *v = getenv(name);
  int x;
  int y;

  if (!v || sscanf(v, "%dx%d", &x, &y) != 2 || x <= 0 || y <= 0) {
    return 0;
  }

  *a = x;
  *b = y;

  return 1;
}

static lk_i32 tui_round_to(lk_i32 v, lk_i32 unit) {
  return ((v + unit / 2 - 1) / unit) * unit;
}

static int tui_kind_is_leaf(lk_u16 kind) {
  return kind == UIK_LABEL || kind == UIK_BUTTON || kind == UIK_CHECKBOX ||
         kind == UIK_RADIO || kind == UIK_SLIDER || kind == UIK_OPTION ||
         kind == UIK_TEXT_INPUT || kind == UIK_DROPDOWN;
}

static void tui_snap_style(lk_style *st, lk_u16 kind, int cw, int ch) {
  lk_i32 bw = st->border_width > 0 ? 1 : 0;

  if (st->gap > 0) {
    int horiz = kind == UIK_ROW || kind == UIK_TABS || kind == UIK_CHECKBOX ||
                kind == UIK_RADIO;

    st->gap = tui_round_to(st->gap, horiz ? cw : ch);
  }

  if (bw) {
    lk_i32 inset = tui_round_to(st->padding + st->border_width, ch);

    if (inset < ch) {
      inset = ch;
    }

    st->border_width = 1;
    st->padding = inset - 1;
  } else if (st->padding > 0) {
    st->padding = tui_round_to(st->padding, tui_kind_is_leaf(kind) ? cw : ch);
  }
}

static const lk_style *tui_snap_styles(lk_window *win, const lk_tree *t) {
  const lk_style *src = lk_ui_styles(win->ui);
  lk_u32 i;

  if (!src || !win->snap) {
    return src;
  }

  if (t->node_count > win->styles_cap) {
    lk_sys_dealloc(NULL, win->styles);
    win->styles = (lk_style *)lk_sys_alloc(
        NULL, (lk_u32)(sizeof(lk_style) * t->node_count));

    if (!win->styles) {
      win->styles_cap = 0;
      return src;
    }

    win->styles_cap = t->node_count;
  }

  memcpy(win->styles, src, sizeof(lk_style) * t->node_count);

  for (i = 0; i < t->node_count; i++) {
    tui_snap_style(&win->styles[i], t->nodes[i].kind, win->cw, win->ch);
  }

  return win->styles;
}

static void tui_theme_rules(lk_theme *th) {
  lk_style s;

  memset(&s, 0, sizeof(s));
  s.border_width = 1;
  s.border_color.r = 90;
  s.border_color.g = 90;
  s.border_color.b = 110;
  s.border_color.a = 255;
  lk_theme_add_rule(th, UIK_TEXT_INPUT, 0, 0, &s,
                    LK_SF_BORDER_WIDTH | LK_SF_BORDER_COLOR);
  memset(&s, 0, sizeof(s));
  s.border_color.r = 80;
  s.border_color.g = 140;
  s.border_color.b = 220;
  s.border_color.a = 255;
  lk_theme_add_rule(th, UIK_TEXT_INPUT, 0, LK_NSTATE_FOCUSED, &s,
                    LK_SF_BORDER_COLOR);

  memset(&s, 0, sizeof(s));
  s.padding = 0;
  lk_theme_add_rule(th, UIK_TABS, 0, 0, &s, LK_SF_PADDING);
}

#define TUI_IN_MAX 4096
#define TUI_EVQ_MAX 128
#define TUI_ESC_WAIT_MS 25

typedef struct tui_term {
  int fd;
  struct termios orig;
  int raw;
  int kitty;
  int kitty_pushed;
  int da1;
  int colors;
  int err_saved;

  unsigned char in[TUI_IN_MAX];
  lk_u32 in_len;
  lk_u32 esc_since_ms;

  int in_paste;
  char *paste;
  lk_u32 paste_len;
  lk_u32 paste_cap;

  lk_event evq[TUI_EVQ_MAX];
  int evq_n;

  char *out;
  size_t out_len, out_cap;

  tui_cell *prev;
  int force;
  int cursor_shown;
  int caret_x, caret_y;

  lk_i32 mouse_x;
  lk_i32 mouse_y;
} tui_term;

static tui_term *g_term;
static volatile sig_atomic_t g_winch;
static volatile sig_atomic_t g_cont;

static void tui_fd_write(int fd, const char *p, size_t n) {
  while (n > 0) {
    ssize_t w = write(fd, p, n);

    if (w < 0) {
      if (errno == EINTR) {
        continue;
      }

      return;
    }

    p += (size_t)w;
    n -= (size_t)w;
  }
}

#define TUI_SEQ_ENTER                                                          \
  "\033[?1049h"                                         \
  "\033[?25l"                                                \
  "\033[?2004h"                                          \
  "\033[?1003h"                                 \
  "\033[?1006h"                                       \
  "\033[2J"

#define TUI_SEQ_LEAVE                                                          \
  "\033[?1006l\033[?1003l\033[?2004l"                                           \
  "\033[0m\033[?25h"                         \
  "\033[?1049l"

static void tui_term_park_stderr(tui_term *t) {
  const char *log = getenv("LK_TUI_LOG");

  t->err_saved = -1;

  if (log && log[0]) {
    if (freopen(log, "a", stderr)) {
      setvbuf(stderr, NULL, _IONBF, 0);
    }

    return;
  }

  if (isatty(STDERR_FILENO)) {
    char path[] = "/tmp/lk-tui-XXXXXX";
    int cap = mkstemp(path);

    if (cap < 0) {
      return;
    }

    unlink(path);
    fflush(stderr);
    t->err_saved = dup(STDERR_FILENO);

    if (t->err_saved >= 0) {
      dup2(cap, STDERR_FILENO);
    }

    close(cap);
  }
}

static void tui_term_release_stderr(tui_term *t) {
  char buf[4096];
  int saved;

  if (!t || t->err_saved < 0) {
    return;
  }

  saved = t->err_saved;
  t->err_saved = -1;

  if (lseek(STDERR_FILENO, 0, SEEK_SET) == 0) {
    for (;;) {
      ssize_t r = read(STDERR_FILENO, buf, sizeof(buf));

      if (r < 0 && errno == EINTR) {
        continue;
      }

      if (r <= 0) {
        break;
      }

      tui_fd_write(saved, buf, (size_t)r);
    }
  }

  dup2(saved, STDERR_FILENO);
  close(saved);
}

static void tui_term_osc52(tui_term *t, const char *text, size_t len) {
  static const char b64[] =
      "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
  size_t out_len = 4 * ((len + 2) / 3) + 16;
  char *out;
  size_t i;
  size_t o = 0;

  if (!t || !t->raw) {
    return;
  }

  out = (char *)malloc(out_len);

  if (!out) {
    return;
  }

  memcpy(out, "\033]52;c;", 7);
  o = 7;

  for (i = 0; i + 2 < len; i += 3) {
    lk_u32 v = ((lk_u32)(unsigned char)text[i] << 16) |
               ((lk_u32)(unsigned char)text[i + 1] << 8) |
               (lk_u32)(unsigned char)text[i + 2];

    out[o++] = b64[(v >> 18) & 63];
    out[o++] = b64[(v >> 12) & 63];
    out[o++] = b64[(v >> 6) & 63];
    out[o++] = b64[v & 63];
  }

  if (i < len) {
    lk_u32 v = (lk_u32)(unsigned char)text[i] << 16;

    if (i + 1 < len) {
      v |= (lk_u32)(unsigned char)text[i + 1] << 8;
    }

    out[o++] = b64[(v >> 18) & 63];
    out[o++] = b64[(v >> 12) & 63];
    out[o++] = i + 1 < len ? b64[(v >> 6) & 63] : '=';
    out[o++] = '=';
  }

  out[o++] = '\007';
  tui_fd_write(t->fd, out, o);
  free(out);
}

static const char *tui_clipboard_get(void *ud) {
  lk_window *win = (lk_window *)ud;

  return win->clipboard ? win->clipboard : "";
}

static void tui_clipboard_set(void *ud, const char *text) {
  lk_window *win = (lk_window *)ud;
  size_t len = text ? strlen(text) : 0;

  if (len + 1 > win->clipboard_cap) {
    size_t cap = win->clipboard_cap ? win->clipboard_cap : 256;
    char *grown;

    while (cap < len + 1) {
      cap *= 2;
    }

    grown = (char *)realloc(win->clipboard, cap);

    if (!grown) {
      return;
    }

    win->clipboard = grown;
    win->clipboard_cap = cap;
  }

  if (len > 0) {
    memcpy(win->clipboard, text, len);
  }

  win->clipboard[len] = '\0';
  tui_term_osc52(win->term, win->clipboard, len);
}

static void tui_term_restore_modes(tui_term *t) {
  if (!t || !t->raw) {
    return;
  }

  if (t->kitty_pushed) {
    tui_fd_write(t->fd, "\033[<u", 4);
  }

  tui_fd_write(t->fd, TUI_SEQ_LEAVE, sizeof(TUI_SEQ_LEAVE) - 1);
  tcsetattr(t->fd, TCSAFLUSH, &t->orig);
  t->raw = 0;
}

static void tui_term_enter_modes(tui_term *t) {
  struct termios raw = t->orig;

  cfmakeraw(&raw);
  raw.c_cc[VMIN] = 0;
  raw.c_cc[VTIME] = 0;
  tcsetattr(t->fd, TCSAFLUSH, &raw);
  tui_fd_write(t->fd, TUI_SEQ_ENTER, sizeof(TUI_SEQ_ENTER) - 1);

  if (t->kitty_pushed) {
    tui_fd_write(t->fd, "\033[>3u", 5);
  }

  t->raw = 1;
  t->force = 1;
  t->cursor_shown = 0;
  t->caret_x = t->caret_y = -1;
}

static void tui_on_winch(int sig) {
  (void)sig;
  g_winch = 1;
}

static void tui_on_fatal(int sig) {
  tui_term_restore_modes(g_term);
  tui_term_release_stderr(g_term);
  signal(sig, SIG_DFL);
  raise(sig);
}

static void tui_on_tstp(int sig) {
  sigset_t m;

  tui_term_restore_modes(g_term);
  signal(sig, SIG_DFL);
  sigemptyset(&m);
  sigaddset(&m, sig);
  sigprocmask(SIG_UNBLOCK, &m, NULL);
  raise(sig);
  signal(sig, tui_on_tstp);

  if (g_term) {
    tui_term_enter_modes(g_term);
  }

  g_cont = 1;
}

static void tui_atexit(void) {
  tui_term_restore_modes(g_term);
  tui_term_release_stderr(g_term);
}

static void tui_term_signals(int install) {
  struct sigaction sa;

  memset(&sa, 0, sizeof(sa));
  sigemptyset(&sa.sa_mask);
  sa.sa_handler = install ? tui_on_winch : SIG_DFL;
  sigaction(SIGWINCH, &sa, NULL);
  sa.sa_handler = install ? tui_on_fatal : SIG_DFL;
  sigaction(SIGTERM, &sa, NULL);
  sigaction(SIGHUP, &sa, NULL);
  sigaction(SIGINT, &sa, NULL);
  sa.sa_handler = install ? tui_on_tstp : SIG_DFL;
  sigaction(SIGTSTP, &sa, NULL);
}

static int tui_term_size(lk_window *win) {
  tui_term *t = win->term;
  struct winsize ws;
  int cols = win->cols;
  int rows = win->rows;

  if (ioctl(t->fd, TIOCGWINSZ, &ws) == 0 && ws.ws_col > 0 && ws.ws_row > 0) {
    cols = ws.ws_col;
    rows = ws.ws_row;
  }

  if (cols == win->cols && rows == win->rows && t->prev) {
    return 0;
  }

  {
    size_t n = (size_t)cols * (size_t)rows;
    tui_cell *cells =
        (tui_cell *)lk_sys_alloc(NULL, (lk_u32)(sizeof(tui_cell) * n));
    tui_cell *prev =
        (tui_cell *)lk_sys_alloc(NULL, (lk_u32)(sizeof(tui_cell) * n));

    if (!cells || !prev) {
      lk_sys_dealloc(NULL, cells);
      lk_sys_dealloc(NULL, prev);
      return 0;
    }

    lk_sys_dealloc(NULL, win->cells);
    lk_sys_dealloc(NULL, t->prev);
    win->cells = cells;
    t->prev = prev;
  }

  win->cols = cols;
  win->rows = rows;
  win->width = cols * win->cw;
  win->height = rows * win->ch;
  tui_clear(win);
  t->force = 1;

  return 1;
}

static void tui_term_read(lk_window *win);
static int tui_color_depth(void);

#define TUI_OPEN_WAIT_MS 500

static tui_term *tui_term_open(lk_window *win) {
  tui_term *t;
  int fd;

  if (getenv("LK_TUI_HEADLESS")) {
    return NULL;
  }

  fd = open("/dev/tty", O_RDWR | O_NOCTTY);

  if (fd < 0) {
    return NULL;
  }

  if (!isatty(fd)) {
    close(fd);
    return NULL;
  }

  t = (tui_term *)lk_sys_alloc(NULL, (lk_u32)sizeof(tui_term));

  if (!t) {
    close(fd);
    return NULL;
  }

  memset(t, 0, sizeof(*t));
  t->fd = fd;
  t->colors = tui_color_depth();

  if (tcgetattr(fd, &t->orig) != 0) {
    lk_sys_dealloc(NULL, t);
    close(fd);
    return NULL;
  }

  tui_term_park_stderr(t);
  win->term = t;
  g_term = t;
  {
    static int atexit_done;

    if (!atexit_done) {
      atexit_done = 1;
      atexit(tui_atexit);
    }
  }
  tui_term_signals(1);
  tui_term_enter_modes(t);
  tui_term_size(win);
  tui_fd_write(fd, "\033[?u\033[c", 7);

  {
    lk_u32 start = tui_ticks_ms();

    while (!t->da1) {
      struct pollfd pfd;
      lk_u32 waited = tui_ticks_ms() - start;
      int r;

      if (waited >= TUI_OPEN_WAIT_MS) {
        break;
      }

      pfd.fd = fd;
      pfd.events = POLLIN;
      r = poll(&pfd, 1, (int)(TUI_OPEN_WAIT_MS - waited));

      if (r < 0 && errno == EINTR) {
        continue;
      }

      if (r <= 0) {
        break;
      }

      tui_term_read(win);
    }
  }

  return t;
}

static void tui_term_close(lk_window *win) {
  tui_term *t = win->term;

  if (!t) {
    return;
  }

  tui_term_restore_modes(t);
  tui_term_release_stderr(t);
  tui_term_signals(0);

  if (g_term == t) {
    g_term = NULL;
  }

  close(t->fd);
  lk_sys_dealloc(NULL, t->prev);
  free(t->paste);
  free(t->out);
  lk_sys_dealloc(NULL, t);
  win->term = NULL;
}

static void tui_out(tui_term *t, const char *p, size_t n) {
  if (t->out_len + n > t->out_cap) {
    size_t cap = t->out_cap ? t->out_cap : 4096;
    char *nb;

    while (cap < t->out_len + n) {
      cap *= 2;
    }

    nb = (char *)realloc(t->out, cap);

    if (!nb) {
      return;
    }

    t->out = nb;
    t->out_cap = cap;
  }

  memcpy(t->out + t->out_len, p, n);
  t->out_len += n;
}

static void tui_out_cstr(tui_term *t, const char *s) {
  tui_out(t, s, strlen(s));
}

static void tui_out_cup(tui_term *t, int cx, int cy) {
  char b[32];

  sprintf(b, "\033[%d;%dH", cy + 1, cx + 1);
  tui_out_cstr(t, b);
}

static int tui_color_depth(void) {
  const char *v = getenv("LK_TUI_COLOR");

  if (v && v[0]) {
    return strcmp(v, "256") == 0 ? 256 : 24;
  }

  v = getenv("COLORTERM");

  if (v && (strcmp(v, "truecolor") == 0 || strcmp(v, "24bit") == 0)) {
    return 24;
  }

  v = getenv("TERM");

  if (v && strstr(v, "direct")) {
    return 24;
  }

  return 256;
}

static int tui_cube_level(int v) {
  if (v < 48) {
    return 0;
  }

  if (v < 115) {
    return 1;
  }

  return (v - 35) / 40;
}

static int tui_xterm256(int r, int g, int b) {
  static const int lv[6] = {0, 95, 135, 175, 215, 255};
  int qr = tui_cube_level(r), qg = tui_cube_level(g), qb = tui_cube_level(b);
  int cr = lv[qr], cg = lv[qg], cb = lv[qb];
  int avg = (r + g + b) / 3;
  int gi = avg > 238 ? 23 : (avg - 3) / 10;
  int gl;
  int dc, dg;

  if (gi < 0) {
    gi = 0;
  }

  gl = 8 + 10 * gi;
  dc = (cr - r) * (cr - r) + (cg - g) * (cg - g) + (cb - b) * (cb - b);
  dg = (gl - r) * (gl - r) + (gl - g) * (gl - g) + (gl - b) * (gl - b);

  return dg < dc ? 232 + gi : 16 + 36 * qr + 6 * qg + qb;
}

static void tui_out_sgr(tui_term *t, const tui_cell *c) {
  char b[80];
  const char *ul = (c->attr & TUI_ATTR_UNDERLINE) ? ";4" : "";
  const char *rv = (c->attr & TUI_ATTR_REVERSE) ? ";7" : "";

  if (t->colors == 256) {
    sprintf(b, "\033[0%s%s;38;5;%d;48;5;%dm", ul, rv,
            tui_xterm256(c->fg.r, c->fg.g, c->fg.b),
            tui_xterm256(c->bg.r, c->bg.g, c->bg.b));
  } else {
    sprintf(b, "\033[0%s%s;38;2;%d;%d;%d;48;2;%d;%d;%dm", ul, rv, c->fg.r,
            c->fg.g, c->fg.b, c->bg.r, c->bg.g, c->bg.b);
  }

  tui_out_cstr(t, b);
}

static void tui_out_utf8(tui_term *t, lk_u32 cp) {
  char b[4];
  size_t n;

  if (cp < 0x80) {
    b[0] = (char)cp;
    n = 1;
  } else if (cp < 0x800) {
    b[0] = (char)(0xC0 | (cp >> 6));
    b[1] = (char)(0x80 | (cp & 0x3F));
    n = 2;
  } else if (cp < 0x10000) {
    b[0] = (char)(0xE0 | (cp >> 12));
    b[1] = (char)(0x80 | ((cp >> 6) & 0x3F));
    b[2] = (char)(0x80 | (cp & 0x3F));
    n = 3;
  } else {
    b[0] = (char)(0xF0 | (cp >> 18));
    b[1] = (char)(0x80 | ((cp >> 12) & 0x3F));
    b[2] = (char)(0x80 | ((cp >> 6) & 0x3F));
    b[3] = (char)(0x80 | (cp & 0x3F));
    n = 4;
  }

  tui_out(t, b, n);
}

static int tui_cell_eq(const tui_cell *a, const tui_cell *b) {
  return a->cp == b->cp && a->attr == b->attr && tui_color_eq(a->fg, b->fg) &&
         tui_color_eq(a->bg, b->bg);
}

static void tui_term_flush(lk_window *win) {
  tui_term *t = win->term;
  int cx, cy;
  int cur_x = -1;
  int cur_y = -1;
  int have_sgr = 0;
  size_t body;
  tui_cell sgr;

  t->out_len = 0;
  memset(&sgr, 0, sizeof(sgr));
  tui_out_cstr(t, "\033[?25l");

  if (t->force) {
    int n = win->cols * win->rows;
    int i;

    for (i = 0; i < n; i++) {
      t->prev[i].cp = 0xFFFFFFFFu;
    }

    tui_out_cstr(t, "\033[0m\033[2J");
    t->force = 0;
    t->caret_x = t->caret_y = -2;
  }

  body = t->out_len;

  for (cy = 0; cy < win->rows; cy++) {
    for (cx = 0; cx < win->cols; cx++) {
      int i = cy * win->cols + cx;
      const tui_cell *c = &win->cells[i];
      int w;

      if (c->cp == 0) {
        continue;
      }

      w = tui_cp_width(c->cp);

      if (w == 2 && cx + 1 >= win->cols) {
        continue;
      }

      if (tui_cell_eq(c, &t->prev[i]) &&
          (w == 1 || tui_cell_eq(&win->cells[i + 1], &t->prev[i + 1]))) {
        continue;
      }

      if (cx != cur_x || cy != cur_y) {
        tui_out_cup(t, cx, cy);
      }

      if (!have_sgr || !tui_cell_eq(&sgr, c) || sgr.cp != c->cp) {
        tui_out_sgr(t, c);
        sgr = *c;
        have_sgr = 1;
      }

      tui_out_utf8(t, c->cp);
      t->prev[i] = *c;

      if (w == 2) {
        t->prev[i + 1] = win->cells[i + 1];
      }

      cur_x = cx + w;
      cur_y = cy;

      if (cur_x >= win->cols) {
        cur_x = -1;
      }
    }
  }

  if (t->out_len == body && win->caret_cx == t->caret_x &&
      win->caret_cy == t->caret_y) {
    return;
  }

  if (t->out_len == body) {
    t->out_len = 0;
  }

  t->caret_x = win->caret_cx;
  t->caret_y = win->caret_cy;

  if (win->caret_cx >= 0) {
    tui_out_cup(t, win->caret_cx, win->caret_cy);
    tui_out_cstr(t, "\033[?25h");
    t->cursor_shown = 1;
  } else {
    t->cursor_shown = 0;
  }

  tui_fd_write(t->fd, t->out, t->out_len);
}

static void tui_push_ev(tui_term *t, const lk_event *ev) {
  if (t->evq_n < TUI_EVQ_MAX) {
    t->evq[t->evq_n++] = *ev;
  }
}

static lk_u8 tui_mods_param(int m) {
  lk_u8 r = 0;

  if (m <= 1) {
    return 0;
  }

  m -= 1;

  if (m & 1) {
    r |= (lk_u8)LK_MOD_SHIFT;
  }

  if (m & 2) {
    r |= (lk_u8)LK_MOD_ALT;
  }

  if (m & 4) {
    r |= (lk_u8)LK_MOD_CTRL;
  }

  if (m & 8) {
    r |= (lk_u8)LK_MOD_GUI;
  }

  return r;
}

static lk_u16 tui_key_from_cp(lk_u32 cp, lk_u8 *mods) {
  if (cp >= 'a' && cp <= 'z') {
    return (lk_u16)(LKK_A + (cp - 'a'));
  }

  if (cp >= 'A' && cp <= 'Z') {
    *mods |= (lk_u8)LK_MOD_SHIFT;
    return (lk_u16)(LKK_A + (cp - 'A'));
  }

  if (cp >= '0' && cp <= '9') {
    return (lk_u16)(LKK_0 + (cp - '0'));
  }

  switch (cp) {
  case ' ': return LKK_SPACE;
  case 9: return LKK_TAB;
  case 13: return LKK_RETURN;
  case 27: return LKK_ESCAPE;
  case 127: return LKK_BACKSPACE;
  case 8: return LKK_BACKSPACE;
  default: return LKK_UNKNOWN;
  }
}

static void tui_ev_key(tui_term *t, lk_u8 type, lk_u16 kc, lk_u8 mods,
                       int repeat) {
  lk_event ev;

  if (kc == LKK_UNKNOWN) {
    return;
  }

  memset(&ev, 0, sizeof(ev));
  ev.type = type;
  ev.mods = mods;
  ev.data.key.keycode = kc;
  ev.data.key.repeat = (lk_u8)(repeat ? 1 : 0);
  tui_push_ev(t, &ev);
}

static void tui_ev_text(tui_term *t, const unsigned char *p, lk_u32 n) {
  lk_event ev;

  if (n == 0 || n > 31) {
    return;
  }

  memset(&ev, 0, sizeof(ev));
  ev.type = LK_EVENT_TEXT;
  memcpy(ev.data.text.buf, p, n);
  ev.data.text.len = (lk_u8)n;
  tui_push_ev(t, &ev);
}

static lk_u16 tui_key_tilde(int n) {
  switch (n) {
  case 1:
  case 7: return LKK_HOME;
  case 3: return LKK_DELETE;
  case 4:
  case 8: return LKK_END;
  case 5: return LKK_PAGEUP;
  case 6: return LKK_PAGEDOWN;
  case 11: return LKK_F1;
  case 12: return LKK_F2;
  case 13: return LKK_F3;
  case 14: return LKK_F4;
  case 15: return LKK_F5;
  case 17: return LKK_F6;
  case 18: return LKK_F7;
  case 19: return LKK_F8;
  case 20: return LKK_F9;
  case 21: return LKK_F10;
  case 23: return LKK_F11;
  case 24: return LKK_F12;
  default: return LKK_UNKNOWN;
  }
}

static lk_u16 tui_key_final(unsigned char f) {
  switch (f) {
  case 'A': return LKK_UP;
  case 'B': return LKK_DOWN;
  case 'C': return LKK_RIGHT;
  case 'D': return LKK_LEFT;
  case 'H': return LKK_HOME;
  case 'F': return LKK_END;
  case 'P': return LKK_F1;
  case 'Q': return LKK_F2;
  case 'R': return LKK_F3;
  case 'S': return LKK_F4;
  default: return LKK_UNKNOWN;
  }
}

static int tui_csi_params(const unsigned char *p, lk_u32 n, int *nums, int *sub,
                          int max) {
  int count = 0;
  int cur = 0;
  int cur_sub = 0;
  int in_sub = 0;
  int have = 0;
  lk_u32 i;

  for (i = 0; i <= n; i++) {
    unsigned char c = i < n ? p[i] : ';';

    if (c >= '0' && c <= '9') {
      if (in_sub) {
        cur_sub = cur_sub * 10 + (c - '0');
      } else {
        cur = cur * 10 + (c - '0');
      }

      have = 1;
    } else if (c == ':') {
      in_sub = 1;
    } else if (c == ';') {
      if (count < max) {
        nums[count] = have ? cur : 0;
        sub[count] = cur_sub;
        count++;
      }

      cur = cur_sub = in_sub = have = 0;
    }
  }

  return count;
}

static void tui_ev_pointer(tui_term *t, lk_u8 type, lk_i32 x, lk_i32 y,
                           lk_u8 button, lk_u8 mods) {
  lk_event ev;

  memset(&ev, 0, sizeof(ev));
  ev.type = type;
  ev.mods = mods;
  ev.data.pointer.x = x;
  ev.data.pointer.y = y;
  ev.data.pointer.button = button;
  tui_push_ev(t, &ev);
}

static void tui_csi_mouse(lk_window *win, const unsigned char *p, lk_u32 n,
                          unsigned char final) {
  tui_term *t = win->term;
  int nums[3] = {0, 0, 0};
  int sub[3];
  int b, cxp, cyp;
  lk_i32 x, y;
  lk_u8 mods = 0;

  if (tui_csi_params(p, n, nums, sub, 3) < 3) {
    return;
  }

  b = nums[0];
  cxp = nums[1] - 1;
  cyp = nums[2] - 1;
  x = (lk_i32)cxp * win->cw + (win->cw - 1) / 2;
  y = (lk_i32)cyp * win->ch + win->ch / 2;

  if (b & 4) {
    mods |= (lk_u8)LK_MOD_SHIFT;
  }

  if (b & 8) {
    mods |= (lk_u8)LK_MOD_ALT;
  }

  if (b & 16) {
    mods |= (lk_u8)LK_MOD_CTRL;
  }

  t->mouse_x = x;
  t->mouse_y = y;

  if (b & 64) {
    lk_event ev;

    if (final != 'M') {
      return;
    }

    memset(&ev, 0, sizeof(ev));
    ev.type = LK_EVENT_WHEEL;
    ev.mods = mods;

    switch (b & 3) {
    case 0: ev.data.wheel.dy = 1; break;
    case 1: ev.data.wheel.dy = -1; break;
    case 2: ev.data.wheel.dx = -1; break;
    default: ev.data.wheel.dx = 1; break;
    }

    tui_push_ev(t, &ev);
    return;
  }

  if (b & 32) {
    tui_ev_pointer(t, LK_EVENT_POINTER_MOVE, x, y, 0, mods);
    return;
  }

  {
    lk_u8 button;

    switch (b & 3) {
    case 0: button = (lk_u8)LK_POINTER_BUTTON_PRIMARY; break;
    case 1: button = (lk_u8)LK_POINTER_BUTTON_MIDDLE; break;
    case 2: button = (lk_u8)LK_POINTER_BUTTON_SECONDARY; break;
    default: return;
    }

    tui_ev_pointer(t,
                   final == 'M' ? LK_EVENT_POINTER_DOWN : LK_EVENT_POINTER_UP,
                   x, y, button, mods);
  }
}

static void tui_csi(lk_window *win, const unsigned char *p, lk_u32 n,
                    unsigned char final) {
  tui_term *t = win->term;
  int nums[4] = {0, 0, 0, 0};
  int sub[4] = {0, 0, 0, 0};
  int count;

  if (n > 0 && p[0] == '<' && (final == 'M' || final == 'm')) {
    tui_csi_mouse(win, p + 1, n - 1, final);
    return;
  }

  if (n > 0 && p[0] == '?' && final == 'u') {
    if (!t->kitty_pushed) {
      t->kitty = 1;
      t->kitty_pushed = 1;
      tui_fd_write(t->fd, "\033[>3u", 5);
    }

    return;
  }

  if (n > 0 && p[0] == '?' && final == 'c') {
    t->da1 = 1;
    return;
  }

  if (n > 0 && (p[0] == '?' || p[0] == '>' || p[0] == '=')) {
    return;
  }

  count = tui_csi_params(p, n, nums, sub, 4);

  switch (final) {
  case '~': {
    lk_u8 mods = tui_mods_param(count > 1 ? nums[1] : 0);

    if (nums[0] == 200) {
      t->in_paste = 1;
      t->paste_len = 0;
      return;
    }

    if (nums[0] == 201) {
      return;
    }

    tui_ev_key(t, LK_EVENT_KEY_DOWN, tui_key_tilde(nums[0]), mods, 0);
    return;
  }

  case 'u': {
    lk_u8 mods = tui_mods_param(count > 1 ? nums[1] : 0);
    int type = count > 1 ? sub[1] : 1;
    lk_u16 kc = tui_key_from_cp((lk_u32)nums[0], &mods);

    if (type == 3) {
      tui_ev_key(t, LK_EVENT_KEY_UP, kc, mods, 0);
    } else {
      tui_ev_key(t, LK_EVENT_KEY_DOWN, kc, mods, type == 2);
    }

    return;
  }

  case 'Z':
    tui_ev_key(t, LK_EVENT_KEY_DOWN, LKK_TAB, (lk_u8)LK_MOD_SHIFT, 0);
    return;

  case 'A':
  case 'B':
  case 'C':
  case 'D':
  case 'H':
  case 'F':
  case 'P':
  case 'Q':
  case 'R':
  case 'S': {
    lk_u8 mods = tui_mods_param(count > 1 ? nums[1] : 0);
    int type = count > 1 ? sub[1] : 1;
    lk_u16 kc = tui_key_final(final);

    if (final == 'R' && count >= 2 && sub[1] == 0 && nums[0] != 1) {
      return;
    }

    if (type == 3) {
      tui_ev_key(t, LK_EVENT_KEY_UP, kc, mods, 0);
    } else {
      tui_ev_key(t, LK_EVENT_KEY_DOWN, kc, mods, type == 2);
    }

    return;
  }

  default: return;
  }
}

static void tui_paste_append(tui_term *t, const unsigned char *p, lk_u32 n) {
  lk_u32 i;

  if (t->paste_len + n + 1 > t->paste_cap) {
    lk_u32 cap = t->paste_cap ? t->paste_cap : 1024;
    char *nb;

    while (cap < t->paste_len + n + 1) {
      cap *= 2;
    }

    nb = (char *)realloc(t->paste, cap);

    if (!nb) {
      return;
    }

    t->paste = nb;
    t->paste_cap = cap;
  }

  for (i = 0; i < n; i++) {
    unsigned char c = p[i];

    if (c == '\r') {
      if (i + 1 < n && p[i + 1] == '\n') {
        continue;
      }

      c = '\n';
    }

    t->paste[t->paste_len++] = (char)c;
  }
}

static lk_u32 tui_parse_one(lk_window *win, lk_u32 now_ms) {
  tui_term *t = win->term;
  const unsigned char *in = t->in;
  lk_u32 n = t->in_len;
  unsigned char b = in[0];

  if (t->in_paste) {
    static const unsigned char end[] = "\033[201~";
    lk_u32 i;

    for (i = 0; i < n; i++) {
      if (in[i] == 0x1B) {
        if (n - i < sizeof(end) - 1) {
          tui_paste_append(t, in, i);
          return i;
        }

        if (memcmp(in + i, end, sizeof(end) - 1) == 0) {
          lk_event ev;

          tui_paste_append(t, in, i);
          t->in_paste = 0;
          memset(&ev, 0, sizeof(ev));
          ev.type = LK_EVENT_PASTE;
          ev.data.paste.ptr = t->paste;
          ev.data.paste.len = t->paste_len;
          tui_push_ev(t, &ev);

          return i + (lk_u32)(sizeof(end) - 1);
        }
      }
    }

    tui_paste_append(t, in, n);
    return n;
  }

  if (b == 0x1B) {
    if (n == 1) {
      if (t->esc_since_ms == 0) {
        t->esc_since_ms = now_ms ? now_ms : 1;
        return 0;
      }

      if (now_ms - t->esc_since_ms < TUI_ESC_WAIT_MS) {
        return 0;
      }

      t->esc_since_ms = 0;
      tui_ev_key(t, LK_EVENT_KEY_DOWN, LKK_ESCAPE, 0, 0);
      return 1;
    }

    t->esc_since_ms = 0;

    if (in[1] == '[') {
      lk_u32 i = 2;

      while (i < n && in[i] >= 0x20 && in[i] <= 0x3F) {
        i++;
      }

      if (i >= n) {
        return n > 64 ? 2 : 0;
      }

      if (in[i] < 0x40 || in[i] > 0x7E) {
        return 2;
      }

      tui_csi(win, in + 2, i - 2, in[i]);
      return i + 1;
    }

    if (in[1] == 'O') {
      if (n < 3) {
        return 0;
      }

      tui_ev_key(t, LK_EVENT_KEY_DOWN, tui_key_final(in[2]), 0, 0);
      return 3;
    }

    if (in[1] == 0x1B) {
      tui_ev_key(t, LK_EVENT_KEY_DOWN, LKK_ESCAPE, 0, 0);
      return 1;
    }

    {
      lk_u8 mods = (lk_u8)LK_MOD_ALT;
      lk_u16 kc = tui_key_from_cp(in[1], &mods);

      tui_ev_key(t, LK_EVENT_KEY_DOWN, kc, mods, 0);
      return 2;
    }
  }

  if (b < 0x20 || b == 0x7F) {
    lk_u8 mods = 0;
    lk_u16 kc;

    switch (b) {
    case 0x09: kc = LKK_TAB; break;
    case 0x0D:
    case 0x0A: kc = LKK_RETURN; break;
    case 0x08:
    case 0x7F: kc = LKK_BACKSPACE; break;
    case 0x00:
      kc = LKK_SPACE;
      mods = (lk_u8)LK_MOD_CTRL;
      break;
    default:
      if (b >= 0x01 && b <= 0x1A) {
        kc = (lk_u16)(LKK_A + (b - 1));
        mods = (lk_u8)LK_MOD_CTRL;
      } else {
        kc = LKK_UNKNOWN;
      }

      break;
    }

    tui_ev_key(t, LK_EVENT_KEY_DOWN, kc, mods, 0);
    return 1;
  }

  {
    lk_u32 len = b < 0x80             ? 1
                 : (b & 0xE0) == 0xC0 ? 2
                 : (b & 0xF0) == 0xE0 ? 3
                                      : 4;
    lk_u32 cp;
    lk_u8 mods = 0;

    if (len > n) {
      return 0;
    }

    tui_decode((const char *)in, len, 0, &cp);
    tui_ev_key(t, LK_EVENT_KEY_DOWN, tui_key_from_cp(cp, &mods), mods, 0);
    tui_ev_text(t, in, len);

    return len;
  }
}

static void tui_term_read(lk_window *win) {
  tui_term *t = win->term;
  lk_u32 now = tui_ticks_ms();

  for (;;) {
    ssize_t r;

    if (t->in_len >= TUI_IN_MAX) {
      break;
    }

    r = read(t->fd, t->in + t->in_len, TUI_IN_MAX - t->in_len);

    if (r > 0) {
      t->in_len += (lk_u32)r;
      continue;
    }

    if (r < 0 && errno == EINTR) {
      continue;
    }

    break;
  }

  while (t->in_len > 0 && t->evq_n < TUI_EVQ_MAX) {
    lk_u32 used = tui_parse_one(win, now);

    if (used == 0) {
      break;
    }

    if (used >= t->in_len) {
      t->in_len = 0;
    } else {
      memmove(t->in, t->in + used, t->in_len - used);
      t->in_len -= used;
    }
  }
}

static void tui_dispatch(lk_window *win, const lk_tree *cur,
                         const lk_layout_cfg *lcfg, lk_event *ev) {
  tui_term *t = win->term;

  if (ev->type == LK_EVENT_POINTER_MOVE || ev->type == LK_EVENT_POINTER_DOWN ||
      ev->type == LK_EVENT_POINTER_UP) {
    int captured = 0;

    if (ev->type != LK_EVENT_POINTER_DOWN) {
      lk_node_id cap = lk_capture_current(win->ui);

      if (cap != 0) {
        lk_ix cix = lk_tree_find_by_id(cur, cap);

        if (cix != 0) {
          ev->target = cix;
          captured = 1;
        } else {
          lk_capture_clear(win->ui);
        }
      }
    }

    if (!captured) {
      ev->target = lk_hit_test_overlay(win->ui, win->rects, lcfg,
                                       ev->data.pointer.x, ev->data.pointer.y);

      if (ev->target == 0) {
        ev->target = lk_hit_test(cur, win->rects, ev->data.pointer.x,
                                 ev->data.pointer.y);
      }

      if (ev->type == LK_EVENT_POINTER_DOWN &&
          lk_overlay_dismiss_outside(win->ui, win->rects, lcfg,
                                     ev->data.pointer.x, ev->data.pointer.y) ==
              LK_DISMISS_BLOCKED) {
        return;
      }

      if (ev->target != 0) {
        lk_hover_set(win->ui, cur->nodes[ev->target].id);
      } else {
        lk_hover_clear(win->ui);
      }
    }
  } else if (ev->type == LK_EVENT_WHEEL) {
    ev->target =
        lk_hit_test_overlay(win->ui, win->rects, lcfg, t->mouse_x, t->mouse_y);

    if (ev->target == 0) {
      ev->target = lk_hit_test(cur, win->rects, t->mouse_x, t->mouse_y);
    }
  } else if (ev->type == LK_EVENT_KEY_DOWN || ev->type == LK_EVENT_KEY_UP ||
             ev->type == LK_EVENT_TEXT || ev->type == LK_EVENT_PASTE) {
    ev->target = lk_focus_current(win->ui, cur);

    if (ev->target == 0) {
      ev->target = cur->root;
    }
  } else if (ev->type == LK_EVENT_WINDOW_RESIZE) {
    ev->target = cur->root;
  }

  if (ev->target != 0) {
    lk_event_route(win->ui, ev);
  }

  if (!ev->handled) {
    if (ev->type == LK_EVENT_KEY_DOWN && ev->data.key.keycode == LKK_TAB) {
      if (ev->mods & (lk_u8)LK_MOD_SHIFT) {
        lk_focus_prev(win->ui, cur);
      } else {
        lk_focus_next(win->ui, cur);
      }
    }

    if (ev->type == LK_EVENT_KEY_DOWN && ev->data.key.keycode == LKK_Z &&
        ev->mods == (lk_u8)LK_MOD_CTRL) {
      raise(SIGTSTP);
    }

    if (ev->type == LK_EVENT_POINTER_DOWN && ev->target != 0) {
      lk_focus_set(win->ui, cur, cur->nodes[ev->target].id);
    }
  }
}

static void tui_term_input(lk_window *win, const lk_tree *cur,
                           const lk_layout_cfg *lcfg, int resized) {
  tui_term *t = win->term;
  int i;

  if (resized) {
    lk_event ev;

    memset(&ev, 0, sizeof(ev));
    ev.type = LK_EVENT_WINDOW_RESIZE;
    ev.data.window.w = win->width;
    ev.data.window.h = win->height;
    tui_dispatch(win, cur, lcfg, &ev);
  }

  tui_term_read(win);

  for (i = 0; i < t->evq_n; i++) {
    tui_dispatch(win, cur, lcfg, &t->evq[i]);
  }

  t->evq_n = 0;
}

lk_window *lk_window_create(const lk_window_cfg *cfg) {
  lk_window *win;
  int w;
  int h;

  if (!cfg) {
    return NULL;
  }

  w = cfg->width > 0 ? cfg->width : 800;
  h = cfg->height > 0 ? cfg->height : 600;

  win = (lk_window *)lk_sys_alloc(NULL, (lk_u32)sizeof(lk_window));

  if (!win) {
    return NULL;
  }

  memset(win, 0, sizeof(*win));

  win->cw = 8;
  win->ch = 16;
  tui_env_pair("LK_TUI_CELL", &win->cw, &win->ch);

  win->cols = w / win->cw;
  win->rows = h / win->ch;
  tui_env_pair("LK_TUI_SIZE", &win->cols, &win->rows);

  if (win->cols < 1) {
    win->cols = 1;
  }

  if (win->rows < 1) {
    win->rows = 1;
  }

  win->width = win->cols * win->cw;
  win->height = win->rows * win->ch;

  win->cells = (tui_cell *)lk_sys_alloc(
      NULL, (lk_u32)(sizeof(tui_cell) * (size_t)win->cols * (size_t)win->rows));

  if (!win->cells) {
    lk_sys_dealloc(NULL, win);
    return NULL;
  }

  tui_clear(win);

  win->tb.ud = win;
  win->tb.measure = tui_measure;
  win->tb.x_from_index = tui_x_from_index;
  win->tb.index_from_x = tui_index_from_x;
  win->tb.line_height = tui_line_height;
  win->tb.register_font = tui_register_font;
  win->next_font = 1;

  win->running = 1;
  win->ui = lk_ui_create(NULL);

  if (!win->ui) {
    lk_sys_dealloc(NULL, win->cells);
    lk_sys_dealloc(NULL, win);
    return NULL;
  }

  lk_ui_set_text_backend(win->ui, &win->tb);
  lk_ui_set_clipboard(win->ui, tui_clipboard_get, tui_clipboard_set, win);
  win->snap =
      !(getenv("LK_TUI_SNAP") && strcmp(getenv("LK_TUI_SNAP"), "0") == 0);

  {
    const char *tv = getenv("LK_TUI_THEME");

    if (!(tv && strcmp(tv, "none") == 0) && lk_ui_theme(win->ui)) {
      tui_theme_rules(lk_ui_theme(win->ui));
    }
  }

  tui_term_open(win);

  return win;
}

void lk_window_destroy(lk_window *win) {
  if (!win) {
    return;
  }

  tui_term_close(win);
  lk_render_list_destroy(&win->rl);

  if (win->ui) {
    lk_ui_destroy(win->ui);
  }

  free(win->shot_path);
  lk_sys_dealloc(NULL, win->styles);
  lk_sys_dealloc(NULL, win->cells);
  free(win->clipboard);
  lk_sys_dealloc(NULL, win);
}

static void tui_deliver_dialogs(lk_window *win) {
  int i;
  int n = win->dialog_count;
  tui_dialog pending[TUI_DIALOG_MAX];

  if (n == 0) {
    return;
  }

  memcpy(pending, win->dialogs, sizeof(pending));
  win->dialog_count = 0;

  for (i = 0; i < n; i++) {
    pending[i].fn(pending[i].ud, NULL, -1);
  }
}

static void tui_present(lk_window *win) {
  tui_service_screenshot(win);

  if (win->term) {
    tui_term_flush(win);
  }
}

int lk_window_step(lk_window *win, lk_frame_fn frame, void *ud) {
  lk_tree *tree;
  const lk_tree *cur;
  const lk_style *styles;
  lk_layout_cfg lcfg;
  int resized = 0;

  if (!win || !frame || !win->running) {
    return 0;
  }

  win->frame_serial++;

  if (win->term) {
    if (g_cont) {
      g_cont = 0;
      win->term->force = 1;
    }

    if (g_winch) {
      g_winch = 0;
      resized = tui_term_size(win);
    }
  }

  tui_deliver_dialogs(win);

  lk_ui_clear_commands(win->ui);
  lk_ui_set_time_ms(win->ui, tui_ticks_ms());

  tree = lk_ui_begin_frame(win->ui);
  frame(tree, ud);
  lk_ui_end_frame(win->ui);
  cur = lk_ui_tree(win->ui);

  lk_ui_flush_events(win->ui, cur);

  if (!cur || cur->root == 0) {
    tui_clear(win);
    tui_present(win);

    return win->running;
  }

  lk_ui_resolve_styles(win->ui);
  styles = tui_snap_styles(win, cur);

  win->rects = lk_ui_rects(win->ui);

  if (!win->rects) {
    tui_clear(win);
    tui_present(win);

    return win->running;
  }

  memset(&lcfg, 0, sizeof(lcfg));
  lcfg.text = &win->tb;
  lk_ui_set_text_backend(win->ui, lcfg.text);
  lcfg.viewport_w = win->width;
  lcfg.viewport_h = win->height;
  lcfg.styles = styles;
  lcfg.state = lk_ui_state(win->ui);
  lcfg.geom = lk_ui_geom(win->ui);

  if (win->snap) {
    lcfg.grid_w = win->cw;
    lcfg.grid_h = win->ch;
  }

  if (!lk_layout(cur, &lcfg, win->rects)) {
    tui_clear(win);
    tui_present(win);

    return win->running;
  }

  if (win->term) {
    tui_term_input(win, cur, &lcfg, resized);
    lk_ui_flush_events(win->ui, cur);

    if (!win->running) {
      return 0;
    }
  }

  lk_render_build(cur, win->rects, styles, lk_ui_state(win->ui), lcfg.geom,
                  &win->rl);
  lk_render_build_overlays(win->ui, win->rects, &lcfg, &win->rl);

  if (getenv("LK_TUI_DUMP")) {
    tui_dump_render_list(win, cur);
  }

  tui_paint(win, cur);
  tui_present(win);

  return win->running;
}

void lk_window_run(lk_window *win, lk_frame_fn frame, void *ud) {
  struct timespec pace;

  if (!win || !frame) {
    return;
  }

  win->running = 1;
  pace.tv_sec = 0;
  pace.tv_nsec = 16L * 1000L * 1000L;

  while (lk_window_step(win, frame, ud)) {
    if (win->term) {
      struct pollfd pfd;

      pfd.fd = win->term->fd;
      pfd.events = POLLIN;
      pfd.revents = 0;
      poll(&pfd, 1, win->term->esc_since_ms ? TUI_ESC_WAIT_MS : 33);
    } else {
      nanosleep(&pace, NULL);
    }
  }
}

lk_ui *lk_window_ui(lk_window *win) {
  return win ? win->ui : NULL;
}

lk_u32 lk_window_caps(const lk_window *win) {
  lk_u32 caps = 0;

  if (win && win->term) {
    caps |= LK_WCAP_POINTER;

    caps |= LK_WCAP_CLIPBOARD;

    if (win->term->kitty) {
      caps |= LK_WCAP_KEY_UP | LK_WCAP_KEY_CHORDS;
    }
  }

  return caps;
}

void lk_window_set_event_handler(lk_window *win, lk_event_handler_fn fn,
                                 void *ud) {
  if (win && win->ui) {
    lk_ui_set_event_handler(win->ui, fn, ud);
  }
}

lk_u16 lk_window_register_font(lk_window *win, const char *path) {
  if (!win) {
    return 0;
  }

  return win->tb.register_font(win, path);
}

void lk_window_stop(lk_window *win) {
  if (win) {
    win->running = 0;
  }
}

int lk_window_request_screenshot(lk_window *win, const char *path) {
  char *copy;
  size_t n;

  if (!win || !path) {
    return 0;
  }

  n = strlen(path);
  copy = (char *)malloc(n + 1);

  if (!copy) {
    return 0;
  }

  memcpy(copy, path, n + 1);
  free(win->shot_path);
  win->shot_path = copy;

  return 1;
}

int lk_window_set_icon(lk_window *win, const char *path) {
  (void)win;
  (void)path;

  return 0;
}

int lk_window_set_icon_mem(lk_window *win, const void *data, lk_u32 len) {
  (void)win;
  (void)data;
  (void)len;

  return 0;
}

int lk_window_set_icon_rgba(lk_window *win, int w, int h, const void *pixels,
                            int pitch) {
  (void)win;
  (void)w;
  (void)h;
  (void)pixels;
  (void)pitch;

  return 0;
}

lk_image *lk_image_load_file(const char *path) {
  (void)path;

  return NULL;
}

lk_image *lk_image_load_mem(const void *data, lk_u32 len) {
  (void)data;
  (void)len;

  return NULL;
}

int lk_image_save_bmp(const lk_image *img, const char *path) {
  (void)img;
  (void)path;

  return 0;
}

int lk_image_save_png(const lk_image *img, const char *path) {
  (void)img;
  (void)path;

  return 0;
}

static void tui_queue_dialog(lk_window *win, lk_file_dialog_fn fn, void *ud) {
  if (!win || !fn) {
    return;
  }

  if (win->dialog_count < TUI_DIALOG_MAX) {
    win->dialogs[win->dialog_count].fn = fn;
    win->dialogs[win->dialog_count].ud = ud;
    win->dialog_count++;
  } else {
    fn(ud, NULL, -1);
  }
}

void lk_window_open_file_dialog(lk_window *win,
                                const lk_file_dialog_filter *filters,
                                int nfilters, const char *default_location,
                                int allow_many, lk_file_dialog_fn fn,
                                void *ud) {
  (void)filters;
  (void)nfilters;
  (void)default_location;
  (void)allow_many;
  tui_queue_dialog(win, fn, ud);
}

void lk_window_save_file_dialog(lk_window *win,
                                const lk_file_dialog_filter *filters,
                                int nfilters, const char *default_location,
                                lk_file_dialog_fn fn, void *ud) {
  (void)filters;
  (void)nfilters;
  (void)default_location;
  tui_queue_dialog(win, fn, ud);
}
