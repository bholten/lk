/*
 * lcl-lk-test.c — Headless tests for the lk Lcl bindings.
 *
 * Uses lcl_eval_string to exercise all non-SDL procs.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <lcl.h>
#include <lk-editor.h>
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

/* ============================================================================
 * Layer-2 DSL tests (lib/lk-dsl.lcl) — DSL v2 harness.
 *
 * These pin the v2 props-dict syntax (docs/dsl-v2.md §3 candidate A):
 * `kind id ?props-dict? ?body-block?`, disambiguated by value shape
 * (dict = props, anything else = body).  Unknown prop keys are hard
 * errors carrying the widget id and known-key list; malformed trailing
 * args (numeric, stale -flag, second dict) are hard errors too.
 *
 * The `app` proc is NOT tested here: it creates an SDL window and runs
 * the event loop (lk::window_create / lk::window_run), which needs a
 * display.  It is exercised by the examples under the dummy driver.
 * ============================================================================
 */

#ifndef TEST_DSL_PATH
#define TEST_DSL_PATH "lib/lk-dsl.lcl"
#endif

/* Interp with core + lk + the Layer-2 DSL prelude evaluated. */
static lcl_interp *make_dsl_interp(void) {
  lcl_interp *interp = make_interp();
  lcl_value *r = NULL;
  int rc = lcl_eval_file(interp, TEST_DSL_PATH, &r);

  if (r) lcl_ref_dec(r);
  if (rc != LCL_RC_OK) {
    const char *msg = lcl_interp_error_msg(interp);
    if (g_cur_ok)
      printf("FAIL\n");
    printf("    dsl load error (%s): %s\n", TEST_DSL_PATH,
           msg ? msg : "(null)");
    g_cur_ok = 0;
  }
  return interp;
}

/* Point the DSL's module state at a fresh ui + open frame.  After this
 * the script vars $u / $t hold the ui and the in-progress tree, and
 * bare widget procs (column, button, ...) build into $t. */
static void dsl_begin(lcl_interp *interp) {
  lcl_value *r = NULL;

  eval_ok(interp,
    "let u [lk::ui_create]\n"
    "let t [lk::begin_frame $u]\n"
    "set! lk_dsl::_ui $u\n"
    "set! lk_dsl::_tree $t\n"
    "set! lk_dsl::_parent_stack ()",
    &r);
  if (r) lcl_ref_dec(r);
}

/* Eval expecting an error: asserts eval fails and that the interp
 * error message contains each given substring (NULL subs are skipped). */
static void eval_expect_err(lcl_interp *interp, const char *src,
                            const char *sub1, const char *sub2,
                            const char *sub3) {
  lcl_value *r = NULL;
  int rc = lcl_eval_string(interp, src, &r);

  if (r) lcl_ref_dec(r);
  if (rc == LCL_RC_OK) {
    if (g_cur_ok)
      printf("FAIL\n");
    printf("    expected error but eval succeeded\n    src: %.80s\n", src);
    g_cur_ok = 0;
  } else {
    const char *msg = lcl_interp_error_msg(interp);
    CHECK(msg != NULL);
    if (msg) {
      if (sub1) CHECK(strstr(msg, sub1) != NULL);
      if (sub2) CHECK(strstr(msg, sub2) != NULL);
      if (sub3) CHECK(strstr(msg, sub3) != NULL);
      if (!g_cur_ok)
        printf("    error message was: %s\n", msg);
    }
  }
}

/* Fetch the lk_tree* behind the script-side "$t". */
static lk_tree *dsl_tree(lcl_interp *interp) {
  lcl_value *r = NULL;
  lk_tree *t = NULL;

  eval_ok(interp, "$t", &r);
  if (r) {
    if (lcl_opaque_get(r, "lk_tree", (void **)&t) != LCL_OK) {
      t = NULL;
    }
    lcl_ref_dec(r);
  }
  return t;
}

/* Fetch the lk_ui* behind the script-side "$u". */
static lk_ui *dsl_ui(lcl_interp *interp) {
  lcl_value *r = NULL;
  lk_ui *ui = NULL;

  eval_ok(interp, "$u", &r);
  if (r) {
    if (lcl_opaque_get(r, "lk_ui", (void **)&ui) != LCL_OK) {
      ui = NULL;
    }
    lcl_ref_dec(r);
  }
  return ui;
}

/* Find a node index by its string id. */
static lk_ix dsl_find(lk_tree *t, const char *id) {
  if (!t) return 0;
  return lk_tree_find_by_id(t, lk_intern_cid(t->intern, id));
}

/* Read a string-valued prop directly from the props arena (there is no
 * lk_node_text-style getter for UIP_TOOLTIP).  Returns NULL if absent. */
static const char *dsl_prop_str(const lk_tree *t, lk_ix n, lk_prop_key key) {
  const lk_node *nd;
  lk_u16 i;

  if (!t || n == 0) return NULL;
  nd = &t->nodes[n];
  for (i = 0; i < nd->props_len; i++) {
    const lk_prop *p = &t->props[nd->props_off + i];
    if (p->key == (lk_u16)key && p->value.tag == UIV_STR) {
      return lk_intern_cstr(t->intern, p->value.as.str_id);
    }
  }
  return NULL;
}

static void test_dsl_widget_kinds(void) {
  static const struct {
    const char *id;
    int kind;
  } exp[] = {
    {"k_col", UIK_COLUMN},   {"k_row", UIK_ROW},
    {"k_lbl", UIK_LABEL},    {"k_btn", UIK_BUTTON},
    {"k_ti", UIK_TEXT_INPUT},{"k_sp", UIK_SPACER},
    {"k_sc", UIK_SCROLL},    {"k_dd", UIK_DROPDOWN},
    {"k_opt", UIK_OPTION},   {"k_sh", UIK_SPLIT_H},
    {"k_sv", UIK_SPLIT_V}
  };
  lcl_interp *interp;
  lcl_value *r = NULL;
  lk_tree *t;
  size_t i;

  BEGIN_TEST("dsl: each widget proc creates the right kind");
  interp = make_dsl_interp();
  dsl_begin(interp);

  eval_ok(interp,
    "column k_col\n"
    "row k_row\n"
    "label k_lbl\n"
    "button k_btn\n"
    "text_input k_ti\n"
    "spacer k_sp\n"
    "scroll k_sc\n"
    "dropdown k_dd\n"
    "option k_opt\n"
    "split_h k_sh\n"
    "split_v k_sv",
    &r);
  if (r) lcl_ref_dec(r);

  t = dsl_tree(interp);
  CHECK(t != NULL);
  if (t) {
    for (i = 0; i < sizeof(exp) / sizeof(exp[0]); i++) {
      lk_ix n = dsl_find(t, exp[i].id);
      CHECK(n != 0);
      CHECK(lk_node_kind_get(t, n) == (lk_u16)exp[i].kind);
    }
  }

  lcl_interp_free(interp);
  END_TEST();
}

static void test_dsl_props_text_dims(void) {
  lcl_interp *interp;
  lcl_value *r = NULL;
  lk_tree *t;

  BEGIN_TEST("dsl: text/w/h props land as props");
  interp = make_dsl_interp();
  dsl_begin(interp);

  eval_ok(interp, "label f_lab #{text \"Hello\" w 120 h 40}", &r);
  if (r) lcl_ref_dec(r);

  t = dsl_tree(interp);
  CHECK(t != NULL);
  if (t) {
    lk_ix n = dsl_find(t, "f_lab");
    CHECK(n != 0);
    CHECK(strcmp(lk_node_text_cstr(t, n), "Hello") == 0);
    CHECK(lk_node_prop_i32(t, n, UIP_W, -1) == 120);
    CHECK(lk_node_prop_i32(t, n, UIP_H, -1) == 40);
  }

  lcl_interp_free(interp);
  END_TEST();
}

static void test_dsl_props_layout(void) {
  lcl_interp *interp;
  lcl_value *r = NULL;
  lk_tree *t;

  BEGIN_TEST("dsl: padding/gap/align/justify props");
  interp = make_dsl_interp();
  dsl_begin(interp);

  eval_ok(interp, "column f_col #{padding 8 gap 4 align center justify end}",
          &r);
  if (r) lcl_ref_dec(r);

  t = dsl_tree(interp);
  CHECK(t != NULL);
  if (t) {
    lk_ix n = dsl_find(t, "f_col");
    CHECK(n != 0);
    CHECK(lk_node_prop_i32(t, n, UIP_PADDING, -1) == 8);
    CHECK(lk_node_prop_i32(t, n, UIP_GAP, -1) == 4);
    CHECK(lk_node_prop_i32(t, n, UIP_ALIGN, -1) == (lk_i32)LK_ALIGN_CENTER);
    CHECK(lk_node_prop_i32(t, n, UIP_JUSTIFY, -1) == (lk_i32)LK_ALIGN_END);
  }

  lcl_interp_free(interp);
  END_TEST();
}

static void test_dsl_props_bools(void) {
  lcl_interp *interp;
  lcl_value *r = NULL;
  lk_tree *t;

  BEGIN_TEST("dsl: focusable/disabled/hidden props");
  interp = make_dsl_interp();
  dsl_begin(interp);

  eval_ok(interp,
    "button f_btn #{focusable 1 disabled 1}\n"
    "column f_hid #{hidden 1}\n"
    "button f_off #{focusable 0}",
    &r);
  if (r) lcl_ref_dec(r);

  t = dsl_tree(interp);
  CHECK(t != NULL);
  if (t) {
    lk_ix b = dsl_find(t, "f_btn");
    lk_ix h = dsl_find(t, "f_hid");
    lk_ix o = dsl_find(t, "f_off");
    CHECK(b != 0 && h != 0 && o != 0);
    CHECK(lk_node_prop_bool(t, b, UIP_FOCUSABLE) == 1);
    CHECK(lk_node_prop_bool(t, b, UIP_DISABLED) == 1);
    CHECK(lk_node_prop_bool(t, h, UIP_HIDDEN) == 1);
    CHECK(lk_node_prop_bool(t, o, UIP_FOCUSABLE) == 0);
  }

  lcl_interp_free(interp);
  END_TEST();
}

static void test_dsl_props_tooltip_split_ratio(void) {
  lcl_interp *interp;
  lcl_value *r = NULL;
  lk_tree *t;

  BEGIN_TEST("dsl: tooltip/split_ratio props");
  interp = make_dsl_interp();
  dsl_begin(interp);

  eval_ok(interp,
    "button f_tip #{tooltip \"Saves the file\"}\n"
    "split_h f_spl #{split_ratio 300}",
    &r);
  if (r) lcl_ref_dec(r);

  t = dsl_tree(interp);
  CHECK(t != NULL);
  if (t) {
    lk_ix b = dsl_find(t, "f_tip");
    lk_ix s = dsl_find(t, "f_spl");
    const char *tip;
    CHECK(b != 0 && s != 0);
    tip = dsl_prop_str(t, b, UIP_TOOLTIP);
    CHECK(tip != NULL && strcmp(tip, "Saves the file") == 0);
    CHECK(lk_node_prop_i32(t, s, UIP_SPLIT_RATIO, -1) == 300);
  }

  lcl_interp_free(interp);
  END_TEST();
}

static void test_state_internal_keys_blocked(void) {
  lcl_interp *interp;
  lcl_value *r = NULL;

  BEGIN_TEST("state procs reject internal widget keys (< LKS_USER)");
  interp = make_interp();

  eval_ok(interp,
    "let ui [lk::ui_create]\n"
    "let t [lk::begin_frame $ui]\n"
    "let w [lk::node $t \"main\" \"window\"]\n"
    "lk::set_root $t $w\n"
    "lk::end_frame $ui",
    &r);
  if (r) lcl_ref_dec(r);
  r = NULL;

  /* Scripts never poke widget state: builtin keys error */
  eval_expect_err(interp, "lk::state_set $ui \"main\" 6 1",
                  "lk::state_set", "internal widget state", NULL);
  eval_expect_err(interp, "lk::state_get $ui \"main\" 6",
                  "lk::state_get", "internal widget state", NULL);

  /* App-owned keys (>= LKS_USER = 256) still round-trip */
  eval_ok(interp, "lk::state_set $ui \"main\" 300 7", &r);
  if (r) lcl_ref_dec(r);
  r = NULL;
  eval_ok(interp, "lk::state_get $ui \"main\" 300", &r);
  if (r) {
    long v;
    CHECK(lcl_value_to_int(r, &v) == LCL_OK);
    CHECK(v == 7);
    lcl_ref_dec(r);
  }

  lcl_interp_free(interp);
  END_TEST();
}

static void test_value_prop_binding(void) {
  lcl_interp *interp;
  lcl_value *r = NULL;

  BEGIN_TEST("value prop settable via lk::prop (string coercion)");
  interp = make_interp();

  eval_ok(interp,
    "let ui [lk::ui_create]\n"
    "let t [lk::begin_frame $ui]\n"
    "let w [lk::node $t \"w\" \"window\"]\n"
    "let dd [lk::node $t \"dd\" \"dropdown\"]\n"
    "lk::prop $t $dd \"value\" \"Banana\"\n"
    "lk::set_root $t $w\n"
    "lk::append_child $t $w $dd\n"
    "lk::end_frame $ui",
    &r);
  if (r) lcl_ref_dec(r);

  {
    lk_tree *t = dsl_tree(interp);
    CHECK(t != NULL);
    if (t) {
      lk_ix n = dsl_find(t, "dd");
      const char *v;
      CHECK(n != 0);
      v = dsl_prop_str(t, n, UIP_VALUE);
      CHECK(v != NULL && strcmp(v, "Banana") == 0);
    }
  }

  lcl_interp_free(interp);
  END_TEST();
}

static void test_dsl_prop_value(void) {
  lcl_interp *interp;
  lcl_value *r = NULL;
  lk_tree *t;

  BEGIN_TEST("dsl: value prop on dropdown");
  interp = make_dsl_interp();
  dsl_begin(interp);

  eval_ok(interp,
    "dropdown v_dd #{value \"Banana\"} {\n"
    "    option v_o1 #{text \"Apple\"}\n"
    "    option v_o2 #{text \"Banana\"}\n"
    "}",
    &r);
  if (r) lcl_ref_dec(r);

  t = dsl_tree(interp);
  CHECK(t != NULL);
  if (t) {
    lk_ix dd = dsl_find(t, "v_dd");
    const char *v;
    CHECK(dd != 0);
    v = dsl_prop_str(t, dd, UIP_VALUE);
    CHECK(v != NULL && strcmp(v, "Banana") == 0);
  }

  lcl_interp_free(interp);
  END_TEST();
}

