#include <stdlib.h>
#include <string.h>

#include <SDL3/SDL.h>
#include <SDL3_ttf/SDL_ttf.h>

#include "core/lk-memory.h"
#include "lk-sdl.h"

/* ------- Text texture cache ------- */

#define TEXT_CACHE_DEFAULT_CAP 2048

typedef struct text_cache_entry {
  lk_u32 str_id;    /* 0 = empty slot */
  lk_u32 color_key; /* packed RGBA — same string in two colors = two entries */
  SDL_Texture *tex;
  int w, h;
  lk_u32 last_frame; /* frame counter when last accessed */
} text_cache_entry;

static lk_u32 pack_color(lk_color c) {
  return ((lk_u32)c.r << 24) | ((lk_u32)c.g << 16) | ((lk_u32)c.b << 8) |
         (lk_u32)c.a;
}

typedef struct text_cache_stats {
  lk_u32 hits;
  lk_u32 misses;
  lk_u32 evictions;
  lk_u32 probe_steps_total;
  lk_u32 probe_steps_max;
} text_cache_stats;

struct lk_window {
  SDL_Window *sdl_win;
  SDL_Renderer *sdl_ren;
  TTF_Font *font;
  lk_text_backend text_backend; /* ud = font; see sdl_text_* */
  lk_ui *ui;
  lk_rect *rects;
  lk_u32 rects_cap;
  lk_render_list rl;
  text_cache_entry *text_cache;
  lk_u32 text_cache_cap;
  lk_u32 frame_counter;
  text_cache_stats cache_stats;
  int warned_probe;
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

/* ------- Text backend (minimal adapter over the single global font;
 * the real TTF_TextEngine implementation is stage B of the
 * text-contract plan) ------- */

/* Measure text.ptr[0..len) with the window's single TTF_Font.
 * font_id/font_size are ignored until stage B (one global font). */
static void sdl_text_size(TTF_Font *font, lk_str text, int *out_w, int *out_h) {
  int w = 0;
  int h = 0;
  char stack_buf[256];
  char *buf = stack_buf;

  *out_w = 0;
  *out_h = 0;

  if (!font || text.len == 0) {
    return;
  }

  /* NUL-terminate for SDL_ttf; heap only for long strings */
  if (text.len + 1 > sizeof(stack_buf)) {
    buf = (char *)malloc(text.len + 1);

    if (!buf) {
      return;
    }
  }

  memcpy(buf, text.ptr, text.len);
  buf[text.len] = '\0';

  TTF_GetStringSize(font, buf, 0, &w, &h);

  if (buf != stack_buf) {
    free(buf);
  }

  *out_w = w;
  *out_h = h;
}

static void sdl_text_measure(void *ud, lk_str run, lk_u16 font_id,
                             lk_u16 font_size, lk_text_metrics *out) {
  TTF_Font *font = (TTF_Font *)ud;
  int w;
  int h;

  (void)font_id;
  (void)font_size;

  if (!out) {
    return;
  }

  sdl_text_size(font, run, &w, &h);
  out->w = (lk_i32)w;
  out->h = (lk_i32)h;
  out->baseline = font ? (lk_i32)TTF_GetFontAscent(font) : 0;
}

static lk_i32 sdl_text_x_from_index(void *ud, lk_str run, lk_u16 font_id,
                                    lk_u16 font_size, lk_u32 byte_ix) {
  TTF_Font *font = (TTF_Font *)ud;
  lk_str prefix;
  int w;
  int h;

  (void)font_id;
  (void)font_size;

  prefix.ptr = run.ptr;
  prefix.len = byte_ix > run.len ? run.len : byte_ix;
  sdl_text_size(font, prefix, &w, &h);
  return (lk_i32)w;
}

/* Linear scan of prefix widths picking the nearest codepoint
 * boundary.  O(n^2) — interim until stage B (TTF_TextEngine's
 * TTF_GetTextSubStringForPoint). */
static lk_u32 sdl_text_index_from_x(void *ud, lk_str run, lk_u16 font_id,
                                    lk_u16 font_size, lk_i32 x) {
  TTF_Font *font = (TTF_Font *)ud;
  lk_u32 best_ix = 0;
  lk_i32 best_dist = x < 0 ? -x : x; /* distance to boundary 0 (x=0) */
  lk_u32 i = 0;

  (void)font_id;
  (void)font_size;

  if (x <= 0) {
    return 0;
  }

  while (i < run.len) {
    lk_str prefix;
    int w;
    int h;
    lk_i32 dist;

    /* advance to next codepoint boundary */
    i++;
    while (i < run.len && ((unsigned char)run.ptr[i] & 0xC0) == 0x80) {
      i++;
    }

    prefix.ptr = run.ptr;
    prefix.len = i;
    sdl_text_size(font, prefix, &w, &h);
    dist = x - (lk_i32)w;

    if (dist < 0) {
      dist = -dist;
    }

    /* <= so ties round toward the later boundary */
    if (dist <= best_dist) {
      best_dist = dist;
      best_ix = i;
    }
  }

  return best_ix;
}

static lk_i32 sdl_text_line_height(void *ud, lk_u16 font_id,
                                   lk_u16 font_size) {
  TTF_Font *font = (TTF_Font *)ud;

  (void)font_id;
  (void)font_size;

  return font ? (lk_i32)TTF_GetFontHeight(font) : 0;
}

static lk_u16 sdl_text_register_font(void *ud, const char *path) {
  /* Stage B: face registry.  Until then only the default face exists. */
  (void)ud;
  (void)path;
  return 0;
}

static void text_cache_clear(lk_window *win) {
  lk_u32 i;

  if (!win->text_cache) {
    return;
  }

  for (i = 0; i < win->text_cache_cap; i++) {
    if (win->text_cache[i].tex) {
      SDL_DestroyTexture(win->text_cache[i].tex);
    }
  }

  memset(win->text_cache, 0, sizeof(text_cache_entry) * win->text_cache_cap);
}

/* Look up or create a cached text texture.  Returns the texture, sets
 * out_w/out_h to the texture dimensions.  Returns NULL on failure.
 *
 * On miss: probes for a match or empty slot, tracking the stalest entry
 * seen.  Inserts into the empty slot if found, otherwise evicts the
 * stalest entry in the probe chain.  Cache always owns the returned
 * texture — caller must not destroy it.
 */
static SDL_Texture *text_cache_get(lk_window *win, lk_u32 str_id, lk_str text,
                                   lk_color color, int *out_w, int *out_h) {
  lk_u32 mask = win->text_cache_cap - 1;
  lk_u32 color_key = pack_color(color);
  /* Mix color into the home slot so two colors of one string don't
   * always land in the same probe chain. */
  lk_u32 slot = (str_id ^ (color_key * 2654435761u)) & mask;
  lk_u32 probes = 0;
  lk_u32 stalest_slot = slot;
  lk_u32 stalest_frame = (lk_u32)~0u;
  int found_empty = 0;
  lk_u32 empty_slot = 0;

  /* Linear probe: look for match or empty slot */
  while (probes < win->text_cache_cap) {
    text_cache_entry *e = &win->text_cache[slot];
    probes++;

    if (e->str_id == 0) {
      empty_slot = slot;
      found_empty = 1;
      break;
    }

    if (e->str_id == str_id && e->color_key == color_key) {
      /* Cache hit */
      e->last_frame = win->frame_counter;
      win->cache_stats.hits++;
      win->cache_stats.probe_steps_total += probes;

      if (probes > win->cache_stats.probe_steps_max) {
        win->cache_stats.probe_steps_max = probes;
      }

      *out_w = e->w;
      *out_h = e->h;
      return e->tex;
    }

    /* Track stalest entry in probe chain for potential eviction */
    if (e->last_frame < stalest_frame) {
      stalest_frame = e->last_frame;
      stalest_slot = slot;
    }

    slot = (slot + 1) & mask;
  }

  /* Cache miss — render the texture and insert */
  win->cache_stats.misses++;
  win->cache_stats.probe_steps_total += probes;

  if (probes > win->cache_stats.probe_steps_max) {
    win->cache_stats.probe_steps_max = probes;
  }

  /* One-time warning for long probe chains */
  if (!win->warned_probe && win->cache_stats.probe_steps_max > 32) {
    SDL_Log("lk: text cache max probe reached %u (cap=%u).",
            win->cache_stats.probe_steps_max, win->text_cache_cap);
    win->warned_probe = 1;
  }

  {
    SDL_Color c;
    SDL_Surface *surf;
    SDL_Texture *tex;
    text_cache_entry *target;
    int w, h;
    char stack_buf[256];
    char *buf = stack_buf;

    /* NUL-terminate for SDL_ttf; heap only for long strings, and only
     * on the miss path — hits never allocate. */
    if (text.len + 1 > sizeof(stack_buf)) {
      buf = (char *)malloc(text.len + 1);

      if (!buf) {
        return NULL;
      }
    }

    memcpy(buf, text.ptr, text.len);
    buf[text.len] = '\0';

    c.r = color.r;
    c.g = color.g;
    c.b = color.b;
    c.a = color.a;
    surf = TTF_RenderText_Blended(win->font, buf, 0, c);

    if (buf != stack_buf) {
      free(buf);
    }

    if (!surf) {
      return NULL;
    }

    tex = SDL_CreateTextureFromSurface(win->sdl_ren, surf);
    w = surf->w;
    h = surf->h;
    SDL_DestroySurface(surf);

    if (!tex) {
      return NULL;
    }

    if (found_empty) {
      target = &win->text_cache[empty_slot];
    } else {
      /* Evict stalest entry in probe chain */
      target = &win->text_cache[stalest_slot];

      if (target->tex) {
        SDL_DestroyTexture(target->tex);
      }

      win->cache_stats.evictions++;
    }

    target->str_id = str_id;
    target->color_key = color_key;
    target->tex = tex;
    target->w = w;
    target->h = h;
    target->last_frame = win->frame_counter;

    *out_w = w;
    *out_h = h;
    return tex;
  }
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

  if (cfg->font_path) {
    win->font = TTF_OpenFont(cfg->font_path, (float)cfg->font_size);
    /* NULL font is OK — falls back to the stub text backend */
  }

  win->text_backend.ud = win->font;
  win->text_backend.measure = sdl_text_measure;
  win->text_backend.x_from_index = sdl_text_x_from_index;
  win->text_backend.index_from_x = sdl_text_index_from_x;
  win->text_backend.line_height = sdl_text_line_height;
  win->text_backend.register_font = sdl_text_register_font;

  /* Allocate text cache (dies wholesale in stage B) */
  {
    lk_u32 cache_cap = TEXT_CACHE_DEFAULT_CAP;
    win->text_cache = (text_cache_entry *)lk_sys_alloc(
        NULL, (lk_u32)(sizeof(text_cache_entry) * cache_cap));

    if (!win->text_cache) {
      if (win->font) {
        TTF_CloseFont(win->font);
      }

      SDL_DestroyRenderer(win->sdl_ren);
      SDL_DestroyWindow(win->sdl_win);
      sdl_global_release();
      lk_sys_dealloc(NULL, win);
      return NULL;
    }

    memset(win->text_cache, 0, sizeof(text_cache_entry) * cache_cap);
    win->text_cache_cap = cache_cap;
  }

  /* Text input is started on demand when a text-entry widget gains
   * focus (see lk_window_run) so IME/on-screen keyboards only engage
   * when a field is actually focused. */

  win->ui = lk_ui_create(NULL);

  if (!win->ui) {
    lk_sys_dealloc(NULL, win->text_cache);

    if (win->font) {
      TTF_CloseFont(win->font);
    }

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

  text_cache_clear(win);

  if (win->text_cache) {
    lk_sys_dealloc(NULL, win->text_cache);
  }

  lk_render_list_destroy(&win->rl);

  if (win->rects) {
    lk_sys_dealloc(NULL, win->rects);
  }

  if (win->ui) {
    lk_ui_destroy(win->ui);
  }

  if (win->font) {
    TTF_CloseFont(win->font);
  }

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
    out->data.pointer.button = (lk_u8)sdl->button.button;
    return 1;
  case SDL_EVENT_MOUSE_BUTTON_UP:
    out->type = LK_EVENT_POINTER_UP;
    out->data.pointer.x = (lk_i32)sdl->button.x;
    out->data.pointer.y = (lk_i32)sdl->button.y;
    out->data.pointer.button = (lk_u8)sdl->button.button;
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

      lcfg.text = win->font ? &win->text_backend : lk_text_backend_stub();

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
      int want = (f != 0 && cur->nodes[f].kind == (lk_u16)UIK_TEXT_INPUT);

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
        if (have_rects) {
          /* Overlay hit-test first (popups draw on top of everything) */
          const lk_style *styles = lk_ui_styles(win->ui);
          const lk_state *state = lk_ui_state(win->ui);

          lk_ev.target = lk_hit_test_overlay(cur, win->rects, styles, state,
                                              &lcfg, lk_ev.data.pointer.x,
                                              lk_ev.data.pointer.y);

          if (lk_ev.target == 0) {
            lk_ev.target =
                lk_hit_test(cur, win->rects, lk_ev.data.pointer.x,
                            lk_ev.data.pointer.y);
          }

          /* Pointer-down outside any open overlay closes it.
           * Call before routing so the click still fires on whatever
           * the user clicked. */
          if (lk_ev.type == LK_EVENT_POINTER_DOWN) {
            lk_overlay_dismiss_outside(win->ui, win->rects, styles, &lcfg,
                                        lk_ev.data.pointer.x,
                                        lk_ev.data.pointer.y);
          }
        }

        /* Update hover state */
        if (lk_ev.target != 0) {
          lk_hover_set(win->ui, cur->nodes[lk_ev.target].id);
        } else {
          lk_hover_clear(win->ui);
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

    if (!win->running) {
      break;
    }

    if (!have_rects) {
      SDL_Delay(16);
      continue;
    }

    /* 5. Render */
    lk_render_build(cur, win->rects, lk_ui_styles(win->ui),
                    lk_ui_state(win->ui), &win->rl);

    /* 5b. Overlays (dropdown popups) draw on top of the main tree.
     * See docs/overlays.md for the roadmap to a generalized system. */
    lk_render_build_overlays(cur, win->rects, lk_ui_styles(win->ui),
                              lk_ui_state(win->ui), &lcfg, &win->rl);

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
          if (win->font && cmd->str_id != 0) {
            lk_str text = lk_intern_str(cur->intern, cmd->str_id);

            if (text.ptr && text.len > 0) {
              int tw, th;
              SDL_Texture *tex =
                  text_cache_get(win, cmd->str_id, text, cmd->color, &tw, &th);

              if (tex) {
                /* Draw at the texture's natural size — stretching to
                 * the command rect distorts glyphs when the widget is
                 * wider/narrower than the text. Overflow is handled by
                 * the active clip. */
                fr.w = (float)tw;
                fr.h = (float)th;
                SDL_RenderTexture(win->sdl_ren, tex, NULL, &fr);
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
    win->frame_counter++;

    /* With vsync, RenderPresent paces the loop; only sleep manually
     * when vsync is unavailable. */
    if (!win->vsync) {
      SDL_Delay(16);
    }
  }
}
