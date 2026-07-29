/*
 * lk-document-test.c -- document + edit history tests (editor track,
 * stage A).
 *
 * Two blocks: a port of weft's test_document.c (every case carried
 * over), and new tests for the change protocol -- the pinned
 * contracts of docs/editor.md section 3.3, transaction grouping,
 * subscriber dispatch, delta byte contents, revision chains, and
 * undo/redo through the notification path.
 */

#include <stdio.h>
#include <string.h>

#include <lk-document.h>

#include "lk-test-harness.h"

/* ---- helpers ---- */

/* Read the whole document into buf (must be large enough) and
 * NUL-terminate. */
static void doc_all(const lk_document *d, char *buf) {
  lk_u32 n = lk_doc_get_text(d, 0, buf, lk_doc_len(d));

  buf[n] = '\0';
}

/* Notification recorder: copies delta metadata and bytes (the
 * notification-scoped pointers are dead after the callback). */
#define REC_MAX_DELTAS 8
#define REC_MAX_BYTES 64

typedef struct doc_rec {
  int calls;
  lk_u32 n; /* delta count of the last notification */
  lk_doc_delta deltas[REC_MAX_DELTAS];
  char ins[REC_MAX_DELTAS][REC_MAX_BYTES];
  char del[REC_MAX_DELTAS][REC_MAX_BYTES];
  int order;  /* value of *seq when last notified */
  int *seq;   /* optional shared call-order counter */
} doc_rec;

static void rec_listener(void *ud, const lk_document *d,
                         const lk_doc_delta *deltas, lk_u32 n) {
  doc_rec *r = (doc_rec *)ud;
  lk_u32 i;

  (void)d;

  r->calls++;
  r->n = n > REC_MAX_DELTAS ? REC_MAX_DELTAS : n;

  for (i = 0; i < r->n; i++) {
    lk_u32 c;

    r->deltas[i] = deltas[i];

    c = deltas[i].inserted_len;

    if (c > REC_MAX_BYTES - 1) {
      c = REC_MAX_BYTES - 1;
    }

    if (c && deltas[i].inserted) {
      memcpy(r->ins[i], deltas[i].inserted, c);
    }

    r->ins[i][c] = '\0';

    c = deltas[i].deleted_len;

    if (c > REC_MAX_BYTES - 1) {
      c = REC_MAX_BYTES - 1;
    }

    if (c && deltas[i].deleted) {
      memcpy(r->del[i], deltas[i].deleted, c);
    }

    r->del[i][c] = '\0';
  }

  if (r->seq) {
    (*r->seq)++;
    r->order = *r->seq;
  }
}

/* Reentrancy probe: attempts to mutate the document from inside a
 * notification. */
typedef struct doc_reent {
  int calls;
  int insert_ret;
  int delete_ret;
} doc_reent;

static void reent_listener(void *ud, const lk_document *d,
                           const lk_doc_delta *deltas, lk_u32 n) {
  doc_reent *r = (doc_reent *)ud;
  lk_document *md = (lk_document *)d; /* deliberate: probing the guard */

  (void)deltas;
  (void)n;

  r->calls++;
  r->insert_ret = lk_doc_insert(md, 0, "x", 1);
  r->delete_ret = lk_doc_delete(md, 0, 1);
}

/* ================================================================
 * Ported: weft test_document.c
 * ================================================================ */

static void test_doc_new_empty(void) {
  lk_document *d = lk_doc_new(NULL, NULL, NULL);

  BEGIN_TEST("doc: new creates empty document");

  CHECK(d != NULL);
  CHECK_EQ(lk_doc_len(d), 0);
  CHECK_EQ(lk_doc_line_count(d), 1); /* empty doc has 1 line */

  lk_doc_destroy(d);
  END_TEST();
}

static void test_doc_from_str(void) {
  lk_document *d = lk_doc_from_str(NULL, NULL, NULL, "hello", 5);
  char buf[16];
  lk_u32 got;

  BEGIN_TEST("doc: from_str creates content");

  CHECK(d != NULL);
  CHECK_EQ(lk_doc_len(d), 5);

  got = lk_doc_get_text(d, 0, buf, 5);
  buf[got] = '\0';
  CHECK_EQ(got, 5);
  CHECK(strcmp(buf, "hello") == 0);

  lk_doc_destroy(d);
  END_TEST();
}

static void test_doc_insert_at_start(void) {
  lk_document *d = lk_doc_from_str(NULL, NULL, NULL, "world", 5);
  char buf[16];

  BEGIN_TEST("doc: insert at start");

  CHECK(lk_doc_insert(d, 0, "hello ", 6));
  CHECK_EQ(lk_doc_len(d), 11);

  doc_all(d, buf);
  CHECK(strcmp(buf, "hello world") == 0);

  lk_doc_destroy(d);
  END_TEST();
}

static void test_doc_insert_at_end(void) {
  lk_document *d = lk_doc_from_str(NULL, NULL, NULL, "hello", 5);
  char buf[16];

  BEGIN_TEST("doc: insert at end");

  CHECK(lk_doc_insert(d, 5, " world", 6));
  CHECK_EQ(lk_doc_len(d), 11);

  doc_all(d, buf);
  CHECK(strcmp(buf, "hello world") == 0);

  lk_doc_destroy(d);
  END_TEST();
}

static void test_doc_insert_in_middle(void) {
  lk_document *d = lk_doc_from_str(NULL, NULL, NULL, "helloworld", 10);
  char buf[16];

  BEGIN_TEST("doc: insert in middle");

  CHECK(lk_doc_insert(d, 5, " ", 1));
  CHECK_EQ(lk_doc_len(d), 11);

  doc_all(d, buf);
  CHECK(strcmp(buf, "hello world") == 0);

  lk_doc_destroy(d);
  END_TEST();
}

static void test_doc_insert_empty_doc(void) {
  lk_document *d = lk_doc_new(NULL, NULL, NULL);
  char buf[16];

  BEGIN_TEST("doc: insert into empty document");

  CHECK(lk_doc_insert(d, 0, "hello", 5));
  CHECK_EQ(lk_doc_len(d), 5);

  doc_all(d, buf);
  CHECK(strcmp(buf, "hello") == 0);

  lk_doc_destroy(d);
  END_TEST();
}