static void test_dsl_prop_grow(void) {
  lcl_interp *interp;
  lcl_value *r = NULL;
  lk_tree *t;

  BEGIN_TEST("dsl: grow prop lands as UIP_GROW");
  interp = make_dsl_interp();
  dsl_begin(interp);

  eval_ok(interp,
    "spacer g_sp #{grow 2}\n"
    "editor g_ed_zero #{grow 0}",
    &r);
  if (r) lcl_ref_dec(r);

  t = dsl_tree(interp);
  CHECK(t != NULL);
  if (t) {
    lk_ix sp = dsl_find(t, "g_sp");
    lk_ix ez = dsl_find(t, "g_ed_zero");
    CHECK(sp != 0 && ez != 0);
    CHECK(lk_node_prop_i32(t, sp, UIP_GROW, -1) == 2);
    /* grow 0 is present-and-zero, not absent */
    CHECK(lk_node_has_prop(t, ez, UIP_GROW) == 1);
    CHECK(lk_node_prop_i32(t, ez, UIP_GROW, -1) == 0);
  }

  /* the unknown-prop known-keys list now advertises grow */
  eval_expect_err(interp, "label g_bad #{bogus 1}", "unknown prop 'bogus'",
                  "grow", NULL);

  lcl_interp_free(interp);
  END_TEST();
}

static void test_prop_grow_negative_errors(void) {
  lcl_interp *interp;
  lcl_value *r = NULL;

  BEGIN_TEST("grow: negative values hard-error in bindings");
  interp = make_dsl_interp();
  dsl_begin(interp);

  eval_ok(interp, "let gn [lk::node $t \"g_neg\" \"spacer\"]", &r);
  if (r) lcl_ref_dec(r);

  /* raw binding: negative and non-integer both error, naming the
   * constraint */
  eval_expect_err(interp, "lk::prop $t $gn \"grow\" -1", "grow", ">= 0",
                  NULL);
  eval_expect_err(interp, "lk::prop $t $gn \"grow\" \"lots\"", "grow",
                  "integer", NULL);

  /* DSL negative grow reaches the same binding error */
  eval_expect_err(interp, "spacer g_neg2 #{grow -3}", "grow", ">= 0", NULL);

  lcl_interp_free(interp);
  END_TEST();
}

static void test_dsl_tag(void) {
  lcl_interp *interp;
  lcl_value *r = NULL;
  lk_tree *t;

  BEGIN_TEST("dsl: tag prop applies a tag");
  interp = make_dsl_interp();
  dsl_begin(interp);

  eval_ok(interp, "button g_tag #{tag primary}", &r);
  if (r) lcl_ref_dec(r);

  t = dsl_tree(interp);
  CHECK(t != NULL);
  if (t) {
    lk_ix n = dsl_find(t, "g_tag");
    CHECK(n != 0);
    CHECK(lk_tree_has_tag(t, n, lk_intern_cid(t->intern, "primary")) == 1);
    CHECK(lk_tree_has_tag(t, n, lk_intern_cid(t->intern, "danger")) == 0);
  }

  lcl_interp_free(interp);
  END_TEST();
}

static void test_dsl_present_scalar(void) {
  lcl_interp *interp;
  lcl_value *r = NULL;
  lk_tree *t;

  BEGIN_TEST("dsl: present (ptype value) attaches presentation");
  interp = make_dsl_interp();
  dsl_begin(interp);

  eval_ok(interp, "button g_p1 #{present (item 42)}", &r);
  if (r) lcl_ref_dec(r);

  t = dsl_tree(interp);
  CHECK(t != NULL);
  if (t) {
    lk_ix n = dsl_find(t, "g_p1");
    const lk_presentation *pres;
    CHECK(n != 0);
    pres = lk_tree_get_presentation(t, n);
    CHECK(pres != NULL);
    if (pres) {
      CHECK(pres->ptype == lk_intern_cid(t->intern, "item"));
      CHECK(pres->pvalue_count == 1);
      CHECK(pres->pvalues[0].tag == UIV_I32);
      CHECK(pres->pvalues[0].as.i == 42);
    }
  }

  lcl_interp_free(interp);
  END_TEST();
}

static void test_dsl_present_multiarg(void) {
  lcl_interp *interp;
  lcl_value *r = NULL;
  lk_tree *t;

  BEGIN_TEST("dsl: present list shape (action (remove_row 3))");
  interp = make_dsl_interp();
  dsl_begin(interp);

  eval_ok(interp, "button g_p2 #{present (action (remove_row 3))}", &r);
  if (r) lcl_ref_dec(r);

  t = dsl_tree(interp);
  CHECK(t != NULL);
  if (t) {
    lk_ix n = dsl_find(t, "g_p2");
    const lk_presentation *pres;
    CHECK(n != 0);
    pres = lk_tree_get_presentation(t, n);
    CHECK(pres != NULL);
    if (pres) {
      CHECK(pres->ptype == lk_intern_cid(t->intern, "action"));
      CHECK(pres->pvalue_count == 2);
      CHECK(pres->pvalues[0].tag == UIV_STR);
      CHECK(pres->pvalues[0].as.str_id ==
            lk_intern_cid(t->intern, "remove_row"));
      CHECK(pres->pvalues[1].tag == UIV_I32);
      CHECK(pres->pvalues[1].as.i == 3);
    }
  }

  lcl_interp_free(interp);
  END_TEST();
}

static void test_dsl_nesting(void) {
  lcl_interp *interp;
  lcl_value *r = NULL;
  lk_tree *t;

  BEGIN_TEST("dsl: body blocks nest 3 levels, order kept");
  interp = make_dsl_interp();
  dsl_begin(interp);

  eval_ok(interp,
    "column outer #{gap 2} {\n"
    "    row mid {\n"
    "        label leaf1 #{text a}\n"
    "        label leaf2 #{text b}\n"
    "    }\n"
    "}",
    &r);
  if (r) lcl_ref_dec(r);

  t = dsl_tree(interp);
  CHECK(t != NULL);
  if (t) {
    lk_ix outer = dsl_find(t, "outer");
    lk_ix mid = dsl_find(t, "mid");
    lk_ix leaf1 = dsl_find(t, "leaf1");
    lk_ix leaf2 = dsl_find(t, "leaf2");
    CHECK(outer != 0 && mid != 0 && leaf1 != 0 && leaf2 != 0);
    CHECK(lk_node_parent(t, outer) == 0);
    CHECK(lk_node_parent(t, mid) == outer);
    CHECK(lk_node_parent(t, leaf1) == mid);
    CHECK(lk_node_parent(t, leaf2) == mid);
    CHECK(lk_node_first_child(t, outer) == mid);
    CHECK(lk_node_first_child(t, mid) == leaf1);
    CHECK(lk_node_next_sibling(t, leaf1) == leaf2);
    CHECK(lk_node_next_sibling(t, leaf2) == 0);
  }

  lcl_interp_free(interp);
  END_TEST();
}

static void test_dsl_unknown_prop_errors(void) {
  lcl_interp *interp;

  /* Flipped WART (DSL v2): unknown prop keys are hard errors carrying
   * the widget id and the known-key list — the v1 whitelist silently
   * swallowed them (how -tooltip/-hidden were lost pre-whitelist). */
  BEGIN_TEST("dsl: unknown prop key is a hard error");
  interp = make_dsl_interp();
  dsl_begin(interp);

  eval_expect_err(interp, "label w_unk #{bogus 42 text ok}",
                  "w_unk", "unknown prop 'bogus'", "(known:");

  lcl_interp_free(interp);
  END_TEST();
}

static void test_dsl_bad_trailing_arg_errors(void) {
  lcl_interp *interp;

  /* Flipped WART (DSL v2): the trailing-flag-becomes-boolean shape is
   * gone with flag parsing.  Malformed trailing args now error cleanly:
   * a lone numeric arg (not a dict, not a plausible body), a stale
   * v1 `-flag`, a non-dict where props are expected, a second dict
   * where the body should be, and >2 trailing args.  A lone non-dict,
   * non-numeric, non-dash arg is treated as the body block (blocks are
   * plain strings in Lcl — not distinguishable by value shape). */
  BEGIN_TEST("dsl: malformed trailing args error cleanly");
  interp = make_dsl_interp();
  dsl_begin(interp);

  eval_expect_err(interp, "button w_tr1 42",
                  "w_tr1", "expected a props dict", NULL);
  eval_expect_err(interp, "label w_tr2 -w",
                  "w_tr2", "'-flag' syntax was removed", NULL);
  eval_expect_err(interp, "button w_tr3 42 { label x }",
                  "w_tr3", "expected a props dict", NULL);
  eval_expect_err(interp, "row w_tr4 #{gap 2} #{gap 3}",
                  "w_tr4", "body must be a block", NULL);
  eval_expect_err(interp, "row w_tr5 #{gap 2} { label y } extra",
                  "w_tr5", "too many arguments", NULL);

  lcl_interp_free(interp);
  END_TEST();
}

static void test_dsl_theme_rules(void) {
  lcl_interp *interp;
  lcl_value *r = NULL;
  lk_ui *ui;

  BEGIN_TEST("dsl: theme + rule selector dicts resolve");
  interp = make_dsl_interp();
  dsl_begin(interp);

  /* Three rule shapes: kind+tag, wildcard kind, kind+state.  The
   * hovered rule is added last but must NOT apply (no node is in the
   * hovered state at resolve time). */
  eval_ok(interp,
    "theme {\n"
    "    rule button #{tag primary} #{bg (10 20 30)}\n"
    "    rule * #{fg (200 201 202)}\n"
    "    rule button #{state hovered} #{bg (1 2 3)}\n"
    "}\n"
    "let w [lk::node $t rw window]\n"
    "lk::set_root $t $w\n"
    "let b [button pb #{tag primary}]\n"
    "lk::append_child $t $w $b\n"
    "lk::end_frame $u",
    &r);
  if (r) lcl_ref_dec(r);

  ui = dsl_ui(interp);
  CHECK(ui != NULL);
  if (ui) {
    const lk_tree *cur = lk_ui_tree(ui);
    const lk_style *styles;
    lk_ix n;

    lk_ui_resolve_styles(ui);
    styles = lk_ui_styles(ui);
    CHECK(styles != NULL);
    n = lk_tree_find_by_id(cur, lk_intern_cid(ui->intern, "pb"));
    CHECK(n != 0);
    if (styles && n != 0) {
      CHECK(styles[n].bg.r == 10 && styles[n].bg.g == 20 &&
            styles[n].bg.b == 30);
      CHECK(styles[n].fg.r == 200 && styles[n].fg.g == 201 &&
            styles[n].fg.b == 202);
    }
  }

  lcl_interp_free(interp);
  END_TEST();
}

static void test_dsl_translators_keybindings(void) {
  lcl_interp *interp;
  lcl_value *r = NULL;
  lk_ui *ui;

  BEGIN_TEST("dsl: translator/keybinding 3- and 4-arg forms");
  interp = make_dsl_interp();
  dsl_begin(interp);

  eval_ok(interp,
    "translator pointer_down item Select\n"
    "translator pointer_down action button DoIt\n"
    "keybinding s ctrl Save\n"
    "keybinding f5 \"\" doc Refresh",
    &r);
  if (r) lcl_ref_dec(r);

  ui = dsl_ui(interp);
  CHECK(ui != NULL);
  if (ui) {
    CHECK(ui->translator_count == 4);
    if (ui->translator_count == 4) {
      const lk_translator *tr = ui->translators;
      /* 3-arg translator: event + ptype, kind wildcard */
      CHECK(tr[0].event_type == (lk_u8)LK_EVENT_POINTER_DOWN);
      CHECK(tr[0].ptype == lk_intern_cid(ui->intern, "item"));
      CHECK(tr[0].node_kind == 0);
      CHECK(tr[0].keycode == 0);
      CHECK(tr[0].command_name == lk_intern_cid(ui->intern, "Select"));
      /* 4-arg translator: kind filter set */
      CHECK(tr[1].ptype == lk_intern_cid(ui->intern, "action"));
      CHECK(tr[1].node_kind == (lk_u16)UIK_BUTTON);
      CHECK(tr[1].command_name == lk_intern_cid(ui->intern, "DoIt"));
      /* 3-arg keybinding: global, no ptype */
      CHECK(tr[2].event_type == (lk_u8)LK_EVENT_KEY_DOWN);
      CHECK(tr[2].ptype == 0);
      CHECK(tr[2].keycode == (lk_u16)LKK_S);
      CHECK(tr[2].mods == LK_MOD_CTRL);
      CHECK(tr[2].command_name == lk_intern_cid(ui->intern, "Save"));
      /* 4-arg keybinding: ptype filter set */
      CHECK(tr[3].event_type == (lk_u8)LK_EVENT_KEY_DOWN);
      CHECK(tr[3].ptype == lk_intern_cid(ui->intern, "doc"));
      CHECK(tr[3].keycode == (lk_u16)LKK_F5);
      CHECK(tr[3].mods == 0);
      CHECK(tr[3].command_name == lk_intern_cid(ui->intern, "Refresh"));
    }
  }

  lcl_interp_free(interp);
  END_TEST();
}

static void test_dsl_on_dispatch(void) {
  lcl_interp *interp;
  lcl_value *r = NULL;

  BEGIN_TEST("dsl: on + _dispatch_command invokes handler");
  interp = make_dsl_interp();
  dsl_begin(interp);

  /* Handler writes into lk state so the effect is observable from
   * script.  Dispatching an unregistered command name is a no-op (no
   * error, state untouched). */
  eval_ok(interp,
    "on Save [lambda {cmd} { lk::state_set $u sink 300 [get $cmd n] }]\n"
    "lk_dsl::_dispatch_command #{name Save n 7}\n"
    "lk_dsl::_dispatch_command #{name Unknown n 9}\n"
    "lk::state_get $u sink 300",
    &r);
  if (r) {
    long v = -1;
    lcl_value_to_int(r, &v);
    CHECK(v == 7);
    lcl_ref_dec(r);
  }

  lcl_interp_free(interp);
  END_TEST();
}

