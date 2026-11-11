#include <stdlib.h>
#include <string.h>

#include <SDL3/SDL.h>
#include <SDL3_ttf/SDL_ttf.h>
#ifdef LK_HAVE_SDL_IMAGE
#include <SDL3_image/SDL_image.h>
#endif

#include "core/lk-memory.h"
#include <lk-window.h>

#define SDL_TEXT_MAX_FACES 16
#define SDL_TEXT_MAX_INSTANCES 32
#define SDL_TEXT_DEFAULT_SIZE 16

typedef struct sdl_font_instance {
  lk_u16 face_id;
  int size;
  TTF_Font *font;
} sdl_font_instance;

#define SDL_IMG_CACHE_MAX 32

typedef struct sdl_img_entry {
  lk_u32 res_id;
  lk_u32 res_gen;
  lk_u32 pix_gen;
  SDL_Texture *tex;
  int w, h;
  Uint64 last_frame;
} sdl_img_entry;

struct lk_window {
  SDL_Window *sdl_win;
  SDL_Renderer *sdl_ren;
  TTF_TextEngine *text_engine;
  TTF_Text *scratch_text;
  char *face_paths[SDL_TEXT_MAX_FACES];
  int face_count;
  int default_font_size;
  sdl_font_instance instances[SDL_TEXT_MAX_INSTANCES];
  int instance_count;
  lk_text_backend text_backend;
  lk_ui *ui;
  lk_rect *rects;
  lk_render_list rl;
  int width;
  int height;
  int running;
  char *shot_path;
  lk_i32 mouse_x, mouse_y;
  float wheel_acc_x, wheel_acc_y;
  int text_input_active;
  int vsync;
  sdl_img_entry img_cache[SDL_IMG_CACHE_MAX];
  int img_cache_count;
  Uint64 frame_serial;
};

static int g_sdl_refs = 0;

static int sdl_global_acquire(void) {
  if (g_sdl_refs == 0) {
    if (!SDL_Init(SDL_INIT_VIDEO)) {
      return 0;
    }

    if (!TTF_Init()) {
      SDL_Quit();
      return 0;
    }
  }

  g_sdl_refs++;
  return 1;
}

static void sdl_global_release(void) {
  if (g_sdl_refs > 0 && --g_sdl_refs == 0) {
    TTF_Quit();
    SDL_Quit();
  }
}

static TTF_Font *sdl_text_instance(lk_window *win, lk_u16 face_id,
                                   lk_u16 size) {
  const char *path;
  TTF_Font *font;
  int px;
  int i;

  if ((int)face_id >= win->face_count) {
    return NULL;
  }

  path = win->face_paths[face_id];

  if (!path) {
    return NULL;
  }

  px = size != 0 ? (int)size : win->default_font_size;

  for (i = 0; i < win->instance_count; i++) {
    if (win->instances[i].face_id == face_id && win->instances[i].size == px) {
      return win->instances[i].font;
    }
  }

  if (win->instance_count >= SDL_TEXT_MAX_INSTANCES) {
    return NULL;
  }

  font = TTF_OpenFont(path, (float)px);

  if (!font) {
    return NULL;
  }

  win->instances[win->instance_count].face_id = face_id;
  win->instances[win->instance_count].size = px;
  win->instances[win->instance_count].font = font;
  win->instance_count++;

  return font;
}

static int sdl_text_have_faces(const lk_window *win) {
  return win->face_paths[0] != NULL || win->face_count > 1;
}

static TTF_Text *sdl_text_scratch(lk_window *win, TTF_Font *font, lk_str run) {
  if (!win->text_engine || !font) {
    return NULL;
  }

  if (!win->scratch_text) {
    win->scratch_text = TTF_CreateText(win->text_engine, font, "", 0);

    if (!win->scratch_text) {
      return NULL;
    }
  }

  if (!TTF_SetTextFont(win->scratch_text, font)) {
    return NULL;
  }

  if (run.len == 0) {
    TTF_SetTextString(win->scratch_text, "", 0);
  } else if (!TTF_SetTextString(win->scratch_text, run.ptr, run.len)) {
    return NULL;
  }

  return win->scratch_text;
}

static void sdl_text_attrs(lk_window *win, TTF_Font *font, TTF_Text *text,
                           const lk_render_cmd *cmd) {
  SDL_FRect ul;
  int w = cmd->rect.w;
  int h = 0;

  if (!(cmd->text_attr & LK_TA_UNDERLINE)) {
    return;
  }

  if (w <= 0) {
    TTF_GetTextSize(text, &w, &h);
  }

  ul.x = (float)cmd->rect.x;
  ul.y = (float)(cmd->rect.y + TTF_GetFontAscent(font) + 1);
  ul.w = (float)w;
  ul.h = 1.0f;
  SDL_SetRenderDrawColor(win->sdl_ren, cmd->color.r, cmd->color.g, cmd->color.b,
                         cmd->color.a);
  SDL_RenderFillRect(win->sdl_ren, &ul);
}

static void sdl_text_measure(void *ud, lk_str run, lk_u16 font_id,
                             lk_u16 font_size, lk_text_metrics *out) {
  lk_window *win = (lk_window *)ud;
  TTF_Font *font;
  int w = 0;
  int h = 0;

  if (!out) {
    return;
  }

  out->w = 0;
  out->h = 0;
  out->baseline = 0;

  font = sdl_text_instance(win, font_id, font_size);

  if (!font) {
    return;
  }

  out->baseline = (lk_i32)TTF_GetFontAscent(font);

  if (run.len > 0) {
    TTF_GetStringSize(font, run.ptr, run.len, &w, &h);
  }

  out->w = (lk_i32)w;
  out->h = (lk_i32)h;
}

static lk_i32 sdl_text_x_from_index(void *ud, lk_str run, lk_u16 font_id,
                                    lk_u16 font_size, lk_u32 byte_ix) {
  lk_window *win = (lk_window *)ud;
  TTF_Font *font;
  TTF_Text *text;
  TTF_SubString sub;

  font = sdl_text_instance(win, font_id, font_size);

  if (!font || run.len == 0 || byte_ix == 0) {
    return 0;
  }

  if (byte_ix >= run.len) {

    int w = 0;
    int h = 0;
    TTF_GetStringSize(font, run.ptr, run.len, &w, &h);
    return (lk_i32)w;
  }

  text = sdl_text_scratch(win, font, run);

  if (!text || !TTF_GetTextSubString(text, (int)byte_ix, &sub)) {
    return 0;
  }

  return (lk_i32)sub.rect.x;
}

static lk_u32 sdl_text_index_from_x(void *ud, lk_str run, lk_u16 font_id,
                                    lk_u16 font_size, lk_i32 x) {
  lk_window *win = (lk_window *)ud;
  TTF_Font *font;
  TTF_Text *text;
  TTF_SubString sub;
  lk_u32 ix;

  if (run.len == 0 || x <= 0) {
    return 0;
  }

  font = sdl_text_instance(win, font_id, font_size);
  text = font ? sdl_text_scratch(win, font, run) : NULL;

  if (!text || !TTF_GetTextSubStringForPoint(text, (int)x, 0, &sub)) {
    return 0;
  }

  ix = (lk_u32)sub.offset;

  if (sub.length > 0 && (x - sub.rect.x) * 2 >= sub.rect.w) {
    ix = (lk_u32)(sub.offset + sub.length);
  }

  if (ix > run.len) {
    ix = run.len;
  }

  return ix;
}

