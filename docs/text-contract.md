# Text Contract v1 — Design Draft

Replaces the single global font and whole-string measure/draw with a
proper text backend contract. This is step 2 of the post-review
sequence ("b1"): it fixes text-input correctness today and is the
prerequisite for the editor widget (`UIK_EDITOR`) later. It does NOT
hand-roll FreeType/HarfBuzz — SDL3_ttf's `TTF_TextEngine` (glyph
atlas, HarfBuzz shaping inside) is the substrate, with the contract
designed so a hand-rolled stack could replace it behind the same
interface if SDF/GPU text ever matters.

## 1. Current state (the debt)

- **One global font.** `lk_window` holds a single `TTF_Font *`;
  `lk_style.font_id` and `font_size` exist but are dead fields — no
  code path reads them.
- **Whole-string measure.** `lk_measure_text_fn(ud, text, *w, *h)`
  carries no font, no size, no baseline. Widgets cannot ask "where is
  byte 7?" or "what index is at x=52?".
- **Whole-string textures.** The SDL backend renders each interned
  string to its own texture, cached by `(str_id, color)`. Wrong
  granularity for an editor: a 10k-line buffer of unique lines
  thrashes the cache; the atlas path caches *glyphs*.
- **Consequences in `lk-text-input.c`:** cursor x computed by
  re-measuring a prefix; selection rectangle from *average char
  width* (wrong for proportional fonts); no click-to-position at
  all; LEFT/RIGHT/BACKSPACE move and delete single *bytes*, so
  multibyte UTF-8 input gets corrupted.

## 2. Goals

- Multiple fonts and sizes per UI, selected by the theme
  (`font_id`/`font_size` become live).
- Baseline-aware measurement.
- Byte-index ↔ pixel-x mapping in both directions, at codepoint
  granularity (the enabler for cursor placement, click-to-position,
  and exact selection rectangles).
- Glyph-atlas rendering in the SDL backend; delete the per-string
  texture cache.
- UTF-8-correct cursor motion and editing in the text input.
- Completed keycode map (digits, PageUp/PageDown, F-keys) — small
  rider, batched here because it touches the same input surface.

## 3. Non-goals (deferred, isolated behind the contract)

- Hand-rolled FreeType/HarfBuzz atlas, SDF/GPU text.
- Bidi (needs FriBidi/SheenBidi regardless of stack).
- Baseline-aligned row layout (labels stay box-centered until mixed
  fonts in one row actually occur).
- Grapheme clusters (codepoint granularity for v1, like weft).
- Soft wrap / multi-line measurement (the editor widget owns that).
- DPI scaling of font sizes (px ints for v1).

## 4. Design

### 4.1 The text backend vtable

Replaces `lk_measure_text_fn` + `measure_ud` wholesale (clean break;
we are pre-1.0 and the test churn is mechanical).

```c
typedef struct lk_text_metrics {
  lk_i32 w, h;      /* tight box of the run */
  lk_i32 baseline;  /* top of box -> baseline, px */
} lk_text_metrics;

typedef struct lk_text_backend {
  void *ud;
  void (*measure)(void *ud, lk_str run, lk_u16 font_id,
                  lk_u16 font_size, lk_text_metrics *out);
  lk_i32 (*x_from_index)(void *ud, lk_str run, lk_u16 font_id,
                         lk_u16 font_size, lk_u32 byte_ix);
  lk_u32 (*index_from_x)(void *ud, lk_str run, lk_u16 font_id,
                         lk_u16 font_size, lk_i32 x);
  lk_i32 (*line_height)(void *ud, lk_u16 font_id, lk_u16 font_size);
  lk_u16 (*register_font)(void *ud, const char *path);
} lk_text_backend;
```

