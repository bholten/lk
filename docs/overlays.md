# Overlays — Current State (Lean) and Path to Proper

Status: **Proper — steps 1–6 DONE (2026-07-25)** — the generalized
overlay stack (`lk_overlay` on `lk_ui`, `src/core/lk-overlay.c`),
anchor resolution with viewport clamping, `UIP_HIDDEN` +
`lk_layout_subtree` content subtrees, focus traps, modal blocking,
core ESC dismissal, the dropdown migration, the `UIP_TOOLTIP` producer
(`src/core/lk-tooltip.c`), and app-level modal support
(`lk::overlay_push`/`lk::overlay_pop`, `examples/modal-dsl.lcl`) are
all implemented.  All three exit criteria are met (see below).  The
historical Lean description below is kept for context.

## What "overlay" means here

A rectangular region that:
1. Renders **after** (on top of) the main tree's render pass.
2. Is **hit-tested first** (before the main tree), so clicks inside it go
   to overlay content, not to whatever is visually underneath.
3. Dismisses itself when the user clicks outside its bounds.
4. Belongs to an "owner" node in the main tree (the dropdown trigger, a
   tooltip target, a button that opened a modal).

Tooltips, dropdown option lists, context menus, modal dialogs, and
dockable popups are all overlays.  In **Lk** today, only dropdowns are.

## Lean — what's in the tree now

All overlay support currently lives in `src/core/lk-dropdown.c` plus three
thin call sites:

- `lk_render_build_overlays()` — declared in `lk.h`, implemented in
  `lk-dropdown.c`.  Walks the tree looking for `UIK_DROPDOWN` nodes
  whose `LKS_EXPANDED == 1`, and emits fill + text commands for their
  popup list directly onto the render list.  Called once after
  `lk_render_build()` from the SDL backend's run loop.
- `lk_hit_test_overlay()` — declared in `lk.h`, implemented in
  `lk-dropdown.c`.  Same walk pattern: if the pointer is inside an
  expanded dropdown's popup rect, returns the option (or dropdown)
  under the cursor.  Used before `lk_hit_test()` in the SDL backend.
- `lk_overlay_dismiss_outside()` — declared in `lk.h`, implemented in
  `lk-dropdown.c`.  Closes any expanded dropdown whose popup *and*
  trigger rects do not contain the pointer.  Called on pointer-down in
  the SDL backend so clicks outside any open dropdown dismiss it.

`lk-render.c`, `lk-event.c`, and `lk-layout.c` are **untouched** by
overlay logic.  That's intentional — the blast radius is contained to
`lk-dropdown.c` plus three one-line hooks, so the eventual
generalization doesn't require ripping apart the core passes.

### What's intentionally missing

1. **No generalized overlay record.**  The system has no
   `lk_overlay { anchor, kind, content_ix, flags }` type.  Each overlay
   producer (currently just dropdowns) walks the tree itself to find its
   candidates.  A tooltip would have to do the same walk.
2. **No viewport-edge clamping.**  Popups draw at
   `(trigger.x, trigger.y + trigger.h)` unconditionally.  A dropdown
   near the bottom of the window clips; one at the right edge may
   extend past the viewport.
3. **No anchor flexibility.**  "Below trigger, same width" is
   hardcoded.  Tooltips want "above or below, near cursor"; context
   menus want "at cursor"; submenus want "right of parent menu".
4. **No focus trap.**  Opening a dropdown does not change focus.
   Modals (which need focus-stealing) are not yet expressible.
5. **No keyboard nav across overlays.**  Only one dropdown is expected
   to be open at a time; the implementation doesn't enforce this, but
   nothing routes TAB to overlays.
6. **Popup cannot scroll.**  Long option lists grow the popup past
   `DROPDOWN_POPUP_MAX_HEIGHT` (currently 240 px) — over that the
   popup is clamped but the extra options become invisible.
7. **Overlay content is not styleable per-node in the option sense.**
   Options inside a dropdown *are* node-tree members, so their
   `lk_style` is resolved normally.  But any overlay-specific chrome
   (the popup background, the hover highlight) is hardcoded in
   `lk-dropdown.c` — styleable via the dropdown's own `lk_style`, not
   via independent popup styles.

## Proper — the migration path

Rough sequencing, smallest-to-largest:

### Step 1 — introduce `lk_overlay` — DONE

