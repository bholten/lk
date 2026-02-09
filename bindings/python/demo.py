#!/usr/bin/env python3
"""
Headless fruit selector -- validates the lk Python binding.

Equivalent to the C demo (src/sdl/demo.c) but without SDL rendering.
Exercises: tree building, frame diffing, layout, hit-testing, event routing,
presentations, translators, commands, and focus management -- all from Python.
"""

from lk import UI, Kind, Prop, Event, Key, ChangeKind

FRUITS = ["Apple", "Banana", "Cherry", "Date", "Elderberry", "Fig"]


def build_frame(ui, tree, selected):
    """Build the fruit selector tree."""
    w = tree.add_node("main", Kind.WINDOW)
    col = tree.add_node("root", Kind.COLUMN)
    tree.add_prop(col, Prop.PADDING, 20)
    tree.add_prop(col, Prop.GAP, 12)

    title = tree.add_node("title", Kind.LABEL)
    tree.set_text(title, "Fruit Selector")

    list_col = tree.add_node("list", Kind.COLUMN)
    tree.add_prop(list_col, Prop.GAP, 4)

    items = []
    for i, fruit in enumerate(FRUITS):
        prefix = "> " if i == selected else "  "
        btn = tree.add_node(f"item_{i}", Kind.BUTTON)
        tree.set_text(btn, f"{prefix}{fruit}")
        tree.add_prop(btn, Prop.PADDING, 6)
        tree.add_prop(btn, Prop.FOCUSABLE, True)
        tree.add_presentation(btn, "item", i)
        items.append(btn)

    status = tree.add_node("status", Kind.LABEL)
    if 0 <= selected < len(FRUITS):
        tree.set_text(status, f"Selected: {FRUITS[selected]}")
    else:
        tree.set_text(status, "Selected: (none)")

    tree.set_root(w)
    tree.append_child(w, col)
    tree.append_child(col, title)
    tree.append_child(col, list_col)
    for btn in items:
        tree.append_child(list_col, btn)
    tree.append_child(col, status)