static void test_dsl_frame_view_rebuild(void) {
  lcl_interp *interp;
  lcl_value *r = NULL;
  lk_ui *ui;

  BEGIN_TEST("dsl: _frame builds implicit root; rebuilds");
  interp = make_dsl_interp();

  /* Frame 1: view body + _frame on a fresh begin_frame tree. */
  eval_ok(interp,
    "let u [lk::ui_create]\n"
    "set! lk_dsl::_ui $u\n"
    "view {\n"
    "    column main #{padding 4} {\n"
    "        label greet #{text Hi}\n"
    "        button ok #{text OK}\n"
    "    }\n"
    "}\n"
    "let t [lk::begin_frame $u]\n"
    "lk_dsl::_frame $t\n"
    "let cs [lk::end_frame $u]\n"
    "len $cs",
    &r);
  if (r) {
    long v = -1;
    lcl_value_to_int(r, &v);
    CHECK(v == 4); /* root + main + greet + ok all ADDED */
    lcl_ref_dec(r);
  }
  r = NULL;

  ui = dsl_ui(interp);
  CHECK(ui != NULL);
  if (ui) {
    const lk_tree *cur = lk_ui_tree(ui);
    lk_ix root = cur->root;
    lk_ix main_n = lk_tree_find_by_id(cur, lk_intern_cid(ui->intern, "main"));
    lk_ix greet = lk_tree_find_by_id(cur, lk_intern_cid(ui->intern, "greet"));
    lk_ix ok_n = lk_tree_find_by_id(cur, lk_intern_cid(ui->intern, "ok"));

    CHECK(root != 0);
    CHECK(lk_node_id_get(cur, root) == lk_intern_cid(ui->intern, "root"));
    CHECK(lk_node_kind_get(cur, root) == (lk_u16)UIK_WINDOW);
    CHECK(main_n != 0 && greet != 0 && ok_n != 0);
    CHECK(lk_node_parent(cur, main_n) == root);
    CHECK(lk_node_kind_get(cur, main_n) == (lk_u16)UIK_COLUMN);
    CHECK(lk_node_prop_i32(cur, main_n, UIP_PADDING, -1) == 4);
    CHECK(lk_node_first_child(cur, main_n) == greet);
    CHECK(lk_node_next_sibling(cur, greet) == ok_n);
  }

  /* Frame 2: identical rebuild diffs to zero changes. */
  eval_ok(interp,
    "let t2 [lk::begin_frame $u]\n"
    "lk_dsl::_frame $t2\n"
    "let cs2 [lk::end_frame $u]\n"
    "len $cs2",
    &r);
  if (r) {
    long v = -1;
    lcl_value_to_int(r, &v);
    CHECK(v == 0);
    lcl_ref_dec(r);
  }

  if (ui) {
    const lk_tree *cur = lk_ui_tree(ui);
    CHECK(lk_tree_find_by_id(cur, lk_intern_cid(ui->intern, "greet")) != 0);
  }

  lcl_interp_free(interp);
  END_TEST();
}

static void test_dsl_props_dict_variable(void) {
  lcl_interp *interp;
  lcl_value *r = NULL;
  lk_tree *t;

  /* DSL v2 exit criterion: a props dict built in a variable and
   * shared across two widgets works. */
  BEGIN_TEST("dsl: props dict in a variable, shared");
  interp = make_dsl_interp();
  dsl_begin(interp);

  eval_ok(interp,
    "let compact #{padding 2 gap 2}\n"
    "column v1 $compact\n"
    "column v2 $compact",
    &r);
  if (r) lcl_ref_dec(r);

  t = dsl_tree(interp);
  CHECK(t != NULL);
  if (t) {
    lk_ix a = dsl_find(t, "v1");
    lk_ix b = dsl_find(t, "v2");
    CHECK(a != 0 && b != 0);
    CHECK(lk_node_prop_i32(t, a, UIP_PADDING, -1) == 2);
    CHECK(lk_node_prop_i32(t, a, UIP_GAP, -1) == 2);
    CHECK(lk_node_prop_i32(t, b, UIP_PADDING, -1) == 2);
    CHECK(lk_node_prop_i32(t, b, UIP_GAP, -1) == 2);
  }

  lcl_interp_free(interp);
  END_TEST();
}

static void test_dsl_props_dict_merge(void) {
  lcl_interp *interp;
  lcl_value *r = NULL;
  lk_tree *t;

  /* Composition: Dict::merge of a base props dict with an override
   * (later dict wins on key collisions). */
  BEGIN_TEST("dsl: Dict::merge base props + override");
  interp = make_dsl_interp();
  dsl_begin(interp);

  eval_ok(interp,
    "let base #{text Base w 100}\n"
    "button v3 [Dict::merge $base #{w 150 tooltip Hi}]",
    &r);
  if (r) lcl_ref_dec(r);

  t = dsl_tree(interp);
  CHECK(t != NULL);
  if (t) {
    lk_ix n = dsl_find(t, "v3");
    const char *tip;
    CHECK(n != 0);
    CHECK(strcmp(lk_node_text_cstr(t, n), "Base") == 0);
    CHECK(lk_node_prop_i32(t, n, UIP_W, -1) == 150);
    tip = dsl_prop_str(t, n, UIP_TOOLTIP);
    CHECK(tip != NULL && strcmp(tip, "Hi") == 0);
  }

  lcl_interp_free(interp);
  END_TEST();
}

/* ============================================================================
 * Editor track (stage D): documents, histories, editors, annot stores.
 * ============================================================================
 */

/* Eval an expression and check its int result. */
static void check_int(lcl_interp *interp, const char *src, long expect) {
  lcl_value *r = NULL;

  eval_ok(interp, src, &r);
  if (r) {
    long v = -99999;
    CHECK(lcl_value_to_int(r, &v) == LCL_OK);
    if (v != expect) {
      if (g_cur_ok)
        printf("FAIL\n");
      printf("    %s => %ld, expected %ld\n", src, v, expect);
      g_cur_ok = 0;
    }
    lcl_ref_dec(r);
  }
}

/* Eval an expression and check its string result. */
static void check_str(lcl_interp *interp, const char *src, const char *expect) {
  lcl_value *r = NULL;

  eval_ok(interp, src, &r);
  if (r) {
    const char *s = lcl_value_to_string(r);
    if (!s || strcmp(s, expect) != 0) {
      if (g_cur_ok)
        printf("FAIL\n");
      printf("    %s => \"%s\", expected \"%s\"\n", src, s ? s : "(null)",
             expect);
      g_cur_ok = 0;
    }
    lcl_ref_dec(r);
  }
}

static void test_doc_new_read(void) {
  lcl_interp *interp;
  lcl_value *r = NULL;

  BEGIN_TEST("doc: new/text/len/line_count");
  interp = make_interp();

  eval_ok(interp, "let d [lk::doc_new \"hello\nworld\"]", &r);
  if (r) lcl_ref_dec(r);

  check_str(interp, "lk::doc_text $d", "hello\nworld");
  check_int(interp, "lk::doc_len $d", 11);
  check_int(interp, "lk::doc_line_count $d", 2);

  /* The empty document is one empty line. */
  r = NULL;
  eval_ok(interp, "let d0 [lk::doc_new]", &r);
  if (r) lcl_ref_dec(r);
  check_int(interp, "lk::doc_len $d0", 0);
  check_int(interp, "lk::doc_line_count $d0", 1);
  check_str(interp, "lk::doc_text $d0", "");

  lcl_interp_free(interp);
  END_TEST();
}

static void test_doc_revision_string(void) {
  lcl_interp *interp;
  lcl_value *r = NULL;

  BEGIN_TEST("doc: revision is a stable \"hi:lo\" token");
  interp = make_interp();

  eval_ok(interp, "let d [lk::doc_new \"abc\"]", &r);
  if (r) lcl_ref_dec(r);

  /* Reading does not advance it; comparing from script works. */
  check_int(interp,
            "let r1 [lk::doc_revision $d]\n"
            "let r2 [lk::doc_revision $d]\n"
            "== $r1 $r2",
            1);

  /* One committed edit advances it exactly once. */
  check_int(interp,
            "lk::doc_insert $d 0 \"x\"\n"
            "let r3 [lk::doc_revision $d]\n"
            "== $r1 $r3",
            0);

  lcl_interp_free(interp);
  END_TEST();
}

static void test_doc_insert_delete(void) {
  lcl_interp *interp;
  lcl_value *r = NULL;

  BEGIN_TEST("doc: insert/delete edit the contents");
  interp = make_interp();

  eval_ok(interp, "let d [lk::doc_new \"hello world\"]", &r);
  if (r) lcl_ref_dec(r);

  check_str(interp,
            "lk::doc_insert $d 5 \",\"\n"
            "lk::doc_text $d",
            "hello, world");
  check_str(interp,
            "lk::doc_delete $d 0 7\n"
            "lk::doc_text $d",
            "world");

  lcl_interp_free(interp);
  END_TEST();
}

static void test_doc_mutation_errors(void) {
  lcl_interp *interp;
  lcl_value *r = NULL;

  BEGIN_TEST("doc: rejected mutations and bad handles error");
  interp = make_interp();

  eval_ok(interp,
          "let d [lk::doc_new \"abc\"]\n"
          "let ui [lk::ui_create]",
          &r);
  if (r) lcl_ref_dec(r);

  /* insert past the end / delete at the end are rejected by the C
   * contract; the binding turns the rejection into a hard error. */
  eval_expect_err(interp, "lk::doc_insert $d 99 \"x\"",
                  "lk::doc_insert", "rejected", NULL);
  eval_expect_err(interp, "lk::doc_delete $d 3 1",
                  "lk::doc_delete", "rejected", NULL);
  eval_expect_err(interp, "lk::doc_insert $d -1 \"x\"",
                  "lk::doc_insert", "non-negative", NULL);

  /* Arity and wrong opaque type. */
  eval_expect_err(interp, "lk::doc_insert $d 0", "lk::doc_insert",
                  "3 arguments", NULL);
  eval_expect_err(interp, "lk::doc_text $ui", "expected lk_document opaque",
                  NULL, NULL);
  eval_expect_err(interp, "lk::doc_text", "lk::doc_text", "1 argument", NULL);

  /* The rejected calls left the document untouched. */
  check_str(interp, "lk::doc_text $d", "abc");

  lcl_interp_free(interp);
  END_TEST();
}

static void test_doc_line_procs(void) {
  lcl_interp *interp;
  lcl_value *r = NULL;

  BEGIN_TEST("doc: pos_to_line / line_start / line_end");
  interp = make_interp();

  eval_ok(interp, "let d [lk::doc_new \"ab\ncd\nef\"]", &r);
  if (r) lcl_ref_dec(r);

  /* 0-based lines, mirroring the C API. */
  check_int(interp, "lk::doc_pos_to_line $d 0", 0);
  check_int(interp, "lk::doc_pos_to_line $d 2", 0); /* the \n itself */
  check_int(interp, "lk::doc_pos_to_line $d 3", 1);
  check_int(interp, "lk::doc_pos_to_line $d 7", 2);
  check_int(interp, "lk::doc_pos_to_line $d 99", 2); /* clamps to last */

  check_int(interp, "lk::doc_line_start $d 0", 0);
  check_int(interp, "lk::doc_line_start $d 1", 3);
  check_int(interp, "lk::doc_line_start $d 2", 6);

  /* line_end: the \n (exclusive) for inner lines, doc len for last. */
  check_int(interp, "lk::doc_line_end $d 0", 2);
  check_int(interp, "lk::doc_line_end $d 1", 5);
  check_int(interp, "lk::doc_line_end $d 2", 8);

  /* Errors: arity, wrong opaque, bad values, out-of-range lines. */
  eval_expect_err(interp, "lk::doc_pos_to_line $d", "lk::doc_pos_to_line",
                  "2 arguments", NULL);
  eval_expect_err(interp, "lk::doc_pos_to_line $d -1",
                  "non-negative integer", NULL, NULL);
  eval_expect_err(interp, "lk::doc_line_start $d 3", "line out of range",
                  NULL, NULL);
  eval_expect_err(interp, "lk::doc_line_end $d 99", "line out of range",
                  NULL, NULL);
  eval_expect_err(interp, "lk::doc_line_start $d nope",
                  "non-negative integer", NULL, NULL);
  eval_expect_err(interp, "lk::doc_line_start 5 0",
                  "expected lk_document opaque", NULL, NULL);
  eval_expect_err(interp, "lk::doc_line_end $d", "lk::doc_line_end",
                  "2 arguments", NULL);

  lcl_interp_free(interp);
  END_TEST();
}

static void test_doc_char_col(void) {
  lcl_interp *interp;
  lcl_value *r = NULL;

  BEGIN_TEST("doc: char_col counts codepoints, tab = 1");
  interp = make_interp();

  /* Line 0: "hello" (bytes 0..4, \n at 5)
   * Line 1: "na\xC3\xAFve caf\xC3\xA9" (starts at 6; 10 codepoints,
   *         12 bytes; \n at 18)
   * Line 2: "\ta\tb" (starts at 19; len = 23) */
  eval_ok(interp,
          "let d [lk::doc_new \"hello\nna\xC3\xAFve caf\xC3\xA9\n\ta\tb\"]",
          &r);
  if (r) lcl_ref_dec(r);

  check_int(interp, "lk::doc_len $d", 23);

  /* ASCII: position at line start is column 1; 1-based thereafter. */
  check_int(interp, "lk::doc_char_col $d 0", 1);
  check_int(interp, "lk::doc_char_col $d 3", 4);
  check_int(interp, "lk::doc_char_col $d 5", 6); /* end of "hello" */

  /* Multi-byte UTF-8: columns count codepoints, not bytes. */
  check_int(interp, "lk::doc_char_col $d 6", 1);  /* line start */
  check_int(interp, "lk::doc_char_col $d 8", 3);  /* before the i-uml */
  check_int(interp, "lk::doc_char_col $d 10", 4); /* after it: 3 cp */
  check_int(interp, "lk::doc_char_col $d 18", 11); /* 10 cp, 12 bytes */

  /* Tabs count as ONE character (pinned definition, editor-wrap #8). */
  check_int(interp, "lk::doc_char_col $d 19", 1);
  check_int(interp, "lk::doc_char_col $d 20", 2); /* after the tab */
  check_int(interp, "lk::doc_char_col $d 22", 4);
  check_int(interp, "lk::doc_char_col $d 23", 5); /* pos == doc len */

  /* Errors. */
  eval_expect_err(interp, "lk::doc_char_col $d 24", "pos out of range",
                  NULL, NULL);
  eval_expect_err(interp, "lk::doc_char_col $d -1", "non-negative integer",
                  NULL, NULL);
  eval_expect_err(interp, "lk::doc_char_col $d", "lk::doc_char_col",
                  "2 arguments", NULL);
  eval_expect_err(interp, "lk::doc_char_col 5 0",
                  "expected lk_document opaque", NULL, NULL);

  lcl_interp_free(interp);
  END_TEST();
}

static void test_doc_find_binding(void) {
  lcl_interp *interp;
  lcl_value *r = NULL;

  BEGIN_TEST("doc: find happy paths incl. piece seams");
  interp = make_interp();

  eval_ok(interp, "let d [lk::doc_new \"one two one\"]", &r);
  if (r) lcl_ref_dec(r);

  check_int(interp, "lk::doc_find $d \"one\"", 0);
  check_int(interp, "lk::doc_find $d \"two\"", 4);
  check_int(interp, "lk::doc_find $d \"one\" 1", 8);
  check_int(interp, "lk::doc_find $d \"one\" 8", 8);
  check_int(interp, "lk::doc_find $d \"one\" 9", -1); /* not found */
  check_int(interp, "lk::doc_find $d \"zzz\"", -1);
  check_int(interp, "lk::doc_find $d \"one\" 99", -1); /* from past end */

  /* Insert-in-middle splits the original piece; the needle spans the
   * resulting seams. */
  r = NULL;
  eval_ok(interp, "lk::doc_insert $d 4 \"XY \"", &r);
  if (r) lcl_ref_dec(r);
  check_str(interp, "lk::doc_text $d", "one XY two one");
  check_int(interp, "lk::doc_find $d \"e XY t\"", 2);
  check_int(interp, "lk::doc_find $d \"Y tw\" 3", 5);

  /* UTF-8 needle matches its exact bytes. */
  r = NULL;
  eval_ok(interp, "let d2 [lk::doc_new \"caf\xC3\xA9 bar\"]", &r);
  if (r) lcl_ref_dec(r);
  check_int(interp, "lk::doc_find $d2 \"\xC3\xA9 b\"", 3);

  /* Search-next idiom from script. */
  check_int(interp,
            "let h [lk::doc_find $d \"one\"]\n"
            "lk::doc_find $d \"one\" [+ $h 1]",
            11);

  lcl_interp_free(interp);
  END_TEST();
}

