/*
 * lk-resource-test.c -- editor track stage B1 (core substrate):
 * typed resource references (docs/editor.md section 5) and the
 * self-contained render-list run arena (section 8).
 *
 * Three blocks: the lk_resources table (register/get/release,
 * generation checks, slot reuse), UIV_RESOURCE integration with the
 * tree/props/diff/dump, and the render list byte arena
 * (lk_render_list_push_run + LK_ROP_DRAW_RUN).
 */

#include <stdio.h>
#include <string.h>

#include <lk.h>

#include "lk-test-harness.h"

/* Two distinct type descriptors — type checking is descriptor-pointer
 * equality, so these must be separate objects. */
static const lk_resource_type g_editor_type = {"editor", NULL};
static const lk_resource_type g_document_type = {"document", NULL};

/* ---- resource table ---- */

static void test_res_register_and_get(void) {
  lk_resources *rs = lk_resources_new(NULL, NULL, NULL);
  int obj = 42;
  lk_resource_ref ref;

  BEGIN_TEST("resource: register returns working ref");

  CHECK(rs != NULL);

  ref = lk_resource_register(rs, &g_editor_type, &obj, "src-view");

  CHECK(ref.id != 0);
  CHECK(lk_resource_get(rs, ref, &g_editor_type) == (void *)&obj);

  lk_resources_destroy(rs);
  END_TEST();
}

static void test_res_wrong_type_null(void) {
  lk_resources *rs = lk_resources_new(NULL, NULL, NULL);
  int obj = 1;
  lk_resource_ref ref;

  BEGIN_TEST("resource: wrong type descriptor -> NULL");

  ref = lk_resource_register(rs, &g_editor_type, &obj, "e");

  CHECK(lk_resource_get(rs, ref, &g_document_type) == NULL);
  CHECK(lk_resource_get(rs, ref, &g_editor_type) == (void *)&obj);

  lk_resources_destroy(rs);
  END_TEST();
}

static void test_res_release_stale(void) {
  lk_resources *rs = lk_resources_new(NULL, NULL, NULL);
  int obj = 1;
  lk_resource_ref ref;

  BEGIN_TEST("resource: release -> stale ref gets NULL");

  ref = lk_resource_register(rs, &g_editor_type, &obj, "e");

  CHECK(lk_resource_get(rs, ref, &g_editor_type) != NULL);

  lk_resource_release(rs, ref);

  CHECK(lk_resource_get(rs, ref, &g_editor_type) == NULL);

  lk_resources_destroy(rs);
  END_TEST();
}

static void test_res_slot_reuse_generation(void) {
  lk_resources *rs = lk_resources_new(NULL, NULL, NULL);
  int a = 1;
  int b = 2;
  lk_resource_ref old_ref;
  lk_resource_ref new_ref;

  BEGIN_TEST("resource: slot reuse keeps old ref dead");

  old_ref = lk_resource_register(rs, &g_editor_type, &a, "a");
  lk_resource_release(rs, old_ref);

  new_ref = lk_resource_register(rs, &g_editor_type, &b, "b");

  /* The released slot is reused: same logical slot, bumped
   * generation. */
  CHECK_EQ(new_ref.id, old_ref.id);
  CHECK(new_ref.generation != old_ref.generation);

  CHECK(lk_resource_get(rs, new_ref, &g_editor_type) == (void *)&b);
  CHECK(lk_resource_get(rs, old_ref, &g_editor_type) == NULL);

  lk_resources_destroy(rs);
  END_TEST();
}

