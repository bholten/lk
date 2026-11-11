#ifndef LK_TOOLTIP_H
#define LK_TOOLTIP_H

#include <lk.h>

void lk_tooltip_hover_changed(lk_ui *ui, lk_node_id hovered_id);

lk_rect lk_tooltip_rect(const lk_tree *t, lk_ix n, const lk_overlay *ov,
                        const lk_rect *rects, const lk_layout_cfg *cfg);

void lk_tooltip_render(const lk_tree *t, lk_ix n, const lk_overlay *ov,
                       const lk_rect *rects, const lk_layout_cfg *cfg,
                       lk_render_list *out);

#endif
