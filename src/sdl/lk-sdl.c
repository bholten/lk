#include <stdlib.h>
#include <string.h>

#include <SDL3/SDL.h>
#include <SDL3_ttf/SDL_ttf.h>

#include "core/lk-memory.h"
#include "lk-sdl.h"

/* ------- Text backend: face registry, (face, size) instances,
 * TTF_TextEngine rendering (text-contract stage B) ------- */

#define SDL_TEXT_MAX_FACES 16
#define SDL_TEXT_MAX_INSTANCES 32
#define SDL_TEXT_DEFAULT_SIZE 16

/* One lazily-opened TTF_Font per (face, px size) pair. SDL_ttf sizes
 * are per-open; resizing a shared handle would thrash its glyph
 * cache. */
typedef struct sdl_font_instance {
  lk_u16 face_id;
  int size; /* resolved px size, never 0 */
  TTF_Font *font;
} sdl_font_instance;

struct lk_window {
  SDL_Window *sdl_win;
  SDL_Renderer *sdl_ren;
  TTF_TextEngine *text_engine; /* glyph-atlas engine bound to sdl_ren */
  TTF_Text *scratch_text;      /* reused for drawing + index<->x mapping */
  char *face_paths[SDL_TEXT_MAX_FACES]; /* [0] = cfg->font_path (may be NULL) */
  int face_count;                       /* >= 1; slot 0 always reserved */
  int default_font_size;                /* cfg->font_size, fallback 16 */
  sdl_font_instance instances[SDL_TEXT_MAX_INSTANCES];
  int instance_count;
  lk_text_backend text_backend; /* ud = lk_window*; see sdl_text_* */
  lk_ui *ui;
  lk_rect *rects;
  lk_u32 rects_cap;
  lk_render_list rl;
  int width;
  int height;
  int running;
  lk_i32 mouse_x, mouse_y;
  float wheel_acc_x, wheel_acc_y; /* fractional wheel deltas (trackpads) */
  int text_input_active;          /* SDL text input currently started */
  int vsync;                      /* renderer vsync enabled */
};

/* SDL_Init/TTF_Init are process-global; refcount so multiple windows
 * (or destroy of one) don't tear down SDL for the others. */
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

/* Resolve (face_id, size) to a TTF_Font*, lazily opening and caching
 * the instance.  size 0 resolves to the window default size.  Returns
 * NULL for unknown faces, faces without a path (no default face), or
 * open failures. */
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

/* Any real face available?  Decides real backend vs stub in the run
 * loop (face 0 may be absent while registered faces exist). */
static int sdl_text_have_faces(const lk_window *win) {
  return win->face_paths[0] != NULL || win->face_count > 1;
}

/* Point the scratch TTF_Text at (font, run).  NOTE: SDL_ttf treats
 * length 0 as "null-terminated", so an empty run must pass a literal
 * "" — run.ptr is not NUL-terminated. */
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
    /* Contract: x_from_index(run, run.len) == measure(run).w — use
     * the same TTF_GetStringSize path as measure. */
    int w = 0;
    int h = 0;
    TTF_GetStringSize(font, run.ptr, run.len, &w, &h);
    return (lk_i32)w;
  }

  text = sdl_text_scratch(win, font, run);

  if (!text || !TTF_GetTextSubString(text, (int)byte_ix, &sub)) {
    return 0;
  }

  /* Left edge of the glyph cluster containing byte_ix (byte_ix is
   * codepoint-aligned per contract, so it is the cluster start). */
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

  /* Nearest boundary: at or past the midpoint of the containing
   * cluster snaps to the boundary after it (ties round later, like
   * the stub backend). */
  ix = (lk_u32)sub.offset;

  if (sub.length > 0 && (x - sub.rect.x) * 2 >= sub.rect.w) {
    ix = (lk_u32)(sub.offset + sub.length);
  }

  if (ix > run.len) {
    ix = run.len;
  }

  return ix;
}