static void test_res_null_ref_and_table(void) {
  lk_resources *rs = lk_resources_new(NULL, NULL, NULL);
  int obj = 1;
  lk_resource_ref null_ref;
  lk_resource_ref ref;
  lk_resource_ref bogus;

  BEGIN_TEST("resource: null ref / NULL table -> NULL");

  null_ref.id = 0;
  null_ref.generation = 0;

  ref = lk_resource_register(rs, &g_editor_type, &obj, "e");

  CHECK(lk_resource_get(rs, null_ref, &g_editor_type) == NULL);
  CHECK(lk_resource_get(NULL, ref, &g_editor_type) == NULL);

  /* Out-of-range id */
  bogus.id = 999;
  bogus.generation = 1;

  CHECK(lk_resource_get(rs, bogus, &g_editor_type) == NULL);

  lk_resources_destroy(rs);
  END_TEST();
}

static void test_res_distinct_ids(void) {
  lk_resources *rs = lk_resources_new(NULL, NULL, NULL);
  int a = 1;
  int b = 2;
  lk_resource_ref ra;
  lk_resource_ref rb;

  BEGIN_TEST("resource: two registrations -> distinct ids");

  ra = lk_resource_register(rs, &g_editor_type, &a, "a");
  rb = lk_resource_register(rs, &g_document_type, &b, "b");

  CHECK(ra.id != 0);
  CHECK(rb.id != 0);
  CHECK(ra.id != rb.id);

  CHECK(lk_resource_get(rs, ra, &g_editor_type) == (void *)&a);
  CHECK(lk_resource_get(rs, rb, &g_document_type) == (void *)&b);

  lk_resources_destroy(rs);
  END_TEST();
}

static void test_res_release_noop_stale_null(void) {
  lk_resources *rs = lk_resources_new(NULL, NULL, NULL);
  int a = 1;
  int b = 2;
  lk_resource_ref ref;
  lk_resource_ref new_ref;
  lk_resource_ref null_ref;

  BEGIN_TEST("resource: releasing stale/null ref is a no-op");

  null_ref.id = 0;
  null_ref.generation = 0;

  lk_resource_release(rs, null_ref); /* no-op, no crash */
  lk_resource_release(NULL, null_ref);

  ref = lk_resource_register(rs, &g_editor_type, &a, "a");
  lk_resource_release(rs, ref);

  /* Slot is reused; the stale first ref must not release the new
   * occupant. */
  new_ref = lk_resource_register(rs, &g_editor_type, &b, "b");
  lk_resource_release(rs, ref); /* stale: no-op */

  CHECK(lk_resource_get(rs, new_ref, &g_editor_type) == (void *)&b);

  lk_resources_destroy(rs);
  END_TEST();
}

/* ---- value + tree integration ---- */

static void test_res_value_roundtrip(void) {
  lk_tree *t = lk_tree_create(NULL);
  lk_resources *rs = lk_resources_new(NULL, NULL, NULL);
  int obj = 7;
  lk_resource_ref ref;
  lk_resource_ref back;
  lk_ix n;
  const lk_node *nd;
  lk_value v;

  BEGIN_TEST("resource: UIV_RESOURCE prop roundtrip");

  lk_tree_set_resources(t, rs);

  CHECK(t->resources == rs);

  ref = lk_resource_register(rs, &g_editor_type, &obj, "ed");

  n = lk_tree_add_node_c(t, "root", UIK_WINDOW);
  lk_tree_set_root(t, n);
  lk_tree_add_prop(t, n, UIP_TEXT, lk_v_resource(ref));

  nd = &t->nodes[n];

  CHECK_EQ(nd->props_len, 1);

  v = t->props[nd->props_off].value;

  CHECK_EQ(v.tag, UIV_RESOURCE);

  back = lk_v_resource_ref(v);

  CHECK_EQ(back.id, ref.id);
  CHECK_EQ(back.generation, ref.generation);
  CHECK(lk_resource_get(rs, back, &g_editor_type) == (void *)&obj);

  /* Extracting from a non-resource value yields the null ref. */
  back = lk_v_resource_ref(lk_v_i32(5));

  CHECK_EQ(back.id, 0);
  CHECK_EQ(back.generation, 0);

  lk_tree_destroy(t);
  lk_resources_destroy(rs);
  END_TEST();
}

