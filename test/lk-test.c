#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <lk.h>
#include "core/lk-dropdown.h" /* lk_dropdown_popup_rect (geometry tests) */
#include "core/lk-memory.h"
#include "core/lk-tooltip.h" /* lk_tooltip_rect (geometry tests) */

/* ---- minimal test harness (macros in lk-test-harness.h) ---- */

#include "lk-test-harness.h"

int g_tests = 0;
int g_pass = 0;
int g_fail = 0;
int g_cur_ok = 0;

/* document + edit history tests (test/lk-document-test.c) */
void lk_document_run_tests(void);

/* resource table + render-list run arena tests
 * (test/lk-resource-test.c, editor track stage B1) */
void lk_resource_run_tests(void);

/* editor view + command layer + UIK_EDITOR widget tests
 * (test/lk-editor-test.c, editor track stage B2) */
void lk_editor_run_tests(void);

/* annotation store + styled-span render tests
 * (test/lk-annot-test.c, editor track stage C) */
void lk_annot_run_tests(void);

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
  cfg.text = lk_text_backend_stub();
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
  cfg.text = lk_text_backend_stub();
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
  ok = lk_render_build(t, NULL, NULL, NULL, &rl);
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
    lk_render_build(t, r, NULL, NULL, &rl);
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
    lk_render_build(t, r, NULL, NULL, &rl);
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
    lk_render_build(t, r, NULL, NULL, &rl);
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
    lk_render_build(t, r, NULL, NULL, &rl);
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
    lk_render_build(t, r, NULL, NULL, &rl);
    CHECK_EQ(rl.count, 4u);
    cap_after_first = rl.cap;
    CHECK(cap_after_first > 0);

    /* second build on same list — count resets, cap stays */
    lk_render_build(t, r, NULL, NULL, &rl);
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

  /* Use a kind slot beyond the built-ins (UIK__COUNT..LK_KIND_MAX) —
   * slot 10 is UIK_OPTION nowadays, so don't clobber built-ins. */
  lk_widget_register((lk_kind)20, &custom);
  got = lk_widget_get((lk_kind)20);

  CHECK(got != NULL);
  CHECK(got->clips == 1);
  CHECK(got->measure == NULL);
  CHECK(got->render == NULL);

  /* Clean up: reset slot to zero */
  memset(&custom, 0, sizeof(custom));
  lk_widget_register((lk_kind)20, &custom);

  END_TEST();
}