static void test_doc_delete_from_start(void) {
  lk_document *d = lk_doc_from_str(NULL, NULL, NULL, "hello world", 11);
  char buf[16];

  BEGIN_TEST("doc: delete from start");

  CHECK(lk_doc_delete(d, 0, 6));
  CHECK_EQ(lk_doc_len(d), 5);

  doc_all(d, buf);
  CHECK(strcmp(buf, "world") == 0);

  lk_doc_destroy(d);
  END_TEST();
}

static void test_doc_delete_from_end(void) {
  lk_document *d = lk_doc_from_str(NULL, NULL, NULL, "hello world", 11);
  char buf[16];

  BEGIN_TEST("doc: delete from end");

  CHECK(lk_doc_delete(d, 5, 6));
  CHECK_EQ(lk_doc_len(d), 5);

  doc_all(d, buf);
  CHECK(strcmp(buf, "hello") == 0);

  lk_doc_destroy(d);
  END_TEST();
}

static void test_doc_delete_from_middle(void) {
  lk_document *d = lk_doc_from_str(NULL, NULL, NULL, "hello world", 11);
  char buf[16];

  BEGIN_TEST("doc: delete from middle");

  CHECK(lk_doc_delete(d, 5, 1)); /* delete the space */
  CHECK_EQ(lk_doc_len(d), 10);

  doc_all(d, buf);
  CHECK(strcmp(buf, "helloworld") == 0);

  lk_doc_destroy(d);
  END_TEST();
}

static void test_doc_delete_all(void) {
  lk_document *d = lk_doc_from_str(NULL, NULL, NULL, "hello", 5);

  BEGIN_TEST("doc: delete all content");

  CHECK(lk_doc_delete(d, 0, 5));
  CHECK_EQ(lk_doc_len(d), 0);

  lk_doc_destroy(d);
  END_TEST();
}

static void test_doc_line_count_single(void) {
  lk_document *d = lk_doc_from_str(NULL, NULL, NULL, "hello", 5);

  BEGIN_TEST("doc: line count single line");

  CHECK_EQ(lk_doc_line_count(d), 1);

  lk_doc_destroy(d);
  END_TEST();
}

static void test_doc_line_count_multiple(void) {
  lk_document *d = lk_doc_from_str(NULL, NULL, NULL, "hello\nworld\n", 12);

  BEGIN_TEST("doc: line count multiple lines");

  CHECK_EQ(lk_doc_line_count(d), 3); /* "hello", "world", "" */

  lk_doc_destroy(d);
  END_TEST();
}

static void test_doc_line_start_end(void) {
  lk_document *d = lk_doc_from_str(NULL, NULL, NULL, "hello\nworld\ntest", 16);

  BEGIN_TEST("doc: line_start and line_end");

  CHECK_EQ(lk_doc_line_start(d, 0), 0);
  CHECK_EQ(lk_doc_line_end(d, 0), 5);

  CHECK_EQ(lk_doc_line_start(d, 1), 6);
  CHECK_EQ(lk_doc_line_end(d, 1), 11);

  CHECK_EQ(lk_doc_line_start(d, 2), 12);
  CHECK_EQ(lk_doc_line_end(d, 2), 16);

  lk_doc_destroy(d);
  END_TEST();
}

static void test_doc_pos_to_line(void) {
  lk_document *d = lk_doc_from_str(NULL, NULL, NULL, "hello\nworld\ntest", 16);

  BEGIN_TEST("doc: pos_to_line conversion");

  CHECK_EQ(lk_doc_pos_to_line(d, 0), 0);
  CHECK_EQ(lk_doc_pos_to_line(d, 4), 0);
  CHECK_EQ(lk_doc_pos_to_line(d, 5), 0); /* \n belongs to line 0 */
  CHECK_EQ(lk_doc_pos_to_line(d, 6), 1);
  CHECK_EQ(lk_doc_pos_to_line(d, 12), 2);

  lk_doc_destroy(d);
  END_TEST();
}

static void test_doc_line_index_after_insert(void) {
  lk_document *d = lk_doc_from_str(NULL, NULL, NULL, "hello\nworld", 11);

  BEGIN_TEST("doc: line index updates after insert");

  CHECK_EQ(lk_doc_line_count(d), 2);

  CHECK(lk_doc_insert(d, 3, "\n", 1));
  CHECK_EQ(lk_doc_line_count(d), 3);

  CHECK_EQ(lk_doc_line_start(d, 0), 0);
  CHECK_EQ(lk_doc_line_end(d, 0), 3);

  CHECK_EQ(lk_doc_line_start(d, 1), 4);
  CHECK_EQ(lk_doc_line_end(d, 1), 6);

  lk_doc_destroy(d);
  END_TEST();
}

static void test_doc_line_index_after_delete(void) {
  lk_document *d = lk_doc_from_str(NULL, NULL, NULL, "hello\nworld\ntest", 16);

  BEGIN_TEST("doc: line index updates after delete");

  CHECK_EQ(lk_doc_line_count(d), 3);

  CHECK(lk_doc_delete(d, 5, 1)); /* merge lines 0 and 1 */
  CHECK_EQ(lk_doc_line_count(d), 2);

  CHECK_EQ(lk_doc_line_start(d, 0), 0);
  CHECK_EQ(lk_doc_line_end(d, 0), 10);

  lk_doc_destroy(d);
  END_TEST();
}

static void test_doc_get_byte(void) {
  lk_document *d = lk_doc_from_str(NULL, NULL, NULL, "hello", 5);

  BEGIN_TEST("doc: get_byte retrieves correct bytes");

  CHECK_EQ(lk_doc_get_byte(d, 0), 'h');
  CHECK_EQ(lk_doc_get_byte(d, 1), 'e');
  CHECK_EQ(lk_doc_get_byte(d, 4), 'o');
  CHECK_EQ(lk_doc_get_byte(d, 5), 0); /* out of bounds */

  lk_doc_destroy(d);
  END_TEST();
}

