# Editor Track — Weft's Document Core on lk (Design, v2)

Step 4 of the MVP sequencing: lift weft's proven document machinery
into lk and build the multi-line editor widget on top of it. This is
the design's endgame, not a pivot — `docs/design_draft.md` reserved a
`surface` kind ("Weft document surface") from the beginning.

Revision history: v1 draft 2026-07-26; v2 same day after review.
The v2 changes are architectural, not cosmetic: typed resource
references instead of a raw pointer tag, undo split into a shared
edit history, a real change protocol in stage A, a command layer
under the event hook, a self-contained render list, and an anchored
viewport. The review's framing is adopted wholesale and worth
stating up front, because every API below follows from it:

> The application environment contains persistent, typed,
> inspectable objects. The lk tree temporarily presents views of
> those objects. Input is translated into named commands, commands
> create document transactions, and transactions publish changes
> from which views and presentations update.

```
Lcl application environment
│
├── lk_document          text state and change notifications
├── lk_edit_history      transactions and undo/redo
├── lk_editor            one view: cursor, selection, viewport
├── annotation sources   semantic ranges (annot store, stage C)
└── presentation objects (future weft surface — §12)
          │  stable typed references (lk_resource_ref)
          ▼
     ephemeral lk tree  ──►  layout snapshot  ──►  self-contained
                                                   render list
```

The tree is a projection of the application world, not the owner of
it. Objects above the line are **application-owned** (out-of-tree):
their lifetime domain is the application environment — the C host in
the demo, the Lcl object environment in a script — never `lk_state`,
never the intern pool. Reparenting an editor node across splits costs
nothing; every bit of view state survives because none of it lives in
the tree.

Weft source of truth: `~/dev/weft/editor/src/` (also vendored at
`submodules/weft`).

| weft file | lines | disposition |
|---|---|---|
| `document.c/h` | 667 + 74 | **lift** → `src/editor/lk-document.c` |
| `annot_store.c/h` | 848 + 145 | **lift** → `src/editor/lk-annot-store.c` (stage C) |
| `utf8.c/h` | 220 + 61 | already lifted (`lk-utf8.c`, text contract stage C) |
| `tests/test_document.c` | 401 | **port** into `lk_test` |
| `tests/test_annot_store.c` | 505 | **port** into `lk_test` (stage C) |
| `editor.c/h` | 2405 + 172 | **salvage reference** — motion/edit/undo logic ports piecemeal; layout/blink/wrap/column-mode stays behind |
| `buffer.c`, `pane.c`, `layout.c`, `renderer.c` | ~2900 | **superseded** — lk owns splits, panes-as-tree, rendering, text |
| `annot_persist.c`, tree-sitter, `lcl_host.c` | ~10600 | **deferred** — later editor-track stages |

The lift is mechanical but real: weft is C11; lk core is C89 with
threaded allocators. Same treatment `utf8.c` already got.


## 1. Scope

**v1 delivers**: a `UIK_EDITOR` widget — multi-line, editable,
scrollable, virtualized — over an application-owned document, with
cursor, linear selection, clipboard, transactional undo/redo,
click/drag positioning, and (stage C) styled-run rendering fed by
the annotation store. Plus Lcl bindings and a runnable example.

**v1 pulls forward** (substrate, per review — none of it visible,
all of it load-bearing): typed resource references, document
transactions and change sets, multi-subscriber change notification,
a command layer beneath event handling, an anchored viewport, and
self-contained render-list text storage.

**v1 defers** (§13): wrapping, column/block selection, cursor blink,
IME composition display, tree-sitter, annotation persistence,
search/goto UI (app-level composition of existing widgets — weft's
`pane.c` dialogs do not port), full multi-view cursor sync (its
substrate — the change protocol — does ship).


## 2. Module and header layout

New directory `src/editor/`, new public headers beside `lk.h`:

```
include/lk-document.h     document, revision, deltas, history
include/lk-editor.h       view, commands, spans (includes lk-document.h)
include/lk-annot-store.h  stage C
src/editor/lk-document.c
src/editor/lk-edit-history.c
src/editor/lk-annot-store.c   (stage C)
src/editor/lk-editor.c        view state + command implementations
src/editor/lk-editor-widget.c UIK_EDITOR vtable
```

