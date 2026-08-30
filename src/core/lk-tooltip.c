/*
 * lk-tooltip.c — Tooltip overlay producer (docs/overlays.md step 6).
 *
 * Tooltips are a prop, not a widget kind: any node may carry
 * UIP_TOOLTIP (interned string).  Hover transitions — routed here from
 * lk_hover_set / lk_hover_clear — pop the previous tooltip overlay and
 * push a new one when the freshly hovered node has tooltip text.
 *
 * The overlay is passive: dismiss_on_outside = 0 and traps_focus = 0,
 * and lk-overlay.c never hit-tests LK_OVERLAY_TOOLTIP, so it is
 * transparent to the pointer.  Content is procedural
 * (content_root_id = 0): geometry and paint live below, dispatched
 * from lk_render_build_overlays.
 */

#include <string.h>

#include "lk-tooltip.h"
#include <lk.h>

#define LK_TOOLTIP_PAD 5 /* px between text and tooltip edge */

/* Interned tooltip text id of node n, or 0 when absent. */
static lk_u32 tooltip_text_id(const lk_tree *t, lk_ix n) {
  const lk_node *nd;
  lk_u16 i;

  if (!t || n == 0 || n >= (lk_ix)t->node_count) {
    return 0;
  }

  nd = &t->nodes[n];

  for (i = 0; i < nd->props_len; i++) {
    const lk_prop *p = &t->props[nd->props_off + i];

    if (p->key == UIP_TOOLTIP && p->value.tag == UIV_STR) {
      return p->value.as.str_id;
    }
  }

  return 0;
}

void lk_tooltip_hover_changed(lk_ui *ui, lk_node_id hovered_id) {
  const lk_tree *t;
  lk_ix n;
  lk_u32 i;

  if (!ui) {
    return;
  }

  /* Pop any existing tooltip overlay (at most one in practice). */
  i = ui->overlay_count;

  while (i > 0) {
    i--;

    if (ui->overlays[i].kind == LK_OVERLAY_TOOLTIP) {
      lk_u32 k;

      for (k = i; k + 1 < ui->overlay_count; k++) {
        ui->overlays[k] = ui->overlays[k + 1];
      }

      ui->overlay_count--;
    }
  }

  if (hovered_id == 0) {
    return;
  }

  t = lk_ui_tree(ui);

  if (!t || t->root == 0) {
    return;
  }

  n = lk_tree_find_by_id(t, hovered_id);

  if (n == 0 || tooltip_text_id(t, n) == 0) {
    return;
  }

  {
    lk_overlay ov;

    memset(&ov, 0, sizeof(ov));
    ov.kind = LK_OVERLAY_TOOLTIP;
    ov.anchor_mode = LK_ANCHOR_BELOW;
    ov.dismiss_on_outside = 0; /* passive: outside clicks pass through */
    ov.traps_focus = 0;
    ov.owner_id = hovered_id;
    ov.content_root_id = 0; /* procedural */
    lk_overlay_push(ui, &ov);
  }
}

lk_rect lk_tooltip_rect(const lk_tree *t, lk_ix n, const lk_overlay *ov,
                        const lk_rect *rects, const lk_layout_cfg *cfg) {
  lk_text_metrics m;
  lk_u32 sid;

  m.w = 0;
  m.h = 0;
  m.baseline = 0;
  sid = tooltip_text_id(t, n);

  if (sid != 0 && cfg && cfg->text) {
    lk_u16 font_id = cfg->styles ? (lk_u16)cfg->styles[n].font_id : 0;
    lk_u16 font_size = cfg->styles ? (lk_u16)cfg->styles[n].font_size : 0;
    lk_str s = lk_intern_str(t->intern, sid);

    cfg->text->measure(cfg->text->ud, s, font_id, font_size, &m);
  }

  return lk_anchor_resolve(ov, rects[n], cfg ? cfg->viewport_w : 0,
                           cfg ? cfg->viewport_h : 0, m.w + LK_TOOLTIP_PAD * 2,
                           m.h + LK_TOOLTIP_PAD * 2);
}

void lk_tooltip_render(const lk_tree *t, lk_ix n, const lk_overlay *ov,
                       const lk_rect *rects, const lk_layout_cfg *cfg,
                       lk_render_list *out) {
  lk_u32 sid;
  lk_rect r;
  lk_color bg, fg;
  const lk_style *st;
  lk_render_cmd cmd;

  if (!t || !rects || !out) {
    return;
  }

  sid = tooltip_text_id(t, n);

  if (sid == 0) {
    return;
  }

  r = lk_tooltip_rect(t, n, ov, rects, cfg);
  st = (cfg && cfg->styles) ? &cfg->styles[n] : NULL;

  if (st) {
    /* Owner style with fg/bg swapped: light plate, dark text on the
     * default dark theme.  Theme-rule styling can layer on later. */
    bg = st->fg;
    fg = st->bg;
  } else {
    bg.r = 230;
    bg.g = 230;
    bg.b = 230;
    bg.a = 255;
    fg.r = 25;
    fg.g = 25;
    fg.b = 25;
    fg.a = 255;
  }

  /* Background plate */
  memset(&cmd, 0, sizeof(cmd));
  cmd.op = LK_ROP_FILL_RECT;
  cmd.rect = r;
  cmd.color = bg;
  lk_render_list_push(out, cmd);

  /* 1 px border in the text color */
  memset(&cmd, 0, sizeof(cmd));
  cmd.op = LK_ROP_FILL_RECT;
  cmd.color = fg;
  cmd.rect.x = r.x;
  cmd.rect.y = r.y;
  cmd.rect.w = r.w;
  cmd.rect.h = 1;
  lk_render_list_push(out, cmd); /* top */
  cmd.rect.y = r.y + r.h - 1;
  lk_render_list_push(out, cmd); /* bottom */
  cmd.rect.y = r.y;
  cmd.rect.w = 1;
  cmd.rect.h = r.h;
  lk_render_list_push(out, cmd); /* left */
  cmd.rect.x = r.x + r.w - 1;
  lk_render_list_push(out, cmd); /* right */

  /* Text */
  memset(&cmd, 0, sizeof(cmd));
  cmd.op = LK_ROP_DRAW_TEXT;
  cmd.rect.x = r.x + LK_TOOLTIP_PAD;
  cmd.rect.y = r.y + LK_TOOLTIP_PAD;
  cmd.rect.w = r.w - LK_TOOLTIP_PAD * 2;
  cmd.rect.h = r.h - LK_TOOLTIP_PAD * 2;
  cmd.color = fg;
  cmd.str_id = sid;

  if (st) {
    cmd.font_id = (lk_u16)st->font_id;
    cmd.font_size = (lk_u16)st->font_size;
  }

  lk_render_list_push(out, cmd);
}