static lk_i32 sdl_text_line_height(void *ud, lk_u16 font_id, lk_u16 font_size) {
  lk_window *win = (lk_window *)ud;
  TTF_Font *font = sdl_text_instance(win, font_id, font_size);

  return font ? (lk_i32)TTF_GetFontHeight(font) : 0;
}

static lk_u16 sdl_text_register_font(void *ud, const char *path) {
  lk_window *win = (lk_window *)ud;
  TTF_Font *font;
  char *copy;
  lk_u16 id;

  if (!win || !path || path[0] == '\0') {
    return 0;
  }

  if (win->face_count >= SDL_TEXT_MAX_FACES ||
      win->instance_count >= SDL_TEXT_MAX_INSTANCES) {
    return 0;
  }

  font = TTF_OpenFont(path, (float)win->default_font_size);

  if (!font) {
    return 0;
  }

  copy = SDL_strdup(path);

  if (!copy) {
    TTF_CloseFont(font);
    return 0;
  }

  id = (lk_u16)win->face_count;
  win->face_paths[win->face_count++] = copy;
  win->instances[win->instance_count].face_id = id;
  win->instances[win->instance_count].size = win->default_font_size;
  win->instances[win->instance_count].font = font;
  win->instance_count++;

  return id;
}

static void sdl_text_shutdown(lk_window *win) {
  int i;

  if (win->scratch_text) {
    TTF_DestroyText(win->scratch_text);
    win->scratch_text = NULL;
  }

  for (i = 0; i < win->instance_count; i++) {
    if (win->instances[i].font) {
      TTF_CloseFont(win->instances[i].font);
    }
  }

  win->instance_count = 0;

  if (win->text_engine) {
    TTF_DestroyRendererTextEngine(win->text_engine);
    win->text_engine = NULL;
  }

  for (i = 0; i < win->face_count; i++) {
    if (win->face_paths[i]) {
      SDL_free(win->face_paths[i]);
      win->face_paths[i] = NULL;
    }
  }

  win->face_count = 0;
}

lk_u16 lk_window_register_font(lk_window *win, const char *path) {
  if (!win || !win->text_backend.register_font) {
    return 0;
  }

  return win->text_backend.register_font(win->text_backend.ud, path);
}

static SDL_Surface *sdl_image_decode(SDL_IOStream *io) {
  SDL_Surface *surf = NULL;

  if (!io) {
    return NULL;
  }

#ifdef LK_HAVE_SDL_IMAGE
  surf = IMG_Load_IO(io, false);

  if (!surf) {
    SDL_SeekIO(io, 0, SDL_IO_SEEK_SET);
  }
#endif

  if (!surf) {
    surf = SDL_LoadBMP_IO(io, false);
  }

  SDL_CloseIO(io);

  return surf;
}

static int sdl_icon_apply(lk_window *win, SDL_Surface *surf) {
  int ok;

  if (!surf) {
    return 0;
  }

  ok = SDL_SetWindowIcon(win->sdl_win, surf) ? 1 : 0;
  SDL_DestroySurface(surf);

  return ok;
}

static lk_image *sdl_image_from_surface(SDL_Surface *surf);

int lk_window_request_screenshot(lk_window *win, const char *path) {
  char *copy;
  size_t n;

  if (!win || !path) {
    return 0;
  }

  n = strlen(path);
  copy = (char *)malloc(n + 1);

  if (!copy) {
    return 0;
  }

  memcpy(copy, path, n + 1);
  free(win->shot_path);
  win->shot_path = copy;

  return 1;
}

void lk_window_stop(lk_window *win) {
  if (win) {
    win->running = 0;
  }
}

static void sdl_service_screenshot(lk_window *win) {
  SDL_Surface *surf;
  lk_image *img;
  char *path = win->shot_path;
  size_t n;

  if (!path) {
    return;
  }

  win->shot_path = NULL;
  surf = SDL_RenderReadPixels(win->sdl_ren, NULL);
  img = sdl_image_from_surface(surf);

  if (img) {
    n = strlen(path);

    if (n >= 4 && SDL_strcasecmp(path + n - 4, ".png") == 0) {
      if (!lk_image_save_png(img, path)) {
        lk_image_save_bmp(img, path);
      }
    } else {
      lk_image_save_bmp(img, path);
    }

    lk_image_destroy(img);
  }

  free(path);
}

int lk_window_set_icon(lk_window *win, const char *path) {
  if (!win || !win->sdl_win || !path || !*path) {
    return 0;
  }

  return sdl_icon_apply(win, sdl_image_decode(SDL_IOFromFile(path, "rb")));
}

int lk_window_set_icon_mem(lk_window *win, const void *data, lk_u32 len) {
  if (!win || !win->sdl_win || !data || len == 0) {
    return 0;
  }

  return sdl_icon_apply(
      win, sdl_image_decode(SDL_IOFromConstMem(data, (size_t)len)));
}

int lk_window_set_icon_rgba(lk_window *win, int w, int h, const void *pixels,
                            int pitch) {
  SDL_Surface *view;
  SDL_Surface *copy;

  if (!win || !win->sdl_win || !pixels || w <= 0 || h <= 0) {
    return 0;
  }

  if (pitch <= 0) {
    pitch = w * 4;
  }

  if (pitch < w * 4) {
    return 0;
  }

  view = SDL_CreateSurfaceFrom(w, h, SDL_PIXELFORMAT_RGBA32, (void *)pixels,
                               pitch);

  if (!view) {
    return 0;
  }

  copy = SDL_DuplicateSurface(view);
  SDL_DestroySurface(view);

  return sdl_icon_apply(win, copy);
}

static lk_image *sdl_image_from_surface(SDL_Surface *surf) {
  SDL_Surface *conv;
  lk_image *img;
  lk_u8 *dst;
  int y;

  if (!surf) {
    return NULL;
  }

  conv = SDL_ConvertSurface(surf, SDL_PIXELFORMAT_RGBA32);
  SDL_DestroySurface(surf);

  if (!conv) {
    return NULL;
  }

  img = lk_image_new((lk_u32)conv->w, (lk_u32)conv->h, NULL, NULL, NULL);

  if (!img) {
    SDL_DestroySurface(conv);
    return NULL;
  }

  dst = lk_image_pixels(img);

  for (y = 0; y < conv->h; y++) {
    memcpy(dst + (size_t)y * (size_t)conv->w * 4,
           (const Uint8 *)conv->pixels + (size_t)y * (size_t)conv->pitch,
           (size_t)conv->w * 4);
  }

  SDL_DestroySurface(conv);
  return img;
}

lk_image *lk_image_load_file(const char *path) {
  if (!path) {
    return NULL;
  }

  return sdl_image_from_surface(sdl_image_decode(SDL_IOFromFile(path, "rb")));
}

