# The Weft Surface: Interior Presentations and Weft-in-Lcl (Design, v2)

The composition layer promised by `docs/editor.md` §12 — and the
thesis document for where lk is going. Revision history: v1 draft
2026-07-28; v2 2026-07-29 after review. North star, agreed
2026-07-28:

> lk should (eventually) be capable of implementing weft in pure
> Lcl/lk, with usual widgets — widgets that are not overly
> optimized for weft's use cases. Weft needs Acme-style
> interactions within the text surface, and that should be
> relatively generic and configurable.

The thesis survives review intact: **Acme interactions are CLIM
presentations at the wrong granularity.** lk already has the full
pipeline for making a *node* mean something — `present` attaches
semantics, translators turn gestures on presentations into
commands, commands carry the semantics to the application — and
already has the range substrate (bias-anchored annotations that
survive edits). The missing mechanism is one bridge. Weft's
`layer_stack_resolve_action` does not port; there is no second
dispatch system; and there is no required C-level surface widget
or toolkit primitive — the weft surface is an Lcl program.

What v2 changes (all from review, all at the semantic boundary —
the v1 draft was "too text-shaped" there): presentations carry a
**typed value**, not a string pair; resolution yields **ordered
candidates**, not a pre-chosen winner (with overlapping
presentations, the *gesture* determines which presentation is
relevant — picking innermost before matching can discard a valid
interaction); the translation API takes a **generic locus**, not a
byte position; the editor consumes a **presentation-source
interface**, not a hard annot-store reference; command arguments
get a real **lifetime mechanism** instead of intern-at-dispatch;
producers are **revision-driven derived state**, not frame
callbacks; button matching uses **lk-owned enums**; focus behavior
under translated gestures is **pinned and testable**.

Naming, two levels (per review): **interior presentations** — the
generic lk mechanism (sub-node presentation candidates offered to
the one translator matcher); **range presentations** — the
editor/annotation specialization, the first client.

Design rules the north star imposes:

1. The editor acquires *mechanism only* (discover candidates at a
   locus, offer them). Which button executes, what "look" means,
   what plumbing patterns exist — policy, in script.
2. Anything generic weft needs (button matching, doc search, the
   presentation machinery itself) lands as a general facility
   justified on its own terms.
3. Anything weft-specific is Lcl application code, and the
   milestone artifact is exactly that program (§6).


## 1. The model

> An interior-presentable widget resolves zero or more
> presentation candidates at a gesture locus. Each candidate
> carries a presentation type, a typed application value, a
> widget-specific immutable locus, and explicit precedence. The
> existing translator system considers the event against the
> candidates in order and emits the first applicable command. The
> widget applies its ordinary behavior only if no translation
> fires.

The invariant underneath: **there is one translator matcher.**
Widgets differ only in how they discover candidates — node
presentations, editor ranges, someday table cells and canvas
shapes all converge on the same matching function after discovery.
(A `presentation_at` widget-vtable hook that would let the event
router drive discovery uniformly is anticipated but deliberately
not in S1; the editor must simply not use a private alternate
path.)

### 1.1 Presentations and hits

```c
typedef struct lk_presentation_hit {
  lk_u32 type_id;      /* interned ptype — vocabulary is bounded */
  lk_value value;      /* the typed application value (below)    */
  lk_u32 locus_kind;   /* interned, e.g. "editor-range"          */
  lk_u32 locus[6];     /* immutable snapshot, packed by the       */
                       /* discovering widget                      */
} lk_presentation_hit;
```

- **The value is typed, never display text to re-parse.** A
  `file:line` range *presents* a location object; the visible
  bytes and the presented value are independent (`main` on screen
  may present a symbol with module identity and definition
  location). Value forms, by nature of the value:
  - **application objects** → `UIV_RESOURCE` refs. The typed,
    generation-checked, *reclaimable* mechanism built in editor
    stage B1 — release actually frees, unlike interning. On the
    Lcl side the wrapper retains the Lcl value and releases the
    registration when the presentation detaches (the same
    lifetime discipline the doc/editor wrappers already use).
  - **bounded symbols** (enum-like: a verb name, a layer tag) →
    interned `UIV_STR`. Interning is for bounded vocabularies
    only.
  - **genuinely-text values** (a path string when no richer
    object exists) → transient text via the command arena (§1.2)
    — never interned.