static void test_doc_find_errors(void) {
  lcl_interp *interp;
  lcl_value *r = NULL;

  BEGIN_TEST("doc: find error paths");
  interp = make_interp();

  eval_ok(interp, "let d [lk::doc_new \"abc\"]", &r);
  if (r) lcl_ref_dec(r);

  eval_expect_err(interp, "lk::doc_find $d", "lk::doc_find",
                  "2 or 3 arguments", NULL);
  eval_expect_err(interp, "lk::doc_find $d \"a\" 0 extra", "lk::doc_find",
                  "2 or 3 arguments", NULL);
  eval_expect_err(interp, "lk::doc_find $d \"\"", "needle must be non-empty",
                  NULL, NULL);
  eval_expect_err(interp, "lk::doc_find $d \"a\" -1",
                  "non-negative integer", NULL, NULL);
  eval_expect_err(interp, "lk::doc_find $d \"a\" nope",
                  "non-negative integer", NULL, NULL);
  eval_expect_err(interp, "lk::doc_find 5 \"a\"",
                  "expected lk_document opaque", NULL, NULL);

  lcl_interp_free(interp);
  END_TEST();
}

static void test_doc_transact_groups(void) {
  lcl_interp *interp;
  lcl_value *r = NULL;

  BEGIN_TEST("doc: transact groups 3 edits into one undo step");
  interp = make_interp();

  eval_ok(interp,
          "let d [lk::doc_new \"base\"]\n"
          "let h [lk::history_new $d]",
          &r);
  if (r) lcl_ref_dec(r);

  check_str(interp,
            "lk::doc_transact $d {\n"
            "    lk::doc_insert $d 0 \"A\"\n"
            "    lk::doc_insert $d 1 \"B\"\n"
            "    lk::doc_insert $d 6 \"C\"\n"
            "}\n"
            "lk::doc_text $d",
            "ABbaseC");

  /* One undo step reverts all three edits. */
  check_int(interp, "lk::history_can_undo $h", 1);
  check_int(interp, "lk::history_undo $h $d", 1);
  check_str(interp, "lk::doc_text $d", "base");
  check_int(interp, "lk::history_can_undo $h", 0);
  check_int(interp, "lk::history_can_redo $h", 1);

  lcl_interp_free(interp);
  END_TEST();
}

static void test_doc_transact_error_propagates(void) {
  lcl_interp *interp;
  lcl_value *r = NULL;

  BEGIN_TEST("doc: transact commits then propagates a body error");
  interp = make_interp();

  eval_ok(interp,
          "let d [lk::doc_new \"base\"]\n"
          "let h [lk::history_new $d]",
          &r);
  if (r) lcl_ref_dec(r);

  /* The body errors after one successful edit: the commit still runs
   * (the partial edit stays applied, as one transaction), then the
   * body's error propagates to the caller. */
  eval_expect_err(interp,
                  "lk::doc_transact $d {\n"
                  "    lk::doc_insert $d 0 \"X\"\n"
                  "    error \"boom\"\n"
                  "}",
                  "boom", NULL, NULL);

  check_str(interp, "lk::doc_text $d", "Xbase");
  check_int(interp, "lk::history_undo $h $d", 1);
  check_str(interp, "lk::doc_text $d", "base");

  lcl_interp_free(interp);
  END_TEST();
}

static void test_doc_subscribe_deltas(void) {
  lcl_interp *interp;
  lcl_value *r = NULL;

  BEGIN_TEST("doc: subscriber receives delta dicts (insert + delete)");
  interp = make_interp();

  eval_ok(interp,
          "let d [lk::doc_new \"hello\"]\n"
          "var got ()\n"
          "let sid [lk::doc_subscribe $d [lambda {deltas} {\n"
          "    set! got $deltas\n"
          "}]]",
          &r);
  if (r) lcl_ref_dec(r);

  /* Insert: one delta with the inserted bytes, nothing deleted. */
  r = NULL;
  eval_ok(interp, "lk::doc_insert $d 2 \"XY\"", &r);
  if (r) lcl_ref_dec(r);
  check_int(interp, "len $got", 1);
  check_int(interp, "get [get $got 0] start", 2);
  check_int(interp, "get [get $got 0] inserted_len", 2);
  check_str(interp, "get [get $got 0] inserted", "XY");
  check_int(interp, "get [get $got 0] deleted_len", 0);
  check_str(interp, "get [get $got 0] deleted", "");
  check_int(interp, "get [get $got 0] origin", 0);

  /* Delete: the removed bytes were copied into the delta dict. */
  r = NULL;
  eval_ok(interp, "lk::doc_delete $d 2 2", &r);
  if (r) lcl_ref_dec(r);
  check_int(interp, "len $got", 1);
  check_int(interp, "get [get $got 0] start", 2);
  check_int(interp, "get [get $got 0] deleted_len", 2);
  check_str(interp, "get [get $got 0] deleted", "XY");
  check_int(interp, "get [get $got 0] inserted_len", 0);

  /* A transaction delivers all its deltas in one notification. */
  r = NULL;
  eval_ok(interp,
          "lk::doc_transact $d {\n"
          "    lk::doc_insert $d 0 \"a\"\n"
          "    lk::doc_insert $d 1 \"b\"\n"
          "}",
          &r);
  if (r) lcl_ref_dec(r);
  check_int(interp, "len $got", 2);
  check_str(interp, "get [get $got 1] inserted", "b");

  lcl_interp_free(interp);
  END_TEST();
}

static void test_doc_unsubscribe(void) {
  lcl_interp *interp;
  lcl_value *r = NULL;

  BEGIN_TEST("doc: unsubscribe stops callbacks; unknown id errors");
  interp = make_interp();

  eval_ok(interp,
          "let d [lk::doc_new \"hello\"]\n"
          "var count 0\n"
          "let sid [lk::doc_subscribe $d [lambda {deltas} {\n"
          "    set! count [+ $count 1]\n"
          "}]]\n"
          "lk::doc_insert $d 0 \"x\"\n"
          "lk::doc_unsubscribe $d $sid\n"
          "lk::doc_insert $d 0 \"y\"",
          &r);
  if (r) lcl_ref_dec(r);

  check_int(interp, "$count", 1);
  eval_expect_err(interp, "lk::doc_unsubscribe $d 999",
                  "unknown subscription id", NULL, NULL);
  eval_expect_err(interp, "lk::doc_subscribe $d 42", "expected callable",
                  NULL, NULL);

  lcl_interp_free(interp);
  END_TEST();
}

static void test_focus_get_round_trip(void) {
  lcl_interp *interp;
  lcl_value *r = NULL;

  BEGIN_TEST("focus_get returns focused id string");
  interp = make_interp();

  eval_ok(interp,
    "let ui [lk::ui_create]\n"
    "let t [lk::begin_frame $ui]\n"
    "let w [lk::node $t \"main\" \"window\"]\n"
    "let btn [lk::node $t \"btn\" \"button\"]\n"
    "lk::prop $t $btn \"focusable\" 1\n"
    "lk::set_root $t $w\n"
    "lk::append_child $t $w $btn\n"
    "lk::end_frame $ui",
    &r);
  if (r) lcl_ref_dec(r);

  /* no focus yet -> "" */
  check_str(interp, "lk::focus_get $ui", "");

  /* set -> get round trip */
  check_str(interp,
            "lk::focus_set $ui \"btn\"\n"
            "lk::focus_get $ui",
            "btn");

  /* clear -> "" again */
  check_str(interp,
            "lk::focus_clear $ui\n"
            "lk::focus_get $ui",
            "");

  eval_expect_err(interp, "lk::focus_get", "lk::focus_get", "1 argument",
                  NULL);

  lcl_interp_free(interp);
  END_TEST();
}

static void test_time_ms_binding(void) {
  lcl_interp *interp;
  lcl_value *r = NULL;
  lk_ui *ui;

  BEGIN_TEST("time_ms reads backend-stamped frame time");
  interp = make_interp();

  eval_ok(interp, "let ui [lk::ui_create]", &r);
  if (r) lcl_ref_dec(r);

  /* no backend stamped anything yet -> 0 */
  check_int(interp, "lk::time_ms $ui", 0);

  /* stamp from C (standing in for the SDL loop) and re-read */
  ui = fetch_ui(interp);
  CHECK(ui != NULL);
  if (ui) {
    lk_ui_set_time_ms(ui, 4321u);
  }
  check_int(interp, "lk::time_ms $ui", 4321);

  eval_expect_err(interp, "lk::time_ms", "lk::time_ms", "1 argument", NULL);

  lcl_interp_free(interp);
  END_TEST();
}

static void test_history_undo_redo(void) {
  lcl_interp *interp;
  lcl_value *r = NULL;

  BEGIN_TEST("history: undo/redo round-trip, empty-stack returns 0");
  interp = make_interp();

  eval_ok(interp,
          "let d [lk::doc_new \"one\"]\n"
          "let h [lk::history_new $d]",
          &r);
  if (r) lcl_ref_dec(r);

  check_int(interp, "lk::history_can_undo $h", 0);
  check_int(interp, "lk::history_undo $h $d", 0);

  check_str(interp,
            "lk::doc_insert $d 3 \" two\"\n"
            "lk::doc_text $d",
            "one two");
  check_int(interp, "lk::history_undo $h $d", 1);
  check_str(interp, "lk::doc_text $d", "one");
  check_int(interp, "lk::history_can_redo $h", 1);
  check_int(interp, "lk::history_redo $h $d", 1);
  check_str(interp, "lk::doc_text $d", "one two");
  check_int(interp, "lk::history_can_redo $h", 0);

  lcl_interp_free(interp);
  END_TEST();
}

static void test_history_errors(void) {
  lcl_interp *interp;
  lcl_value *r = NULL;

  BEGIN_TEST("history: arity and wrong-opaque errors");
  interp = make_interp();

  eval_ok(interp,
          "let d [lk::doc_new \"x\"]\n"
          "let h [lk::history_new]",
          &r);
  if (r) lcl_ref_dec(r);

  eval_expect_err(interp, "lk::history_undo $h", "lk::history_undo",
                  "2 arguments", NULL);
  eval_expect_err(interp, "lk::history_undo $d $d",
                  "expected lk_edit_history opaque", NULL, NULL);
  eval_expect_err(interp, "lk::history_can_undo $d",
                  "expected lk_edit_history opaque", NULL, NULL);
  eval_expect_err(interp, "lk::history_new $h", "expected lk_document opaque",
                  NULL, NULL);

  lcl_interp_free(interp);
  END_TEST();
}

static void test_history_savepoint(void) {
  lcl_interp *interp;
  lcl_value *r = NULL;

  BEGIN_TEST("history: savepoint mark/edit/undo round trip");
  interp = make_interp();

  eval_ok(interp,
          "let d [lk::doc_new \"one\"]\n"
          "let h [lk::history_new $d]",
          &r);
  if (r) lcl_ref_dec(r);

  /* fresh history: no savepoint */
  check_int(interp, "lk::history_at_saved $h", 0);

  check_str(interp, "lk::history_mark_saved $h", "");
  check_int(interp, "lk::history_at_saved $h", 1);

  check_int(interp,
            "lk::doc_insert $d 3 \" two\"\n"
            "lk::history_at_saved $h",
            0);

  check_int(interp,
            "lk::history_undo $h $d\n"
            "lk::history_at_saved $h",
            1);

  eval_expect_err(interp, "lk::history_mark_saved",
                  "lk::history_mark_saved", "1 argument", NULL);
  eval_expect_err(interp, "lk::history_at_saved $d",
                  "expected lk_edit_history opaque", NULL, NULL);

  lcl_interp_free(interp);
  END_TEST();
}

static void test_editor_new_basic(void) {
  lcl_interp *interp;
  lcl_value *r = NULL;

  BEGIN_TEST("editor: new returns opaque, cursor starts at 0");
  interp = make_interp();

  eval_ok(interp,
          "let ui [lk::ui_create]\n"
          "let d [lk::doc_new \"hello\"]\n"
          "let h [lk::history_new]\n"
          "let e [lk::editor_new $ui $d $h]",
          &r);
  if (r) lcl_ref_dec(r);

  check_int(interp, "opaque? $e", 1);
  check_int(interp, "lk::editor_cursor $e", 0);

  /* Without a history the editor still works (undo just no-ops). */
  r = NULL;
  eval_ok(interp, "let e2 [lk::editor_new $ui $d]", &r);
  if (r) lcl_ref_dec(r);
  check_int(interp, "lk::editor_command $e2 undo", 0);

  /* Errors: arity, wrong opaques, foreign-doc history. */
  eval_expect_err(interp, "lk::editor_new $ui", "lk::editor_new",
                  "2 or 3 arguments", NULL);
  eval_expect_err(interp, "lk::editor_new $d $d", "expected lk_ui opaque",
                  NULL, NULL);
  eval_expect_err(interp, "lk::editor_new $ui $ui",
                  "expected lk_document opaque", NULL, NULL);
  eval_expect_err(interp,
                  "let d2 [lk::doc_new \"other\"]\n"
                  "let h2 [lk::history_new $d2]\n"
                  "lk::editor_new $ui $d $h2",
                  "different", "document", NULL);

  lcl_interp_free(interp);
  END_TEST();
}

static void test_editor_cursor_selection(void) {
  lcl_interp *interp;
  lcl_value *r = NULL;

  BEGIN_TEST("editor: set_cursor / cursor / selection");
  interp = make_interp();

  eval_ok(interp,
          "let ui [lk::ui_create]\n"
          "let d [lk::doc_new \"hello world\"]\n"
          "let e [lk::editor_new $ui $d]",
          &r);
  if (r) lcl_ref_dec(r);

  check_str(interp,
            "lk::editor_set_cursor $e 5\n"
            "",
            "");
  check_int(interp, "lk::editor_cursor $e", 5);

  /* Past-the-end clamps to the document length. */
  check_str(interp, "lk::editor_set_cursor $e 999", "");
  check_int(interp, "lk::editor_cursor $e", 11);

  /* No selection -> empty list; select_all -> (0 len). */
  check_int(interp, "len [lk::editor_selection $e]", 0);
  check_int(interp, "lk::editor_command $e select_all", 1);
  check_int(interp, "len [lk::editor_selection $e]", 2);
  check_int(interp, "get [lk::editor_selection $e] 0", 0);
  check_int(interp, "get [lk::editor_selection $e] 1", 11);

  eval_expect_err(interp, "lk::editor_set_cursor $e nope",
                  "non-negative integer", NULL, NULL);

  lcl_interp_free(interp);
  END_TEST();
}

