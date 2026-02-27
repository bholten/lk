#ifndef LK_SDL_H
#define LK_SDL_H

#include <lk.h>

typedef struct lk_window lk_window;

typedef struct lk_window_cfg {
  const char *title;
  int width, height;
  const char *font_path;
  int font_size;
  const char *icon_path; /* optional; see lk_window_set_icon */
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

/* Window icon (taskbar / title bar / dock, as the platform allows).
 * All three return 1 on success, 0 on failure; failure is never
 * fatal -- the window simply keeps its previous (or default) icon.
 *
 *   lk_window_set_icon      decode an image FILE.  PNG (and whatever
 *                           else SDL3_image knows) when lk was built
 *                           with SDL3_image; BMP always.
 *   lk_window_set_icon_mem  same, but from an encoded image already
 *                           in memory -- the path for applications
 *                           that embed their icon in the executable
 *                           (single static binary, no files beside
 *                           it).  The bytes are only read during the
 *                           call; the caller keeps ownership.
 *   lk_window_set_icon_rgba raw 8-bit RGBA pixels, row-major, `pitch`
 *                           bytes per row (0 = w * 4).  Needs no image
 *                           library at all.  Bytes copied during the
 *                           call.
 *
 * lk_window_cfg.icon_path, when set, is applied at creation through
 * lk_window_set_icon. */
int lk_window_set_icon(lk_window *win, const char *path);

/* Screenshot request: at the end of the CURRENT run-loop iteration
 * (after every render command is drawn, before present) the window
 * reads its back buffer into an lk_image and saves it to path --
 * .png through lk_image_save_png (SDL3_image; falls back to BMP bytes
 * when it is compiled out), anything else through lk_image_save_bmp.
 * One request at a time (a second call replaces the path).  Works
 * under SDL_VIDEODRIVER=offscreen, which is the point: a script can
 * drive itself through its pages and leave PNGs behind headless (the
 * Widget Tour's --snap mode).  Returns 1 when queued, 0 on NULL args
 * or allocation failure; a failed save is reported nowhere but the
 * missing file. */
int lk_window_request_screenshot(lk_window *win, const char *path);

/* Leave lk_window_run after the current iteration completes (the
 * frame is still rendered and a pending screenshot still saved).
 * Safe from a frame callback, a command handler, or an event
 * handler; a no-op when the loop is not running. */
void lk_window_stop(lk_window *win);
int lk_window_set_icon_mem(lk_window *win, const void *data, lk_u32 len);
int lk_window_set_icon_rgba(lk_window *win, int w, int h,
                            const void *pixels, int pitch);

/* Image file IO (image track, docs/image-widget.md).  Loaders decode
 * BMP always (SDL core) plus PNG and whatever else SDL3_image knows
 * when lk was built with it, and return a NEW app-owned lk_image
 * (RGBA8888) — NULL on failure.  Savers return 1/0; save_png returns
 * 0 when SDL3_image was compiled out.  For lossless round-trips
 * (steganography!) stick to BMP and PNG.  None of these need a
 * window or SDL_Init. */
lk_image *lk_image_load_file(const char *path);
lk_image *lk_image_load_mem(const void *data, lk_u32 len);
int lk_image_save_bmp(const lk_image *img, const char *path);
int lk_image_save_png(const lk_image *img, const char *path);

/* Native file dialogs (async).  Both calls return immediately; the
 * completion fn runs later ON THE MAIN THREAD, from inside the
 * window's run loop.  npaths == 0 means cancelled (or the platform
 * reported an error); otherwise paths[0..npaths-1] are UTF-8 file
 * paths.  The paths are freed when fn returns — copy what you keep.
 * A window destroyed while its dialog is open simply never gets the
 * callback.  Filter patterns are SDL's ';'-separated extension
 * lists, e.g. { "Images", "bmp;png" } — no dots, "*" for anything;
 * filters/default_location may be NULL. */
typedef struct lk_file_dialog_filter {
  const char *name;    /* shown to the user, e.g. "Images" */
  const char *pattern; /* "bmp;png" style, or "*" */
} lk_file_dialog_filter;

typedef void (*lk_file_dialog_fn)(void *ud, const char *const *paths,
                                  int npaths);

void lk_window_open_file_dialog(lk_window *win,
                                const lk_file_dialog_filter *filters,
                                int nfilters, const char *default_location,
                                int allow_many, lk_file_dialog_fn fn,
                                void *ud);
void lk_window_save_file_dialog(lk_window *win,
                                const lk_file_dialog_filter *filters,
                                int nfilters, const char *default_location,
                                lk_file_dialog_fn fn, void *ud);

#endif
