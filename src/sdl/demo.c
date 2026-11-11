

#include <lk-window.h>
#include <stdio.h>
#include <string.h>

#define FRUIT_COUNT 6

static const char *g_fruits[FRUIT_COUNT] = {"Apple", "Banana",     "Cherry",
                                            "Date",  "Elderberry", "Fig"};

typedef struct demo_state {
  int selected;
  lk_ui *ui;
  lk_u32 select_cmd_id;
} demo_state;

static void on_command(const lk_command *cmd, void *ud) {
  demo_state *st = (demo_state *)ud;

  if (cmd->name == st->select_cmd_id) {
    if (cmd->arg_count >= 1 && cmd->args[0].tag == UIV_I32) {
      int idx = (int)cmd->args[0].as.i;

      if (idx >= 0 && idx < FRUIT_COUNT) {
        st->selected = idx;
      }
    }
  }
}

static int on_event(lk_event *event, lk_ix node_ix, void *ud) {
  demo_state *st = (demo_state *)ud;
  const lk_tree *cur;
  (void)node_ix;

  if (event->phase != LK_PHASE_CAPTURE) {
    return 0;
  }

  if (event->type == LK_EVENT_KEY_DOWN && event->data.key.keycode == LKK_TAB) {
    cur = lk_ui_tree(st->ui);
    if (event->mods & (lk_u8)LK_MOD_SHIFT) {
      lk_focus_prev(st->ui, cur);
    } else {
      lk_focus_next(st->ui, cur);
    }
    event->handled = 1;
    return 0;
  }

  return 0;
}

static void build_frame(lk_tree *t, void *ud) {
  demo_state *st = (demo_state *)ud;
  lk_ix w, root_col, title, list_col, items[FRUIT_COUNT], status;
  char status_buf[64];
  char item_buf[48];
  int i;

  w = lk_tree_add_node_s(t, lk_str_c("main"), UIK_WINDOW);

  root_col = lk_tree_add_node_s(t, lk_str_c("root"), UIK_COLUMN);
  lk_tree_add_prop(t, root_col, UIP_PADDING, lk_v_i32(20));
  lk_tree_add_prop(t, root_col, UIP_GAP, lk_v_i32(12));

  title = lk_tree_add_node_s(t, lk_str_c("title"), UIK_LABEL);
  lk_tree_add_prop(t, title, UIP_TEXT, lk_v_cstr(t->intern, "Fruit Selector"));

  list_col = lk_tree_add_node_s(t, lk_str_c("list"), UIK_COLUMN);
  lk_tree_add_prop(t, list_col, UIP_GAP, lk_v_i32(4));

  for (i = 0; i < FRUIT_COUNT; i++) {
    char id_buf[16];
    sprintf(id_buf, "item_%d", i);

    if (st->selected == i) {
      sprintf(item_buf, "> %s", g_fruits[i]);
    } else {
      sprintf(item_buf, "  %s", g_fruits[i]);
    }

    items[i] = lk_tree_add_node_s(t, lk_str_c(id_buf), UIK_BUTTON);
    lk_tree_add_prop(t, items[i], UIP_TEXT, lk_v_cstr(t->intern, item_buf));
    lk_tree_add_prop(t, items[i], UIP_PADDING, lk_v_i32(6));
    lk_tree_add_prop(t, items[i], UIP_FOCUSABLE, lk_v_bool(1));

    lk_tree_add_presentation_s(t, items[i], "item", lk_v_i32(i));
  }

  status = lk_tree_add_node_s(t, lk_str_c("status"), UIK_LABEL);
  if (st->selected >= 0 && st->selected < FRUIT_COUNT) {
    sprintf(status_buf, "Selected: %s", g_fruits[st->selected]);
  } else {
    sprintf(status_buf, "Selected: (none)");
  }
  lk_tree_add_prop(t, status, UIP_TEXT, lk_v_cstr(t->intern, status_buf));

  lk_tree_set_root(t, w);
  lk_tree_append_child(t, w, root_col);
  lk_tree_append_child(t, root_col, title);
  lk_tree_append_child(t, root_col, list_col);
  for (i = 0; i < FRUIT_COUNT; i++) {
    lk_tree_append_child(t, list_col, items[i]);
  }
  lk_tree_append_child(t, root_col, status);
}

int main(int argc, char **argv) {
  lk_window_cfg cfg;
  lk_window *win;
  lk_ui *ui;
  demo_state state;
  (void)argc;
  (void)argv;

  memset(&cfg, 0, sizeof(cfg));
  cfg.title = "lk fruit selector";
  cfg.width = 800;
  cfg.height = 600;
  cfg.font_path =
      "/usr/share/fonts/truetype/liberation/LiberationMono-Regular.ttf";
  cfg.font_size = 18;

  win = lk_window_create(&cfg);
  if (!win) {
    fprintf(stderr, "Failed to create window\n");
    return 1;
  }

  ui = lk_window_ui(win);

  memset(&state, 0, sizeof(state));
  state.selected = -1;
  state.ui = ui;
  state.select_cmd_id = lk_intern_id(ui->intern, lk_str_c("Select"));

  lk_ui_add_translator_s(ui, LK_EVENT_POINTER_DOWN, "item", 0, 0, 0, 0,
                         "Select");
  lk_ui_add_translator_s(ui, LK_EVENT_KEY_DOWN, "item", 0, 0, 0, 0, "Select");

  lk_ui_set_command_handler(ui, on_command, &state);
  lk_window_set_event_handler(win, on_event, &state);

  lk_window_run(win, build_frame, &state);
  lk_window_destroy(win);

  return 0;
}