static lk_i32 sdl_text_line_height(void *ud, lk_u16 font_id,
                                   lk_u16 font_size) {
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

  /* Open at the default size immediately so unreadable paths fail at
   * registration, not first use.  The opened instance is kept. */
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

/* Destroy scratch text, close all font instances, destroy the text
 * engine, free face paths.  Order matters: the scratch TTF_Text
 * references a font and the engine; the engine must go before the
 * renderer (caller destroys the renderer after this). */
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

/* ---- Clipboard callbacks ---- */

static const char *sdl_clipboard_get(void *ud) {
  static char clip_buf[4096];
  char *sdl_text;
  size_t len;

  (void)ud;

  sdl_text = SDL_GetClipboardText();
  if (!sdl_text) {
    clip_buf[0] = '\0';
    return clip_buf;
  }

  len = strlen(sdl_text);
  if (len >= sizeof(clip_buf)) {
    len = sizeof(clip_buf) - 1;
  }

  memcpy(clip_buf, sdl_text, len);
  clip_buf[len] = '\0';
  SDL_free(sdl_text);

  return clip_buf;
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

  /* SDL's default draw blend mode is NONE, which ignores alpha in
   * fill colors — semi-transparent fills (modal scrim, selection
   * highlights) would paint opaque. */
  SDL_SetRenderDrawBlendMode(win->sdl_ren, SDL_BLENDMODE_BLEND);

  /* Glyph-atlas text engine bound to the renderer.  A NULL engine is
   * survivable: measurement still works via font instances; DRAW_TEXT
   * and index<->x mapping degrade to no-ops. */
  win->text_engine = TTF_CreateRendererTextEngine(win->sdl_ren);

  if (!win->text_engine) {
    SDL_Log("lk: TTF_CreateRendererTextEngine failed: %s", SDL_GetError());
  }

  /* Face registry: slot 0 is the default face from cfg. */
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

    /* Verify readable now (mirrors register_font).  Unreadable paths
     * degrade to "no default face" — the stub text backend. */
    if (win->face_paths[0] && !sdl_text_instance(win, 0, 0)) {
      SDL_free(win->face_paths[0]);
      win->face_paths[0] = NULL;
    }
  }

  /* Text input is started on demand when a text-entry widget gains
   * focus (see lk_window_run) so IME/on-screen keyboards only engage
   * when a field is actually focused. */

  win->ui = lk_ui_create(NULL);

  if (!win->ui) {
    sdl_text_shutdown(win);
    SDL_DestroyRenderer(win->sdl_ren);
    SDL_DestroyWindow(win->sdl_win);
    sdl_global_release();
    lk_sys_dealloc(NULL, win);

    return NULL;
  }

  lk_ui_set_clipboard(win->ui, sdl_clipboard_get, sdl_clipboard_set, NULL);

  return win;
}

void lk_window_destroy(lk_window *win) {
  if (!win) {
    return;
  }

  lk_render_list_destroy(&win->rl);

  if (win->rects) {
    lk_sys_dealloc(NULL, win->rects);
  }

  if (win->ui) {
    lk_ui_destroy(win->ui);
  }

  sdl_text_shutdown(win);

  if (win->sdl_ren) {
    SDL_DestroyRenderer(win->sdl_ren);
  }

  if (win->sdl_win) {
    if (win->text_input_active) {
      SDL_StopTextInput(win->sdl_win);
    }

    SDL_DestroyWindow(win->sdl_win);
  }

  sdl_global_release();
  lk_sys_dealloc(NULL, win);
}

lk_ui *lk_window_ui(lk_window *win) {
  return win ? win->ui : NULL;
}

void lk_window_set_event_handler(lk_window *win, lk_event_handler_fn fn,
                                 void *ud) {
  if (win && win->ui) {
    lk_ui_set_event_handler(win->ui, fn, ud);
  }
}

