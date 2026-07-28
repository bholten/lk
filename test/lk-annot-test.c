/*
 * lk-annot-test.c -- editor track stage C: the annotation store
 * (weft's test_annot_store.c ported; the edit hooks are gone, so
 * every transform scenario drives a real attached lk_document --
 * the subscription path IS the product now), subscription-specific
 * tests (multi-op transactions, undo round trips, destroy order,
 * revision tracking), and styled-span render tests against the
 * deterministic stub backend (8 px per codepoint, line height 16,
 * baseline 12).
 */

#include <stdio.h>
#include <string.h>

#include <lk-annot-store.h>
#include <lk-editor.h>
#include <lk.h>

#include "lk-test-harness.h"

/* ================================================================
 * store fixture: a document of 'x' bytes with an attached store
 * ================================================================ */

typedef struct as_fix {
  lk_document *doc;
  lk_annot_store *st;
} as_fix;

static void as_init(as_fix *f, lk_u32 doc_len) {
  char buf[128];
  lk_u32 i;

  if (doc_len > sizeof(buf)) {
    doc_len = (lk_u32)sizeof(buf);
  }

  for (i = 0; i < doc_len; i++) {
    buf[i] = 'x';
  }

  f->doc = lk_doc_from_str(NULL, NULL, NULL, buf, doc_len);
  f->st = lk_annot_store_new(NULL, NULL, NULL);
  lk_annot_store_attach(f->st, f->doc);
}

/* Safe order (contract): store before document. */
static void as_destroy(as_fix *f) {
  lk_annot_store_destroy(f->st);
  lk_doc_destroy(f->doc);
}

static int as_insert(as_fix *f, lk_u32 pos, lk_u32 len) {
  char buf[64];
  lk_u32 i;

  for (i = 0; i < len && i < sizeof(buf); i++) {
    buf[i] = 'y';
  }

  return lk_doc_insert(f->doc, pos, buf, len);
}

static int as_delete(as_fix *f, lk_u32 pos, lk_u32 len) {
  return lk_doc_delete(f->doc, pos, len);
}

/* ================================================================
 * (a) weft test_annot_store.c, ported
 * ================================================================ */

/* === Basic Lifecycle === */

static void test_annot_store_new_empty(void) {
  as_fix f;

  BEGIN_TEST("annot: new creates empty store");

  as_init(&f, 64);

  CHECK(f.st != NULL);
  CHECK_EQ(f.st->record_count, 0);
  CHECK_EQ(f.st->anchor_count, 0);

  as_destroy(&f);
  END_TEST();
}

/* === Add and Get === */

static void test_add_annotation(void) {
  as_fix f;
  const char *keys[1];
  const char *vals[1];
  lk_u32 id;
  const lk_annot_record *rec;
  lk_u32 start;
  lk_u32 end;
  const char *fg;

  BEGIN_TEST("annot: add annotation and retrieve");

  as_init(&f, 64);
  keys[0] = "style:fg";
  vals[0] = "red";
  id = lk_annot_add(f.st, 10, 20, "style", keys, vals, 1);

  CHECK(id != 0);
  CHECK_EQ(f.st->record_count, 1);

  rec = lk_annot_get(f.st, id);
  CHECK(rec != NULL);
  CHECK(rec && strcmp(rec->layer, "style") == 0);

  CHECK(lk_annot_get_span(f.st, id, &start, &end));
  CHECK_EQ(start, 10);
  CHECK_EQ(end, 20);

  fg = lk_annot_get_meta(f.st, id, "style:fg");
  CHECK(fg != NULL);
  CHECK(fg && strcmp(fg, "red") == 0);

  as_destroy(&f);
  END_TEST();
}

static void test_add_multiple_annotations(void) {
  as_fix f;
  lk_u32 id1;
  lk_u32 id2;
  lk_u32 id3;

  BEGIN_TEST("annot: add multiple annotations");

  as_init(&f, 64);
  id1 = lk_annot_add(f.st, 0, 10, "style", NULL, NULL, 0);
  id2 = lk_annot_add(f.st, 5, 15, "lint", NULL, NULL, 0);
  id3 = lk_annot_add(f.st, 20, 30, "style", NULL, NULL, 0);

  CHECK(id1 != 0);
  CHECK(id2 != 0);
  CHECK(id3 != 0);
  CHECK(id1 != id2);
  CHECK(id2 != id3);
  CHECK_EQ(f.st->record_count, 3);

  as_destroy(&f);
  END_TEST();
}

static void test_remove_annotation(void) {
  as_fix f;
  lk_u32 id;

  BEGIN_TEST("annot: remove annotation");

  as_init(&f, 64);
  id = lk_annot_add(f.st, 10, 20, "style", NULL, NULL, 0);

  CHECK(id != 0);
  CHECK(lk_annot_remove(f.st, id) == 1);
  CHECK(lk_annot_get(f.st, id) == NULL);

  as_destroy(&f);
  END_TEST();
}

/* === Queries === */

static void test_query_at_position(void) {
  as_fix f;
  lk_annot_query q;

  BEGIN_TEST("annot: query annotations at position");

  as_init(&f, 64);
  lk_annot_add(f.st, 0, 10, "style", NULL, NULL, 0);
  lk_annot_add(f.st, 5, 15, "style", NULL, NULL, 0);
  lk_annot_add(f.st, 20, 30, "style", NULL, NULL, 0);

  lk_annot_query_init(&q);

  /* position 7 matches the first two */
  lk_annot_at(f.st, 7, NULL, &q);
  CHECK_EQ(q.count, 2);

  lk_annot_query_clear(&q);

  /* position 25 matches only the third */
  lk_annot_at(f.st, 25, NULL, &q);
  CHECK_EQ(q.count, 1);

  lk_annot_query_clear(&q);

  /* position 18 matches nothing */
  lk_annot_at(f.st, 18, NULL, &q);
  CHECK_EQ(q.count, 0);

  lk_annot_query_free(&q);
  as_destroy(&f);
  END_TEST();
}

static void test_query_in_range(void) {
  as_fix f;
  lk_annot_query q;

  BEGIN_TEST("annot: query annotations in range");

  as_init(&f, 64);
  lk_annot_add(f.st, 0, 10, "style", NULL, NULL, 0);
  lk_annot_add(f.st, 15, 25, "style", NULL, NULL, 0);
  lk_annot_add(f.st, 30, 40, "style", NULL, NULL, 0);

  lk_annot_query_init(&q);

  /* range 5-20 overlaps the first two */
  lk_annot_in_range(f.st, 5, 20, NULL, &q);
  CHECK_EQ(q.count, 2);

  lk_annot_query_clear(&q);

  /* range 50-60 matches nothing */
  lk_annot_in_range(f.st, 50, 60, NULL, &q);
  CHECK_EQ(q.count, 0);

  lk_annot_query_free(&q);
  as_destroy(&f);
  END_TEST();
}

