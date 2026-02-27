# Lk

**Lk** is a GUI toolkit designed for [Lcl](https://github.com/bholten/lcl).

It is a small, embeddable, retained-mode UI toolkit with Tk's
ergonomics and McCLIM's presentation/command semantics.

Strict C89, no dependencies for the core, an optional SDL3 backend,
and optional (but canonical) bindings to the
[Lcl](https://github.com/bholten/lcl) scripting language with a
declarative Layer‑2 DSL.

Version 0.1.0 -- pre-release.  MIT.

```lcl
app "Hello" #{width 320 height 160} {
    translator pointer_down greet Greet
    on Greet [lambda {cmd} { puts "hello, [get [get $cmd args] 0]" }]

    view {
        column "main" #{padding 12 gap 8} {
            label "l" #{text "A button that presents a value:"}
            button "b" #{text "Greet" focusable 1 present (greet (world))}
        }
    }
}
```

## Basic Idea

McCLIM meets Tk.

The host rebuilds a tree of nodes with **stable string ids** every
frame; lk diffs it against the previous frame and keeps per-node state
(cursor, scroll, selection) across the diff.

Nodes can **present** typed values; **translators** turn gestures on
presentations into **commands** -- the app handles commands, never raw
callbacks.

Application-owned objects (documents, editors, images, canvases) are
referenced from the tree through typed resource refs, so the tree
stays data.

## Build

```sh
cmake -B build && cmake --build build          # core + tests
build/lk_test

cmake -B build -DLK_BUILD_LCL=ON               # + Lcl bindings, DSL, runner
cmake --build build
build/lcl_lk_test                              # binding tests + doctests
build/lcl_lk_main examples/tour-dsl.lcl        # needs SDL3 + SDL3_ttf
```

## License

MIT -- see [LICENSE](LICENSE).