/* Build a one-node frame whose root carries a resource prop. */
static void res_build_frame(lk_ui *ui, lk_resource_ref ref) {
  lk_tree *t = lk_ui_begin_frame(ui);
  lk_ix n = lk_tree_add_node_c(t, "root", UIK_WINDOW);

  lk_tree_set_root(t, n);
  lk_tree_add_prop(t, n, UIP_TEXT, lk_v_resource(ref));
}

/* Count UPDATED entries in the changeset. */
static lk_u32 cs_updated_count(const lk_changeset *cs) {
  lk_u32 i;
  lk_u32 n = 0;

  for (i = 0; i < cs->count; i++) {
    if (cs->changes[i].kind == LK_CHANGE_UPDATED) {
      n++;
    }
  }

  return n;
}

static void test_res_diff_same_ref_no_update(void) {
  lk_ui *ui = lk_ui_create(NULL);
  lk_resources *rs = lk_ui_resources(ui);
  int obj = 1;
  lk_resource_ref ref;
  const lk_changeset *cs;

  BEGIN_TEST("resource: same ref across frames -> no UPDATED");

  CHECK(rs != NULL);

  ref = lk_resource_register(rs, &g_editor_type, &obj, "ed");

  res_build_frame(ui, ref);
  lk_ui_end_frame(ui);

  res_build_frame(ui, ref);
  cs = lk_ui_end_frame(ui);

  CHECK_EQ(cs->count, 0);

  /* The current tree resolves the ref through t alone. */
  CHECK(lk_ui_tree(ui)->resources == rs);

  lk_ui_destroy(ui);
  END_TEST();
}

static void test_res_diff_changed_ref_updates(void) {
  lk_ui *ui = lk_ui_create(NULL);
  lk_resources *rs = lk_ui_resources(ui);
  int a = 1;
  int b = 2;
  lk_resource_ref ra;
  lk_resource_ref rb;
  const lk_changeset *cs;

  BEGIN_TEST("resource: different id across frames -> UPDATED");

  ra = lk_resource_register(rs, &g_editor_type, &a, "a");
  rb = lk_resource_register(rs, &g_editor_type, &b, "b");

  res_build_frame(ui, ra);
  lk_ui_end_frame(ui);

  res_build_frame(ui, rb);
  cs = lk_ui_end_frame(ui);

  CHECK_EQ(cs_updated_count(cs), 1);

  lk_ui_destroy(ui);
  END_TEST();
}

static void test_res_diff_regenerated_ref_updates(void) {
  lk_ui *ui = lk_ui_create(NULL);
  lk_resources *rs = lk_ui_resources(ui);
  int a = 1;
  int b = 2;
  lk_resource_ref ra;
  lk_resource_ref rb;
  const lk_changeset *cs;

  BEGIN_TEST("resource: same id, new generation -> UPDATED");

  ra = lk_resource_register(rs, &g_editor_type, &a, "a");

  res_build_frame(ui, ra);
  lk_ui_end_frame(ui);

  /* Release + re-register reuses the slot: same id, bumped
   * generation.  The diff must see that as a change. */
  lk_resource_release(rs, ra);
  rb = lk_resource_register(rs, &g_editor_type, &b, "b");

  CHECK_EQ(rb.id, ra.id);
  CHECK(rb.generation != ra.generation);

  res_build_frame(ui, rb);
  cs = lk_ui_end_frame(ui);

  CHECK_EQ(cs_updated_count(cs), 1);

  lk_ui_destroy(ui);
  END_TEST();
}

/* ---- dump ---- */

typedef struct dump_buf {
  char buf[4096];
  lk_u32 len;
} dump_buf;

static void dump_write(void *ud, const char *bytes, lk_u32 len) {
  dump_buf *db = (dump_buf *)ud;
  lk_u32 i;

  for (i = 0; i < len && db->len + 1 < sizeof(db->buf); i++) {
    db->buf[db->len++] = bytes[i];
  }

  db->buf[db->len] = '\0';
}

