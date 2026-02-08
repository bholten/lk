#include <memory.h>

#include "lk-data.h"
#include "lk-memory.h"

static int lk_tree_reserve_nodes(lk_tree *t, lk_u32 need) {
  lk_node *nn;
  lk_u32 new_cap;

  if (need <= t->node_cap) {
    return 1;
  }

  new_cap = t->node_cap ? t->node_cap : 64;

  while (new_cap < need) {
    new_cap *= 2;
  }

  nn = (lk_node *)lk_alloc(t, (lk_u32)(sizeof(lk_node) * new_cap));

  if (!nn) {
    return 0;
  }

  if (t->nodes && t->node_count) {
    memcpy(nn, t->nodes, sizeof(lk_node) * t->node_count);
  }

  if (t->nodes) {
    lk_dealloc(t, t->nodes);
  }

  t->nodes = nn;
  t->node_cap = new_cap;

  return 1;
}

static int lk_tree_reserve_props(lk_tree *t, lk_u32 need) {
  lk_prop *np;
  lk_u32 new_cap;

  if (need <= t->prop_cap) {
    return 1;
  }

  new_cap = t->prop_cap ? t->prop_cap : 128;

  while (new_cap < need) {
    new_cap *= 2;
  }

  np = (lk_prop *)lk_alloc(t, (lk_u32)(sizeof(lk_prop) * new_cap));

  if (!np) {
    return 0;
  }

  if (t->props && t->prop_count) {
    memcpy(np, t->props, sizeof(lk_prop) * t->prop_count);
  }

  if (t->props) {
    lk_dealloc(t, t->props);
  }

  t->props = np;
  t->prop_cap = new_cap;

  return 1;
}

/* Tree lifecycle */
lk_tree *lk_tree_create(const lk_tree_cfg *cfg) {
  lk_tree *t;
  lk_tree_cfg d;

  if (!cfg) {
    memset(&d, 0, sizeof(d));
    d.alloc = lk_sys_alloc;
    d.dealloc = lk_sys_dealloc;
    cfg = &d;
  }

  if (cfg->alloc) {
    t = cfg->alloc(cfg->ud, (lk_u32)sizeof(lk_tree));
  } else {
    t = lk_sys_alloc(NULL, (lk_u32)sizeof(lk_tree));
  }

  if (!t) {
    return 0;
  }

  memset(t, 0, sizeof(*t));

  t->intern = cfg->intern;
  t->alloc = cfg->alloc ? cfg->alloc : lk_sys_alloc;
  t->dealloc = cfg->dealloc ? cfg->dealloc : lk_sys_dealloc;
  t->alloc_ud = cfg->ud;

  t->node_count = 1;

  if (!lk_tree_reserve_nodes(t, cfg->node_cap_hint ? cfg->node_cap_hint : 64)) {
    lk_tree_destroy(t);
    return 0;
  }

  memset(&t->nodes[0], 0, sizeof(lk_node));

  if (!lk_tree_reserve_props(t, cfg->prop_cap_hint ? cfg->prop_cap_hint : 128)) {
    lk_tree_destroy(t);
    return 0;
  }

  t->root = 0;

  return t;
}


void lk_tree_destroy(lk_tree *t) {
  if (!t) {
    return;
  }

  if (t->nodes) {
    lk_dealloc(t, t->nodes);
  }

  if (t->props) {
    lk_dealloc(t, t->props);
  }

  lk_dealloc(t, t);
}


/* keep capacity, clear counts */
void lk_tree_reset(lk_tree *t) {
  if (!t) {
    return;
  }

  t->node_count = 1;
  t->prop_count = 0;
  t->root = 0;

  if (t->nodes) {
    memset(&t->nodes[0], 0, sizeof(lk_node));
  }
}


/* Create a node with id+kind, append to arena, return node index (1..N).
 * Caller wires parent/children via lk_tree_append_child.
 */
lk_ix lk_tree_add_node(lk_tree *t, lk_node_id id, lk_kind kind) {
  lk_ix ix;

  if (!t) {
    return 0;
  }

  if (!lk_tree_reserve_nodes(t, t->node_count + 1)) {
    return 0;
  }

  ix = (lk_ix)t->node_count++;
  memset(&t->nodes[ix], 0, sizeof(lk_node));

  t->nodes[ix].id = id;
  t->nodes[ix].kind = (lk_u16)kind;
  t->nodes[ix].props_off = (lk_ix)t->prop_count;
  t->nodes[ix].props_len = 0;

  return ix;
}

