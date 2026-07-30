# Lk Implementation Roadmap

## Design Decisions (Agreed)

- **C89** (`-std=c90 -pedantic`). No C99/C11 features.
- **Intern all prop strings** into `lk_intern`. This makes tree diff O(1) per prop comparison (compare `lk_node_id`s, not string contents) and gives the tree clear ownership of string data.
- **Double-buffered trees**. The core holds `prev` and `next` trees. Each frame, the host builds into `next` (using `lk_tree_reset` + add nodes). The core diffs `prev` vs `next`, produces a changeset, applies layout only to dirty subtrees, then swaps. Works naturally with the existing arena design.
- **Node IDs are opaque interned strings**. Hierarchical path semantics (for queries/selectors) deferred until needed. The important invariant is stability across frames, not internal structure.


## Phase 1: Tree Foundations

Everything here is testable in pure C with no SDL dependency.

- [x] **Fix integer types in `lk-int.h`**
  - `lk_u32` is currently `unsigned long` (64 bits on LP64). Should be `unsigned int`.
  - `lk_i32` is currently `long`. Should be `int`.
  - `lk_u16` and `lk_u8` are fine as-is.

- [x] **Intern prop strings**
  - `lk_tree_add_prop` with `UIV_STR` should intern the string via `t->intern` and store the interned ID rather than a raw `lk_str` pointer.
  - Requires `lk_intern` to always be present on a tree (enforce in `lk_tree_create`).
  - Prop diff then becomes: compare `lk_node_id` values, not string bytes.

- [x] **Double-buffered tree diffing**
  - New struct: `lk_ui` (or similar) that owns two `lk_tree` pointers (`prev`, `next`) and a shared `lk_intern`.
  - API: `lk_ui_begin_frame` resets `next`; host builds into `next`; `lk_ui_end_frame` diffs and swaps.
  - Diff algorithm (keyed on `lk_node_id`):
    - Walk both trees from root.
    - Match children by ID.
    - Matched nodes: compare kind + props slice → mark dirty if changed.
    - Unmatched in prev = removed; unmatched in next = added.
  - Diff output: a `lk_changeset` (array of add/remove/update entries) — consumed by layout to know which subtrees need re-layout.

- [x] **Expand test harness**
  - Build tree A, build tree B, diff, verify changeset.
  - Test cases: add node, remove node, reorder children, change props, no-op (identical trees).

- [x] **Deferred prop addition fix** (`lk-tree.c`)
  - `lk_tree_add_prop` now lazily initializes `props_off` on first prop addition to a node.
  - Allows all nodes to be created first, then props grouped per-node afterward (previously required props immediately after each node creation, which silently dropped props in the all-nodes-first pattern used by Lcl scripts).
  - 3 regression tests: deferred-all-nodes-first, interleave-drops, render-deferred-props-emit-text.


## Phase 2: Layout

Still no SDL needed. Layout produces numeric geometry, testable with assertions.

- [x] **Layout data structures**
  - `lk_rect { lk_i32 x, y, w, h }`, `lk_size { lk_i32 w, h }` (scratch).
  - `lk_layout_cfg` with pluggable `lk_measure_text_fn`, viewport size.
  - `lk_layout(tree, cfg, rects)` — caller provides parallel `lk_rect[]` array indexed by `lk_ix`.
  - `lk_measure_text_stub` — 8px/char, 16px tall (swap for real measurer in Phase 3).

- [x] **Flexbox-ish layout algorithm**
  - Two passes: measure (bottom-up iterative post-order DFS) → layout (top-down iterative pre-order DFS).
  - `column`: children stacked vertically, intrinsic height on main axis, stretch on cross axis.
  - `row`: children placed horizontally, intrinsic width on main axis, stretch on cross axis.
  - `window`: fills viewport, children fill entire content area.
  - `spacer`: flex items split remaining space equally (with 1px remainder distribution). Explicit `UIP_H`/`UIP_W` makes a spacer fixed.
  - `button`: text size + padding on each axis.
  - Respects `UIP_W`, `UIP_H`, `UIP_PADDING`, `UIP_GAP`. Explicit W/H overrides intrinsics.

- [x] **Test layout numerically**
  - 15 tests: viewport fill, column/row with labels, gap, padding, spacers (flex + fixed), explicit W/H, button intrinsics, padding+gap, empty tree, nested column>row>labels.


## Phase 3: SDL3 + Rendering

Get pixels on screen.

