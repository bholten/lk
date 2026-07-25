#include <memory.h>

#include "lk-memory.h"
#include <lk.h>

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

static int lk_tree_reserve_pres(lk_tree *t, lk_u32 need) {
  lk_presentation *np;
  lk_u32 new_cap;

  if (need <= t->pres_cap) {
    return 1;
  }

  new_cap = t->pres_cap ? t->pres_cap : 16;

  while (new_cap < need) {
    new_cap *= 2;
  }

  np = (lk_presentation *)lk_alloc(t,
                                   (lk_u32)(sizeof(lk_presentation) * new_cap));

  if (!np) {
    return 0;
  }

  if (t->pres && t->pres_count) {
    memcpy(np, t->pres, sizeof(lk_presentation) * t->pres_count);
  }

  if (t->pres) {
    lk_dealloc(t, t->pres);
  }

  t->pres = np;
  t->pres_cap = new_cap;

  return 1;
}

static int lk_tree_reserve_tags(lk_tree *t, lk_u32 need) {
  lk_tag *nt;
  lk_u32 new_cap;

  if (need <= t->tag_cap) {
    return 1;
  }

  new_cap = t->tag_cap ? t->tag_cap : 16;

  while (new_cap < need) {
    new_cap *= 2;
  }

  nt = (lk_tag *)lk_alloc(t, (lk_u32)(sizeof(lk_tag) * new_cap));

  if (!nt) {
    return 0;
  }

  if (t->tags && t->tag_count) {
    memcpy(nt, t->tags, sizeof(lk_tag) * t->tag_count);
  }

  if (t->tags) {
    lk_dealloc(t, t->tags);
  }

  t->tags = nt;
  t->tag_cap = new_cap;

  return 1;
}

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

  t->alloc = cfg->alloc ? cfg->alloc : lk_sys_alloc;
  t->dealloc = cfg->dealloc ? cfg->dealloc : lk_sys_dealloc;
  t->alloc_ud = cfg->ud;

  if (cfg->intern) {
    t->intern = cfg->intern;
    t->owns_intern = 0;
  } else {
    t->intern = lk_intern_new(t->alloc, t->dealloc, t->alloc_ud);
    t->owns_intern = 1;

    if (!t->intern) {
      lk_dealloc(t, t);
      return 0;
    }
  }

  t->node_count = 1;

  if (!lk_tree_reserve_nodes(t, cfg->node_cap_hint ? cfg->node_cap_hint : 64)) {
    lk_tree_destroy(t);
    return 0;
  }

  memset(&t->nodes[0], 0, sizeof(lk_node));

  if (!lk_tree_reserve_props(t,
                             cfg->prop_cap_hint ? cfg->prop_cap_hint : 128)) {
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

  if (t->intern && t->owns_intern) {
    lk_intern_destroy(t->intern);
  }

  if (t->nodes) {
    lk_dealloc(t, t->nodes);
  }

  if (t->props) {
    lk_dealloc(t, t->props);
  }

  if (t->pres) {
    lk_dealloc(t, t->pres);
  }

  if (t->tags) {
    lk_dealloc(t, t->tags);
  }

  lk_dealloc(t, t);
}

void lk_tree_reset(lk_tree *t) {
  if (!t) {
    return;
  }

  t->node_count = 1;
  t->prop_count = 0;
  t->pres_count = 0;
  t->tag_count = 0;
  t->root = 0;

  if (t->nodes) {
    memset(&t->nodes[0], 0, sizeof(lk_node));
  }
}

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

lk_ix lk_tree_add_node_s(lk_tree *t, lk_str id_str, lk_kind kind) {
  if (!t || !t->intern) {
    return 0;
  }

  return lk_tree_add_node(t, lk_intern_id(t->intern, id_str), kind);
}