static void test_query_by_layer(void) {
  as_fix f;
  lk_annot_query q;

  BEGIN_TEST("annot: query annotations by layer");

  as_init(&f, 64);
  lk_annot_add(f.st, 0, 10, "style", NULL, NULL, 0);
  lk_annot_add(f.st, 5, 15, "lint", NULL, NULL, 0);
  lk_annot_add(f.st, 20, 30, "style", NULL, NULL, 0);

  lk_annot_query_init(&q);

  /* "style" layer only */
  lk_annot_at(f.st, 7, "style", &q);
  CHECK_EQ(q.count, 1);

  lk_annot_query_clear(&q);

  /* "lint" layer only */
  lk_annot_at(f.st, 7, "lint", &q);
  CHECK_EQ(q.count, 1);

  /* by_layer sees every "style" record */
  lk_annot_query_clear(&q);
  lk_annot_by_layer(f.st, "style", &q);
  CHECK_EQ(q.count, 2);

  lk_annot_query_free(&q);
  as_destroy(&f);
  END_TEST();
}

/* === Edit transformations -- insert (via the document) === */

static void test_on_insert_after(void) {
  as_fix f;
  lk_u32 id;
  lk_u32 start;
  lk_u32 end;

  BEGIN_TEST("annot: insert after annotation - no change");

  as_init(&f, 40);
  id = lk_annot_add(f.st, 10, 20, "style", NULL, NULL, 0);

  /* insert at position 25 (after the annotation) */
  CHECK(as_insert(&f, 25, 5));

  lk_annot_get_span(f.st, id, &start, &end);
  CHECK_EQ(start, 10);
  CHECK_EQ(end, 20);

  as_destroy(&f);
  END_TEST();
}

static void test_on_insert_before(void) {
  as_fix f;
  lk_u32 id;
  lk_u32 start;
  lk_u32 end;

  BEGIN_TEST("annot: insert before annotation - shifts fwd");

  as_init(&f, 40);
  id = lk_annot_add(f.st, 10, 20, "style", NULL, NULL, 0);

  /* insert 5 bytes at position 5 (before the annotation) */
  CHECK(as_insert(&f, 5, 5));

  lk_annot_get_span(f.st, id, &start, &end);
  CHECK_EQ(start, 15);
  CHECK_EQ(end, 25);

  as_destroy(&f);
  END_TEST();
}

static void test_on_insert_at_start_left_bias(void) {
  as_fix f;
  lk_u32 id;
  lk_u32 start;
  lk_u32 end;

  BEGIN_TEST("annot: insert at start (left bias) stays");

  as_init(&f, 40);
  id = lk_annot_add(f.st, 10, 20, "style", NULL, NULL, 0);

  /* insert at position 10 (at the annotation start) */
  CHECK(as_insert(&f, 10, 5));

  lk_annot_get_span(f.st, id, &start, &end);
  CHECK_EQ(start, 10); /* LEFT bias: stays at 10 */
  CHECK_EQ(end, 25);   /* end shifts */

  as_destroy(&f);
  END_TEST();
}

static void test_on_insert_at_end_right_bias(void) {
  as_fix f;
  lk_u32 id;
  lk_u32 start;
  lk_u32 end;

  BEGIN_TEST("annot: insert at end (right bias) moves");

  as_init(&f, 40);
  id = lk_annot_add(f.st, 10, 20, "style", NULL, NULL, 0);

  /* insert at position 20 (at the annotation end) */
  CHECK(as_insert(&f, 20, 5));

  lk_annot_get_span(f.st, id, &start, &end);
  CHECK_EQ(start, 10);
  CHECK_EQ(end, 25); /* RIGHT bias: moves to 25 */

  as_destroy(&f);
  END_TEST();
}

static void test_on_insert_in_middle(void) {
  as_fix f;
  lk_u32 id;
  lk_u32 start;
  lk_u32 end;

  BEGIN_TEST("annot: insert in middle - end shifts");

  as_init(&f, 40);
  id = lk_annot_add(f.st, 10, 20, "style", NULL, NULL, 0);

  /* insert 5 bytes at position 15 (middle of the annotation) */
  CHECK(as_insert(&f, 15, 5));

  lk_annot_get_span(f.st, id, &start, &end);
  CHECK_EQ(start, 10);
  CHECK_EQ(end, 25);

  as_destroy(&f);
  END_TEST();
}

/* === Edit transformations -- delete (via the document) === */

static void test_on_delete_after(void) {
  as_fix f;
  lk_u32 id;
  lk_u32 start;
  lk_u32 end;

  BEGIN_TEST("annot: delete after annotation - no change");

  as_init(&f, 40);
  id = lk_annot_add(f.st, 10, 20, "style", NULL, NULL, 0);

  /* delete 5 bytes starting at position 25 */
  CHECK(as_delete(&f, 25, 5));

  lk_annot_get_span(f.st, id, &start, &end);
  CHECK_EQ(start, 10);
  CHECK_EQ(end, 20);

  as_destroy(&f);
  END_TEST();
}

static void test_on_delete_before(void) {
  as_fix f;
  lk_u32 id;
  lk_u32 start;
  lk_u32 end;

  BEGIN_TEST("annot: delete before annotation - shifts back");

  as_init(&f, 40);
  id = lk_annot_add(f.st, 10, 20, "style", NULL, NULL, 0);

  /* delete 5 bytes starting at position 0 */
  CHECK(as_delete(&f, 0, 5));

  lk_annot_get_span(f.st, id, &start, &end);
  CHECK_EQ(start, 5);
  CHECK_EQ(end, 15);

  as_destroy(&f);
  END_TEST();
}

static void test_on_delete_overlapping_start(void) {
  as_fix f;
  lk_u32 id;
  lk_u32 start;
  lk_u32 end;

  BEGIN_TEST("annot: delete overlapping annotation start");

  as_init(&f, 40);
  id = lk_annot_add(f.st, 10, 20, "style", NULL, NULL, 0);

  /* delete from 5 to 15 (overlaps the start) */
  CHECK(as_delete(&f, 5, 10));

  lk_annot_get_span(f.st, id, &start, &end);
  CHECK_EQ(start, 5); /* collapsed to the deletion point */
  CHECK_EQ(end, 10);  /* shifted back */

  as_destroy(&f);
  END_TEST();
}

static void test_on_delete_overlapping_end(void) {
  as_fix f;
  lk_u32 id;
  lk_u32 start;
  lk_u32 end;

  BEGIN_TEST("annot: delete overlapping annotation end");

  as_init(&f, 40);
  id = lk_annot_add(f.st, 10, 20, "style", NULL, NULL, 0);

  /* delete from 15 to 25 (overlaps the end) */
  CHECK(as_delete(&f, 15, 10));

  lk_annot_get_span(f.st, id, &start, &end);
  CHECK_EQ(start, 10);
  CHECK_EQ(end, 15); /* collapsed to the deletion point */

  as_destroy(&f);
  END_TEST();
}