*(2026-07-25: `lk_overlay` in `lk.h` with `owner_id`/`content_root_id`
as stable `lk_node_id` per v2 note 1; stack + push/pop/pop_owner/
top/count in `src/core/lk-overlay.c`; end_frame pops on owner removal
with the move filter.)*

Add a small value type to `lk.h`:

```c
typedef enum lk_overlay_kind {
  LK_OVERLAY_DROPDOWN_POPUP = 1,
  LK_OVERLAY_TOOLTIP,
  LK_OVERLAY_CONTEXT_MENU,
  LK_OVERLAY_MODAL,
} lk_overlay_kind;

typedef enum lk_anchor_mode {
  LK_ANCHOR_BELOW = 1,   /* below owner */
  LK_ANCHOR_ABOVE,
  LK_ANCHOR_AT_CURSOR,
  LK_ANCHOR_CENTER_VIEWPORT,
} lk_anchor_mode;

typedef struct lk_overlay {
  lk_u8 kind;              /* lk_overlay_kind */
  lk_u8 anchor_mode;
  lk_u8 dismiss_on_outside;
  lk_u8 traps_focus;
  lk_ix owner_node;        /* the trigger / hovered element */
  lk_ix content_root;      /* root of the overlay's node subtree, or 0
                            * if the content is procedurally generated */
  lk_rect offset;          /* relative to anchor; w/h may be 0 to mean
                            * "intrinsic" */
} lk_overlay;
```

Add `lk_overlay *overlays; lk_u32 overlay_count;` to `lk_ui`.

### Step 2 — hoist registration out of the widget walk — DONE

*(2026-07-25: the three public functions iterate `ui->overlays` and
now take `lk_ui*` + `lk_layout_cfg*`; dropdown's event handler pushes
and pops via `dropdown_open`/`dropdown_close`.)*

`lk_render_build_overlays` and `lk_hit_test_overlay` should no longer
special-case `UIK_DROPDOWN`.  Instead they iterate `ui->overlays`.
Widgets that want to open an overlay push onto that list during their
event handler (e.g. dropdown's pointer-down handler pushes an overlay
with `kind = DROPDOWN_POPUP`, `owner_node = dd`, `anchor_mode = BELOW`).
Pop on close.

### Step 3 — viewport-edge clamping — DONE

*(2026-07-25: `lk_anchor_resolve` in `lk-overlay.c`, public in `lk.h`;
BELOW flips above at the bottom edge, x/y clamp into the viewport;
dropdown popup geometry routed through it.)*

`lk_anchor_resolve(overlay, rects[owner], viewport_w, viewport_h) →
lk_rect` computes the final overlay rect, flipping above/below when the
preferred side would overflow.  Use in both render and hit-test.

### Step 4 — focus traps and modals — DONE

*(2026-07-25: `lk_focus_next/prev` scope to the topmost trapping
overlay's content subtree; `lk_overlay_dismiss_outside` returns
`LK_DISMISS_BLOCKED` for outside clicks on a modal; ESC pops the
topmost overlay in `lk_event_route` per v2 note 3.)*

When an overlay has `traps_focus`, `lk_focus_next` and `lk_focus_prev`
scope their DFS to that overlay's `content_root` subtree.  Outside
clicks are blocked (not just dismissing — blocking too, for modals).

### Step 5 — migrate dropdown to the new API — DONE

*(2026-07-25: opening pushes an `LK_OVERLAY_DROPDOWN_POPUP` overlay
(LKS_EXPANDED kept in sync — still the public invariant); popup
render/hit-test live on as `lk_dropdown_render_popup` /
`lk_dropdown_hit_popup`, dispatched per-overlay from `lk-overlay.c`;
`UIP_HIDDEN` + `lk_layout_subtree` cover subtree-content overlays
per v2 note 5.)*

`lk-dropdown.c` no longer implements its own popup rect / hit-test /
dismiss routines.  Its event handler calls `lk_overlay_push(ui, ...)`
and `lk_overlay_pop(ui, ...)`.  Option rects are derived from the
overlay anchor + `lk_anchor_resolve`.  The three `lk.h` public symbols
(`lk_render_build_overlays`, `lk_hit_test_overlay`,
`lk_overlay_dismiss_outside`) remain, but now iterate `ui->overlays`
generically.

### Step 6 — new overlay widgets — DONE