lk_image *lk_image_load_mem(const void *data, lk_u32 len) {
  if (!data || len == 0) {
    return NULL;
  }

  return sdl_image_from_surface(
      sdl_image_decode(SDL_IOFromConstMem(data, (size_t)len)));
}

static SDL_Surface *sdl_image_wrap(const lk_image *img) {
  lk_u32 w = 0;
  lk_u32 h = 0;

  if (!img) {
    return NULL;
  }

  lk_image_size(img, &w, &h);
  return SDL_CreateSurfaceFrom((int)w, (int)h, SDL_PIXELFORMAT_RGBA32,
                               lk_image_pixels((lk_image *)img), (int)(w * 4));
}

int lk_image_save_bmp(const lk_image *img, const char *path) {
  SDL_Surface *surf;
  int ok;

  if (!path) {
    return 0;
  }

  surf = sdl_image_wrap(img);

  if (!surf) {
    return 0;
  }

  ok = SDL_SaveBMP(surf, path) ? 1 : 0;
  SDL_DestroySurface(surf);
  return ok;
}

int lk_image_save_png(const lk_image *img, const char *path) {
#ifdef LK_HAVE_SDL_IMAGE
  SDL_Surface *surf;
  int ok;

  if (!path) {
    return 0;
  }

  surf = sdl_image_wrap(img);

  if (!surf) {
    return 0;
  }

  ok = IMG_SavePNG(surf, path) ? 1 : 0;
  SDL_DestroySurface(surf);
  return ok;
#else
  (void)img;
  (void)path;
  return 0;
#endif
}

static Uint32 g_dialog_event_type;

static lk_window **g_live_windows;
static int g_live_window_count;
static int g_live_window_cap;

static void live_window_add(lk_window *win) {
  if (g_live_window_count == g_live_window_cap) {
    int cap = g_live_window_cap ? g_live_window_cap * 2 : 4;
    lk_window **grown =
        (lk_window **)SDL_realloc(g_live_windows, sizeof(*grown) * (size_t)cap);

    if (!grown) {
      return;
    }

    g_live_windows = grown;
    g_live_window_cap = cap;
  }

  g_live_windows[g_live_window_count++] = win;
}

static void live_window_remove(lk_window *win) {
  int i;

  for (i = 0; i < g_live_window_count; i++) {
    if (g_live_windows[i] == win) {
      g_live_windows[i] = g_live_windows[--g_live_window_count];
      break;
    }
  }

  if (g_live_window_count == 0 && g_live_windows) {
    SDL_free(g_live_windows);
    g_live_windows = NULL;
    g_live_window_cap = 0;
  }
}

static int live_window_check(const lk_window *win) {
  int i;

  for (i = 0; i < g_live_window_count; i++) {
    if (g_live_windows[i] == win) {
      return 1;
    }
  }

  return 0;
}

typedef struct sdl_dialog_ctx {
  lk_window *win;
  lk_file_dialog_fn fn;
  void *ud;
  SDL_DialogFileFilter *filters;
  int nfilters;
  char *default_location;
  char **paths;
  int npaths;
} sdl_dialog_ctx;

static void sdl_dialog_ctx_free(sdl_dialog_ctx *ctx) {
  int i;

  for (i = 0; i < ctx->nfilters; i++) {
    SDL_free((void *)ctx->filters[i].name);
    SDL_free((void *)ctx->filters[i].pattern);
  }

  SDL_free(ctx->filters);
  SDL_free(ctx->default_location);

  for (i = 0; i < ctx->npaths; i++) {
    SDL_free(ctx->paths[i]);
  }

  SDL_free(ctx->paths);
  SDL_free(ctx);
}

static void SDLCALL sdl_dialog_cb(void *userdata, const char *const *filelist,
                                  int filter) {
  sdl_dialog_ctx *ctx = (sdl_dialog_ctx *)userdata;
  SDL_Event ev;
  int n = 0;

  (void)filter;

  if (filelist) {
    while (filelist[n]) {
      n++;
    }
  }

  if (n > 0) {
    ctx->paths = (char **)SDL_calloc((size_t)n, sizeof(char *));

    if (ctx->paths) {
      int i;

      for (i = 0; i < n; i++) {
        ctx->paths[i] = SDL_strdup(filelist[i]);

        if (!ctx->paths[i]) {
          break;
        }

        ctx->npaths = i + 1;
      }
    }
  }

  SDL_zero(ev);
  ev.type = g_dialog_event_type;
  ev.user.data1 = ctx;

  if (!SDL_PushEvent(&ev)) {
    sdl_dialog_ctx_free(ctx);
  }
}

static void sdl_dialog_deliver(const SDL_Event *ev) {
  sdl_dialog_ctx *ctx = (sdl_dialog_ctx *)ev->user.data1;

  if (!ctx) {
    return;
  }

  if (live_window_check(ctx->win) && ctx->fn) {
    ctx->fn(ctx->ud, (const char *const *)ctx->paths, ctx->npaths);
  }

  sdl_dialog_ctx_free(ctx);
}

static void sdl_dialog_drain_for(lk_window *win) {
  SDL_Event *evs = NULL;
  int count = 0;
  int cap = 0;
  int i;

  if (g_dialog_event_type == 0) {
    return;
  }

  for (;;) {
    SDL_Event ev;

    if (SDL_PeepEvents(&ev, 1, SDL_GETEVENT, g_dialog_event_type,
                       g_dialog_event_type) <= 0) {
      break;
    }

    if (count == cap) {
      int grown_cap = cap ? cap * 2 : 8;
      SDL_Event *grown =
          (SDL_Event *)SDL_realloc(evs, sizeof(*grown) * (size_t)grown_cap);

      if (!grown) {
        sdl_dialog_ctx_free((sdl_dialog_ctx *)ev.user.data1);
        continue;
      }

      evs = grown;
      cap = grown_cap;
    }

    evs[count++] = ev;
  }

  for (i = 0; i < count; i++) {
    sdl_dialog_ctx *ctx = (sdl_dialog_ctx *)evs[i].user.data1;

    if (ctx && ctx->win == win) {
      sdl_dialog_ctx_free(ctx);
    } else {
      SDL_PushEvent(&evs[i]);
    }
  }

  SDL_free(evs);
}

static sdl_dialog_ctx *sdl_dialog_ctx_new(lk_window *win,
                                          const lk_file_dialog_filter *filters,
                                          int nfilters,
                                          const char *default_location,
                                          lk_file_dialog_fn fn, void *ud) {
  sdl_dialog_ctx *ctx = (sdl_dialog_ctx *)SDL_calloc(1, sizeof(*ctx));
  int i;

  if (!ctx) {
    return NULL;
  }

  ctx->win = win;
  ctx->fn = fn;
  ctx->ud = ud;

  if (default_location) {
    ctx->default_location = SDL_strdup(default_location);

    if (!ctx->default_location) {
      sdl_dialog_ctx_free(ctx);
      return NULL;
    }
  }

  if (filters && nfilters > 0) {
    ctx->filters = (SDL_DialogFileFilter *)SDL_calloc((size_t)nfilters,
                                                      sizeof(*ctx->filters));

    if (!ctx->filters) {
      sdl_dialog_ctx_free(ctx);
      return NULL;
    }

    for (i = 0; i < nfilters; i++) {
      ctx->filters[i].name = SDL_strdup(filters[i].name ? filters[i].name : "");
      ctx->filters[i].pattern =
          SDL_strdup(filters[i].pattern ? filters[i].pattern : "*");
      ctx->nfilters = i + 1;

      if (!ctx->filters[i].name || !ctx->filters[i].pattern) {
        sdl_dialog_ctx_free(ctx);
        return NULL;
      }
    }
  }

  return ctx;
}

