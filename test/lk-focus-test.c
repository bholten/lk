/*
 * lk-focus-test.c -- the footgun pass (docs/TODO.md, roadmap item 1):
 *
 *   1. Window-level fallback for ptype-less KEY translators: when no
 *      presentation on the focus path matches, they get one chance
 *      against the root.  Pointer events keep the presentation
 *      discipline; a presentation match still wins; a disabled root
 *      suppresses.
 *   2. lk_focus_request: deferred focus applied at end_frame once the
 *      node exists (focusable + enabled), superseded by an explicit
 *      focus_set, cancellable with 0.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <lk.h>

#include "lk-test-harness.h"

/* window > column > label "lbl" (+ optional presentation) > button
 * "btn" (focusable).  Returns nothing; the frame is committed. */
static void build_frame(lk_ui *ui, int present_lbl, int disable_root,
                        int with_btn) {
  lk_tree *t = lk_ui_begin_frame(ui);
  lk_ix w = lk_tree_add_node_c(t, "w", UIK_WINDOW);
  lk_ix col = lk_tree_add_node_c(t, "col", UIK_COLUMN);
  lk_ix lbl = lk_tree_add_node_c(t, "lbl", UIK_LABEL);

  lk_tree_set_root(t, w);
  lk_tree_append_child(t, w, col);
  lk_tree_append_child(t, col, lbl);

  if (disable_root) {
    lk_tree_add_prop(t, w, UIP_DISABLED, lk_v_bool(1));
  }

  if (present_lbl) {
    lk_tree_add_presentation_s(t, lbl, "doc", lk_v_i32(7));
  }

  if (with_btn) {
    lk_ix btn = lk_tree_add_node_c(t, "btn", UIK_BUTTON);

    lk_tree_append_child(t, col, btn);
    lk_tree_add_prop(t, btn, UIP_FOCUSABLE, lk_v_bool(1));
  }

  lk_ui_end_frame(ui);
}

static lk_ix find(lk_ui *ui, const char *id) {
  return lk_tree_find_by_id(lk_ui_tree(ui), lk_intern_cid(ui->intern, id));
}

static int route_key(lk_ui *ui, lk_ix target, lk_u16 key, lk_u8 mods) {
  lk_event ev;

  memset(&ev, 0, sizeof(ev));
  ev.type = LK_EVENT_KEY_DOWN;
  ev.target = target;
  ev.data.key.keycode = key;
  ev.mods = mods;
  lk_event_route(ui, &ev);

  return ev.handled;
}

static int route_click(lk_ui *ui, lk_ix target) {
  lk_event ev;

  memset(&ev, 0, sizeof(ev));
  ev.type = LK_EVENT_POINTER_DOWN;
  ev.target = target;
  ev.data.pointer.button = LK_POINTER_BUTTON_PRIMARY;
  lk_event_route(ui, &ev);

  return ev.handled;
}

static lk_u32 first_cmd_name(lk_ui *ui) {
  const lk_command_queue *q = lk_ui_commands(ui);

  return q->count ? q->cmds[0].name : 0;
}

/* ---- 1. keybinding fallback ---- */

static void test_key_fallback_fires_at_root(void) {
  lk_ui *ui = lk_ui_create(NULL);

  BEGIN_TEST("keys: ptype-less key translator fires with no presentation");

  lk_ui_add_translator_s(ui, LK_EVENT_KEY_DOWN, "", 0, (lk_u16)LKK_F2, 0, 0,
                         "NewGame");
  build_frame(ui, 0, 0, 0);

  /* Nothing on the path presents anything: target = a plain label. */
  CHECK_EQ(route_key(ui, find(ui, "lbl"), LKK_F2, 0), 1);
  CHECK_EQ(lk_ui_commands(ui)->count, 1);
  CHECK_EQ(first_cmd_name(ui), lk_intern_cid(ui->intern, "NewGame"));
  {
    const lk_command_queue *q = lk_ui_commands(ui);

    if (q->count) {
      /* Emitted at the root, no presentation args. */
      CHECK_EQ(q->cmds[0].source_node, lk_ui_tree(ui)->root);
      CHECK_EQ(q->cmds[0].source_ptype, 0);
      CHECK_EQ(q->cmds[0].arg_count, 0);
    }
  }

  /* Target = root itself (nothing focused): same. */
  lk_ui_clear_commands(ui);
  CHECK_EQ(route_key(ui, lk_ui_tree(ui)->root, LKK_F2, 0), 1);
  CHECK_EQ(lk_ui_commands(ui)->count, 1);

  /* Wrong key / wrong mods: nothing (exact-chord discipline). */
  lk_ui_clear_commands(ui);
  CHECK_EQ(route_key(ui, find(ui, "lbl"), LKK_F3, 0), 0);
  CHECK_EQ(route_key(ui, find(ui, "lbl"), LKK_F2, LK_MOD_CTRL), 0);
  CHECK_EQ(lk_ui_commands(ui)->count, 0);

  lk_ui_destroy(ui);
  END_TEST();
}