static void test_doc_multiple_inserts(void) {
  lk_document *d = lk_doc_new(NULL, NULL, NULL);
  char buf[8];

  BEGIN_TEST("doc: multiple sequential inserts");

  CHECK(lk_doc_insert(d, 0, "c", 1));
  CHECK(lk_doc_insert(d, 0, "b", 1));
  CHECK(lk_doc_insert(d, 0, "a", 1));
  CHECK_EQ(lk_doc_len(d), 3);

  doc_all(d, buf);
  CHECK(strcmp(buf, "abc") == 0);

  lk_doc_destroy(d);
  END_TEST();
}

static void test_doc_insert_delete_insert(void) {
  lk_document *d = lk_doc_from_str(NULL, NULL, NULL, "hello", 5);
  char buf[8];

  BEGIN_TEST("doc: insert, delete, insert sequence");

  CHECK(lk_doc_delete(d, 0, 2)); /* "llo" */
  CHECK_EQ(lk_doc_len(d), 3);

  CHECK(lk_doc_insert(d, 0, "ye", 2)); /* "yello" */
  CHECK_EQ(lk_doc_len(d), 5);

  doc_all(d, buf);
  CHECK(strcmp(buf, "yello") == 0);

  lk_doc_destroy(d);
  END_TEST();
}

static void test_doc_insert_past_end_fails(void) {
  lk_document *d = lk_doc_from_str(NULL, NULL, NULL, "hello", 5);

  BEGIN_TEST("doc: insert past end returns 0");

  CHECK(!lk_doc_insert(d, 10, "x", 1));
  CHECK_EQ(lk_doc_len(d), 5); /* unchanged */

  lk_doc_destroy(d);
  END_TEST();
}

static void test_doc_delete_past_end_clamps(void) {
  lk_document *d = lk_doc_from_str(NULL, NULL, NULL, "hello", 5);
  char buf[8];

  BEGIN_TEST("doc: delete past end clamps");

  CHECK(lk_doc_delete(d, 3, 10)); /* clamps to deleting "lo" */
  CHECK_EQ(lk_doc_len(d), 3);

  doc_all(d, buf);
  CHECK(strcmp(buf, "hel") == 0);

  lk_doc_destroy(d);
  END_TEST();
}

static void test_doc_get_text_partial(void) {
  lk_document *d = lk_doc_from_str(NULL, NULL, NULL, "hello world", 11);
  char buf[6];
  lk_u32 got;

  BEGIN_TEST("doc: get_text with smaller buffer");

  got = lk_doc_get_text(d, 0, buf, 5);
  buf[got] = '\0';
  CHECK_EQ(got, 5);
  CHECK(strcmp(buf, "hello") == 0);

  lk_doc_destroy(d);
  END_TEST();
}

/* ================================================================
 * New: pinned contracts (docs/editor.md section 3.3)
 * ================================================================ */

static void test_doc_empty_is_one_line(void) {
  lk_document *d = lk_doc_new(NULL, NULL, NULL);

  BEGIN_TEST("contract 1: empty doc is one empty line");

  CHECK_EQ(lk_doc_line_count(d), 1);
  CHECK_EQ(lk_doc_line_start(d, 0), 0);
  CHECK_EQ(lk_doc_line_end(d, 0), 0);
  CHECK_EQ(lk_doc_line_length(d, 0), 0);
  CHECK_EQ(lk_doc_pos_to_line(d, 0), 0);

  lk_doc_destroy(d);
  END_TEST();
}

static void test_doc_trailing_newline_lines(void) {
  lk_document *d = lk_doc_from_str(NULL, NULL, NULL, "abc\n", 4);

  BEGIN_TEST("contract 2: line_end excl \\n, trailing line");

  /* trailing \n yields a final empty line */
  CHECK_EQ(lk_doc_line_count(d), 2);

  /* line_end is the offset of the \n, exclusive of it */
  CHECK_EQ(lk_doc_line_end(d, 0), 3);

  /* line_length includes the \n when present */
  CHECK_EQ(lk_doc_line_length(d, 0), 4);

  CHECK_EQ(lk_doc_line_start(d, 1), 4);
  CHECK_EQ(lk_doc_line_end(d, 1), 4);
  CHECK_EQ(lk_doc_line_length(d, 1), 0);

  lk_doc_destroy(d);
  END_TEST();
}

static void test_doc_get_text_no_terminator(void) {
  lk_document *d = lk_doc_from_str(NULL, NULL, NULL, "abc", 3);
  char buf[8];
  lk_u32 got;

  BEGIN_TEST("contract 3: get_text terminator-free");

  memset(buf, 'Z', sizeof(buf));
  got = lk_doc_get_text(d, 0, buf, 8);
  CHECK_EQ(got, 3);
  CHECK(buf[0] == 'a' && buf[1] == 'b' && buf[2] == 'c');

  /* bytes past the written region are untouched -- no terminator */
  CHECK(buf[3] == 'Z' && buf[7] == 'Z');

  /* pos >= len reads 0 bytes */
  CHECK_EQ(lk_doc_get_text(d, 3, buf, 8), 0);
  CHECK_EQ(lk_doc_get_text(d, 99, buf, 8), 0);

  lk_doc_destroy(d);
  END_TEST();
}

static void test_doc_zero_len_ops_rejected(void) {
  lk_document *d = lk_doc_from_str(NULL, NULL, NULL, "abc", 3);
  doc_rec rec;
  lk_revision r0;

  BEGIN_TEST("contract 4: zero-length ops rejected");

  memset(&rec, 0, sizeof(rec));
  lk_doc_subscribe(d, rec_listener, &rec);
  r0 = lk_doc_revision(d);

  CHECK(!lk_doc_insert(d, 0, "x", 0));
  CHECK(!lk_doc_delete(d, 0, 0));

  CHECK_EQ(lk_doc_len(d), 3);
  CHECK_EQ(rec.calls, 0);
  CHECK(lk_revision_equal(r0, lk_doc_revision(d)));

  lk_doc_destroy(d);
  END_TEST();
}

