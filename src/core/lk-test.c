#include <memory.h>
#include <stdlib.h>
#include <stdio.h>

#include "lk-data.h"
#include "lk-memory.h"

static void write_stdout(void *ud, const char *b, lk_u32 n) {
  (void)ud;
  fwrite(b, 1, (size_t)n, stdout);
}

int main(void) {
  lk_tree_cfg cfg;
  lk_tree *t;
  lk_intern *it;

  memset(&cfg, 0, sizeof(cfg));
  cfg.alloc = lk_sys_alloc;
  cfg.dealloc = lk_sys_dealloc;

  it = lk_intern_new(lk_sys_alloc, NULL);

  cfg.intern = it;
  
  t = lk_tree_create(&cfg);

  {
    lk_ix root = lk_tree_add_node_s(t, lk_str_c("main"), UIK_WINDOW);
    lk_ix col = lk_tree_add_node_s(t, lk_str_c("root"), UIK_COLUMN);
    lk_ix btn = lk_tree_add_node_s(t, lk_str_c("inc"), UIK_BUTTON);
    
    lk_tree_set_root(t, root);
    lk_tree_append_child(t, root, col);
    lk_tree_append_child(t, col, btn);

    lk_tree_add_prop(t, btn, UIP_TEXT, lk_v_cstr(t->intern, "Increment"));
    lk_tree_add_prop(t, btn, UIP_FOCUSABLE, lk_v_bool(1));
  }
  
  {
    lk_validate_opts o;
    lk_diag diags[32];
    lk_u32 n = 0;
    int ok;
        
    memset(&o, 0, sizeof(o));
    o.require_root = 1;
    o.forbid_cycles = 1;
    o.forbid_duplicate_ids = 1;
    o.forbid_multiple_parents = 1;
    
    ok = lk_tree_validate(t, &o, diags, 32, &n);

    if (!ok) {
      /* print diags */
    }
  }

  lk_tree_dump(t, write_stdout, NULL);
  
  lk_tree_destroy(t);
  lk_intern_destroy(it);
  
  return 0;
}
