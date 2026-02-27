/*
 * lk-menu.c -- the context-menu popup (docs/context-menu.md sections
 * 2, 4, 5).
 *
 * A menu is LK_OVERLAY_CONTEXT_MENU with content_root_id == 0: its
 * items are copied onto the ui when it opens (explicit items, or the
 * producer's candidates), the popup is laid out through
 * lk_anchor_resolve like every overlay, and it is gone whenever no
 * such overlay is on the stack -- ESC and outside clicks pop it
 * through the generic overlay paths without telling this file, so
 * every entry point re-derives "open" from the stack.  The menu is
 * not a node: it takes no focus; keys reach it through the event
 * pre-step while it is topmost.
 */

#include <string.h>

#include "lk-memory.h"
#include "lk-menu.h"
#include <lk.h>

/* ---- state ---- */

static struct lk_menu_state *menu_state(lk_ui *ui) {
  if (!ui->menu) {
    ui->menu = (struct lk_menu_state *)ui->alloc(
        ui->alloc_ud, (lk_u32)sizeof(struct lk_menu_state));

    if (ui->menu) {
      memset(ui->menu, 0, sizeof(*ui->menu));
      ui->menu->hover = -1;
    }
  }

  return ui->menu;
}

void lk_menu_ui_destroy(lk_ui *ui) {
  if (ui && ui->menu) {
    if (ui->menu->items) {
      ui->dealloc(ui->alloc_ud, ui->menu->items);
    }

    ui->dealloc(ui->alloc_ud, ui->menu);
    ui->menu = NULL;
  }
}

const lk_overlay *lk_menu_overlay(const lk_ui *ui) {
  lk_u32 i;

  if (!ui) {
    return NULL;
  }

  i = ui->overlay_count;

  while (i > 0) {
    const lk_overlay *ov = &ui->overlays[--i];

    if (ov->kind == LK_OVERLAY_CONTEXT_MENU && ov->content_root_id == 0) {
      return ov;
    }
  }

  return NULL;
}

int lk_menu_is_open(const lk_ui *ui) {
  return lk_menu_overlay(ui) != NULL && ui->menu != NULL && ui->menu->count > 0;
}

lk_u32 lk_menu_count(const lk_ui *ui) {
  return lk_menu_is_open(ui) ? ui->menu->count : 0;
}

const lk_menu_item *lk_menu_item_get(const lk_ui *ui, lk_u32 i) {
  if (!lk_menu_is_open(ui) || i >= ui->menu->count) {
    return NULL;
  }

  return &ui->menu->items[i];
}

lk_i32 lk_menu_hover(const lk_ui *ui) {
  return lk_menu_is_open(ui) ? ui->menu->hover : -1;
}

static int item_choosable(const lk_menu_item *it) {
  return it->enabled && !it->separator;
}

void lk_menu_set_hover(lk_ui *ui, lk_i32 i) {
  if (!lk_menu_is_open(ui)) {
    return;
  }

  if (i < 0 || (lk_u32)i >= ui->menu->count ||
      !item_choosable(&ui->menu->items[i])) {
    ui->menu->hover = -1;
  } else {
    ui->menu->hover = i;
  }
}

lk_rect lk_menu_rect(const lk_ui *ui) {
  lk_rect z;

  memset(&z, 0, sizeof(z));

  if (!lk_menu_is_open(ui) || !ui->menu->placed) {
    return z;
  }

  return ui->menu->rect;
}

/* Pop the menu overlay when it is on the stack (topmost or not: a menu
 * is dismissible, so anything above it means it should already be
 * gone -- pop down to it). */
void lk_menu_close(lk_ui *ui) {
  while (lk_menu_overlay(ui)) {
    lk_overlay_pop(ui);
  }

  if (ui && ui->menu) {
    ui->menu->count = 0;
    ui->menu->hover = -1;
    ui->menu->placed = 0;
  }
}

/* ---- opening ---- */

static int menu_reserve(lk_ui *ui, struct lk_menu_state *m, lk_u32 n) {
  lk_menu_item *ni;

  if (n <= m->cap) {
    return 1;
  }

  ni = (lk_menu_item *)ui->alloc(ui->alloc_ud,
                                 (lk_u32)(sizeof(lk_menu_item) * n));

  if (!ni) {
    return 0;
  }

  if (m->items) {
    ui->dealloc(ui->alloc_ud, m->items);
  }

  m->items = ni;
  m->cap = n;

  return 1;
}