static void test_doc_bounds_and_overflow(void) {
  lk_document *d = lk_doc_from_str(NULL, NULL, NULL, "hello", 5);
  char buf[8];

  BEGIN_TEST("contract 5: bounds and u32 overflow");

  /* insert with pos > len fails */
  CHECK(!lk_doc_insert(d, 6, "x", 1));

  /* delete with pos >= len fails */
  CHECK(!lk_doc_delete(d, 5, 1));
  CHECK(!lk_doc_delete(d, 99, 1));

  /* pos + len overflowing lk_u32 fails */
  CHECK(!lk_doc_delete(d, 2, 0xFFFFFFFFu));
  CHECK(!lk_doc_insert(d, 0, "x", 0xFFFFFFFFu));
  CHECK_EQ(lk_doc_len(d), 5);

  /* delete clamps pos + len to the document end */
  CHECK(lk_doc_delete(d, 3, 10));

  doc_all(d, buf);
  CHECK(strcmp(buf, "hel") == 0);

  lk_doc_destroy(d);
  END_TEST();
}

static void test_doc_bytes_verbatim(void) {
  lk_document *d = lk_doc_new(NULL, NULL, NULL);
  char raw[5];
  char buf[8];
  lk_u32 got;

  BEGIN_TEST("contract 6: bytes stored verbatim");

  raw[0] = (char)0xFF; /* invalid UTF-8, kept as-is */
  raw[1] = (char)0xC0;
  raw[2] = (char)0x80;
  raw[3] = '\n';
  raw[4] = (char)0xFE;

  CHECK(lk_doc_insert(d, 0, raw, 5));
  CHECK_EQ(lk_doc_len(d), 5);

  got = lk_doc_get_text(d, 0, buf, 5);
  CHECK_EQ(got, 5);
  CHECK(memcmp(buf, raw, 5) == 0);

  CHECK_EQ(lk_doc_get_byte(d, 0), 0xFF);
  CHECK_EQ(lk_doc_get_byte(d, 4), 0xFE);

  /* the \n still counts as a line break -- just a byte on the line */
  CHECK_EQ(lk_doc_line_count(d), 2);

  lk_doc_destroy(d);
  END_TEST();
}

static void test_doc_listener_no_reentrancy(void) {
  lk_document *d = lk_doc_new(NULL, NULL, NULL);
  doc_reent re;
  lk_revision r0;
  lk_revision r1;
  char buf[8];

  BEGIN_TEST("contract 7: listener mutation rejected");

  memset(&re, 0, sizeof(re));
  lk_doc_subscribe(d, reent_listener, &re);
  r0 = lk_doc_revision(d);

  CHECK(lk_doc_insert(d, 0, "abc", 3));

  CHECK_EQ(re.calls, 1);
  CHECK_EQ(re.insert_ret, 0);
  CHECK_EQ(re.delete_ret, 0);

  /* the reentrant attempts changed nothing */
  CHECK_EQ(lk_doc_len(d), 3);
  doc_all(d, buf);
  CHECK(strcmp(buf, "abc") == 0);

  /* revision advanced exactly once, for the outer insert */
  r1 = lk_doc_revision(d);
  CHECK(r1.hi == r0.hi && r1.lo == r0.lo + 1);

  lk_doc_destroy(d);
  END_TEST();
}

static void test_doc_failed_ops_keep_revision(void) {
  lk_document *d = lk_doc_from_str(NULL, NULL, NULL, "hello", 5);
  lk_revision r0 = lk_doc_revision(d);

  BEGIN_TEST("contract 8: failed ops keep revision");

  CHECK(!lk_doc_insert(d, 10, "x", 1));
  CHECK(!lk_doc_delete(d, 5, 1));
  CHECK(!lk_doc_insert(d, 0, "x", 0));
  CHECK(!lk_doc_delete(d, 2, 0xFFFFFFFFu));

  CHECK(lk_revision_equal(r0, lk_doc_revision(d)));

  lk_doc_destroy(d);
  END_TEST();
}

static void test_doc_delta_revision_pairs(void) {
  lk_document *d = lk_doc_new(NULL, NULL, NULL);
  doc_rec rec;
  lk_revision r0;

  BEGIN_TEST("contract 8: deltas carry before/after");

  memset(&rec, 0, sizeof(rec));
  lk_doc_subscribe(d, rec_listener, &rec);
  r0 = lk_doc_revision(d);

  CHECK(lk_doc_insert(d, 0, "ab", 2));

  CHECK_EQ(rec.n, 1);
  CHECK(lk_revision_equal(rec.deltas[0].before, r0));
  CHECK(lk_revision_equal(rec.deltas[0].after, lk_doc_revision(d)));
  CHECK(lk_revision_before(rec.deltas[0].before, rec.deltas[0].after));
  CHECK(!lk_revision_equal(rec.deltas[0].before, rec.deltas[0].after));

  lk_doc_destroy(d);
  END_TEST();
}

static void test_revision_compare(void) {
  lk_revision a;
  lk_revision b;
  lk_revision c;

  BEGIN_TEST("revision: equal and before helpers");

  a.hi = 0;
  a.lo = 1;
  b.hi = 0;
  b.lo = 2;
  c.hi = 1;
  c.lo = 0; /* lo wrapped, carried into hi */

  CHECK(lk_revision_equal(a, a));
  CHECK(!lk_revision_equal(a, b));

  CHECK(lk_revision_before(a, b));
  CHECK(!lk_revision_before(b, a));
  CHECK(!lk_revision_before(a, a));

  /* hi dominates: a post-wrap revision is after any pre-wrap one */
  CHECK(lk_revision_before(b, c));
  CHECK(!lk_revision_before(c, b));

  END_TEST();
}

/* ================================================================
 * New: transactions, subscribers, delta bytes
 * ================================================================ */