/* scroll_to_cursor requests viewport work only: the cursor and any
 * selection survive it.  That is the whole reason it exists as its
 * own binding -- set_cursor would scroll too, but it collapses the
 * selection and re-snaps the cursor, so a tailing log could not use
 * it without stealing the reader's selection. */
static void test_editor_scroll_to_cursor(void) {
  lcl_interp *interp;
  lcl_value *r = NULL;

  BEGIN_TEST("editor: scroll_to_cursor keeps cursor and selection");
  interp = make_interp();

  eval_ok(interp,
          "let ui [lk::ui_create]\n"
          "let d [lk::doc_new \"hello world\"]\n"
          "let e [lk::editor_new $ui $d]\n"
          "lk::editor_set_cursor $e 4\n"
          "lk::editor_command $e select_all",
          &r);
  if (r) lcl_ref_dec(r);

  check_str(interp, "lk::editor_scroll_to_cursor $e", "");
  check_int(interp, "lk::editor_cursor $e", 11);
  check_int(interp, "len [lk::editor_selection $e]", 2);

  /* Contrast: set_cursor at the SAME position drops the selection. */
  check_str(interp, "lk::editor_set_cursor $e 11", "");
  check_int(interp, "len [lk::editor_selection $e]", 0);

  eval_expect_err(interp, "lk::editor_scroll_to_cursor",
                  "expected 1 argument", NULL, NULL);

  lcl_interp_free(interp);
  END_TEST();
}

static void test_editor_command_drives_doc(void) {
  lcl_interp *interp;
  lcl_value *r = NULL;

  BEGIN_TEST("editor: commands drive document + cursor + undo");
  interp = make_interp();

  eval_ok(interp,
          "let ui [lk::ui_create]\n"
          "let d [lk::doc_new \"world\"]\n"
          "let h [lk::history_new $d]\n"
          "let e [lk::editor_new $ui $d $h]",
          &r);
  if (r) lcl_ref_dec(r);

  /* insert_text at the cursor, cursor follows. */
  check_int(interp, "lk::editor_command $e insert_text \"hello \"", 1);
  check_str(interp, "lk::doc_text $d", "hello world");
  check_int(interp, "lk::editor_cursor $e", 6);

  /* Motion, plus the optional \"select\" flag extending a selection. */
  check_int(interp, "lk::editor_command $e move_left", 1);
  check_int(interp, "lk::editor_cursor $e", 5);
  check_int(interp, "lk::editor_command $e move_left select", 1);
  check_int(interp, "get [lk::editor_selection $e] 0", 4);
  check_int(interp, "get [lk::editor_selection $e] 1", 5);
  check_int(interp, "lk::editor_command $e move_doc_end", 1);
  check_int(interp, "lk::editor_cursor $e", 11);
  check_int(interp, "len [lk::editor_selection $e]", 0);

  /* delete_backward eats one codepoint. */
  check_int(interp, "lk::editor_command $e delete_backward", 1);
  check_str(interp, "lk::doc_text $d", "hello worl");

  /* undo/redo through the same verb the keyboard uses. */
  check_int(interp, "lk::editor_command $e undo", 1);
  check_str(interp, "lk::doc_text $d", "hello world");
  check_int(interp, "lk::editor_command $e redo", 1);
  check_str(interp, "lk::doc_text $d", "hello worl");

  /* set_cursor command with the extend flag keeps the anchor. */
  check_int(interp, "lk::editor_command $e set_cursor 0", 1);
  check_int(interp, "lk::editor_command $e set_cursor 5 extend", 1);
  check_int(interp, "get [lk::editor_selection $e] 1", 5);

  /* scroll_lines takes a signed count and never errors on ints. */
  r = NULL;
  eval_ok(interp, "lk::editor_command $e scroll_lines 2", &r);
  if (r) lcl_ref_dec(r);
  r = NULL;
  eval_ok(interp, "lk::editor_command $e scroll_lines -2", &r);
  if (r) lcl_ref_dec(r);

  lcl_interp_free(interp);
  END_TEST();
}

static void test_editor_command_errors(void) {
  lcl_interp *interp;
  lcl_value *r = NULL;

  BEGIN_TEST("editor: unknown command lists known commands");
  interp = make_interp();

  eval_ok(interp,
          "let ui [lk::ui_create]\n"
          "let d [lk::doc_new \"abc\"]\n"
          "let e [lk::editor_new $ui $d]",
          &r);
  if (r) lcl_ref_dec(r);

  /* Unknown command errors name the known-command list. */
  eval_expect_err(interp, "lk::editor_command $e frobnicate",
                  "unknown command 'frobnicate'", "insert_text",
                  "scroll_lines");
  eval_expect_err(interp, "lk::editor_command $e frobnicate", "select_all",
                  "move_doc_end", NULL);

  /* Malformed per-command args. */
  eval_expect_err(interp, "lk::editor_command $e insert_text",
                  "insert_text expects the text", NULL, NULL);
  eval_expect_err(interp, "lk::editor_command $e move_left sideways",
                  "\"select\" flag", NULL, NULL);
  eval_expect_err(interp, "lk::editor_command $e set_cursor nope",
                  "set_cursor pos", NULL, NULL);
  eval_expect_err(interp, "lk::editor_command $e set_cursor 3 shift",
                  "\"extend\"", NULL, NULL);
  eval_expect_err(interp, "lk::editor_command $e scroll_lines many",
                  "signed line count", NULL, NULL);
  eval_expect_err(interp, "lk::editor_command $e select_all now",
                  "takes no arguments", NULL, NULL);
  eval_expect_err(interp, "lk::editor_command $e", "lk::editor_command",
                  NULL, NULL);
  eval_expect_err(interp, "lk::editor_command $d insert_text x",
                  "expected lk_editor opaque", NULL, NULL);

  lcl_interp_free(interp);
  END_TEST();
}

static void test_editor_wrap_proc(void) {
  lcl_interp *interp;
  lcl_value *r = NULL;
  lk_ui *ui = NULL;

  BEGIN_TEST("editor: wrap mode set/get round-trip + errors");
  interp = make_interp();

  eval_ok(interp,
          "let ui [lk::ui_create]\n"
          "let d [lk::doc_new \"abc\"]\n"
          "let e [lk::editor_new $ui $d]",
          &r);
  if (r) lcl_ref_dec(r);

  /* Default NONE; all three modes round-trip through the getter. */
  check_str(interp, "lk::editor_wrap_get $e", "none");
  check_str(interp, "lk::editor_wrap $e character", "");
  check_str(interp, "lk::editor_wrap_get $e", "character");
  check_str(interp, "lk::editor_wrap $e word", "");
  check_str(interp, "lk::editor_wrap_get $e", "word");
  check_str(interp, "lk::editor_wrap $e none", "");
  check_str(interp, "lk::editor_wrap_get $e", "none");

  /* Bogus mode name: hard error listing the supported modes; the
   * mode is left unchanged. */
  eval_expect_err(interp, "lk::editor_wrap $e diagonal",
                  "unknown mode 'diagonal'",
                  "supported: none, character, word", NULL);
  check_str(interp, "lk::editor_wrap_get $e", "none");

  /* Word mode wraps for real: "hello world foo" under the stub
   * backend (8 px per codepoint) at width 80 breaks after "hello "
   * (byte 6, not the char-fit floor 10) -- ROW_START from inside
   * row 1 lands on the word break. */
  r = NULL;
  eval_ok(interp,
          "let d2 [lk::doc_new \"hello world foo\"]\n"
          "let e2 [lk::editor_new $ui $d2]\n"
          "let t [lk::begin_frame $ui]\n"
          "let w [lk::node $t \"w\" \"window\"]\n"
          "let n [lk::node $t \"ed\" \"editor\"]\n"
          "lk::set_root $t $w\n"
          "lk::append_child $t $w $n\n"
          "lk::prop $t $n focusable 1\n"
          "lk::prop $t $n editor $e2\n"
          "lk::end_frame $ui\n"
          "lk::editor_wrap $e2 word",
          &r);
  if (r) lcl_ref_dec(r);

  r = NULL;
  eval_ok(interp, "$ui", &r);
  if (r) {
    CHECK(lcl_opaque_get(r, "lk_ui", (void **)&ui) == LCL_OK);
    lcl_ref_dec(r);
  }
  CHECK(ui != NULL);

  if (ui) {
    lk_rect rects[8];
    lk_layout_cfg cfg;

    memset(&cfg, 0, sizeof(cfg));
    cfg.text = lk_text_backend_stub();
    cfg.viewport_w = 80;
    cfg.viewport_h = 80;
    cfg.state = lk_ui_state(ui);
    lk_ui_set_text_backend(ui, lk_text_backend_stub());
    CHECK(lk_layout(lk_ui_tree(ui), &cfg, rects));

    check_int(interp, "lk::editor_command $e2 set_cursor 8", 1);
    check_int(interp, "lk::editor_command $e2 move_row_start", 1);
    check_int(interp, "lk::editor_cursor $e2", 6);
    check_int(interp, "lk::editor_command $e2 move_line_start", 1);
    check_int(interp, "lk::editor_cursor $e2", 0);
  }

  /* Arity and type errors. */
  eval_expect_err(interp, "lk::editor_wrap $e", "lk::editor_wrap",
                  "2 arguments", NULL);
  eval_expect_err(interp, "lk::editor_wrap $e none extra",
                  "2 arguments", NULL, NULL);
  eval_expect_err(interp, "lk::editor_wrap $d character",
                  "expected lk_editor opaque", NULL, NULL);
  eval_expect_err(interp, "lk::editor_wrap_get $d",
                  "expected lk_editor opaque", NULL, NULL);
  eval_expect_err(interp, "lk::editor_wrap_get", "lk::editor_wrap_get",
                  "1 argument", NULL);

  lcl_interp_free(interp);
  END_TEST();
}

static void test_editor_row_commands(void) {
  lcl_interp *interp;
  lcl_value *r = NULL;
  lk_ui *ui = NULL;

  BEGIN_TEST("editor: move_row_start/end, wrapped vs logical");
  interp = make_interp();

  /* Unwrapped: the ROW variants are identical to the logical pair. */
  eval_ok(interp,
          "let ui [lk::ui_create]\n"
          "let d0 [lk::doc_new \"ab\ncd\"]\n"
          "let e0 [lk::editor_new $ui $d0]",
          &r);
  if (r) lcl_ref_dec(r);
  check_int(interp, "lk::editor_command $e0 set_cursor 4", 1);
  check_int(interp, "lk::editor_command $e0 move_row_start", 1);
  check_int(interp, "lk::editor_cursor $e0", 3);
  check_int(interp, "lk::editor_command $e0 move_row_end", 1);
  check_int(interp, "lk::editor_cursor $e0", 5);

  /* Wrapped: one 20-codepoint line under the stub backend (8 px per
   * codepoint, so width 80 = 10 codepoints per row -- the same
   * geometry as the core wrap tests).  The frame carries the editor
   * ref; a real lk_layout stamps the wrap key (content width). */
  r = NULL;
  eval_ok(interp,
          "let d [lk::doc_new \"abcdefghijklmnopqrst\"]\n"
          "let e [lk::editor_new $ui $d]\n"
          "let t [lk::begin_frame $ui]\n"
          "let w [lk::node $t \"w\" \"window\"]\n"
          "let n [lk::node $t \"ed\" \"editor\"]\n"
          "lk::set_root $t $w\n"
          "lk::append_child $t $w $n\n"
          "lk::prop $t $n focusable 1\n"
          "lk::prop $t $n editor $e\n"
          "lk::end_frame $ui\n"
          "lk::editor_wrap $e character",
          &r);
  if (r) lcl_ref_dec(r);

  r = NULL;
  eval_ok(interp, "$ui", &r);
  if (r) {
    CHECK(lcl_opaque_get(r, "lk_ui", (void **)&ui) == LCL_OK);
    lcl_ref_dec(r);
  }
  CHECK(ui != NULL);

  if (ui) {
    lk_rect rects[8];
    lk_layout_cfg cfg;

    memset(&cfg, 0, sizeof(cfg));
    cfg.text = lk_text_backend_stub();
    cfg.viewport_w = 80;
    cfg.viewport_h = 80;
    cfg.state = lk_ui_state(ui);
    lk_ui_set_text_backend(ui, lk_text_backend_stub());
    CHECK(lk_layout(lk_ui_tree(ui), &cfg, rects));

    /* Rows are [0,10) and [10,20].  Cursor on row 1: ROW_START goes
     * to the break, LINE_START to the line start. */
    check_int(interp, "lk::editor_command $e set_cursor 15", 1);
    check_int(interp, "lk::editor_command $e move_row_start", 1);
    check_int(interp, "lk::editor_cursor $e", 10);
    check_int(interp, "lk::editor_command $e move_line_start", 1);
    check_int(interp, "lk::editor_cursor $e", 0);

    /* Cursor on row 0: ROW_END stops at the break byte (owned by the
     * NEXT row), LINE_END goes to the line end. */
    check_int(interp, "lk::editor_command $e set_cursor 5", 1);
    check_int(interp, "lk::editor_command $e move_row_end", 1);
    check_int(interp, "lk::editor_cursor $e", 10);
    check_int(interp, "lk::editor_command $e set_cursor 5", 1);
    check_int(interp, "lk::editor_command $e move_line_end", 1);
    check_int(interp, "lk::editor_cursor $e", 20);

    /* The optional "select" flag extends, exactly like other motion
     * commands. */
    check_int(interp, "lk::editor_command $e set_cursor 15", 1);
    check_int(interp, "lk::editor_command $e move_row_start select", 1);
    check_int(interp, "get [lk::editor_selection $e] 0", 10);
    check_int(interp, "get [lk::editor_selection $e] 1", 15);
  }

  /* Malformed flag: the same hard error as the other motions; the
   * unknown-command listing names the row commands. */
  eval_expect_err(interp, "lk::editor_command $e move_row_end sideways",
                  "\"select\" flag", NULL, NULL);
  eval_expect_err(interp, "lk::editor_command $e frobnicate",
                  "move_row_start", "move_row_end", NULL);

  lcl_interp_free(interp);
  END_TEST();
}