static sdl_dialog_ctx *sdl_dialog_prepare(lk_window *win,
                                          const lk_file_dialog_filter *filters,
                                          int nfilters,
                                          const char *default_location,
                                          lk_file_dialog_fn fn, void *ud) {
  sdl_dialog_ctx *ctx;

  if (!win || !fn) {
    return NULL;
  }

  if (g_dialog_event_type == 0) {
    g_dialog_event_type = SDL_RegisterEvents(1);
  }

  ctx = g_dialog_event_type ? sdl_dialog_ctx_new(win, filters, nfilters,
                                                 default_location, fn, ud)
                            : NULL;

  if (!ctx) {
    fn(ud, NULL, 0);
    return NULL;
  }

  return ctx;
}

void lk_window_open_file_dialog(lk_window *win,
                                const lk_file_dialog_filter *filters,
                                int nfilters, const char *default_location,
                                int allow_many, lk_file_dialog_fn fn,
                                void *ud) {
  sdl_dialog_ctx *ctx =
      sdl_dialog_prepare(win, filters, nfilters, default_location, fn, ud);

  if (!ctx) {
    return;
  }

  SDL_ShowOpenFileDialog(sdl_dialog_cb, ctx, win->sdl_win, ctx->filters,
                         ctx->nfilters, ctx->default_location,
                         allow_many ? true : false);
}

void lk_window_save_file_dialog(lk_window *win,
                                const lk_file_dialog_filter *filters,
                                int nfilters, const char *default_location,
                                lk_file_dialog_fn fn, void *ud) {
  sdl_dialog_ctx *ctx =
      sdl_dialog_prepare(win, filters, nfilters, default_location, fn, ud);

  if (!ctx) {
    return;
  }

  SDL_ShowSaveFileDialog(sdl_dialog_cb, ctx, win->sdl_win, ctx->filters,
                         ctx->nfilters, ctx->default_location);
}

static void sdl_img_cache_shutdown(lk_window *win) {
  int i;

  for (i = 0; i < win->img_cache_count; i++) {
    if (win->img_cache[i].tex) {
      SDL_DestroyTexture(win->img_cache[i].tex);
    }
  }

  win->img_cache_count = 0;
}

static SDL_Texture *sdl_img_texture(lk_window *win, const lk_render_cmd *cmd) {
  lk_resource_ref ref;
  lk_image *img;
  lk_u32 iw = 0;
  lk_u32 ih = 0;
  lk_u32 gen;
  sdl_img_entry *e = NULL;
  int i;

  ref.id = cmd->img_id;
  ref.generation = cmd->img_gen;
  img = (lk_image *)lk_resource_get(lk_ui_resources(win->ui), ref,
                                    lk_image_type());

  if (!img) {
    return NULL;
  }

  lk_image_size(img, &iw, &ih);
  gen = lk_image_generation(img);

  for (i = 0; i < win->img_cache_count; i++) {
    if (win->img_cache[i].res_id == ref.id &&
        win->img_cache[i].res_gen == ref.generation) {
      e = &win->img_cache[i];
      break;
    }
  }

  if (e && e->pix_gen == gen) {
    e->last_frame = win->frame_serial;
    return e->tex;
  }

  if (e && e->w == (int)iw && e->h == (int)ih) {

    SDL_UpdateTexture(e->tex, NULL, lk_image_pixels(img), (int)(iw * 4));
    e->pix_gen = gen;
    e->last_frame = win->frame_serial;
    return e->tex;
  }

  if (e) {

    SDL_DestroyTexture(e->tex);
    e->tex = NULL;
  } else if (win->img_cache_count < SDL_IMG_CACHE_MAX) {
    e = &win->img_cache[win->img_cache_count++];
    memset(e, 0, sizeof(*e));
  } else {

    e = &win->img_cache[0];

    for (i = 1; i < SDL_IMG_CACHE_MAX; i++) {
      if (win->img_cache[i].last_frame < e->last_frame) {
        e = &win->img_cache[i];
      }
    }

    SDL_DestroyTexture(e->tex);
    memset(e, 0, sizeof(*e));
  }

  e->tex = SDL_CreateTexture(win->sdl_ren, SDL_PIXELFORMAT_RGBA32,
                             SDL_TEXTUREACCESS_STATIC, (int)iw, (int)ih);

  if (!e->tex) {

    memset(e, 0, sizeof(*e));
    return NULL;
  }

  SDL_UpdateTexture(e->tex, NULL, lk_image_pixels(img), (int)(iw * 4));
  SDL_SetTextureBlendMode(e->tex, SDL_BLENDMODE_BLEND);
  e->res_id = ref.id;
  e->res_gen = ref.generation;
  e->pix_gen = gen;
  e->w = (int)iw;
  e->h = (int)ih;
  e->last_frame = win->frame_serial;

  return e->tex;
}

static char *g_clip_buf;
static size_t g_clip_cap;

static const char *sdl_clipboard_get(void *ud) {
  char *sdl_text;
  size_t len;

  (void)ud;

  sdl_text = SDL_GetClipboardText();
  if (!sdl_text) {
    len = 0;
  } else {
    len = strlen(sdl_text);
  }

  if (len + 1 > g_clip_cap) {
    size_t cap = g_clip_cap ? g_clip_cap : 4096;
    char *grown;

    while (cap < len + 1) {
      cap *= 2;
    }

    grown = (char *)SDL_realloc(g_clip_buf, cap);

    if (!grown) {

      if (!g_clip_buf) {
        SDL_free(sdl_text);
        return "";
      }

      len = g_clip_cap - 1;
    } else {
      g_clip_buf = grown;
      g_clip_cap = cap;
    }
  }

  if (len > 0) {
    memcpy(g_clip_buf, sdl_text, len);
  }

  g_clip_buf[len] = '\0';
  SDL_free(sdl_text);

  return g_clip_buf;
}

static void sdl_clipboard_shutdown(void) {
  if (g_clip_buf) {
    SDL_free(g_clip_buf);
    g_clip_buf = NULL;
    g_clip_cap = 0;
  }
}

static void sdl_clipboard_set(void *ud, const char *text) {
  (void)ud;
  SDL_SetClipboardText(text);
}