- [x] **Render list** (`lk-render.c`)
  - Flat display list: `FILL_RECT`, `DRAW_TEXT`. Walks tree pre-order DFS, emits commands per kind.
  - `lk_render_build(tree, rects, out)` reuses capacity across frames. 6 tests.
  - Keeps renderer swappable; enables snapshot testing by comparing render lists.

- [x] **SDL3 backend** (`src/sdl/lk-sdl.c`, `lk-sdl.h`)
  - `lk_window_create` / `lk_window_run` / `lk_window_destroy` — built-in run loop.
  - Conditional CMake targets (SDL3 + SDL3_ttf). No SDL types in public headers.
  - SDL_ttf text measurement plugged into layout; falls back to stub measurer.
  - Consumes render list: `FILL_RECT` → `SDL_RenderFillRect`, `DRAW_TEXT` → `TTF_RenderText_Blended` (since replaced by `TTF_TextEngine` glyph-atlas drawing, text-contract stage B).

- [x] **Basic theme/style**
  - Hardcoded MVP dark theme: window bg `(30,30,30)`, button bg `(60,60,60)`, text `(220,220,220)`.

- [x] **Demo** (`src/sdl/demo.c`)
  - Window > column > label + button. Works with `lk_demo` executable.

- [x] **Text texture caching** *(deleted in text-contract stage B)*
  - Was: open-addressing per-string texture cache keyed by `(str_id, color)` with frame-stamped LRU eviction.
  - Replaced wholesale by SDL_ttf's `TTF_TextEngine` (internal glyph atlas) — see `docs/text-contract.md` §4.4.

- [x] **Clip rects**
  - `LK_ROP_CLIP_BEGIN` / `LK_ROP_CLIP_END` render ops. WINDOW clips its children.
  - DFS stack uses high-bit marker to emit CLIP_END after children.
  - SDL backend: `SDL_SetRenderClipRect` / NULL.

- [x] **Alignment** (deferred from Phase 2)
  - `UIP_ALIGN` (cross-axis) and `UIP_JUSTIFY` (main-axis) prop keys.
  - `lk_align` enum: START, CENTER, END, STRETCH (default cross-axis behavior).
  - 6 alignment tests (column + row, center + end, align + justify).


## Phase 4: Input + Event Routing

Now unblocked — SDL backend provides the window and event pump.

- [x] **Hit-testing** (`lk-event.c`)
  - Iterative DFS point-in-rect against computed layout. Deepest match wins (front-to-back).
  - `lk_hit_test(tree, rects, x, y)` returns `lk_ix`. 6 tests.

- [x] **Event types and routing** (`lk-data.h`, `lk-event.c`)
  - `lk_event` struct with type, phase, mods, handled flag, target, and per-type data unions.
  - `lk_event_type` enum: POINTER_MOVE/DOWN/UP, KEY_DOWN/UP, TEXT, WHEEL, WINDOW_RESIZE/CLOSE.
  - `lk_event_route(ui, event)`: capture (root→target) → target → bubble (target→root). Stops on `handled`.
  - `lk_event_handler_fn` callback fired synchronously during routing. 6 routing tests.

- [x] **Focus management** (`lk-event.c`, `lk-ui.c`)
  - Focus tracked by `lk_node_id` on `lk_ui` (stable across frame rebuilds).
  - `lk_focus_set/clear/next/prev/current`. Next/prev collect focusable+enabled nodes via DFS, wrap around.
  - Disabled nodes skipped. Removed nodes auto-clear focus in `lk_ui_end_frame`. 8 tests.

- [x] **SDL event normalization** (`lk-sdl.c`)
  - `sdl_to_lk_event` maps SDL3 events to `lk_event` structs.
  - `sdl_to_lk_keycode` maps SDL keycodes to the `lk_keycode` enum. ~~Minimal (Tab, Return, Escape, arrows, etc.)~~ — completed in text-contract stage D: A–Z, 0–9, PageUp/PageDown, F1–F12 all mapped (enum + SDL map + Lcl binding table).
  - Run loop restructured: frame → layout → event poll (with hit-test/focus targeting) → render.
  - Built-in behaviors: Tab/Shift-Tab cycles focus, click-to-focus on pointer down.

- [x] **Demo updated** (`demo.c`)
  - Button marked `UIP_FOCUSABLE`. Event handler prints "Button clicked!" on pointer down.

- [x] **Retained UI state**
  - `lk_state` hash table keyed by `(lk_node_id, lk_u16 state_key)`.
  - Survives across frames (not part of the diffed tree).
  - Automatic GC when nodes are REMOVED in changeset.
  - `focused_id` remains a separate global singleton for fast access.


## Phase 4.5: Widget Abstraction Refactor

