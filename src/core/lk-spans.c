/*
 * lk-spans.c -- lk_spans: an application-owned, sorted, non-overlapping
 * span set over a STYLED_TEXT's bytes (docs/styled-text.md section 3).
 *
 * Each entry pairs appearance (lk_text_span) with an optional
 * presentation (interned ptype + typed value): the span set is the
 * presentation carrier for prop-text, the way the annot store is for
 * documents.  Ordering and non-overlap are enforced at add time so
 * the renderer walks the list without checking.  Presented values are
 * released through one hook, exactly once per detachment (clear,
 * destroy) -- the annot store's contract.
 */

#include <string.h>

#include "lk-memory.h"
#include <lk.h>

typedef struct spans_entry {
  lk_text_span span;
  lk_u32 type_id; /* 0 = no presentation */
  lk_value value;
} spans_entry;

struct lk_spans {
  spans_entry *e;
  lk_u32 count, cap;

  void (*release)(void *ud, lk_value v);
  void *release_ud;

  void *(*alloc)(void *, lk_u32);
  void (*dealloc)(void *, void *);
  void *ud;
};

static const lk_resource_type g_spans_type = {"spans", NULL};

lk_spans *lk_spans_new(void *(*alloc)(void *, lk_u32),
                       void (*dealloc)(void *, void *), void *ud) {
  lk_spans *s;

  if (!alloc) {
    alloc = lk_sys_alloc;
  }

  if (!dealloc) {
    dealloc = lk_sys_dealloc;
  }

  s = (lk_spans *)alloc(ud, (lk_u32)sizeof(*s));

  if (!s) {
    return NULL;
  }

  memset(s, 0, sizeof(*s));
  s->alloc = alloc;
  s->dealloc = dealloc;
  s->ud = ud;

  return s;
}

static void spans_release_all(lk_spans *s) {
  lk_u32 i;

  for (i = 0; i < s->count; i++) {
    if (s->e[i].type_id != 0 && s->release) {
      s->release(s->release_ud, s->e[i].value);
    }
  }
}

void lk_spans_destroy(lk_spans *s) {
  if (!s) {
    return;
  }

  spans_release_all(s);

  if (s->e) {
    s->dealloc(s->ud, s->e);
  }

  s->dealloc(s->ud, s);
}

void lk_spans_clear(lk_spans *s) {
  if (!s) {
    return;
  }

  spans_release_all(s);
  s->count = 0;
}

lk_u32 lk_spans_count(const lk_spans *s) {
  return s ? s->count : 0;
}

void lk_spans_set_release(lk_spans *s, void (*fn)(void *ud, lk_value v),
                          void *ud) {
  if (s) {
    s->release = fn;
    s->release_ud = ud;
  }
}

/* Insert position for [start, end): the index i such that every entry
 * before i ends at or before start and every entry from i starts at or
 * after end.  Returns 0 (with *out unset) on overlap; 2 (with *out =
 * the entry) when an entry with the IDENTICAL range exists -- the
 * caller merges into it (a range may carry both a style and a
 * presentation). */
static int spans_slot(const lk_spans *s, lk_u32 start, lk_u32 end,
                      lk_u32 *out) {
  lk_u32 i = 0;

  while (i < s->count && s->e[i].span.start < start) {
    i++;
  }

  if (i < s->count && s->e[i].span.start == start &&
      s->e[i].span.end == end) {
    *out = i;

    return 2;
  }

  /* everything before i starts before `start`; the last of them must
   * end at or before start */
  if (i > 0 && s->e[i - 1].span.end > start) {
    return 0;
  }

  /* everything from i starts at or after `start`; the first of them
   * must start at or after end */
  if (i < s->count && s->e[i].span.start < end) {
    return 0;
  }

  *out = i;

  return 1;
}

static int spans_insert(lk_spans *s, lk_u32 at, const spans_entry *ent) {
  if (s->count == s->cap) {
    lk_u32 ncap = s->cap ? s->cap * 2 : 8;
    spans_entry *ne =
        (spans_entry *)s->alloc(s->ud, (lk_u32)(sizeof(spans_entry) * ncap));

    if (!ne) {
      return 0;
    }

    if (s->e) {
      memcpy(ne, s->e, sizeof(spans_entry) * s->count);
      s->dealloc(s->ud, s->e);
    }

    s->e = ne;
    s->cap = ncap;
  }

  if (at < s->count) {
    memmove(&s->e[at + 1], &s->e[at], sizeof(spans_entry) * (s->count - at));
  }

  s->e[at] = *ent;
  s->count++;

  return 1;
}

