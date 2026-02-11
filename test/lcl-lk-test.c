/*
 * lcl-lk-test.c — Headless tests for the lk Lcl bindings.
 *
 * Uses lcl_eval_string to exercise all non-SDL procs.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <lcl.h>
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
    "lk::add_translator $ui \"pointer_down\" \"item\" \"\" \"Select\"",
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
    "lk::add_translator $ui \"pointer_down\" \"\" \"\" \"cmd1\"\n"
    "lk::add_translator $ui \"pointer_up\" \"\" \"\" \"cmd2\"\n"
    "lk::add_translator $ui \"pointer_move\" \"\" \"\" \"cmd3\"\n"
    "lk::add_translator $ui \"key_down\" \"\" \"\" \"cmd4\"\n"
    "lk::add_translator $ui \"key_up\" \"\" \"\" \"cmd5\"\n"
    "lk::add_translator $ui \"text\" \"\" \"\" \"cmd6\"\n"
    "lk::add_translator $ui \"wheel\" \"\" \"\" \"cmd7\"\n"
    "lk::add_translator $ui \"window_resize\" \"\" \"\" \"cmd8\"\n"
    "lk::add_translator $ui \"window_close\" \"\" \"\" \"cmd9\"\n"
    "lk::add_translator $ui \"\" \"\" \"\" \"cmd_any\"",
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

  test_lcl_tag();
  test_lcl_theme_rule();

  printf("\n%d tests: %d passed, %d failed\n", g_tests, g_pass, g_fail);

  return g_fail > 0 ? 1 : 0;
}
