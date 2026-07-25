# Overlays — Current State (Lean) and Path to Proper

Status: **Lean, dropdown-only**.  Introduced alongside `UIK_DROPDOWN` in
the MVP-1.0 milestone.  This document names the debt so it is not lost.

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

### Step 1 — introduce `lk_overlay`

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

### Step 2 — hoist registration out of the widget walk

`lk_render_build_overlays` and `lk_hit_test_overlay` should no longer
special-case `UIK_DROPDOWN`.  Instead they iterate `ui->overlays`.
Widgets that want to open an overlay push onto that list during their
event handler (e.g. dropdown's pointer-down handler pushes an overlay
with `kind = DROPDOWN_POPUP`, `owner_node = dd`, `anchor_mode = BELOW`).
Pop on close.

### Step 3 — viewport-edge clamping

`lk_anchor_resolve(overlay, rects[owner], viewport_w, viewport_h) →
lk_rect` computes the final overlay rect, flipping above/below when the
preferred side would overflow.  Use in both render and hit-test.

### Step 4 — focus traps and modals

When an overlay has `traps_focus`, `lk_focus_next` and `lk_focus_prev`
scope their DFS to that overlay's `content_root` subtree.  Outside
clicks are blocked (not just dismissing — blocking too, for modals).

### Step 5 — migrate dropdown to the new API

`lk-dropdown.c` no longer implements its own popup rect / hit-test /
dismiss routines.  Its event handler calls `lk_overlay_push(ui, ...)`
and `lk_overlay_pop(ui, ...)`.  Option rects are derived from the
overlay anchor + `lk_anchor_resolve`.  The three `lk.h` public symbols
(`lk_render_build_overlays`, `lk_hit_test_overlay`,
`lk_overlay_dismiss_outside`) remain, but now iterate `ui->overlays`
generically.

### Step 6 — new overlay widgets

With the above in place, adding a tooltip is: one new widget-kind that
pushes a TOOLTIP overlay with its text as content on `pointer_move` and
pops on `pointer_leave`.  No changes to core passes.

Adding a context menu: overlay kind + click-on-empty-space trigger in
the app code.

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

Until then: the three overlay functions stay dropdown-flavored, and
adding a second overlay producer means either accepting parallel
dropdown-shaped code (bad) or taking the generalization step (good).

## File-level summary of Lean debt

| File | Overlay-specific code | Migration difficulty |
|------|-----------------------|----------------------|
| `src/core/lk-dropdown.c` | all of it — widget + popup geometry + overlay render/hit-test/dismiss | replace ~200 LoC of overlay machinery with calls into `ui->overlays` |
| `src/sdl/lk-sdl.c` | three added call sites (2 hit-test, 1 render, 1 dismiss) | unchanged — same calls still work, just with new implementations |
| `include/lk.h` | three public declarations (`lk_render_build_overlays`, `lk_hit_test_overlay`, `lk_overlay_dismiss_outside`) plus kinds/state keys | symbols stay; implementation behind them generalizes |
| `src/core/lk-style.c` | default theme rules for DROPDOWN/OPTION | unchanged |
| `src/core/lk-widget.c` | dropdown/option registration | unchanged |
