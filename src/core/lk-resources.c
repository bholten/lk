#include <string.h>

#include "lk-memory.h"
#include "lk-resources.h"
#include <lk.h>

/*
 * lk_resources — typed, generation-checked resource reference table
 * (docs/editor.md §5).
 *
 * Slots are addressed by ref.id - 1 (id 0 is the null ref).  The
 * table never owns registered objects: release only invalidates the
 * ref by bumping the slot generation and clearing the object.  A
 * released slot is reused by a later register, which hands out a ref
 * carrying the bumped generation — the old ref stays dead.
 */

typedef struct lk_resource_slot {
  const lk_resource_type *type;
  void *obj;
  char *debug_name; /* copied with the table's allocator; NULL when
                       the slot is not live */
  lk_u32 generation;
  lk_u8 live;
} lk_resource_slot;

struct lk_resources {
  lk_resource_slot *slots;
  lk_u32 count;
  lk_u32 cap;
  void *(*alloc)(void *ud, lk_u32 bytes);
  void (*dealloc)(void *ud, void *ptr);
  void *ud;
};

static int resources_reserve(lk_resources *rs, lk_u32 need) {
  lk_resource_slot *ns;
  lk_u32 new_cap;

  if (need <= rs->cap) {
    return 1;
  }

  new_cap = rs->cap ? rs->cap : 16;

  while (new_cap < need) {
    new_cap *= 2;
  }

  ns = (lk_resource_slot *)rs->alloc(
      rs->ud, (lk_u32)(sizeof(lk_resource_slot) * new_cap));

  if (!ns) {
    return 0;
  }

  memset(ns, 0, sizeof(lk_resource_slot) * new_cap);

  if (rs->slots) {
    memcpy(ns, rs->slots, sizeof(lk_resource_slot) * rs->count);
    rs->dealloc(rs->ud, rs->slots);
  }

  rs->slots = ns;
  rs->cap = new_cap;

  return 1;
}

lk_resources *lk_resources_new(void *(*alloc)(void *, lk_u32),
                               void (*dealloc)(void *, void *), void *ud) {
  lk_resources *rs;

  if (!alloc) {
    alloc = lk_sys_alloc;
  }

  if (!dealloc) {
    dealloc = lk_sys_dealloc;
  }

  rs = (lk_resources *)alloc(ud, (lk_u32)sizeof(lk_resources));

  if (!rs) {
    return NULL;
  }

  memset(rs, 0, sizeof(*rs));
  rs->alloc = alloc;
  rs->dealloc = dealloc;
  rs->ud = ud;

  return rs;
}

void lk_resources_destroy(lk_resources *rs) {
  lk_u32 i;

  if (!rs) {
    return;
  }

  for (i = 0; i < rs->count; i++) {
    if (rs->slots[i].debug_name) {
      rs->dealloc(rs->ud, rs->slots[i].debug_name);
    }
  }

  if (rs->slots) {
    rs->dealloc(rs->ud, rs->slots);
  }

  rs->dealloc(rs->ud, rs);
}

lk_resource_ref lk_resource_register(lk_resources *rs,
                                     const lk_resource_type *type, void *obj,
                                     const char *debug_name) {
  lk_resource_ref ref;
  lk_resource_slot *s;
  lk_u32 slot_ix;
  lk_u32 i;
  lk_u32 name_len;
  char *name_copy;

  ref.id = 0;
  ref.generation = 0;

  if (!rs || !type) {
    return ref;
  }

  /* Reuse a released slot when one exists — its generation was bumped
   * on release, so refs to the previous occupant stay dead. */
  slot_ix = rs->count;

  for (i = 0; i < rs->count; i++) {
    if (!rs->slots[i].live) {
      slot_ix = i;
      break;
    }
  }

  if (slot_ix == rs->count && !resources_reserve(rs, rs->count + 1)) {
    return ref;
  }

  if (!debug_name) {
    debug_name = "";
  }

  name_len = (lk_u32)strlen(debug_name);
  name_copy = (char *)rs->alloc(rs->ud, name_len + 1);

  if (!name_copy) {
    return ref;
  }

  memcpy(name_copy, debug_name, name_len + 1);

  s = &rs->slots[slot_ix];

  if (slot_ix == rs->count) {
    /* Fresh slot: generations start at 1 so a zeroed ref never
     * resolves. */
    s->generation = 1;
    rs->count++;
  }

  s->type = type;
  s->obj = obj;
  s->debug_name = name_copy;
  s->live = 1;

  ref.id = slot_ix + 1;
  ref.generation = s->generation;

  return ref;
}

void lk_resource_release(lk_resources *rs, lk_resource_ref ref) {
  lk_resource_slot *s;

  if (!rs || ref.id == 0 || ref.id > rs->count) {
    return;
  }

  s = &rs->slots[ref.id - 1];

  if (!s->live || s->generation != ref.generation) {
    return; /* stale ref: no-op */
  }

  if (s->debug_name) {
    rs->dealloc(rs->ud, s->debug_name);
  }

  s->debug_name = NULL;
  s->type = NULL;
  s->obj = NULL;
  s->generation++;
  s->live = 0;
}

void *lk_resource_get(const lk_resources *rs, lk_resource_ref ref,
                      const lk_resource_type *type) {
  const lk_resource_slot *s;

  if (!rs || ref.id == 0 || ref.id > rs->count) {
    return NULL;
  }

  s = &rs->slots[ref.id - 1];

  if (!s->live || s->generation != ref.generation) {
    return NULL;
  }

  if (s->type != type) {
    return NULL;
  }

  return s->obj;
}

int lk_resources_lookup_names(const lk_resources *rs, lk_resource_ref ref,
                              const char **type_name, const char **debug_name) {
  const lk_resource_slot *s;

  if (!rs || ref.id == 0 || ref.id > rs->count) {
    return 0;
  }

  s = &rs->slots[ref.id - 1];

  if (!s->live || s->generation != ref.generation) {
    return 0;
  }

  *type_name = (s->type && s->type->name) ? s->type->name : "";
  *debug_name = s->debug_name ? s->debug_name : "";

  return 1;
}