static void test_doc_txn_groups_one_notification(void) {
  lk_document *d = lk_doc_new(NULL, NULL, NULL);
  doc_rec rec;
  char buf[16];

  BEGIN_TEST("txn: bracket -> one notification, in order");

  memset(&rec, 0, sizeof(rec));
  lk_doc_subscribe(d, rec_listener, &rec);

  lk_doc_begin(d, 42); /* app-range origin flows to every delta */
  CHECK(lk_doc_insert(d, 0, "abc", 3));
  CHECK(lk_doc_insert(d, 3, "def", 3));
  CHECK(lk_doc_delete(d, 0, 1));
  CHECK_EQ(rec.calls, 0); /* nothing observable before commit */
  lk_doc_commit(d);

  CHECK_EQ(rec.calls, 1);
  CHECK_EQ(rec.n, 3);

  CHECK_EQ(rec.deltas[0].start, 0);
  CHECK_EQ(rec.deltas[0].inserted_len, 3);
  CHECK(strcmp(rec.ins[0], "abc") == 0);
  CHECK_EQ(rec.deltas[0].origin, 42);

  CHECK_EQ(rec.deltas[1].start, 3);
  CHECK_EQ(rec.deltas[1].inserted_len, 3);
  CHECK(strcmp(rec.ins[1], "def") == 0);

  CHECK_EQ(rec.deltas[2].start, 0);
  CHECK_EQ(rec.deltas[2].deleted_len, 1);
  CHECK(strcmp(rec.del[2], "a") == 0);
  CHECK_EQ(rec.deltas[2].origin, 42);

  doc_all(d, buf);
  CHECK(strcmp(buf, "bcdef") == 0);

  lk_doc_destroy(d);
  END_TEST();
}

static void test_doc_implicit_txn_notifies(void) {
  lk_document *d = lk_doc_new(NULL, NULL, NULL);
  doc_rec rec;

  BEGIN_TEST("txn: implicit single-op notifies at once");

  memset(&rec, 0, sizeof(rec));
  lk_doc_subscribe(d, rec_listener, &rec);

  CHECK(lk_doc_insert(d, 0, "hi", 2));
  CHECK_EQ(rec.calls, 1);
  CHECK_EQ(rec.n, 1);
  CHECK_EQ(rec.deltas[0].origin, LK_ORIGIN_NONE);

  CHECK(lk_doc_delete(d, 0, 1));
  CHECK_EQ(rec.calls, 2);
  CHECK_EQ(rec.n, 1);
  CHECK(strcmp(rec.del[0], "h") == 0);

  lk_doc_destroy(d);
  END_TEST();
}

static void test_doc_multi_subscriber_order(void) {
  lk_document *d = lk_doc_new(NULL, NULL, NULL);
  doc_rec a;
  doc_rec b;
  int seq = 0;

  BEGIN_TEST("subs: both notified, in subscribe order");

  memset(&a, 0, sizeof(a));
  memset(&b, 0, sizeof(b));
  a.seq = &seq;
  b.seq = &seq;

  CHECK(lk_doc_subscribe(d, rec_listener, &a) != 0);
  CHECK(lk_doc_subscribe(d, rec_listener, &b) != 0);

  CHECK(lk_doc_insert(d, 0, "x", 1));

  CHECK_EQ(a.calls, 1);
  CHECK_EQ(b.calls, 1);
  CHECK_EQ(a.order, 1);
  CHECK_EQ(b.order, 2);

  lk_doc_destroy(d);
  END_TEST();
}

static void test_doc_unsubscribe_stops(void) {
  lk_document *d = lk_doc_new(NULL, NULL, NULL);
  doc_rec a;
  doc_rec b;
  lk_u32 ida;

  BEGIN_TEST("subs: unsubscribed listener not notified");

  memset(&a, 0, sizeof(a));
  memset(&b, 0, sizeof(b));

  ida = lk_doc_subscribe(d, rec_listener, &a);
  lk_doc_subscribe(d, rec_listener, &b);

  lk_doc_unsubscribe(d, ida);

  CHECK(lk_doc_insert(d, 0, "x", 1));

  CHECK_EQ(a.calls, 0);
  CHECK_EQ(b.calls, 1);

  lk_doc_destroy(d);
  END_TEST();
}

static void test_doc_delta_bytes_across_pieces(void) {
  lk_document *d = lk_doc_from_str(NULL, NULL, NULL, "hello world", 11);
  doc_rec rec;
  char buf[16];

  BEGIN_TEST("deltas: exact bytes across piece splits");

  memset(&rec, 0, sizeof(rec));
  lk_doc_subscribe(d, rec_listener, &rec);

  /* split the original piece: [hello][XY][ world] */
  CHECK(lk_doc_insert(d, 5, "XY", 2));
  CHECK_EQ(rec.n, 1);
  CHECK_EQ(rec.deltas[0].inserted_len, 2);
  CHECK(strcmp(rec.ins[0], "XY") == 0);

  /* delete across all three piece boundaries */
  CHECK(lk_doc_delete(d, 4, 4));
  CHECK_EQ(rec.n, 1);
  CHECK_EQ(rec.deltas[0].start, 4);
  CHECK_EQ(rec.deltas[0].deleted_len, 4);
  CHECK(strcmp(rec.del[0], "oXY ") == 0);

  doc_all(d, buf);
  CHECK(strcmp(buf, "hellworld") == 0);

  lk_doc_destroy(d);
  END_TEST();
}

static void test_doc_revision_chain_in_txn(void) {
  lk_document *d = lk_doc_new(NULL, NULL, NULL);
  doc_rec rec;

  BEGIN_TEST("txn: revision chain across deltas");

  memset(&rec, 0, sizeof(rec));
  lk_doc_subscribe(d, rec_listener, &rec);

  lk_doc_begin(d, LK_ORIGIN_NONE);
  CHECK(lk_doc_insert(d, 0, "a", 1));
  CHECK(lk_doc_insert(d, 1, "b", 1));
  CHECK(lk_doc_insert(d, 2, "c", 1));
  lk_doc_commit(d);

  CHECK_EQ(rec.n, 3);

  /* after of delta N == before of delta N+1 */
  CHECK(lk_revision_equal(rec.deltas[0].after, rec.deltas[1].before));
  CHECK(lk_revision_equal(rec.deltas[1].after, rec.deltas[2].before));

  /* the document now sits at the last delta's after */
  CHECK(lk_revision_equal(rec.deltas[2].after, lk_doc_revision(d)));

  lk_doc_destroy(d);
  END_TEST();
}

/* ================================================================
 * New: edit history
 * ================================================================ */

static void test_hist_empty_returns_zero(void) {
  lk_document *d = lk_doc_new(NULL, NULL, NULL);
  lk_edit_history *h = lk_history_new(NULL, NULL, NULL);

  BEGIN_TEST("hist: undo/redo on empty stacks");

  lk_history_attach(h, d);

  CHECK(!lk_history_can_undo(h));
  CHECK(!lk_history_can_redo(h));
  CHECK_EQ(lk_history_undo(h, d), 0);
  CHECK_EQ(lk_history_redo(h, d), 0);

  lk_history_destroy(h);
  lk_doc_destroy(d);
  END_TEST();
}