lk_window *lk_window_create(const lk_window_cfg *cfg) {
  lk_window *win;
  const char *title;
  int w;
  int h;

  if (!cfg) {
    return NULL;
  }

  title = cfg->title ? cfg->title : "lk";
  w = cfg->width > 0 ? cfg->width : 800;
  h = cfg->height > 0 ? cfg->height : 600;

  win = (lk_window *)lk_sys_alloc(NULL, (lk_u32)sizeof(lk_window));

  if (!win) {
    return NULL;
  }

  memset(win, 0, sizeof(*win));

  win->width = w;
  win->height = h;

  if (!sdl_global_acquire()) {
    lk_sys_dealloc(NULL, win);
    return NULL;
  }

  win->sdl_win = SDL_CreateWindow(title, w, h, SDL_WINDOW_RESIZABLE);

  if (!win->sdl_win) {
    sdl_global_release();
    lk_sys_dealloc(NULL, win);
    return NULL;
  }

  win->sdl_ren = SDL_CreateRenderer(win->sdl_win, NULL);

  if (!win->sdl_ren) {
    SDL_DestroyWindow(win->sdl_win);
    sdl_global_release();
    lk_sys_dealloc(NULL, win);
    return NULL;
  }

  win->vsync = SDL_SetRenderVSync(win->sdl_ren, 1) ? 1 : 0;

  SDL_SetRenderDrawBlendMode(win->sdl_ren, SDL_BLENDMODE_BLEND);

  win->text_engine = TTF_CreateRendererTextEngine(win->sdl_ren);

  if (!win->text_engine) {
    SDL_Log("lk: TTF_CreateRendererTextEngine failed: %s", SDL_GetError());
  }

  win->face_count = 1;
  win->default_font_size =
      cfg->font_size > 0 ? cfg->font_size : SDL_TEXT_DEFAULT_SIZE;

  win->text_backend.ud = win;
  win->text_backend.measure = sdl_text_measure;
  win->text_backend.x_from_index = sdl_text_x_from_index;
  win->text_backend.index_from_x = sdl_text_index_from_x;
  win->text_backend.line_height = sdl_text_line_height;
  win->text_backend.register_font = sdl_text_register_font;

  if (cfg->font_path) {
    win->face_paths[0] = SDL_strdup(cfg->font_path);

    if (win->face_paths[0] && !sdl_text_instance(win, 0, 0)) {
      SDL_free(win->face_paths[0]);
      win->face_paths[0] = NULL;
    }
  }

  win->running = 1;
  win->ui = lk_ui_create(NULL);

  if (win->ui) {
    lk_ui_set_text_backend(win->ui, sdl_text_have_faces(win)
                                        ? &win->text_backend
                                        : lk_text_backend_stub());
  }

  if (!win->ui) {
    sdl_text_shutdown(win);
    SDL_DestroyRenderer(win->sdl_ren);
    SDL_DestroyWindow(win->sdl_win);
    sdl_global_release();
    lk_sys_dealloc(NULL, win);

    return NULL;
  }

  lk_ui_set_clipboard(win->ui, sdl_clipboard_get, sdl_clipboard_set, NULL);

  if (cfg->icon_path && *cfg->icon_path) {

    (void)lk_window_set_icon(win, cfg->icon_path);
  }

  live_window_add(win);

  return win;
}

void lk_window_destroy(lk_window *win) {
  if (!win) {
    return;
  }

  live_window_remove(win);
  sdl_dialog_drain_for(win);

  lk_render_list_destroy(&win->rl);

  if (win->ui) {
    lk_ui_destroy(win->ui);
  }

  free(win->shot_path);
  win->shot_path = NULL;
  sdl_text_shutdown(win);
  sdl_img_cache_shutdown(win);

  if (win->sdl_ren) {
    SDL_DestroyRenderer(win->sdl_ren);
  }

  if (win->sdl_win) {
    if (win->text_input_active) {
      SDL_StopTextInput(win->sdl_win);
    }

    SDL_DestroyWindow(win->sdl_win);
  }

  if (g_live_window_count == 0) {
    sdl_clipboard_shutdown();
  }

  sdl_global_release();
  lk_sys_dealloc(NULL, win);
}

lk_ui *lk_window_ui(lk_window *win) {
  return win ? win->ui : NULL;
}

lk_u32 lk_window_caps(const lk_window *win) {
  (void)win;

  return LK_WCAP_FILE_DIALOGS | LK_WCAP_ICON | LK_WCAP_CLIPBOARD |
         LK_WCAP_IMAGE_IO | LK_WCAP_POINTER | LK_WCAP_KEY_UP |
         LK_WCAP_KEY_CHORDS;
}

void lk_window_set_event_handler(lk_window *win, lk_event_handler_fn fn,
                                 void *ud) {
  if (win && win->ui) {
    lk_ui_set_event_handler(win->ui, fn, ud);
  }
}

static lk_u16 sdl_to_lk_keycode(SDL_Keycode k) {

  if (k >= 'A' && k <= 'Z') {
    k = k - 'A' + 'a';
  }

  switch (k) {
  case SDLK_TAB: return LKK_TAB;
  case SDLK_RETURN: return LKK_RETURN;
  case SDLK_ESCAPE: return LKK_ESCAPE;
  case SDLK_BACKSPACE: return LKK_BACKSPACE;
  case SDLK_DELETE: return LKK_DELETE;
  case SDLK_SPACE: return LKK_SPACE;
  case SDLK_LEFT: return LKK_LEFT;
  case SDLK_RIGHT: return LKK_RIGHT;
  case SDLK_UP: return LKK_UP;
  case SDLK_DOWN: return LKK_DOWN;
  case SDLK_HOME: return LKK_HOME;
  case SDLK_END: return LKK_END;
  case SDLK_A: return LKK_A;
  case SDLK_B: return LKK_B;
  case SDLK_C: return LKK_C;
  case SDLK_D: return LKK_D;
  case SDLK_E: return LKK_E;
  case SDLK_F: return LKK_F;
  case SDLK_G: return LKK_G;
  case SDLK_H: return LKK_H;
  case SDLK_I: return LKK_I;
  case SDLK_J: return LKK_J;
  case SDLK_K: return LKK_K;
  case SDLK_L: return LKK_L;
  case SDLK_M: return LKK_M;
  case SDLK_N: return LKK_N;
  case SDLK_O: return LKK_O;
  case SDLK_P: return LKK_P;
  case SDLK_Q: return LKK_Q;
  case SDLK_R: return LKK_R;
  case SDLK_S: return LKK_S;
  case SDLK_T: return LKK_T;
  case SDLK_U: return LKK_U;
  case SDLK_V: return LKK_V;
  case SDLK_W: return LKK_W;
  case SDLK_X: return LKK_X;
  case SDLK_Y: return LKK_Y;
  case SDLK_Z: return LKK_Z;
  case SDLK_0: return LKK_0;
  case SDLK_1: return LKK_1;
  case SDLK_2: return LKK_2;
  case SDLK_3: return LKK_3;
  case SDLK_4: return LKK_4;
  case SDLK_5: return LKK_5;
  case SDLK_6: return LKK_6;
  case SDLK_7: return LKK_7;
  case SDLK_8: return LKK_8;
  case SDLK_9: return LKK_9;
  case SDLK_PAGEUP: return LKK_PAGEUP;
  case SDLK_PAGEDOWN: return LKK_PAGEDOWN;
  case SDLK_F1: return LKK_F1;
  case SDLK_F2: return LKK_F2;
  case SDLK_F3: return LKK_F3;
  case SDLK_F4: return LKK_F4;
  case SDLK_F5: return LKK_F5;
  case SDLK_F6: return LKK_F6;
  case SDLK_F7: return LKK_F7;
  case SDLK_F8: return LKK_F8;
  case SDLK_F9: return LKK_F9;
  case SDLK_F10: return LKK_F10;
  case SDLK_F11: return LKK_F11;
  case SDLK_F12: return LKK_F12;
  default: return LKK_UNKNOWN;
  }
}