int lk_menu_open(lk_ui *ui, lk_node_id owner, lk_u8 anchor, lk_i32 x,
                 lk_i32 y, const lk_menu_item *items, lk_u32 n) {
  struct lk_menu_state *m;
  lk_overlay ov;
  lk_u32 i;

  if (!ui || !items || n == 0) {
    return 0;
  }

  if (n > LK_MENU_MAX_ITEMS) {
    n = LK_MENU_MAX_ITEMS;
  }

  m = menu_state(ui);

  if (!m || !menu_reserve(ui, m, n)) {
    return 0;
  }

  lk_menu_close(ui);
  memcpy(m->items, items, sizeof(lk_menu_item) * n);
  m->count = n;
  m->owner = owner;
  m->placed = 0;
  m->hover = -1;

  for (i = 0; i < n; i++) {
    if (item_choosable(&m->items[i])) {
      m->hover = (lk_i32)i;
      break;
    }
  }

  memset(&ov, 0, sizeof(ov));
  ov.kind = LK_OVERLAY_CONTEXT_MENU;
  ov.anchor_mode = anchor;
  ov.dismiss_on_outside = 1;
  ov.traps_focus = 0;
  ov.owner_id = owner;
  ov.content_root_id = 0;
  ov.offset.x = x;
  ov.offset.y = y;

  if (!lk_overlay_push(ui, &ov)) {
    m->count = 0;
    return 0;
  }

  return 1;
}

lk_u32 lk_menu_open_context(lk_ui *ui, const lk_tree *t, lk_i32 x,
                            lk_i32 y) {
  lk_menu_item items[LK_MENU_MAX_ITEMS];
  lk_rect *rects;
  lk_ix target;
  lk_u32 n;

  if (!ui || !t) {
    return 0;
  }

  rects = lk_ui_rects(ui);

  if (!rects) {
    return 0;
  }

  target = lk_hit_test(t, rects, x, y);

  if (target == 0) {
    return 0;
  }

  n = lk_menu_candidates(ui, t, target, x, y, items, LK_MENU_MAX_ITEMS);

  if (n == 0) {
    return 0;
  }

  if (!lk_menu_open(ui, t->nodes[target].id, LK_ANCHOR_AT_CURSOR, x, y, items,
                    n)) {
    return 0;
  }

  return n;
}

lk_u32 lk_menu_open_context_at_focus(lk_ui *ui, const lk_tree *t) {
  lk_rect r;
  lk_ix target;

  if (!ui || !t || ui->focused_id == 0 || !lk_node_rect(ui, ui->focused_id, &r)) {
    return 0;
  }

  target = lk_tree_find_by_id(t, ui->focused_id);

  if (target == 0) {
    return 0;
  }

  {
    lk_menu_item items[LK_MENU_MAX_ITEMS];
    lk_i32 cx = r.x + r.w / 2;
    lk_i32 cy = r.y + r.h / 2;
    lk_u32 n = lk_menu_candidates(ui, t, target, cx, cy, items,
                                  LK_MENU_MAX_ITEMS);

    if (n == 0 || !lk_menu_open(ui, ui->focused_id, LK_ANCHOR_AT_CURSOR, cx,
                                cy, items, n)) {
      return 0;
    }

    return n;
  }
}

/* ---- geometry ---- */

static const lk_text_backend *menu_backend(const lk_ui *ui,
                                           const lk_layout_cfg *cfg) {
  if (cfg && cfg->text) {
    return cfg->text;
  }

  if (ui && ui->text) {
    return ui->text;
  }

  return lk_text_backend_stub();
}

static const lk_style *root_style(const lk_tree *t, const lk_layout_cfg *cfg) {
  if (cfg && cfg->styles && t && t->root != 0) {
    return &cfg->styles[t->root];
  }

  return NULL;
}