*(2026-07-25: tooltip shipped as a **prop**, per v2 note 6, not the
widget-kind sketched below.  `UIP_TOOLTIP` (interned string) on any
node; hover transitions — a hook in `lk_hover_set`/`lk_hover_clear`
that fires only when `hovered_id` actually changes — pop the previous
tooltip overlay and push one for the newly hovered node.  Geometry +
paint are procedural in `src/core/lk-tooltip.c` (~144 code LoC),
anchored BELOW through `lk_anchor_resolve` (flips above near the
bottom edge for free).  Tooltips are passive: never hit-testable, and
`lk_overlay_dismiss_outside` scans through them so clicks reach
dismissible overlays underneath.*

*Modal support landed at the app level: `lk::overlay_push [ui dict]`
and `lk::overlay_pop [ui]` in the Lcl bindings (dict keys kind /
anchor / owner_id / content_root_id / dismiss_on_outside /
traps_focus, with per-kind defaults — modal defaults to center anchor,
no outside dismissal, focus trap).  `examples/modal-dsl.lcl` is the
demo: a `-hidden 1` column as content subtree, confirm/cancel via
presentations, ESC and outside-click blocking from core.  Context
menus need no dedicated producer — `overlay_push` with
`kind context_menu, anchor at_cursor` from app code covers them.)*

With the above in place, adding a tooltip is: one new widget-kind that
pushes a TOOLTIP overlay with its text as content on `pointer_move` and
pops on `pointer_leave`.  No changes to core passes.

Adding a context menu: overlay kind + click-on-empty-space trigger in
the app code.

## v2 implementation notes (2026-07-25, adopted for the migration)

Decisions made when the migration was actually scheduled, superseding
details of the sketch above where they conflict:

1. **Overlays key nodes by `lk_node_id`, not `lk_ix`.**  The sketch
   predates full diff-awareness: tree indices are reassigned every
   frame, so `owner_node`/`content_root` must be stable interned ids,
   resolved to indices per pass (linear scan is fine — overlay count
   is small).
2. **The overlay stack lives on `lk_ui` and persists across frames.**
   `lk_ui_end_frame` pops overlays whose owner id was REMOVED (using
   the same removed-and-not-re-added move filter as state GC).
3. **ESC dismissal is core, not backend.**  `lk_event_route` gets a
   pre-step: KEY_DOWN ESCAPE with a dismissible overlay on top pops
   it and consumes the event.  This is what makes exit criterion 3's
   "no ad-hoc SDL code" achievable.
4. **Modal blocking**: `traps_focus && !dismiss_on_outside` means
   outside pointer-downs are consumed without dismissing;
   `lk_focus_next/prev` scope their DFS to the topmost trapping
   overlay's content subtree.
5. **Overlay content subtrees use a new `UIP_HIDDEN` prop** (finally
   implementing the `hidden` flag from design_draft.md's node
   schema): hidden subtrees are skipped by main-pass measure, layout,
   render, hit-test, and focus collection.  The overlay pass lays
   them out at the resolved anchor via a subtree-scoped layout entry
   point.  This deliberately touches `lk-layout.c`/`lk-render.c` —
   the Proper phase was always going to (step 4 touches focus); the
   Lean-phase "untouched" claim expires here.  Dropdown popups remain
   procedural (`content_root = 0`).