`lk.h` stays the UI-tree contract and gains only the pieces the tree
itself must know: `UIV_RESOURCE`, `UIP_EDITOR`, `UIK_EDITOR`,
`LK_ROP_DRAW_RUN`, and the resource-table API. Everything document-
shaped lives in the new headers (precedent: `lk-sdl.h`, `lcl-lk.h`).

Sources compile into `lk_core` (still one static library), but under
a **first-party-module discipline**: `src/editor/` consumes public lk
API plus the shared internal allocator header only; any further
private-header dependency must be called out in this doc. The editor
is wired in as a built-in kind for v1 (enum entry, registered in
`lk_widget_init`, theme defaults, DSL export), but it is deliberately
structured so it can later become the first widget to dogfood a
richer registration protocol — one that can register theme defaults,
DSL schema, and resource-prop types for out-of-core widgets. That
protocol upgrade is explicitly out of scope here; the editor just
must not make it harder.


## 3. The document (`lk-document.c`, stage A)

Straight port of weft's piece table: immutable original buffer,
append-only add buffer, piece array, line index, cached total
length. The weft test suite ports with it as the correctness gate.
`size_t` → `lk_u32` (4 GB cap, consistent with the rest of lk);
allocator triple threaded through; no `malloc`/`free`.

What changes is everything *around* the mutation primitives.

### 3.1 Revision as a token

```c
typedef struct lk_revision { lk_u32 hi, lo; } lk_revision;

int lk_revision_equal(lk_revision a, lk_revision b);
int lk_revision_before(lk_revision a, lk_revision b);
```

A revision is an identity token, not arithmetic — so it gets an
opaque comparable type rather than a general-purpose `lk_u64` that
C89 cannot honestly spell. Incremented internally as a pair. If lk
ever needs true 64-bit arithmetic (timestamps, file sizes), that is
a separate, compiler-checked decision.

### 3.2 Transactions and deltas — the change protocol

The revision counter answers "is this different?"; it cannot answer
"what changed and how should anchored state move?" — which the annot
store, other views, cursors, persistence, tree-sitter, and search
results all need. So mutation is transactional and observable from
day one, even though multi-view sync itself is deferred:

```c
typedef struct lk_doc_delta {
  lk_u32 start;
  lk_u32 deleted_len;
  lk_u32 inserted_len;
  const char *deleted;      /* the removed bytes  — valid only during */
  const char *inserted;     /* the added bytes    — the notification  */
  lk_revision before, after;
  lk_u32 origin;            /* committer-supplied, e.g. editor command id */
} lk_doc_delta;

typedef void (*lk_doc_listener_fn)(void *ud, const lk_document *d,
                                   const lk_doc_delta *deltas, lk_u32 n);

lk_u32 lk_doc_subscribe(lk_document *d, lk_doc_listener_fn fn, void *ud);
void   lk_doc_unsubscribe(lk_document *d, lk_u32 subscription);

void lk_doc_begin(lk_document *d, lk_u32 origin);
int  lk_doc_insert(lk_document *d, lk_u32 pos, const char *text, lk_u32 len);
int  lk_doc_delete(lk_document *d, lk_u32 pos, lk_u32 len);
void lk_doc_commit(lk_document *d);
```

- `insert`/`delete` outside a `begin`/`commit` bracket form an
  implicit single-op transaction (the common typing path stays one
  call).
- Listeners are notified **once per committed transaction** with the
  full delta array — observers never see a half-finished compound
  edit, and the transaction is the natural undo step.
- Deltas carry the **actual bytes**, not just lengths: `inserted`
  points into the add buffer, `deleted` into transaction-scoped
  scratch copied before the delete applies. Both are valid only for
  the duration of the notification — listeners that need them
  longer copy. This is what lets the edit history capture inverse
  operations (and, later, tree-sitter see old text) as an ordinary
  subscriber instead of a privileged hook.
- A listener *list*, not a slot: the annot store is one subscriber
  among peers (views, status displays, evaluators), not a
  privileged one.
- The revision remains the cheap invalidation token it always was;
  it just stops being the whole story.

