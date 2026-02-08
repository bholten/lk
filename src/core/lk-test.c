#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "lk-data.h"
#include "lk-memory.h"

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

  printf("\n%d/%d tests passed", g_pass, g_tests);

  if (g_fail > 0) {
    printf(", %d FAILED", g_fail);
  }

  printf("\n");

  return g_fail > 0 ? 1 : 0;
}