static void test_widget_override_render(void) {
  /* Override LABEL's render to emit nothing; verify render output differs */
  lk_tree *t;
  lk_rect *r;
  lk_render_list rl;
  lk_widget_def override;
  lk_widget_def saved;
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
    lk_render_build(t, r, NULL, NULL, &rl);
    count_before = rl.count;

    /* Override LABEL to emit nothing */
    saved = *lk_widget_get(UIK_LABEL);
    override = saved;
    override.render = NULL;
    lk_widget_register(UIK_LABEL, &override);

    rl.count = 0;
    lk_render_build(t, r, NULL, NULL, &rl);
    count_after = rl.count;

    CHECK(count_after < count_before);

    /* Restore original */
    lk_widget_register(UIK_LABEL, &saved);

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
    CHECK_EQ(p->pvalue_count, 1);
    CHECK_EQ(p->pvalues[0].tag, UIV_I32);
    CHECK_EQ(p->pvalues[0].as.i, 42);
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

static void test_pres_multi_arg(void) {
  lk_tree *t = lk_tree_create(NULL);
  lk_ix w, btn;
  const lk_presentation *p;
  lk_value args[3];

  BEGIN_TEST("pres: multi-arg presentation stored and retrieved");

  w = lk_tree_add_node_s(t, lk_str_c("w"), UIK_WINDOW);
  btn = lk_tree_add_node_s(t, lk_str_c("btn"), UIK_BUTTON);
  lk_tree_set_root(t, w);
  lk_tree_append_child(t, w, btn);

  args[0] = lk_v_cstr(t->intern, "remove_row");
  args[1] = lk_v_i32(5);
  args[2] = lk_v_cstr(t->intern, "force");
  lk_tree_add_presentation_sv(t, btn, "action", args, 3);

  p = lk_tree_get_presentation(t, btn);
  CHECK(p != NULL);
  if (p) {
    CHECK_EQ(p->pvalue_count, 3);
    CHECK_EQ(p->pvalues[0].tag, UIV_STR);
    CHECK_EQ(p->pvalues[1].tag, UIV_I32);
    CHECK_EQ(p->pvalues[1].as.i, 5);
    CHECK_EQ(p->pvalues[2].tag, UIV_STR);
  }

  END_TEST();
  lk_tree_destroy(t);
}

static void test_pres_multi_arg_cmd(void) {
  lk_ui *ui = lk_ui_create(NULL);
  lk_tree *t;
  lk_event ev;
  const lk_command_queue *q;
  lk_value args[2];

  BEGIN_TEST("pres: multi-arg pres emits multi-arg command");

  lk_ui_add_translator_s(ui, LK_EVENT_POINTER_DOWN, "action", 0, 0, 0, "DoIt");

  t = lk_ui_begin_frame(ui);
  {
    lk_ix w = lk_tree_add_node_s(t, lk_str_c("w"), UIK_WINDOW);
    lk_ix btn = lk_tree_add_node_s(t, lk_str_c("btn"), UIK_BUTTON);
    lk_tree_set_root(t, w);
    lk_tree_append_child(t, w, btn);

    args[0] = lk_v_cstr(t->intern, "remove_row");
    args[1] = lk_v_i32(5);
    lk_tree_add_presentation_sv(t, btn, "action", args, 2);
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
    CHECK_EQ(q->cmds[0].arg_count, 2);
    CHECK_EQ(q->cmds[0].args[0].tag, UIV_STR);
    CHECK_EQ(q->cmds[0].args[1].tag, UIV_I32);
    CHECK_EQ(q->cmds[0].args[1].as.i, 5);
  }

  END_TEST();
  lk_ui_destroy(ui);
}

static void test_pres_multi_arg_change_detected(void) {
  lk_ui *ui = lk_ui_create(NULL);
  lk_tree *t;
  const lk_changeset *cs;
  lk_value args[2];

  BEGIN_TEST("pres: changed arg in multi-arg pres triggers UPDATED");

  /* Frame 1: two args (remove_row, 5) */
  t = lk_ui_begin_frame(ui);
  {
    lk_ix w = lk_tree_add_node_s(t, lk_str_c("w"), UIK_WINDOW);
    lk_ix btn = lk_tree_add_node_s(t, lk_str_c("btn"), UIK_BUTTON);
    lk_tree_set_root(t, w);
    lk_tree_append_child(t, w, btn);
    args[0] = lk_v_cstr(t->intern, "remove_row");
    args[1] = lk_v_i32(5);
    lk_tree_add_presentation_sv(t, btn, "action", args, 2);
  }
  lk_ui_end_frame(ui);

  /* Frame 2: second arg changed */
  t = lk_ui_begin_frame(ui);
  {
    lk_ix w = lk_tree_add_node_s(t, lk_str_c("w"), UIK_WINDOW);
    lk_ix btn = lk_tree_add_node_s(t, lk_str_c("btn"), UIK_BUTTON);
    lk_tree_set_root(t, w);
    lk_tree_append_child(t, w, btn);
    args[0] = lk_v_cstr(t->intern, "remove_row");
    args[1] = lk_v_i32(7);
    lk_tree_add_presentation_sv(t, btn, "action", args, 2);
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
  lk_ui_add_translator_s(ui, LK_EVENT_POINTER_DOWN, "item", 0, 0, 0, "Select");

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
  lk_ui_add_translator_s(ui, LK_EVENT_KEY_DOWN, "item", 0, 0, 0, "Select");

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

  lk_ui_add_translator_s(ui, LK_EVENT_POINTER_DOWN, "list", 0, 0, 0, "ListClick");

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
  lk_ui_add_translator_s(ui, 0, "action", (lk_u16)UIK_BUTTON, 0, 0, "DoAction");

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

static void test_translator_keycode_match(void) {
  lk_ui *ui = lk_ui_create(NULL);
  lk_tree *t;
  lk_event ev;
  const lk_command_queue *q;

  BEGIN_TEST("translator: keycode+mods match Ctrl+S");

  /* Ctrl+S -> "Save" */
  lk_ui_add_translator_s(ui, LK_EVENT_KEY_DOWN, "doc", 0,
                          (lk_u16)LKK_S, LK_MOD_CTRL, "Save");

  t = lk_ui_begin_frame(ui);
  {
    lk_ix w = lk_tree_add_node_s(t, lk_str_c("w"), UIK_WINDOW);
    lk_ix lbl = lk_tree_add_node_s(t, lk_str_c("lbl"), UIK_LABEL);
    lk_tree_set_root(t, w);
    lk_tree_append_child(t, w, lbl);
    lk_tree_add_presentation_s(t, lbl, "doc", lk_v_i32(1));
  }
  lk_ui_end_frame(ui);

  memset(&ev, 0, sizeof(ev));
  ev.type = LK_EVENT_KEY_DOWN;
  ev.data.key.keycode = LKK_S;
  ev.mods = LK_MOD_CTRL;
  ev.target = lk_tree_find_by_id(lk_ui_tree(ui),
               lk_intern_id(ui->intern, lk_str_c("lbl")));

  lk_event_route(ui, &ev);

  q = lk_ui_commands(ui);
  CHECK_EQ(q->count, 1);
  if (q->count >= 1) {
    CHECK_EQ(q->cmds[0].name,
             lk_intern_id(ui->intern, lk_str_c("Save")));
  }
  CHECK_EQ(ev.handled, 1);

  END_TEST();
  lk_ui_destroy(ui);
}

static void test_translator_keycode_wrong_key(void) {
  lk_ui *ui = lk_ui_create(NULL);
  lk_tree *t;
  lk_event ev;
  const lk_command_queue *q;

  BEGIN_TEST("translator: keycode mismatch = no command");

  lk_ui_add_translator_s(ui, LK_EVENT_KEY_DOWN, "doc", 0,
                          (lk_u16)LKK_S, LK_MOD_CTRL, "Save");

  t = lk_ui_begin_frame(ui);
  {
    lk_ix w = lk_tree_add_node_s(t, lk_str_c("w"), UIK_WINDOW);
    lk_ix lbl = lk_tree_add_node_s(t, lk_str_c("lbl"), UIK_LABEL);
    lk_tree_set_root(t, w);
    lk_tree_append_child(t, w, lbl);
    lk_tree_add_presentation_s(t, lbl, "doc", lk_v_i32(1));
  }
  lk_ui_end_frame(ui);

  /* Send Ctrl+F instead of Ctrl+S */
  memset(&ev, 0, sizeof(ev));
  ev.type = LK_EVENT_KEY_DOWN;
  ev.data.key.keycode = LKK_F;
  ev.mods = LK_MOD_CTRL;
  ev.target = lk_tree_find_by_id(lk_ui_tree(ui),
               lk_intern_id(ui->intern, lk_str_c("lbl")));

  lk_event_route(ui, &ev);

  q = lk_ui_commands(ui);
  CHECK_EQ(q->count, 0);
  CHECK_EQ(ev.handled, 0);

  END_TEST();
  lk_ui_destroy(ui);
}

static void test_translator_keycode_wrong_mods(void) {
  lk_ui *ui = lk_ui_create(NULL);
  lk_tree *t;
  lk_event ev;
  const lk_command_queue *q;

  BEGIN_TEST("translator: wrong mods = no command");

  /* Ctrl+S translator */
  lk_ui_add_translator_s(ui, LK_EVENT_KEY_DOWN, "doc", 0,
                          (lk_u16)LKK_S, LK_MOD_CTRL, "Save");

  t = lk_ui_begin_frame(ui);
  {
    lk_ix w = lk_tree_add_node_s(t, lk_str_c("w"), UIK_WINDOW);
    lk_ix lbl = lk_tree_add_node_s(t, lk_str_c("lbl"), UIK_LABEL);
    lk_tree_set_root(t, w);
    lk_tree_append_child(t, w, lbl);
    lk_tree_add_presentation_s(t, lbl, "doc", lk_v_i32(1));
  }
  lk_ui_end_frame(ui);

  /* Send Ctrl+Shift+S — should NOT match Ctrl+S */
  memset(&ev, 0, sizeof(ev));
  ev.type = LK_EVENT_KEY_DOWN;
  ev.data.key.keycode = LKK_S;
  ev.mods = LK_MOD_CTRL | LK_MOD_SHIFT;
  ev.target = lk_tree_find_by_id(lk_ui_tree(ui),
               lk_intern_id(ui->intern, lk_str_c("lbl")));

  lk_event_route(ui, &ev);

  q = lk_ui_commands(ui);
  CHECK_EQ(q->count, 0);
  CHECK_EQ(ev.handled, 0);

  END_TEST();
  lk_ui_destroy(ui);
}

static void test_translator_keycode_no_pres_required(void) {
  lk_ui *ui = lk_ui_create(NULL);
  lk_tree *t;
  lk_event ev;
  const lk_command_queue *q;

  BEGIN_TEST("translator: keycode+mods with ptype=0 needs pres");

  /* Ctrl+F -> "Find", ptype=0 (match any presentation) */
  lk_ui_add_translator_s(ui, LK_EVENT_KEY_DOWN, NULL, 0,
                          (lk_u16)LKK_F, LK_MOD_CTRL, "Find");

  t = lk_ui_begin_frame(ui);
  {
    lk_ix w = lk_tree_add_node_s(t, lk_str_c("w"), UIK_WINDOW);
    lk_ix lbl = lk_tree_add_node_s(t, lk_str_c("lbl"), UIK_LABEL);
    lk_tree_set_root(t, w);
    lk_tree_append_child(t, w, lbl);
    /* Node has a presentation (any type) */
    lk_tree_add_presentation_s(t, w, "app", lk_v_i32(0));
  }
  lk_ui_end_frame(ui);

  memset(&ev, 0, sizeof(ev));
  ev.type = LK_EVENT_KEY_DOWN;
  ev.data.key.keycode = LKK_F;
  ev.mods = LK_MOD_CTRL;
  ev.target = lk_tree_find_by_id(lk_ui_tree(ui),
               lk_intern_id(ui->intern, lk_str_c("lbl")));

  lk_event_route(ui, &ev);

  q = lk_ui_commands(ui);
  CHECK_EQ(q->count, 1);
  if (q->count >= 1) {
    CHECK_EQ(q->cmds[0].name,
             lk_intern_id(ui->intern, lk_str_c("Find")));
  }
  CHECK_EQ(ev.handled, 1);

  END_TEST();
  lk_ui_destroy(ui);
}

static void test_translator_keycode_on_pointer_event(void) {
  lk_ui *ui = lk_ui_create(NULL);
  lk_tree *t;
  lk_event ev;
  const lk_command_queue *q;

  BEGIN_TEST("translator: keycode filter skips non-key events");

  /* Translator with keycode set — should not match pointer events */
  lk_ui_add_translator_s(ui, 0, "item", 0,
                          (lk_u16)LKK_S, LK_MOD_CTRL, "Save");

  t = lk_ui_begin_frame(ui);
  {
    lk_ix w = lk_tree_add_node_s(t, lk_str_c("w"), UIK_WINDOW);
    lk_ix btn = lk_tree_add_node_s(t, lk_str_c("btn"), UIK_BUTTON);
    lk_tree_set_root(t, w);
    lk_tree_append_child(t, w, btn);
    lk_tree_add_presentation_s(t, btn, "item", lk_v_i32(5));
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

static void test_translator_keycode_zero_mods(void) {
  lk_ui *ui = lk_ui_create(NULL);
  lk_tree *t;
  lk_event ev;
  const lk_command_queue *q;

  BEGIN_TEST("translator: keycode with zero mods matches bare key");

  /* Return key with no modifiers -> "Confirm" */
  lk_ui_add_translator_s(ui, LK_EVENT_KEY_DOWN, "form", 0,
                          (lk_u16)LKK_RETURN, 0, "Confirm");

  t = lk_ui_begin_frame(ui);
  {
    lk_ix w = lk_tree_add_node_s(t, lk_str_c("w"), UIK_WINDOW);
    lk_ix btn = lk_tree_add_node_s(t, lk_str_c("btn"), UIK_BUTTON);
    lk_tree_set_root(t, w);
    lk_tree_append_child(t, w, btn);
    lk_tree_add_presentation_s(t, btn, "form", lk_v_i32(1));
  }
  lk_ui_end_frame(ui);

  /* Send Return with no mods */
  memset(&ev, 0, sizeof(ev));
  ev.type = LK_EVENT_KEY_DOWN;
  ev.data.key.keycode = LKK_RETURN;
  ev.mods = 0;
  ev.target = lk_tree_find_by_id(lk_ui_tree(ui),
               lk_intern_id(ui->intern, lk_str_c("btn")));

  lk_event_route(ui, &ev);

  q = lk_ui_commands(ui);
  CHECK_EQ(q->count, 1);
  CHECK_EQ(ev.handled, 1);

  /* Return+Ctrl should NOT match (mods=0 means exact: no mods) */
  lk_ui_clear_commands(ui);

  memset(&ev, 0, sizeof(ev));
  ev.type = LK_EVENT_KEY_DOWN;
  ev.data.key.keycode = LKK_RETURN;
  ev.mods = LK_MOD_CTRL;
  ev.target = lk_tree_find_by_id(lk_ui_tree(ui),
               lk_intern_id(ui->intern, lk_str_c("btn")));

  lk_event_route(ui, &ev);

  q = lk_ui_commands(ui);
  CHECK_EQ(q->count, 0);
  CHECK_EQ(ev.handled, 0);

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
  lk_ui_add_translator_s(ui, LK_EVENT_POINTER_DOWN, "item", 0, 0, 0, "Pick");

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

  lk_ui_add_translator_s(ui, LK_EVENT_POINTER_DOWN, "item", 0, 0, 0, "Select");

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

  lk_ui_add_translator_s(ui, LK_EVENT_POINTER_DOWN, "item", 0, 0, 0, "Select");
  lk_ui_add_translator_s(ui, LK_EVENT_KEY_DOWN, NULL, 0, 0, 0, "Activate");

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

  lk_ui_add_translator_s(ui, LK_EVENT_POINTER_DOWN, "item", 0, 0, 0, "Select");
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
  lk_intern *it = lk_intern_new(NULL, NULL, NULL);
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

  lk_ui_add_translator_s(ui, LK_EVENT_POINTER_DOWN, "item", 0, 0, 0, "Select");

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
 * State store tests
 * ================================================================ */

static void test_state_set_get(void) {
  lk_ui *ui;
  lk_state *st;
  lk_value v;
  lk_node_id nid;

  BEGIN_TEST("state: set and get");

  ui = lk_ui_create(NULL);
  st = lk_ui_state(ui);
  nid = lk_intern_cid(lk_ui_intern(ui), "my_node");

  CHECK(lk_state_set(st, nid, LKS_SCROLL_Y, lk_v_i32(42)));
  v = lk_state_get(st, nid, LKS_SCROLL_Y);
  CHECK_EQ(v.tag, UIV_I32);
  CHECK_EQ(v.as.i, 42);

  lk_ui_destroy(ui);
  END_TEST();
}

static void test_state_overwrite(void) {
  lk_ui *ui;
  lk_state *st;
  lk_value v;
  lk_node_id nid;

  BEGIN_TEST("state: overwrite existing");

  ui = lk_ui_create(NULL);
  st = lk_ui_state(ui);
  nid = lk_intern_cid(lk_ui_intern(ui), "nd");

  lk_state_set(st, nid, LKS_CURSOR_POS, lk_v_i32(10));
  lk_state_set(st, nid, LKS_CURSOR_POS, lk_v_i32(77));
  v = lk_state_get(st, nid, LKS_CURSOR_POS);
  CHECK_EQ(v.tag, UIV_I32);
  CHECK_EQ(v.as.i, 77);

  lk_ui_destroy(ui);
  END_TEST();
}

static void test_state_missing_key(void) {
  lk_ui *ui;
  lk_state *st;
  lk_value v;
  lk_node_id nid;

  BEGIN_TEST("state: missing key returns NONE");

  ui = lk_ui_create(NULL);
  st = lk_ui_state(ui);
  nid = lk_intern_cid(lk_ui_intern(ui), "nd");

  v = lk_state_get(st, nid, LKS_SCROLL_X);
  CHECK_EQ(v.tag, UIV_NONE);

  lk_ui_destroy(ui);
  END_TEST();
}

static void test_state_gc_on_removal(void) {
  lk_ui *ui;
  lk_state *st;
  lk_tree *t;
  lk_ix win, btn;
  lk_value v;
  lk_node_id btn_id;

  BEGIN_TEST("state: GC on node removal");

  ui = lk_ui_create(NULL);
  st = lk_ui_state(ui);

  /* Frame 1: window > button */
  t = lk_ui_begin_frame(ui);
  win = lk_tree_add_node_c(t, "win", UIK_WINDOW);
  btn = lk_tree_add_node_c(t, "btn", UIK_BUTTON);
  lk_tree_set_root(t, win);
  lk_tree_append_child(t, win, btn);
  lk_ui_end_frame(ui);

  /* Set state on button */
  btn_id = lk_intern_cid(lk_ui_intern(ui), "btn");
  lk_state_set(st, btn_id, LKS_SCROLL_Y, lk_v_i32(99));

  /* Frame 2: remove the button */
  t = lk_ui_begin_frame(ui);
  win = lk_tree_add_node_c(t, "win", UIK_WINDOW);
  lk_tree_set_root(t, win);
  lk_ui_end_frame(ui);

  /* State should be GC'd */
  v = lk_state_get(st, btn_id, LKS_SCROLL_Y);
  CHECK_EQ(v.tag, UIV_NONE);

  lk_ui_destroy(ui);
  END_TEST();
}

static void test_state_multiple_keys(void) {
  lk_ui *ui;
  lk_state *st;
  lk_value v;
  lk_node_id nid;

  BEGIN_TEST("state: multiple keys on one node");

  ui = lk_ui_create(NULL);
  st = lk_ui_state(ui);
  nid = lk_intern_cid(lk_ui_intern(ui), "nd");

  lk_state_set(st, nid, LKS_SCROLL_X, lk_v_i32(10));
  lk_state_set(st, nid, LKS_SCROLL_Y, lk_v_i32(20));
  lk_state_set(st, nid, LKS_EXPANDED, lk_v_bool(1));

  v = lk_state_get(st, nid, LKS_SCROLL_X);
  CHECK_EQ(v.tag, UIV_I32);
  CHECK_EQ(v.as.i, 10);

  v = lk_state_get(st, nid, LKS_SCROLL_Y);
  CHECK_EQ(v.tag, UIV_I32);
  CHECK_EQ(v.as.i, 20);

  v = lk_state_get(st, nid, LKS_EXPANDED);
  CHECK_EQ(v.tag, UIV_BOOL);
  CHECK_EQ(v.as.b, 1);

  lk_ui_destroy(ui);
  END_TEST();
}

static void test_state_gc_preserves_other(void) {
  lk_ui *ui;
  lk_state *st;
  lk_tree *t;
  lk_ix win, a, b;
  lk_value v;
  lk_node_id a_id, b_id;

  BEGIN_TEST("state: GC preserves other nodes");

  ui = lk_ui_create(NULL);
  st = lk_ui_state(ui);

  /* Frame 1: window > a, b */
  t = lk_ui_begin_frame(ui);
  win = lk_tree_add_node_c(t, "win", UIK_WINDOW);
  a = lk_tree_add_node_c(t, "a", UIK_LABEL);
  b = lk_tree_add_node_c(t, "b", UIK_LABEL);
  lk_tree_set_root(t, win);
  lk_tree_append_child(t, win, a);
  lk_tree_append_child(t, win, b);
  lk_ui_end_frame(ui);

  a_id = lk_intern_cid(lk_ui_intern(ui), "a");
  b_id = lk_intern_cid(lk_ui_intern(ui), "b");
  lk_state_set(st, a_id, LKS_SCROLL_Y, lk_v_i32(11));
  lk_state_set(st, b_id, LKS_SCROLL_Y, lk_v_i32(22));

  /* Frame 2: remove "a", keep "b" */
  t = lk_ui_begin_frame(ui);
  win = lk_tree_add_node_c(t, "win", UIK_WINDOW);
  b = lk_tree_add_node_c(t, "b", UIK_LABEL);
  lk_tree_set_root(t, win);
  lk_tree_append_child(t, win, b);
  lk_ui_end_frame(ui);

  v = lk_state_get(st, a_id, LKS_SCROLL_Y);
  CHECK_EQ(v.tag, UIV_NONE);

  v = lk_state_get(st, b_id, LKS_SCROLL_Y);
  CHECK_EQ(v.tag, UIV_I32);
  CHECK_EQ(v.as.i, 22);

  lk_ui_destroy(ui);
  END_TEST();
}

static void test_state_remove_node_manual(void) {
  lk_ui *ui;
  lk_state *st;
  lk_value v;
  lk_node_id nid;

  BEGIN_TEST("state: remove_node clears all keys");

  ui = lk_ui_create(NULL);
  st = lk_ui_state(ui);
  nid = lk_intern_cid(lk_ui_intern(ui), "nd");

  lk_state_set(st, nid, LKS_SCROLL_X, lk_v_i32(5));
  lk_state_set(st, nid, LKS_SCROLL_Y, lk_v_i32(6));

  lk_state_remove_node(st, nid);

  v = lk_state_get(st, nid, LKS_SCROLL_X);
  CHECK_EQ(v.tag, UIV_NONE);

  v = lk_state_get(st, nid, LKS_SCROLL_Y);
  CHECK_EQ(v.tag, UIV_NONE);

  lk_ui_destroy(ui);
  END_TEST();
}


/* ================================================================
 * Style system tests
 * ================================================================ */

static void test_theme_create_destroy(void) {
  lk_theme *th;
  BEGIN_TEST("style: theme create/destroy");
  th = lk_theme_new(NULL, NULL, NULL);
  CHECK(th != NULL);
  lk_theme_destroy(th);
  END_TEST();
}

static void test_style_resolve_basic(void) {
  /* window > column > button — verify colors match default theme */
  lk_tree *t;
  lk_theme *th;
  lk_style *styles;
  lk_ix w, col, btn;

  BEGIN_TEST("style: resolve basic (default theme colors)");

  t = lk_tree_create(NULL);
  w = lk_tree_add_node_s(t, lk_str_c("w"), UIK_WINDOW);
  col = lk_tree_add_node_s(t, lk_str_c("col"), UIK_COLUMN);
  btn = lk_tree_add_node_s(t, lk_str_c("btn"), UIK_BUTTON);
  lk_tree_set_root(t, w);
  lk_tree_append_child(t, w, col);
  lk_tree_append_child(t, col, btn);

  th = lk_theme_default(NULL, NULL, NULL);
  CHECK(th != NULL);

  styles = (lk_style *)malloc(sizeof(lk_style) * t->node_count);
  CHECK(styles != NULL);

  if (th && styles) {
    lk_style_resolve(th, t, NULL, styles);
    /* window bg = (30,30,30,255) */
    CHECK_EQ(styles[w].bg.r, 30);
    CHECK_EQ(styles[w].bg.g, 30);
    CHECK_EQ(styles[w].bg.a, 255);
    /* button bg = (60,60,60,255) */
    CHECK_EQ(styles[btn].bg.r, 60);
    CHECK_EQ(styles[btn].bg.g, 60);
    CHECK_EQ(styles[btn].bg.a, 255);
    /* button padding = 8 */
    CHECK_EQ((unsigned)styles[btn].padding, 8u);
  }

  free(styles);
  lk_theme_destroy(th);
  lk_tree_destroy(t);
  END_TEST();
}

static void test_style_resolve_inheritance(void) {
  /* window fg propagates to label child */
  lk_tree *t;
  lk_theme *th;
  lk_style *styles;
  lk_ix w, col, lbl;

  BEGIN_TEST("style: fg inherits from parent");

  t = lk_tree_create(NULL);
  w = lk_tree_add_node_s(t, lk_str_c("w"), UIK_WINDOW);
  col = lk_tree_add_node_s(t, lk_str_c("col"), UIK_COLUMN);
  lbl = lk_tree_add_node_s(t, lk_str_c("lbl"), UIK_LABEL);
  lk_tree_add_prop(t, lbl, UIP_TEXT, lk_v_cstr(t->intern, "Hi"));
  lk_tree_set_root(t, w);
  lk_tree_append_child(t, w, col);
  lk_tree_append_child(t, col, lbl);

  th = lk_theme_default(NULL, NULL, NULL);
  styles = (lk_style *)malloc(sizeof(lk_style) * t->node_count);

  if (th && styles) {
    lk_style_resolve(th, t, NULL, styles);
    /* label inherits fg = (220,220,220,255) from wildcard rule */
    CHECK_EQ(styles[lbl].fg.r, 220);
    CHECK_EQ(styles[lbl].fg.g, 220);
    CHECK_EQ(styles[lbl].fg.b, 220);
    CHECK_EQ(styles[lbl].fg.a, 255);
  }

  free(styles);
  lk_theme_destroy(th);
  lk_tree_destroy(t);
  END_TEST();
}

static void test_style_resolve_tree_prop_override(void) {
  /* Tree prop UIP_PADDING on node overrides theme rule */
  lk_tree *t;
  lk_theme *th;
  lk_style *styles;
  lk_ix w, btn;

  BEGIN_TEST("style: tree prop override (padding)");

  t = lk_tree_create(NULL);
  w = lk_tree_add_node_s(t, lk_str_c("w"), UIK_WINDOW);
  btn = lk_tree_add_node_s(t, lk_str_c("btn"), UIK_BUTTON);
  /* theme says padding=8 for buttons, but tree prop says 20 */
  lk_tree_add_prop(t, btn, UIP_PADDING, lk_v_i32(20));
  lk_tree_set_root(t, w);
  lk_tree_append_child(t, w, btn);

  th = lk_theme_default(NULL, NULL, NULL);
  styles = (lk_style *)malloc(sizeof(lk_style) * t->node_count);

  if (th && styles) {
    lk_style_resolve(th, t, NULL, styles);
    CHECK_EQ((unsigned)styles[btn].padding, 20u);
  }

  free(styles);
  lk_theme_destroy(th);
  lk_tree_destroy(t);
  END_TEST();
}

static void test_style_resolve_rule_order(void) {
  /* Later rule wins for same field */
  lk_tree *t;
  lk_theme *th;
  lk_style *styles;
  lk_style s;
  lk_ix w;

  BEGIN_TEST("style: later rule wins for same field");

  t = lk_tree_create(NULL);
  w = lk_tree_add_node_s(t, lk_str_c("w"), UIK_WINDOW);
  lk_tree_set_root(t, w);

  th = lk_theme_new(NULL, NULL, NULL);
  memset(&s, 0, sizeof(s));
  s.bg.r = 10;
  s.bg.a = 255;
  lk_theme_add_rule(th, UIK_WINDOW, 0, 0, &s, LK_SF_BG);

  memset(&s, 0, sizeof(s));
  s.bg.r = 99;
  s.bg.a = 255;
  lk_theme_add_rule(th, UIK_WINDOW, 0, 0, &s, LK_SF_BG);

  styles = (lk_style *)malloc(sizeof(lk_style) * t->node_count);

  if (styles) {
    lk_style_resolve(th, t, NULL, styles);
    CHECK_EQ(styles[w].bg.r, 99);
  }

  free(styles);
  lk_theme_destroy(th);
  lk_tree_destroy(t);
  END_TEST();
}

static void test_style_resolve_state_match(void) {
  /* Button with FOCUSED state gets different bg */
  lk_tree *t;
  lk_theme *th;
  lk_style *styles;
  lk_u8 *nstates;
  lk_style s;
  lk_ix w, btn;

  BEGIN_TEST("style: state match (focused button)");

  t = lk_tree_create(NULL);
  w = lk_tree_add_node_s(t, lk_str_c("w"), UIK_WINDOW);
  btn = lk_tree_add_node_s(t, lk_str_c("btn"), UIK_BUTTON);
  lk_tree_set_root(t, w);
  lk_tree_append_child(t, w, btn);

  th = lk_theme_new(NULL, NULL, NULL);
  /* normal button bg */
  memset(&s, 0, sizeof(s));
  s.bg.r = 60;
  s.bg.a = 255;
  lk_theme_add_rule(th, UIK_BUTTON, 0, 0, &s, LK_SF_BG);

  /* focused button bg */
  memset(&s, 0, sizeof(s));
  s.bg.r = 100;
  s.bg.a = 255;
  lk_theme_add_rule(th, UIK_BUTTON, 0, LK_NSTATE_FOCUSED, &s, LK_SF_BG);

  styles = (lk_style *)malloc(sizeof(lk_style) * t->node_count);
  nstates = (lk_u8 *)malloc(sizeof(lk_u8) * t->node_count);

  if (styles && nstates) {
    memset(nstates, 0, sizeof(lk_u8) * t->node_count);
    nstates[btn] = LK_NSTATE_FOCUSED;
    lk_style_resolve(th, t, nstates, styles);
    CHECK_EQ(styles[btn].bg.r, 100);
  }

  free(nstates);
  free(styles);
  lk_theme_destroy(th);
  lk_tree_destroy(t);
  END_TEST();
}

static void test_style_trace(void) {
  lk_tree *t;
  lk_theme *th;
  lk_style s;
  lk_style_trace trace;
  lk_ix w;

  BEGIN_TEST("style: trace reports matching rules");

  t = lk_tree_create(NULL);
  w = lk_tree_add_node_s(t, lk_str_c("w"), UIK_WINDOW);
  lk_tree_set_root(t, w);

  th = lk_theme_new(NULL, NULL, NULL);
  /* rule 0: wildcard fg */
  memset(&s, 0, sizeof(s));
  s.fg.r = 220;
  lk_theme_add_rule(th, 0, 0, 0, &s, LK_SF_FG);
  /* rule 1: window bg */
  memset(&s, 0, sizeof(s));
  s.bg.r = 30;
  lk_theme_add_rule(th, UIK_WINDOW, 0, 0, &s, LK_SF_BG);
  /* rule 2: button only (should not match) */
  memset(&s, 0, sizeof(s));
  s.bg.r = 60;
  lk_theme_add_rule(th, UIK_BUTTON, 0, 0, &s, LK_SF_BG);

  memset(&trace, 0, sizeof(trace));
  lk_style_trace_node(th, t, w, 0, &trace);

  /* Should match rules 0 and 1, not 2 */
  CHECK_EQ(trace.count, 2u);
  if (trace.count >= 2) {
    CHECK_EQ(trace.entries[0].rule_index, 0u);
    CHECK_EQ(trace.entries[0].field_mask, LK_SF_FG);
    CHECK_EQ(trace.entries[1].rule_index, 1u);
    CHECK_EQ(trace.entries[1].field_mask, LK_SF_BG);
  }

  if (trace.entries) {
    free(trace.entries);
  }
  lk_theme_destroy(th);
  lk_tree_destroy(t);
  END_TEST();
}

/* ================================================================
 * Style + layout integration tests
 * ================================================================ */

static void test_layout_with_resolved_styles(void) {
  /* Theme sets button padding=20, verify geometry */
  lk_tree *t;
  lk_theme *th;
  lk_style *styles;
  lk_style s;
  lk_layout_cfg cfg;
  lk_rect *rects;
  lk_ix w, col, btn;

  BEGIN_TEST("layout: resolved styles drive padding");

  t = lk_tree_create(NULL);
  w = lk_tree_add_node_s(t, lk_str_c("w"), UIK_WINDOW);
  col = lk_tree_add_node_s(t, lk_str_c("col"), UIK_COLUMN);
  btn = lk_tree_add_node_s(t, lk_str_c("btn"), UIK_BUTTON);
  lk_tree_add_prop(t, btn, UIP_TEXT, lk_v_cstr(t->intern, "OK"));
  lk_tree_set_root(t, w);
  lk_tree_append_child(t, w, col);
  lk_tree_append_child(t, col, btn);

  /* Custom theme with padding=20 on buttons */
  th = lk_theme_new(NULL, NULL, NULL);
  memset(&s, 0, sizeof(s));
  s.padding = 20;
  s.bg.r = 60; s.bg.a = 255;
  lk_theme_add_rule(th, UIK_BUTTON, 0, 0, &s, LK_SF_PADDING | LK_SF_BG);

  styles = (lk_style *)malloc(sizeof(lk_style) * t->node_count);
  rects = (lk_rect *)malloc(sizeof(lk_rect) * t->node_count);

  if (th && styles && rects) {
    lk_style_resolve(th, t, NULL, styles);

    memset(&cfg, 0, sizeof(cfg));
    cfg.text = lk_text_backend_stub();
    cfg.viewport_w = 800;
    cfg.viewport_h = 600;
    cfg.styles = styles;

    lk_layout(t, &cfg, rects);

    /* Button padding in layout is 20 (from style, not tree prop).
     * The layout_pass reads cfg->styles[n].padding = 20.
     * But button has no layout func (leaf), so padding affects only
     * the parent's layout_pass content rect. For the button's own
     * rendering, the style padding is used.
     *
     * Actually, padding on the button itself is read by the layout engine
     * when computing the button's content rect. But button is a leaf
     * (no layout func), so the engine skips it. The padding only
     * affects the measure function (measure_button reads tree prop).
     *
     * For this test, let's verify the render uses style padding.
     */
    {
      lk_render_list rl;
      memset(&rl, 0, sizeof(rl));
      lk_render_build(t, rects, styles, NULL, &rl);
      /* button FILL_RECT at rl.cmds[2] (after window FILL + CLIP_BEGIN)
       * button TEXT at rl.cmds[3]
       * text should be inset by 20 from button rect
       */
      if (rl.count >= 4) {
        CHECK_EQ((unsigned)rl.cmds[3].rect.x,
                 (unsigned)(rl.cmds[2].rect.x + 20));
        CHECK_EQ((unsigned)rl.cmds[3].rect.y,
                 (unsigned)(rl.cmds[2].rect.y + 20));
      }
      lk_render_list_destroy(&rl);
    }
  }

  free(rects);
  free(styles);
  lk_theme_destroy(th);
  lk_tree_destroy(t);
  END_TEST();
}

static void test_layout_style_tree_prop_override(void) {
  /* Theme says padding=10, tree prop says padding=5, verify 5 wins */
  lk_tree *t;
  lk_theme *th;
  lk_style *styles;
  lk_style s;
  lk_layout_cfg cfg;
  lk_rect *rects;
  lk_ix w, col, btn;

  BEGIN_TEST("layout: tree prop overrides style padding");

  t = lk_tree_create(NULL);
  w = lk_tree_add_node_s(t, lk_str_c("w"), UIK_WINDOW);
  col = lk_tree_add_node_s(t, lk_str_c("col"), UIK_COLUMN);
  btn = lk_tree_add_node_s(t, lk_str_c("btn"), UIK_BUTTON);
  lk_tree_add_prop(t, btn, UIP_TEXT, lk_v_cstr(t->intern, "OK"));
  lk_tree_add_prop(t, btn, UIP_PADDING, lk_v_i32(5));
  lk_tree_set_root(t, w);
  lk_tree_append_child(t, w, col);
  lk_tree_append_child(t, col, btn);

  th = lk_theme_new(NULL, NULL, NULL);
  memset(&s, 0, sizeof(s));
  s.padding = 10;
  s.bg.r = 60; s.bg.a = 255;
  lk_theme_add_rule(th, UIK_BUTTON, 0, 0, &s, LK_SF_PADDING | LK_SF_BG);

  styles = (lk_style *)malloc(sizeof(lk_style) * t->node_count);
  rects = (lk_rect *)malloc(sizeof(lk_rect) * t->node_count);

  if (th && styles && rects) {
    lk_style_resolve(th, t, NULL, styles);
    /* Resolver should give padding=5 (tree prop wins) */
    CHECK_EQ((unsigned)styles[btn].padding, 5u);

    memset(&cfg, 0, sizeof(cfg));
    cfg.text = lk_text_backend_stub();
    cfg.viewport_w = 800;
    cfg.viewport_h = 600;
    cfg.styles = styles;

    lk_layout(t, &cfg, rects);

    {
      lk_render_list rl;
      memset(&rl, 0, sizeof(rl));
      lk_render_build(t, rects, styles, NULL, &rl);
      if (rl.count >= 4) {
        CHECK_EQ((unsigned)rl.cmds[3].rect.x,
                 (unsigned)(rl.cmds[2].rect.x + 5));
      }
      lk_render_list_destroy(&rl);
    }
  }

  free(rects);
  free(styles);
  lk_theme_destroy(th);
  lk_tree_destroy(t);
  END_TEST();
}

/* ================================================================
 * Tag tests
 * ================================================================ */

static void test_tag_add_query(void) {
  lk_tree *t;
  lk_ix w, btn;

  BEGIN_TEST("tag: add and query");

  t = lk_tree_create(NULL);
  w = lk_tree_add_node_s(t, lk_str_c("w"), UIK_WINDOW);
  btn = lk_tree_add_node_s(t, lk_str_c("btn"), UIK_BUTTON);
  lk_tree_set_root(t, w);
  lk_tree_append_child(t, w, btn);

  lk_tree_add_tag_s(t, btn, "primary");

  CHECK(lk_tree_has_tag(t, btn, lk_intern_cid(t->intern, "primary")));
  CHECK(!lk_tree_has_tag(t, btn, lk_intern_cid(t->intern, "other")));
  CHECK(!lk_tree_has_tag(t, w, lk_intern_cid(t->intern, "primary")));

  lk_tree_destroy(t);
  END_TEST();
}

static void test_tag_diffing(void) {
  lk_ui *ui = lk_ui_create(NULL);
  lk_tree *t;
  const lk_changeset *cs;

  BEGIN_TEST("tag: adding tag triggers UPDATED");

  /* Frame 1: button, no tags */
  t = lk_ui_begin_frame(ui);
  {
    lk_ix w = lk_tree_add_node_s(t, lk_str_c("w"), UIK_WINDOW);
    lk_ix btn = lk_tree_add_node_s(t, lk_str_c("btn"), UIK_BUTTON);
    lk_tree_set_root(t, w);
    lk_tree_append_child(t, w, btn);
  }
  lk_ui_end_frame(ui);

  /* Frame 2: same button, add tag */
  t = lk_ui_begin_frame(ui);
  {
    lk_ix w = lk_tree_add_node_s(t, lk_str_c("w"), UIK_WINDOW);
    lk_ix btn = lk_tree_add_node_s(t, lk_str_c("btn"), UIK_BUTTON);
    lk_tree_set_root(t, w);
    lk_tree_append_child(t, w, btn);
    lk_tree_add_tag_s(t, btn, "primary");
  }
  cs = lk_ui_end_frame(ui);

  CHECK(cs_has(cs, ui, LK_CHANGE_UPDATED, "btn"));

  lk_ui_destroy(ui);
  END_TEST();
}

static void test_style_resolve_with_tag(void) {
  lk_tree *t;
  lk_theme *th;
  lk_style *styles;
  lk_style s;
  lk_ix w, btn;

  BEGIN_TEST("style: resolve with tag match");

  t = lk_tree_create(NULL);
  w = lk_tree_add_node_s(t, lk_str_c("w"), UIK_WINDOW);
  btn = lk_tree_add_node_s(t, lk_str_c("btn"), UIK_BUTTON);
  lk_tree_set_root(t, w);
  lk_tree_append_child(t, w, btn);
  lk_tree_add_tag_s(t, btn, "danger");

  th = lk_theme_new(NULL, NULL, NULL);
  /* base button bg */
  memset(&s, 0, sizeof(s));
  s.bg.r = 60;
  s.bg.a = 255;
  lk_theme_add_rule(th, UIK_BUTTON, 0, 0, &s, LK_SF_BG);

  /* "danger" tagged button gets red bg */
  memset(&s, 0, sizeof(s));
  s.bg.r = 200;
  s.bg.g = 50;
  s.bg.a = 255;
  lk_theme_add_rule(th, UIK_BUTTON,
                    lk_intern_cid(t->intern, "danger"),
                    0, &s, LK_SF_BG);

  styles = (lk_style *)malloc(sizeof(lk_style) * t->node_count);
  if (styles) {
    lk_style_resolve(th, t, NULL, styles);
    CHECK_EQ(styles[btn].bg.r, 200);
    CHECK_EQ(styles[btn].bg.g, 50);
  }

  free(styles);
  lk_theme_destroy(th);
  lk_tree_destroy(t);
  END_TEST();
}

static void test_tag_multiple_on_node(void) {
  lk_tree *t;
  lk_ix w, btn;

  BEGIN_TEST("tag: multiple tags on one node");

  t = lk_tree_create(NULL);
  w = lk_tree_add_node_s(t, lk_str_c("w"), UIK_WINDOW);
  btn = lk_tree_add_node_s(t, lk_str_c("btn"), UIK_BUTTON);
  lk_tree_set_root(t, w);
  lk_tree_append_child(t, w, btn);

  lk_tree_add_tag_s(t, btn, "primary");
  lk_tree_add_tag_s(t, btn, "large");

  CHECK(lk_tree_has_tag(t, btn, lk_intern_cid(t->intern, "primary")));
  CHECK(lk_tree_has_tag(t, btn, lk_intern_cid(t->intern, "large")));
  CHECK(!lk_tree_has_tag(t, btn, lk_intern_cid(t->intern, "small")));

  lk_tree_destroy(t);
  END_TEST();
}

/* ---- Phase 5: lk_ui owns theme + styles ---- */

static void test_ui_owns_default_theme(void) {
  lk_ui *ui;

  BEGIN_TEST("ui: owns default theme");

  ui = lk_ui_create(NULL);
  CHECK(ui != NULL);
  CHECK(lk_ui_theme(ui) != NULL);

  lk_ui_destroy(ui);
  END_TEST();
}

static void test_ui_resolve_styles_headless(void) {
  lk_ui *ui;
  lk_tree *tree;
  const lk_tree *cur;
  const lk_style *styles;
  lk_ix w, btn;

  BEGIN_TEST("ui: resolve_styles headless");

  ui = lk_ui_create(NULL);

  /* Build a frame */
  tree = lk_ui_begin_frame(ui);
  w = lk_tree_add_node_s(tree, lk_str_c("w"), UIK_WINDOW);
  btn = lk_tree_add_node_s(tree, lk_str_c("btn"), UIK_BUTTON);
  lk_tree_set_root(tree, w);
  lk_tree_append_child(tree, w, btn);
  lk_ui_end_frame(ui);
  cur = lk_ui_tree(ui);

  /* Before resolve, styles should be NULL */
  CHECK(lk_ui_styles(ui) == NULL);

  lk_ui_resolve_styles(ui);
  styles = lk_ui_styles(ui);
  CHECK(styles != NULL);

  /* Window bg should match default theme (30,30,30) */
  CHECK(styles[cur->root].bg.r == 30);
  CHECK(styles[cur->root].bg.g == 30);
  CHECK(styles[cur->root].bg.b == 30);

  /* Button bg should match default theme (60,60,60) */
  CHECK(styles[btn].bg.r == 60);
  CHECK(styles[btn].bg.g == 60);

  lk_ui_destroy(ui);
  END_TEST();
}

static void test_ui_set_theme_custom(void) {
  lk_ui *ui;
  lk_theme *th;
  lk_style s;
  lk_tree *tree;
  lk_ix w;
  const lk_style *styles;

  BEGIN_TEST("ui: set_theme with custom theme");

  ui = lk_ui_create(NULL);

  /* Create custom theme with red window bg */
  th = lk_theme_new(NULL, NULL, NULL);
  memset(&s, 0, sizeof(s));
  s.bg.r = 255;
  s.bg.g = 0;
  s.bg.b = 0;
  s.bg.a = 255;
  lk_theme_add_rule(th, UIK_WINDOW, 0, 0, &s, LK_SF_BG);

  lk_ui_set_theme(ui, th);
  CHECK(lk_ui_theme(ui) == th);

  /* Build a frame and resolve */
  tree = lk_ui_begin_frame(ui);
  w = lk_tree_add_node_s(tree, lk_str_c("w"), UIK_WINDOW);
  lk_tree_set_root(tree, w);
  lk_ui_end_frame(ui);

  lk_ui_resolve_styles(ui);
  styles = lk_ui_styles(ui);
  CHECK(styles != NULL);

  /* Window bg should be red from custom theme */
  CHECK(styles[w].bg.r == 255);
  CHECK(styles[w].bg.g == 0);
  CHECK(styles[w].bg.b == 0);

  lk_ui_destroy(ui);
  END_TEST();
}

static void test_ui_hover_state(void) {
  lk_ui *ui;
  lk_tree *tree;
  lk_ix w, btn;
  const lk_style *styles;
  lk_theme *th;
  lk_style s;

  BEGIN_TEST("ui: hover_set populates HOVERED node state");

  ui = lk_ui_create(NULL);

  /* Add a hover rule: hovered buttons get bg=(100,200,100) */
  th = lk_ui_theme(ui);
  memset(&s, 0, sizeof(s));
  s.bg.r = 100;
  s.bg.g = 200;
  s.bg.b = 100;
  s.bg.a = 255;
  lk_theme_add_rule(th, UIK_BUTTON, 0, LK_NSTATE_HOVERED, &s, LK_SF_BG);

  /* Build a frame */
  tree = lk_ui_begin_frame(ui);
  w = lk_tree_add_node_s(tree, lk_str_c("w"), UIK_WINDOW);
  btn = lk_tree_add_node_s(tree, lk_str_c("btn"), UIK_BUTTON);
  lk_tree_set_root(tree, w);
  lk_tree_append_child(tree, w, btn);
  lk_ui_end_frame(ui);

  /* No hover: button should have default bg (60,60,60) */
  lk_ui_resolve_styles(ui);
  styles = lk_ui_styles(ui);
  CHECK(styles[btn].bg.r == 60);
  CHECK(styles[btn].bg.g == 60);

  /* Set hover on button */
  lk_hover_set(ui, lk_ui_tree(ui)->nodes[btn].id);

  /* Rebuild same frame so resolve runs with hover state */
  tree = lk_ui_begin_frame(ui);
  w = lk_tree_add_node_s(tree, lk_str_c("w"), UIK_WINDOW);
  btn = lk_tree_add_node_s(tree, lk_str_c("btn"), UIK_BUTTON);
  lk_tree_set_root(tree, w);
  lk_tree_append_child(tree, w, btn);
  lk_ui_end_frame(ui);

  lk_ui_resolve_styles(ui);
  styles = lk_ui_styles(ui);
  CHECK(styles[btn].bg.r == 100);
  CHECK(styles[btn].bg.g == 200);
  CHECK(styles[btn].bg.b == 100);

  /* Clear hover: should revert */
  lk_hover_clear(ui);
  tree = lk_ui_begin_frame(ui);
  w = lk_tree_add_node_s(tree, lk_str_c("w"), UIK_WINDOW);
  btn = lk_tree_add_node_s(tree, lk_str_c("btn"), UIK_BUTTON);
  lk_tree_set_root(tree, w);
  lk_tree_append_child(tree, w, btn);
  lk_ui_end_frame(ui);

  lk_ui_resolve_styles(ui);
  styles = lk_ui_styles(ui);
  CHECK(styles[btn].bg.r == 60);
  CHECK(styles[btn].bg.g == 60);

  lk_ui_destroy(ui);
  END_TEST();
}

/* ================================================================
 * Additional style resolution tests
 * ================================================================ */

static void test_style_bg_does_not_inherit(void) {
  /* Parent (window) has bg=(30,30,30), child (column) should NOT inherit it */
  lk_tree *t;
  lk_theme *th;
  lk_style *styles;
  lk_ix w, col, lbl;

  BEGIN_TEST("style: bg does NOT inherit to children");

  t = lk_tree_create(NULL);
  w = lk_tree_add_node_s(t, lk_str_c("w"), UIK_WINDOW);
  col = lk_tree_add_node_s(t, lk_str_c("col"), UIK_COLUMN);
  lbl = lk_tree_add_node_s(t, lk_str_c("lbl"), UIK_LABEL);
  lk_tree_set_root(t, w);
  lk_tree_append_child(t, w, col);
  lk_tree_append_child(t, col, lbl);

  th = lk_theme_default(NULL, NULL, NULL);
  styles = (lk_style *)malloc(sizeof(lk_style) * t->node_count);

  if (th && styles) {
    lk_style_resolve(th, t, NULL, styles);
    /* window bg = (30,30,30,255) from default theme */
    CHECK_EQ(styles[w].bg.r, 30);
    CHECK_EQ(styles[w].bg.a, 255);
    /* column has no bg rule — should be zero (not inherited) */
    CHECK_EQ(styles[col].bg.r, 0);
    CHECK_EQ(styles[col].bg.g, 0);
    CHECK_EQ(styles[col].bg.b, 0);
    CHECK_EQ(styles[col].bg.a, 0);
    /* label also has no bg rule */
    CHECK_EQ(styles[lbl].bg.r, 0);
    CHECK_EQ(styles[lbl].bg.a, 0);
  }

  free(styles);
  lk_theme_destroy(th);
  lk_tree_destroy(t);
  END_TEST();
}

static void test_style_font_inherits(void) {
  /* Custom theme sets font on root; children should inherit it */
  lk_tree *t;
  lk_theme *th;
  lk_style *styles;
  lk_style s;
  lk_ix w, col, lbl;

  BEGIN_TEST("style: font_id and font_size inherit");

  t = lk_tree_create(NULL);
  w = lk_tree_add_node_s(t, lk_str_c("w"), UIK_WINDOW);
  col = lk_tree_add_node_s(t, lk_str_c("col"), UIK_COLUMN);
  lbl = lk_tree_add_node_s(t, lk_str_c("lbl"), UIK_LABEL);
  lk_tree_set_root(t, w);
  lk_tree_append_child(t, w, col);
  lk_tree_append_child(t, col, lbl);

  th = lk_theme_new(NULL, NULL, NULL);
  /* Set font on wildcard (all nodes) */
  memset(&s, 0, sizeof(s));
  s.font_id = 42;
  s.font_size = 18;
  lk_theme_add_rule(th, 0, 0, 0, &s, LK_SF_FONT_ID | LK_SF_FONT_SIZE);

  styles = (lk_style *)malloc(sizeof(lk_style) * t->node_count);

  if (styles) {
    lk_style_resolve(th, t, NULL, styles);
    /* All nodes should have font_id=42, font_size=18 */
    CHECK_EQ(styles[w].font_id, 42u);
    CHECK_EQ((unsigned)styles[w].font_size, 18u);
    CHECK_EQ(styles[col].font_id, 42u);
    CHECK_EQ((unsigned)styles[col].font_size, 18u);
    CHECK_EQ(styles[lbl].font_id, 42u);
    CHECK_EQ((unsigned)styles[lbl].font_size, 18u);
  }

  free(styles);
  lk_theme_destroy(th);
  lk_tree_destroy(t);
  END_TEST();
}

static void test_style_kind_no_cross_match(void) {
  /* BUTTON rule should NOT match a LABEL node */
  lk_tree *t;
  lk_theme *th;
  lk_style *styles;
  lk_style s;
  lk_ix w, btn, lbl;

  BEGIN_TEST("style: kind-specific rule doesn't cross-match");

  t = lk_tree_create(NULL);
  w = lk_tree_add_node_s(t, lk_str_c("w"), UIK_WINDOW);
  btn = lk_tree_add_node_s(t, lk_str_c("btn"), UIK_BUTTON);
  lbl = lk_tree_add_node_s(t, lk_str_c("lbl"), UIK_LABEL);
  lk_tree_set_root(t, w);
  lk_tree_append_child(t, w, btn);
  lk_tree_append_child(t, w, lbl);

  th = lk_theme_new(NULL, NULL, NULL);
  memset(&s, 0, sizeof(s));
  s.bg.r = 80;
  s.bg.a = 255;
  lk_theme_add_rule(th, UIK_BUTTON, 0, 0, &s, LK_SF_BG);

  styles = (lk_style *)malloc(sizeof(lk_style) * t->node_count);

  if (styles) {
    lk_style_resolve(th, t, NULL, styles);
    /* button gets the bg */
    CHECK_EQ(styles[btn].bg.r, 80);
    CHECK_EQ(styles[btn].bg.a, 255);
    /* label does NOT */
    CHECK_EQ(styles[lbl].bg.r, 0);
    CHECK_EQ(styles[lbl].bg.a, 0);
  }

  free(styles);
  lk_theme_destroy(th);
  lk_tree_destroy(t);
  END_TEST();
}

static void test_style_state_requires_all_bits(void) {
  /* Rule with state=FOCUSED|HOVERED only matches when BOTH are set */
  lk_tree *t;
  lk_theme *th;
  lk_style *styles;
  lk_u8 *nstates;
  lk_style s;
  lk_ix w, btn;

  BEGIN_TEST("style: state mask requires all bits set");

  t = lk_tree_create(NULL);
  w = lk_tree_add_node_s(t, lk_str_c("w"), UIK_WINDOW);
  btn = lk_tree_add_node_s(t, lk_str_c("btn"), UIK_BUTTON);
  lk_tree_set_root(t, w);
  lk_tree_append_child(t, w, btn);

  th = lk_theme_new(NULL, NULL, NULL);
  /* base rule */
  memset(&s, 0, sizeof(s));
  s.bg.r = 50;
  s.bg.a = 255;
  lk_theme_add_rule(th, UIK_BUTTON, 0, 0, &s, LK_SF_BG);
  /* rule requiring both focused AND hovered */
  memset(&s, 0, sizeof(s));
  s.bg.r = 200;
  s.bg.a = 255;
  lk_theme_add_rule(th, UIK_BUTTON, 0,
                    LK_NSTATE_FOCUSED | LK_NSTATE_HOVERED,
                    &s, LK_SF_BG);

  styles = (lk_style *)malloc(sizeof(lk_style) * t->node_count);
  nstates = (lk_u8 *)malloc(sizeof(lk_u8) * t->node_count);

  if (styles && nstates) {
    /* Only FOCUSED — combined rule should NOT match */
    memset(nstates, 0, sizeof(lk_u8) * t->node_count);
    nstates[btn] = LK_NSTATE_FOCUSED;
    lk_style_resolve(th, t, nstates, styles);
    CHECK_EQ(styles[btn].bg.r, 50);

    /* Both FOCUSED|HOVERED — combined rule SHOULD match */
    nstates[btn] = LK_NSTATE_FOCUSED | LK_NSTATE_HOVERED;
    lk_style_resolve(th, t, nstates, styles);
    CHECK_EQ(styles[btn].bg.r, 200);
  }

  free(nstates);
  free(styles);
  lk_theme_destroy(th);
  lk_tree_destroy(t);
  END_TEST();
}

static void test_style_tag_no_match_untagged(void) {
  /* Tag rule should NOT apply to untagged nodes of same kind */
  lk_tree *t;
  lk_theme *th;
  lk_style *styles;
  lk_style s;
  lk_ix w, btn_tagged, btn_plain;

  BEGIN_TEST("style: tag rule skips untagged node");

  t = lk_tree_create(NULL);
  w = lk_tree_add_node_s(t, lk_str_c("w"), UIK_WINDOW);
  btn_tagged = lk_tree_add_node_s(t, lk_str_c("t"), UIK_BUTTON);
  btn_plain = lk_tree_add_node_s(t, lk_str_c("p"), UIK_BUTTON);
  lk_tree_set_root(t, w);
  lk_tree_append_child(t, w, btn_tagged);
  lk_tree_append_child(t, w, btn_plain);
  lk_tree_add_tag_s(t, btn_tagged, "primary");

  th = lk_theme_new(NULL, NULL, NULL);
  /* base button */
  memset(&s, 0, sizeof(s));
  s.bg.r = 60;
  s.bg.a = 255;
  lk_theme_add_rule(th, UIK_BUTTON, 0, 0, &s, LK_SF_BG);
  /* primary button — only matches tagged */
  memset(&s, 0, sizeof(s));
  s.bg.r = 150;
  s.bg.a = 255;
  lk_theme_add_rule(th, UIK_BUTTON,
                    lk_intern_cid(t->intern, "primary"),
                    0, &s, LK_SF_BG);

  styles = (lk_style *)malloc(sizeof(lk_style) * t->node_count);

  if (styles) {
    lk_style_resolve(th, t, NULL, styles);
    CHECK_EQ(styles[btn_tagged].bg.r, 150);
    CHECK_EQ(styles[btn_plain].bg.r, 60);
  }

  free(styles);
  lk_theme_destroy(th);
  lk_tree_destroy(t);
  END_TEST();
}

static void test_style_gap_prop_override(void) {
  /* Tree prop UIP_GAP overrides style gap */
  lk_tree *t;
  lk_theme *th;
  lk_style *styles;
  lk_style s;
  lk_ix w, col;

  BEGIN_TEST("style: gap tree prop overrides theme");

  t = lk_tree_create(NULL);
  w = lk_tree_add_node_s(t, lk_str_c("w"), UIK_WINDOW);
  col = lk_tree_add_node_s(t, lk_str_c("col"), UIK_COLUMN);
  lk_tree_add_prop(t, col, UIP_GAP, lk_v_i32(15));
  lk_tree_set_root(t, w);
  lk_tree_append_child(t, w, col);

  th = lk_theme_new(NULL, NULL, NULL);
  memset(&s, 0, sizeof(s));
  s.gap = 8;
  lk_theme_add_rule(th, UIK_COLUMN, 0, 0, &s, LK_SF_GAP);

  styles = (lk_style *)malloc(sizeof(lk_style) * t->node_count);

  if (styles) {
    lk_style_resolve(th, t, NULL, styles);
    CHECK_EQ((unsigned)styles[col].gap, 15u); /* tree prop wins */
  }

  free(styles);
  lk_theme_destroy(th);
  lk_tree_destroy(t);
  END_TEST();
}

static void test_style_align_justify_prop_override(void) {
  /* Tree props UIP_ALIGN and UIP_JUSTIFY override style values */
  lk_tree *t;
  lk_theme *th;
  lk_style *styles;
  lk_style s;
  lk_ix w, col;

  BEGIN_TEST("style: align/justify tree props override theme");

  t = lk_tree_create(NULL);
  w = lk_tree_add_node_s(t, lk_str_c("w"), UIK_WINDOW);
  col = lk_tree_add_node_s(t, lk_str_c("col"), UIK_COLUMN);
  lk_tree_add_prop(t, col, UIP_ALIGN, lk_v_i32(LK_ALIGN_CENTER));
  lk_tree_add_prop(t, col, UIP_JUSTIFY, lk_v_i32(LK_ALIGN_END));
  lk_tree_set_root(t, w);
  lk_tree_append_child(t, w, col);

  th = lk_theme_new(NULL, NULL, NULL);
  memset(&s, 0, sizeof(s));
  s.align = LK_ALIGN_START;
  s.justify = LK_ALIGN_START;
  lk_theme_add_rule(th, UIK_COLUMN, 0, 0, &s,
                    LK_SF_ALIGN | LK_SF_JUSTIFY);

  styles = (lk_style *)malloc(sizeof(lk_style) * t->node_count);

  if (styles) {
    lk_style_resolve(th, t, NULL, styles);
    CHECK_EQ((unsigned)styles[col].align, (unsigned)LK_ALIGN_CENTER);
    CHECK_EQ((unsigned)styles[col].justify, (unsigned)LK_ALIGN_END);
  }

  free(styles);
  lk_theme_destroy(th);
  lk_tree_destroy(t);
  END_TEST();
}

static void test_render_uses_style_colors(void) {
  /* Verify render list FILL_RECT uses bg from resolved style */
  lk_tree *t;
  lk_theme *th;
  lk_style *styles;
  lk_style s;
  lk_layout_cfg cfg;
  lk_rect *rects;
  lk_render_list rl;
  lk_ix w, btn;
  lk_u32 i;
  int found_btn_fill;

  BEGIN_TEST("render: uses style fg/bg colors");

  t = lk_tree_create(NULL);
  w = lk_tree_add_node_s(t, lk_str_c("w"), UIK_WINDOW);
  btn = lk_tree_add_node_s(t, lk_str_c("btn"), UIK_BUTTON);
  lk_tree_add_prop(t, btn, UIP_TEXT, lk_v_cstr(t->intern, "X"));
  lk_tree_set_root(t, w);
  lk_tree_append_child(t, w, btn);

  /* Custom theme: button bg = (0, 200, 100, 255) */
  th = lk_theme_new(NULL, NULL, NULL);
  memset(&s, 0, sizeof(s));
  s.bg.r = 0;
  s.bg.g = 200;
  s.bg.b = 100;
  s.bg.a = 255;
  s.padding = 4;
  lk_theme_add_rule(th, UIK_BUTTON, 0, 0, &s,
                    LK_SF_BG | LK_SF_PADDING);
  /* fg for text */
  memset(&s, 0, sizeof(s));
  s.fg.r = 255;
  s.fg.g = 255;
  s.fg.b = 0;
  s.fg.a = 255;
  lk_theme_add_rule(th, 0, 0, 0, &s, LK_SF_FG);

  styles = (lk_style *)malloc(sizeof(lk_style) * t->node_count);
  rects = (lk_rect *)malloc(sizeof(lk_rect) * t->node_count);

  if (styles && rects) {
    lk_style_resolve(th, t, NULL, styles);

    memset(&cfg, 0, sizeof(cfg));
    cfg.text = lk_text_backend_stub();
    cfg.viewport_w = 800;
    cfg.viewport_h = 600;
    cfg.styles = styles;
    lk_layout(t, &cfg, rects);

    memset(&rl, 0, sizeof(rl));
    lk_render_build(t, rects, styles, NULL, &rl);

    /* Find the button FILL_RECT — should have our custom green bg */
    found_btn_fill = 0;
    for (i = 0; i < rl.count; i++) {
      if (rl.cmds[i].op == LK_ROP_FILL_RECT &&
          rl.cmds[i].color.g == 200 &&
          rl.cmds[i].color.b == 100) {
        found_btn_fill = 1;
        break;
      }
    }
    CHECK(found_btn_fill);

    lk_render_list_destroy(&rl);
  }

  free(rects);
  free(styles);
  lk_theme_destroy(th);
  lk_tree_destroy(t);
  END_TEST();
}

static void test_layout_style_gap(void) {
  /* Theme sets gap=20 on column; verify child positions reflect it */
  lk_tree *t;
  lk_theme *th;
  lk_style *styles;
  lk_style s;
  lk_layout_cfg cfg;
  lk_rect *rects;
  lk_ix w, col, lbl1, lbl2;

  BEGIN_TEST("layout: style gap affects child positions");

  t = lk_tree_create(NULL);
  w = lk_tree_add_node_s(t, lk_str_c("w"), UIK_WINDOW);
  col = lk_tree_add_node_s(t, lk_str_c("col"), UIK_COLUMN);
  lbl1 = lk_tree_add_node_s(t, lk_str_c("l1"), UIK_LABEL);
  lbl2 = lk_tree_add_node_s(t, lk_str_c("l2"), UIK_LABEL);
  lk_tree_add_prop(t, lbl1, UIP_TEXT, lk_v_cstr(t->intern, "A"));
  lk_tree_add_prop(t, lbl2, UIP_TEXT, lk_v_cstr(t->intern, "B"));
  lk_tree_set_root(t, w);
  lk_tree_append_child(t, w, col);
  lk_tree_append_child(t, col, lbl1);
  lk_tree_append_child(t, col, lbl2);

  th = lk_theme_new(NULL, NULL, NULL);
  memset(&s, 0, sizeof(s));
  s.gap = 20;
  lk_theme_add_rule(th, UIK_COLUMN, 0, 0, &s, LK_SF_GAP);

  styles = (lk_style *)malloc(sizeof(lk_style) * t->node_count);
  rects = (lk_rect *)malloc(sizeof(lk_rect) * t->node_count);

  if (styles && rects) {
    lk_style_resolve(th, t, NULL, styles);

    memset(&cfg, 0, sizeof(cfg));
    cfg.text = lk_text_backend_stub();
    cfg.viewport_w = 800;
    cfg.viewport_h = 600;
    cfg.styles = styles;
    lk_layout(t, &cfg, rects);

    /* With stub text measurer: label height = 16.
     * lbl2.y should be lbl1.y + lbl1.h + gap(20) = 0 + 16 + 20 = 36 */
    CHECK_EQ((unsigned)rects[lbl1].y, 0u);
    CHECK_EQ((unsigned)rects[lbl2].y, (unsigned)(rects[lbl1].y +
                                                  rects[lbl1].h + 20));
  }

  free(rects);
  free(styles);
  lk_theme_destroy(th);
  lk_tree_destroy(t);
  END_TEST();
}

static void test_style_wildcard_matches_all(void) {
  /* A wildcard rule (kind=0) should match every node kind */
  lk_tree *t;
  lk_theme *th;
  lk_style *styles;
  lk_style s;
  lk_ix w, col, lbl, btn, sp;

  BEGIN_TEST("style: wildcard rule matches all kinds");

  t = lk_tree_create(NULL);
  w = lk_tree_add_node_s(t, lk_str_c("w"), UIK_WINDOW);
  col = lk_tree_add_node_s(t, lk_str_c("c"), UIK_COLUMN);
  lbl = lk_tree_add_node_s(t, lk_str_c("l"), UIK_LABEL);
  btn = lk_tree_add_node_s(t, lk_str_c("b"), UIK_BUTTON);
  sp = lk_tree_add_node_s(t, lk_str_c("s"), UIK_SPACER);
  lk_tree_set_root(t, w);
  lk_tree_append_child(t, w, col);
  lk_tree_append_child(t, col, lbl);
  lk_tree_append_child(t, col, btn);
  lk_tree_append_child(t, col, sp);

  th = lk_theme_new(NULL, NULL, NULL);
  memset(&s, 0, sizeof(s));
  s.fg.r = 123;
  s.fg.a = 255;
  lk_theme_add_rule(th, 0, 0, 0, &s, LK_SF_FG);

  styles = (lk_style *)malloc(sizeof(lk_style) * t->node_count);

  if (styles) {
    lk_style_resolve(th, t, NULL, styles);
    CHECK_EQ(styles[w].fg.r, 123);
    CHECK_EQ(styles[col].fg.r, 123);
    CHECK_EQ(styles[lbl].fg.r, 123);
    CHECK_EQ(styles[btn].fg.r, 123);
    CHECK_EQ(styles[sp].fg.r, 123);
  }

  free(styles);
  lk_theme_destroy(th);
  lk_tree_destroy(t);
  END_TEST();
}

/* ================================================================
 * Tests: deferred prop append (lazy props_off)
 * ================================================================ */

/* Create all nodes first, then add props grouped per node.
 * This is the pattern used by Lcl scripts (hello.lcl). */
static void test_prop_deferred_all_nodes_first(void) {
  lk_tree *t = lk_tree_create(NULL);
  lk_ix w, col, lbl, btn;

  BEGIN_TEST("prop: all nodes first, then grouped props");

  w = lk_tree_add_node_s(t, lk_str_c("w"), UIK_WINDOW);
  col = lk_tree_add_node_s(t, lk_str_c("col"), UIK_COLUMN);
  lbl = lk_tree_add_node_s(t, lk_str_c("lbl"), UIK_LABEL);
  btn = lk_tree_add_node_s(t, lk_str_c("btn"), UIK_BUTTON);

  /* props grouped per node — not interleaved */
  lk_tree_add_prop(t, col, UIP_PADDING, lk_v_i32(10));
  lk_tree_add_prop(t, col, UIP_GAP, lk_v_i32(5));
  lk_tree_add_prop(t, lbl, UIP_TEXT, lk_v_cstr(t->intern, "Hello"));
  lk_tree_add_prop(t, btn, UIP_TEXT, lk_v_cstr(t->intern, "OK"));
  lk_tree_add_prop(t, btn, UIP_PADDING, lk_v_i32(8));

  lk_tree_set_root(t, w);
  lk_tree_append_child(t, w, col);
  lk_tree_append_child(t, col, lbl);
  lk_tree_append_child(t, col, btn);

  CHECK_EQ((unsigned)lk_node_prop_i32(t, col, UIP_PADDING, 0), 10u);
  CHECK_EQ((unsigned)lk_node_prop_i32(t, col, UIP_GAP, 0), 5u);
  CHECK(lk_str_cmp(lk_node_text(t, lbl), lk_str_c("Hello")));
  CHECK(lk_str_cmp(lk_node_text(t, btn), lk_str_c("OK")));
  CHECK_EQ((unsigned)lk_node_prop_i32(t, btn, UIP_PADDING, 0), 8u);

  lk_tree_destroy(t);
  END_TEST();
}

/* Interleaved props for different nodes: second add to node A after
 * adding props to node B should be silently dropped. */
static void test_prop_interleave_drops(void) {
  lk_tree *t = lk_tree_create(NULL);
  lk_ix w, col;

  BEGIN_TEST("prop: interleaved props for different nodes drop");

  w = lk_tree_add_node_s(t, lk_str_c("w"), UIK_WINDOW);
  col = lk_tree_add_node_s(t, lk_str_c("col"), UIK_COLUMN);

  lk_tree_add_prop(t, col, UIP_PADDING, lk_v_i32(10));
  lk_tree_add_prop(t, w, UIP_PADDING, lk_v_i32(5));
  /* Now try adding a second prop to col — arena tail belongs to w */
  lk_tree_add_prop(t, col, UIP_GAP, lk_v_i32(8));

  CHECK_EQ((unsigned)lk_node_prop_i32(t, col, UIP_PADDING, 0), 10u);
  CHECK_EQ((unsigned)lk_node_prop_i32(t, w, UIP_PADDING, 0), 5u);
  /* col's GAP was dropped — interleaving */
  CHECK_EQ((unsigned)lk_node_prop_i32(t, col, UIP_GAP, 0), 0u);

  lk_tree_destroy(t);
  END_TEST();
}

/* End-to-end: build tree like hello.lcl (all nodes first), then
 * verify render list contains DRAW_TEXT commands. */
static void test_render_deferred_props_emit_text(void) {
  lk_tree *t = lk_tree_create(NULL);
  lk_ix w, col, lbl, btn;
  lk_rect *r;
  lk_render_list rl;
  lk_u32 text_count;
  lk_u32 i;

  BEGIN_TEST("render: deferred props produce DRAW_TEXT");

  w = lk_tree_add_node_s(t, lk_str_c("w"), UIK_WINDOW);
  col = lk_tree_add_node_s(t, lk_str_c("col"), UIK_COLUMN);
  lbl = lk_tree_add_node_s(t, lk_str_c("lbl"), UIK_LABEL);
  btn = lk_tree_add_node_s(t, lk_str_c("btn"), UIK_BUTTON);

  /* all props after all nodes — the hello.lcl pattern */
  lk_tree_add_prop(t, col, UIP_PADDING, lk_v_i32(10));
  lk_tree_add_prop(t, lbl, UIP_TEXT, lk_v_cstr(t->intern, "Title"));
  lk_tree_add_prop(t, btn, UIP_TEXT, lk_v_cstr(t->intern, "Go"));
  lk_tree_add_prop(t, btn, UIP_PADDING, lk_v_i32(4));

  lk_tree_set_root(t, w);
  lk_tree_append_child(t, w, col);
  lk_tree_append_child(t, col, lbl);
  lk_tree_append_child(t, col, btn);

  r = run_layout(t, 800, 600);
  CHECK(r != NULL);
  if (r) {
    memset(&rl, 0, sizeof(rl));
    lk_render_build(t, r, NULL, NULL, &rl);

    text_count = 0;
    for (i = 0; i < rl.count; i++) {
      if (rl.cmds[i].op == LK_ROP_DRAW_TEXT) {
        text_count++;
      }
    }
    /* label text + button text = 2 DRAW_TEXT commands */
    CHECK_EQ(text_count, 2u);

    lk_render_list_destroy(&rl);
    free(r);
  }

  lk_tree_destroy(t);
  END_TEST();
}

/* ================================================================
 * Tests: text input widget
 * ================================================================ */

/* Helper: build a UI with a focused text input, run one frame, return
 * the current tree.  Caller must destroy ui.
 */
static lk_ui *make_text_input_ui(const char *initial_text,
                                  lk_ix *out_ti) {
  lk_ui *ui = lk_ui_create(NULL);
  lk_tree *t;
  lk_ix w, ti;
  const lk_tree *cur;
  lk_node_id ti_id;

  t = lk_ui_begin_frame(ui);
  w = lk_tree_add_node_s(t, lk_str_c("w"), UIK_WINDOW);
  ti = lk_tree_add_node_s(t, lk_str_c("ti"), UIK_TEXT_INPUT);
  lk_tree_add_prop(t, ti, UIP_TEXT,
                   lk_v_cstr(t->intern, initial_text));
  lk_tree_add_prop(t, ti, UIP_FOCUSABLE, lk_v_bool(1));
  lk_tree_set_root(t, w);
  lk_tree_append_child(t, w, ti);
  lk_ui_end_frame(ui);

  cur = lk_ui_tree(ui);
  ti_id = lk_intern_id(ui->intern, lk_str_c("ti"));
  lk_focus_set(ui, cur, ti_id);

  *out_ti = lk_tree_find_by_id(cur, ti_id);
  return ui;
}

static void test_text_input_measure(void) {
  lk_tree *t = lk_tree_create(NULL);
  lk_ix w, col, ti;
  lk_rect *r;

  BEGIN_TEST("text_input: measure includes padding + min width");

  w = lk_tree_add_node_s(t, lk_str_c("w"), UIK_WINDOW);
  col = lk_tree_add_node_s(t, lk_str_c("col"), UIK_COLUMN);
  ti = lk_tree_add_node_s(t, lk_str_c("ti"), UIK_TEXT_INPUT);
  lk_tree_add_prop(t, ti, UIP_TEXT, lk_v_cstr(t->intern, ""));
  lk_tree_add_prop(t, ti, UIP_PADDING, lk_v_i32(4));
  lk_tree_set_root(t, w);
  lk_tree_append_child(t, w, col);
  lk_tree_append_child(t, col, ti);

  r = run_layout(t, 800, 600);
  CHECK(r != NULL);
  if (r) {
    /* Column stretches cross-axis (width) but uses intrinsic main-axis (h).
     * Empty text, min width 100 + padding 4*2 = 108. Width stretched to 800.
     * Height: 16 (stub text height) + 8 (padding*2) = 24
     */
    CHECK(r[ti].w >= 108);
    CHECK_EQ((unsigned)r[ti].h, 24u);
    free(r);
  }

  END_TEST();
  lk_tree_destroy(t);
}

static void test_text_input_render(void) {
  lk_tree *t = lk_tree_create(NULL);
  lk_ix w, ti;
  lk_rect *r;
  lk_render_list rl;

  BEGIN_TEST("text_input: render emits FILL_RECT + DRAW_TEXT");

  w = lk_tree_add_node_s(t, lk_str_c("w"), UIK_WINDOW);
  ti = lk_tree_add_node_s(t, lk_str_c("ti"), UIK_TEXT_INPUT);
  lk_tree_add_prop(t, ti, UIP_TEXT, lk_v_cstr(t->intern, "hello"));
  lk_tree_set_root(t, w);
  lk_tree_append_child(t, w, ti);

  r = run_layout(t, 800, 600);
  CHECK(r != NULL);
  if (r) {
    memset(&rl, 0, sizeof(rl));
    lk_render_build(t, r, NULL, NULL, &rl);
    /* window: FILL + CLIP_BEGIN, text_input: FILL + TEXT, window: CLIP_END */
    /* => at least 5 ops */
    CHECK(rl.count >= 4u);
    /* text_input FILL_RECT */
    CHECK_EQ((unsigned)rl.cmds[2].op, (unsigned)LK_ROP_FILL_RECT);
    /* text_input DRAW_TEXT */
    CHECK_EQ((unsigned)rl.cmds[3].op, (unsigned)LK_ROP_DRAW_TEXT);
    lk_render_list_destroy(&rl);
    free(r);
  }

  END_TEST();
  lk_tree_destroy(t);
}

static void test_text_input_insert(void) {
  lk_ui *ui;
  lk_ix ti;
  lk_event ev;
  lk_state *st;
  lk_node_id ti_id;
  lk_value v;
  lk_str text;

  BEGIN_TEST("text_input: TEXT event inserts, cursor advances");

  ui = make_text_input_ui("ab", &ti);
  st = lk_ui_state(ui);
  ti_id = lk_intern_id(ui->intern, lk_str_c("ti"));

  /* Set cursor at position 1 (between a and b) */
  lk_state_set(st, ti_id, LKS_CURSOR_POS, lk_v_i32(1));

  /* Send TEXT event "x" */
  memset(&ev, 0, sizeof(ev));
  ev.type = LK_EVENT_TEXT;
  ev.target = ti;
  ev.data.text.buf[0] = 'x';
  ev.data.text.len = 1;
  lk_event_route(ui, &ev);

  CHECK_EQ((unsigned)ev.handled, 1u);

  /* Text should be "axb" */
  v = lk_state_get(st, ti_id, LKS_TEXT_BUF);
  CHECK_EQ((unsigned)v.tag, (unsigned)UIV_STR);
  text = lk_intern_str(ui->intern, v.as.str_id);
  CHECK_EQ(text.len, 3u);
  CHECK(memcmp(text.ptr, "axb", 3) == 0);

  /* Cursor should be at 2 */
  v = lk_state_get(st, ti_id, LKS_CURSOR_POS);
  CHECK_EQ((unsigned)v.as.i, 2u);

  END_TEST();
  lk_ui_destroy(ui);
}

static void test_text_input_emits_value_changed(void) {
  lk_ui *ui;
  lk_ix ti;
  lk_event ev;
  lk_state *st;
  lk_node_id ti_id;
  lk_value v;
  const lk_command_queue *q;

  BEGIN_TEST("text_input: insert emits VALUE_CHANGED + command");

  ui = make_text_input_ui("ab", &ti);
  st = lk_ui_state(ui);
  ti_id = lk_intern_id(ui->intern, lk_str_c("ti"));

  /* Translator: value_changed + ptype "field" -> "FieldEdit" */
  lk_ui_add_translator_s(ui, LK_EVENT_VALUE_CHANGED, "field", 0, 0, 0,
                          "FieldEdit");

  /* Attach presentation (arg = the field id) to text_input */
  {
    lk_value pvs[1];
    pvs[0] = lk_v_cstr(ui->intern, "amount");
    lk_tree_add_presentation_sv(ui->next, ti, "field", pvs, 1);
    /* The presentation must be on current tree — we added after end_frame,
     * so re-run a frame to carry it through. */
  }

  /* Rebuild the tree to carry the presentation through the diff. */
  {
    lk_tree *t = lk_ui_begin_frame(ui);
    lk_ix w = lk_tree_add_node_s(t, lk_str_c("w"), UIK_WINDOW);
    lk_ix ti2 = lk_tree_add_node_s(t, lk_str_c("ti"), UIK_TEXT_INPUT);
    lk_value pvs[1];
    lk_tree_add_prop(t, ti2, UIP_TEXT, lk_v_cstr(t->intern, "ab"));
    lk_tree_add_prop(t, ti2, UIP_FOCUSABLE, lk_v_bool(1));
    pvs[0] = lk_v_cstr(t->intern, "amount");
    lk_tree_add_presentation_sv(t, ti2, "field", pvs, 1);
    lk_tree_set_root(t, w);
    lk_tree_append_child(t, w, ti2);
    lk_ui_end_frame(ui);
  }
  ti = lk_tree_find_by_id(lk_ui_tree(ui),
                           lk_intern_id(ui->intern, lk_str_c("ti")));

  /* Re-set cursor (state is preserved but rebuild can reset it) */
  lk_state_set(st, ti_id, LKS_CURSOR_POS, lk_v_i32(1));

  /* Send TEXT event "x" */
  memset(&ev, 0, sizeof(ev));
  ev.type = LK_EVENT_TEXT;
  ev.target = ti;
  ev.data.text.buf[0] = 'x';
  ev.data.text.len = 1;
  lk_event_route(ui, &ev);

  /* Buffer should be "axb" */
  v = lk_state_get(st, ti_id, LKS_TEXT_BUF);
  CHECK_EQ((unsigned)v.tag, (unsigned)UIV_STR);

  /* Command queue should contain FieldEdit with args=["amount"] and
   * source_value = the new buffer "axb". */
  q = lk_ui_commands(ui);
  CHECK_EQ(q->count, 1);
  if (q->count >= 1) {
    const lk_command *cmd = &q->cmds[0];
    CHECK_EQ(cmd->name, lk_intern_id(ui->intern, lk_str_c("FieldEdit")));
    CHECK_EQ(cmd->arg_count, 1);
    CHECK_EQ(cmd->args[0].tag, UIV_STR);
    CHECK_EQ(cmd->source_value.tag, UIV_STR);
    {
      lk_str sv = lk_intern_str(ui->intern, cmd->source_value.as.str_id);
      CHECK_EQ(sv.len, 3u);
      CHECK(memcmp(sv.ptr, "axb", 3) == 0);
    }
    {
      lk_str av = lk_intern_str(ui->intern, cmd->args[0].as.str_id);
      CHECK(av.len == 6 && memcmp(av.ptr, "amount", 6) == 0);
    }
  }

  END_TEST();
  lk_ui_destroy(ui);
}

static void test_text_input_backspace_emits_value_changed(void) {
  lk_ui *ui;
  lk_ix ti;
  lk_event ev;
  lk_state *st;
  lk_node_id ti_id;
  const lk_command_queue *q;

  BEGIN_TEST("text_input: backspace emits VALUE_CHANGED");

  ui = make_text_input_ui("hi", &ti);
  st = lk_ui_state(ui);
  ti_id = lk_intern_id(ui->intern, lk_str_c("ti"));

  lk_ui_add_translator_s(ui, LK_EVENT_VALUE_CHANGED, "", 0, 0, 0, "Edited");

  {
    lk_tree *t = lk_ui_begin_frame(ui);
    lk_ix w = lk_tree_add_node_s(t, lk_str_c("w"), UIK_WINDOW);
    lk_ix ti2 = lk_tree_add_node_s(t, lk_str_c("ti"), UIK_TEXT_INPUT);
    lk_tree_add_prop(t, ti2, UIP_TEXT, lk_v_cstr(t->intern, "hi"));
    lk_tree_add_prop(t, ti2, UIP_FOCUSABLE, lk_v_bool(1));
    lk_tree_add_presentation_s(t, ti2, "whatever", lk_v_i32(0));
    lk_tree_set_root(t, w);
    lk_tree_append_child(t, w, ti2);
    lk_ui_end_frame(ui);
  }
  ti = lk_tree_find_by_id(lk_ui_tree(ui),
                           lk_intern_id(ui->intern, lk_str_c("ti")));
  lk_state_set(st, ti_id, LKS_CURSOR_POS, lk_v_i32(2));

  memset(&ev, 0, sizeof(ev));
  ev.type = LK_EVENT_KEY_DOWN;
  ev.target = ti;
  ev.data.key.keycode = LKK_BACKSPACE;
  lk_event_route(ui, &ev);

  q = lk_ui_commands(ui);
  CHECK_EQ(q->count, 1);
  if (q->count >= 1) {
    const lk_command *cmd = &q->cmds[0];
    CHECK_EQ(cmd->source_value.tag, UIV_STR);
    {
      lk_str sv = lk_intern_str(ui->intern, cmd->source_value.as.str_id);
      CHECK(sv.len == 1 && sv.ptr[0] == 'h');
    }
  }

  END_TEST();
  lk_ui_destroy(ui);
}

static void test_text_input_backspace(void) {
  lk_ui *ui;
  lk_ix ti;
  lk_event ev;
  lk_state *st;
  lk_node_id ti_id;
  lk_value v;
  lk_str text;

  BEGIN_TEST("text_input: BACKSPACE deletes before cursor");

  ui = make_text_input_ui("hello", &ti);
  st = lk_ui_state(ui);
  ti_id = lk_intern_id(ui->intern, lk_str_c("ti"));

  /* Set cursor at position 3 */
  lk_state_set(st, ti_id, LKS_CURSOR_POS, lk_v_i32(3));

  /* Send BACKSPACE */
  memset(&ev, 0, sizeof(ev));
  ev.type = LK_EVENT_KEY_DOWN;
  ev.target = ti;
  ev.data.key.keycode = LKK_BACKSPACE;
  lk_event_route(ui, &ev);

  CHECK_EQ((unsigned)ev.handled, 1u);

  /* Text should be "helo" */
  v = lk_state_get(st, ti_id, LKS_TEXT_BUF);
  text = lk_intern_str(ui->intern, v.as.str_id);
  CHECK_EQ(text.len, 4u);
  CHECK(memcmp(text.ptr, "helo", 4) == 0);

  /* Cursor should be at 2 */
  v = lk_state_get(st, ti_id, LKS_CURSOR_POS);
  CHECK_EQ((unsigned)v.as.i, 2u);

  END_TEST();
  lk_ui_destroy(ui);
}

static void test_text_input_delete(void) {
  lk_ui *ui;
  lk_ix ti;
  lk_event ev;
  lk_state *st;
  lk_node_id ti_id;
  lk_value v;
  lk_str text;

  BEGIN_TEST("text_input: DELETE deletes after cursor");

  ui = make_text_input_ui("hello", &ti);
  st = lk_ui_state(ui);
  ti_id = lk_intern_id(ui->intern, lk_str_c("ti"));

  /* Set cursor at position 2 */
  lk_state_set(st, ti_id, LKS_CURSOR_POS, lk_v_i32(2));

  /* Send DELETE */
  memset(&ev, 0, sizeof(ev));
  ev.type = LK_EVENT_KEY_DOWN;
  ev.target = ti;
  ev.data.key.keycode = LKK_DELETE;
  lk_event_route(ui, &ev);

  CHECK_EQ((unsigned)ev.handled, 1u);

  /* Text should be "helo" */
  v = lk_state_get(st, ti_id, LKS_TEXT_BUF);
  text = lk_intern_str(ui->intern, v.as.str_id);
  CHECK_EQ(text.len, 4u);
  CHECK(memcmp(text.ptr, "helo", 4) == 0);

  /* Cursor should stay at 2 */
  v = lk_state_get(st, ti_id, LKS_CURSOR_POS);
  CHECK_EQ((unsigned)v.as.i, 2u);

  END_TEST();
  lk_ui_destroy(ui);
}

static void test_text_input_cursor_movement(void) {
  lk_ui *ui;
  lk_ix ti;
  lk_event ev;
  lk_state *st;
  lk_node_id ti_id;
  lk_value v;

  BEGIN_TEST("text_input: LEFT/RIGHT/HOME/END move cursor");

  ui = make_text_input_ui("abcde", &ti);
  st = lk_ui_state(ui);
  ti_id = lk_intern_id(ui->intern, lk_str_c("ti"));

  /* Set cursor at position 3 */
  lk_state_set(st, ti_id, LKS_CURSOR_POS, lk_v_i32(3));

  /* LEFT -> 2 */
  memset(&ev, 0, sizeof(ev));
  ev.type = LK_EVENT_KEY_DOWN;
  ev.target = ti;
  ev.data.key.keycode = LKK_LEFT;
  lk_event_route(ui, &ev);
  v = lk_state_get(st, ti_id, LKS_CURSOR_POS);
  CHECK_EQ((unsigned)v.as.i, 2u);

  /* RIGHT -> 3 */
  memset(&ev, 0, sizeof(ev));
  ev.type = LK_EVENT_KEY_DOWN;
  ev.target = ti;
  ev.data.key.keycode = LKK_RIGHT;
  lk_event_route(ui, &ev);
  v = lk_state_get(st, ti_id, LKS_CURSOR_POS);
  CHECK_EQ((unsigned)v.as.i, 3u);

  /* HOME -> 0 */
  memset(&ev, 0, sizeof(ev));
  ev.type = LK_EVENT_KEY_DOWN;
  ev.target = ti;
  ev.data.key.keycode = LKK_HOME;
  lk_event_route(ui, &ev);
  v = lk_state_get(st, ti_id, LKS_CURSOR_POS);
  CHECK_EQ((unsigned)v.as.i, 0u);

  /* END -> 5 */
  memset(&ev, 0, sizeof(ev));
  ev.type = LK_EVENT_KEY_DOWN;
  ev.target = ti;
  ev.data.key.keycode = LKK_END;
  lk_event_route(ui, &ev);
  v = lk_state_get(st, ti_id, LKS_CURSOR_POS);
  CHECK_EQ((unsigned)v.as.i, 5u);

  END_TEST();
  lk_ui_destroy(ui);
}

static void test_text_input_selection(void) {
  lk_ui *ui;
  lk_ix ti;
  lk_event ev;
  lk_state *st;
  lk_node_id ti_id;
  lk_value v;
  lk_str text;

  BEGIN_TEST("text_input: SHIFT+arrow selects, typing replaces");

  ui = make_text_input_ui("abcde", &ti);
  st = lk_ui_state(ui);
  ti_id = lk_intern_id(ui->intern, lk_str_c("ti"));

  /* Set cursor at position 2 */
  lk_state_set(st, ti_id, LKS_CURSOR_POS, lk_v_i32(2));

  /* SHIFT+RIGHT -> selection 2..3, cursor 3 */
  memset(&ev, 0, sizeof(ev));
  ev.type = LK_EVENT_KEY_DOWN;
  ev.target = ti;
  ev.data.key.keycode = LKK_RIGHT;
  ev.mods = LK_MOD_SHIFT;
  lk_event_route(ui, &ev);

  v = lk_state_get(st, ti_id, LKS_SELECTION_START);
  CHECK_EQ((unsigned)v.as.i, 2u);
  v = lk_state_get(st, ti_id, LKS_SELECTION_END);
  CHECK_EQ((unsigned)v.as.i, 3u);
  v = lk_state_get(st, ti_id, LKS_CURSOR_POS);
  CHECK_EQ((unsigned)v.as.i, 3u);

  /* SHIFT+RIGHT again -> selection 2..4, cursor 4 */
  memset(&ev, 0, sizeof(ev));
  ev.type = LK_EVENT_KEY_DOWN;
  ev.target = ti;
  ev.data.key.keycode = LKK_RIGHT;
  ev.mods = LK_MOD_SHIFT;
  lk_event_route(ui, &ev);

  v = lk_state_get(st, ti_id, LKS_SELECTION_END);
  CHECK_EQ((unsigned)v.as.i, 4u);

  /* Type "X" -> replaces selection "cd", text becomes "abXe" */
  memset(&ev, 0, sizeof(ev));
  ev.type = LK_EVENT_TEXT;
  ev.target = ti;
  ev.data.text.buf[0] = 'X';
  ev.data.text.len = 1;
  lk_event_route(ui, &ev);

  v = lk_state_get(st, ti_id, LKS_TEXT_BUF);
  text = lk_intern_str(ui->intern, v.as.str_id);
  CHECK_EQ(text.len, 4u);
  CHECK(memcmp(text.ptr, "abXe", 4) == 0);

  /* Selection should be cleared */
  v = lk_state_get(st, ti_id, LKS_SELECTION_START);
  CHECK_EQ((unsigned)v.as.i, 0u);
  v = lk_state_get(st, ti_id, LKS_SELECTION_END);
  CHECK_EQ((unsigned)v.as.i, 0u);

  END_TEST();
  lk_ui_destroy(ui);
}

static void test_text_input_select_all(void) {
  lk_ui *ui;
  lk_ix ti;
  lk_event ev;
  lk_state *st;
  lk_node_id ti_id;
  lk_value v;

  BEGIN_TEST("text_input: CTRL+A selects all");

  ui = make_text_input_ui("hello", &ti);
  st = lk_ui_state(ui);
  ti_id = lk_intern_id(ui->intern, lk_str_c("ti"));

  /* CTRL+A */
  memset(&ev, 0, sizeof(ev));
  ev.type = LK_EVENT_KEY_DOWN;
  ev.target = ti;
  ev.data.key.keycode = LKK_A;
  ev.mods = LK_MOD_CTRL;
  lk_event_route(ui, &ev);

  CHECK_EQ((unsigned)ev.handled, 1u);

  v = lk_state_get(st, ti_id, LKS_SELECTION_START);
  CHECK_EQ((unsigned)v.as.i, 0u);
  v = lk_state_get(st, ti_id, LKS_SELECTION_END);
  CHECK_EQ((unsigned)v.as.i, 5u);
  v = lk_state_get(st, ti_id, LKS_CURSOR_POS);
  CHECK_EQ((unsigned)v.as.i, 5u);

  END_TEST();
  lk_ui_destroy(ui);
}

static void test_text_input_initial_text(void) {
  lk_ui *ui;
  lk_ix ti;
  lk_event ev;
  lk_state *st;
  lk_node_id ti_id;
  lk_value v;
  lk_str text;

  BEGIN_TEST("text_input: first edit copies from UIP_TEXT");

  ui = make_text_input_ui("abc", &ti);
  st = lk_ui_state(ui);
  ti_id = lk_intern_id(ui->intern, lk_str_c("ti"));

  /* Before any event, LKS_TEXT_BUF should be NONE */
  v = lk_state_get(st, ti_id, LKS_TEXT_BUF);
  CHECK_EQ((unsigned)v.tag, (unsigned)UIV_NONE);

  /* Send a TEXT event to trigger initialization */
  memset(&ev, 0, sizeof(ev));
  ev.type = LK_EVENT_TEXT;
  ev.target = ti;
  ev.data.text.buf[0] = 'x';
  ev.data.text.len = 1;
  lk_event_route(ui, &ev);

  /* Text should now be "abcx" (copied initial + inserted) */
  v = lk_state_get(st, ti_id, LKS_TEXT_BUF);
  CHECK_EQ((unsigned)v.tag, (unsigned)UIV_STR);
  text = lk_intern_str(ui->intern, v.as.str_id);
  CHECK_EQ(text.len, 4u);
  CHECK(memcmp(text.ptr, "abcx", 4) == 0);

  END_TEST();
  lk_ui_destroy(ui);
}

static void test_text_input_cursor_clamp(void) {
  lk_ui *ui;
  lk_ix ti;
  lk_event ev;
  lk_state *st;
  lk_node_id ti_id;
  lk_value v;

  BEGIN_TEST("text_input: cursor clamps to [0, len]");

  ui = make_text_input_ui("ab", &ti);
  st = lk_ui_state(ui);
  ti_id = lk_intern_id(ui->intern, lk_str_c("ti"));

  /* Set cursor beyond text length */
  lk_state_set(st, ti_id, LKS_CURSOR_POS, lk_v_i32(99));

  /* LEFT should clamp to len first, then move to len-1 */
  memset(&ev, 0, sizeof(ev));
  ev.type = LK_EVENT_KEY_DOWN;
  ev.target = ti;
  ev.data.key.keycode = LKK_LEFT;
  lk_event_route(ui, &ev);

  v = lk_state_get(st, ti_id, LKS_CURSOR_POS);
  /* Cursor was clamped to 2 (len), then LEFT moved to 1 */
  CHECK_EQ((unsigned)v.as.i, 1u);

  /* Set cursor negative (impossible via i32 but test 0 boundary) */
  lk_state_set(st, ti_id, LKS_CURSOR_POS, lk_v_i32(0));

  /* LEFT at 0 should stay at 0 */
  memset(&ev, 0, sizeof(ev));
  ev.type = LK_EVENT_KEY_DOWN;
  ev.target = ti;
  ev.data.key.keycode = LKK_LEFT;
  lk_event_route(ui, &ev);

  v = lk_state_get(st, ti_id, LKS_CURSOR_POS);
  CHECK_EQ((unsigned)v.as.i, 0u);

  END_TEST();
  lk_ui_destroy(ui);
}

/* ================================================================
 * Tests: scroll widget
 * ================================================================ */

static lk_rect *run_layout_with_state(lk_tree *t, lk_i32 vw, lk_i32 vh,
                                      lk_state *state) {
  lk_layout_cfg cfg;
  lk_rect *rects;

  memset(&cfg, 0, sizeof(cfg));
  cfg.text = lk_text_backend_stub();
  cfg.viewport_w = vw;
  cfg.viewport_h = vh;
  cfg.state = state;

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

/* Build: window > column > scroll(h=100) > labels
 * Column respects scroll's measured UIP_H, unlike window which fills viewport.
 */
static lk_ui *make_scroll_ui(int label_count, lk_ix *out_scroll) {
  lk_ui *ui = lk_ui_create(NULL);
  lk_tree *t;
  lk_ix w, col, sc;
  int i;

  t = lk_ui_begin_frame(ui);
  w = lk_tree_add_node_s(t, lk_str_c("w"), UIK_WINDOW);
  col = lk_tree_add_node_s(t, lk_str_c("col"), UIK_COLUMN);
  sc = lk_tree_add_node_s(t, lk_str_c("sc"), UIK_SCROLL);
  lk_tree_add_prop(t, sc, UIP_H, lk_v_i32(100));
  lk_tree_set_root(t, w);
  lk_tree_append_child(t, w, col);
  lk_tree_append_child(t, col, sc);

  for (i = 0; i < label_count; i++) {
    char name[32];
    lk_ix lbl;
    sprintf(name, "lbl%d", i);
    lbl = lk_tree_add_node_s(t, lk_str_c(name), UIK_LABEL);
    lk_tree_add_prop(t, lbl, UIP_TEXT, lk_v_cstr(t->intern, name));
    lk_tree_append_child(t, sc, lbl);
  }

  lk_ui_end_frame(ui);

  {
    const lk_tree *cur = lk_ui_tree(ui);
    lk_node_id sc_id = lk_intern_id(ui->intern, lk_str_c("sc"));
    *out_scroll = lk_tree_find_by_id(cur, sc_id);
  }

  return ui;
}

static void test_scroll_measure(void) {
  /* Content = 10 labels * 16px each = 160px. Scroll natural size = 160. */
  lk_tree *t = lk_tree_create(NULL);
  lk_ix w, sc, l1, l2;
  lk_rect *r;

  BEGIN_TEST("scroll: measure sums children heights");

  w = lk_tree_add_node_s(t, lk_str_c("w"), UIK_WINDOW);
  sc = lk_tree_add_node_s(t, lk_str_c("sc"), UIK_SCROLL);
  l1 = lk_tree_add_node_s(t, lk_str_c("l1"), UIK_LABEL);
  l2 = lk_tree_add_node_s(t, lk_str_c("l2"), UIK_LABEL);
  lk_tree_add_prop(t, l1, UIP_TEXT, lk_v_cstr(t->intern, "Hello"));
  lk_tree_add_prop(t, l2, UIP_TEXT, lk_v_cstr(t->intern, "World!"));
  lk_tree_set_root(t, w);
  lk_tree_append_child(t, w, sc);
  lk_tree_append_child(t, sc, l1);
  lk_tree_append_child(t, sc, l2);

  r = run_layout(t, 800, 600);
  CHECK(r != NULL);
  if (r) {
    /* With UIP_H not set on scroll, intrinsic h = 16+16 = 32.
     * But scroll is inside a window that gives it full viewport height.
     * The scroll itself gets 600px (window fills viewport, no padding).
     */
    CHECK_EQ((unsigned)r[sc].h, 600u);
    /* Children each get 16px height */
    CHECK_EQ((unsigned)r[l1].h, 16u);
    CHECK_EQ((unsigned)r[l2].h, 16u);
    free(r);
  }

  END_TEST();
  lk_tree_destroy(t);
}

static void test_scroll_layout_no_offset(void) {
  /* No scroll offset: children start at top of content area. */
  lk_ix sc;
  lk_ui *ui = make_scroll_ui(3, &sc);
  const lk_tree *cur = lk_ui_tree(ui);
  lk_rect *r;

  BEGIN_TEST("scroll: children at y=0 when no scroll offset");

  r = run_layout_with_state((lk_tree *)cur, 800, 600, lk_ui_state(ui));
  CHECK(r != NULL);
  if (r) {
    lk_ix ch = cur->nodes[sc].first_child;
    /* First child y should equal scroll content y (scroll.y + padding) */
    CHECK_EQ((unsigned)r[ch].y, (unsigned)r[sc].y);
    free(r);
  }

  END_TEST();
  lk_ui_destroy(ui);
}

static void test_scroll_layout_with_offset(void) {
  /* Set scroll_y = 20, children should shift up by 20. */
  lk_ix sc;
  lk_ui *ui = make_scroll_ui(10, &sc);
  const lk_tree *cur = lk_ui_tree(ui);
  lk_state *st = lk_ui_state(ui);
  lk_node_id sc_id;
  lk_rect *r;

  BEGIN_TEST("scroll: children shifted by -scroll_y");

  sc_id = cur->nodes[sc].id;
  lk_state_set(st, sc_id, LKS_SCROLL_Y, lk_v_i32(20));

  r = run_layout_with_state((lk_tree *)cur, 800, 600, st);
  CHECK(r != NULL);
  if (r) {
    lk_ix ch = cur->nodes[sc].first_child;
    /* First child y = scroll.y - 20 */
    CHECK_EQ((unsigned)r[ch].y, (unsigned)(r[sc].y - 20));
    free(r);
  }

  END_TEST();
  lk_ui_destroy(ui);
}

static void test_scroll_render_clips(void) {
  /* Scroll should produce FILL_RECT + CLIP_BEGIN + children + CLIP_END. */
  lk_ix sc;
  lk_ui *ui = make_scroll_ui(2, &sc);
  const lk_tree *cur = lk_ui_tree(ui);
  lk_rect *r;
  lk_render_list rl;
  int found_clip_begin = 0;
  int found_clip_end = 0;
  lk_u32 i;

  BEGIN_TEST("scroll: render list has CLIP_BEGIN/CLIP_END");

  r = run_layout_with_state((lk_tree *)cur, 800, 600, lk_ui_state(ui));
  CHECK(r != NULL);
  if (r) {
    memset(&rl, 0, sizeof(rl));
    lk_render_build(cur, r, NULL, NULL, &rl);

    for (i = 0; i < rl.count; i++) {
      if (rl.cmds[i].op == LK_ROP_CLIP_BEGIN) found_clip_begin++;
      if (rl.cmds[i].op == LK_ROP_CLIP_END) found_clip_end++;
    }
    /* At least 2 CLIP_BEGIN: one for window, one for scroll */
    CHECK(found_clip_begin >= 2);
    CHECK(found_clip_end >= 2);

    lk_render_list_destroy(&rl);
    free(r);
  }

  END_TEST();
  lk_ui_destroy(ui);
}

static void test_scroll_wheel_event(void) {
  /* Wheel event adjusts scroll_y. */
  lk_ix sc;
  lk_ui *ui = make_scroll_ui(10, &sc);
  const lk_tree *cur = lk_ui_tree(ui);
  lk_state *st = lk_ui_state(ui);
  lk_node_id sc_id;
  lk_rect *r;
  lk_event ev;
  lk_value v;

  BEGIN_TEST("scroll: wheel adjusts scroll_y");

  sc_id = cur->nodes[sc].id;

  /* Need to do layout first so scroll_max is computed */
  r = run_layout_with_state((lk_tree *)cur, 800, 600, st);
  CHECK(r != NULL);
  if (r) {
    /* Scroll on the scroll node itself */
    memset(&ev, 0, sizeof(ev));
    ev.type = LK_EVENT_WHEEL;
    ev.target = sc;
    ev.data.wheel.dy = -1; /* scroll down */

    lk_event_route(ui, &ev);

    v = lk_state_get(st, sc_id, LKS_SCROLL_Y);
    CHECK(v.tag == UIV_I32);
    CHECK((lk_i32)v.as.i == 30); /* SCROLL_STEP = 30 */

    free(r);
  }

  END_TEST();
  lk_ui_destroy(ui);
}

static void test_scroll_clamp_bounds(void) {
  /* scroll_y stays in [0, scroll_max]. */
  lk_ix sc;
  lk_ui *ui = make_scroll_ui(10, &sc);
  const lk_tree *cur = lk_ui_tree(ui);
  lk_state *st = lk_ui_state(ui);
  lk_node_id sc_id;
  lk_rect *r;
  lk_value v;
  lk_i32 scroll_max;

  BEGIN_TEST("scroll: scroll_y clamped to [0, scroll_max]");

  sc_id = cur->nodes[sc].id;

  /* Layout to compute scroll_max */
  r = run_layout_with_state((lk_tree *)cur, 800, 600, st);
  CHECK(r != NULL);
  if (r) {
    scroll_max = (lk_i32)lk_state_get(st, sc_id, LKS_SCROLL_MAX).as.i;
    CHECK(scroll_max > 0);

    /* Set scroll_y way past max, then do layout again to clamp */
    lk_state_set(st, sc_id, LKS_SCROLL_Y, lk_v_i32(scroll_max + 1000));
    free(r);
    r = run_layout_with_state((lk_tree *)cur, 800, 600, st);
    if (r) {
      v = lk_state_get(st, sc_id, LKS_SCROLL_Y);
      CHECK_EQ((unsigned)(lk_i32)v.as.i, (unsigned)scroll_max);
      free(r);
    }

    /* Set scroll_y negative, layout clamps to 0 */
    lk_state_set(st, sc_id, LKS_SCROLL_Y, lk_v_i32(-100));
    r = run_layout_with_state((lk_tree *)cur, 800, 600, st);
    if (r) {
      v = lk_state_get(st, sc_id, LKS_SCROLL_Y);
      CHECK_EQ((unsigned)(lk_i32)v.as.i, 0u);
      free(r);
    }
  }

  END_TEST();
  lk_ui_destroy(ui);
}

static void test_scroll_wheel_bubbles(void) {
  /* Wheel event on a child label bubbles up to scroll ancestor. */
  lk_ix sc;
  lk_ui *ui = make_scroll_ui(10, &sc);
  const lk_tree *cur = lk_ui_tree(ui);
  lk_state *st = lk_ui_state(ui);
  lk_node_id sc_id;
  lk_rect *r;
  lk_event ev;
  lk_value v;
  lk_ix first_child;

  BEGIN_TEST("scroll: wheel on child handled by scroll ancestor");

  sc_id = cur->nodes[sc].id;
  first_child = cur->nodes[sc].first_child;

  /* Layout to compute scroll_max */
  r = run_layout_with_state((lk_tree *)cur, 800, 600, st);
  CHECK(r != NULL);
  if (r) {
    /* Send wheel to child label, should bubble to scroll */
    memset(&ev, 0, sizeof(ev));
    ev.type = LK_EVENT_WHEEL;
    ev.target = first_child;
    ev.data.wheel.dy = -1; /* scroll down */

    lk_event_route(ui, &ev);

    v = lk_state_get(st, sc_id, LKS_SCROLL_Y);
    CHECK(v.tag == UIV_I32);
    CHECK((lk_i32)v.as.i == 30);
    CHECK(ev.handled == 1);

    free(r);
  }

  END_TEST();
  lk_ui_destroy(ui);
}

static void test_scroll_empty(void) {
  /* Empty scroll container: no crash. */
  lk_ix sc;
  lk_ui *ui;
  const lk_tree *cur;
  lk_rect *r;

  BEGIN_TEST("scroll: empty container no crash");

  ui = make_scroll_ui(0, &sc);
  cur = lk_ui_tree(ui);

  r = run_layout_with_state((lk_tree *)cur, 800, 600, lk_ui_state(ui));
  CHECK(r != NULL);
  if (r) {
    /* Scroll with 0 children should not crash and rect should be valid */
    CHECK(r[sc].w > 0);
    free(r);
  }

  END_TEST();
  lk_ui_destroy(ui);
}

static void test_scroll_bar_rendered(void) {
  /* When content > viewport, extra FILL_RECTs for track + thumb. */
  lk_ix sc;
  lk_ui *ui = make_scroll_ui(10, &sc);
  const lk_tree *cur = lk_ui_tree(ui);
  lk_state *st = lk_ui_state(ui);
  lk_node_id sc_id;
  lk_rect *r;
  lk_render_list rl;
  lk_u32 fill_count = 0;
  lk_u32 i;

  BEGIN_TEST("scroll: scroll bar rendered when content > viewport");

  sc_id = cur->nodes[sc].id;

  r = run_layout_with_state((lk_tree *)cur, 800, 600, st);
  CHECK(r != NULL);
  if (r) {
    lk_i32 smax = (lk_i32)lk_state_get(st, sc_id, LKS_SCROLL_MAX).as.i;
    CHECK(smax > 0);

    memset(&rl, 0, sizeof(rl));
    lk_render_build(cur, r, NULL, st, &rl);

    /* Count FILL_RECTs. Should include: window bg, scroll bg,
     * scroll track, scroll thumb = at least 4 FILL_RECTs
     * (plus DRAW_TEXT for labels, CLIPs for window and scroll)
     */
    for (i = 0; i < rl.count; i++) {
      if (rl.cmds[i].op == LK_ROP_FILL_RECT) fill_count++;
    }
    CHECK(fill_count >= 4);

    lk_render_list_destroy(&rl);
    free(r);
  }

  END_TEST();
  lk_ui_destroy(ui);
}

static void test_widget_bubble_no_break_text_input(void) {
  /* Verify text_input still works: key events targeted at text_input
   * should NOT be consumed by any ancestor widget handler.
   */
  lk_ix ti;
  lk_ui *ui;
  lk_state *st;
  lk_node_id ti_id;
  lk_event ev;
  lk_value v;

  BEGIN_TEST("scroll: bubbling does not break text_input");

  {
    lk_tree *t;
    lk_ix w, sc;
    const lk_tree *cur;

    ui = lk_ui_create(NULL);
    t = lk_ui_begin_frame(ui);
    w = lk_tree_add_node_s(t, lk_str_c("w"), UIK_WINDOW);
    sc = lk_tree_add_node_s(t, lk_str_c("sc"), UIK_SCROLL);
    lk_tree_add_prop(t, sc, UIP_H, lk_v_i32(200));
    ti = lk_tree_add_node_s(t, lk_str_c("ti"), UIK_TEXT_INPUT);
    lk_tree_add_prop(t, ti, UIP_TEXT, lk_v_cstr(t->intern, "abc"));
    lk_tree_add_prop(t, ti, UIP_FOCUSABLE, lk_v_bool(1));
    lk_tree_set_root(t, w);
    lk_tree_append_child(t, w, sc);
    lk_tree_append_child(t, sc, ti);
    lk_ui_end_frame(ui);

    cur = lk_ui_tree(ui);
    ti_id = lk_intern_id(ui->intern, lk_str_c("ti"));
    lk_focus_set(ui, cur, ti_id);
    ti = lk_tree_find_by_id(cur, ti_id);
  }

  st = lk_ui_state(ui);

  /* Type 'x' into text input */
  memset(&ev, 0, sizeof(ev));
  ev.type = LK_EVENT_TEXT;
  ev.target = ti;
  ev.data.text.buf[0] = 'x';
  ev.data.text.len = 1;
  lk_event_route(ui, &ev);

  /* Text should now be "abcx" (appended at end since cursor defaults to end) */
  v = lk_state_get(st, ti_id, LKS_TEXT_BUF);
  CHECK(v.tag == UIV_STR);
  {
    const lk_tree *cur = lk_ui_tree(ui);
    lk_str txt = lk_intern_str(cur->intern, v.as.str_id);
    CHECK_EQ(txt.len, 4u);
  }

  END_TEST();
  lk_ui_destroy(ui);
}

/* ================================================================
 * Border rendering tests
 * ================================================================ */

static void test_border_render_four_fill_rects(void) {
  /* Node with border_width=2 via styles -> 4 extra FILL_RECTs */
  lk_tree *t;
  lk_style *styles;
  lk_layout_cfg cfg;
  lk_rect *rects;
  lk_render_list rl;
  lk_ix w, btn;
  lk_u32 i;
  int border_count;

  BEGIN_TEST("border: emits 4 fill rects");

  t = lk_tree_create(NULL);
  w = lk_tree_add_node_s(t, lk_str_c("w"), UIK_WINDOW);
  btn = lk_tree_add_node_s(t, lk_str_c("btn"), UIK_BUTTON);
  lk_tree_add_prop(t, btn, UIP_TEXT, lk_v_cstr(t->intern, "OK"));
  lk_tree_set_root(t, w);
  lk_tree_append_child(t, w, btn);

  styles = (lk_style *)malloc(sizeof(lk_style) * t->node_count);
  rects = (lk_rect *)malloc(sizeof(lk_rect) * t->node_count);

  if (styles && rects) {
    memset(styles, 0, sizeof(lk_style) * t->node_count);
    /* window style */
    styles[w].bg.a = 255;
    /* button style with border */
    styles[btn].bg.r = 60; styles[btn].bg.g = 60; styles[btn].bg.b = 60;
    styles[btn].bg.a = 255;
    styles[btn].fg.r = 255; styles[btn].fg.g = 255; styles[btn].fg.b = 255;
    styles[btn].fg.a = 255;
    styles[btn].padding = 4;
    styles[btn].border_width = 2;
    styles[btn].border_color.r = 100;
    styles[btn].border_color.g = 150;
    styles[btn].border_color.b = 200;
    styles[btn].border_color.a = 255;

    memset(&cfg, 0, sizeof(cfg));
    cfg.text = lk_text_backend_stub();
    cfg.viewport_w = 800;
    cfg.viewport_h = 600;
    cfg.styles = styles;
    lk_layout(t, &cfg, rects);

    memset(&rl, 0, sizeof(rl));
    lk_render_build(t, rects, styles, NULL, &rl);

    /* Count FILL_RECTs with border color */
    border_count = 0;
    for (i = 0; i < rl.count; i++) {
      if (rl.cmds[i].op == LK_ROP_FILL_RECT &&
          rl.cmds[i].color.r == 100 &&
          rl.cmds[i].color.g == 150 &&
          rl.cmds[i].color.b == 200) {
        border_count++;
      }
    }
    CHECK_EQ((unsigned)border_count, 4u);

    lk_render_list_destroy(&rl);
  }

  free(rects);
  free(styles);
  lk_tree_destroy(t);
  END_TEST();
}

static void test_border_inset_layout(void) {
  /* border_width + padding insets the content rect */
  lk_tree *t;
  lk_style *styles;
  lk_layout_cfg cfg;
  lk_rect *rects;
  lk_ix w, col, lbl;

  BEGIN_TEST("border: insets layout content rect");

  t = lk_tree_create(NULL);
  w = lk_tree_add_node_s(t, lk_str_c("w"), UIK_WINDOW);
  col = lk_tree_add_node_s(t, lk_str_c("col"), UIK_COLUMN);
  lbl = lk_tree_add_node_s(t, lk_str_c("lbl"), UIK_LABEL);
  lk_tree_add_prop(t, lbl, UIP_TEXT, lk_v_cstr(t->intern, "Hi"));
  lk_tree_set_root(t, w);
  lk_tree_append_child(t, w, col);
  lk_tree_append_child(t, col, lbl);

  styles = (lk_style *)malloc(sizeof(lk_style) * t->node_count);
  rects = (lk_rect *)malloc(sizeof(lk_rect) * t->node_count);

  if (styles && rects) {
    memset(styles, 0, sizeof(lk_style) * t->node_count);
    /* column: padding=5, border_width=3 -> inset=8 */
    styles[col].padding = 5;
    styles[col].border_width = 3;
    styles[col].align = LK_ALIGN_STRETCH;

    memset(&cfg, 0, sizeof(cfg));
    cfg.text = lk_text_backend_stub();
    cfg.viewport_w = 400;
    cfg.viewport_h = 300;
    cfg.styles = styles;
    lk_layout(t, &cfg, rects);

    /* Column fills viewport (from window layout).
     * Label should be inset by 5+3=8 from column origin.
     */
    CHECK_EQ((unsigned)rects[lbl].x, 8u);
    CHECK_EQ((unsigned)rects[lbl].y, 8u);
    /* Width should be 400 - 8*2 = 384 (stretch) */
    CHECK_EQ((unsigned)rects[lbl].w, 384u);
  }

  free(rects);
  free(styles);
  lk_tree_destroy(t);
  END_TEST();
}

static void test_border_zero_no_extra_commands(void) {
  /* border_width=0 should not emit extra FILL_RECTs */
  lk_tree *t;
  lk_style *styles;
  lk_layout_cfg cfg;
  lk_rect *rects;
  lk_render_list rl;
  lk_render_list rl2;
  lk_ix w, btn;

  BEGIN_TEST("border: zero width = no extra commands");

  t = lk_tree_create(NULL);
  w = lk_tree_add_node_s(t, lk_str_c("w"), UIK_WINDOW);
  btn = lk_tree_add_node_s(t, lk_str_c("btn"), UIK_BUTTON);
  lk_tree_add_prop(t, btn, UIP_TEXT, lk_v_cstr(t->intern, "X"));
  lk_tree_set_root(t, w);
  lk_tree_append_child(t, w, btn);

  styles = (lk_style *)malloc(sizeof(lk_style) * t->node_count);
  rects = (lk_rect *)malloc(sizeof(lk_rect) * t->node_count);

  if (styles && rects) {
    memset(styles, 0, sizeof(lk_style) * t->node_count);
    styles[btn].bg.a = 255;
    styles[btn].fg.a = 255;
    styles[btn].padding = 4;
    styles[btn].border_width = 0;

    memset(&cfg, 0, sizeof(cfg));
    cfg.text = lk_text_backend_stub();
    cfg.viewport_w = 800;
    cfg.viewport_h = 600;
    cfg.styles = styles;
    lk_layout(t, &cfg, rects);

    /* Render with border_width=0 */
    memset(&rl, 0, sizeof(rl));
    lk_render_build(t, rects, styles, NULL, &rl);

    /* Render without styles (fallback, also no border) */
    memset(&rl2, 0, sizeof(rl2));
    lk_render_build(t, rects, NULL, NULL, &rl2);

    /* Same number of commands — no border extras */
    CHECK_EQ(rl.count, rl2.count);

    lk_render_list_destroy(&rl);
    lk_render_list_destroy(&rl2);
  }

  free(rects);
  free(styles);
  lk_tree_destroy(t);
  END_TEST();
}

static void test_border_theme_integration(void) {
  /* Theme rule with border -> resolve -> render -> border commands present */
  lk_tree *t;
  lk_theme *th;
  lk_style *styles;
  lk_style s;
  lk_layout_cfg cfg;
  lk_rect *rects;
  lk_render_list rl;
  lk_ix w, btn;
  lk_u32 i;
  int border_count;

  BEGIN_TEST("border: theme rule integration");

  t = lk_tree_create(NULL);
  w = lk_tree_add_node_s(t, lk_str_c("w"), UIK_WINDOW);
  btn = lk_tree_add_node_s(t, lk_str_c("btn"), UIK_BUTTON);
  lk_tree_add_prop(t, btn, UIP_TEXT, lk_v_cstr(t->intern, "Go"));
  lk_tree_set_root(t, w);
  lk_tree_append_child(t, w, btn);

  th = lk_theme_new(NULL, NULL, NULL);
  memset(&s, 0, sizeof(s));
  s.border_width = 1;
  s.border_color.r = 80;
  s.border_color.g = 140;
  s.border_color.b = 220;
  s.border_color.a = 255;
  lk_theme_add_rule(th, UIK_BUTTON, 0, 0, &s,
                    LK_SF_BORDER_WIDTH | LK_SF_BORDER_COLOR);

  styles = (lk_style *)malloc(sizeof(lk_style) * t->node_count);
  rects = (lk_rect *)malloc(sizeof(lk_rect) * t->node_count);

  if (th && styles && rects) {
    lk_style_resolve(th, t, NULL, styles);

    memset(&cfg, 0, sizeof(cfg));
    cfg.text = lk_text_backend_stub();
    cfg.viewport_w = 800;
    cfg.viewport_h = 600;
    cfg.styles = styles;
    lk_layout(t, &cfg, rects);

    memset(&rl, 0, sizeof(rl));
    lk_render_build(t, rects, styles, NULL, &rl);

    /* Should find 4 FILL_RECTs with the theme border color */
    border_count = 0;
    for (i = 0; i < rl.count; i++) {
      if (rl.cmds[i].op == LK_ROP_FILL_RECT &&
          rl.cmds[i].color.r == 80 &&
          rl.cmds[i].color.g == 140 &&
          rl.cmds[i].color.b == 220) {
        border_count++;
      }
    }
    CHECK_EQ((unsigned)border_count, 4u);

    lk_render_list_destroy(&rl);
  }

  free(rects);
  free(styles);
  lk_theme_destroy(th);
  lk_tree_destroy(t);
  END_TEST();
}

/* ---- clipboard mock + tests ---- */

static char g_mock_clipboard[1024];

static const char *mock_clipboard_get(void *ud) {
  (void)ud;
  return g_mock_clipboard;
}

static void mock_clipboard_set(void *ud, const char *text) {
  size_t len;
  (void)ud;
  len = strlen(text);
  if (len >= sizeof(g_mock_clipboard)) {
    len = sizeof(g_mock_clipboard) - 1;
  }
  memcpy(g_mock_clipboard, text, len);
  g_mock_clipboard[len] = '\0';
}

static void test_text_input_ctrl_c(void) {
  lk_ui *ui;
  lk_ix ti;
  lk_event ev;
  lk_state *st;
  lk_node_id ti_id;

  BEGIN_TEST("text_input: Ctrl+C copies selection to clipboard");

  ui = make_text_input_ui("hello world", &ti);
  st = lk_ui_state(ui);
  ti_id = lk_intern_id(ui->intern, lk_str_c("ti"));

  lk_ui_set_clipboard(ui, mock_clipboard_get, mock_clipboard_set, NULL);
  g_mock_clipboard[0] = '\0';

  /* Select "llo w" (positions 2..7) */
  lk_state_set(st, ti_id, LKS_SELECTION_START, lk_v_i32(2));
  lk_state_set(st, ti_id, LKS_SELECTION_END, lk_v_i32(7));
  lk_state_set(st, ti_id, LKS_CURSOR_POS, lk_v_i32(7));

  memset(&ev, 0, sizeof(ev));
  ev.type = LK_EVENT_KEY_DOWN;
  ev.target = ti;
  ev.data.key.keycode = LKK_C;
  ev.mods = LK_MOD_CTRL;
  lk_event_route(ui, &ev);

  CHECK_EQ((unsigned)ev.handled, 1u);
  CHECK(strcmp(g_mock_clipboard, "llo w") == 0);

  /* Text should be unchanged */
  {
    lk_value v = lk_state_get(st, ti_id, LKS_TEXT_BUF);
    lk_str text = lk_intern_str(ui->intern, v.as.str_id);
    CHECK_EQ(text.len, 11u);
    CHECK(memcmp(text.ptr, "hello world", 11) == 0);
  }

  END_TEST();
  lk_ui_destroy(ui);
}

static void test_text_input_ctrl_v(void) {
  lk_ui *ui;
  lk_ix ti;
  lk_event ev;
  lk_state *st;
  lk_node_id ti_id;
  lk_value v;
  lk_str text;

  BEGIN_TEST("text_input: Ctrl+V pastes from clipboard");

  ui = make_text_input_ui("ab", &ti);
  st = lk_ui_state(ui);
  ti_id = lk_intern_id(ui->intern, lk_str_c("ti"));

  lk_ui_set_clipboard(ui, mock_clipboard_get, mock_clipboard_set, NULL);
  strcpy(g_mock_clipboard, "XY");

  /* Cursor at position 1 */
  lk_state_set(st, ti_id, LKS_CURSOR_POS, lk_v_i32(1));

  memset(&ev, 0, sizeof(ev));
  ev.type = LK_EVENT_KEY_DOWN;
  ev.target = ti;
  ev.data.key.keycode = LKK_V;
  ev.mods = LK_MOD_CTRL;
  lk_event_route(ui, &ev);

  CHECK_EQ((unsigned)ev.handled, 1u);

  /* Text should be "aXYb" */
  v = lk_state_get(st, ti_id, LKS_TEXT_BUF);
  text = lk_intern_str(ui->intern, v.as.str_id);
  CHECK_EQ(text.len, 4u);
  CHECK(memcmp(text.ptr, "aXYb", 4) == 0);

  /* Cursor at 3 */
  v = lk_state_get(st, ti_id, LKS_CURSOR_POS);
  CHECK_EQ((unsigned)v.as.i, 3u);

  END_TEST();
  lk_ui_destroy(ui);
}

static void test_text_input_ctrl_x(void) {
  lk_ui *ui;
  lk_ix ti;
  lk_event ev;
  lk_state *st;
  lk_node_id ti_id;
  lk_value v;
  lk_str text;

  BEGIN_TEST("text_input: Ctrl+X cuts selection");

  ui = make_text_input_ui("abcde", &ti);
  st = lk_ui_state(ui);
  ti_id = lk_intern_id(ui->intern, lk_str_c("ti"));

  lk_ui_set_clipboard(ui, mock_clipboard_get, mock_clipboard_set, NULL);
  g_mock_clipboard[0] = '\0';

  /* Select "bcd" (positions 1..4) */
  lk_state_set(st, ti_id, LKS_SELECTION_START, lk_v_i32(1));
  lk_state_set(st, ti_id, LKS_SELECTION_END, lk_v_i32(4));
  lk_state_set(st, ti_id, LKS_CURSOR_POS, lk_v_i32(4));

  memset(&ev, 0, sizeof(ev));
  ev.type = LK_EVENT_KEY_DOWN;
  ev.target = ti;
  ev.data.key.keycode = LKK_X;
  ev.mods = LK_MOD_CTRL;
  lk_event_route(ui, &ev);

  CHECK_EQ((unsigned)ev.handled, 1u);
  CHECK(strcmp(g_mock_clipboard, "bcd") == 0);

  /* Text should be "ae" */
  v = lk_state_get(st, ti_id, LKS_TEXT_BUF);
  text = lk_intern_str(ui->intern, v.as.str_id);
  CHECK_EQ(text.len, 2u);
  CHECK(memcmp(text.ptr, "ae", 2) == 0);

  /* Cursor at 1 */
  v = lk_state_get(st, ti_id, LKS_CURSOR_POS);
  CHECK_EQ((unsigned)v.as.i, 1u);

  END_TEST();
  lk_ui_destroy(ui);
}

static void test_text_input_ctrl_v_no_clipboard(void) {
  lk_ui *ui;
  lk_ix ti;
  lk_event ev;

  BEGIN_TEST("text_input: Ctrl+V no-op without clipboard");

  ui = make_text_input_ui("ab", &ti);
  /* No clipboard installed */

  memset(&ev, 0, sizeof(ev));
  ev.type = LK_EVENT_KEY_DOWN;
  ev.target = ti;
  ev.data.key.keycode = LKK_V;
  ev.mods = LK_MOD_CTRL;
  lk_event_route(ui, &ev);

  /* Should not be handled (bubbles up) */
  CHECK_EQ((unsigned)ev.handled, 0u);

  END_TEST();
  lk_ui_destroy(ui);
}

/* ================================================================
 * Dropdown widget tests
 * ================================================================ */

static lk_ui *make_dropdown_ui(lk_ix *out_dd) {
  lk_ui *ui = lk_ui_create(NULL);
  lk_tree *t;
  lk_ix w, col, dd, o1, o2, o3;
  const lk_tree *cur;
  lk_node_id dd_id;

  t = lk_ui_begin_frame(ui);
  w = lk_tree_add_node_s(t, lk_str_c("w"), UIK_WINDOW);
  col = lk_tree_add_node_s(t, lk_str_c("col"), UIK_COLUMN);
  dd = lk_tree_add_node_s(t, lk_str_c("dd"), UIK_DROPDOWN);
  lk_tree_add_prop(t, dd, UIP_FOCUSABLE, lk_v_bool(1));
  lk_tree_add_prop(t, dd, UIP_W, lk_v_i32(140));
  o1 = lk_tree_add_node_s(t, lk_str_c("o1"), UIK_OPTION);
  lk_tree_add_prop(t, o1, UIP_TEXT, lk_v_cstr(t->intern, "Apple"));
  o2 = lk_tree_add_node_s(t, lk_str_c("o2"), UIK_OPTION);
  lk_tree_add_prop(t, o2, UIP_TEXT, lk_v_cstr(t->intern, "Banana"));
  o3 = lk_tree_add_node_s(t, lk_str_c("o3"), UIK_OPTION);
  lk_tree_add_prop(t, o3, UIP_TEXT, lk_v_cstr(t->intern, "Cherry"));
  lk_tree_set_root(t, w);
  lk_tree_append_child(t, w, col);
  lk_tree_append_child(t, col, dd);
  lk_tree_append_child(t, dd, o1);
  lk_tree_append_child(t, dd, o2);
  lk_tree_append_child(t, dd, o3);
  lk_ui_end_frame(ui);

  cur = lk_ui_tree(ui);
  dd_id = lk_intern_id(ui->intern, lk_str_c("dd"));
  *out_dd = lk_tree_find_by_id(cur, dd_id);
  return ui;
}

/* Open a dropdown by routing a trigger click — the real open path,
 * which pushes the popup overlay AND sets LKS_EXPANDED (the two are
 * kept in sync; the overlay stack drives the overlay passes). */
static void open_dropdown(lk_ui *ui, lk_ix dd) {
  lk_event ev;

  memset(&ev, 0, sizeof(ev));
  ev.type = LK_EVENT_POINTER_DOWN;
  ev.target = dd;
  lk_event_route(ui, &ev);
}

static void test_dropdown_pointer_down_toggles(void) {
  lk_ui *ui;
  lk_ix dd;
  lk_event ev;
  lk_state *st;
  lk_node_id dd_id;

  BEGIN_TEST("dropdown: pointer_down toggles expanded");

  ui = make_dropdown_ui(&dd);
  st = lk_ui_state(ui);
  dd_id = lk_intern_id(ui->intern, lk_str_c("dd"));

  /* Initially not expanded — tag is UIV_NONE when unset */
  CHECK_EQ((unsigned)lk_state_get(st, dd_id, LKS_EXPANDED).tag,
           (unsigned)UIV_NONE);

  /* Click */
  memset(&ev, 0, sizeof(ev));
  ev.type = LK_EVENT_POINTER_DOWN;
  ev.target = dd;
  lk_event_route(ui, &ev);
  CHECK_EQ((unsigned)ev.handled, 1u);
  CHECK_EQ((unsigned)lk_state_get(st, dd_id, LKS_EXPANDED).as.i, 1u);

  /* Click again — closes */
  memset(&ev, 0, sizeof(ev));
  ev.type = LK_EVENT_POINTER_DOWN;
  ev.target = dd;
  lk_event_route(ui, &ev);
  CHECK_EQ((unsigned)lk_state_get(st, dd_id, LKS_EXPANDED).as.i, 0u);

  END_TEST();
  lk_ui_destroy(ui);
}

static void test_dropdown_arrow_keys_navigate(void) {
  lk_ui *ui;
  lk_ix dd;
  lk_event ev;
  lk_state *st;
  lk_node_id dd_id;

  BEGIN_TEST("dropdown: arrow keys move hover index");

  ui = make_dropdown_ui(&dd);
  st = lk_ui_state(ui);
  dd_id = lk_intern_id(ui->intern, lk_str_c("dd"));

  /* Open via Down */
  memset(&ev, 0, sizeof(ev));
  ev.type = LK_EVENT_KEY_DOWN;
  ev.target = dd;
  ev.data.key.keycode = LKK_DOWN;
  lk_event_route(ui, &ev);
  CHECK_EQ((unsigned)lk_state_get(st, dd_id, LKS_EXPANDED).as.i, 1u);
  CHECK_EQ((int)lk_state_get(st, dd_id, LKS_HOVER_INDEX).as.i, 0);

  /* Down -> 1 */
  memset(&ev, 0, sizeof(ev));
  ev.type = LK_EVENT_KEY_DOWN;
  ev.target = dd;
  ev.data.key.keycode = LKK_DOWN;
  lk_event_route(ui, &ev);
  CHECK_EQ((int)lk_state_get(st, dd_id, LKS_HOVER_INDEX).as.i, 1);

  /* Up -> 0 */
  memset(&ev, 0, sizeof(ev));
  ev.type = LK_EVENT_KEY_DOWN;
  ev.target = dd;
  ev.data.key.keycode = LKK_UP;
  lk_event_route(ui, &ev);
  CHECK_EQ((int)lk_state_get(st, dd_id, LKS_HOVER_INDEX).as.i, 0);

  END_TEST();
  lk_ui_destroy(ui);
}

/* Build a dropdown UI whose tree structure will stay stable across
 * frames (window > col > dd > options).  With an attached presentation
 * on the dropdown so translators can match.  Returns dd index. */
static lk_ui *make_dropdown_ui_with_pres(lk_ix *out_dd) {
  lk_ui *ui = lk_ui_create(NULL);
  lk_tree *t;
  lk_ix w, col, dd, o1, o2, o3;
  lk_node_id dd_id;

  t = lk_ui_begin_frame(ui);
  w = lk_tree_add_node_s(t, lk_str_c("w"), UIK_WINDOW);
  col = lk_tree_add_node_s(t, lk_str_c("col"), UIK_COLUMN);
  dd = lk_tree_add_node_s(t, lk_str_c("dd"), UIK_DROPDOWN);
  lk_tree_add_prop(t, dd, UIP_FOCUSABLE, lk_v_bool(1));
  lk_tree_add_prop(t, dd, UIP_W, lk_v_i32(140));
  lk_tree_add_presentation_s(t, dd, "picker", lk_v_i32(0));
  o1 = lk_tree_add_node_s(t, lk_str_c("o1"), UIK_OPTION);
  lk_tree_add_prop(t, o1, UIP_TEXT, lk_v_cstr(t->intern, "Apple"));
  o2 = lk_tree_add_node_s(t, lk_str_c("o2"), UIK_OPTION);
  lk_tree_add_prop(t, o2, UIP_TEXT, lk_v_cstr(t->intern, "Banana"));
  o3 = lk_tree_add_node_s(t, lk_str_c("o3"), UIK_OPTION);
  lk_tree_add_prop(t, o3, UIP_TEXT, lk_v_cstr(t->intern, "Cherry"));
  lk_tree_set_root(t, w);
  lk_tree_append_child(t, w, col);
  lk_tree_append_child(t, col, dd);
  lk_tree_append_child(t, dd, o1);
  lk_tree_append_child(t, dd, o2);
  lk_tree_append_child(t, dd, o3);
  lk_ui_end_frame(ui);

  dd_id = lk_intern_id(ui->intern, lk_str_c("dd"));
  *out_dd = lk_tree_find_by_id(lk_ui_tree(ui), dd_id);
  return ui;
}

static void test_dropdown_return_commits(void) {
  lk_ui *ui;
  lk_ix dd;
  lk_event ev;
  lk_state *st;
  lk_node_id dd_id;

  BEGIN_TEST("dropdown: RETURN commits hover to selection and closes");

  ui = make_dropdown_ui_with_pres(&dd);
  st = lk_ui_state(ui);
  dd_id = lk_intern_id(ui->intern, lk_str_c("dd"));

  /* Register translator so commit emits a command we can inspect */
  lk_ui_add_translator_s(ui, LK_EVENT_VALUE_CHANGED, "picker", 0, 0, 0,
                          "Selected");

  /* Open + navigate to index 1 */
  lk_state_set(st, dd_id, LKS_EXPANDED, lk_v_i32(1));
  lk_state_set(st, dd_id, LKS_HOVER_INDEX, lk_v_i32(1));

  /* Commit */
  memset(&ev, 0, sizeof(ev));
  ev.type = LK_EVENT_KEY_DOWN;
  ev.target = dd;
  ev.data.key.keycode = LKK_RETURN;
  lk_event_route(ui, &ev);

  CHECK_EQ((int)lk_state_get(st, dd_id, LKS_SELECTED_INDEX).as.i, 1);
  CHECK_EQ((int)lk_state_get(st, dd_id, LKS_EXPANDED).as.i, 0);

  /* Verify value_changed command was dispatched with source_value="Banana" */
  {
    const lk_command_queue *q = lk_ui_commands(ui);
    CHECK_EQ(q->count, 1);
    if (q->count >= 1) {
      const lk_command *cmd = &q->cmds[0];
      CHECK_EQ(cmd->source_value.tag, UIV_STR);
      {
        lk_str sv = lk_intern_str(ui->intern, cmd->source_value.as.str_id);
        CHECK(sv.len == 6 && memcmp(sv.ptr, "Banana", 6) == 0);
      }
    }
  }

  END_TEST();
  lk_ui_destroy(ui);
}

static void test_dropdown_escape_closes(void) {
  lk_ui *ui;
  lk_ix dd;
  lk_event ev;
  lk_state *st;
  lk_node_id dd_id;

  BEGIN_TEST("dropdown: ESCAPE closes expanded popup");

  ui = make_dropdown_ui(&dd);
  st = lk_ui_state(ui);
  dd_id = lk_intern_id(ui->intern, lk_str_c("dd"));

  lk_state_set(st, dd_id, LKS_EXPANDED, lk_v_i32(1));

  memset(&ev, 0, sizeof(ev));
  ev.type = LK_EVENT_KEY_DOWN;
  ev.target = dd;
  ev.data.key.keycode = LKK_ESCAPE;
  lk_event_route(ui, &ev);

  CHECK_EQ((unsigned)lk_state_get(st, dd_id, LKS_EXPANDED).as.i, 0u);

  END_TEST();
  lk_ui_destroy(ui);
}

static void test_dropdown_overlay_hit_test(void) {
  lk_ui *ui;
  lk_ix dd;
  lk_rect *rects;
  lk_ix hit;
  lk_layout_cfg cfg;
  lk_state *st;
  lk_node_id dd_id;

  BEGIN_TEST("dropdown: overlay hit-test returns option under cursor");

  ui = make_dropdown_ui(&dd);
  st = lk_ui_state(ui);
  dd_id = lk_intern_id(ui->intern, lk_str_c("dd"));

  rects = (lk_rect *)calloc(lk_ui_tree(ui)->node_count, sizeof(lk_rect));
  memset(&cfg, 0, sizeof(cfg));
  cfg.text = lk_text_backend_stub();
  cfg.viewport_w = 800;
  cfg.viewport_h = 600;
  lk_ui_resolve_styles(ui);
  cfg.styles = lk_ui_styles(ui);
  cfg.state = st;
  lk_layout(lk_ui_tree(ui), &cfg, rects);

  open_dropdown(ui, dd);
  CHECK_EQ((int)lk_state_get(st, dd_id, LKS_EXPANDED).as.i, 1);
  CHECK_EQ(lk_overlay_count(ui), 1u);

  /* Popup starts below trigger. Click on row 1 (second option). */
  {
    lk_rect tr = rects[dd];
    lk_i32 row_h = 20 + 4 * 2; /* DROPDOWN_MIN_OPTION_H is 20, pad 4 each side */
    lk_i32 click_y = tr.y + tr.h + 7 + row_h + row_h / 2; /* inset + 1 row + middle */
    lk_i32 click_x = tr.x + tr.w / 2;

    hit = lk_hit_test_overlay(ui, rects, &cfg, click_x, click_y);
    /* Expect an option node ix (one of o1/o2/o3). */
    CHECK(hit > 0);
    if (hit > 0) {
      CHECK_EQ((unsigned)lk_ui_tree(ui)->nodes[hit].kind,
               (unsigned)UIK_OPTION);
    }
  }

  free(rects);
  END_TEST();
  lk_ui_destroy(ui);
}

static void test_dropdown_click_outside_closes(void) {
  lk_ui *ui;
  lk_ix dd;
  lk_rect *rects;
  lk_layout_cfg cfg;
  lk_state *st;
  lk_node_id dd_id;
  int dismissed;

  BEGIN_TEST("dropdown: click outside dismisses popup");

  ui = make_dropdown_ui(&dd);
  st = lk_ui_state(ui);
  dd_id = lk_intern_id(ui->intern, lk_str_c("dd"));

  rects = (lk_rect *)calloc(lk_ui_tree(ui)->node_count, sizeof(lk_rect));
  memset(&cfg, 0, sizeof(cfg));
  cfg.text = lk_text_backend_stub();
  cfg.viewport_w = 800;
  cfg.viewport_h = 600;
  lk_ui_resolve_styles(ui);
  cfg.styles = lk_ui_styles(ui);
  cfg.state = st;
  lk_layout(lk_ui_tree(ui), &cfg, rects);

  open_dropdown(ui, dd);
  CHECK_EQ(lk_overlay_count(ui), 1u);

  /* Far-away click (outside both trigger and popup) */
  dismissed = lk_overlay_dismiss_outside(ui, rects, &cfg, 700, 500);
  CHECK_EQ(dismissed, LK_DISMISS_DISMISSED);
  CHECK_EQ((unsigned)lk_state_get(st, dd_id, LKS_EXPANDED).tag,
           (unsigned)UIV_I32);
  CHECK_EQ((int)lk_state_get(st, dd_id, LKS_EXPANDED).as.i, 0);
  CHECK_EQ(lk_overlay_count(ui), 0u);

  free(rects);
  END_TEST();
  lk_ui_destroy(ui);
}

static void test_dropdown_overlay_render_when_expanded(void) {
  lk_ui *ui;
  lk_ix dd;
  lk_rect *rects;
  lk_layout_cfg cfg;
  lk_state *st;
  lk_node_id dd_id;
  lk_render_list rl;
  lk_u32 cmds_collapsed, cmds_expanded;

  BEGIN_TEST("dropdown: overlay render emits commands only when expanded");

  ui = make_dropdown_ui(&dd);
  st = lk_ui_state(ui);
  dd_id = lk_intern_id(ui->intern, lk_str_c("dd"));

  rects = (lk_rect *)calloc(lk_ui_tree(ui)->node_count, sizeof(lk_rect));
  memset(&cfg, 0, sizeof(cfg));
  cfg.text = lk_text_backend_stub();
  cfg.viewport_w = 800;
  cfg.viewport_h = 600;
  lk_ui_resolve_styles(ui);
  cfg.styles = lk_ui_styles(ui);
  cfg.state = st;
  lk_layout(lk_ui_tree(ui), &cfg, rects);

  /* Collapsed: no overlay on the stack, no overlay commands */
  memset(&rl, 0, sizeof(rl));
  lk_render_build_overlays(ui, rects, &cfg, &rl);
  cmds_collapsed = rl.count;
  CHECK_EQ(cmds_collapsed, 0u);

  /* Expand (pushes the popup overlay), re-run */
  open_dropdown(ui, dd);
  CHECK_EQ((int)lk_state_get(st, dd_id, LKS_EXPANDED).as.i, 1);
  rl.count = 0;
  lk_render_build_overlays(ui, rects, &cfg, &rl);
  cmds_expanded = rl.count;
  CHECK(cmds_expanded > 0u);

  lk_render_list_destroy(&rl);
  free(rects);
  END_TEST();
  lk_ui_destroy(ui);
}

/* ================================================================
 * Overlay generalization tests (lk-overlay.c, UIP_HIDDEN,
 * lk_layout_subtree, ESC-in-core, focus traps)
 * ================================================================ */

static void test_anchor_below_fits(void) {
  lk_overlay ov;
  lk_rect owner;
  lk_rect r;

  BEGIN_TEST("anchor: BELOW fits under owner");

  memset(&ov, 0, sizeof(ov));
  ov.anchor_mode = LK_ANCHOR_BELOW;
  owner.x = 100;
  owner.y = 100;
  owner.w = 80;
  owner.h = 30;

  r = lk_anchor_resolve(&ov, owner, 800, 600, 120, 200);
  CHECK_EQ(r.x, 100);
  CHECK_EQ(r.y, 130);
  CHECK_EQ(r.w, 120);
  CHECK_EQ(r.h, 200);

  END_TEST();
}

static void test_anchor_below_flips_above(void) {
  lk_overlay ov;
  lk_rect owner;
  lk_rect r;

  BEGIN_TEST("anchor: BELOW flips above at bottom edge");

  memset(&ov, 0, sizeof(ov));
  ov.anchor_mode = LK_ANCHOR_BELOW;
  owner.x = 100;
  owner.y = 500;
  owner.w = 80;
  owner.h = 30;

  /* Below would end at 530+200=730 > 600; room above (500-200 >= 0). */
  r = lk_anchor_resolve(&ov, owner, 800, 600, 120, 200);
  CHECK_EQ(r.x, 100);
  CHECK_EQ(r.y, 300); /* owner.y - h */
  CHECK_EQ(r.h, 200);

  /* No room above either (owner near top of a short viewport):
   * stays below but clamps to the viewport bottom. */
  owner.y = 50;
  r = lk_anchor_resolve(&ov, owner, 800, 200, 120, 180);
  CHECK_EQ(r.y, 20); /* clamped: 200 - 180 */

  END_TEST();
}

static void test_anchor_x_clamp(void) {
  lk_overlay ov;
  lk_rect owner;
  lk_rect r;

  BEGIN_TEST("anchor: x clamped into viewport");

  memset(&ov, 0, sizeof(ov));
  ov.anchor_mode = LK_ANCHOR_BELOW;
  owner.x = 750;
  owner.y = 100;
  owner.w = 80;
  owner.h = 30;

  r = lk_anchor_resolve(&ov, owner, 800, 600, 120, 50);
  CHECK_EQ(r.x, 680); /* 800 - 120 */
  CHECK_EQ(r.y, 130);

  /* Left edge */
  owner.x = -40;
  r = lk_anchor_resolve(&ov, owner, 800, 600, 120, 50);
  CHECK_EQ(r.x, 0);

  END_TEST();
}

static void test_anchor_center_viewport(void) {
  lk_overlay ov;
  lk_rect owner;
  lk_rect r;

  BEGIN_TEST("anchor: CENTER_VIEWPORT centers");

  memset(&ov, 0, sizeof(ov));
  ov.anchor_mode = LK_ANCHOR_CENTER_VIEWPORT;
  memset(&owner, 0, sizeof(owner));

  r = lk_anchor_resolve(&ov, owner, 800, 600, 200, 100);
  CHECK_EQ(r.x, 300);
  CHECK_EQ(r.y, 250);
  CHECK_EQ(r.w, 200);
  CHECK_EQ(r.h, 100);

  END_TEST();
}

static void test_anchor_at_cursor(void) {
  lk_overlay ov;
  lk_rect owner;
  lk_rect r;

  BEGIN_TEST("anchor: AT_CURSOR uses offset point + clamps");

  memset(&ov, 0, sizeof(ov));
  ov.anchor_mode = LK_ANCHOR_AT_CURSOR;
  ov.offset.x = 333;
  ov.offset.y = 222;
  memset(&owner, 0, sizeof(owner));

  r = lk_anchor_resolve(&ov, owner, 800, 600, 50, 40);
  CHECK_EQ(r.x, 333);
  CHECK_EQ(r.y, 222);

  /* Cursor near the bottom-right corner clamps back inside. */
  ov.offset.x = 790;
  ov.offset.y = 590;
  r = lk_anchor_resolve(&ov, owner, 800, 600, 50, 40);
  CHECK_EQ(r.x, 750);
  CHECK_EQ(r.y, 560);

  END_TEST();
}

static void test_overlay_push_pop_count(void) {
  lk_ui *ui = lk_ui_create(NULL);
  lk_overlay ov;

  BEGIN_TEST("overlay: push/pop/pop_owner/top/count");

  CHECK_EQ(lk_overlay_count(ui), 0u);
  CHECK(lk_overlay_top(ui) == NULL);

  memset(&ov, 0, sizeof(ov));
  ov.kind = LK_OVERLAY_DROPDOWN_POPUP;
  ov.owner_id = 11;
  CHECK_EQ(lk_overlay_push(ui, &ov), 1);

  ov.kind = LK_OVERLAY_MODAL;
  ov.owner_id = 22;
  CHECK_EQ(lk_overlay_push(ui, &ov), 1);

  CHECK_EQ(lk_overlay_count(ui), 2u);
  CHECK(lk_overlay_top(ui) != NULL);
  CHECK_EQ(lk_overlay_top(ui)->owner_id, 22u);

  lk_overlay_pop(ui);
  CHECK_EQ(lk_overlay_count(ui), 1u);
  CHECK_EQ(lk_overlay_top(ui)->owner_id, 11u);

  /* pop_owner removes mid-stack entries */
  ov.owner_id = 22;
  lk_overlay_push(ui, &ov);
  CHECK_EQ(lk_overlay_pop_owner(ui, 11), 1);
  CHECK_EQ(lk_overlay_count(ui), 1u);
  CHECK_EQ(lk_overlay_top(ui)->owner_id, 22u);

  /* pop_owner on an absent owner is a no-op */
  CHECK_EQ(lk_overlay_pop_owner(ui, 99), 0);
  CHECK_EQ(lk_overlay_count(ui), 1u);

  /* pop on empty is safe */
  lk_overlay_pop(ui);
  lk_overlay_pop(ui);
  CHECK_EQ(lk_overlay_count(ui), 0u);

  END_TEST();
  lk_ui_destroy(ui);
}

/* Build w > colA + colB, with button "b" under colA (moved=0) or
 * colB (moved=1), or omitted entirely (present=0). */
static void build_move_tree(lk_tree *t, int present, int moved) {
  lk_ix w = lk_tree_add_node_s(t, lk_str_c("w"), UIK_WINDOW);
  lk_ix ca = lk_tree_add_node_s(t, lk_str_c("colA"), UIK_COLUMN);
  lk_ix cb = lk_tree_add_node_s(t, lk_str_c("colB"), UIK_COLUMN);

  lk_tree_set_root(t, w);
  lk_tree_append_child(t, w, ca);
  lk_tree_append_child(t, w, cb);

  if (present) {
    lk_ix b = lk_tree_add_node_s(t, lk_str_c("b"), UIK_BUTTON);
    lk_tree_append_child(t, moved ? cb : ca, b);
  }
}

static void test_overlay_end_frame_gc(void) {
  lk_ui *ui = lk_ui_create(NULL);
  lk_tree *t;
  lk_overlay ov;
  lk_node_id b_id;

  BEGIN_TEST("overlay: end_frame pops on owner removal, not move");

  t = lk_ui_begin_frame(ui);
  build_move_tree(t, 1, 0);
  lk_ui_end_frame(ui);

  b_id = lk_intern_id(ui->intern, lk_str_c("b"));

  memset(&ov, 0, sizeof(ov));
  ov.kind = LK_OVERLAY_DROPDOWN_POPUP;
  ov.owner_id = b_id;
  lk_overlay_push(ui, &ov);

  /* Move "b" to a different parent: REMOVED+ADDED — overlay stays. */
  t = lk_ui_begin_frame(ui);
  build_move_tree(t, 1, 1);
  lk_ui_end_frame(ui);
  CHECK_EQ(lk_overlay_count(ui), 1u);

  /* Remove "b" entirely — overlay popped. */
  t = lk_ui_begin_frame(ui);
  build_move_tree(t, 0, 0);
  lk_ui_end_frame(ui);
  CHECK_EQ(lk_overlay_count(ui), 0u);

  END_TEST();
  lk_ui_destroy(ui);
}

static void test_overlay_escape_pops(void) {
  lk_ui *ui;
  lk_ix dd;
  lk_event ev;
  lk_state *st;
  lk_node_id dd_id;

  BEGIN_TEST("overlay: ESC pops topmost + syncs dropdown state");

  ui = make_dropdown_ui(&dd);
  st = lk_ui_state(ui);
  dd_id = lk_intern_id(ui->intern, lk_str_c("dd"));

  open_dropdown(ui, dd);
  CHECK_EQ(lk_overlay_count(ui), 1u);
  CHECK_EQ((int)lk_state_get(st, dd_id, LKS_EXPANDED).as.i, 1);

  /* ESC routed anywhere (target = root, as when nothing is focused)
   * is consumed by the pre-step in lk_event_route. */
  memset(&ev, 0, sizeof(ev));
  ev.type = LK_EVENT_KEY_DOWN;
  ev.target = lk_ui_tree(ui)->root;
  ev.data.key.keycode = LKK_ESCAPE;
  lk_event_route(ui, &ev);

  CHECK_EQ((unsigned)ev.handled, 1u);
  CHECK_EQ(lk_overlay_count(ui), 0u);
  CHECK_EQ((int)lk_state_get(st, dd_id, LKS_EXPANDED).as.i, 0);

  /* With no overlay open, ESC is NOT consumed by the pre-step. */
  memset(&ev, 0, sizeof(ev));
  ev.type = LK_EVENT_KEY_DOWN;
  ev.target = lk_ui_tree(ui)->root;
  ev.data.key.keycode = LKK_ESCAPE;
  lk_event_route(ui, &ev);
  CHECK_EQ((unsigned)ev.handled, 0u);

  END_TEST();
  lk_ui_destroy(ui);
}

static void test_dropdown_bottom_edge_flips(void) {
  lk_ui *ui = lk_ui_create(NULL);
  lk_tree *t;
  lk_ix w, col, sp, dd, o1, o2, o3;
  lk_rect *rects;
  lk_layout_cfg cfg;
  lk_rect popup;

  BEGIN_TEST("dropdown: popup flips above near bottom edge");

  t = lk_ui_begin_frame(ui);
  w = lk_tree_add_node_s(t, lk_str_c("w"), UIK_WINDOW);
  col = lk_tree_add_node_s(t, lk_str_c("col"), UIK_COLUMN);
  sp = lk_tree_add_node_s(t, lk_str_c("sp"), UIK_SPACER);
  lk_tree_add_prop(t, sp, UIP_H, lk_v_i32(150));
  dd = lk_tree_add_node_s(t, lk_str_c("dd"), UIK_DROPDOWN);
  lk_tree_add_prop(t, dd, UIP_W, lk_v_i32(140));
  o1 = lk_tree_add_node_s(t, lk_str_c("o1"), UIK_OPTION);
  lk_tree_add_prop(t, o1, UIP_TEXT, lk_v_cstr(t->intern, "Apple"));
  o2 = lk_tree_add_node_s(t, lk_str_c("o2"), UIK_OPTION);
  lk_tree_add_prop(t, o2, UIP_TEXT, lk_v_cstr(t->intern, "Banana"));
  o3 = lk_tree_add_node_s(t, lk_str_c("o3"), UIK_OPTION);
  lk_tree_add_prop(t, o3, UIP_TEXT, lk_v_cstr(t->intern, "Cherry"));
  lk_tree_set_root(t, w);
  lk_tree_append_child(t, w, col);
  lk_tree_append_child(t, col, sp);
  lk_tree_append_child(t, col, dd);
  lk_tree_append_child(t, dd, o1);
  lk_tree_append_child(t, dd, o2);
  lk_tree_append_child(t, dd, o3);
  lk_ui_end_frame(ui);

  dd = lk_tree_find_by_id(lk_ui_tree(ui),
                          lk_intern_id(ui->intern, lk_str_c("dd")));

  rects = (lk_rect *)calloc(lk_ui_tree(ui)->node_count, sizeof(lk_rect));
  memset(&cfg, 0, sizeof(cfg));
  cfg.text = lk_text_backend_stub();
  lk_ui_resolve_styles(ui);
  cfg.styles = lk_ui_styles(ui);
  cfg.state = lk_ui_state(ui);

  /* Short viewport: popup would overflow the bottom -> flips above,
   * sitting exactly on top of the trigger. */
  cfg.viewport_w = 400;
  cfg.viewport_h = 200;
  lk_layout(lk_ui_tree(ui), &cfg, rects);
  popup = lk_dropdown_popup_rect(lk_ui_tree(ui), dd, rects, cfg.styles, &cfg);
  CHECK(popup.y < rects[dd].y);
  CHECK_EQ(popup.y + popup.h, rects[dd].y);
  CHECK(popup.y >= 0);

  /* Tall viewport: same tree, popup stays below the trigger. */
  cfg.viewport_h = 600;
  lk_layout(lk_ui_tree(ui), &cfg, rects);
  popup = lk_dropdown_popup_rect(lk_ui_tree(ui), dd, rects, cfg.styles, &cfg);
  CHECK_EQ(popup.y, rects[dd].y + rects[dd].h);

  free(rects);
  END_TEST();
  lk_ui_destroy(ui);
}

/* Build a tree with a hidden modal subtree:
 *   w > col > btn "outside" (focusable)
 *           > col "modal" (hidden) > btn "m1", btn "m2" (focusable)
 * Returns node indices via out params. */
static lk_ui *make_modal_ui(lk_ix *out_outside, lk_ix *out_modal) {
  lk_ui *ui = lk_ui_create(NULL);
  lk_tree *t;
  lk_ix w, col, bo, modal, m1, m2;
  const lk_tree *cur;

  t = lk_ui_begin_frame(ui);
  w = lk_tree_add_node_s(t, lk_str_c("w"), UIK_WINDOW);
  col = lk_tree_add_node_s(t, lk_str_c("col"), UIK_COLUMN);
  bo = lk_tree_add_node_s(t, lk_str_c("outside"), UIK_BUTTON);
  lk_tree_add_prop(t, bo, UIP_TEXT, lk_v_cstr(t->intern, "Open"));
  lk_tree_add_prop(t, bo, UIP_FOCUSABLE, lk_v_bool(1));
  modal = lk_tree_add_node_s(t, lk_str_c("modal"), UIK_COLUMN);
  lk_tree_add_prop(t, modal, UIP_HIDDEN, lk_v_bool(1));
  m1 = lk_tree_add_node_s(t, lk_str_c("m1"), UIK_BUTTON);
  lk_tree_add_prop(t, m1, UIP_TEXT, lk_v_cstr(t->intern, "OK"));
  lk_tree_add_prop(t, m1, UIP_FOCUSABLE, lk_v_bool(1));
  m2 = lk_tree_add_node_s(t, lk_str_c("m2"), UIK_BUTTON);
  lk_tree_add_prop(t, m2, UIP_TEXT, lk_v_cstr(t->intern, "Cancel"));
  lk_tree_add_prop(t, m2, UIP_FOCUSABLE, lk_v_bool(1));
  lk_tree_set_root(t, w);
  lk_tree_append_child(t, w, col);
  lk_tree_append_child(t, col, bo);
  lk_tree_append_child(t, col, modal);
  lk_tree_append_child(t, modal, m1);
  lk_tree_append_child(t, modal, m2);
  lk_ui_end_frame(ui);

  cur = lk_ui_tree(ui);
  *out_outside =
      lk_tree_find_by_id(cur, lk_intern_id(ui->intern, lk_str_c("outside")));
  *out_modal =
      lk_tree_find_by_id(cur, lk_intern_id(ui->intern, lk_str_c("modal")));
  return ui;
}

static void test_overlay_modal_blocks(void) {
  lk_ui *ui;
  lk_ix outside, modal;
  lk_rect *rects;
  lk_layout_cfg cfg;
  lk_overlay ov;
  lk_node_id modal_id;
  int rc;

  BEGIN_TEST("overlay: modal consumes outside click, no dismiss");

  ui = make_modal_ui(&outside, &modal);
  modal_id = lk_intern_id(ui->intern, lk_str_c("modal"));

  rects = (lk_rect *)calloc(lk_ui_tree(ui)->node_count, sizeof(lk_rect));
  memset(&cfg, 0, sizeof(cfg));
  cfg.text = lk_text_backend_stub();
  cfg.viewport_w = 800;
  cfg.viewport_h = 600;
  lk_ui_resolve_styles(ui);
  cfg.styles = lk_ui_styles(ui);
  cfg.state = lk_ui_state(ui);
  lk_layout(lk_ui_tree(ui), &cfg, rects);

  memset(&ov, 0, sizeof(ov));
  ov.kind = LK_OVERLAY_MODAL;
  ov.anchor_mode = LK_ANCHOR_CENTER_VIEWPORT;
  ov.dismiss_on_outside = 0;
  ov.traps_focus = 1;
  ov.owner_id = modal_id;
  ov.content_root_id = modal_id;
  lk_overlay_push(ui, &ov);

  /* Outside click: blocked (consumed), overlay NOT dismissed. */
  rc = lk_overlay_dismiss_outside(ui, rects, &cfg, 5, 595);
  CHECK_EQ(rc, LK_DISMISS_BLOCKED);
  CHECK_EQ(lk_overlay_count(ui), 1u);

  /* Click inside the centered modal: nothing dismissed or blocked. */
  rc = lk_overlay_dismiss_outside(ui, rects, &cfg, 400, 300);
  CHECK_EQ(rc, LK_DISMISS_NONE);
  CHECK_EQ(lk_overlay_count(ui), 1u);

  /* The modal content is hit-testable through the overlay pass. */
  CHECK(lk_hit_test_overlay(ui, rects, &cfg, 400, 300) != 0);

  free(rects);
  END_TEST();
  lk_ui_destroy(ui);
}

static void test_hidden_subtree_layout(void) {
  lk_ui *ui = lk_ui_create(NULL);
  lk_tree *t;
  lk_ix w, col, la, hid, lb, lc;
  lk_rect *rects;
  lk_layout_cfg cfg;

  BEGIN_TEST("hidden: subtree excluded from layout stacking");

  t = lk_ui_begin_frame(ui);
  w = lk_tree_add_node_s(t, lk_str_c("w"), UIK_WINDOW);
  col = lk_tree_add_node_s(t, lk_str_c("col"), UIK_COLUMN);
  la = lk_tree_add_node_s(t, lk_str_c("la"), UIK_LABEL);
  lk_tree_add_prop(t, la, UIP_TEXT, lk_v_cstr(t->intern, "aa"));
  hid = lk_tree_add_node_s(t, lk_str_c("hid"), UIK_COLUMN);
  lk_tree_add_prop(t, hid, UIP_HIDDEN, lk_v_bool(1));
  lb = lk_tree_add_node_s(t, lk_str_c("lb"), UIK_LABEL);
  lk_tree_add_prop(t, lb, UIP_TEXT, lk_v_cstr(t->intern, "hidden"));
  lc = lk_tree_add_node_s(t, lk_str_c("lc"), UIK_LABEL);
  lk_tree_add_prop(t, lc, UIP_TEXT, lk_v_cstr(t->intern, "cc"));
  lk_tree_set_root(t, w);
  lk_tree_append_child(t, w, col);
  lk_tree_append_child(t, col, la);
  lk_tree_append_child(t, col, hid);
  lk_tree_append_child(t, col, lc);
  lk_tree_append_child(t, hid, lb);
  lk_ui_end_frame(ui);

  rects = (lk_rect *)calloc(lk_ui_tree(ui)->node_count, sizeof(lk_rect));
  memset(&cfg, 0, sizeof(cfg));
  cfg.text = lk_text_backend_stub();
  cfg.viewport_w = 400;
  cfg.viewport_h = 300;
  /* styles == NULL: layout reads tree props (no theme padding/gap),
   * so the geometry below is exact stub arithmetic. */
  lk_layout(lk_ui_tree(ui), &cfg, rects);

  {
    const lk_tree *cur = lk_ui_tree(ui);
    lk_ix ila = lk_tree_find_by_id(cur, lk_intern_id(ui->intern,
                                                     lk_str_c("la")));
    lk_ix ihid = lk_tree_find_by_id(cur, lk_intern_id(ui->intern,
                                                      lk_str_c("hid")));
    lk_ix ilb = lk_tree_find_by_id(cur, lk_intern_id(ui->intern,
                                                     lk_str_c("lb")));
    lk_ix ilc = lk_tree_find_by_id(cur, lk_intern_id(ui->intern,
                                                     lk_str_c("lc")));

    /* la at y=0 h=16; lc packs directly under it (hidden col skipped,
     * contributes no height and no gap). */
    CHECK_EQ(rects[ila].y, 0);
    CHECK_EQ(rects[ila].h, 16);
    CHECK_EQ(rects[ilc].y, 16);

    /* Hidden nodes keep zeroed rects. */
    CHECK_EQ(rects[ihid].w, 0);
    CHECK_EQ(rects[ihid].h, 0);
    CHECK_EQ(rects[ilb].w, 0);
    CHECK_EQ(rects[ilb].h, 0);
  }

  free(rects);
  END_TEST();
  lk_ui_destroy(ui);
}

static void test_hidden_subtree_render_hit(void) {
  lk_ui *ui = lk_ui_create(NULL);
  lk_tree *t;
  lk_ix w, col, la, hid, lb;
  lk_rect *rects;
  lk_layout_cfg cfg;
  lk_render_list rl;
  lk_u32 i, draw_text_count;

  BEGIN_TEST("hidden: subtree skipped by render and hit-test");

  t = lk_ui_begin_frame(ui);
  w = lk_tree_add_node_s(t, lk_str_c("w"), UIK_WINDOW);
  col = lk_tree_add_node_s(t, lk_str_c("col"), UIK_COLUMN);
  la = lk_tree_add_node_s(t, lk_str_c("la"), UIK_LABEL);
  lk_tree_add_prop(t, la, UIP_TEXT, lk_v_cstr(t->intern, "aa"));
  hid = lk_tree_add_node_s(t, lk_str_c("hid"), UIK_COLUMN);
  lk_tree_add_prop(t, hid, UIP_HIDDEN, lk_v_bool(1));
  lb = lk_tree_add_node_s(t, lk_str_c("lb"), UIK_LABEL);
  lk_tree_add_prop(t, lb, UIP_TEXT, lk_v_cstr(t->intern, "hidden"));
  lk_tree_set_root(t, w);
  lk_tree_append_child(t, w, col);
  lk_tree_append_child(t, col, la);
  lk_tree_append_child(t, col, hid);
  lk_tree_append_child(t, hid, lb);
  lk_ui_end_frame(ui);

  rects = (lk_rect *)calloc(lk_ui_tree(ui)->node_count, sizeof(lk_rect));
  memset(&cfg, 0, sizeof(cfg));
  cfg.text = lk_text_backend_stub();
  cfg.viewport_w = 400;
  cfg.viewport_h = 300;
  lk_layout(lk_ui_tree(ui), &cfg, rects);

  {
    const lk_tree *cur = lk_ui_tree(ui);
    lk_ix ila = lk_tree_find_by_id(cur, lk_intern_id(ui->intern,
                                                     lk_str_c("la")));
    lk_ix ihid = lk_tree_find_by_id(cur, lk_intern_id(ui->intern,
                                                      lk_str_c("hid")));
    lk_ix ilb = lk_tree_find_by_id(cur, lk_intern_id(ui->intern,
                                                     lk_str_c("lb")));

    /* Render: only the visible label's text is emitted. */
    memset(&rl, 0, sizeof(rl));
    lk_render_build(cur, rects, NULL, lk_ui_state(ui), &rl);
    draw_text_count = 0;

    for (i = 0; i < rl.count; i++) {
      if (rl.cmds[i].op == LK_ROP_DRAW_TEXT) {
        draw_text_count++;
      }
    }

    CHECK_EQ(draw_text_count, 1u);

    /* Hit-test: even with stale rects overlapping visible content,
     * hidden nodes are never returned. */
    rects[ihid] = rects[ila];
    rects[ilb] = rects[ila];
    CHECK_EQ(lk_hit_test(cur, rects,
                         rects[ila].x + 1, rects[ila].y + 1), ila);

    lk_render_list_destroy(&rl);
  }

  free(rects);
  END_TEST();
  lk_ui_destroy(ui);
}

static void test_hidden_focus_next_skips(void) {
  lk_ui *ui;
  lk_ix outside, modal;
  lk_node_id got;

  BEGIN_TEST("hidden: focus_next skips hidden subtree");

  /* modal subtree is hidden and no trapping overlay is active, so
   * m1/m2 are not focus-collectable — only "outside" is. */
  ui = make_modal_ui(&outside, &modal);

  got = lk_focus_next(ui, lk_ui_tree(ui));
  CHECK_EQ(got, lk_intern_id(ui->intern, lk_str_c("outside")));

  got = lk_focus_next(ui, lk_ui_tree(ui));
  CHECK_EQ(got, lk_intern_id(ui->intern, lk_str_c("outside"))); /* wraps */

  END_TEST();
  lk_ui_destroy(ui);
}

static void test_layout_subtree_positions(void) {
  lk_ui *ui = lk_ui_create(NULL);
  lk_tree *t;
  lk_ix w, col, ov, l1, l2;
  lk_rect *rects;
  lk_layout_cfg cfg;

  BEGIN_TEST("layout_subtree: column laid out at origin");

  t = lk_ui_begin_frame(ui);
  w = lk_tree_add_node_s(t, lk_str_c("w"), UIK_WINDOW);
  col = lk_tree_add_node_s(t, lk_str_c("col"), UIK_COLUMN);
  ov = lk_tree_add_node_s(t, lk_str_c("ov"), UIK_COLUMN);
  lk_tree_add_prop(t, ov, UIP_HIDDEN, lk_v_bool(1));
  lk_tree_add_prop(t, ov, UIP_GAP, lk_v_i32(4));
  l1 = lk_tree_add_node_s(t, lk_str_c("l1"), UIK_LABEL);
  lk_tree_add_prop(t, l1, UIP_TEXT, lk_v_cstr(t->intern, "abc"));
  l2 = lk_tree_add_node_s(t, lk_str_c("l2"), UIK_LABEL);
  lk_tree_add_prop(t, l2, UIP_TEXT, lk_v_cstr(t->intern, "de"));
  lk_tree_set_root(t, w);
  lk_tree_append_child(t, w, col);
  lk_tree_append_child(t, col, ov);
  lk_tree_append_child(t, ov, l1);
  lk_tree_append_child(t, ov, l2);
  lk_ui_end_frame(ui);

  rects = (lk_rect *)calloc(lk_ui_tree(ui)->node_count, sizeof(lk_rect));
  memset(&cfg, 0, sizeof(cfg));
  cfg.text = lk_text_backend_stub();
  cfg.viewport_w = 400;
  cfg.viewport_h = 300;
  /* styles NULL: exact tree-prop arithmetic (no theme padding). */

  {
    const lk_tree *cur = lk_ui_tree(ui);
    lk_ix iov = lk_tree_find_by_id(cur, lk_intern_id(ui->intern,
                                                     lk_str_c("ov")));
    lk_ix il1 = lk_tree_find_by_id(cur, lk_intern_id(ui->intern,
                                                     lk_str_c("l1")));
    lk_ix il2 = lk_tree_find_by_id(cur, lk_intern_id(ui->intern,
                                                     lk_str_c("l2")));

    CHECK_EQ(lk_layout_subtree(cur, &cfg, iov, 100, 50, rects), 1);

    /* Stub text: 8 px per codepoint, 16 px tall.
     * "abc" = 24x16, "de" = 16x16, gap 4.
     * Column: w = 24, h = 16 + 4 + 16 = 36. */
    CHECK_EQ(rects[iov].x, 100);
    CHECK_EQ(rects[iov].y, 50);
    CHECK_EQ(rects[iov].w, 24);
    CHECK_EQ(rects[iov].h, 36);

    CHECK_EQ(rects[il1].x, 100);
    CHECK_EQ(rects[il1].y, 50);
    CHECK_EQ(rects[il1].w, 24); /* stretched to column width */
    CHECK_EQ(rects[il1].h, 16);

    CHECK_EQ(rects[il2].y, 70); /* 50 + 16 + gap 4 */
    CHECK_EQ(rects[il2].h, 16);
  }

  free(rects);
  END_TEST();
  lk_ui_destroy(ui);
}

static void test_focus_trap_scopes_tab(void) {
  lk_ui *ui;
  lk_ix outside, modal;
  lk_overlay ov;
  lk_node_id modal_id, got;

  BEGIN_TEST("focus trap: tab-cycling scoped to trap subtree");

  ui = make_modal_ui(&outside, &modal);
  modal_id = lk_intern_id(ui->intern, lk_str_c("modal"));

  memset(&ov, 0, sizeof(ov));
  ov.kind = LK_OVERLAY_MODAL;
  ov.anchor_mode = LK_ANCHOR_CENTER_VIEWPORT;
  ov.dismiss_on_outside = 0;
  ov.traps_focus = 1;
  ov.owner_id = modal_id;
  ov.content_root_id = modal_id;
  lk_overlay_push(ui, &ov);

  /* Cycling only visits m1 and m2 — never "outside". */
  got = lk_focus_next(ui, lk_ui_tree(ui));
  CHECK_EQ(got, lk_intern_id(ui->intern, lk_str_c("m1")));

  got = lk_focus_next(ui, lk_ui_tree(ui));
  CHECK_EQ(got, lk_intern_id(ui->intern, lk_str_c("m2")));

  got = lk_focus_next(ui, lk_ui_tree(ui));
  CHECK_EQ(got, lk_intern_id(ui->intern, lk_str_c("m1"))); /* wraps */

  got = lk_focus_prev(ui, lk_ui_tree(ui));
  CHECK_EQ(got, lk_intern_id(ui->intern, lk_str_c("m2")));

  /* Popping the modal restores whole-tree cycling (hidden subtree
   * skipped again). */
  lk_overlay_pop(ui);
  got = lk_focus_next(ui, lk_ui_tree(ui));
  CHECK_EQ(got, lk_intern_id(ui->intern, lk_str_c("outside")));

  END_TEST();
  lk_ui_destroy(ui);
}

/* ================================================================
 * Tooltip tests (UIP_TOOLTIP prop, lk-tooltip.c, hover hook)
 * ================================================================ */

/* w > col > b1 (tooltip), b2 (tooltip), lab (plain) */
static lk_ui *make_tooltip_ui(void) {
  lk_ui *ui = lk_ui_create(NULL);
  lk_tree *t;
  lk_ix w, col, b1, b2, lab;

  t = lk_ui_begin_frame(ui);
  w = lk_tree_add_node_s(t, lk_str_c("w"), UIK_WINDOW);
  col = lk_tree_add_node_s(t, lk_str_c("col"), UIK_COLUMN);
  b1 = lk_tree_add_node_s(t, lk_str_c("b1"), UIK_BUTTON);
  lk_tree_add_prop(t, b1, UIP_TEXT, lk_v_cstr(t->intern, "Save"));
  lk_tree_add_prop(t, b1, UIP_TOOLTIP, lk_v_cstr(t->intern, "Saves the file"));
  b2 = lk_tree_add_node_s(t, lk_str_c("b2"), UIK_BUTTON);
  lk_tree_add_prop(t, b2, UIP_TEXT, lk_v_cstr(t->intern, "Undo"));
  lk_tree_add_prop(t, b2, UIP_TOOLTIP, lk_v_cstr(t->intern, "Reverts"));
  lab = lk_tree_add_node_s(t, lk_str_c("lab"), UIK_LABEL);
  lk_tree_add_prop(t, lab, UIP_TEXT, lk_v_cstr(t->intern, "plain"));
  lk_tree_set_root(t, w);
  lk_tree_append_child(t, w, col);
  lk_tree_append_child(t, col, b1);
  lk_tree_append_child(t, col, b2);
  lk_tree_append_child(t, col, lab);
  lk_ui_end_frame(ui);

  return ui;
}

static void test_tooltip_hover_push_pop(void) {
  lk_ui *ui;
  lk_node_id b1_id, b2_id, lab_id;

  BEGIN_TEST("tooltip: hover transitions push/pop/swap");

  ui = make_tooltip_ui();
  b1_id = lk_intern_id(ui->intern, lk_str_c("b1"));
  b2_id = lk_intern_id(ui->intern, lk_str_c("b2"));
  lab_id = lk_intern_id(ui->intern, lk_str_c("lab"));

  /* Hover onto a tooltip'd button pushes exactly one TOOLTIP overlay. */
  lk_hover_set(ui, b1_id);
  CHECK_EQ(lk_overlay_count(ui), 1u);
  CHECK_EQ((unsigned)lk_overlay_top(ui)->kind, (unsigned)LK_OVERLAY_TOOLTIP);
  CHECK_EQ(lk_overlay_top(ui)->owner_id, b1_id);

  /* Re-hovering the same node is not a transition — still one. */
  lk_hover_set(ui, b1_id);
  CHECK_EQ(lk_overlay_count(ui), 1u);

  /* Hover between two tooltip'd nodes swaps cleanly (count stays 1). */
  lk_hover_set(ui, b2_id);
  CHECK_EQ(lk_overlay_count(ui), 1u);
  CHECK_EQ(lk_overlay_top(ui)->owner_id, b2_id);

  /* Moving hover to a plain node pops it. */
  lk_hover_set(ui, lab_id);
  CHECK_EQ(lk_overlay_count(ui), 0u);

  /* And hover_clear pops too. */
  lk_hover_set(ui, b1_id);
  CHECK_EQ(lk_overlay_count(ui), 1u);
  lk_hover_clear(ui);
  CHECK_EQ(lk_overlay_count(ui), 0u);

  END_TEST();
  lk_ui_destroy(ui);
}

static void test_tooltip_render_on_top(void) {
  lk_ui *ui;
  lk_rect *rects;
  lk_layout_cfg cfg;
  lk_render_list rl;
  lk_u32 main_count;
  lk_u32 tip_sid;

  BEGIN_TEST("tooltip: render list gets tooltip text on top");

  ui = make_tooltip_ui();
  tip_sid = lk_intern_id(ui->intern, lk_str_c("Saves the file"));

  rects = (lk_rect *)calloc(lk_ui_tree(ui)->node_count, sizeof(lk_rect));
  memset(&cfg, 0, sizeof(cfg));
  cfg.text = lk_text_backend_stub();
  cfg.viewport_w = 400;
  cfg.viewport_h = 300;
  lk_ui_resolve_styles(ui);
  cfg.styles = lk_ui_styles(ui);
  cfg.state = lk_ui_state(ui);
  lk_layout(lk_ui_tree(ui), &cfg, rects);

  lk_hover_set(ui, lk_intern_id(ui->intern, lk_str_c("b1")));
  CHECK_EQ(lk_overlay_count(ui), 1u);

  memset(&rl, 0, sizeof(rl));
  lk_render_build(lk_ui_tree(ui), rects, cfg.styles, cfg.state, &rl);
  main_count = rl.count;
  lk_render_build_overlays(ui, rects, &cfg, &rl);

  /* Tooltip commands (bg + 4 border strips + text) appended on top;
   * the final command is the tooltip's DRAW_TEXT. */
  CHECK_EQ(rl.count, main_count + 6);
  CHECK_EQ((unsigned)rl.cmds[rl.count - 1].op, (unsigned)LK_ROP_DRAW_TEXT);
  CHECK_EQ(rl.cmds[rl.count - 1].str_id, tip_sid);
  CHECK_EQ((unsigned)rl.cmds[main_count].op, (unsigned)LK_ROP_FILL_RECT);

  lk_render_list_destroy(&rl);
  free(rects);
  END_TEST();
  lk_ui_destroy(ui);
}

static void test_tooltip_passive(void) {
  lk_ui *ui;
  lk_rect *rects;
  lk_layout_cfg cfg;
  lk_rect tip;
  lk_ix b1;
  int rc;

  BEGIN_TEST("tooltip: passive — no hit, no click consumption");

  ui = make_tooltip_ui();

  rects = (lk_rect *)calloc(lk_ui_tree(ui)->node_count, sizeof(lk_rect));
  memset(&cfg, 0, sizeof(cfg));
  cfg.text = lk_text_backend_stub();
  cfg.viewport_w = 400;
  cfg.viewport_h = 300;
  lk_ui_resolve_styles(ui);
  cfg.styles = lk_ui_styles(ui);
  cfg.state = lk_ui_state(ui);
  lk_layout(lk_ui_tree(ui), &cfg, rects);

  lk_hover_set(ui, lk_intern_id(ui->intern, lk_str_c("b1")));
  CHECK_EQ(lk_overlay_count(ui), 1u);

  /* Pointer-down far outside is NOT consumed and does not pop. */
  rc = lk_overlay_dismiss_outside(ui, rects, &cfg, 399, 299);
  CHECK_EQ(rc, LK_DISMISS_NONE);
  CHECK_EQ(lk_overlay_count(ui), 1u);

  /* The tooltip rect itself is transparent to the overlay hit-test. */
  b1 = lk_tree_find_by_id(lk_ui_tree(ui),
                          lk_intern_id(ui->intern, lk_str_c("b1")));
  tip = lk_tooltip_rect(lk_ui_tree(ui), b1, lk_overlay_top(ui), rects, &cfg);
  CHECK(tip.w > 0 && tip.h > 0);
  CHECK_EQ(lk_hit_test_overlay(ui, rects, &cfg, tip.x + tip.w / 2,
                               tip.y + tip.h / 2), 0u);

  free(rects);
  END_TEST();
  lk_ui_destroy(ui);
}

static void test_tooltip_bottom_edge_flips(void) {
  lk_ui *ui = lk_ui_create(NULL);
  lk_tree *t;
  lk_ix w, col, sp, b;
  lk_rect *rects;
  lk_layout_cfg cfg;
  lk_rect tip;

  BEGIN_TEST("tooltip: flips above near bottom edge");

  t = lk_ui_begin_frame(ui);
  w = lk_tree_add_node_s(t, lk_str_c("w"), UIK_WINDOW);
  col = lk_tree_add_node_s(t, lk_str_c("col"), UIK_COLUMN);
  sp = lk_tree_add_node_s(t, lk_str_c("sp"), UIK_SPACER);
  lk_tree_add_prop(t, sp, UIP_H, lk_v_i32(150));
  b = lk_tree_add_node_s(t, lk_str_c("b"), UIK_BUTTON);
  lk_tree_add_prop(t, b, UIP_TEXT, lk_v_cstr(t->intern, "Go"));
  lk_tree_add_prop(t, b, UIP_TOOLTIP, lk_v_cstr(t->intern, "Runs it"));
  lk_tree_set_root(t, w);
  lk_tree_append_child(t, w, col);
  lk_tree_append_child(t, col, sp);
  lk_tree_append_child(t, col, b);
  lk_ui_end_frame(ui);

  rects = (lk_rect *)calloc(lk_ui_tree(ui)->node_count, sizeof(lk_rect));
  memset(&cfg, 0, sizeof(cfg));
  cfg.text = lk_text_backend_stub();
  cfg.viewport_w = 400;
  cfg.viewport_h = 200; /* short: below the button would overflow */
  lk_ui_resolve_styles(ui);
  cfg.styles = lk_ui_styles(ui);
  cfg.state = lk_ui_state(ui);
  lk_layout(lk_ui_tree(ui), &cfg, rects);

  lk_hover_set(ui, lk_intern_id(ui->intern, lk_str_c("b")));
  CHECK_EQ(lk_overlay_count(ui), 1u);

  b = lk_tree_find_by_id(lk_ui_tree(ui),
                         lk_intern_id(ui->intern, lk_str_c("b")));
  tip = lk_tooltip_rect(lk_ui_tree(ui), b, lk_overlay_top(ui), rects, &cfg);
  CHECK(tip.y < rects[b].y);
  CHECK_EQ(tip.y + tip.h, rects[b].y); /* sits directly above */
  CHECK(tip.y >= 0);

  /* Tall viewport: same tree, tooltip stays below. */
  cfg.viewport_h = 600;
  lk_layout(lk_ui_tree(ui), &cfg, rects);
  tip = lk_tooltip_rect(lk_ui_tree(ui), b, lk_overlay_top(ui), rects, &cfg);
  CHECK_EQ(tip.y, rects[b].y + rects[b].h);

  free(rects);
  END_TEST();
  lk_ui_destroy(ui);
}

/* ================================================================
 * Bug-fix regression tests
 * ================================================================ */

static void test_value_none_zeroed(void) {
  BEGIN_TEST("value: NONE has zeroed union");

  {
    lk_value v = lk_v_none();
    CHECK_EQ((unsigned)v.tag, (unsigned)UIV_NONE);
    CHECK_EQ((unsigned)v.as.i, 0u);
  }

  /* lk_command_arg out-of-range path returns the same zeroed none */
  {
    lk_value a = lk_command_arg(NULL, 0);
    CHECK_EQ((unsigned)a.tag, (unsigned)UIV_NONE);
    CHECK_EQ((unsigned)a.as.i, 0u);
  }

  END_TEST();
}

/* Counting allocator for intern alloc/dealloc symmetry check */
static int g_cnt_allocs = 0;
static int g_cnt_deallocs = 0;

static void *counting_alloc(void *ud, lk_u32 bytes) {
  (void)ud;
  g_cnt_allocs++;
  return malloc(bytes);
}

static void counting_dealloc(void *ud, void *ptr) {
  (void)ud;
  g_cnt_deallocs++;
  free(ptr);
}

static void test_intern_custom_alloc_balanced(void) {
  lk_intern *it;
  int i;
  char buf[32];

  BEGIN_TEST("intern: custom alloc/dealloc counts balance");

  g_cnt_allocs = 0;
  g_cnt_deallocs = 0;

  it = lk_intern_new(counting_alloc, counting_dealloc, NULL);
  CHECK(it != NULL);

  /* Force table growth (>44 entries at 70% of 64) and pool growth
   * (>1024 bytes) so grow paths free through the custom dealloc. */
  for (i = 0; i < 200; i++) {
    sprintf(buf, "intern-key-%d", i);
    CHECK(lk_intern_cid(it, buf) != 0);
  }

  lk_intern_destroy(it);

  CHECK(g_cnt_allocs > 0);
  CHECK_EQ((unsigned)g_cnt_allocs, (unsigned)g_cnt_deallocs);

  END_TEST();
}

static void test_translator_disabled_no_command(void) {
  lk_ui *ui = lk_ui_create(NULL);
  lk_tree *t;
  lk_ix w, col, bd, be, dcol, bs;
  lk_event ev;
  const lk_command_queue *q;

  BEGIN_TEST("translator: disabled nodes emit no commands");

  t = lk_ui_begin_frame(ui);
  w = lk_tree_add_node_s(t, lk_str_c("w"), UIK_WINDOW);
  col = lk_tree_add_node_s(t, lk_str_c("col"), UIK_COLUMN);
  bd = lk_tree_add_node_s(t, lk_str_c("bd"), UIK_BUTTON);
  lk_tree_add_prop(t, bd, UIP_DISABLED, lk_v_bool(1));
  lk_tree_add_presentation_s(t, bd, "act", lk_v_i32(1));
  be = lk_tree_add_node_s(t, lk_str_c("be"), UIK_BUTTON);
  lk_tree_add_presentation_s(t, be, "act", lk_v_i32(2));
  dcol = lk_tree_add_node_s(t, lk_str_c("dcol"), UIK_COLUMN);
  lk_tree_add_prop(t, dcol, UIP_DISABLED, lk_v_bool(1));
  bs = lk_tree_add_node_s(t, lk_str_c("bs"), UIK_BUTTON);
  lk_tree_add_presentation_s(t, bs, "act", lk_v_i32(3));
  lk_tree_set_root(t, w);
  lk_tree_append_child(t, w, col);
  lk_tree_append_child(t, col, bd);
  lk_tree_append_child(t, col, be);
  lk_tree_append_child(t, col, dcol);
  lk_tree_append_child(t, dcol, bs);
  lk_ui_end_frame(ui);

  lk_ui_add_translator_s(ui, LK_EVENT_POINTER_DOWN, "act", 0, 0, 0, "Do");
  q = lk_ui_commands(ui);

  /* Click disabled button: no command */
  memset(&ev, 0, sizeof(ev));
  ev.type = LK_EVENT_POINTER_DOWN;
  ev.target = bd;
  lk_event_route(ui, &ev);
  CHECK_EQ(q->count, 0u);

  /* Click enabled twin: command fires */
  memset(&ev, 0, sizeof(ev));
  ev.type = LK_EVENT_POINTER_DOWN;
  ev.target = be;
  lk_event_route(ui, &ev);
  CHECK_EQ(q->count, 1u);

  /* Click button inside disabled subtree: suppressed too */
  memset(&ev, 0, sizeof(ev));
  ev.type = LK_EVENT_POINTER_DOWN;
  ev.target = bs;
  lk_event_route(ui, &ev);
  CHECK_EQ(q->count, 1u);

  END_TEST();
  lk_ui_destroy(ui);
}

static void test_state_survives_reparent(void) {
  lk_ui *ui = lk_ui_create(NULL);
  lk_tree *t;
  lk_ix w, ca, cb, ti;
  lk_state *st;
  lk_node_id ti_id;
  lk_value v;

  BEGIN_TEST("state: node move keeps state and focus");

  /* Frame 1: ti under column A */
  t = lk_ui_begin_frame(ui);
  w = lk_tree_add_node_s(t, lk_str_c("w"), UIK_WINDOW);
  ca = lk_tree_add_node_s(t, lk_str_c("ca"), UIK_COLUMN);
  cb = lk_tree_add_node_s(t, lk_str_c("cb"), UIK_COLUMN);
  ti = lk_tree_add_node_s(t, lk_str_c("ti"), UIK_TEXT_INPUT);
  lk_tree_add_prop(t, ti, UIP_TEXT, lk_v_cstr(t->intern, "hello"));
  lk_tree_add_prop(t, ti, UIP_FOCUSABLE, lk_v_bool(1));
  lk_tree_set_root(t, w);
  lk_tree_append_child(t, w, ca);
  lk_tree_append_child(t, w, cb);
  lk_tree_append_child(t, ca, ti);
  lk_ui_end_frame(ui);

  st = lk_ui_state(ui);
  ti_id = lk_intern_id(ui->intern, lk_str_c("ti"));
  CHECK(lk_focus_set(ui, lk_ui_tree(ui), ti_id));
  lk_state_set(st, ti_id, LKS_CURSOR_POS, lk_v_i32(3));

  /* Frame 2: same nodes, ti moved under column B */
  t = lk_ui_begin_frame(ui);
  w = lk_tree_add_node_s(t, lk_str_c("w"), UIK_WINDOW);
  ca = lk_tree_add_node_s(t, lk_str_c("ca"), UIK_COLUMN);
  cb = lk_tree_add_node_s(t, lk_str_c("cb"), UIK_COLUMN);
  ti = lk_tree_add_node_s(t, lk_str_c("ti"), UIK_TEXT_INPUT);
  lk_tree_add_prop(t, ti, UIP_TEXT, lk_v_cstr(t->intern, "hello"));
  lk_tree_add_prop(t, ti, UIP_FOCUSABLE, lk_v_bool(1));
  lk_tree_set_root(t, w);
  lk_tree_append_child(t, w, ca);
  lk_tree_append_child(t, w, cb);
  lk_tree_append_child(t, cb, ti);
  lk_ui_end_frame(ui);

  /* Retained state survived the move */
  v = lk_state_get(st, ti_id, LKS_CURSOR_POS);
  CHECK_EQ((unsigned)v.tag, (unsigned)UIV_I32);
  CHECK_EQ((unsigned)v.as.i, 3u);

  /* Focus survived the move */
  CHECK_EQ(ui->focused_id, ti_id);

  /* Frame 3: ti actually removed — state and focus must be cleared */
  t = lk_ui_begin_frame(ui);
  w = lk_tree_add_node_s(t, lk_str_c("w"), UIK_WINDOW);
  ca = lk_tree_add_node_s(t, lk_str_c("ca"), UIK_COLUMN);
  cb = lk_tree_add_node_s(t, lk_str_c("cb"), UIK_COLUMN);
  lk_tree_set_root(t, w);
  lk_tree_append_child(t, w, ca);
  lk_tree_append_child(t, w, cb);
  lk_ui_end_frame(ui);

  v = lk_state_get(st, ti_id, LKS_CURSOR_POS);
  CHECK_EQ((unsigned)v.tag, (unsigned)UIV_NONE);
  CHECK_EQ(ui->focused_id, 0u);

  END_TEST();
  lk_ui_destroy(ui);
}

static void test_text_input_cursor_only_when_focused(void) {
  lk_ui *ui;
  lk_ix ti;
  lk_rect *rects;
  lk_layout_cfg cfg;
  lk_render_list rl;
  lk_u32 focused_count;
  lk_u32 i;
  int cursor_found;

  BEGIN_TEST("text_input: cursor renders only when focused");

  ui = make_text_input_ui("hello", &ti); /* focuses ti */

  rects = (lk_rect *)calloc(lk_ui_tree(ui)->node_count, sizeof(lk_rect));
  memset(&cfg, 0, sizeof(cfg));
  cfg.text = lk_text_backend_stub();
  cfg.viewport_w = 800;
  cfg.viewport_h = 600;
  cfg.state = lk_ui_state(ui);
  lk_layout(lk_ui_tree(ui), &cfg, rects);

  /* Focused: render list contains the 1px cursor bar */
  memset(&rl, 0, sizeof(rl));
  lk_render_build(lk_ui_tree(ui), rects, NULL, lk_ui_state(ui), &rl);
  focused_count = rl.count;
  cursor_found = 0;
  for (i = 0; i < rl.count; i++) {
    if (rl.cmds[i].op == LK_ROP_FILL_RECT && rl.cmds[i].rect.w == 1) {
      cursor_found = 1;
    }
  }
  CHECK(cursor_found);

  /* Unfocused: exactly the cursor command disappears */
  lk_focus_clear(ui);
  lk_render_build(lk_ui_tree(ui), rects, NULL, lk_ui_state(ui), &rl);
  CHECK_EQ(rl.count + 1, focused_count);
  cursor_found = 0;
  for (i = 0; i < rl.count; i++) {
    if (rl.cmds[i].op == LK_ROP_FILL_RECT && rl.cmds[i].rect.w == 1) {
      cursor_found = 1;
    }
  }
  CHECK(!cursor_found);

  lk_render_list_destroy(&rl);
  free(rects);
  END_TEST();
  lk_ui_destroy(ui);
}

static void test_dropdown_padding_click_stays_open(void) {
  lk_ui *ui;
  lk_ix dd;
  lk_rect *rects;
  lk_layout_cfg cfg;
  lk_state *st;
  lk_node_id dd_id;
  lk_event ev;
  lk_rect tr;
  lk_ix hit;

  BEGIN_TEST("dropdown: popup-padding click keeps popup open");

  ui = make_dropdown_ui(&dd);
  st = lk_ui_state(ui);
  dd_id = lk_intern_id(ui->intern, lk_str_c("dd"));

  rects = (lk_rect *)calloc(lk_ui_tree(ui)->node_count, sizeof(lk_rect));
  memset(&cfg, 0, sizeof(cfg));
  cfg.text = lk_text_backend_stub();
  cfg.viewport_w = 800;
  cfg.viewport_h = 600;
  lk_ui_resolve_styles(ui);
  cfg.styles = lk_ui_styles(ui);
  cfg.state = st;
  lk_layout(lk_ui_tree(ui), &cfg, rects); /* stores trigger rect */

  tr = rects[dd];

  /* Open by clicking the trigger */
  memset(&ev, 0, sizeof(ev));
  ev.type = LK_EVENT_POINTER_DOWN;
  ev.target = dd;
  ev.data.pointer.x = tr.x + tr.w / 2;
  ev.data.pointer.y = tr.y + tr.h / 2;
  lk_event_route(ui, &ev);
  CHECK_EQ((int)lk_state_get(st, dd_id, LKS_EXPANDED).as.i, 1);

  /* Click in the popup's padding zone (inside popup, above the first
   * option row: inset is padding 6 + border 1 = 7). */
  {
    lk_i32 px = tr.x + tr.w / 2;
    lk_i32 py = tr.y + tr.h + 2;

    hit = lk_hit_test_overlay(ui, rects, &cfg, px, py);
    CHECK_EQ(hit, dd); /* padding resolves to the dropdown itself */

    memset(&ev, 0, sizeof(ev));
    ev.type = LK_EVENT_POINTER_DOWN;
    ev.target = hit;
    ev.data.pointer.x = px;
    ev.data.pointer.y = py;
    lk_event_route(ui, &ev);
    CHECK_EQ((unsigned)ev.handled, 1u);
    /* Popup must stay open */
    CHECK_EQ((int)lk_state_get(st, dd_id, LKS_EXPANDED).as.i, 1);
  }

  /* A real trigger click still closes */
  memset(&ev, 0, sizeof(ev));
  ev.type = LK_EVENT_POINTER_DOWN;
  ev.target = dd;
  ev.data.pointer.x = tr.x + tr.w / 2;
  ev.data.pointer.y = tr.y + tr.h / 2;
  lk_event_route(ui, &ev);
  CHECK_EQ((int)lk_state_get(st, dd_id, LKS_EXPANDED).as.i, 0);

  free(rects);
  END_TEST();
  lk_ui_destroy(ui);
}

static void test_dropdown_hover_follows_pointer(void) {
  lk_ui *ui;
  lk_ix dd;
  lk_rect *rects;
  lk_layout_cfg cfg;
  lk_state *st;
  lk_node_id dd_id;
  lk_event ev;
  lk_rect tr;
  lk_ix hit;
  lk_i32 row_h = 24;   /* stub text height 16 + option pad 4*2 */
  lk_i32 inset = 7;    /* dropdown default padding 6 + border 1 */

  BEGIN_TEST("dropdown: pointer move updates hover index");

  ui = make_dropdown_ui(&dd);
  st = lk_ui_state(ui);
  dd_id = lk_intern_id(ui->intern, lk_str_c("dd"));

  rects = (lk_rect *)calloc(lk_ui_tree(ui)->node_count, sizeof(lk_rect));
  memset(&cfg, 0, sizeof(cfg));
  cfg.text = lk_text_backend_stub();
  cfg.viewport_w = 800;
  cfg.viewport_h = 600;
  lk_ui_resolve_styles(ui);
  cfg.styles = lk_ui_styles(ui);
  cfg.state = st;
  lk_layout(lk_ui_tree(ui), &cfg, rects);

  open_dropdown(ui, dd); /* hover starts at selection (0) */

  tr = rects[dd];

  /* Move over option row 1 (second option) */
  {
    lk_i32 px = tr.x + tr.w / 2;
    lk_i32 py = tr.y + tr.h + inset + row_h + row_h / 2;

    hit = lk_hit_test_overlay(ui, rects, &cfg, px, py);
    CHECK(hit > 0);
    CHECK_EQ((unsigned)lk_ui_tree(ui)->nodes[hit].kind, (unsigned)UIK_OPTION);

    memset(&ev, 0, sizeof(ev));
    ev.type = LK_EVENT_POINTER_MOVE;
    ev.target = hit;
    ev.data.pointer.x = px;
    ev.data.pointer.y = py;
    lk_event_route(ui, &ev);
    CHECK_EQ((int)lk_state_get(st, dd_id, LKS_HOVER_INDEX).as.i, 1);
  }

  /* Move over option row 2 (third option) */
  {
    lk_i32 px = tr.x + tr.w / 2;
    lk_i32 py = tr.y + tr.h + inset + row_h * 2 + row_h / 2;

    hit = lk_hit_test_overlay(ui, rects, &cfg, px, py);
    CHECK(hit > 0);

    memset(&ev, 0, sizeof(ev));
    ev.type = LK_EVENT_POINTER_MOVE;
    ev.target = hit;
    ev.data.pointer.x = px;
    ev.data.pointer.y = py;
    lk_event_route(ui, &ev);
    CHECK_EQ((int)lk_state_get(st, dd_id, LKS_HOVER_INDEX).as.i, 2);
  }

  free(rects);
  END_TEST();
  lk_ui_destroy(ui);
}

/* ================================================================
 * Main
 * ================================================================ */

/* ---- text backend contract tests (stage A) ----
 *
 * Stub metrics: 8 px advance per codepoint, h=16, baseline=12,
 * line_height=16, independent of font_id/font_size.
 */

static void test_text_stub_measure_x_agree(void) {
  const lk_text_backend *tb = lk_text_backend_stub();
  lk_text_metrics m;
  lk_str run;

  BEGIN_TEST("text stub: x_from_index(len) == measure().w");

  /* ASCII run: 5 codepoints -> 40 px */
  run = lk_str_c("hello");
  tb->measure(tb->ud, run, 0, 0, &m);
  CHECK_EQ((unsigned)m.w, 40u);
  CHECK_EQ((unsigned)m.h, 16u);
  CHECK_EQ((unsigned)m.baseline, 12u);
  CHECK_EQ((unsigned)tb->x_from_index(tb->ud, run, 0, 0, run.len),
           (unsigned)m.w);

  /* Empty run */
  run = lk_str_c("");
  tb->measure(tb->ud, run, 0, 0, &m);
  CHECK_EQ((unsigned)m.w, 0u);
  CHECK_EQ((unsigned)tb->x_from_index(tb->ud, run, 0, 0, 0), 0u);

  /* Multibyte run: "a" + e-acute (2 bytes) + CJK (3 bytes) = 3 cp */
  run = lk_str_c("a\xC3\xA9\xE6\x97\xA5");
  tb->measure(tb->ud, run, 0, 0, &m);
  CHECK_EQ((unsigned)m.w, 24u);
  CHECK_EQ((unsigned)tb->x_from_index(tb->ud, run, 0, 0, run.len),
           (unsigned)m.w);

  /* Metrics independent of font_id/font_size */
  run = lk_str_c("hello");
  tb->measure(tb->ud, run, 7, 99, &m);
  CHECK_EQ((unsigned)m.w, 40u);
  CHECK_EQ((unsigned)m.h, 16u);
  CHECK_EQ((unsigned)tb->line_height(tb->ud, 0, 0), 16u);
  CHECK_EQ((unsigned)tb->line_height(tb->ud, 7, 99), 16u);

  END_TEST();
}

static void test_text_stub_x_from_index_multibyte(void) {
  const lk_text_backend *tb = lk_text_backend_stub();
  /* bytes: 'a' [0], e-acute [1..2], CJK [3..5]; len 6, 3 codepoints */
  lk_str run = lk_str_c("a\xC3\xA9\xE6\x97\xA5");

  BEGIN_TEST("text stub: x_from_index counts codepoints");

  CHECK_EQ((unsigned)tb->x_from_index(tb->ud, run, 0, 0, 0), 0u);
  CHECK_EQ((unsigned)tb->x_from_index(tb->ud, run, 0, 0, 1), 8u);
  CHECK_EQ((unsigned)tb->x_from_index(tb->ud, run, 0, 0, 3), 16u);
  CHECK_EQ((unsigned)tb->x_from_index(tb->ud, run, 0, 0, 6), 24u);

  /* Mid-codepoint indices snap DOWN to the previous boundary */
  CHECK_EQ((unsigned)tb->x_from_index(tb->ud, run, 0, 0, 2), 8u);
  CHECK_EQ((unsigned)tb->x_from_index(tb->ud, run, 0, 0, 4), 16u);
  CHECK_EQ((unsigned)tb->x_from_index(tb->ud, run, 0, 0, 5), 16u);

  /* Out-of-range clamps to len */
  CHECK_EQ((unsigned)tb->x_from_index(tb->ud, run, 0, 0, 99), 24u);

  END_TEST();
}

static void test_text_stub_index_from_x_rounding(void) {
  const lk_text_backend *tb = lk_text_backend_stub();
  lk_str run;

  BEGIN_TEST("text stub: index_from_x nearest boundary");

  /* ASCII "ab": boundaries at byte 0 (x=0), 1 (x=8), 2 (x=16) */
  run = lk_str_c("ab");
  CHECK_EQ(tb->index_from_x(tb->ud, run, 0, 0, -5), 0u); /* clamp low */
  CHECK_EQ(tb->index_from_x(tb->ud, run, 0, 0, 0), 0u);
  CHECK_EQ(tb->index_from_x(tb->ud, run, 0, 0, 3), 0u);
  CHECK_EQ(tb->index_from_x(tb->ud, run, 0, 0, 4), 1u); /* tie rounds up */
  CHECK_EQ(tb->index_from_x(tb->ud, run, 0, 0, 11), 1u);
  CHECK_EQ(tb->index_from_x(tb->ud, run, 0, 0, 12), 2u); /* tie rounds up */
  CHECK_EQ(tb->index_from_x(tb->ud, run, 0, 0, 100), 2u); /* clamp high */

  /* Multibyte: e-acute [0..1], CJK [2..4]; boundaries at bytes 0, 2, 5 */
  run = lk_str_c("\xC3\xA9\xE6\x97\xA5");
  CHECK_EQ(tb->index_from_x(tb->ud, run, 0, 0, 4), 2u);
  CHECK_EQ(tb->index_from_x(tb->ud, run, 0, 0, 7), 2u);
  CHECK_EQ(tb->index_from_x(tb->ud, run, 0, 0, 11), 2u);
  CHECK_EQ(tb->index_from_x(tb->ud, run, 0, 0, 12), 5u);
  CHECK_EQ(tb->index_from_x(tb->ud, run, 0, 0, 100), 5u); /* clamp to len */

  END_TEST();
}

static void test_text_stub_register_font_ids(void) {
  const lk_text_backend *tb = lk_text_backend_stub();
  lk_u16 a;
  lk_u16 b;

  BEGIN_TEST("text stub: register_font increasing ids");

  a = tb->register_font(tb->ud, "fake-a.ttf");
  b = tb->register_font(tb->ud, "fake-b.ttf");
  CHECK(a >= 1);
  CHECK_EQ((unsigned)b, (unsigned)(a + 1));

  END_TEST();
}

static void test_render_text_carries_font(void) {
  lk_ui *ui;
  lk_tree *tree;
  lk_theme *th;
  lk_style s;
  lk_ix w, lbl;
  lk_rect *rects;
  lk_layout_cfg cfg;
  lk_render_list rl;
  const lk_style *styles;
  lk_u32 i;
  int found;

  BEGIN_TEST("render: DRAW_TEXT carries style font_id/size");

  ui = lk_ui_create(NULL);

  /* Theme rule: labels use font_id 3 at size 24 */
  th = lk_ui_theme(ui);
  memset(&s, 0, sizeof(s));
  s.font_id = 3;
  s.font_size = 24;
  lk_theme_add_rule(th, UIK_LABEL, 0, 0, &s,
                    LK_SF_FONT_ID | LK_SF_FONT_SIZE);

  tree = lk_ui_begin_frame(ui);
  w = lk_tree_add_node_s(tree, lk_str_c("w"), UIK_WINDOW);
  lbl = lk_tree_add_node_s(tree, lk_str_c("lbl"), UIK_LABEL);
  lk_tree_add_prop(tree, lbl, UIP_TEXT, lk_v_cstr(tree->intern, "Hi"));
  lk_tree_set_root(tree, w);
  lk_tree_append_child(tree, w, lbl);
  lk_ui_end_frame(ui);

  lk_ui_resolve_styles(ui);
  styles = lk_ui_styles(ui);
  CHECK(styles != NULL);

  rects = (lk_rect *)calloc(lk_ui_tree(ui)->node_count, sizeof(lk_rect));
  memset(&cfg, 0, sizeof(cfg));
  cfg.text = lk_text_backend_stub();
  cfg.viewport_w = 800;
  cfg.viewport_h = 600;
  cfg.styles = styles;
  CHECK(lk_layout(lk_ui_tree(ui), &cfg, rects));

  memset(&rl, 0, sizeof(rl));
  lk_render_build(lk_ui_tree(ui), rects, styles, NULL, &rl);

  found = 0;
  for (i = 0; i < rl.count; i++) {
    if (rl.cmds[i].op == LK_ROP_DRAW_TEXT) {
      found = 1;
      CHECK_EQ((unsigned)rl.cmds[i].font_id, 3u);
      CHECK_EQ((unsigned)rl.cmds[i].font_size, 24u);
    }
  }
  CHECK(found);

  /* Without styles (fallback path): default theme sets no fonts,
   * so DRAW_TEXT carries 0/0 */
  lk_render_build(lk_ui_tree(ui), rects, NULL, NULL, &rl);
  found = 0;
  for (i = 0; i < rl.count; i++) {
    if (rl.cmds[i].op == LK_ROP_DRAW_TEXT) {
      found = 1;
      CHECK_EQ((unsigned)rl.cmds[i].font_id, 0u);
      CHECK_EQ((unsigned)rl.cmds[i].font_size, 0u);
    }
  }
  CHECK(found);

  lk_render_list_destroy(&rl);
  free(rects);
  END_TEST();
  lk_ui_destroy(ui);
}

/* ---- text input correctness tests (stage C) ----
 *
 * Multibyte fixture used throughout: "a\xC3\xA9\xE6\x97\xA5"
 * ("a" + e-acute + CJK) — bytes: 'a' [0], e-acute [1..2], CJK [3..5];
 * len 6, 3 codepoints.  Stub advance: 8 px per codepoint.
 */

static void test_text_input_multibyte_arrows(void) {
  lk_ui *ui;
  lk_ix ti;
  lk_event ev;
  lk_state *st;
  lk_node_id ti_id;
  lk_value v;
  static const unsigned expected_left[3] = {3u, 1u, 0u};
  static const unsigned expected_right[3] = {1u, 3u, 6u};
  int i;

  BEGIN_TEST("text_input: LEFT/RIGHT move whole codepoints");

  ui = make_text_input_ui("a\xC3\xA9\xE6\x97\xA5", &ti);
  st = lk_ui_state(ui);
  ti_id = lk_intern_id(ui->intern, lk_str_c("ti"));
  lk_state_set(st, ti_id, LKS_CURSOR_POS, lk_v_i32(6));

  /* LEFT walks boundaries 6 -> 3 -> 1 -> 0, then stays at 0 */
  for (i = 0; i < 3; i++) {
    memset(&ev, 0, sizeof(ev));
    ev.type = LK_EVENT_KEY_DOWN;
    ev.target = ti;
    ev.data.key.keycode = LKK_LEFT;
    lk_event_route(ui, &ev);
    v = lk_state_get(st, ti_id, LKS_CURSOR_POS);
    CHECK_EQ((unsigned)v.as.i, expected_left[i]);
  }

  memset(&ev, 0, sizeof(ev));
  ev.type = LK_EVENT_KEY_DOWN;
  ev.target = ti;
  ev.data.key.keycode = LKK_LEFT;
  lk_event_route(ui, &ev);
  v = lk_state_get(st, ti_id, LKS_CURSOR_POS);
  CHECK_EQ((unsigned)v.as.i, 0u);

  /* RIGHT walks 0 -> 1 -> 3 -> 6, then stays at 6 */
  for (i = 0; i < 3; i++) {
    memset(&ev, 0, sizeof(ev));
    ev.type = LK_EVENT_KEY_DOWN;
    ev.target = ti;
    ev.data.key.keycode = LKK_RIGHT;
    lk_event_route(ui, &ev);
    v = lk_state_get(st, ti_id, LKS_CURSOR_POS);
    CHECK_EQ((unsigned)v.as.i, expected_right[i]);
  }

  memset(&ev, 0, sizeof(ev));
  ev.type = LK_EVENT_KEY_DOWN;
  ev.target = ti;
  ev.data.key.keycode = LKK_RIGHT;
  lk_event_route(ui, &ev);
  v = lk_state_get(st, ti_id, LKS_CURSOR_POS);
  CHECK_EQ((unsigned)v.as.i, 6u);

  END_TEST();
  lk_ui_destroy(ui);
}

static void test_text_input_multibyte_backspace_delete(void) {
  lk_ui *ui;
  lk_ix ti;
  lk_event ev;
  lk_state *st;
  lk_node_id ti_id;
  lk_value v;
  lk_str text;

  BEGIN_TEST("text_input: BACKSPACE/DELETE remove whole codepoints");

  ui = make_text_input_ui("a\xC3\xA9\xE6\x97\xA5", &ti);
  st = lk_ui_state(ui);
  ti_id = lk_intern_id(ui->intern, lk_str_c("ti"));

  /* Cursor after e-acute (byte 3); BACKSPACE removes both its bytes */
  lk_state_set(st, ti_id, LKS_CURSOR_POS, lk_v_i32(3));
  memset(&ev, 0, sizeof(ev));
  ev.type = LK_EVENT_KEY_DOWN;
  ev.target = ti;
  ev.data.key.keycode = LKK_BACKSPACE;
  lk_event_route(ui, &ev);

  v = lk_state_get(st, ti_id, LKS_TEXT_BUF);
  CHECK_EQ((unsigned)v.tag, (unsigned)UIV_STR);
  text = lk_intern_str(ui->intern, v.as.str_id);
  CHECK_EQ(text.len, 4u);
  CHECK(memcmp(text.ptr, "a\xE6\x97\xA5", 4) == 0);
  v = lk_state_get(st, ti_id, LKS_CURSOR_POS);
  CHECK_EQ((unsigned)v.as.i, 1u);

  /* DELETE removes all three bytes of the CJK codepoint */
  memset(&ev, 0, sizeof(ev));
  ev.type = LK_EVENT_KEY_DOWN;
  ev.target = ti;
  ev.data.key.keycode = LKK_DELETE;
  lk_event_route(ui, &ev);

  v = lk_state_get(st, ti_id, LKS_TEXT_BUF);
  text = lk_intern_str(ui->intern, v.as.str_id);
  CHECK_EQ(text.len, 1u);
  CHECK(text.ptr[0] == 'a');
  v = lk_state_get(st, ti_id, LKS_CURSOR_POS);
  CHECK_EQ((unsigned)v.as.i, 1u);

  END_TEST();
  lk_ui_destroy(ui);
}

static void test_text_input_insert_cap_boundary(void) {
  lk_ui *ui;
  lk_ix ti;
  lk_event ev;
  lk_state *st;
  lk_node_id ti_id;
  lk_value v;
  lk_str text;
  char big[LK_TEXT_INPUT_MAX];

  BEGIN_TEST("text_input: cap truncation lands on codepoint boundary");

  ui = make_text_input_ui("", &ti);
  st = lk_ui_state(ui);
  ti_id = lk_intern_id(ui->intern, lk_str_c("ti"));

  /* Fill the buffer to 2 bytes below the max payload (1023) */
  memset(big, 'a', 1021);
  text.ptr = big;
  text.len = 1021;
  v.tag = UIV_STR;
  v.as.str_id = lk_intern_id(ui->intern, text);
  lk_state_set(st, ti_id, LKS_TEXT_BUF, v);
  lk_state_set(st, ti_id, LKS_CURSOR_POS, lk_v_i32(1021));

  /* Insert "a" + e-acute (3 bytes): only 2 fit, and byte 2 is
   * mid-sequence, so exactly "a" (1 byte) must be inserted */
  memset(&ev, 0, sizeof(ev));
  ev.type = LK_EVENT_TEXT;
  ev.target = ti;
  ev.data.text.buf[0] = 'a';
  ev.data.text.buf[1] = (char)0xC3;
  ev.data.text.buf[2] = (char)0xA9;
  ev.data.text.len = 3;
  lk_event_route(ui, &ev);

  CHECK_EQ((unsigned)ev.handled, 1u);
  v = lk_state_get(st, ti_id, LKS_TEXT_BUF);
  text = lk_intern_str(ui->intern, v.as.str_id);
  CHECK_EQ(text.len, 1022u);
  CHECK(text.ptr[1021] == 'a');
  v = lk_state_get(st, ti_id, LKS_CURSOR_POS);
  CHECK_EQ((unsigned)v.as.i, 1022u);

  /* Insert CJK (3 bytes): 1 byte of room, boundary snap drops the
   * whole codepoint — consumed, buffer unchanged */
  memset(&ev, 0, sizeof(ev));
  ev.type = LK_EVENT_TEXT;
  ev.target = ti;
  ev.data.text.buf[0] = (char)0xE6;
  ev.data.text.buf[1] = (char)0x97;
  ev.data.text.buf[2] = (char)0xA5;
  ev.data.text.len = 3;
  lk_event_route(ui, &ev);

  CHECK_EQ((unsigned)ev.handled, 1u);
  v = lk_state_get(st, ti_id, LKS_TEXT_BUF);
  text = lk_intern_str(ui->intern, v.as.str_id);
  CHECK_EQ(text.len, 1022u);

  END_TEST();
  lk_ui_destroy(ui);
}

static void test_text_input_paste_cap_boundary(void) {
  lk_ui *ui;
  lk_ix ti;
  lk_event ev;
  lk_state *st;
  lk_node_id ti_id;
  lk_value v;
  lk_str text;
  char big[LK_TEXT_INPUT_MAX];

  BEGIN_TEST("text_input: paste at cap truncates on codepoint boundary");

  ui = make_text_input_ui("", &ti);
  st = lk_ui_state(ui);
  ti_id = lk_intern_id(ui->intern, lk_str_c("ti"));
  lk_ui_set_clipboard(ui, mock_clipboard_get, mock_clipboard_set, NULL);

  /* Buffer at 1021 bytes; clipboard "a" + e-acute + CJK (6 bytes).
   * 2 bytes of room: "a" fits, e-acute would be split — so exactly
   * 1 byte is pasted. */
  memset(big, 'a', 1021);
  text.ptr = big;
  text.len = 1021;
  v.tag = UIV_STR;
  v.as.str_id = lk_intern_id(ui->intern, text);
  lk_state_set(st, ti_id, LKS_TEXT_BUF, v);
  lk_state_set(st, ti_id, LKS_CURSOR_POS, lk_v_i32(1021));

  strcpy(g_mock_clipboard, "a\xC3\xA9\xE6\x97\xA5");

  memset(&ev, 0, sizeof(ev));
  ev.type = LK_EVENT_KEY_DOWN;
  ev.target = ti;
  ev.data.key.keycode = LKK_V;
  ev.mods = LK_MOD_CTRL;
  lk_event_route(ui, &ev);

  CHECK_EQ((unsigned)ev.handled, 1u);
  v = lk_state_get(st, ti_id, LKS_TEXT_BUF);
  text = lk_intern_str(ui->intern, v.as.str_id);
  CHECK_EQ(text.len, 1022u);
  CHECK(text.ptr[1021] == 'a');

  END_TEST();
  lk_ui_destroy(ui);
}

static void test_text_input_cursor_x_from_index(void) {
  lk_ui *ui;
  lk_ix ti;
  lk_rect *rects;
  lk_layout_cfg cfg;
  lk_state *st;
  lk_node_id ti_id;
  lk_value v;

  BEGIN_TEST("text_input: cursor x via x_from_index (multibyte exact)");

  ui = make_text_input_ui("a\xC3\xA9\xE6\x97\xA5", &ti);
  st = lk_ui_state(ui);
  ti_id = lk_intern_id(ui->intern, lk_str_c("ti"));

  rects = (lk_rect *)calloc(lk_ui_tree(ui)->node_count, sizeof(lk_rect));
  memset(&cfg, 0, sizeof(cfg));
  cfg.text = lk_text_backend_stub();
  cfg.viewport_w = 800;
  cfg.viewport_h = 600;
  cfg.state = st;

  /* Cursor after e-acute (byte 3, 2 codepoints in) -> 16 px */
  lk_state_set(st, ti_id, LKS_CURSOR_POS, lk_v_i32(3));
  CHECK(lk_layout(lk_ui_tree(ui), &cfg, rects));
  v = lk_state_get(st, ti_id, LKS_CURSOR_X);
  CHECK_EQ((unsigned)v.tag, (unsigned)UIV_I32);
  CHECK_EQ((unsigned)v.as.i, 16u);

  /* Cursor at end (byte 6, 3 codepoints) -> 24 px == measure().w */
  lk_state_set(st, ti_id, LKS_CURSOR_POS, lk_v_i32(6));
  CHECK(lk_layout(lk_ui_tree(ui), &cfg, rects));
  v = lk_state_get(st, ti_id, LKS_CURSOR_X);
  CHECK_EQ((unsigned)v.as.i, 24u);

  free(rects);
  END_TEST();
  lk_ui_destroy(ui);
}

static void test_text_input_selection_rect_exact(void) {
  lk_ui *ui;
  lk_ix ti;
  lk_rect *rects;
  lk_layout_cfg cfg;
  lk_state *st;
  lk_node_id ti_id;
  lk_render_list rl;
  lk_u32 i;
  int found;

  BEGIN_TEST("text_input: selection rect exact px (multibyte)");

  ui = make_text_input_ui("a\xC3\xA9\xE6\x97\xA5", &ti);
  st = lk_ui_state(ui);
  ti_id = lk_intern_id(ui->intern, lk_str_c("ti"));

  /* Select e-acute + CJK: bytes 1..6 = codepoints 1..3, so the
   * highlight must span exactly 8..24 px from the text origin */
  lk_state_set(st, ti_id, LKS_CURSOR_POS, lk_v_i32(6));
  lk_state_set(st, ti_id, LKS_SELECTION_START, lk_v_i32(1));
  lk_state_set(st, ti_id, LKS_SELECTION_END, lk_v_i32(6));

  rects = (lk_rect *)calloc(lk_ui_tree(ui)->node_count, sizeof(lk_rect));
  memset(&cfg, 0, sizeof(cfg));
  cfg.text = lk_text_backend_stub();
  cfg.viewport_w = 800;
  cfg.viewport_h = 600;
  cfg.state = st;
  CHECK(lk_layout(lk_ui_tree(ui), &cfg, rects));

  /* Endpoint x-offsets stashed by measure are exact: 1 cp -> 8 px,
   * 3 cp -> 24 px */
  {
    lk_value v = lk_state_get(st, ti_id, LKS_SEL_X0);
    CHECK_EQ((unsigned)v.tag, (unsigned)UIV_I32);
    CHECK_EQ((unsigned)v.as.i, 8u);
    v = lk_state_get(st, ti_id, LKS_SEL_X1);
    CHECK_EQ((unsigned)v.as.i, 24u);
  }

  memset(&rl, 0, sizeof(rl));
  lk_render_build(lk_ui_tree(ui), rects, NULL, st, &rl);

  /* Selection fill is the only command with alpha 128.  Anchor the
   * x assertion to the DRAW_TEXT origin so the render-side inset
   * (fallback theme padding/border) cancels out. */
  {
    lk_i32 text_x = 0;
    int text_found = 0;

    for (i = 0; i < rl.count; i++) {
      if (rl.cmds[i].op == LK_ROP_DRAW_TEXT) {
        text_x = rl.cmds[i].rect.x;
        text_found = 1;
      }
    }
    CHECK(text_found);

    found = 0;
    for (i = 0; i < rl.count; i++) {
      if (rl.cmds[i].op == LK_ROP_FILL_RECT && rl.cmds[i].color.a == 128) {
        found = 1;
        CHECK_EQ((unsigned)rl.cmds[i].rect.x, (unsigned)(text_x + 8));
        CHECK_EQ((unsigned)rl.cmds[i].rect.w, 16u);
      }
    }
    CHECK(found);
  }

  lk_render_list_destroy(&rl);
  free(rects);
  END_TEST();
  lk_ui_destroy(ui);
}

static void test_text_input_click_to_position(void) {
  lk_ui *ui;
  lk_ix ti;
  lk_rect *rects;
  lk_layout_cfg cfg;
  lk_state *st;
  lk_node_id ti_id;
  lk_event ev;
  lk_value v;
  lk_i32 origin;

  BEGIN_TEST("text_input: click-to-position via index_from_x");

  ui = make_text_input_ui("a\xC3\xA9\xE6\x97\xA5", &ti);
  st = lk_ui_state(ui);
  ti_id = lk_intern_id(ui->intern, lk_str_c("ti"));

  rects = (lk_rect *)calloc(lk_ui_tree(ui)->node_count, sizeof(lk_rect));
  memset(&cfg, 0, sizeof(cfg));
  cfg.text = lk_text_backend_stub();
  cfg.viewport_w = 800;
  cfg.viewport_h = 600;
  cfg.state = st;
  CHECK(lk_layout(lk_ui_tree(ui), &cfg, rects)); /* stashes origin */

  v = lk_state_get(st, ti_id, LKS_TEXT_ORIGIN_X);
  CHECK_EQ((unsigned)v.tag, (unsigned)UIV_I32);
  origin = (lk_i32)v.as.i;
  CHECK_EQ((unsigned)origin, (unsigned)rects[ti].x);

  /* No backend installed on the UI: event bubbles (handled == 0) */
  memset(&ev, 0, sizeof(ev));
  ev.type = LK_EVENT_POINTER_DOWN;
  ev.target = ti;
  ev.data.pointer.x = origin + 5;
  lk_event_route(ui, &ev);
  CHECK_EQ((unsigned)ev.handled, 0u);

  lk_ui_set_text_backend(ui, lk_text_backend_stub());

  /* Left half of first glyph -> boundary before it (byte 0) */
  memset(&ev, 0, sizeof(ev));
  ev.type = LK_EVENT_POINTER_DOWN;
  ev.target = ti;
  ev.data.pointer.x = origin + 3;
  lk_event_route(ui, &ev);
  CHECK_EQ((unsigned)ev.handled, 1u);
  v = lk_state_get(st, ti_id, LKS_CURSOR_POS);
  CHECK_EQ((unsigned)v.as.i, 0u);

  /* Right half of first glyph -> boundary after it (byte 1) */
  memset(&ev, 0, sizeof(ev));
  ev.type = LK_EVENT_POINTER_DOWN;
  ev.target = ti;
  ev.data.pointer.x = origin + 5;
  lk_event_route(ui, &ev);
  v = lk_state_get(st, ti_id, LKS_CURSOR_POS);
  CHECK_EQ((unsigned)v.as.i, 1u);

  /* Right half of e-acute -> boundary after it (byte 3) */
  memset(&ev, 0, sizeof(ev));
  ev.type = LK_EVENT_POINTER_DOWN;
  ev.target = ti;
  ev.data.pointer.x = origin + 13;
  lk_event_route(ui, &ev);
  v = lk_state_get(st, ti_id, LKS_CURSOR_POS);
  CHECK_EQ((unsigned)v.as.i, 3u);

  /* Click past the end -> clamp to len (byte 6) */
  memset(&ev, 0, sizeof(ev));
  ev.type = LK_EVENT_POINTER_DOWN;
  ev.target = ti;
  ev.data.pointer.x = origin + 500;
  lk_event_route(ui, &ev);
  v = lk_state_get(st, ti_id, LKS_CURSOR_POS);
  CHECK_EQ((unsigned)v.as.i, 6u);

  /* Click before the origin -> 0 */
  memset(&ev, 0, sizeof(ev));
  ev.type = LK_EVENT_POINTER_DOWN;
  ev.target = ti;
  ev.data.pointer.x = origin - 50;
  lk_event_route(ui, &ev);
  v = lk_state_get(st, ti_id, LKS_CURSOR_POS);
  CHECK_EQ((unsigned)v.as.i, 0u);

  /* Click clears any selection and keeps focus on the input */
  lk_state_set(st, ti_id, LKS_SELECTION_START, lk_v_i32(1));
  lk_state_set(st, ti_id, LKS_SELECTION_END, lk_v_i32(6));
  memset(&ev, 0, sizeof(ev));
  ev.type = LK_EVENT_POINTER_DOWN;
  ev.target = ti;
  ev.data.pointer.x = origin + 5;
  lk_event_route(ui, &ev);
  v = lk_state_get(st, ti_id, LKS_SELECTION_START);
  CHECK_EQ((unsigned)v.as.i, 0u);
  v = lk_state_get(st, ti_id, LKS_SELECTION_END);
  CHECK_EQ((unsigned)v.as.i, 0u);
  CHECK_EQ(ui->focused_id, ti_id);

  free(rects);
  END_TEST();
  lk_ui_destroy(ui);
}

/* ================================================================
 * Tests: split panes (UIK_SPLIT_H / UIK_SPLIT_V) + pointer capture
 * ================================================================ */

/* Build a plain tree: window "w" > split "sp" > column "c1", column
 * "c2".  Caller owns the tree. */
static lk_tree *make_split_tree(lk_kind kind, lk_ix *out_sp, lk_ix *out_c1,
                                lk_ix *out_c2) {
  lk_tree *t = lk_tree_create(NULL);
  lk_ix w = lk_tree_add_node_s(t, lk_str_c("w"), UIK_WINDOW);
  lk_ix sp = lk_tree_add_node_s(t, lk_str_c("sp"), kind);
  lk_ix c1 = lk_tree_add_node_s(t, lk_str_c("c1"), UIK_COLUMN);
  lk_ix c2 = lk_tree_add_node_s(t, lk_str_c("c2"), UIK_COLUMN);

  lk_tree_set_root(t, w);
  lk_tree_append_child(t, w, sp);
  lk_tree_append_child(t, sp, c1);
  lk_tree_append_child(t, sp, c2);

  *out_sp = sp;
  *out_c1 = c1;
  *out_c2 = c2;
  return t;
}

/* Same shape via lk_ui (retained state + stashed geometry). */
static lk_ui *make_split_ui(lk_kind kind, lk_i32 ratio_prop, lk_ix *out_sp,
                            lk_ix *out_c1, lk_ix *out_c2) {
  lk_ui *ui = lk_ui_create(NULL);
  lk_tree *t;
  lk_ix w, sp, c1, c2;
  const lk_tree *cur;

  t = lk_ui_begin_frame(ui);
  w = lk_tree_add_node_s(t, lk_str_c("w"), UIK_WINDOW);
  sp = lk_tree_add_node_s(t, lk_str_c("sp"), kind);

  if (ratio_prop >= 0) {
    lk_tree_add_prop(t, sp, UIP_SPLIT_RATIO, lk_v_i32(ratio_prop));
  }

  c1 = lk_tree_add_node_s(t, lk_str_c("c1"), UIK_COLUMN);
  c2 = lk_tree_add_node_s(t, lk_str_c("c2"), UIK_COLUMN);
  lk_tree_set_root(t, w);
  lk_tree_append_child(t, w, sp);
  lk_tree_append_child(t, sp, c1);
  lk_tree_append_child(t, sp, c2);
  lk_ui_end_frame(ui);

  cur = lk_ui_tree(ui);
  *out_sp = lk_tree_find_by_id(cur, lk_intern_id(ui->intern, lk_str_c("sp")));
  *out_c1 = lk_tree_find_by_id(cur, lk_intern_id(ui->intern, lk_str_c("c1")));
  *out_c2 = lk_tree_find_by_id(cur, lk_intern_id(ui->intern, lk_str_c("c2")));
  return ui;
}

static void test_split_h_layout_default_ratio(void) {
  /* Viewport 400x300, padding 0.  avail = 400 - 5 = 395.
   * first = 395*500/1000 = 197; divider at x=197..202; second = 198. */
  lk_ix sp, c1, c2;
  lk_tree *t = make_split_tree(UIK_SPLIT_H, &sp, &c1, &c2);
  lk_rect *r;

  BEGIN_TEST("split_h: default ratio halves minus divider");

  r = run_layout(t, 400, 300);
  CHECK(r != NULL);
  if (r) {
    CHECK_RECT(r[c1], 0, 0, 197, 300);
    CHECK_RECT(r[c2], 202, 0, 198, 300);

    /* Divider band renders in the gap (from the split's own rect) */
    {
      lk_render_list rl;
      int found_band = 0;
      lk_u32 i;

      memset(&rl, 0, sizeof(rl));
      lk_render_build(t, r, NULL, NULL, &rl);

      for (i = 0; i < rl.count; i++) {
        if (rl.cmds[i].op == LK_ROP_FILL_RECT && rl.cmds[i].rect.x == 197 &&
            rl.cmds[i].rect.y == 0 && rl.cmds[i].rect.w == 5 &&
            rl.cmds[i].rect.h == 300) {
          found_band = 1;
        }
      }

      CHECK(found_band);
      lk_render_list_destroy(&rl);
    }

    free(r);
  }

  END_TEST();
  lk_tree_destroy(t);
}

static void test_split_v_layout_default_ratio(void) {
  /* avail = 300 - 5 = 295; first = 147; second = 148. */
  lk_ix sp, c1, c2;
  lk_tree *t = make_split_tree(UIK_SPLIT_V, &sp, &c1, &c2);
  lk_rect *r;

  BEGIN_TEST("split_v: default ratio stacks minus divider");

  r = run_layout(t, 400, 300);
  CHECK(r != NULL);
  if (r) {
    CHECK_RECT(r[c1], 0, 0, 400, 147);
    CHECK_RECT(r[c2], 0, 152, 400, 148);
    free(r);
  }

  END_TEST();
  lk_tree_destroy(t);
}

static void test_split_ratio_prop_initial(void) {
  /* UIP_SPLIT_RATIO=250: first = 395*250/1000 = 98. */
  lk_ix sp, c1, c2;
  lk_tree *t = make_split_tree(UIK_SPLIT_H, &sp, &c1, &c2);
  lk_rect *r;

  BEGIN_TEST("split: UIP_SPLIT_RATIO sets initial position");

  lk_tree_add_prop(t, sp, UIP_SPLIT_RATIO, lk_v_i32(250));

  r = run_layout(t, 400, 300);
  CHECK(r != NULL);
  if (r) {
    CHECK_RECT(r[c1], 0, 0, 98, 300);
    CHECK_RECT(r[c2], 103, 0, 297, 300);
    free(r);
  }

  END_TEST();
  lk_tree_destroy(t);
}

static void test_split_state_overrides_prop(void) {
  /* Prop says 250 but LKS_SPLIT_RATIO=750 (a drag happened):
   * first = 395*750/1000 = 296. */
  lk_ix sp, c1, c2;
  lk_ui *ui = make_split_ui(UIK_SPLIT_H, 250, &sp, &c1, &c2);
  const lk_tree *cur = lk_ui_tree(ui);
  lk_state *st = lk_ui_state(ui);
  lk_rect *r;

  BEGIN_TEST("split: state ratio overrides prop");

  lk_state_set(st, cur->nodes[sp].id, LKS_SPLIT_RATIO, lk_v_i32(750));

  r = run_layout_with_state((lk_tree *)cur, 400, 300, st);
  CHECK(r != NULL);
  if (r) {
    CHECK_RECT(r[c1], 0, 0, 296, 300);
    CHECK_RECT(r[c2], 301, 0, 99, 300);
    free(r);
  }

  END_TEST();
  lk_ui_destroy(ui);
}

static void test_split_ratio_clamped_min_pane(void) {
  /* Extreme ratios clamp so each pane keeps >= 40 px:
   * ratio 10 -> first 40; ratio 990 -> first 355 (= 395 - 40). */
  lk_ix sp, c1, c2;
  lk_tree *t = make_split_tree(UIK_SPLIT_H, &sp, &c1, &c2);
  lk_rect *r;

  BEGIN_TEST("split: ratio clamped to MIN_PANE");

  lk_tree_add_prop(t, sp, UIP_SPLIT_RATIO, lk_v_i32(10));
  r = run_layout(t, 400, 300);
  CHECK(r != NULL);
  if (r) {
    CHECK_RECT(r[c1], 0, 0, 40, 300);
    CHECK_RECT(r[c2], 45, 0, 355, 300);
    free(r);
  }

  lk_tree_destroy(t);

  t = make_split_tree(UIK_SPLIT_H, &sp, &c1, &c2);
  lk_tree_add_prop(t, sp, UIP_SPLIT_RATIO, lk_v_i32(990));
  r = run_layout(t, 400, 300);
  CHECK(r != NULL);
  if (r) {
    CHECK_RECT(r[c1], 0, 0, 355, 300);
    CHECK_RECT(r[c2], 360, 0, 40, 300);
    free(r);
  }

  END_TEST();
  lk_tree_destroy(t);
}

static void test_split_single_child_fills(void) {
  /* One child: plain container, child fills content rect. */
  lk_tree *t = lk_tree_create(NULL);
  lk_ix w = lk_tree_add_node_s(t, lk_str_c("w"), UIK_WINDOW);
  lk_ix sp = lk_tree_add_node_s(t, lk_str_c("sp"), UIK_SPLIT_H);
  lk_ix c1 = lk_tree_add_node_s(t, lk_str_c("c1"), UIK_COLUMN);
  lk_rect *r;

  BEGIN_TEST("split: single child fills whole rect");

  lk_tree_set_root(t, w);
  lk_tree_append_child(t, w, sp);
  lk_tree_append_child(t, sp, c1);

  r = run_layout(t, 400, 300);
  CHECK(r != NULL);
  if (r) {
    CHECK_RECT(r[c1], 0, 0, 400, 300);
    free(r);
  }

  END_TEST();
  lk_tree_destroy(t);
}

static void test_split_zero_children_bg_only(void) {
  /* Zero children: background fill only, no divider. */
  lk_tree *t = lk_tree_create(NULL);
  lk_ix w = lk_tree_add_node_s(t, lk_str_c("w"), UIK_WINDOW);
  lk_ix sp = lk_tree_add_node_s(t, lk_str_c("sp"), UIK_SPLIT_V);
  lk_rect *r;

  BEGIN_TEST("split: zero children renders bg only");

  lk_tree_set_root(t, w);
  lk_tree_append_child(t, w, sp);

  r = run_layout(t, 400, 300);
  CHECK(r != NULL);
  if (r) {
    lk_render_list rl;
    lk_u32 i;
    lk_u32 fills = 0;

    memset(&rl, 0, sizeof(rl));
    lk_render_build(t, r, NULL, NULL, &rl);

    /* window bg + split bg, nothing else */
    for (i = 0; i < rl.count; i++) {
      if (rl.cmds[i].op == LK_ROP_FILL_RECT) {
        fills++;
      }
    }

    CHECK_EQ(fills, 2u);
    lk_render_list_destroy(&rl);
    free(r);
  }

  END_TEST();
  lk_tree_destroy(t);
}

static void test_split_hidden_child_full_fill(void) {
  /* One of two children hidden: the visible one fills everything. */
  lk_ix sp, c1, c2;
  lk_tree *t = make_split_tree(UIK_SPLIT_H, &sp, &c1, &c2);
  lk_rect *r;

  BEGIN_TEST("split: hidden child leaves full fill");

  lk_tree_add_prop(t, c1, UIP_HIDDEN, lk_v_bool(1));

  r = run_layout(t, 400, 300);
  CHECK(r != NULL);
  if (r) {
    CHECK_RECT(r[c2], 0, 0, 400, 300);
    /* Hidden child never laid out */
    CHECK_RECT(r[c1], 0, 0, 0, 0);
    free(r);
  }

  END_TEST();
  lk_tree_destroy(t);
}

static void test_split_nested_exact_rects(void) {
  /* split_h "outer" > column "left", split_v "inner" > "top","bottom".
   * Outer: left (0,0,197,300), inner (202,0,198,300).
   * Inner: avail = 300-5 = 295; top h=147, bottom y=152 h=148.
   * Divider geometry must come from the inner split's own rect. */
  lk_tree *t = lk_tree_create(NULL);
  lk_ix w = lk_tree_add_node_s(t, lk_str_c("w"), UIK_WINDOW);
  lk_ix outer = lk_tree_add_node_s(t, lk_str_c("outer"), UIK_SPLIT_H);
  lk_ix left = lk_tree_add_node_s(t, lk_str_c("left"), UIK_COLUMN);
  lk_ix inner = lk_tree_add_node_s(t, lk_str_c("inner"), UIK_SPLIT_V);
  lk_ix top = lk_tree_add_node_s(t, lk_str_c("top"), UIK_COLUMN);
  lk_ix bottom = lk_tree_add_node_s(t, lk_str_c("bottom"), UIK_COLUMN);
  lk_rect *r;

  BEGIN_TEST("split: nested split_v in split_h exact rects");

  lk_tree_set_root(t, w);
  lk_tree_append_child(t, w, outer);
  lk_tree_append_child(t, outer, left);
  lk_tree_append_child(t, outer, inner);
  lk_tree_append_child(t, inner, top);
  lk_tree_append_child(t, inner, bottom);

  r = run_layout(t, 400, 300);
  CHECK(r != NULL);
  if (r) {
    CHECK_RECT(r[left], 0, 0, 197, 300);
    CHECK_RECT(r[inner], 202, 0, 198, 300);
    CHECK_RECT(r[top], 202, 0, 198, 147);
    CHECK_RECT(r[bottom], 202, 152, 198, 148);
    free(r);
  }

  END_TEST();
  lk_tree_destroy(t);
}

static void test_split_hit_test_divider_band(void) {
  /* A point in the divider band (197..202) hits the split node itself
   * — the band is owned by the split, not a child. */
  lk_ix sp, c1, c2;
  lk_tree *t = make_split_tree(UIK_SPLIT_H, &sp, &c1, &c2);
  lk_rect *r;

  BEGIN_TEST("split: hit-test in divider band returns split");

  r = run_layout(t, 400, 300);
  CHECK(r != NULL);
  if (r) {
    CHECK_EQ(lk_hit_test(t, r, 199, 150), sp);
    CHECK_EQ(lk_hit_test(t, r, 100, 150), c1);
    CHECK_EQ(lk_hit_test(t, r, 300, 150), c2);
    free(r);
  }

  END_TEST();
  lk_tree_destroy(t);
}

static void test_split_drag_sequence(void) {
  /* Full drag through routed events.  Content rect (0,0,400,300)
   * stashed by layout; avail = 395; MOVE maps pointer x to per-mille
   * via rel = (x - 2), ratio = rel*1000/395 (divider centered under
   * the cursor), clamped to [ceil(40000/395), 355000/395] = [102,898]. */
  lk_ix sp, c1, c2;
  lk_ui *ui = make_split_ui(UIK_SPLIT_H, -1, &sp, &c1, &c2);
  const lk_tree *cur = lk_ui_tree(ui);
  lk_state *st = lk_ui_state(ui);
  lk_node_id sp_id = cur->nodes[sp].id;
  lk_rect *r;
  lk_event ev;

  BEGIN_TEST("split: drag sequence updates ratio via capture");

  r = run_layout_with_state((lk_tree *)cur, 400, 300, st);
  CHECK(r != NULL);

  if (r) {
    /* Pane click bubbling through the split is NOT consumed */
    memset(&ev, 0, sizeof(ev));
    ev.type = LK_EVENT_POINTER_DOWN;
    ev.target = lk_hit_test(cur, r, 50, 150);
    ev.data.pointer.x = 50;
    ev.data.pointer.y = 150;
    CHECK_EQ(ev.target, c1);
    lk_event_route(ui, &ev);
    CHECK_EQ((unsigned)ev.handled, 0u);
    CHECK_EQ((unsigned)lk_capture_current(ui), 0u);

    /* DOWN in the band starts the drag and takes the capture */
    memset(&ev, 0, sizeof(ev));
    ev.type = LK_EVENT_POINTER_DOWN;
    ev.target = lk_hit_test(cur, r, 199, 150);
    ev.data.pointer.x = 199;
    ev.data.pointer.y = 150;
    CHECK_EQ(ev.target, sp);
    lk_event_route(ui, &ev);
    CHECK_EQ((unsigned)ev.handled, 1u);
    CHECK_EQ((unsigned)lk_state_get(st, sp_id, LKS_SPLIT_DRAGGING).as.i, 1u);
    CHECK_EQ(lk_capture_current(ui), sp_id);

    /* MOVE to x=100: ratio = (100-2)*1000/395 = 248 */
    memset(&ev, 0, sizeof(ev));
    ev.type = LK_EVENT_POINTER_MOVE;
    ev.target = sp; /* capture targets the split (see lk-sdl.c) */
    ev.data.pointer.x = 100;
    ev.data.pointer.y = 150;
    lk_event_route(ui, &ev);
    CHECK_EQ((unsigned)ev.handled, 1u);
    CHECK_EQ((int)lk_state_get(st, sp_id, LKS_SPLIT_RATIO).as.i, 248);

    /* MOVE far outside the band (and the content rect) still updates
     * — that is what the capture is for: (350-2)*1000/395 = 881 */
    memset(&ev, 0, sizeof(ev));
    ev.type = LK_EVENT_POINTER_MOVE;
    ev.target = sp;
    ev.data.pointer.x = 350;
    ev.data.pointer.y = 400;
    lk_event_route(ui, &ev);
    CHECK_EQ((int)lk_state_get(st, sp_id, LKS_SPLIT_RATIO).as.i, 881);

    /* MOVE to the far left clamps at MIN_PANE: ratio 102 -> 40 px */
    memset(&ev, 0, sizeof(ev));
    ev.type = LK_EVENT_POINTER_MOVE;
    ev.target = sp;
    ev.data.pointer.x = 5;
    ev.data.pointer.y = 150;
    lk_event_route(ui, &ev);
    CHECK_EQ((int)lk_state_get(st, sp_id, LKS_SPLIT_RATIO).as.i, 102);

    free(r);
    r = run_layout_with_state((lk_tree *)cur, 400, 300, st);
    CHECK(r != NULL);
    if (r) {
      CHECK_EQ((unsigned)r[c1].w, 40u);
      free(r);
    }

    /* UP ends the drag and releases the capture */
    memset(&ev, 0, sizeof(ev));
    ev.type = LK_EVENT_POINTER_UP;
    ev.target = sp;
    ev.data.pointer.x = 5;
    ev.data.pointer.y = 150;
    lk_event_route(ui, &ev);
    CHECK_EQ((unsigned)ev.handled, 1u);
    CHECK_EQ((unsigned)lk_state_get(st, sp_id, LKS_SPLIT_DRAGGING).as.i, 0u);
    CHECK_EQ((unsigned)lk_capture_current(ui), 0u);
  }

  END_TEST();
  lk_ui_destroy(ui);
}

static void test_capture_api(void) {
  lk_ui *ui = lk_ui_create(NULL);
  lk_node_id id;

  BEGIN_TEST("capture: set/current/clear");

  prime_base(ui);
  id = lk_intern_id(ui->intern, lk_str_c("inc"));

  CHECK_EQ((unsigned)lk_capture_current(ui), 0u);
  lk_capture_set(ui, id);
  CHECK_EQ(lk_capture_current(ui), id);
  lk_capture_clear(ui);
  CHECK_EQ((unsigned)lk_capture_current(ui), 0u);

  /* NULL-safe */
  lk_capture_set(NULL, id);
  lk_capture_clear(NULL);
  CHECK_EQ((unsigned)lk_capture_current(NULL), 0u);

  END_TEST();
  lk_ui_destroy(ui);
}

static void test_capture_end_frame_gc(void) {
  /* end_frame clears the capture when the node is removed, but a
   * REMOVED+ADDED move keeps it (same filter as focus). */
  lk_ui *ui = lk_ui_create(NULL);
  lk_node_id id;
  lk_tree *t;
  lk_ix w, col, btn;

  BEGIN_TEST("capture: cleared on removal, survives move");

  prime_base(ui);
  id = lk_intern_id(ui->intern, lk_str_c("inc"));
  lk_capture_set(ui, id);

  /* Move "inc" from column "root" to window "main" — capture stays */
  t = lk_ui_begin_frame(ui);
  w = lk_tree_add_node_s(t, lk_str_c("main"), UIK_WINDOW);
  col = lk_tree_add_node_s(t, lk_str_c("root"), UIK_COLUMN);
  btn = lk_tree_add_node_s(t, lk_str_c("inc"), UIK_BUTTON);
  lk_tree_set_root(t, w);
  lk_tree_append_child(t, w, col);
  lk_tree_append_child(t, w, btn);
  lk_ui_end_frame(ui);
  CHECK_EQ(lk_capture_current(ui), id);

  /* Drop "inc" entirely — capture cleared */
  t = lk_ui_begin_frame(ui);
  w = lk_tree_add_node_s(t, lk_str_c("main"), UIK_WINDOW);
  col = lk_tree_add_node_s(t, lk_str_c("root"), UIK_COLUMN);
  lk_tree_set_root(t, w);
  lk_tree_append_child(t, w, col);
  lk_ui_end_frame(ui);
  CHECK_EQ((unsigned)lk_capture_current(ui), 0u);

  END_TEST();
  lk_ui_destroy(ui);
}

static void test_dump_kind_names(void) {
  /* lk_tree_dump prints real names for all registered kinds. */
  lk_tree *t = lk_tree_create(NULL);
  lk_ix w = lk_tree_add_node_s(t, lk_str_c("w"), UIK_WINDOW);
  lk_ix sh = lk_tree_add_node_s(t, lk_str_c("sh"), UIK_SPLIT_H);
  lk_ix sv = lk_tree_add_node_s(t, lk_str_c("sv"), UIK_SPLIT_V);
  lk_ix ti = lk_tree_add_node_s(t, lk_str_c("ti"), UIK_TEXT_INPUT);
  lk_ix sc = lk_tree_add_node_s(t, lk_str_c("sc"), UIK_SCROLL);
  lk_ix dd = lk_tree_add_node_s(t, lk_str_c("dd"), UIK_DROPDOWN);
  lk_ix op = lk_tree_add_node_s(t, lk_str_c("op"), UIK_OPTION);
  lk_ix ed = lk_tree_add_node_s(t, lk_str_c("ed"), UIK_EDITOR);
  test_buf buf;

  BEGIN_TEST("dump: names for all widget kinds");

  lk_tree_set_root(t, w);
  lk_tree_append_child(t, w, sh);
  lk_tree_append_child(t, sh, sv);
  lk_tree_append_child(t, sv, ti);
  lk_tree_append_child(t, sv, sc);
  lk_tree_append_child(t, sh, dd);
  lk_tree_append_child(t, dd, op);
  lk_tree_append_child(t, sv, ed);

  memset(&buf, 0, sizeof(buf));
  lk_tree_dump(t, test_buf_write, &buf);

  CHECK(strstr(buf.buf, "split_h") != NULL);
  CHECK(strstr(buf.buf, "split_v") != NULL);
  CHECK(strstr(buf.buf, "text_input") != NULL);
  CHECK(strstr(buf.buf, "scroll") != NULL);
  CHECK(strstr(buf.buf, "dropdown") != NULL);
  CHECK(strstr(buf.buf, "option") != NULL);
  CHECK(strstr(buf.buf, "(editor") != NULL);
  CHECK(strstr(buf.buf, "unknown") == NULL);

  END_TEST();
  lk_tree_destroy(t);
}

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
  test_pres_multi_arg();
  test_pres_multi_arg_cmd();
  test_pres_multi_arg_change_detected();

  /* command / translator */
  printf("\nlk command/translator tests:\n");
  test_translator_fires_command();
  test_translator_no_match();
  test_translator_walks_ancestors();
  test_translator_ptype_and_kind();
  test_translator_keycode_match();
  test_translator_keycode_wrong_key();
  test_translator_keycode_wrong_mods();
  test_translator_keycode_no_pres_required();
  test_translator_keycode_on_pointer_event();
  test_translator_keycode_zero_mods();
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

  /* state store */
  printf("\nlk state store tests:\n");
  test_state_set_get();
  test_state_overwrite();
  test_state_missing_key();
  test_state_gc_on_removal();
  test_state_multiple_keys();
  test_state_gc_preserves_other();
  test_state_remove_node_manual();

  /* style + layout */
  printf("\nlk style+layout tests:\n");
  test_layout_with_resolved_styles();
  test_layout_style_tree_prop_override();

  /* tags */
  printf("\nlk tag tests:\n");
  test_tag_add_query();
  test_tag_diffing();
  test_style_resolve_with_tag();
  test_tag_multiple_on_node();

  /* style system */
  printf("\nlk style tests:\n");
  test_theme_create_destroy();
  test_style_resolve_basic();
  test_style_resolve_inheritance();
  test_style_resolve_tree_prop_override();
  test_style_resolve_rule_order();
  test_style_resolve_state_match();
  test_style_trace();
  test_style_bg_does_not_inherit();
  test_style_font_inherits();
  test_style_kind_no_cross_match();
  test_style_state_requires_all_bits();
  test_style_tag_no_match_untagged();
  test_style_gap_prop_override();
  test_style_align_justify_prop_override();
  test_style_wildcard_matches_all();

  /* style + render/layout integration */
  printf("\nlk style integration tests:\n");
  test_render_uses_style_colors();
  test_layout_style_gap();

  /* ui style integration */
  printf("\nlk ui style tests:\n");
  test_ui_owns_default_theme();
  test_ui_resolve_styles_headless();
  test_ui_set_theme_custom();
  test_ui_hover_state();

  /* border rendering */
  printf("\nlk border tests:\n");
  test_border_render_four_fill_rects();
  test_border_inset_layout();
  test_border_zero_no_extra_commands();
  test_border_theme_integration();

  /* deferred prop append */
  printf("\nlk deferred prop tests:\n");
  test_prop_deferred_all_nodes_first();
  test_prop_interleave_drops();
  test_render_deferred_props_emit_text();

  /* text input widget */
  printf("\nlk text input tests:\n");
  test_text_input_measure();
  test_text_input_render();
  test_text_input_insert();
  test_text_input_emits_value_changed();
  test_text_input_backspace_emits_value_changed();
  test_text_input_backspace();
  test_text_input_delete();
  test_text_input_cursor_movement();
  test_text_input_selection();
  test_text_input_select_all();
  test_text_input_initial_text();
  test_text_input_cursor_clamp();

  /* scroll widget */
  printf("\nlk scroll tests:\n");
  test_scroll_measure();
  test_scroll_layout_no_offset();
  test_scroll_layout_with_offset();
  test_scroll_render_clips();
  test_scroll_wheel_event();
  test_scroll_clamp_bounds();
  test_scroll_wheel_bubbles();
  test_scroll_empty();
  test_scroll_bar_rendered();
  test_widget_bubble_no_break_text_input();

  /* clipboard */
  printf("\nlk clipboard tests:\n");
  test_text_input_ctrl_c();
  test_text_input_ctrl_v();
  test_text_input_ctrl_x();
  test_text_input_ctrl_v_no_clipboard();

  /* dropdown */
  printf("\nlk dropdown tests:\n");
  test_dropdown_pointer_down_toggles();
  test_dropdown_arrow_keys_navigate();
  test_dropdown_return_commits();
  test_dropdown_escape_closes();
  test_dropdown_overlay_hit_test();
  test_dropdown_click_outside_closes();
  test_dropdown_overlay_render_when_expanded();

  /* overlay generalization */
  printf("\nlk overlay tests:\n");
  test_anchor_below_fits();
  test_anchor_below_flips_above();
  test_anchor_x_clamp();
  test_anchor_center_viewport();
  test_anchor_at_cursor();
  test_overlay_push_pop_count();
  test_overlay_end_frame_gc();
  test_overlay_escape_pops();
  test_dropdown_bottom_edge_flips();
  test_overlay_modal_blocks();
  test_hidden_subtree_layout();
  test_hidden_subtree_render_hit();
  test_hidden_focus_next_skips();
  test_layout_subtree_positions();
  test_focus_trap_scopes_tab();

  /* tooltips (overlay step 6) */
  printf("\nlk tooltip tests:\n");
  test_tooltip_hover_push_pop();
  test_tooltip_render_on_top();
  test_tooltip_passive();
  test_tooltip_bottom_edge_flips();

  /* text backend contract (stage A) */
  printf("\nlk text backend tests:\n");
  test_text_stub_measure_x_agree();
  test_text_stub_x_from_index_multibyte();
  test_text_stub_index_from_x_rounding();
  test_text_stub_register_font_ids();
  test_render_text_carries_font();

  /* text input correctness (stage C) */
  printf("\nlk text-contract stage C tests:\n");
  test_text_input_multibyte_arrows();
  test_text_input_multibyte_backspace_delete();
  test_text_input_insert_cap_boundary();
  test_text_input_paste_cap_boundary();
  test_text_input_cursor_x_from_index();
  test_text_input_selection_rect_exact();
  test_text_input_click_to_position();

  /* bug-fix regressions */
  printf("\nlk bug-fix regression tests:\n");
  test_value_none_zeroed();
  test_intern_custom_alloc_balanced();
  test_translator_disabled_no_command();
  test_state_survives_reparent();
  test_text_input_cursor_only_when_focused();
  test_dropdown_padding_click_stays_open();
  test_dropdown_hover_follows_pointer();

  /* split panes + pointer capture */
  printf("\nlk split tests:\n");
  test_split_h_layout_default_ratio();
  test_split_v_layout_default_ratio();
  test_split_ratio_prop_initial();
  test_split_state_overrides_prop();
  test_split_ratio_clamped_min_pane();
  test_split_single_child_fills();
  test_split_zero_children_bg_only();
  test_split_hidden_child_full_fill();
  test_split_nested_exact_rects();
  test_split_hit_test_divider_band();
  test_split_drag_sequence();
  test_capture_api();
  test_capture_end_frame_gc();
  test_dump_kind_names();

  /* document + edit history (editor track, stage A) */
  lk_document_run_tests();

  /* resource refs + run arena (editor track, stage B1) */
  lk_resource_run_tests();

  /* editor view + command layer + widget (editor track, stage B2) */
  lk_editor_run_tests();

  /* annotation store + styled spans (editor track, stage C) */
  lk_annot_run_tests();

  printf("\n%d/%d tests passed", g_pass, g_tests);

  if (g_fail > 0) {
    printf(", %d FAILED", g_fail);
  }

  printf("\n");

  return g_fail > 0 ? 1 : 0;
}