- **The locus is the immutable evidence of what was hit.** For the
  editor (`locus_kind = "editor-range"`, packing documented in
  `lk-editor.h`): annotation id, range start, range end, hit byte
  position, and the document revision pair at dispatch — enough
  for a handler to act on the exact presented extent and to
  *detect staleness* if it runs after further edits, without
  re-querying a mutable store to rediscover what was clicked.
  Scoping position (recorded): opaque packed words rather than
  resource-registered snapshot objects — same immutability, no
  per-click table churn.
- Precedence metadata travels with discovery order (§1.4), not in
  the hit.

### 1.2 Command-argument lifetime (no intern-at-dispatch)

v1's "intern at dispatch, clicks are human-scale" is withdrawn —
interning is permanent state and interactive values are unbounded
(logs full of unique paths, scripted gestures, long sessions).
Instead the **command queue gains a dispatch arena**, the
render-list byte-arena pattern reapplied: commands may carry
transient text arguments copied into queue-owned storage, valid
until `lk_ui_clear_commands` (the Lcl bridge copies into Lcl
strings at handler time, as it already does for everything else).
`lk_command` carries the full `lk_presentation_hit` (fixed-size)
alongside its existing args; resource-valued presentations pass
the 8-byte ref. Nothing about a command's payload outlives the
dispatch window unless the application retains it — which is the
application's call, made with a typed value in hand.

### 1.3 Candidate translation

```c
/* THE matcher: consider ev against candidates in order; emit the
 * first applicable translator's command (hit attached).  Node-level
 * presentation routing converges on this same function after its
 * own (single-candidate) discovery. */
int lk_translate_presentations(lk_ui *ui, const lk_tree *t, lk_ix node,
                               lk_event *ev,
                               const lk_presentation_hit *hits,
                               lk_u32 n);
```

- **Button matching, lk-owned**: `lk_pointer_button`
  (`LK_POINTER_BUTTON_ANY = 0`, `PRIMARY`, `MIDDLE`, `SECONDARY`)
  on `lk_translator`; the SDL backend maps its own values in.
  Node-level pointer translators get discrimination for free.
- **Modifier semantics, verified rather than reproduced**:
  `lk_event.mods` is already the four *semantic* bits
  (SHIFT/CTRL/ALT/GUI) — the SDL translation never forwards
  Caps/Num Lock — so exact-match over normalized mods (the
  existing keycode discipline, extended to `button != 0`) is
  sound. Required/forbidden masks are noted as a future
  refinement if a real matcher needs "shift-agnostic".
- **DSL**: matcher dict, DSL-v2 style —
  `translator pointer_down #{button middle} action Execute` —
  unknown keys hard-error; exact composition with the positional
  forms finalized at implementation.

### 1.4 The annotation adapter (range presentations proper)

The store carries presentations and answers *query-all*:

```c
int lk_annot_set_present(lk_annot_store *s, lk_u32 id,
                         lk_u32 type_id, lk_value value);
/* ALL candidates at pos, precedence-ordered:
 * layer priority desc -> smaller range -> insertion serial. */
lk_u32 lk_annot_presentations_at(const lk_annot_store *s, lk_u32 pos,
                                 lk_presentation_hit *out, lk_u32 cap);
void lk_annot_layer_set_priority(lk_annot_store *s, const char *layer,
                                 lk_i32 priority);   /* default 0 */
```

- Precedence is explicit (review): layer priority first, then
  specificity, with insertion serial only as the stable final
  tie-break — producer rebuild order can never silently reorder
  interaction semantics. Scoping position (recorded): a per-layer
  integer is the minimal explicit form; richer schemes wait for a
  conflict that needs them.
- Value lifetime: the store owns no value semantics; an optional
  per-store release hook (installed by the bindings) fires when a
  presentation detaches (record removed, presentation replaced,
  store cleared/destroyed) so resource registrations and Lcl
  retains are released. The store stays UI-independent.
