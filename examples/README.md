# Examples

Each example exists to prove a capability — and to flush out what is
missing (findings go to `docs/TODO.md`, "Showcase-example findings").
Run one with `build/lcl_lk_main examples/<name>.lcl` (needs
`LK_BUILD_LCL=ON` and SDL3); `tools/smoke.sh` runs them all headless.

| Example | What it proves |
|---|---|
| `hello.lcl`, `hello-dsl.lcl` | Layer‑1 bindings vs the Layer‑2 DSL, same app |
| `budget-dsl.lcl` | Dynamic rows, dropdowns, `value_changed` sync |
| `modal-dsl.lcl` | A modal from a hidden subtree: focus trap, outside-click block, ESC |
| `split-dsl.lcl` | `split_h` with a draggable divider and `split_ratio` |
| `forms-dsl.lcl` | Deed Lab: controlled tabs, grid, sliders, radios, checkboxes, live readout |
| `editor-dsl.lcl` | The multi-line editor: wrap, spans from annotations, goto-line as pure composition, multi-cursor |
| `weft-mini.lcl` | Two-pane Acme-flavoured editor: interior presentations, plumb / execute / look on ranges |
| `listener-dsl.lcl` | A CLIM listener: results are live values with presentations; Inspect prints proc bodies |
| `minesweeper-dsl.lcl` | The command model alone: every cell presents, translators discriminated by button |
| `stego-dsl.lcl` | Stego Lab: `image` buffers, hex byte access, load/save, native file dialogs |
| `paint-dsl.lcl` | Tiny Paint: the RGBA buffer as a canvas, pointer→pixel via `Lk::node_rect` |
| `plotter-dsl.lcl` | Plotter: the vector `canvas` display list, rebuilt when dirty; pan/zoom/crosshair |
| `tour-dsl.lcl` | Widget Tour: every kind on a tab; `--snap DIR` photographs itself headless |
| `sheet-dsl.lcl` | Cells: a mini spreadsheet of *controlled* `text_input`s — the app owns every cell's text; formulas, cycles, `text_align end` |
| `files-dsl.lcl` | File Explorer: a `listview` that builds only the rows on screen — 50 000 synthetic rows at O(visible); table as sugar (header buttons sort), cursor vs selection, right-click menu |
| `log-dsl.lcl` | Log Lens: `styled_text` lines wrapping at the pane width, spans for colour/underline, `path:line` ranges presented — click opens the file in an editor pane; right-click lists what applies |

Recipes that recur across them: global chords are plain 3-arg
`keybinding`s; first-frame focus is `Lk::focus_request`; gestures the
toolkit does not own live in `event_handler` and map window
coordinates through `Lk::node_rect`; runner-only Lcl packages
(`Io::`, `Regex::`, `Xoshiro::`) are probed with `catch` and degrade.
