# Weft Gap Analysis: What Lk Needs

This document captures the gap analysis between Lk (the UI toolkit) and Weft
(a structural text editor). The goal is to reimplement Weft on Lcl/Lk. Some
gaps require C extensions alongside Lk; these are noted explicitly.

Weft lives at `submodules/weft`. Its README.md and `docs/` are the primary
references for its design.


## Weft in Brief

Weft is an Acme-inspired structural text editor with:

- **Piece-table document storage** with line index
- **Anchor-based annotation spans** that survive edits (left/right bias)
- **Decoration layers** (ephemeral per-frame styling from syntax highlighting,
  search results, plumber patterns, etc.)
- **Tiled pane layout** (binary split tree with draggable dividers)
- **Per-character styled text** (fg, bg, underline from overlapping layers)
- **Lcl scripting** (~100 procs in the `weft::` namespace)
- **Acme-style interaction** (middle-click execution, annotation handlers)
- **Tree-sitter syntax highlighting** (incremental parse, AST-based)
- **Search/replace, go-to-line, transform prompt** dialogs
- **REPL** with buffer attachment
- **Undo/redo**, clipboard, configurable keybindings, file-type associations


## Toolkit-Level Gaps (Lk Core)

### 1. Text Input Widget (single-line)

**Already on roadmap (Phase 5.5).** Foundation for all text entry.

Used in Weft for: search bar query/replace fields, go-to-line numeric input,
transform prompt input, REPL command line.

Needs: cursor positioning, text event consumption, selection (shift+arrows,
click-drag), clipboard (cut/copy/paste), basic key handling (home/end,
backspace/delete, ctrl+arrows for word movement).

### 2. Scroll Container

**Already on roadmap (Phase 5.5).** Required for any content larger than the
viewport.

Needs: scroll offset in `lk_state`, wheel event handling, viewport clipping
(only render/layout visible children), optional scroll bar.

### 3. Per-Character Styled Text

**Architecturally significant.** Lk currently renders labels as single-style
text spans. Weft needs each character to potentially have different fg, bg,
and underline attributes, driven by overlapping decoration layers.