static void test_on_delete_entire_annotation(void) {
  as_fix f;
  lk_u32 id;

  BEGIN_TEST("annot: delete entire annotation removes it");

  as_init(&f, 40);
  id = lk_annot_add(f.st, 10, 20, "style", NULL, NULL, 0);

  /* delete from 5 to 25 (the entire annotation): both anchors
   * collapse to 5, the zero-length record is removed */
  CHECK(as_delete(&f, 5, 20));
  CHECK(lk_annot_get(f.st, id) == NULL);

  as_destroy(&f);
  END_TEST();
}

static void test_on_delete_in_middle(void) {
  as_fix f;
  lk_u32 id;
  lk_u32 start;
  lk_u32 end;

  BEGIN_TEST("annot: delete in middle of annotation");

  as_init(&f, 40);
  id = lk_annot_add(f.st, 10, 20, "style", NULL, NULL, 0);

  /* delete 2 bytes from the middle (position 14-16) */
  CHECK(as_delete(&f, 14, 2));

  lk_annot_get_span(f.st, id, &start, &end);
  CHECK_EQ(start, 10);
  CHECK_EQ(end, 18); /* shifted back by 2 */

  as_destroy(&f);
  END_TEST();
}

/* === Layer Operations === */

static void test_clear_layer(void) {
  as_fix f;
  lk_u32 id1;
  lk_u32 id2;
  lk_u32 id3;

  BEGIN_TEST("annot: clear_layer removes only that layer");

  as_init(&f, 64);
  id1 = lk_annot_add(f.st, 0, 10, "style", NULL, NULL, 0);
  id2 = lk_annot_add(f.st, 5, 15, "lint", NULL, NULL, 0);
  id3 = lk_annot_add(f.st, 10, 20, "style", NULL, NULL, 0);

  lk_annot_store_clear_layer(f.st, "style");

  CHECK(lk_annot_get(f.st, id1) == NULL);
  CHECK(lk_annot_get(f.st, id2) != NULL);
  CHECK(lk_annot_get(f.st, id3) == NULL);

  as_destroy(&f);
  END_TEST();
}

static void test_layer_state_version(void) {
  as_fix f;
  lk_u32 v;

  BEGIN_TEST("annot: layer state + version counter");

  as_init(&f, 64);

  /* unknown layers read DIRTY / version 0 */
  CHECK(lk_annot_layer_state(f.st, "nope") == LK_LAYER_DIRTY);
  CHECK_EQ(lk_annot_layer_version(f.st, "nope"), 0);

  lk_annot_register_layer(f.st, "style");
  CHECK(lk_annot_layer_state(f.st, "style") == LK_LAYER_VALID);
  CHECK_EQ(lk_annot_layer_version(f.st, "style"), 1);

  /* an edit marks layers dirty WITHOUT bumping the version (weft) */
  CHECK(as_insert(&f, 0, 3));
  CHECK(lk_annot_layer_state(f.st, "style") == LK_LAYER_DIRTY);
  CHECK_EQ(lk_annot_layer_version(f.st, "style"), 1);

  lk_annot_set_layer_valid(f.st, "style");
  CHECK(lk_annot_layer_state(f.st, "style") == LK_LAYER_VALID);

  /* set_dirty bumps the version */
  v = lk_annot_layer_version(f.st, "style");
  lk_annot_set_layer_dirty(f.st, "style");
  CHECK(lk_annot_layer_state(f.st, "style") == LK_LAYER_DIRTY);
  CHECK_EQ(lk_annot_layer_version(f.st, "style"), v + 1);

  /* clear bumps every layer's version */
  v = lk_annot_layer_version(f.st, "style");
  lk_annot_store_clear(f.st);
  CHECK_EQ(f.st->record_count, 0);
  CHECK_EQ(lk_annot_layer_version(f.st, "style"), v + 1);

  as_destroy(&f);
  END_TEST();
}

/* === Edge Cases === */

static void test_zero_length_annotation_rejected(void) {
  as_fix f;
  lk_u32 id;

  BEGIN_TEST("annot: zero-length annotation is rejected");

  as_init(&f, 64);
  id = lk_annot_add(f.st, 10, 10, "cursor", NULL, NULL, 0);
  CHECK_EQ(id, 0);

  as_destroy(&f);
  END_TEST();
}

static void test_annotation_becomes_zero_length(void) {
  as_fix f;
  lk_u32 id;

  BEGIN_TEST("annot: becomes zero-length -> removed");

  as_init(&f, 40);
  id = lk_annot_add(f.st, 10, 12, "style", NULL, NULL, 0);

  CHECK(id != 0);

  /* delete the entire span - the annotation is removed */
  CHECK(as_delete(&f, 10, 2));
  CHECK(lk_annot_get(f.st, id) == NULL);

  as_destroy(&f);
  END_TEST();
}

static void test_adjacent_annotations(void) {
  as_fix f;
  lk_u32 id1;
  lk_u32 id2;
  lk_u32 start1;
  lk_u32 end1;
  lk_u32 start2;
  lk_u32 end2;

  BEGIN_TEST("annot: adjacent annotations don't interfere");

  as_init(&f, 64);
  id1 = lk_annot_add(f.st, 0, 10, "style", NULL, NULL, 0);
  id2 = lk_annot_add(f.st, 10, 20, "style", NULL, NULL, 0);

  /* insert at the shared boundary (position 10) */
  CHECK(as_insert(&f, 10, 5));

  lk_annot_get_span(f.st, id1, &start1, &end1);
  lk_annot_get_span(f.st, id2, &start2, &end2);

  /* first annotation's end has RIGHT bias: moves */
  CHECK_EQ(end1, 15);

  /* second annotation's start has LEFT bias: stays (weft) */
  CHECK_EQ(start2, 10);
  CHECK_EQ(end2, 25);

  as_destroy(&f);
  END_TEST();
}

/* ================================================================
 * (b) subscription-specific
 * ================================================================ */

/* counting listener to prove once-per-commit */
static lk_u32 g_notify_count;
static lk_u32 g_notify_deltas;

static void counting_listener(void *ud, const lk_document *d,
                              const lk_doc_delta *deltas, lk_u32 n) {
  (void)ud;
  (void)d;
  (void)deltas;
  g_notify_count++;
  g_notify_deltas += n;
}

static void test_sub_multi_op_transaction(void) {
  as_fix f;
  lk_u32 id;
  lk_u32 start;
  lk_u32 end;

  BEGIN_TEST("annot sub: multi-op transaction, in order");

  as_init(&f, 40);
  id = lk_annot_add(f.st, 10, 20, "style", NULL, NULL, 0);

  g_notify_count = 0;
  g_notify_deltas = 0;
  lk_doc_subscribe(f.doc, counting_listener, NULL);

  /* one bracket, three edits -> ONE notification, deltas applied in
   * order:
   *   insert 5 @ 0    -> [15, 25)
   *   delete 2 @ 2    -> [13, 23)
   *   insert 3 @ 30   -> unchanged (both anchors < 30) */
  lk_doc_begin(f.doc, 100);
  CHECK(as_insert(&f, 0, 5));
  CHECK(as_delete(&f, 2, 2));
  CHECK(as_insert(&f, 30, 3));
  lk_doc_commit(f.doc);

  CHECK_EQ(g_notify_count, 1);
  CHECK_EQ(g_notify_deltas, 3);

  lk_annot_get_span(f.st, id, &start, &end);
  CHECK_EQ(start, 13);
  CHECK_EQ(end, 23);

  as_destroy(&f);
  END_TEST();
}