static void test_key_fallback_discipline(void) {
  lk_ui *ui = lk_ui_create(NULL);

  BEGIN_TEST("keys: fallback is key-only, ptype-less only, root-enabled");

  /* A ptype-less POINTER translator must NOT fall back to the root. */
  lk_ui_add_translator_s(ui, LK_EVENT_POINTER_DOWN, "", 0, 0, 0, 0,
                         "Click");
  /* A ptype-scoped key translator must NOT fire without its ptype. */
  lk_ui_add_translator_s(ui, LK_EVENT_KEY_DOWN, "doc", 0, (lk_u16)LKK_S,
                         LK_MOD_CTRL, 0, "Save");
  build_frame(ui, 0, 0, 0);

  CHECK_EQ(route_click(ui, find(ui, "lbl")), 0);
  CHECK_EQ(route_key(ui, find(ui, "lbl"), LKK_S, LK_MOD_CTRL), 0);
  CHECK_EQ(lk_ui_commands(ui)->count, 0);

  /* With the presentation on the path the scoped one fires as before. */
  build_frame(ui, 1, 0, 0);
  CHECK_EQ(route_key(ui, find(ui, "lbl"), LKK_S, LK_MOD_CTRL), 1);
  CHECK_EQ(first_cmd_name(ui), lk_intern_cid(ui->intern, "Save"));

  /* Disabled root: even the ptype-less key fallback is suppressed. */
  lk_ui_clear_commands(ui);
  lk_ui_add_translator_s(ui, LK_EVENT_KEY_DOWN, "", 0, (lk_u16)LKK_F2, 0, 0,
                         "NewGame");
  build_frame(ui, 0, 1, 0);
  CHECK_EQ(route_key(ui, find(ui, "lbl"), LKK_F2, 0), 0);
  CHECK_EQ(lk_ui_commands(ui)->count, 0);

  lk_ui_destroy(ui);
  END_TEST();
}

static void test_key_presentation_beats_fallback(void) {
  lk_ui *ui = lk_ui_create(NULL);

  BEGIN_TEST("keys: a presentation match wins over the root fallback");

  /* Both a scoped and a global binding for the same chord.  With the
   * presentation on the path the scoped one is found during the
   * ancestor walk; the fallback never runs (one command, not two). */
  lk_ui_add_translator_s(ui, LK_EVENT_KEY_DOWN, "", 0, (lk_u16)LKK_S,
                         LK_MOD_CTRL, 0, "SaveAll");
  lk_ui_add_translator_s(ui, LK_EVENT_KEY_DOWN, "doc", 0, (lk_u16)LKK_S,
                         LK_MOD_CTRL, 0, "SaveDoc");
  build_frame(ui, 1, 0, 0);

  CHECK_EQ(route_key(ui, find(ui, "lbl"), LKK_S, LK_MOD_CTRL), 1);
  CHECK_EQ(lk_ui_commands(ui)->count, 1);
  /* The ancestor walk considers translators in order at the presenting
   * node: the ptype-less one is registered first and matches ptype
   * "doc" too (0 = any), carrying the presentation's args. */
  CHECK_EQ(first_cmd_name(ui), lk_intern_cid(ui->intern, "SaveAll"));
  if (lk_ui_commands(ui)->count) {
    CHECK_EQ(lk_ui_commands(ui)->cmds[0].arg_count, 1);
  }

  /* Without the presentation only the global one can fire -- via the
   * fallback, with no args. */
  lk_ui_clear_commands(ui);
  build_frame(ui, 0, 0, 0);
  CHECK_EQ(route_key(ui, find(ui, "lbl"), LKK_S, LK_MOD_CTRL), 1);
  CHECK_EQ(lk_ui_commands(ui)->count, 1);
  CHECK_EQ(first_cmd_name(ui), lk_intern_cid(ui->intern, "SaveAll"));
  if (lk_ui_commands(ui)->count) {
    CHECK_EQ(lk_ui_commands(ui)->cmds[0].arg_count, 0);
  }

  lk_ui_destroy(ui);
  END_TEST();
}