static void test_editor_set_spans(void) {
  lcl_interp *interp;
  lcl_value *r = NULL;

  BEGIN_TEST("editor: set_spans accepts span dicts and clears");
  interp = make_interp();

  eval_ok(interp,
          "let ui [lk::ui_create]\n"
          "let d [lk::doc_new \"hello world again\"]\n"
          "let e [lk::editor_new $ui $d]",
          &r);
  if (r) lcl_ref_dec(r);

  /* fg-only, bg + underline, all three. */
  check_str(interp,
            "lk::editor_set_spans $e $d ( #{start 0 end 5 fg (255 0 0)} "
            "#{start 6 end 11 bg (0 0 80) underline 1} "
            "#{start 12 end 17 fg (1 2 3) bg (4 5 6 128)} )",
            "");

  /* Empty list clears. */
  check_str(interp, "lk::editor_set_spans $e $d ()", "");

  lcl_interp_free(interp);
  END_TEST();
}

static void test_editor_set_spans_errors(void) {
  lcl_interp *interp;
  lcl_value *r = NULL;

  BEGIN_TEST("editor: set_spans rejects malformed span dicts");
  interp = make_interp();

  eval_ok(interp,
          "let ui [lk::ui_create]\n"
          "let d [lk::doc_new \"hello world\"]\n"
          "let e [lk::editor_new $ui $d]",
          &r);
  if (r) lcl_ref_dec(r);

  eval_expect_err(interp, "lk::editor_set_spans $e $d #{start 0 end 2}",
                  "must be a list", NULL, NULL);
  eval_expect_err(interp, "lk::editor_set_spans $e $d ( #{end 2} )",
                  "missing 'start'", NULL, NULL);
  eval_expect_err(interp, "lk::editor_set_spans $e $d ( #{start 2 end 2} )",
                  "end must be an integer > start", NULL, NULL);
  eval_expect_err(interp,
                  "lk::editor_set_spans $e $d ( #{start 0 end 2 color (1 2 3)} )",
                  "unknown span key", "underline", NULL);
  eval_expect_err(interp,
                  "lk::editor_set_spans $e $d ( #{start 0 end 2 fg (1 2)} )",
                  "fg must be", NULL, NULL);
  eval_expect_err(interp,
                  "lk::editor_set_spans $e $d "
                  "( #{start 4 end 8} #{start 0 end 2} )",
                  "sorted and non-overlapping", NULL, NULL);
  eval_expect_err(interp, "lk::editor_set_spans $e $d", "3 arguments", NULL,
                  NULL);

  lcl_interp_free(interp);
  END_TEST();
}

static void test_editor_lifetime(void) {
  lcl_interp *interp;
  lcl_value *r = NULL;

  /* The lifetime guarantee (docs/editor.md section 5/11): the editor
   * wrapper retains the document and history VALUES, so dropping every
   * script handle to them cannot destroy the document while the editor
   * still uses it.  Under the naive scheme (doc finalizer fires as
   * soon as the script refs drop) the insert below would be a
   * use-after-free on the destroyed piece table. */
  BEGIN_TEST("editor: outlives dropped doc/history handles");
  interp = make_interp();

  eval_ok(interp,
          "let ui [lk::ui_create]\n"
          "proc mk {ui} {\n"
          "    let d [lk::doc_new \"ephemeral\"]\n"
          "    let h [lk::history_new $d]\n"
          "    return [lk::editor_new $ui $d $h]\n"
          "}\n"
          "var e [mk $ui]",
          &r);
  if (r) lcl_ref_dec(r);

  /* d and h went out of scope with mk's frame; only the editor keeps
   * them alive.  The editor must keep working: */
  check_int(interp, "lk::editor_command $e insert_text \"still \"", 1);
  check_int(interp, "lk::editor_cursor $e", 6);
  check_int(interp, "lk::editor_command $e undo", 1);
  check_int(interp, "lk::editor_cursor $e", 0);

  /* Now drop the editor too: its finalizer releases the resource
   * registration, destroys the editor (unsubscribing from the still-
   * live doc), and only then lets the doc/history finalizers run. */
  r = NULL;
  eval_ok(interp, "set! e 0", &r);
  if (r) lcl_ref_dec(r);

  /* Interp still healthy afterwards. */
  check_int(interp, "+ 1 1", 2);

  lcl_interp_free(interp);
  END_TEST();
}

static void test_annot_store_basics(void) {
  lcl_interp *interp;
  lcl_value *r = NULL;

  BEGIN_TEST("annot: store new/attach/add/span/meta");
  interp = make_interp();

  eval_ok(interp,
          "let d [lk::doc_new \"hello world\"]\n"
          "let s [lk::annot_store_new]\n"
          "lk::annot_attach $s $d\n"
          "lk::annot_layer_register $s \"marks\"\n"
          "let a [lk::annot_add $s 0 5 \"marks\" #{kind word note \"greeting\"}]",
          &r);
  if (r) lcl_ref_dec(r);

  check_int(interp, "$a", 1);
  check_int(interp, "get [lk::annot_span $s $a] 0", 0);
  check_int(interp, "get [lk::annot_span $s $a] 1", 5);
  check_str(interp, "lk::annot_meta $s $a kind", "word");
  check_str(interp, "lk::annot_meta $s $a note", "greeting");

  /* Absent meta key reads as "" (the record still exists). */
  check_str(interp, "lk::annot_meta $s $a missing", "");

  /* annot_meta_all: every pair as a dict. */
  check_int(interp, "len [lk::annot_meta_all $s $a]", 2);
  check_str(interp, "get [lk::annot_meta_all $s $a] kind", "word");
  check_str(interp, "get [lk::annot_meta_all $s $a] note", "greeting");
  eval_expect_err(interp, "lk::annot_meta_all $s 999", "no such annotation",
                  NULL, NULL);

  /* annot_layer: the record's layer name. */
  check_str(interp, "lk::annot_layer $s $a", "marks");
  eval_expect_err(interp, "lk::annot_layer $s 999", "no such annotation",
                  NULL, NULL);

  lcl_interp_free(interp);
  END_TEST();
}

static void test_annot_queries(void) {
  lcl_interp *interp;
  lcl_value *r = NULL;

  BEGIN_TEST("annot: in_range/at/by_layer with layer filters");
  interp = make_interp();

  eval_ok(interp,
          "let d [lk::doc_new \"hello world again\"]\n"
          "let s [lk::annot_store_new]\n"
          "lk::annot_attach $s $d\n"
          "let a1 [lk::annot_add $s 0 5 \"x\"]\n"
          "let a2 [lk::annot_add $s 6 11 \"y\"]\n"
          "let a3 [lk::annot_add $s 12 17 \"x\"]",
          &r);
  if (r) lcl_ref_dec(r);

  check_int(interp, "len [lk::annot_in_range $s 0 100]", 3);
  check_int(interp, "len [lk::annot_in_range $s 0 100 x]", 2);
  check_int(interp, "len [lk::annot_in_range $s 0 6]", 1);
  check_int(interp, "len [lk::annot_at $s 7]", 1);
  check_int(interp, "get [lk::annot_at $s 7] 0", 2);
  check_int(interp, "len [lk::annot_at $s 7 x]", 0);
  check_int(interp, "len [lk::annot_by_layer $s x]", 2);
  check_int(interp, "len [lk::annot_by_layer $s nothere]", 0);

  lcl_interp_free(interp);
  END_TEST();
}

static void test_annot_anchor_tracking(void) {
  lcl_interp *interp;
  lcl_value *r = NULL;

  BEGIN_TEST("annot: anchors track document edits via subscription");
  interp = make_interp();

  eval_ok(interp,
          "let d [lk::doc_new \"hello world\"]\n"
          "let s [lk::annot_store_new]\n"
          "lk::annot_attach $s $d\n"
          "let a [lk::annot_add $s 6 11 \"w\"]",
          &r);
  if (r) lcl_ref_dec(r);

  /* Insert before the annotation: both ends shift right. */
  r = NULL;
  eval_ok(interp, "lk::doc_insert $d 0 \"say: \"", &r);
  if (r) lcl_ref_dec(r);
  check_int(interp, "get [lk::annot_span $s $a] 0", 11);
  check_int(interp, "get [lk::annot_span $s $a] 1", 16);

  /* Delete across the middle: the span shrinks. */
  r = NULL;
  eval_ok(interp, "lk::doc_delete $d 11 2", &r);
  if (r) lcl_ref_dec(r);
  check_int(interp, "get [lk::annot_span $s $a] 0", 11);
  check_int(interp, "get [lk::annot_span $s $a] 1", 14);

  lcl_interp_free(interp);
  END_TEST();
}

static void test_annot_errors(void) {
  lcl_interp *interp;
  lcl_value *r = NULL;

  BEGIN_TEST("annot: remove + error paths");
  interp = make_interp();

  eval_ok(interp,
          "let d [lk::doc_new \"hello\"]\n"
          "let s [lk::annot_store_new]\n"
          "lk::annot_attach $s $d\n"
          "let a [lk::annot_add $s 0 5 \"m\"]",
          &r);
  if (r) lcl_ref_dec(r);

  check_int(interp, "lk::annot_remove $s $a", 1);
  check_int(interp, "lk::annot_remove $s $a", 0);
  eval_expect_err(interp, "lk::annot_span $s $a", "no such annotation", NULL,
                  NULL);
  eval_expect_err(interp, "lk::annot_meta $s $a kind", "no such annotation",
                  NULL, NULL);

  /* Bad ranges, bad meta, re-attach, wrong opaque, arity. */
  eval_expect_err(interp, "lk::annot_add $s 5 5 \"m\"",
                  "end must be an integer > start", NULL, NULL);
  eval_expect_err(interp, "lk::annot_add $s 0 5 \"m\" nope",
                  "meta must be a dict", NULL, NULL);
  eval_expect_err(interp, "lk::annot_attach $s $d", "already attached", NULL,
                  NULL);
  eval_expect_err(interp, "lk::annot_span $d 1", "expected lk_annot_store",
                  NULL, NULL);
  eval_expect_err(interp, "lk::annot_add $s", "lk::annot_add", "arguments",
                  NULL);

  lcl_interp_free(interp);
  END_TEST();
}

static void test_dsl_editor_widget(void) {
  lcl_interp *interp;
  lcl_value *r = NULL;
  lk_tree *t;
  lk_ui *ui;

  /* `editor "e" #{editor $ed focusable 1}` builds a UIK_EDITOR node
   * whose UIP_EDITOR prop carries the typed resource ref — resolvable
   * back to the lk_editor through the ui's resource table. */
  BEGIN_TEST("dsl: editor widget carries the resource prop");
  interp = make_dsl_interp();
  dsl_begin(interp);

  eval_ok(interp,
          "let d [lk::doc_new \"hi\"]\n"
          "let ed [lk::editor_new $u $d]\n"
          "editor e1 #{editor $ed focusable 1 w 300 h 200}",
          &r);
  if (r) lcl_ref_dec(r);

  t = dsl_tree(interp);
  ui = dsl_ui(interp);
  CHECK(t != NULL && ui != NULL);
  if (t && ui) {
    lk_ix n = dsl_find(t, "e1");

    CHECK(n != 0);
    CHECK(lk_node_kind_get(t, n) == (lk_u16)UIK_EDITOR);
    CHECK(lk_node_has_prop(t, n, UIP_EDITOR) == 1);
    CHECK(lk_node_prop_bool(t, n, UIP_FOCUSABLE) == 1);
    CHECK(lk_node_prop_i32(t, n, UIP_W, -1) == 300);

    /* The ref resolves to a live lk_editor of the right type. */
    CHECK(lk_editor_from_node(lk_ui_resources(ui), t, n) != NULL);
  }

  /* Typo'd resource value is a hard error at the binding layer. */
  eval_expect_err(interp, "editor e2 #{editor 42}",
                  "editor prop expects an lk_editor opaque", NULL, NULL);

  lcl_interp_free(interp);
  END_TEST();
}

static void test_dsl_unknown_prop_lists_editor(void) {
  lcl_interp *interp;

  /* The DSL's known-prop list now includes `editor`. */
  BEGIN_TEST("dsl: unknown-prop error lists editor among known keys");
  interp = make_dsl_interp();
  dsl_begin(interp);

  eval_expect_err(interp, "label w_ed #{bogus 1}", "unknown prop 'bogus'",
                  "(known:", "editor");

  lcl_interp_free(interp);
  END_TEST();
}

/* ============================================================================
 * Range presentations (weft-surface track, S1)
 * ============================================================================
 */

static void test_add_translator_button_arg(void) {
  lcl_interp *interp;
  lcl_value *r = NULL;
  lk_ui *ui;

  BEGIN_TEST("add_translator: optional 8th button arg");
  interp = make_interp();

  eval_ok(interp,
    "let ui [lk::ui_create]\n"
    /* 7-arg form still works (examples run unmodified) */
    "lk::add_translator $ui \"pointer_down\" \"a\" \"\" \"\" \"\" \"C0\"\n"
    "lk::add_translator $ui \"pointer_down\" \"a\" \"\" \"\" \"\" \"C1\" \"primary\"\n"
    "lk::add_translator $ui \"pointer_down\" \"a\" \"\" \"\" \"ctrl\" \"C2\" \"middle\"\n"
    "lk::add_translator $ui \"pointer_down\" \"a\" \"\" \"\" \"\" \"C3\" \"secondary\"\n"
    "lk::add_translator $ui \"pointer_down\" \"a\" \"\" \"\" \"\" \"C4\" \"\"\n"
    "lk::add_translator $ui \"pointer_down\" \"a\" \"\" \"\" \"\" \"C5\" 0",
    &r);
  if (r) lcl_ref_dec(r);

  ui = fetch_ui(interp);
  CHECK(ui != NULL);
  if (ui) {
    CHECK(ui->translator_count == 6);
    if (ui->translator_count == 6) {
      CHECK(ui->translators[0].button == 0);
      CHECK(ui->translators[1].button == (lk_u8)LK_POINTER_BUTTON_PRIMARY);
      CHECK(ui->translators[2].button == (lk_u8)LK_POINTER_BUTTON_MIDDLE);
      CHECK(ui->translators[2].mods == LK_MOD_CTRL);
      CHECK(ui->translators[3].button == (lk_u8)LK_POINTER_BUTTON_SECONDARY);
      CHECK(ui->translators[4].button == 0);
      CHECK(ui->translators[5].button == 0);
    }
  }

  eval_expect_err(interp,
                  "lk::add_translator $ui \"pointer_down\" \"a\" \"\" \"\" "
                  "\"\" \"C6\" \"wheel-click\"",
                  "unknown button", "primary", NULL);

  lcl_interp_free(interp);
  END_TEST();
}

/* Full pipeline in script: store + presentations + editor source +
 * button translator; the pointer event is driven through
 * lk_event_route from the C fixture (there is no event-synthesis
 * proc); the handler-visible command dict is asserted from script. */
