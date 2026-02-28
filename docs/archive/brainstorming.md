# Lcl/Lk

## Goals:
- Embeddable
- Cross-platform
- Trivial statically-compiled binaries
- Mix of Tk and McCLIM

## Overall Architecture:
- Retained semantic tree + declarative diff

Build a tree of nodes in Lcl, then C performs diff + layout + render.

A small C UI kernel that owns input/layout/render, exposes an introspectable semantic tree with stable node IDs, and lets Lcl declare views + bind events, while the renderer stays immediate-mode and cheap.

The “right” abstraction boundary: keep the GUI kernel non-scriptable

Tk let scripts reach deep into widget internals. Convenient, but it creates:
- performance cliffs,
- hard-to-change internals,
- implicit coupling everywhere.

Hard boundary:

C kernel owns:
- event loop integration
- scene graph recording (even if IMGUI)
- layout engine
- input focus + capture
- text editing primitives
- rendering + invalidation

Lcl owns:
- declaring UI structure
- app state + reducers / callbacks
- binding events to commands
- constructing “widgets” as macros/functions

### Layout: don’t repeat pack/grid/place

Pick one modern layout model and nail it.

I’d do:

- Flexbox-ish as the default (row/column, gap, align, justify, wrap)

Optional grid later, but only if you truly need it

A huge “Tk done right” win is:

- layout is predictable
- measurement is explicit
- no spooky action at a distance

### Identity: Tk path names were genius—recreate the idea

Tk widget paths (.main.left.list) were:

- human-readable
- stable
- introspectable

In your world:

- every node has a stable key (id).

Lcl DSL can auto-generate hierarchical IDs via lexical structure.

Example vibe (pseudo-Lcl):

```
window main { vbox { button id:"build" text:"Build" on:click build! } }
```

But under the hood:

- main/vbox[0]/button[build]

Now you can:

- bind events by pattern (bind "main/**/button" click ...)
- query the tree (ui query main/**/list)
- build tooling (inspectors, live reload, macro expanders)

That’s Tk’s superpower, modernized.

### Events: steal Tk’s binding model, but make it typed

Tk’s bind is still one of the best UX ideas in GUI history.

Do it, but:

- events are structured objects, not strings
- handlers are Lcl closures/commands, but their signatures are stable

Also: add capture/bubble phases (DOM got this right):

- capture from root → target
- bubble target → root

This enables “global shortcuts,” “modal layers,” etc., without hacks.

### The hard part you must budget for: text

If you want “Tk done right,” your Text widget equivalent is the make-or-break.

Minimum viable:

- UTF-8 storage
- selection, cursor movement
- clipboard
- basic shaping (HarfBuzz)

IME composition support (platform-specific, but SDL helps somewhat)

Given you’re building Weft: you already know text surfaces become an engine, not a widget. So I’d make “text surface” a first-class primitive in the kernel, not a pile of widgets.

### Rendering: don’t chase “native widgets”

We don't really care about respecting host themes. This is not GNOME or KDE.

Modern “toolkit reality” is: consistent custom rendering wins for embeddability.

So: ship a clean, minimal theme system:

- colors, spacing, radii, typography scale
- stateful styling (hover, active, disabled, focus)
- no CSS; just a small property table

Tk tried to be native-ish, then fought that forever. “Tk done right” embraces coherent custom UI.

### Presentations: “the UI is a structured explanation of itself”

CLIM’s presentation system is the big loss.

In CLIM, you don’t just draw pixels — you draw objects with meaning. The system remembers: “that thing on screen is Customer #42”, and events carry that meaning back to you. It’s not “click at x,y”, it’s “user clicked this object”.

Why this matters for your world:

- It’s the cleanest answer to “introspection” and “toolability”.
- It makes command dispatch natural (see next point).
- It’s also the best model I know for debuggable UIs.

How to steal it without becoming CLIM:

- When you record your semantic tree each frame, attach a value (an opaque handle) to nodes: e.g. {type: "customer", key: "42"} or {kind: Symbol, payload: u64}.
- Hit-testing returns (node_id, presentation_value, role) — not just coordinates.
- Your event delivered to Lcl carries that payload.

This dovetails perfectly with your “IMGUI + semantic tree” plan: presentations are just semantic metadata on nodes.

### Commands: “events call commands, not callbacks”

CLIM strongly separates:

- Commands: named actions with arguments and constraints
- Presentations: produce objects that can satisfy command args
- Translators: “clicking this object can invoke that command with these args”

This is ridiculously coherent. And it’s exactly the kind of “language-game” compositionality you like.

Steal the shape:

- Define a command as a first-class value: (command "OpenFile" [path] ...)
- Input gestures (mouse/menu/keyboard) map to commands.
- Presentations can supply arguments.

In Tk, everything devolves into ad-hoc callbacks. CLIM’s model gives you global reasoning about UI behavior.

### Output records: “draw now, inspect later”

CLIM maintains “output records” — a retained structure of what was drawn — enabling repaint, incremental redisplay, and inspection.

Your plan already re-invents a modern version as “semantic tree capture”, but CLIM’s lesson is:

Make the recorded structure a first-class artifact.

- dump it
- query it
- annotate it
- diff it

This is exactly how you make “Tk done right” feel alive for debugging and tooling.

### Incremental redisplay: “update only what changed, but don’t make the author suffer”

CLIM tried to give you efficient updates without requiring the programmer to micro-optimize.

In your architecture, you can do this simply:

- Keep last frame’s semantic tree
- diff by stable IDs
- only re-layout/re-render dirty subtrees

This is basically “CLIM’s incremental redisplay” in a small, modern form.


### The McCLIM ideas I would not steal (or would steal carefully)

#### The full generic-function protocol / deep metaobject extensibility

CLIM is a Lisp cathedral: everything is open, everything is generic, everything is extensible.

That is the opposite of your “small kernel, stable ABI, embedded-first” constraint.

What to do instead:

- Provide one extension mechanism: “custom widget = function/macro that emits nodes”.
- And optionally: “custom draw node” with a small vtable in C if needed.

#### The “pane” model and its complexity

CLIM’s panes/layout/composition can get intricate. If you adopt flex-ish layout, you’ll get 90% of usability with 10% of complexity.

#### Device independence as an abstraction religion

CLIM was built in an era where it was plausible to target many graphics substrates.

You can keep practical device independence (SDL now, maybe a different backend later), but don’t architect around it.


#### The coherence rule that prevents a “Frankenstein GUI”

If you take only one principle from CLIM, take this:

UI is a projection of an application model, and user input is interpreted in terms of that model.