static void test_hist_undo_redo_basic(void) {
  lk_document *d = lk_doc_new(NULL, NULL, NULL);
  lk_edit_history *h = lk_history_new(NULL, NULL, NULL);
  char buf[16];

  BEGIN_TEST("hist: undo restores, redo re-applies");

  lk_history_attach(h, d);

  CHECK(lk_doc_insert(d, 0, "hello", 5));
  CHECK(lk_history_can_undo(h));
  CHECK(!lk_history_can_redo(h));

  CHECK_EQ(lk_history_undo(h, d), 1);
  CHECK_EQ(lk_doc_len(d), 0);
  CHECK(!lk_history_can_undo(h));
  CHECK(lk_history_can_redo(h));

  CHECK_EQ(lk_history_redo(h, d), 1);
  doc_all(d, buf);
  CHECK(strcmp(buf, "hello") == 0);
  CHECK(lk_history_can_undo(h));
  CHECK(!lk_history_can_redo(h));

  lk_history_destroy(h);
  lk_doc_destroy(d);
  END_TEST();
}

static void test_hist_undo_delete_restores(void) {
  lk_document *d = lk_doc_from_str(NULL, NULL, NULL, "hello world", 11);
  lk_edit_history *h = lk_history_new(NULL, NULL, NULL);
  char buf[16];

  BEGIN_TEST("hist: undo of delete restores bytes");

  lk_history_attach(h, d);

  CHECK(lk_doc_delete(d, 0, 6));
  doc_all(d, buf);
  CHECK(strcmp(buf, "world") == 0);

  CHECK_EQ(lk_history_undo(h, d), 1);
  doc_all(d, buf);
  CHECK(strcmp(buf, "hello world") == 0);

  CHECK_EQ(lk_history_redo(h, d), 1);
  doc_all(d, buf);
  CHECK(strcmp(buf, "world") == 0);

  lk_history_destroy(h);
  lk_doc_destroy(d);
  END_TEST();
}

static void test_hist_txn_undone_as_one_step(void) {
  lk_document *d = lk_doc_new(NULL, NULL, NULL);
  lk_edit_history *h = lk_history_new(NULL, NULL, NULL);
  char buf[16];

  BEGIN_TEST("hist: 3-op transaction = one undo step");

  lk_history_attach(h, d);

  lk_doc_begin(d, LK_ORIGIN_NONE);
  CHECK(lk_doc_insert(d, 0, "aaa", 3));
  CHECK(lk_doc_insert(d, 3, "bbb", 3));
  CHECK(lk_doc_insert(d, 6, "ccc", 3));
  lk_doc_commit(d);

  doc_all(d, buf);
  CHECK(strcmp(buf, "aaabbbccc") == 0);

  CHECK_EQ(lk_history_undo(h, d), 1);
  CHECK_EQ(lk_doc_len(d), 0);
  CHECK(!lk_history_can_undo(h)); /* all three went in one step */

  CHECK_EQ(lk_history_redo(h, d), 1);
  doc_all(d, buf);
  CHECK(strcmp(buf, "aaabbbccc") == 0);

  lk_history_destroy(h);
  lk_doc_destroy(d);
  END_TEST();
}

static void test_hist_foreign_edit_clears_redo(void) {
  lk_document *d = lk_doc_new(NULL, NULL, NULL);
  lk_edit_history *h = lk_history_new(NULL, NULL, NULL);
  char buf[8];

  BEGIN_TEST("hist: foreign edit clears redo stack");

  lk_history_attach(h, d);

  CHECK(lk_doc_insert(d, 0, "a", 1));
  CHECK(lk_doc_insert(d, 1, "b", 1));

  CHECK_EQ(lk_history_undo(h, d), 1);
  doc_all(d, buf);
  CHECK(strcmp(buf, "a") == 0);
  CHECK(lk_history_can_redo(h));

  CHECK(lk_doc_insert(d, 1, "c", 1)); /* foreign edit */
  CHECK(!lk_history_can_redo(h));
  CHECK_EQ(lk_history_redo(h, d), 0);

  doc_all(d, buf);
  CHECK(strcmp(buf, "ac") == 0);

  lk_history_destroy(h);
  lk_doc_destroy(d);
  END_TEST();
}

static void test_hist_replay_origins_observed(void) {
  lk_document *d = lk_doc_new(NULL, NULL, NULL);
  lk_edit_history *h = lk_history_new(NULL, NULL, NULL);
  doc_rec rec;

  BEGIN_TEST("hist: replays notify others w/ origins");

  lk_history_attach(h, d);
  memset(&rec, 0, sizeof(rec));
  lk_doc_subscribe(d, rec_listener, &rec);

  CHECK(lk_doc_insert(d, 0, "abc", 3));
  CHECK_EQ(rec.calls, 1);
  CHECK_EQ(rec.deltas[0].origin, LK_ORIGIN_NONE);

  CHECK_EQ(lk_history_undo(h, d), 1);
  CHECK_EQ(rec.calls, 2);
  CHECK_EQ(rec.n, 1);
  CHECK_EQ(rec.deltas[0].origin, LK_ORIGIN_UNDO);
  CHECK_EQ(rec.deltas[0].deleted_len, 3);
  CHECK(strcmp(rec.del[0], "abc") == 0);

  CHECK_EQ(lk_history_redo(h, d), 1);
  CHECK_EQ(rec.calls, 3);
  CHECK_EQ(rec.n, 1);
  CHECK_EQ(rec.deltas[0].origin, LK_ORIGIN_REDO);
  CHECK_EQ(rec.deltas[0].inserted_len, 3);
  CHECK(strcmp(rec.ins[0], "abc") == 0);

  lk_history_destroy(h);
  lk_doc_destroy(d);
  END_TEST();
}