static void test_lcl_presentation_pipeline(void) {
  lcl_interp *interp;
  lcl_value *r = NULL;
  lk_ui *ui;
  lk_rect rects[16];
  lk_layout_cfg cfg;
  lk_ix node = 0;

  BEGIN_TEST("pipeline: annot_present -> click -> hit dict");
  interp = make_interp();

  eval_ok(interp,
    "let ui [lk::ui_create]\n"
    "let doc [lk::doc_new \"hello file.c:12 world\"]\n"
    "let ed [lk::editor_new $ui $doc]\n"
    "let s [lk::annot_store_new]\n"
    "lk::annot_attach $s $doc\n"
    "let a [lk::annot_add $s 6 15 \"links\"]\n"
    "lk::annot_layer_priority $s \"links\" 3\n"
    "lk::annot_present $ui $s $a \"loc\" #{path \"file.c\" line 12}\n"
    "lk::editor_presentations $ed $s",
    &r);
  if (r) lcl_ref_dec(r);
  r = NULL;

  eval_ok(interp,
    "lk::add_translator $ui \"pointer_down\" \"loc\" \"\" \"\" \"\" \"Open\" \"middle\"\n"
    "let t [lk::begin_frame $ui]\n"
    "let w [lk::node $t \"w\" \"window\"]\n"
    "let e [lk::node $t \"ed\" \"editor\"]\n"
    "lk::set_root $t $w\n"
    "lk::append_child $t $w $e\n"
    "lk::prop $t $e \"editor\" $ed\n"
    "lk::end_frame $ui",
    &r);
  if (r) lcl_ref_dec(r);
  r = NULL;

  ui = fetch_ui(interp);
  CHECK(ui != NULL);

  if (ui) {
    lk_event ev;

    lk_ui_set_text_backend(ui, lk_text_backend_stub());
    memset(&cfg, 0, sizeof(cfg));
    cfg.text = lk_text_backend_stub();
    cfg.viewport_w = 640;
    cfg.viewport_h = 480;
    cfg.state = lk_ui_state(ui);
    CHECK(lk_layout(lk_ui_tree(ui), &cfg, rects) == 1);

    node = lk_tree_find_by_id(lk_ui_tree(ui),
                              lk_intern_cid(ui->intern, "ed"));
    CHECK(node != 0);

    /* middle-click at byte 8 (inside [6,15)): 8 px stub chars */
    memset(&ev, 0, sizeof(ev));
    ev.type = LK_EVENT_POINTER_DOWN;
    ev.target = node;
    ev.data.pointer.x = rects[node].x + 8 * 8 + 3;
    ev.data.pointer.y = rects[node].y + 8;
    ev.data.pointer.button = (lk_u8)LK_POINTER_BUTTON_MIDDLE;
    lk_event_route(ui, &ev);
    CHECK(ev.handled == 1);
  }

  /* script-side assertions on the marshaled command */
  eval_ok(interp,
    "let cmds [lk::commands $ui]\n"
    "let c [get $cmds 0]\n"
    "let h [get $c hit]\n"
    "let lc [get $h locus]",
    &r);
  if (r) lcl_ref_dec(r);
  r = NULL;

  {
    static const struct {
      const char *src;
      const char *want;
    } checks[] = {
      {"len $cmds", "1"},
      {"get $c name", "Open"},
      {"get $h ptype", "loc"},
      {"get $h locus_kind", "editor-range"},
      {"get [get $h value] path", "file.c"},
      {"get [get $h value] line", "12"},
      {"get $lc annot_id", "1"},
      {"get $lc start", "6"},
      {"get $lc end", "15"},
      {"get $lc pos", "8"},
      /* pinned: translated click moved no cursor */
      {"lk::editor_cursor $ed", "0"},
      /* the hit's revision matches the doc now... */
      {"== [get $lc rev] [lk::doc_revision $doc]", "1"},
    };
    size_t i;

    for (i = 0; i < sizeof(checks) / sizeof(checks[0]); i++) {
      eval_ok(interp, checks[i].src, &r);
      if (r) {
        const char *got = lcl_value_to_string(r);
        if (strcmp(got, checks[i].want) != 0) {
          if (g_cur_ok) printf("FAIL\n");
          printf("    %s -> '%s', want '%s'\n", checks[i].src, got,
                 checks[i].want);
          g_cur_ok = 0;
        }
        lcl_ref_dec(r);
        r = NULL;
      }
    }
  }

  /* ...and goes stale detectably after an edit (script-visible) */
  eval_ok(interp,
    "lk::doc_insert $doc 0 \"x\"\n"
    "== [get $lc rev] [lk::doc_revision $doc]",
    &r);
  if (r) {
    CHECK(strcmp(lcl_value_to_string(r), "0") == 0);
    lcl_ref_dec(r);
    r = NULL;
  }

  /* removing the annotation releases the wrapped value (hook path)
   * and ends the candidacy: the next click bubbles unhandled */
  eval_ok(interp, "lk::clear_commands $ui\nlk::annot_remove $s $a", &r);
  if (r) lcl_ref_dec(r);
  r = NULL;

  if (ui && node) {
    lk_event ev;

    memset(&ev, 0, sizeof(ev));
    ev.type = LK_EVENT_POINTER_DOWN;
    ev.target = node;
    ev.data.pointer.x = rects[node].x + 8 * 8 + 3;
    ev.data.pointer.y = rects[node].y + 8;
    ev.data.pointer.button = (lk_u8)LK_POINTER_BUTTON_MIDDLE;
    lk_event_route(ui, &ev);
    CHECK(ev.handled == 0);
    CHECK(lk_ui_commands(ui)->count == 0);
  }

  /* lk::editor_pos_at against the same layout snapshot */
  eval_ok(interp, "lk::editor_pos_at $ed 67 8", &r);
  if (r) {
    long v = -2;
    lcl_value_to_int(r, &v);
    /* x=67 -> nearest boundary 8 (still valid: the edit above
     * invalidated geometry, so accept -1 or 8?  No: pos_at needs a
     * live snapshot — relayout first happened before the edit; the
     * edit invalidated it, so this returns -1.  Assert exactly that,
     * then relayout and assert the position. */
    CHECK(v == -1);
    lcl_ref_dec(r);
    r = NULL;
  }

  if (ui) {
    CHECK(lk_layout(lk_ui_tree(ui), &cfg, rects) == 1);
  }

  eval_ok(interp, "lk::editor_pos_at $ed 67 8", &r);
  if (r) {
    long v = -2;
    lcl_value_to_int(r, &v);
    CHECK(v == 8);
    lcl_ref_dec(r);
    r = NULL;
  }

  eval_ok(interp, "lk::editor_pos_at $ed -5 -5", &r);
  if (r) {
    long v = -2;
    lcl_value_to_int(r, &v);
    CHECK(v == -1);
    lcl_ref_dec(r);
    r = NULL;
  }

  lcl_interp_free(interp);
  END_TEST();
}

/* ---- focus_changed (polish F2) ---- */

/* The event-to-dict marshal is exported by lcl-lk.c for hosts that
 * drive lk_event_route themselves (and for these tests). */
extern lcl_value *lcl_lk_event_to_dict(const lk_event *ev,
                                       const lk_intern *intern);

/* Minimal headless event-handler bridge: marshal the event and call
 * the script lambda fetched by the test (the window bridge does the
 * same thing with extra target_id/node_id sugar). */
struct focus_probe {
  lcl_interp *interp;
  lcl_value *handler;
  const lk_intern *intern;
};

static int focus_probe_handler(lk_event *event, lk_ix node_ix, void *ud) {
  struct focus_probe *p = (struct focus_probe *)ud;
  lcl_value *args[2];
  lcl_value *result = NULL;

  (void)node_ix;

  if (event->phase != LK_PHASE_TARGET) {
    return 0;
  }

  args[0] = lcl_lk_event_to_dict(event, p->intern);
  args[1] = lcl_int_new((long)node_ix);
  lcl_call_proc(p->interp, p->handler, 2, args, &result);
  lcl_ref_dec(args[0]);
  lcl_ref_dec(args[1]);

  if (result) {
    lcl_ref_dec(result);
  }

  return 0;
}

static void test_focus_changed_marshal(void) {
  lcl_interp *interp;
  lcl_value *r = NULL;
  lcl_value *handler = NULL;
  lk_ui *ui;
  struct focus_probe probe;

  BEGIN_TEST("focus_changed reaches script handler with ids");
  interp = make_interp();

  eval_ok(interp,
    "let ui [lk::ui_create]\n"
    "let t [lk::begin_frame $ui]\n"
    "let w [lk::node $t \"w\" \"window\"]\n"
    "let b1 [lk::node $t \"b1\" \"button\"]\n"
    "let b2 [lk::node $t \"b2\" \"button\"]\n"
    "lk::prop $t $b1 \"focusable\" 1\n"
    "lk::prop $t $b2 \"focusable\" 1\n"
    "lk::set_root $t $w\n"
    "lk::append_child $t $w $b1\n"
    "lk::append_child $t $w $b2\n"
    "lk::end_frame $ui",
    &r);
  if (r) lcl_ref_dec(r);
  r = NULL;

  eval_ok(interp,
    "var g_prev \"-\"\n"
    "var g_next \"-\"\n"
    "var g_count 0\n"
    "let h [lambda {ev node} {\n"
    "  if [== [get $ev type] focus_changed] {\n"
    "    set! g_prev [get $ev prev_id]\n"
    "    set! g_next [get $ev next_id]\n"
    "    set! g_count [+ $g_count 1]\n"
    "  }\n"
    "  return 0\n"
    "}]",
    &r);
  if (r) lcl_ref_dec(r);
  r = NULL;

  ui = fetch_ui(interp);
  CHECK(ui != NULL);

  eval_ok(interp, "$h", &handler);
  CHECK(handler != NULL);

  if (ui && handler) {
    probe.interp = interp;
    probe.handler = handler;
    probe.intern = ui->intern;
    lk_ui_set_event_handler(ui, focus_probe_handler, &probe);

    /* Focus via the script-facing proc, drain outside routing. */
    eval_ok(interp, "lk::focus_set $ui \"b1\"", &r);
    if (r) lcl_ref_dec(r);
    r = NULL;
    lk_ui_flush_events(ui, NULL);

    {
      static const struct {
        const char *src;
        const char *want;
      } checks[] = {
        {"$g_count", "1"},
        {"$g_prev", ""},
        {"$g_next", "b1"},
      };
      size_t i;

      for (i = 0; i < sizeof(checks) / sizeof(checks[0]); i++) {
        eval_ok(interp, checks[i].src, &r);
        if (r) {
          const char *got = lcl_value_to_string(r);
          if (strcmp(got, checks[i].want) != 0) {
            if (g_cur_ok) printf("FAIL\n");
            printf("    %s -> '%s', want '%s'\n", checks[i].src, got,
                   checks[i].want);
            g_cur_ok = 0;
          }
          lcl_ref_dec(r);
          r = NULL;
        }
      }
    }

    /* Second change carries the old focus as prev_id. */
    eval_ok(interp, "lk::focus_set $ui \"b2\"", &r);
    if (r) lcl_ref_dec(r);
    r = NULL;
    lk_ui_flush_events(ui, NULL);

    eval_ok(interp, "$g_prev", &r);
    if (r) {
      CHECK(strcmp(lcl_value_to_string(r), "b1") == 0);
      lcl_ref_dec(r);
      r = NULL;
    }

    eval_ok(interp, "$g_next", &r);
    if (r) {
      CHECK(strcmp(lcl_value_to_string(r), "b2") == 0);
      lcl_ref_dec(r);
      r = NULL;
    }

    /* focus_clear marshals next_id as the empty string. */
    eval_ok(interp, "lk::focus_clear $ui", &r);
    if (r) lcl_ref_dec(r);
    r = NULL;
    lk_ui_flush_events(ui, NULL);

    eval_ok(interp, "$g_next", &r);
    if (r) {
      CHECK(strcmp(lcl_value_to_string(r), "") == 0);
      lcl_ref_dec(r);
      r = NULL;
    }

    lk_ui_set_event_handler(ui, NULL, NULL);
  }

  if (handler) {
    lcl_ref_dec(handler);
  }

  lcl_interp_free(interp);
  END_TEST();
}

static void test_focus_changed_translator_name(void) {
  lcl_interp *interp;
  lcl_value *r = NULL;
  lk_ui *ui;

  BEGIN_TEST("add_translator accepts focus_changed");
  interp = make_interp();

  eval_ok(interp,
    "let ui [lk::ui_create]\n"
    "lk::add_translator $ui \"focus_changed\" \"\" \"\" \"\" \"\" \"Act\"",
    &r);
  if (r) lcl_ref_dec(r);
  CHECK(g_cur_ok);

  ui = fetch_ui(interp);
  CHECK(ui != NULL);

  if (ui) {
    CHECK(ui->translator_count == 1);
    CHECK(ui->translators[0].event_type == (lk_u8)LK_EVENT_FOCUS_CHANGED);
  }

  lcl_interp_free(interp);
  END_TEST();
}

static void test_annot_present_errors(void) {
  lcl_interp *interp;
  lcl_value *r = NULL;

  BEGIN_TEST("annot_present: error paths");
  interp = make_interp();

  eval_ok(interp,
    "let ui [lk::ui_create]\n"
    "let doc [lk::doc_new \"hello\"]\n"
    "let s [lk::annot_store_new]\n"
    "lk::annot_attach $s $doc\n"
    "let a [lk::annot_add $s 0 5 \"l\"]",
    &r);
  if (r) lcl_ref_dec(r);

  eval_expect_err(interp, "lk::annot_present $ui $s 999 \"t\" 1",
                  "no such annotation", NULL, NULL);
  eval_expect_err(interp, "lk::annot_present $ui $s $a \"\" 1",
                  "ptype must be non-empty", NULL, NULL);
  eval_expect_err(interp, "lk::annot_present $ui $s", "expected 5 arguments",
                  NULL, NULL);

  /* binding a second ui is rejected */
  eval_ok(interp,
    "lk::annot_present $ui $s $a \"t\" 1\n"
    "let ui2 [lk::ui_create]",
    &r);
  if (r) lcl_ref_dec(r);
  eval_expect_err(interp, "lk::annot_present $ui2 $s $a \"t\" 2",
                  "bound to a different ui", NULL, NULL);

  lcl_interp_free(interp);
  END_TEST();
}

/* Name-shaped args (node ids, tags, ptypes, layers) accept strings
 * and typed numbers (which render), but hard-error on structured
 * values — a dict or opaque passed where a name was meant is a
 * wrong-variable bug, not a name. */