static void test_res_dump_live_and_stale(void) {
  lk_tree *t = lk_tree_create(NULL);
  lk_resources *rs = lk_resources_new(NULL, NULL, NULL);
  int obj = 1;
  lk_resource_ref ref;
  lk_ix n;
  dump_buf db;

  BEGIN_TEST("resource: dump prints typename=\"name\"#id");

  lk_tree_set_resources(t, rs);

  ref = lk_resource_register(rs, &g_editor_type, &obj, "src-view");

  n = lk_tree_add_node_c(t, "root", UIK_WINDOW);
  lk_tree_set_root(t, n);
  lk_tree_add_prop(t, n, UIP_TEXT, lk_v_resource(ref));

  db.len = 0;
  db.buf[0] = '\0';
  lk_tree_dump(t, dump_write, &db);

  CHECK(strstr(db.buf, "editor=\"src-view\"#1") != NULL);

  /* Stale ref falls back to resource#id. */
  lk_resource_release(rs, ref);

  db.len = 0;
  db.buf[0] = '\0';
  lk_tree_dump(t, dump_write, &db);

  CHECK(strstr(db.buf, "resource#1") != NULL);
  CHECK(strstr(db.buf, "editor=\"src-view\"") == NULL);

  /* No table at all: same fallback. */
  lk_tree_set_resources(t, NULL);

  db.len = 0;
  db.buf[0] = '\0';
  lk_tree_dump(t, dump_write, &db);

  CHECK(strstr(db.buf, "resource#1") != NULL);

  lk_tree_destroy(t);
  lk_resources_destroy(rs);
  END_TEST();
}

/* ---- render-list run arena ---- */

static void test_arena_push_copies(void) {
  lk_render_list rl;
  char src[8];
  lk_u32 off = 99;

  BEGIN_TEST("arena: push_run copies bytes");

  memset(&rl, 0, sizeof(rl));
  strcpy(src, "hello");

  CHECK(lk_render_list_push_run(&rl, src, 5, &off) == 1);
  CHECK_EQ(off, 0);
  CHECK_EQ(rl.bytes_count, 5);

  /* Mutate the source: the arena must hold its own copy. */
  strcpy(src, "XXXXX");

  CHECK(memcmp(rl.bytes, "hello", 5) == 0);

  lk_render_list_destroy(&rl);
  END_TEST();
}

static void test_arena_sequential_offsets_and_len0(void) {
  lk_render_list rl;
  lk_u32 off_a = 99;
  lk_u32 off_b = 99;
  lk_u32 off_c = 99;
  lk_u32 off_d = 99;

  BEGIN_TEST("arena: sequential offsets, len 0 legal");

  memset(&rl, 0, sizeof(rl));

  CHECK(lk_render_list_push_run(&rl, "abc", 3, &off_a) == 1);
  CHECK(lk_render_list_push_run(&rl, "defg", 4, &off_b) == 1);

  /* len 0: legal, writes the current offset, copies nothing. */
  CHECK(lk_render_list_push_run(&rl, NULL, 0, &off_c) == 1);
  CHECK(lk_render_list_push_run(&rl, "h", 1, &off_d) == 1);

  CHECK_EQ(off_a, 0);
  CHECK_EQ(off_b, 3);
  CHECK_EQ(off_c, 7);
  CHECK_EQ(off_d, 7);
  CHECK_EQ(rl.bytes_count, 8);
  CHECK(memcmp(rl.bytes, "abcdefgh", 8) == 0);

  lk_render_list_destroy(&rl);
  END_TEST();
}