static void test_sub_boundary_exact_deletes(void) {
  as_fix f;
  lk_u32 id;
  lk_u32 start;
  lk_u32 end;

  BEGIN_TEST("annot sub: delete ending exactly at anchor");

  as_init(&f, 40);
  id = lk_annot_add(f.st, 10, 20, "style", NULL, NULL, 0);

  /* delete [5, 10): range end == start anchor position, so the
   * anchor is "at or past the end" and shifts back (never
   * collapses) -- weft semantics */
  CHECK(as_delete(&f, 5, 5));

  lk_annot_get_span(f.st, id, &start, &end);
  CHECK_EQ(start, 5);
  CHECK_EQ(end, 15);

  /* delete [15, 20): ends exactly at the (already shifted) end
   * anchor 15... start of range == span end?  No: span is now
   * [5, 15), so delete [10, 15) ends exactly at the end anchor:
   * it shifts back to 10, collapsing the tail */
  CHECK(as_delete(&f, 10, 5));

  lk_annot_get_span(f.st, id, &start, &end);
  CHECK_EQ(start, 5);
  CHECK_EQ(end, 10);

  as_destroy(&f);
  END_TEST();
}

static void test_sub_undo_round_trip(void) {
  as_fix f;
  lk_edit_history *hist;
  lk_u32 id;
  lk_u32 start;
  lk_u32 end;

  BEGIN_TEST("annot sub: span survives insert-then-undo");

  as_init(&f, 40);
  hist = lk_history_new(NULL, NULL, NULL);
  lk_history_attach(hist, f.doc);

  id = lk_annot_add(f.st, 10, 20, "style", NULL, NULL, 0);

  CHECK(as_insert(&f, 5, 5));
  lk_annot_get_span(f.st, id, &start, &end);
  CHECK_EQ(start, 15);
  CHECK_EQ(end, 25);

  /* undo arrives as an ordinary LK_ORIGIN_UNDO transaction through
   * the same subscription: the span returns to its original range */
  CHECK(lk_history_undo(hist, f.doc) == 1);
  lk_annot_get_span(f.st, id, &start, &end);
  CHECK_EQ(start, 10);
  CHECK_EQ(end, 20);

  /* and redo shifts it again */
  CHECK(lk_history_redo(hist, f.doc) == 1);
  lk_annot_get_span(f.st, id, &start, &end);
  CHECK_EQ(start, 15);
  CHECK_EQ(end, 25);

  lk_history_destroy(hist);
  as_destroy(&f);
  END_TEST();
}

static void test_sub_destroy_order(void) {
  lk_document *doc;
  lk_annot_store *st;

  BEGIN_TEST("annot sub: store destroyed before document");

  doc = lk_doc_from_str(NULL, NULL, NULL, "hello world", 11);
  st = lk_annot_store_new(NULL, NULL, NULL);
  lk_annot_store_attach(st, doc);
  lk_annot_add(st, 0, 5, "style", NULL, NULL, 0);

  /* the contract order: store first (it unsubscribes), document
   * after; edits after the store is gone must not touch freed
   * listener state */
  lk_annot_store_destroy(st);

  CHECK(lk_doc_insert(doc, 0, "x", 1) == 1);
  CHECK_EQ(lk_doc_len(doc), 12);

  lk_doc_destroy(doc);
  END_TEST();
}

static void test_sub_revision_tracking(void) {
  as_fix f;

  BEGIN_TEST("annot sub: revision follows delta->after");

  as_init(&f, 40);

  /* attach point: store rev == document rev */
  CHECK(lk_revision_equal(lk_annot_store_rev(f.st),
                          lk_doc_revision(f.doc)));

  CHECK(as_insert(&f, 0, 3));
  CHECK(lk_revision_equal(lk_annot_store_rev(f.st),
                          lk_doc_revision(f.doc)));

  /* multi-op transaction: still ends at the document's revision */
  lk_doc_begin(f.doc, 100);
  as_insert(&f, 0, 1);
  as_delete(&f, 5, 2);
  lk_doc_commit(f.doc);
  CHECK(lk_revision_equal(lk_annot_store_rev(f.st),
                          lk_doc_revision(f.doc)));

  as_destroy(&f);
  END_TEST();
}

/* ================================================================
 * (c) styled-span render tests (stub backend, exact geometry)
 * ================================================================ */

typedef struct sp_fix {
  lk_ui *ui;
  lk_document *doc;
  lk_editor *ed;
  lk_resource_ref ref;
  lk_ix node;
  lk_node_id nid;
  lk_rect rects[8];
  lk_layout_cfg cfg;
} sp_fix;

static void sp_frame(sp_fix *f) {
  lk_tree *t = lk_ui_begin_frame(f->ui);
  lk_ix w = lk_tree_add_node_c(t, "w", UIK_WINDOW);
  lk_ix ed = lk_tree_add_node_c(t, "ed", UIK_EDITOR);

  lk_tree_set_root(t, w);
  lk_tree_append_child(t, w, ed);
  lk_tree_add_prop(t, ed, UIP_FOCUSABLE, lk_v_bool(1));
  lk_tree_add_prop(t, ed, UIP_EDITOR, lk_v_resource(f->ref));
  lk_ui_end_frame(f->ui);
  f->node = lk_tree_find_by_id(lk_ui_tree(f->ui),
                               lk_intern_cid(lk_ui_intern(f->ui), "ed"));
}

static int sp_layout(sp_fix *f) {
  return lk_layout(lk_ui_tree(f->ui), &f->cfg, f->rects);
}

static void sp_init(sp_fix *f, const char *text, lk_i32 vw, lk_i32 vh) {
  memset(f, 0, sizeof(*f));
  f->doc = lk_doc_from_str(NULL, NULL, NULL, text, (lk_u32)strlen(text));
  f->ed = lk_editor_new(NULL, NULL, NULL, f->doc, NULL);
  f->ui = lk_ui_create(NULL);
  f->ref = lk_resource_register(lk_ui_resources(f->ui), lk_editor_type(),
                                f->ed, "ed");
  lk_ui_set_text_backend(f->ui, lk_text_backend_stub());
  f->cfg.text = lk_text_backend_stub();
  f->cfg.viewport_w = vw;
  f->cfg.viewport_h = vh;
  f->cfg.state = lk_ui_state(f->ui);
  sp_frame(f);
  sp_layout(f);
  f->nid = lk_intern_cid(lk_ui_intern(f->ui), "ed");
}

static void sp_destroy(sp_fix *f) {
  lk_ui_destroy(f->ui);
  lk_editor_destroy(f->ed);
  lk_doc_destroy(f->doc);
}

static int sp_render(sp_fix *f, lk_render_list *rl) {
  return lk_render_build(lk_ui_tree(f->ui), f->rects, NULL,
                         lk_ui_state(f->ui), rl);
}

