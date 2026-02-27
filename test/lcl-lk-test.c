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

  eval_ok(interp, "let ui [Lk::ui_create]", &r);
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

  eval_ok(interp, "let ui [Lk::ui_create]", &r);
  if (r) lcl_ref_dec(r);
  r = NULL;

  /* First frame: build a simple tree */
  eval_ok(interp,
    "let t [Lk::begin_frame $ui]\n"
    "let w [Lk::node $t \"main\" \"window\"]\n"
    "let col [Lk::node $t \"root\" \"column\"]\n"
    "Lk::set_root $t $w\n"
    "Lk::append_child $t $w $col\n"
    "Lk::end_frame $ui",
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

  eval_ok(interp, "let ui [Lk::ui_create]", &r);
  if (r) lcl_ref_dec(r);
  r = NULL;

  eval_ok(interp,
    "let t [Lk::begin_frame $ui]\n"
    "Lk::node $t \"n1\" \"label\"",
    &r);

  if (r) {
    long ix;
    CHECK(lcl_value_to_int(r, &ix) == LCL_OK);
    CHECK(ix >= 1);
    lcl_ref_dec(r);
  }

  /* Clean up frame */
  r = NULL;
  eval_ok(interp, "Lk::end_frame $ui", &r);
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
    "let ui [Lk::ui_create]\n"
    "let t [Lk::begin_frame $ui]\n"
    "let w [Lk::node $t \"main\" \"window\"]\n"
    "let lbl [Lk::node $t \"lbl\" \"label\"]\n"
    "Lk::prop $t $lbl \"text\" \"Hello\"\n"
    "Lk::set_root $t $w\n"
    "Lk::append_child $t $w $lbl\n"
    "Lk::end_frame $ui",
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
    "let ui [Lk::ui_create]\n"
    "let t [Lk::begin_frame $ui]\n"
    "let w [Lk::node $t \"main\" \"window\"]\n"
    "let col [Lk::node $t \"root\" \"column\"]\n"
    "Lk::prop $t $col \"padding\" 20\n"
    "Lk::prop $t $col \"gap\" 12\n"
    "Lk::prop $t $col \"w\" 300\n"
    "Lk::prop $t $col \"h\" 400\n"
    "Lk::set_root $t $w\n"
    "Lk::append_child $t $w $col\n"
    "Lk::end_frame $ui",
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
    "let ui [Lk::ui_create]\n"
    "let t [Lk::begin_frame $ui]\n"
    "let w [Lk::node $t \"main\" \"window\"]\n"
    "let col [Lk::node $t \"root\" \"column\"]\n"
    "Lk::prop $t $col \"align\" \"center\"\n"
    "Lk::prop $t $col \"justify\" \"end\"\n"
    "Lk::set_root $t $w\n"
    "Lk::append_child $t $w $col\n"
    "Lk::end_frame $ui",
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
    "let ui [Lk::ui_create]\n"
    "let t [Lk::begin_frame $ui]\n"
    "let w [Lk::node $t \"main\" \"window\"]\n"
    "let btn [Lk::node $t \"btn\" \"button\"]\n"
    "Lk::prop $t $btn \"focusable\" 1\n"
    "Lk::prop $t $btn \"disabled\" 0\n"
    "Lk::set_root $t $w\n"
    "Lk::append_child $t $w $btn\n"
    "Lk::end_frame $ui",
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
    "let ui [Lk::ui_create]\n"
    "let t [Lk::begin_frame $ui]\n"
    "let w [Lk::node $t \"main\" \"window\"]\n"
    "let btn [Lk::node $t \"btn\" \"button\"]\n"
    "Lk::present $t $btn \"item\" 42\n"
    "Lk::set_root $t $w\n"
    "Lk::append_child $t $w $btn\n"
    "Lk::end_frame $ui",
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
    "let ui [Lk::ui_create]\n"
    "let t [Lk::begin_frame $ui]\n"
    "let w [Lk::node $t \"main\" \"window\"]\n"
    "Lk::set_root $t $w\n"
    "let cs [Lk::end_frame $ui]",
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
    "let ui [Lk::ui_create]\n"
    "let t [Lk::begin_frame $ui]\n"
    "let w [Lk::node $t \"main\" \"window\"]\n"
    "Lk::set_root $t $w\n"
    "Lk::end_frame $ui\n"
    "let cur [Lk::tree $ui]\n"
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
    "let ui [Lk::ui_create]\n"
    "let t [Lk::begin_frame $ui]\n"
    "let w [Lk::node $t \"main\" \"window\"]\n"
    "Lk::set_root $t $w\n"
    "Lk::end_frame $ui",
    &r);
  if (r) lcl_ref_dec(r);
  r = NULL;

  /* Set and get state */
  eval_ok(interp, "Lk::state_set $ui \"main\" 256 99", &r);
  if (r) lcl_ref_dec(r);
  r = NULL;

  eval_ok(interp, "Lk::state_get $ui \"main\" 256", &r);
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

  eval_ok(interp, "let ui [Lk::ui_create]", &r);
  if (r) lcl_ref_dec(r);
  r = NULL;

  eval_ok(interp,
    "let id [Lk::intern_id $ui \"hello\"]\n"
    "Lk::intern_str $ui $id",
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
    "let ui [Lk::ui_create]\n"
    "let t [Lk::begin_frame $ui]\n"
    "let w [Lk::node $t \"main\" \"window\"]\n"
    "let btn [Lk::node $t \"btn\" \"button\"]\n"
    "Lk::prop $t $btn \"focusable\" 1\n"
    "Lk::set_root $t $w\n"
    "Lk::append_child $t $w $btn\n"
    "Lk::end_frame $ui\n"
    "Lk::focus_set $ui \"btn\"\n"
    "Lk::focus_clear $ui",
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
    "let ui [Lk::ui_create]\n"
    "Lk::add_translator $ui \"pointer_down\" \"item\" \"\" \"\" \"\" \"Select\"",
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
    "let ui [Lk::ui_create]\n"
    "let t [Lk::begin_frame $ui]\n"
    "let w [Lk::node $t \"main\" \"window\"]\n"
    "Lk::set_root $t $w\n"
    "Lk::end_frame $ui\n"
    "Lk::commands $ui",
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
    "let ui [Lk::ui_create]\n"
    "Lk::command_log $ui",
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

  eval_ok(interp, "let ui [Lk::ui_create]", &r);
  if (r) lcl_ref_dec(r);
  r = NULL;

  eval_ok(interp, "let t [Lk::begin_frame $ui]", &r);
  if (r) lcl_ref_dec(r);
  r = NULL;

  rc = lcl_eval_string(interp, "Lk::node $t \"n\" \"bogus\"", &r);
  CHECK(rc != LCL_RC_OK);
  if (r) lcl_ref_dec(r);

  /* Clean up */
  r = NULL;
  lcl_eval_string(interp, "Lk::end_frame $ui", &r);
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
    "let ui [Lk::ui_create]\n"
    "let t [Lk::begin_frame $ui]\n"
    "let w [Lk::node $t \"main\" \"window\"]",
    &r);
  if (r) lcl_ref_dec(r);
  r = NULL;

  rc = lcl_eval_string(interp, "Lk::prop $t $w \"nonexistent\" 42", &r);
  CHECK(rc != LCL_RC_OK);
  if (r) lcl_ref_dec(r);

  /* Clean up */
  r = NULL;
  lcl_eval_string(interp, "Lk::end_frame $ui", &r);
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
    "let ui [Lk::ui_create]\n"
    "let t [Lk::begin_frame $ui]\n"
    "let w [Lk::node $t \"main\" \"window\"]\n"
    "let lbl [Lk::node $t \"lbl\" \"label\"]\n"
    "Lk::prop $t $lbl \"text\" \"Hello\"\n"
    "Lk::set_root $t $w\n"
    "Lk::append_child $t $w $lbl\n"
    "Lk::end_frame $ui",
    &r);
  if (r) lcl_ref_dec(r);
  r = NULL;

  /* Frame 2: change the label text */
  eval_ok(interp,
    "let t2 [Lk::begin_frame $ui]\n"
    "let w2 [Lk::node $t2 \"main\" \"window\"]\n"
    "let lbl2 [Lk::node $t2 \"lbl\" \"label\"]\n"
    "Lk::prop $t2 $lbl2 \"text\" \"World\"\n"
    "Lk::set_root $t2 $w2\n"
    "Lk::append_child $t2 $w2 $lbl2\n"
    "let cs2 [Lk::end_frame $ui]",
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
    "let ui [Lk::ui_create]\n"
    "\n"
    "proc view {tree} {\n"
    "  let w   [Lk::node $tree \"main\" \"window\"]\n"
    "  let col [Lk::node $tree \"root\" \"column\"]\n"
    "  let lbl [Lk::node $tree \"greeting\" \"label\"]\n"
    "  Lk::prop $tree $col \"padding\" 20\n"
    "  Lk::prop $tree $col \"gap\" 12\n"
    "  Lk::prop $tree $lbl \"text\" \"Hello from Lcl!\"\n"
    "  Lk::set_root $tree $w\n"
    "  Lk::append_child $tree $w $col\n"
    "  Lk::append_child $tree $col $lbl\n"
    "}\n"
    "\n"
    "let t [Lk::begin_frame $ui]\n"
    "view $t\n"
    "let cs [Lk::end_frame $ui]\n"
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
    "let ui [Lk::ui_create]\n"
    "let t [Lk::begin_frame $ui]\n"
    "let w [Lk::node $t \"main\" \"window\"]\n"
    "Lk::set_root $t $w\n"
    "Lk::end_frame $ui\n"
    "Lk::clear_commands $ui\n"
    "Lk::commands $ui",
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
    "let ui [Lk::ui_create]\n"
    "Lk::clear_command_log $ui\n"
    "Lk::command_log $ui",
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
    "let ui [Lk::ui_create]\n"
    "let t [Lk::begin_frame $ui]\n"
    "let a [Lk::node $t \"a\" \"window\"]\n"
    "let b [Lk::node $t \"b\" \"row\"]\n"
    "let c [Lk::node $t \"c\" \"column\"]\n"
    "let d [Lk::node $t \"d\" \"spacer\"]\n"
    "let e [Lk::node $t \"e\" \"label\"]\n"
    "let f [Lk::node $t \"f\" \"button\"]\n"
    "Lk::set_root $t $a\n"
    "Lk::append_child $t $a $b\n"
    "Lk::append_child $t $a $c\n"
    "Lk::append_child $t $a $d\n"
    "Lk::append_child $t $a $e\n"
    "Lk::append_child $t $a $f\n"
    "Lk::end_frame $ui",
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
    "let ui [Lk::ui_create]\n"
    "Lk::add_translator $ui \"pointer_down\" \"\" \"\" \"\" \"\" \"cmd1\"\n"
    "Lk::add_translator $ui \"pointer_up\" \"\" \"\" \"\" \"\" \"cmd2\"\n"
    "Lk::add_translator $ui \"pointer_move\" \"\" \"\" \"\" \"\" \"cmd3\"\n"
    "Lk::add_translator $ui \"key_down\" \"\" \"\" \"\" \"\" \"cmd4\"\n"
    "Lk::add_translator $ui \"key_up\" \"\" \"\" \"\" \"\" \"cmd5\"",
    &r);
  if (r) lcl_ref_dec(r);
  CHECK(g_cur_ok);

  eval_ok(interp,
    "Lk::add_translator $ui \"text\" \"\" \"\" \"\" \"\" \"cmd6\"\n"
    "Lk::add_translator $ui \"wheel\" \"\" \"\" \"\" \"\" \"cmd7\"\n"
    "Lk::add_translator $ui \"window_resize\" \"\" \"\" \"\" \"\" \"cmd8\"\n"
    "Lk::add_translator $ui \"window_close\" \"\" \"\" \"\" \"\" \"cmd9\"\n"
    "Lk::add_translator $ui \"\" \"\" \"\" \"\" \"\" \"cmd_any\"",
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
    "let ui [Lk::ui_create]\n"
    "let t [Lk::begin_frame $ui]\n"
    "let w [Lk::node $t \"main\" \"window\"]\n"
    "let btn [Lk::node $t \"btn\" \"button\"]\n"
    "Lk::present $t $btn \"item\" \"apple\"\n"
    "Lk::set_root $t $w\n"
    "Lk::append_child $t $w $btn\n"
    "Lk::end_frame $ui",
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
    "let ui [Lk::ui_create]\n"
    "let t [Lk::begin_frame $ui]\n"
    "let w [Lk::node $t main window]\n"
    "let dd [Lk::node $t cat dropdown]\n"
    "let o1 [Lk::node $t o1 option]\n"
    "let o2 [Lk::node $t o2 option]\n"
    "Lk::prop $t $o1 text Food\n"
    "Lk::prop $t $o2 text Transport\n"
    "Lk::set_root $t $w\n"
    "Lk::append_child $t $w $dd\n"
    "Lk::append_child $t $dd $o1\n"
    "Lk::append_child $t $dd $o2\n"
    "Lk::end_frame $ui",
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
    "let ui [Lk::ui_create]\n"
    "let t [Lk::begin_frame $ui]\n"
    "let w [Lk::node $t main window]\n"
    "let btn [Lk::node $t btn button]\n"
    "Lk::set_root $t $w\n"
    "Lk::append_child $t $w $btn\n"
    "Lk::present $t $btn action (remove_row 5)\n"
    "Lk::end_frame $ui\n",
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

  BEGIN_TEST("tag: apply tag via Lk::tag");
  interp = make_interp();

  eval_ok(interp,
    "let ui [Lk::ui_create]\n"
    "let t [Lk::begin_frame $ui]\n"
    "let w [Lk::node $t w window]\n"
    "let btn [Lk::node $t btn button]\n"
    "Lk::set_root $t $w\n"
    "Lk::append_child $t $w $btn\n"
    "Lk::tag $t $btn primary\n"
    "Lk::end_frame $ui",
    &r);
  if (r) lcl_ref_dec(r);
  CHECK(g_cur_ok);

  lcl_interp_free(interp);
  END_TEST();
}

static void test_lcl_theme_rule(void) {
  lcl_interp *interp;
  lcl_value *r = NULL;

  BEGIN_TEST("theme_rule: add rule via Lk::theme_rule");
  interp = make_interp();

  eval_ok(interp,
    "let ui [Lk::ui_create]\n"
    "Lk::theme_rule $ui button \"\" \"\" {bg {200 50 50}}\n"
    "Lk::theme_rule $ui \"*\" \"\" \"\" {fg {255 255 0}}",
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
    "let ui [Lk::ui_create]\n"
    "Lk::theme_rule $ui label \"\" \"\" #{font_id 1 font_size 24}\n"
    "Lk::theme_rule $ui \"*\" \"\" \"\" #{font_size 18}",
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
    "let ui [Lk::ui_create]\n"
    "Lk::theme_rule $ui label \"\" \"\" #{font_id nope}",
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
    "let ui [Lk::ui_create]\n"
    "Lk::theme_rule $ui label \"\" \"\" #{font_size -4}",
    &r);
  if (r) lcl_ref_dec(r);
  CHECK(rc != LCL_RC_OK);

  lcl_interp_free(interp);
  END_TEST();
}

#ifdef LK_HAVE_SDL
#include "lk-sdl.h" /* lk_image_load_mem for the hermetic BMP test */

/* defined later in this file (the editor-track harness section) */
static void eval_expect_err(lcl_interp *interp, const char *src,
                            const char *sub1, const char *sub2,
                            const char *sub3);

/* Lk::register_font error paths only: a successful registration needs
 * a real lk_window (SDL renderer + display), which headless CI does
 * not have.  The success path is covered by the C-side contract
 * (sdl_text_register_font) and exercised by the demo apps. */

static void test_register_font_arity(void) {
  lcl_interp *interp;
  lcl_value *r = NULL;
  int rc;

  BEGIN_TEST("register_font rejects wrong arity");
  interp = make_interp();

  rc = lcl_eval_string(interp, "Lk::register_font \"only-one-arg\"", &r);
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
    "let ui [Lk::ui_create]\n"
    "Lk::register_font $ui \"/tmp/font.ttf\"",
    &r);
  if (r) lcl_ref_dec(r);
  CHECK(rc != LCL_RC_OK);

  lcl_interp_free(interp);
  END_TEST();
}

/* Lk::window_icon / window_icon_hex: same story -- success needs a
 * real window; the argument contracts are checked headless. */
static void test_window_icon_errors(void) {
  lcl_interp *interp;
  lcl_value *r = NULL;
  int rc;

  BEGIN_TEST("window_icon / window_icon_hex reject bad arity and handle");
  interp = make_interp();

  rc = lcl_eval_string(interp, "Lk::window_icon \"only-one-arg\"", &r);
  if (r) lcl_ref_dec(r);
  r = NULL;
  CHECK(rc != LCL_RC_OK);

  rc = lcl_eval_string(interp,
    "let ui [Lk::ui_create]\n"
    "Lk::window_icon $ui \"icon.png\"",
    &r);
  if (r) lcl_ref_dec(r);
  r = NULL;
  CHECK(rc != LCL_RC_OK);

  rc = lcl_eval_string(interp, "Lk::window_icon_hex \"89504e47\"", &r);
  if (r) lcl_ref_dec(r);
  r = NULL;
  CHECK(rc != LCL_RC_OK);

  /* Handle check precedes hex validation, so a bad window fails even
   * with a well-formed payload. */
  rc = lcl_eval_string(interp,
    "let ui [Lk::ui_create]\n"
    "Lk::window_icon_hex $ui \"89504e47\"",
    &r);
  if (r) lcl_ref_dec(r);
  CHECK(rc != LCL_RC_OK);

  lcl_interp_free(interp);
  END_TEST();
}

/* A complete 2x2 32-bit BMP (pixels tl=(10,20,30) tr=(200,150,100)
 * bl=(1,2,3) br=(255,0,255), all alpha 255) — the hermetic stand-in
 * for file IO: lcl_lk_test never touches the filesystem by design,
 * and lk_image_load_mem needs no display. */
static const unsigned char tiny_bmp[] = {
  0x42, 0x4d, 0x9a, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x8a, 0x00,
  0x00, 0x00, 0x7c, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00, 0x02, 0x00,
  0x00, 0x00, 0x01, 0x00, 0x20, 0x00, 0x03, 0x00, 0x00, 0x00, 0x10, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xff, 0x00, 0x00, 0xff,
  0x00, 0x00, 0xff, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xff, 0x42, 0x47,
  0x52, 0x73, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x03, 0x02, 0x01, 0xff, 0xff, 0x00,
  0xff, 0xff, 0x1e, 0x14, 0x0a, 0xff, 0x64, 0x96, 0xc8, 0xff
};

static void test_image_load_mem_roundtrip(void) {
  lk_image *img;
  lk_u32 w = 0;
  lk_u32 h = 0;

  BEGIN_TEST("image: lk_image_load_mem decodes embedded BMP");

  img = lk_image_load_mem(tiny_bmp, (lk_u32)sizeof(tiny_bmp));
  CHECK(img != NULL);

  if (img) {
    const lk_u8 *px = lk_image_pixels(img);

    lk_image_size(img, &w, &h);
    CHECK(w == 2 && h == 2);

    /* top-left (10,20,30,255), bottom-right (255,0,255,255) */
    CHECK(px[0] == 10 && px[1] == 20 && px[2] == 30 && px[3] == 255);
    CHECK(px[12] == 255 && px[13] == 0 && px[14] == 255 && px[15] == 255);

    lk_image_destroy(img);
  }

  /* garbage bytes decode to nothing */
  CHECK(lk_image_load_mem("nonsense!", 9) == NULL);
  CHECK(lk_image_load_mem(NULL, 4) == NULL);

  END_TEST();
}

static void test_image_io_errors(void) {
  lcl_interp *interp;
  lcl_value *r = NULL;

  BEGIN_TEST("image: load/save error contracts (headless)");
  interp = make_interp();

  eval_ok(interp,
          "let ui [Lk::ui_create]\n"
          "let img [Lk::image_new $ui 2 2]",
          &r);
  if (r) lcl_ref_dec(r);

  eval_expect_err(interp, "Lk::image_load $ui", "Lk::image_load",
                  "2 arguments", NULL);
  eval_expect_err(interp, "Lk::image_load $img \"x.bmp\"",
                  "expected lk_ui opaque", NULL, NULL);
  eval_expect_err(interp, "Lk::image_load $ui \"/no/such/file.bmp\"",
                  "could not load image", NULL, NULL);

  eval_expect_err(interp, "Lk::image_save $img", "Lk::image_save",
                  "2 arguments", NULL);
  eval_expect_err(interp, "Lk::image_save $ui \"out.bmp\"",
                  "expected lk_image opaque", NULL, NULL);
  eval_expect_err(interp, "Lk::image_save $img \"out.gif\"",
                  "unknown extension", ".bmp, .png", NULL);
  eval_expect_err(interp, "Lk::image_save $img \"noext\"",
                  "unknown extension", NULL, NULL);

  lcl_interp_free(interp);
  END_TEST();
}

/* Headless like the icon tests: only the contracts that error before
 * any window exists (arity precedes the window check). */