static lk_u8 sdl_to_lk_mods(SDL_Keymod m) {
  lk_u8 r = 0;

  if (m & SDL_KMOD_SHIFT) {
    r |= (lk_u8)LK_MOD_SHIFT;
  }

  if (m & SDL_KMOD_CTRL) {
    r |= (lk_u8)LK_MOD_CTRL;
  }

  if (m & SDL_KMOD_ALT) {
    r |= (lk_u8)LK_MOD_ALT;
  }

  if (m & SDL_KMOD_GUI) {
    r |= (lk_u8)LK_MOD_GUI;
  }

  return r;
}

static lk_u8 sdl_to_lk_button(Uint8 b) {
  switch (b) {
  case SDL_BUTTON_LEFT: return (lk_u8)LK_POINTER_BUTTON_PRIMARY;
  case SDL_BUTTON_MIDDLE: return (lk_u8)LK_POINTER_BUTTON_MIDDLE;
  case SDL_BUTTON_RIGHT: return (lk_u8)LK_POINTER_BUTTON_SECONDARY;
  default: return (lk_u8)LK_POINTER_BUTTON_ANY;
  }
}

static int sdl_to_lk_event(lk_window *win, const SDL_Event *sdl,
                           lk_event *out) {
  memset(out, 0, sizeof(*out));
  switch (sdl->type) {

  case SDL_EVENT_MOUSE_MOTION:
    out->type = LK_EVENT_POINTER_MOVE;
    out->mods = sdl_to_lk_mods(SDL_GetModState());
    out->data.pointer.x = (lk_i32)sdl->motion.x;
    out->data.pointer.y = (lk_i32)sdl->motion.y;
    return 1;
  case SDL_EVENT_MOUSE_BUTTON_DOWN:
    out->type = LK_EVENT_POINTER_DOWN;
    out->mods = sdl_to_lk_mods(SDL_GetModState());
    out->data.pointer.x = (lk_i32)sdl->button.x;
    out->data.pointer.y = (lk_i32)sdl->button.y;
    out->data.pointer.button = sdl_to_lk_button(sdl->button.button);
    return 1;
  case SDL_EVENT_MOUSE_BUTTON_UP:
    out->type = LK_EVENT_POINTER_UP;
    out->mods = sdl_to_lk_mods(SDL_GetModState());
    out->data.pointer.x = (lk_i32)sdl->button.x;
    out->data.pointer.y = (lk_i32)sdl->button.y;
    out->data.pointer.button = sdl_to_lk_button(sdl->button.button);
    return 1;
  case SDL_EVENT_KEY_DOWN:
    out->type = LK_EVENT_KEY_DOWN;
    out->data.key.keycode = sdl_to_lk_keycode(sdl->key.key);
    out->data.key.repeat = sdl->key.repeat ? 1 : 0;
    out->mods = sdl_to_lk_mods(sdl->key.mod);
    return 1;
  case SDL_EVENT_KEY_UP:
    out->type = LK_EVENT_KEY_UP;
    out->data.key.keycode = sdl_to_lk_keycode(sdl->key.key);
    out->mods = sdl_to_lk_mods(sdl->key.mod);
    return 1;
  case SDL_EVENT_TEXT_INPUT: {
    size_t len = strlen(sdl->text.text);
    out->type = LK_EVENT_TEXT;
    if (len > 31) {
      len = 31;
    }
    memcpy(out->data.text.buf, sdl->text.text, len);
    out->data.text.buf[len] = '\0';
    out->data.text.len = (lk_u8)len;
    return 1;
  }
  case SDL_EVENT_MOUSE_WHEEL: {

    lk_i32 dx, dy;
    win->wheel_acc_x += sdl->wheel.x;
    win->wheel_acc_y += sdl->wheel.y;
    dx = (lk_i32)win->wheel_acc_x;
    dy = (lk_i32)win->wheel_acc_y;

    if (dx == 0 && dy == 0) {
      return 0;
    }

    win->wheel_acc_x -= (float)dx;
    win->wheel_acc_y -= (float)dy;
    out->type = LK_EVENT_WHEEL;
    out->mods = sdl_to_lk_mods(SDL_GetModState());
    out->data.wheel.dx = dx;
    out->data.wheel.dy = dy;
    return 1;
  }
  case SDL_EVENT_WINDOW_RESIZED:
    out->type = LK_EVENT_WINDOW_RESIZE;
    out->data.window.w = sdl->window.data1;
    out->data.window.h = sdl->window.data2;
    return 1;
  default: return 0;
  }
}

