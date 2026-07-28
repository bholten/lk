# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

**lk** is the C core of a UI toolkit called "Lcl" — a retained semantic UI tree engine inspired by Tk's ergonomics and McCLIM's presentation/command semantics. It is designed to be embeddable, cross-platform, and suitable for single statically-compiled executables. SDL3 is the platform backend (integrated, optional at build time).

The design documents in `docs/` are authoritative for understanding the project's direction. Key concepts: nodes have stable IDs (interned strings), a CLIM-style presentation system attaches semantic meaning to nodes, and commands (not raw callbacks) are the primary action model.

## Build

```bash
cmake -B build && cmake --build build
```

Targets: `lk_core` (static library), `lk_shared` (shared library), `lk_test` (test harness at `build/lk_test`).

Optional SDL3 targets (auto-detected): `lk_sdl`, `lk_demo`.

Optional Lcl scripting bindings:
```bash
cmake -B build -DLK_BUILD_LCL=ON && cmake --build build
```
Adds: `lcl_lk` (static library), `lcl_lk_test` (binding tests at `build/lcl_lk_test`), `lcl_lk_main` (standalone script runner, SDL-conditional).

The project is **C89** (`-std=c90 -pedantic -Wall -Wextra`). No C99/C11 features — declare variables at block top, use `/* */` comments only. Exception: SDL-dependent files (`src/sdl/`, `src/lcl/lcl-lk-main.c`) use C11 because SDL3 headers require it.

## Architecture

### Core Data Model (`src/core/`)

The entire runtime centers on `lk_tree` — an arena-allocated retained tree of `lk_node` structs.

- **lk.h** (`include/lk.h`) — The single public header defining all core types. This is the primary file to read when understanding the codebase.
- **lk-memory.h** (`src/core/lk-memory.h`) — Internal allocator interface. Trees carry their own `alloc`/`dealloc` function pointers; `lk_sys_alloc`/`lk_sys_dealloc` are the default (malloc/free) wrappers.

### UI Context and Diffing (`lk-ui.c`)

`lk_ui` is the top-level context. It owns two `lk_tree`s (`prev` and `next`) sharing a single `lk_intern` table. The frame lifecycle:

1. `lk_ui_begin_frame(ui)` — resets `next`, returns it for building
2. Host builds the tree using `lk_tree_add_node_s`, `lk_tree_append_child`, `lk_tree_add_prop`
3. `lk_ui_end_frame(ui)` — diffs `prev` vs `next`, swaps them, returns a `lk_changeset`
4. `lk_ui_tree(ui)` — returns the current tree (for rendering, hit-testing, etc.)

The diff matches children by `lk_node_id` (interned, so comparison is a u32 check). Changeset entries are ADDED, REMOVED, or UPDATED. A node "move" across parents appears as REMOVED + ADDED.

### Key Types