Extract per-kind logic (measure, layout, render) behind a `lk_widget_def` vtable.

- [x] **Public prop helpers** (`lk-tree.c`)
  - `lk_node_prop_i32`, `lk_node_has_prop`, `lk_node_prop_bool`, `lk_node_text`, `lk_node_text_id`.
  - Eliminated duplicate static helpers from lk-layout.c, lk-render.c, lk-event.c.

- [x] **`lk_widget_def` vtable** (`lk-data.h`)
  - Function pointer struct: `measure`, `layout`, `render`, plus `clips` flag.
  - `layout = NULL` means leaf (engine skips child recursion).

- [x] **Global registry** (`lk-widget.c`)
  - `lk_widget_def g_widgets[LK_KIND_MAX]` with lazy init via `lk_widget_get`.
  - `lk_widget_register(kind, def)` for custom/override kinds.
  - Default entries for all 6 built-in kinds.

- [x] **Column/row unification**
  - `layout_stack(axis)` — single axis-parameterized function replaces ~100 lines of near-identical column/row layout code.

- [x] **Dispatch refactor**
  - `lk_layout` measure + layout passes dispatch through `lk_widget_get()` instead of `switch (kind)`.
  - `lk_render_build` dispatches render + clips through vtable instead of `switch (kind)`.
  - `lk_render_list_push` made public (widgets need it to emit commands).

- [x] **Tests** — 3 new widget registry tests. All existing tests pass unchanged.


## Phase 5: Presentations + Commands

The CLIM-inspired semantic layer.

- [x] **Presentation attachment**
  - `lk_presentation` on nodes: `{ ptype, pvalue }` with interned types.
  - Presentations diffed across frames; changes emit UPDATED.

- [x] **Command registry**
  - Named commands with up to 4 `lk_value` arguments.
  - Command queue, handler callback, and append-only command log.

- [x] **Translators**
  - Rules: `(event_type, ptype?, node_kind?) → command_name`.
  - Walk ancestors for presentation matching during event routing.

- [x] **Introspection APIs**
  - `lk_ui_dump_commands`, `lk_ui_command_log`, `lk_ui_clear_command_log`.


## Phase 5.5: Interactive Widgets

Depends on retained UI state (Phase 4), layout, rendering, and input.

- [x] **Text input widget** (`lk-text-input.c`)
  - `UIK_TEXT_INPUT` kind. Single-line text input with cursor, selection, and key handling.
  - Text buffer as interned string in `lk_state` (`LKS_TEXT_BUF`). Cursor/selection via `LKS_CURSOR_POS`, `LKS_SELECTION_START`, `LKS_SELECTION_END`. Cursor pixel offset in `LKS_CURSOR_X`.
  - ~~Byte-wise cursor motion/deletion corrupts multibyte UTF-8~~ — fixed in text-contract stage C: codepoint-wise motion and deletion via `src/core/lk-utf8.c`; cursor/selection geometry via `x_from_index`; click-to-position via `index_from_x` (`lk_ui_set_text_backend`).
  - Widget `event` handler on `lk_widget_def` vtable (called at TARGET phase before global handler).
  - `const lk_state *state` threaded through render vtable and `lk_render_build`; `lk_state *state` added to `lk_layout_cfg`.
  - Handles: TEXT (insert), BACKSPACE, DELETE, LEFT/RIGHT/HOME/END (with SHIFT for selection), CTRL+A (select all). TAB/RETURN/ESCAPE bubble to app.
  - Default theme: bg=(45,45,45), padding=4; focused: border_width=1, border_color=(80,140,220).
  - Separate file (`src/core/lk-text-input.c`, ~400 lines) — complex interactive widgets get their own files.
  - 10 new headless tests (131→141 total). 1 new Lcl binding test (29→30 total).
  - Known issues:
    - **Text stretching**: ~~DRAW_TEXT render command uses the full content rect width; SDL backend stretches the text to fill it~~ — fixed; text draws at its natural size, overflow handled by the active clip.
    - **No default stretch**: Text input uses intrinsic width (text + padding, min 100px) and shrinks as text is deleted. Workaround: set explicit `UIP_W`. Could default to stretch in cross-axis, or increase minimum width.

- [x] **Scroll container** (`lk-scroll.c`)
  - `UIK_SCROLL` kind. Clips children, scrolls vertically via wheel events, displays scroll bar indicator.
  - Scroll offset in `lk_state` (`LKS_SCROLL_Y`), max offset in `LKS_SCROLL_MAX`.
  - Widget `event` handler consumes `LK_EVENT_WHEEL` and adjusts scroll offset.
  - Children stacked vertically like a column. Scroll bar reduces available width when content overflows.
  - Default theme: bg=(35,35,35), padding=0, gap=0.
  - 10 new headless tests. 1 new Lcl binding test.


