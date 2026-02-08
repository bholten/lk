#include <memory.h>
#include <stdlib.h>
#include <string.h>

#include "lk-data.h"
#include "lk-memory.h"

static lk_u32 lk_hash_bytes(const char *p, lk_u32 n) {
  lk_u32 h = 2166136261u;
  lk_u32 i;

  for (i = 0; i < n; i++) {
    h ^= (lk_u8)p[i];
    h *= 16777619u;
  }

  /* Avoid 0 to keep "none" sentinel if you ever need it */
  if (h == 0) {
    h = 1;
  }

  return h;
}

struct lk_intern_entry {
  lk_u32 hash;
  lk_u32 str_off;
  lk_u32 str_len;
  lk_node_id id;
  lk_u8 used;
};

struct lk_intern {
  void *(*alloc)(void *, lk_u32);
  void (*dealloc)(void *, void *);
  void *ud;

  struct lk_intern_entry *tab;
  lk_u32 tab_cap;
  lk_u32 tab_len;

  char *pool;
  lk_u32 pool_cap;
  lk_u32 pool_len;

  struct lk_intern_entry **by_id;
  lk_u32 by_id_cap;
  lk_u32 by_id_len;
};

static void *it_alloc(lk_intern *it, lk_u32 bytes) {
  if (it->alloc) {
    return it->alloc(it->ud, bytes);
  }

  return lk_sys_alloc(NULL, bytes);
}

static void it_dealloc(lk_intern *it, void *ptr) {
  if (!ptr) {
    return;
  }

  if (it->dealloc) {
    it->dealloc(it->ud, ptr);
  } else {
    lk_sys_dealloc(NULL, ptr);
  }
}

static int it_grow_table(lk_intern *it, lk_u32 new_cap) {
  struct lk_intern_entry *old = it->tab;
  lk_u32 old_cap = it->tab_cap;
  lk_u32 i;

  it->tab = (struct lk_intern_entry *)
      it_alloc(it, sizeof(struct lk_intern_entry) * new_cap);

  if (!it->tab) {
    it->tab = old;

    return 0;
  }

  memset(it->tab, 0, sizeof(struct lk_intern_entry) * new_cap);

  it->tab_cap = new_cap;
  it->tab_len = 0;

  /* rehash */
  for (i = 0; i < old_cap; i++) {
    struct lk_intern_entry e = old[i];

    if (!e.used) {
      continue;
    }

    /* insertt */
    {
      lk_u32 mask = new_cap - 1;
      lk_u32 idx = e.hash & mask;

      while (it->tab[idx].used) {
        idx = (idx + 1) & mask;
      }

      it->tab[idx] = e;
      it->tab[idx].used = 1;
      it->tab_len++;
    }
  }

  if (old) {
    it_dealloc(it, old);
  }

  return 1;
}

static int it_ensure(lk_intern *it) {
  if (it->tab_cap == 0) {
    if (!it_grow_table(it, 64)) {
      return 0;
    }
  }

  if ((it->tab_len + 1) * 10 >= it->tab_cap * 7) {
    lk_u32 new_cap = it->tab_cap * 2;

    if (new_cap < 64) {
      new_cap = 64;
    }

    if (!it_grow_table(it, new_cap)) {
      return 0;
    }
  }

  return 1;
}

static int it_pool_reserve(lk_intern *it, lk_u32 add_len) {
  lk_u32 need = it->pool_len + add_len;
  char *np;
  lk_u32 new_cap;

  if (need <= it->pool_cap) {
    return 1;
  }

  new_cap = it->pool_cap ? it->pool_cap : 1024;

  while (new_cap < need) {
    new_cap *= 2;
  }

  np = (char *)it_alloc(it, new_cap);

  if (!np) {
    return 0;
  }

  if (it->pool && it->pool_len) {
    memcpy(np, it->pool, it->pool_len);
  }

  if (it->pool) {
    it_dealloc(it, it->pool);
  }

  it->pool = np;
  it->pool_cap = new_cap;

  return 1;
}

