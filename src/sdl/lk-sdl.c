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
                                   const char *text, lk_color color,
                                   int *out_w, int *out_h) {
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

      c.r = color.r; c.g = color.g; c.b = color.b; c.a = color.a;
      surf = TTF_RenderText_Blended(win->font, text, 0, c);
      if (!surf) return NULL;

      tex = SDL_CreateTextureFromSurface(win->sdl_ren, surf);
      w = surf->w;
      h = surf->h;
      SDL_DestroySurface(surf);
      if (!tex) return NULL;

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

    c.r = color.r; c.g = color.g; c.b = color.b; c.a = color.a;
    surf = TTF_RenderText_Blended(win->font, text, 0, c);
    if (!surf) return NULL;

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
    SDL_DestroyWindow(win->sdl_win);
  }

  TTF_Quit();
  SDL_Quit();
  lk_sys_dealloc(NULL, win);
}

lk_ui *lk_window_ui(lk_window *win) {
  return win ? win->ui : NULL;
}

void lk_window_run(lk_window *win, lk_frame_fn frame, void *ud) {
  if (!win || !frame) {
    return;
  }

  win->running = 1;

  while (win->running) {
    SDL_Event ev;
    lk_tree *tree;
    const lk_tree *cur;
    lk_layout_cfg lcfg;
    lk_u32 i;

    while (SDL_PollEvent(&ev)) {
      if (ev.type == SDL_EVENT_QUIT) {
        win->running = 0;
      } else if (ev.type == SDL_EVENT_WINDOW_RESIZED) {
        win->width = ev.window.data1;
        win->height = ev.window.data2;
      }
    }

    if (!win->running) {
      break;
    }

    tree = lk_ui_begin_frame(win->ui);
    frame(tree, ud);
    lk_ui_end_frame(win->ui);
    cur = lk_ui_tree(win->ui);

    if (!cur || cur->root == 0) {
      SDL_SetRenderDrawColor(win->sdl_ren, 0, 0, 0, 255);
      SDL_RenderClear(win->sdl_ren);
      SDL_RenderPresent(win->sdl_ren);
      SDL_Delay(16);
      continue;
    }

    if (cur->node_count > win->rects_cap) {
      if (win->rects) {
        lk_sys_dealloc(NULL, win->rects);
      }

      win->rects_cap = cur->node_count;
      win->rects = (lk_rect *)lk_sys_alloc(
          NULL, (lk_u32)(sizeof(lk_rect) * win->rects_cap));
    }

    if (!win->rects) {
      SDL_Delay(16);
      continue;
    }

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

    if (!lk_layout(cur, &lcfg, win->rects)) {
      SDL_Delay(16);
      continue;
    }

    lk_render_build(cur, win->rects, &win->rl);

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
              tex = text_cache_get(win, cmd->str_id, buf, cmd->color,
                                   &tw, &th);
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

      case LK_ROP_CLIP_END:
        SDL_SetRenderClipRect(win->sdl_ren, NULL);
        break;

      default: break;
      }
    }

    SDL_RenderPresent(win->sdl_ren);
    SDL_Delay(16); /* TODO delta time tracking */
  }
}
