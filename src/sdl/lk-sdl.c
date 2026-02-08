#include <stdlib.h>
#include <string.h>

#include <SDL3/SDL.h>
#include <SDL3_ttf/SDL_ttf.h>

#include "core/lk-memory.h"
#include "lk-sdl.h"

struct lk_window {
  SDL_Window *sdl_win;
  SDL_Renderer *sdl_ren;
  TTF_Font *font;
  lk_ui *ui;
  lk_rect *rects;
  lk_u32 rects_cap;
  lk_render_list rl;
  int width, height;
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

static void render_text(SDL_Renderer *ren, TTF_Font *font, const char *text,
                        SDL_FRect *dst, lk_color color) {
  SDL_Color c;
  SDL_Surface *surf;
  SDL_Texture *tex;

  c.r = color.r;
  c.g = color.g;
  c.b = color.b;
  c.a = color.a;

  surf = TTF_RenderText_Blended(font, text, 0, c);

  if (!surf) {
    return;
  }

  tex = SDL_CreateTextureFromSurface(ren, surf);
  SDL_DestroySurface(surf);

  if (!tex) {
    return;
  }

  SDL_RenderTexture(ren, tex, NULL, dst);
  SDL_DestroyTexture(tex);
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
              memcpy(buf, text.ptr, text.len);
              buf[text.len] = '\0';
              render_text(win->sdl_ren, win->font, buf, &fr, cmd->color);
              free(buf);
            }
          }
        }

        break;

      default: break;
      }
    }

    SDL_RenderPresent(win->sdl_ren);
    SDL_Delay(16); /* TODO delta time tracking */
  }
}