static void test_hist_deep_chain(void) {
  lk_document *d = lk_doc_new(NULL, NULL, NULL);
  lk_edit_history *h = lk_history_new(NULL, NULL, NULL);
  char expect[32];
  char buf[32];
  lk_u32 i;

  BEGIN_TEST("hist: 20 edits, undo all, redo all");

  lk_history_attach(h, d);

  for (i = 0; i < 20; i++) {
    char ch = (char)('a' + (i % 26));

    expect[i] = ch;
    CHECK(lk_doc_insert(d, i, &ch, 1));
  }

  expect[20] = '\0';
  doc_all(d, buf);
  CHECK(strcmp(buf, expect) == 0);

  for (i = 0; i < 20; i++) {
    CHECK_EQ(lk_history_undo(h, d), 1);
  }

  CHECK_EQ(lk_doc_len(d), 0);
  CHECK(!lk_history_can_undo(h));
  CHECK(lk_history_can_redo(h));

  for (i = 0; i < 20; i++) {
    CHECK_EQ(lk_history_redo(h, d), 1);
  }

  doc_all(d, buf);
  CHECK(strcmp(buf, expect) == 0);
  CHECK(!lk_history_can_redo(h));
  CHECK(lk_history_can_undo(h));

  lk_history_destroy(h);
  lk_doc_destroy(d);
  END_TEST();
}

/* ================================================================
 * Delta coordinate contract (SEQUENTIAL, docs/editor-wrap.md
 * section 7): each delta's positions are expressed in the document
 * state produced by all preceding deltas of the same transaction.
 * The decisive check is a replay: applying the recorded deltas one
 * by one to a copy of the pre-transaction bytes must reproduce the
 * post-transaction document exactly.
 * ================================================================ */

/* Replay rec's deltas sequentially over `before`; 1 when the result
 * (and every delta's captured deleted bytes) matches the document. */
static int rec_replays_to(const doc_rec *rec, const char *before,
                          const lk_document *d) {
  char buf[128];
  char cur[128];
  lk_u32 len = (lk_u32)strlen(before);
  lk_u32 i;

  if (len >= sizeof(buf)) {
    return 0;
  }

  memcpy(buf, before, len);

  for (i = 0; i < rec->n; i++) {
    lk_u32 p = rec->deltas[i].start;
    lk_u32 dl = rec->deltas[i].deleted_len;
    lk_u32 il = rec->deltas[i].inserted_len;

    if (p > len || dl > len - p || len - dl + il >= sizeof(buf)) {
      return 0; /* coordinates invalid in the sequential state */
    }

    /* the captured deleted bytes must match this state at p */
    if (dl && memcmp(buf + p, rec->del[i], dl) != 0) {
      return 0;
    }

    memmove(buf + p + il, buf + p + dl, len - p - dl);

    if (il) {
      memcpy(buf + p, rec->ins[i], il);
    }

    len = len - dl + il;
  }

  buf[len] = '\0';

  if (len != lk_doc_len(d) || len >= sizeof(cur)) {
    return 0;
  }

  doc_all(d, cur);

  return strcmp(buf, cur) == 0;
}

static void test_delta_seq_newline_insert(void) {
  lk_document *d = lk_doc_from_str(NULL, NULL, NULL, "ab", 2);
  doc_rec rec;

  BEGIN_TEST("delta seq: one-newline insert");

  memset(&rec, 0, sizeof(rec));
  lk_doc_subscribe(d, rec_listener, &rec);

  CHECK(lk_doc_insert(d, 1, "x\ny", 3) == 1);

  CHECK_EQ(rec.n, 1);
  CHECK_EQ(rec.deltas[0].start, 1);
  CHECK_EQ(rec.deltas[0].inserted_len, 3);
  CHECK_EQ(rec.deltas[0].deleted_len, 0);
  CHECK(strcmp(rec.ins[0], "x\ny") == 0);
  CHECK_EQ(lk_doc_line_count(d), 2);
  CHECK(rec_replays_to(&rec, "ab", d));

  lk_doc_destroy(d);
  END_TEST();
}

static void test_delta_seq_newline_delete(void) {
  lk_document *d = lk_doc_from_str(NULL, NULL, NULL, "ab\ncd", 5);
  doc_rec rec;

  BEGIN_TEST("delta seq: one-newline delete");

  memset(&rec, 0, sizeof(rec));
  lk_doc_subscribe(d, rec_listener, &rec);

  CHECK(lk_doc_delete(d, 2, 1) == 1);

  CHECK_EQ(rec.n, 1);
  CHECK_EQ(rec.deltas[0].start, 2);
  CHECK_EQ(rec.deltas[0].deleted_len, 1);
  CHECK(strcmp(rec.del[0], "\n") == 0);
  CHECK_EQ(lk_doc_line_count(d), 1);
  CHECK(rec_replays_to(&rec, "ab\ncd", d));

  lk_doc_destroy(d);
  END_TEST();
}

static void test_delta_seq_lines_replaced(void) {
  lk_document *d =
      lk_doc_from_str(NULL, NULL, NULL, "aaa\nbbb\nccc\nddd", 15);
  doc_rec rec;
  char buf[32];

  BEGIN_TEST("delta seq: N lines replaced with M");

  memset(&rec, 0, sizeof(rec));
  lk_doc_subscribe(d, rec_listener, &rec);

  /* replace lines 1-2 with three new lines, one transaction */
  lk_doc_begin(d, 42);
  CHECK(lk_doc_delete(d, 4, 8) == 1); /* "bbb\nccc\n" */
  CHECK(lk_doc_insert(d, 4, "X\nY\nZ\n", 6) == 1);
  lk_doc_commit(d);

  CHECK_EQ(rec.calls, 1);
  CHECK_EQ(rec.n, 2);
  CHECK_EQ(rec.deltas[0].start, 4);
  CHECK_EQ(rec.deltas[0].deleted_len, 8);
  CHECK(strcmp(rec.del[0], "bbb\nccc\n") == 0);

  /* the insert's start is a position in the POST-delete state */
  CHECK_EQ(rec.deltas[1].start, 4);
  CHECK_EQ(rec.deltas[1].inserted_len, 6);

  doc_all(d, buf);
  CHECK(strcmp(buf, "aaa\nX\nY\nZ\nddd") == 0);
  CHECK_EQ(lk_doc_line_count(d), 5);
  CHECK(rec_replays_to(&rec, "aaa\nbbb\nccc\nddd", d));

  lk_doc_destroy(d);
  END_TEST();
}