`register_font` returns a new `font_id` (>= 1; 0 is both "default
face" and the failure sentinel). Registration lives in the vtable —
backend-neutral by decision (see §6) — so the contract is
self-contained and the stub can hand out ids, making font selection
testable headlessly.

Contract rules:

- `run` is a single-line UTF-8 run (no newlines; caller splits).
- `byte_ix` in/out values are always codepoint-boundary-aligned.
  `index_from_x` snaps to the nearest boundary (nearest edge, not
  floor — clicking the right half of a glyph puts the cursor after
  it).
- `font_id = 0` means the default face; `font_size = 0` means the
  face's default size. Widgets pass `styles[n].font_id/font_size`
  straight through, so a NULL-style caller degrades to defaults.
- `x_from_index(run, len)` equals `measure(run).w` — one code path
  for "cursor at end".

`lk_layout_cfg` changes:

```c
typedef struct lk_layout_cfg {
  const lk_text_backend *text;   /* replaces measure_text/measure_ud */
  ...
} lk_layout_cfg;
```

`lk_measure_text_stub` is replaced by `lk_text_backend_stub()` — a
deterministic monospace backend (fixed advance, e.g. 8×16 px,
baseline 12) so every headless test can assert exact cursor and
selection geometry, not just "some rect exists". Codepoint-aware:
advance counts codepoints, not bytes, so UTF-8 tests are exact too.

### 4.2 Fonts: face registry, instances owned by the backend

The core never touches font files. Faces are registered with the
backend and referred to by `lk_u16 font_id`:

```c
/* Sugar on the SDL backend (lk-sdl.h) — delegates to the vtable slot */
lk_u16 lk_window_register_font(lk_window *win, const char *path);
```

- `lk_window_cfg.font_path`/`font_size` keep working and become face
  0 / its default size — existing apps and the DSL `-font` flag are
  unchanged.
- The backend lazily opens `(face, size)` instances on first use and
  caches them in a small table (SDL_ttf sizes are per-open;
  `TTF_SetFontSize` on a shared handle would thrash its glyph cache).
- The theme selects fonts: a rule sets `font_id`/`font_size` like any
  other style field (already plumbed — `LK_SF_*` flags exist).
- Lcl binding: `lk::register_font $win <path>` returns the id;
  DSL theme rules gain `-font_id`/`-font_size` (parse-only change,
  the fields already exist in `lk_style`).

Content-addressed resources (design_draft.md "sha256:..." note) stay
future work; ids are registration-order for v1.

### 4.3 Render command changes

`DRAW_TEXT` gains the font fields (4 bytes):

```c
typedef struct lk_render_cmd {
  lk_u8 op;
  lk_rect rect;
  lk_color color;
  lk_u32 str_id;
  lk_u16 font_id;    /* NEW */
  lk_u16 font_size;  /* NEW */
} lk_render_cmd;
```

Widget render functions copy them from `style`. Render-list snapshot
tests extend to assert them.

### 4.4 SDL backend implementation

- Create one `TTF_TextEngine` via `TTF_CreateRendererTextEngine` per
  window, plus one scratch `TTF_Text` (weft's proven pattern —
  `weft/editor/src/renderer.c` is the reference).
- `DRAW_TEXT`: set font/string/color on the scratch `TTF_Text`, then
  `TTF_DrawRendererText(text, x, y)`. Color is per-draw state — free.
  The engine's internal glyph atlas does the caching.
- `measure`: `TTF_GetStringSize` on the `(face,size)` instance;
  baseline from `TTF_GetFontAscent`.
- `x_from_index` / `index_from_x`: `TTF_GetTextSubString` /
  `TTF_GetTextSubStringForPoint` on the scratch `TTF_Text`.
- **Delete** the per-string texture cache: `text_cache_entry`,
  `text_cache_get`, `text_cache_clear`, stats, probe warnings,
  `text_cache_cap` config. (~250 lines removed. If profiling the
  budget demo later shows scratch-`TTF_Text` churn matters, a small
  `TTF_Text` object cache can come back behind the same contract —
  decision deferred until there are numbers.)

### 4.5 Text input correctness (rides on the new contract)

- New `src/core/lk-utf8.c/.h` — codepoint helpers lifted from weft's
  `utf8.c` (C89-ified): `lk_utf8_next`, `lk_utf8_prev`,
  `lk_utf8_is_boundary`. Shared later by the editor widget.
- LEFT/RIGHT/HOME/END, BACKSPACE/DELETE operate on codepoints.
- Cursor x via `x_from_index` (no prefix re-measure); selection
  rectangle from two `x_from_index` calls (average-char-width math
  deleted).
- `POINTER_DOWN` inside the text input sets the cursor via
  `index_from_x` (finally: click-to-position). Click+drag selection
  stays out of scope (needs drag capture — editor-track work).
- `LKS_CURSOR_X` derived-state hack: unchanged in this phase (it is
  on the design-coherence list; moving derived geometry to per-frame
  scratch is its own change and shouldn't ride shotgun here).

### 4.6 Keycode rider

Add `LKK_0..9`, `LKK_PAGEUP/PAGEDOWN`, `LKK_F1..F12` to the enum,
the SDL map, and the binding lookup tables. Pure enumeration work.

## 5. Staging

Each stage lands green on its own:

- **A. Contract + stub + core threading.** Add `lk_text_backend`,
  swap `lk_layout_cfg`, convert widgets, port tests to the stub
  backend (mechanical), delete `lk_measure_text_fn`. Style
  `font_id`/`font_size` flow into render commands.
- **B. SDL TTF_TextEngine.** Implement the vtable, face registry,
  scratch text; delete the texture cache; wire `register_font` +
  binding + DSL flags. Verify with the demo + budget app.
- **C. Text input correctness.** `lk-utf8.c`, codepoint motion,
  `x_from_index`/`index_from_x` cursor+selection+click. New tests:
  exact selection rects under the stub, multibyte editing, click-to-
  position (incl. multibyte and click-past-end).
- **D. Keycode rider + docs.** Enum/map/tables; update CLAUDE.md and
  TODO.md.

## 6. Resolved questions (2026-07-25)

1. **`register_font` level** — backend-neutral vtable slot (more
   principled; contract stays self-contained). `lk_window_register_font`
   remains as SDL-side sugar delegating to the slot.
2. **`index_from_x` out-of-range** — clamp to 0 / len.
3. **`text_cache_cap`** — dropped from `lk_window_cfg`. Pre-1.0,
   private project; we break our own compatibility freely.

## 7. Exit criteria

- Two faces at two sizes render in one window, selected via theme
  rules only.
- With a proportional font: cursor lands exactly between glyphs,
  selection highlight covers exactly the selected glyphs,
  click-to-position hits the clicked boundary.
- Typing/deleting multibyte UTF-8 (e.g. "héllo→日本") never splits a
  codepoint; cursor motion is codepoint-wise.
- Per-string texture cache is gone; text renders through the glyph
  atlas.
- All headless tests green under the stub backend, including new
  exact-geometry tests.