## Phase 6: Layer System

See [layers.md](layers.md) for the full design. Only implement after phases 1-5 are solid.

- [ ] TreeLayer and SurfaceLayer interfaces
- [ ] Layer pipeline execution
- [ ] Annotation / tag system
- [ ] Overlay anchoring
- [ ] Layer caching and incrementality


## Phase 5.7: Lcl Bindings (Layer 1)

Thin C extension package exposing lk to Lcl scripts via explicit handle passing.

- [x] **Public header** (`include/lcl-lk.h`)
  - `lcl_register_lk(interp)` registers the `lk` namespace.

- [x] **29 procs in `lk` namespace** (`src/lcl/lcl-lk.c`)
  - UI lifecycle: `ui_create`, `ui_destroy`, `begin_frame`, `end_frame`, `tree`.
  - Tree building: `node`, `set_root`, `append_child`, `prop`, `present`.
  - Commands/translators: `add_translator`, `commands`, `clear_commands`, `command_log`, `clear_command_log`, `set_command_handler`.
  - State: `state_set`, `state_get`.
  - Focus: `focus_set`, `focus_clear`.
  - Tags & style: `tag`, `theme_rule`.
  - Interning: `intern_str`, `intern_id`.
  - SDL window (conditional on `LK_HAVE_SDL`): `window_create`, `window_destroy`, `window_run`, `window_ui`, `window_set_event_handler`.

- [x] **String-to-enum lookup tables**
  - Kinds, prop keys, event types, align values. Scripts use readable strings; binding maps to C enums.

- [x] **Prop value coercion**
  - `text` → interned string, `focusable`/`disabled` → bool, `w`/`h`/`padding`/`gap` → i32, `align`/`justify` → align enum lookup.

- [x] **Event handler bridge**
  - `lk::window_set_event_handler` marshals `lk_event` fields into an Lcl dict (type, phase, mods, target, target_id, node_id, plus type-specific fields) and calls the user's proc.
  - `target_id` and `node_id` are string fields resolved from the intern table, so scripts can identify nodes by name.
  - `struct lcl_lk_window` wrapper manages lifetime of the handler callback.

- [x] **Command handler bridge**
  - `lk::set_command_handler` registers a callback that receives commands as Lcl dicts (name, args, source_node, source_ptype, source_node_id).
  - `struct lcl_cmd_ctx` holds `{interp, handler, ui}`. Cleanup in both `ui_finalizer` and `lcl_lk_window_finalizer`.

- [x] **Frame callback bridge**
  - `lk::window_run` blocks, calling the Lcl view proc each frame with an `lk_tree` opaque.

- [x] **Standalone binary** (`src/lcl/lcl-lk-main.c`)
  - `lcl_lk_main <script.lcl>` — creates interp, registers core + lk, evals file.

- [x] **CMake integration**
  - `LK_BUILD_LCL` option. Targets: `lcl_lk` (static lib), `lcl_lk_test`, `lcl_lk_main` (SDL-conditional).

- [x] **Headless tests** (`test/lcl-lk-test.c`)
  - 29 tests exercising all non-SDL procs via `lcl_eval_string`.

- [x] **Key event routing fix** (`lk-sdl.c`)
  - Key/text events now fall back to root node when nothing is focused, instead of being silently dropped.

- [x] **Example application** (`examples/hello.lcl`)
  - Demonstrates full CLIM-style pattern: presentations on buttons, translator mapping `pointer_down` + `"action"` → `"ButtonClick"` command, command handler printing action.
  - Also demonstrates event handler for keyboard events, custom theme rules with `lk::theme_rule`, and tags with `lk::tag`.


## Phase 5.8: Styles and Themes

See [styles.md](styles.md) for the full design. Style is a projection of
meaning into appearance — the view function describes **what**, the theme
decides **how it looks**.

- [x] **Design style/theme system** — see `docs/styles.md`.

- [x] **`lk_style` struct and `lk_theme` type** (`lk.h`)
  - `lk_style`: fg, bg, font_id, font_size, padding, gap, border_width, border_color, border_radius, align, justify.
  - `lk_theme`: opaque, owns an ordered list of style rules.
  - `lk_theme_new`, `lk_theme_destroy`, `lk_theme_add_rule`.
  - `lk_style_resolve(theme, tree, node_states, styles[])`.