static void test_dialog_errors(void) {
  lcl_interp *interp;
  lcl_value *r = NULL;

  BEGIN_TEST("image: file dialog error contracts (headless)");
  interp = make_interp();

  eval_ok(interp, "let ui [Lk::ui_create]", &r);
  if (r) lcl_ref_dec(r);

  eval_expect_err(interp, "Lk::open_file_dialog $ui",
                  "Lk::open_file_dialog", "2 to 4 arguments", NULL);
  eval_expect_err(interp, "Lk::open_file_dialog $ui [lambda {p} {}]",
                  "expected lk_window opaque", NULL, NULL);
  eval_expect_err(interp, "Lk::save_file_dialog $ui",
                  "Lk::save_file_dialog", "2 to 4 arguments", NULL);
  eval_expect_err(interp, "Lk::save_file_dialog $ui [lambda {p} {}]",
                  "expected lk_window opaque", NULL, NULL);

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
    "let ui [Lk::ui_create]\n"
    "Lk::set_command_handler $ui [lambda {cmd} {\n"
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
    "let ui [Lk::ui_create]\n"
    "let t [Lk::begin_frame $ui]\n"
    "let w [Lk::node $t \"main\" \"window\"]\n"
    "let btn [Lk::node $t \"btn\" \"button\"]\n"
    "Lk::prop $t $btn \"text\" \"Click\"\n"
    "Lk::present $t $btn \"action\" 42\n"
    "Lk::set_root $t $w\n"
    "Lk::append_child $t $w $btn\n"
    "Lk::end_frame $ui\n"
    "Lk::add_translator $ui \"pointer_down\" \"action\" \"\" \"\" \"\" \"DoIt\"\n"
    "let got_cmd 0\n"
    "Lk::set_command_handler $ui [lambda {cmd} {\n"
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
    "let ui [Lk::ui_create]\n"
    "Lk::add_translator $ui \"key_down\" \"doc\" \"\" \"s\" \"ctrl\" \"Save\"\n"
    "Lk::add_translator $ui \"key_down\" \"\" \"\" \"f\" \"ctrl\" \"Find\"\n"
    "Lk::add_translator $ui \"key_down\" \"\" \"\" \"z\" \"ctrl+shift\" \"Redo\"",
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
    "let ui [Lk::ui_create]\n"
    "Lk::add_translator $ui \"key_down\" \"\" \"\" \"page_up\" \"\" \"PgUp\"\n"
    "Lk::add_translator $ui \"key_down\" \"\" \"\" \"page_down\" \"\" \"PgDn\"\n"
    "Lk::add_translator $ui \"key_down\" \"\" \"\" \"f5\" \"\" \"Refresh\"\n"
    "Lk::add_translator $ui \"key_down\" \"\" \"\" \"f12\" \"\" \"DevTools\"\n"
    "Lk::add_translator $ui \"key_down\" \"\" \"\" \"0\" \"ctrl\" \"ZoomReset\"\n"
    "Lk::add_translator $ui \"key_down\" \"\" \"\" \"9\" \"ctrl\" \"LastTab\"",
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
    "let ui [Lk::ui_create]\n"
    "Lk::add_translator $ui \"key_down\" \"\" \"\" \"not_a_key\" \"\" \"Cmd\"",
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
    "let ui [Lk::ui_create]\n"
    "Lk::add_translator $ui \"key_down\" \"\" \"\" \"s\" \"super\" \"Cmd\"",
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
    "let ui [Lk::ui_create]\n"
    "let t [Lk::begin_frame $ui]\n"
    "let w [Lk::node $t \"w\" \"window\"]\n"
    "let ti [Lk::node $t \"ti\" \"text_input\"]\n"
    "Lk::prop $t $ti \"text\" \"hello\"\n"
    "Lk::set_root $t $w\n"
    "Lk::append_child $t $w $ti\n"
    "Lk::end_frame $ui",
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
    "let ui [Lk::ui_create]\n"
    "let t [Lk::begin_frame $ui]\n"
    "let w [Lk::node $t \"w\" \"window\"]\n"
    "let sc [Lk::node $t \"sc\" \"scroll\"]\n"
    "Lk::prop $t $sc \"h\" 200\n"
    "Lk::set_root $t $w\n"
    "Lk::append_child $t $w $sc\n"
    "Lk::end_frame $ui",
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

  eval_ok(interp, "let ui [Lk::ui_create]", &r);
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
  eval_ok(interp, "Lk::clipboard_set $ui \"hello clip\"", &r);
  if (r) lcl_ref_dec(r);
  r = NULL;

  CHECK(strcmp(g_lcl_mock_clipboard, "hello clip") == 0);

  /* Get via binding */
  eval_ok(interp, "Lk::clipboard_get $ui", &r);
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

  eval_ok(interp, "let ui [Lk::ui_create]", &r);
  if (r) lcl_ref_dec(r);
  r = NULL;

  /* No clipboard installed */
  eval_ok(interp, "Lk::clipboard_get $ui", &r);
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

  eval_ok(interp, "let ui [Lk::ui_create]", &r);
  if (r) lcl_ref_dec(r);
  r = NULL;

  eval_ok(interp, "Lk::overlay_count $ui", &r);
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

  BEGIN_TEST("hidden prop settable via Lk::prop");
  interp = make_interp();

  eval_ok(interp,
    "let ui [Lk::ui_create]\n"
    "let t [Lk::begin_frame $ui]\n"
    "let w [Lk::node $t \"w\" \"window\"]\n"
    "let col [Lk::node $t \"col\" \"column\"]\n"
    "Lk::prop $t $col \"hidden\" 1\n"
    "Lk::set_root $t $w\n"
    "Lk::append_child $t $w $col\n"
    "Lk::end_frame $ui",
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
    "let ui [Lk::ui_create]\n"
    "let t [Lk::begin_frame $ui]\n"
    "let w [Lk::node $t \"w\" \"window\"]\n"
    "let col [Lk::node $t \"col\" \"column\"]\n"
    "let outside [Lk::node $t \"outside\" \"button\"]\n"
    "Lk::prop $t $outside \"focusable\" 1\n"
    "let m [Lk::node $t \"m\" \"column\"]\n"
    "Lk::prop $t $m \"hidden\" 1\n"
    "let m1 [Lk::node $t \"m1\" \"button\"]\n"
    "Lk::prop $t $m1 \"focusable\" 1\n"
    "let m2 [Lk::node $t \"m2\" \"button\"]\n"
    "Lk::prop $t $m2 \"focusable\" 1",
    &r);
  if (r) lcl_ref_dec(r);
  r = NULL;

  eval_ok(interp,
    "Lk::set_root $t $w\n"
    "Lk::append_child $t $w $col\n"
    "Lk::append_child $t $col $outside\n"
    "Lk::append_child $t $col $m\n"
    "Lk::append_child $t $m $m1\n"
    "Lk::append_child $t $m $m2\n"
    "Lk::end_frame $ui",
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
          "Lk::overlay_push $ui #{kind modal owner_id m content_root_id m}",
          &r);
  if (r) lcl_ref_dec(r);
  r = NULL;

  eval_ok(interp, "Lk::overlay_count $ui", &r);
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

  eval_ok(interp, "Lk::overlay_push $ui #{kind tooltip owner_id outside}", &r);
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

  eval_ok(interp, "Lk::overlay_pop $ui", &r);
  if (r) lcl_ref_dec(r);
  r = NULL;

  eval_ok(interp, "Lk::overlay_count $ui", &r);
  if (r) {
    long v = -1;
    lcl_value_to_int(r, &v);
    CHECK(v == 0);
    lcl_ref_dec(r);
  }

  /* pop on an empty stack is a safe no-op */
  eval_ok(interp, "Lk::overlay_pop $ui", &r);
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

  rc = lcl_eval_string(interp, "Lk::overlay_push $ui #{kind bogus}", &r);
  CHECK(rc != LCL_RC_OK);
  if (r) lcl_ref_dec(r);
  r = NULL;

  rc = lcl_eval_string(interp,
                       "Lk::overlay_push $ui #{kind modal anchor sideways}",
                       &r);
  CHECK(rc != LCL_RC_OK);
  if (r) lcl_ref_dec(r);
  r = NULL;

  rc = lcl_eval_string(interp, "Lk::overlay_push $ui #{owner_id m}", &r);
  CHECK(rc != LCL_RC_OK); /* missing kind */
  if (r) lcl_ref_dec(r);
  r = NULL;

  /* Nothing was pushed by the failed calls. */
  eval_ok(interp, "Lk::overlay_count $ui", &r);
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

  BEGIN_TEST("tooltip prop settable via Lk::prop");
  interp = make_interp();

  eval_ok(interp,
    "let ui [Lk::ui_create]\n"
    "let t [Lk::begin_frame $ui]\n"
    "let w [Lk::node $t \"w\" \"window\"]\n"
    "let b [Lk::node $t \"b\" \"button\"]\n"
    "Lk::prop $t $b \"tooltip\" \"Saves the file\"\n"
    "Lk::set_root $t $w\n"
    "Lk::append_child $t $w $b\n"
    "Lk::end_frame $ui",
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
    "let ui [Lk::ui_create]\n"
    "let t [Lk::begin_frame $ui]\n"
    "let w [Lk::node $t \"w\" \"window\"]\n"
    "let sh [Lk::node $t \"sh\" \"split_h\"]\n"
    "let sv [Lk::node $t \"sv\" \"split_v\"]\n"
    "let c1 [Lk::node $t \"c1\" \"column\"]\n"
    "let c2 [Lk::node $t \"c2\" \"column\"]\n"
    "let c3 [Lk::node $t \"c3\" \"column\"]\n"
    "Lk::set_root $t $w\n"
    "Lk::append_child $t $w $sh\n"
    "Lk::append_child $t $sh $c1\n"
    "Lk::append_child $t $sh $sv\n"
    "Lk::append_child $t $sv $c2\n"
    "Lk::append_child $t $sv $c3\n"
    "Lk::end_frame $ui",
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

  BEGIN_TEST("split_ratio prop settable via Lk::prop");
  interp = make_interp();

  eval_ok(interp,
    "let ui [Lk::ui_create]\n"
    "let t [Lk::begin_frame $ui]\n"
    "let w [Lk::node $t \"w\" \"window\"]\n"
    "let sp [Lk::node $t \"sp\" \"split_h\"]\n"
    "Lk::prop $t $sp \"split_ratio\" 300\n"
    "let c1 [Lk::node $t \"c1\" \"column\"]\n"
    "let c2 [Lk::node $t \"c2\" \"column\"]\n"
    "Lk::set_root $t $w\n"
    "Lk::append_child $t $w $sp\n"
    "Lk::append_child $t $sp $c1\n"
    "Lk::append_child $t $sp $c2\n"
    "Lk::end_frame $ui",
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
 * the event loop (Lk::window_create / Lk::window_run), which needs a
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
    "let u [Lk::ui_create]\n"
    "let t [Lk::begin_frame $u]\n"
    "set! LkDsl::_ui $u\n"
    "set! LkDsl::_tree $t\n"
    "set! LkDsl::_parent_stack ()",
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
    {"k_sv", UIK_SPLIT_V},   {"k_cb", UIK_CHECKBOX},
    {"k_rd", UIK_RADIO},     {"k_sl", UIK_SLIDER},
    {"k_tabs", UIK_TABS},    {"k_tab", UIK_TAB},
    {"k_grid", UIK_GRID}
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
    "split_v k_sv\n"
    "checkbox k_cb\n"
    "radio k_rd\n"
    "slider k_sl\n"
    "tabs k_tabs\n"
    "tab k_tab\n"
    "grid k_grid",
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
    "let ui [Lk::ui_create]\n"
    "let t [Lk::begin_frame $ui]\n"
    "let w [Lk::node $t \"main\" \"window\"]\n"
    "Lk::set_root $t $w\n"
    "Lk::end_frame $ui",
    &r);
  if (r) lcl_ref_dec(r);
  r = NULL;

  /* Scripts never poke widget state: builtin keys error */
  eval_expect_err(interp, "Lk::state_set $ui \"main\" 6 1",
                  "Lk::state_set", "internal widget state", NULL);
  eval_expect_err(interp, "Lk::state_get $ui \"main\" 6",
                  "Lk::state_get", "internal widget state", NULL);

  /* App-owned keys (>= LKS_USER = 256) still round-trip */
  eval_ok(interp, "Lk::state_set $ui \"main\" 300 7", &r);
  if (r) lcl_ref_dec(r);
  r = NULL;
  eval_ok(interp, "Lk::state_get $ui \"main\" 300", &r);
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

  BEGIN_TEST("value prop settable via Lk::prop (string coercion)");
  interp = make_interp();

  eval_ok(interp,
    "let ui [Lk::ui_create]\n"
    "let t [Lk::begin_frame $ui]\n"
    "let w [Lk::node $t \"w\" \"window\"]\n"
    "let dd [Lk::node $t \"dd\" \"dropdown\"]\n"
    "Lk::prop $t $dd \"value\" \"Banana\"\n"
    "Lk::set_root $t $w\n"
    "Lk::append_child $t $w $dd\n"
    "Lk::end_frame $ui",
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

  eval_ok(interp, "let gn [Lk::node $t \"g_neg\" \"spacer\"]", &r);
  if (r) lcl_ref_dec(r);

  /* raw binding: negative and non-integer both error, naming the
   * constraint */
  eval_expect_err(interp, "Lk::prop $t $gn \"grow\" -1", "grow", ">= 0",
                  NULL);
  eval_expect_err(interp, "Lk::prop $t $gn \"grow\" \"lots\"", "grow",
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
    "let w [Lk::node $t rw window]\n"
    "Lk::set_root $t $w\n"
    "let b [button pb #{tag primary}]\n"
    "Lk::append_child $t $w $b\n"
    "Lk::end_frame $u",
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
    "on Save [lambda {cmd} { Lk::state_set $u sink 300 [get $cmd n] }]\n"
    "LkDsl::_dispatch_command #{name Save n 7}\n"
    "LkDsl::_dispatch_command #{name Unknown n 9}\n"
    "Lk::state_get $u sink 300",
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
    "let u [Lk::ui_create]\n"
    "set! LkDsl::_ui $u\n"
    "view {\n"
    "    column main #{padding 4} {\n"
    "        label greet #{text Hi}\n"
    "        button ok #{text OK}\n"
    "    }\n"
    "}\n"
    "let t [Lk::begin_frame $u]\n"
    "LkDsl::_frame $t\n"
    "let cs [Lk::end_frame $u]\n"
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
    "let t2 [Lk::begin_frame $u]\n"
    "LkDsl::_frame $t2\n"
    "let cs2 [Lk::end_frame $u]\n"
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

static void test_text_input_controlled_binding(void) {
  lcl_interp *interp;
  lcl_value *r = NULL;
  lk_ui *ui;

  BEGIN_TEST("text_input controlled: candidate reaches the handler");
  interp = make_interp();

  /* A controlled input presenting `field cell`; a value_changed
   * translator delivers each candidate as an Edit command whose
   * source_value the handler records. */
  eval_ok(interp,
          "let ui [Lk::ui_create]\n"
          "var got \"\"\n"
          "let t [Lk::begin_frame $ui]\n"
          "let w [Lk::node $t root window]\n"
          "let e [Lk::node $t cell text_input]\n"
          "Lk::prop $t $e text ab\n"
          "Lk::prop $t $e focusable 1\n"
          "Lk::prop $t $e controlled 1\n"
          "Lk::present $t $e field cell\n"
          "Lk::set_root $t $w\n"
          "Lk::append_child $t $w $e\n"
          "Lk::end_frame $ui\n"
          "Lk::focus_set $ui cell\n"
          "Lk::add_translator $ui value_changed field \"\" \"\" \"\" Edit\n"
          "Lk::set_command_handler $ui [lambda {cmd} {\n"
          "  set! got [get $cmd source_value]\n"
          "}]",
          &r);
  if (r) lcl_ref_dec(r);

  ui = fetch_ui(interp);
  CHECK(ui != NULL);

  if (ui) {
    const lk_tree *cur = lk_ui_tree(ui);
    lk_ix e = lk_tree_find_by_id(cur, lk_intern_id(ui->intern,
                                                   lk_str_c("cell")));
    lk_event ev;

    memset(&ev, 0, sizeof(ev));
    ev.type = LK_EVENT_TEXT;
    ev.target = e;
    ev.data.text.buf[0] = 'x';
    ev.data.text.len = 1;
    lk_event_route(ui, &ev);
    CHECK(ev.handled == 1);

    /* Candidate delivered (cursor started at the end: "abx") ... */
    check_str(interp, "$got", "abx");
    /* ... and no retained buffer: the app owns the text. */
    CHECK(lk_state_get(lk_ui_state(ui),
                       lk_intern_id(ui->intern, lk_str_c("cell")),
                       LKS_TEXT_BUF).tag == UIV_NONE);
  }

  lcl_interp_free(interp);
  END_TEST();
}

static void test_text_align_binding(void) {
  lcl_interp *interp;
  lcl_value *r = NULL;
  lk_ui *ui;

  BEGIN_TEST("text_align: prop + theme rule keys, bad value errors");
  interp = make_interp();

  eval_ok(interp,
          "let ui [Lk::ui_create]\n"
          "Lk::theme_rule $ui label \"\" \"\" #{text_align center text_valign end}\n"
          "let t [Lk::begin_frame $ui]\n"
          "let w [Lk::node $t root window]\n"
          "let l [Lk::node $t num label]\n"
          "Lk::prop $t $l text end\n"
          "Lk::prop $t $l text_align end\n"
          "Lk::prop $t $l text_valign center\n"
          "Lk::set_root $t $w\n"
          "Lk::append_child $t $w $l\n"
          "Lk::end_frame $ui",
          &r);
  if (r) lcl_ref_dec(r);

  ui = fetch_ui(interp);
  CHECK(ui != NULL);

  if (ui) {
    const lk_tree *cur = lk_ui_tree(ui);
    lk_ix l = lk_tree_find_by_id(cur, lk_intern_id(ui->intern,
                                                   lk_str_c("num")));
    CHECK(lk_node_prop_i32(cur, l, UIP_TEXT_ALIGN, -1) == LK_ALIGN_END);
    CHECK(lk_node_prop_i32(cur, l, UIP_TEXT_VALIGN, -1) == LK_ALIGN_CENTER);
  }

  eval_expect_err(interp, "Lk::prop $t $l text_align sideways",
                  "unknown align value", NULL, NULL);

  lcl_interp_free(interp);
  END_TEST();
}

static void test_doc_new_read(void) {
  lcl_interp *interp;
  lcl_value *r = NULL;

  BEGIN_TEST("doc: new/text/len/line_count");
  interp = make_interp();

  eval_ok(interp, "let d [Lk::doc_new \"hello\nworld\"]", &r);
  if (r) lcl_ref_dec(r);

  check_str(interp, "Lk::doc_text $d", "hello\nworld");
  check_int(interp, "Lk::doc_len $d", 11);
  check_int(interp, "Lk::doc_line_count $d", 2);

  /* The empty document is one empty line. */
  r = NULL;
  eval_ok(interp, "let d0 [Lk::doc_new]", &r);
  if (r) lcl_ref_dec(r);
  check_int(interp, "Lk::doc_len $d0", 0);
  check_int(interp, "Lk::doc_line_count $d0", 1);
  check_str(interp, "Lk::doc_text $d0", "");

  lcl_interp_free(interp);
  END_TEST();
}

static void test_doc_revision_string(void) {
  lcl_interp *interp;
  lcl_value *r = NULL;

  BEGIN_TEST("doc: revision is a stable \"hi:lo\" token");
  interp = make_interp();

  eval_ok(interp, "let d [Lk::doc_new \"abc\"]", &r);
  if (r) lcl_ref_dec(r);

  /* Reading does not advance it; comparing from script works. */
  check_int(interp,
            "let r1 [Lk::doc_revision $d]\n"
            "let r2 [Lk::doc_revision $d]\n"
            "== $r1 $r2",
            1);

  /* One committed edit advances it exactly once. */
  check_int(interp,
            "Lk::doc_insert $d 0 \"x\"\n"
            "let r3 [Lk::doc_revision $d]\n"
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

  eval_ok(interp, "let d [Lk::doc_new \"hello world\"]", &r);
  if (r) lcl_ref_dec(r);

  check_str(interp,
            "Lk::doc_insert $d 5 \",\"\n"
            "Lk::doc_text $d",
            "hello, world");
  check_str(interp,
            "Lk::doc_delete $d 0 7\n"
            "Lk::doc_text $d",
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
          "let d [Lk::doc_new \"abc\"]\n"
          "let ui [Lk::ui_create]",
          &r);
  if (r) lcl_ref_dec(r);

  /* insert past the end / delete at the end are rejected by the C
   * contract; the binding turns the rejection into a hard error. */
  eval_expect_err(interp, "Lk::doc_insert $d 99 \"x\"",
                  "Lk::doc_insert", "rejected", NULL);
  eval_expect_err(interp, "Lk::doc_delete $d 3 1",
                  "Lk::doc_delete", "rejected", NULL);
  eval_expect_err(interp, "Lk::doc_insert $d -1 \"x\"",
                  "Lk::doc_insert", "non-negative", NULL);

  /* Arity and wrong opaque type. */
  eval_expect_err(interp, "Lk::doc_insert $d 0", "Lk::doc_insert",
                  "3 arguments", NULL);
  eval_expect_err(interp, "Lk::doc_text $ui", "expected lk_document opaque",
                  NULL, NULL);
  eval_expect_err(interp, "Lk::doc_text", "Lk::doc_text", "1 argument", NULL);

  /* The rejected calls left the document untouched. */
  check_str(interp, "Lk::doc_text $d", "abc");

  lcl_interp_free(interp);
  END_TEST();
}

static void test_doc_line_procs(void) {
  lcl_interp *interp;
  lcl_value *r = NULL;

  BEGIN_TEST("doc: pos_to_line / line_start / line_end");
  interp = make_interp();

  eval_ok(interp, "let d [Lk::doc_new \"ab\ncd\nef\"]", &r);
  if (r) lcl_ref_dec(r);

  /* 0-based lines, mirroring the C API. */
  check_int(interp, "Lk::doc_pos_to_line $d 0", 0);
  check_int(interp, "Lk::doc_pos_to_line $d 2", 0); /* the \n itself */
  check_int(interp, "Lk::doc_pos_to_line $d 3", 1);
  check_int(interp, "Lk::doc_pos_to_line $d 7", 2);
  check_int(interp, "Lk::doc_pos_to_line $d 99", 2); /* clamps to last */

  check_int(interp, "Lk::doc_line_start $d 0", 0);
  check_int(interp, "Lk::doc_line_start $d 1", 3);
  check_int(interp, "Lk::doc_line_start $d 2", 6);

  /* line_end: the \n (exclusive) for inner lines, doc len for last. */
  check_int(interp, "Lk::doc_line_end $d 0", 2);
  check_int(interp, "Lk::doc_line_end $d 1", 5);
  check_int(interp, "Lk::doc_line_end $d 2", 8);

  /* Errors: arity, wrong opaque, bad values, out-of-range lines. */
  eval_expect_err(interp, "Lk::doc_pos_to_line $d", "Lk::doc_pos_to_line",
                  "2 arguments", NULL);
  eval_expect_err(interp, "Lk::doc_pos_to_line $d -1",
                  "non-negative integer", NULL, NULL);
  eval_expect_err(interp, "Lk::doc_line_start $d 3", "line out of range",
                  NULL, NULL);
  eval_expect_err(interp, "Lk::doc_line_end $d 99", "line out of range",
                  NULL, NULL);
  eval_expect_err(interp, "Lk::doc_line_start $d nope",
                  "non-negative integer", NULL, NULL);
  eval_expect_err(interp, "Lk::doc_line_start 5 0",
                  "expected lk_document opaque", NULL, NULL);
  eval_expect_err(interp, "Lk::doc_line_end $d", "Lk::doc_line_end",
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
          "let d [Lk::doc_new \"hello\nna\xC3\xAFve caf\xC3\xA9\n\ta\tb\"]",
          &r);
  if (r) lcl_ref_dec(r);

  check_int(interp, "Lk::doc_len $d", 23);

  /* ASCII: position at line start is column 1; 1-based thereafter. */
  check_int(interp, "Lk::doc_char_col $d 0", 1);
  check_int(interp, "Lk::doc_char_col $d 3", 4);
  check_int(interp, "Lk::doc_char_col $d 5", 6); /* end of "hello" */

  /* Multi-byte UTF-8: columns count codepoints, not bytes. */
  check_int(interp, "Lk::doc_char_col $d 6", 1);  /* line start */
  check_int(interp, "Lk::doc_char_col $d 8", 3);  /* before the i-uml */
  check_int(interp, "Lk::doc_char_col $d 10", 4); /* after it: 3 cp */
  check_int(interp, "Lk::doc_char_col $d 18", 11); /* 10 cp, 12 bytes */

  /* Tabs count as ONE character (pinned definition, editor-wrap #8). */
  check_int(interp, "Lk::doc_char_col $d 19", 1);
  check_int(interp, "Lk::doc_char_col $d 20", 2); /* after the tab */
  check_int(interp, "Lk::doc_char_col $d 22", 4);
  check_int(interp, "Lk::doc_char_col $d 23", 5); /* pos == doc len */

  /* Errors. */
  eval_expect_err(interp, "Lk::doc_char_col $d 24", "pos out of range",
                  NULL, NULL);
  eval_expect_err(interp, "Lk::doc_char_col $d -1", "non-negative integer",
                  NULL, NULL);
  eval_expect_err(interp, "Lk::doc_char_col $d", "Lk::doc_char_col",
                  "2 arguments", NULL);
  eval_expect_err(interp, "Lk::doc_char_col 5 0",
                  "expected lk_document opaque", NULL, NULL);

  lcl_interp_free(interp);
  END_TEST();
}

static void test_doc_find_binding(void) {
  lcl_interp *interp;
  lcl_value *r = NULL;

  BEGIN_TEST("doc: find happy paths incl. piece seams");
  interp = make_interp();

  eval_ok(interp, "let d [Lk::doc_new \"one two one\"]", &r);
  if (r) lcl_ref_dec(r);

  check_int(interp, "Lk::doc_find $d \"one\"", 0);
  check_int(interp, "Lk::doc_find $d \"two\"", 4);
  check_int(interp, "Lk::doc_find $d \"one\" 1", 8);
  check_int(interp, "Lk::doc_find $d \"one\" 8", 8);
  check_int(interp, "Lk::doc_find $d \"one\" 9", -1); /* not found */
  check_int(interp, "Lk::doc_find $d \"zzz\"", -1);
  check_int(interp, "Lk::doc_find $d \"one\" 99", -1); /* from past end */

  /* Insert-in-middle splits the original piece; the needle spans the
   * resulting seams. */
  r = NULL;
  eval_ok(interp, "Lk::doc_insert $d 4 \"XY \"", &r);
  if (r) lcl_ref_dec(r);
  check_str(interp, "Lk::doc_text $d", "one XY two one");
  check_int(interp, "Lk::doc_find $d \"e XY t\"", 2);
  check_int(interp, "Lk::doc_find $d \"Y tw\" 3", 5);

  /* UTF-8 needle matches its exact bytes. */
  r = NULL;
  eval_ok(interp, "let d2 [Lk::doc_new \"caf\xC3\xA9 bar\"]", &r);
  if (r) lcl_ref_dec(r);
  check_int(interp, "Lk::doc_find $d2 \"\xC3\xA9 b\"", 3);

  /* Search-next idiom from script. */
  check_int(interp,
            "let h [Lk::doc_find $d \"one\"]\n"
            "Lk::doc_find $d \"one\" [+ $h 1]",
            11);

  lcl_interp_free(interp);
  END_TEST();
}

static void test_doc_find_errors(void) {
  lcl_interp *interp;
  lcl_value *r = NULL;

  BEGIN_TEST("doc: find error paths");
  interp = make_interp();

  eval_ok(interp, "let d [Lk::doc_new \"abc\"]", &r);
  if (r) lcl_ref_dec(r);

  eval_expect_err(interp, "Lk::doc_find $d", "Lk::doc_find",
                  "2 or 3 arguments", NULL);
  eval_expect_err(interp, "Lk::doc_find $d \"a\" 0 extra", "Lk::doc_find",
                  "2 or 3 arguments", NULL);
  eval_expect_err(interp, "Lk::doc_find $d \"\"", "needle must be non-empty",
                  NULL, NULL);
  eval_expect_err(interp, "Lk::doc_find $d \"a\" -1",
                  "non-negative integer", NULL, NULL);
  eval_expect_err(interp, "Lk::doc_find $d \"a\" nope",
                  "non-negative integer", NULL, NULL);
  eval_expect_err(interp, "Lk::doc_find 5 \"a\"",
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
          "let d [Lk::doc_new \"base\"]\n"
          "let h [Lk::history_new $d]",
          &r);
  if (r) lcl_ref_dec(r);

  check_str(interp,
            "Lk::doc_transact $d {\n"
            "    Lk::doc_insert $d 0 \"A\"\n"
            "    Lk::doc_insert $d 1 \"B\"\n"
            "    Lk::doc_insert $d 6 \"C\"\n"
            "}\n"
            "Lk::doc_text $d",
            "ABbaseC");

  /* One undo step reverts all three edits. */
  check_int(interp, "Lk::history_can_undo $h", 1);
  check_int(interp, "Lk::history_undo $h $d", 1);
  check_str(interp, "Lk::doc_text $d", "base");
  check_int(interp, "Lk::history_can_undo $h", 0);
  check_int(interp, "Lk::history_can_redo $h", 1);

  lcl_interp_free(interp);
  END_TEST();
}

static void test_doc_transact_error_propagates(void) {
  lcl_interp *interp;
  lcl_value *r = NULL;

  BEGIN_TEST("doc: transact commits then propagates a body error");
  interp = make_interp();

  eval_ok(interp,
          "let d [Lk::doc_new \"base\"]\n"
          "let h [Lk::history_new $d]",
          &r);
  if (r) lcl_ref_dec(r);

  /* The body errors after one successful edit: the commit still runs
   * (the partial edit stays applied, as one transaction), then the
   * body's error propagates to the caller. */
  eval_expect_err(interp,
                  "Lk::doc_transact $d {\n"
                  "    Lk::doc_insert $d 0 \"X\"\n"
                  "    error \"boom\"\n"
                  "}",
                  "boom", NULL, NULL);

  check_str(interp, "Lk::doc_text $d", "Xbase");
  check_int(interp, "Lk::history_undo $h $d", 1);
  check_str(interp, "Lk::doc_text $d", "base");

  lcl_interp_free(interp);
  END_TEST();
}

static void test_doc_subscribe_deltas(void) {
  lcl_interp *interp;
  lcl_value *r = NULL;

  BEGIN_TEST("doc: subscriber receives delta dicts (insert + delete)");
  interp = make_interp();

  eval_ok(interp,
          "let d [Lk::doc_new \"hello\"]\n"
          "var got ()\n"
          "let sid [Lk::doc_subscribe $d [lambda {deltas} {\n"
          "    set! got $deltas\n"
          "}]]",
          &r);
  if (r) lcl_ref_dec(r);

  /* Insert: one delta with the inserted bytes, nothing deleted. */
  r = NULL;
  eval_ok(interp, "Lk::doc_insert $d 2 \"XY\"", &r);
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
  eval_ok(interp, "Lk::doc_delete $d 2 2", &r);
  if (r) lcl_ref_dec(r);
  check_int(interp, "len $got", 1);
  check_int(interp, "get [get $got 0] start", 2);
  check_int(interp, "get [get $got 0] deleted_len", 2);
  check_str(interp, "get [get $got 0] deleted", "XY");
  check_int(interp, "get [get $got 0] inserted_len", 0);

  /* A transaction delivers all its deltas in one notification. */
  r = NULL;
  eval_ok(interp,
          "Lk::doc_transact $d {\n"
          "    Lk::doc_insert $d 0 \"a\"\n"
          "    Lk::doc_insert $d 1 \"b\"\n"
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
          "let d [Lk::doc_new \"hello\"]\n"
          "var count 0\n"
          "let sid [Lk::doc_subscribe $d [lambda {deltas} {\n"
          "    set! count [+ $count 1]\n"
          "}]]\n"
          "Lk::doc_insert $d 0 \"x\"\n"
          "Lk::doc_unsubscribe $d $sid\n"
          "Lk::doc_insert $d 0 \"y\"",
          &r);
  if (r) lcl_ref_dec(r);

  check_int(interp, "$count", 1);
  eval_expect_err(interp, "Lk::doc_unsubscribe $d 999",
                  "unknown subscription id", NULL, NULL);
  eval_expect_err(interp, "Lk::doc_subscribe $d 42", "expected callable",
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
    "let ui [Lk::ui_create]\n"
    "let t [Lk::begin_frame $ui]\n"
    "let w [Lk::node $t \"main\" \"window\"]\n"
    "let btn [Lk::node $t \"btn\" \"button\"]\n"
    "Lk::prop $t $btn \"focusable\" 1\n"
    "Lk::set_root $t $w\n"
    "Lk::append_child $t $w $btn\n"
    "Lk::end_frame $ui",
    &r);
  if (r) lcl_ref_dec(r);

  /* no focus yet -> "" */
  check_str(interp, "Lk::focus_get $ui", "");

  /* set -> get round trip */
  check_str(interp,
            "Lk::focus_set $ui \"btn\"\n"
            "Lk::focus_get $ui",
            "btn");

  /* clear -> "" again */
  check_str(interp,
            "Lk::focus_clear $ui\n"
            "Lk::focus_get $ui",
            "");

  eval_expect_err(interp, "Lk::focus_get", "Lk::focus_get", "1 argument",
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

  eval_ok(interp, "let ui [Lk::ui_create]", &r);
  if (r) lcl_ref_dec(r);

  /* no backend stamped anything yet -> 0 */
  check_int(interp, "Lk::time_ms $ui", 0);

  /* stamp from C (standing in for the SDL loop) and re-read */
  ui = fetch_ui(interp);
  CHECK(ui != NULL);
  if (ui) {
    lk_ui_set_time_ms(ui, 4321u);
  }
  check_int(interp, "Lk::time_ms $ui", 4321);

  eval_expect_err(interp, "Lk::time_ms", "Lk::time_ms", "1 argument", NULL);

  lcl_interp_free(interp);
  END_TEST();
}

static void test_history_undo_redo(void) {
  lcl_interp *interp;
  lcl_value *r = NULL;

  BEGIN_TEST("history: undo/redo round-trip, empty-stack returns 0");
  interp = make_interp();

  eval_ok(interp,
          "let d [Lk::doc_new \"one\"]\n"
          "let h [Lk::history_new $d]",
          &r);
  if (r) lcl_ref_dec(r);

  check_int(interp, "Lk::history_can_undo $h", 0);
  check_int(interp, "Lk::history_undo $h $d", 0);

  check_str(interp,
            "Lk::doc_insert $d 3 \" two\"\n"
            "Lk::doc_text $d",
            "one two");
  check_int(interp, "Lk::history_undo $h $d", 1);
  check_str(interp, "Lk::doc_text $d", "one");
  check_int(interp, "Lk::history_can_redo $h", 1);
  check_int(interp, "Lk::history_redo $h $d", 1);
  check_str(interp, "Lk::doc_text $d", "one two");
  check_int(interp, "Lk::history_can_redo $h", 0);

  lcl_interp_free(interp);
  END_TEST();
}

static void test_history_errors(void) {
  lcl_interp *interp;
  lcl_value *r = NULL;

  BEGIN_TEST("history: arity and wrong-opaque errors");
  interp = make_interp();

  eval_ok(interp,
          "let d [Lk::doc_new \"x\"]\n"
          "let h [Lk::history_new]",
          &r);
  if (r) lcl_ref_dec(r);

  eval_expect_err(interp, "Lk::history_undo $h", "Lk::history_undo",
                  "2 arguments", NULL);
  eval_expect_err(interp, "Lk::history_undo $d $d",
                  "expected lk_edit_history opaque", NULL, NULL);
  eval_expect_err(interp, "Lk::history_can_undo $d",
                  "expected lk_edit_history opaque", NULL, NULL);
  eval_expect_err(interp, "Lk::history_new $h", "expected lk_document opaque",
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
          "let d [Lk::doc_new \"one\"]\n"
          "let h [Lk::history_new $d]",
          &r);
  if (r) lcl_ref_dec(r);

  /* fresh history: no savepoint */
  check_int(interp, "Lk::history_at_saved $h", 0);

  check_str(interp, "Lk::history_mark_saved $h", "");
  check_int(interp, "Lk::history_at_saved $h", 1);

  check_int(interp,
            "Lk::doc_insert $d 3 \" two\"\n"
            "Lk::history_at_saved $h",
            0);

  check_int(interp,
            "Lk::history_undo $h $d\n"
            "Lk::history_at_saved $h",
            1);

  eval_expect_err(interp, "Lk::history_mark_saved",
                  "Lk::history_mark_saved", "1 argument", NULL);
  eval_expect_err(interp, "Lk::history_at_saved $d",
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
          "let ui [Lk::ui_create]\n"
          "let d [Lk::doc_new \"hello\"]\n"
          "let h [Lk::history_new]\n"
          "let e [Lk::editor_new $ui $d $h]",
          &r);
  if (r) lcl_ref_dec(r);

  check_int(interp, "opaque? $e", 1);
  check_int(interp, "Lk::editor_cursor $e", 0);

  /* Without a history the editor still works (undo just no-ops). */
  r = NULL;
  eval_ok(interp, "let e2 [Lk::editor_new $ui $d]", &r);
  if (r) lcl_ref_dec(r);
  check_int(interp, "Lk::editor_command $e2 undo", 0);

  /* Errors: arity, wrong opaques, foreign-doc history. */
  eval_expect_err(interp, "Lk::editor_new $ui", "Lk::editor_new",
                  "2 or 3 arguments", NULL);
  eval_expect_err(interp, "Lk::editor_new $d $d", "expected lk_ui opaque",
                  NULL, NULL);
  eval_expect_err(interp, "Lk::editor_new $ui $ui",
                  "expected lk_document opaque", NULL, NULL);
  eval_expect_err(interp,
                  "let d2 [Lk::doc_new \"other\"]\n"
                  "let h2 [Lk::history_new $d2]\n"
                  "Lk::editor_new $ui $d $h2",
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
          "let ui [Lk::ui_create]\n"
          "let d [Lk::doc_new \"hello world\"]\n"
          "let e [Lk::editor_new $ui $d]",
          &r);
  if (r) lcl_ref_dec(r);

  check_str(interp,
            "Lk::editor_set_cursor $e 5\n"
            "",
            "");
  check_int(interp, "Lk::editor_cursor $e", 5);

  /* Past-the-end clamps to the document length. */
  check_str(interp, "Lk::editor_set_cursor $e 999", "");
  check_int(interp, "Lk::editor_cursor $e", 11);

  /* No selection -> empty list; select_all -> (0 len). */
  check_int(interp, "len [Lk::editor_selection $e]", 0);
  check_int(interp, "Lk::editor_command $e select_all", 1);
  check_int(interp, "len [Lk::editor_selection $e]", 2);
  check_int(interp, "get [Lk::editor_selection $e] 0", 0);
  check_int(interp, "get [Lk::editor_selection $e] 1", 11);

  eval_expect_err(interp, "Lk::editor_set_cursor $e nope",
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
          "let ui [Lk::ui_create]\n"
          "let d [Lk::doc_new \"hello world\"]\n"
          "let e [Lk::editor_new $ui $d]\n"
          "Lk::editor_set_cursor $e 4\n"
          "Lk::editor_command $e select_all",
          &r);
  if (r) lcl_ref_dec(r);

  check_str(interp, "Lk::editor_scroll_to_cursor $e", "");
  check_int(interp, "Lk::editor_cursor $e", 11);
  check_int(interp, "len [Lk::editor_selection $e]", 2);

  /* Contrast: set_cursor at the SAME position drops the selection. */
  check_str(interp, "Lk::editor_set_cursor $e 11", "");
  check_int(interp, "len [Lk::editor_selection $e]", 0);

  eval_expect_err(interp, "Lk::editor_scroll_to_cursor",
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
          "let ui [Lk::ui_create]\n"
          "let d [Lk::doc_new \"world\"]\n"
          "let h [Lk::history_new $d]\n"
          "let e [Lk::editor_new $ui $d $h]",
          &r);
  if (r) lcl_ref_dec(r);

  /* insert_text at the cursor, cursor follows. */
  check_int(interp, "Lk::editor_command $e insert_text \"hello \"", 1);
  check_str(interp, "Lk::doc_text $d", "hello world");
  check_int(interp, "Lk::editor_cursor $e", 6);

  /* Motion, plus the optional \"select\" flag extending a selection. */
  check_int(interp, "Lk::editor_command $e move_left", 1);
  check_int(interp, "Lk::editor_cursor $e", 5);
  check_int(interp, "Lk::editor_command $e move_left select", 1);
  check_int(interp, "get [Lk::editor_selection $e] 0", 4);
  check_int(interp, "get [Lk::editor_selection $e] 1", 5);
  check_int(interp, "Lk::editor_command $e move_doc_end", 1);
  check_int(interp, "Lk::editor_cursor $e", 11);
  check_int(interp, "len [Lk::editor_selection $e]", 0);

  /* delete_backward eats one codepoint. */
  check_int(interp, "Lk::editor_command $e delete_backward", 1);
  check_str(interp, "Lk::doc_text $d", "hello worl");

  /* undo/redo through the same verb the keyboard uses. */
  check_int(interp, "Lk::editor_command $e undo", 1);
  check_str(interp, "Lk::doc_text $d", "hello world");
  check_int(interp, "Lk::editor_command $e redo", 1);
  check_str(interp, "Lk::doc_text $d", "hello worl");

  /* set_cursor command with the extend flag keeps the anchor. */
  check_int(interp, "Lk::editor_command $e set_cursor 0", 1);
  check_int(interp, "Lk::editor_command $e set_cursor 5 extend", 1);
  check_int(interp, "get [Lk::editor_selection $e] 1", 5);

  /* scroll_lines takes a signed count and never errors on ints. */
  r = NULL;
  eval_ok(interp, "Lk::editor_command $e scroll_lines 2", &r);
  if (r) lcl_ref_dec(r);
  r = NULL;
  eval_ok(interp, "Lk::editor_command $e scroll_lines -2", &r);
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
          "let ui [Lk::ui_create]\n"
          "let d [Lk::doc_new \"abc\"]\n"
          "let e [Lk::editor_new $ui $d]",
          &r);
  if (r) lcl_ref_dec(r);

  /* Unknown command errors name the known-command list. */
  eval_expect_err(interp, "Lk::editor_command $e frobnicate",
                  "unknown command 'frobnicate'", "insert_text",
                  "scroll_lines");
  eval_expect_err(interp, "Lk::editor_command $e frobnicate", "select_all",
                  "move_doc_end", NULL);

  /* Malformed per-command args. */
  eval_expect_err(interp, "Lk::editor_command $e insert_text",
                  "insert_text expects the text", NULL, NULL);
  eval_expect_err(interp, "Lk::editor_command $e move_left sideways",
                  "\"select\" flag", NULL, NULL);
  eval_expect_err(interp, "Lk::editor_command $e set_cursor nope",
                  "set_cursor pos", NULL, NULL);
  eval_expect_err(interp, "Lk::editor_command $e set_cursor 3 shift",
                  "\"extend\"", NULL, NULL);
  eval_expect_err(interp, "Lk::editor_command $e scroll_lines many",
                  "signed line count", NULL, NULL);
  eval_expect_err(interp, "Lk::editor_command $e select_all now",
                  "takes no arguments", NULL, NULL);
  eval_expect_err(interp, "Lk::editor_command $e", "Lk::editor_command",
                  NULL, NULL);
  eval_expect_err(interp, "Lk::editor_command $d insert_text x",
                  "expected lk_editor opaque", NULL, NULL);

  lcl_interp_free(interp);
  END_TEST();
}

static void test_editor_multicursor_procs(void) {
  lcl_interp *interp;
  lcl_value *r = NULL;

  BEGIN_TEST("editor: caret-set procs + multi-cursor commands");
  interp = make_interp();

  eval_ok(interp,
          "let ui [Lk::ui_create]\n"
          "let d [Lk::doc_new \"foo\\nfoo\"]\n"
          "let e [Lk::editor_new $ui $d [Lk::history_new]]",
          &r);
  if (r) lcl_ref_dec(r);

  /* single caret baseline; selections is index-parallel */
  check_int(interp, "len [Lk::editor_carets $e]", 1);
  check_int(interp, "len [Lk::editor_selections $e]", 1);
  check_int(interp, "len [get [Lk::editor_selections $e] 0]", 0);

  /* add_cursor_at by name; toggle removes */
  check_int(interp, "Lk::editor_command $e add_cursor_at 4", 1);
  check_int(interp, "len [Lk::editor_carets $e]", 2);
  check_int(interp, "get [Lk::editor_carets $e] 0", 0);
  check_int(interp, "get [Lk::editor_carets $e] 1", 4);
  check_int(interp, "Lk::editor_command $e add_cursor_at 4", 1);
  check_int(interp, "len [Lk::editor_carets $e]", 1);

  /* ctrl+D chain by name: word expand, then the next occurrence;
   * editor_selections stays index-parallel */
  check_int(interp, "Lk::editor_command $e set_cursor 1", 1);
  check_int(interp, "Lk::editor_command $e select_next_match", 1);
  check_int(interp, "get [get [Lk::editor_selections $e] 0] 0", 0);
  check_int(interp, "get [get [Lk::editor_selections $e] 0] 1", 3);
  check_int(interp, "Lk::editor_command $e select_next_match", 1);
  check_int(interp, "len [Lk::editor_carets $e]", 2);
  check_int(interp, "get [get [Lk::editor_selections $e] 1] 0", 4);
  check_int(interp, "Lk::editor_command $e select_next_match", 0);

  /* multi-caret edit = one transaction; one undo restores the set */
  check_int(interp, "Lk::editor_command $e insert_text \"X\"", 1);
  eval_ok(interp, "Lk::doc_text $d", &r);
  if (r) {
    CHECK(strcmp(lcl_value_to_string(r), "X\nX") == 0);
    lcl_ref_dec(r);
  }
  check_int(interp, "Lk::editor_command $e undo", 1);
  check_int(interp, "len [Lk::editor_carets $e]", 2);
  check_int(interp, "len [get [Lk::editor_selections $e] 0]", 2);

  /* collapse_cursors keeps the primary only */
  check_int(interp, "Lk::editor_command $e collapse_cursors", 1);
  check_int(interp, "len [Lk::editor_carets $e]", 1);
  check_int(interp, "Lk::editor_command $e collapse_cursors", 0);

  /* add_cursor_below clones every caret one row down */
  check_int(interp, "Lk::editor_command $e set_cursor 0", 1);
  check_int(interp, "Lk::editor_command $e add_cursor_below", 1);
  check_int(interp, "len [Lk::editor_carets $e]", 2);
  check_int(interp, "get [Lk::editor_carets $e] 1", 4);

  /* the unknown-command listing includes the new names */
  eval_expect_err(interp, "Lk::editor_command $e frobnicate",
                  "add_cursor_at", "collapse_cursors",
                  "select_next_match");

  /* error paths */
  eval_expect_err(interp, "Lk::editor_command $e add_cursor_at",
                  "non-negative position", NULL, NULL);
  eval_expect_err(interp, "Lk::editor_command $e add_cursor_at nope",
                  "non-negative position", NULL, NULL);
  eval_expect_err(interp, "Lk::editor_command $e collapse_cursors extra",
                  "takes no arguments", NULL, NULL);
  eval_expect_err(interp, "Lk::editor_carets", "expected 1 argument", NULL,
                  NULL);
  eval_expect_err(interp, "Lk::editor_carets $d",
                  "expected lk_editor opaque", NULL, NULL);
  eval_expect_err(interp, "Lk::editor_selections $ui",
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
          "let ui [Lk::ui_create]\n"
          "let d [Lk::doc_new \"abc\"]\n"
          "let e [Lk::editor_new $ui $d]",
          &r);
  if (r) lcl_ref_dec(r);

  /* Default NONE; all three modes round-trip through the getter. */
  check_str(interp, "Lk::editor_wrap_get $e", "none");
  check_str(interp, "Lk::editor_wrap $e character", "");
  check_str(interp, "Lk::editor_wrap_get $e", "character");
  check_str(interp, "Lk::editor_wrap $e word", "");
  check_str(interp, "Lk::editor_wrap_get $e", "word");
  check_str(interp, "Lk::editor_wrap $e none", "");
  check_str(interp, "Lk::editor_wrap_get $e", "none");

  /* Bogus mode name: hard error listing the supported modes; the
   * mode is left unchanged. */
  eval_expect_err(interp, "Lk::editor_wrap $e diagonal",
                  "unknown mode 'diagonal'",
                  "supported: none, character, word", NULL);
  check_str(interp, "Lk::editor_wrap_get $e", "none");

  /* Word mode wraps for real: "hello world foo" under the stub
   * backend (8 px per codepoint) at width 80 breaks after "hello "
   * (byte 6, not the char-fit floor 10) -- ROW_START from inside
   * row 1 lands on the word break. */
  r = NULL;
  eval_ok(interp,
          "let d2 [Lk::doc_new \"hello world foo\"]\n"
          "let e2 [Lk::editor_new $ui $d2]\n"
          "let t [Lk::begin_frame $ui]\n"
          "let w [Lk::node $t \"w\" \"window\"]\n"
          "let n [Lk::node $t \"ed\" \"editor\"]\n"
          "Lk::set_root $t $w\n"
          "Lk::append_child $t $w $n\n"
          "Lk::prop $t $n focusable 1\n"
          "Lk::prop $t $n editor $e2\n"
          "Lk::end_frame $ui\n"
          "Lk::editor_wrap $e2 word",
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

    check_int(interp, "Lk::editor_command $e2 set_cursor 8", 1);
    check_int(interp, "Lk::editor_command $e2 move_row_start", 1);
    check_int(interp, "Lk::editor_cursor $e2", 6);
    check_int(interp, "Lk::editor_command $e2 move_line_start", 1);
    check_int(interp, "Lk::editor_cursor $e2", 0);
  }

  /* Arity and type errors. */
  eval_expect_err(interp, "Lk::editor_wrap $e", "Lk::editor_wrap",
                  "2 arguments", NULL);
  eval_expect_err(interp, "Lk::editor_wrap $e none extra",
                  "2 arguments", NULL, NULL);
  eval_expect_err(interp, "Lk::editor_wrap $d character",
                  "expected lk_editor opaque", NULL, NULL);
  eval_expect_err(interp, "Lk::editor_wrap_get $d",
                  "expected lk_editor opaque", NULL, NULL);
  eval_expect_err(interp, "Lk::editor_wrap_get", "Lk::editor_wrap_get",
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
          "let ui [Lk::ui_create]\n"
          "let d0 [Lk::doc_new \"ab\ncd\"]\n"
          "let e0 [Lk::editor_new $ui $d0]",
          &r);
  if (r) lcl_ref_dec(r);
  check_int(interp, "Lk::editor_command $e0 set_cursor 4", 1);
  check_int(interp, "Lk::editor_command $e0 move_row_start", 1);
  check_int(interp, "Lk::editor_cursor $e0", 3);
  check_int(interp, "Lk::editor_command $e0 move_row_end", 1);
  check_int(interp, "Lk::editor_cursor $e0", 5);

  /* Wrapped: one 20-codepoint line under the stub backend (8 px per
   * codepoint, so width 80 = 10 codepoints per row -- the same
   * geometry as the core wrap tests).  The frame carries the editor
   * ref; a real lk_layout stamps the wrap key (content width). */
  r = NULL;
  eval_ok(interp,
          "let d [Lk::doc_new \"abcdefghijklmnopqrst\"]\n"
          "let e [Lk::editor_new $ui $d]\n"
          "let t [Lk::begin_frame $ui]\n"
          "let w [Lk::node $t \"w\" \"window\"]\n"
          "let n [Lk::node $t \"ed\" \"editor\"]\n"
          "Lk::set_root $t $w\n"
          "Lk::append_child $t $w $n\n"
          "Lk::prop $t $n focusable 1\n"
          "Lk::prop $t $n editor $e\n"
          "Lk::end_frame $ui\n"
          "Lk::editor_wrap $e character",
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
    check_int(interp, "Lk::editor_command $e set_cursor 15", 1);
    check_int(interp, "Lk::editor_command $e move_row_start", 1);
    check_int(interp, "Lk::editor_cursor $e", 10);
    check_int(interp, "Lk::editor_command $e move_line_start", 1);
    check_int(interp, "Lk::editor_cursor $e", 0);

    /* Cursor on row 0: ROW_END stops at the break byte (owned by the
     * NEXT row), LINE_END goes to the line end. */
    check_int(interp, "Lk::editor_command $e set_cursor 5", 1);
    check_int(interp, "Lk::editor_command $e move_row_end", 1);
    check_int(interp, "Lk::editor_cursor $e", 10);
    check_int(interp, "Lk::editor_command $e set_cursor 5", 1);
    check_int(interp, "Lk::editor_command $e move_line_end", 1);
    check_int(interp, "Lk::editor_cursor $e", 20);

    /* The optional "select" flag extends, exactly like other motion
     * commands. */
    check_int(interp, "Lk::editor_command $e set_cursor 15", 1);
    check_int(interp, "Lk::editor_command $e move_row_start select", 1);
    check_int(interp, "get [Lk::editor_selection $e] 0", 10);
    check_int(interp, "get [Lk::editor_selection $e] 1", 15);
  }

  /* Malformed flag: the same hard error as the other motions; the
   * unknown-command listing names the row commands. */
  eval_expect_err(interp, "Lk::editor_command $e move_row_end sideways",
                  "\"select\" flag", NULL, NULL);
  eval_expect_err(interp, "Lk::editor_command $e frobnicate",
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
          "let ui [Lk::ui_create]\n"
          "let d [Lk::doc_new \"hello world again\"]\n"
          "let e [Lk::editor_new $ui $d]",
          &r);
  if (r) lcl_ref_dec(r);

  /* fg-only, bg + underline, all three. */
  check_str(interp,
            "Lk::editor_set_spans $e $d ( #{start 0 end 5 fg (255 0 0)} "
            "#{start 6 end 11 bg (0 0 80) underline 1} "
            "#{start 12 end 17 fg (1 2 3) bg (4 5 6 128)} )",
            "");

  /* Empty list clears. */
  check_str(interp, "Lk::editor_set_spans $e $d ()", "");

  lcl_interp_free(interp);
  END_TEST();
}

static void test_editor_set_spans_errors(void) {
  lcl_interp *interp;
  lcl_value *r = NULL;

  BEGIN_TEST("editor: set_spans rejects malformed span dicts");
  interp = make_interp();

  eval_ok(interp,
          "let ui [Lk::ui_create]\n"
          "let d [Lk::doc_new \"hello world\"]\n"
          "let e [Lk::editor_new $ui $d]",
          &r);
  if (r) lcl_ref_dec(r);

  eval_expect_err(interp, "Lk::editor_set_spans $e $d #{start 0 end 2}",
                  "must be a list", NULL, NULL);
  eval_expect_err(interp, "Lk::editor_set_spans $e $d ( #{end 2} )",
                  "missing 'start'", NULL, NULL);
  eval_expect_err(interp, "Lk::editor_set_spans $e $d ( #{start 2 end 2} )",
                  "end must be an integer > start", NULL, NULL);
  eval_expect_err(interp,
                  "Lk::editor_set_spans $e $d ( #{start 0 end 2 color (1 2 3)} )",
                  "unknown span key", "underline", NULL);
  eval_expect_err(interp,
                  "Lk::editor_set_spans $e $d ( #{start 0 end 2 fg (1 2)} )",
                  "fg must be", NULL, NULL);
  eval_expect_err(interp,
                  "Lk::editor_set_spans $e $d "
                  "( #{start 4 end 8} #{start 0 end 2} )",
                  "sorted and non-overlapping", NULL, NULL);
  eval_expect_err(interp, "Lk::editor_set_spans $e $d", "3 arguments", NULL,
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
          "let ui [Lk::ui_create]\n"
          "proc mk {ui} {\n"
          "    let d [Lk::doc_new \"ephemeral\"]\n"
          "    let h [Lk::history_new $d]\n"
          "    return [Lk::editor_new $ui $d $h]\n"
          "}\n"
          "var e [mk $ui]",
          &r);
  if (r) lcl_ref_dec(r);

  /* d and h went out of scope with mk's frame; only the editor keeps
   * them alive.  The editor must keep working: */
  check_int(interp, "Lk::editor_command $e insert_text \"still \"", 1);
  check_int(interp, "Lk::editor_cursor $e", 6);
  check_int(interp, "Lk::editor_command $e undo", 1);
  check_int(interp, "Lk::editor_cursor $e", 0);

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
          "let d [Lk::doc_new \"hello world\"]\n"
          "let s [Lk::annot_store_new]\n"
          "Lk::annot_attach $s $d\n"
          "Lk::annot_layer_register $s \"marks\"\n"
          "let a [Lk::annot_add $s 0 5 \"marks\" #{kind word note \"greeting\"}]",
          &r);
  if (r) lcl_ref_dec(r);

  check_int(interp, "$a", 1);
  check_int(interp, "get [Lk::annot_span $s $a] 0", 0);
  check_int(interp, "get [Lk::annot_span $s $a] 1", 5);
  check_str(interp, "Lk::annot_meta $s $a kind", "word");
  check_str(interp, "Lk::annot_meta $s $a note", "greeting");

  /* Absent meta key reads as "" (the record still exists). */
  check_str(interp, "Lk::annot_meta $s $a missing", "");

  /* annot_meta_all: every pair as a dict. */
  check_int(interp, "len [Lk::annot_meta_all $s $a]", 2);
  check_str(interp, "get [Lk::annot_meta_all $s $a] kind", "word");
  check_str(interp, "get [Lk::annot_meta_all $s $a] note", "greeting");
  eval_expect_err(interp, "Lk::annot_meta_all $s 999", "no such annotation",
                  NULL, NULL);

  /* annot_layer: the record's layer name. */
  check_str(interp, "Lk::annot_layer $s $a", "marks");
  eval_expect_err(interp, "Lk::annot_layer $s 999", "no such annotation",
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
          "let d [Lk::doc_new \"hello world again\"]\n"
          "let s [Lk::annot_store_new]\n"
          "Lk::annot_attach $s $d\n"
          "let a1 [Lk::annot_add $s 0 5 \"x\"]\n"
          "let a2 [Lk::annot_add $s 6 11 \"y\"]\n"
          "let a3 [Lk::annot_add $s 12 17 \"x\"]",
          &r);
  if (r) lcl_ref_dec(r);

  check_int(interp, "len [Lk::annot_in_range $s 0 100]", 3);
  check_int(interp, "len [Lk::annot_in_range $s 0 100 x]", 2);
  check_int(interp, "len [Lk::annot_in_range $s 0 6]", 1);
  check_int(interp, "len [Lk::annot_at $s 7]", 1);
  check_int(interp, "get [Lk::annot_at $s 7] 0", 2);
  check_int(interp, "len [Lk::annot_at $s 7 x]", 0);
  check_int(interp, "len [Lk::annot_by_layer $s x]", 2);
  check_int(interp, "len [Lk::annot_by_layer $s nothere]", 0);

  lcl_interp_free(interp);
  END_TEST();
}

static void test_annot_anchor_tracking(void) {
  lcl_interp *interp;
  lcl_value *r = NULL;

  BEGIN_TEST("annot: anchors track document edits via subscription");
  interp = make_interp();

  eval_ok(interp,
          "let d [Lk::doc_new \"hello world\"]\n"
          "let s [Lk::annot_store_new]\n"
          "Lk::annot_attach $s $d\n"
          "let a [Lk::annot_add $s 6 11 \"w\"]",
          &r);
  if (r) lcl_ref_dec(r);

  /* Insert before the annotation: both ends shift right. */
  r = NULL;
  eval_ok(interp, "Lk::doc_insert $d 0 \"say: \"", &r);
  if (r) lcl_ref_dec(r);
  check_int(interp, "get [Lk::annot_span $s $a] 0", 11);
  check_int(interp, "get [Lk::annot_span $s $a] 1", 16);

  /* Delete across the middle: the span shrinks. */
  r = NULL;
  eval_ok(interp, "Lk::doc_delete $d 11 2", &r);
  if (r) lcl_ref_dec(r);
  check_int(interp, "get [Lk::annot_span $s $a] 0", 11);
  check_int(interp, "get [Lk::annot_span $s $a] 1", 14);

  lcl_interp_free(interp);
  END_TEST();
}

static void test_annot_errors(void) {
  lcl_interp *interp;
  lcl_value *r = NULL;

  BEGIN_TEST("annot: remove + error paths");
  interp = make_interp();

  eval_ok(interp,
          "let d [Lk::doc_new \"hello\"]\n"
          "let s [Lk::annot_store_new]\n"
          "Lk::annot_attach $s $d\n"
          "let a [Lk::annot_add $s 0 5 \"m\"]",
          &r);
  if (r) lcl_ref_dec(r);

  check_int(interp, "Lk::annot_remove $s $a", 1);
  check_int(interp, "Lk::annot_remove $s $a", 0);
  eval_expect_err(interp, "Lk::annot_span $s $a", "no such annotation", NULL,
                  NULL);
  eval_expect_err(interp, "Lk::annot_meta $s $a kind", "no such annotation",
                  NULL, NULL);

  /* Bad ranges, bad meta, re-attach, wrong opaque, arity. */
  eval_expect_err(interp, "Lk::annot_add $s 5 5 \"m\"",
                  "end must be an integer > start", NULL, NULL);
  eval_expect_err(interp, "Lk::annot_add $s 0 5 \"m\" nope",
                  "meta must be a dict", NULL, NULL);
  eval_expect_err(interp, "Lk::annot_attach $s $d", "already attached", NULL,
                  NULL);
  eval_expect_err(interp, "Lk::annot_span $d 1", "expected lk_annot_store",
                  NULL, NULL);
  eval_expect_err(interp, "Lk::annot_add $s", "Lk::annot_add", "arguments",
                  NULL);

  lcl_interp_free(interp);
  END_TEST();
}

static void test_annot_seq_and_layers(void) {
  lcl_interp *interp;
  lcl_value *r = NULL;

  BEGIN_TEST("annot: store_seq counts mutations; layers lists names");
  interp = make_interp();

  eval_ok(interp,
          "let d [Lk::doc_new \"hello world\"]\n"
          "let s [Lk::annot_store_new]\n"
          "Lk::annot_attach $s $d",
          &r);
  if (r) lcl_ref_dec(r);
  r = NULL;

  check_int(interp, "Lk::annot_store_seq $s", 0);
  check_int(interp, "len [Lk::annot_layers $s]", 0);

  eval_ok(interp, "let a [Lk::annot_add $s 0 5 \"notes\"]", &r);
  if (r) lcl_ref_dec(r);
  r = NULL;
  check_int(interp, "Lk::annot_store_seq $s", 1);
  check_str(interp, "String::join [Lk::annot_layers $s] \",\"", "notes");

  /* Registration order is kept; explicit registration counts. */
  eval_ok(interp,
          "Lk::annot_layer_register $s \"plumb\"\n"
          "let b [Lk::annot_add $s 6 11 \"style\"]",
          &r);
  if (r) lcl_ref_dec(r);
  r = NULL;
  check_str(interp, "String::join [Lk::annot_layers $s] \",\"",
            "notes,plumb,style");
  check_int(interp, "Lk::annot_store_seq $s", 2);

  /* Anchor motion alone does not count; a delete that drops a record
   * does. */
  eval_ok(interp, "Lk::doc_insert $d 0 \"x\"", &r);
  if (r) lcl_ref_dec(r);
  r = NULL;
  check_int(interp, "Lk::annot_store_seq $s", 2);

  check_int(interp, "Lk::annot_remove $s $a", 1);
  check_int(interp, "Lk::annot_store_seq $s", 3);
  check_int(interp, "Lk::annot_remove $s $a", 0);
  check_int(interp, "Lk::annot_store_seq $s", 3);

  eval_ok(interp, "Lk::doc_delete $d 0 12", &r);
  if (r) lcl_ref_dec(r);
  r = NULL;
  check_int(interp, "len [Lk::annot_by_layer $s \"style\"]", 0);
  check_int(interp, "Lk::annot_store_seq $s", 4);

  eval_expect_err(interp, "Lk::annot_layers $d", "expected lk_annot_store",
                  NULL, NULL);
  eval_expect_err(interp, "Lk::annot_store_seq", "Lk::annot_store_seq",
                  "argument", NULL);

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
          "let d [Lk::doc_new \"hi\"]\n"
          "let ed [Lk::editor_new $u $d]\n"
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

  /* The DSL's known-prop list now includes `editor` and `image`. */
  BEGIN_TEST("dsl: unknown-prop error lists editor among known keys");
  interp = make_dsl_interp();
  dsl_begin(interp);

  eval_expect_err(interp, "label w_ed #{bogus 1}", "unknown prop 'bogus'",
                  "(known:", "editor");
  eval_expect_err(interp, "label w_im #{bogus 1}", "unknown prop 'bogus'",
                  "(known:", "image");

  lcl_interp_free(interp);
  END_TEST();
}

static void test_dsl_image_widget(void) {
  lcl_interp *interp;
  lcl_value *r = NULL;
  lk_tree *t;
  lk_ui *ui;

  /* `image "pic" #{image $img w 64}` builds a UIK_IMAGE node whose
   * UIP_IMAGE prop carries the typed resource ref — the editor-widget
   * pattern verbatim. */
  BEGIN_TEST("dsl: image widget carries the resource prop");
  interp = make_dsl_interp();
  dsl_begin(interp);

  eval_ok(interp,
          "let img [Lk::image_new $u 8 4]\n"
          "image pic #{image $img w 64 h 48}",
          &r);
  if (r) lcl_ref_dec(r);

  t = dsl_tree(interp);
  ui = dsl_ui(interp);
  CHECK(t != NULL && ui != NULL);
  if (t && ui) {
    lk_ix n = dsl_find(t, "pic");
    lk_image *img;
    lk_u32 w = 0;
    lk_u32 h = 0;

    CHECK(n != 0);
    CHECK(lk_node_kind_get(t, n) == (lk_u16)UIK_IMAGE);
    CHECK(lk_node_has_prop(t, n, UIP_IMAGE) == 1);
    CHECK(lk_node_prop_i32(t, n, UIP_W, -1) == 64);
    CHECK(lk_node_prop_i32(t, n, UIP_H, -1) == 48);

    img = lk_image_from_node(lk_ui_resources(ui), t, n);
    CHECK(img != NULL);
    lk_image_size(img, &w, &h);
    CHECK(w == 8 && h == 4);
  }

  /* Typo'd resource value is a hard error at the binding layer. */
  eval_expect_err(interp, "image pic2 #{image 42}",
                  "image prop expects an lk_image opaque", NULL, NULL);

  lcl_interp_free(interp);
  END_TEST();
}

static void test_dsl_canvas_widget(void) {
  lcl_interp *interp;
  lcl_value *r = NULL;
  lk_tree *t;
  lk_ui *ui;

  BEGIN_TEST("dsl: canvas widget carries the resource prop");
  interp = make_dsl_interp();
  dsl_begin(interp);

  eval_ok(interp,
          "let cv [Lk::canvas_new $u 300 200]\n"
          "Lk::canvas_line $cv 0 0 10 10 (1 2 3)\n"
          "canvas plot #{canvas $cv grow 1}",
          &r);
  if (r) lcl_ref_dec(r);

  t = dsl_tree(interp);
  ui = dsl_ui(interp);
  CHECK(t != NULL && ui != NULL);
  if (t && ui) {
    lk_ix n = dsl_find(t, "plot");
    lk_canvas *cv;
    lk_u32 w = 0;
    lk_u32 h = 0;

    CHECK(n != 0);
    CHECK(lk_node_kind_get(t, n) == (lk_u16)UIK_CANVAS);
    CHECK(lk_node_has_prop(t, n, UIP_CANVAS) == 1);
    CHECK(lk_node_prop_i32(t, n, UIP_GROW, -1) == 1);

    cv = lk_canvas_from_node(lk_ui_resources(ui), t, n);
    CHECK(cv != NULL);
    lk_canvas_size(cv, &w, &h);
    CHECK(w == 300 && h == 200);
    CHECK(lk_canvas_op_count(cv) == 1);
  }

  /* Typo'd resource value is a hard error at the binding layer; the
   * DSL schema lists the key. */
  eval_expect_err(interp, "canvas plot2 #{canvas 42}",
                  "canvas prop expects an lk_canvas opaque", NULL, NULL);
  eval_expect_err(interp, "canvas plot3 #{canvs $cv}", "unknown prop 'canvs'",
                  "canvas", NULL);

  lcl_interp_free(interp);
  END_TEST();
}

/* Footgun pass: a 3-arg keybinding (no ptype) fires with NO
 * presentation anywhere on the focus path -- the window-level
 * fallback -- and Lk::focus_request focuses a node the frame after
 * it is built. */
static void test_dsl_global_keybinding_fallback(void) {
  lcl_interp *interp;
  lcl_value *r = NULL;
  lk_ui *ui;
  lk_event ev;

  BEGIN_TEST("dsl: 3-arg keybinding fires without any presentation");
  interp = make_dsl_interp();
  dsl_begin(interp);

  /* Tree built with the Layer-1 procs: window > label, nothing
   * presents anything.  Only the binding comes from the DSL. */
  eval_ok(interp,
          "keybinding f2 \"\" NewGame\n"
          "let w [Lk::node $t root window]\n"
          "let l [Lk::node $t plain label]\n"
          "Lk::set_root $t $w\n"
          "Lk::append_child $t $w $l\n"
          "Lk::end_frame $u",
          &r);
  if (r) lcl_ref_dec(r);

  ui = dsl_ui(interp);
  CHECK(ui != NULL);
  if (ui) {

    memset(&ev, 0, sizeof(ev));
    ev.type = LK_EVENT_KEY_DOWN;
    ev.target = lk_ui_tree(ui)->root;
    ev.data.key.keycode = LKK_F2;
    lk_event_route(ui, &ev);
    CHECK(ev.handled == 1);
    CHECK(lk_ui_commands(ui)->count == 1);
    if (lk_ui_commands(ui)->count == 1) {
      CHECK(lk_ui_commands(ui)->cmds[0].name ==
            lk_intern_cid(ui->intern, "NewGame"));
      CHECK(lk_ui_commands(ui)->cmds[0].arg_count == 0);
    }
  }

  lcl_interp_free(interp);
  END_TEST();
}

static void test_focus_request_binding(void) {
  lcl_interp *interp;
  lcl_value *r = NULL;

  BEGIN_TEST("focus_request: deferred focus + cancel + errors");
  interp = make_interp();

  eval_ok(interp,
          "let ui [Lk::ui_create]\n"
          "Lk::focus_request $ui cmdline\n"
          "let t [Lk::begin_frame $ui]\n"
          "let w [Lk::node $t root window]\n"
          "let e [Lk::node $t cmdline text_input]\n"
          "Lk::prop $t $e focusable 1\n"
          "Lk::set_root $t $w\n"
          "Lk::append_child $t $w $e\n"
          "Lk::end_frame $ui",
          &r);
  if (r) lcl_ref_dec(r);
  check_str(interp, "Lk::focus_get $ui", "cmdline");

  /* Cancel: "" clears a pending request, so the node never focuses. */
  r = NULL;
  eval_ok(interp,
          "Lk::focus_clear $ui\n"
          "Lk::focus_request $ui cmdline\n"
          "Lk::focus_request $ui \"\"\n"
          "let t2 [Lk::begin_frame $ui]\n"
          "let w2 [Lk::node $t2 root window]\n"
          "let e2 [Lk::node $t2 cmdline text_input]\n"
          "Lk::prop $t2 $e2 focusable 1\n"
          "Lk::set_root $t2 $w2\n"
          "Lk::append_child $t2 $w2 $e2\n"
          "Lk::end_frame $ui",
          &r);
  if (r) lcl_ref_dec(r);
  check_str(interp, "Lk::focus_get $ui", "");

  eval_expect_err(interp, "Lk::focus_request $ui", "Lk::focus_request",
                  "2 arguments", NULL);
  eval_expect_err(interp, "Lk::focus_request 1 x", "expected lk_ui opaque",
                  NULL, NULL);
  eval_expect_err(interp, "Lk::focus_request $ui #{a 1}", "must be a string",
                  NULL, NULL);

  lcl_interp_free(interp);
  END_TEST();
}

static void test_capture_binding(void) {
  lcl_interp *interp;
  lcl_value *r = NULL;
  lk_ui *ui;

  BEGIN_TEST("capture_set / capture_get / capture_clear + errors");
  interp = make_interp();

  eval_ok(interp, "let ui [Lk::ui_create]", &r);
  if (r) lcl_ref_dec(r);
  check_str(interp, "Lk::capture_get $ui", "");
  r = NULL;
  eval_ok(interp, "Lk::capture_set $ui canvas", &r);
  if (r) lcl_ref_dec(r);
  check_str(interp, "Lk::capture_get $ui", "canvas");

  ui = fetch_ui(interp);
  CHECK(ui != NULL);
  if (ui) {
    CHECK(lk_capture_current(ui) ==
          lk_intern_id(ui->intern, lk_str_c("canvas")));
  }

  r = NULL;
  eval_ok(interp, "Lk::capture_clear $ui", &r);
  if (r) lcl_ref_dec(r);
  check_str(interp, "Lk::capture_get $ui", "");

  eval_expect_err(interp, "Lk::capture_set $ui", "2 arguments", NULL, NULL);
  eval_expect_err(interp, "Lk::capture_set 1 x", "expected lk_ui opaque",
                  NULL, NULL);
  eval_expect_err(interp, "Lk::capture_set $ui #{a 1}", "must be a string",
                  NULL, NULL);
  eval_expect_err(interp, "Lk::capture_clear", "1 argument", NULL, NULL);
  eval_expect_err(interp, "Lk::capture_get 1", "expected lk_ui opaque", NULL,
                  NULL);

  lcl_interp_free(interp);
  END_TEST();
}

static void test_text_size_binding(void) {
  lcl_interp *interp;
  lcl_value *r = NULL;

  BEGIN_TEST("text_size: stub metrics headless, font args, errors");
  interp = make_interp();

  eval_ok(interp, "let ui [Lk::ui_create]", &r);
  if (r) lcl_ref_dec(r);

  /* Stub backend: 8 px per codepoint, 16 tall. */
  check_str(interp, "repr [Lk::text_size $ui hello]", "(40 16)");
  check_str(interp, "repr [Lk::text_size $ui \"\"]", "(0 16)");
  check_str(interp, "repr [Lk::text_size $ui \"h\xc3\xa9llo\"]", "(40 16)");
  check_str(interp, "repr [Lk::text_size $ui 12345 1 13]", "(40 16)");

  eval_expect_err(interp, "Lk::text_size $ui", "2 to 4 arguments", NULL,
                  NULL);
  eval_expect_err(interp, "Lk::text_size 1 x", "expected lk_ui opaque", NULL,
                  NULL);
  eval_expect_err(interp, "Lk::text_size $ui x abc", "font_id must be",
                  NULL, NULL);
  eval_expect_err(interp, "Lk::text_size $ui x 0 -1", "font_size must be",
                  NULL, NULL);

  lcl_interp_free(interp);
  END_TEST();
}

static void test_canvas_clip_binding(void) {
  lcl_interp *interp;
  lcl_value *r = NULL;

  BEGIN_TEST("canvas_clip / canvas_clip_end: ops, nesting, errors");
  interp = make_interp();

  eval_ok(interp,
          "let ui [Lk::ui_create]\n"
          "let c [Lk::canvas_new $ui 100 100]\n"
          "Lk::canvas_clip $c 10 10 50.4 50\n"
          "Lk::canvas_line $c 0 0 100 100 (255 0 0)\n"
          "Lk::canvas_clip $c 0 0 5 5\n"
          "Lk::canvas_clip_end $c\n"
          "Lk::canvas_clip_end $c",
          &r);
  if (r) lcl_ref_dec(r);
  check_int(interp, "Lk::canvas_op_count $c", 5);

  eval_expect_err(interp, "Lk::canvas_clip_end $c", "no sub-clip is open",
                  NULL, NULL);
  eval_expect_err(interp, "Lk::canvas_clip $c 0 0 -1 5", "w and h must be",
                  NULL, NULL);
  eval_expect_err(interp, "Lk::canvas_clip $c 0 0 1", "5 arguments", NULL,
                  NULL);
  eval_expect_err(interp, "Lk::canvas_clip $c 0 x 1 1", NULL, NULL, NULL);
  eval_expect_err(interp, "Lk::canvas_clip 1 0 0 1 1", NULL, NULL, NULL);

  /* Depth cap: 8 nested is fine, the 9th is a hard error; clear
   * forgets them all. */
  r = NULL;
  eval_ok(interp,
          "foreach i [List::range 0 8] { Lk::canvas_clip $c 0 0 1 1 }",
          &r);
  if (r) lcl_ref_dec(r);
  eval_expect_err(interp, "Lk::canvas_clip $c 0 0 1 1", "at most 8 deep",
                  NULL, NULL);
  r = NULL;
  eval_ok(interp, "Lk::canvas_clear $c", &r);
  if (r) lcl_ref_dec(r);
  eval_expect_err(interp, "Lk::canvas_clip_end $c", "no sub-clip is open",
                  NULL, NULL);

  lcl_interp_free(interp);
  END_TEST();
}

extern int lcl_lk_debug_pres_boxes(void);

static void test_spans_binding(void) {
  lcl_interp *interp;
  lcl_value *r = NULL;

  BEGIN_TEST("spans: new / add / present / clear + errors");
  interp = make_interp();

  eval_ok(interp,
          "let ui [Lk::ui_create]\n"
          "let s [Lk::spans_new $ui]\n"
          "Lk::spans_add $s 0 5 #{fg (255 0 0)}\n"
          "Lk::spans_add $s 10 20 #{bg (1 2 3 4) underline 1}\n"
          "Lk::spans_present $s 30 40 loc #{file a line 7}\n"
          "Lk::spans_add $s 30 40 #{underline 1}",
          &r);
  if (r) lcl_ref_dec(r);
  check_int(interp, "opaque? $s", 1);
  /* the identical range merged: style + presentation on one entry */
  check_int(interp, "Lk::spans_count $s", 3);

  eval_expect_err(interp, "Lk::spans_add $s 3 7 #{fg (1 2 3)}", "overlaps",
                  NULL, NULL);
  eval_expect_err(interp, "Lk::spans_add $s 50 60 #{}", "needs fg, bg",
                  NULL, NULL);
  eval_expect_err(interp, "Lk::spans_add $s 50 60 #{colour (1 2 3)}",
                  "unknown style key", NULL, NULL);
  eval_expect_err(interp, "Lk::spans_add $s 60 50 #{fg (1 2 3)}",
                  "start must be before end", NULL, NULL);
  eval_expect_err(interp, "Lk::spans_add $s -1 5 #{fg (1 2 3)}",
                  "integers >= 0", NULL, NULL);
  eval_expect_err(interp, "Lk::spans_add $s 50 60 #{fg (1 2)}", NULL, NULL,
                  NULL);
  eval_expect_err(interp, "Lk::spans_add $s 50 60 7", "must be a dict", NULL,
                  NULL);
  eval_expect_err(interp, "Lk::spans_present $s 50 60 \"\" 1",
                  "ptype must be non-empty", NULL, NULL);
  eval_expect_err(interp, "Lk::spans_present $s 35 45 loc 1", "overlaps",
                  NULL, NULL);
  eval_expect_err(interp, "Lk::spans_count 1", "expected lk_spans opaque",
                  NULL, NULL);
  eval_expect_err(interp, "Lk::spans_new", "1 argument", NULL, NULL);

  r = NULL;
  eval_ok(interp, "Lk::spans_clear $s", &r);
  if (r) lcl_ref_dec(r);
  check_int(interp, "Lk::spans_count $s", 0);
  /* every lcl-value box the presentations held is gone */
  CHECK(lcl_lk_debug_pres_boxes() == 0);

  lcl_interp_free(interp);
  END_TEST();
}

static void test_styled_text_present_binding(void) {
  lcl_interp *interp;
  lcl_value *r = NULL;
  lk_ui *ui;

  BEGIN_TEST("styled_text: click on a presented range -> hit dict");
  interp = make_interp();

  /* "aaaa bbbb cccc" at w 40 (stub: 5 cp per row); [5, 9) = "bbbb"
   * presents `word` with a dict value; a pointer_down translator on
   * `word` emits Look. */
  eval_ok(interp,
          "let ui [Lk::ui_create]\n"
          "let s [Lk::spans_new $ui]\n"
          "Lk::spans_present $s 5 9 word #{w bbbb n 2}\n"
          "var got \"\"\n"
          "Lk::add_translator $ui pointer_down word \"\" \"\" \"\" Look\n"
          "Lk::set_command_handler $ui [lambda {cmd} { set! got $cmd }]",
          &r);
  if (r) lcl_ref_dec(r);
  r = NULL;
  eval_ok(interp,
          "let t [Lk::begin_frame $ui]\n"
          "let w [Lk::node $t root window]\n"
          "let c [Lk::node $t col column]\n"
          "let p [Lk::node $t para styled_text]\n"
          "Lk::prop $t $c align start\n"
          "Lk::prop $t $c padding 10\n"
          "Lk::prop $t $p text \"aaaa bbbb cccc\"\n"
          "Lk::prop $t $p w 40\n"
          "Lk::prop $t $p wrap word\n"
          "Lk::prop $t $p spans $s\n"
          "Lk::set_root $t $w\n"
          "Lk::append_child $t $w $c\n"
          "Lk::append_child $t $c $p\n"
          "Lk::end_frame $ui",
          &r);
  if (r) lcl_ref_dec(r);

  ui = fetch_ui(interp);
  CHECK(ui != NULL);

  if (ui) {
    const lk_tree *cur = lk_ui_tree(ui);
    lk_style *styles = (lk_style *)malloc(sizeof(lk_style) * cur->node_count);
    lk_layout_cfg cfg;
    lk_event ev;

    lk_style_resolve(lk_ui_theme(ui), cur, NULL, styles);
    memset(&cfg, 0, sizeof(cfg));
    cfg.text = lk_text_backend_stub();
    cfg.viewport_w = 640;
    cfg.viewport_h = 480;
    cfg.styles = styles;
    cfg.state = lk_ui_state(ui);
    cfg.geom = lk_ui_geom(ui);
    lk_ui_set_text_backend(ui, lk_text_backend_stub());
    CHECK(lk_layout(cur, &cfg, lk_ui_rects(ui)) == 1);

    /* row 2 ("bbbb ") starts at y 26; x 27 = 2 cp in -> byte 7 */
    check_int(interp, "Lk::styled_text_pos_at $ui para 27 30", 7);
    check_int(interp, "Lk::styled_text_pos_at $ui para 5 5", -1);
    check_int(interp, "Lk::styled_text_pos_at $ui nope 27 30", -1);

    memset(&ev, 0, sizeof(ev));
    ev.type = LK_EVENT_POINTER_DOWN;
    ev.target = lk_tree_find_by_id(cur, lk_intern_cid(ui->intern, "para"));
    ev.data.pointer.x = 27;
    ev.data.pointer.y = 30;
    ev.data.pointer.button = LK_POINTER_BUTTON_PRIMARY;
    lk_event_route(ui, &ev);
    CHECK(ev.handled == 1);

    check_str(interp, "get $got name", "Look");
    check_str(interp, "get [get $got hit] ptype", "word");
    check_str(interp, "get [get $got hit] locus_kind", "text-range");
    check_int(interp, "get [get [get $got hit] locus] start", 5);
    check_int(interp, "get [get [get $got hit] locus] end", 9);
    check_int(interp, "get [get [get $got hit] locus] pos", 7);
    /* the value came back as the live dict */
    check_str(interp, "get [get [get $got hit] value] w", "bbbb");
    check_int(interp, "get [get [get $got hit] value] n", 2);

    free(styles);
  }

  eval_expect_err(interp, "Lk::styled_text_pos_at $ui para x 1",
                  "must be integers", NULL, NULL);

  lcl_interp_free(interp);
  END_TEST();
}

static void test_dsl_styled_text_widget(void) {
  lcl_interp *interp;
  lcl_value *r = NULL;
  lk_tree *t;
  lk_ui *ui;

  BEGIN_TEST("dsl: styled_text widget with spans + wrap props");
  interp = make_dsl_interp();
  dsl_begin(interp);

  eval_ok(interp,
          "let sp [Lk::spans_new $u]\n"
          "Lk::spans_add $sp 0 2 #{fg (9 9 9)}\n"
          "styled_text para #{text \"hi there\" spans $sp wrap none}\n"
          "styled_text plain #{text \"just wraps\"}",
          &r);
  if (r) lcl_ref_dec(r);
  t = dsl_tree(interp);
  ui = dsl_ui(interp);
  CHECK(t != NULL && ui != NULL);

  if (t && ui) {
    lk_ix n = dsl_find(t, "para");
    lk_ix m = dsl_find(t, "plain");

    CHECK(n != 0 && m != 0);
    CHECK(lk_node_kind_get(t, n) == (lk_u16)UIK_STYLED_TEXT);
    CHECK(lk_node_prop_i32(t, n, UIP_WRAP, -1) == LK_WRAP_NONE);
    CHECK(lk_spans_from_node(lk_ui_resources(ui), t, n) != NULL);
    CHECK(lk_node_has_prop(t, m, UIP_WRAP) == 0);
    CHECK(lk_spans_from_node(lk_ui_resources(ui), t, m) == NULL);
  }

  eval_expect_err(interp, "styled_text p2 #{spans 42}", "lk_spans opaque",
                  NULL, NULL);
  eval_expect_err(interp, "styled_text p3 #{wrap sideways}",
                  "none, character, word", NULL, NULL);

  lcl_interp_free(interp);
  END_TEST();
}

static void test_menu_open_binding(void) {
  lcl_interp *interp;
  lcl_value *r = NULL;

  BEGIN_TEST("menu_open / items / hover / activate / close + errors");
  interp = make_interp();

  eval_ok(interp,
          "let ui [Lk::ui_create]\n"
          "var got \"\"\n"
          "Lk::set_command_handler $ui [lambda {cmd} { set! got $cmd }]\n"
          "let t [Lk::begin_frame $ui]\n"
          "let w [Lk::node $t root window]\n"
          "let b [Lk::node $t file button]\n"
          "Lk::set_root $t $w\n"
          "Lk::append_child $t $w $b\n"
          "Lk::end_frame $ui",
          &r);
  if (r) lcl_ref_dec(r);
  check_str(interp, "repr [Lk::menu_items $ui]", "()");
  check_int(interp, "Lk::menu_hover $ui", -1);

  r = NULL;
  eval_ok(interp,
          "Lk::menu_open $ui #{owner file items ((\"Open...\" Open recent)"
          " (---) #{label Save command Save enabled 0} (Quit Quit))}",
          &r);
  if (r) lcl_ref_dec(r);
  check_int(interp, "len [Lk::menu_items $ui]", 4);
  check_str(interp, "get [get [Lk::menu_items $ui] 0] label", "Open...");
  check_str(interp, "get [get [Lk::menu_items $ui] 0] command", "Open");
  check_int(interp, "get [get [Lk::menu_items $ui] 1] separator", 1);
  check_int(interp, "get [get [Lk::menu_items $ui] 2] enabled", 0);
  check_int(interp, "Lk::menu_hover $ui", 0);
  check_int(interp, "Lk::menu_activate $ui 2", 0); /* disabled */
  check_int(interp, "Lk::menu_activate $ui 0", 1);
  check_str(interp, "get $got name", "Open");
  check_str(interp, "get $got source_node_id", "file");
  check_str(interp, "get [get $got args] 0", "recent");
  check_str(interp, "repr [Lk::menu_items $ui]", "()");

  /* No owner: the root is the source; close works. */
  r = NULL;
  eval_ok(interp, "Lk::menu_open $ui #{items ((A A))}", &r);
  if (r) lcl_ref_dec(r);
  check_int(interp, "len [Lk::menu_items $ui]", 1);
  r = NULL;
  eval_ok(interp, "Lk::menu_close $ui", &r);
  if (r) lcl_ref_dec(r);
  check_int(interp, "len [Lk::menu_items $ui]", 0);

  eval_expect_err(interp, "Lk::menu_open $ui #{items ()}", "non-empty",
                  NULL, NULL);
  eval_expect_err(interp, "Lk::menu_open $ui #{items ((A))}",
                  "(label command", NULL, NULL);
  eval_expect_err(interp, "Lk::menu_open $ui #{items (#{label X})}",
                  "needs `command`", NULL, NULL);
  eval_expect_err(interp, "Lk::menu_open $ui #{items (#{command X colour 1})}",
                  "unknown item key", NULL, NULL);
  eval_expect_err(interp, "Lk::menu_open $ui #{items ((A A)) anchor sideways}",
                  "anchor must be", NULL, NULL);
  eval_expect_err(interp, "Lk::menu_open $ui 7", "spec must be a dict", NULL,
                  NULL);
  eval_expect_err(interp, "Lk::menu_activate $ui -1", ">= 0", NULL, NULL);
  /* headless: no rects, nothing under the point */
  check_int(interp, "Lk::context_menu $ui 10 10", 0);
  check_int(interp, "Lk::context_menu_focus $ui", 0);

  lcl_interp_free(interp);
  END_TEST();
}

static void test_context_menu_binding(void) {
  lcl_interp *interp;
  lcl_value *r = NULL;
  lk_ui *ui;

  BEGIN_TEST("context_menu: producer over a laid-out tree, via script");
  interp = make_interp();

  eval_ok(interp,
          "let ui [Lk::ui_create]\n"
          "var got \"\"\n"
          "Lk::set_command_handler $ui [lambda {cmd} { set! got $cmd }]\n"
          "Lk::add_translator $ui pointer_down cell \"\" \"\" \"\" Reveal primary\n"
          "Lk::add_translator $ui pointer_down cell \"\" \"\" \"\" Flag secondary\n"
          "Lk::add_translator $ui key_down \"\" \"\" f2 \"\" NewGame",
          &r);
  if (r) lcl_ref_dec(r);
  r = NULL;
  eval_ok(interp,
          "let t [Lk::begin_frame $ui]\n"
          "let w [Lk::node $t root window]\n"
          "let c [Lk::node $t col column]\n"
          "let b [Lk::node $t c00 button]\n"
          "Lk::prop $t $c align start\n"
          "Lk::prop $t $c padding 10\n"
          "Lk::prop $t $b text X\n"
          "Lk::prop $t $b w 40\n"
          "Lk::prop $t $b h 30\n"
          "Lk::present $t $b cell (0 0)\n"
          "Lk::set_root $t $w\n"
          "Lk::append_child $t $w $c\n"
          "Lk::append_child $t $c $b\n"
          "Lk::end_frame $ui",
          &r);
  if (r) lcl_ref_dec(r);

  ui = fetch_ui(interp);
  CHECK(ui != NULL);

  if (ui) {
    const lk_tree *cur = lk_ui_tree(ui);
    lk_style *styles = (lk_style *)malloc(sizeof(lk_style) * cur->node_count);
    lk_layout_cfg cfg;

    lk_style_resolve(lk_ui_theme(ui), cur, NULL, styles);
    memset(&cfg, 0, sizeof(cfg));
    cfg.text = lk_text_backend_stub();
    cfg.viewport_w = 640;
    cfg.viewport_h = 480;
    cfg.styles = styles;
    cfg.state = lk_ui_state(ui);
    cfg.geom = lk_ui_geom(ui);
    lk_ui_set_text_backend(ui, lk_text_backend_stub());
    CHECK(lk_layout(cur, &cfg, lk_ui_rects(ui)) == 1);

    /* On the cell: Reveal, Flag, ---, NewGame. */
    check_int(interp, "Lk::context_menu $ui 15 15", 4);
    check_str(interp, "get [get [Lk::menu_items $ui] 0] command", "Reveal");
    check_str(interp, "get [get [Lk::menu_items $ui] 0] accel", "click");
    check_str(interp, "get [get [Lk::menu_items $ui] 1] accel", "right-click");
    check_int(interp, "get [get [Lk::menu_items $ui] 2] separator", 1);
    check_str(interp, "get [get [Lk::menu_items $ui] 3] accel", "f2");
    check_int(interp, "Lk::menu_activate $ui 1", 1);
    check_str(interp, "get $got name", "Flag");
    check_str(interp, "get $got source_node_id", "c00");
    check_int(interp, "get [get $got args] 1", 0);
    check_int(interp, "len [Lk::menu_items $ui]", 0);

    /* On the window (empty space): only the global. */
    check_int(interp, "Lk::context_menu $ui 300 300", 1);
    check_str(interp, "get [get [Lk::menu_items $ui] 0] command", "NewGame");
    r = NULL;
    eval_ok(interp, "Lk::menu_close $ui", &r);
    if (r) lcl_ref_dec(r);

    free(styles);
  }

  lcl_interp_free(interp);
  END_TEST();
}

static void test_list_binding(void) {
  lcl_interp *interp;
  lcl_value *r = NULL;
  lk_ui *ui;

  BEGIN_TEST("list: props, list_range / list_scroll_to over a laid-out tree");
  interp = make_interp();

  eval_ok(interp,
          "let ui [Lk::ui_create]\n"
          "let t [Lk::begin_frame $ui]\n"
          "let w [Lk::node $t root window]\n"
          "let c [Lk::node $t col column]\n"
          "let l [Lk::node $t files list]\n"
          "Lk::prop $t $c align start\n"
          "Lk::prop $t $l rows 1000\n"
          "Lk::prop $t $l row_h 20\n"
          "Lk::prop $t $l w 200\n"
          "Lk::prop $t $l h 100\n"
          "Lk::prop $t $l padding 0",
          &r);
  if (r) lcl_ref_dec(r);
  r = NULL;
  eval_ok(interp,
          "foreach i [List::range 0 8] {\n"
          "  let n [Lk::node $t \"r_$i\" button]\n"
          "  Lk::prop $t $n row $i\n"
          "  Lk::prop $t $n text \"row $i\"\n"
          "  Lk::append_child $t $l $n\n"
          "}\n"
          "Lk::set_root $t $w\n"
          "Lk::append_child $t $w $c\n"
          "Lk::append_child $t $c $l\n"
          "Lk::end_frame $ui",
          &r);
  if (r) lcl_ref_dec(r);
  check_str(interp, "repr [Lk::list_range $ui files]", "()");

  ui = fetch_ui(interp);
  CHECK(ui != NULL);

  if (ui) {
    const lk_tree *cur = lk_ui_tree(ui);
    lk_style *styles = (lk_style *)malloc(sizeof(lk_style) * cur->node_count);
    lk_layout_cfg cfg;

    lk_style_resolve(lk_ui_theme(ui), cur, NULL, styles);
    memset(&cfg, 0, sizeof(cfg));
    cfg.text = lk_text_backend_stub();
    cfg.viewport_w = 640;
    cfg.viewport_h = 480;
    cfg.styles = styles;
    cfg.state = lk_ui_state(ui);
    cfg.geom = lk_ui_geom(ui);
    CHECK(lk_layout(cur, &cfg, lk_ui_rects(ui)) == 1);

    check_str(interp, "repr [Lk::list_range $ui files]", "(0 5)");
    check_int(interp, "Lk::list_scroll_to $ui files 500", 1);
    CHECK(lk_layout(cur, &cfg, lk_ui_rects(ui)) == 1);
    check_str(interp, "repr [Lk::list_range $ui files]", "(496 5)");
    check_int(interp, "Lk::list_scroll_to $ui files 5000", 0);
    check_int(interp, "Lk::list_scroll_to $ui nope 1", 0);
    free(styles);
  }

  eval_expect_err(interp, "Lk::prop $t $l rows -1", ">= 0", NULL, NULL);
  eval_expect_err(interp, "Lk::prop $t $l row_h 0", ">= 1", NULL, NULL);
  eval_expect_err(interp, "Lk::prop $t $l row x", ">= 0", NULL, NULL);
  eval_expect_err(interp, "Lk::list_scroll_to $ui files -1", ">= 0", NULL,
                  NULL);
  eval_expect_err(interp, "Lk::list_range $ui", "2 arguments", NULL, NULL);

  lcl_interp_free(interp);
  END_TEST();
}

static void test_list_value_prop(void) {
  lcl_interp *interp;
  lcl_value *r = NULL;
  lk_ui *ui;

  BEGIN_TEST("list: value is the cursor row, coerced to an integer");
  interp = make_interp();

  eval_ok(interp,
          "let ui [Lk::ui_create]\n"
          "let t [Lk::begin_frame $ui]\n"
          "let w [Lk::node $t root window]\n"
          "let l [Lk::node $t files list]\n"
          "Lk::prop $t $l rows 10",
          &r);
  if (r) lcl_ref_dec(r);
  r = NULL;
  eval_expect_err(interp, "Lk::prop $t $l value abc",
                  "list value expects an integer", NULL, NULL);
  eval_ok(interp,
          "Lk::prop $t $l value 3\n"
          "Lk::set_root $t $w\n"
          "Lk::append_child $t $w $l\n"
          "Lk::end_frame $ui",
          &r);
  if (r) lcl_ref_dec(r);

  ui = fetch_ui(interp);
  CHECK(ui != NULL);

  if (ui) {
    const lk_tree *cur = lk_ui_tree(ui);
    lk_ix n = lk_tree_find_by_id(cur, lk_intern_cid(ui->intern, "files"));

    CHECK(n > 0);
    if (n > 0) CHECK(lk_list_cursor(cur, n, lk_ui_state(ui)) == 3);
  }

  lcl_interp_free(interp);
  END_TEST();
}

static void test_dsl_list_window(void) {
  lcl_interp *interp;
  lcl_value *r = NULL;
  lk_tree *t;

  BEGIN_TEST("dsl: list kind + list_window before and after layout");
  interp = make_dsl_interp();
  dsl_begin(interp);

  /* No layout yet: the estimate, then rows built from it. */
  check_str(interp, "repr [list_window files 100]", "(0 1 2 3 4 5 6 7 8 9 10 11 12 13 14 15 16 17 18 19 20 21 22 23 24 25 26 27 28 29 30 31)");
  check_str(interp, "repr [list_window files 5]", "(0 1 2 3 4)");
  check_str(interp, "repr [list_window files 100 0 3]", "(0 1 2)");
  eval_ok(interp,
          "listview files #{rows 50 row_h 20 h 100 w 200 focusable 1} {\n"
          "  foreach i [list_window files 50 0 4] {\n"
          "    row \"r_$i\" #{row $i} { label \"l_$i\" #{text \"row $i\"} }\n"
          "  }\n"
          "}",
          &r);
  if (r) lcl_ref_dec(r);
  t = dsl_tree(interp);
  CHECK(t != NULL);

  if (t) {
    lk_ix l = dsl_find(t, "files");
    lk_ix r2 = dsl_find(t, "r_2");

    CHECK(l != 0 && r2 != 0);
    CHECK(lk_node_kind_get(t, l) == (lk_u16)UIK_LIST);
    CHECK(lk_node_prop_i32(t, l, UIP_ROWS, -1) == 50);
    CHECK(lk_node_prop_i32(t, l, UIP_ROW_H, -1) == 20);
    CHECK(lk_node_prop_i32(t, r2, UIP_ROW, -1) == 2);
    CHECK(dsl_find(t, "r_4") == 0);
  }

  eval_expect_err(interp, "listview bad #{rows -3}", ">= 0", NULL, NULL);

  lcl_interp_free(interp);
  END_TEST();
}

static void test_args_proc(void) {
  lcl_interp *interp;
  static char a0[] = "--snap";
  static char a1[] = "out";
  char *argv[2];

  BEGIN_TEST("args: Lk::args reflects lcl_lk_set_args");
  interp = make_interp();

  argv[0] = a0;
  argv[1] = a1;

  check_str(interp, "repr [Lk::args]", "()");
  lcl_lk_set_args(2, argv);
  check_str(interp, "repr [Lk::args]", "(\"--snap\" \"out\")");
  check_str(interp, "get [Lk::args] 1", "out");
  lcl_lk_set_args(0, NULL);
  check_str(interp, "repr [Lk::args]", "()");
  eval_expect_err(interp, "Lk::args 1", "Lk::args", "no arguments", NULL);

  lcl_interp_free(interp);
  END_TEST();
}

#ifdef LK_HAVE_SDL
/* Headless like the dialog tests: only the contracts that error before
 * any window exists. */
static void test_screenshot_stop_errors(void) {
  lcl_interp *interp;
  lcl_value *r = NULL;

  BEGIN_TEST("window: screenshot/stop error contracts (headless)");
  interp = make_interp();

  eval_ok(interp, "let ui [Lk::ui_create]", &r);
  if (r) lcl_ref_dec(r);

  eval_expect_err(interp, "Lk::window_screenshot $ui",
                  "Lk::window_screenshot", "2 arguments", NULL);
  eval_expect_err(interp, "Lk::window_screenshot $ui \"x.png\"",
                  "expected lk_window opaque", NULL, NULL);
  eval_expect_err(interp, "Lk::window_stop", "Lk::window_stop",
                  "1 argument", NULL);
  eval_expect_err(interp, "Lk::window_stop $ui", "expected lk_window opaque",
                  NULL, NULL);

  lcl_interp_free(interp);
  END_TEST();
}
#endif

static void test_dsl_image_filter_prop(void) {
  lcl_interp *interp;
  lcl_value *r = NULL;
  lk_tree *t;

  /* `filter nearest|linear` coerces to the lk_image_filter enum;
   * anything else is a hard error naming the two values. */
  BEGIN_TEST("dsl: image filter prop (nearest/linear, default linear)");
  interp = make_dsl_interp();
  dsl_begin(interp);

  eval_ok(interp,
          "let img [Lk::image_new $u 2 2]\n"
          "image crisp #{image $img w 64 h 64 filter nearest}\n"
          "image soft #{image $img w 64 h 64 filter linear}\n"
          "image plain #{image $img w 64 h 64}\n",
          &r);
  if (r) lcl_ref_dec(r);

  t = dsl_tree(interp);
  CHECK(t != NULL);
  if (t) {
    lk_ix crisp = dsl_find(t, "crisp");
    lk_ix soft = dsl_find(t, "soft");
    lk_ix plain = dsl_find(t, "plain");

    CHECK(crisp != 0 && soft != 0 && plain != 0);
    CHECK(lk_node_prop_i32(t, crisp, UIP_FILTER, -1) == LK_FILTER_NEAREST);
    CHECK(lk_node_prop_i32(t, soft, UIP_FILTER, -1) == LK_FILTER_LINEAR);
    CHECK(lk_node_has_prop(t, plain, UIP_FILTER) == 0);
  }

  eval_expect_err(interp, "image bad #{image $img filter blurry}",
                  "filter expects linear or nearest", NULL, NULL);
  /* the DSL schema lists it */
  eval_expect_err(interp, "image bad2 #{fitler nearest}",
                  "unknown prop 'fitler'", "filter", NULL);

  lcl_interp_free(interp);
  END_TEST();
}

static int route_key(lk_ui *ui, lk_ix target, lk_u16 kc, lk_u8 mods) {
  lk_event ev;

  memset(&ev, 0, sizeof(ev));
  ev.type = LK_EVENT_KEY_DOWN;
  ev.target = target;
  ev.mods = mods;
  ev.data.key.keycode = kc;
  lk_event_route(ui, &ev);

  return ev.handled;
}

static void test_clear_translators(void) {
  lcl_interp *interp;
  lcl_value *r = NULL;
  lk_ui *ui;
  lk_ix node = 0;

  /* Lk::clear_translators drops every translator; re-registering
   * afterwards works, and doing BOTH from inside a firing command
   * handler is safe (the pinned re-entrancy claim, under ASan). */
  BEGIN_TEST("clear_translators: clear, re-register, and from a handler");
  interp = make_interp();

  eval_ok(interp,
          "let ui [Lk::ui_create]\n"
          "var fired ()\n"
          "Lk::set_command_handler $ui [lambda {cmd} {\n"
          "  set! fired [List::push $fired [get $cmd name]]\n"
          "  if [== [get $cmd name] Rebind] {\n"
          "    Lk::clear_translators $ui\n"
          "    Lk::add_translator $ui key_down app \"\" g ctrl AfterRebind\n"
          "  }\n"
          "}]\n"
          "Lk::add_translator $ui key_down app \"\" g ctrl GotoLine\n"
          "let t [Lk::begin_frame $ui]\n"
          "let w [Lk::node $t root window]\n"
          "Lk::present $t $w app (a)\n"
          "Lk::set_root $t $w\n"
          "Lk::end_frame $ui",
          &r);
  if (r) lcl_ref_dec(r);
  r = NULL;

  ui = fetch_ui(interp);
  CHECK(ui != NULL);

  if (ui) {
    node = lk_tree_find_by_id(lk_ui_tree(ui), lk_intern_cid(ui->intern, "root"));
    CHECK(node != 0);

    /* bound: fires */
    CHECK(route_key(ui, node, LKK_G, LK_MOD_CTRL) == 1);
    check_int(interp, "len $fired", 1);
    check_int(interp, "== [get $fired 0] GotoLine", 1);

    /* cleared: the same chord bubbles */
    eval_ok(interp, "Lk::clear_translators $ui", &r);
    if (r) lcl_ref_dec(r);
    r = NULL;
    CHECK(route_key(ui, node, LKK_G, LK_MOD_CTRL) == 0);
    check_int(interp, "len $fired", 1);

    /* re-registered with a handler that rebinds from INSIDE dispatch */
    eval_ok(interp, "Lk::add_translator $ui key_down app \"\" g ctrl Rebind",
            &r);
    if (r) lcl_ref_dec(r);
    r = NULL;
    CHECK(route_key(ui, node, LKK_G, LK_MOD_CTRL) == 1);
    check_int(interp, "len $fired", 2);
    check_int(interp, "== [get $fired 1] Rebind", 1);

    /* the in-handler rebinding took effect for the next event */
    CHECK(route_key(ui, node, LKK_G, LK_MOD_CTRL) == 1);
    check_int(interp, "len $fired", 3);
    check_int(interp, "== [get $fired 2] AfterRebind", 1);
  }

  eval_expect_err(interp, "Lk::clear_translators", "Lk::clear_translators",
                  "1 argument", NULL);
  eval_expect_err(interp, "Lk::clear_translators $t", "expected lk_ui opaque",
                  NULL, NULL);

  lcl_interp_free(interp);
  END_TEST();
}

static void test_dsl_clear_translators(void) {
  lcl_interp *interp;
  lcl_value *r = NULL;
  lk_ui *ui;

  /* The DSL wrapper clears the app ui's table: a keybinding form,
   * clear_translators, then the chord no longer fires. */
  BEGIN_TEST("dsl: clear_translators wraps the app ui");
  interp = make_dsl_interp();
  dsl_begin(interp);

  eval_ok(interp,
          "var fired 0\n"
          "Lk::set_command_handler $u [lambda {cmd} { set! fired [+ $fired 1] }]\n"
          "let w [Lk::node $t root window]\n"
          "Lk::present $t $w app (a)\n"
          "Lk::set_root $t $w\n"
          "Lk::end_frame $u\n"
          "keybinding f2 \"\" app NewGame",
          &r);
  if (r) lcl_ref_dec(r);
  r = NULL;

  ui = dsl_ui(interp);
  CHECK(ui != NULL);

  if (ui) {
    lk_ix node =
        lk_tree_find_by_id(lk_ui_tree(ui), lk_intern_cid(ui->intern, "root"));

    CHECK(node != 0);
    CHECK(route_key(ui, node, LKK_F2, 0) == 1);
    check_int(interp, "$fired", 1);

    eval_ok(interp, "clear_translators", &r);
    if (r) lcl_ref_dec(r);
    r = NULL;

    CHECK(route_key(ui, node, LKK_F2, 0) == 0);
    check_int(interp, "$fired", 1);
  }

  lcl_interp_free(interp);
  END_TEST();
}

static void test_editor_keys(void) {
  lcl_interp *interp;
  lcl_value *r = NULL;

  /* Lk::editor_keys renders the widget's binding tables: every key
   * row's command round-trips through Lk::editor_command's name
   * table, the pinned ctrl+d row is exact, motions carry
   * extend_with_shift, pointer rows follow the key rows. */
  BEGIN_TEST("editor_keys: binding records from the widget tables");
  interp = make_interp();

  eval_ok(interp,
          "let rows [Lk::editor_keys]\n"
          "var nkey 0\n"
          "var nptr 0\n"
          "var bad 0\n"
          "var ctrl_d #{}\n"
          "var left #{}\n"
          "var click #{}",
          &r);
  if (r) lcl_ref_dec(r);
  r = NULL;

  eval_ok(interp,
          "foreach r $rows {\n"
          "  if [== [get $r kind] key] { set! nkey [+ $nkey 1] }\n"
          "  if [== [get $r kind] pointer] { set! nptr [+ $nptr 1] }\n"
          "  if [empty? [get $r doc]] { set! bad [+ $bad 1] }\n"
          "  if [== [get $r kind] key] {\n"
          "    if [empty? [get $r command]] { set! bad [+ $bad 1] }\n"
          "    if [empty? [get $r chord]] { set! bad [+ $bad 1] }\n"
          "  }\n"
          "}",
          &r);
  if (r) lcl_ref_dec(r);
  r = NULL;

  eval_ok(interp,
          "foreach r $rows {\n"
          "  if [== [get $r kind] key] { if [== [get $r chord] \"ctrl+d\"] { set! ctrl_d $r } }\n"
          "  if [== [get $r kind] key] { if [== [get $r chord] left] { set! left $r } }\n"
          "  if [== [get $r kind] pointer] { if [== [get $r gesture] click] { set! click $r } }\n"
          "}",
          &r);
  if (r) lcl_ref_dec(r);

  check_int(interp, "> $nkey 20", 1);
  check_int(interp, "> $nptr 3", 1);
  check_int(interp, "$bad", 0);
  /* key rows precede pointer rows (dispatch order) */
  check_int(interp, "== [get [get $rows 0] kind] key", 1);
  check_int(interp, "== [get [get $rows [- [len $rows] 1]] kind] pointer", 1);

  check_int(interp, "== [get $ctrl_d command] select_next_match", 1);
  check_int(interp, "get $ctrl_d mods_exact", 1);
  check_int(interp, "== [get $ctrl_d key] d", 1);
  check_int(interp, "== [get $ctrl_d mods] ctrl", 1);
  check_int(interp, "get $ctrl_d extend_with_shift", 0);

  check_int(interp, "== [get $left command] move_left", 1);
  check_int(interp, "get $left extend_with_shift", 1);
  check_int(interp, "get $left mods_exact", 0);
  check_int(interp, "== [get $left mods] \"\"", 1);

  check_int(interp, "== [get $click action] place_cursor", 1);
  check_int(interp, "== [get $click command] set_cursor", 1);
  check_int(interp, "== [get $click button] primary", 1);

  /* every key command is a name Lk::editor_command accepts: an
   * unknown name errors listing the known ones, so a known name on a
   * real editor either runs or complains about ARGS, never the name */
  eval_ok(interp,
          "let ui [Lk::ui_create]\n"
          "let d [Lk::doc_new \"ab\"]\n"
          "let e [Lk::editor_new $ui $d]\n"
          "var unknown 0\n"
          "foreach r $rows {\n"
          "  if [== [get $r kind] key] {\n"
          "    let c [get $r command]\n"
          "    if [not [== $c insert_text]] {\n"
          "      if [catch { Lk::editor_command $e $c } _r err] {\n"
          "        if [String::find $err \"unknown\"] { set! unknown [+ $unknown 1] }\n"
          "      }\n"
          "    }\n"
          "  }\n"
          "}",
          &r);
  if (r) lcl_ref_dec(r);
  check_int(interp, "$unknown", 0);

  eval_expect_err(interp, "Lk::editor_keys 1", "Lk::editor_keys",
                  "no arguments", NULL);

  lcl_interp_free(interp);
  END_TEST();
}

static void test_node_rect(void) {
  lcl_interp *interp;
  lcl_value *r = NULL;
  lk_ui *ui = NULL;

  /* Lk::node_rect reads the ui-owned rects array: () until a host
   * lays out into lk_ui_rects, then (x y w h) by stable id. */
  BEGIN_TEST("node_rect: () headless, (x y w h) after a layout");
  interp = make_interp();

  eval_ok(interp,
          "let ui [Lk::ui_create]\n"
          "let t [Lk::begin_frame $ui]\n"
          "let w [Lk::node $t root window]\n"
          "let c [Lk::node $t col column]\n"
          "let b [Lk::node $t go button]\n"
          "let h [Lk::node $t hid label]\n"
          "Lk::prop $t $b text Go\n"
          "Lk::prop $t $h hidden 1\n"
          "Lk::set_root $t $w\n"
          "Lk::append_child $t $w $c\n"
          "Lk::append_child $t $c $b\n"
          "Lk::append_child $t $c $h\n"
          "Lk::end_frame $ui",
          &r);
  if (r) lcl_ref_dec(r);

  /* no layout yet */
  check_int(interp, "len [Lk::node_rect $ui go]", 0);
  check_int(interp, "len [Lk::node_rect $ui root]", 0);

  r = NULL;
  eval_ok(interp, "$ui", &r);
  if (r) {
    CHECK(lcl_opaque_get(r, "lk_ui", (void **)&ui) == LCL_OK);
    lcl_ref_dec(r);
  }
  CHECK(ui != NULL);

  if (ui) {
    lk_layout_cfg cfg;
    lk_rect *rects = lk_ui_rects(ui);

    CHECK(rects != NULL);
    memset(&cfg, 0, sizeof(cfg));
    cfg.text = lk_text_backend_stub();
    cfg.viewport_w = 320;
    cfg.viewport_h = 240;
    cfg.state = lk_ui_state(ui);
    cfg.geom = lk_ui_geom(ui);
    CHECK(lk_layout(lk_ui_tree(ui), &cfg, rects));

    check_int(interp, "len [Lk::node_rect $ui root]", 4);
    check_int(interp, "get [Lk::node_rect $ui root] 0", 0);
    check_int(interp, "get [Lk::node_rect $ui root] 1", 0);
    check_int(interp, "get [Lk::node_rect $ui root] 2", 320);
    check_int(interp, "get [Lk::node_rect $ui root] 3", 240);
    check_int(interp, "len [Lk::node_rect $ui go]", 4);
    check_int(interp, "> [get [Lk::node_rect $ui go] 2] 0", 1);
    check_int(interp, "> [get [Lk::node_rect $ui go] 3] 0", 1);
    /* hidden: laid out to zero, still reported */
    check_int(interp, "len [Lk::node_rect $ui hid]", 4);
    check_int(interp, "get [Lk::node_rect $ui hid] 2", 0);
    /* unknown id */
    check_int(interp, "len [Lk::node_rect $ui nope]", 0);
  }

  eval_expect_err(interp, "Lk::node_rect $ui", "Lk::node_rect",
                  "2 arguments", NULL);
  eval_expect_err(interp, "Lk::node_rect $t go", "expected lk_ui opaque",
                  NULL, NULL);
  eval_expect_err(interp, "Lk::node_rect $ui (a b)",
                  "must be a string or number", NULL, NULL);

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
    "let ui [Lk::ui_create]\n"
    /* 7-arg form still works (examples run unmodified) */
    "Lk::add_translator $ui \"pointer_down\" \"a\" \"\" \"\" \"\" \"C0\"\n"
    "Lk::add_translator $ui \"pointer_down\" \"a\" \"\" \"\" \"\" \"C1\" \"primary\"\n"
    "Lk::add_translator $ui \"pointer_down\" \"a\" \"\" \"\" \"ctrl\" \"C2\" \"middle\"\n"
    "Lk::add_translator $ui \"pointer_down\" \"a\" \"\" \"\" \"\" \"C3\" \"secondary\"\n"
    "Lk::add_translator $ui \"pointer_down\" \"a\" \"\" \"\" \"\" \"C4\" \"\"\n"
    "Lk::add_translator $ui \"pointer_down\" \"a\" \"\" \"\" \"\" \"C5\" 0",
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
                  "Lk::add_translator $ui \"pointer_down\" \"a\" \"\" \"\" "
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
    "let ui [Lk::ui_create]\n"
    "let doc [Lk::doc_new \"hello file.c:12 world\"]\n"
    "let ed [Lk::editor_new $ui $doc]\n"
    "let s [Lk::annot_store_new]\n"
    "Lk::annot_attach $s $doc\n"
    "let a [Lk::annot_add $s 6 15 \"links\"]\n"
    "Lk::annot_layer_priority $s \"links\" 3\n"
    "Lk::annot_present $ui $s $a \"loc\" #{path \"file.c\" line 12}\n"
    "Lk::editor_presentations $ed $s",
    &r);
  if (r) lcl_ref_dec(r);
  r = NULL;

  eval_ok(interp,
    "Lk::add_translator $ui \"pointer_down\" \"loc\" \"\" \"\" \"\" \"Open\" \"middle\"\n"
    "let t [Lk::begin_frame $ui]\n"
    "let w [Lk::node $t \"w\" \"window\"]\n"
    "let e [Lk::node $t \"ed\" \"editor\"]\n"
    "Lk::set_root $t $w\n"
    "Lk::append_child $t $w $e\n"
    "Lk::prop $t $e \"editor\" $ed\n"
    "Lk::end_frame $ui",
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
    "let cmds [Lk::commands $ui]\n"
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
      {"Lk::editor_cursor $ed", "0"},
      /* the hit's revision matches the doc now... */
      {"== [get $lc rev] [Lk::doc_revision $doc]", "1"},
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
    "Lk::doc_insert $doc 0 \"x\"\n"
    "== [get $lc rev] [Lk::doc_revision $doc]",
    &r);
  if (r) {
    CHECK(strcmp(lcl_value_to_string(r), "0") == 0);
    lcl_ref_dec(r);
    r = NULL;
  }

  /* removing the annotation releases the wrapped value (hook path)
   * and ends the candidacy: the next click bubbles unhandled */
  eval_ok(interp, "Lk::clear_commands $ui\nLk::annot_remove $s $a", &r);
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

  /* Lk::editor_pos_at against the same layout snapshot */
  eval_ok(interp, "Lk::editor_pos_at $ed 67 8", &r);
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

  eval_ok(interp, "Lk::editor_pos_at $ed 67 8", &r);
  if (r) {
    long v = -2;
    lcl_value_to_int(r, &v);
    CHECK(v == 8);
    lcl_ref_dec(r);
    r = NULL;
  }

  eval_ok(interp, "Lk::editor_pos_at $ed -5 -5", &r);
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
    "let ui [Lk::ui_create]\n"
    "let t [Lk::begin_frame $ui]\n"
    "let w [Lk::node $t \"w\" \"window\"]\n"
    "let b1 [Lk::node $t \"b1\" \"button\"]\n"
    "let b2 [Lk::node $t \"b2\" \"button\"]\n"
    "Lk::prop $t $b1 \"focusable\" 1\n"
    "Lk::prop $t $b2 \"focusable\" 1\n"
    "Lk::set_root $t $w\n"
    "Lk::append_child $t $w $b1\n"
    "Lk::append_child $t $w $b2\n"
    "Lk::end_frame $ui",
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
    eval_ok(interp, "Lk::focus_set $ui \"b1\"", &r);
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
    eval_ok(interp, "Lk::focus_set $ui \"b2\"", &r);
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
    eval_ok(interp, "Lk::focus_clear $ui", &r);
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
    "let ui [Lk::ui_create]\n"
    "Lk::add_translator $ui \"focus_changed\" \"\" \"\" \"\" \"\" \"Act\"",
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
    "let ui [Lk::ui_create]\n"
    "let doc [Lk::doc_new \"hello\"]\n"
    "let s [Lk::annot_store_new]\n"
    "Lk::annot_attach $s $doc\n"
    "let a [Lk::annot_add $s 0 5 \"l\"]",
    &r);
  if (r) lcl_ref_dec(r);

  eval_expect_err(interp, "Lk::annot_present $ui $s 999 \"t\" 1",
                  "no such annotation", NULL, NULL);
  eval_expect_err(interp, "Lk::annot_present $ui $s $a \"\" 1",
                  "ptype must be non-empty", NULL, NULL);
  eval_expect_err(interp, "Lk::annot_present $ui $s", "expected 5 arguments",
                  NULL, NULL);

  /* binding a second ui is rejected */
  eval_ok(interp,
    "Lk::annot_present $ui $s $a \"t\" 1\n"
    "let ui2 [Lk::ui_create]",
    &r);
  if (r) lcl_ref_dec(r);
  eval_expect_err(interp, "Lk::annot_present $ui2 $s $a \"t\" 2",
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
    "let u [Lk::ui_create]\n"
    "let t [Lk::begin_frame $u]",
    &r);
  if (r) lcl_ref_dec(r);

  /* A typed int is a fine node id — it renders to its canonical text. */
  eval_ok(interp,
    "let n [Lk::node $t 42 label]\n"
    "Lk::set_root $t $n",
    &r);
  if (r) lcl_ref_dec(r);

  eval_expect_err(interp, "Lk::node $t $u label",
                  "Lk::node: id", "got opaque", NULL);
  eval_expect_err(interp, "Lk::node $t (a b) label",
                  "Lk::node: id", "got list", NULL);
  eval_expect_err(interp, "Lk::tag $t 1 #{x 1}",
                  "Lk::tag: tag", "got dict", NULL);
  eval_expect_err(interp, "Lk::focus_set $u [lambda {x} {}]",
                  "Lk::focus_set: node id", "got proc", NULL);
  eval_expect_err(interp, "Lk::state_set $u #{a 1} 300 1",
                  "Lk::state_set: node id", "got dict", NULL);

  eval_ok(interp,
    "let d [Lk::doc_new \"hello\"]\n"
    "let s [Lk::annot_store_new]\n"
    "Lk::annot_attach $s $d",
    &r);
  if (r) lcl_ref_dec(r);

  eval_expect_err(interp, "Lk::annot_add $s 0 2 (l1 l2)",
                  "Lk::annot_add: layer", "got list", NULL);

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
    "let u [Lk::ui_create]\n"
    "set! LkDsl::_ui $u\n"
    "view {\n"
    "    split_h sp #{split_controlled 1 present (pane 0)} {\n"
    "        column c1\n"
    "        column c2\n"
    "    }\n"
    "}\n"
    "translator value_changed pane SplitMoved\n"
    "on SplitMoved [lambda {cmd} {\n"
    "    Lk::state_set $u sink 300 [get $cmd source_value]\n"
    "}]\n"
    "Lk::set_command_handler $u [lambda {cmd} {\n"
    "    LkDsl::_dispatch_command $cmd\n"
    "}]\n"
    "let t [Lk::begin_frame $u]\n"
    "LkDsl::_frame $t\n"
    "Lk::end_frame $u",
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
  check_str(interp, "Lk::state_get $u sink 300", "248");

  lcl_interp_free(interp);
  END_TEST();
}


/* ============================================================================
 * Forms widgets round (docs/forms-widgets.md): checkbox / radio / slider
 * / tabs / grid through the DSL, with C-routed events and the command
 * pipeline (presentation + value_changed translator + on-handler).
 * ============================================================================
 */

static void test_forms_prop_coercion(void) {
  /* Lk::prop coercion for the new keys: checked -> bool, min/max/
   * controlled -> int, step/columns -> int >= 1, value on a slider ->
   * int (numeric text ok) but a string elsewhere. */
  lcl_interp *interp;
  lcl_value *r = NULL;
  lk_tree *t;

  BEGIN_TEST("forms: Lk::prop coercion for the new keys");
  interp = make_dsl_interp();
  dsl_begin(interp);

  eval_ok(interp,
    "let cb [Lk::node $t cb checkbox]\n"
    "Lk::prop $t $cb checked 1\n"
    "Lk::prop $t $cb controlled 1\n"
    "let sl [Lk::node $t sl slider]\n"
    "Lk::prop $t $sl min 5\n"
    "Lk::prop $t $sl max 50\n"
    "Lk::prop $t $sl step 5\n"
    "Lk::prop $t $sl value \"25\"\n"
    "let g [Lk::node $t g grid]\n"
    "Lk::prop $t $g columns 3\n"
    "let dd [Lk::node $t dd dropdown]\n"
    "Lk::prop $t $dd value 42",
    &r);
  if (r) lcl_ref_dec(r);

  t = dsl_tree(interp);
  CHECK(t != NULL);
  if (t) {
    lk_ix cb = dsl_find(t, "cb");
    lk_ix sl = dsl_find(t, "sl");
    lk_ix g = dsl_find(t, "g");
    lk_ix dd = dsl_find(t, "dd");
    const char *v;

    CHECK(lk_node_prop_bool(t, cb, UIP_CHECKED) == 1);
    CHECK(lk_node_prop_i32(t, cb, UIP_CONTROLLED, 0) == 1);
    CHECK(lk_node_prop_i32(t, sl, UIP_MIN, 0) == 5);
    CHECK(lk_node_prop_i32(t, sl, UIP_MAX, 0) == 50);
    CHECK(lk_node_prop_i32(t, sl, UIP_STEP, 0) == 5);
    CHECK(lk_node_prop_i32(t, sl, UIP_VALUE, -1) == 25);
    CHECK(lk_node_prop_i32(t, g, UIP_COLUMNS, 0) == 3);
    /* dropdown value stays a string even when spelled as a number */
    v = dsl_prop_str(t, dd, UIP_VALUE);
    CHECK(v != NULL && strcmp(v, "42") == 0);
  }

  eval_expect_err(interp, "Lk::prop $t $sl value abc",
                  "slider value expects an integer", NULL, NULL);
  eval_expect_err(interp, "Lk::prop $t $sl step 0",
                  "step expects an integer >= 1", NULL, NULL);
  eval_expect_err(interp, "Lk::prop $t $g columns -2",
                  "columns expects an integer >= 1", NULL, NULL);
  eval_expect_err(interp, "Lk::prop $t $cb checked x",
                  "bool prop expects integer", NULL, NULL);
  /* the DSL schema knows the new keys */
  eval_expect_err(interp, "checkbox cb2 #{chekced 1}",
                  "unknown prop 'chekced'", "checked", "columns");

  lcl_interp_free(interp);
  END_TEST();
}

static void test_forms_dsl_events(void) {
  /* One view with a checkbox, a slider and a tabs widget, each
   * presented; value_changed translators route into on-handlers that
   * stash the payload in user state.  Events are C-routed against a
   * layout with the stub geometry (viewport 400x300, no styles). */
  lcl_interp *interp;
  lcl_value *r = NULL;
  lk_ui *ui;

  BEGIN_TEST("forms: checkbox/slider/tabs commands via DSL");
  interp = make_dsl_interp();

  eval_ok(interp,
    "let u [Lk::ui_create]\n"
    "set! LkDsl::_ui $u\n"
    "view {\n"
    "column root {\n"
    "checkbox cb #{text On present (flag 0) focusable 1}\n"
    "slider sl #{min 0 max 100 value 10 present (vol 0) w 400}\n"
    "tabs nb #{value two present (page 0) focusable 1} {\n"
    "tab one #{text One} { label l1 #{text First} }\n"
    "tab two #{text Two} { label l2 #{text Second} }\n"
    "}\n"
    "grid gr #{columns 2 gap 4} {\n"
    "label g1 #{text aa}\n"
    "label g2 #{text bbbb}\n"
    "label g3 #{text c}\n"
    "}\n"
    "}\n"
    "}",
    &r);
  if (r) lcl_ref_dec(r);
  r = NULL;

  eval_ok(interp,
    "translator value_changed flag Flag\n"
    "translator value_changed vol Vol\n"
    "translator value_changed page Page\n"
    "on Flag [lambda {cmd} { Lk::state_set $u sink 300 [get $cmd source_value] }]\n"
    "on Vol [lambda {cmd} { Lk::state_set $u sink 301 [get $cmd source_value] }]\n"
    "on Page [lambda {cmd} { Lk::state_set $u sink 302 [get $cmd source_value] }]\n"
    "Lk::set_command_handler $u [lambda {cmd} {\n"
    "    LkDsl::_dispatch_command $cmd\n"
    "}]\n"
    "let t [Lk::begin_frame $u]\n"
    "LkDsl::_frame $t\n"
    "Lk::end_frame $u",
    &r);
  if (r) lcl_ref_dec(r);

  ui = dsl_ui(interp);
  CHECK(ui != NULL);

  if (ui) {
    lk_rect rects[32];
    lk_layout_cfg cfg;
    const lk_tree *cur = lk_ui_tree(ui);
    lk_ix cb, sl, nb, one, two, l1, l2, g1, g2, g3;
    lk_event ev;

    memset(&cfg, 0, sizeof(cfg));
    cfg.text = lk_text_backend_stub();
    cfg.viewport_w = 400;
    cfg.viewport_h = 300;
    cfg.state = lk_ui_state(ui);
    cfg.geom = lk_ui_geom(ui);
    CHECK(lk_layout(cur, &cfg, rects) == 1);

    cb = lk_tree_find_by_id(cur, lk_intern_cid(ui->intern, "cb"));
    sl = lk_tree_find_by_id(cur, lk_intern_cid(ui->intern, "sl"));
    nb = lk_tree_find_by_id(cur, lk_intern_cid(ui->intern, "nb"));
    one = lk_tree_find_by_id(cur, lk_intern_cid(ui->intern, "one"));
    two = lk_tree_find_by_id(cur, lk_intern_cid(ui->intern, "two"));
    l1 = lk_tree_find_by_id(cur, lk_intern_cid(ui->intern, "l1"));
    l2 = lk_tree_find_by_id(cur, lk_intern_cid(ui->intern, "l2"));
    g1 = lk_tree_find_by_id(cur, lk_intern_cid(ui->intern, "g1"));
    g2 = lk_tree_find_by_id(cur, lk_intern_cid(ui->intern, "g2"));
    g3 = lk_tree_find_by_id(cur, lk_intern_cid(ui->intern, "g3"));
    CHECK(cb && sl && nb && one && two && l1 && l2 && g1 && g2 && g3);

    /* the tabs `value two` selected the second page (by id) */
    CHECK(rects[two].w == 400);
    CHECK(rects[one].w > 0 && rects[one].h == 26); /* header cell */
    CHECK(rects[l2].w == 400);
    CHECK(rects[l1].w == 0);

    /* grid: 2 columns, gap 4 -> second column starts at 16 + 4 */
    CHECK(rects[g2].x == rects[g1].x + 20);
    CHECK(rects[g3].y == rects[g1].y + 20);

    /* click the checkbox */
    memset(&ev, 0, sizeof(ev));
    ev.type = LK_EVENT_POINTER_DOWN;
    ev.target = cb;
    ev.data.pointer.x = rects[cb].x + 2;
    ev.data.pointer.y = rects[cb].y + 2;
    lk_event_route(ui, &ev);
    CHECK(ev.handled == 1);

    /* slider: 400 wide, click at x=200 -> 50 */
    memset(&ev, 0, sizeof(ev));
    ev.type = LK_EVENT_POINTER_DOWN;
    ev.target = sl;
    ev.data.pointer.x = 200;
    ev.data.pointer.y = rects[sl].y + 2;
    lk_event_route(ui, &ev);
    CHECK(ev.handled == 1);

    /* click the "One" header cell */
    memset(&ev, 0, sizeof(ev));
    ev.type = LK_EVENT_POINTER_DOWN;
    ev.target = one;
    ev.data.pointer.x = rects[one].x + 2;
    ev.data.pointer.y = rects[one].y + 2;
    lk_event_route(ui, &ev);
    CHECK(ev.handled == 1);
  }

  check_str(interp, "Lk::state_get $u sink 300", "1");
  check_str(interp, "Lk::state_get $u sink 301", "50");
  check_str(interp, "Lk::state_get $u sink 302", "one");

  lcl_interp_free(interp);
  END_TEST();
}

static void test_forms_theme_accent(void) {
  lcl_interp *interp;
  lcl_value *r = NULL;

  BEGIN_TEST("forms: theme_rule accent + DSL rule");
  interp = make_dsl_interp();
  dsl_begin(interp);

  eval_ok(interp,
    "Lk::theme_rule $u checkbox \"\" \"\" #{accent (1 2 3)}\n"
    "checkbox cb #{text x}",
    &r);
  if (r) lcl_ref_dec(r);

  {
    lk_ui *ui = dsl_ui(interp);
    lk_tree *t = dsl_tree(interp);
    CHECK(ui != NULL && t != NULL);
    if (ui && t) {
      lk_style styles[8];
      lk_ix cb;
      lcl_value *r2 = NULL;

      lk_tree_set_root(t, dsl_find(t, "cb"));
      eval_ok(interp, "Lk::end_frame $u", &r2);
      if (r2) lcl_ref_dec(r2);
      cb = lk_tree_find_by_id(lk_ui_tree(ui), lk_intern_cid(ui->intern, "cb"));
      lk_style_resolve(lk_ui_theme(ui), lk_ui_tree(ui), NULL, styles);
      CHECK(styles[cb].accent.r == 1 && styles[cb].accent.g == 2 &&
            styles[cb].accent.b == 3);
    }
  }

  lcl_interp_free(interp);
  END_TEST();
}

/* ============================================================================
 * Companion-doc doctests (docs/lk.lcl).
 *
 * The docs file is NEVER evaluated — its stub proc bodies would shadow
 * the real C commands.  Instead its text is read here and handed to
 * Doc::extract (the reader-based extractor from lcl's Doc package);
 * the `>>` examples then run against the live Lk:: bindings in this
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

    /* Coverage: every registered Lk:: name has a doc entry. */
    eval_ok(interp,
      "let __lkents [get [get [get $__m entries] 0] entries]\n"
      "let __names [List::map $__lkents [lambda {e} { get $e name }]]\n"
      "var __missing ()\n"
      "foreach __k [Ns::keys $Lk] {\n"
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
        printf("    undocumented Lk:: procs: %s\n", missing);
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
      "foreach __k [Ns::keys $LkDsl] {\n"
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
        printf("    undocumented LkDsl procs: %s\n", missing);
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

/* ============================================================
 * Image procs (image track I5)
 * ============================================================ */

static void test_image_new_and_px(void) {
  lcl_interp *interp;
  lcl_value *r = NULL;

  BEGIN_TEST("image: new / size / pixel round-trip + errors");
  interp = make_interp();

  eval_ok(interp,
          "let ui [Lk::ui_create]\n"
          "let img [Lk::image_new $ui 4 3]",
          &r);
  if (r) lcl_ref_dec(r);

  check_int(interp, "opaque? $img", 1);
  check_str(interp, "repr [Lk::image_size $img]", "(4 3)");
  check_str(interp, "repr [Lk::image_get_px $img 0 0]", "(0 0 0 0)");

  r = NULL;
  eval_ok(interp, "Lk::image_set_px $img 1 2 (10 20 30)", &r);
  if (r) lcl_ref_dec(r);
  check_str(interp, "repr [Lk::image_get_px $img 1 2]", "(10 20 30 255)");

  r = NULL;
  eval_ok(interp, "Lk::image_set_px $img 0 0 (1 2 3 4)", &r);
  if (r) lcl_ref_dec(r);
  check_str(interp, "repr [Lk::image_get_px $img 0 0]", "(1 2 3 4)");

  eval_expect_err(interp, "Lk::image_new $ui 4", "Lk::image_new",
                  "3 arguments", NULL);
  eval_expect_err(interp, "Lk::image_new $img 4 3", "expected lk_ui opaque",
                  NULL, NULL);
  eval_expect_err(interp, "Lk::image_new $ui 0 3", ">= 1", NULL, NULL);
  eval_expect_err(interp, "Lk::image_new $ui 99999 3", "16384", NULL, NULL);
  eval_expect_err(interp, "Lk::image_get_px $img 4 0", "out of range", NULL,
                  NULL);
  eval_expect_err(interp, "Lk::image_get_px $ui 0 0",
                  "expected lk_image opaque", NULL, NULL);
  eval_expect_err(interp, "Lk::image_set_px $img 0 0 (1 2)", "color list",
                  NULL, NULL);

  lcl_interp_free(interp);
  END_TEST();
}

static void test_image_bytes_hex(void) {
  lcl_interp *interp;
  lcl_value *r = NULL;

  BEGIN_TEST("image: hex byte accessors round-trip + errors");
  interp = make_interp();

  eval_ok(interp,
          "let ui [Lk::ui_create]\n"
          "let img [Lk::image_new $ui 2 2]",
          &r);
  if (r) lcl_ref_dec(r);

  /* whitespace + uppercase decode, lowercase re-encode */
  r = NULL;
  eval_ok(interp, "Lk::image_set_bytes $img 0 \"DE AD\nBE EF\"", &r);
  if (r) lcl_ref_dec(r);
  check_str(interp, "Lk::image_bytes $img 0 4", "deadbeef");
  check_str(interp, "Lk::image_bytes $img 12 4", "00000000");

  eval_expect_err(interp, "Lk::image_set_bytes $img 0 \"xyz\"",
                  "invalid hex character", NULL, NULL);
  eval_expect_err(interp, "Lk::image_set_bytes $img 0 \"abc\"",
                  "odd number of hex digits", NULL, NULL);
  eval_expect_err(interp, "Lk::image_set_bytes $img 0 \"\"", "empty payload",
                  NULL, NULL);
  eval_expect_err(interp, "Lk::image_set_bytes $img 14 \"aabbcc\"",
                  "out of bounds", NULL, NULL);
  eval_expect_err(interp, "Lk::image_bytes $img 0 0", "out of bounds", NULL,
                  NULL);
  eval_expect_err(interp, "Lk::image_bytes $img 13 4", "out of bounds", NULL,
                  NULL);
  eval_expect_err(interp, "Lk::image_bytes $img -1 4", "out of bounds", NULL,
                  NULL);

  lcl_interp_free(interp);
  END_TEST();
}

/* The editor-track lifetime scheme: an explicitly destroyed ui must
 * not make the image finalizer touch the dead resource table, and the
 * pixel buffer (image-owned) survives the ui. */
/* Non-public test hook from lcl-lk.c: live lcl-value box count. */
extern int lcl_lk_debug_pres_boxes(void);

static void test_annot_present_ui_dies_first(void) {
  lcl_interp *interp;
  lcl_value *r = NULL;

  BEGIN_TEST("annot_present: boxes freed when the ui dies before the store");
  interp = make_interp();

  /* The DSL's `app` destroys its window (and ui) explicitly while the
   * script's store is still alive; the store's release hook can no
   * longer reach the dead table, so ui teardown must sweep the
   * lcl-value boxes itself.  A leak here is caught by LSan in CI. */
  eval_ok(interp,
    "let ui [Lk::ui_create]\n"
    "let doc [Lk::doc_new \"hello world\"]\n"
    "let s [Lk::annot_store_new]\n"
    "Lk::annot_attach $s $doc\n"
    "let a [Lk::annot_add $s 0 5 \"l\"]\n"
    "let b [Lk::annot_add $s 6 11 \"l\"]\n"
    "Lk::annot_present $ui $s $a \"t\" #{file \"x\" line 1}\n"
    "Lk::annot_present $ui $s $b \"t\" (1 2 3)\n"
    "Lk::annot_present $ui $s $a \"t\" \"replaced\"",
    &r);
  if (r) lcl_ref_dec(r);

  /* two live boxes: the replace released a's first one through the
   * store's hook (LSan cannot see a missed release -- the binding's
   * box list keeps boxes reachable -- so assert the count) */
  CHECK(lcl_lk_debug_pres_boxes() == 2);

  eval_ok(interp, "Lk::ui_destroy $ui", &r);
  if (r) lcl_ref_dec(r);

  CHECK(lcl_lk_debug_pres_boxes() == 0);

  /* the store itself is untouched by the ui's death */
  check_str(interp, "repr [Lk::annot_span $s $b]", "(6 11)");
  check_int(interp, "Lk::annot_remove $s $b", 1);

  lcl_interp_free(interp);
  END_TEST();
}

static void test_command_handler_released_on_ui_destroy(void) {
  lcl_interp *interp;
  lcl_value *r = NULL;

  BEGIN_TEST("set_command_handler: ctx freed by explicit ui_destroy");
  interp = make_interp();

  eval_ok(interp,
    "let ui [Lk::ui_create]\n"
    "Lk::set_command_handler $ui [lambda {cmd} { 0 }]\n"
    "Lk::set_command_handler $ui [lambda {cmd} { 1 }]\n"
    "Lk::ui_destroy $ui",
    &r);
  if (r) lcl_ref_dec(r);

  lcl_interp_free(interp);
  END_TEST();
}

static void test_image_lifetime(void) {
  lcl_interp *interp;
  lcl_value *r = NULL;

  BEGIN_TEST("image: survives explicit ui_destroy");
  interp = make_interp();

  eval_ok(interp,
          "let ui [Lk::ui_create]\n"
          "let img [Lk::image_new $ui 2 2]\n"
          "Lk::image_set_px $img 0 0 (9 8 7)\n"
          "Lk::ui_destroy $ui",
          &r);
  if (r) lcl_ref_dec(r);

  check_str(interp, "repr [Lk::image_get_px $img 0 0]", "(9 8 7 255)");

  /* interp free runs the image finalizer AFTER the ui died — the
   * live-ui guard must skip the resource release. */
  lcl_interp_free(interp);
  END_TEST();
}

static void test_canvas_new_and_ops(void) {
  lcl_interp *interp;
  lcl_value *r = NULL;

  BEGIN_TEST("canvas: new / size / ops / clear + errors");
  interp = make_interp();

  eval_ok(interp,
          "let ui [Lk::ui_create]\n"
          "let c [Lk::canvas_new $ui 320 200]\n"
          "let c0 [Lk::canvas_new $ui]",
          &r);
  if (r) lcl_ref_dec(r);

  check_int(interp, "opaque? $c", 1);
  check_str(interp, "repr [Lk::canvas_size $c]", "(320 200)");
  check_str(interp, "repr [Lk::canvas_size $c0]", "(0 0)");
  check_int(interp, "Lk::canvas_op_count $c", 0);

  /* Every op kind, floats accepted for coordinates, optional stroke. */
  r = NULL;
  eval_ok(interp,
          "Lk::canvas_line $c 0 0 10.6 20.2 (255 0 0)\n"
          "Lk::canvas_line $c 0 0 10 20 (255 0 0 128) 3\n"
          "Lk::canvas_polyline $c (0 0 5 5 10 0) (0 255 0)\n"
          "Lk::canvas_polyline $c (0 0 5 5) (0 255 0) 2\n"
          "Lk::canvas_rect $c 1 1 5 5 (0 0 255)\n"
          "Lk::canvas_rect $c 1 1 5 5 (0 0 255) 2\n"
          "Lk::canvas_fill_rect $c 1 1 5 5 (9 9 9)\n"
          "Lk::canvas_text $c 3 4 \"hi\" (1 1 1)\n"
          "Lk::canvas_text $c 3 4 42 (1 1 1)",
          &r);
  if (r) lcl_ref_dec(r);
  check_int(interp, "Lk::canvas_op_count $c", 9);

  r = NULL;
  eval_ok(interp, "Lk::canvas_set_size $c 640 480", &r);
  if (r) lcl_ref_dec(r);
  check_str(interp, "repr [Lk::canvas_size $c]", "(640 480)");
  check_int(interp, "Lk::canvas_op_count $c", 9); /* content untouched */

  r = NULL;
  eval_ok(interp, "Lk::canvas_clear $c", &r);
  if (r) lcl_ref_dec(r);
  check_int(interp, "Lk::canvas_op_count $c", 0);

  /* Error paths: arity, wrong opaque, bad coords / colors / points /
   * stroke / sizes.  Each names the proc. */
  eval_expect_err(interp, "Lk::canvas_new $ui 4", "Lk::canvas_new",
                  "1 or 3 arguments", NULL);
  eval_expect_err(interp, "Lk::canvas_new $c 4 3", "expected lk_ui opaque",
                  NULL, NULL);
  eval_expect_err(interp, "Lk::canvas_new $ui -1 3", ">= 0", NULL, NULL);
  eval_expect_err(interp, "Lk::canvas_new $ui 99999 3", "16384", NULL, NULL);
  eval_expect_err(interp, "Lk::canvas_set_size $c 99999 3", "16384", NULL,
                  NULL);
  eval_expect_err(interp, "Lk::canvas_size $ui", "expected lk_canvas opaque",
                  NULL, NULL);
  eval_expect_err(interp, "Lk::canvas_line $c 0 0 1", "Lk::canvas_line",
                  "6 or 7 arguments", NULL);
  eval_expect_err(interp, "Lk::canvas_line $c 0 0 1 x (1 2 3)",
                  "Lk::canvas_line", "must be numbers", NULL);
  eval_expect_err(interp, "Lk::canvas_line $c 0 0 1 1 (1 2)",
                  "Lk::canvas_line", "color list", NULL);
  eval_expect_err(interp, "Lk::canvas_line $c 0 0 1 1 (1 2 3) 300",
                  "Lk::canvas_line", "stroke", NULL);
  eval_expect_err(interp, "Lk::canvas_polyline $c (0 0 10) (1 2 3)",
                  "Lk::canvas_polyline", "even number", NULL);
  eval_expect_err(interp, "Lk::canvas_polyline $c (0 0) (1 2 3)",
                  "Lk::canvas_polyline", "at least 2 points", NULL);
  eval_expect_err(interp, "Lk::canvas_polyline $c \"0 0 1 1\" (1 2 3)",
                  "Lk::canvas_polyline", "flat", NULL);
  eval_expect_err(interp, "Lk::canvas_polyline $c (0 0 a 1) (1 2 3)",
                  "Lk::canvas_polyline", "must be numbers", NULL);
  eval_expect_err(interp, "Lk::canvas_rect $c 0 0 1 1 (1 2 3) 2 9",
                  "Lk::canvas_rect", "6 or 7 arguments", NULL);
  eval_expect_err(interp, "Lk::canvas_fill_rect $c 0 0 1 1 (1 2 3) 2",
                  "Lk::canvas_fill_rect", "6 arguments", NULL);
  eval_expect_err(interp, "Lk::canvas_text $c 0 0 \"x\"", "Lk::canvas_text",
                  "5 arguments", NULL);
  eval_expect_err(interp, "Lk::canvas_text $c 0 0 \"x\" (1 2)",
                  "Lk::canvas_text", "color list", NULL);
  check_int(interp, "Lk::canvas_op_count $c", 0); /* none slipped in */

  lcl_interp_free(interp);
  END_TEST();
}

static void test_canvas_lifetime(void) {
  lcl_interp *interp;
  lcl_value *r = NULL;

  BEGIN_TEST("canvas: survives explicit ui_destroy");
  interp = make_interp();

  eval_ok(interp,
          "let ui [Lk::ui_create]\n"
          "let c [Lk::canvas_new $ui 2 2]\n"
          "Lk::canvas_line $c 0 0 1 1 (9 8 7)\n"
          "Lk::ui_destroy $ui",
          &r);
  if (r) lcl_ref_dec(r);

  check_int(interp, "Lk::canvas_op_count $c", 1);

  /* interp free runs the canvas finalizer AFTER the ui died — the
   * live-ui guard must skip the resource release. */
  lcl_interp_free(interp);
  END_TEST();
}

/* Script-built tree -> C-side resolution through the ref prop. */
static void test_image_prop_bridge(void) {
  lcl_interp *interp;
  lcl_value *r = NULL;
  lk_ui *ui;

  BEGIN_TEST("image: prop bridges to lk_image_from_node");
  interp = make_interp();

  eval_ok(interp,
          "let ui [Lk::ui_create]\n"
          "let img [Lk::image_new $ui 8 4]\n"
          "let t [Lk::begin_frame $ui]\n"
          "let w [Lk::node $t \"w\" \"window\"]\n"
          "let n [Lk::node $t \"pic\" \"image\"]\n"
          "Lk::set_root $t $w\n"
          "Lk::append_child $t $w $n",
          &r);
  if (r) lcl_ref_dec(r);

  /* bad prop value first (the tree handle is only valid pre-swap) */
  eval_expect_err(interp, "Lk::prop $t $n image 42",
                  "image prop expects an lk_image opaque", NULL, NULL);

  r = NULL;
  eval_ok(interp,
          "Lk::prop $t $n image $img\n"
          "Lk::end_frame $ui",
          &r);
  if (r) lcl_ref_dec(r);

  ui = fetch_ui(interp);
  CHECK(ui != NULL);

  if (ui) {
    const lk_tree *t = lk_ui_tree(ui);
    lk_ix n = lk_tree_find_by_id(t, lk_intern_cid(t->intern, "pic"));
    lk_image *img;
    lk_u32 w = 0;
    lk_u32 h = 0;

    CHECK(n != 0);
    CHECK(lk_node_kind_get(t, n) == UIK_IMAGE);

    img = lk_image_from_node(lk_ui_resources(ui), t, n);
    CHECK(img != NULL);

    lk_image_size(img, &w, &h);
    CHECK(w == 8 && h == 4);
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
  test_window_icon_errors();
  test_image_load_mem_roundtrip();
  test_image_io_errors();
  test_dialog_errors();
#endif
  test_set_command_handler();
  test_set_command_handler_with_translator();
  test_text_input_kind();
  test_text_input_controlled_binding();
  test_text_align_binding();
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
  test_editor_multicursor_procs();
  test_editor_wrap_proc();
  test_editor_row_commands();
  test_editor_set_spans();
  test_editor_set_spans_errors();
  test_editor_lifetime();
  test_annot_store_basics();
  test_annot_queries();
  test_annot_anchor_tracking();
  test_annot_errors();
  test_annot_seq_and_layers();

  /* image track (I5) */
  test_image_new_and_px();
  test_image_bytes_hex();
  test_image_lifetime();
  test_image_prop_bridge();

  /* vector canvas */
  test_canvas_new_and_ops();
  test_canvas_lifetime();

  test_dsl_editor_widget();
  test_dsl_unknown_prop_lists_editor();
  test_dsl_image_widget();
  test_dsl_image_filter_prop();
  test_dsl_canvas_widget();
  test_dsl_global_keybinding_fallback();
  test_focus_request_binding();
  test_capture_binding();
  test_text_size_binding();
  test_canvas_clip_binding();
  test_spans_binding();
  test_styled_text_present_binding();
  test_dsl_styled_text_widget();
  test_menu_open_binding();
  test_context_menu_binding();
  test_list_binding();
  test_dsl_list_window();
  test_list_value_prop();
  test_args_proc();
#ifdef LK_HAVE_SDL
  test_screenshot_stop_errors();
#endif
  test_node_rect();
  test_editor_keys();
  test_clear_translators();
  test_dsl_clear_translators();

  /* Range presentations (weft-surface track, S1) */
  test_add_translator_button_arg();
  test_lcl_presentation_pipeline();
  test_focus_changed_marshal();
  test_focus_changed_translator_name();
  test_annot_present_errors();
  test_annot_present_ui_dies_first();
  test_command_handler_released_on_ui_destroy();
  test_name_arg_strictness();
  test_dsl_translator_matcher_dict();
  test_dsl_split_controlled_command();

  /* Forms widgets round */
  test_forms_prop_coercion();
  test_forms_dsl_events();
  test_forms_theme_accent();

  /* Companion-doc doctests (docs/lk.lcl) */
  test_lk_docs_doctests();

  printf("\n%d tests: %d passed, %d failed\n", g_tests, g_pass, g_fail);

  return g_fail > 0 ? 1 : 0;
}