- **`lk_tree`** — Arena-backed node tree. Nodes are stored in a flat `lk_node` array (indices 1..N; index 0 is sentinel/null). Props are stored in a separate contiguous `lk_prop` array; each node holds an `(offset, len)` slice into it. Props for a given node must be added contiguously (no interleaving with other nodes' props), but all nodes may be created first and props grouped per-node afterward (lazy `props_off` initialization).
- **`lk_node`** — Has `id` (interned u32), `kind` (enum), `parent`/`first_child`/`next_sibling` (index-based adjacency), and a props slice.
- **`lk_intern`** — String interning table (FNV-1a hash, open addressing, string pool). Maps strings to stable `lk_node_id` (u32, starting at 1). Supports reverse lookup via `by_id` array.
- **`lk_value`** — Tagged union: `NONE`, `BOOL`, `I32`, `STR` (where STR stores an interned `lk_u32` string ID, not a raw pointer), `RESOURCE` (an 8-byte `{id, generation}` `lk_resource_ref` — no pointer in the value; see the Editor Track section).
- **`lk_kind`** — Node kinds: `WINDOW`, `ROW`, `COLUMN`, `SPACER`, `LABEL`, `BUTTON`, `TEXT_INPUT`, `SCROLL`, `DROPDOWN`, `OPTION`, `SPLIT_H`, `SPLIT_V`, `EDITOR`.
- **`lk_prop_key`** — Property keys: `TEXT`, `FOCUSABLE`, `DISABLED`, `W`, `H`, `PADDING`, `GAP`, `ALIGN`, `JUSTIFY`, `HIDDEN` (bool — subtree skipped by main measure/layout/render/hit-test/focus passes; used for overlay content subtrees), `TOOLTIP` (string, interned like TEXT — hover help text; hover transitions push/pop an `LK_OVERLAY_TOOLTIP` overlay, see `lk-tooltip.c`), `EDITOR` (`UIV_RESOURCE` ref to an application-owned `lk_editor`).

### Style and Theme System (`lk-style.c`)

Resolved styles drive appearance. The theme owns an ordered list of rules; `lk_style_resolve` walks them per-node.

- **`lk_style`** — Per-node resolved style: `fg`, `bg`, `border_color`, `scrollbar_track`, `scrollbar_thumb` (lk_color), `padding`, `gap`, `border_width`, `border_radius`, `align`, `justify`, `font_id`, `font_size`.
- **`lk_theme`** — Owns an array of `lk_theme_rule` entries. `lk_theme_new` includes a default dark theme. `lk_theme_add_rule` appends user rules that layer on top.
- **`lk_theme_rule`** — Matches on kind (0=any, `UIK_*`=specific), tag (0=any), state mask. Field mask tracks which style fields the rule sets.
- **Rule priority** — Later rules win. User rules added after defaults override them.
- **Style resolution** — `lk_style_resolve(theme, tree, node_states, styles[])` fills a parallel `lk_style[]` array. Inheritance pass propagates `fg` and `font` top-down from parent.
- **Widget integration** — Widget measure/layout functions read padding/gap/align/justify from `lk_layout_cfg.styles` when available (falling back to tree props for backwards compat). Render functions use `style->bg` and `style->fg` directly.

### Widget System (`lk-widget.c`)

Per-kind behavior (measure, layout, render) is abstracted behind `lk_widget_def` vtables, dispatched by kind.

- **`lk_widget_def`** — struct with function pointers: `measure(t, n, sizes, cfg)`, `layout(t, n, sizes, content, cfg, rects)`, `render(t, n, rect, style, state, out)`, `event(ui, t, n, ev)`, plus a `clips` flag. `layout = NULL` means leaf node (engine skips child recursion for that kind). The `cfg` parameter carries resolved styles and state; measure/layout read padding/gap/align/justify from `cfg->styles[n]` when available. The `event` handler participates in the two-tier event routing model (see below); returns 1 if handled.
- **Global registry** — `lk_widget_def g_widgets[LK_KIND_MAX]` (static array, lazy-initialized on first `lk_widget_get` call). No explicit init required.
- **`lk_widget_register(kind, def)`** — override or register new kinds at runtime.
- **`lk_widget_get(kind)`** — returns the `lk_widget_def*` for a kind.
- **`layout_stack(axis)`** — internal shared function that unifies column (axis=0) and row (axis=1) layout into one parameterized implementation.
- **Engine owns traversal**: DFS, padding-to-content-rect, W/H override, and stack-push all stay in `lk-layout.c`/`lk-render.c`. Widgets only compute intrinsics, position children, and emit render commands.
- **Code organization**: Simple built-in widgets (WINDOW, COLUMN, ROW, SPACER, LABEL, BUTTON) live in `lk-widget.c`. Complex interactive widgets get their own files (e.g., `lk-text-input.c`).

### Text Input Widget (`lk-text-input.c`)

Single-line text input with cursor, selection, key handling, and click-to-position. Leaf node (no children). UTF-8 correct: all cursor motion and deletion is codepoint-wise via the `src/core/lk-utf8.h` helpers (`lk_utf8_next`/`prev`/`is_boundary` — internal header, shared with the text stub and the future editor widget).

- **Text buffer**: Stored as interned string in `lk_state` under `LKS_TEXT_BUF`. Each edit interns a new string. Host feeds edited text back as `UIP_TEXT` each frame.
- **Cursor/selection**: `LKS_CURSOR_POS` (byte index, always codepoint-boundary-aligned), `LKS_SELECTION_START`/`LKS_SELECTION_END`. Cursor and selection pixel x-offsets are computed during measure via the text backend's `x_from_index` and stored in `LKS_CURSOR_X` / `LKS_SEL_X0` / `LKS_SEL_X1` (render has no backend access; derived-geometry-in-state is on the design-coherence list in docs/TODO.md).
- **Event handling**: Consumes `LK_EVENT_TEXT` (insert), `LK_EVENT_KEY_DOWN` (BACKSPACE/DELETE remove whole codepoints, LEFT/RIGHT move by codepoint, HOME, END, CTRL+A/C/X/V), and `LK_EVENT_POINTER_DOWN` (click-to-position). TAB/RETURN/ESCAPE bubble to app.
- **Click-to-position**: Maps pointer x to a byte index via `ui->text->index_from_x` (nearest boundary, clamped). Requires `lk_ui_set_text_backend(ui, backend)` — the SDL run loop installs the same backend it puts in `lk_layout_cfg.text`; NULL disables the feature (the event then bubbles so host click-to-focus still works). Text origin x and resolved font are stashed in state (`LKS_TEXT_ORIGIN_X`, `LKS_FONT_ID`, `LKS_FONT_SIZE`) by `lk_text_input_store_geometry`, called from `lk_layout` (same pattern as the dropdown trigger-rect stash). Since the widget consumes the click, it calls `lk_focus_set` itself.
- **Rendering**: Background FILL_RECT, selection highlight FILL_RECT (exact rect from the stashed `LKS_SEL_X0`/`X1`), DRAW_TEXT, cursor bar FILL_RECT.
- **Max buffer**: `LK_TEXT_INPUT_MAX` = 1024 bytes. Insert and paste truncate at a codepoint boundary at the cap — a UTF-8 sequence is never split.

### Scroll Widget (`lk-scroll.c`)

Scroll container that clips children and scrolls vertically via wheel events. Children stacked vertically like a column.

- **Scroll offset**: Stored as `LKS_SCROLL_Y` in `lk_state`. Max offset in `LKS_SCROLL_MAX`. Both computed during layout.
- **Event handling**: Consumes `LK_EVENT_WHEEL` events (via bubbling — wheel on a child is handled by the scroll ancestor). Adjusts `LKS_SCROLL_Y` by `SCROLL_STEP` (30px).
- **Rendering**: Background FILL_RECT, scroll bar track + thumb (colors from `style->scrollbar_track` / `style->scrollbar_thumb`) when content overflows, CLIP_BEGIN/CLIP_END for child clipping.
- **Layout**: Scroll bar reduces available width by `SCROLL_BAR_W` (6px) when content exceeds viewport.

### Split Panes (`lk-split.c`)

Resizable two-pane containers with a draggable divider. `UIK_SPLIT_H` places two children side-by-side (vertical divider); `UIK_SPLIT_V` stacks them (horizontal divider).

- **Ratio**: per-mille `lk_i32` (0..1000; `lk_value` has no float). Priority: `LKS_SPLIT_RATIO` state (written by dragging) > `UIP_SPLIT_RATIO` prop (host-set initial value — initial values are props, not state pokes) > default 500. Layout clamps so each pane keeps >= `SPLIT_MIN_PANE` (40 px) when the axis is big enough; the divider band is `SPLIT_DIVIDER_W` (5 px).
- **Divider band**: owned by the split node itself, NOT a separate node — hit-testing a point in the band lands on the split since no child covers it. Render derives band geometry from the node's own laid-out rect (never from ancestors); events use the content rect stashed in state by `lk_layout` (`LKS_SPLIT_CX/CY/CW/CH`, same coherence-debt pattern as `LKS_TRIGGER_*`).
- **Degradation**: one visible (non-`UIP_HIDDEN`) child fills the whole content rect; zero children renders bg only; children beyond the first two are ignored (zero rects).
- **Dragging**: POINTER_DOWN in the band → `lk_capture_set` + `LKS_SPLIT_DRAGGING=1`; POINTER_MOVE while dragging maps pointer position within the stashed content rect to a clamped per-mille ratio (divider center anchored under the cursor); POINTER_UP releases. Non-drag pointer events pass through so pane clicks are unaffected.
- **Theme**: near-transparent bg; `style->border_color` doubles as the divider color.

### Overlay System (`lk-overlay.c`), Dropdown Widget (`lk-dropdown.c`), Tooltips (`lk-tooltip.c`)

Generalized overlay machinery (docs/overlays.md steps 1–6, "Proper"). An overlay draws after the main tree, is hit-tested before it, and belongs to an owner node.

- **`lk_overlay`** — `{kind, anchor_mode, dismiss_on_outside, traps_focus, owner_id, content_root_id, offset}`. Nodes referenced by stable `lk_node_id` (tree indices are reassigned every frame), resolved per pass via `lk_tree_find_by_id`. Kinds: `DROPDOWN_POPUP`, `TOOLTIP`, `CONTEXT_MENU`, `MODAL`. Anchors: `BELOW`, `ABOVE`, `AT_CURSOR` (offset.x/y = cursor point), `CENTER_VIEWPORT`.
- **Overlay stack on `lk_ui`** — `overlays/overlay_count/overlay_cap`, topmost = last. Persists across frames; `lk_ui_end_frame` pops overlays whose owner was REMOVED and not re-ADDED (same move filter as state GC / focus-clear, shared helper `cs_id_removed_not_readded` in lk-ui.c). API: `lk_overlay_push/pop/pop_owner/top/count`.
- **`lk_anchor_resolve(ov, owner_rect, vw, vh, content_w, content_h)`** — final overlay rect. BELOW flips above when overflowing the bottom with room above (ABOVE flips symmetrically); result clamped into the viewport on both axes; `offset.w/h` override content size when non-zero; vw/vh = 0 skips clamping on that axis.
- **Three overlay passes** (public, take `lk_ui*` + `lk_layout_cfg*`): `lk_render_build_overlays` (bottom-to-top, draws over the main list), `lk_hit_test_overlay` (top-to-bottom, before `lk_hit_test`), `lk_overlay_dismiss_outside` (pointer-down: pops dismissible overlays not containing the point — syncing dropdown `LKS_EXPANDED`; returns `LK_DISMISS_NONE/DISMISSED/BLOCKED`; BLOCKED = a modal (`traps_focus && !dismiss_on_outside`) consumed the click and the caller must not route the event).
- **Content models** — Procedural (`content_root_id == 0`): per-kind dispatch; DROPDOWN_POPUP delegates to `lk_dropdown_render_popup`/`lk_dropdown_hit_popup`. Subtree (`content_root_id != 0`): a `UIP_HIDDEN` subtree in the main tree is measured/laid out at the resolved anchor via `lk_layout_subtree` into a transient scratch rects array (the shared `lk_layout` rects stay untouched), rendered via internal `lk_render_build_from` (lk-overlay.h).
- **`lk_layout_subtree(t, cfg, subtree_root, ox, oy, rects)`** — measures + lays out just that subtree with its root at (ox, oy); ignores `UIP_HIDDEN` on the root itself, still skips hidden descendants; zeroes the subtree's rect slots first.
- **ESC in core** — pre-step in `lk_event_route`: KEY_DOWN + ESCAPE with a non-empty overlay stack pops the topmost overlay (any kind; dropdowns get `LKS_EXPANDED` cleared) and marks the event handled before widget/user dispatch.
- **Focus traps** — `lk_focus_next/prev` scope their collection DFS to the topmost trapping overlay's content subtree (which is hidden, but exempt from the hidden-skip while it is the active trap root).
- **Dropdown** — `UIK_DROPDOWN` (leaf in main layout) + `UIK_OPTION` children. Opening (click/Down/Return/Space) pushes a DROPDOWN_POPUP overlay AND sets `LKS_EXPANDED` (public invariant — kept in sync via `dropdown_open`/`dropdown_close`). Popup geometry goes through `lk_anchor_resolve`, so it flips above near the bottom edge instead of clipping. State: `LKS_SELECTED_INDEX`, `LKS_HOVER_INDEX`, `LKS_EXPANDED`, `LKS_TRIGGER_*` (trigger rect stashed by `lk_layout`). Selection emits `LK_EVENT_VALUE_CHANGED` with the option text in `data.value_changed.str_id`.
- **Tooltips** (`lk-tooltip.c`) — a prop, not a widget kind: `UIP_TOOLTIP` (interned string) on any node. Hover *transitions* (hook in `lk_hover_set`/`lk_hover_clear`, fires only when `hovered_id` actually changes) pop any existing TOOLTIP overlay and push one for the newly hovered node if it carries the prop (owner_id = node id, BELOW anchor — flips/clamps via `lk_anchor_resolve` — procedural content). Tooltips are fully passive: `lk_hit_test_overlay` never returns them, and `lk_overlay_dismiss_outside` scans *through* passive (non-dismissible, non-modal) overlays so clicks reach dismissible overlays underneath. Render: fg/bg-swapped plate from the owner's resolved style + 1 px border + text (`LK_TOOLTIP_PAD` = 5). Internal API in `src/core/lk-tooltip.h` (`lk_tooltip_hover_changed`, `lk_tooltip_rect`, `lk_tooltip_render`); no public lk.h surface beyond the prop key.
- **App-level overlays from scripts** — `lk::overlay_push [ui dict]` (dict: `kind` "dropdown_popup"|"tooltip"|"context_menu"|"modal" required; `anchor` "below"|"above"|"at_cursor"|"center", default center for modal else below; `owner_id`/`content_root_id` node-id strings; `dismiss_on_outside` default modal 0 else 1; `traps_focus` default modal 1 else 0) and `lk::overlay_pop [ui]`. A modal is: push kind=modal with a `hidden 1` content subtree — focus trap, outside-click blocking, and ESC dismissal all come from core. See `examples/modal-dsl.lcl`.

### Editor Track (`src/editor/`, headers `lk-document.h` / `lk-editor.h` / `lk-annot-store.h`)

Weft's document machinery lifted into lk plus the multi-line `UIK_EDITOR` widget (docs/editor.md, stages A–D). Everything here is **application-owned** (out-of-tree): documents, histories, editors, and annotation stores live in the application environment — never in `lk_state`, never the intern pool. The tree references an editor through a typed resource ref carried by the `UIP_EDITOR` prop. `src/editor/` compiles into `lk_core` under a first-party-module discipline (public lk API + the shared allocator header only).

- **`lk_document`** (`lk-document.c`) — piece-table document (immutable original buffer, append-only add buffer, line index). Mutation is transactional and observable: `lk_doc_begin(origin)`/`insert`/`delete`/`commit`; bare insert/delete form an implicit single-op transaction. Subscribers (`lk_doc_subscribe`) are notified **once per committed transaction** with the full `lk_doc_delta` array — deltas carry the actual bytes (valid only during the notification; listeners copy). Revision is an opaque comparable `lk_revision {hi, lo}` token — compare via `lk_revision_equal`/`before`, never compute. Pinned contracts (docs/editor.md §3.3): ≥ 1 line always, bytes stored verbatim (no UTF-8 repair, no CRLF normalization), zero-length/out-of-range mutations rejected without advancing the revision, no mutation from inside a notification.
- **`lk_edit_history`** (`lk-edit-history.c`) — undo lives outside both the view and the document. `lk_history_attach` subscribes to the document and records committed transactions (with deleted bytes for inverse replay); undo/redo replays as an `LK_ORIGIN_UNDO`/`LK_ORIGIN_REDO` transaction through the same protocol (origin-filtered so it doesn't record its own replays; redo stack clears on foreign edits). One `begin`/`commit` bracket = one undo step.
- **Resource refs** (`lk-resources.c`, stage B1) — `lk_resources` table owned by `lk_ui` (borrowed by both trees, like the intern table). `lk_resource_register(rs, type, obj, debug_name)` → `lk_resource_ref {id, generation}`; `lk_resource_get` returns NULL for stale/wrong-typed refs (type check = descriptor-pointer equality), so use-after-free is unrepresentable at this boundary. The table does not own objects — release only invalidates. `lk_tree_dump` prints `editor="name"#id`.
- **`lk_editor`** (`lk-editor.c`, stage B2) — one view over a document: cursor (codepoint-aligned byte offset), selection anchor, sticky x-pixel, anchored viewport `{top_line, y_offset}` (never an absolute pixel offset), drag state, tab settings (TAB inserts spaces, `tab_size` 4; literal `\t` renders via segment-wise tab-stop expansion). Editing semantics live in the **command layer**: `lk_editor_command(e, ui, LK_ED_*, arg)` — each editing command brackets one document transaction (origin `16 + cmd`). NULL `ui` degrades (clipboard no-ops, monospace vertical-motion fallback).
- **Widget** (`lk-editor-widget.c`) — `UIP_EDITOR` prop carries the ref; missing/stale/wrong-typed ref renders background only and bubbles events. Layout hook does all backend-dependent geometry into the editor's transient block (no derived geometry in `lk_state`); render emits `LK_ROP_DRAW_RUN` ops whose bytes are **copied into the render list's byte arena** (`lk_render_list_push_run` — the list is self-contained, no interning, cost bounded by the viewport). Events translate to commands: typing, motion ±CTRL ±SHIFT, RETURN/TAB consumed (insert), CTRL+A/C/X/V, CTRL+Z / CTRL+SHIFT+Z, click/drag via pointer capture, WHEEL consumed (owns its viewport). One editor attaches to at most one node per frame; two panes over one document = two editors sharing doc + history.
- **Styled spans** (stage C) — `lk_editor_set_spans(e, snapshot)` deep-copies a viewport-scoped `lk_edit_span_snapshot` (spans sorted, non-overlapping; flags `LK_SPAN_FG/BG/UNDERLINE`). Staleness policy: a snapshot whose revision doesn't match the document at geometry time is ignored entirely (unstyled beats misplaced); the producer re-runs off the same change notification that made it stale.
- **`lk_annot_store`** (`lk-annot-store.c`, stage C) — canonical span producer substrate: anchors with insert bias (LEFT/RIGHT) and stable ids, records tying two anchors to a layer + metadata key/values (owned copies, never interned), dirty-tracked layers, position/range/layer queries. Attaches to a document as an **ordinary subscriber** (one listener among peers) and transforms anchors from committed deltas — undo/redo transforms them again for free. Destroy dependents (history/editor/store) before the document.

### Event Routing (`lk-event.c`)

Two-tier event dispatch model. Widget handlers get first-right-of-refusal; user handlers see only unconsumed events.

1. **Widget dispatch** (target → bubble): Target node's `lk_widget_def.event` fires at TARGET phase. If not handled, walks ancestors (BUBBLE). This lets built-in widgets (text input, scroll) consume internal events (keystrokes, wheel) without leaking to user code.
2. **User handler dispatch** (capture → target → bubble): Only reached if no widget consumed. The `lk_event_handler_fn` fires in full DOM-style phases for application-level concerns (global shortcuts, modal interception).
3. **Translator dispatch**: Only reached if neither tier consumed. Walks ancestors for presentation matches, emits commands.

An overlay pre-step runs before all tiers: ESC pops the topmost overlay and consumes the event (see Overlay System above). `lk_hit_test` and focus collection skip `UIP_HIDDEN` subtrees.

**Pointer capture**: `lk_capture_set/clear/current` on `lk_ui` (stable `lk_node_id`, like focus). While set, the host event loop targets POINTER_MOVE/UP at the captured node (bypassing hit-test) and suppresses hover updates — used by split-divider drags. `lk_ui_end_frame` clears the capture when the node is REMOVED and not re-ADDED (same move filter as focus).

Key functions: `lk_event_route(ui, event)`, `lk_hit_test(tree, rects, x, y)`, `lk_hit_test_overlay(ui, rects, cfg, x, y)`, `lk_focus_set/clear/next/prev`, `lk_hover_set/clear`, `lk_capture_set/clear/current`.

### SDL3 Backend (`src/sdl/`)

Optional platform backend. Conditional on `find_package(SDL3)` and `find_package(SDL3_ttf)`.

- **lk-sdl.h** — Public API: `lk_window_create/destroy/run`, `lk_window_ui`, `lk_window_set_event_handler`. The `lk_frame_fn` callback receives a mutable `lk_tree*`; the run loop handles begin/end frame, layout, event polling, and rendering.
- **lk-sdl.c** — SDL3 integration: event translation (`sdl_to_lk_event`; `sdl_to_lk_keycode` covers TAB/RETURN/ESCAPE/BACKSPACE/DELETE/SPACE/arrows/HOME/END, A–Z, 0–9, PageUp/PageDown, and F1–F12), render list consumption, and the real `lk_text_backend` implementation (text-contract stage B). Text renders through one `TTF_TextEngine` per window (`TTF_CreateRendererTextEngine`; internal glyph atlas does the caching — the old per-string texture cache is gone) with a single reusable scratch `TTF_Text`. Fonts: a face registry (`face_paths[]`; face 0 = `lk_window_cfg.font_path`, may be absent) plus a lazily-populated `(face_id, size)` → `TTF_Font*` instance cache (linear-scan array, size 0 resolves to the window default size, fallback 16; all instances closed on window destroy). `lk_window_register_font(win, path)` (lk-sdl.h) registers a new face and returns its `font_id` (>= 1; 0 on failure — paths are verified by opening at the default size at registration). Vtable: measure via `TTF_GetStringSize`/`TTF_GetFontAscent`; `x_from_index`/`index_from_x` via `TTF_GetTextSubString`/`TTF_GetTextSubStringForPoint` on the scratch text (nearest-boundary snapping, clamped); `line_height` via `TTF_GetFontHeight`. The run loop uses the stub text backend when no face is available. Gotcha: `TTF_SetTextString` treats length 0 as "null-terminated" — empty runs must pass a literal `""`.
- **demo.c** — Fruit selector demo exercising presentations, commands, focus, and event handling.
- Run loop order: clear commands → begin_frame → frame callback → end_frame → resolve styles → layout (with state) → SDL text-input gating (engaged while a `UIK_TEXT_INPUT` or `UIK_EDITOR` is focused; IME area follows the focused rect) → poll events (hit-test targeting for pointer events — unless a pointer capture is active, in which case MOVE/UP target the captured node and hover updates are suppressed; focus targeting for key/text events; two-tier event routing via `lk_event_route`; hover state updated on pointer move; click-to-focus and tab-cycling as built-in behaviors) → **re-layout** (events mutate state after the first layout — editor documents, scroll offsets, split ratios — so a second pass keeps the frame's render consistent with what the events did; hit-testing deliberately used the pre-event rects) → render (with state). Key/text events fall back to root when nothing is focused.

### Lcl Scripting Bindings (`src/lcl/`)

Layer 1 bindings exposing lk to the Lcl scripting language (submodule at `submodules/lcl`). Built when `LK_BUILD_LCL=ON`.

- **lcl-lk.h** (`include/lcl-lk.h`) — Public header. Single entry point: `lcl_register_lk(interp)`.
- **lcl-lk.c** (`src/lcl/lcl-lk.c`) — 65 procs in the `lk` namespace (59 core + 6 SDL-conditional). `lk::register_font [win path]` returns the new `font_id` (int; 0 = failure, mirroring C — an unreadable path is not an Lcl error). Theme rules take `font_id`/`font_size` int keys in the style dict (bad values are errors). There is deliberately no DSL wrapper for `register_font` — apps call `lk::register_font` directly on the window. C89. Opaque types: `lk_ui` (with finalizer), `lk_tree` (borrowed, no finalizer), `lk_window` (wrapped in `struct lcl_lk_window` for event handler lifetime), plus the editor-track wrappers (`lk_document`, `lk_edit_history`, `lk_editor`, `lk_annot_store` — see below). Static string-to-enum lookup tables for kinds, prop keys, event types, align values, node states, overlay kinds, anchors, and editor commands. Prop value coercion dispatches on key (text/tooltip→string, focusable/disabled/hidden→bool, w/h/padding/gap→i32, align/justify→enum lookup, editor→`lk_v_resource` of the wrapper's ref; a non-editor value is an error). Overlay procs: `lk::overlay_count`, `lk::overlay_push` (dict-driven, per-kind defaults — see Overlay System above), `lk::overlay_pop`. Event handler bridge marshals `lk_event` fields into an Lcl dict (including `target_id` and `node_id` string fields resolved from the intern table) and calls the user's proc. Command handler bridge converts `lk_command` to an Lcl dict (via `command_to_dict`, plus `source_node_id`) and calls the user's command handler proc.
- **Editor-track bindings** (stage D, in `lcl-lk.c`) — 30 procs. Documents: `lk::doc_new ?text?`, `doc_text`, `doc_len`, `doc_line_count`, `doc_revision` (the `{hi,lo}` pair as a deterministic `"hi:lo"` string — equality-comparable from script, never arithmetic), `doc_insert [d pos text]` / `doc_delete [d pos len]` (a C-level rejection is a hard script error), `doc_transact [d body]` (one begin/commit bracket; **commit runs even if the body errors**, then the error propagates), `doc_subscribe [d proc]` → id (the proc receives a LIST of delta dicts: `start`, `deleted_len`, `inserted_len`, `deleted`, `inserted` — bytes copied into Lcl strings during notification — and `origin`), `doc_unsubscribe`. History: `lk::history_new ?doc?` (attaches when given a doc; a detached history is auto-attached by `lk::editor_new`), `history_undo/redo [h d]`, `history_can_undo/can_redo`. Editors: `lk::editor_new [ui doc ?hist]` (registers the editor in the ui's resource table; debug name `"lcl-editor"`), `editor_cursor`, `editor_set_cursor`, `editor_selection` (2-list or empty), **`lk::editor_command [e cmd ?args...]`** (the `LK_ED_*` enum by name — `insert_text` takes text, `set_cursor` takes pos + optional literal `extend`, `scroll_lines` takes a signed count, motion commands take an optional literal `select`; unknown commands hard-error listing all known names), `editor_set_spans [e d spans]` (list of `#{start N end N ?fg (r g b ?a?)? ?bg ...? ?underline 1?}` dicts, sorted/non-overlapping enforced; empty list clears; the snapshot is stamped with the doc's **current** revision + the spans' min/max range — equivalent to producer-stamping for synchronous script producers). Annot stores: `lk::annot_store_new`, `annot_attach` (re-attach errors), `annot_add [s start end layer ?meta-dict]` → id, `annot_remove`, `annot_span` (2-list; error if gone), `annot_meta` (`""` for an absent key; error if the record is gone), `annot_in_range`/`annot_at` (optional layer filter), `annot_by_layer`, `annot_layer_register`. **Lifetime scheme**: dependents retain their dependencies' Lcl values (`lcl_ref_inc`) — history/editor/annot wrappers hold the doc value, the editor also the ui and history values — so GC order becomes refcount order and dependents always destroy (and unsubscribe) before the document, regardless of when the script drops its handles. A file-local live-ui registry additionally guards the editor finalizer's `lk_resource_release` against uis torn down explicitly (`lk::ui_destroy`, `lk::window_destroy`).
- **lcl-lk-main.c** (`src/lcl/lcl-lk-main.c`) — Standalone binary. C11 (SDL headers). Creates interp, registers core + lk, evals a `.lcl` file.
- **lcl-lk-test.c** (`test/lcl-lk-test.c`) — 92 headless tests via `lcl_eval_string`. Covers all non-SDL procs including text_input kind and overlay push/pop (modal defaults, focus trap, routed ESC), plus SDL-gated error-path tests (`lk::register_font` arity/type) that don't need a display. Includes the Layer-2 DSL harness (19 tests, DSL v2): evals `lib/lk-dsl.lcl` via the `TEST_DSL_PATH` compile definition, drives the DSL's module state (`lk_dsl::_ui`/`lk_dsl::_tree`) from script, and asserts tree shape through the C API (`lk_tree_find_by_id`, `lk_node_kind_get`, prop getters, `lk_tree_get_presentation`, `lk_tree_has_tag`, `lk_editor_from_node`). Error-path tests (`eval_expect_err`) assert that unknown prop keys and malformed trailing args raise script errors carrying the widget id; composition tests cover props-dict-in-a-variable and `Dict::merge`. The editor-track batch (23 tests) covers every stage-D proc's happy path, error paths (arity, wrong opaque, unknown editor command listing known names, malformed span/meta dicts), delta-dict contents for insert + delete, transaction grouping through undo, anchor tracking through edits, and the lifetime test (doc/history handles dropped while the editor lives and keeps working).
- **Examples** (`examples/`) — `hello.lcl` (Layer 1 bindings), `hello-dsl.lcl` (Layer 2 DSL rewrite), `budget-dsl.lcl` (row editor: dynamic rows, dropdowns, value-changed sync), `modal-dsl.lcl` (confirm dialog: `lk::overlay_push`/`pop`, hidden content subtree, focus trap, ESC), `split-dsl.lcl` (resizable two-pane layout: `split_h`, `split_ratio` prop, divider drag), `editor-dsl.lcl` (multi-line editor in a split beside live status labels: document/history/annot-store in the app environment, TODO/underline annotations projected to styled spans by a per-frame producer, buttons driving `lk::editor_command`).

### Layer 2 DSL (`lib/lk-dsl.lcl`)

Declarative DSL over the Layer 1 bindings (DSL v2 props-dict syntax — see `docs/dsl-v2.md`). Auto-loaded by `lcl_lk_main` via the `LK_DSL_PATH` compile definition.

- **Widget shape** — `kind id ?props-dict? ?body-block?`, e.g. `button "save" #{text "Save" tooltip "Saves the file"} { ... }`. Both trailing args are optional and disambiguated by value shape: a dict is props, anything else is the body block. Props dicts are ordinary values — build them in variables, share across widgets, compose with `[Dict::merge $base $override]`. Multiline dict literals parse at bare level, so long props dicts just wrap.
- **Hard errors, no silent swallowing** — an unknown prop key raises `button "del_3": unknown prop 'tooltp' (known: text, w, h, ...)` via the script-side `error` proc. Malformed trailing args also error: a lone numeric arg (`button "x" 42`), a stale v1 `-flag`, a non-dict where props are expected, a second dict where the body should be, or more than two trailing args. The schema is the `_prop_schema` dict (`text focusable disabled w h padding gap align justify hidden tooltip split_ratio editor tag present`); `tag` maps to `lk::tag`, `present` (a `(ptype value-or-list)` 2-list) to `lk::present`, everything else to `lk::prop` (the `editor` value is the opaque from `lk::editor_new` — `editor "src" #{editor $ed focusable 1}`).
- **`app`** — `app "Title" ?#{width 900 height 600 font "/path" font_size 16}? { body }`; unknown keys hard-error.
- **`theme` / `rule`** — `rule <kind> ?selector-dict? style-dict`: one dict arg is the style, two dicts are selector then style. Selector keys: `tag`, `state` (unknown selector keys error), e.g. `rule button #{tag primary state hovered} #{bg (65 120 210)}`.
- **Unchanged positional forms** — `translator <event> <ptype> ?<kind>? <cmd>`, `keybinding <key> <mods> ?<ptype>? <cmd>`, `on <cmd> <handler>`, `view { body }`, `event_handler <callable>`.
- Exports into caller scope: `app view theme translator keybinding on event_handler` + all widget kinds (`column row label button text_input spacer scroll dropdown option split_h split_v editor`).

## Code Conventions

- Strict C89 (`-std=c90 -pedantic`). No `//` comments, no mixed declarations and code, no `for`-loop initializer declarations.
- All public symbols are prefixed `lk_`. Enums use `UIK_` (kinds), `UIP_` (prop keys), `UIV_` (value tags), `UID_` (diag kinds), `UIDC_` (diag codes).
- Custom allocators are threaded through all data structures — never call `malloc`/`free` directly in core code; use `lk_alloc`/`lk_dealloc` or the struct's own allocator pointers.
- Node index 0 is the null/sentinel value; valid nodes start at index 1.
- `lk_str` is a non-owning `(ptr, len)` view; `lk_str_cmp` returns 1 for equal (not strcmp-style).
- All prop strings are interned. `lk_v_str` and `lk_v_cstr` take an `lk_intern*` and store the interned ID. To read a string prop back, look it up via `lk_intern_str(tree->intern, value.as.str_id)`.
- Trees always have an intern table. If none is provided in `lk_tree_cfg`, one is created automatically and freed on `lk_tree_destroy`. If provided externally, the tree borrows it (does not free).