- [x] **Node tags** (`lk.h`, `lk-tree.c`)
  - Tag set per node (interned symbol IDs). `lk_tree_add_tag(t, ix, tag_id)`.
  - Tags are diffed across frames (like props and presentations).
  - Separate tag storage on `lk_tree`.

- [x] **Default theme** (`lk-style.c`)
  - Built-in rules reproducing current hardcoded dark theme colors.
  - Default padding/gap/font for all built-in kinds.
  - Registered automatically on `lk_theme_new` (user rules layer on top).

- [x] **Style resolution pass** (`lk-style.c`)
  - Per-node rule matching: walk rules, check kind/tag/state, merge partial styles.
  - Inheritance pass (top-down): fg and font inherit from parent if unset.
  - Tree prop override merge: tree prop wins if present, else style value.

- [x] **Thread style through layout** (`lk-layout.c`)
  - Layout reads font from resolved `lk_style[]` for text measurement.
  - Layout reads padding/gap/align/justify from style (with tree prop override).
  - `lk_layout_cfg` gains `const lk_style *styles` field; widget measure/layout functions read from styles when available, falling back to tree props.

- [x] **Thread style through render** (`lk-widget.c`)
  - Widget render functions receive `const lk_style *style` via render list styles pointer.
  - Removed hardcoded `color_window_bg`, `color_button_bg`, `color_text` functions.
  - Render uses `style->bg`, `style->fg` directly.

- [x] **Interaction state tracking** (`lk-ui.c`)
  - `lk_u8` state bitmask per node: `LK_NSTATE_FOCUSED`, `LK_NSTATE_HOVERED`, `LK_NSTATE_DISABLED`.
  - Computed per-frame from focus, hit-test, and disabled prop.
  - Passed to `lk_style_resolve` as `node_states[]`.

- [x] **`lk_ui` integration** (`lk-ui.c`)
  - `lk_ui` owns `lk_theme*` and `lk_style[]` (parallel to `lk_rect[]`).
  - `lk_ui_set_theme`, `lk_ui_theme`, `lk_ui_styles`, `lk_ui_resolve_styles`.
  - Style resolution runs after `lk_ui_end_frame`, before layout in the run loop.

- [x] **SDL backend update** (`lk-sdl.c`)
  - Run loop calls `lk_ui_resolve_styles` between end_frame and layout.
  - Passes `lk_style[]` to layout and render.
  - Font resolution: style `font_id` selects from loaded fonts (v0: single default font).

- [x] **Lcl bindings** (`lcl-lk.c`)
  - `lk::theme_rule` — add a rule from script (kind, tag, state, style dict with bg/fg/padding/gap/etc.).
  - `lk::tag` — tag a node.
  - `lk::window_create` default theme auto-applied.

- [x] **Tests**
  - 10 new tests (121→131 total). Covers: bg non-inheritance, font inheritance,
    kind cross-matching, multi-bit state, tag matching, gap/align/justify prop
    overrides, render-uses-style-colors, layout-style-gap, wildcard matching.


## Phase 5.9: Lcl DSL (Layer 2)

- [x] **DSL library** (`lib/lk-dsl.lcl`)
  - Pure-Lcl declarative sugar over Layer 1 bindings.
  - Kind names as commands (`column`, `button`, ...), nesting implies parent-child,
    a props dict sets props (`kind id ?props-dict? ?body?` — DSL v2; the
    original `-flag value` syntax was replaced 2026-07-25, see
    `docs/dsl-v2.md`). `app` top-level entry, `view` captures frame body,
    `theme`/`rule` for styling, `translator`/`on` for commands.
  - Auto-loaded by `lcl_lk_main` via `LK_DSL_PATH` compile definition.
  - `examples/hello-dsl.lcl` demonstrates full rewrite (~60 lines vs ~90 Layer 1).
- [x] ~~**DSL debt: silent flag swallowing / flag-parsing warts**~~
  Resolved by DSL v2 stage 3 (2026-07-25, `docs/dsl-v2.md`):
  `_parse_flags`/`_apply_flags`/`_prop_keys` deleted.  Unknown prop
  keys are now hard errors carrying the widget id and known-key list
  (no whitelist to fall behind); trailing-flag-becomes-boolean and the
  `app` nested-if flag parser are gone (malformed trailing args error
  cleanly).  Props dicts are first-class values (shareable,
  `Dict::merge`-composable); theme rules take selector dicts.


## Up Next

### ~~1. Border rendering~~ ✓

### ~~2. Clipboard API + input polish~~ ✓

