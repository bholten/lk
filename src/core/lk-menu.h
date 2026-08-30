/*
 * lk-menu.h -- internal: the context-menu popup (docs/context-menu.md).
 *
 * The menu is LK_OVERLAY_CONTEXT_MENU with procedural content: its
 * items live on the ui (lk_ui.menu), lk-overlay.c delegates render /
 * hit / containment here, lk_event_route's pre-step routes keys and
 * in-popup pointer events here, and lk-command.c owns the producer
 * (lk_menu_candidates) and activation (lk_menu_emit).
 */
#ifndef LK_MENU_H
#define LK_MENU_H

#include <lk.h>

#define LK_MENU_PAD_X 10
#define LK_MENU_PAD_Y 4
#define LK_MENU_ACCEL_GAP 24

struct lk_menu_state {
  lk_menu_item *items;
  lk_u32 count, cap;
  lk_i32 hover; /* -1 = none */
  lk_node_id owner;
  lk_rect rect; /* last resolved popup rect (render / hit) */
  lk_i32 row_h; /* row metrics of that resolution */
  lk_u8 placed;
};

/* The open procedural context-menu overlay, or NULL. */
const lk_overlay *lk_menu_overlay(const lk_ui *ui);

/* Resolve + stash the popup rect for the overlay passes. */
lk_rect lk_menu_layout(lk_ui *ui, const lk_overlay *ov, const lk_rect *rects,
                       const lk_layout_cfg *cfg);
void lk_menu_render(lk_ui *ui, const lk_overlay *ov, const lk_rect *rects,
                    const lk_layout_cfg *cfg, lk_render_list *out);
int lk_menu_contains(lk_ui *ui, const lk_overlay *ov, const lk_rect *rects,
                     const lk_layout_cfg *cfg, lk_i32 x, lk_i32 y);

/* Event pre-step: keys while a menu is topmost, pointer events inside
 * the popup.  1 = consumed. */
int lk_menu_route(lk_ui *ui, const lk_tree *t, lk_event *ev);

/* Activation (lk-command.c): emit the item's command exactly as its
 * gesture would have.  1 on emission. */
int lk_menu_emit(lk_ui *ui, const lk_tree *t, const lk_menu_item *it);

void lk_menu_ui_destroy(lk_ui *ui);

#endif /* LK_MENU_H */
