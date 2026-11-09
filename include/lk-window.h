#ifndef LK_WINDOW_H
#define LK_WINDOW_H

#include <lk.h>

typedef struct lk_window lk_window;

#define LK_WCAP_FILE_DIALOGS (1u << 0)

#define LK_WCAP_ICON (1u << 1)
#define LK_WCAP_CLIPBOARD (1u << 2)

#define LK_WCAP_IMAGE_IO (1u << 3)

#define LK_WCAP_POINTER (1u << 4)

#define LK_WCAP_KEY_UP (1u << 5)

#define LK_WCAP_KEY_CHORDS (1u << 6)

lk_u32 lk_window_caps(const lk_window *win);

typedef struct lk_window_cfg {
  const char *title;
  int width, height;
  const char *font_path;
  int font_size;
  const char *icon_path;
} lk_window_cfg;

typedef void (*lk_frame_fn)(lk_tree *t, void *ud);

lk_window *lk_window_create(const lk_window_cfg *cfg);
void lk_window_destroy(lk_window *win);

int lk_window_step(lk_window *win, lk_frame_fn frame, void *ud);
void lk_window_run(lk_window *win, lk_frame_fn frame, void *ud);
lk_ui *lk_window_ui(lk_window *win);

void lk_window_set_event_handler(lk_window *win, lk_event_handler_fn fn,
                                 void *ud);

lk_u16 lk_window_register_font(lk_window *win, const char *path);

int lk_window_set_icon(lk_window *win, const char *path);

int lk_window_request_screenshot(lk_window *win, const char *path);

void lk_window_stop(lk_window *win);
int lk_window_set_icon_mem(lk_window *win, const void *data, lk_u32 len);
int lk_window_set_icon_rgba(lk_window *win, int w, int h, const void *pixels,
                            int pitch);

lk_image *lk_image_load_file(const char *path);
lk_image *lk_image_load_mem(const void *data, lk_u32 len);
int lk_image_save_bmp(const lk_image *img, const char *path);
int lk_image_save_png(const lk_image *img, const char *path);

typedef struct lk_file_dialog_filter {
  const char *name;
  const char *pattern;
} lk_file_dialog_filter;

typedef void (*lk_file_dialog_fn)(void *ud, const char *const *paths,
                                  int npaths);

void lk_window_open_file_dialog(lk_window *win,
                                const lk_file_dialog_filter *filters,
                                int nfilters, const char *default_location,
                                int allow_many, lk_file_dialog_fn fn, void *ud);
void lk_window_save_file_dialog(lk_window *win,
                                const lk_file_dialog_filter *filters,
                                int nfilters, const char *default_location,
                                lk_file_dialog_fn fn, void *ud);

#endif