/* test palette (distinct from the theme and the selection color) */

static lk_color sp_rgb(lk_u8 r, lk_u8 g, lk_u8 b) {
  lk_color c;

  c.r = r;
  c.g = g;
  c.b = b;
  c.a = 255;

  return c;
}

static int col_eq(lk_color a, lk_color b) {
  return a.r == b.r && a.g == b.g && a.b == b.b && a.a == b.a;
}

#define SP_FG sp_rgb(201, 7, 9)
#define SP_BG sp_rgb(9, 33, 91)
#define SP_FG2 sp_rgb(7, 99, 190)

/* Build a one-span snapshot at the document's current revision. */
static void sp_set_one(sp_fix *f, lk_u32 start, lk_u32 end, lk_u8 flags) {
  lk_edit_span sp;
  lk_edit_span_snapshot snap;

  memset(&sp, 0, sizeof(sp));
  sp.start = start;
  sp.end = end;
  sp.flags = flags;
  sp.fg = SP_FG;
  sp.bg = SP_BG;

  memset(&snap, 0, sizeof(snap));
  snap.revision = lk_doc_revision(f->doc);
  snap.range_start = 0;
  snap.range_end = lk_doc_len(f->doc);
  snap.spans = &sp;
  snap.count = 1;
  lk_editor_set_spans(f->ed, &snap);
}

/* render-list query helpers */

static lk_u32 sp_count_runs(const lk_render_list *rl) {
  lk_u32 i;
  lk_u32 n = 0;

  for (i = 0; i < rl->count; i++) {
    if (rl->cmds[i].op == LK_ROP_DRAW_RUN) {
      n++;
    }
  }

  return n;
}

static const lk_render_cmd *sp_nth_run(const lk_render_list *rl, lk_u32 idx) {
  lk_u32 i;
  lk_u32 n = 0;

  for (i = 0; i < rl->count; i++) {
    if (rl->cmds[i].op == LK_ROP_DRAW_RUN) {
      if (n == idx) {
        return &rl->cmds[i];
      }

      n++;
    }
  }

  return NULL;
}

static int sp_run_is(const lk_render_list *rl, const lk_render_cmd *c,
                     const char *s) {
  lk_u32 n = (lk_u32)strlen(s);

  if (!c || c->run_len != n) {
    return 0;
  }

  return memcmp(rl->bytes + c->run_off, s, n) == 0;
}

static lk_u32 sp_count_runs_colored(const lk_render_list *rl, lk_color c) {
  lk_u32 i;
  lk_u32 n = 0;

  for (i = 0; i < rl->count; i++) {
    if (rl->cmds[i].op == LK_ROP_DRAW_RUN && col_eq(rl->cmds[i].color, c)) {
      n++;
    }
  }

  return n;
}

/* First FILL_RECT with exactly this color (NULL if none). */
static const lk_render_cmd *sp_find_fill(const lk_render_list *rl,
                                         lk_color c) {
  lk_u32 i;

  for (i = 0; i < rl->count; i++) {
    if (rl->cmds[i].op == LK_ROP_FILL_RECT && col_eq(rl->cmds[i].color, c)) {
      return &rl->cmds[i];
    }
  }

  return NULL;
}

static void test_span_midline_three_runs(void) {
  sp_fix f;
  lk_render_list rl;
  const lk_render_cmd *r0;
  const lk_render_cmd *r1;
  const lk_render_cmd *r2;

  BEGIN_TEST("span render: mid-line span -> 3 runs");

  sp_init(&f, "hello world", 400, 80);
  memset(&rl, 0, sizeof(rl));

  sp_set_one(&f, 2, 5, LK_SPAN_FG); /* "llo" */
  sp_layout(&f);

  CHECK(sp_render(&f, &rl));
  CHECK_EQ(sp_count_runs(&rl), 3);

  r0 = sp_nth_run(&rl, 0);
  r1 = sp_nth_run(&rl, 1);
  r2 = sp_nth_run(&rl, 2);

  CHECK(sp_run_is(&rl, r0, "he"));
  CHECK(r0 && r0->rect.x == 0 && r0->rect.w == 16);

  CHECK(sp_run_is(&rl, r1, "llo"));
  CHECK(r1 && r1->rect.x == 16 && r1->rect.w == 24);
  CHECK(r1 && col_eq(r1->color, SP_FG)); /* middle carries span fg */

  CHECK(sp_run_is(&rl, r2, " world"));
  CHECK(r2 && r2->rect.x == 40 && r2->rect.w == 48);

  /* flanks carry the style fg, not the span fg */
  CHECK(r0 && r2 && col_eq(r0->color, r2->color));
  CHECK(r0 && !col_eq(r0->color, SP_FG));

  lk_render_list_destroy(&rl);
  sp_destroy(&f);
  END_TEST();
}

static void test_span_bg_rect(void) {
  sp_fix f;
  lk_render_list rl;
  const lk_render_cmd *bg;

  BEGIN_TEST("span render: LK_SPAN_BG exact rect");

  sp_init(&f, "hello world", 400, 80);
  memset(&rl, 0, sizeof(rl));

  sp_set_one(&f, 2, 5, LK_SPAN_BG);
  sp_layout(&f);

  CHECK(sp_render(&f, &rl));

  bg = sp_find_fill(&rl, SP_BG);
  CHECK(bg != NULL);
  CHECK(bg && bg->rect.x == 16 && bg->rect.y == 0 && bg->rect.w == 24 &&
        bg->rect.h == 16);

  /* bg-only span: run colors unchanged (no LK_SPAN_FG) */
  CHECK_EQ(sp_count_runs_colored(&rl, SP_FG), 0);

  lk_render_list_destroy(&rl);
  sp_destroy(&f);
  END_TEST();
}

static void test_span_underline(void) {
  sp_fix f;
  lk_render_list rl;
  const lk_render_cmd *ul;

  BEGIN_TEST("span render: underline at baseline+1");

  sp_init(&f, "hello world", 400, 80);
  memset(&rl, 0, sizeof(rl));

  sp_set_one(&f, 2, 5, LK_SPAN_FG | LK_SPAN_UNDERLINE);
  sp_layout(&f);

  CHECK(sp_render(&f, &rl));

  /* the underline is the only SP_FG FILL_RECT (the run itself is a
   * DRAW_RUN); stub baseline = 12 -> y = 0 + 12 + 1 */
  ul = sp_find_fill(&rl, SP_FG);
  CHECK(ul != NULL);
  CHECK(ul && ul->rect.x == 16 && ul->rect.y == 13 && ul->rect.w == 24 &&
        ul->rect.h == 1);

  lk_render_list_destroy(&rl);
  sp_destroy(&f);
  END_TEST();
}

