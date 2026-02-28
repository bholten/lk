# Lcl UI Toolkit (“Tk done right” with CLIM semantics) — Design Dratf

## 0. Goals and Non-Goals

Goals

- Embeddable-first: tiny C core, easy to embed into existing apps, controllable event loop integration.
- Single executable friendly: compile-in scripts/resources; minimal external runtime requirements.
- Retained semantic UI tree: declarative view construction; diff/patch; stable identity; predictable updates.
- Introspectable and toolable: UI tree is queryable, dumpable, inspectable at runtime.
- CLIM-like semantics: UI is a projection of an application model; events carry meaning (presentations), not just coordinates.
- Tk-like ergonomics: simple DSL, straightforward binding, understandable layout rules.
- Low baseline cost: no giant background subsystems; opt-in features; minimal allocations.

Non-Goals (initially)

- Native platform look-and-feel parity.
- Full web/CSS feature surface.
- “Everything is extensible from script” (avoid Tk’s internal coupling).

## 1. Architectural Overview

### Layers

#### 1. Platform Backend (SDL3)

- Windows, input, timers, clipboard, cursor.
- Provides “events” and “surfaces”.

#### 2. UI Core (C, small, stable ABI)

- Owns: semantic tree storage, diff/patch, layout, hit-testing, focus, text editing primitives, render list building.
- Exposes: a minimal API to submit a view tree and receive events + commands.

#### 3. Renderer

- Default: simple 2D renderer (SDL renderer or your own minimal GPU path).
- Consumes a render list (draw commands) produced by UI Core.
- Renderer is replaceable, but the semantic tree is not.

#### 4. Lcl Integration

- Lcl constructs view trees (DSL/macros/functions).
- Lcl registers commands and handlers.
- Lcl receives events with presentation payloads and dispatches to commands.

Key point: scripts never poke internal widget state. They declare view + respond to commands/events.

## 2. The Big Unification: Tk’s Widget Tree × CLIM’s Presentations

### 2.1 Nodes and Identity (Tk’s “path names”, modernized)

Every element is a Node in a retained tree.

Each node has:

- id (stable key; hierarchical strings are fine; can be hashed internally)
- kind (button, text, vbox, list, canvas, etc.)
- props (immutable property map for the node)
- children (ordered)
- optional presentation payload (CLIM idea)
- optional role (accessibility + command mapping)

Rule: identity is stable across frames or you lose diff efficiency and state (focus, scroll, text cursor, etc.).

### 2.2 Presentations (CLIM)

A presentation is semantic meaning attached to a node (or a region of a node).

It is:

- ptype (presentation type / tag)
- pvalue (opaque payload; typically an app-level handle or key)
- optional metadata (e.g., “selection target”, “draggable”, etc.)

Hit-testing yields:

- node id
- presentation (if any)
- “part” (e.g., list item vs scrollbar thumb vs text selection region)

So input is interpreted as:

```
“User activated Customer#42”
not
“User clicked at (x,y)”.
```

## 3. Commands as the Primary Action Model (CLIM-inspired)

### 3.1 Command Definitino

Commands are named actions with a signature.

Example conceptual form:

```
Command(name="OpenCustomer", args=[CustomerId])
```

Commands can be invoked from:

- click/activate gestures
- keyboard shortcuts
- menus
- programmatic triggers

### 3.2 Translators (Gesture → Command)

Instead of “onClick callback attached to a widget” everywhere, we support rules:

On event activate with presentation type customer → OpenCustomer(customer_id)

You can still have Tk-style sugar that compiles into a translator + command.

### 3.3 Why this is “Tk done right”

Tk made it easy to bind strings to events but offered little structure.

This design keeps binding ease, but gives you:

- centralized discoverable actions
- consistent keyboard/menu integration
- better introspection (command log)
- a path to accessibility

## 4. The View Function: Declarative UI from App State


The UI is produced by a view function:

```
view(app_state) -> NodeTree
```

This is the “React-like” part, but kept small and embeddable.

### 4.1 Diff/Patch

UI Core retains:

- prev_tree
- next_tree

It computes:

- structural changes (add/remove/move nodes)
- prop changes
- layout invalidations
- renderer invalidations

### 4.2 Retained Local UI State

Some UI state should not live in app_state:

- scroll positions
- text cursor/selection
- hover/focus
- internal animation timers

UI Core keeps a small StateStore keyed by node id, holding:

- focus state
- scroll offsets
- textbox state
- transient interaction state

This is important for embeddability and low friction.

Rule: app_state is source-of-truth for meaningful state; UI Core stores interaction mechanics.

## 5. Layout System

Pick one coherent layout model (do not repeat pack/grid/place).

### 5.1 Layout Primitives

- row / column containers (flex-ish)
- spacing (gap, padding, margins)
- alignment (start/center/end, stretch)
- overflow (clip, scroll)
- min/max/preferred size constraints
- grid is optional later

### 5.2 Measuring Text

UI Core exposes:

- measure(text, font, size) -> metrics
- used by layout for size calculation

Text shaping (HarfBuzz etc.) can be behind a feature flag; basic Latin can work without it initially.