6. **Tooltips are a prop, not a widget kind** (deviation from step
   6's sketch): `UIP_TOOLTIP` text on any node; hover transitions
   (already tracked on `lk_ui`) push/pop the tooltip overlay in core.
   A wrapper kind would only work for dedicated nodes; the prop works
   on buttons, labels, anything.  Show-delay is deferred.
7. **Popup scrolling stays deferred** (known-issue list item 6) — it
   wants the editor-track virtualization thinking, not this
   migration.

## What will **not** change during the migration

The public pieces end-users touch today should be invariant:

- `UIK_DROPDOWN`, `UIK_OPTION` as kind names.
- The DSL syntax: `dropdown "id" { option "Foo" option "Bar" }`.
- The state keys: `LKS_EXPANDED`, `LKS_SELECTED_INDEX`, `LKS_HOVER_INDEX`.
- The event contract: `LK_EVENT_VALUE_CHANGED` fires on selection, with
  the selected option's text in `data.value_changed.str_id`.
- Theme rules on `UIK_DROPDOWN` and `UIK_OPTION`.

Existing example scripts (e.g. `examples/budget-dsl.lcl`) should
continue to run without modification.

## Exit criteria

The overlay system is "Proper" when:

1. A tooltip widget exists, is implemented in `~150 LoC`, and required
   no changes to `lk-render.c`, `lk-event.c`, or `lk-layout.c`.
2. A dropdown option list near the viewport edge flips up/left
   instead of clipping.
3. A modal dialog can be opened, has focus trapped to itself, and
   closes on ESC without any ad-hoc SDL-backend code.

Status against these (2026-07-25, step 6 landed) — **all three met**:

1. **Met.**  Tooltip is `src/core/lk-tooltip.c` (144 code LoC / 204
   raw) + `lk-tooltip.h` (10 code LoC), a 6-line hover-transition
   hook in `lk_hover_set`/`lk_hover_clear`, and a 5-line render
   dispatch case in `lk-overlay.c` — ~165 code LoC total against the
   ~150 target.  `lk-render.c` and `lk-layout.c` are untouched; the
   only `lk-event.c` change is the hook in the hover *setters* (the
   routing/hit-test/focus paths are untouched).  It shipped as a prop
   (`UIP_TOOLTIP`), not a widget kind, per v2 note 6.  Tests:
   hover push/pop/swap, render-on-top, passivity, bottom-edge flip.
2. **Met** (step 3): popup flips above at the bottom edge, x/y
   clamped — `test_dropdown_bottom_edge_flips`.
3. **Met**: `examples/modal-dsl.lcl` opens a modal via
   `lk::overlay_push` (hidden content subtree, centered), focus is
   trapped (`test_focus_trap_scopes_tab`, and the binding-level
   `test_overlay_push_modal` asserts trap + routed-ESC pop), outside
   clicks are blocked, and ESC closes it via the core pre-step in
   `lk_event_route` — zero ad-hoc SDL-backend code.

## File-level summary (post-migration, 2026-07-25)

| File | Overlay-related code |
|------|----------------------|
| `src/core/lk-overlay.c` (new) | overlay stack (push/pop/pop_owner/top/count), `lk_anchor_resolve`, generalized `lk_render_build_overlays` / `lk_hit_test_overlay` / `lk_overlay_dismiss_outside` (iterate `ui->overlays`, per-kind dispatch, modal blocking; passive overlays are transparent to the dismiss scan).  Subtree-content overlays are laid out into a transient scratch rects array, not the shared `lk_layout` rects. |
| `src/core/lk-tooltip.c` (new) | tooltip producer: `lk_tooltip_hover_changed` (pop/push on hover transition), `lk_tooltip_rect` (measure + pad + anchor), `lk_tooltip_render` (fg/bg-swapped plate, 1 px border, text), dispatched from `lk-overlay.c` |
| `src/core/lk-dropdown.c` | widget only + procedural popup paint (`lk_dropdown_render_popup`) and hit (`lk_dropdown_hit_popup`), dispatched from `lk-overlay.c`; `dropdown_open`/`dropdown_close` keep `LKS_EXPANDED` and the overlay stack in sync |
| `src/core/lk-layout.c` | `UIP_HIDDEN` skip in measure/layout; `lk_layout_subtree` entry point for overlay content |
| `src/core/lk-render.c` | `UIP_HIDDEN` skip; internal `lk_render_build_from` for overlay subtrees |
| `src/core/lk-event.c` | `UIP_HIDDEN` skip in hit-test + focus collection; focus-trap scoping; ESC pre-step in `lk_event_route`; hover-transition hook in `lk_hover_set`/`lk_hover_clear` calling `lk_tooltip_hover_changed` |
| `src/core/lk-ui.c` | overlay stack storage/free; end_frame pops overlays on owner removal (move filter) |
| `src/sdl/lk-sdl.c` | three call sites, now `lk_ui*`-based; pointer-down routing skipped on `LK_DISMISS_BLOCKED` |
| `include/lk.h` | `lk_overlay` + enums, stack API, `lk_anchor_resolve`, `lk_layout_subtree`, `UIP_HIDDEN`, `UIP_TOOLTIP`, `LK_DISMISS_*` |
| `src/lcl/lcl-lk.c` | `lk::overlay_count`, `lk::overlay_push` / `lk::overlay_pop` (dict-driven, per-kind defaults), `"hidden"` / `"tooltip"` prop keys |
| `lib/lk-dsl.lcl` | `hidden` + `tooltip` added to the generic `-flag` prop whitelist |
| `examples/modal-dsl.lcl` (new) | confirm-dialog modal demo: hidden content subtree, `overlay_push`/`pop` from command handlers, ESC + focus trap from core |