static void test_span_across_lines(void) {
  sp_fix f;
  lk_render_list rl;

  BEGIN_TEST("span render: span crossing a line boundary");

  /* a0 b1 c2 d3 \n4 e5 f6 g7 h8 */
  sp_init(&f, "abcd\nefgh", 400, 80);
  memset(&rl, 0, sizeof(rl));

  sp_set_one(&f, 2, 7, LK_SPAN_FG); /* "cd" + \n + "ef" */
  sp_layout(&f);

  CHECK(sp_render(&f, &rl));
  CHECK_EQ(sp_count_runs(&rl), 4);
  CHECK_EQ(sp_count_runs_colored(&rl, SP_FG), 2);

  /* tail of line 0 */
  CHECK(sp_run_is(&rl, sp_nth_run(&rl, 1), "cd"));
  CHECK(sp_nth_run(&rl, 1)->rect.x == 16 && sp_nth_run(&rl, 1)->rect.y == 0);
  CHECK(col_eq(sp_nth_run(&rl, 1)->color, SP_FG));

  /* head of line 1 */
  CHECK(sp_run_is(&rl, sp_nth_run(&rl, 2), "ef"));
  CHECK(sp_nth_run(&rl, 2)->rect.x == 0 && sp_nth_run(&rl, 2)->rect.y == 16);
  CHECK(col_eq(sp_nth_run(&rl, 2)->color, SP_FG));

  /* flanks unstyled */
  CHECK(!col_eq(sp_nth_run(&rl, 0)->color, SP_FG));
  CHECK(!col_eq(sp_nth_run(&rl, 3)->color, SP_FG));

  lk_render_list_destroy(&rl);
  sp_destroy(&f);
  END_TEST();
}

static void test_span_inside_tab_segment(void) {
  sp_fix f;
  lk_render_list rl;
  const lk_render_cmd *r1;
  const lk_render_cmd *r2;

  BEGIN_TEST("span render: sub-segment in tab-split run");

  /* a0 b1 \t2 c3 d4: runs "ab" at x0 and "cd" at the 32 px stop */
  sp_init(&f, "ab\tcd", 400, 80);
  memset(&rl, 0, sizeof(rl));

  sp_set_one(&f, 4, 5, LK_SPAN_FG); /* just "d", inside the 2nd run */
  sp_layout(&f);

  CHECK(sp_render(&f, &rl));
  CHECK_EQ(sp_count_runs(&rl), 3); /* "ab", "c", "d" */

  r1 = sp_nth_run(&rl, 1);
  r2 = sp_nth_run(&rl, 2);

  CHECK(sp_run_is(&rl, r1, "c"));
  CHECK(r1 && r1->rect.x == 32 && r1->rect.w == 8);
  CHECK(r1 && !col_eq(r1->color, SP_FG));

  CHECK(sp_run_is(&rl, r2, "d"));
  CHECK(r2 && r2->rect.x == 40 && r2->rect.w == 8);
  CHECK(r2 && col_eq(r2->color, SP_FG));

  lk_render_list_destroy(&rl);
  sp_destroy(&f);
  END_TEST();
}

static void test_span_forward_transform_on_edit(void) {
  sp_fix f;
  lk_render_list rl;

  BEGIN_TEST("span render: current snapshot forward-transforms through edit");

  sp_init(&f, "hello world", 400, 80);
  memset(&rl, 0, sizeof(rl));

  sp_set_one(&f, 2, 5, LK_SPAN_FG | LK_SPAN_BG);

  /* The copy was current when this transaction began, so it shifts
   * with the insert and stays styled -- no per-keystroke blink. */
  CHECK(lk_doc_insert(f.doc, 0, "zz", 2) == 1);
  sp_layout(&f);

  CHECK(sp_render(&f, &rl));

  CHECK_EQ(sp_count_runs(&rl), 3);
  CHECK(sp_run_is(&rl, sp_nth_run(&rl, 0), "zzhe"));
  CHECK(sp_run_is(&rl, sp_nth_run(&rl, 1), "llo"));
  CHECK_EQ(sp_count_runs_colored(&rl, SP_FG), 1);
  CHECK(sp_find_fill(&rl, SP_BG) != NULL);

  lk_render_list_destroy(&rl);
  sp_destroy(&f);
  END_TEST();
}

static void test_span_transform_insert_at_start(void) {
  sp_fix f;
  lk_render_list rl;

  BEGIN_TEST("span render: insert at span start grows leftward-inclusively");

  sp_init(&f, "hello world", 400, 80);
  memset(&rl, 0, sizeof(rl));

  sp_set_one(&f, 2, 5, LK_SPAN_FG);

  /* Insert exactly at the span's start: start stays (LEFT-anchor
   * rule), end shifts (RIGHT) -- matching what the annot store's
   * anchors do, so the producer agrees next frame. */
  CHECK(lk_doc_insert(f.doc, 2, "zz", 2) == 1);
  sp_layout(&f);

  CHECK(sp_render(&f, &rl));

  CHECK_EQ(sp_count_runs(&rl), 3);
  CHECK(sp_run_is(&rl, sp_nth_run(&rl, 1), "zzllo"));
  CHECK_EQ(sp_count_runs_colored(&rl, SP_FG), 1);

  lk_render_list_destroy(&rl);
  sp_destroy(&f);
  END_TEST();
}

static void test_span_transform_delete_covering_drops(void) {
  sp_fix f;
  lk_render_list rl;

  BEGIN_TEST("span render: delete covering the span drops it");

  sp_init(&f, "hello world", 400, 80);
  memset(&rl, 0, sizeof(rl));

  sp_set_one(&f, 2, 5, LK_SPAN_FG | LK_SPAN_BG);

  CHECK(lk_doc_delete(f.doc, 1, 6) == 1);
  sp_layout(&f);

  CHECK(sp_render(&f, &rl));

  /* Both edges collapsed to the delete point -> degenerate ->
   * dropped; render is identical to no-spans. */
  CHECK_EQ(sp_count_runs(&rl), 1);
  CHECK(sp_run_is(&rl, sp_nth_run(&rl, 0), "horld"));
  CHECK_EQ(sp_count_runs_colored(&rl, SP_FG), 0);
  CHECK(sp_find_fill(&rl, SP_BG) == NULL);

  lk_render_list_destroy(&rl);
  sp_destroy(&f);
  END_TEST();
}

static void test_span_transform_transaction_net(void) {
  sp_fix f;
  lk_render_list rl;

  BEGIN_TEST("span render: multi-op transaction transforms per delta");

  sp_init(&f, "hello world", 400, 80);
  memset(&rl, 0, sizeof(rl));

  sp_set_one(&f, 2, 5, LK_SPAN_FG);

  /* Insert then delete the same bytes in one bracket: the span rides
   * both deltas and lands back where it started. */
  lk_doc_begin(f.doc, 99);
  CHECK(lk_doc_insert(f.doc, 0, "zz", 2) == 1);
  CHECK(lk_doc_delete(f.doc, 0, 2) == 1);
  lk_doc_commit(f.doc);
  sp_layout(&f);

  CHECK(sp_render(&f, &rl));

  CHECK_EQ(sp_count_runs(&rl), 3);
  CHECK(sp_run_is(&rl, sp_nth_run(&rl, 1), "llo"));
  CHECK_EQ(sp_count_runs_colored(&rl, SP_FG), 1);

  lk_render_list_destroy(&rl);
  sp_destroy(&f);
  END_TEST();
}