/* Convenience: create node by string id via intern (must not be NULL). */
lk_ix lk_tree_add_node_s(lk_tree *t, lk_str id_str, lk_kind kind) {
  if (!t || !t->intern) {
    return 0;
  }

  return lk_tree_add_node(t, lk_intern_id(t->intern, id_str), kind);
}

/* Set root node index. */
void lk_tree_set_root(lk_tree *t, lk_ix root) {
  if (!t) {
    return;
  }

  t->root = root;
}

/* Append child to parent's child list (preserves insertion order). */
void lk_tree_append_child(lk_tree *t, lk_ix parent, lk_ix child) {
  lk_ix *link;

  if (!t) {
    return;
  }

  if (parent == 0 || child == 0) {
    return;
  }

  t->nodes[child].parent = parent;

  link = &t->nodes[parent].first_child;

  while (*link != 0) {
    link = &t->nodes[*link].next_sibling;
  }

  *link = child;
}


/* Set/append a prop on a node.
 * Phase 0: allows duplicates; validation can flag duplicates later if desired.
 */
void lk_tree_add_prop(lk_tree *t, lk_ix node, lk_prop_key key, lk_value v) {
  lk_node *n;

  if (!t || node == 0 || node >= t->node_count) {
    return;
  }


  /* Ensure contiguous prop slice: we enforce "append-only per node"
     in construction order. In Phase 0, easiest is: require user to add
     props right after creating node, before adding any props to other nodes.
     But that's annoying.

     Instead: we store props in global arena and node has (off,len) slice.
     For arbitrary append order, we'd need per-node dynamic list or "prop
     blocks". For Phase 0, simplest compromise is to require props added
     right after node creation (or accept that this function assumes it).
  */

  n = &t->nodes[node];

  /* enforce contiguous append in global arena */
  if (n->props_off + n->props_len != t->prop_count) {
    /* In Phase 0 sketch: just ignore or you could assert/record diag.
       I recommend: document the rule + keep it strict.
    */
    return;
  }

  if (!lk_tree_reserve_props(t, t->prop_count + 1)) {
    return;
  }

  t->props[t->prop_count].key = (lk_u16)key;
  t->props[t->prop_count].value = v;
  t->prop_count++;
  n->props_len++;
}


static void push_diag(lk_diag* diags, lk_u32 cap, lk_u32* len,
                      lk_diag_kind k, lk_diag_code c, lk_ix node,
                      lk_u16 key, lk_u16 kind_u16, const char* msg) {
  lk_u32 i;

  if (!diags || !len) {
    return;
  }

  i = *len;

  if (i >= cap) {
      return;
  }

  diags[i].kind = k;
  diags[i].code = c;
  diags[i].node = node;
  diags[i].key = key;
  diags[i].kind_u16 = kind_u16;
  diags[i].msg = msg;
  *len = i + 1;
}