Read API is the weft surface, C89-ized: `lk_doc_len`,
`lk_doc_line_count`, `lk_doc_get_text`, `lk_doc_get_byte`,
`lk_doc_line_start/end/length`, `lk_doc_pos_to_line`,
`lk_doc_revision`.

### 3.3 Pinned contracts

Behavioral contract, stated here so the ported tests can assert it.
"(weft)" = verified current weft behavior, preserved; "(new)" =
defined now because the lift makes it contractual.

1. A document always has ≥ 1 line; the empty document is one empty
   line. (weft)
2. `line_end(i)` is the offset of line *i*'s `\n`, or `doc_len` for
   the last line — exclusive of the newline. `line_length` includes
   the `\n` when present. A trailing `\n` therefore yields a final
   empty line. (weft)
3. `get_text` writes no terminator and returns bytes written;
   `pos >= len` reads 0 bytes. (weft)
4. Zero-length insert/delete returns 0 (rejected), advances nothing,
   notifies nobody. (weft)
5. `insert` with `pos > len` fails; `delete` with `pos >= len`
   fails; `delete` clamps `pos + len` to the document end; any
   `pos + len` that would overflow `lk_u32` fails. (weft + new)
6. Bytes are stored verbatim: invalid UTF-8 is preserved, never
   rejected or repaired (files are dirty; cursor motion is defensive
   via the `lk_utf8` boundary helpers). CRLF is not normalized —
   `\n` is the only line terminator the document knows; a `\r` is
   just a byte on the line. Apps normalize on load if they care.
   (new; weft is implicitly the same)
7. Listeners must not mutate the document from inside a
   notification (debug-asserted via an in-notify flag). (new)
8. Failed mutations do not advance the revision; every committed
   transaction with ≥ 1 effective delta advances it exactly once
   per delta (deltas carry before/after pairs). (new)


## 4. Edit history (`lk-edit-history.c`, stage A)

Undo leaves the view (weft kept an `EditOp` stack per `Editor` —
two views over one buffer had independent, mutually-corrupting
stacks) but does **not** collapse into the document. The current
text is a fact about the document; the path by which it got there is
provenance, and formatters, file reloads, scripted mutations,
multi-op refactorings, and macros all need provenance policy the
document should not hard-code:

```c
typedef struct lk_edit_history lk_edit_history;

lk_edit_history *lk_history_new(/* allocators */);
void lk_history_destroy(lk_edit_history *h);

/* Attach: subscribes to the document and records every committed
 * transaction (captured with the deleted text needed for inverse
 * replay).  One history per document is the v1 configuration;
 * the relationship is explicit, not inherent. */
void lk_history_attach(lk_edit_history *h, lk_document *d);

int lk_history_undo(lk_edit_history *h, lk_document *d);
int lk_history_redo(lk_edit_history *h, lk_document *d);
int lk_history_can_undo(const lk_edit_history *h);
int lk_history_can_redo(const lk_edit_history *h);
```

- Undo/redo replays the inverse **as a transaction** with origin
  `LK_ORIGIN_UNDO`/`LK_ORIGIN_REDO` — so it flows to every observer
  through the same protocol as any edit. There is no separate
  "undo happened" channel and no returned cursor position: the
  invoking view derives its new cursor from the deltas it receives
  as a subscriber (v1 rule: cursor to the end of the last inserted
  range, start of the last deleted one). This replaces v1-draft's
  too-narrow `lk_doc_undo(d, &pos)`.
- History does not record its own replays (origin-filtered), and
  clears the redo stack on a foreign committed transaction, the
  standard bargain.
- Grouping falls out of transactions: one `begin`/`commit` bracket =
  one undo step. Weft's keystroke-coalescing behavior (verify
  against `editor.c` during the lift) is reproduced *in the editor
  command layer* by bracketing, not inside history.