static void test_span_truly_stale_ignored(void) {
  sp_fix f;
  lk_render_list rl;
  lk_edit_span sp;
  lk_edit_span_snapshot snap;
  lk_revision old_rev;

  BEGIN_TEST("span render: stale-base snapshot stays ignored");

  sp_init(&f, "hello world", 400, 80);
  memset(&rl, 0, sizeof(rl));

  /* Stamp a snapshot with a revision that predates an edit: the
   * transform guard (span_rev == delta.before) can never fire for
   * it, so it is ignored at render, before and after further
   * edits. */
  old_rev = lk_doc_revision(f.doc);
  CHECK(lk_doc_insert(f.doc, 0, "zz", 2) == 1);

  memset(&sp, 0, sizeof(sp));
  sp.start = 2;
  sp.end = 5;
  sp.flags = LK_SPAN_FG | LK_SPAN_BG;
  sp.fg = SP_FG;
  sp.bg = SP_BG;

  memset(&snap, 0, sizeof(snap));
  snap.revision = old_rev;
  snap.range_start = 0;
  snap.range_end = lk_doc_len(f.doc);
  snap.spans = &sp;
  snap.count = 1;
  lk_editor_set_spans(f.ed, &snap);

  sp_layout(&f);
  CHECK(sp_render(&f, &rl));
  CHECK_EQ(sp_count_runs(&rl), 1);
  CHECK_EQ(sp_count_runs_colored(&rl, SP_FG), 0);

  /* A further edit must not transform-from-wrong-base either. */
  CHECK(lk_doc_insert(f.doc, 0, "q", 1) == 1);
  sp_layout(&f);
  lk_render_list_destroy(&rl);
  memset(&rl, 0, sizeof(rl));
  CHECK(sp_render(&f, &rl));
  CHECK_EQ(sp_count_runs(&rl), 1);
  CHECK_EQ(sp_count_runs_colored(&rl, SP_FG), 0);
  CHECK(sp_find_fill(&rl, SP_BG) == NULL);

  lk_render_list_destroy(&rl);
  sp_destroy(&f);
  END_TEST();
}

static void test_span_partial_viewport_coverage(void) {
  sp_fix f;
  lk_render_list rl;

  BEGIN_TEST("span render: partial coverage styles its part");

  /* 3 visible lines; the snapshot only covers part of line 0 */
  sp_init(&f, "aaaa\nbbbb\ncccc", 400, 80);
  memset(&rl, 0, sizeof(rl));

  sp_set_one(&f, 1, 3, LK_SPAN_FG);
  sp_layout(&f);

  CHECK(sp_render(&f, &rl));
  CHECK_EQ(sp_count_runs(&rl), 5); /* "a","aa","a","bbbb","cccc" */
  CHECK_EQ(sp_count_runs_colored(&rl, SP_FG), 1);
  CHECK(sp_run_is(&rl, sp_nth_run(&rl, 1), "aa"));
  CHECK(col_eq(sp_nth_run(&rl, 1)->color, SP_FG));

  /* the other lines render exactly one plain run each */
  CHECK(sp_run_is(&rl, sp_nth_run(&rl, 3), "bbbb"));
  CHECK(sp_run_is(&rl, sp_nth_run(&rl, 4), "cccc"));

  lk_render_list_destroy(&rl);
  sp_destroy(&f);
  END_TEST();
}

static void test_span_utf8_boundary_clamp(void) {
  sp_fix f;
  lk_render_list rl;
  const lk_render_cmd *r0;
  const lk_render_cmd *r1;

  BEGIN_TEST("span render: boundary clamps to codepoint");

  /* a0 e-acute(1,2) sp3 b4 */
  sp_init(&f, "a\xC3\xA9 b", 400, 80);
  memset(&rl, 0, sizeof(rl));

  /* end lands mid-codepoint (byte 2): clamps down to 1, so only
   * "a" is styled and the e-acute sequence is never split */
  sp_set_one(&f, 0, 2, LK_SPAN_FG);
  sp_layout(&f);

  CHECK(sp_render(&f, &rl));
  CHECK_EQ(sp_count_runs(&rl), 2);

  r0 = sp_nth_run(&rl, 0);
  r1 = sp_nth_run(&rl, 1);

  CHECK(sp_run_is(&rl, r0, "a"));
  CHECK(r0 && r0->rect.x == 0 && r0->rect.w == 8);
  CHECK(r0 && col_eq(r0->color, SP_FG));

  CHECK(sp_run_is(&rl, r1, "\xC3\xA9 b"));
  CHECK(r1 && r1->rect.x == 8 && r1->rect.w == 24); /* 3 codepoints */
  CHECK(r1 && !col_eq(r1->color, SP_FG));

  lk_render_list_destroy(&rl);
  sp_destroy(&f);
  END_TEST();
}

static void test_span_replace_and_clear(void) {
  sp_fix f;
  lk_render_list rl;

  BEGIN_TEST("span render: set_spans replace + clear");

  sp_init(&f, "hello world", 400, 80);
  memset(&rl, 0, sizeof(rl));

  sp_set_one(&f, 0, 5, LK_SPAN_FG);
  sp_layout(&f);
  CHECK(sp_render(&f, &rl));
  CHECK_EQ(sp_count_runs_colored(&rl, SP_FG), 1);
  CHECK(sp_run_is(&rl, sp_nth_run(&rl, 0), "hello"));

  /* replace: only the new span is styled */
  sp_set_one(&f, 6, 11, LK_SPAN_FG);
  sp_layout(&f);
  CHECK(sp_render(&f, &rl));
  CHECK_EQ(sp_count_runs_colored(&rl, SP_FG), 1);
  CHECK(sp_run_is(&rl, sp_nth_run(&rl, 1), "world"));
  CHECK(col_eq(sp_nth_run(&rl, 1)->color, SP_FG));

  /* clear via NULL */
  lk_editor_set_spans(f.ed, NULL);
  sp_layout(&f);
  CHECK(sp_render(&f, &rl));
  CHECK_EQ(sp_count_runs(&rl), 1);
  CHECK_EQ(sp_count_runs_colored(&rl, SP_FG), 0);

  /* clear via an empty snapshot */
  sp_set_one(&f, 0, 5, LK_SPAN_FG);
  {
    lk_edit_span_snapshot empty;

    memset(&empty, 0, sizeof(empty));
    empty.revision = lk_doc_revision(f.doc);
    lk_editor_set_spans(f.ed, &empty);
  }
  sp_layout(&f);
  CHECK(sp_render(&f, &rl));
  CHECK_EQ(sp_count_runs_colored(&rl, SP_FG), 0);

  lk_render_list_destroy(&rl);
  sp_destroy(&f);
  END_TEST();
}

/* Full producer pattern: annotations in an attached store -> query
 * over the visible byte range -> sorted spans via a test-local
 * layer -> color table -> set_spans -> styled render; edit the
 * document -> stale, unstyled; re-produce -> styling back at the
 * transformed positions. */