- `lk_clipboard_get_fn`/`lk_clipboard_set_fn` callbacks on `lk_ui`.
- SDL backend installs real clipboard. Ctrl+C/V/X in text input widget.
- Lcl bindings: `lk::clipboard_get`, `lk::clipboard_set`.
- 4 new core tests, 2 new Lcl tests.

### ~~3. Modifier-aware keybindings~~ ✓

- Extended `lk_translator` with `keycode` (lk_u16) and `mods` (lk_u8) fields.
- When `keycode != 0`, exact match on `event.data.key.keycode` and `event.mods`.
- When `keycode == 0`, backward-compatible (ignores key/mods).
- Full A-Z keycodes in `lk_keycode` enum; SDL backend maps all 26 letters.
- Lcl bindings: 7-arg `lk::add_translator` with keycode + mods strings.
- DSL: new `keybinding <key> <mods> ?<ptype>? <cmd>` command.
- Mods parsed from `+`-joined strings (e.g. `"ctrl+shift"`).
- 6 new core tests, 3 new Lcl tests.

### ~~4. Dropdown widget (MVP row-editor)~~ ✓

- New kinds `UIK_DROPDOWN` + `UIK_OPTION`. Leaf in main layout; popup
  rendered via `lk_render_build_overlays` overlay pass.
- New state keys `LKS_SELECTED_INDEX`, `LKS_HOVER_INDEX`. `LKS_EXPANDED`
  reused for open/closed.
- Event-driven value sync: `LK_EVENT_VALUE_CHANGED` + `source_value` on
  `lk_command` route selection (and text_input buffer changes) through
  normal translator/command pipeline.
- Multi-arg presentations: `lk_presentation` now carries up to
  `LK_PRES_MAX_ARGS` (4) values, copied into the emitted command's
  `args`.  DSL: `-present (action (remove_row $rid))`.
- Lean overlay machinery in `lk-dropdown.c` (render + hit-test +
  dismiss).  Generalization path documented in `docs/overlays.md`.
- Example: `examples/budget-dsl.lcl` — row-based budget tracker.
- 7 new core tests, 2 new Lcl tests (178 core, 38 Lcl).

### ~~5. Resizable split layout~~ ✓

- New kinds `UIK_SPLIT_H` / `UIK_SPLIT_V` (`src/core/lk-split.c`): two
  panes with a draggable 5 px divider band owned by the split node
  itself (no divider node — a band hit lands on the split because no
  child covers it).  One child = plain container; zero = bg only;
  extras beyond two ignored (zero rects).
- Ratio is per-mille (0..1000).  Priority: `LKS_SPLIT_RATIO` state
  (written by dragging) > `UIP_SPLIT_RATIO` prop > 500.  **Initial
  values are props, not state pokes** — deliberate, learning from the
  dropdown `-value` gap.  Layout clamps panes to >= 40 px (MIN_PANE).
- New core facility: **pointer capture** — `captured_id` on `lk_ui`,
  `lk_capture_set/clear/current`.  While set, the SDL loop targets
  POINTER_MOVE/UP at the captured node (bypassing hit-test) and
  suppresses hover; `lk_ui_end_frame` clears it via the same
  removed-not-readded filter as focus.  This is what lets a divider
  drag keep tracking after the cursor leaves the band.