static void test_name_arg_strictness(void) {
  lcl_interp *interp;
  lcl_value *r = NULL;

  BEGIN_TEST("name args: numbers render, structured values error");
  interp = make_interp();

  eval_ok(interp,
    "let u [lk::ui_create]\n"
    "let t [lk::begin_frame $u]",
    &r);
  if (r) lcl_ref_dec(r);

  /* A typed int is a fine node id — it renders to its canonical text. */
  eval_ok(interp,
    "let n [lk::node $t 42 label]\n"
    "lk::set_root $t $n",
    &r);
  if (r) lcl_ref_dec(r);

  eval_expect_err(interp, "lk::node $t $u label",
                  "lk::node: id", "got opaque", NULL);
  eval_expect_err(interp, "lk::node $t (a b) label",
                  "lk::node: id", "got list", NULL);
  eval_expect_err(interp, "lk::tag $t 1 #{x 1}",
                  "lk::tag: tag", "got dict", NULL);
  eval_expect_err(interp, "lk::focus_set $u [lambda {x} {}]",
                  "lk::focus_set: node id", "got proc", NULL);
  eval_expect_err(interp, "lk::state_set $u #{a 1} 300 1",
                  "lk::state_set: node id", "got dict", NULL);

  eval_ok(interp,
    "let d [lk::doc_new \"hello\"]\n"
    "let s [lk::annot_store_new]\n"
    "lk::annot_attach $s $d",
    &r);
  if (r) lcl_ref_dec(r);

  eval_expect_err(interp, "lk::annot_add $s 0 2 (l1 l2)",
                  "lk::annot_add: layer", "got list", NULL);

  lcl_interp_free(interp);
  END_TEST();
}

static void test_dsl_translator_matcher_dict(void) {
  lcl_interp *interp;
  lcl_value *r = NULL;
  lk_ui *ui;

  BEGIN_TEST("dsl: translator matcher dict");
  interp = make_dsl_interp();
  dsl_begin(interp);

  eval_ok(interp,
    "translator pointer_down #{button middle} action Execute\n"
    "translator pointer_down #{button secondary mods ctrl} loc button Look\n"
    "translator pointer_down item Select",
    &r);
  if (r) lcl_ref_dec(r);

  ui = dsl_ui(interp);
  CHECK(ui != NULL);
  if (ui) {
    CHECK(ui->translator_count == 3);
    if (ui->translator_count == 3) {
      const lk_translator *tr = ui->translators;

      CHECK(tr[0].ptype == lk_intern_cid(ui->intern, "action"));
      CHECK(tr[0].button == (lk_u8)LK_POINTER_BUTTON_MIDDLE);
      CHECK(tr[0].mods == 0);
      CHECK(tr[0].command_name == lk_intern_cid(ui->intern, "Execute"));

      /* dict + kind filter compose */
      CHECK(tr[1].ptype == lk_intern_cid(ui->intern, "loc"));
      CHECK(tr[1].node_kind == (lk_u16)UIK_BUTTON);
      CHECK(tr[1].button == (lk_u8)LK_POINTER_BUTTON_SECONDARY);
      CHECK(tr[1].mods == LK_MOD_CTRL);

      /* positional form unchanged */
      CHECK(tr[2].button == 0);
      CHECK(tr[2].ptype == lk_intern_cid(ui->intern, "item"));
    }
  }

  /* error paths: unknown matcher key, two dicts, bad shape */
  eval_expect_err(interp,
                  "translator pointer_down #{buttn middle} action Go",
                  "unknown matcher key 'buttn'", "button, mods", NULL);
  eval_expect_err(interp,
                  "translator pointer_down #{button middle} #{mods ctrl} action Go",
                  "more than one matcher dict", NULL, NULL);
  eval_expect_err(interp, "translator pointer_down action",
                  "expected ?matcher-dict? ptype ?kind? cmd", NULL, NULL);

  lcl_interp_free(interp);
  END_TEST();
}

static void test_dsl_split_controlled_command(void) {
  /* Controlled split through the full DSL surface: `split_controlled 1`
   * + a `pane` presentation + a value_changed translator + an
   * on-handler.  A C-routed divider drag delivers the per-mille ratio
   * to the handler as the command's source_value; no LKS_SPLIT_RATIO
   * state appears.  Geometry mirrors the C split tests: viewport
   * 400x300, band at x=197..202, MOVE to x=100 -> (100-2)*1000/395 =
   * 248. */
  lcl_interp *interp;
  lcl_value *r = NULL;
  lk_ui *ui;

  BEGIN_TEST("dsl: controlled split ratio command");
  interp = make_dsl_interp();

  eval_ok(interp,
    "let u [lk::ui_create]\n"
    "set! lk_dsl::_ui $u\n"
    "view {\n"
    "    split_h sp #{split_controlled 1 present (pane 0)} {\n"
    "        column c1\n"
    "        column c2\n"
    "    }\n"
    "}\n"
    "translator value_changed pane SplitMoved\n"
    "on SplitMoved [lambda {cmd} {\n"
    "    lk::state_set $u sink 300 [get $cmd source_value]\n"
    "}]\n"
    "lk::set_command_handler $u [lambda {cmd} {\n"
    "    lk_dsl::_dispatch_command $cmd\n"
    "}]\n"
    "let t [lk::begin_frame $u]\n"
    "lk_dsl::_frame $t\n"
    "lk::end_frame $u",
    &r);
  if (r) lcl_ref_dec(r);

  ui = dsl_ui(interp);
  CHECK(ui != NULL);

  if (ui) {
    lk_rect rects[16];
    lk_layout_cfg cfg;
    lk_ix sp;
    lk_event ev;

    memset(&cfg, 0, sizeof(cfg));
    cfg.viewport_w = 400;
    cfg.viewport_h = 300;
    cfg.state = lk_ui_state(ui);
    cfg.geom = lk_ui_geom(ui); /* split dragging needs the stash */
    CHECK(lk_layout(lk_ui_tree(ui), &cfg, rects) == 1);

    sp = lk_tree_find_by_id(lk_ui_tree(ui), lk_intern_cid(ui->intern, "sp"));
    CHECK(sp != 0);

    /* DOWN in the divider band, then MOVE to x=100 */
    memset(&ev, 0, sizeof(ev));
    ev.type = LK_EVENT_POINTER_DOWN;
    ev.target = sp;
    ev.data.pointer.x = 199;
    ev.data.pointer.y = 150;
    lk_event_route(ui, &ev);
    CHECK(ev.handled == 1);

    memset(&ev, 0, sizeof(ev));
    ev.type = LK_EVENT_POINTER_MOVE;
    ev.target = sp;
    ev.data.pointer.x = 100;
    ev.data.pointer.y = 150;
    lk_event_route(ui, &ev);
    CHECK(ev.handled == 1);

    /* controlled: no ratio state was written */
    CHECK(lk_state_get(lk_ui_state(ui), lk_ui_tree(ui)->nodes[sp].id,
                       LKS_SPLIT_RATIO)
              .tag == UIV_NONE);
  }

  /* the on-handler saw the command and stored its source_value */
  check_str(interp, "lk::state_get $u sink 300", "248");

  lcl_interp_free(interp);
  END_TEST();
}

/* ============================================================================
 * Companion-doc doctests (docs/lk.lcl).
 *
 * The docs file is NEVER evaluated — its stub proc bodies would shadow
 * the real C commands.  Instead its text is read here and handed to
 * Doc::extract (the reader-based extractor from lcl's Doc package);
 * the `>>` examples then run against the live lk:: bindings in this
 * interp, so every doctest is a genuine test of the C code.
 *
 * Coverage is enforced in the forward direction only: every name
 * registered in the lk namespace must have a documented entry.  The
 * reverse (every documented name registered) is deliberately not
 * checked — docs describe the SDL window procs even in non-SDL builds.
 * ============================================================================
 */

#ifndef TEST_DOC_LIB_PATH
#define TEST_DOC_LIB_PATH "build/_deps/lcl-src/lib/doc/src/Doc.lcl"
#endif
#ifndef TEST_LK_DOCS_PATH
#define TEST_LK_DOCS_PATH "docs/lk.lcl"
#endif

static char *read_text_file(const char *path) {
  FILE *f = fopen(path, "rb");
  long n;
  char *buf;

  if (!f)
    return NULL;
  fseek(f, 0, SEEK_END);
  n = ftell(f);
  fseek(f, 0, SEEK_SET);
  buf = (char *)malloc((size_t)n + 1);
  if (!buf) {
    fclose(f);
    return NULL;
  }
  if (fread(buf, 1, (size_t)n, f) != (size_t)n) {
    free(buf);
    fclose(f);
    return NULL;
  }
  buf[n] = '\0';
  fclose(f);
  return buf;
}

static void test_lk_docs_doctests(void) {
  lcl_interp *interp;
  lcl_value *r = NULL;
  char *src;
  int rc;

  BEGIN_TEST("docs/lk.lcl doctests + coverage");
  interp = make_dsl_interp();

  rc = lcl_eval_file(interp, TEST_DOC_LIB_PATH, &r);
  if (r) {
    lcl_ref_dec(r);
    r = NULL;
  }
  if (rc != LCL_RC_OK) {
    const char *msg = lcl_interp_error_msg(interp);
    if (g_cur_ok)
      printf("FAIL\n");
    printf("    Doc lib load error (%s): %s\n", TEST_DOC_LIB_PATH,
           msg ? msg : "(null)");
    g_cur_ok = 0;
  }

  src = read_text_file(TEST_LK_DOCS_PATH);
  CHECK(src != NULL);

  if (g_cur_ok && src) {
    lcl_define_take(interp, "__lk_docs_src", lcl_string_new(src));

    eval_ok(interp, "let __m [Doc::extract $__lk_docs_src]", &r);
    if (r) {
      lcl_ref_dec(r);
      r = NULL;
    }

    /* Coverage: every registered lk:: name has a doc entry. */
    eval_ok(interp,
      "let __lkents [get [get [get $__m entries] 0] entries]\n"
      "let __names [List::map $__lkents [lambda {e} { get $e name }]]\n"
      "var __missing ()\n"
      "foreach __k [Ns::keys $lk] {\n"
      "  if [not [List::any? $__names [lambda {n} { == $n $__k }]]] {\n"
      "    set! __missing [List::push $__missing $__k]\n"
      "  }\n"
      "}\n"
      "String::join $__missing \", \"",
      &r);
    if (r) {
      const char *missing = lcl_value_to_string(r);
      if (missing && missing[0] != '\0') {
        if (g_cur_ok)
          printf("FAIL\n");
        printf("    undocumented lk:: procs: %s\n", missing);
        g_cur_ok = 0;
      }
      lcl_ref_dec(r);
      r = NULL;
    }

    /* Doctests: run every example against the live bindings. */
    eval_ok(interp, "Doc::report [Doc::doctest $__m]", &r);
    if (r) {
      long fails = -1;
      CHECK(lcl_value_to_int(r, &fails) == LCL_OK);
      CHECK(fails == 0);
      lcl_ref_dec(r);
      r = NULL;
    }
  }

  free(src);

  /* Same treatment for the DSL's inline docs (lib/lk-dsl.lcl — a real
   * evaluated library, already loaded above by make_dsl_interp).
   * Coverage skips underscore-prefixed (private) names. */
  src = read_text_file(TEST_DSL_PATH);
  CHECK(src != NULL);

  if (g_cur_ok && src) {
    lcl_define_take(interp, "__dsl_src", lcl_string_new(src));

    eval_ok(interp, "let __dm [Doc::extract $__dsl_src]", &r);
    if (r) {
      lcl_ref_dec(r);
      r = NULL;
    }

    eval_ok(interp,
      "let __dents [get [get [get $__dm entries] 0] entries]\n"
      "let __dnames [List::map $__dents [lambda {e} { get $e name }]]\n"
      "var __dmissing ()\n"
      "foreach __k [Ns::keys $lk_dsl] {\n"
      "  if [not [== [String::range $__k 0 1] \"_\"]] {\n"
      "    if [not [List::any? $__dnames [lambda {n} { == $n $__k }]]] {\n"
      "      set! __dmissing [List::push $__dmissing $__k]\n"
      "    }\n"
      "  }\n"
      "}\n"
      "String::join $__dmissing \", \"",
      &r);
    if (r) {
      const char *missing = lcl_value_to_string(r);
      if (missing && missing[0] != '\0') {
        if (g_cur_ok)
          printf("FAIL\n");
        printf("    undocumented lk_dsl procs: %s\n", missing);
        g_cur_ok = 0;
      }
      lcl_ref_dec(r);
      r = NULL;
    }

    eval_ok(interp, "Doc::report [Doc::doctest $__dm]", &r);
    if (r) {
      long fails = -1;
      CHECK(lcl_value_to_int(r, &fails) == LCL_OK);
      CHECK(fails == 0);
      lcl_ref_dec(r);
      r = NULL;
    }
  }

  free(src);
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
  test_focus_get_round_trip();
  test_time_ms_binding();
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

  /* Layer-2 DSL (lib/lk-dsl.lcl) — v2 props-dict harness */
  test_dsl_widget_kinds();
  test_dsl_props_text_dims();
  test_dsl_props_layout();
  test_dsl_props_bools();
  test_dsl_props_tooltip_split_ratio();
  test_state_internal_keys_blocked();
  test_value_prop_binding();
  test_dsl_prop_value();
  test_dsl_prop_grow();
  test_prop_grow_negative_errors();
  test_dsl_tag();
  test_dsl_present_scalar();
  test_dsl_present_multiarg();
  test_dsl_nesting();
  test_dsl_unknown_prop_errors();
  test_dsl_bad_trailing_arg_errors();
  test_dsl_theme_rules();
  test_dsl_translators_keybindings();
  test_dsl_on_dispatch();
  test_dsl_props_dict_variable();
  test_dsl_props_dict_merge();
  test_dsl_frame_view_rebuild();

  /* Editor track (stage D): documents, histories, editors, annots */
  test_doc_new_read();
  test_doc_revision_string();
  test_doc_insert_delete();
  test_doc_mutation_errors();
  test_doc_line_procs();
  test_doc_char_col();
  test_doc_find_binding();
  test_doc_find_errors();
  test_doc_transact_groups();
  test_doc_transact_error_propagates();
  test_doc_subscribe_deltas();
  test_doc_unsubscribe();
  test_history_undo_redo();
  test_history_errors();
  test_history_savepoint();
  test_editor_new_basic();
  test_editor_cursor_selection();
  test_editor_scroll_to_cursor();
  test_editor_command_drives_doc();
  test_editor_command_errors();
  test_editor_wrap_proc();
  test_editor_row_commands();
  test_editor_set_spans();
  test_editor_set_spans_errors();
  test_editor_lifetime();
  test_annot_store_basics();
  test_annot_queries();
  test_annot_anchor_tracking();
  test_annot_errors();
  test_dsl_editor_widget();
  test_dsl_unknown_prop_lists_editor();

  /* Range presentations (weft-surface track, S1) */
  test_add_translator_button_arg();
  test_lcl_presentation_pipeline();
  test_focus_changed_marshal();
  test_focus_changed_translator_name();
  test_annot_present_errors();
  test_name_arg_strictness();
  test_dsl_translator_matcher_dict();
  test_dsl_split_controlled_command();

  /* Companion-doc doctests (docs/lk.lcl) */
  test_lk_docs_doctests();

  printf("\n%d tests: %d passed, %d failed\n", g_tests, g_pass, g_fail);

  return g_fail > 0 ? 1 : 0;
}
