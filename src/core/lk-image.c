

#include <string.h>

#include "lk-image.h"
#include "lk-memory.h"
#include <lk.h>

#define LK_IMAGE_MAX_DIM 16384u

struct lk_image {
  lk_u32 w;
  lk_u32 h;
  lk_u32 generation;
  lk_u8 *pixels;

  void *(*alloc)(void *, lk_u32);
  void (*dealloc)(void *, void *);
  void *ud;
};

lk_image *lk_image_new(lk_u32 w, lk_u32 h, void *(*alloc)(void *, lk_u32),
                       void (*dealloc)(void *, void *), void *ud) {
  lk_image *img;
  lk_u32 bytes;

  if (w == 0 || h == 0 || w > LK_IMAGE_MAX_DIM || h > LK_IMAGE_MAX_DIM) {
    return NULL;
  }

  if (!alloc) {
    alloc = lk_sys_alloc;
  }
  if (!dealloc) {
    dealloc = lk_sys_dealloc;
  }

  img = (lk_image *)alloc(ud, sizeof(lk_image));

  if (!img) {
    return NULL;
  }

  bytes = w * h * 4;
  memset(img, 0, sizeof(*img));
  img->w = w;
  img->h = h;
  img->generation = 1;
  img->alloc = alloc;
  img->dealloc = dealloc;
  img->ud = ud;
  img->pixels = (lk_u8 *)alloc(ud, bytes);

  if (!img->pixels) {
    dealloc(ud, img);
    return NULL;
  }

  memset(img->pixels, 0, bytes);
  return img;
}

void lk_image_destroy(lk_image *img) {
  if (!img) {
    return;
  }

  img->dealloc(img->ud, img->pixels);
  img->dealloc(img->ud, img);
}

lk_u8 *lk_image_pixels(lk_image *img) {
  return img ? img->pixels : NULL;
}

void lk_image_size(const lk_image *img, lk_u32 *w, lk_u32 *h) {
  if (w) {
    *w = img ? img->w : 0;
  }
  if (h) {
    *h = img ? img->h : 0;
  }
}

void lk_image_mark_dirty(lk_image *img) {
  if (img) {
    img->generation++;
  }
}

lk_u32 lk_image_generation(const lk_image *img) {
  return img ? img->generation : 0;
}

static const lk_resource_type g_image_type = {"image", NULL};

const lk_resource_type *lk_image_type(void) {
  return &g_image_type;
}

static lk_resource_ref image_node_ref(const lk_tree *t, lk_ix n) {
  const lk_node *nd;
  lk_u32 k;
  lk_resource_ref none;

  none.id = 0;
  none.generation = 0;

  if (!t || n == 0 || n >= t->node_count) {
    return none;
  }

  nd = &t->nodes[n];

  for (k = 0; k < nd->props_len; k++) {
    const lk_prop *p = &t->props[nd->props_off + k];

    if (p->key == UIP_IMAGE && p->value.tag == UIV_RESOURCE) {
      return lk_v_resource_ref(p->value);
    }
  }

  return none;
}

lk_image *lk_image_from_node(const lk_resources *rs, const lk_tree *t,
                             lk_ix n) {
  lk_resource_ref ref;

  if (!rs) {
    return NULL;
  }

  ref = image_node_ref(t, n);

  if (ref.id == 0) {
    return NULL;
  }

  return (lk_image *)lk_resource_get(rs, ref, &g_image_type);
}

static void measure_image(const lk_tree *t, lk_ix n, const lk_size *sizes,
                          const lk_layout_cfg *cfg, lk_i32 *out_w,
                          lk_i32 *out_h) {
  const lk_image *img = lk_image_from_node(t->resources, t, n);

  (void)sizes;
  (void)cfg;

  if (img) {
    *out_w = (lk_i32)img->w;
    *out_h = (lk_i32)img->h;
  } else {
    *out_w = 0;
    *out_h = 0;
  }
}

static lk_u8 image_filter(const lk_tree *t, lk_ix n) {
  if (lk_node_prop_i32(t, n, UIP_FILTER, LK_FILTER_LINEAR) ==
      LK_FILTER_NEAREST) {
    return (lk_u8)LK_FILTER_NEAREST;
  }

  return (lk_u8)LK_FILTER_LINEAR;
}

static void render_image(const lk_tree *t, lk_ix n, const lk_rect *rect,
                         const lk_style *style, const lk_state *state,
                         const lk_widget_geom *geom, lk_render_list *out) {
  lk_resource_ref ref;
  lk_render_cmd cmd;

  (void)state;
  (void)geom;

  memset(&cmd, 0, sizeof(cmd));
  cmd.op = LK_ROP_FILL_RECT;
  cmd.rect = *rect;
  cmd.color = style->bg;
  lk_render_list_push(out, cmd);

  ref = image_node_ref(t, n);

  if (ref.id == 0 || !lk_resource_get(t->resources, ref, &g_image_type)) {
    return;
  }

  memset(&cmd, 0, sizeof(cmd));
  cmd.op = LK_ROP_DRAW_IMAGE;
  cmd.rect = *rect;
  cmd.color.r = 255;
  cmd.color.g = 255;
  cmd.color.b = 255;
  cmd.color.a = 255;
  cmd.img_id = ref.id;
  cmd.img_gen = ref.generation;
  cmd.img_filter = image_filter(t, n);
  lk_render_list_push(out, cmd);
}

lk_widget_def lk_image_widget_def(void) {
  lk_widget_def def;

  memset(&def, 0, sizeof(def));
  def.measure = measure_image;
  def.render = render_image;

  return def;
}