int lk_spans_add(lk_spans *s, lk_u32 start, lk_u32 end, lk_color fg,
                 lk_color bg, lk_u8 flags) {
  spans_entry ent;
  lk_u32 at;
  int slot;

  if (!s || start >= end) {
    return 0;
  }

  slot = spans_slot(s, start, end, &at);

  if (slot == 0) {
    return 0;
  }

  if (slot == 2) {
    /* merge: the given fields replace, the presentation stays */
    lk_text_span *sp = &s->e[at].span;

    if (flags & LK_SPAN_FG) {
      sp->fg = fg;
    }

    if (flags & LK_SPAN_BG) {
      sp->bg = bg;
    }

    sp->flags |= flags;

    return 1;
  }

  memset(&ent, 0, sizeof(ent));
  ent.span.start = start;
  ent.span.end = end;
  ent.span.fg = fg;
  ent.span.bg = bg;
  ent.span.flags = flags;
  ent.value = lk_v_none();

  return spans_insert(s, at, &ent);
}

int lk_spans_add_present(lk_spans *s, lk_u32 start, lk_u32 end,
                         lk_u32 type_id, lk_value value) {
  spans_entry ent;
  lk_u32 at;
  int slot;

  if (!s || start >= end || type_id == 0) {
    return 0;
  }

  slot = spans_slot(s, start, end, &at);

  if (slot == 0) {
    return 0;
  }

  if (slot == 2) {
    /* merge: replace the presentation (releasing the old value),
     * keep the appearance */
    spans_entry *e = &s->e[at];

    if (e->type_id != 0 && s->release) {
      s->release(s->release_ud, e->value);
    }

    e->type_id = type_id;
    e->value = value;

    return 1;
  }

  memset(&ent, 0, sizeof(ent));
  ent.span.start = start;
  ent.span.end = end;
  ent.type_id = type_id;
  ent.value = value;

  return spans_insert(s, at, &ent);
}

const lk_text_span *lk_spans_get(const lk_spans *s, lk_u32 i) {
  if (!s || i >= s->count) {
    return NULL;
  }

  return &s->e[i].span;
}

int lk_spans_present_get(const lk_spans *s, lk_u32 i, lk_u32 *type_id,
                         lk_value *value) {
  if (!s || i >= s->count || s->e[i].type_id == 0) {
    return 0;
  }

  if (type_id) {
    *type_id = s->e[i].type_id;
  }

  if (value) {
    *value = s->e[i].value;
  }

  return 1;
}

lk_u32 lk_spans_present_at(const lk_spans *s, lk_u32 pos,
                           lk_presentation_hit *out, lk_u32 cap) {
  lk_u32 n = 0;
  lk_u32 i;

  if (!s || !out || cap == 0) {
    return 0;
  }

  /* Spans are disjoint, so at most one covers pos -- but the contract
   * is a precedence-ordered list, kept general for a future nesting
   * relaxation: shortest first, insertion (= start) order on ties. */
  for (i = 0; i < s->count && n < cap; i++) {
    const spans_entry *e = &s->e[i];

    if (e->type_id != 0 && e->span.start <= pos && pos < e->span.end) {
      lk_presentation_hit *h = &out[n++];

      memset(h, 0, sizeof(*h));
      h->type_id = e->type_id;
      h->value = e->value;
      h->locus[0] = e->span.start;
      h->locus[1] = e->span.end;
    }
  }

  return n;
}

const lk_resource_type *lk_spans_type(void) {
  return &g_spans_type;
}

lk_spans *lk_spans_from_node(const lk_resources *rs, const lk_tree *t,
                             lk_ix n) {
  const lk_node *nd;
  lk_u32 k;

  if (!rs || !t || n == 0 || n >= t->node_count) {
    return NULL;
  }

  nd = &t->nodes[n];

  for (k = 0; k < nd->props_len; k++) {
    const lk_prop *p = &t->props[nd->props_off + k];

    if (p->key == UIP_SPANS && p->value.tag == UIV_RESOURCE) {
      return (lk_spans *)lk_resource_get(rs, lk_v_resource_ref(p->value),
                                         &g_spans_type);
    }
  }

  return NULL;
}
