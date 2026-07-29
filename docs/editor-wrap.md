# Editor Wrap + Usability Round (Design, v2)

Track A after the editor-track v1 landing: text wrapping, minimal
horizontal scrolling, and the two usability riders the demo points
at (line:col status, Ctrl+G goto-line as app-level composition).
Revision history: v1 draft 2026-07-28; v2 same day after review.
The v2 corrections are low-level invariants, not architecture:
line-relative break offsets, generation validity, backend-delegated
break finding, a distant re-anchor path, pinned anchor affinity and
row-ownership rules, a wrap-mode enum instead of a boolean,
horizontal scroll pulled into W1 with default mode NONE, and the
delta coordinate contract pinned in `lk-document.h`.

Weft reference: `editor.c` (`rebuild_visual_line_cache`,
`visual_line_starts[]`, pos↔visual mapping). Wrap is a **redesign,
not a port**: weft wraps by character columns on a monospace grid
and full-rebuilds its cache per edit; lk must wrap in pixels
through the text contract and can invalidate incrementally through
the change protocol. What survives from weft: the visual-line
concept, tab stops resetting per visual row, empty-line handling.

Three review pushbacks stand (recorded here so they're revisitable
deliberately, not rediscovered): no new text-backend vtable slot in
v1 (break finding delegates through the existing two calls, §2); no
backend generation field in v1 (explicit invalidate hook instead,
§1); no `lk_doc_line_change` convenience struct until a second
subscriber needs it (§7 pins the coordinate contract itself now).


## 1. The cache

Per **document line**, wrap breaks stored **relative to the line
start**, first row implicit:

```c
/* inside lk_editor (all private) */
typedef struct ed_line_wrap {
  lk_u32 *breaks;      /* offsets RELATIVE to the line start, of the
                          2nd..Nth row starts; NULL when unwrapped */
  lk_u32 break_count;  /* row_count == break_count + 1 */
  lk_u32 break_cap;
  lk_u32 generation;   /* wrap_generation this entry was measured at */
} ed_line_wrap;

ed_line_wrap *wrap;          /* one per document line (spliced with
                                the line index, see §7) */
lk_u32 wrap_count, wrap_cap;

lk_u32 wrap_generation;      /* bumped on any wrap-key change */
/* wrap key (compared, and folded into bumping the generation):
   content width px, font_id, font_size, tab_size, and the text
   backend POINTER.  A backend that mutates its own metrics in
   place (font hot-reload, DPI change) is invisible to the key;
   lk_editor_invalidate_layout(e) is the explicit escape hatch.
   A backend-side generation counter is text-contract-v2 material,
   deferred until a live consumer exists. */
```

- **Relative offsets are the point**: an edit anywhere above a
  cached line shifts that line's *absolute* position but not its
  internal break shape — entries below an edit move by index only
  (§7 splice), with zero re-measurement and zero offset rewriting.
  Absolute row starts are produced on demand:
  `lk_doc_line_start(d, line) + rel_break`.
- **Unwrapped lines allocate nothing** (`breaks == NULL`,
  `row_count == 1`) — the common case for code is free.
- **Generation validity, no sweeps**: an entry is valid iff
  `entry->generation == e->wrap_generation`. Width/font/tab/backend
  changes bump the editor's generation — one integer write, not an
  O(lines) walk; a split-divider drag on a 100k-line file costs
  nothing per frame beyond re-measuring the visible rows.
- **Measurement is lazy and viewport-first**: layout validates only
  the lines it touches (visible window + anchor/cursor/jump math).
  There is deliberately **no global visual-row prefix** — nothing
  in editing needs a global row number (§3, §6).
- Mode `LK_EDITOR_WRAP_NONE` bypasses the cache: every line is one
  row; the pre-wrap code path is the degenerate case, not a fork.


## 2. Break finding — delegate fit to the backend

The engine never accumulates per-codepoint advances (that is both
quadratic under shaping-per-prefix backends and wrong for kerned,
ligated, or combining text). Break finding within a tab-free
segment is a **fit query answered by the backend** using the two
calls the contract already has:

```
ix = index_from_x(seg, font, size, remaining_width)   /* nearest */
if (x_from_index(seg, font, size, ix) > remaining_width)
    ix = previous boundary                            /* floor    */
```

- If the whole segment fits, continue to the next tab or line end.
  If nothing fits and the row is empty, take one boundary anyway
  (progress guarantee).
- Cost: O(rows) backend calls per line, not O(codepoints) — the
  backend shapes the line internally per call (SDL_ttf's substring
  queries), so total work is rows × line-shaping, the same class
  as rendering it.
- "Boundary" means whatever the text contract's `byte_ix` values
  are — today codepoint-aligned by contract. When the contract
  grows cluster-aware caret positions (a v2 contract change, with
  an affinity-taking `index_at_x` as its natural shape), this
  engine inherits it through the same two calls without
  modification: the break-finder holds no opinion about what a
  boundary is.
- Tabs advance to the next `tab_size × space-advance` stop relative
  to the CURRENT VISUAL ROW's x = 0 (weft behavior); a tab that
  would land past the width breaks first.
- Word wrap later = a different fit policy in this one function
  (prefer the last whitespace boundary at-or-before the fit point,
  char-fallback for unbreakable runs). Nothing outside changes.


## 3. Viewport anchor, rows, motion

- **Anchor**: `{lk_u32 top_byte; lk_i32 y_offset}` — `top_byte` is
  a visual-row start; resolution is local (find its doc line,
  measure that line, snap to the row start at-or-before it).
- **Anchor affinity is pinned: RIGHT.** An insertion exactly at
  `top_byte` shifts the anchor past the inserted bytes, so the
  content the user was reading stays at the top of the viewport
  (the inserted text lands above, off-screen). This is explicit in
  the transform code and tested — not folded into an ambiguous
  "standard transform"; cursors, selection endpoints, and future
  markers each pin their own affinity where they're implemented.
- **Row ownership is pinned: half-open.** Row *i* owns caret
  positions `[row_start_i, row_start_i+1)`; a position exactly at a
  wrap break belongs to the NEXT row; the final row additionally
  owns the end-of-line caret. One helper (`ed_pos_to_row`) encodes
  this and every consumer — cursor placement, vertical motion,
  HOME/END, selection rects, edge clicks, scroll-to-cursor — goes
  through it, so layout, render, and hit-testing cannot disagree
  by one row or one pixel.
- **Motion**: UP/DOWN move by visual row (sticky-x via the
  segment-aware `index_from_x` in the target row); PAGE_UP/DOWN
  count visual rows. Word/doc motion and editing stay byte-based.
- **HOME/END become command pairs, not one policy** (fits the
  command layer): `LK_ED_MOVE_ROW_START/END` (visual row) and the
  existing `LK_ED_MOVE_LINE_START/END` (logical line). The default
  keymap binds HOME/END to the ROW variants (identical to logical
  when unwrapped); Lcl, menus, and future keymaps can reach both.
- **scroll-to-cursor has two paths** (the v1-draft walk was wrong
  for distant jumps — Ctrl+G to line 90k, programmatic set_cursor,
  search hits, undo far away):
  - *Near*: target row within (or adjacent to) the materialized
    window → walk the anchor by rows until visible.
  - *Distant*: measure the target's line directly, re-anchor at the
    target row (`top_byte = row_start, y_offset = 0`), optionally
    walking back a few rows to center/bottom-place the cursor.
    Viewport-bounded regardless of distance; no global index
    needed. Scrollbar dragging and session restore use the same
    direct path.


## 4. Horizontal scrolling (W1, not deferred)

Wrap-off with a cursor that can vanish past the right edge is an
incomplete editor, and logs/code/tabular text are ordinary wrap-off
uses — the v1-draft's "wrap-on makes it moot for the demo" was
demo reasoning, withdrawn. Minimal implementation, active only in
`LK_EDITOR_WRAP_NONE`:

- `lk_i32 scroll_x` on the editor; forced to 0 whenever wrapping
  is active.
- scroll-to-cursor extends horizontally: keep the cursor inside
  the content rect with a small margin (~2 space-advances).
- Render subtracts `scroll_x`; hit-testing adds it; selection and
  span geometry are already x-relative to the same origin.
- SHIFT+WHEEL scrolls horizontally (matching the scroll widget's
  axis convention if it has one; otherwise dx wheel input maps
  naturally).
- No horizontal scrollbar rendering in v1.


## 5. API — a mode, not a boolean

```c
typedef enum lk_editor_wrap_mode {
  LK_EDITOR_WRAP_NONE = 0,
  LK_EDITOR_WRAP_CHARACTER,
  LK_EDITOR_WRAP_WORD        /* rejected until implemented */
} lk_editor_wrap_mode;

int lk_editor_set_wrap_mode(lk_editor *e, lk_editor_wrap_mode m);
                             /* 0 if unsupported (WORD, for now) */
lk_editor_wrap_mode lk_editor_wrap_mode_get(const lk_editor *e);
void lk_editor_invalidate_layout(lk_editor *e);  /* §1 escape hatch */
```

- **Default: `LK_EDITOR_WRAP_NONE`.** With working horizontal
  follow-cursor, wrap-off is no longer broken, character wrap's
  mid-word breaks aren't enshrined as anyone's default experience,
  and no interim policy calcifies in docs. Applications choose;
  the demo calls `lk::editor_wrap $ed character` explicitly — the
  API on camera. When WORD lands, apps that want text-area
  behavior switch one atom.
- Lcl: `lk::editor_wrap $e none|character|word` (unsupported →
  hard error listing supported modes, DSL-v2 style);
  `lk::editor_wrap_get $e` returns the mode name.


## 6. Scroll extent estimation

Vertical scroll clamping (and any future scrollbar) needs total
height, which laziness refuses to compute exactly. The model keeps
**exact rows for measured lines, estimates for unmeasured ones** —
never "unmeasured = 1 row" (an order-of-magnitude lie in
prose-heavy files):

```
est_rows(line) = max(1, ceil(line_byte_len * avg_px_per_byte
                             / wrap_width_px))
```

with `avg_px_per_byte` running over lines measured so far (space
advance as the seed before any measurement). Estimates converge as
scrolling measures; the thumb/clamp twitching toward truth is
standard editor behavior. The wrapping engine itself never
consumes the estimate — it exists only at the scroll-extent edge.


## 7. Delta contract (pinned in `lk-document.h`, stage W1)

The wrap cache is the third delta-driven structure (after history
and the annot store), and splicing demands an unambiguous
coordinate rule. **Pinned, matching the existing de facto
behavior the annot store already relies on: delta coordinates are
SEQUENTIAL** — each delta's positions are expressed in the
document state produced by all preceding deltas of the same
transaction, in array order. This goes in `lk-document.h` prose at
the `lk_doc_delta` declaration (it is a contract, not an
implementation detail) with dedicated tests: one-newline insert,
one-newline delete, N-lines-replaced-with-M, multiple primitive
deltas per transaction, delete-then-insert at one position, and
changes beginning/ending exactly on a newline.

Wrap invalidation in `ed_on_doc`, per delta in order: dirty the
line at `pos_to_line(delta.start)`; count `\n` in the delta's
`deleted`/`inserted` byte buffers (they're right there in the
delta) and splice `wrap[]` entries to match — inserted newlines
add fresh invalid entries after the dirty line, deleted newlines
remove entries. Entries below shift by index only (§1 relative
offsets make that free). A normalized per-transaction line-impact
struct (`lk_doc_line_change`) is deliberately NOT added: wrap
computes its impact locally in ~10 lines; protocol surface waits
for a second consumer.


## 8. Usability riders (stage W2)