int lk_tree_validate(const lk_tree* t,
                     const lk_validate_opts* opts,
                     lk_diag* diags,
                     lk_u32 diags_cap,
                     lk_u32* out_diags_len) {
  lk_validate_opts o;
  lk_u32 dl = 0;
  int ok = 1;

  if (!opts) {
    memset(&o, 0, sizeof(o));
    o.require_root = 1;
    o.forbid_duplicate_ids = 1;
    o.forbid_cycles = 1;
    o.forbid_multiple_parents = 1;
    o.check_prop_schema = 0;
    opts = &o;
  }

  if (!t) {
    push_diag(diags, diags_cap, &dl, UID_ERROR, UIDC_INVALID_NODE_INDEX, 0, 0, 0, "tree is NULL");
    ok = 0;

    if (out_diags_len) {
      *out_diags_len = dl;
    }

    return ok;
  }

  if (opts->require_root) {
    if (t->root == 0 || t->root >= t->node_count) {
      push_diag(diags, diags_cap, &dl, UID_ERROR, UIDC_NO_ROOT, t->root, 0, 0, "missing or invalid root");
      ok = 0;
    }
  }

  /* Basic index sanity for child links + parent pointers, and detect
     multiple parents */
  if (opts->forbid_multiple_parents) {
    lk_u32* parent_count = 0;
    lk_u32 i;
    parent_count = (lk_u32*)lk_sys_alloc(NULL, (lk_u32)(sizeof(lk_u32) * t->node_count));

    if (parent_count) {
      for (i = 0; i < t->node_count; i++) {
        parent_count[i] = 0;
      }

      for (i = 1; i < t->node_count; i++) {
        lk_ix ch = t->nodes[i].first_child;
        while (ch) {
          if (ch >= t->node_count) {
            push_diag(diags, diags_cap, &dl, UID_ERROR, UIDC_INVALID_NODE_INDEX, ch, 0, 0, "child index out of range");
            ok = 0;
            break;
          }

          parent_count[ch]++;
          ch = t->nodes[ch].next_sibling;
        }
      }

      for (i = 1; i < t->node_count; i++) {
        if (parent_count[i] > 1) {
          push_diag(diags, diags_cap, &dl, UID_ERROR, UIDC_MULTIPLE_PARENTS, (lk_ix)i, 0, 0, "node has multiple parents via child lists");
          ok = 0;
        }
      }

      lk_sys_dealloc(NULL, parent_count);
    }
  }

    /* Parent mismatch: child's parent pointer should match actual
       container */
  {
    lk_u32 i;

    for (i = 1; i < t->node_count; i++) {
      lk_ix ch = t->nodes[i].first_child;

      while (ch) {
        if (ch >= t->node_count) {
          break;
        }

        if (t->nodes[ch].parent != (lk_ix)i) {
          push_diag(diags, diags_cap, &dl, UID_ERROR, UIDC_PARENT_MISMATCH, ch, 0, 0, "child.parent does not match container");
          ok = 0;
        }

        ch = t->nodes[ch].next_sibling;
      }
    }
  }

  /* Duplicate ID check (O(n^2) in v0; fine for tests/MVP; optimize
     later) */
  if (opts->forbid_duplicate_ids) {
    lk_u32 i, j;

    for (i = 1; i < t->node_count; i++) {
      if (t->nodes[i].id == 0) {
        continue;
      }

      for (j = i + 1; j < t->node_count; j++) {
        if (t->nodes[i].id == t->nodes[j].id && t->nodes[j].id != 0) {
          push_diag(diags, diags_cap, &dl, UID_ERROR, UIDC_DUPLICATE_NODE_ID, (lk_ix)j, 0, 0, "duplicate node id");
          ok = 0;
        }
      }
    }
  }

  /* Cycle detection (DFS colors) */
  if (opts->forbid_cycles && t->root != 0 && t->root < t->node_count) {
    lk_u8* color = (lk_u8*)lk_sys_alloc(NULL, (lk_u32)(sizeof(lk_u8) * t->node_count));

    if (color) {
      lk_u32 i;

      for (i = 0; i < t->node_count; i++) {
        color[i] = 0;
      }

      /* recursive DFS not C89-friendly for deep trees; implement
         manual stack */
      {
        lk_ix* stack = (lk_ix*)lk_sys_alloc(NULL, (lk_u32)(sizeof(lk_ix) * t->node_count));
        lk_ix* iter  = (lk_ix*)lk_sys_alloc(NULL, (lk_u32)(sizeof(lk_ix) * t->node_count));
        lk_u32 sp = 0;

        if (stack && iter) {
          stack[sp] = t->root;
          iter[sp] = t->nodes[t->root].first_child;
          color[t->root] = 1; /* grey */
          sp++;

          while (sp > 0) {
            lk_ix n = stack[sp - 1];
            lk_ix c = iter[sp - 1];

            if (c == 0) {
              color[n] = 2; /* black */
              sp--;
              continue;
            }

            /* advance iterator */
            iter[sp - 1] = t->nodes[c].next_sibling;

            if (c >= t->node_count) {
              continue;
            }

            if (color[c] == 1) {
              push_diag(diags, diags_cap, &dl, UID_ERROR, UIDC_CYCLE_DETECTED, c, 0, 0, "cycle detected");
              ok = 0;
              break;
            }

            if (color[c] == 0) {
              color[c] = 1;
              stack[sp] = c;
              iter[sp] = t->nodes[c].first_child;
              sp++;
            }
          }
        }

        if (stack) {
          lk_sys_dealloc(NULL, stack);
        }

        if (iter) {
          lk_sys_dealloc(NULL, iter);
        }
      }

      lk_sys_dealloc(NULL, color);
    }
  }

  if (out_diags_len) {
    *out_diags_len = dl;
  }

  if (!ok) {
    return 0;
  }

  return 1;
}

/* Debug/Dump */
/* TODO */
static void wr_cstr(lk_write_fn wr, void* ud, const char* s) {
  if (!wr || !s) {
    return;
  }

  wr(ud, s, (lk_u32)strlen(s));
}

static void wr_u32(lk_write_fn wr, void* ud, lk_u32 x) {
  char buf[32];
  int n;

  /* C89 snprintf isn't guaranteed; use a tiny manual conversion */
  char tmp[32];
  lk_u32 i = 0;

  if (x == 0) {
    buf[0] = '0'; buf[1] = 0;
    wr_cstr(wr, ud, buf);
    return;
  }

  while (x > 0 && i < sizeof(tmp)) {
    tmp[i++] = (char)('0' + (x % 10));
    x /= 10;
  }

  n = 0;

  while (i > 0) {
    buf[n++] = tmp[--i];
  }

  buf[n] = 0;
  wr_cstr(wr, ud, buf);
}