static void test_span_producer_end_to_end(void) {
  sp_fix f;
  lk_annot_store *st;
  lk_render_list rl;
  lk_u32 kw_id;
  lk_u32 err_id;

  BEGIN_TEST("span render: producer end-to-end");

  /* a0 b1 c2 sp3 d4 e5 f6 */
  sp_init(&f, "abc def", 400, 80);
  memset(&rl, 0, sizeof(rl));

  st = lk_annot_store_new(NULL, NULL, NULL);
  lk_annot_store_attach(st, f.doc);
  kw_id = lk_annot_add(st, 0, 3, "kw", NULL, NULL, 0);
  err_id = lk_annot_add(st, 4, 7, "err", NULL, NULL, 0);
  CHECK(kw_id != 0);
  CHECK(err_id != 0);

  /* produce + render, twice around an edit */
  {
    lk_u32 round;

    for (round = 0; round < 2; round++) {
      lk_edit_span spans[8];
      lk_u32 span_n = 0;
      lk_annot_query q;
      lk_u32 i;
      lk_edit_span_snapshot snap;

      lk_annot_query_init(&q);
      lk_annot_in_range(st, 0, lk_doc_len(f.doc), NULL, &q);
      CHECK_EQ(q.count, 2);

      for (i = 0; i < q.count && span_n < 8; i++) {
        lk_u32 s;
        lk_u32 e;
        const lk_annot_record *rec = lk_annot_get(st, q.ids[i]);

        if (!rec || !lk_annot_get_span(st, q.ids[i], &s, &e)) {
          continue;
        }

        memset(&spans[span_n], 0, sizeof(spans[0]));
        spans[span_n].start = s;
        spans[span_n].end = e;

        /* test-local layer -> color policy (the store never knows
         * what a layer MEANS -- flattening to colors happens here) */
        if (strcmp(rec->layer, "kw") == 0) {
          spans[span_n].flags = LK_SPAN_FG;
          spans[span_n].fg = SP_FG2;
        } else {
          spans[span_n].flags = LK_SPAN_FG | LK_SPAN_UNDERLINE;
          spans[span_n].fg = SP_FG;
        }

        span_n++;
      }

      /* insertion order == start order here; assert the producer
       * contract anyway */
      CHECK_EQ(span_n, 2);
      CHECK(spans[0].start < spans[1].start);
      CHECK(spans[0].end <= spans[1].start);

      memset(&snap, 0, sizeof(snap));
      snap.revision = lk_doc_revision(f.doc);
      snap.range_start = 0;
      snap.range_end = lk_doc_len(f.doc);
      snap.spans = spans;
      snap.count = span_n;
      lk_editor_set_spans(f.ed, &snap);
      lk_annot_query_free(&q);

      sp_layout(&f);
      CHECK(sp_render(&f, &rl));

      if (round == 0) {
        /* "abc"(kw) " " "def"(err+underline) */
        CHECK_EQ(sp_count_runs(&rl), 3);
        CHECK(sp_run_is(&rl, sp_nth_run(&rl, 0), "abc"));
        CHECK(col_eq(sp_nth_run(&rl, 0)->color, SP_FG2));
        CHECK(sp_run_is(&rl, sp_nth_run(&rl, 2), "def"));
        CHECK(col_eq(sp_nth_run(&rl, 2)->color, SP_FG));
        CHECK(sp_find_fill(&rl, SP_FG) != NULL); /* underline */

        /* now edit: prepend "zz".  The editor forward-transforms its
         * span copy through the delta, so the in-between frame
         * renders EXACTLY what the producer will re-derive from the
         * transformed anchors next round -- assert that agreement
         * directly (same expectations as the round-1 block). */
        CHECK(lk_doc_insert(f.doc, 0, "zz", 2) == 1);
        sp_layout(&f);
        CHECK(sp_render(&f, &rl));
        CHECK_EQ(sp_count_runs(&rl), 3);
        CHECK(sp_run_is(&rl, sp_nth_run(&rl, 0), "zzabc"));
        CHECK(col_eq(sp_nth_run(&rl, 0)->color, SP_FG2));
        CHECK(sp_run_is(&rl, sp_nth_run(&rl, 2), "def"));
        CHECK(col_eq(sp_nth_run(&rl, 2)->color, SP_FG));
        CHECK(sp_find_fill(&rl, SP_FG) != NULL); /* underline */
      } else {
        /* re-produced after the edit.  The "kw" annotation started
         * at 0 with a LEFT-bias anchor, so inserting AT 0 leaves it
         * there: the span grew to [0,5) "zzabc" (weft semantics --
         * the inserted text lands inside the annotation).  "err"
         * shifted to [6,9). */
        CHECK_EQ(sp_count_runs(&rl), 3); /* "zzabc"," ","def" */
        CHECK(sp_run_is(&rl, sp_nth_run(&rl, 0), "zzabc"));
        CHECK(sp_nth_run(&rl, 0)->rect.x == 0);
        CHECK(col_eq(sp_nth_run(&rl, 0)->color, SP_FG2));
        CHECK(sp_run_is(&rl, sp_nth_run(&rl, 2), "def"));
        CHECK(sp_nth_run(&rl, 2)->rect.x == 48);
        CHECK(col_eq(sp_nth_run(&rl, 2)->color, SP_FG));
      }
    }
  }

  lk_render_list_destroy(&rl);
  lk_annot_store_destroy(st);
  sp_destroy(&f);
  END_TEST();
}

/* ---- runner ---- */

void lk_annot_run_tests(void) {
  printf("\nlk annot store tests (ported weft suite):\n");
  test_annot_store_new_empty();
  test_add_annotation();
  test_add_multiple_annotations();
  test_remove_annotation();
  test_query_at_position();
  test_query_in_range();
  test_query_by_layer();
  test_on_insert_after();
  test_on_insert_before();
  test_on_insert_at_start_left_bias();
  test_on_insert_at_end_right_bias();
  test_on_insert_in_middle();
  test_on_delete_after();
  test_on_delete_before();
  test_on_delete_overlapping_start();
  test_on_delete_overlapping_end();
  test_on_delete_entire_annotation();
  test_on_delete_in_middle();
  test_clear_layer();
  test_layer_state_version();
  test_zero_length_annotation_rejected();
  test_annotation_becomes_zero_length();
  test_adjacent_annotations();

  printf("\nlk annot subscription tests:\n");
  test_sub_multi_op_transaction();
  test_sub_boundary_exact_deletes();
  test_sub_undo_round_trip();
  test_sub_destroy_order();
  test_sub_revision_tracking();

  printf("\nlk styled-span render tests:\n");
  test_span_midline_three_runs();
  test_span_bg_rect();
  test_span_underline();
  test_span_across_lines();
  test_span_inside_tab_segment();
  test_span_forward_transform_on_edit();
  test_span_transform_insert_at_start();
  test_span_transform_delete_covering_drops();
  test_span_transform_transaction_net();
  test_span_truly_stale_ignored();
  test_span_partial_viewport_coverage();
  test_span_utf8_boundary_clamp();
  test_span_replace_and_clear();
  test_span_producer_end_to_end();
}
