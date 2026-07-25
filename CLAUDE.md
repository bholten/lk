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
- **`lk_value`** — Tagged union: `NONE`, `BOOL`, `I32`, `STR` (where STR stores an interned `lk_u32` string ID, not a raw pointer).
- **`lk_kind`** — Node kinds: `WINDOW`, `ROW`, `COLUMN`, `SPACER`, `LABEL`, `BUTTON`, `TEXT_INPUT`, `SCROLL`.
- **`lk_prop_key`** — Property keys: `TEXT`, `FOCUSABLE`, `DISABLED`, `W`, `H`, `PADDING`, `GAP`, `ALIGN`, `JUSTIFY`.

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

Single-line text input with cursor, selection, and key handling. Leaf node (no children).

- **Text buffer**: Stored as interned string in `lk_state` under `LKS_TEXT_BUF`. Each edit interns a new string. Host feeds edited text back as `UIP_TEXT` each frame.
- **Cursor/selection**: `LKS_CURSOR_POS` (char index), `LKS_SELECTION_START`/`LKS_SELECTION_END`. Cursor pixel offset computed during measure and stored in `LKS_CURSOR_X`.
- **Event handling**: Consumes `LK_EVENT_TEXT` (insert), `LK_EVENT_KEY_DOWN` (BACKSPACE, DELETE, LEFT, RIGHT, HOME, END, CTRL+A). TAB/RETURN/ESCAPE bubble to app.
- **Rendering**: Background FILL_RECT, selection highlight FILL_RECT, DRAW_TEXT, cursor bar FILL_RECT.
- **Max buffer**: `LK_TEXT_INPUT_MAX` = 1024 bytes.

### Scroll Widget (`lk-scroll.c`)

Scroll container that clips children and scrolls vertically via wheel events. Children stacked vertically like a column.

- **Scroll offset**: Stored as `LKS_SCROLL_Y` in `lk_state`. Max offset in `LKS_SCROLL_MAX`. Both computed during layout.
- **Event handling**: Consumes `LK_EVENT_WHEEL` events (via bubbling — wheel on a child is handled by the scroll ancestor). Adjusts `LKS_SCROLL_Y` by `SCROLL_STEP` (30px).
- **Rendering**: Background FILL_RECT, scroll bar track + thumb (colors from `style->scrollbar_track` / `style->scrollbar_thumb`) when content overflows, CLIP_BEGIN/CLIP_END for child clipping.
- **Layout**: Scroll bar reduces available width by `SCROLL_BAR_W` (6px) when content exceeds viewport.

### Event Routing (`lk-event.c`)

Two-tier event dispatch model. Widget handlers get first-right-of-refusal; user handlers see only unconsumed events.

1. **Widget dispatch** (target → bubble): Target node's `lk_widget_def.event` fires at TARGET phase. If not handled, walks ancestors (BUBBLE). This lets built-in widgets (text input, scroll) consume internal events (keystrokes, wheel) without leaking to user code.
2. **User handler dispatch** (capture → target → bubble): Only reached if no widget consumed. The `lk_event_handler_fn` fires in full DOM-style phases for application-level concerns (global shortcuts, modal interception).
3. **Translator dispatch**: Only reached if neither tier consumed. Walks ancestors for presentation matches, emits commands.

Key functions: `lk_event_route(ui, event)`, `lk_hit_test(tree, rects, x, y)`, `lk_focus_set/clear/next/prev`, `lk_hover_set/clear`.

### Node Prop Helpers (`lk-tree.c`)

Public functions for querying node properties (used by widget implementations and event code):

- `lk_node_prop_i32(t, n, key, def)` — get i32 prop or default
- `lk_node_has_prop(t, n, key)` — check if prop exists
- `lk_node_prop_bool(t, n, key)` — get bool prop (0 if missing)
- `lk_node_text(t, n)` — get text prop as `lk_str`
- `lk_node_text_id(t, n)` — get text prop as interned string ID

### Validation

`lk_tree_validate` checks structural integrity (root exists, no cycles via DFS, no duplicate IDs, no multi-parent nodes, parent pointer consistency). `lk_tree_validate_schema` is stubbed out for future per-kind prop validation.

### Debug

`lk_tree_dump` outputs the tree as s-expression text via a `lk_write_fn` callback.

### SDL3 Backend (`src/sdl/`)

Optional platform backend. Conditional on `find_package(SDL3)` and `find_package(SDL3_ttf)`.