static void dump_node(const lk_tree *t, lk_ix n, lk_write_fn wr,
                      void* ud, lk_u32 indent) {
  lk_u32 i;
  lk_node* node;

  for (i = 0; i < indent; i++) {
    wr_cstr(wr, ud, "  ");
  }

  if (n == 0 || n >= t->node_count) {
    wr_cstr(wr, ud, "(<invalid-node>)\n");
    return;
  }

  node = &t->nodes[n];

  wr_cstr(wr, ud, "(");
  /* kind */

  switch ((lk_kind)node->kind) {
  case UIK_WINDOW: wr_cstr(wr, ud, "window"); break;
  case UIK_ROW: wr_cstr(wr, ud, "row"); break;
  case UIK_COLUMN: wr_cstr(wr, ud, "column"); break;
  case UIK_SPACER: wr_cstr(wr, ud, "spacer"); break;
  case UIK_LABEL: wr_cstr(wr, ud, "label"); break;
  case UIK_BUTTON: wr_cstr(wr, ud, "button"); break;

  default: wr_cstr(wr, ud, "unknown"); break;
  }

  wr_cstr(wr, ud, " :id ");

  if (t->intern) {
    lk_str s = lk_intern_str(t->intern, node->id);
    wr_cstr(wr, ud, "\"");

    if (s.ptr && s.len) {
      wr(ud, s.ptr, s.len);
    }

    wr_cstr(wr, ud, "\"");
  } else {
    wr_u32(wr, ud, (lk_u32)node->id);
  }

  /* props */
  if (node->props_len) {
    lk_u32 k;
    wr_cstr(wr, ud, " :props {");

    for (k = 0; k < node->props_len; k++) {
      lk_prop* p = &t->props[node->props_off + k];
      wr_cstr(wr, ud, " ");
      wr_u32(wr, ud, (lk_u32)p->key);
      wr_cstr(wr, ud, "=");

      switch (p->value.tag) {
      case UIV_BOOL: wr_cstr(wr, ud, p->value.as.b ? "true" : "false"); break;
      case UIV_I32:  wr_u32(wr, ud, (lk_u32)p->value.as.i); break;
      case UIV_STR:
        wr_cstr(wr, ud, "\"");

        if (p->value.as.s.ptr && p->value.as.s.len) {
          wr(ud, p->value.as.s.ptr, p->value.as.s.len);
        }

        wr_cstr(wr, ud, "\"");
        break;
      default: wr_cstr(wr, ud, "null"); break;
      }
    }
    wr_cstr(wr, ud, " }");
  }

  /* children */
  if (node->first_child) {
    wr_cstr(wr, ud, "\n");

    {
      lk_ix ch = node->first_child;

      while (ch) {
        dump_node(t, ch, wr, ud, indent + 1);
        ch = t->nodes[ch].next_sibling;
      }
    }

    for (i = 0; i < indent; i++) {
      wr_cstr(wr, ud, "  ");
    }

    wr_cstr(wr, ud, ")\n");
  } else {
    wr_cstr(wr, ud, ")\n");
  }
}

void lk_tree_dump(const lk_tree *t, lk_write_fn wr, void *wr_ud) {
  if (!t || !wr) {
    return;
  }

  if (t->root == 0) {
    wr_cstr(wr, wr_ud, "(<no-root>)\n");
    return;
  }

  dump_node(t, t->root, wr, wr_ud, 0);
}

lk_ix lk_tree_find_by_id(const lk_tree* t, lk_node_id id) {
  lk_u32 i;

  if (!t || id == 0) {
    return 0;
  }

  for (i = 1; i < t->node_count; i++) {
    if (t->nodes[i].id == id) {
      return (lk_ix)i;
    }
  }

  return 0;
}

/* Schema (lots of TODOs) */

lk_kind_schema *ui_default_schema(lk_u32* out_count) {
    (void)out_count;
    return 0; /* implement later if you want schema checks in Phase 0 */
}

int lk_tree_validate_schema(
    const lk_tree* t,
    const lk_kind_schema* schema,
    lk_u32 schema_count,
    lk_diag* diags,
    lk_u32 diags_cap,
    lk_u32* out_diags_len
) {
    (void)t;
    (void)schema;
    (void)schema_count;
    (void)diags;
    (void)diags_cap;

  if (out_diags_len) {
    *out_diags_len = 0;
  }    

  return 1;
}