static lk_u16 sdl_to_lk_keycode(SDL_Keycode k) {
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

/* SDL button number -> lk-owned lk_pointer_button.  Extra buttons
 * (X1/X2, ...) become ANY: unmatched by button-specific translators,
 * still visible to button-wildcard ones. */
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
    out->data.pointer.x = (lk_i32)sdl->motion.x;
    out->data.pointer.y = (lk_i32)sdl->motion.y;
    return 1;
  case SDL_EVENT_MOUSE_BUTTON_DOWN:
    out->type = LK_EVENT_POINTER_DOWN;
    out->data.pointer.x = (lk_i32)sdl->button.x;
    out->data.pointer.y = (lk_i32)sdl->button.y;
    out->data.pointer.button = sdl_to_lk_button(sdl->button.button);
    return 1;
  case SDL_EVENT_MOUSE_BUTTON_UP:
    out->type = LK_EVENT_POINTER_UP;
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
    /* Trackpads deliver fractional deltas; accumulate and emit whole
     * steps so smooth scrolling isn't truncated to zero. */
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

void lk_window_run(lk_window *win, lk_frame_fn frame, void *ud) {
  if (!win || !frame) {
    return;
  }

  win->running = 1;

  while (win->running) {
    SDL_Event sdl_ev;
    lk_tree *tree;
    const lk_tree *cur;
    lk_layout_cfg lcfg;
    lk_u32 i;
    int have_rects;

    /* 0. Clear per-frame command queue */
    lk_ui_clear_commands(win->ui);

    /* 1. Build frame */
    tree = lk_ui_begin_frame(win->ui);
    frame(tree, ud);
    lk_ui_end_frame(win->ui);
    cur = lk_ui_tree(win->ui);

    /* Drain synthetic events from between-frame mutations (end_frame
     * focus GC, host API calls made from the frame callback). */
    lk_ui_flush_events(win->ui, cur);

    if (!cur || cur->root == 0) {
      /* Poll events even with empty tree to handle quit */
      while (SDL_PollEvent(&sdl_ev)) {
        if (sdl_ev.type == SDL_EVENT_QUIT) {
          win->running = 0;
        }
      }
      SDL_SetRenderDrawColor(win->sdl_ren, 0, 0, 0, 255);
      SDL_RenderClear(win->sdl_ren);
      SDL_RenderPresent(win->sdl_ren);
      SDL_Delay(16);
      continue;
    }

    /* 2. Resolve styles */
    lk_ui_resolve_styles(win->ui);

    /* 3. Layout */
    if (cur->node_count > win->rects_cap) {
      if (win->rects) {
        lk_sys_dealloc(NULL, win->rects);
      }

      win->rects_cap = cur->node_count;
      win->rects = (lk_rect *)lk_sys_alloc(
          NULL, (lk_u32)(sizeof(lk_rect) * win->rects_cap));
    }

    have_rects = 0;
    if (win->rects) {
      const lk_style *styles = lk_ui_styles(win->ui);
      memset(&lcfg, 0, sizeof(lcfg));

      lcfg.text =
          sdl_text_have_faces(win) ? &win->text_backend : lk_text_backend_stub();
      /* Same backend for widget event handlers (click-to-position). */
      lk_ui_set_text_backend(win->ui, lcfg.text);

      lcfg.viewport_w = win->width;
      lcfg.viewport_h = win->height;
      lcfg.styles = styles;
      lcfg.state = lk_ui_state(win->ui);

      if (lk_layout(cur, &lcfg, win->rects)) {
        have_rects = 1;
      }
    }

    /* 3.5. Engage SDL text input only while a text-entry widget is
     * focused, and tell the IME where the field is so composition
     * windows appear next to it. */
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

    /* 4. Poll events (after layout so we have rects for hit-testing) */
    while (SDL_PollEvent(&sdl_ev)) {
      lk_event lk_ev;

      if (sdl_ev.type == SDL_EVENT_QUIT) {
        win->running = 0;
        break;
      }

      if (!sdl_to_lk_event(win, &sdl_ev, &lk_ev)) {
        continue;
      }

      /* Track mouse position */
      if (lk_ev.type == LK_EVENT_POINTER_MOVE ||
          lk_ev.type == LK_EVENT_POINTER_DOWN ||
          lk_ev.type == LK_EVENT_POINTER_UP) {
        win->mouse_x = lk_ev.data.pointer.x;
        win->mouse_y = lk_ev.data.pointer.y;
      }

      /* Set target */
      if (lk_ev.type == LK_EVENT_POINTER_MOVE ||
          lk_ev.type == LK_EVENT_POINTER_DOWN ||
          lk_ev.type == LK_EVENT_POINTER_UP) {
        int captured = 0;

        /* Pointer capture: while a node holds the capture (e.g. a
         * split divider mid-drag), MOVE/UP events target it directly
         * (bypassing hit-test) and hover updates are suppressed.
         * Capture referencing a node that no longer resolves in the
         * current tree is dropped. */
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
          /* Overlay hit-test first (popups draw on top of everything) */
          lk_ev.target = lk_hit_test_overlay(win->ui, win->rects, &lcfg,
                                             lk_ev.data.pointer.x,
                                             lk_ev.data.pointer.y);

          if (lk_ev.target == 0) {
            lk_ev.target =
                lk_hit_test(cur, win->rects, lk_ev.data.pointer.x,
                            lk_ev.data.pointer.y);
          }

          /* Pointer-down outside any open overlay closes it.
           * Call before routing so the click still fires on whatever
           * the user clicked.  A modal (focus-trapping, non-dismissing)
           * overlay consumes the click instead — skip routing. */
          if (lk_ev.type == LK_EVENT_POINTER_DOWN) {
            if (lk_overlay_dismiss_outside(win->ui, win->rects, &lcfg,
                                           lk_ev.data.pointer.x,
                                           lk_ev.data.pointer.y) ==
                LK_DISMISS_BLOCKED) {
              continue;
            }
          }
        }

        /* Update hover state (suppressed while a capture is active) */
        if (!captured) {
          if (lk_ev.target != 0) {
            lk_hover_set(win->ui, cur->nodes[lk_ev.target].id);
          } else {
            lk_hover_clear(win->ui);
          }
        }
      } else if (lk_ev.type == LK_EVENT_WHEEL) {
        if (have_rects) {
          lk_ev.target =
              lk_hit_test(cur, win->rects, win->mouse_x, win->mouse_y);
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

      /* Route through tree */
      if (lk_ev.target != 0) {
        lk_event_route(win->ui, &lk_ev);
      }

      /* Built-in behaviors (only if not already handled) */
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

    /* Drain synthetic events from the built-in behaviors above
     * (click-to-focus, tab cycling run outside lk_event_route). */
    lk_ui_flush_events(win->ui, cur);

    if (!win->running) {
      break;
    }

    if (!have_rects) {
      SDL_Delay(16);
      continue;
    }

    /* 4.5. Events mutate state after layout ran — editor documents
     * (which invalidate their geometry), scroll offsets, split
     * ratios.  Re-lay out so this frame renders what the events did
     * instead of a one-frame-stale (or, for the editor, blanked)
     * view.  Hit-testing above deliberately used the pre-event
     * rects. */
    lk_layout(cur, &lcfg, win->rects);

    /* 5. Render */
    lk_render_build(cur, win->rects, lk_ui_styles(win->ui),
                    lk_ui_state(win->ui), &win->rl);

    /* 5b. Overlays (the ui's overlay stack: dropdown popups, subtree
     * overlays) draw on top of the main tree.  See docs/overlays.md. */
    lk_render_build_overlays(win->ui, win->rects, &lcfg, &win->rl);

    SDL_SetRenderDrawColor(win->sdl_ren, 0, 0, 0, 255);
    SDL_RenderClear(win->sdl_ren);

    {
      /* Clip stack: CLIP_END must restore the *enclosing* clip, not
       * clear clipping entirely (nested clippers: window > scroll). */
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
          SDL_SetRenderDrawColor(win->sdl_ren, cmd->color.r, cmd->color.g,
                                 cmd->color.b, cmd->color.a);
          SDL_RenderFillRect(win->sdl_ren, &fr);
          break;

        case LK_ROP_DRAW_TEXT:
          if (cmd->str_id != 0) {
            lk_str text = lk_intern_str(cur->intern, cmd->str_id);

            if (text.ptr && text.len > 0) {
              TTF_Font *font =
                  sdl_text_instance(win, cmd->font_id, cmd->font_size);
              TTF_Text *scratch =
                  font ? sdl_text_scratch(win, font, text) : NULL;

              if (scratch) {
                TTF_SetTextColor(scratch, cmd->color.r, cmd->color.g,
                                 cmd->color.b, cmd->color.a);
                /* The engine draws at the text's natural size —
                 * stretching to the command rect would distort glyphs.
                 * Overflow is handled by the active clip. */
                TTF_DrawRendererText(scratch, fr.x, fr.y);
              }
            }
          }

          break;

        case LK_ROP_DRAW_RUN:
          /* Like DRAW_TEXT, but the bytes live in the render list's
           * own arena.  Empty runs are skipped entirely — the
           * documented TTF_SetTextString len-0 gotcha. */
          if (cmd->run_len > 0 && win->rl.bytes) {
            lk_str run;
            TTF_Font *font;
            TTF_Text *scratch;

            run.ptr = win->rl.bytes + cmd->run_off;
            run.len = cmd->run_len;

            font = sdl_text_instance(win, cmd->font_id, cmd->font_size);
            scratch = font ? sdl_text_scratch(win, font, run) : NULL;

            if (scratch) {
              TTF_SetTextColor(scratch, cmd->color.r, cmd->color.g,
                               cmd->color.b, cmd->color.a);
              TTF_DrawRendererText(scratch, fr.x, fr.y);
            }
          }

          break;

        case LK_ROP_CLIP_BEGIN: {
          SDL_Rect cr;
          cr.x = (int)cmd->rect.x;
          cr.y = (int)cmd->rect.y;
          cr.w = (int)cmd->rect.w;
          cr.h = (int)cmd->rect.h;

          /* Nested clips intersect with the enclosing clip */
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

    SDL_RenderPresent(win->sdl_ren);

    /* With vsync, RenderPresent paces the loop; only sleep manually
     * when vsync is unavailable. */
    if (!win->vsync) {
      SDL_Delay(16);
    }
  }
}