- Deferred but priced in: named origins for policy ("don't record
  reloads"), macro grouping, per-session histories over a shared
  document.


## 5. Resource references (`UIV_RESOURCE`, stage B)

The tree needs to reference application-owned objects. v1-draft
proposed a raw `UIV_PTR`; review rejected it on inspectability
grounds — no runtime type, nothing dumpable, unchecked
use-after-free, every future widget inventing its own pointer
conventions — while confirming the *property* mechanism over a
node-id registry (which would be a second identity system with
unbind-lifetime questions). So: a typed, generation-checked
resource reference.

```c
typedef struct lk_resource_type {
  const char *name;                       /* "editor", "document", ... */
  /* optional hooks, all NULL-able in v1: */
  void (*describe)(void *obj, lk_write_fn w, void *wud);
} lk_resource_type;

typedef struct lk_resource_ref {
  lk_u32 id;          /* stable logical identity, 0 = null ref */
  lk_u32 generation;  /* stale-handle detection */
} lk_resource_ref;

lk_resource_ref lk_resource_register(lk_resources *rs,
                                     const lk_resource_type *type,
                                     void *obj, const char *debug_name);
void lk_resource_release(lk_resources *rs, lk_resource_ref ref);
void *lk_resource_get(const lk_resources *rs, lk_resource_ref ref,
                      const lk_resource_type *type);  /* NULL if stale
                                                         or wrong type */
```

- The table (`lk_resources`) is owned by `lk_ui` and borrowed by
  both trees via a pointer, exactly like the shared intern table —
  which makes refs resolvable from every widget vtable hook through
  `t` alone, the same reachability property that motivated the prop
  approach.
- `lk_value` gains `UIV_RESOURCE` holding the 8-byte
  `{id, generation}` — no pointer in the value; resolution and type
  checking go through the table. A stale or wrong-typed ref
  resolves to NULL, and the widget degrades exactly like a missing
  prop (§7). Use-after-free becomes unrepresentable at this
  boundary.
- Registration happens **once** per object (id stability across
  frames); the per-frame cost is one prop carrying 8 bytes.
  Diffing compares id+generation.
- `lk_tree_dump` prints `editor="src-view"#17` (type name, debug
  name, id) — deterministic, distinguishable, address-free.
- The Lcl boundary is where this pays hardest: the tree routinely
  outlives the lexical expression that built it. The Lcl wrapper
  for an editor owns the registration and releases it in its
  finalizer — an explicit, visible lifetime guarantee where a raw
  pointer prop had an implicit prayer.
- Convenience layer so call sites stay typed:
  `lk_v_editor_ref(ref)`, and in the widget
  `lk_editor_from_node(rs, t, n)`.
- Deliberately *not* in v1: retain/release refcounting hooks — the
  table does not own objects; release only invalidates. The type
  struct is where ownership hooks land later if needed.


## 6. The editor view and its commands (`lk-editor.c`)

`lk_editor` is one view over a document: cursor byte offset
(codepoint-aligned), selection anchor, sticky **x-pixel** for
vertical motion (weft's `target_column` generalized through the text
contract — proportional-correct for free, identical under the
monospace stub), anchored viewport (§9), drag state, tab settings, a
growable frame scratch for line extraction, and a transient
per-frame geometry block (§8).

```c
lk_editor *lk_editor_new(/* allocators, */ lk_document *doc,
                         lk_edit_history *hist /* NULL ok */);
void lk_editor_destroy(lk_editor *e);   /* releases nothing it
                                           doesn't own: doc and
                                           history are referenced */
```

The editor subscribes to its document. v1 uses the subscription for
self-consistency (dropping stale geometry); *foreign*-edit cursor
adjustment is deferred, but arrives later as a better subscriber
callback, not a redesign.

### 6.1 Command layer

Editing semantics do not live in the widget event hook. The pipeline
is:

```
keyboard/pointer event → binding resolution → editor command
                       → document transaction → change notification
```

```c
typedef enum lk_editor_cmd_id {
  LK_ED_INSERT_TEXT,        /* arg: bytes */
  LK_ED_DELETE_BACKWARD, LK_ED_DELETE_FORWARD,
  LK_ED_DELETE_WORD_BACKWARD, LK_ED_DELETE_WORD_FORWARD,
  LK_ED_MOVE_LEFT, LK_ED_MOVE_RIGHT, LK_ED_MOVE_UP, LK_ED_MOVE_DOWN,
  LK_ED_MOVE_WORD_LEFT, LK_ED_MOVE_WORD_RIGHT,
  LK_ED_MOVE_LINE_START, LK_ED_MOVE_LINE_END,
  LK_ED_MOVE_DOC_START, LK_ED_MOVE_DOC_END,
  LK_ED_MOVE_PAGE_UP, LK_ED_MOVE_PAGE_DOWN,
  LK_ED_SELECT_ALL,
  LK_ED_CUT, LK_ED_COPY, LK_ED_PASTE,
  LK_ED_UNDO, LK_ED_REDO,
  LK_ED_SET_CURSOR,         /* arg: pos (+ extend flag) */
  LK_ED_SCROLL_LINES        /* arg: signed line count */
} lk_editor_cmd_id;

int lk_editor_command(lk_editor *e, lk_ui *ui,
                      lk_editor_cmd_id cmd, const lk_editor_cmd_arg *arg);
```

Motion commands take a `select` modifier (SHIFT extends). `ui`
supplies the clipboard vtable and the text backend for geometry
(word motion, vertical motion, page size); NULL `ui` degrades
(clipboard commands no-op, vertical motion uses the last-known line
height) so pure-C tests can drive most commands headless — though
tests normally pass a real `lk_ui` with the stub backend.

Why this instead of a switch inside the event hook, in order of
payoff: Lcl invokes the same commands the keyboard does
(`lk::editor_command`, stage D); commands are the transaction
boundary (each editing command brackets `begin`/`commit`; keystroke
coalescing is a bracketing policy here, not history magic); commands
are loggable and inspectable; keybindings can become data later
without semantic surgery; and it is the most directly CLIM-shaped
move available — application operations as named commands, not
callback bodies. Hard-coded bindings are fine for v1; they resolve
to command ids. (Convergence with lk's tree-level
translator/command system — editor commands as `lk_command`s with
presentation context — is a future step the enum does not block.)


## 7. The widget (`UIK_EDITOR`, stage B)

- **Attachment**: `UIP_EDITOR` prop carrying a `UIV_RESOURCE` ref,
  set by the app every frame like any prop. No ref, stale ref, or
  wrong-typed ref → renders background only, ignores events (the
  split-pane degradation convention).
- **measure**: intrinsic 0×0 plus W/H props — greedy, sized by its
  container (split pane, stretched column), like weft's panes.
- **layout**: leaf with a non-NULL hook (engine calls `layout`
  whenever non-NULL, children or not — verified in `lk-layout.c`).
  The hook has both the final rect and `cfg->text`, so all
  backend-dependent geometry happens here: resolve the anchored
  viewport to a pixel window, clamp scroll, extract visible lines
  into editor scratch, compute cursor x/y and selection rects via
  `x_from_index`, stash in the editor's transient block. `render`
  then reads pure geometry — no backend access, and **no derived
  geometry in `lk_state`**: the editor adds zero entries to the
  coherence-debt list (`LKS_CURSOR_X` et al.), because the stash
  target is the application-owned struct.
- **render**: background; per-line runs into the render list's byte
  arena (§8); selection FILL_RECTs (head partial line, body block,
  tail partial line); cursor bar when focused; `clips = 1`.
- **event** (pure translation to §6.1 commands):
  - `TEXT` → `LK_ED_INSERT_TEXT`;
  - `KEY_DOWN`: arrows ±CTRL ±SHIFT → motion commands;
    HOME/END (±CTRL → line/doc); PAGEUP/PAGEDOWN;
    BACKSPACE/DELETE (±CTRL → word variants); RETURN → insert
    `"\n"` (consumed — unlike text input it does not bubble);
    TAB → insert indent (consumed; focus escape is ESC-then-TAB,
    consistent with editors everywhere); CTRL+A/C/X/V;
    CTRL+Z / CTRL+SHIFT+Z;
  - `POINTER_DOWN` → `LK_ED_SET_CURSOR` (hit line via viewport,
    byte via `index_from_x`), `lk_focus_set`, `lk_capture_set`,
    begin drag;
  - `POINTER_MOVE` while captured → `LK_ED_SET_CURSOR` with extend
    (pointer capture's second client, as predicted when it was
    built);
  - `POINTER_UP` → release capture;
  - `WHEEL` → `LK_ED_SCROLL_LINES` (consumed — the editor owns its
    viewport; it never leaks to an ancestor `UIK_SCROLL`);
  - ESC and anything unlisted bubbles.
  - Editing commands that move the cursor scroll-to-cursor.
- **Attachment cardinality contract**: one `lk_editor` attaches to
  at most one tree node per frame (single transient geometry
  block). Two panes over one document = two `lk_editor`s sharing
  `lk_document` + `lk_edit_history` — which is the configuration
  the whole design exists to make cheap. Debug-assertable: the
  layout hook stamps the frame + node, and a second claimant hits
  an assert.
- **Tabs (pinned)**: v1 default inserts spaces (`tab_size` = 4;
  weft defaulted to literal tabs — deliberate deviation, revisit
  when tab rendering matures). Literal `\t` bytes in loaded text
  still render correctly via segment-wise tab-stop expansion:
  runs split at `\t`, x advances to the next `tab_size ×
  space-advance` stop, and `x_from_index`/`index_from_x` walk the
  same segments. This is the same run-splitting machinery stage C
  needs for spans, landed one stage early because code files make
  it unavoidable.
- No `LK_EVENT_VALUE_CHANGED`: its payload is an interned string,
  which the no-interning rule forbids — and the change protocol
  (§3.2) is the strictly better signal. Hosts subscribe to the
  document.
- Blink: deferred; steady cursor (lk has no time source — candidate
  `lk_ui_set_time_ms` when animations also want one).


## 8. Self-contained rendering (`LK_ROP_DRAW_RUN`, stage B)

`LK_ROP_DRAW_TEXT` carries an interned `str_id`; editor lines must
never intern. v1-draft had the new op carry a borrowed pointer into
editor scratch; review rejected that — it silently assumes
synchronous consumption, forbids render-list capture/replay and
inspection, aliases editor internals, and hides a one-editor-one-
node constraint in a lifetime rule. Instead **the render list owns
its bytes**:

```c
/* on lk_render_list: */
char  *bytes;                 /* frame-local run arena          */
lk_u32 bytes_count, bytes_cap;

lk_u32 lk_render_list_push_run(lk_render_list *rl,
                               const char *ptr, lk_u32 len);
                               /* copies; returns offset         */

/* on lk_render_cmd (DRAW_RUN): */
lk_u32 run_off, run_len;      /* into rl->bytes                 */
```

The widget extracts and shapes in its own scratch, then copies each
emitted run into the arena. The copy is bounded by *visible* text —
viewport-sized, not document-sized — and buys a render list that is
a value: inspectable after the fact, capturable, replayable,
consumable late, and alive after the editor is destroyed. (This is
the first step of the road toward CLIM-style output records: not
yet semantic, but at least self-contained.) The arena resets with
the command array each frame; existing capacity is reused.

The SDL consumer draws runs through the existing scratch `TTF_Text`
(`TTF_SetTextString` with explicit length; empty runs are skipped —
the documented len-0 gotcha). The stub backend needs nothing;
headless tests assert on the render list itself, which the arena
makes *more* testable (bytes are right there).

**Virtualization** is the line index doing its job: visible line
range from the anchored viewport, extract and emit only those
lines. Per-frame cost is proportional to the viewport, never the
document.


## 9. Anchored viewport

Not an absolute pixel offset. `scroll_y: lk_i32` breaks twice: a
4 GB document's pixel extent overflows signed 32 bits, and an edit
above the viewport silently changes what the same pixel offset
means.

```c
typedef struct lk_editor_viewport {
  lk_u32 top_line;   /* first (partially) visible document line */
  lk_i32 y_offset;   /* pixel offset into that line, [0, line_h) */
} lk_editor_viewport;
```

Scrolling adjusts the anchor; layout resolves it to a pixel window
for the visible range only, so nothing ever multiplies
`line_count × line_height` into an `lk_i32` (scrollbar thumb
proportions are computed in ratio space with explicit widening).
When wrapping arrives, `top_line` becomes a visual-line anchor —
same shape, richer index.

The same *position + bias* thinking applies to cursor and selection
under foreign edits: v1 keeps naked offsets (the editor is its own
document's only mutator in practice), but no API added here assumes
positions are bias-free forever — the annot store's anchors already
embody the general mechanism, and a future shared marker facility
can serve cursors, search results, and annotations with one
transformation rule. Nothing in v1 forecloses that; nothing in v1
builds it.


## 10. Styled spans (stage C)

The layering is: annotation (semantic) → projection (app-side) →
spans (appearance) → editor rendering. The editor never knows what
"syntax error" means.

Span delivery is an explicit **viewport-scoped snapshot**, not a
bare array (which would imply copying every span in the document
every frame and would be silently wrong one revision later):

```c
typedef struct lk_edit_span {
  lk_u32 start, end;          /* byte offsets, [start, end) */
  lk_color fg, bg;
  lk_u8 flags;                /* LK_SPAN_FG | LK_SPAN_BG |
                                 LK_SPAN_UNDERLINE */
} lk_edit_span;

typedef struct lk_edit_span_snapshot {
  lk_revision revision;       /* coordinates valid at this revision */
  lk_u32 range_start, range_end;  /* range the producer resolved */
  const lk_edit_span *spans;  /* sorted, non-overlapping */
  lk_u32 count;
} lk_edit_span_snapshot;

void lk_editor_set_spans(lk_editor *e, const lk_edit_span_snapshot *snap);
```

The editor copies the snapshot. Staleness policy (pinned): a
snapshot whose revision no longer matches the document is **ignored
at render** (unstyled text for a frame beats misplaced styling; the
producer re-runs off the same change notification that made it
stale). A snapshot not covering the visible range styles what it
covers. Render splits each visible line's runs at span boundaries —
the §7 tab machinery generalized — emitting bg FILL_RECT, DRAW_RUN
with span fg, underline as 1-px FILL_RECT.

The **annotation store** lifts in this stage (`lk-annot-store.c`,
same treatment as the document, its 505-line test suite ported) as
the canonical span *producer*: it subscribes to the document (one
listener among peers — v1-draft's "document calls the annot store"
slot is dead, replaced by §3.2), and a small helper walks
`lk_annot_in_range(viewport)` + a merge into a snapshot. Weft's
layer-stack pattern with lk's style sensibility.

**Decorations are not presentations** — pinned distinction for the
future: a span is appearance only. An underlined range that *means*
something (a diagnostic, an evaluated value, a definition target, a
command-bearing presentation) keeps its identity in the annotation
layer, projects to spans for drawing, and will get hit-testing and
applicable-command resolution from the future presentation layer
(§12). Flattening to colors is a rendering step, never the model.


## 11. Lcl bindings and DSL (stage D)

- Opaque Lcl values for `lk_document`, `lk_edit_history`,
  `lk_editor` (finalizers release their resource registrations —
  the explicit lifetime domain of §5).
- Procs (sketch): `lk::doc_new ?text?`, `lk::doc_text`,
  `lk::doc_insert/delete` (transactional: `lk::doc_transact $d
  {...}` brackets), `lk::doc_len/line_count/revision`,
  `lk::doc_subscribe $d $proc` (deltas as a list of dicts),
  `lk::history_new/undo/redo`, `lk::editor_new $doc $hist`,
  `lk::editor_cursor/selection`,
  **`lk::editor_command $ed $cmd ?arg?`** (the §6.1 enum by name —
  scripts drive the same verbs as keys), `lk::editor_set_spans`
  (snapshot dict).
- `lk::prop` learns `UIV_RESOURCE` passthrough for opaque editor
  values; DSL: `editor` joins the widget exports, one new
  `_prop_schema` key — `editor "ed" #{editor $ed focusable 1}` —
  which under DSL v2 hard-errors if typo'd.
- Example: `examples/editor-dsl.lcl` — load a file, editor in a
  split beside a status column (line/col/revision via a document
  subscription), a few annotation spans to prove styling, and a
  button invoking `lk::editor_command` to prove the command path.
  The first visibly weft-shaped lk app.


## 12. The weft surface composes this; it is not this

Boundary statement, so scope stays honest as the track continues:
`lk_document` stays a text model; `lk_editor` stays a text-editor
view. Neither absorbs weft feature by feature. The eventual
notebook/environment layer (`weft_surface` or kin) *owns* documents,
views, evaluation sessions, output records, presentations of Lcl
objects, inspectors, and command context — and projects them through
ordinary lk composition (splits, overlays, editors, labels).
Evaluation results anchored to source ranges are presentations
beside the document, not bytes inside the piece table. CLIM-like
means persistent semantic objects projecting themselves through
ordinary panes — not one gigantic magical widget.


## 13. Deferred, with landing spots

| feature | lands as |
|---|---|
| text wrapping | visual-line cache between doc lines and screen lines (weft `editor.c` reference); viewport anchor already shaped for it (§9) |
| column/block selection | editor state + multi-rect selection render; no core change |
| cursor blink | needs a time source; candidate `lk_ui_set_time_ms` when animation wants one too |
| IME composition display | preedit events + underline; SDL backend stage (`SDL_SetTextInputArea` is already focus-driven) |
| tree-sitter | span producer over the annot store; zero widget changes |
| annotation persistence | `annot_persist.c` lift (1240 lines) after the store proves out |
| search/goto/prompt UI | app-level: `text_input` + overlay + translators; weft `pane.c` superseded, not ported |
| multi-view cursor sync | views' document subscriptions adjust cursors on foreign deltas — protocol ships in stage A, policy later |
| position markers (bias'd cursors) | generalize the annot store's anchors into a shared marker facility when a second client (search results) exists |
| editor keybindings as data | command layer is the substrate; a binding table replaces the switch when Lcl-configurable keymaps arrive |
| widget-registration protocol upgrade | editor dogfoods it when out-of-core widgets need theme/DSL/resource registration |


## 14. Staging and gates

Each stage one commit, suites green, gates explicit.

- **A — document + history + change protocol.** `lk_revision`,
  `lk_document` (transactions, deltas, subscriptions, pinned
  contracts §3.3), `lk_edit_history`. Weft's `test_document.c`
  ported, plus new tests for every pinned contract, transaction
  grouping, listener dispatch (multi-subscriber, once-per-commit,
  reentrancy assert), undo/redo through the notification path.
  Gate: ported + new tests green; zero UI files touched.
- **B — substrate + widget.** Two commits:
  - **B1 (core)**: `UIV_RESOURCE` + `lk_resources` on `lk_ui`
    (+ dump format), `LK_ROP_DRAW_RUN` + render-list byte arena +
    SDL consumer. Gate: resource-table tests (stale ref, wrong
    type, dump), render-list arena tests.
  - **B2 (editor)**: `lk_editor` + command layer + `UIK_EDITOR`
    vtable + `UIP_EDITOR` + anchored viewport + tab segments +
    theme defaults. Gate: headless tests drive typing, motion,
    selection, clipboard, undo, click/drag through
    `lk_event_route` + stub backend, asserting document content,
    cursor, and render-list shape (runs readable from the arena);
    commands also invoked directly (bypassing events) to prove the
    layer split; demo hand-check.
- **C — annotations + styled spans.** `lk-annot-store.c` +
  ported `test_annot_store.c` (as a document subscriber), span
  snapshot API + boundary-split rendering + staleness policy.
  Gate: ported suite green; span/staleness render-list tests.
- **D — bindings, DSL, example.** Gate: binding tests for every
  proc incl. error paths and finalizer/release behavior; example
  under the dummy driver; docs updated (CLAUDE.md, this file,
  TODO.md).


## 15. Resolved questions (v1 draft → review)

1. **Instance binding** — property mechanism confirmed; raw
   `UIV_PTR` replaced by typed `lk_resource_ref` (§5).
2. **Undo placement** — out of the view, into a shared
   `lk_edit_history`; document applies transactions, history
   remembers and reverses them (§4).
3. **Headers** — separate: `lk-document.h`, `lk-editor.h`,
   `lk-annot-store.h`; `lk.h` stays the UI-tree contract (§2).
4. **64-bit revisions** — no general `lk_u64`; opaque comparable
   `lk_revision` pair (§3.1).
5. **Deferrals** — wrapping and the visible features stay deferred;
   the invisible substrate (resource refs, transactions,
   multi-subscriber notification, command layer, anchored viewport,
   self-contained render list) pulled into v1 (§1).

Remaining small confirmations (defaults chosen, veto freely):
tab policy §7 (spaces on insert, segment-expanded rendering of
literal tabs); span staleness = ignore-at-render §10; resource
table lives on `lk_ui` §5; contract answers §3.3.