static lk_i32 text_w(const lk_text_backend *tb, const lk_style *st,
                     const lk_intern *it, lk_u32 sid) {
  lk_text_metrics m;
  lk_str s;

  if (sid == 0 || !tb) {
    return 0;
  }

  s = lk_intern_str(it, sid);
  m.w = 0;
  m.h = 0;
  m.baseline = 0;
  tb->measure(tb->ud, s, st ? (lk_u16)st->font_id : 0,
              st ? (lk_u16)st->font_size : 0, &m);

  return m.w;
}

lk_rect lk_menu_layout(lk_ui *ui, const lk_overlay *ov, const lk_rect *rects,
                       const lk_layout_cfg *cfg) {
  struct lk_menu_state *m = ui ? ui->menu : NULL;
  const lk_tree *t = lk_ui_tree(ui);
  const lk_text_backend *tb = menu_backend(ui, cfg);
  const lk_style *st = root_style(t, cfg);
  lk_i32 lh;
  lk_i32 label_w = 0;
  lk_i32 accel_w = 0;
  lk_i32 h = 0;
  lk_i32 w;
  lk_rect owner_rect;
  lk_ix oix;
  lk_u32 i;

  memset(&owner_rect, 0, sizeof(owner_rect));

  if (!m || m->count == 0 || !t) {
    return owner_rect;
  }

  lh = tb->line_height(tb->ud, st ? (lk_u16)st->font_id : 0,
                       st ? (lk_u16)st->font_size : 0);

  if (lh <= 0) {
    lh = 16;
  }

  m->row_h = lh + LK_MENU_PAD_Y * 2;

  for (i = 0; i < m->count; i++) {
    const lk_menu_item *it = &m->items[i];
    lk_i32 lw, aw;

    if (it->separator) {
      h += m->row_h / 2;
      continue;
    }

    lw = text_w(tb, st, ui->intern, it->label);
    aw = text_w(tb, st, ui->intern, it->accel);

    if (lw > label_w) {
      label_w = lw;
    }

    if (aw > accel_w) {
      accel_w = aw;
    }

    h += m->row_h;
  }

  w = label_w + (accel_w > 0 ? LK_MENU_ACCEL_GAP + accel_w : 0) +
      LK_MENU_PAD_X * 2;
  h += LK_MENU_PAD_Y * 2;

  oix = ov->owner_id ? lk_tree_find_by_id(t, ov->owner_id) : 0;

  if (oix != 0 && rects) {
    owner_rect = rects[oix];
  }

  m->rect = lk_anchor_resolve(ov, owner_rect, cfg ? cfg->viewport_w : 0,
                              cfg ? cfg->viewport_h : 0, w, h);
  m->placed = 1;

  return m->rect;
}

/* The item index at window y inside the popup, -1 for padding /
 * separators. */
static lk_i32 menu_row_at(const struct lk_menu_state *m, lk_i32 y) {
  lk_i32 top = m->rect.y + LK_MENU_PAD_Y;
  lk_u32 i;

  for (i = 0; i < m->count; i++) {
    lk_i32 rh = m->items[i].separator ? m->row_h / 2 : m->row_h;

    if (y >= top && y < top + rh) {
      return m->items[i].separator ? -1 : (lk_i32)i;
    }

    top += rh;
  }

  return -1;
}

static int point_in(const lk_rect *r, lk_i32 x, lk_i32 y) {
  return x >= r->x && y >= r->y && x < r->x + r->w && y < r->y + r->h;
}

int lk_menu_contains(lk_ui *ui, const lk_overlay *ov, const lk_rect *rects,
                     const lk_layout_cfg *cfg, lk_i32 x, lk_i32 y) {
  lk_rect r = lk_menu_layout(ui, ov, rects, cfg);

  return r.w > 0 && point_in(&r, x, y);
}

/* ---- render ---- */

static lk_color dim(lk_color c, lk_u8 pct) {
  c.r = (lk_u8)((lk_u32)c.r * pct / 100);
  c.g = (lk_u8)((lk_u32)c.g * pct / 100);
  c.b = (lk_u8)((lk_u32)c.b * pct / 100);
  return c;
}

static lk_color rgba(lk_u8 r, lk_u8 g, lk_u8 b, lk_u8 a) {
  lk_color c;
  c.r = r;
  c.g = g;
  c.b = b;
  c.a = a;
  return c;
}