- **lk-sdl.h** — Public API: `lk_window_create/destroy/run`, `lk_window_ui`, `lk_window_set_event_handler`. The `lk_frame_fn` callback receives a mutable `lk_tree*`; the run loop handles begin/end frame, layout, event polling, and rendering.
- **lk-sdl.c** — SDL3 integration: event translation (`sdl_to_lk_event`), render list consumption, and the real `lk_text_backend` implementation (text-contract stage B). Text renders through one `TTF_TextEngine` per window (`TTF_CreateRendererTextEngine`; internal glyph atlas does the caching — the old per-string texture cache is gone) with a single reusable scratch `TTF_Text`. Fonts: a face registry (`face_paths[]`; face 0 = `lk_window_cfg.font_path`, may be absent) plus a lazily-populated `(face_id, size)` → `TTF_Font*` instance cache (linear-scan array, size 0 resolves to the window default size, fallback 16; all instances closed on window destroy). `lk_window_register_font(win, path)` (lk-sdl.h) registers a new face and returns its `font_id` (>= 1; 0 on failure — paths are verified by opening at the default size at registration). Vtable: measure via `TTF_GetStringSize`/`TTF_GetFontAscent`; `x_from_index`/`index_from_x` via `TTF_GetTextSubString`/`TTF_GetTextSubStringForPoint` on the scratch text (nearest-boundary snapping, clamped); `line_height` via `TTF_GetFontHeight`. The run loop uses the stub text backend when no face is available. Gotcha: `TTF_SetTextString` treats length 0 as "null-terminated" — empty runs must pass a literal `""`.
- **demo.c** — Fruit selector demo exercising presentations, commands, focus, and event handling.
- Run loop order: clear commands → begin_frame → frame callback → end_frame → resolve styles → layout (with state) → poll events (hit-test targeting for pointer events, focus targeting for key/text events; two-tier event routing via `lk_event_route`; hover state updated on pointer move; click-to-focus and tab-cycling as built-in behaviors) → render (with state). Key/text events fall back to root when nothing is focused.

### Lcl Scripting Bindings (`src/lcl/`)

Layer 1 bindings exposing lk to the Lcl scripting language (submodule at `submodules/lcl`). Built when `LK_BUILD_LCL=ON`.

- **lcl-lk.h** (`include/lcl-lk.h`) — Public header. Single entry point: `lcl_register_lk(interp)`.
- **lcl-lk.c** (`src/lcl/lcl-lk.c`) — 32 procs in the `lk` namespace (26 core + 6 SDL-conditional). `lk::register_font [win path]` returns the new `font_id` (int; 0 = failure, mirroring C — an unreadable path is not an Lcl error). Theme rules take `font_id`/`font_size` int keys in the style dict (bad values are errors). There is deliberately no DSL wrapper for `register_font` — apps call `lk::register_font` directly on the window. C89. Opaque types: `lk_ui` (with finalizer), `lk_tree` (borrowed, no finalizer), `lk_window` (wrapped in `struct lcl_lk_window` for event handler lifetime). Static string-to-enum lookup tables for kinds, prop keys, event types, align values, and node states. Prop value coercion dispatches on key (text→string, focusable/disabled→bool, w/h/padding/gap→i32, align/justify→enum lookup). Event handler bridge marshals `lk_event` fields into an Lcl dict (including `target_id` and `node_id` string fields resolved from the intern table) and calls the user's proc. Command handler bridge converts `lk_command` to an Lcl dict (via `command_to_dict`, plus `source_node_id`) and calls the user's command handler proc.
- **lcl-lk-main.c** (`src/lcl/lcl-lk-main.c`) — Standalone binary. C11 (SDL headers). Creates interp, registers core + lk, evals a `.lcl` file.
- **lcl-lk-test.c** (`test/lcl-lk-test.c`) — 43 headless tests via `lcl_eval_string`. Covers all non-SDL procs including text_input kind, plus SDL-gated error-path tests (`lk::register_font` arity/type) that don't need a display.

## Code Conventions

- Strict C89 (`-std=c90 -pedantic`). No `//` comments, no mixed declarations and code, no `for`-loop initializer declarations.
- All public symbols are prefixed `lk_`. Enums use `UIK_` (kinds), `UIP_` (prop keys), `UIV_` (value tags), `UID_` (diag kinds), `UIDC_` (diag codes).
- Custom allocators are threaded through all data structures — never call `malloc`/`free` directly in core code; use `lk_alloc`/`lk_dealloc` or the struct's own allocator pointers.
- Node index 0 is the null/sentinel value; valid nodes start at index 1.
- `lk_str` is a non-owning `(ptr, len)` view; `lk_str_cmp` returns 1 for equal (not strcmp-style).
- All prop strings are interned. `lk_v_str` and `lk_v_cstr` take an `lk_intern*` and store the interned ID. To read a string prop back, look it up via `lk_intern_str(tree->intern, value.as.str_id)`.
- Trees always have an intern table. If none is provided in `lk_tree_cfg`, one is created automatically and freed on `lk_tree_destroy`. If provided externally, the tree borrows it (does not free).