void lk_tree_set_root(lk_tree *t, lk_ix root) {
  if (!t) {
    return;
  }

  t->root = root;
}

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

  /* Lazily set props_off when the first prop is added. Allows all
     nodes to be created up front, then props grouped per node. */
  if (n->props_len == 0) {
    n->props_off = (lk_ix)t->prop_count;
  }

  /* enforce contiguous append in global arena */
  if (n->props_off + n->props_len != t->prop_count) {
    /* Props for this node are not at the arena tail — another node's
       props were appended in between. Silently drop. */
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

/* ---- Presentation API ---- */

void lk_tree_add_presentation_v(lk_tree *t, lk_ix node, lk_u32 ptype,
                                const lk_value *pvalues, lk_u8 count) {
  lk_presentation *p;
  lk_u8 i;

  if (!t || node == 0 || node >= t->node_count) {
    return;
  }

  if (count > LK_PRES_MAX_ARGS) {
    count = LK_PRES_MAX_ARGS;
  }

  if (!lk_tree_reserve_pres(t, t->pres_count + 1)) {
    return;
  }

  p = &t->pres[t->pres_count++];
  memset(p, 0, sizeof(*p));
  p->node = node;
  p->ptype = ptype;
  p->pvalue_count = count;

  for (i = 0; i < count && pvalues; i++) {
    p->pvalues[i] = pvalues[i];
  }
}

void lk_tree_add_presentation_sv(lk_tree *t, lk_ix node, const char *ptype,
                                 const lk_value *pvalues, lk_u8 count) {
  if (!t || !t->intern || !ptype) {
    return;
  }

  lk_tree_add_presentation_v(t, node, lk_intern_id(t->intern, lk_str_c(ptype)),
                             pvalues, count);
}

void lk_tree_add_presentation(lk_tree *t, lk_ix node, lk_u32 ptype,
                              lk_value pvalue) {
  lk_tree_add_presentation_v(t, node, ptype, &pvalue, 1);
}

void lk_tree_add_presentation_s(lk_tree *t, lk_ix node, const char *ptype,
                                lk_value pvalue) {
  lk_tree_add_presentation_sv(t, node, ptype, &pvalue, 1);
}

const lk_presentation *lk_tree_get_presentation(const lk_tree *t, lk_ix node) {
  lk_u32 i;

  if (!t || node == 0) {
    return NULL;
  }

  for (i = 0; i < t->pres_count; i++) {
    if (t->pres[i].node == node) {
      return &t->pres[i];
    }
  }

  return NULL;
}

/* ---- Tag API ---- */

void lk_tree_add_tag(lk_tree *t, lk_ix node, lk_u32 tag_id) {
  lk_tag *tg;

  if (!t || node == 0 || node >= t->node_count || tag_id == 0) {
    return;
  }

  if (!lk_tree_reserve_tags(t, t->tag_count + 1)) {
    return;
  }

  tg = &t->tags[t->tag_count++];
  tg->node = node;
  tg->tag_id = tag_id;
}

void lk_tree_add_tag_s(lk_tree *t, lk_ix node, const char *tag) {
  if (!t || !t->intern || !tag) {
    return;
  }

  lk_tree_add_tag(t, node, lk_intern_cid(t->intern, tag));
}

int lk_tree_has_tag(const lk_tree *t, lk_ix node, lk_u32 tag_id) {
  lk_u32 i;

  if (!t || node == 0 || tag_id == 0) {
    return 0;
  }

  for (i = 0; i < t->tag_count; i++) {
    if (t->tags[i].node == node && t->tags[i].tag_id == tag_id) {
      return 1;
    }
  }

  return 0;
}

static void push_diag(lk_diag *diags, lk_u32 cap, lk_u32 *len, lk_diag_kind k,
                      lk_diag_code c, lk_ix node, lk_u16 key, lk_u16 kind_u16,
                      const char *msg) {
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

int lk_tree_validate(const lk_tree *t, const lk_validate_opts *opts,
                     lk_diag *diags, lk_u32 diags_cap, lk_u32 *out_diags_len) {
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
    push_diag(diags, diags_cap, &dl, UID_ERROR, UIDC_INVALID_NODE_INDEX, 0, 0,
              0, "tree is NULL");
    ok = 0;

    if (out_diags_len) {
      *out_diags_len = dl;
    }

    return ok;
  }

  if (opts->require_root) {
    if (t->root == 0 || t->root >= t->node_count) {
      push_diag(diags, diags_cap, &dl, UID_ERROR, UIDC_NO_ROOT, t->root, 0, 0,
                "missing or invalid root");
      ok = 0;
    }
  }

  if (opts->forbid_multiple_parents) {
    lk_u32 *parent_count = 0;
    lk_u32 i;
    parent_count =
        (lk_u32 *)lk_sys_alloc(NULL, (lk_u32)(sizeof(lk_u32) * t->node_count));

    if (parent_count) {
      for (i = 0; i < t->node_count; i++) {
        parent_count[i] = 0;
      }

      for (i = 1; i < t->node_count; i++) {
        lk_ix ch = t->nodes[i].first_child;
        while (ch) {
          if (ch >= t->node_count) {
            push_diag(diags, diags_cap, &dl, UID_ERROR, UIDC_INVALID_NODE_INDEX,
                      ch, 0, 0, "child index out of range");
            ok = 0;
            break;
          }

          parent_count[ch]++;
          ch = t->nodes[ch].next_sibling;
        }
      }

      for (i = 1; i < t->node_count; i++) {
        if (parent_count[i] > 1) {
          push_diag(diags, diags_cap, &dl, UID_ERROR, UIDC_MULTIPLE_PARENTS,
                    (lk_ix)i, 0, 0,
                    "node has multiple parents via child lists");
          ok = 0;
        }
      }

      lk_sys_dealloc(NULL, parent_count);
    }
  }

  {
    lk_u32 i;

    for (i = 1; i < t->node_count; i++) {
      lk_ix ch = t->nodes[i].first_child;

      while (ch) {
        if (ch >= t->node_count) {
          break;
        }

        if (t->nodes[ch].parent != (lk_ix)i) {
          push_diag(diags, diags_cap, &dl, UID_ERROR, UIDC_PARENT_MISMATCH, ch,
                    0, 0, "child.parent does not match container");
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
          push_diag(diags, diags_cap, &dl, UID_ERROR, UIDC_DUPLICATE_NODE_ID,
                    (lk_ix)j, 0, 0, "duplicate node id");
          ok = 0;
        }
      }
    }
  }

  if (opts->forbid_cycles && t->root != 0 && t->root < t->node_count) {
    lk_u8 *color =
        (lk_u8 *)lk_sys_alloc(NULL, (lk_u32)(sizeof(lk_u8) * t->node_count));

    if (color) {
      lk_u32 i;

      for (i = 0; i < t->node_count; i++) {
        color[i] = 0;
      }

      {
        lk_ix *stack = (lk_ix *)lk_sys_alloc(
            NULL, (lk_u32)(sizeof(lk_ix) * t->node_count));
        lk_ix *iter = (lk_ix *)lk_sys_alloc(
            NULL, (lk_u32)(sizeof(lk_ix) * t->node_count));
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

            iter[sp - 1] = t->nodes[c].next_sibling;

            if (c >= t->node_count) {
              continue;
            }

            if (color[c] == 1) {
              push_diag(diags, diags_cap, &dl, UID_ERROR, UIDC_CYCLE_DETECTED,
                        c, 0, 0, "cycle detected");
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

/* TODO */
static void wr_cstr(lk_write_fn wr, void *ud, const char *s) {
  if (!wr || !s) {
    return;
  }

  wr(ud, s, (lk_u32)strlen(s));
}

static void wr_u32(lk_write_fn wr, void *ud, lk_u32 x) {
  char buf[32];
  int n;

  /* C89 snprintf isn't guaranteed; use a tiny manual conversion */
  char tmp[32];
  lk_u32 i = 0;

  if (x == 0) {
    buf[0] = '0';
    buf[1] = 0;
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

static void dump_node(const lk_tree *t, lk_ix n, lk_write_fn wr, void *ud,
                      lk_u32 indent) {
  lk_u32 i;
  lk_node *node;

  for (i = 0; i < indent; i++) {
    wr_cstr(wr, ud, "  ");
  }

  if (n == 0 || n >= t->node_count) {
    wr_cstr(wr, ud, "(<invalid-node>)\n");
    return;
  }

  node = &t->nodes[n];

  wr_cstr(wr, ud, "(");

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

  if (node->props_len) {
    lk_u32 k;
    wr_cstr(wr, ud, " :props {");

    for (k = 0; k < node->props_len; k++) {
      lk_prop *p = &t->props[node->props_off + k];
      wr_cstr(wr, ud, " ");
      wr_u32(wr, ud, (lk_u32)p->key);
      wr_cstr(wr, ud, "=");

      switch (p->value.tag) {
      case UIV_BOOL: wr_cstr(wr, ud, p->value.as.b ? "true" : "false"); break;
      case UIV_I32: wr_u32(wr, ud, (lk_u32)p->value.as.i); break;
      case UIV_STR:
        wr_cstr(wr, ud, "\"");

        if (t->intern && p->value.as.str_id) {
          lk_str sv = lk_intern_str(t->intern, p->value.as.str_id);

          if (sv.ptr && sv.len) {
            wr(ud, sv.ptr, sv.len);
          }
        }

        wr_cstr(wr, ud, "\"");
        break;
      default: wr_cstr(wr, ud, "null"); break;
      }
    }
    wr_cstr(wr, ud, " }");
  }

  /* Dump presentations on this node */
  {
    lk_u32 pi;
    int has_pres = 0;

    for (pi = 0; pi < t->pres_count; pi++) {
      if (t->pres[pi].node == n) {
        if (!has_pres) {
          wr_cstr(wr, ud, " :pres {");
          has_pres = 1;
        }

        wr_cstr(wr, ud, " ");

        if (t->intern && t->pres[pi].ptype) {
          lk_str ps = lk_intern_str(t->intern, t->pres[pi].ptype);

          if (ps.ptr && ps.len) {
            wr(ud, ps.ptr, ps.len);
          }
        }

        wr_cstr(wr, ud, "=");

        if (t->pres[pi].pvalue_count > 1) {
          wr_cstr(wr, ud, "(");
        }

        {
          lk_u8 ai;
          for (ai = 0; ai < t->pres[pi].pvalue_count; ai++) {
            const lk_value *pv = &t->pres[pi].pvalues[ai];

            if (ai > 0) {
              wr_cstr(wr, ud, " ");
            }

            switch (pv->tag) {
            case UIV_BOOL: wr_cstr(wr, ud, pv->as.b ? "true" : "false"); break;
            case UIV_I32: wr_u32(wr, ud, (lk_u32)pv->as.i); break;
            case UIV_STR:
              wr_cstr(wr, ud, "\"");

              if (t->intern && pv->as.str_id) {
                lk_str sv = lk_intern_str(t->intern, pv->as.str_id);

                if (sv.ptr && sv.len) {
                  wr(ud, sv.ptr, sv.len);
                }
              }

              wr_cstr(wr, ud, "\"");
              break;
            default: wr_cstr(wr, ud, "null"); break;
            }
          }
        }

        if (t->pres[pi].pvalue_count > 1) {
          wr_cstr(wr, ud, ")");
        }
      }
    }

    if (has_pres) {
      wr_cstr(wr, ud, " }");
    }
  }

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

lk_ix lk_tree_find_by_id(const lk_tree *t, lk_node_id id) {
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

/* ---- Public prop helpers ---- */

lk_i32 lk_node_prop_i32(const lk_tree *t, lk_ix n, lk_prop_key key,
                        lk_i32 def) {
  const lk_node *nd = &t->nodes[n];
  lk_u32 i;

  for (i = 0; i < nd->props_len; i++) {
    const lk_prop *p = &t->props[nd->props_off + i];

    if ((lk_prop_key)p->key == key && p->value.tag == UIV_I32) {
      return (lk_i32)p->value.as.i;
    }
  }

  return def;
}

int lk_node_has_prop(const lk_tree *t, lk_ix n, lk_prop_key key) {
  const lk_node *nd = &t->nodes[n];
  lk_u32 i;

  for (i = 0; i < nd->props_len; i++) {
    if ((lk_prop_key)t->props[nd->props_off + i].key == key) {
      return 1;
    }
  }

  return 0;
}

int lk_node_prop_bool(const lk_tree *t, lk_ix n, lk_prop_key key) {
  const lk_node *nd = &t->nodes[n];
  lk_u32 i;

  for (i = 0; i < nd->props_len; i++) {
    const lk_prop *p = &t->props[nd->props_off + i];

    if ((lk_prop_key)p->key == key && p->value.tag == UIV_BOOL) {
      return p->value.as.b ? 1 : 0;
    }
  }

  return 0;
}

lk_str lk_node_text(const lk_tree *t, lk_ix n) {
  const lk_node *nd = &t->nodes[n];
  lk_str empty;
  lk_u32 i;

  empty.ptr = "";
  empty.len = 0;

  for (i = 0; i < nd->props_len; i++) {
    const lk_prop *p = &t->props[nd->props_off + i];

    if ((lk_prop_key)p->key == UIP_TEXT && p->value.tag == UIV_STR) {
      return lk_intern_str(t->intern, p->value.as.str_id);
    }
  }

  return empty;
}

lk_u32 lk_node_text_id(const lk_tree *t, lk_ix n) {
  const lk_node *nd = &t->nodes[n];
  lk_u32 i;

  for (i = 0; i < nd->props_len; i++) {
    const lk_prop *p = &t->props[nd->props_off + i];

    if ((lk_prop_key)p->key == UIP_TEXT && p->value.tag == UIV_STR) {
      return p->value.as.str_id;
    }
  }

  return 0;
}

/* Schema (lots of TODOs) */
int lk_tree_validate_schema(const lk_tree *t, const lk_kind_schema *schema,
                            lk_u32 schema_count, lk_diag *diags,
                            lk_u32 diags_cap, lk_u32 *out_diags_len) {
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
