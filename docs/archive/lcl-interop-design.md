# Question: 

Do we want the Lcl binding to drive the frame loop (like Tk's mainloop), or just build trees that the SDL loop renders? Given that lk already has a frame callback model (lk_frame_fn), I'd say the Lcl binding registers commands that build the tree declaratively, and the frame callback evaluates an Lcl expression each frame.

# Answer

Do host-driven loop as the core, and offer an optional Lcl-driven “mainloop” wrapper as sugar.

That gives you the best of all three worlds:
- Embeddability / “Lcl”: the host owns the loop; Lcl is a library you can drop into any program (games, tools, servers-with-UI, editors). This matches your embedded-first posture.
- Tk feel (when you want it): a tiny wrapper that looks like Tk’s mainloop and timers.
- CLIM feel: the UI is a projection (view(app_state) -> tree) plus command dispatch; this naturally fits a frame/tick model, and CLIM doesn’t require “the UI library owns the loop” so long as you preserve the command/event discipline.

The important part: don’t eval arbitrary Lcl every frame by default

You can do it for early testing, but long-term it’s better to structure it as:

- events → commands (from lk core)
- host applies commands to app_state (in Lcl)
- view function produces a new tree (in Lcl)
- lk diffs and renders

That’s the “CLIM spine” and it’s testable/replayable.

So: call a specific Lcl function (e.g. ui::view(state, ctx)) each frame, not “eval an expression string.” Keep it explicit and deterministic.

What’s “most Tk”, “most CLIM”, “most Lcl”?

Most Tk:
- UI toolkit owns a mainloop
- callbacks mutate widget state
- binding/event strings

You can support this as a surface UX, but it’s not your architecture.

Most CLIM:
- the app defines commands
- the UI is a projection of app model
- interactions invoke commands with semantic arguments (presentations)

This aligns best with host-driven frame + command queue

Lcl implements command handlers + view function

Most Lcl (given your goals):
- embeddable C core, tiny API
- script drives behavior but doesn’t “own the world”
- deterministic traces

So: host-driven loop is the most “Lcl”.

Recommended integration shape (MVP and beyond)
A) Two APIs: “embed mode” and “app mode”
1) Embed mode (primary)

Host (SDL loop) calls:

- lk_begin_frame(dt)
- lk_process_event(e)
- lk_collect_commands(queue)
- lcl_apply_commands(state, cmds) (in Lcl)
- tree = lcl_view(state, frame_ctx) (in Lcl)
- lk_submit_tree(tree)
- lk_end_frame()
- lk_render()

Lcl provides two user functions:

- ui::update(state, cmds) -> state (or mutates a state cell)
- ui::view(state, ctx) -> tree

This is React-ish, but the command/presentation system keeps it CLIM-ish.

2) App mode (optional wrapper)

Provide an Lcl function like:

- ui::main { init ... view ... update ... }

Internally it just spins SDL and calls embed mode, but it feels like Tk.

Concrete Lcl-facing contracts (keep them small)

1) Register commands

Lcl registers handlers:

- (ui.command 'OpenCustomer (fn [state id] ...))

This should compile to a dispatch table (symbol → function).

2) View function

One entrypoint:

- (ui.view (fn [state ctx] (window ...)))

Return a tree (your DSL).

3) Update function

One entrypoint:

- (ui.update (fn [state cmds] ...))

Or more CLIM-ish:

each command handler directly mutates/returns state (and update is just a dispatcher).

4) Frame context

ctx can include:

- time, dt
- focused node id
- maybe viewport size
- maybe “debug flags”

Don’t include raw SDL events; lk core already converted them into commands.

How to make it “interesting CLIM-style” (not conventional glue)

Two deliberate choices will make your system feel qualitatively different from Tk/IMGUI:

1) Treat commands as the unit of meaning (queue-first)

Even inside Lcl:
- commands are values
- you can log them, replay them, filter them
- you can run update in tests with a synthetic command stream

This is extremely “CLIM” and extremely “your ecosystem.”

2) Make presentations first-class in the tree DSL

So Lcl can write:

- (present 'customer cust.id (row ...))

…and the runtime naturally produces:

Activate(customer:42)

This keeps the semantic layer pure and makes accessibility/tooling “fall out.”

What I’d do right now for initial testing

You’re close, so do a pragmatic stepping stone:

In the frame callback, call one named Lcl function: ui::frame(dt, width, height) that:

- drains commands from lk and updates state
- returns the new tree

That gets you moving quickly without committing to “string eval every frame.”

Then later split it cleanly into update/view.

Bottom line

Core design: host (SDL) owns the loop; Lcl supplies update/view and command handlers.

Optional sugar: an Lcl mainloop wrapper for “Tk feel.”

Avoid: “eval arbitrary expression each frame” as the long-term model.
