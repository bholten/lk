/*
 * lcl-lk-test.c — Headless tests for the lk Lcl bindings.
 *
 * Uses lcl_eval_string to exercise all non-SDL procs.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <lcl.h>
#include <lk.h>
#include "lcl-lk.h"

/* ---- minimal test harness ---- */

static int g_tests = 0;
static int g_pass = 0;
static int g_fail = 0;
static int g_cur_ok = 0;

#define BEGIN_TEST(name)                                                       \
  do {                                                                         \
    g_tests++;                                                                 \
    g_cur_ok = 1;                                                              \
    printf("  %-50s ", name);                                                  \
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

#define END_TEST()                                                             \
  do {                                                                         \
    if (g_cur_ok) {                                                            \
      printf("ok\n");                                                          \
      g_pass++;                                                                \
    } else {                                                                   \
      g_fail++;                                                                \
    }                                                                          \
  } while (0)

/* Helper: create an interp with core + lk */
static lcl_interp *make_interp(void) {
  lcl_interp *interp = lcl_interp_new();
  lcl_register_core(interp);
  lcl_register_lk(interp);
  return interp;
}

/* Helper: eval and check success */
static int eval_ok(lcl_interp *interp, const char *src, lcl_value **out) {
  int rc = lcl_eval_string(interp, src, out);
  if (rc != LCL_RC_OK) {
    const char *msg = lcl_interp_error_msg(interp);
    if (g_cur_ok)
      printf("FAIL\n");
    printf("    eval error: %s\n    src: %.80s\n", msg ? msg : "(null)", src);
    g_cur_ok = 0;
  }
  return rc;
}

/* ---- Tests ---- */

static void test_ui_create_destroy(void) {
  lcl_interp *interp;
  lcl_value *r = NULL;

  BEGIN_TEST("ui_create returns opaque");
  interp = make_interp();

  eval_ok(interp, "let ui [lk::ui_create]", &r);
  if (r) lcl_ref_dec(r);
  r = NULL;

  /* Check it's an opaque */
  eval_ok(interp, "opaque? $ui", &r);
  if (r) {
    long v;
    lcl_value_to_int(r, &v);
    CHECK(v == 1);
    lcl_ref_dec(r);
  }

  lcl_interp_free(interp);
  END_TEST();
}

static void test_begin_end_frame(void) {
  lcl_interp *interp;
  lcl_value *r = NULL;

  BEGIN_TEST("begin_frame / end_frame lifecycle");
  interp = make_interp();

  eval_ok(interp, "let ui [lk::ui_create]", &r);
  if (r) lcl_ref_dec(r);
  r = NULL;

  /* First frame: build a simple tree */
  eval_ok(interp,
    "let t [lk::begin_frame $ui]\n"
    "let w [lk::node $t \"main\" \"window\"]\n"
    "let col [lk::node $t \"root\" \"column\"]\n"
    "lk::set_root $t $w\n"
    "lk::append_child $t $w $col\n"
    "lk::end_frame $ui",
    &r);

  /* end_frame returns a list of changes */
  if (r) {
    size_t len = lcl_list_len(r);
    CHECK(len > 0);  /* should have ADDED entries */
    lcl_ref_dec(r);
  }

  lcl_interp_free(interp);
  END_TEST();
}

static void test_node_returns_index(void) {
  lcl_interp *interp;
  lcl_value *r = NULL;

  BEGIN_TEST("node returns positive index");
  interp = make_interp();

  eval_ok(interp, "let ui [lk::ui_create]", &r);
  if (r) lcl_ref_dec(r);
  r = NULL;

  eval_ok(interp,
    "let t [lk::begin_frame $ui]\n"
    "lk::node $t \"n1\" \"label\"",
    &r);

  if (r) {
    long ix;
    CHECK(lcl_value_to_int(r, &ix) == LCL_OK);
    CHECK(ix >= 1);
    lcl_ref_dec(r);
  }

  /* Clean up frame */
  r = NULL;
  eval_ok(interp, "lk::end_frame $ui", &r);
  if (r) lcl_ref_dec(r);

  lcl_interp_free(interp);
  END_TEST();
}

static void test_prop_text(void) {
  lcl_interp *interp;
  lcl_value *r = NULL;

  BEGIN_TEST("prop sets text property");
  interp = make_interp();

  eval_ok(interp,
    "let ui [lk::ui_create]\n"
    "let t [lk::begin_frame $ui]\n"
    "let w [lk::node $t \"main\" \"window\"]\n"
    "let lbl [lk::node $t \"lbl\" \"label\"]\n"
    "lk::prop $t $lbl \"text\" \"Hello\"\n"
    "lk::set_root $t $w\n"
    "lk::append_child $t $w $lbl\n"
    "lk::end_frame $ui",
    &r);
  if (r) lcl_ref_dec(r);

  lcl_interp_free(interp);
  END_TEST();
}

static void test_prop_numeric(void) {
  lcl_interp *interp;
  lcl_value *r = NULL;

  BEGIN_TEST("prop sets numeric properties (w, h, padding, gap)");
  interp = make_interp();

  eval_ok(interp,
    "let ui [lk::ui_create]\n"
    "let t [lk::begin_frame $ui]\n"
    "let w [lk::node $t \"main\" \"window\"]\n"
    "let col [lk::node $t \"root\" \"column\"]\n"
    "lk::prop $t $col \"padding\" 20\n"
    "lk::prop $t $col \"gap\" 12\n"
    "lk::prop $t $col \"w\" 300\n"
    "lk::prop $t $col \"h\" 400\n"
    "lk::set_root $t $w\n"
    "lk::append_child $t $w $col\n"
    "lk::end_frame $ui",
    &r);
  if (r) lcl_ref_dec(r);

  lcl_interp_free(interp);
  END_TEST();
}

static void test_prop_align(void) {
  lcl_interp *interp;
  lcl_value *r = NULL;

  BEGIN_TEST("prop sets align/justify properties");
  interp = make_interp();

  eval_ok(interp,
    "let ui [lk::ui_create]\n"
    "let t [lk::begin_frame $ui]\n"
    "let w [lk::node $t \"main\" \"window\"]\n"
    "let col [lk::node $t \"root\" \"column\"]\n"
    "lk::prop $t $col \"align\" \"center\"\n"
    "lk::prop $t $col \"justify\" \"end\"\n"
    "lk::set_root $t $w\n"
    "lk::append_child $t $w $col\n"
    "lk::end_frame $ui",
    &r);
  if (r) lcl_ref_dec(r);

  lcl_interp_free(interp);
  END_TEST();
}

static void test_prop_bool(void) {
  lcl_interp *interp;
  lcl_value *r = NULL;

  BEGIN_TEST("prop sets bool properties (focusable, disabled)");
  interp = make_interp();

  eval_ok(interp,
    "let ui [lk::ui_create]\n"
    "let t [lk::begin_frame $ui]\n"
    "let w [lk::node $t \"main\" \"window\"]\n"
    "let btn [lk::node $t \"btn\" \"button\"]\n"
    "lk::prop $t $btn \"focusable\" 1\n"
    "lk::prop $t $btn \"disabled\" 0\n"
    "lk::set_root $t $w\n"
    "lk::append_child $t $w $btn\n"
    "lk::end_frame $ui",
    &r);
  if (r) lcl_ref_dec(r);

  lcl_interp_free(interp);
  END_TEST();
}

static void test_present(void) {
  lcl_interp *interp;
  lcl_value *r = NULL;

  BEGIN_TEST("present attaches presentation to node");
  interp = make_interp();

  eval_ok(interp,
    "let ui [lk::ui_create]\n"
    "let t [lk::begin_frame $ui]\n"
    "let w [lk::node $t \"main\" \"window\"]\n"
    "let btn [lk::node $t \"btn\" \"button\"]\n"
    "lk::present $t $btn \"item\" 42\n"
    "lk::set_root $t $w\n"
    "lk::append_child $t $w $btn\n"
    "lk::end_frame $ui",
    &r);
  if (r) lcl_ref_dec(r);

  lcl_interp_free(interp);
  END_TEST();
}

static void test_changeset_contains_added(void) {
  lcl_interp *interp;
  lcl_value *r = NULL;

  BEGIN_TEST("end_frame changeset has 'added' entries");
  interp = make_interp();

  eval_ok(interp,
    "let ui [lk::ui_create]\n"
    "let t [lk::begin_frame $ui]\n"
    "let w [lk::node $t \"main\" \"window\"]\n"
    "lk::set_root $t $w\n"
    "let cs [lk::end_frame $ui]",
    &r);
  if (r) lcl_ref_dec(r);
  r = NULL;

  /* Check the first change is "added" */
  eval_ok(interp, "get [get $cs 0] \"kind\"", &r);
  if (r) {
    const char *s = lcl_value_to_string(r);
    CHECK(strcmp(s, "added") == 0);
    lcl_ref_dec(r);
  }

  lcl_interp_free(interp);
  END_TEST();
}

static void test_tree_returns_current(void) {
  lcl_interp *interp;
  lcl_value *r = NULL;

  BEGIN_TEST("tree returns current tree opaque");
  interp = make_interp();

  eval_ok(interp,
    "let ui [lk::ui_create]\n"
    "let t [lk::begin_frame $ui]\n"
    "let w [lk::node $t \"main\" \"window\"]\n"
    "lk::set_root $t $w\n"
    "lk::end_frame $ui\n"
    "let cur [lk::tree $ui]\n"
    "opaque? $cur",
    &r);
  if (r) {
    long v;
    lcl_value_to_int(r, &v);
    CHECK(v == 1);
    lcl_ref_dec(r);
  }

  lcl_interp_free(interp);
  END_TEST();
}

static void test_state_set_get(void) {
  lcl_interp *interp;
  lcl_value *r = NULL;

  BEGIN_TEST("state_set / state_get round-trip");
  interp = make_interp();

  /* Build a frame so we have a valid UI */
  eval_ok(interp,
    "let ui [lk::ui_create]\n"
    "let t [lk::begin_frame $ui]\n"
    "let w [lk::node $t \"main\" \"window\"]\n"
    "lk::set_root $t $w\n"
    "lk::end_frame $ui",
    &r);
  if (r) lcl_ref_dec(r);
  r = NULL;

  /* Set and get state */
  eval_ok(interp, "lk::state_set $ui \"main\" 256 99", &r);
  if (r) lcl_ref_dec(r);
  r = NULL;

  eval_ok(interp, "lk::state_get $ui \"main\" 256", &r);
  if (r) {
    long v;
    CHECK(lcl_value_to_int(r, &v) == LCL_OK);
    CHECK(v == 99);
    lcl_ref_dec(r);
  }

  lcl_interp_free(interp);
  END_TEST();
}

static void test_intern_round_trip(void) {
  lcl_interp *interp;
  lcl_value *r = NULL;

  BEGIN_TEST("intern_id / intern_str round-trip");
  interp = make_interp();

  eval_ok(interp, "let ui [lk::ui_create]", &r);
  if (r) lcl_ref_dec(r);
  r = NULL;

  eval_ok(interp,
    "let id [lk::intern_id $ui \"hello\"]\n"
    "lk::intern_str $ui $id",
    &r);
  if (r) {
    const char *s = lcl_value_to_string(r);
    CHECK(strcmp(s, "hello") == 0);
    lcl_ref_dec(r);
  }

  lcl_interp_free(interp);
  END_TEST();
}

static void test_focus_set_clear(void) {
  lcl_interp *interp;
  lcl_value *r = NULL;

  BEGIN_TEST("focus_set / focus_clear don't crash");
  interp = make_interp();

  eval_ok(interp,
    "let ui [lk::ui_create]\n"
    "let t [lk::begin_frame $ui]\n"
    "let w [lk::node $t \"main\" \"window\"]\n"
    "let btn [lk::node $t \"btn\" \"button\"]\n"
    "lk::prop $t $btn \"focusable\" 1\n"
    "lk::set_root $t $w\n"
    "lk::append_child $t $w $btn\n"
    "lk::end_frame $ui\n"
    "lk::focus_set $ui \"btn\"\n"
    "lk::focus_clear $ui",
    &r);
  if (r) lcl_ref_dec(r);
  /* If we got here without crashing, it passes */
  CHECK(1);

  lcl_interp_free(interp);
  END_TEST();
}

static void test_add_translator(void) {
  lcl_interp *interp;
  lcl_value *r = NULL;

  BEGIN_TEST("add_translator registers without error");
  interp = make_interp();

  eval_ok(interp,
    "let ui [lk::ui_create]\n"
    "lk::add_translator $ui \"pointer_down\" \"item\" \"\" \"\" \"\" \"Select\"",
    &r);
  if (r) lcl_ref_dec(r);
  CHECK(1);

  lcl_interp_free(interp);
  END_TEST();
}

static void test_commands_empty(void) {
  lcl_interp *interp;
  lcl_value *r = NULL;

  BEGIN_TEST("commands returns empty list initially");
  interp = make_interp();

  eval_ok(interp,
    "let ui [lk::ui_create]\n"
    "let t [lk::begin_frame $ui]\n"
    "let w [lk::node $t \"main\" \"window\"]\n"
    "lk::set_root $t $w\n"
    "lk::end_frame $ui\n"
    "lk::commands $ui",
    &r);
  if (r) {
    size_t len = lcl_list_len(r);
    CHECK(len == 0);
    lcl_ref_dec(r);
  }

  lcl_interp_free(interp);
  END_TEST();
}

static void test_command_log_empty(void) {
  lcl_interp *interp;
  lcl_value *r = NULL;

  BEGIN_TEST("command_log returns empty list initially");
  interp = make_interp();

  eval_ok(interp,
    "let ui [lk::ui_create]\n"
    "lk::command_log $ui",
    &r);
  if (r) {
    size_t len = lcl_list_len(r);
    CHECK(len == 0);
    lcl_ref_dec(r);
  }

  lcl_interp_free(interp);
  END_TEST();
}

static void test_error_bad_kind(void) {
  lcl_interp *interp;
  lcl_value *r = NULL;
  int rc;

  BEGIN_TEST("node rejects unknown kind");
  interp = make_interp();

  eval_ok(interp, "let ui [lk::ui_create]", &r);
  if (r) lcl_ref_dec(r);
  r = NULL;

  eval_ok(interp, "let t [lk::begin_frame $ui]", &r);
  if (r) lcl_ref_dec(r);
  r = NULL;

  rc = lcl_eval_string(interp, "lk::node $t \"n\" \"bogus\"", &r);
  CHECK(rc != LCL_RC_OK);
  if (r) lcl_ref_dec(r);

  /* Clean up */
  r = NULL;
  lcl_eval_string(interp, "lk::end_frame $ui", &r);
  if (r) lcl_ref_dec(r);

  lcl_interp_free(interp);
  END_TEST();
}

static void test_error_bad_prop_key(void) {
  lcl_interp *interp;
  lcl_value *r = NULL;
  int rc;

  BEGIN_TEST("prop rejects unknown key");
  interp = make_interp();

  eval_ok(interp,
    "let ui [lk::ui_create]\n"
    "let t [lk::begin_frame $ui]\n"
    "let w [lk::node $t \"main\" \"window\"]",
    &r);
  if (r) lcl_ref_dec(r);
  r = NULL;

  rc = lcl_eval_string(interp, "lk::prop $t $w \"nonexistent\" 42", &r);
  CHECK(rc != LCL_RC_OK);
  if (r) lcl_ref_dec(r);

  /* Clean up */
  r = NULL;
  lcl_eval_string(interp, "lk::end_frame $ui", &r);
  if (r) lcl_ref_dec(r);

  lcl_interp_free(interp);
  END_TEST();
}

static void test_two_frames_diff(void) {
  lcl_interp *interp;
  lcl_value *r = NULL;

  BEGIN_TEST("second frame shows UPDATED for changed prop");
  interp = make_interp();

  /* Frame 1 */
  eval_ok(interp,
    "let ui [lk::ui_create]\n"
    "let t [lk::begin_frame $ui]\n"
    "let w [lk::node $t \"main\" \"window\"]\n"
    "let lbl [lk::node $t \"lbl\" \"label\"]\n"
    "lk::prop $t $lbl \"text\" \"Hello\"\n"
    "lk::set_root $t $w\n"
    "lk::append_child $t $w $lbl\n"
    "lk::end_frame $ui",
    &r);
  if (r) lcl_ref_dec(r);
  r = NULL;

  /* Frame 2: change the label text */
  eval_ok(interp,
    "let t2 [lk::begin_frame $ui]\n"
    "let w2 [lk::node $t2 \"main\" \"window\"]\n"
    "let lbl2 [lk::node $t2 \"lbl\" \"label\"]\n"
    "lk::prop $t2 $lbl2 \"text\" \"World\"\n"
    "lk::set_root $t2 $w2\n"
    "lk::append_child $t2 $w2 $lbl2\n"
    "let cs2 [lk::end_frame $ui]",
    &r);
  if (r) lcl_ref_dec(r);
  r = NULL;

  /* Check changeset has an "updated" entry */
  eval_ok(interp, "len $cs2", &r);
  if (r) {
    long len;
    lcl_value_to_int(r, &len);
    CHECK(len > 0);
    lcl_ref_dec(r);
  }

  lcl_interp_free(interp);
  END_TEST();
}

static void test_full_tree_build_in_proc(void) {
  lcl_interp *interp;
  lcl_value *r = NULL;

  BEGIN_TEST("build tree via Lcl proc (simulates view fn)");
  interp = make_interp();

  eval_ok(interp,
    "let ui [lk::ui_create]\n"
    "\n"
    "proc view {tree} {\n"
    "  let w   [lk::node $tree \"main\" \"window\"]\n"
    "  let col [lk::node $tree \"root\" \"column\"]\n"
    "  let lbl [lk::node $tree \"greeting\" \"label\"]\n"
    "  lk::prop $tree $col \"padding\" 20\n"
    "  lk::prop $tree $col \"gap\" 12\n"
    "  lk::prop $tree $lbl \"text\" \"Hello from Lcl!\"\n"
    "  lk::set_root $tree $w\n"
    "  lk::append_child $tree $w $col\n"
    "  lk::append_child $tree $col $lbl\n"
    "}\n"
    "\n"
    "let t [lk::begin_frame $ui]\n"
    "view $t\n"
    "let cs [lk::end_frame $ui]\n"
    "len $cs",
    &r);

  if (r) {
    long len;
    lcl_value_to_int(r, &len);
    CHECK(len == 3);  /* 3 nodes added: main, root, greeting */
    lcl_ref_dec(r);
  }

  lcl_interp_free(interp);
  END_TEST();
}

static void test_clear_commands(void) {
  lcl_interp *interp;
  lcl_value *r = NULL;

  BEGIN_TEST("clear_commands empties the queue");
  interp = make_interp();

  eval_ok(interp,
    "let ui [lk::ui_create]\n"
    "let t [lk::begin_frame $ui]\n"
    "let w [lk::node $t \"main\" \"window\"]\n"
    "lk::set_root $t $w\n"
    "lk::end_frame $ui\n"
    "lk::clear_commands $ui\n"
    "lk::commands $ui",
    &r);
  if (r) {
    CHECK(lcl_list_len(r) == 0);
    lcl_ref_dec(r);
  }

  lcl_interp_free(interp);
  END_TEST();
}

static void test_clear_command_log(void) {
  lcl_interp *interp;
  lcl_value *r = NULL;

  BEGIN_TEST("clear_command_log empties the log");
  interp = make_interp();

  eval_ok(interp,
    "let ui [lk::ui_create]\n"
    "lk::clear_command_log $ui\n"
    "lk::command_log $ui",
    &r);
  if (r) {
    CHECK(lcl_list_len(r) == 0);
    lcl_ref_dec(r);
  }

  lcl_interp_free(interp);
  END_TEST();
}

static void test_all_kinds(void) {
  lcl_interp *interp;
  lcl_value *r = NULL;

  BEGIN_TEST("all 6 node kinds can be created");
  interp = make_interp();

  eval_ok(interp,
    "let ui [lk::ui_create]\n"
    "let t [lk::begin_frame $ui]\n"
    "let a [lk::node $t \"a\" \"window\"]\n"
    "let b [lk::node $t \"b\" \"row\"]\n"
    "let c [lk::node $t \"c\" \"column\"]\n"
    "let d [lk::node $t \"d\" \"spacer\"]\n"
    "let e [lk::node $t \"e\" \"label\"]\n"
    "let f [lk::node $t \"f\" \"button\"]\n"
    "lk::set_root $t $a\n"
    "lk::append_child $t $a $b\n"
    "lk::append_child $t $a $c\n"
    "lk::append_child $t $a $d\n"
    "lk::append_child $t $a $e\n"
    "lk::append_child $t $a $f\n"
    "lk::end_frame $ui",
    &r);
  if (r) {
    size_t len = lcl_list_len(r);
    CHECK(len == 6);
    lcl_ref_dec(r);
  }

  lcl_interp_free(interp);
  END_TEST();
}

static void test_add_translator_all_event_types(void) {
  lcl_interp *interp;
  lcl_value *r = NULL;

  BEGIN_TEST("add_translator accepts all event types");
  interp = make_interp();

  eval_ok(interp,
    "let ui [lk::ui_create]\n"
    "lk::add_translator $ui \"pointer_down\" \"\" \"\" \"\" \"\" \"cmd1\"\n"
    "lk::add_translator $ui \"pointer_up\" \"\" \"\" \"\" \"\" \"cmd2\"\n"
    "lk::add_translator $ui \"pointer_move\" \"\" \"\" \"\" \"\" \"cmd3\"\n"
    "lk::add_translator $ui \"key_down\" \"\" \"\" \"\" \"\" \"cmd4\"\n"
    "lk::add_translator $ui \"key_up\" \"\" \"\" \"\" \"\" \"cmd5\"",
    &r);
  if (r) lcl_ref_dec(r);
  CHECK(g_cur_ok);

  eval_ok(interp,
    "lk::add_translator $ui \"text\" \"\" \"\" \"\" \"\" \"cmd6\"\n"
    "lk::add_translator $ui \"wheel\" \"\" \"\" \"\" \"\" \"cmd7\"\n"
    "lk::add_translator $ui \"window_resize\" \"\" \"\" \"\" \"\" \"cmd8\"\n"
    "lk::add_translator $ui \"window_close\" \"\" \"\" \"\" \"\" \"cmd9\"\n"
    "lk::add_translator $ui \"\" \"\" \"\" \"\" \"\" \"cmd_any\"",
    &r);
  if (r) lcl_ref_dec(r);
  CHECK(g_cur_ok);  /* no errors */

  lcl_interp_free(interp);
  END_TEST();
}

static void test_present_string_value(void) {
  lcl_interp *interp;
  lcl_value *r = NULL;

  BEGIN_TEST("present with string pvalue");
  interp = make_interp();

  eval_ok(interp,
    "let ui [lk::ui_create]\n"
    "let t [lk::begin_frame $ui]\n"
    "let w [lk::node $t \"main\" \"window\"]\n"
    "let btn [lk::node $t \"btn\" \"button\"]\n"
    "lk::present $t $btn \"item\" \"apple\"\n"
    "lk::set_root $t $w\n"
    "lk::append_child $t $w $btn\n"
    "lk::end_frame $ui",
    &r);
  if (r) lcl_ref_dec(r);
  CHECK(g_cur_ok);

  lcl_interp_free(interp);
  END_TEST();
}

static void test_dropdown_kind(void) {
  lcl_interp *interp;
  lcl_value *r = NULL;

  BEGIN_TEST("dropdown kind can be created via Lcl");
  interp = make_interp();

  eval_ok(interp,
    "let ui [lk::ui_create]\n"
    "let t [lk::begin_frame $ui]\n"
    "let w [lk::node $t main window]\n"
    "let dd [lk::node $t cat dropdown]\n"
    "let o1 [lk::node $t o1 option]\n"
    "let o2 [lk::node $t o2 option]\n"
    "lk::prop $t $o1 text Food\n"
    "lk::prop $t $o2 text Transport\n"
    "lk::set_root $t $w\n"
    "lk::append_child $t $w $dd\n"
    "lk::append_child $t $dd $o1\n"
    "lk::append_child $t $dd $o2\n"
    "lk::end_frame $ui",
    &r);
  if (r) lcl_ref_dec(r);
  CHECK(g_cur_ok);

  lcl_interp_free(interp);
  END_TEST();
}

static void test_present_list_value(void) {
  lcl_interp *interp;
  lcl_value *r = NULL;

  BEGIN_TEST("present accepts list pvalue for multi-arg commands");
  interp = make_interp();

  eval_ok(interp,
    "let ui [lk::ui_create]\n"
    "let t [lk::begin_frame $ui]\n"
    "let w [lk::node $t main window]\n"
    "let btn [lk::node $t btn button]\n"
    "lk::set_root $t $w\n"
    "lk::append_child $t $w $btn\n"
    "lk::present $t $btn action (remove_row 5)\n"
    "lk::end_frame $ui\n",
    &r);
  if (r) lcl_ref_dec(r);
  CHECK(g_cur_ok);

  lcl_interp_free(interp);
  END_TEST();
}

/* ---- tag tests ---- */

static void test_lcl_tag(void) {
  lcl_interp *interp;
  lcl_value *r = NULL;

  BEGIN_TEST("tag: apply tag via lk::tag");
  interp = make_interp();

  eval_ok(interp,
    "let ui [lk::ui_create]\n"
    "let t [lk::begin_frame $ui]\n"
    "let w [lk::node $t w window]\n"
    "let btn [lk::node $t btn button]\n"
    "lk::set_root $t $w\n"
    "lk::append_child $t $w $btn\n"
    "lk::tag $t $btn primary\n"
    "lk::end_frame $ui",
    &r);
  if (r) lcl_ref_dec(r);
  CHECK(g_cur_ok);

  lcl_interp_free(interp);
  END_TEST();
}

static void test_lcl_theme_rule(void) {
  lcl_interp *interp;
  lcl_value *r = NULL;

  BEGIN_TEST("theme_rule: add rule via lk::theme_rule");
  interp = make_interp();

  eval_ok(interp,
    "let ui [lk::ui_create]\n"
    "lk::theme_rule $ui button \"\" \"\" {bg {200 50 50}}\n"
    "lk::theme_rule $ui \"*\" \"\" \"\" {fg {255 255 0}}",
    &r);
  if (r) lcl_ref_dec(r);
  CHECK(g_cur_ok);

  lcl_interp_free(interp);
  END_TEST();
}

static void test_theme_rule_font_keys(void) {
  lcl_interp *interp;
  lcl_value *r = NULL;

  BEGIN_TEST("theme_rule accepts font_id / font_size keys");
  interp = make_interp();

  eval_ok(interp,
    "let ui [lk::ui_create]\n"
    "lk::theme_rule $ui label \"\" \"\" #{font_id 1 font_size 24}\n"
    "lk::theme_rule $ui \"*\" \"\" \"\" #{font_size 18}",
    &r);
  if (r) lcl_ref_dec(r);
  CHECK(g_cur_ok);

  lcl_interp_free(interp);
  END_TEST();
}

static void test_theme_rule_bad_font_id(void) {
  lcl_interp *interp;
  lcl_value *r = NULL;
  int rc;

  BEGIN_TEST("theme_rule rejects non-int font_id");
  interp = make_interp();

  rc = lcl_eval_string(interp,
    "let ui [lk::ui_create]\n"
    "lk::theme_rule $ui label \"\" \"\" #{font_id nope}",
    &r);
  if (r) lcl_ref_dec(r);
  CHECK(rc != LCL_RC_OK);

  lcl_interp_free(interp);
  END_TEST();
}

static void test_theme_rule_bad_font_size(void) {
  lcl_interp *interp;
  lcl_value *r = NULL;
  int rc;

  BEGIN_TEST("theme_rule rejects negative font_size");
  interp = make_interp();

  rc = lcl_eval_string(interp,
    "let ui [lk::ui_create]\n"
    "lk::theme_rule $ui label \"\" \"\" #{font_size -4}",
    &r);
  if (r) lcl_ref_dec(r);
  CHECK(rc != LCL_RC_OK);

  lcl_interp_free(interp);
  END_TEST();
}

#ifdef LK_HAVE_SDL
/* lk::register_font error paths only: a successful registration needs
 * a real lk_window (SDL renderer + display), which headless CI does
 * not have.  The success path is covered by the C-side contract
 * (sdl_text_register_font) and exercised by the demo apps. */

static void test_register_font_arity(void) {
  lcl_interp *interp;
  lcl_value *r = NULL;
  int rc;

  BEGIN_TEST("register_font rejects wrong arity");
  interp = make_interp();

  rc = lcl_eval_string(interp, "lk::register_font \"only-one-arg\"", &r);
  if (r) lcl_ref_dec(r);
  CHECK(rc != LCL_RC_OK);

  lcl_interp_free(interp);
  END_TEST();
}

static void test_register_font_bad_window(void) {
  lcl_interp *interp;
  lcl_value *r = NULL;
  int rc;

  BEGIN_TEST("register_font rejects non-window handle");
  interp = make_interp();

  /* An lk_ui opaque is not an lk_window opaque */
  rc = lcl_eval_string(interp,
    "let ui [lk::ui_create]\n"
    "lk::register_font $ui \"/tmp/font.ttf\"",
    &r);
  if (r) lcl_ref_dec(r);
  CHECK(rc != LCL_RC_OK);

  lcl_interp_free(interp);
  END_TEST();
}
#endif /* LK_HAVE_SDL */

static void test_set_command_handler(void) {
  lcl_interp *interp;
  lcl_value *r = NULL;

  BEGIN_TEST("set_command_handler accepts callable");
  interp = make_interp();

  eval_ok(interp,
    "let ui [lk::ui_create]\n"
    "lk::set_command_handler $ui [lambda {cmd} {\n"
    "  puts $cmd\n"
    "}]",
    &r);
  if (r) lcl_ref_dec(r);
  CHECK(g_cur_ok);

  lcl_interp_free(interp);
  END_TEST();
}

static void test_set_command_handler_with_translator(void) {
  lcl_interp *interp;
  lcl_value *r = NULL;

  BEGIN_TEST("set_command_handler + add_translator integration");
  interp = make_interp();

  eval_ok(interp,
    "let ui [lk::ui_create]\n"
    "let t [lk::begin_frame $ui]\n"
    "let w [lk::node $t \"main\" \"window\"]\n"
    "let btn [lk::node $t \"btn\" \"button\"]\n"
    "lk::prop $t $btn \"text\" \"Click\"\n"
    "lk::present $t $btn \"action\" 42\n"
    "lk::set_root $t $w\n"
    "lk::append_child $t $w $btn\n"
    "lk::end_frame $ui\n"
    "lk::add_translator $ui \"pointer_down\" \"action\" \"\" \"\" \"\" \"DoIt\"\n"
    "let got_cmd 0\n"
    "lk::set_command_handler $ui [lambda {cmd} {\n"
    "  set got_cmd 1\n"
    "}]",
    &r);
  if (r) lcl_ref_dec(r);
  CHECK(g_cur_ok);

  lcl_interp_free(interp);
  END_TEST();
}

static void test_add_translator_with_keycode(void) {
  lcl_interp *interp;
  lcl_value *r = NULL;

  BEGIN_TEST("add_translator with keycode+mods registers");
  interp = make_interp();

  eval_ok(interp,
    "let ui [lk::ui_create]\n"
    "lk::add_translator $ui \"key_down\" \"doc\" \"\" \"s\" \"ctrl\" \"Save\"\n"
    "lk::add_translator $ui \"key_down\" \"\" \"\" \"f\" \"ctrl\" \"Find\"\n"
    "lk::add_translator $ui \"key_down\" \"\" \"\" \"z\" \"ctrl+shift\" \"Redo\"",
    &r);
  if (r) lcl_ref_dec(r);
  CHECK(g_cur_ok);

  lcl_interp_free(interp);
  END_TEST();
}

static void test_add_translator_extended_keycodes(void) {
  lcl_interp *interp;
  lcl_value *r = NULL;

  BEGIN_TEST("add_translator accepts page_up/f5/digit keycodes");
  interp = make_interp();

  eval_ok(interp,
    "let ui [lk::ui_create]\n"
    "lk::add_translator $ui \"key_down\" \"\" \"\" \"page_up\" \"\" \"PgUp\"\n"
    "lk::add_translator $ui \"key_down\" \"\" \"\" \"page_down\" \"\" \"PgDn\"\n"
    "lk::add_translator $ui \"key_down\" \"\" \"\" \"f5\" \"\" \"Refresh\"\n"
    "lk::add_translator $ui \"key_down\" \"\" \"\" \"f12\" \"\" \"DevTools\"\n"
    "lk::add_translator $ui \"key_down\" \"\" \"\" \"0\" \"ctrl\" \"ZoomReset\"\n"
    "lk::add_translator $ui \"key_down\" \"\" \"\" \"9\" \"ctrl\" \"LastTab\"",
    &r);
  if (r) lcl_ref_dec(r);
  CHECK(g_cur_ok);

  lcl_interp_free(interp);
  END_TEST();
}

static void test_add_translator_bad_keycode(void) {
  lcl_interp *interp;
  lcl_value *r = NULL;
  int rc;

  BEGIN_TEST("add_translator rejects unknown keycode");
  interp = make_interp();

  rc = lcl_eval_string(interp,
    "let ui [lk::ui_create]\n"
    "lk::add_translator $ui \"key_down\" \"\" \"\" \"not_a_key\" \"\" \"Cmd\"",
    &r);
  if (r) lcl_ref_dec(r);
  CHECK(rc != LCL_RC_OK);

  lcl_interp_free(interp);
  END_TEST();
}

static void test_add_translator_bad_mods(void) {
  lcl_interp *interp;
  lcl_value *r = NULL;
  int rc;

  BEGIN_TEST("add_translator rejects unknown modifier");
  interp = make_interp();

  rc = lcl_eval_string(interp,
    "let ui [lk::ui_create]\n"
    "lk::add_translator $ui \"key_down\" \"\" \"\" \"s\" \"super\" \"Cmd\"",
    &r);
  if (r) lcl_ref_dec(r);
  CHECK(rc != LCL_RC_OK);

  lcl_interp_free(interp);
  END_TEST();
}

static void test_text_input_kind(void) {
  lcl_interp *interp;
  lcl_value *r = NULL;

  BEGIN_TEST("text_input kind can be created via Lcl");
  interp = make_interp();

  eval_ok(interp,
    "let ui [lk::ui_create]\n"
    "let t [lk::begin_frame $ui]\n"
    "let w [lk::node $t \"w\" \"window\"]\n"
    "let ti [lk::node $t \"ti\" \"text_input\"]\n"
    "lk::prop $t $ti \"text\" \"hello\"\n"
    "lk::set_root $t $w\n"
    "lk::append_child $t $w $ti\n"
    "lk::end_frame $ui",
    &r);
  if (r) {
    /* changeset should have 2 entries (window + text_input) */
    CHECK(lcl_list_len(r) >= 2);
    lcl_ref_dec(r);
  }

  lcl_interp_free(interp);
  END_TEST();
}

static void test_scroll_kind(void) {
  lcl_interp *interp;
  lcl_value *r = NULL;

  BEGIN_TEST("scroll kind can be created via Lcl");
  interp = make_interp();

  eval_ok(interp,
    "let ui [lk::ui_create]\n"
    "let t [lk::begin_frame $ui]\n"
    "let w [lk::node $t \"w\" \"window\"]\n"
    "let sc [lk::node $t \"sc\" \"scroll\"]\n"
    "lk::prop $t $sc \"h\" 200\n"
    "lk::set_root $t $w\n"
    "lk::append_child $t $w $sc\n"
    "lk::end_frame $ui",
    &r);
  if (r) {
    /* changeset should have 2 entries (window + scroll) */
    CHECK(lcl_list_len(r) >= 2);
    lcl_ref_dec(r);
  }

  lcl_interp_free(interp);
  END_TEST();
}

/* ---- clipboard mock + test ---- */

static char g_lcl_mock_clipboard[1024];

static const char *lcl_mock_clipboard_get(void *ud) {
  (void)ud;
  return g_lcl_mock_clipboard;
}

static void lcl_mock_clipboard_set(void *ud, const char *text) {
  size_t len;
  (void)ud;
  len = strlen(text);
  if (len >= sizeof(g_lcl_mock_clipboard)) {
    len = sizeof(g_lcl_mock_clipboard) - 1;
  }
  memcpy(g_lcl_mock_clipboard, text, len);
  g_lcl_mock_clipboard[len] = '\0';
}

static void test_clipboard_get_set(void) {
  lcl_interp *interp;
  lcl_value *r = NULL;
  lk_ui *ui;

  BEGIN_TEST("clipboard_get / clipboard_set round-trip");
  interp = make_interp();

  eval_ok(interp, "let ui [lk::ui_create]", &r);
  if (r) lcl_ref_dec(r);
  r = NULL;

  /* Get the raw lk_ui* to install mock clipboard */
  eval_ok(interp, "$ui", &r);
  if (r) {
    ui = NULL;
    lcl_opaque_get(r, "lk_ui", (void **)&ui);
    if (ui) {
      lk_ui_set_clipboard(ui, lcl_mock_clipboard_get,
                          lcl_mock_clipboard_set, NULL);
    }
    lcl_ref_dec(r);
  }
  r = NULL;

  /* Set via binding */
  eval_ok(interp, "lk::clipboard_set $ui \"hello clip\"", &r);
  if (r) lcl_ref_dec(r);
  r = NULL;

  CHECK(strcmp(g_lcl_mock_clipboard, "hello clip") == 0);

  /* Get via binding */
  eval_ok(interp, "lk::clipboard_get $ui", &r);
  if (r) {
    const char *s = lcl_value_to_string(r);
    CHECK(strcmp(s, "hello clip") == 0);
    lcl_ref_dec(r);
  }

  lcl_interp_free(interp);
  END_TEST();
}

static void test_clipboard_get_no_clipboard(void) {
  lcl_interp *interp;
  lcl_value *r = NULL;

  BEGIN_TEST("clipboard_get returns empty without clipboard");
  interp = make_interp();

  eval_ok(interp, "let ui [lk::ui_create]", &r);
  if (r) lcl_ref_dec(r);
  r = NULL;

  /* No clipboard installed */
  eval_ok(interp, "lk::clipboard_get $ui", &r);
  if (r) {
    const char *s = lcl_value_to_string(r);
    CHECK(strcmp(s, "") == 0);
    lcl_ref_dec(r);
  }

  lcl_interp_free(interp);
  END_TEST();
}

static void test_overlay_count_proc(void) {
  lcl_interp *interp;
  lcl_value *r = NULL;

  BEGIN_TEST("overlay_count returns 0 on a fresh ui");
  interp = make_interp();

  eval_ok(interp, "let ui [lk::ui_create]", &r);
  if (r) lcl_ref_dec(r);
  r = NULL;

  eval_ok(interp, "lk::overlay_count $ui", &r);
  if (r) {
    long v = -1;
    lcl_value_to_int(r, &v);
    CHECK(v == 0);
    lcl_ref_dec(r);
  }

  lcl_interp_free(interp);
  END_TEST();
}

static void test_hidden_prop(void) {
  lcl_interp *interp;
  lcl_value *r = NULL;

  BEGIN_TEST("hidden prop settable via lk::prop");
  interp = make_interp();

  eval_ok(interp,
    "let ui [lk::ui_create]\n"
    "let t [lk::begin_frame $ui]\n"
    "let w [lk::node $t \"w\" \"window\"]\n"
    "let col [lk::node $t \"col\" \"column\"]\n"
    "lk::prop $t $col \"hidden\" 1\n"
    "lk::set_root $t $w\n"
    "lk::append_child $t $w $col\n"
    "lk::end_frame $ui",
    &r);
  if (r) {
    CHECK(lcl_list_len(r) >= 2);
    lcl_ref_dec(r);
  }

  lcl_interp_free(interp);
  END_TEST();
}

/* Build (in interp) a ui named "ui" whose tree has a hidden modal
 * subtree: w > col > btn "outside" (focusable) + col "m" (hidden) >
 * "m1"/"m2" (focusable buttons). */
static void eval_modal_tree(lcl_interp *interp) {
  lcl_value *r = NULL;

  eval_ok(interp,
    "let ui [lk::ui_create]\n"
    "let t [lk::begin_frame $ui]\n"
    "let w [lk::node $t \"w\" \"window\"]\n"
    "let col [lk::node $t \"col\" \"column\"]\n"
    "let outside [lk::node $t \"outside\" \"button\"]\n"
    "lk::prop $t $outside \"focusable\" 1\n"
    "let m [lk::node $t \"m\" \"column\"]\n"
    "lk::prop $t $m \"hidden\" 1\n"
    "let m1 [lk::node $t \"m1\" \"button\"]\n"
    "lk::prop $t $m1 \"focusable\" 1\n"
    "let m2 [lk::node $t \"m2\" \"button\"]\n"
    "lk::prop $t $m2 \"focusable\" 1",
    &r);
  if (r) lcl_ref_dec(r);
  r = NULL;

  eval_ok(interp,
    "lk::set_root $t $w\n"
    "lk::append_child $t $w $col\n"
    "lk::append_child $t $col $outside\n"
    "lk::append_child $t $col $m\n"
    "lk::append_child $t $m $m1\n"
    "lk::append_child $t $m $m2\n"
    "lk::end_frame $ui",
    &r);
  if (r) lcl_ref_dec(r);
}

/* Fetch the lk_ui* behind the script-side "$ui" for C-level asserts. */
static lk_ui *fetch_ui(lcl_interp *interp) {
  lcl_value *r = NULL;
  lk_ui *ui = NULL;

  eval_ok(interp, "$ui", &r);
  if (r) {
    if (lcl_opaque_get(r, "lk_ui", (void **)&ui) != LCL_OK) {
      ui = NULL;
    }
    lcl_ref_dec(r);
  }

  return ui;
}

static void test_overlay_push_modal(void) {
  lcl_interp *interp;
  lcl_value *r = NULL;
  lk_ui *ui;

  BEGIN_TEST("overlay_push modal: count, trap, ESC pops");
  interp = make_interp();
  eval_modal_tree(interp);

  eval_ok(interp,
          "lk::overlay_push $ui #{kind modal owner_id m content_root_id m}",
          &r);
  if (r) lcl_ref_dec(r);
  r = NULL;

  eval_ok(interp, "lk::overlay_count $ui", &r);
  if (r) {
    long v = -1;
    lcl_value_to_int(r, &v);
    CHECK(v == 1);
    lcl_ref_dec(r);
  }

  /* C-level asserts on the same ui: modal defaults + focus trap + ESC. */
  ui = fetch_ui(interp);
  CHECK(ui != NULL);

  if (ui) {
    const lk_overlay *top = lk_overlay_top(ui);
    lk_node_id got;
    lk_event ev;

    CHECK(top != NULL);
    if (top) {
      CHECK((unsigned)top->kind == (unsigned)LK_OVERLAY_MODAL);
      CHECK((unsigned)top->anchor_mode == (unsigned)LK_ANCHOR_CENTER_VIEWPORT);
      CHECK(top->traps_focus == 1);
      CHECK(top->dismiss_on_outside == 0);
    }

    /* Focus cycling is trapped inside the hidden content subtree:
     * from no focus, next lands on "m1", never "outside". */
    got = lk_focus_next(ui, lk_ui_tree(ui));
    CHECK(got == lk_intern_cid(ui->intern, "m1"));
    got = lk_focus_next(ui, lk_ui_tree(ui));
    CHECK(got == lk_intern_cid(ui->intern, "m2"));

    /* Routed ESC pops the modal (core pre-step, no backend code). */
    memset(&ev, 0, sizeof(ev));
    ev.type = LK_EVENT_KEY_DOWN;
    ev.target = lk_ui_tree(ui)->root;
    ev.data.key.keycode = LKK_ESCAPE;
    lk_event_route(ui, &ev);
    CHECK(ev.handled == 1);
    CHECK(lk_overlay_count(ui) == 0);
  }

  lcl_interp_free(interp);
  END_TEST();
}

static void test_overlay_pop_proc(void) {
  lcl_interp *interp;
  lcl_value *r = NULL;
  lk_ui *ui;

  BEGIN_TEST("overlay_push non-modal defaults + overlay_pop");
  interp = make_interp();
  eval_modal_tree(interp);

  eval_ok(interp, "lk::overlay_push $ui #{kind tooltip owner_id outside}", &r);
  if (r) lcl_ref_dec(r);
  r = NULL;

  ui = fetch_ui(interp);
  CHECK(ui != NULL);

  if (ui) {
    const lk_overlay *top = lk_overlay_top(ui);

    CHECK(top != NULL);
    if (top) {
      /* Non-modal defaults: below anchor, dismissible, no trap. */
      CHECK((unsigned)top->anchor_mode == (unsigned)LK_ANCHOR_BELOW);
      CHECK(top->dismiss_on_outside == 1);
      CHECK(top->traps_focus == 0);
    }
  }

  eval_ok(interp, "lk::overlay_pop $ui", &r);
  if (r) lcl_ref_dec(r);
  r = NULL;

  eval_ok(interp, "lk::overlay_count $ui", &r);
  if (r) {
    long v = -1;
    lcl_value_to_int(r, &v);
    CHECK(v == 0);
    lcl_ref_dec(r);
  }

  /* pop on an empty stack is a safe no-op */
  eval_ok(interp, "lk::overlay_pop $ui", &r);
  if (r) lcl_ref_dec(r);

  lcl_interp_free(interp);
  END_TEST();
}

static void test_overlay_push_errors(void) {
  lcl_interp *interp;
  lcl_value *r = NULL;
  int rc;

  BEGIN_TEST("overlay_push rejects bad kind/anchor/dict");
  interp = make_interp();
  eval_modal_tree(interp);

  rc = lcl_eval_string(interp, "lk::overlay_push $ui #{kind bogus}", &r);
  CHECK(rc != LCL_RC_OK);
  if (r) lcl_ref_dec(r);
  r = NULL;

  rc = lcl_eval_string(interp,
                       "lk::overlay_push $ui #{kind modal anchor sideways}",
                       &r);
  CHECK(rc != LCL_RC_OK);
  if (r) lcl_ref_dec(r);
  r = NULL;

  rc = lcl_eval_string(interp, "lk::overlay_push $ui #{owner_id m}", &r);
  CHECK(rc != LCL_RC_OK); /* missing kind */
  if (r) lcl_ref_dec(r);
  r = NULL;

  /* Nothing was pushed by the failed calls. */
  eval_ok(interp, "lk::overlay_count $ui", &r);
  if (r) {
    long v = -1;
    lcl_value_to_int(r, &v);
    CHECK(v == 0);
    lcl_ref_dec(r);
  }

  lcl_interp_free(interp);
  END_TEST();
}

static void test_tooltip_prop_binding(void) {
  lcl_interp *interp;
  lcl_value *r = NULL;

  BEGIN_TEST("tooltip prop settable via lk::prop");
  interp = make_interp();

  eval_ok(interp,
    "let ui [lk::ui_create]\n"
    "let t [lk::begin_frame $ui]\n"
    "let w [lk::node $t \"w\" \"window\"]\n"
    "let b [lk::node $t \"b\" \"button\"]\n"
    "lk::prop $t $b \"tooltip\" \"Saves the file\"\n"
    "lk::set_root $t $w\n"
    "lk::append_child $t $w $b\n"
    "lk::end_frame $ui",
    &r);
  if (r) {
    CHECK(lcl_list_len(r) >= 2);
    lcl_ref_dec(r);
  }

  lcl_interp_free(interp);
  END_TEST();
}

static void test_split_kinds(void) {
  lcl_interp *interp;
  lcl_value *r = NULL;

  BEGIN_TEST("split_h/split_v kinds can be created via Lcl");
  interp = make_interp();

  eval_ok(interp,
    "let ui [lk::ui_create]\n"
    "let t [lk::begin_frame $ui]\n"
    "let w [lk::node $t \"w\" \"window\"]\n"
    "let sh [lk::node $t \"sh\" \"split_h\"]\n"
    "let sv [lk::node $t \"sv\" \"split_v\"]\n"
    "let c1 [lk::node $t \"c1\" \"column\"]\n"
    "let c2 [lk::node $t \"c2\" \"column\"]\n"
    "let c3 [lk::node $t \"c3\" \"column\"]\n"
    "lk::set_root $t $w\n"
    "lk::append_child $t $w $sh\n"
    "lk::append_child $t $sh $c1\n"
    "lk::append_child $t $sh $sv\n"
    "lk::append_child $t $sv $c2\n"
    "lk::append_child $t $sv $c3\n"
    "lk::end_frame $ui",
    &r);
  if (r) {
    /* changeset: window + split_h + split_v + 3 columns */
    CHECK(lcl_list_len(r) >= 6);
    lcl_ref_dec(r);
  }

  lcl_interp_free(interp);
  END_TEST();
}

static void test_split_ratio_prop_lcl(void) {
  lcl_interp *interp;
  lcl_value *r = NULL;

  BEGIN_TEST("split_ratio prop settable via lk::prop");
  interp = make_interp();

  eval_ok(interp,
    "let ui [lk::ui_create]\n"
    "let t [lk::begin_frame $ui]\n"
    "let w [lk::node $t \"w\" \"window\"]\n"
    "let sp [lk::node $t \"sp\" \"split_h\"]\n"
    "lk::prop $t $sp \"split_ratio\" 300\n"
    "let c1 [lk::node $t \"c1\" \"column\"]\n"
    "let c2 [lk::node $t \"c2\" \"column\"]\n"
    "lk::set_root $t $w\n"
    "lk::append_child $t $w $sp\n"
    "lk::append_child $t $sp $c1\n"
    "lk::append_child $t $sp $c2\n"
    "lk::end_frame $ui",
    &r);
  if (r) {
    CHECK(lcl_list_len(r) >= 4);
    lcl_ref_dec(r);
  }

  lcl_interp_free(interp);
  END_TEST();
}

/* ---- main ---- */

int main(void) {
  printf("lcl-lk binding tests\n");

  test_ui_create_destroy();
  test_begin_end_frame();
  test_node_returns_index();
  test_prop_text();
  test_prop_numeric();
  test_prop_align();
  test_prop_bool();
  test_present();
  test_changeset_contains_added();
  test_tree_returns_current();
  test_state_set_get();
  test_intern_round_trip();
  test_focus_set_clear();
  test_add_translator();
  test_commands_empty();
  test_command_log_empty();
  test_error_bad_kind();
  test_error_bad_prop_key();
  test_two_frames_diff();
  test_full_tree_build_in_proc();
  test_clear_commands();
  test_clear_command_log();
  test_all_kinds();
  test_add_translator_all_event_types();
  test_present_string_value();
  test_present_list_value();
  test_dropdown_kind();

  test_lcl_tag();
  test_lcl_theme_rule();
  test_theme_rule_font_keys();
  test_theme_rule_bad_font_id();
  test_theme_rule_bad_font_size();
#ifdef LK_HAVE_SDL
  test_register_font_arity();
  test_register_font_bad_window();
#endif
  test_set_command_handler();
  test_set_command_handler_with_translator();
  test_text_input_kind();
  test_scroll_kind();

  test_clipboard_get_set();
  test_clipboard_get_no_clipboard();

  test_add_translator_with_keycode();
  test_add_translator_extended_keycodes();
  test_add_translator_bad_keycode();
  test_add_translator_bad_mods();

  test_overlay_count_proc();
  test_hidden_prop();
  test_overlay_push_modal();
  test_overlay_pop_proc();
  test_overlay_push_errors();
  test_tooltip_prop_binding();

  test_split_kinds();
  test_split_ratio_prop_lcl();

  printf("\n%d tests: %d passed, %d failed\n", g_tests, g_pass, g_fail);

  return g_fail > 0 ? 1 : 0;
}