static void test_delta_seq_multiple_deltas(void) {
  lk_document *d = lk_doc_from_str(NULL, NULL, NULL, "xy", 2);
  doc_rec rec;
  char buf[16];

  BEGIN_TEST("delta seq: multiple primitives, shifted coords");

  memset(&rec, 0, sizeof(rec));
  lk_doc_subscribe(d, rec_listener, &rec);

  lk_doc_begin(d, 42);
  CHECK(lk_doc_insert(d, 0, "A", 1) == 1); /* "Axy" */
  CHECK(lk_doc_insert(d, 2, "B", 1) == 1); /* "AxBy" */
  lk_doc_commit(d);

  CHECK_EQ(rec.n, 2);
  CHECK_EQ(rec.deltas[0].start, 0);

  /* start 2 is "between x and y" only in the state produced by the
   * first delta -- the sequential rule, not original-document
   * coordinates (which would say 1) */
  CHECK_EQ(rec.deltas[1].start, 2);

  doc_all(d, buf);
  CHECK(strcmp(buf, "AxBy") == 0);
  CHECK(rec_replays_to(&rec, "xy", d));

  lk_doc_destroy(d);
  END_TEST();
}

static void test_delta_seq_delete_then_insert(void) {
  lk_document *d = lk_doc_from_str(NULL, NULL, NULL, "hello", 5);
  doc_rec rec;
  char buf[16];

  BEGIN_TEST("delta seq: delete-then-insert at one position");

  memset(&rec, 0, sizeof(rec));
  lk_doc_subscribe(d, rec_listener, &rec);

  lk_doc_begin(d, 42);
  CHECK(lk_doc_delete(d, 1, 3) == 1);
  CHECK(lk_doc_insert(d, 1, "XY", 2) == 1);
  lk_doc_commit(d);

  CHECK_EQ(rec.n, 2);
  CHECK_EQ(rec.deltas[0].start, 1);
  CHECK_EQ(rec.deltas[0].deleted_len, 3);
  CHECK_EQ(rec.deltas[1].start, 1);
  CHECK_EQ(rec.deltas[1].inserted_len, 2);

  doc_all(d, buf);
  CHECK(strcmp(buf, "hXYo") == 0);
  CHECK(rec_replays_to(&rec, "hello", d));

  lk_doc_destroy(d);
  END_TEST();
}

static void test_delta_seq_newline_edges(void) {
  lk_document *d = lk_doc_from_str(NULL, NULL, NULL, "ab\ncd\nef", 8);
  doc_rec rec;
  char buf[16];

  BEGIN_TEST("delta seq: change begins/ends exactly on \\n");

  memset(&rec, 0, sizeof(rec));
  lk_doc_subscribe(d, rec_listener, &rec);

  /* delete "\ncd\n" -- begins on line 0's \n, ends just past line
   * 1's \n -- then insert a lone "\n" at the join */
  lk_doc_begin(d, 42);
  CHECK(lk_doc_delete(d, 2, 4) == 1); /* "abef" */
  CHECK(lk_doc_insert(d, 2, "\n", 1) == 1);
  lk_doc_commit(d);

  CHECK_EQ(rec.n, 2);
  CHECK_EQ(rec.deltas[0].start, 2);
  CHECK(strcmp(rec.del[0], "\ncd\n") == 0);
  CHECK_EQ(rec.deltas[1].start, 2);
  CHECK(strcmp(rec.ins[1], "\n") == 0);

  doc_all(d, buf);
  CHECK(strcmp(buf, "ab\nef") == 0);
  CHECK_EQ(lk_doc_line_count(d), 2);
  CHECK(rec_replays_to(&rec, "ab\ncd\nef", d));

  lk_doc_destroy(d);
  END_TEST();
}

/* ================================================================
 * Runner
 * ================================================================ */

void lk_document_run_tests(void) {
  printf("\nlk document tests (ported from weft):\n");
  test_doc_new_empty();
  test_doc_from_str();
  test_doc_insert_at_start();
  test_doc_insert_at_end();
  test_doc_insert_in_middle();
  test_doc_insert_empty_doc();
  test_doc_delete_from_start();
  test_doc_delete_from_end();
  test_doc_delete_from_middle();
  test_doc_delete_all();
  test_doc_line_count_single();
  test_doc_line_count_multiple();
  test_doc_line_start_end();
  test_doc_pos_to_line();
  test_doc_line_index_after_insert();
  test_doc_line_index_after_delete();
  test_doc_get_byte();
  test_doc_multiple_inserts();
  test_doc_insert_delete_insert();
  test_doc_insert_past_end_fails();
  test_doc_delete_past_end_clamps();
  test_doc_get_text_partial();

  printf("\nlk document contract tests:\n");
  test_doc_empty_is_one_line();
  test_doc_trailing_newline_lines();
  test_doc_get_text_no_terminator();
  test_doc_zero_len_ops_rejected();
  test_doc_bounds_and_overflow();
  test_doc_bytes_verbatim();
  test_doc_listener_no_reentrancy();
  test_doc_failed_ops_keep_revision();
  test_doc_delta_revision_pairs();
  test_revision_compare();

  printf("\nlk document change-protocol tests:\n");
  test_doc_txn_groups_one_notification();
  test_doc_implicit_txn_notifies();
  test_doc_multi_subscriber_order();
  test_doc_unsubscribe_stops();
  test_doc_delta_bytes_across_pieces();
  test_doc_revision_chain_in_txn();

  printf("\nlk document delta-contract tests (sequential):\n");
  test_delta_seq_newline_insert();
  test_delta_seq_newline_delete();
  test_delta_seq_lines_replaced();
  test_delta_seq_multiple_deltas();
  test_delta_seq_delete_then_insert();
  test_delta_seq_newline_edges();

  printf("\nlk edit history tests:\n");
  test_hist_empty_returns_zero();
  test_hist_undo_redo_basic();
  test_hist_undo_delete_restores();
  test_hist_txn_undone_as_one_step();
  test_hist_foreign_edit_clears_redo();
  test_hist_replay_origins_observed();
  test_hist_deep_chain();
}