Options:
- A `STYLED_TEXT` widget that accepts an array of `(start, end, style)` runs
- A richer `LABEL` that accepts inline style spans
- A `TEXT_AREA` widget (see #5) that subsumes this

The decoration *merging* logic (multiple layers -> per-character style) is
application-level. Lk just needs to render the result.

### 4. Resizable Split Layout

Weft's tiled panes use a binary split tree. Each split has:
- Direction (horizontal or vertical)
- Adjustable ratio (0.1-0.9), persisted across frames
- 4px draggable divider between children

Lk's `row`/`column` use intrinsic sizing + stretch, not proportional splits.
No draggable dividers, no runtime ratio adjustment.

Options:
- New `SPLIT_H`/`SPLIT_V` widget kinds with ratio stored in `lk_state`
- A "splitter/divider" child widget that handles drag interaction
- Dynamic tree mutation (splitting/closing panes) maps naturally to Lk's
  per-frame tree rebuild; the split *state* (ratios) persists in `lk_state`

### 5. Text Area Widget (multi-line)

The heart of Weft. A multi-line text display with:
- Cursor (blinking vertical bar at a character position)
- Selection (linear + column/block mode, click-drag)
- Viewport scrolling (scroll offset, only render visible lines)
- Line wrapping (visual line cache)
- Tab expansion (configurable tab size)
- Per-character styling (see #3)
- Line number gutter (auto-width, current line highlighted)
- Hit-testing: pixel (x, y) -> document byte offset

This is likely a **C extension widget** registered via `lk_widget_register`
rather than a built-in. The document model (piece table, anchors, undo) lives
in the extension; Lk provides the rendering surface and event routing.

### 6. Overlays / Dialogs

Weft has three overlay patterns:
- **Docked bar** (search bar at top of pane, pushes content down)
- **Centered modal** (go-to-line: 200x60, transform prompt: 350x80)
- **Focus indicator** (borders on focused pane, REPL target indicator)

Lk has no overlay/z-ordering concept. Options:
- Docked bars can be modeled as conditional children (a row that appears/
  disappears in the tree based on state)
- Centered modals need either absolute positioning or a dedicated overlay
  container kind
- Focus indicators could be border styles driven by node state (focused/
  hovered — Lk's style system already has state matching)

### 7. Style System Extensions

Lk's `lk_style` struct has `border_width`, `border_color`, `border_radius`
fields but they may not be wired through rendering. Weft needs:

- **Underline** — for clickable links (plumber `file:line` patterns). Add
  `underline` flag to `lk_style`.
- **Border rendering** — focus indicator (blue bottom+side borders), REPL
  target (red full border). Wire existing border fields through render.
- **Cursor style** — blinking vertical bar. Likely widget-level, not style.

### 8. Input System Extensions

- **Clipboard API** — Lk has no clipboard abstraction. SDL provides
  `SDL_GetClipboardText`/`SDL_SetClipboardText`; Lk needs either a public
  API or Lcl bindings (`lk::clipboard_get`, `lk::clipboard_set`).
- **Mouse drag state** — Weft uses click-drag for text selection and divider
  resizing. Lk has pointer_down/move/up but no built-in drag tracking. May
  be sufficient as-is if widgets track their own drag state.
- **Modifier matching in translators** — Weft's keybindings map
  `Ctrl+F` -> search, `Ctrl+G` -> goto, etc. Lk's translators currently
  match on `(event_type, ptype, node_kind)` but not on key+modifiers. Either
  extend translators or handle keybindings at the application level.


## Application-Level (C Extensions, Not Lk Core)

These are Weft-specific and would be built as C extensions alongside Lk,
registered as custom widget kinds and Lcl procs.

### Document Model
- **Piece table** — immutable original + append-only add buffer, piece array
- **Line index** — byte offsets of line starts, rebuilt on edit
- **Undo/redo** — edit operation stack with inverse replay

### Annotation Store
- **Anchors with bias** — positions that transform on insert/delete
  (left-biased stays put, right-biased moves with insertion)
- **Annotation records** — (start_anchor, end_anchor, layer, metadata[])
- **Layer dirty tracking** — document revision-based cache invalidation
- **Persistence** — context-based re-anchoring (before/after text snippets,
  fuzzy matching, orphan handling)

### Decoration / Layer Stack
- **ViewLayer abstraction** — render callback producing ephemeral decorations
  for a viewport range
- **Layer stack** — priority-ordered list of ViewLayers per pane
- **Decoration merging** — combine overlapping layers into per-character style
- **Cache** — track document revision, skip recompute if unchanged

### Tree-sitter Integration
- **Incremental parsing** — reparse only changed regions
- **Query execution** — highlight captures -> decoration spans
- **Language registry** — dynamic loading of grammars

### Interaction Dispatcher
- **Acme-style dispatch** — middle-click checks: layer decorations with
  actions -> annotation handlers -> global handlers (priority-ordered)
- **Context population** — selection text, word-at-pos, modifiers

### Other
- Plumber (file:line pattern matching -> clickable decorations)
- Transform system (named operations -> patch preview -> apply/reject)
- REPL with buffer attachment
- Configuration system (file-type associations, indent settings, keybindings)


## Priority Order

| # | Gap | Category | Notes |
|---|-----|----------|-------|
| 1 | Text input (single-line) | Lk core | Phase 5.5, foundation for dialogs |
| 2 | Scroll container | Lk core | Phase 5.5, required for large content |
| 3 | Per-char styled text | Lk core | Core rendering primitive for editor |
| 4 | Resizable split layout | Lk core | Tiled panes, draggable dividers |
| 5 | Text area (multi-line) | C extension | Custom widget via lk_widget_register |
| 6 | Overlays / positioning | Lk core | Search bar, goto dialog, prompt |
| 7 | Underline + border render | Lk core | Style polish, wire existing fields |
| 8 | Clipboard API | Lk core | Copy/paste abstraction |
| 9 | Modifier-aware keybindings | Either | Extend translators or app-level |


## Architectural Alignment

The good news: Lk's existing architecture maps well to Weft's needs.

- **Retained tree + diffing** — Weft rebuilds its pane tree each frame;
  Lk's begin_frame/end_frame/changeset model is a natural fit.
- **Style resolution from themes** — Weft's annotation layers map to Lk's
  style rules (kind/tag/state matching). Per-character styling goes beyond
  this but the infrastructure is extensible.
- **Widget vtable** — `lk_widget_register` lets Weft register its own
  `TEXT_AREA` kind with custom measure/layout/render, keeping Lk core lean.
- **Command/translator pattern** — Weft's interaction dispatcher (click on
  presentation -> named command) maps directly to Lk's CLIM-style
  translator/command system.
- **Lcl bindings** — The `lcl_register_lk` pattern shows how to add new
  namespaces. Weft would register `weft::doc`, `weft::annot`, etc. as
  additional Lcl proc sets.
- **Retained state** — `lk_state` (keyed by node_id + state_key) can hold
  cursor positions, scroll offsets, split ratios — all the per-widget state
  that needs to survive across frames.

The main gaps are in **rendering primitives** (styled text, scroll, splits)
rather than the **architectural model**.