static void fill(lk_render_list *out, lk_rect r, lk_color c) {
  lk_render_cmd cmd;

  memset(&cmd, 0, sizeof(cmd));
  cmd.op = LK_ROP_FILL_RECT;
  cmd.rect = r;
  cmd.color = c;
  lk_render_list_push(out, cmd);
}

static void text(lk_render_list *out, lk_i32 x, lk_i32 y, lk_u32 sid,
                 lk_color c, const lk_style *st) {
  lk_render_cmd cmd;

  if (sid == 0) {
    return;
  }

  memset(&cmd, 0, sizeof(cmd));
  cmd.op = LK_ROP_DRAW_TEXT;
  cmd.rect.x = x;
  cmd.rect.y = y;
  cmd.color = c;
  cmd.str_id = sid;
  cmd.font_id = st ? (lk_u16)st->font_id : 0;
  cmd.font_size = st ? (lk_u16)st->font_size : 0;
  lk_render_list_push(out, cmd);
}

void lk_menu_render(lk_ui *ui, const lk_overlay *ov, const lk_rect *rects,
                    const lk_layout_cfg *cfg, lk_render_list *out) {
  struct lk_menu_state *m = ui ? ui->menu : NULL;
  const lk_tree *t = lk_ui_tree(ui);
  const lk_style *st = root_style(t, cfg);
  const lk_text_backend *tb = menu_backend(ui, cfg);
  lk_rect r;
  lk_color bg, border, fg, accent;
  lk_i32 y;
  lk_u32 i;

  if (!m || m->count == 0 || !out) {
    return;
  }

  r = lk_menu_layout(ui, ov, rects, cfg);

  if (r.w <= 0) {
    return;
  }

  /* Plate from the root's resolved style, with fixed fallbacks. */
  bg = (st && st->bg.a > 0) ? st->bg : rgba(30, 30, 30, 255);
  bg.r = (lk_u8)(bg.r + (255 - bg.r) / 8);
  bg.g = (lk_u8)(bg.g + (255 - bg.g) / 8);
  bg.b = (lk_u8)(bg.b + (255 - bg.b) / 8);
  bg.a = 255;
  border = (st && st->border_color.a > 0) ? st->border_color
                                          : rgba(90, 94, 110, 255);
  fg = st ? st->fg : rgba(220, 220, 220, 255);
  accent = (st && st->accent.a > 0) ? st->accent : rgba(80, 140, 220, 255);

  fill(out, r, border);
  {
    lk_rect inner = r;
    inner.x += 1;
    inner.y += 1;
    inner.w -= 2;
    inner.h -= 2;
    fill(out, inner, bg);
  }

  y = r.y + LK_MENU_PAD_Y;

  for (i = 0; i < m->count; i++) {
    const lk_menu_item *it = &m->items[i];

    if (it->separator) {
      lk_rect line;

      line.x = r.x + LK_MENU_PAD_X / 2;
      line.y = y + m->row_h / 4;
      line.w = r.w - LK_MENU_PAD_X;
      line.h = 1;
      fill(out, line, border);
      y += m->row_h / 2;
      continue;
    }

    if ((lk_i32)i == m->hover && item_choosable(it)) {
      lk_rect row;

      row.x = r.x + 1;
      row.y = y;
      row.w = r.w - 2;
      row.h = m->row_h;
      fill(out, row, accent);
    }

    {
      lk_color lc = it->enabled ? fg : dim(fg, 45);
      lk_color ac = it->enabled ? dim(fg, 60) : dim(fg, 35);

      text(out, r.x + LK_MENU_PAD_X, y + LK_MENU_PAD_Y, it->label, lc, st);

      if (it->accel != 0) {
        lk_i32 aw = text_w(tb, st, ui->intern, it->accel);

        text(out, r.x + r.w - LK_MENU_PAD_X - aw, y + LK_MENU_PAD_Y,
             it->accel, ac, st);
      }
    }

    y += m->row_h;
  }
}

/* ---- events ---- */

int lk_menu_activate(lk_ui *ui, lk_u32 i) {
  lk_menu_item it;
  const lk_tree *t;

  if (!lk_menu_is_open(ui) || i >= ui->menu->count ||
      !item_choosable(&ui->menu->items[i])) {
    return 0;
  }

  it = ui->menu->items[i]; /* copy: emission may reopen a menu */
  t = lk_ui_tree(ui);
  lk_menu_close(ui);

  return lk_menu_emit(ui, t, &it);
}

