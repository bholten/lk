#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <lk.h>
#include "core/lk-memory.h"

/* ---- minimal test harness ---- */

static int g_tests = 0;
static int g_pass = 0;
static int g_fail = 0;
static int g_cur_ok = 0;

#define BEGIN_TEST(name)                                                       \
  do {                                                                         \
    g_tests++;                                                                 \
    g_cur_ok = 1;                                                              \
    printf("  %-44s ", name);                                                  \
  } while (0)

#define CHECK(cond)                                                            \
  do {                                                                         \
    if (!(cond)) {                                                             \
      if (g_cur_ok)                                                            \
        printf("FAIL\n");                                                      \
      printf("    line %d: %s\n", __LINE__, #cond);                            \
      g_cur_ok = 0;                                                            \
    }                                                                          \
  } while (0)

#define CHECK_EQ(a, b)                                                         \
  do {                                                                         \
    if ((a) != (b)) {                                                          \
      if (g_cur_ok)                                                            \
        printf("FAIL\n");                                                      \
      printf("    line %d: %s == %u, expected %u\n", __LINE__, #a,             \
             (unsigned)(a), (unsigned)(b));                                    \
      g_cur_ok = 0;                                                            \
    }                                                                          \
  } while (0)

#define END_TEST()                                                             \
  do {                                                                         \
    if (g_cur_ok) {                                                            \
      printf("ok\n");                                                          \
      g_pass++;                                                                \
    } else {                                                                   \
      g_fail++;                                                                \
    }                                                                          \
  } while (0)

/* ---- changeset query helpers ---- */

static int cs_has(const lk_changeset *cs, lk_ui *ui, lk_u8 kind,
                  const char *name) {
  lk_node_id id = lk_intern_id(ui->intern, lk_str_c(name));
  lk_u32 i;

  for (i = 0; i < cs->count; i++) {
    if (cs->changes[i].kind == kind && cs->changes[i].id == id) {
      return 1;
    }
  }

  return 0;
}

static lk_u32 cs_count_kind(const lk_changeset *cs, lk_u8 kind) {
  lk_u32 i, n = 0;

  for (i = 0; i < cs->count; i++) {
    if (cs->changes[i].kind == kind) {
      n++;
    }
  }

  return n;
}

/* ---- tree building helpers ---- */

/* window "main" > column "root" > button "inc" (text="Increment") */
static void build_base_tree(lk_tree *t) {
  lk_ix w = lk_tree_add_node_s(t, lk_str_c("main"), UIK_WINDOW);
  lk_ix col = lk_tree_add_node_s(t, lk_str_c("root"), UIK_COLUMN);
  lk_ix btn = lk_tree_add_node_s(t, lk_str_c("inc"), UIK_BUTTON);

  lk_tree_set_root(t, w);
  lk_tree_append_child(t, w, col);
  lk_tree_append_child(t, col, btn);
  lk_tree_add_prop(t, btn, UIP_TEXT, lk_v_cstr(t->intern, "Increment"));
}

/* prime the UI with the base tree so tests start from a known state */
static void prime_base(lk_ui *ui) {
  lk_tree *t = lk_ui_begin_frame(ui);
  build_base_tree(t);
  lk_ui_end_frame(ui);
}

/* ================================================================
 * Tests: basic lifecycle
 * ================================================================ */

static void test_first_frame_all_added(void) {
  lk_ui *ui = lk_ui_create(NULL);
  lk_tree *t;
  const lk_changeset *cs;

  BEGIN_TEST("first frame: all nodes ADDED");

  t = lk_ui_begin_frame(ui);
  build_base_tree(t);
  cs = lk_ui_end_frame(ui);

  CHECK_EQ(cs->count, 3);
  CHECK_EQ(cs_count_kind(cs, LK_CHANGE_ADDED), 3);
  CHECK(cs_has(cs, ui, LK_CHANGE_ADDED, "main"));
  CHECK(cs_has(cs, ui, LK_CHANGE_ADDED, "root"));
  CHECK(cs_has(cs, ui, LK_CHANGE_ADDED, "inc"));

  END_TEST();
  lk_ui_destroy(ui);
}

static void test_identical_no_changes(void) {
  lk_ui *ui = lk_ui_create(NULL);
  lk_tree *t;
  const lk_changeset *cs;

  BEGIN_TEST("identical rebuild: no changes");

  prime_base(ui);

  t = lk_ui_begin_frame(ui);
  build_base_tree(t);
  cs = lk_ui_end_frame(ui);

  CHECK_EQ(cs->count, 0);

  END_TEST();
  lk_ui_destroy(ui);
}

static void test_three_identical_frames(void) {
  lk_ui *ui = lk_ui_create(NULL);
  lk_tree *t;
  const lk_changeset *cs;
  int i;

  BEGIN_TEST("three identical rebuilds: no changes");

  prime_base(ui);

  for (i = 0; i < 3; i++) {
    t = lk_ui_begin_frame(ui);
    build_base_tree(t);
    cs = lk_ui_end_frame(ui);
    CHECK_EQ(cs->count, 0);
  }

  END_TEST();
  lk_ui_destroy(ui);
}

/* ================================================================
 * Tests: prop changes
 * ================================================================ */

static void test_prop_value_change(void) {
  lk_ui *ui = lk_ui_create(NULL);
  lk_tree *t;
  const lk_changeset *cs;
  lk_ix w, col, btn;

  BEGIN_TEST("change prop value: one UPDATED");

  prime_base(ui);

  t = lk_ui_begin_frame(ui);
  w = lk_tree_add_node_s(t, lk_str_c("main"), UIK_WINDOW);
  col = lk_tree_add_node_s(t, lk_str_c("root"), UIK_COLUMN);
  btn = lk_tree_add_node_s(t, lk_str_c("inc"), UIK_BUTTON);
  lk_tree_set_root(t, w);
  lk_tree_append_child(t, w, col);
  lk_tree_append_child(t, col, btn);
  lk_tree_add_prop(t, btn, UIP_TEXT, lk_v_cstr(t->intern, "Decrement"));
  cs = lk_ui_end_frame(ui);

  CHECK_EQ(cs->count, 1);
  CHECK(cs_has(cs, ui, LK_CHANGE_UPDATED, "inc"));
  CHECK(!cs_has(cs, ui, LK_CHANGE_UPDATED, "main"));
  CHECK(!cs_has(cs, ui, LK_CHANGE_UPDATED, "root"));

  END_TEST();
  lk_ui_destroy(ui);
}

static void test_prop_added(void) {
  lk_ui *ui = lk_ui_create(NULL);
  lk_tree *t;
  const lk_changeset *cs;
  lk_ix w, col, btn;

  BEGIN_TEST("add prop to node: UPDATED");

  prime_base(ui);

  /* rebuild with an extra prop on button */
  t = lk_ui_begin_frame(ui);
  w = lk_tree_add_node_s(t, lk_str_c("main"), UIK_WINDOW);
  col = lk_tree_add_node_s(t, lk_str_c("root"), UIK_COLUMN);
  btn = lk_tree_add_node_s(t, lk_str_c("inc"), UIK_BUTTON);
  lk_tree_set_root(t, w);
  lk_tree_append_child(t, w, col);
  lk_tree_append_child(t, col, btn);
  lk_tree_add_prop(t, btn, UIP_TEXT, lk_v_cstr(t->intern, "Increment"));
  lk_tree_add_prop(t, btn, UIP_FOCUSABLE, lk_v_bool(1));
  cs = lk_ui_end_frame(ui);

  CHECK_EQ(cs->count, 1);
  CHECK(cs_has(cs, ui, LK_CHANGE_UPDATED, "inc"));

  END_TEST();
  lk_ui_destroy(ui);
}

static void test_prop_removed(void) {
  lk_ui *ui = lk_ui_create(NULL);
  lk_tree *t;
  const lk_changeset *cs;
  lk_ix w, col, btn;

  BEGIN_TEST("remove prop from node: UPDATED");

  /* prime with button having text + focusable */
  t = lk_ui_begin_frame(ui);
  w = lk_tree_add_node_s(t, lk_str_c("main"), UIK_WINDOW);
  col = lk_tree_add_node_s(t, lk_str_c("root"), UIK_COLUMN);
  btn = lk_tree_add_node_s(t, lk_str_c("inc"), UIK_BUTTON);
  lk_tree_set_root(t, w);
  lk_tree_append_child(t, w, col);
  lk_tree_append_child(t, col, btn);
  lk_tree_add_prop(t, btn, UIP_TEXT, lk_v_cstr(t->intern, "Increment"));
  lk_tree_add_prop(t, btn, UIP_FOCUSABLE, lk_v_bool(1));
  lk_ui_end_frame(ui);

  /* rebuild with only text (focusable removed) */
  t = lk_ui_begin_frame(ui);
  w = lk_tree_add_node_s(t, lk_str_c("main"), UIK_WINDOW);
  col = lk_tree_add_node_s(t, lk_str_c("root"), UIK_COLUMN);
  btn = lk_tree_add_node_s(t, lk_str_c("inc"), UIK_BUTTON);
  lk_tree_set_root(t, w);
  lk_tree_append_child(t, w, col);
  lk_tree_append_child(t, col, btn);
  lk_tree_add_prop(t, btn, UIP_TEXT, lk_v_cstr(t->intern, "Increment"));
  cs = lk_ui_end_frame(ui);

  CHECK_EQ(cs->count, 1);
  CHECK(cs_has(cs, ui, LK_CHANGE_UPDATED, "inc"));

  END_TEST();
  lk_ui_destroy(ui);
}

static void test_prop_type_change(void) {
  lk_ui *ui = lk_ui_create(NULL);
  lk_tree *t;
  const lk_changeset *cs;
  lk_ix w, col, btn;

  BEGIN_TEST("change prop type (str->i32): UPDATED");

  prime_base(ui);

  t = lk_ui_begin_frame(ui);
  w = lk_tree_add_node_s(t, lk_str_c("main"), UIK_WINDOW);
  col = lk_tree_add_node_s(t, lk_str_c("root"), UIK_COLUMN);
  btn = lk_tree_add_node_s(t, lk_str_c("inc"), UIK_BUTTON);
  lk_tree_set_root(t, w);
  lk_tree_append_child(t, w, col);
  lk_tree_append_child(t, col, btn);
  /* same key UIP_TEXT but wrong type — still detects the change */
  lk_tree_add_prop(t, btn, UIP_TEXT, lk_v_i32(42));
  cs = lk_ui_end_frame(ui);

  CHECK_EQ(cs->count, 1);
  CHECK(cs_has(cs, ui, LK_CHANGE_UPDATED, "inc"));

  END_TEST();
  lk_ui_destroy(ui);
}

/* ================================================================
 * Tests: kind change
 * ================================================================ */

static void test_kind_change(void) {
  lk_ui *ui = lk_ui_create(NULL);
  lk_tree *t;
  const lk_changeset *cs;
  lk_ix w, col, lbl;

  BEGIN_TEST("same ID different kind: UPDATED");

  prime_base(ui);

  /* rebuild "inc" as a LABEL instead of BUTTON */
  t = lk_ui_begin_frame(ui);
  w = lk_tree_add_node_s(t, lk_str_c("main"), UIK_WINDOW);
  col = lk_tree_add_node_s(t, lk_str_c("root"), UIK_COLUMN);
  lbl = lk_tree_add_node_s(t, lk_str_c("inc"), UIK_LABEL);
  lk_tree_set_root(t, w);
  lk_tree_append_child(t, w, col);
  lk_tree_append_child(t, col, lbl);
  lk_tree_add_prop(t, lbl, UIP_TEXT, lk_v_cstr(t->intern, "Increment"));
  cs = lk_ui_end_frame(ui);

  CHECK_EQ(cs->count, 1);
  CHECK(cs_has(cs, ui, LK_CHANGE_UPDATED, "inc"));

  END_TEST();
  lk_ui_destroy(ui);
}

/* ================================================================
 * Tests: structural changes (add/remove children)
 * ================================================================ */

static void test_child_added(void) {
  lk_ui *ui = lk_ui_create(NULL);
  lk_tree *t;
  const lk_changeset *cs;
  lk_ix w, col, btn, lbl;

  BEGIN_TEST("add one child: one ADDED");

  prime_base(ui);

  t = lk_ui_begin_frame(ui);
  w = lk_tree_add_node_s(t, lk_str_c("main"), UIK_WINDOW);
  col = lk_tree_add_node_s(t, lk_str_c("root"), UIK_COLUMN);
  btn = lk_tree_add_node_s(t, lk_str_c("inc"), UIK_BUTTON);
  lbl = lk_tree_add_node_s(t, lk_str_c("title"), UIK_LABEL);
  lk_tree_set_root(t, w);
  lk_tree_append_child(t, w, col);
  lk_tree_append_child(t, col, btn);
  lk_tree_append_child(t, col, lbl);
  lk_tree_add_prop(t, btn, UIP_TEXT, lk_v_cstr(t->intern, "Increment"));
  lk_tree_add_prop(t, lbl, UIP_TEXT, lk_v_cstr(t->intern, "Hello"));
  cs = lk_ui_end_frame(ui);

  CHECK_EQ(cs->count, 1);
  CHECK(cs_has(cs, ui, LK_CHANGE_ADDED, "title"));
  CHECK(!cs_has(cs, ui, LK_CHANGE_UPDATED, "inc"));

  END_TEST();
  lk_ui_destroy(ui);
}

static void test_child_removed(void) {
  lk_ui *ui = lk_ui_create(NULL);
  lk_tree *t;
  const lk_changeset *cs;
  lk_ix w, col;

  BEGIN_TEST("remove one child: one REMOVED");

  prime_base(ui);

  /* rebuild without the button */
  t = lk_ui_begin_frame(ui);
  w = lk_tree_add_node_s(t, lk_str_c("main"), UIK_WINDOW);
  col = lk_tree_add_node_s(t, lk_str_c("root"), UIK_COLUMN);
  lk_tree_set_root(t, w);
  lk_tree_append_child(t, w, col);
  cs = lk_ui_end_frame(ui);

  CHECK_EQ(cs->count, 1);
  CHECK(cs_has(cs, ui, LK_CHANGE_REMOVED, "inc"));

  END_TEST();
  lk_ui_destroy(ui);
}

static void test_child_replaced(void) {
  lk_ui *ui = lk_ui_create(NULL);
  lk_tree *t;
  const lk_changeset *cs;
  lk_ix w, col, lbl;

  BEGIN_TEST("replace child: one REMOVED + one ADDED");

  prime_base(ui);

  t = lk_ui_begin_frame(ui);
  w = lk_tree_add_node_s(t, lk_str_c("main"), UIK_WINDOW);
  col = lk_tree_add_node_s(t, lk_str_c("root"), UIK_COLUMN);
  lbl = lk_tree_add_node_s(t, lk_str_c("title"), UIK_LABEL);
  lk_tree_set_root(t, w);
  lk_tree_append_child(t, w, col);
  lk_tree_append_child(t, col, lbl);
  lk_tree_add_prop(t, lbl, UIP_TEXT, lk_v_cstr(t->intern, "Hello"));
  cs = lk_ui_end_frame(ui);

  CHECK_EQ(cs->count, 2);
  CHECK(cs_has(cs, ui, LK_CHANGE_REMOVED, "inc"));
  CHECK(cs_has(cs, ui, LK_CHANGE_ADDED, "title"));
  CHECK(!cs_has(cs, ui, LK_CHANGE_UPDATED, "main"));
  CHECK(!cs_has(cs, ui, LK_CHANGE_UPDATED, "root"));

  END_TEST();
  lk_ui_destroy(ui);
}

static void test_reorder_no_change(void) {
  lk_ui *ui = lk_ui_create(NULL);
  lk_tree *t;
  const lk_changeset *cs;
  lk_ix w, col, a, b, c;

  BEGIN_TEST("reorder children: no changes");

  /* frame 1: a, b, c */
  t = lk_ui_begin_frame(ui);
  w = lk_tree_add_node_s(t, lk_str_c("main"), UIK_WINDOW);
  col = lk_tree_add_node_s(t, lk_str_c("root"), UIK_COLUMN);
  a = lk_tree_add_node_s(t, lk_str_c("a"), UIK_LABEL);
  b = lk_tree_add_node_s(t, lk_str_c("b"), UIK_LABEL);
  c = lk_tree_add_node_s(t, lk_str_c("c"), UIK_LABEL);
  lk_tree_set_root(t, w);
  lk_tree_append_child(t, w, col);
  lk_tree_append_child(t, col, a);
  lk_tree_append_child(t, col, b);
  lk_tree_append_child(t, col, c);
  lk_ui_end_frame(ui);

  /* frame 2: c, a, b (same IDs, different order) */
  t = lk_ui_begin_frame(ui);
  w = lk_tree_add_node_s(t, lk_str_c("main"), UIK_WINDOW);
  col = lk_tree_add_node_s(t, lk_str_c("root"), UIK_COLUMN);
  c = lk_tree_add_node_s(t, lk_str_c("c"), UIK_LABEL);
  a = lk_tree_add_node_s(t, lk_str_c("a"), UIK_LABEL);
  b = lk_tree_add_node_s(t, lk_str_c("b"), UIK_LABEL);
  lk_tree_set_root(t, w);
  lk_tree_append_child(t, w, col);
  lk_tree_append_child(t, col, c);
  lk_tree_append_child(t, col, a);
  lk_tree_append_child(t, col, b);
  cs = lk_ui_end_frame(ui);

  CHECK_EQ(cs->count, 0);

  END_TEST();
  lk_ui_destroy(ui);
}

/* ================================================================
 * Tests: subtree add/remove
 * ================================================================ */

static void test_subtree_added(void) {
  lk_ui *ui = lk_ui_create(NULL);
  lk_tree *t;
  const lk_changeset *cs;
  lk_ix w, col, btn, row, lbl1, lbl2;

  BEGIN_TEST("add subtree: parent + children all ADDED");

  prime_base(ui);

  /* add a row with two labels alongside the button */
  t = lk_ui_begin_frame(ui);
  w = lk_tree_add_node_s(t, lk_str_c("main"), UIK_WINDOW);
  col = lk_tree_add_node_s(t, lk_str_c("root"), UIK_COLUMN);
  btn = lk_tree_add_node_s(t, lk_str_c("inc"), UIK_BUTTON);
  row = lk_tree_add_node_s(t, lk_str_c("bar"), UIK_ROW);
  lbl1 = lk_tree_add_node_s(t, lk_str_c("l1"), UIK_LABEL);
  lbl2 = lk_tree_add_node_s(t, lk_str_c("l2"), UIK_LABEL);
  lk_tree_set_root(t, w);
  lk_tree_append_child(t, w, col);
  lk_tree_append_child(t, col, btn);
  lk_tree_append_child(t, col, row);
  lk_tree_append_child(t, row, lbl1);
  lk_tree_append_child(t, row, lbl2);
  lk_tree_add_prop(t, btn, UIP_TEXT, lk_v_cstr(t->intern, "Increment"));
  cs = lk_ui_end_frame(ui);

  CHECK_EQ(cs->count, 3);
  CHECK_EQ(cs_count_kind(cs, LK_CHANGE_ADDED), 3);
  CHECK(cs_has(cs, ui, LK_CHANGE_ADDED, "bar"));
  CHECK(cs_has(cs, ui, LK_CHANGE_ADDED, "l1"));
  CHECK(cs_has(cs, ui, LK_CHANGE_ADDED, "l2"));

  END_TEST();
  lk_ui_destroy(ui);
}

static void test_subtree_removed(void) {
  lk_ui *ui = lk_ui_create(NULL);
  lk_tree *t;
  const lk_changeset *cs;
  lk_ix w, col, btn, row, lbl1, lbl2;

  BEGIN_TEST("remove subtree: parent + children all REMOVED");

  /* build tree with a nested row */
  t = lk_ui_begin_frame(ui);
  w = lk_tree_add_node_s(t, lk_str_c("main"), UIK_WINDOW);
  col = lk_tree_add_node_s(t, lk_str_c("root"), UIK_COLUMN);
  btn = lk_tree_add_node_s(t, lk_str_c("inc"), UIK_BUTTON);
  row = lk_tree_add_node_s(t, lk_str_c("bar"), UIK_ROW);
  lbl1 = lk_tree_add_node_s(t, lk_str_c("l1"), UIK_LABEL);
  lbl2 = lk_tree_add_node_s(t, lk_str_c("l2"), UIK_LABEL);
  lk_tree_set_root(t, w);
  lk_tree_append_child(t, w, col);
  lk_tree_append_child(t, col, btn);
  lk_tree_append_child(t, col, row);
  lk_tree_append_child(t, row, lbl1);
  lk_tree_append_child(t, row, lbl2);
  lk_tree_add_prop(t, btn, UIP_TEXT, lk_v_cstr(t->intern, "Increment"));
  lk_ui_end_frame(ui);

  /* remove the row (and its children) */
  t = lk_ui_begin_frame(ui);
  w = lk_tree_add_node_s(t, lk_str_c("main"), UIK_WINDOW);
  col = lk_tree_add_node_s(t, lk_str_c("root"), UIK_COLUMN);
  btn = lk_tree_add_node_s(t, lk_str_c("inc"), UIK_BUTTON);
  lk_tree_set_root(t, w);
  lk_tree_append_child(t, w, col);
  lk_tree_append_child(t, col, btn);
  lk_tree_add_prop(t, btn, UIP_TEXT, lk_v_cstr(t->intern, "Increment"));
  cs = lk_ui_end_frame(ui);

  CHECK_EQ(cs->count, 3);
  CHECK_EQ(cs_count_kind(cs, LK_CHANGE_REMOVED), 3);
  CHECK(cs_has(cs, ui, LK_CHANGE_REMOVED, "bar"));
  CHECK(cs_has(cs, ui, LK_CHANGE_REMOVED, "l1"));
  CHECK(cs_has(cs, ui, LK_CHANGE_REMOVED, "l2"));

  END_TEST();
  lk_ui_destroy(ui);
}

/* ================================================================
 * Tests: deep tree
 * ================================================================ */

static void test_deep_change(void) {
  lk_ui *ui = lk_ui_create(NULL);
  lk_tree *t;
  const lk_changeset *cs;
  lk_ix w, col, row, lbl;

  BEGIN_TEST("deep prop change: only leaf UPDATED");

  /* frame 1: window > column > row > label */
  t = lk_ui_begin_frame(ui);
  w = lk_tree_add_node_s(t, lk_str_c("main"), UIK_WINDOW);
  col = lk_tree_add_node_s(t, lk_str_c("col"), UIK_COLUMN);
  row = lk_tree_add_node_s(t, lk_str_c("row"), UIK_ROW);
  lbl = lk_tree_add_node_s(t, lk_str_c("lbl"), UIK_LABEL);
  lk_tree_set_root(t, w);
  lk_tree_append_child(t, w, col);
  lk_tree_append_child(t, col, row);
  lk_tree_append_child(t, row, lbl);
  lk_tree_add_prop(t, lbl, UIP_TEXT, lk_v_cstr(t->intern, "A"));
  lk_ui_end_frame(ui);

  /* frame 2: only the leaf label text changes */
  t = lk_ui_begin_frame(ui);
  w = lk_tree_add_node_s(t, lk_str_c("main"), UIK_WINDOW);
  col = lk_tree_add_node_s(t, lk_str_c("col"), UIK_COLUMN);
  row = lk_tree_add_node_s(t, lk_str_c("row"), UIK_ROW);
  lbl = lk_tree_add_node_s(t, lk_str_c("lbl"), UIK_LABEL);
  lk_tree_set_root(t, w);
  lk_tree_append_child(t, w, col);
  lk_tree_append_child(t, col, row);
  lk_tree_append_child(t, row, lbl);
  lk_tree_add_prop(t, lbl, UIP_TEXT, lk_v_cstr(t->intern, "B"));
  cs = lk_ui_end_frame(ui);

  CHECK_EQ(cs->count, 1);
  CHECK(cs_has(cs, ui, LK_CHANGE_UPDATED, "lbl"));
  CHECK(!cs_has(cs, ui, LK_CHANGE_UPDATED, "main"));
  CHECK(!cs_has(cs, ui, LK_CHANGE_UPDATED, "col"));
  CHECK(!cs_has(cs, ui, LK_CHANGE_UPDATED, "row"));

  END_TEST();
  lk_ui_destroy(ui);
}

/* ================================================================
 * Tests: edge cases
 * ================================================================ */

static void test_root_identity_change(void) {
  lk_ui *ui = lk_ui_create(NULL);
  lk_tree *t;
  const lk_changeset *cs;
  lk_ix w, col;

  BEGIN_TEST("root identity change: full remove + add");

  prime_base(ui);

  /* rebuild with a different root id */
  t = lk_ui_begin_frame(ui);
  w = lk_tree_add_node_s(t, lk_str_c("app"), UIK_WINDOW);
  col = lk_tree_add_node_s(t, lk_str_c("stuff"), UIK_COLUMN);
  lk_tree_set_root(t, w);
  lk_tree_append_child(t, w, col);
  cs = lk_ui_end_frame(ui);

  /* old tree (main, root, inc) removed; new tree (app, stuff) added */
  CHECK_EQ(cs_count_kind(cs, LK_CHANGE_REMOVED), 3);
  CHECK_EQ(cs_count_kind(cs, LK_CHANGE_ADDED), 2);
  CHECK(cs_has(cs, ui, LK_CHANGE_REMOVED, "main"));
  CHECK(cs_has(cs, ui, LK_CHANGE_REMOVED, "root"));
  CHECK(cs_has(cs, ui, LK_CHANGE_REMOVED, "inc"));
  CHECK(cs_has(cs, ui, LK_CHANGE_ADDED, "app"));
  CHECK(cs_has(cs, ui, LK_CHANGE_ADDED, "stuff"));

  END_TEST();
  lk_ui_destroy(ui);
}

static void test_to_empty(void) {
  lk_ui *ui = lk_ui_create(NULL);
  lk_tree *t;
  const lk_changeset *cs;

  BEGIN_TEST("populated -> empty: all REMOVED");

  prime_base(ui);

  /* submit an empty frame (no root set) */
  t = lk_ui_begin_frame(ui);
  (void)t;
  cs = lk_ui_end_frame(ui);

  CHECK_EQ(cs->count, 3);
  CHECK_EQ(cs_count_kind(cs, LK_CHANGE_REMOVED), 3);
  CHECK(cs_has(cs, ui, LK_CHANGE_REMOVED, "main"));
  CHECK(cs_has(cs, ui, LK_CHANGE_REMOVED, "root"));
  CHECK(cs_has(cs, ui, LK_CHANGE_REMOVED, "inc"));

  END_TEST();
  lk_ui_destroy(ui);
}

static void test_empty_to_empty(void) {
  lk_ui *ui = lk_ui_create(NULL);
  lk_tree *t;
  const lk_changeset *cs;

  BEGIN_TEST("empty -> empty: no changes");

  /* first frame: empty */
  t = lk_ui_begin_frame(ui);
  (void)t;
  lk_ui_end_frame(ui);

  /* second frame: still empty */
  t = lk_ui_begin_frame(ui);
  (void)t;
  cs = lk_ui_end_frame(ui);

  CHECK_EQ(cs->count, 0);

  END_TEST();
  lk_ui_destroy(ui);
}

static void test_single_root_only(void) {
  lk_ui *ui = lk_ui_create(NULL);
  lk_tree *t;
  const lk_changeset *cs;
  lk_ix w;

  BEGIN_TEST("single root node, no children");

  t = lk_ui_begin_frame(ui);
  w = lk_tree_add_node_s(t, lk_str_c("w"), UIK_WINDOW);
  lk_tree_set_root(t, w);
  cs = lk_ui_end_frame(ui);

  CHECK_EQ(cs->count, 1);
  CHECK(cs_has(cs, ui, LK_CHANGE_ADDED, "w"));

  /* identical rebuild */
  t = lk_ui_begin_frame(ui);
  w = lk_tree_add_node_s(t, lk_str_c("w"), UIK_WINDOW);
  lk_tree_set_root(t, w);
  cs = lk_ui_end_frame(ui);

  CHECK_EQ(cs->count, 0);

  END_TEST();
  lk_ui_destroy(ui);
}

/* ================================================================
 * Tests: changeset node_ix validity
 * ================================================================ */

static void test_node_ix_valid_after_swap(void) {
  lk_ui *ui = lk_ui_create(NULL);
  lk_tree *t;
  const lk_changeset *cs;
  const lk_tree *cur;
  lk_u32 i;

  BEGIN_TEST("ADDED/UPDATED node_ix valid in current tree");

  t = lk_ui_begin_frame(ui);
  build_base_tree(t);
  cs = lk_ui_end_frame(ui);
  cur = lk_ui_tree(ui);

  /* every ADDED entry's node_ix should point to a node with matching id */
  for (i = 0; i < cs->count; i++) {
    const lk_change *c = &cs->changes[i];

    if (c->kind == LK_CHANGE_ADDED || c->kind == LK_CHANGE_UPDATED) {
      CHECK(c->node_ix > 0);
      CHECK(c->node_ix < cur->node_count);
      CHECK_EQ(cur->nodes[c->node_ix].id, c->id);
    }
  }

  END_TEST();
  lk_ui_destroy(ui);
}

static void test_removed_node_ix_zero(void) {
  lk_ui *ui = lk_ui_create(NULL);
  lk_tree *t;
  const lk_changeset *cs;
  lk_u32 i;

  BEGIN_TEST("REMOVED entries have node_ix == 0");

  prime_base(ui);

  /* remove button */
  t = lk_ui_begin_frame(ui);
  {
    lk_ix w = lk_tree_add_node_s(t, lk_str_c("main"), UIK_WINDOW);
    lk_ix col = lk_tree_add_node_s(t, lk_str_c("root"), UIK_COLUMN);
    lk_tree_set_root(t, w);
    lk_tree_append_child(t, w, col);
  }
  cs = lk_ui_end_frame(ui);

  for (i = 0; i < cs->count; i++) {
    if (cs->changes[i].kind == LK_CHANGE_REMOVED) {
      CHECK_EQ(cs->changes[i].node_ix, 0);
    }
  }

  END_TEST();
  lk_ui_destroy(ui);
}

/* ================================================================
 * Tests: multi-frame sequences
 * ================================================================ */

static void test_add_then_remove(void) {
  lk_ui *ui = lk_ui_create(NULL);
  lk_tree *t;
  const lk_changeset *cs;
  lk_ix w, col, btn, lbl;

  BEGIN_TEST("add child then remove it next frame");

  prime_base(ui);

  /* frame 2: add a label */
  t = lk_ui_begin_frame(ui);
  w = lk_tree_add_node_s(t, lk_str_c("main"), UIK_WINDOW);
  col = lk_tree_add_node_s(t, lk_str_c("root"), UIK_COLUMN);
  btn = lk_tree_add_node_s(t, lk_str_c("inc"), UIK_BUTTON);
  lbl = lk_tree_add_node_s(t, lk_str_c("title"), UIK_LABEL);
  lk_tree_set_root(t, w);
  lk_tree_append_child(t, w, col);
  lk_tree_append_child(t, col, btn);
  lk_tree_append_child(t, col, lbl);
  lk_tree_add_prop(t, btn, UIP_TEXT, lk_v_cstr(t->intern, "Increment"));
  cs = lk_ui_end_frame(ui);
  CHECK_EQ(cs_count_kind(cs, LK_CHANGE_ADDED), 1);
  CHECK(cs_has(cs, ui, LK_CHANGE_ADDED, "title"));

  /* frame 3: remove the label again */
  t = lk_ui_begin_frame(ui);
  w = lk_tree_add_node_s(t, lk_str_c("main"), UIK_WINDOW);
  col = lk_tree_add_node_s(t, lk_str_c("root"), UIK_COLUMN);
  btn = lk_tree_add_node_s(t, lk_str_c("inc"), UIK_BUTTON);
  lk_tree_set_root(t, w);
  lk_tree_append_child(t, w, col);
  lk_tree_append_child(t, col, btn);
  lk_tree_add_prop(t, btn, UIP_TEXT, lk_v_cstr(t->intern, "Increment"));
  cs = lk_ui_end_frame(ui);
  CHECK_EQ(cs_count_kind(cs, LK_CHANGE_REMOVED), 1);
  CHECK(cs_has(cs, ui, LK_CHANGE_REMOVED, "title"));

  /* frame 4: identical to frame 3 — no changes */
  t = lk_ui_begin_frame(ui);
  w = lk_tree_add_node_s(t, lk_str_c("main"), UIK_WINDOW);
  col = lk_tree_add_node_s(t, lk_str_c("root"), UIK_COLUMN);
  btn = lk_tree_add_node_s(t, lk_str_c("inc"), UIK_BUTTON);
  lk_tree_set_root(t, w);
  lk_tree_append_child(t, w, col);
  lk_tree_append_child(t, col, btn);
  lk_tree_add_prop(t, btn, UIP_TEXT, lk_v_cstr(t->intern, "Increment"));
  cs = lk_ui_end_frame(ui);
  CHECK_EQ(cs->count, 0);

  END_TEST();
  lk_ui_destroy(ui);
}

static void test_many_siblings(void) {
  lk_ui *ui = lk_ui_create(NULL);
  lk_tree *t;
  const lk_changeset *cs;
  lk_ix w, col;
  char name[16];
  int i;

  BEGIN_TEST("many siblings: add 20, remove 5, add 3");

  /* frame 1: 20 labels */
  t = lk_ui_begin_frame(ui);
  w = lk_tree_add_node_s(t, lk_str_c("w"), UIK_WINDOW);
  col = lk_tree_add_node_s(t, lk_str_c("c"), UIK_COLUMN);
  lk_tree_set_root(t, w);
  lk_tree_append_child(t, w, col);

  for (i = 0; i < 20; i++) {
    lk_ix lbl;
    sprintf(name, "n%d", i);
    lbl = lk_tree_add_node_s(t, lk_str_c(name), UIK_LABEL);
    lk_tree_append_child(t, col, lbl);
  }
  cs = lk_ui_end_frame(ui);
  CHECK_EQ(cs_count_kind(cs, LK_CHANGE_ADDED), 22); /* w + c + 20 labels */

  /* frame 2: keep 15 (drop n0-n4), add n20-n22 */
  t = lk_ui_begin_frame(ui);
  w = lk_tree_add_node_s(t, lk_str_c("w"), UIK_WINDOW);
  col = lk_tree_add_node_s(t, lk_str_c("c"), UIK_COLUMN);
  lk_tree_set_root(t, w);
  lk_tree_append_child(t, w, col);

  for (i = 5; i < 23; i++) {
    lk_ix lbl;
    sprintf(name, "n%d", i);
    lbl = lk_tree_add_node_s(t, lk_str_c(name), UIK_LABEL);
    lk_tree_append_child(t, col, lbl);
  }
  cs = lk_ui_end_frame(ui);
  CHECK_EQ(cs_count_kind(cs, LK_CHANGE_REMOVED), 5);
  CHECK_EQ(cs_count_kind(cs, LK_CHANGE_ADDED), 3);
  CHECK_EQ(cs_count_kind(cs, LK_CHANGE_UPDATED), 0);
  CHECK(cs_has(cs, ui, LK_CHANGE_REMOVED, "n0"));
  CHECK(cs_has(cs, ui, LK_CHANGE_REMOVED, "n4"));
  CHECK(cs_has(cs, ui, LK_CHANGE_ADDED, "n20"));
  CHECK(cs_has(cs, ui, LK_CHANGE_ADDED, "n22"));

  END_TEST();
  lk_ui_destroy(ui);
}

static void test_ui_tree_returns_current(void) {
  lk_ui *ui = lk_ui_create(NULL);
  lk_tree *t;
  const lk_tree *cur;
  lk_ix w;
  lk_node_id wid;

  BEGIN_TEST("lk_ui_tree returns the just-submitted tree");

  t = lk_ui_begin_frame(ui);
  w = lk_tree_add_node_s(t, lk_str_c("mywin"), UIK_WINDOW);
  lk_tree_set_root(t, w);
  wid = t->nodes[w].id;
  lk_ui_end_frame(ui);

  cur = lk_ui_tree(ui);
  CHECK(cur != NULL);
  CHECK(cur->root != 0);
  CHECK_EQ(cur->nodes[cur->root].id, wid);

  END_TEST();
  lk_ui_destroy(ui);
}

/* ================================================================
 * Tests: layout
 * ================================================================ */

/* helper: build tree, run layout, return rects (caller must free) */
static lk_rect *run_layout(lk_tree *t, lk_i32 vw, lk_i32 vh) {
  lk_layout_cfg cfg;
  lk_rect *rects;

  memset(&cfg, 0, sizeof(cfg));
  cfg.measure_text = lk_measure_text_stub;
  cfg.measure_ud = NULL;
  cfg.viewport_w = vw;
  cfg.viewport_h = vh;

  rects = (lk_rect *)malloc(sizeof(lk_rect) * t->node_count);
  if (!rects) {
    return NULL;
  }

  if (!lk_layout(t, &cfg, rects)) {
    free(rects);
    return NULL;
  }

  return rects;
}

#define CHECK_RECT(r, ex, ey, ew, eh)                                          \
  do {                                                                         \
    CHECK_EQ((unsigned)(r).x, (unsigned)(ex));                                 \
    CHECK_EQ((unsigned)(r).y, (unsigned)(ey));                                 \
    CHECK_EQ((unsigned)(r).w, (unsigned)(ew));                                 \
    CHECK_EQ((unsigned)(r).h, (unsigned)(eh));                                 \
  } while (0)

static void test_layout_window_fills_viewport(void) {
  lk_tree *t = lk_tree_create(NULL);
  lk_ix w;
  lk_rect *r;

  BEGIN_TEST("layout: window fills viewport");

  w = lk_tree_add_node_s(t, lk_str_c("w"), UIK_WINDOW);
  lk_tree_set_root(t, w);

  r = run_layout(t, 800, 600);
  CHECK(r != NULL);
  if (r) {
    CHECK_RECT(r[w], 0, 0, 800, 600);
    free(r);
  }

  END_TEST();
  lk_tree_destroy(t);
}

static void test_layout_column_two_labels(void) {
  /* Column with two labels: labels stretch cross-axis, intrinsic main-axis.
   * "Hello" = 5 chars = 40px wide, 16px tall
   * "World!" = 6 chars = 48px wide, 16px tall
   */
  lk_tree *t = lk_tree_create(NULL);
  lk_ix w, col, l1, l2;
  lk_rect *r;

  BEGIN_TEST("layout: column with two labels");

  w = lk_tree_add_node_s(t, lk_str_c("w"), UIK_WINDOW);
  col = lk_tree_add_node_s(t, lk_str_c("col"), UIK_COLUMN);
  l1 = lk_tree_add_node_s(t, lk_str_c("l1"), UIK_LABEL);
  lk_tree_add_prop(t, l1, UIP_TEXT, lk_v_cstr(t->intern, "Hello"));
  l2 = lk_tree_add_node_s(t, lk_str_c("l2"), UIK_LABEL);
  lk_tree_add_prop(t, l2, UIP_TEXT, lk_v_cstr(t->intern, "World!"));

  lk_tree_set_root(t, w);
  lk_tree_append_child(t, w, col);
  lk_tree_append_child(t, col, l1);
  lk_tree_append_child(t, col, l2);

  r = run_layout(t, 800, 600);
  CHECK(r != NULL);
  if (r) {
    CHECK_RECT(r[w], 0, 0, 800, 600);
    CHECK_RECT(r[col], 0, 0, 800, 600); /* column stretches to window */
    CHECK_RECT(r[l1], 0, 0, 800, 16);   /* stretch cross, intrinsic main */
    CHECK_RECT(r[l2], 0, 16, 800, 16);
    free(r);
  }

  END_TEST();
  lk_tree_destroy(t);
}

static void test_layout_column_with_gap(void) {
  lk_tree *t = lk_tree_create(NULL);
  lk_ix w, col, l1, l2;
  lk_rect *r;

  BEGIN_TEST("layout: column with gap");

  w = lk_tree_add_node_s(t, lk_str_c("w"), UIK_WINDOW);
  col = lk_tree_add_node_s(t, lk_str_c("col"), UIK_COLUMN);
  lk_tree_add_prop(t, col, UIP_GAP, lk_v_i32(10));
  l1 = lk_tree_add_node_s(t, lk_str_c("l1"), UIK_LABEL);
  lk_tree_add_prop(t, l1, UIP_TEXT, lk_v_cstr(t->intern, "A"));
  l2 = lk_tree_add_node_s(t, lk_str_c("l2"), UIK_LABEL);
  lk_tree_add_prop(t, l2, UIP_TEXT, lk_v_cstr(t->intern, "B"));

  lk_tree_set_root(t, w);
  lk_tree_append_child(t, w, col);
  lk_tree_append_child(t, col, l1);
  lk_tree_append_child(t, col, l2);

  r = run_layout(t, 800, 600);
  CHECK(r != NULL);
  if (r) {
    CHECK_RECT(r[l1], 0, 0, 800, 16);
    CHECK_RECT(r[l2], 0, 26, 800, 16); /* 16 + 10 gap = 26 */
    free(r);
  }

  END_TEST();
  lk_tree_destroy(t);
}

static void test_layout_column_with_padding(void) {
  lk_tree *t = lk_tree_create(NULL);
  lk_ix w, col, l1;
  lk_rect *r;

  BEGIN_TEST("layout: column with padding");

  w = lk_tree_add_node_s(t, lk_str_c("w"), UIK_WINDOW);
  col = lk_tree_add_node_s(t, lk_str_c("col"), UIK_COLUMN);
  lk_tree_add_prop(t, col, UIP_PADDING, lk_v_i32(20));
  l1 = lk_tree_add_node_s(t, lk_str_c("l1"), UIK_LABEL);
  lk_tree_add_prop(t, l1, UIP_TEXT, lk_v_cstr(t->intern, "Hi"));

  lk_tree_set_root(t, w);
  lk_tree_append_child(t, w, col);
  lk_tree_append_child(t, col, l1);

  r = run_layout(t, 800, 600);
  CHECK(r != NULL);
  if (r) {
    /* col stretches to window (800x600), content area starts at (20,20), w=760
     */
    CHECK_RECT(r[l1], 20, 20, 760, 16);
    free(r);
  }

  END_TEST();
  lk_tree_destroy(t);
}

static void test_layout_row_two_labels(void) {
  /* "AA" = 16px wide, "BBB" = 24px wide, both 16px tall */
  lk_tree *t = lk_tree_create(NULL);
  lk_ix w, row, l1, l2;
  lk_rect *r;

  BEGIN_TEST("layout: row with two labels");

  w = lk_tree_add_node_s(t, lk_str_c("w"), UIK_WINDOW);
  row = lk_tree_add_node_s(t, lk_str_c("row"), UIK_ROW);
  l1 = lk_tree_add_node_s(t, lk_str_c("l1"), UIK_LABEL);
  lk_tree_add_prop(t, l1, UIP_TEXT, lk_v_cstr(t->intern, "AA"));
  l2 = lk_tree_add_node_s(t, lk_str_c("l2"), UIK_LABEL);
  lk_tree_add_prop(t, l2, UIP_TEXT, lk_v_cstr(t->intern, "BBB"));

  lk_tree_set_root(t, w);
  lk_tree_append_child(t, w, row);
  lk_tree_append_child(t, row, l1);
  lk_tree_append_child(t, row, l2);

  r = run_layout(t, 800, 600);
  CHECK(r != NULL);
  if (r) {
    CHECK_RECT(r[row], 0, 0, 800, 600);
    CHECK_RECT(r[l1], 0, 0, 16, 600); /* intrinsic w, stretch h */
    CHECK_RECT(r[l2], 16, 0, 24, 600);
    free(r);
  }

  END_TEST();
  lk_tree_destroy(t);
}

static void test_layout_row_with_gap(void) {
  lk_tree *t = lk_tree_create(NULL);
  lk_ix w, row, l1, l2;
  lk_rect *r;

  BEGIN_TEST("layout: row with gap");

  w = lk_tree_add_node_s(t, lk_str_c("w"), UIK_WINDOW);
  row = lk_tree_add_node_s(t, lk_str_c("row"), UIK_ROW);
  lk_tree_add_prop(t, row, UIP_GAP, lk_v_i32(5));
  l1 = lk_tree_add_node_s(t, lk_str_c("l1"), UIK_LABEL);
  lk_tree_add_prop(t, l1, UIP_TEXT, lk_v_cstr(t->intern, "X"));
  l2 = lk_tree_add_node_s(t, lk_str_c("l2"), UIK_LABEL);
  lk_tree_add_prop(t, l2, UIP_TEXT, lk_v_cstr(t->intern, "Y"));

  lk_tree_set_root(t, w);
  lk_tree_append_child(t, w, row);
  lk_tree_append_child(t, row, l1);
  lk_tree_append_child(t, row, l2);

  r = run_layout(t, 800, 600);
  CHECK(r != NULL);
  if (r) {
    /* X = 8px, Y = 8px, gap = 5 */
    CHECK_RECT(r[l1], 0, 0, 8, 600);
    CHECK_RECT(r[l2], 13, 0, 8, 600); /* 8 + 5 = 13 */
    free(r);
  }

  END_TEST();
  lk_tree_destroy(t);
}

static void test_layout_spacer_in_column(void) {
  /* column > spacer + label: spacer takes remaining space, pushes label down */
  lk_tree *t = lk_tree_create(NULL);
  lk_ix w, col, sp, l1;
  lk_rect *r;

  BEGIN_TEST("layout: spacer in column pushes label down");

  w = lk_tree_add_node_s(t, lk_str_c("w"), UIK_WINDOW);
  col = lk_tree_add_node_s(t, lk_str_c("col"), UIK_COLUMN);
  sp = lk_tree_add_node_s(t, lk_str_c("sp"), UIK_SPACER);
  l1 = lk_tree_add_node_s(t, lk_str_c("l1"), UIK_LABEL);
  lk_tree_add_prop(t, l1, UIP_TEXT, lk_v_cstr(t->intern, "Bottom"));

  lk_tree_set_root(t, w);
  lk_tree_append_child(t, w, col);
  lk_tree_append_child(t, col, sp);
  lk_tree_append_child(t, col, l1);

  r = run_layout(t, 800, 600);
  CHECK(r != NULL);
  if (r) {
    /* label is 48px wide ("Bottom" = 6*8), 16px tall
     * spacer takes 600 - 16 = 584
     */
    CHECK_RECT(r[sp], 0, 0, 800, 584);
    CHECK_RECT(r[l1], 0, 584, 800, 16);
    free(r);
  }

  END_TEST();
  lk_tree_destroy(t);
}

static void test_layout_two_spacers_split(void) {
  /* column > spacer + label + spacer: two spacers split remaining evenly */
  lk_tree *t = lk_tree_create(NULL);
  lk_ix w, col, sp1, l1, sp2;
  lk_rect *r;

  BEGIN_TEST("layout: two spacers split remaining space");

  w = lk_tree_add_node_s(t, lk_str_c("w"), UIK_WINDOW);
  col = lk_tree_add_node_s(t, lk_str_c("col"), UIK_COLUMN);
  sp1 = lk_tree_add_node_s(t, lk_str_c("sp1"), UIK_SPACER);
  l1 = lk_tree_add_node_s(t, lk_str_c("l1"), UIK_LABEL);
  lk_tree_add_prop(t, l1, UIP_TEXT, lk_v_cstr(t->intern, "Mid"));
  sp2 = lk_tree_add_node_s(t, lk_str_c("sp2"), UIK_SPACER);

  lk_tree_set_root(t, w);
  lk_tree_append_child(t, w, col);
  lk_tree_append_child(t, col, sp1);
  lk_tree_append_child(t, col, l1);
  lk_tree_append_child(t, col, sp2);

  r = run_layout(t, 800, 600);
  CHECK(r != NULL);
  if (r) {
    /* label "Mid" = 3*8 = 24px wide, 16px tall
     * remaining = 600 - 16 = 584, two spacers: 292 each
     */
    CHECK_RECT(r[sp1], 0, 0, 800, 292);
    CHECK_RECT(r[l1], 0, 292, 800, 16);
    CHECK_RECT(r[sp2], 0, 308, 800, 292);
    free(r);
  }

  END_TEST();
  lk_tree_destroy(t);
}

static void test_layout_spacer_in_row(void) {
  /* row > label + spacer + label */
  lk_tree *t = lk_tree_create(NULL);
  lk_ix w, row, l1, sp, l2;
  lk_rect *r;

  BEGIN_TEST("layout: spacer in row");

  w = lk_tree_add_node_s(t, lk_str_c("w"), UIK_WINDOW);
  row = lk_tree_add_node_s(t, lk_str_c("row"), UIK_ROW);
  l1 = lk_tree_add_node_s(t, lk_str_c("l1"), UIK_LABEL);
  lk_tree_add_prop(t, l1, UIP_TEXT, lk_v_cstr(t->intern, "L"));
  sp = lk_tree_add_node_s(t, lk_str_c("sp"), UIK_SPACER);
  l2 = lk_tree_add_node_s(t, lk_str_c("l2"), UIK_LABEL);
  lk_tree_add_prop(t, l2, UIP_TEXT, lk_v_cstr(t->intern, "R"));

  lk_tree_set_root(t, w);
  lk_tree_append_child(t, w, row);
  lk_tree_append_child(t, row, l1);
  lk_tree_append_child(t, row, sp);
  lk_tree_append_child(t, row, l2);

  r = run_layout(t, 800, 600);
  CHECK(r != NULL);
  if (r) {
    /* "L" = 8px, "R" = 8px, spacer = 800 - 8 - 8 = 784 */
    CHECK_RECT(r[l1], 0, 0, 8, 600);
    CHECK_RECT(r[sp], 8, 0, 784, 600);
    CHECK_RECT(r[l2], 792, 0, 8, 600);
    free(r);
  }

  END_TEST();
  lk_tree_destroy(t);
}

static void test_layout_explicit_wh(void) {
  /* explicit W/H override intrinsics */
  lk_tree *t = lk_tree_create(NULL);
  lk_ix w, col, l1;
  lk_rect *r;

  BEGIN_TEST("layout: explicit W/H override intrinsics");

  w = lk_tree_add_node_s(t, lk_str_c("w"), UIK_WINDOW);
  col = lk_tree_add_node_s(t, lk_str_c("col"), UIK_COLUMN);
  l1 = lk_tree_add_node_s(t, lk_str_c("l1"), UIK_LABEL);
  lk_tree_add_prop(t, l1, UIP_TEXT, lk_v_cstr(t->intern, "Hi"));
  lk_tree_add_prop(t, l1, UIP_W, lk_v_i32(200));
  lk_tree_add_prop(t, l1, UIP_H, lk_v_i32(50));

  lk_tree_set_root(t, w);
  lk_tree_append_child(t, w, col);
  lk_tree_append_child(t, col, l1);

  r = run_layout(t, 800, 600);
  CHECK(r != NULL);
  if (r) {
    /* explicit W=200 overrides cross-axis stretch, explicit H=50 overrides
     * intrinsic 16 */
    CHECK_RECT(r[l1], 0, 0, 200, 50);
    free(r);
  }

  END_TEST();
  lk_tree_destroy(t);
}

static void test_layout_button_intrinsic(void) {
  /* button intrinsic = text size + padding*2 on each axis */
  lk_tree *t = lk_tree_create(NULL);
  lk_ix w, col, btn;
  lk_rect *r;

  BEGIN_TEST("layout: button intrinsic includes padding");

  w = lk_tree_add_node_s(t, lk_str_c("w"), UIK_WINDOW);
  col = lk_tree_add_node_s(t, lk_str_c("col"), UIK_COLUMN);
  btn = lk_tree_add_node_s(t, lk_str_c("btn"), UIK_BUTTON);
  lk_tree_add_prop(t, btn, UIP_TEXT, lk_v_cstr(t->intern, "OK"));
  lk_tree_add_prop(t, btn, UIP_PADDING, lk_v_i32(8));

  lk_tree_set_root(t, w);
  lk_tree_append_child(t, w, col);
  lk_tree_append_child(t, col, btn);

  r = run_layout(t, 800, 600);
  CHECK(r != NULL);
  if (r) {
    /* "OK" = 2*8 = 16px, +8*2 padding = 32px wide, 16+16=32 tall
     * but cross-axis stretches in column (no explicit W), so w = 800
     * main axis is intrinsic: h = 32
     */
    CHECK_RECT(r[btn], 0, 0, 800, 32);
    free(r);
  }

  END_TEST();
  lk_tree_destroy(t);
}

static void test_layout_padding_gap_combined(void) {
  lk_tree *t = lk_tree_create(NULL);
  lk_ix w, col, l1, l2;
  lk_rect *r;

  BEGIN_TEST("layout: padding + gap combined");

  w = lk_tree_add_node_s(t, lk_str_c("w"), UIK_WINDOW);
  col = lk_tree_add_node_s(t, lk_str_c("col"), UIK_COLUMN);
  lk_tree_add_prop(t, col, UIP_PADDING, lk_v_i32(10));
  lk_tree_add_prop(t, col, UIP_GAP, lk_v_i32(5));
  l1 = lk_tree_add_node_s(t, lk_str_c("l1"), UIK_LABEL);
  lk_tree_add_prop(t, l1, UIP_TEXT, lk_v_cstr(t->intern, "A"));
  l2 = lk_tree_add_node_s(t, lk_str_c("l2"), UIK_LABEL);
  lk_tree_add_prop(t, l2, UIP_TEXT, lk_v_cstr(t->intern, "B"));

  lk_tree_set_root(t, w);
  lk_tree_append_child(t, w, col);
  lk_tree_append_child(t, col, l1);
  lk_tree_append_child(t, col, l2);

  r = run_layout(t, 800, 600);
  CHECK(r != NULL);
  if (r) {
    /* content area: x=10, y=10, w=780, h=580
     * l1: (10, 10, 780, 16)
     * l2: (10, 10+16+5, 780, 16) = (10, 31, 780, 16)
     */
    CHECK_RECT(r[l1], 10, 10, 780, 16);
    CHECK_RECT(r[l2], 10, 31, 780, 16);
    free(r);
  }

  END_TEST();
  lk_tree_destroy(t);
}

static void test_layout_empty_tree(void) {
  lk_tree *t = lk_tree_create(NULL);
  lk_layout_cfg cfg;
  lk_rect rects[2];
  int ok;

  BEGIN_TEST("layout: empty tree (no root)");

  memset(&cfg, 0, sizeof(cfg));
  cfg.measure_text = lk_measure_text_stub;
  cfg.viewport_w = 800;
  cfg.viewport_h = 600;

  /* root is 0 (no root set), layout should return 0 */
  ok = lk_layout(t, &cfg, rects);
  CHECK_EQ((unsigned)ok, 0u);

  END_TEST();
  lk_tree_destroy(t);
}

static void test_layout_spacer_explicit_h(void) {
  /* spacer with explicit H in column: fixed, not flex */
  lk_tree *t = lk_tree_create(NULL);
  lk_ix w, col, sp, l1;
  lk_rect *r;

  BEGIN_TEST("layout: spacer with explicit H (fixed)");

  w = lk_tree_add_node_s(t, lk_str_c("w"), UIK_WINDOW);
  col = lk_tree_add_node_s(t, lk_str_c("col"), UIK_COLUMN);
  sp = lk_tree_add_node_s(t, lk_str_c("sp"), UIK_SPACER);
  lk_tree_add_prop(t, sp, UIP_H, lk_v_i32(100));
  l1 = lk_tree_add_node_s(t, lk_str_c("l1"), UIK_LABEL);
  lk_tree_add_prop(t, l1, UIP_TEXT, lk_v_cstr(t->intern, "A"));

  lk_tree_set_root(t, w);
  lk_tree_append_child(t, w, col);
  lk_tree_append_child(t, col, sp);
  lk_tree_append_child(t, col, l1);

  r = run_layout(t, 800, 600);
  CHECK(r != NULL);
  if (r) {
    /* spacer is fixed 100px tall, label at y=100 */
    CHECK_RECT(r[sp], 0, 0, 800, 100);
    CHECK_RECT(r[l1], 0, 100, 800, 16);
    free(r);
  }

  END_TEST();
  lk_tree_destroy(t);
}

static void test_layout_nested_column_row_labels(void) {
  /* column > row > two labels */
  lk_tree *t = lk_tree_create(NULL);
  lk_ix w, col, row, l1, l2;
  lk_rect *r;

  BEGIN_TEST("layout: nested column > row > labels");

  w = lk_tree_add_node_s(t, lk_str_c("w"), UIK_WINDOW);
  col = lk_tree_add_node_s(t, lk_str_c("col"), UIK_COLUMN);
  row = lk_tree_add_node_s(t, lk_str_c("row"), UIK_ROW);
  l1 = lk_tree_add_node_s(t, lk_str_c("l1"), UIK_LABEL);
  lk_tree_add_prop(t, l1, UIP_TEXT, lk_v_cstr(t->intern, "AB"));
  l2 = lk_tree_add_node_s(t, lk_str_c("l2"), UIK_LABEL);
  lk_tree_add_prop(t, l2, UIP_TEXT, lk_v_cstr(t->intern, "CD"));

  lk_tree_set_root(t, w);
  lk_tree_append_child(t, w, col);
  lk_tree_append_child(t, col, row);
  lk_tree_append_child(t, row, l1);
  lk_tree_append_child(t, row, l2);

  r = run_layout(t, 800, 600);
  CHECK(r != NULL);
  if (r) {
    /* row intrinsic: w=16+16=32, h=16 (max child)
     * in column, row stretches cross (w=800), uses intrinsic h=16
     * row's content area: (0,0,800,16)
     * l1: "AB"=16px wide, l2: "CD"=16px wide
     * in row, labels get intrinsic w, stretch h to row content (16)
     */
    CHECK_RECT(r[col], 0, 0, 800, 600);
    CHECK_RECT(r[row], 0, 0, 800, 16);
    CHECK_RECT(r[l1], 0, 0, 16, 16);
    CHECK_RECT(r[l2], 16, 0, 16, 16);
    free(r);
  }

  END_TEST();
  lk_tree_destroy(t);
}

/* ================================================================
 * Tests: alignment
 * ================================================================ */

static void test_layout_column_align_center(void) {
  /* column with align=center: children get intrinsic width, centered */
  lk_tree *t = lk_tree_create(NULL);
  lk_ix w, col, l1;
  lk_rect *r;

  BEGIN_TEST("layout: column align=center");

  w = lk_tree_add_node_s(t, lk_str_c("w"), UIK_WINDOW);
  col = lk_tree_add_node_s(t, lk_str_c("col"), UIK_COLUMN);
  lk_tree_add_prop(t, col, UIP_ALIGN, lk_v_i32(LK_ALIGN_CENTER));
  l1 = lk_tree_add_node_s(t, lk_str_c("l1"), UIK_LABEL);
  lk_tree_add_prop(t, l1, UIP_TEXT, lk_v_cstr(t->intern, "Hi"));

  lk_tree_set_root(t, w);
  lk_tree_append_child(t, w, col);
  lk_tree_append_child(t, col, l1);

  r = run_layout(t, 800, 600);
  CHECK(r != NULL);
  if (r) {
    /* "Hi" = 2*8 = 16px wide, centered in 800: x = (800-16)/2 = 392 */
    CHECK_RECT(r[l1], 392, 0, 16, 16);
    free(r);
  }

  END_TEST();
  lk_tree_destroy(t);
}

static void test_layout_column_align_end(void) {
  lk_tree *t = lk_tree_create(NULL);
  lk_ix w, col, l1;
  lk_rect *r;

  BEGIN_TEST("layout: column align=end");

  w = lk_tree_add_node_s(t, lk_str_c("w"), UIK_WINDOW);
  col = lk_tree_add_node_s(t, lk_str_c("col"), UIK_COLUMN);
  lk_tree_add_prop(t, col, UIP_ALIGN, lk_v_i32(LK_ALIGN_END));
  l1 = lk_tree_add_node_s(t, lk_str_c("l1"), UIK_LABEL);
  lk_tree_add_prop(t, l1, UIP_TEXT, lk_v_cstr(t->intern, "Hi"));

  lk_tree_set_root(t, w);
  lk_tree_append_child(t, w, col);
  lk_tree_append_child(t, col, l1);

  r = run_layout(t, 800, 600);
  CHECK(r != NULL);
  if (r) {
    /* "Hi" = 16px wide, end-aligned: x = 800 - 16 = 784 */
    CHECK_RECT(r[l1], 784, 0, 16, 16);
    free(r);
  }

  END_TEST();
  lk_tree_destroy(t);
}

static void test_layout_column_justify_center(void) {
  lk_tree *t = lk_tree_create(NULL);
  lk_ix w, col, l1;
  lk_rect *r;

  BEGIN_TEST("layout: column justify=center");

  w = lk_tree_add_node_s(t, lk_str_c("w"), UIK_WINDOW);
  col = lk_tree_add_node_s(t, lk_str_c("col"), UIK_COLUMN);
  lk_tree_add_prop(t, col, UIP_JUSTIFY, lk_v_i32(LK_ALIGN_CENTER));
  l1 = lk_tree_add_node_s(t, lk_str_c("l1"), UIK_LABEL);
  lk_tree_add_prop(t, l1, UIP_TEXT, lk_v_cstr(t->intern, "A"));

  lk_tree_set_root(t, w);
  lk_tree_append_child(t, w, col);
  lk_tree_append_child(t, col, l1);

  r = run_layout(t, 800, 600);
  CHECK(r != NULL);
  if (r) {
    /* label 16px tall, remaining = 600-16=584, center: y = 584/2 = 292 */
    CHECK_RECT(r[l1], 0, 292, 800, 16);
    free(r);
  }

  END_TEST();
  lk_tree_destroy(t);
}

static void test_layout_column_justify_end(void) {
  lk_tree *t = lk_tree_create(NULL);
  lk_ix w, col, l1;
  lk_rect *r;

  BEGIN_TEST("layout: column justify=end");

  w = lk_tree_add_node_s(t, lk_str_c("w"), UIK_WINDOW);
  col = lk_tree_add_node_s(t, lk_str_c("col"), UIK_COLUMN);
  lk_tree_add_prop(t, col, UIP_JUSTIFY, lk_v_i32(LK_ALIGN_END));
  l1 = lk_tree_add_node_s(t, lk_str_c("l1"), UIK_LABEL);
  lk_tree_add_prop(t, l1, UIP_TEXT, lk_v_cstr(t->intern, "A"));

  lk_tree_set_root(t, w);
  lk_tree_append_child(t, w, col);
  lk_tree_append_child(t, col, l1);

  r = run_layout(t, 800, 600);
  CHECK(r != NULL);
  if (r) {
    /* label 16px tall, remaining = 584, end: y = 584 */
    CHECK_RECT(r[l1], 0, 584, 800, 16);
    free(r);
  }

  END_TEST();
  lk_tree_destroy(t);
}

static void test_layout_row_align_center(void) {
  /* row with align=center: children get intrinsic height, centered */
  lk_tree *t = lk_tree_create(NULL);
  lk_ix w, row, l1;
  lk_rect *r;

  BEGIN_TEST("layout: row align=center");

  w = lk_tree_add_node_s(t, lk_str_c("w"), UIK_WINDOW);
  row = lk_tree_add_node_s(t, lk_str_c("row"), UIK_ROW);
  lk_tree_add_prop(t, row, UIP_ALIGN, lk_v_i32(LK_ALIGN_CENTER));
  l1 = lk_tree_add_node_s(t, lk_str_c("l1"), UIK_LABEL);
  lk_tree_add_prop(t, l1, UIP_TEXT, lk_v_cstr(t->intern, "X"));

  lk_tree_set_root(t, w);
  lk_tree_append_child(t, w, row);
  lk_tree_append_child(t, row, l1);

  r = run_layout(t, 800, 600);
  CHECK(r != NULL);
  if (r) {
    /* "X" = 8px wide, 16px tall. row = 800x600.
     * align=center: y = (600-16)/2 = 292
     */
    CHECK_RECT(r[l1], 0, 292, 8, 16);
    free(r);
  }

  END_TEST();
  lk_tree_destroy(t);
}

static void test_layout_row_justify_end(void) {
  lk_tree *t = lk_tree_create(NULL);
  lk_ix w, row, l1;
  lk_rect *r;

  BEGIN_TEST("layout: row justify=end");

  w = lk_tree_add_node_s(t, lk_str_c("w"), UIK_WINDOW);
  row = lk_tree_add_node_s(t, lk_str_c("row"), UIK_ROW);
  lk_tree_add_prop(t, row, UIP_JUSTIFY, lk_v_i32(LK_ALIGN_END));
  l1 = lk_tree_add_node_s(t, lk_str_c("l1"), UIK_LABEL);
  lk_tree_add_prop(t, l1, UIP_TEXT, lk_v_cstr(t->intern, "X"));

  lk_tree_set_root(t, w);
  lk_tree_append_child(t, w, row);
  lk_tree_append_child(t, row, l1);

  r = run_layout(t, 800, 600);
  CHECK(r != NULL);
  if (r) {
    /* "X" = 8px wide. justify=end: x = 800 - 8 = 792 */
    CHECK_RECT(r[l1], 792, 0, 8, 600);
    free(r);
  }

  END_TEST();
  lk_tree_destroy(t);
}

/* ================================================================
 * Tests: render list
 * ================================================================ */

static void test_render_empty_tree(void) {
  lk_tree *t = lk_tree_create(NULL);
  lk_render_list rl;
  int ok;

  BEGIN_TEST("render: empty tree returns success");

  memset(&rl, 0, sizeof(rl));
  ok = lk_render_build(t, NULL, &rl);
  CHECK_EQ((unsigned)ok, 1u);
  CHECK_EQ(rl.count, 0u);

  lk_render_list_destroy(&rl);
  END_TEST();
  lk_tree_destroy(t);
}

static void test_render_window_only(void) {
  lk_tree *t = lk_tree_create(NULL);
  lk_ix w;
  lk_rect *r;
  lk_render_list rl;

  BEGIN_TEST("render: window only -> FILL_RECT + CLIP_BEGIN + CLIP_END");

  w = lk_tree_add_node_s(t, lk_str_c("w"), UIK_WINDOW);
  lk_tree_set_root(t, w);

  r = run_layout(t, 800, 600);
  CHECK(r != NULL);
  if (r) {
    memset(&rl, 0, sizeof(rl));
    lk_render_build(t, r, &rl);
    CHECK_EQ(rl.count, 3u);
    CHECK_EQ((unsigned)rl.cmds[0].op, (unsigned)LK_ROP_FILL_RECT);
    CHECK_EQ((unsigned)rl.cmds[0].rect.w, 800u);
    CHECK_EQ((unsigned)rl.cmds[0].rect.h, 600u);
    CHECK_EQ((unsigned)rl.cmds[1].op, (unsigned)LK_ROP_CLIP_BEGIN);
    CHECK_EQ((unsigned)rl.cmds[1].rect.w, 800u);
    CHECK_EQ((unsigned)rl.cmds[2].op, (unsigned)LK_ROP_CLIP_END);
    lk_render_list_destroy(&rl);
    free(r);
  }

  END_TEST();
  lk_tree_destroy(t);
}

static void test_render_window_column_label(void) {
  lk_tree *t = lk_tree_create(NULL);
  lk_ix w, col, lbl;
  lk_rect *r;
  lk_render_list rl;
  lk_str resolved;

  BEGIN_TEST("render: window > column > label -> FILL+CLIP+TEXT+CLIP_END");

  w = lk_tree_add_node_s(t, lk_str_c("w"), UIK_WINDOW);
  col = lk_tree_add_node_s(t, lk_str_c("col"), UIK_COLUMN);
  lbl = lk_tree_add_node_s(t, lk_str_c("lbl"), UIK_LABEL);
  lk_tree_add_prop(t, lbl, UIP_TEXT, lk_v_cstr(t->intern, "Hello"));
  lk_tree_set_root(t, w);
  lk_tree_append_child(t, w, col);
  lk_tree_append_child(t, col, lbl);

  r = run_layout(t, 800, 600);
  CHECK(r != NULL);
  if (r) {
    memset(&rl, 0, sizeof(rl));
    lk_render_build(t, r, &rl);
    /* FILL_RECT + CLIP_BEGIN + DRAW_TEXT + CLIP_END */
    CHECK_EQ(rl.count, 4u);
    CHECK_EQ((unsigned)rl.cmds[0].op, (unsigned)LK_ROP_FILL_RECT);
    CHECK_EQ((unsigned)rl.cmds[1].op, (unsigned)LK_ROP_CLIP_BEGIN);
    CHECK_EQ((unsigned)rl.cmds[2].op, (unsigned)LK_ROP_DRAW_TEXT);
    CHECK_EQ((unsigned)rl.cmds[3].op, (unsigned)LK_ROP_CLIP_END);
    /* verify str_id resolves to "Hello" */
    resolved = lk_intern_str(t->intern, rl.cmds[2].str_id);
    CHECK(lk_str_cmp(resolved, lk_str_c("Hello")));
    lk_render_list_destroy(&rl);
    free(r);
  }

  END_TEST();
  lk_tree_destroy(t);
}

static void test_render_button_with_padding(void) {
  lk_tree *t = lk_tree_create(NULL);
  lk_ix w, col, btn;
  lk_rect *r;
  lk_render_list rl;

  BEGIN_TEST("render: button with padding -> FILL+CLIP+FILL+TEXT+CLIP_END");

  w = lk_tree_add_node_s(t, lk_str_c("w"), UIK_WINDOW);
  col = lk_tree_add_node_s(t, lk_str_c("col"), UIK_COLUMN);
  btn = lk_tree_add_node_s(t, lk_str_c("btn"), UIK_BUTTON);
  lk_tree_add_prop(t, btn, UIP_TEXT, lk_v_cstr(t->intern, "OK"));
  lk_tree_add_prop(t, btn, UIP_PADDING, lk_v_i32(8));
  lk_tree_set_root(t, w);
  lk_tree_append_child(t, w, col);
  lk_tree_append_child(t, col, btn);

  r = run_layout(t, 800, 600);
  CHECK(r != NULL);
  if (r) {
    memset(&rl, 0, sizeof(rl));
    lk_render_build(t, r, &rl);
    /* window FILL + CLIP_BEGIN + button FILL + button TEXT + CLIP_END */
    CHECK_EQ(rl.count, 5u);
    CHECK_EQ((unsigned)rl.cmds[0].op, (unsigned)LK_ROP_FILL_RECT);
    CHECK_EQ((unsigned)rl.cmds[1].op, (unsigned)LK_ROP_CLIP_BEGIN);
    CHECK_EQ((unsigned)rl.cmds[2].op, (unsigned)LK_ROP_FILL_RECT);
    CHECK_EQ((unsigned)rl.cmds[3].op, (unsigned)LK_ROP_DRAW_TEXT);
    CHECK_EQ((unsigned)rl.cmds[4].op, (unsigned)LK_ROP_CLIP_END);
    /* text rect should be inset by padding from button rect */
    CHECK_EQ((unsigned)rl.cmds[3].rect.x, (unsigned)(rl.cmds[2].rect.x + 8));
    CHECK_EQ((unsigned)rl.cmds[3].rect.y, (unsigned)(rl.cmds[2].rect.y + 8));
    CHECK_EQ((unsigned)rl.cmds[3].rect.w, (unsigned)(rl.cmds[2].rect.w - 16));
    CHECK_EQ((unsigned)rl.cmds[3].rect.h, (unsigned)(rl.cmds[2].rect.h - 16));
    lk_render_list_destroy(&rl);
    free(r);
  }

  END_TEST();
  lk_tree_destroy(t);
}

static void test_render_larger_tree(void) {
  lk_tree *t = lk_tree_create(NULL);
  lk_ix w, col, l1, l2, btn;
  lk_rect *r;
  lk_render_list rl;

  BEGIN_TEST("render: larger tree -> correct command count");

  w = lk_tree_add_node_s(t, lk_str_c("w"), UIK_WINDOW);
  col = lk_tree_add_node_s(t, lk_str_c("col"), UIK_COLUMN);
  l1 = lk_tree_add_node_s(t, lk_str_c("l1"), UIK_LABEL);
  lk_tree_add_prop(t, l1, UIP_TEXT, lk_v_cstr(t->intern, "Title"));
  l2 = lk_tree_add_node_s(t, lk_str_c("l2"), UIK_LABEL);
  lk_tree_add_prop(t, l2, UIP_TEXT, lk_v_cstr(t->intern, "Subtitle"));
  btn = lk_tree_add_node_s(t, lk_str_c("btn"), UIK_BUTTON);
  lk_tree_add_prop(t, btn, UIP_TEXT, lk_v_cstr(t->intern, "Go"));
  lk_tree_add_prop(t, btn, UIP_PADDING, lk_v_i32(4));
  lk_tree_set_root(t, w);
  lk_tree_append_child(t, w, col);
  lk_tree_append_child(t, col, l1);
  lk_tree_append_child(t, col, l2);
  lk_tree_append_child(t, col, btn);

  r = run_layout(t, 800, 600);
  CHECK(r != NULL);
  if (r) {
    memset(&rl, 0, sizeof(rl));
    lk_render_build(t, r, &rl);
    /* 1 window FILL + CLIP_BEGIN + 2 label TEXT + 1 btn FILL + 1 btn TEXT + CLIP_END = 7 */
    CHECK_EQ(rl.count, 7u);
    lk_render_list_destroy(&rl);
    free(r);
  }

  END_TEST();
  lk_tree_destroy(t);
}

static void test_render_build_reuse(void) {
  lk_tree *t = lk_tree_create(NULL);
  lk_ix w, col, lbl;
  lk_rect *r;
  lk_render_list rl;
  lk_u32 cap_after_first;

  BEGIN_TEST("render: build twice reuses capacity");

  w = lk_tree_add_node_s(t, lk_str_c("w"), UIK_WINDOW);
  col = lk_tree_add_node_s(t, lk_str_c("col"), UIK_COLUMN);
  lbl = lk_tree_add_node_s(t, lk_str_c("lbl"), UIK_LABEL);
  lk_tree_add_prop(t, lbl, UIP_TEXT, lk_v_cstr(t->intern, "Test"));
  lk_tree_set_root(t, w);
  lk_tree_append_child(t, w, col);
  lk_tree_append_child(t, col, lbl);

  r = run_layout(t, 800, 600);
  CHECK(r != NULL);
  if (r) {
    memset(&rl, 0, sizeof(rl));

    /* first build: FILL + CLIP_BEGIN + TEXT + CLIP_END = 4 */
    lk_render_build(t, r, &rl);
    CHECK_EQ(rl.count, 4u);
    cap_after_first = rl.cap;
    CHECK(cap_after_first > 0);

    /* second build on same list — count resets, cap stays */
    lk_render_build(t, r, &rl);
    CHECK_EQ(rl.count, 4u);
    CHECK_EQ(rl.cap, cap_after_first);

    lk_render_list_destroy(&rl);
    free(r);
  }

  END_TEST();
  lk_tree_destroy(t);
}

/* ================================================================
 * Tests: hit testing
 * ================================================================ */

static void test_hit_single_button(void) {
  lk_tree *t = lk_tree_create(NULL);
  lk_ix w, col, btn;
  lk_rect *r;

  BEGIN_TEST("hit: point inside single button");

  w = lk_tree_add_node_s(t, lk_str_c("w"), UIK_WINDOW);
  col = lk_tree_add_node_s(t, lk_str_c("col"), UIK_COLUMN);
  btn = lk_tree_add_node_s(t, lk_str_c("btn"), UIK_BUTTON);
  lk_tree_add_prop(t, btn, UIP_TEXT, lk_v_cstr(t->intern, "OK"));
  lk_tree_add_prop(t, btn, UIP_PADDING, lk_v_i32(8));
  lk_tree_set_root(t, w);
  lk_tree_append_child(t, w, col);
  lk_tree_append_child(t, col, btn);

  r = run_layout(t, 800, 600);
  CHECK(r != NULL);
  if (r) {
    lk_ix hit = lk_hit_test(t, r, 10, 10);
    CHECK_EQ(hit, btn);
    free(r);
  }

  END_TEST();
  lk_tree_destroy(t);
}

static void test_hit_outside(void) {
  lk_tree *t = lk_tree_create(NULL);
  lk_ix w, col, btn;
  lk_rect *r;

  BEGIN_TEST("hit: point outside all nodes");

  w = lk_tree_add_node_s(t, lk_str_c("w"), UIK_WINDOW);
  col = lk_tree_add_node_s(t, lk_str_c("col"), UIK_COLUMN);
  btn = lk_tree_add_node_s(t, lk_str_c("btn"), UIK_BUTTON);
  lk_tree_add_prop(t, btn, UIP_TEXT, lk_v_cstr(t->intern, "OK"));
  lk_tree_set_root(t, w);
  lk_tree_append_child(t, w, col);
  lk_tree_append_child(t, col, btn);

  r = run_layout(t, 800, 600);
  CHECK(r != NULL);
  if (r) {
    lk_ix hit = lk_hit_test(t, r, 900, 700);
    CHECK_EQ(hit, 0u);
    free(r);
  }

  END_TEST();
  lk_tree_destroy(t);
}

static void test_hit_parent_padding(void) {
  /* Hit point in column padding area (not child) returns column */
  lk_tree *t = lk_tree_create(NULL);
  lk_ix w, col, btn;
  lk_rect *r;

  BEGIN_TEST("hit: parent padding area returns parent");

  w = lk_tree_add_node_s(t, lk_str_c("w"), UIK_WINDOW);
  col = lk_tree_add_node_s(t, lk_str_c("col"), UIK_COLUMN);
  lk_tree_add_prop(t, col, UIP_PADDING, lk_v_i32(50));
  btn = lk_tree_add_node_s(t, lk_str_c("btn"), UIK_BUTTON);
  lk_tree_add_prop(t, btn, UIP_TEXT, lk_v_cstr(t->intern, "X"));
  lk_tree_set_root(t, w);
  lk_tree_append_child(t, w, col);
  lk_tree_append_child(t, col, btn);

  r = run_layout(t, 800, 600);
  CHECK(r != NULL);
  if (r) {
    /* (5, 5) is inside window and column but outside button */
    lk_ix hit = lk_hit_test(t, r, 5, 5);
    CHECK_EQ(hit, col);
    free(r);
  }

  END_TEST();
  lk_tree_destroy(t);
}

static void test_hit_deepest_child(void) {
  /* Nested: window > column > row > button; click inside returns button */
  lk_tree *t = lk_tree_create(NULL);
  lk_ix w, col, row, btn;
  lk_rect *r;

  BEGIN_TEST("hit: deepest child wins");

  w = lk_tree_add_node_s(t, lk_str_c("w"), UIK_WINDOW);
  col = lk_tree_add_node_s(t, lk_str_c("col"), UIK_COLUMN);
  row = lk_tree_add_node_s(t, lk_str_c("row"), UIK_ROW);
  btn = lk_tree_add_node_s(t, lk_str_c("btn"), UIK_BUTTON);
  lk_tree_add_prop(t, btn, UIP_TEXT, lk_v_cstr(t->intern, "Go"));
  lk_tree_set_root(t, w);
  lk_tree_append_child(t, w, col);
  lk_tree_append_child(t, col, row);
  lk_tree_append_child(t, row, btn);

  r = run_layout(t, 800, 600);
  CHECK(r != NULL);
  if (r) {
    lk_ix hit = lk_hit_test(t, r, 5, 5);
    CHECK_EQ(hit, btn);
    free(r);
  }

  END_TEST();
  lk_tree_destroy(t);
}

static void test_hit_later_sibling(void) {
  /* Two siblings in a row; click in second returns second */
  lk_tree *t = lk_tree_create(NULL);
  lk_ix w, row, b1, b2;
  lk_rect *r;

  BEGIN_TEST("hit: later sibling wins overlap");

  w = lk_tree_add_node_s(t, lk_str_c("w"), UIK_WINDOW);
  row = lk_tree_add_node_s(t, lk_str_c("row"), UIK_ROW);
  b1 = lk_tree_add_node_s(t, lk_str_c("b1"), UIK_BUTTON);
  lk_tree_add_prop(t, b1, UIP_TEXT, lk_v_cstr(t->intern, "A"));
  lk_tree_add_prop(t, b1, UIP_W, lk_v_i32(100));
  b2 = lk_tree_add_node_s(t, lk_str_c("b2"), UIK_BUTTON);
  lk_tree_add_prop(t, b2, UIP_TEXT, lk_v_cstr(t->intern, "B"));
  lk_tree_add_prop(t, b2, UIP_W, lk_v_i32(100));
  lk_tree_set_root(t, w);
  lk_tree_append_child(t, w, row);
  lk_tree_append_child(t, row, b1);
  lk_tree_append_child(t, row, b2);

  r = run_layout(t, 800, 600);
  CHECK(r != NULL);
  if (r) {
    /* b2 starts at x=100 */
    lk_ix hit = lk_hit_test(t, r, 150, 300);
    CHECK_EQ(hit, b2);
    free(r);
  }

  END_TEST();
  lk_tree_destroy(t);
}

static void test_hit_empty_tree(void) {
  lk_tree *t = lk_tree_create(NULL);
  lk_rect rects[2];

  BEGIN_TEST("hit: empty tree returns 0");

  memset(rects, 0, sizeof(rects));
  CHECK_EQ(lk_hit_test(t, rects, 10, 10), 0u);

  END_TEST();
  lk_tree_destroy(t);
}

/* ================================================================
 * Tests: focus management
 * ================================================================ */

static void test_focus_set_focusable(void) {
  lk_ui *ui = lk_ui_create(NULL);
  lk_tree *t;
  const lk_tree *cur;
  lk_ix w, btn;
  lk_node_id btn_id;
  int ok;

  BEGIN_TEST("focus: set on focusable node succeeds");

  t = lk_ui_begin_frame(ui);
  w = lk_tree_add_node_s(t, lk_str_c("w"), UIK_WINDOW);
  btn = lk_tree_add_node_s(t, lk_str_c("btn"), UIK_BUTTON);
  lk_tree_add_prop(t, btn, UIP_FOCUSABLE, lk_v_bool(1));
  lk_tree_set_root(t, w);
  lk_tree_append_child(t, w, btn);
  btn_id = t->nodes[btn].id;
  lk_ui_end_frame(ui);

  cur = lk_ui_tree(ui);
  ok = lk_focus_set(ui, cur, btn_id);
  CHECK_EQ((unsigned)ok, 1u);
  CHECK(lk_focus_current(ui, cur) != 0);

  END_TEST();
  lk_ui_destroy(ui);
}

static void test_focus_set_not_focusable(void) {
  lk_ui *ui = lk_ui_create(NULL);
  lk_tree *t;
  const lk_tree *cur;
  lk_ix w, lbl;
  lk_node_id lbl_id;
  int ok;

  BEGIN_TEST("focus: set on non-focusable node fails");

  t = lk_ui_begin_frame(ui);
  w = lk_tree_add_node_s(t, lk_str_c("w"), UIK_WINDOW);
  lbl = lk_tree_add_node_s(t, lk_str_c("lbl"), UIK_LABEL);
  lk_tree_add_prop(t, lbl, UIP_TEXT, lk_v_cstr(t->intern, "Hi"));
  lk_tree_set_root(t, w);
  lk_tree_append_child(t, w, lbl);
  lbl_id = t->nodes[lbl].id;
  lk_ui_end_frame(ui);

  cur = lk_ui_tree(ui);
  ok = lk_focus_set(ui, cur, lbl_id);
  CHECK_EQ((unsigned)ok, 0u);
  CHECK_EQ(lk_focus_current(ui, cur), 0u);

  END_TEST();
  lk_ui_destroy(ui);
}

static void test_focus_set_disabled(void) {
  lk_ui *ui = lk_ui_create(NULL);
  lk_tree *t;
  const lk_tree *cur;
  lk_ix w, btn;
  lk_node_id btn_id;
  int ok;

  BEGIN_TEST("focus: set on disabled node fails");

  t = lk_ui_begin_frame(ui);
  w = lk_tree_add_node_s(t, lk_str_c("w"), UIK_WINDOW);
  btn = lk_tree_add_node_s(t, lk_str_c("btn"), UIK_BUTTON);
  lk_tree_add_prop(t, btn, UIP_FOCUSABLE, lk_v_bool(1));
  lk_tree_add_prop(t, btn, UIP_DISABLED, lk_v_bool(1));
  lk_tree_set_root(t, w);
  lk_tree_append_child(t, w, btn);
  btn_id = t->nodes[btn].id;
  lk_ui_end_frame(ui);

  cur = lk_ui_tree(ui);
  ok = lk_focus_set(ui, cur, btn_id);
  CHECK_EQ((unsigned)ok, 0u);

  END_TEST();
  lk_ui_destroy(ui);
}

static void test_focus_clear(void) {
  lk_ui *ui = lk_ui_create(NULL);
  lk_tree *t;
  const lk_tree *cur;
  lk_ix w, btn;
  lk_node_id btn_id;

  BEGIN_TEST("focus: clear sets focus to 0");

  t = lk_ui_begin_frame(ui);
  w = lk_tree_add_node_s(t, lk_str_c("w"), UIK_WINDOW);
  btn = lk_tree_add_node_s(t, lk_str_c("btn"), UIK_BUTTON);
  lk_tree_add_prop(t, btn, UIP_FOCUSABLE, lk_v_bool(1));
  lk_tree_set_root(t, w);
  lk_tree_append_child(t, w, btn);
  btn_id = t->nodes[btn].id;
  lk_ui_end_frame(ui);

  cur = lk_ui_tree(ui);
  lk_focus_set(ui, cur, btn_id);
  CHECK(lk_focus_current(ui, cur) != 0);

  lk_focus_clear(ui);
  CHECK_EQ(lk_focus_current(ui, cur), 0u);

  END_TEST();
  lk_ui_destroy(ui);
}

static void test_focus_next_wraps(void) {
  lk_ui *ui = lk_ui_create(NULL);
  lk_tree *t;
  const lk_tree *cur;
  lk_ix w, col, b1, b2, b3;
  lk_node_id id1, id2, id3, r;

  BEGIN_TEST("focus: next wraps around 3 buttons");

  t = lk_ui_begin_frame(ui);
  w = lk_tree_add_node_s(t, lk_str_c("w"), UIK_WINDOW);
  col = lk_tree_add_node_s(t, lk_str_c("col"), UIK_COLUMN);
  b1 = lk_tree_add_node_s(t, lk_str_c("b1"), UIK_BUTTON);
  lk_tree_add_prop(t, b1, UIP_FOCUSABLE, lk_v_bool(1));
  b2 = lk_tree_add_node_s(t, lk_str_c("b2"), UIK_BUTTON);
  lk_tree_add_prop(t, b2, UIP_FOCUSABLE, lk_v_bool(1));
  b3 = lk_tree_add_node_s(t, lk_str_c("b3"), UIK_BUTTON);
  lk_tree_add_prop(t, b3, UIP_FOCUSABLE, lk_v_bool(1));
  lk_tree_set_root(t, w);
  lk_tree_append_child(t, w, col);
  lk_tree_append_child(t, col, b1);
  lk_tree_append_child(t, col, b2);
  lk_tree_append_child(t, col, b3);
  id1 = t->nodes[b1].id;
  id2 = t->nodes[b2].id;
  id3 = t->nodes[b3].id;
  lk_ui_end_frame(ui);

  cur = lk_ui_tree(ui);

  /* No focus -> first */
  r = lk_focus_next(ui, cur);
  CHECK_EQ(r, id1);

  /* first -> second */
  r = lk_focus_next(ui, cur);
  CHECK_EQ(r, id2);

  /* second -> third */
  r = lk_focus_next(ui, cur);
  CHECK_EQ(r, id3);

  /* third -> wraps to first */
  r = lk_focus_next(ui, cur);
  CHECK_EQ(r, id1);

  END_TEST();
  lk_ui_destroy(ui);
}

static void test_focus_prev_wraps(void) {
  lk_ui *ui = lk_ui_create(NULL);
  lk_tree *t;
  const lk_tree *cur;
  lk_ix w, col, b1, b2, b3;
  lk_node_id id1, id2, id3, r;

  BEGIN_TEST("focus: prev wraps around");

  t = lk_ui_begin_frame(ui);
  w = lk_tree_add_node_s(t, lk_str_c("w"), UIK_WINDOW);
  col = lk_tree_add_node_s(t, lk_str_c("col"), UIK_COLUMN);
  b1 = lk_tree_add_node_s(t, lk_str_c("b1"), UIK_BUTTON);
  lk_tree_add_prop(t, b1, UIP_FOCUSABLE, lk_v_bool(1));
  b2 = lk_tree_add_node_s(t, lk_str_c("b2"), UIK_BUTTON);
  lk_tree_add_prop(t, b2, UIP_FOCUSABLE, lk_v_bool(1));
  b3 = lk_tree_add_node_s(t, lk_str_c("b3"), UIK_BUTTON);
  lk_tree_add_prop(t, b3, UIP_FOCUSABLE, lk_v_bool(1));
  lk_tree_set_root(t, w);
  lk_tree_append_child(t, w, col);
  lk_tree_append_child(t, col, b1);
  lk_tree_append_child(t, col, b2);
  lk_tree_append_child(t, col, b3);
  id1 = t->nodes[b1].id;
  id2 = t->nodes[b2].id;
  id3 = t->nodes[b3].id;
  lk_ui_end_frame(ui);

  cur = lk_ui_tree(ui);

  /* No focus -> last */
  r = lk_focus_prev(ui, cur);
  CHECK_EQ(r, id3);

  /* last -> second */
  r = lk_focus_prev(ui, cur);
  CHECK_EQ(r, id2);

  /* second -> first */
  r = lk_focus_prev(ui, cur);
  CHECK_EQ(r, id1);

  /* first -> wraps to last */
  r = lk_focus_prev(ui, cur);
  CHECK_EQ(r, id3);

  END_TEST();
  lk_ui_destroy(ui);
}

static void test_focus_next_skips_disabled(void) {
  lk_ui *ui = lk_ui_create(NULL);
  lk_tree *t;
  const lk_tree *cur;
  lk_ix w, col, b1, b2, b3;
  lk_node_id id1, id3, r;

  BEGIN_TEST("focus: next skips disabled");

  t = lk_ui_begin_frame(ui);
  w = lk_tree_add_node_s(t, lk_str_c("w"), UIK_WINDOW);
  col = lk_tree_add_node_s(t, lk_str_c("col"), UIK_COLUMN);
  b1 = lk_tree_add_node_s(t, lk_str_c("b1"), UIK_BUTTON);
  lk_tree_add_prop(t, b1, UIP_FOCUSABLE, lk_v_bool(1));
  b2 = lk_tree_add_node_s(t, lk_str_c("b2"), UIK_BUTTON);
  lk_tree_add_prop(t, b2, UIP_FOCUSABLE, lk_v_bool(1));
  lk_tree_add_prop(t, b2, UIP_DISABLED, lk_v_bool(1));
  b3 = lk_tree_add_node_s(t, lk_str_c("b3"), UIK_BUTTON);
  lk_tree_add_prop(t, b3, UIP_FOCUSABLE, lk_v_bool(1));
  lk_tree_set_root(t, w);
  lk_tree_append_child(t, w, col);
  lk_tree_append_child(t, col, b1);
  lk_tree_append_child(t, col, b2);
  lk_tree_append_child(t, col, b3);
  id1 = t->nodes[b1].id;
  id3 = t->nodes[b3].id;
  lk_ui_end_frame(ui);

  cur = lk_ui_tree(ui);

  r = lk_focus_next(ui, cur);
  CHECK_EQ(r, id1);
  /* from b1, next should skip disabled b2, go to b3 */
  r = lk_focus_next(ui, cur);
  CHECK_EQ(r, id3);

  END_TEST();
  lk_ui_destroy(ui);
}

static void test_focus_removed_clears(void) {
  lk_ui *ui = lk_ui_create(NULL);
  lk_tree *t;
  const lk_tree *cur;
  lk_ix w, col, b1;
  lk_node_id b1_id;

  BEGIN_TEST("focus: removed node clears focus");

  t = lk_ui_begin_frame(ui);
  w = lk_tree_add_node_s(t, lk_str_c("w"), UIK_WINDOW);
  col = lk_tree_add_node_s(t, lk_str_c("col"), UIK_COLUMN);
  b1 = lk_tree_add_node_s(t, lk_str_c("b1"), UIK_BUTTON);
  lk_tree_add_prop(t, b1, UIP_FOCUSABLE, lk_v_bool(1));
  lk_tree_set_root(t, w);
  lk_tree_append_child(t, w, col);
  lk_tree_append_child(t, col, b1);
  b1_id = t->nodes[b1].id;
  lk_ui_end_frame(ui);

  cur = lk_ui_tree(ui);
  lk_focus_set(ui, cur, b1_id);
  CHECK(lk_focus_current(ui, cur) != 0);

  /* Next frame: remove b1 */
  t = lk_ui_begin_frame(ui);
  w = lk_tree_add_node_s(t, lk_str_c("w"), UIK_WINDOW);
  col = lk_tree_add_node_s(t, lk_str_c("col"), UIK_COLUMN);
  lk_tree_set_root(t, w);
  lk_tree_append_child(t, w, col);
  lk_ui_end_frame(ui);

  cur = lk_ui_tree(ui);
  CHECK_EQ(lk_focus_current(ui, cur), 0u);

  END_TEST();
  lk_ui_destroy(ui);
}

/* ================================================================
 * Tests: event routing
 * ================================================================ */

#define ROUTE_LOG_CAP 32

typedef struct route_log_entry {
  lk_ix node_ix;
  lk_u8 phase;
} route_log_entry;

typedef struct route_log {
  route_log_entry entries[ROUTE_LOG_CAP];
  int count;
} route_log;

static int route_log_handler(lk_event *event, lk_ix node_ix, void *ud) {
  route_log *log = (route_log *)ud;
  if (log->count < ROUTE_LOG_CAP) {
    log->entries[log->count].node_ix = node_ix;
    log->entries[log->count].phase = event->phase;
    log->count++;
  }
  return 0;
}

static int route_stop_capture_handler(lk_event *event, lk_ix node_ix,
                                       void *ud) {
  route_log *log = (route_log *)ud;
  if (log->count < ROUTE_LOG_CAP) {
    log->entries[log->count].node_ix = node_ix;
    log->entries[log->count].phase = event->phase;
    log->count++;
  }
  if (event->phase == LK_PHASE_CAPTURE) {
    event->handled = 1;
  }
  return 0;
}

static int route_stop_target_handler(lk_event *event, lk_ix node_ix,
                                      void *ud) {
  route_log *log = (route_log *)ud;
  if (log->count < ROUTE_LOG_CAP) {
    log->entries[log->count].node_ix = node_ix;
    log->entries[log->count].phase = event->phase;
    log->count++;
  }
  if (event->phase == LK_PHASE_TARGET) {
    event->handled = 1;
  }
  return 0;
}

static int route_stop_bubble_handler(lk_event *event, lk_ix node_ix,
                                      void *ud) {
  route_log *log = (route_log *)ud;
  if (log->count < ROUTE_LOG_CAP) {
    log->entries[log->count].node_ix = node_ix;
    log->entries[log->count].phase = event->phase;
    log->count++;
  }
  if (event->phase == LK_PHASE_BUBBLE) {
    event->handled = 1;
  }
  return 0;
}

static void test_route_full_traversal(void) {
  /* window > column > button; target = button
   * expect: capture(window), capture(column), target(button),
   *         bubble(column), bubble(window)
   */
  lk_ui *ui = lk_ui_create(NULL);
  lk_tree *t;
  const lk_tree *cur;
  lk_ix w, col, btn;
  lk_event ev;
  route_log log;

  BEGIN_TEST("route: capture + target + bubble full path");

  t = lk_ui_begin_frame(ui);
  w = lk_tree_add_node_s(t, lk_str_c("w"), UIK_WINDOW);
  col = lk_tree_add_node_s(t, lk_str_c("col"), UIK_COLUMN);
  btn = lk_tree_add_node_s(t, lk_str_c("btn"), UIK_BUTTON);
  lk_tree_set_root(t, w);
  lk_tree_append_child(t, w, col);
  lk_tree_append_child(t, col, btn);
  lk_ui_end_frame(ui);

  cur = lk_ui_tree(ui);
  /* btn index is the same after end_frame swap */
  btn = lk_tree_find_by_id(cur, lk_intern_id(ui->intern, lk_str_c("btn")));
  w = lk_tree_find_by_id(cur, lk_intern_id(ui->intern, lk_str_c("w")));
  col = lk_tree_find_by_id(cur, lk_intern_id(ui->intern, lk_str_c("col")));

  memset(&log, 0, sizeof(log));
  lk_ui_set_event_handler(ui, route_log_handler, &log);

  memset(&ev, 0, sizeof(ev));
  ev.type = LK_EVENT_POINTER_DOWN;
  ev.target = btn;

  lk_event_route(ui, &ev);

  CHECK_EQ(log.count, 5);
  CHECK_EQ(log.entries[0].node_ix, w);
  CHECK_EQ((unsigned)log.entries[0].phase, (unsigned)LK_PHASE_CAPTURE);
  CHECK_EQ(log.entries[1].node_ix, col);
  CHECK_EQ((unsigned)log.entries[1].phase, (unsigned)LK_PHASE_CAPTURE);
  CHECK_EQ(log.entries[2].node_ix, btn);
  CHECK_EQ((unsigned)log.entries[2].phase, (unsigned)LK_PHASE_TARGET);
  CHECK_EQ(log.entries[3].node_ix, col);
  CHECK_EQ((unsigned)log.entries[3].phase, (unsigned)LK_PHASE_BUBBLE);
  CHECK_EQ(log.entries[4].node_ix, w);
  CHECK_EQ((unsigned)log.entries[4].phase, (unsigned)LK_PHASE_BUBBLE);

  END_TEST();
  lk_ui_destroy(ui);
}

static void test_route_stop_capture(void) {
  lk_ui *ui = lk_ui_create(NULL);
  lk_tree *t;
  const lk_tree *cur;
  lk_ix w, col, btn;
  lk_event ev;
  route_log log;

  BEGIN_TEST("route: handled in capture stops before target");

  t = lk_ui_begin_frame(ui);
  w = lk_tree_add_node_s(t, lk_str_c("w"), UIK_WINDOW);
  col = lk_tree_add_node_s(t, lk_str_c("col"), UIK_COLUMN);
  btn = lk_tree_add_node_s(t, lk_str_c("btn"), UIK_BUTTON);
  lk_tree_set_root(t, w);
  lk_tree_append_child(t, w, col);
  lk_tree_append_child(t, col, btn);
  lk_ui_end_frame(ui);

  cur = lk_ui_tree(ui);
  btn = lk_tree_find_by_id(cur, lk_intern_id(ui->intern, lk_str_c("btn")));

  memset(&log, 0, sizeof(log));
  lk_ui_set_event_handler(ui, route_stop_capture_handler, &log);

  memset(&ev, 0, sizeof(ev));
  ev.type = LK_EVENT_POINTER_DOWN;
  ev.target = btn;

  lk_event_route(ui, &ev);

  /* Should stop after first capture node (window) */
  CHECK_EQ(log.count, 1);
  CHECK_EQ((unsigned)log.entries[0].phase, (unsigned)LK_PHASE_CAPTURE);

  END_TEST();
  lk_ui_destroy(ui);
}

static void test_route_stop_target(void) {
  lk_ui *ui = lk_ui_create(NULL);
  lk_tree *t;
  const lk_tree *cur;
  lk_ix w, col, btn;
  lk_event ev;
  route_log log;

  BEGIN_TEST("route: handled at target stops bubble");

  t = lk_ui_begin_frame(ui);
  w = lk_tree_add_node_s(t, lk_str_c("w"), UIK_WINDOW);
  col = lk_tree_add_node_s(t, lk_str_c("col"), UIK_COLUMN);
  btn = lk_tree_add_node_s(t, lk_str_c("btn"), UIK_BUTTON);
  lk_tree_set_root(t, w);
  lk_tree_append_child(t, w, col);
  lk_tree_append_child(t, col, btn);
  lk_ui_end_frame(ui);

  cur = lk_ui_tree(ui);
  btn = lk_tree_find_by_id(cur, lk_intern_id(ui->intern, lk_str_c("btn")));

  memset(&log, 0, sizeof(log));
  lk_ui_set_event_handler(ui, route_stop_target_handler, &log);

  memset(&ev, 0, sizeof(ev));
  ev.type = LK_EVENT_POINTER_DOWN;
  ev.target = btn;

  lk_event_route(ui, &ev);

  /* 2 capture + 1 target = 3 entries, no bubble */
  CHECK_EQ(log.count, 3);
  CHECK_EQ((unsigned)log.entries[2].phase, (unsigned)LK_PHASE_TARGET);

  END_TEST();
  lk_ui_destroy(ui);
}

static void test_route_stop_bubble(void) {
  lk_ui *ui = lk_ui_create(NULL);
  lk_tree *t;
  const lk_tree *cur;
  lk_ix w, col, btn;
  lk_event ev;
  route_log log;

  BEGIN_TEST("route: handled mid-bubble stops further");

  t = lk_ui_begin_frame(ui);
  w = lk_tree_add_node_s(t, lk_str_c("w"), UIK_WINDOW);
  col = lk_tree_add_node_s(t, lk_str_c("col"), UIK_COLUMN);
  btn = lk_tree_add_node_s(t, lk_str_c("btn"), UIK_BUTTON);
  lk_tree_set_root(t, w);
  lk_tree_append_child(t, w, col);
  lk_tree_append_child(t, col, btn);
  lk_ui_end_frame(ui);

  cur = lk_ui_tree(ui);
  btn = lk_tree_find_by_id(cur, lk_intern_id(ui->intern, lk_str_c("btn")));

  memset(&log, 0, sizeof(log));
  lk_ui_set_event_handler(ui, route_stop_bubble_handler, &log);

  memset(&ev, 0, sizeof(ev));
  ev.type = LK_EVENT_POINTER_DOWN;
  ev.target = btn;

  lk_event_route(ui, &ev);

  /* 2 capture + 1 target + 1 bubble (col) = 4, stops before window bubble */
  CHECK_EQ(log.count, 4);
  CHECK_EQ((unsigned)log.entries[3].phase, (unsigned)LK_PHASE_BUBBLE);

  END_TEST();
  lk_ui_destroy(ui);
}

static void test_route_no_handler(void) {
  lk_ui *ui = lk_ui_create(NULL);
  lk_tree *t;
  lk_ix w;
  lk_event ev;

  BEGIN_TEST("route: no handler installed, no crash");

  t = lk_ui_begin_frame(ui);
  w = lk_tree_add_node_s(t, lk_str_c("w"), UIK_WINDOW);
  lk_tree_set_root(t, w);
  lk_ui_end_frame(ui);

  memset(&ev, 0, sizeof(ev));
  ev.type = LK_EVENT_POINTER_DOWN;
  ev.target = w;

  lk_event_route(ui, &ev);
  CHECK(1); /* just verify no crash */

  END_TEST();
  lk_ui_destroy(ui);
}

static void test_route_target_is_root(void) {
  lk_ui *ui = lk_ui_create(NULL);
  lk_tree *t;
  const lk_tree *cur;
  lk_ix w;
  lk_event ev;
  route_log log;

  BEGIN_TEST("route: target is root, no capture/bubble");

  t = lk_ui_begin_frame(ui);
  w = lk_tree_add_node_s(t, lk_str_c("w"), UIK_WINDOW);
  lk_tree_set_root(t, w);
  lk_ui_end_frame(ui);

  cur = lk_ui_tree(ui);
  w = cur->root;

  memset(&log, 0, sizeof(log));
  lk_ui_set_event_handler(ui, route_log_handler, &log);

  memset(&ev, 0, sizeof(ev));
  ev.type = LK_EVENT_POINTER_DOWN;
  ev.target = w;

  lk_event_route(ui, &ev);

  /* Only target phase for root */
  CHECK_EQ(log.count, 1);
  CHECK_EQ(log.entries[0].node_ix, w);
  CHECK_EQ((unsigned)log.entries[0].phase, (unsigned)LK_PHASE_TARGET);

  END_TEST();
  lk_ui_destroy(ui);
}

/* ================================================================
 * Tests: integrated event tests
 * ================================================================ */

static void test_hit_route_integrated(void) {
  lk_ui *ui = lk_ui_create(NULL);
  lk_tree *t;
  const lk_tree *cur;
  lk_rect *r;
  lk_ix w, col, btn, hit;
  lk_event ev;
  route_log log;

  BEGIN_TEST("integrated: hit-test + route fires handler");

  t = lk_ui_begin_frame(ui);
  w = lk_tree_add_node_s(t, lk_str_c("w"), UIK_WINDOW);
  col = lk_tree_add_node_s(t, lk_str_c("col"), UIK_COLUMN);
  btn = lk_tree_add_node_s(t, lk_str_c("btn"), UIK_BUTTON);
  lk_tree_add_prop(t, btn, UIP_TEXT, lk_v_cstr(t->intern, "Click"));
  lk_tree_add_prop(t, btn, UIP_PADDING, lk_v_i32(8));
  lk_tree_set_root(t, w);
  lk_tree_append_child(t, w, col);
  lk_tree_append_child(t, col, btn);
  lk_ui_end_frame(ui);

  cur = lk_ui_tree(ui);
  r = run_layout((lk_tree *)cur, 800, 600);
  CHECK(r != NULL);
  if (r) {
    hit = lk_hit_test(cur, r, 10, 10);
    CHECK(hit != 0);

    memset(&log, 0, sizeof(log));
    lk_ui_set_event_handler(ui, route_log_handler, &log);

    memset(&ev, 0, sizeof(ev));
    ev.type = LK_EVENT_POINTER_DOWN;
    ev.target = hit;

    lk_event_route(ui, &ev);
    CHECK(log.count > 0);

    free(r);
  }

  END_TEST();
  lk_ui_destroy(ui);
}

static void test_tab_cycles_focus(void) {
  lk_ui *ui = lk_ui_create(NULL);
  lk_tree *t;
  const lk_tree *cur;
  lk_ix w, col, b1, b2, b3;
  lk_node_id id1, id2, id3;
  lk_node_id r;

  BEGIN_TEST("integrated: tab cycles through 3 focusable buttons");

  t = lk_ui_begin_frame(ui);
  w = lk_tree_add_node_s(t, lk_str_c("w"), UIK_WINDOW);
  col = lk_tree_add_node_s(t, lk_str_c("col"), UIK_COLUMN);
  b1 = lk_tree_add_node_s(t, lk_str_c("b1"), UIK_BUTTON);
  lk_tree_add_prop(t, b1, UIP_FOCUSABLE, lk_v_bool(1));
  b2 = lk_tree_add_node_s(t, lk_str_c("b2"), UIK_BUTTON);
  lk_tree_add_prop(t, b2, UIP_FOCUSABLE, lk_v_bool(1));
  b3 = lk_tree_add_node_s(t, lk_str_c("b3"), UIK_BUTTON);
  lk_tree_add_prop(t, b3, UIP_FOCUSABLE, lk_v_bool(1));
  lk_tree_set_root(t, w);
  lk_tree_append_child(t, w, col);
  lk_tree_append_child(t, col, b1);
  lk_tree_append_child(t, col, b2);
  lk_tree_append_child(t, col, b3);
  id1 = t->nodes[b1].id;
  id2 = t->nodes[b2].id;
  id3 = t->nodes[b3].id;
  lk_ui_end_frame(ui);

  cur = lk_ui_tree(ui);

  r = lk_focus_next(ui, cur);
  CHECK_EQ(r, id1);
  r = lk_focus_next(ui, cur);
  CHECK_EQ(r, id2);
  r = lk_focus_next(ui, cur);
  CHECK_EQ(r, id3);
  r = lk_focus_next(ui, cur);
  CHECK_EQ(r, id1);

  END_TEST();
  lk_ui_destroy(ui);
}

/* ================================================================
 * Tests: widget registry
 * ================================================================ */

static void test_widget_get_defaults(void) {
  const lk_widget_def *def;

  BEGIN_TEST("widget: get defaults returns valid defs");

  def = lk_widget_get(UIK_BUTTON);
  CHECK(def != NULL);
  CHECK(def->measure != NULL);
  CHECK(def->render != NULL);
  CHECK(def->layout == NULL); /* leaf */

  def = lk_widget_get(UIK_WINDOW);
  CHECK(def != NULL);
  CHECK(def->measure != NULL);
  CHECK(def->layout != NULL);
  CHECK(def->render != NULL);
  CHECK(def->clips == 1);

  def = lk_widget_get(UIK_COLUMN);
  CHECK(def != NULL);
  CHECK(def->layout != NULL);
  CHECK(def->render == NULL); /* invisible container */
  CHECK(def->clips == 0);

  END_TEST();
}

static void test_widget_register_custom(void) {
  lk_widget_def custom;
  const lk_widget_def *got;

  BEGIN_TEST("widget: register custom kind");

  memset(&custom, 0, sizeof(custom));
  custom.clips = 1;

  /* Use a kind slot beyond the built-ins */
  lk_widget_register((lk_kind)10, &custom);
  got = lk_widget_get((lk_kind)10);

  CHECK(got != NULL);
  CHECK(got->clips == 1);
  CHECK(got->measure == NULL);
  CHECK(got->render == NULL);

  /* Clean up: reset slot to zero */
  memset(&custom, 0, sizeof(custom));
  lk_widget_register((lk_kind)10, &custom);

  END_TEST();
}

static void test_widget_override_render(void) {
  /* Override LABEL's render to emit nothing; verify render output differs */
  lk_tree *t;
  lk_rect *r;
  lk_render_list rl;
  lk_widget_def override;
  const lk_widget_def *orig;
  lk_u32 count_before;
  lk_u32 count_after;
  lk_ix w, col, lbl;

  BEGIN_TEST("widget: override render changes output");

  /* Build a tree: window > column > label */
  t = lk_tree_create(NULL);
  w = lk_tree_add_node_s(t, lk_str_c("w"), UIK_WINDOW);
  col = lk_tree_add_node_s(t, lk_str_c("col"), UIK_COLUMN);
  lbl = lk_tree_add_node_s(t, lk_str_c("lbl"), UIK_LABEL);
  lk_tree_add_prop(t, lbl, UIP_TEXT, lk_v_cstr(t->intern, "Hello"));
  lk_tree_set_root(t, w);
  lk_tree_append_child(t, w, col);
  lk_tree_append_child(t, col, lbl);

  r = run_layout(t, 800, 600);
  CHECK(r != NULL);

  if (r) {
    memset(&rl, 0, sizeof(rl));
    lk_render_build(t, r, &rl);
    count_before = rl.count;

    /* Override LABEL to emit nothing */
    orig = lk_widget_get(UIK_LABEL);
    override = *orig;
    override.render = NULL;
    lk_widget_register(UIK_LABEL, &override);

    rl.count = 0;
    lk_render_build(t, r, &rl);
    count_after = rl.count;

    CHECK(count_after < count_before);

    /* Restore original */
    lk_widget_register(UIK_LABEL, orig);

    lk_render_list_destroy(&rl);
    free(r);
  }

  lk_tree_destroy(t);

  END_TEST();
}

/* ================================================================
 * Presentation tests
 * ================================================================ */

static void test_pres_add_and_get(void) {
  lk_tree *t = lk_tree_create(NULL);
  lk_ix w, btn;
  const lk_presentation *p;

  BEGIN_TEST("pres: add and get");

  w = lk_tree_add_node_s(t, lk_str_c("w"), UIK_WINDOW);
  btn = lk_tree_add_node_s(t, lk_str_c("btn"), UIK_BUTTON);
  lk_tree_set_root(t, w);
  lk_tree_append_child(t, w, btn);

  lk_tree_add_presentation_s(t, btn, "item", lk_v_i32(42));

  p = lk_tree_get_presentation(t, btn);
  CHECK(p != NULL);
  if (p) {
    CHECK_EQ(p->ptype, lk_intern_id(t->intern, lk_str_c("item")));
    CHECK_EQ(p->pvalue.tag, UIV_I32);
    CHECK_EQ(p->pvalue.as.i, 42);
    CHECK_EQ(p->node, btn);
  }

  END_TEST();
  lk_tree_destroy(t);
}

static void test_pres_none_returns_null(void) {
  lk_tree *t = lk_tree_create(NULL);
  lk_ix w;
  const lk_presentation *p;

  BEGIN_TEST("pres: no presentation returns NULL");

  w = lk_tree_add_node_s(t, lk_str_c("w"), UIK_WINDOW);
  lk_tree_set_root(t, w);

  p = lk_tree_get_presentation(t, w);
  CHECK(p == NULL);

  END_TEST();
  lk_tree_destroy(t);
}

static void test_pres_survives_frame(void) {
  lk_ui *ui = lk_ui_create(NULL);
  lk_tree *t;
  const lk_changeset *cs;

  BEGIN_TEST("pres: same pres across frames = no UPDATED");

  /* Frame 1: button with presentation */
  t = lk_ui_begin_frame(ui);
  {
    lk_ix w = lk_tree_add_node_s(t, lk_str_c("w"), UIK_WINDOW);
    lk_ix btn = lk_tree_add_node_s(t, lk_str_c("btn"), UIK_BUTTON);
    lk_tree_set_root(t, w);
    lk_tree_append_child(t, w, btn);
    lk_tree_add_presentation_s(t, btn, "item", lk_v_i32(5));
  }
  lk_ui_end_frame(ui);

  /* Frame 2: identical tree + presentation */
  t = lk_ui_begin_frame(ui);
  {
    lk_ix w = lk_tree_add_node_s(t, lk_str_c("w"), UIK_WINDOW);
    lk_ix btn = lk_tree_add_node_s(t, lk_str_c("btn"), UIK_BUTTON);
    lk_tree_set_root(t, w);
    lk_tree_append_child(t, w, btn);
    lk_tree_add_presentation_s(t, btn, "item", lk_v_i32(5));
  }
  cs = lk_ui_end_frame(ui);

  CHECK_EQ(cs->count, 0);

  END_TEST();
  lk_ui_destroy(ui);
}

static void test_pres_change_triggers_updated(void) {
  lk_ui *ui = lk_ui_create(NULL);
  lk_tree *t;
  const lk_changeset *cs;

  BEGIN_TEST("pres: changed pvalue triggers UPDATED");

  /* Frame 1 */
  t = lk_ui_begin_frame(ui);
  {
    lk_ix w = lk_tree_add_node_s(t, lk_str_c("w"), UIK_WINDOW);
    lk_ix btn = lk_tree_add_node_s(t, lk_str_c("btn"), UIK_BUTTON);
    lk_tree_set_root(t, w);
    lk_tree_append_child(t, w, btn);
    lk_tree_add_presentation_s(t, btn, "item", lk_v_i32(5));
  }
  lk_ui_end_frame(ui);

  /* Frame 2: different pvalue */
  t = lk_ui_begin_frame(ui);
  {
    lk_ix w = lk_tree_add_node_s(t, lk_str_c("w"), UIK_WINDOW);
    lk_ix btn = lk_tree_add_node_s(t, lk_str_c("btn"), UIK_BUTTON);
    lk_tree_set_root(t, w);
    lk_tree_append_child(t, w, btn);
    lk_tree_add_presentation_s(t, btn, "item", lk_v_i32(99));
  }
  cs = lk_ui_end_frame(ui);

  CHECK(cs_has(cs, ui, LK_CHANGE_UPDATED, "btn"));

  END_TEST();
  lk_ui_destroy(ui);
}

/* ================================================================
 * Command / Translator tests
 * ================================================================ */

static void test_translator_fires_command(void) {
  lk_ui *ui = lk_ui_create(NULL);
  lk_tree *t;
  const lk_tree *cur;
  lk_event ev;
  const lk_command_queue *q;

  BEGIN_TEST("translator: fires command on match");

  /* Register translator: POINTER_DOWN + ptype "item" -> "Select" */
  lk_ui_add_translator_s(ui, LK_EVENT_POINTER_DOWN, "item", 0, "Select");

  /* Build tree with presented button */
  t = lk_ui_begin_frame(ui);
  {
    lk_ix w = lk_tree_add_node_s(t, lk_str_c("w"), UIK_WINDOW);
    lk_ix btn = lk_tree_add_node_s(t, lk_str_c("btn"), UIK_BUTTON);
    lk_tree_set_root(t, w);
    lk_tree_append_child(t, w, btn);
    lk_tree_add_presentation_s(t, btn, "item", lk_v_i32(7));
  }
  lk_ui_end_frame(ui);

  cur = lk_ui_tree(ui);

  /* Route pointer_down to the button */
  memset(&ev, 0, sizeof(ev));
  ev.type = LK_EVENT_POINTER_DOWN;
  ev.target = lk_tree_find_by_id(cur, lk_intern_id(ui->intern, lk_str_c("btn")));

  lk_event_route(ui, &ev);

  q = lk_ui_commands(ui);
  CHECK(q != NULL);
  CHECK_EQ(q->count, 1);
  if (q->count >= 1) {
    CHECK_EQ(q->cmds[0].name,
             lk_intern_id(ui->intern, lk_str_c("Select")));
    CHECK_EQ(q->cmds[0].args[0].tag, UIV_I32);
    CHECK_EQ(q->cmds[0].args[0].as.i, 7);
    CHECK_EQ(q->cmds[0].source_ptype,
             lk_intern_id(ui->intern, lk_str_c("item")));
  }
  CHECK_EQ(ev.handled, 1);

  END_TEST();
  lk_ui_destroy(ui);
}

static void test_translator_no_match(void) {
  lk_ui *ui = lk_ui_create(NULL);
  lk_tree *t;
  lk_event ev;
  const lk_command_queue *q;

  BEGIN_TEST("translator: no match = empty queue");

  /* Translator for KEY_DOWN, but we'll send POINTER_DOWN */
  lk_ui_add_translator_s(ui, LK_EVENT_KEY_DOWN, "item", 0, "Select");

  t = lk_ui_begin_frame(ui);
  {
    lk_ix w = lk_tree_add_node_s(t, lk_str_c("w"), UIK_WINDOW);
    lk_ix btn = lk_tree_add_node_s(t, lk_str_c("btn"), UIK_BUTTON);
    lk_tree_set_root(t, w);
    lk_tree_append_child(t, w, btn);
    lk_tree_add_presentation_s(t, btn, "item", lk_v_i32(1));
  }
  lk_ui_end_frame(ui);

  memset(&ev, 0, sizeof(ev));
  ev.type = LK_EVENT_POINTER_DOWN;
  ev.target = lk_tree_find_by_id(lk_ui_tree(ui),
               lk_intern_id(ui->intern, lk_str_c("btn")));

  lk_event_route(ui, &ev);

  q = lk_ui_commands(ui);
  CHECK_EQ(q->count, 0);
  CHECK_EQ(ev.handled, 0);

  END_TEST();
  lk_ui_destroy(ui);
}

static void test_translator_walks_ancestors(void) {
  lk_ui *ui = lk_ui_create(NULL);
  lk_tree *t;
  lk_event ev;
  const lk_command_queue *q;
  lk_ix btn_ix;

  BEGIN_TEST("translator: walks ancestors for pres");

  lk_ui_add_translator_s(ui, LK_EVENT_POINTER_DOWN, "list", 0, "ListClick");

  /* Presentation is on parent column, event targets child button */
  t = lk_ui_begin_frame(ui);
  {
    lk_ix w = lk_tree_add_node_s(t, lk_str_c("w"), UIK_WINDOW);
    lk_ix col = lk_tree_add_node_s(t, lk_str_c("col"), UIK_COLUMN);
    lk_ix btn = lk_tree_add_node_s(t, lk_str_c("btn"), UIK_BUTTON);
    lk_tree_set_root(t, w);
    lk_tree_append_child(t, w, col);
    lk_tree_append_child(t, col, btn);
    lk_tree_add_presentation_s(t, col, "list", lk_v_i32(10));
  }
  lk_ui_end_frame(ui);

  btn_ix = lk_tree_find_by_id(lk_ui_tree(ui),
            lk_intern_id(ui->intern, lk_str_c("btn")));

  memset(&ev, 0, sizeof(ev));
  ev.type = LK_EVENT_POINTER_DOWN;
  ev.target = btn_ix;

  lk_event_route(ui, &ev);

  q = lk_ui_commands(ui);
  CHECK_EQ(q->count, 1);
  CHECK_EQ(ev.handled, 1);
  if (q->count >= 1) {
    CHECK_EQ(q->cmds[0].args[0].as.i, 10);
  }

  END_TEST();
  lk_ui_destroy(ui);
}

static void test_translator_ptype_and_kind(void) {
  lk_ui *ui = lk_ui_create(NULL);
  lk_tree *t;
  lk_event ev;
  const lk_command_queue *q;

  BEGIN_TEST("translator: match ptype + node_kind");

  /* Only match BUTTON nodes with ptype "action" */
  lk_ui_add_translator_s(ui, 0, "action", (lk_u16)UIK_BUTTON, "DoAction");

  t = lk_ui_begin_frame(ui);
  {
    lk_ix w = lk_tree_add_node_s(t, lk_str_c("w"), UIK_WINDOW);
    lk_ix btn = lk_tree_add_node_s(t, lk_str_c("btn"), UIK_BUTTON);
    lk_tree_set_root(t, w);
    lk_tree_append_child(t, w, btn);
    lk_tree_add_presentation_s(t, btn, "action", lk_v_i32(3));
  }
  lk_ui_end_frame(ui);

  memset(&ev, 0, sizeof(ev));
  ev.type = LK_EVENT_POINTER_DOWN;
  ev.target = lk_tree_find_by_id(lk_ui_tree(ui),
               lk_intern_id(ui->intern, lk_str_c("btn")));

  lk_event_route(ui, &ev);

  q = lk_ui_commands(ui);
  CHECK_EQ(q->count, 1);
  if (q->count >= 1) {
    CHECK_EQ(q->cmds[0].name,
             lk_intern_id(ui->intern, lk_str_c("DoAction")));
  }

  END_TEST();
  lk_ui_destroy(ui);
}

static int g_handler_called = 0;
static lk_u32 g_handler_cmd_name = 0;

static void test_cmd_handler_cb(const lk_command *cmd, void *ud) {
  (void)ud;
  g_handler_called = 1;
  g_handler_cmd_name = cmd->name;
}

static void test_command_handler_fires(void) {
  lk_ui *ui = lk_ui_create(NULL);
  lk_tree *t;
  lk_event ev;

  BEGIN_TEST("translator: command handler fires");

  g_handler_called = 0;
  g_handler_cmd_name = 0;

  lk_ui_set_command_handler(ui, test_cmd_handler_cb, NULL);
  lk_ui_add_translator_s(ui, LK_EVENT_POINTER_DOWN, "item", 0, "Pick");

  t = lk_ui_begin_frame(ui);
  {
    lk_ix w = lk_tree_add_node_s(t, lk_str_c("w"), UIK_WINDOW);
    lk_ix btn = lk_tree_add_node_s(t, lk_str_c("btn"), UIK_BUTTON);
    lk_tree_set_root(t, w);
    lk_tree_append_child(t, w, btn);
    lk_tree_add_presentation_s(t, btn, "item", lk_v_i32(1));
  }
  lk_ui_end_frame(ui);

  memset(&ev, 0, sizeof(ev));
  ev.type = LK_EVENT_POINTER_DOWN;
  ev.target = lk_tree_find_by_id(lk_ui_tree(ui),
               lk_intern_id(ui->intern, lk_str_c("btn")));

  lk_event_route(ui, &ev);

  CHECK_EQ(g_handler_called, 1);
  CHECK_EQ(g_handler_cmd_name,
           lk_intern_id(ui->intern, lk_str_c("Pick")));

  END_TEST();
  lk_ui_destroy(ui);
}

/* ================================================================
 * Introspection tests
 * ================================================================ */

static void test_command_log_accumulates(void) {
  lk_ui *ui = lk_ui_create(NULL);
  lk_tree *t;
  lk_event ev;
  const lk_command *log;
  lk_u32 log_count;

  BEGIN_TEST("introspect: command log accumulates");

  lk_ui_add_translator_s(ui, LK_EVENT_POINTER_DOWN, "item", 0, "Select");

  t = lk_ui_begin_frame(ui);
  {
    lk_ix w = lk_tree_add_node_s(t, lk_str_c("w"), UIK_WINDOW);
    lk_ix b1 = lk_tree_add_node_s(t, lk_str_c("b1"), UIK_BUTTON);
    lk_ix b2 = lk_tree_add_node_s(t, lk_str_c("b2"), UIK_BUTTON);
    lk_tree_set_root(t, w);
    lk_tree_append_child(t, w, b1);
    lk_tree_append_child(t, w, b2);
    lk_tree_add_presentation_s(t, b1, "item", lk_v_i32(1));
    lk_tree_add_presentation_s(t, b2, "item", lk_v_i32(2));
  }
  lk_ui_end_frame(ui);

  /* First click */
  memset(&ev, 0, sizeof(ev));
  ev.type = LK_EVENT_POINTER_DOWN;
  ev.target = lk_tree_find_by_id(lk_ui_tree(ui),
               lk_intern_id(ui->intern, lk_str_c("b1")));
  lk_event_route(ui, &ev);

  /* Clear queue between events (simulate frame boundary) */
  lk_ui_clear_commands(ui);

  /* Second click */
  memset(&ev, 0, sizeof(ev));
  ev.type = LK_EVENT_POINTER_DOWN;
  ev.target = lk_tree_find_by_id(lk_ui_tree(ui),
               lk_intern_id(ui->intern, lk_str_c("b2")));
  lk_event_route(ui, &ev);

  log = lk_ui_command_log(ui, &log_count);
  CHECK_EQ(log_count, 2);
  if (log_count >= 2) {
    CHECK_EQ(log[0].args[0].as.i, 1);
    CHECK_EQ(log[1].args[0].as.i, 2);
  }

  END_TEST();
  lk_ui_destroy(ui);
}

/* Simple write-to-buffer callback for dump tests */
typedef struct {
  char buf[512];
  lk_u32 len;
} test_buf;

static void test_buf_write(void *ud, const char *bytes, lk_u32 len) {
  test_buf *b = (test_buf *)ud;
  lk_u32 avail = (lk_u32)sizeof(b->buf) - b->len;

  if (len > avail) {
    len = avail;
  }

  if (len > 0) {
    memcpy(b->buf + b->len, bytes, len);
    b->len += len;
  }
}

static void test_dump_commands_output(void) {
  lk_ui *ui = lk_ui_create(NULL);
  test_buf buf;

  BEGIN_TEST("introspect: dump_commands output");

  lk_ui_add_translator_s(ui, LK_EVENT_POINTER_DOWN, "item", 0, "Select");
  lk_ui_add_translator_s(ui, LK_EVENT_KEY_DOWN, NULL, 0, "Activate");

  memset(&buf, 0, sizeof(buf));
  lk_ui_dump_commands(ui, test_buf_write, &buf);

  CHECK(buf.len > 0);
  /* Should contain "translators" and "Select" */
  CHECK(buf.len > 20);

  END_TEST();
  lk_ui_destroy(ui);
}

/* ================================================================
 * Accessor tests
 * ================================================================ */

static void test_accessor_node_fields(void) {
  lk_tree *t = lk_tree_create(NULL);
  lk_ix w, col, btn;
  lk_node_id w_id, col_id, btn_id;

  BEGIN_TEST("accessor: node id/kind/parent/child/sibling");

  w = lk_tree_add_node_s(t, lk_str_c("w"), UIK_WINDOW);
  col = lk_tree_add_node_s(t, lk_str_c("col"), UIK_COLUMN);
  btn = lk_tree_add_node_s(t, lk_str_c("btn"), UIK_BUTTON);
  lk_tree_set_root(t, w);
  lk_tree_append_child(t, w, col);
  lk_tree_append_child(t, col, btn);

  w_id = t->nodes[w].id;
  col_id = t->nodes[col].id;
  btn_id = t->nodes[btn].id;

  CHECK_EQ(lk_node_id_get(t, w), w_id);
  CHECK_EQ(lk_node_id_get(t, col), col_id);
  CHECK_EQ(lk_node_id_get(t, btn), btn_id);

  CHECK_EQ(lk_node_kind_get(t, w), (lk_u16)UIK_WINDOW);
  CHECK_EQ(lk_node_kind_get(t, col), (lk_u16)UIK_COLUMN);
  CHECK_EQ(lk_node_kind_get(t, btn), (lk_u16)UIK_BUTTON);

  CHECK_EQ(lk_node_parent(t, col), w);
  CHECK_EQ(lk_node_parent(t, btn), col);
  CHECK_EQ(lk_node_parent(t, w), 0u);

  CHECK_EQ(lk_node_first_child(t, w), col);
  CHECK_EQ(lk_node_first_child(t, col), btn);
  CHECK_EQ(lk_node_first_child(t, btn), 0u);

  CHECK_EQ(lk_node_next_sibling(t, col), 0u);
  CHECK_EQ(lk_node_next_sibling(t, btn), 0u);

  /* null/bounds safety */
  CHECK_EQ(lk_node_id_get(NULL, w), 0u);
  CHECK_EQ(lk_node_id_get(t, 0), 0u);
  CHECK_EQ(lk_node_id_get(t, 999), 0u);

  END_TEST();
  lk_tree_destroy(t);
}

static void test_accessor_tree_fields(void) {
  lk_tree *t = lk_tree_create(NULL);
  lk_ix w;

  BEGIN_TEST("accessor: tree node_count/root/intern");

  w = lk_tree_add_node_s(t, lk_str_c("w"), UIK_WINDOW);
  lk_tree_set_root(t, w);

  CHECK_EQ(lk_tree_node_count(t), t->node_count);
  CHECK_EQ(lk_tree_root(t), w);
  CHECK(lk_tree_intern(t) != NULL);
  CHECK(lk_tree_intern(t) == t->intern);

  /* null safety */
  CHECK_EQ(lk_tree_node_count(NULL), 0u);
  CHECK_EQ(lk_tree_root(NULL), 0u);
  CHECK(lk_tree_intern(NULL) == NULL);

  END_TEST();
  lk_tree_destroy(t);
}

static void test_accessor_changeset(void) {
  lk_ui *ui = lk_ui_create(NULL);
  lk_tree *t;
  const lk_changeset *cs;
  const lk_change *ch;

  BEGIN_TEST("accessor: changeset count/get");

  /* Frame 1: add a tree */
  t = lk_ui_begin_frame(ui);
  {
    lk_ix w = lk_tree_add_node_s(t, lk_str_c("w"), UIK_WINDOW);
    lk_ix btn = lk_tree_add_node_s(t, lk_str_c("btn"), UIK_BUTTON);
    lk_tree_set_root(t, w);
    lk_tree_append_child(t, w, btn);
  }
  cs = lk_ui_end_frame(ui);

  CHECK(lk_changeset_count(cs) >= 2);
  ch = lk_changeset_get(cs, 0);
  CHECK(ch != NULL);

  /* out-of-bounds returns NULL */
  CHECK(lk_changeset_get(cs, 999) == NULL);
  CHECK_EQ(lk_changeset_count(NULL), 0u);
  CHECK(lk_changeset_get(NULL, 0) == NULL);

  END_TEST();
  lk_ui_destroy(ui);
}

static void test_accessor_command_fields(void) {
  lk_ui *ui = lk_ui_create(NULL);
  lk_tree *t;
  lk_event ev;
  const lk_command_queue *q;
  const lk_command *cmd;
  lk_value arg;
  lk_u32 select_id;
  lk_u32 item_id;

  BEGIN_TEST("accessor: command name/arg_count/arg/source");

  lk_ui_add_translator_s(ui, LK_EVENT_POINTER_DOWN, "item", 0, "Select");
  select_id = lk_intern_id(ui->intern, lk_str_c("Select"));
  item_id = lk_intern_id(ui->intern, lk_str_c("item"));

  t = lk_ui_begin_frame(ui);
  {
    lk_ix w = lk_tree_add_node_s(t, lk_str_c("w"), UIK_WINDOW);
    lk_ix btn = lk_tree_add_node_s(t, lk_str_c("btn"), UIK_BUTTON);
    lk_tree_set_root(t, w);
    lk_tree_append_child(t, w, btn);
    lk_tree_add_presentation_s(t, btn, "item", lk_v_i32(42));
  }
  lk_ui_end_frame(ui);

  memset(&ev, 0, sizeof(ev));
  ev.type = LK_EVENT_POINTER_DOWN;
  ev.target = lk_tree_find_by_id(lk_ui_tree(ui),
               lk_intern_id(ui->intern, lk_str_c("btn")));
  lk_event_route(ui, &ev);

  q = lk_ui_commands(ui);
  CHECK_EQ(lk_command_queue_count(q), 1u);

  cmd = lk_command_queue_get(q, 0);
  CHECK(cmd != NULL);
  if (cmd) {
    CHECK_EQ(lk_command_name(cmd), select_id);
    CHECK_EQ(lk_command_arg_count(cmd), 1u);

    arg = lk_command_arg(cmd, 0);
    CHECK_EQ(arg.tag, UIV_I32);
    CHECK_EQ(arg.as.i, 42u);

    CHECK(lk_command_source_node(cmd) != 0);
    CHECK_EQ(lk_command_source_ptype(cmd), item_id);
  }

  /* out-of-bounds arg returns NONE */
  arg = lk_command_arg(cmd, 5);
  CHECK_EQ(arg.tag, UIV_NONE);

  /* null safety */
  CHECK_EQ(lk_command_name(NULL), 0u);
  CHECK_EQ(lk_command_arg_count(NULL), 0u);
  CHECK_EQ(lk_command_queue_count(NULL), 0u);
  CHECK(lk_command_queue_get(NULL, 0) == NULL);

  END_TEST();
  lk_ui_destroy(ui);
}

/* ================================================================
 * Binding-friendly API tests
 * ================================================================ */

static void test_intern_cstr(void) {
  lk_intern *it = lk_intern_new(NULL, NULL);
  lk_node_id id1, id2;
  const char *s;
  lk_str sv;

  BEGIN_TEST("intern: cid/cstr roundtrip");

  /* Intern via C string */
  id1 = lk_intern_cid(it, "hello");
  CHECK(id1 != 0);

  /* Retrieve as null-terminated C string */
  s = lk_intern_cstr(it, id1);
  CHECK(strcmp(s, "hello") == 0);

  /* Same string returns same id */
  id2 = lk_intern_cid(it, "hello");
  CHECK_EQ(id1, id2);

  /* lk_intern_str still works correctly */
  sv = lk_intern_str(it, id1);
  CHECK_EQ(sv.len, 5);
  CHECK(memcmp(sv.ptr, "hello", 5) == 0);

  /* Intern via lk_str also null-terminated */
  id2 = lk_intern_id(it, lk_str_c("world"));
  s = lk_intern_cstr(it, id2);
  CHECK(strcmp(s, "world") == 0);

  /* Edge cases */
  CHECK_EQ(lk_intern_cid(it, NULL), 0);
  CHECK(strcmp(lk_intern_cstr(it, 0), "") == 0);
  CHECK(strcmp(lk_intern_cstr(it, 9999), "") == 0);
  CHECK(strcmp(lk_intern_cstr(NULL, 1), "") == 0);

  END_TEST();
  lk_intern_destroy(it);
}

static void test_add_node_c_and_text_cstr(void) {
  lk_tree *t;
  lk_tree_cfg cfg;
  lk_ix w, lbl;
  const char *txt;

  BEGIN_TEST("tree: add_node_c + text_cstr");

  memset(&cfg, 0, sizeof(cfg));
  t = lk_tree_create(&cfg);

  w = lk_tree_add_node_c(t, "main", UIK_WINDOW);
  CHECK(w != 0);

  lbl = lk_tree_add_node_c(t, "lbl", UIK_LABEL);
  CHECK(lbl != 0);
  lk_tree_add_prop(t, lbl, UIP_TEXT, lk_v_cstr(t->intern, "Hello World"));

  lk_tree_set_root(t, w);
  lk_tree_append_child(t, w, lbl);

  /* Verify text via cstr accessor */
  txt = lk_node_text_cstr(t, lbl);
  CHECK(strcmp(txt, "Hello World") == 0);

  /* No text returns empty string */
  txt = lk_node_text_cstr(t, w);
  CHECK(strcmp(txt, "") == 0);

  /* Null-safe */
  CHECK_EQ(lk_tree_add_node_c(t, NULL, UIK_LABEL), 0);
  CHECK(strcmp(lk_node_text_cstr(NULL, 1), "") == 0);

  END_TEST();
  lk_tree_destroy(t);
}

static void test_command_arg_typed(void) {
  lk_ui *ui = lk_ui_create(NULL);
  lk_tree *t;
  const lk_tree *cur;
  lk_event ev;
  const lk_command_queue *q;

  BEGIN_TEST("command: typed arg accessors");

  lk_ui_add_translator_s(ui, LK_EVENT_POINTER_DOWN, "item", 0, "Select");

  t = lk_ui_begin_frame(ui);
  {
    lk_ix w = lk_tree_add_node_c(t, "w", UIK_WINDOW);
    lk_ix btn = lk_tree_add_node_c(t, "btn", UIK_BUTTON);
    lk_tree_set_root(t, w);
    lk_tree_append_child(t, w, btn);
    lk_tree_add_presentation_s(t, btn, "item", lk_v_i32(42));
  }
  lk_ui_end_frame(ui);

  cur = lk_ui_tree(ui);
  memset(&ev, 0, sizeof(ev));
  ev.type = LK_EVENT_POINTER_DOWN;
  ev.target = lk_tree_find_by_id(cur,
               lk_intern_cid(ui->intern, "btn"));
  lk_event_route(ui, &ev);

  q = lk_ui_commands(ui);
  CHECK_EQ(q->count, 1);
  if (q->count >= 1) {
    CHECK_EQ(lk_command_arg_tag(&q->cmds[0], 0), UIV_I32);
    CHECK_EQ((lk_u32)lk_command_arg_i32(&q->cmds[0], 0), 42);
    /* Wrong type returns 0 */
    CHECK_EQ(lk_command_arg_str_id(&q->cmds[0], 0), 0);
    /* OOB returns defaults */
    CHECK_EQ(lk_command_arg_tag(&q->cmds[0], 4), UIV_NONE);
    CHECK_EQ((lk_u32)lk_command_arg_i32(&q->cmds[0], 4), 0);
    /* NULL-safe */
    CHECK_EQ(lk_command_arg_tag(NULL, 0), UIV_NONE);
    CHECK_EQ((lk_u32)lk_command_arg_i32(NULL, 0), 0);
    CHECK_EQ(lk_command_arg_str_id(NULL, 0), 0);
  }

  END_TEST();
  lk_ui_destroy(ui);
}

static void test_event_init(void) {
  lk_event ev;
  lk_u8 *raw;
  lk_u32 i;
  int all_zero;

  BEGIN_TEST("event: init helpers");

  /* Pointer event */
  memset(&ev, 0xFF, sizeof(ev));
  lk_event_init_pointer(&ev, LK_EVENT_POINTER_DOWN, 100, 200, 2);
  CHECK_EQ(ev.type, LK_EVENT_POINTER_DOWN);
  CHECK_EQ((lk_u32)ev.data.pointer.x, 100);
  CHECK_EQ((lk_u32)ev.data.pointer.y, 200);
  CHECK_EQ(ev.data.pointer.button, 2);
  CHECK_EQ(ev.phase, 0);
  CHECK_EQ(ev.handled, 0);
  CHECK_EQ(ev.target, 0);
  CHECK_EQ(ev.mods, 0);

  /* Key event */
  memset(&ev, 0xFF, sizeof(ev));
  lk_event_init_key(&ev, LK_EVENT_KEY_DOWN, LKK_RETURN, LK_MOD_CTRL);
  CHECK_EQ(ev.type, LK_EVENT_KEY_DOWN);
  CHECK_EQ(ev.data.key.keycode, LKK_RETURN);
  CHECK_EQ(ev.mods, LK_MOD_CTRL);
  CHECK_EQ(ev.phase, 0);
  CHECK_EQ(ev.handled, 0);
  CHECK_EQ(ev.target, 0);
  CHECK_EQ(ev.data.key.repeat, 0);

  /* Verify rest of union is zeroed (pointer init) */
  lk_event_init_pointer(&ev, LK_EVENT_POINTER_MOVE, 0, 0, 0);
  raw = (lk_u8 *)&ev;
  all_zero = 1;
  for (i = 1; i < sizeof(ev); i++) {
    if (raw[i] != 0) {
      all_zero = 0;
    }
  }
  /* type byte is LK_EVENT_POINTER_MOVE (1), rest should be 0 */
  CHECK_EQ(raw[0], LK_EVENT_POINTER_MOVE);
  CHECK(all_zero);

  END_TEST();
}

/* ================================================================
 * Main
 * ================================================================ */

int main(void) {
  printf("lk diff tests:\n");

  /* basic lifecycle */
  test_first_frame_all_added();
  test_identical_no_changes();
  test_three_identical_frames();

  /* prop changes */
  test_prop_value_change();
  test_prop_added();
  test_prop_removed();
  test_prop_type_change();

  /* kind changes */
  test_kind_change();

  /* structural changes */
  test_child_added();
  test_child_removed();
  test_child_replaced();
  test_reorder_no_change();
  test_subtree_added();
  test_subtree_removed();

  /* deep tree */
  test_deep_change();

  /* edge cases */
  test_root_identity_change();
  test_to_empty();
  test_empty_to_empty();
  test_single_root_only();

  /* node_ix validity */
  test_node_ix_valid_after_swap();
  test_removed_node_ix_zero();

  /* multi-frame sequences */
  test_add_then_remove();
  test_many_siblings();
  test_ui_tree_returns_current();

  /* layout */
  printf("\nlk layout tests:\n");
  test_layout_window_fills_viewport();
  test_layout_column_two_labels();
  test_layout_column_with_gap();
  test_layout_column_with_padding();
  test_layout_row_two_labels();
  test_layout_row_with_gap();
  test_layout_spacer_in_column();
  test_layout_two_spacers_split();
  test_layout_spacer_in_row();
  test_layout_explicit_wh();
  test_layout_button_intrinsic();
  test_layout_padding_gap_combined();
  test_layout_empty_tree();
  test_layout_spacer_explicit_h();
  test_layout_nested_column_row_labels();

  /* alignment */
  printf("\nlk alignment tests:\n");
  test_layout_column_align_center();
  test_layout_column_align_end();
  test_layout_column_justify_center();
  test_layout_column_justify_end();
  test_layout_row_align_center();
  test_layout_row_justify_end();

  /* render list */
  printf("\nlk render tests:\n");
  test_render_empty_tree();
  test_render_window_only();
  test_render_window_column_label();
  test_render_button_with_padding();
  test_render_larger_tree();
  test_render_build_reuse();

  /* hit testing */
  printf("\nlk hit-test tests:\n");
  test_hit_single_button();
  test_hit_outside();
  test_hit_parent_padding();
  test_hit_deepest_child();
  test_hit_later_sibling();
  test_hit_empty_tree();

  /* focus management */
  printf("\nlk focus tests:\n");
  test_focus_set_focusable();
  test_focus_set_not_focusable();
  test_focus_set_disabled();
  test_focus_clear();
  test_focus_next_wraps();
  test_focus_prev_wraps();
  test_focus_next_skips_disabled();
  test_focus_removed_clears();

  /* event routing */
  printf("\nlk event routing tests:\n");
  test_route_full_traversal();
  test_route_stop_capture();
  test_route_stop_target();
  test_route_stop_bubble();
  test_route_no_handler();
  test_route_target_is_root();

  /* integrated */
  printf("\nlk integrated event tests:\n");
  test_hit_route_integrated();
  test_tab_cycles_focus();

  /* widget registry */
  printf("\nlk widget registry tests:\n");
  test_widget_get_defaults();
  test_widget_register_custom();
  test_widget_override_render();

  /* presentations */
  printf("\nlk presentation tests:\n");
  test_pres_add_and_get();
  test_pres_none_returns_null();
  test_pres_survives_frame();
  test_pres_change_triggers_updated();

  /* command / translator */
  printf("\nlk command/translator tests:\n");
  test_translator_fires_command();
  test_translator_no_match();
  test_translator_walks_ancestors();
  test_translator_ptype_and_kind();
  test_command_handler_fires();

  /* introspection */
  printf("\nlk introspection tests:\n");
  test_command_log_accumulates();
  test_dump_commands_output();

  /* accessors */
  printf("\nlk accessor tests:\n");
  test_accessor_node_fields();
  test_accessor_tree_fields();
  test_accessor_changeset();
  test_accessor_command_fields();

  /* binding-friendly API */
  printf("\nlk binding API tests:\n");
  test_intern_cstr();
  test_add_node_c_and_text_cstr();
  test_command_arg_typed();
  test_event_init();

  printf("\n%d/%d tests passed", g_pass, g_tests);

  if (g_fail > 0) {
    printf(", %d FAILED", g_fail);
  }

  printf("\n");

  return g_fail > 0 ? 1 : 0;
}