- **Persistence is conditional** (review): range *anchoring* is
  persistence-compatible; whether a presentation's value
  serializes is a per-type decision (codec, stable external form,
  or a transient flag) made when the `annot_persist` lift
  happens. File locations may persist; closures and live objects
  are runtime-only. Nothing here designs the codec.
- The adapter to the generic interface:

```c
typedef struct lk_presentation_source {
  void *ud;
  lk_u32 (*query_at)(void *ud, lk_u32 pos,
                     lk_presentation_hit *out, lk_u32 cap);
} lk_presentation_source;

lk_presentation_source lk_annot_presentation_source(lk_annot_store *s);
```

### 1.5 The editor offering

```c
void lk_editor_set_presentation_source(lk_editor *e,
                                       const lk_presentation_source *src);
```

The source interface (not a hard store reference — review point
adopted): one editor, one source; composing multiple stores is an
application-side source, added the day someone needs it; tests can
feed synthetic sources with no store at all. The annot store
remains the first and primary substrate without becoming the only
possible one.

On POINTER_DOWN (any button), before its normal behavior, the
editor maps (x, y) → byte position, queries the source, and offers
the candidates via `lk_translate_presentations`. Pinned behavior:

- A translated gesture performs **no focus change, no cursor
  movement, no selection mutation, no pointer capture** — the
  command decides policy (`lk::focus_set` is one call away). This
  is a testable contract, not an accident.
- No match → the editor proceeds exactly as today: primary places
  the cursor/starts drag (with its existing focus behavior),
  other buttons return 0 and bubble. No source, no candidates, or
  no matching translator ⇒ behavior byte-identical to the current
  editor.
- Not in v1 (recorded): keyboard activation of the cursor
  position's presentations; hover feedback (pointer-move →
  transient span). Static styling by producers covers the
  affordance need first; both layer on without model changes.

