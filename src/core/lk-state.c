#include <string.h>

#include <lk.h>
#include "lk-memory.h"

/*
 * lk_state — retained per-node state store.
 *
 * Open-addressing hash table keyed by (lk_node_id, lk_u16 key).
 * Values are lk_value.  No tombstones; deletions rebuild the table.
 */

#define LK_STATE_INIT_CAP 32
#define LK_STATE_LOAD_PCT 70

typedef struct lk_state_entry {
  lk_node_id node;
  lk_u16 key;
  lk_u8 used;
  lk_value value;
} lk_state_entry;

struct lk_state {
  lk_state_entry *tab;
  lk_u32 tab_cap;
  lk_u32 tab_len;
  void *(*alloc)(void *ud, lk_u32 bytes);
  void (*dealloc)(void *ud, void *ptr);
  void *alloc_ud;
};

/* FNV-1a mixing of node id (4 bytes) + key (2 bytes) */
static lk_u32 state_hash(lk_node_id node, lk_u16 key) {
  lk_u32 h = 2166136261u;
  lk_u8 b;

  b = (lk_u8)(node & 0xFF);
  h ^= b; h *= 16777619u;
  b = (lk_u8)((node >> 8) & 0xFF);
  h ^= b; h *= 16777619u;
  b = (lk_u8)((node >> 16) & 0xFF);
  h ^= b; h *= 16777619u;
  b = (lk_u8)((node >> 24) & 0xFF);
  h ^= b; h *= 16777619u;

  b = (lk_u8)(key & 0xFF);
  h ^= b; h *= 16777619u;
  b = (lk_u8)((key >> 8) & 0xFF);
  h ^= b; h *= 16777619u;

  return h;
}

static int state_grow(lk_state *st, lk_u32 new_cap) {
  lk_state_entry *old_tab = st->tab;
  lk_u32 old_cap = st->tab_cap;
  lk_state_entry *new_tab;
  lk_u32 i;

  new_tab = (lk_state_entry *)st->alloc(st->alloc_ud,
             (lk_u32)(sizeof(lk_state_entry) * new_cap));
  if (!new_tab) {
    return 0;
  }

  memset(new_tab, 0, sizeof(lk_state_entry) * new_cap);

  st->tab = new_tab;
  st->tab_cap = new_cap;
  st->tab_len = 0;

  /* Reinsert all used entries */
  if (old_tab) {
    for (i = 0; i < old_cap; i++) {
      if (old_tab[i].used) {
        lk_u32 idx = state_hash(old_tab[i].node, old_tab[i].key) & (new_cap - 1);
        while (new_tab[idx].used) {
          idx = (idx + 1) & (new_cap - 1);
        }
        new_tab[idx] = old_tab[i];
        st->tab_len++;
      }
    }
    st->dealloc(st->alloc_ud, old_tab);
  }

  return 1;
}

static int state_ensure(lk_state *st) {
  if (st->tab_cap == 0) {
    return state_grow(st, LK_STATE_INIT_CAP);
  }
  if (st->tab_len * 100 >= st->tab_cap * LK_STATE_LOAD_PCT) {
    return state_grow(st, st->tab_cap * 2);
  }
  return 1;
}

lk_state *lk_state_create(void *(*al)(void *, lk_u32),
                           void (*de)(void *, void *),
                           void *ud) {
  lk_state *st;

  if (!al) { al = lk_sys_alloc; }
  if (!de) { de = lk_sys_dealloc; }

  st = (lk_state *)al(ud, (lk_u32)sizeof(lk_state));
  if (!st) {
    return NULL;
  }

  memset(st, 0, sizeof(*st));
  st->alloc = al;
  st->dealloc = de;
  st->alloc_ud = ud;

  return st;
}

void lk_state_destroy(lk_state *st) {
  void (*de)(void *, void *);
  void *ud;

  if (!st) {
    return;
  }

  de = st->dealloc;
  ud = st->alloc_ud;

  if (st->tab) {
    de(ud, st->tab);
  }

  de(ud, st);
}

int lk_state_set(lk_state *st, lk_node_id node, lk_u16 key, lk_value v) {
  lk_u32 idx;

  if (!st || !state_ensure(st)) {
    return 0;
  }

  idx = state_hash(node, key) & (st->tab_cap - 1);
  while (st->tab[idx].used) {
    if (st->tab[idx].node == node && st->tab[idx].key == key) {
      /* Overwrite existing */
      st->tab[idx].value = v;
      return 1;
    }
    idx = (idx + 1) & (st->tab_cap - 1);
  }

  /* Insert new */
  st->tab[idx].node = node;
  st->tab[idx].key = key;
  st->tab[idx].used = 1;
  st->tab[idx].value = v;
  st->tab_len++;

  return 1;
}

lk_value lk_state_get(const lk_state *st, lk_node_id node, lk_u16 key) {
  lk_u32 idx;

  if (!st || st->tab_len == 0) {
    return lk_v_none();
  }

  idx = state_hash(node, key) & (st->tab_cap - 1);
  while (st->tab[idx].used) {
    if (st->tab[idx].node == node && st->tab[idx].key == key) {
      return st->tab[idx].value;
    }
    idx = (idx + 1) & (st->tab_cap - 1);
  }

  return lk_v_none();
}

void lk_state_remove_node(lk_state *st, lk_node_id node) {
  lk_u32 i;
  int any_removed = 0;

  if (!st || st->tab_len == 0) {
    return;
  }

  /* Linear scan: mark entries for this node as unused */
  for (i = 0; i < st->tab_cap; i++) {
    if (st->tab[i].used && st->tab[i].node == node) {
      st->tab[i].used = 0;
      st->tab_len--;
      any_removed = 1;
    }
  }

  /* Rebuild at same capacity to fix probe chains */
  if (any_removed) {
    state_grow(st, st->tab_cap);
  }
}

void lk_state_gc(lk_state *st, const lk_changeset *cs) {
  lk_u32 ci, i;
  int any_removed = 0;

  if (!st || !cs || st->tab_len == 0) {
    return;
  }

  for (ci = 0; ci < cs->count; ci++) {
    if (cs->changes[ci].kind != LK_CHANGE_REMOVED) {
      continue;
    }

    /* Remove all entries matching this node id */
    for (i = 0; i < st->tab_cap; i++) {
      if (st->tab[i].used && st->tab[i].node == cs->changes[ci].id) {
        st->tab[i].used = 0;
        st->tab_len--;
        any_removed = 1;
      }
    }
  }

  /* Single rebuild after all deletions */
  if (any_removed) {
    state_grow(st, st->tab_cap);
  }
}