static lk_i32 next_choosable(const struct lk_menu_state *m, lk_i32 from,
                             int dir) {
  lk_i32 n = (lk_i32)m->count;
  lk_i32 i = from;
  lk_i32 k;

  for (k = 0; k < n; k++) {
    i += dir;

    if (i < 0) {
      i = n - 1;
    } else if (i >= n) {
      i = 0;
    }

    if (item_choosable(&m->items[i])) {
      return i;
    }
  }

  return from;
}

static int label_starts_with(const lk_ui *ui, const lk_menu_item *it,
                             char c) {
  lk_str s;

  if (it->label == 0) {
    return 0;
  }

  s = lk_intern_str(ui->intern, it->label);

  if (s.len == 0) {
    return 0;
  }

  {
    char l = s.ptr[0];

    if (l >= 'A' && l <= 'Z') {
      l = (char)(l - 'A' + 'a');
    }

    return l == c;
  }
}

int lk_menu_route(lk_ui *ui, const lk_tree *t, lk_event *ev) {
  struct lk_menu_state *m;
  const lk_overlay *ov;

  if (!ui || !ev || !lk_menu_is_open(ui)) {
    return 0;
  }

  m = ui->menu;
  ov = lk_menu_overlay(ui);

  /* Keys: only while the menu is the topmost overlay. */
  if (ev->type == LK_EVENT_KEY_DOWN) {
    lk_u16 kc = ev->data.key.keycode;

    if (&ui->overlays[ui->overlay_count - 1] != ov) {
      return 0;
    }

    switch (kc) {
    case LKK_DOWN:
      m->hover = next_choosable(m, m->hover < 0 ? -1 : m->hover, 1);
      return 1;
    case LKK_UP:
      m->hover = next_choosable(m, m->hover < 0 ? (lk_i32)m->count : m->hover,
                                -1);
      return 1;
    case LKK_HOME:
      m->hover = next_choosable(m, -1, 1);
      return 1;
    case LKK_END:
      m->hover = next_choosable(m, (lk_i32)m->count, -1);
      return 1;
    case LKK_RETURN:
    case LKK_SPACE:
      if (m->hover >= 0) {
        lk_menu_activate(ui, (lk_u32)m->hover);
      }

      return 1;
    default:
      if (kc >= LKK_A && kc <= LKK_Z && ev->mods == 0) {
        char c = (char)('a' + (kc - LKK_A));
        lk_i32 start = m->hover;
        lk_i32 i = start;
        lk_u32 k;

        for (k = 0; k < m->count; k++) {
          i = next_choosable(m, i, 1);

          if (label_starts_with(ui, &m->items[i], c)) {
            m->hover = i;
            break;
          }
        }
      }

      return 1; /* the focused widget never sees keys under a menu */
    }
  }

  if (ev->type == LK_EVENT_TEXT) {
    return 1; /* typed text goes nowhere while a menu is up */
  }

  if (ev->type == LK_EVENT_POINTER_MOVE || ev->type == LK_EVENT_POINTER_DOWN ||
      ev->type == LK_EVENT_POINTER_UP) {
    lk_i32 x = ev->data.pointer.x;
    lk_i32 y = ev->data.pointer.y;

    (void)t;

    if (!m->placed || !point_in(&m->rect, x, y)) {
      return 0; /* outside: the host's dismiss pass decides */
    }

    if (ev->type == LK_EVENT_POINTER_MOVE) {
      lk_i32 row = menu_row_at(m, y);

      if (row >= 0 && item_choosable(&m->items[row])) {
        m->hover = row;
      }

      return 1;
    }

    if (ev->type == LK_EVENT_POINTER_DOWN &&
        (ev->data.pointer.button == LK_POINTER_BUTTON_PRIMARY ||
         ev->data.pointer.button == LK_POINTER_BUTTON_ANY)) {
      lk_i32 row = menu_row_at(m, y);

      if (row >= 0) {
        lk_menu_activate(ui, (lk_u32)row);
      }
    }

    return 1;
  }

  return 0;
}