`lk_editor_pos_at(e, x, y, &pos)` is exposed with a pinned
contract (review §12): coordinates in the same window space
pointer events use; resolves against the editor's last completed
layout snapshot (the transient geometry block); returns 0 before
first layout or when (x, y) is outside the editor's laid-out rect
(no clamping — callers wanting nearest-position semantics use the
widget's own click path); the one-editor-one-node-per-frame
invariant from stage B2 is what makes "its last layout"
well-defined.


## 2. What weft needs: the feature map

| weft feature | status | lives in |
|---|---|---|
| piece-table document, undo, revision | DONE (editor track A) | lk C |
| anchored annotations, layers | DONE (stage C) | lk C |
| multi-line editor: cursor/selection/motion/clipboard | DONE (B2) | lk C |
| wrap, horizontal scroll | DONE (wrap track) | lk C |
| styled ranges (per-char fg/bg/underline) | DONE (spans) | lk C + app producers |
| split panes, dividers | DONE | lk C |
| modal/overlay dialogs (goto, prompts) | DONE — proven composition | Lcl app |
| status bars, tag lines | DONE — ordinary widgets | Lcl app |
| **Acme interactions (execute/look on ranges)** | **THIS DOC §1** | lk mechanism + Lcl policy |
| plumbing (file:line → clickable) | §1 + pattern scan | Lcl app (producer) |
| buffer manager (N docs, open/switch) | nothing needed | Lcl app |
| search / search-next | one primitive (§3) | lk binding + Lcl UI |
| file open/save | `packages/lcl-io` (§3) | Lcl package |
| REPL / eval into buffer | Lcl `eval`/`isolate` + docs | Lcl app |
| keybindings as data | command layer ready | Lcl app |
| syntax highlighting (tree-sitter) | deferred | later C, as producer |
| annotation persistence | deferred | later C (§1.4 caveat) |
| column/block selection | deferred | later C (editor) |


## 3. Supporting gaps

1. **`lk_doc_find`** — literal forward byte search, piece-boundary
   aware, close to the document implementation:
   `int lk_doc_find(const lk_document *d, const char *needle,
   lk_u32 needle_len, lk_u32 from, lk_u32 *out_pos)` (the Lcl
   binding returns -1 for not-found; the C API keeps positions
   unsigned). Backward search waits for a concrete consumer.
2. **`lk::editor_pos_at`** — §1.5 contract; enables app-level
   "word under pointer" fallbacks (word extraction is script over
   viewport-local `doc_text` bytes).
3. **File IO → `packages/lcl-io`** (resolved by Brennan
   2026-07-28): core Lcl stays IO-free by design — it must remain
   usable in environments with custom IO (Windows variants,
   embedded) — and lk stays a UI library (`lk::doc_load` would
   fuse storage policy, encoding, and filesystem capability into
   the toolkit; documents consume bytes the application supplies).
   The existing read/write in lcl-io grows there if weft needs
   more (atomic save, encoding/newline policy, capability).
   Wiring (resolved 2026-07-29): `lcl_lk_main` registers lcl-io,
   building lcl with its IO package enabled (`-DLCL_BUILD_IO`
   or the package's actual flag — verify at S2/S3) — so weft-mini
   opens real files.
4. **Pattern matching** — no regex in Lcl; plumbing v1 uses
   `String::find`-style heuristics in script (viewport-bounded,
   adequate). The mechanism is indifferent to how annotations are
   discovered; real pattern syntax is a future Lcl conversation.


## 4. The surface is an Lcl application

There is **no required C-level surface widget or toolkit
primitive** (softened per review: the application may well grow a
persistent Lcl-side session/workspace model — buffers, view
arrangements, eval sessions, plumbing rules, navigation history —
and should; that is application state, not an lk widget, and
nothing obliges it to stay a loose namespace of dicts).

The surface composes: documents + histories + annot stores
(existing procs, existing wrapper-retention lifetimes), editors
and pane wiring, translators and command handlers (the policy:
what execute/look *do*), keymaps, buffer list, mode state — and
**producers**, whose contract v2 restates per review:

> Producers are persistent derived-state components, revision-
> driven, not frame callbacks.

A plumbing scanner subscribes to (or checks) the document
revision and updates its annotation layer *incrementally on
change*; annotations are stable identities, not per-frame
recreations (which would churn ids, destabilize precedence
serials, fight persistence, and rescan needlessly). Per frame,
only *projection* runs: visible range → query stable annotations →
span snapshot. weft-mini may legitimately start with
"if revision != last_scanned: rebuild layer" — still
revision-driven, just not yet incremental.

**Output records, explicitly not solved here** (review): range
presentations solve semantic *anchoring and interaction* for
evaluated results — they do not solve inline *block layout* (a
tree inspector between text rows is a mixed text/widget
visual-line problem this doc does not touch). Overlays and sibling
panes are the sanctioned composition for rich output, and they are
sufficient for a substantial environment. If inline blocks ever
matter, that is its own design.


## 5. Staging

**S1 — the mechanism** — **DONE 2026-07-29**, four conceptual
pieces, one landing sequence (structure per review).  Landing notes
(implementation decisions within the latitude this doc grants):

- Locus fill split: `lk_annot_presentations_at` fills what the store
  knows — `{annot id, start, end, 0, rev.hi, rev.lo}` with
  `locus_kind = 0` — and the editor's offer path
  (`lk_editor_offer_presentations`) stamps `locus_kind =` interned
  `"editor-range"` plus `locus[3] =` hit position.  The store has no
  intern table and no gesture; the editor has both.  Packing
  documented at `lk_editor_set_presentation_source` in lk-editor.h.
- Insertion serial = the record id (already stable + monotonic); no
  new field.
- The convergence shape: `lk_translate_event` and
  `lk_translate_presentations` share `translator_match` +
  `emit_translated` in lk-command.c; node presentations emit with
  their pvalues as args and a zeroed hit.
- `button` sits before `command_name` in `lk_ui_add_translator/_s`;
  the Lcl `lk::add_translator` takes it as an OPTIONAL 8th arg
  (`primary|middle|secondary`, ""/0 = any) so existing 7-arg callers
  (examples) run unmodified.
- Editor default behavior under the pinned §1.5 contract: primary or
  a button-0 synthetic event places cursor/focus/capture as before;
  middle/secondary with no translation bubble (previously any button
  placed the cursor — the pinned contract supersedes).
- Lcl glue: `lk::annot_present [ui s id ptype value]` (ui first — it
  supplies the intern table and the resource table; a store's
  presentations bind to ONE ui), `lk::annot_layer_priority`,
  `lk::editor_presentations [ed s]`, `lk::editor_pos_at [ed x y]` →
  pos or -1.  Command dicts carry a `hit` sub-dict with the
  editor-range locus decoded and lcl-value resources unwrapped.
- The transient-text emitter is `lk_v_text(ui, ptr, len)`; the
  accessors are `lk_command_arg_text` / `lk_command_text`, which
  resolve against the queue arena or the command log's own copy
  (made at record time, so `lk_ui_dump_commands` — which now also
  dumps the log — never reads reset memory).

Original staging:

1. *Presentation representation*: `lk_presentation_hit`, locus
   kinds + editor packing, command-queue dispatch arena +
   hit-carrying `lk_command`, Lcl marshaling (hit as a sub-dict;
   transient args copied at bridge time).
2. *Candidate translation*: `lk_translate_presentations` as THE
   matcher; node-presentation routing converges on it;
   `lk_pointer_button` enum + translator field + SDL mapping;
   matcher-dict DSL.
3. *Annotation adapter*: `lk_annot_set_present` (typed value +
   release hook), `lk_annot_presentations_at` (query-all,
   precedence: layer priority → specificity → serial),
   `lk_annot_layer_set_priority`, `lk_annot_presentation_source`.
4. *Editor offering*: `lk_editor_set_presentation_source`,
   offer-before-behavior, pinned no-focus/cursor/capture on
   translated gestures, `lk_editor_pos_at` contract.

Gate (headless): overlapping presentations routed by gesture (the
review's motivating case: file-location inside evaluated-output —
middle fires the output's translator, secondary fires the
location's); precedence order asserted incl. layer priority
beating specificity; translated click moves no cursor, takes no
focus, sets no capture; unpresented/unmatched behavior
byte-identical (existing editor tests untouched); button + mods
discrimination at node level too; resource-valued presentation
round-trip incl. release-hook firing on annot removal; arena args
valid at handler time, gone after clear; stale-locus detectability
(edit between dispatch and handling → revision mismatch visible).

**S2 — primitives** — **DONE 2026-07-29**: `lk_doc_find` (+
piece-boundary tests), lcl-io wiring for the example runner.
(`lk::editor_pos_at` landed in S1.)  Landing notes:

- `lk_doc_find` scans by chunked reconstruction: a window (stack
  buffer, 1024 bytes; document-allocator heap window for larger
  needles) filled via `lk_doc_get_text` and slid with
  `needle_len - 1` bytes of overlap, so matches spanning any number
  of piece boundaries are seen without piece-aware matching logic.
  Binding: `lk::doc_find [d needle ?from]` → position or -1
  (from defaults 0; empty needle is a hard error; from past the end
  is just -1 so search-next never errors).
- lcl-io wiring: the package's actual flag is `LCL_BUILD_IO`
  (target `lcl_io`, entry point `lcl_register_io(interp)` in
  `<lcl-io.h>`, procs in the `io` namespace).  lk gates it behind
  `LK_LCL_IO` (default ON when the package dir exists, mirroring
  the SDL auto-detect pattern); when ON, `lcl_lk_main` defines
  `LK_HAVE_LCL_IO`, links `lcl_io`, and registers io after core +
  lk.  `lcl_lk_test` stays hermetic — the io procs are exercised by
  a manual smoke script through the runner, not the test suite.

**S3 — weft-mini** (`examples/weft-mini.lcl`, pure Lcl, target
~300 lines): two panes; revision-driven plumbing producer
annotating `file:line` + pattern ranges with typed presentations
(underlined via spans); middle = execute (eval selection-or-word
into an output document), secondary = look (search-next via
`doc_find`); Ctrl+F bar as composition; every gesture a script
translator registration, re-bound differently in one commented
line to prove configurability. Acme's L/M/R meanings live HERE
and only here. Gate: runs headless; gesture paths driven through
the harness where possible; hand-check for feel.

**S3 — DONE 2026-07-29.**  Landing notes:

- Shaping decisions (Brennan, 2026-07-29): simple panes — one
  `split_h`, two editors (no Emacs-style pane control); and execute
  evaluates INTO THE CLICKED PANE, current-weft style — the result
  inserts after the executed line as `=> ...` (`!! ...` on error),
  annotated in an `output` layer, NOT into a separate output
  document.  The insertion is an ordinary transaction: undo removes
  it, anchors below shift.
- 568 lines (~350 code; the rest is the header doc plus the two
  in-buffer texts) — over the ~300 target, carrying per-pane status
  rows, save, flash messages, and an io-less fallback beyond the
  original sketch.
- Foreign-file plumbing replaces the left pane wholesale:
  `_make_pane` builds a fresh doc/history/store/editor and swaps the
  refs in a `panes` dict the view body re-reads every frame; the old
  editor is refcount-released and its stale resource ref renders as
  an empty pane for at most the frame in flight.
- Error-safe execute: stdlib `catch` (`catch {eval $code} res err`)
  absorbs both runtime and parse errors.  Two Lcl facts recorded: a
  single bare word self-evaluates (so word-execute of a non-proc
  echoes, never errors), and `catch $code` would leak parse errors —
  the `catch {eval $code}` form is required.
- Gesture wiring: plumb rides the S1 editor offer path (matcher-dict
  translator `#{button secondary}` on ptype `plumb`); LOOK and
  EXECUTE act in the app `event_handler` on middle/secondary clicks
  the editor leaves unconsumed (pinned §1.5 contract); Ctrl+F/Ctrl+S
  are keybindings scoped to a `buf` presentation on each editor node
  whose pvalue names the pane — which is also how the app knows the
  active pane.
- Producer: per-doc `scan_rev` cache, drop-and-readd layers, colon
  candidates found by `lk::doc_find` (C-speed) with only the colon
  neighborhood walked in script; plumb values are `#{file .. line
  ..}` dicts.
- Gate: a scratchpad harness (45 checks) evals the example up to the
  `app` call and drives scanner positions + presentation-value
  contents, word extraction, execute (word/selection/echo/error/
  parse-error), look wrap-around cycling, and the plumb pane-swap
  headlessly through `lcl_lk_main`; all seven examples run headless
  (exit 124); suites 420/420 core, 102/102 Lcl, no C changes.
  Hand-check only: gesture feel, visuals, search-bar/save flow.

**Horizon** (unscheduled): tree-sitter as producer; annot
persistence (with §1.4's per-type serialization decision);
output-record/inline-block design per §4; keyboard activation +
hover feedback; the `presentation_at` widget-vtable hook when a
third interior-presentable widget exists.


## 6. Resolved and recorded (v1 → review → v2)

1. **Typed values over string pairs** — adopted (§1.1);
   intern-at-dispatch withdrawn, replaced by resource refs +
   bounded-symbol interning + the command arena (§1.2).
2. **Candidates over winner-first** — adopted (§1.3/§1.4);
   query-all with explicit precedence.
3. **Generic locus over byte_pos; rename** — adopted;
   `lk_translate_presentations(…, hits, n)` (§1.3), locus packing
   per widget (§1.1).
4. **Source interface over store coupling** — adopted (§1.5).
5. **File IO** — `packages/lcl-io`, per Brennan; core Lcl stays
   IO-free for custom-IO environments (§3).
6. **Acme L/M/R as weft-mini-only policy** — confirmed (§5 S3).
7. **Naming** — both levels: interior (generic) / range (editor
   specialization).
8. Scoping positions recorded for review pushback if wanted:
   command arena instead of owned/refcounted string args (same
   guarantee, render-list precedent); per-layer integer priority
   as the minimal explicit precedence; opaque-words locus instead
   of resource-registered snapshots (no per-click churn).
