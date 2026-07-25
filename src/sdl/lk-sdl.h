#ifndef LK_SDL_H
#define LK_SDL_H

#include <lk.h>

typedef struct lk_window lk_window;

typedef struct lk_window_cfg {
  const char *title;
  int width, height;
  const char *font_path;
  int font_size;
} lk_window_cfg;

/* Frame callback: build the tree. Run loop handles begin/end
   frame. */
typedef void (*lk_frame_fn)(lk_tree *t, void *ud);

lk_window *lk_window_create(const lk_window_cfg *cfg);
void lk_window_destroy(lk_window *win);
void lk_window_run(lk_window *win, lk_frame_fn frame, void *ud);
lk_ui *lk_window_ui(lk_window *win);

/* Set event handler on the underlying lk_ui. */
void lk_window_set_event_handler(lk_window *win, lk_event_handler_fn fn,
                                 void *ud);

/* Register a font face with the window's text backend (sugar over the
 * lk_text_backend.register_font vtable slot).  Returns the new
 * font_id (>= 1) for use in theme rules, or 0 on failure (unreadable
 * path, registry full).  Face 0 is always lk_window_cfg.font_path. */
lk_u16 lk_window_register_font(lk_window *win, const char *path);

#endif