static int it_by_id_reserve(lk_intern *it) {
  lk_u32 need = it->by_id_cap + 1;
  struct lk_intern_entry **nb;
  lk_u32 new_cap;

  if (need <= it->by_id_cap) {
    return 1;
  }

  new_cap = it->by_id_cap ? it->by_id_cap : 64;

  while (new_cap < need) {
    new_cap *= 2;
  }

  nb = (struct lk_intern_entry **)it_alloc(
      it, sizeof(struct lk_intern_entry *) * new_cap);

  if (!nb) {
    return 0;
  }

  if (it->by_id && it->by_id_len) {
    memcpy(nb, it->by_id, sizeof(struct lk_intern_entry*) * it->by_id_len);
  }

  if (it->by_id) {
    it_dealloc(it, it->by_id);
  }

  it->by_id = nb;
  it->by_id_cap = new_cap;

  return 1;
}

lk_intern *lk_intern_new(void* (*alloc)(void*, lk_u32), void *alloc_ud) {
  lk_intern *it;

  if (alloc) {
    it = alloc(alloc_ud, (lk_u32)sizeof(lk_intern));
  } else {
    it = lk_sys_alloc(NULL, (lk_u32)sizeof(lk_intern));
  }

  if (!it) {
    return NULL;
  }

  memset(it, 0, sizeof(*it));

  it->alloc = alloc ? alloc : lk_sys_alloc;
  /* if custom alloc, user should provide dealloc via different
     create; keep simple */
  it->dealloc = alloc ? 0 : lk_sys_dealloc;
  it->ud = alloc_ud;

  /* NOTE: if we want symmetric custom alloc/dealloc, adjust create signature. */
  (void)alloc_ud;

  if (!it_ensure(it)) {
    lk_intern_destroy(it);
    return NULL;
  }

  return it;
}

void lk_intern_destroy(lk_intern *it) {
  if (!it) {
    return;
  }

  if (it->tab) {
    it_dealloc(it, it->tab);
  }

  if (it->pool) {
    it_dealloc(it, it->pool);
  }

  if (it->by_id) {
    it_dealloc(it, it->by_id);
  }

  /* best effort free */
  it_dealloc(it, it);
}

lk_node_id lk_intern_id(lk_intern *it, lk_str s) {
  lk_u32 h;
  lk_u32 cap;
  lk_u32 mask;
  lk_u32 idx;

  if (!it || !s.ptr) {
    return 0;
  }

  if (!it_ensure(it)) {
    return 0;
  }

  h = lk_hash_bytes(s.ptr, s.len);
  cap = it->tab_cap;
  mask = cap - 1;
  idx = h & mask;

  for (;;) {
    struct lk_intern_entry *e = &it->tab[idx];

    if (!e->used) {
      break;
    }

    if (e->hash == h && e->str_len == s.len) {
      lk_str existing;
      existing.ptr = it->pool + e->str_off;
      existing.len = e->str_len;

      if (lk_str_cmp(existing, s)) {
        return e->id;
      }
    }

    idx = (idx + 1) & mask;
  }

  if (!it_pool_reserve(it, s.len)) {
    return 0;
  }

  if (!it_by_id_reserve(it)) {
    return 0;
  }

  {
    lk_u32 off = it->pool_len;
    memcpy(it->pool + off, s.ptr, s.len);
    it->pool_len += s.len;

    {
      struct lk_intern_entry *e = &it->tab[idx];
      e->used = 1;
      e->hash = h;
      e->str_off = off;
      e->str_len = s.len;
      e->id = (lk_node_id)(it->by_id_len + 1); /* IDs start at 1 */
      it->tab_len++;

      it->by_id[it->by_id_len] = e;
      it->by_id_len++;

      return e->id;
    }
  }
}

lk_str lk_intern_str(const lk_intern *it, lk_node_id id) {
  lk_str s;
  s.ptr = 0;
  s.len = 0;

  if (!it || id == 0) {
    return s;
  }

  if (id > it->by_id_len) {
    return s;
  }

  {
    struct lk_intern_entry *e = it->by_id[id - 1];

    if (!e) {
      return s;
    }

    s.ptr = it->pool + e->str_off;
    s.len = e->str_len;

    return s;
  }
}

