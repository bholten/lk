#include "lk-sdl.h"
#include <stdio.h>
#include <string.h>

static int my_event_handler(lk_event *event, lk_ix node_ix, void *ud) {
  (void)ud;
  if (event->phase == LK_PHASE_TARGET &&
      event->type == LK_EVENT_POINTER_DOWN) {
    printf("Button clicked! (node_ix=%u)\n", (unsigned)node_ix);
  }
  return 0;
}

static void my_frame(lk_tree *t, void *ud) {
  lk_ix w, col, lbl, btn;
  (void)ud;

  w = lk_tree_add_node_s(t, lk_str_c("main"), UIK_WINDOW);
  col = lk_tree_add_node_s(t, lk_str_c("root"), UIK_COLUMN);
  lk_tree_add_prop(t, col, UIP_PADDING, lk_v_i32(20));
  lk_tree_add_prop(t, col, UIP_GAP, lk_v_i32(10));

  lbl = lk_tree_add_node_s(t, lk_str_c("title"), UIK_LABEL);
  lk_tree_add_prop(t, lbl, UIP_TEXT, lk_v_cstr(t->intern, "Hello, lk!"));

  btn = lk_tree_add_node_s(t, lk_str_c("btn"), UIK_BUTTON);
  lk_tree_add_prop(t, btn, UIP_TEXT, lk_v_cstr(t->intern, "Click Me"));
  lk_tree_add_prop(t, btn, UIP_PADDING, lk_v_i32(8));
  lk_tree_add_prop(t, btn, UIP_FOCUSABLE, lk_v_bool(1));

  lk_tree_set_root(t, w);
  lk_tree_append_child(t, w, col);
  lk_tree_append_child(t, col, lbl);
  lk_tree_append_child(t, col, btn);
}

int main(int argc, char **argv) {
  lk_window_cfg cfg;
  lk_window *win;
  (void)argc;
  (void)argv;

  memset(&cfg, 0, sizeof(cfg));
  cfg.title = "lk demo";
  cfg.width = 800;
  cfg.height = 600;
  /* TODO: fallback embedded font */
  cfg.font_path = "/usr/share/fonts/truetype/liberation/LiberationMono-Regular.ttf";
  cfg.font_size = 18;

  win = lk_window_create(&cfg);

  if (!win) {
    return 1;
  }

  lk_window_set_event_handler(win, my_event_handler, NULL);
  lk_window_run(win, my_frame, NULL);
  lk_window_destroy(win);

  return 0;
}