int lk_window_step(lk_window *win, lk_frame_fn frame, void *ud) {
  SDL_Event sdl_ev;
  lk_tree *tree;
  const lk_tree *cur;
  lk_layout_cfg lcfg;
  lk_u32 i;
  int have_rects;

  if (!win || !frame || !win->running) {
    return 0;
  }

  win->frame_serial++;

  lk_ui_clear_commands(win->ui);
  lk_ui_set_time_ms(win->ui, (lk_u32)SDL_GetTicks());

  tree = lk_ui_begin_frame(win->ui);
  frame(tree, ud);
  lk_ui_end_frame(win->ui);
  cur = lk_ui_tree(win->ui);

  lk_ui_flush_events(win->ui, cur);

  if (!cur || cur->root == 0) {

    while (SDL_PollEvent(&sdl_ev)) {
      if (sdl_ev.type == SDL_EVENT_QUIT) {
        win->running = 0;
      } else if (g_dialog_event_type != 0 &&
                 sdl_ev.type == g_dialog_event_type) {
        sdl_dialog_deliver(&sdl_ev);
      }
    }
    SDL_SetRenderDrawColor(win->sdl_ren, 0, 0, 0, 255);
    SDL_RenderClear(win->sdl_ren);
    SDL_RenderPresent(win->sdl_ren);

    return win->running;
  }

  lk_ui_resolve_styles(win->ui);

  win->rects = lk_ui_rects(win->ui);

  have_rects = 0;
  if (win->rects) {
    const lk_style *styles = lk_ui_styles(win->ui);
    memset(&lcfg, 0, sizeof(lcfg));

    lcfg.text =
        sdl_text_have_faces(win) ? &win->text_backend : lk_text_backend_stub();

    lk_ui_set_text_backend(win->ui, lcfg.text);

    lcfg.viewport_w = win->width;
    lcfg.viewport_h = win->height;
    lcfg.styles = styles;
    lcfg.state = lk_ui_state(win->ui);

    lcfg.geom = lk_ui_geom(win->ui);

    if (lk_layout(cur, &lcfg, win->rects)) {
      have_rects = 1;
    }
  }

  {
    lk_ix f = lk_focus_current(win->ui, cur);
    int want = (f != 0 && (cur->nodes[f].kind == (lk_u16)UIK_TEXT_INPUT ||
                           cur->nodes[f].kind == (lk_u16)UIK_EDITOR));

    if (want && !win->text_input_active) {
      SDL_StartTextInput(win->sdl_win);
      win->text_input_active = 1;
    } else if (!want && win->text_input_active) {
      SDL_StopTextInput(win->sdl_win);
      win->text_input_active = 0;
    }

    if (want && have_rects) {
      SDL_Rect area;
      area.x = (int)win->rects[f].x;
      area.y = (int)win->rects[f].y;
      area.w = (int)win->rects[f].w;
      area.h = (int)win->rects[f].h;
      SDL_SetTextInputArea(win->sdl_win, &area, 0);
    }
  }

  while (SDL_PollEvent(&sdl_ev)) {
    lk_event lk_ev;

    if (sdl_ev.type == SDL_EVENT_QUIT) {
      win->running = 0;
      break;
    }

    if (g_dialog_event_type != 0 && sdl_ev.type == g_dialog_event_type) {
      sdl_dialog_deliver(&sdl_ev);
      continue;
    }

    if (!sdl_to_lk_event(win, &sdl_ev, &lk_ev)) {
      continue;
    }

    if (lk_ev.type == LK_EVENT_POINTER_MOVE ||
        lk_ev.type == LK_EVENT_POINTER_DOWN ||
        lk_ev.type == LK_EVENT_POINTER_UP) {
      win->mouse_x = lk_ev.data.pointer.x;
      win->mouse_y = lk_ev.data.pointer.y;
    }

    if (lk_ev.type == LK_EVENT_POINTER_MOVE ||
        lk_ev.type == LK_EVENT_POINTER_DOWN ||
        lk_ev.type == LK_EVENT_POINTER_UP) {
      int captured = 0;

      if (lk_ev.type == LK_EVENT_POINTER_MOVE ||
          lk_ev.type == LK_EVENT_POINTER_UP) {
        lk_node_id cap = lk_capture_current(win->ui);

        if (cap != 0) {
          lk_ix cix = lk_tree_find_by_id(cur, cap);

          if (cix != 0) {
            lk_ev.target = cix;
            captured = 1;
          } else {
            lk_capture_clear(win->ui);
          }
        }
      }

      if (!captured && have_rects) {

        lk_ev.target =
            lk_hit_test_overlay(win->ui, win->rects, &lcfg,
                                lk_ev.data.pointer.x, lk_ev.data.pointer.y);

        if (lk_ev.target == 0) {
          lk_ev.target = lk_hit_test(cur, win->rects, lk_ev.data.pointer.x,
                                     lk_ev.data.pointer.y);
        }

        if (lk_ev.type == LK_EVENT_POINTER_DOWN) {
          if (lk_overlay_dismiss_outside(
                  win->ui, win->rects, &lcfg, lk_ev.data.pointer.x,
                  lk_ev.data.pointer.y) == LK_DISMISS_BLOCKED) {
            continue;
          }
        }
      }

      if (!captured) {
        if (lk_ev.target != 0) {
          lk_hover_set(win->ui, cur->nodes[lk_ev.target].id);
        } else {
          lk_hover_clear(win->ui);
        }
      }
    } else if (lk_ev.type == LK_EVENT_WHEEL) {
      if (have_rects) {

        lk_ev.target = lk_hit_test_overlay(win->ui, win->rects, &lcfg,
                                           win->mouse_x, win->mouse_y);

        if (lk_ev.target == 0) {
          lk_ev.target =
              lk_hit_test(cur, win->rects, win->mouse_x, win->mouse_y);
        }
      }
    } else if (lk_ev.type == LK_EVENT_KEY_DOWN ||
               lk_ev.type == LK_EVENT_KEY_UP || lk_ev.type == LK_EVENT_TEXT) {
      lk_ev.target = lk_focus_current(win->ui, cur);
      if (lk_ev.target == 0) {
        lk_ev.target = cur->root;
      }
    } else if (lk_ev.type == LK_EVENT_WINDOW_RESIZE) {
      win->width = lk_ev.data.window.w;
      win->height = lk_ev.data.window.h;
      lk_ev.target = cur->root;
    }

    if (lk_ev.target != 0) {
      lk_event_route(win->ui, &lk_ev);
    }

    if (!lk_ev.handled) {
      if (lk_ev.type == LK_EVENT_KEY_DOWN &&
          lk_ev.data.key.keycode == LKK_TAB) {
        if (lk_ev.mods & (lk_u8)LK_MOD_SHIFT) {
          lk_focus_prev(win->ui, cur);
        } else {
          lk_focus_next(win->ui, cur);
        }
      }

      if (lk_ev.type == LK_EVENT_POINTER_DOWN && lk_ev.target != 0) {
        lk_node_id clicked_id = cur->nodes[lk_ev.target].id;
        lk_focus_set(win->ui, cur, clicked_id);
      }
    }
  }

  lk_ui_flush_events(win->ui, cur);

  if (!win->running) {
    return 0;
  }

  if (!have_rects) {

    SDL_SetRenderDrawColor(win->sdl_ren, 0, 0, 0, 255);
    SDL_RenderClear(win->sdl_ren);
    SDL_RenderPresent(win->sdl_ren);

    return 1;
  }

  lk_layout(cur, &lcfg, win->rects);

  lk_render_build(cur, win->rects, lk_ui_styles(win->ui), lk_ui_state(win->ui),
                  lcfg.geom, &win->rl);

  lk_render_build_overlays(win->ui, win->rects, &lcfg, &win->rl);

  SDL_SetRenderDrawColor(win->sdl_ren, 0, 0, 0, 255);
  SDL_RenderClear(win->sdl_ren);

  {

    SDL_Rect clip_stack[32];
    int clip_sp = 0;

    for (i = 0; i < win->rl.count; i++) {
      const lk_render_cmd *cmd = &win->rl.cmds[i];
      SDL_FRect fr;

      fr.x = (float)cmd->rect.x;
      fr.y = (float)cmd->rect.y;
      fr.w = (float)cmd->rect.w;
      fr.h = (float)cmd->rect.h;

      switch (cmd->op) {
      case LK_ROP_FILL_RECT:
      case LK_ROP_CARET:
        SDL_SetRenderDrawColor(win->sdl_ren, cmd->color.r, cmd->color.g,
                               cmd->color.b, cmd->color.a);
        SDL_RenderFillRect(win->sdl_ren, &fr);
        break;

      case LK_ROP_STROKE_RECT: {

        float bw = (float)(cmd->stroke ? cmd->stroke : 1);
        SDL_FRect e;

        SDL_SetRenderDrawColor(win->sdl_ren, cmd->color.r, cmd->color.g,
                               cmd->color.b, cmd->color.a);
        e = fr;
        e.h = bw;
        SDL_RenderFillRect(win->sdl_ren, &e);
        e.y = fr.y + fr.h - bw;
        SDL_RenderFillRect(win->sdl_ren, &e);
        e = fr;
        e.y += bw;
        e.h -= bw * 2;
        e.w = bw;
        SDL_RenderFillRect(win->sdl_ren, &e);
        e.x = fr.x + fr.w - bw;
        SDL_RenderFillRect(win->sdl_ren, &e);
        break;
      }

      case LK_ROP_DRAW_TEXT:
        if (cmd->str_id != 0) {
          lk_str text = lk_intern_str(cur->intern, cmd->str_id);

          if (text.ptr && text.len > 0) {
            TTF_Font *font =
                sdl_text_instance(win, cmd->font_id, cmd->font_size);
            TTF_Text *scratch = font ? sdl_text_scratch(win, font, text) : NULL;

            if (scratch) {
              TTF_SetTextColor(scratch, cmd->color.r, cmd->color.g,
                               cmd->color.b, cmd->color.a);

              TTF_DrawRendererText(scratch, fr.x, fr.y);
              sdl_text_attrs(win, font, scratch, cmd);
            }
          }
        }

        break;

      case LK_ROP_DRAW_RUN:

        if (cmd->run_len > 0 && win->rl.bytes) {
          lk_str run;
          TTF_Font *font;
          TTF_Text *scratch;

          run.ptr = win->rl.bytes + cmd->run_off;
          run.len = cmd->run_len;

          font = sdl_text_instance(win, cmd->font_id, cmd->font_size);
          scratch = font ? sdl_text_scratch(win, font, run) : NULL;

          if (scratch) {
            TTF_SetTextColor(scratch, cmd->color.r, cmd->color.g, cmd->color.b,
                             cmd->color.a);
            TTF_DrawRendererText(scratch, fr.x, fr.y);
            sdl_text_attrs(win, font, scratch, cmd);
          }
        }

        break;

      case LK_ROP_DRAW_IMAGE: {
        SDL_Texture *tex = sdl_img_texture(win, cmd);

        if (tex) {
          SDL_SetTextureScaleMode(tex, cmd->img_filter == LK_FILTER_NEAREST
                                           ? SDL_SCALEMODE_NEAREST
                                           : SDL_SCALEMODE_LINEAR);
          SDL_SetTextureColorMod(tex, cmd->color.r, cmd->color.g, cmd->color.b);
          SDL_SetTextureAlphaMod(tex, cmd->color.a);
          SDL_RenderTexture(win->sdl_ren, tex, NULL, &fr);
        }

        break;
      }

      case LK_ROP_DRAW_LINES:

        if (cmd->run_len >= 16 && win->rl.bytes) {
          const lk_i32 *xy = (const lk_i32 *)(win->rl.bytes + cmd->run_off);
          int n = (int)(cmd->run_len / 8);

          SDL_SetRenderDrawColor(win->sdl_ren, cmd->color.r, cmd->color.g,
                                 cmd->color.b, cmd->color.a);

          if (cmd->stroke <= 1) {
            SDL_FPoint pts[64];
            int k = 0;
            int j;

            for (j = 0; j < n; j++) {
              pts[k].x = (float)xy[j * 2] + 0.5f;
              pts[k].y = (float)xy[j * 2 + 1] + 0.5f;
              k++;

              if (k == 64 || j + 1 == n) {
                if (k >= 2) {
                  SDL_RenderLines(win->sdl_ren, pts, k);
                }

                pts[0] = pts[k - 1];
                k = 1;
              }
            }
          } else {
            SDL_FColor col;
            float hw = (float)cmd->stroke * 0.5f;
            int j;

            col.r = (float)cmd->color.r / 255.0f;
            col.g = (float)cmd->color.g / 255.0f;
            col.b = (float)cmd->color.b / 255.0f;
            col.a = (float)cmd->color.a / 255.0f;

            for (j = 0; j + 1 < n; j++) {
              float x0 = (float)xy[j * 2] + 0.5f;
              float y0 = (float)xy[j * 2 + 1] + 0.5f;
              float x1 = (float)xy[j * 2 + 2] + 0.5f;
              float y1 = (float)xy[j * 2 + 3] + 0.5f;
              float dx = x1 - x0;
              float dy = y1 - y0;
              float len = SDL_sqrtf(dx * dx + dy * dy);
              float nx;
              float ny;
              SDL_Vertex v[4];
              int idx[6] = {0, 1, 2, 0, 2, 3};
              int q;

              if (len <= 0.0f) {
                continue;
              }

              nx = -dy / len * hw;
              ny = dx / len * hw;

              v[0].position.x = x0 + nx;
              v[0].position.y = y0 + ny;
              v[1].position.x = x1 + nx;
              v[1].position.y = y1 + ny;
              v[2].position.x = x1 - nx;
              v[2].position.y = y1 - ny;
              v[3].position.x = x0 - nx;
              v[3].position.y = y0 - ny;

              for (q = 0; q < 4; q++) {
                v[q].color = col;
                v[q].tex_coord.x = 0.0f;
                v[q].tex_coord.y = 0.0f;
              }

              SDL_RenderGeometry(win->sdl_ren, NULL, v, 4, idx, 6);
            }
          }
        }

        break;

      case LK_ROP_CLIP_BEGIN: {
        SDL_Rect cr;
        cr.x = (int)cmd->rect.x;
        cr.y = (int)cmd->rect.y;
        cr.w = (int)cmd->rect.w;
        cr.h = (int)cmd->rect.h;

        if (clip_sp > 0) {
          SDL_Rect merged;

          if (!SDL_GetRectIntersection(&clip_stack[clip_sp - 1], &cr,
                                       &merged)) {
            merged.x = cr.x;
            merged.y = cr.y;
            merged.w = 0;
            merged.h = 0;
          }

          cr = merged;
        }

        if (clip_sp < (int)(sizeof(clip_stack) / sizeof(clip_stack[0]))) {
          clip_stack[clip_sp++] = cr;
        }

        SDL_SetRenderClipRect(win->sdl_ren, &cr);
        break;
      }

      case LK_ROP_CLIP_END:
        if (clip_sp > 0) {
          clip_sp--;
        }

        if (clip_sp > 0) {
          SDL_SetRenderClipRect(win->sdl_ren, &clip_stack[clip_sp - 1]);
        } else {
          SDL_SetRenderClipRect(win->sdl_ren, NULL);
        }

        break;

      default: break;
      }
    }
  }

  sdl_service_screenshot(win);
  SDL_RenderPresent(win->sdl_ren);

  return win->running;
}

void lk_window_run(lk_window *win, lk_frame_fn frame, void *ud) {
  if (!win || !frame) {
    return;
  }

  win->running = 1;

  while (lk_window_step(win, frame, ud)) {
    if (!win->vsync) {
      SDL_Delay(16);
    }
  }
}
