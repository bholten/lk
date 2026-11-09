

#ifndef LK_MENU_H
#define LK_MENU_H

#include <lk.h>

#define LK_MENU_PAD_X 10
#define LK_MENU_PAD_Y 4
#define LK_MENU_ACCEL_GAP 24

struct lk_menu_state {
  lk_menu_item *items;
  lk_u32 count, cap;
  lk_i32 hover;
  lk_node_id owner;
  lk_rect rect;
  lk_i32 row_h;
  lk_i32 sep_h;
  lk_i32 pad_x, pad_y;
  lk_i32 sep_inset;
  lk_i32 text_dy;

  lk_u8 placed;
};

const lk_overlay *lk_menu_overlay(const lk_ui *ui);

lk_rect lk_menu_layout(lk_ui *ui, const lk_overlay *ov, const lk_rect *rects,
                       const lk_layout_cfg *cfg);
void lk_menu_render(lk_ui *ui, const lk_overlay *ov, const lk_rect *rects,
                    const lk_layout_cfg *cfg, lk_render_list *out);
int lk_menu_contains(lk_ui *ui, const lk_overlay *ov, const lk_rect *rects,
                     const lk_layout_cfg *cfg, lk_i32 x, lk_i32 y);

int lk_menu_route(lk_ui *ui, const lk_tree *t, lk_event *ev);

int lk_menu_emit(lk_ui *ui, const lk_tree *t, const lk_menu_item *it);

void lk_menu_ui_destroy(lk_ui *ui);

#endif