- Divider geometry always derives from the split's own laid-out rect
  (render) or its stashed content rect (`LKS_SPLIT_C*`, events) —
  never recomputed from ancestors (weft's nested-splits bug).
- Bindings: kinds `split_h`/`split_v`, prop `split_ratio`; DSL procs +
  whitelist + exports.  Example: `examples/split-dsl.lcl`.
- Rider: `lk_tree_dump` kind-name table completed (text_input, scroll,
  dropdown, option, split_h, split_v — previously "unknown").
- 14 new core tests, 2 new Lcl tests (230 core, 52 Lcl).
- Deferred (known issues below): resize-cursor feedback, keyboard
  resize.

### 6. Per-character styled text

`STYLED_TEXT` widget kind that accepts an array of `(start, end, style)` runs.
Required for syntax-highlighted code display, colored logs, rich text.
See `docs/weft-gap-analysis.md` §3.

### ~~7. Overlays / positioning — generalization~~ ✓ (steps 1–6, "Proper")

All of `docs/overlays.md` landed (2026-07-25): overlay stack on
`lk_ui` (`src/core/lk-overlay.c`), `lk_anchor_resolve` with
flip/clamp, `UIP_HIDDEN` content subtrees + `lk_layout_subtree`,
focus traps, modal outside-click blocking, ESC dismissal in
`lk_event_route`, the dropdown migrated onto the stack, the tooltip
producer (`UIP_TOOLTIP` prop + `src/core/lk-tooltip.c`, hover
push/pop hook in `lk_hover_set`/`lk_hover_clear`), and app-level
modal support (`lk::overlay_push` / `lk::overlay_pop` bindings,
`examples/modal-dsl.lcl`).  All three exit criteria met — see
`docs/overlays.md`.  Remnants (dropdown popup scrolling, dropdown
`-value` flag) live in the known-issues list below.

### ~~8. Editor track (weft's document core + UIK_EDITOR)~~ ✓

All four stages of `docs/editor.md` landed (2026-07-26/27,
9b4ede9 → this change):

- **A**: `lk_document` piece table with transactional, observable
  mutation (deltas carry the actual bytes; subscribers notified once
  per committed transaction), opaque `lk_revision {hi,lo}` tokens,
  `lk_edit_history` as an ordinary origin-filtered subscriber.
- **B1**: typed resource refs — `UIV_RESOURCE` + `lk_resources` on
  `lk_ui`, generation-checked, dumpable (`editor="name"#17`) — and
  the self-contained render list (`LK_ROP_DRAW_RUN` + byte arena).
- **B2**: `lk_editor` view (anchored viewport, sticky-x, tab-stop
  segments) + the `LK_ED_*` command layer (one editing command = one
  transaction = one undo step) + the `UIK_EDITOR` widget.
- **C**: `lk_annot_store` (biased anchors transformed from committed
  deltas via subscription) + viewport-scoped, revision-stamped span
  snapshots with the ignore-stale-at-render policy.
- **D**: 30 Lcl procs (`lk::doc_*`, `lk::history_*`, `lk::editor_*`
  incl. `lk::editor_command` by command name, `lk::annot_*`), opaque
  wrappers whose dependents retain their dependencies' Lcl values
  (finalizer order = refcount order — no destruction-order UAF),
  DSL `editor` widget + `editor` prop key,
  `examples/editor-dsl.lcl` (editor in a split beside live status
  labels, annotation-driven styled spans, command buttons).
- Deferred with landing spots (docs/editor.md §13): ~~wrapping~~
  (landed 2026-07-28 — docs/editor-wrap.md: W1 wrap engine +
  horizontal scroll, e02bf4e; W2 `lk::editor_wrap`, doc line/column
  procs, example wrap + line:col + Ctrl+G goto modal),
  column selection, cursor blink, IME preedit, tree-sitter,
  annotation persistence, ~~search/goto UI~~ (goto-line demonstrated
  app-level in `examples/editor-dsl.lcl`; search UI still open),
  multi-view cursor sync,
  shared position markers, keybindings-as-data, the
  widget-registration protocol upgrade.
- Note: item 6 above (per-character styled text) is now delivered
  for the editor by span-split `DRAW_RUN` rendering; a standalone
  `STYLED_TEXT` widget for labels/logs remains open.

### ~~9. Weft surface track (interior presentations + weft-mini)~~ ✓

All three stages of `docs/weft-surface.md` landed (2026-07-29):
S1 interior/range presentations (typed presentation values, the one
translator matcher with button discrimination, annot-store adapter,
editor offer path), S2 primitives (`lk_doc_find`, lcl-io wiring for
the runner), S3 `examples/weft-mini.lcl` — the north-star artifact:
a two-pane Acme-flavored editor in pure Lcl (self-reading left
buffer, revision-driven plumbing producer, execute-into-pane,
look/search-next, Ctrl+F as composition, all gesture policy in the
example).  Landing notes in `docs/weft-surface.md` §5.  Horizon
items (tree-sitter producer, annot persistence, inline output
records, keyboard activation) recorded there.


## Known issues / small hardening items

Low-priority polish, mostly surfaced during the MVP-1.0 dropdown work.
None block shipping the budget app; revisit as the app surfaces them.

- ~~**Dropdown popup clipping near viewport edges.**~~ Fixed
  (2026-07-25): popup geometry goes through `lk_anchor_resolve` —
  flips above the trigger when overflowing the bottom, clamps x/y
  into the viewport (`docs/overlays.md` step 3).
- ~~**Dropdown popup doesn't scroll.**~~ Fixed (polish F3): popups
  taller than `DROPDOWN_POPUP_MAX_HEIGHT` scroll via `LKS_POPUP_SCROLL`
  (wheel over the open popup, keyboard hover navigation scrolls the
  hovered row into view, offset clamped, reset on open/close) with a
  clipped option list and a track+thumb overflow indicator drawn from
  the trigger's `scrollbar_track`/`scrollbar_thumb` style fields.
- ~~**`value` prop on dropdown is not wired.**~~ Fixed (polish F3):
  `UIP_VALUE` (string, interned) selects the option whose TEXT matches.
  Priority `LKS_SELECTED_INDEX` state > `UIP_VALUE` prop > index 0 —
  the `split_ratio` state>prop>default pattern.  Exposed as `value` in
  the Lcl prop table and the DSL `_prop_schema`; no state poking
  needed (and none possible — see the LKS_USER barrier below).
- **`lk_v_none()` leaves the union uninitialized.**  Reading `.as.i` on a
  NONE value yields garbage.  Low-impact (callers should check `.tag`)
  but trivially hardened by zero-initing the union.
- **Moving a node between parents wipes its retained state.**  Because
  the diff emits REMOVED+ADDED for moves, `lk_state` entries keyed on
  that node_id get GC'd.  Documented behavior but worth flagging if
  anyone builds a reorder UI.
- **Split divider has no cursor feedback.**  The pointer stays the
  default arrow over the divider band; a resize cursor (SDL
  `SDL_SetCursor`) needs a hover-over-band hook in the run loop.
  Deferred from the split work.
- **Split divider has no keyboard resize.**  The divider is not
  focusable and arrow keys don't nudge the ratio.  Deferred from the
  split work.


## Design-coherence items

Places where the implementation diverges from `design_draft.md`/`layers.md`
in mechanism (not meaning). Tracked so they don't fossilize; none block
shipping.

- ~~**Overlay pass is dropdown-specific.**~~ Resolved (2026-07-25):
  the generalized stack landed — `lk_render_build_overlays` iterates
  `ui->overlays` with per-kind dispatch, and subtree-content overlays
  (`content_root_id` + `UIP_HIDDEN`) are the anchored-subtree shape
  `layers.md` §7 called for.  See `docs/overlays.md`.
- ~~**`LK_EVENT_VALUE_CHANGED` re-enters `lk_event_route` mid-dispatch.**~~
  Resolved (polish F2): synthetic emissions (`VALUE_CHANGED`,
  `FOCUS_CHANGED`) now go through a pending queue on `lk_ui`
  (`lk_event_enqueue`), drained FIFO at the end of the OUTERMOST
  `lk_event_route` call — re-entrant routes never double-drain, and
  events enqueued during a drain join the same loop (safety cap 64
  dispatches per drain; overflow dropped and counted in
  `ui->pending_dropped`).  `lk_ui_flush_events` drains outside
  routing (the SDL run loop calls it after `end_frame` and after the
  event-poll loop).
- ~~**Widget-private state keys are a public flat enum.**~~ Resolved
  (polish F3), with one honest deferral: `UIP_VALUE` removed the last
  legitimate reason for scripts to poke widget state, so the Lcl
  `lk::state_set`/`state_get` procs now reject keys below `LKS_USER`
  (256) — "scripts never poke widget state" is enforced, not
  conventional.  The C enum itself stays in `lk.h` (widget files and
  tests reference it heavily; moving it to an internal header was
  judged more churn than value) behind an explicit "INTERNAL WIDGET
  STATE — not API" comment block.
- ~~**Derived geometry stored in retained state.**~~ Resolved (polish
  F3): all derived geometry moved to the per-frame `lk_widget_geom`
  scratch — a union-per-kind array parallel to `rects[]`, carried in
  `lk_layout_cfg.geom`, passed to render (`lk_render_build` geom
  param → per-node slot in the widget `render` vtable) and read by
  widget event handlers through `ui->geom` (host wires `lk_ui_geom(ui)`
  as `cfg->geom`; the SDL run loop does).  Migrated and deleted:
  `LKS_CURSOR_X`, `LKS_SEL_X0/X1`, `LKS_TEXT_ORIGIN_X`,
  `LKS_FONT_ID/SIZE` (text input), `LKS_TRIGGER_*` (dropdown, plus
  popup row metrics), `LKS_SPLIT_C*` (split content rect), and
  `LKS_SCROLL_MAX` (scroll).  Measure/layout no longer write derived
  values into retained state; `lk_state` holds interaction state only.
  NULL geom degrades safely (documented on `lk_widget_geom`).

## Deferred

These are documented in the design but should not be built until their dependencies exist.

- **Schema validation** (`lk_kind_schema`): useful once there are enough kinds to warrant it.
- **Accessibility output**: depends on the presentation system (Phase 5).
- **Animation/time model**: depends on rendering (Phase 3). Start with tick-driven (option A from design doc).
- **Layer system** (Phase 6): TreeLayer/SurfaceLayer interfaces, overlay anchoring, caching.