## 6. Rendering Model

UI Core produces a RenderList (a display list):

- fill rect
- stroke rect
- draw text run
- draw image
- clip begin/end

Renderer consumes it.

This keeps the core stable and allows:

- swap SDL renderer vs custom GPU later
- deterministic “snapshot testing” by comparing render lists

## 7. Event Model

### 7.1 Input → Event objects

SDL events become normalized UI events:

- pointer move/down/up/wheel
- key down/up/text input
- window resize/focus
- timer tick

### 7.2 Event Routing

Event dispatch uses a two-tier model:

1. **Hit-test** to determine the target node (+ presentation if any).

2. **Widget dispatch** (target → bubble):
   - The target node's widget `event` handler fires first (at TARGET phase).
   - If not handled, walk ancestors to root; each ancestor's widget handler fires (BUBBLE phase).
   - If any widget handler returns handled, routing stops here.
   - This gives built-in widgets (text input, scroll) first-right-of-refusal on their internal events (keystrokes, wheel) without leaking mechanics to user code.

3. **User handler dispatch** (capture → target → bubble):
   - Only reached if no widget handler consumed the event.
   - The user's `lk_event_handler_fn` fires in full DOM-style phases: capture (root → target parent), target, bubble (target parent → root).
   - This is where application-level event handling lives (global shortcuts, modal interception, logging).

4. **Translator dispatch**:
   - Only reached if neither widget nor user handlers consumed the event.
   - Walks ancestors looking for presentations that match registered translators.
   - Matching translators emit commands into the command queue.

**Rationale**: Unified capture → target → bubble (as in the DOM) was the original plan, but building interactive widgets revealed that text input must consume keystrokes and scroll must consume wheel events before user code runs. The two-tier model keeps widget internals encapsulated while preserving full DOM-style phases for application-level handling. This is closer to the CLIM model, where presentation methods have priority over application-level event handlers.

### 7.3 Focus & Text Input

UI Core owns:

- focus ring
- IME/text composition hooks (later)
- keyboard navigation baseline (tab, arrows, enter, escape)

## 8. Lcl-facing DSL (Tk-like ergonomics, CLIM under the hood)

You want something that feels like Tcl/Tk to author, but compiles into nodes/commands/translators.

### 8.1 Construction

A canonical pattern:

- (ui window "main" { ... })

Containers:

- (ui column id:"root" gap:8 { ... })

Widgets:

- (ui button id:"save" text:"Save" on:activate (cmd SaveDocument))

### 8.2 Presentations

- (ui present type:"customer" value:$cust.id { (ui label text:$cust.name) })

Now clicking the label can yield OpenCustomer($cust.id) without wiring per-item callbacks.

### 8.3 Bindings (Tk-ish sugar)

- (bind "main/**/button" activate (cmd Click))
- (bind (present-type "customer") activate (cmd OpenCustomer))

The “Tk feel” lives here: globbing selectors + declarative binds.

## 9. Introspection and Tooling (a first-class design requirement)

UI Core provides:

- ui.dump_tree() (for debug)
- ui.query(selector) (returns nodes, props, presentations)
- ui.dump_commands() (registered commands + signatures)
- ui.last_events() and ui.command_log()

This is where your Weft sensibilities shine: the UI is a surface you can inspect.

## 10. Embedding & Execution Model

Two supported modes:

### 10.1 Toolkit-owned loop

- ui_run(app, initial_state)

### 10.2 Host-owned loop (preferred for embedding)

host calls:

- ui_begin_frame(dt)
- ui_submit_tree(tree)
- ui_process_events(sdl_events)
- ui_render(renderer)
- ui_end_frame()

Commands are returned as a queue; host (or Lcl) executes them.

## 11. Versioning and Compatibility Story

A major Tk win was “scripts from 20 years ago still run.”

So define a compatibility discipline:

- UI Core ABI versioning
- Node kinds and property names are stable
- DSL sugar can evolve, but node schema remains stable

## 12. MVP Scope (ship something coherent early)

MVP widgets

- window root
- row/column
- label
- button
- textbox (single-line first)
- scroll container
- list / repeating view (with presentations!)
- basic menu/shortcut system via commands

MVP tooling

- tree dump + query
- command log

# UI Schema v0 proposal

Here’s a v0 that matches everything above. (Not code, but concrete enough to implement.)

## A) Core node shape

### Node

- id: NodeId (string or interned)
- kind: Kind (enum)
- props: Props (typed map per kind)
- children: [Node]
- class: [Class] (0+)
- style: StyleOverride? (optional partial)
- presentation: Presentation? (optional)
- role: Role? (optional; default derived from kind)
- flags: {focusable?, disabled?, hidden?}

### Presentation

- ptype: Symbol
- pvalue: PresentValue where PresentValue = u64 | string | bytes
- meta: {hint?, tags?} (optional)

## B) Kinds (keep small)

- window
- row, column, stack
- spacer
- label
- button
- textbox
- scroll
- list (or repeater)
- surface (Weft document surface)
- menu (later; can be sugar over list/stack)

## C) Layout/style fields (typed)

Common layout props:

- width/height: auto | px | pct
- min/max
- padding, margin
- gap
- align, justify
- overflow: visible|clip|scroll

Style fields (computed):

- font_id, font_size
- fg, bg
- border, radius
- focus_ring
- state variants derived from interaction state

## D) Accessibility semantics output

For each node, computed record:

- role: Role
- name: string?
- value: string|number|enum?
- state: {checked?, selected?, expanded?, disabled?}
- actions: [Action] where Action maps to commands

## E) Events and commands

UI events (normalized):

- pointer, key, text, wheel, window
- plus “semantic activation events” emitted by core: activate(node_id, presentation?)

Commands:

- name + args (typed-ish)
- emitted by translators or explicit bindings

Translators:

- (event_kind, selector, presentation_type?) -> command_template

## F) Debug/inspect overlays

Layer hooks:

- augment_tree(base_tree, context) -> extra_nodes
- augment_semantics(base_semantics) -> extra_semantics
- augment_render(render_list) -> render_list

# Design Notes

## Security Boundary for Presentations

**BAN RAW POINTERS FROM PASSING INTO LCL**

Presentations should be:

- small tagged values: u64, string, or (type, bytes) blob
- ideally stable IDs that reference app state keys (like primary keys)

Also: log/replay testing becomes possible if presentation values are serializable.

## Inspectability and diagnostics as first-class layers

Concrete proposal:

Core supports overlay layers that can contribute:

- extra nodes (debug chrome)
- extra semantics (why this layout size?)
- extra presentations (inspect handles)
- extra render annotations (boxes, baselines, IDs)

In other words: the UI tree is the “base layer,” and debug/inspect layers are separate passes that can augment it.

And absolutely include:

- ui.dump_tree(), ui.query(selector)
- ui.trace(events|commands|layout|diff)
- “show layout boxes” overlay

## Animation/time model

Two options:

- A) Tick-driven (delta-time)
  - every frame you pass dt and t
  - nodes can declare “I’m animating” → UI requests another frame
  - animations are computed from time + state
- B) Transaction-driven (tween timelines)
  - you schedule an animation: animate(node_id, prop, from, to, duration, easing)
  - UI core updates transient animated values over time

My recommendation:

- Start with A (simpler, deterministic, easy to test if you can fix time).
- Add a small helper library that implements B as sugar in Lcl.

Key schema piece: a node’s computed style/layout may depend on env.time and/or “animated values” stored in UI transient state.


## Threading model

Core is single-threaded.

Provide a tiny, portable “post to UI” mechanism:

- ui.post(fn_or_cmd) from any thread
- internally a lock-free or mutexed queue + SDL user event wakeup

Keep rendering single-threaded (at least initially).

If you ever add multithreaded rendering, do it behind the renderer boundary, not the UI core.

## Resources for single-exe + external override

This UI framework is focused on *embedding* and *single statically compiled executables* as an option -- although not necessarily "the only way" or even "the preferred way", we should never give up on the capability to ship single binaries.

As such, I’d add one crucial thing: content addressing.

Every resource has an ID like sha256:... + a friendly name.

Embedding uses the hash; external loading can override by name (dev mode), but builds can pin by hash (release mode).


## Text: UTF-8 now, shaping/IME/bidi staged

## Accessibility as another “layer”

One important constraint: accessibility cannot be only “extra text annotations.” It must include:
- role
- name/label
- value / state (checked, expanded, selected)
- actions (activate, increment, focus, etc.)

## Styling and theming in Lcl (not CSS)

The view tree is a reification of **meaning**, not appearance. The view
function's job is `Data -> Tree`: it describes what things are and their
structure. It does not describe how things look or how they are spaced.

Style is a **terminal projection phase**: it consumes the same semantic
vocabulary that layers produce (tags, state) and projects it into concrete
visual+spatial parameters (`lk_style`). It shares the data model with layers
but runs in a fixed pipeline position after them and before layout/render.

Key design decisions:

- **Style owns all visual and spatial properties**: fg, bg, font, padding, gap,
  border, radius, align, justify. The tree is semantic; style is presentational.
- **No cascading**. Each node's style is resolved independently by matching
  rules against (kind, tag, state). Inheritance is per-field: currently fg,
  font_id, and font_size inherit; all others do not. Each field's inheritance
  behavior is an explicit declaration, not an ad-hoc exception.
- **Tree props as overrides**: layout props (UIP_PADDING, etc.) remain in the
  API as an escape hatch. If set, they override the style-resolved value. But
  the default authoring pattern is to let the theme decide.
- **Tags bridge layers and style**: layers emit semantic tags; the theme maps
  tags to appearance. A syntax highlight layer tags a span `syntax.keyword`;
  the theme says `syntax.keyword -> fg:blue`. The layer never knows about blue.
- **Rule-based theme**: ordered list of (selector, partial_style) rules.
  v0 selectors match kind, tag, and state. No ancestry selectors initially.
- **Resolution order**: rules -> kind defaults -> tree prop overrides ->
  inheritance (only for inherited fields still unset). Explicit and strict.

See [styles.md](styles.md) for the full style system design.
See [layers.md](layers.md) for the layer system design.