/* ---- 2. lk_focus_request ---- */

static void test_focus_request_deferred(void) {
  lk_ui *ui = lk_ui_create(NULL);
  lk_node_id btn;

  BEGIN_TEST("focus_request: applied at the end_frame that commits the node");

  btn = lk_intern_cid(ui->intern, "btn");

  CHECK_EQ(lk_focus_request(ui, btn), 0); /* no previous request */
  CHECK_EQ(ui->focus_request_id, btn);
  CHECK_EQ(ui->focused_id, 0);

  /* A frame WITHOUT the node: still pending, nothing focused. */
  build_frame(ui, 0, 0, 0);
  CHECK_EQ(ui->focused_id, 0);
  CHECK_EQ(ui->focus_request_id, btn);

  /* A frame WITH it: focused, request cleared, FOCUS_CHANGED queued
   * for the host's flush. */
  build_frame(ui, 0, 0, 1);
  CHECK_EQ(ui->focused_id, btn);
  CHECK_EQ(ui->focus_request_id, 0);
  {
    /* The synthetic focus event is delivered by the flush -- observe
     * it through the user event handler tier's absence: just check
     * the flush runs cleanly and focus stays. */
    lk_ui_flush_events(ui, lk_ui_tree(ui));
    CHECK_EQ(ui->focused_id, btn);
  }

  /* NULL-safe, cancel with 0 returns the previous request. */
  CHECK_EQ(lk_focus_request(NULL, btn), 0);
  lk_focus_request(ui, btn);
  CHECK_EQ(lk_focus_request(ui, 0), btn);
  CHECK_EQ(ui->focus_request_id, 0);

  lk_ui_destroy(ui);
  END_TEST();
}

static void test_focus_request_rules(void) {
  lk_ui *ui = lk_ui_create(NULL);
  lk_node_id lbl;
  lk_node_id btn;

  BEGIN_TEST("focus_request: focusable rule, explicit set supersedes");

  lbl = lk_intern_cid(ui->intern, "lbl");
  btn = lk_intern_cid(ui->intern, "btn");

  /* A request for a non-focusable node stays pending (lk_focus_set's
   * rules apply), and is replaced by a later request. */
  lk_focus_request(ui, lbl);
  build_frame(ui, 0, 0, 1);
  CHECK_EQ(ui->focused_id, 0);
  CHECK_EQ(ui->focus_request_id, lbl);

  CHECK_EQ(lk_focus_request(ui, btn), lbl);
  build_frame(ui, 0, 0, 1);
  CHECK_EQ(ui->focused_id, btn);
  CHECK_EQ(ui->focus_request_id, 0);

  /* A successful explicit focus_set cancels a pending request; a
   * failed one (unknown id) leaves it. */
  lk_focus_clear(ui);
  lk_focus_request(ui, btn);
  CHECK_EQ(lk_focus_set(ui, lk_ui_tree(ui), lk_intern_cid(ui->intern, "nope")),
           0);
  CHECK_EQ(ui->focus_request_id, btn);
  CHECK_EQ(lk_focus_set(ui, lk_ui_tree(ui), btn), 1);
  CHECK_EQ(ui->focus_request_id, 0);

  /* Focus GC still runs first: a request for a node that vanishes in
   * the same frame stays pending, focus cleared. */
  lk_focus_request(ui, btn);
  build_frame(ui, 0, 0, 0);
  CHECK_EQ(ui->focused_id, 0);
  CHECK_EQ(ui->focus_request_id, btn);

  lk_ui_destroy(ui);
  END_TEST();
}

void lk_focus_run_tests(void) {
  printf("\nlk focus / keybinding tests:\n");
  test_key_fallback_fires_at_root();
  test_key_fallback_discipline();
  test_key_presentation_beats_fallback();
  test_focus_request_deferred();
  test_focus_request_rules();
}
