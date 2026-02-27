#include <stdlib.h>
#include <string.h>

#include <SDL3/SDL.h>
#include <SDL3_ttf/SDL_ttf.h>

#include "core/lk-memory.h"
#include "lk-sdl.h"

/* ------- Text texture cache ------- */

#define TEXT_CACHE_CAP 256 /* must be power of two */

typedef struct text_cache_entry {
  lk_u32 str_id; /* 0 = empty slot */
  SDL_Texture *tex;
  int w, h;
} text_cache_entry;

struct lk_window {
  SDL_Window *sdl_win;
  SDL_Renderer *sdl_ren;
  TTF_Font *font;
  lk_ui *ui;
  lk_rect *rects;
  lk_u32 rects_cap;
  lk_render_list rl;
  text_cache_entry text_cache[TEXT_CACHE_CAP];
  int width;
  int height;
  int running;
  lk_i32 mouse_x, mouse_y;
};

static void sdl_measure_text(void *ud, lk_str text, lk_i32 *out_w,
                             lk_i32 *out_h) {
  TTF_Font *font = (TTF_Font *)ud;
  int w = 0;
  int h = 0;
  char *buf;

  if (!font || text.len == 0) {
    if (out_w) {
      *out_w = 0;
    }

    if (out_h) {
      *out_h = 0;
    }

    return;
  }

  buf = (char *)malloc(text.len + 1);
  if (!buf) {
    if (out_w) {
      *out_w = 0;
    }

    if (out_h) {
      *out_h = 0;
    }

    return;
  }

  memcpy(buf, text.ptr, text.len);
  buf[text.len] = '\0';

  TTF_GetStringSize(font, buf, 0, &w, &h);
  free(buf);

  if (out_w) {
    *out_w = (lk_i32)w;
  }

  if (out_h) {
    *out_h = (lk_i32)h;
  }
}

static void text_cache_clear(lk_window *win) {
  int i;
  for (i = 0; i < TEXT_CACHE_CAP; i++) {
    if (win->text_cache[i].tex) {
      SDL_DestroyTexture(win->text_cache[i].tex);
    }

    win->text_cache[i].str_id = 0;
    win->text_cache[i].tex = NULL;
  }
}

/* Look up or create a cached text texture.  Returns the texture, sets
 * out_w/out_h to the texture dimensions.  Returns NULL on failure.
 */
static SDL_Texture *text_cache_get(lk_window *win, lk_u32 str_id,
                                   const char *text, lk_color color, int *out_w,
                                   int *out_h) {
  unsigned slot = str_id & (TEXT_CACHE_CAP - 1);
  int probes = 0;

  /* Linear probe for existing entry */
  while (probes < TEXT_CACHE_CAP) {
    text_cache_entry *e = &win->text_cache[slot];
    if (e->str_id == 0) {
      /* Empty slot — not cached.  Render and insert. */
      SDL_Color c;
      SDL_Surface *surf;
      SDL_Texture *tex;
      int w, h;

      c.r = color.r;
      c.g = color.g;
      c.b = color.b;
      c.a = color.a;
      surf = TTF_RenderText_Blended(win->font, text, 0, c);

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

      e->str_id = str_id;
      e->tex = tex;
      e->w = w;
      e->h = h;

      *out_w = w;
      *out_h = h;
      return tex;
    }

    if (e->str_id == str_id) {
      /* Cache hit */
      *out_w = e->w;
      *out_h = e->h;
      return e->tex;
    }

    slot = (slot + 1) & (TEXT_CACHE_CAP - 1);
    probes++;
  }

  /* Cache full — render without caching (should be rare) */
  {
    SDL_Color c;
    SDL_Surface *surf;
    SDL_Texture *tex;

    c.r = color.r;
    c.g = color.g;
    c.b = color.b;
    c.a = color.a;
    surf = TTF_RenderText_Blended(win->font, text, 0, c);
    if (!surf) {
      return NULL;
    }

    tex = SDL_CreateTextureFromSurface(win->sdl_ren, surf);
    *out_w = surf->w;
    *out_h = surf->h;
    SDL_DestroySurface(surf);
    return tex;
  }
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

  if (!SDL_Init(SDL_INIT_VIDEO)) {
    lk_sys_dealloc(NULL, win);
    return NULL;
  }

  if (!TTF_Init()) {
    SDL_Quit();
    lk_sys_dealloc(NULL, win);
    return NULL;
  }

  win->sdl_win = SDL_CreateWindow(title, w, h, SDL_WINDOW_RESIZABLE);

  if (!win->sdl_win) {
    TTF_Quit();
    SDL_Quit();
    lk_sys_dealloc(NULL, win);
    return NULL;
  }

  win->sdl_ren = SDL_CreateRenderer(win->sdl_win, NULL);

  if (!win->sdl_ren) {
    SDL_DestroyWindow(win->sdl_win);
    TTF_Quit();
    SDL_Quit();
    lk_sys_dealloc(NULL, win);
    return NULL;
  }

  if (cfg->font_path) {
    win->font = TTF_OpenFont(cfg->font_path, (float)cfg->font_size);
    /* NULL font is OK — falls back to stub measurer */
  }

  SDL_StartTextInput(win->sdl_win);

  win->ui = lk_ui_create(NULL);

  if (!win->ui) {
    if (win->font) {
      TTF_CloseFont(win->font);
    }

    SDL_DestroyRenderer(win->sdl_ren);
    SDL_DestroyWindow(win->sdl_win);
    TTF_Quit();
    SDL_Quit();
    lk_sys_dealloc(NULL, win);

    return NULL;
  }

  return win;
}

