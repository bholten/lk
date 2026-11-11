#ifndef LK_DROPDOWN_H
#define LK_DROPDOWN_H

#include <lk.h>

lk_widget_def lk_dropdown_widget_def(void);
lk_widget_def lk_option_widget_def(void);

void lk_dropdown_store_trigger_rects(const lk_tree *t, const lk_rect *rects,
                                     const lk_layout_cfg *cfg);

lk_rect lk_dropdown_popup_rect(const lk_tree *t, lk_ix n, const lk_rect *rects,
                               const lk_style *styles,
                               const lk_layout_cfg *cfg);

lk_rect lk_dropdown_option_rect(const lk_tree *t, lk_ix dd, lk_u32 opt_index,
                                const lk_rect *rects, const lk_style *styles,
                                const lk_layout_cfg *cfg);

void lk_dropdown_render_popup(const lk_tree *t, lk_ix n, const lk_rect *rects,
                              const lk_style *styles, const lk_state *state,
                              const lk_layout_cfg *cfg, lk_render_list *out);

lk_ix lk_dropdown_hit_popup(const lk_tree *t, lk_ix n, const lk_rect *rects,
                            const lk_style *styles, const lk_state *state,
                            const lk_layout_cfg *cfg, lk_i32 x, lk_i32 y);

#endif