- **line:col status — with a defined column.** "Column" means
  **character column**: 1-based codepoint count from the line
  start to the caret (tabs count as one character; a visual/
  tab-expanded column is a different, future notion and byte
  column is never displayed). Audit stage-D bindings for
  `lk::doc_pos_to_line` / `lk::doc_line_start`; add
  `lk::doc_char_col $d $pos` if script-side codepoint counting
  can't be made exact — the definition is documented either way
  so the demo shortcut can't become an accidental API.
- **Ctrl+G goto-line**: pure app-level script in the example —
  `keybinding` + the modal-overlay pattern + `text_input` +
  `lk::editor_command $e set_cursor` at `lk::doc_line_start`.
  Running proof that dialog UI is composition.


## 9. Staging and gates

- **W1 — wrap engine + horizontal scroll.** DONE (2026-07-28,
  e02bf4e). (`src/editor/` + the
  delta-contract pin in `lk-document.h` + tests). Internal build
  order (each layer tested against the one visible-row
  representation before the next): (1) relative-offset cache +
  generation validity; (2) visible-row production + rendering;
  (3) cursor mapping + hit-testing; (4) selection geometry;
  (5) vertical motion, ROW/LINE command pairs, paging;
  (6) delta splicing + anchor transform (RIGHT affinity);
  (7) distant jumps + scroll-to-cursor both paths;
  (8) horizontal scroll for NONE mode.
  Gate: all existing editor/span tests green — unwrapped behavior
  bit-identical (tests that assume no wrap get width-widened, not
  weakened; genuine contract changes reported, not slipped in) —
  plus stub-exact new tests: break positions incl. tab-at-edge and
  the empty-row progress guarantee; row-ownership rule at breaks
  (cursor lands on the NEXT row at a break; EOL on the final row);
  UTF-8 multi-byte at a break; motion across rows and line seams;
  sticky-x through wrapped rows; ROW vs LINE START/END commands;
  click on row 2+ and at the right edge; selection head/body/tail
  across rows; spans crossing a break; delta splice cases (§7
  list); generation invalidation on width change (split drag);
  anchor RIGHT affinity at top_byte; distant re-anchor (set_cursor
  far outside the window measures O(viewport), asserted by
  construction not timing); wrap-off degenerate equivalence;
  horizontal follow-cursor and SHIFT+WHEEL in NONE mode.
- **W2 — bindings + example + docs.** DONE (2026-07-28, this
  change): `lk::editor_wrap`/`_get`,
  doc-proc audit + `lk::doc_char_col` if needed, example gains
  explicit `character` wrap + line:col + Ctrl+G overlay; CLAUDE.md,
  editor.md §13 entry, this doc DONE-marked. Gate: binding tests
  incl. error paths; all examples exit 124 under the dummy driver;
  hand-check on a real display (typing, dragging the divider while
  wrapped, Ctrl+G to the last line, wrap-off horizontal follow).
  The audit found none of `doc_pos_to_line`/`doc_line_start`/
  `doc_line_end` existed in stage D — all three bound; and
  script-side codepoint counting cannot be exact (`String::length`
  is `strlen`), so `lk::doc_char_col` was added with the §8 column
  definition pinned at the proc.
- **Deferred**: word wrap (one fit-policy function, §2); wrap-width
  override; gutter/line numbers; horizontal scrollbar rendering;
  backend metric generations + cluster-caret `index_at_x`
  (text-contract v2, §1/§2); `lk_doc_line_change` (§7).


## 10. Resolved questions (v1 draft → review)

1. **Character wrap first** — confirmed, as engine validation; the
   public API is a mode enum from day one (§5), and "character"
   internally means "last backend-valid boundary that fits", not
   codepoint arithmetic (§2).
2. **HOME/END** — both semantics exist as commands; the default
   keymap picks visual-row (§3).
3. **Default** — changed from the v1 draft: `NONE`, made viable by
   pulling horizontal scroll into W1; the demo opts into
   `character` explicitly (§4, §5).
4. **No global visual-row prefix** — confirmed, with the §6
   estimator replacing "unmeasured = 1 row" and §3's direct
   re-anchor path replacing anchor-walking for distant jumps.