void lk_window_destroy(lk_window *win) {
  if (!win) {
    return;
  }

  text_cache_clear(win);
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
    SDL_StopTextInput(win->sdl_win);
    SDL_DestroyWindow(win->sdl_win);
  }

  TTF_Quit();
  SDL_Quit();
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

static int sdl_to_lk_event(const SDL_Event *sdl, lk_event *out) {
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
  case SDL_EVENT_MOUSE_WHEEL:
    out->type = LK_EVENT_WHEEL;
    out->data.wheel.dx = (lk_i32)sdl->wheel.x;
    out->data.wheel.dy = (lk_i32)sdl->wheel.y;
    return 1;
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

      if (win->font) {
        lcfg.measure_text = sdl_measure_text;
        lcfg.measure_ud = win->font;
      } else {
        lcfg.measure_text = lk_measure_text_stub;
        lcfg.measure_ud = NULL;
      }

      lcfg.viewport_w = win->width;
      lcfg.viewport_h = win->height;
      lcfg.styles = styles;
      lcfg.state = lk_ui_state(win->ui);

      if (lk_layout(cur, &lcfg, win->rects)) {
        have_rects = 1;
      }
    }

    /* 4. Poll events (after layout so we have rects for hit-testing) */
    while (SDL_PollEvent(&sdl_ev)) {
      lk_event lk_ev;

      if (sdl_ev.type == SDL_EVENT_QUIT) {
        win->running = 0;
        break;
      }

      if (!sdl_to_lk_event(&sdl_ev, &lk_ev)) {
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
          lk_ev.target = lk_hit_test(cur, win->rects, lk_ev.data.pointer.x,
                                     lk_ev.data.pointer.y);
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

    SDL_SetRenderDrawColor(win->sdl_ren, 0, 0, 0, 255);
    SDL_RenderClear(win->sdl_ren);

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
            char *buf = (char *)malloc(text.len + 1);

            if (buf) {
              int tw, th;
              SDL_Texture *tex;
              memcpy(buf, text.ptr, text.len);
              buf[text.len] = '\0';
              tex = text_cache_get(win, cmd->str_id, buf, cmd->color, &tw, &th);

              if (tex) {
                SDL_RenderTexture(win->sdl_ren, tex, NULL, &fr);
              }

              free(buf);
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
        SDL_SetRenderClipRect(win->sdl_ren, &cr);
        break;
      }

      case LK_ROP_CLIP_END: SDL_SetRenderClipRect(win->sdl_ren, NULL); break;

      default: break;
      }
    }

    SDL_RenderPresent(win->sdl_ren);
    SDL_Delay(16);
  }
}