def main():
    ui = UI()
    selected = -1

    print("=== lk Python Binding Demo ===\n")

    # Register translators
    ui.add_translator(Event.POINTER_DOWN, "item", "Select")
    ui.add_translator(Event.KEY_DOWN, "item", "Select")

    # -- Frame 1: build initial tree --
    print("--- Frame 1: initial tree ---")
    tree = ui.begin_frame()
    build_frame(ui, tree, selected)
    changes = ui.end_frame()
    print(f"Changes: {len(changes)}")
    for c in changes:
        print(f"  {c}")
    assert all(c.kind == ChangeKind.ADDED for c in changes), \
        "Expected all ADDED on first frame"

    # -- Layout --
    print("\n--- Layout (800x600, stub measurer) ---")
    cur = ui.tree()
    rects = cur.layout(800, 600)
    root_ix = cur.root
    print(f"  root ({cur.node_id_str(root_ix)}): "
          f"({rects[root_ix].x}, {rects[root_ix].y}, "
          f"{rects[root_ix].w}, {rects[root_ix].h})")
    for i in range(len(FRUITS)):
        ix = cur.find_by_id(f"item_{i}")
        r = rects[ix]
        print(f"  item_{i} ({FRUITS[i]}): "
              f"({r.x}, {r.y}, {r.w}, {r.h})")

    # -- Hit test + pointer click on Cherry (item_2) --
    print("\n--- Simulate click on Cherry ---")
    item2_ix = cur.find_by_id("item_2")
    r2 = rects[item2_ix]
    hit = cur.hit_test(rects, r2.x + 5, r2.y + 5)
    print(f"  Hit test at ({r2.x+5}, {r2.y+5}) -> "
          f"{cur.node_id_str(hit)} (ix={hit})")
    assert hit == item2_ix, f"Expected item_2, got ix={hit}"

    ev = ui.make_event(Event.POINTER_DOWN, x=r2.x + 5, y=r2.y + 5,
                       target=hit)
    ui.route_event(ev)

    cmds = ui.commands()
    print(f"  Commands: {len(cmds)}")
    for cmd in cmds:
        print(f"    {cmd}")
    assert len(cmds) == 1, f"Expected 1 command, got {len(cmds)}"
    assert cmds[0].name == "Select"
    assert cmds[0].arg(0) == 2, f"Expected arg 2 (Cherry), got {cmds[0].arg(0)}"
    print(f"  -> Selected: {FRUITS[cmds[0].arg(0)]}")
    selected = cmds[0].arg(0)

    # -- Frame 2: rebuild with selection --
    print("\n--- Frame 2: rebuild with Cherry selected ---")
    ui.clear_commands()
    tree = ui.begin_frame()
    build_frame(ui, tree, selected)
    changes = ui.end_frame()
    print(f"Changes: {len(changes)}")
    for c in changes:
        print(f"  {c}")
    # Expect UPDATED for nodes whose text changed (item_2, status)
    updated = [c for c in changes if c.kind == ChangeKind.UPDATED]
    print(f"  ({len(updated)} UPDATED)")
    assert len(updated) >= 2, f"Expected at least 2 UPDATED, got {len(updated)}"

    # Verify the text changed
    cur = ui.tree()
    item2_ix = cur.find_by_id("item_2")
    status_ix = cur.find_by_id("status")
    print(f"  item_2 text: {cur.node_text(item2_ix)!r}")
    print(f"  status text: {cur.node_text(status_ix)!r}")
    assert "> Cherry" in cur.node_text(item2_ix)
    assert "Cherry" in cur.node_text(status_ix)

    # -- Key event: Return on focused item --
    print("\n--- Simulate Return key on item_4 (Elderberry) ---")
    item4_ix = cur.find_by_id("item_4")
    item4_nid = ui.intern("item_4")
    ui.focus_set(item4_nid)
    focus_ix = ui.focus_current()
    print(f"  Focus set to: {cur.node_id_str(focus_ix)} (ix={focus_ix})")

    ev = ui.make_event(Event.KEY_DOWN, keycode=Key.RETURN, target=focus_ix)
    ui.route_event(ev)
    cmds = ui.commands()
    print(f"  Commands: {len(cmds)}")
    for cmd in cmds:
        print(f"    {cmd}")
    assert len(cmds) == 1
    assert cmds[0].arg(0) == 4, f"Expected 4 (Elderberry), got {cmds[0].arg(0)}"
    print(f"  -> Selected: {FRUITS[cmds[0].arg(0)]}")
    selected = cmds[0].arg(0)

    # -- Focus cycling --
    print("\n--- Focus cycling (Tab x8) ---")
    ui.focus_clear()
    for i in range(8):
        nid = ui.focus_next()
        name = ui.intern_str(nid) if nid else "(none)"
        print(f"  Tab -> {name}")
    # Should wrap around: item_0..item_5, item_0, item_1
    last_nid = ui.focus_next()
    last_name = ui.intern_str(last_nid)
    print(f"  Tab -> {last_name}")

    # -- Command handler callback --
    print("\n--- Command handler callback ---")
    ui.clear_commands()
    handler_log = []

    def on_command(cmd):
        handler_log.append(cmd.name)

    ui.set_command_handler(on_command)

    # Rebuild frame with current selection
    tree = ui.begin_frame()
    build_frame(ui, tree, selected)
    ui.end_frame()

    cur = ui.tree()
    item0_ix = cur.find_by_id("item_0")
    ev = ui.make_event(Event.POINTER_DOWN, x=0, y=0, target=item0_ix)
    ui.route_event(ev)
    print(f"  Handler called with: {handler_log}")
    assert handler_log == ["Select"], \
        f"Expected ['Select'], got {handler_log}"

    # -- Identical frame: no changes --
    print("\n--- Frame 4: identical rebuild = no changes ---")
    ui.clear_commands()
    tree = ui.begin_frame()
    build_frame(ui, tree, selected)
    changes = ui.end_frame()
    print(f"Changes: {len(changes)}")
    assert len(changes) == 0, f"Expected 0 changes, got {len(changes)}"

    # -- Summary --
    print("\n=== All checks passed! ===")
    ui.destroy()


if __name__ == "__main__":
    main()