static void test_arena_growth_preserves(void) {
  lk_render_list rl;
  char chunk[64];
  lk_u32 off = 0;
  lk_u32 first_off = 99;
  int i;

  BEGIN_TEST("arena: growth preserves earlier bytes");

  memset(&rl, 0, sizeof(rl));
  memset(chunk, 'x', sizeof(chunk));

  chunk[0] = 'A';

  CHECK(lk_render_list_push_run(&rl, chunk, 64, &first_off) == 1);

  /* Push well past the initial capacity (256). */
  chunk[0] = 'x';

  for (i = 0; i < 20; i++) {
    CHECK(lk_render_list_push_run(&rl, chunk, 64, &off) == 1);
  }

  CHECK_EQ(rl.bytes_count, 64 * 21);
  CHECK(rl.bytes_cap >= rl.bytes_count);
  CHECK(rl.bytes[first_off] == 'A');
  CHECK(rl.bytes[63] == 'x');
  CHECK(rl.bytes[64 * 21 - 1] == 'x');

  lk_render_list_destroy(&rl);
  END_TEST();
}

static void test_arena_build_resets(void) {
  lk_tree *t = lk_tree_create(NULL);
  lk_render_list rl;
  lk_rect rects[4];
  lk_ix n;
  lk_u32 off = 99;
  lk_u32 cap_before;

  BEGIN_TEST("arena: lk_render_build resets bytes_count");

  memset(&rl, 0, sizeof(rl));
  memset(rects, 0, sizeof(rects));

  n = lk_tree_add_node_c(t, "root", UIK_WINDOW);
  lk_tree_set_root(t, n);

  CHECK(lk_render_list_push_run(&rl, "leftover", 8, &off) == 1);
  CHECK_EQ(rl.bytes_count, 8);

  cap_before = rl.bytes_cap;

  CHECK(lk_render_build(t, rects, NULL, NULL, &rl) == 1);

  /* Count reset, capacity reused. */
  CHECK_EQ(rl.bytes_count, 0);
  CHECK_EQ(rl.bytes_cap, cap_before);

  lk_render_list_destroy(&rl);
  lk_tree_destroy(t);
  END_TEST();
}

static void test_arena_draw_run_cmd(void) {
  lk_render_list rl;
  lk_render_cmd cmd;
  lk_u32 off = 99;
  const lk_render_cmd *got;

  BEGIN_TEST("arena: DRAW_RUN cmd reads back through run_off/len");

  memset(&rl, 0, sizeof(rl));

  CHECK(lk_render_list_push_run(&rl, "pad", 3, &off) == 1);
  CHECK(lk_render_list_push_run(&rl, "line one", 8, &off) == 1);

  memset(&cmd, 0, sizeof(cmd));
  cmd.op = LK_ROP_DRAW_RUN;
  cmd.run_off = off;
  cmd.run_len = 8;

  CHECK(lk_render_list_push(&rl, cmd) == 1);

  got = &rl.cmds[rl.count - 1];

  CHECK_EQ(got->op, LK_ROP_DRAW_RUN);
  CHECK_EQ(got->run_len, 8);
  CHECK(memcmp(rl.bytes + got->run_off, "line one", got->run_len) == 0);

  lk_render_list_destroy(&rl);
  END_TEST();
}

/* ---- runner ---- */

void lk_resource_run_tests(void) {
  printf("\nlk resource table tests:\n");
  test_res_register_and_get();
  test_res_wrong_type_null();
  test_res_release_stale();
  test_res_slot_reuse_generation();
  test_res_null_ref_and_table();
  test_res_distinct_ids();
  test_res_release_noop_stale_null();

  printf("\nlk resource value/tree integration tests:\n");
  test_res_value_roundtrip();
  test_res_diff_same_ref_no_update();
  test_res_diff_changed_ref_updates();
  test_res_diff_regenerated_ref_updates();
  test_res_dump_live_and_stale();

  printf("\nlk render-list run arena tests:\n");
  test_arena_push_copies();
  test_arena_sequential_offsets_and_len0();
  test_arena_growth_preserves();
  test_arena_build_resets();
  test_arena_draw_run_cmd();
}
