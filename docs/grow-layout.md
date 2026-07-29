# Grow Layout: Weighted Growth in Stacks (Design, v2)

**F1: DONE** (2026-07-29). Landing notes:

- `UIP_GROW` appended to `lk_prop_key` (after `UIP_EDITOR` — no
  existing value shifted); the spacer distributor in `layout_stack`
  (`src/core/lk-widget.c`) generalized to `grow_weight` +
  largest-remainder apportionment (sort-free O(n²) rank pass; the
  core has no assert facility, so negative weights clamp silently
  as documented in §1). All 420 pre-existing core tests passed
  untouched — the §3 compatibility theorem held in practice.
- **Precedence investigation verdict (§4): the suspected clobber
  was NOT real.** `lk_style_resolve` already gates every tree-prop
  override (align, justify, padding, gap) on `lk_node_has_prop`,
  so theme-sourced align survives an absent prop and an explicit
  `align start` (zero) beats the theme — both directions now
  pinned by tests. The stale "stretch is default" enum comment
  traced instead to the *styleless* fallback in `layout_stack`
  (`cfg->styles == NULL` reads `UIP_ALIGN` with default STRETCH);
  that fallback is kept for backwards compat and the comment now
  says so. WINDOW `align: STRETCH` landed in `lk_theme_default`.
- Bindings: `grow` in the `lk::prop` coercion (i32, negatives
  hard-error naming the constraint); DSL `_prop_schema` gains
  `grow`. Example sweep: weft-mini lost `win_h`, the
  `window_resize` tracking, `_ed_h` and its 76/38 chrome
  constants (editors: `grow 1`; pane columns already stretched).
  editor-dsl had no equivalents to shed (its editor is a direct
  split-pane child); hello-dsl's `scroll h 200` is deliberate
  (forces overflow so the wheel demo scrolls) and stayed.
- Tests: +19 core (§6 list, normative examples exact), +2 Lcl
  (binding validation + DSL schema/negative-grow error path).

Polish-round headliner. Revision history: v1 draft ("flex-layout")
2026-07-29; v2 same day after review — renamed per the review's
§10: stack layouts gain **weighted growth**, analogous to
`flex-grow`, but lk does not implement the CSS Flexbox model (no
shrink, no wrapping, no align-self, no min/max clamping during
distribution, no basis modes, no ordering, no percentages). The
name `grow` states the narrower promise.

Motivation unchanged: today nothing in a stack can claim leftover
main-axis space except `UIK_SPACER`, so any child that should fill
its container — editor, scroll region, canvas, log pane — needs an
application-computed size (weft-mini tracks window height via
`window_resize` and hardcodes chrome constants). That is the
layout engine's job leaking into apps, and it is universal, not
text-surface-specific.

Key insight (verified in `layout_stack`): **lk already contains a
flex distributor** — the spacer share-out — hardcoded to one
widget kind at weight 1. This design generalizes it; it does not
add a second mechanism.

## 1. `UIP_GROW`

New i32 prop `grow`. Effective weight per visible child:

```
if child has UIP_GROW explicitly:  weight = clamp(value, 0, 4096)
else if child is an unsized SPACER: weight = 1   /* legacy */
else:                               weight = 0
```

- **Absent ≠ zero** (pinned): presence is checked with
  `lk_node_has_prop`, so `spacer "gap" #{grow 0}` genuinely
  disables growth while a bare `spacer "gap"` keeps its legacy
  flexibility. The full spacer matrix:

  | spacer main-size prop | explicit grow | result |
  |---|---|---|
  | absent | absent | legacy weight 1 |
  | absent | 0 | zero-sized, fixed |
  | absent | 2 | weight 2 |
  | present | absent | fixed at explicit basis |
  | present | 0 | fixed at explicit basis |
  | present | 2 | explicit basis + weight-2 growth |

- **Validation**: DSL and Lcl bindings hard-error on `grow < 0`;
  the C core defensively clamps negatives to 0 (debug-assert under
  the opt-in flag). Integer weights only.
- **Overflow safety without wide integers** (scoping position,
  replacing the review's 64-bit-intermediate suggestion): weights
  clamp to `[0, 4096]` at distribution time — no meaningful layout
  needs larger ratios, and the cap keeps `leftover × weight`
  inside `lk_i32` for any real extent. lk deliberately has no
  64-bit integer type (the `lk_revision` decision) and a multiply
  is not the reason to add one.
- **Prop, not style**: grow describes a child's spatial
  relationship to its siblings — structural intent like W/H, not
  appearance.

## 2. Semantics

- **Growth is layout-only** (explicit invariant, per review §2):
  weights participate only after the parent has its final
  main-axis extent. Measurement is untouched — a parent's
  intrinsic size never includes growth, so there is no circular
  dependency, and a parent that only ever receives its intrinsic
  size has zero leftover and growth is a no-op. Nested growth is
  two independent phases: the outer stack grants the inner
  container its grown extent; the inner container then distributes
  its own leftover.
- **Basis + grow**: an explicit main-axis size prop is the
  *basis*, and the child still grows — the app explicitly supplied
  both intentions (`w 300 grow 1` = "start at 300, take more when
  available"). Documented nuance: with `grow > 0`, W/H on the main
  axis is no longer an exact final size. Omitting `grow` remains
  the way to stay fixed.
- **Intrinsic is the floor; no shrink**: negative leftover behaves
  exactly as today (children keep bases, overflow clips). Shrink
  is a separate future design needing per-child lower bounds — the
  distributor's shape admits it; nothing here pretends to design
  it.
- **Justify**: operates iff `leftover > 0 && total_weight == 0`
  (the review's directly-testable wording; generalizes the
  existing `spacer_count == 0` gate).
- **Layout invariants, pinned** (review §12): gaps count only
  visible children (`max(visible − 1, 0)`); available space =
  final inner extent − padding − visible gaps, subtracted exactly
  once; the distributor has no signed-underflow path (growth never
  produces negative sizes); zero leftover is a no-op; growth
  changes sizes and downstream positions only, never ordering.

## 3. Apportionment (pinned: largest-remainder)

The v1 draft's cumulative-floor rule contradicted its own
compatibility claim (it yields `33/33/34` for 100 at 1:1:1;
legacy spacers give remainder-to-first, `34/33/33`). Resolution,
per review §4 — **largest-remainder with earlier-child
tie-break**:

```
base_i      = (leftover * weight_i) / total_weight     /* floor */
remainder_i = (leftover * weight_i) % total_weight
extra pixels (leftover − Σ base_i, always < count of weighted
children) go to the largest remainder_i, ties to earlier children
```

Properties: shares sum exactly to `leftover`; closest integer
approximation to the requested ratios; **equal weights make all
remainders equal, so the tie-break hands extras to the first
children — legacy spacer behavior reproduced as a theorem, not a
special case** (existing spacer tests must pass untouched);
deterministic; no sort needed (residual count < child count —
a single pass suffices).

Examples: 100 @ 1:1:1 → 34/33/33; 100 @ 1:2 → 33/67;
101 @ 1:2 → 34/67.

## 4. Cross axis: WINDOW stretches; ROW/COLUMN keep START

Revised from v1's global flip, per review §8 — without an
`align_self` facility, container-wide STRETCH just chooses which
children need wrappers (the editor fills, but every label and
button goes full-width too). Adopted:

- **WINDOW defaults to `align: STRETCH`** (via `lk_theme_default`;
  never enum reordering — script-visible numbers must not shift).
  A root's child should fill the client area; an intrinsic-width
  root is rarely useful.
- **ROW/COLUMN keep START.** Content containers opt in with one
  declaration — `column "main" #{align stretch}` — which is a
  world away from application-side geometry arithmetic.
- **`align_self` is the anticipated future facility**; if mixed
  children keep producing wrapper friction, it lands as its own
  small design, and revisiting the stack default becomes safe
  then. Not in F1.

**Theme-vs-prop precedence fixed regardless** (review §9 — a
correctness fix independent of any default): resolution must be
explicit prop → theme rule → kind default → engine fallback. The
current resolve path reads the align prop with default 0, which
may clobber theme-sourced align with an implicit START (zero *is*
a valid explicit enum value) — and likely explains the stale
"stretch is default" enum comment. Verify, fix, test both
directions (theme STRETCH survives an absent prop; explicit
`align start` beats a theme STRETCH), and fix the stale comment.

## 5. Proof-of-design: the example sweep

Acceptance is deletion. weft-mini loses `win_h`, the
`window_resize` tracking, the 76/38 px chrome constants, and the
explicit editor heights — replaced by `grow 1` on the editors plus
`align stretch` on the pane columns. editor-dsl.lcl sheds its
equivalents. If the diff doesn't delete that machinery, the
design missed.

## 6. Staging

One stage (F1): distributor generalization (largest-remainder,
weight cap, presence rule) + `UIP_GROW` + DSL/bindings (negative
hard-errors) + WINDOW stretch default + align precedence fix +
stale-comment fix + tests + example sweep + docs.

Tests (stub-exact; review §14 adopted wholesale): weight math
(1:1:1 → 34/33/33 asserted exactly, 1:2 splits, 101 @ 1:2);
equal legacy spacers with remainder proving the exact
compatibility order; mixed legacy spacer + grow child; `grow 0`
on an otherwise-flexible spacer; sized spacer with/without grow;
negative grow rejected by bindings, clamped in core; huge weights
(cap engages, no overflow); grow on hidden children affects
neither weights nor gaps; all-hidden; single visible grow child;
zero inner extent; padding + gaps + growth accounting exact;
repeated layout byte-identical; parent intrinsic measurement
unchanged by child grow; basis preserved under negative leftover;
justify inert iff total_weight > 0, active when 0 with leftover;
nested grown containers (outer grants, inner distributes — both
phases asserted); theme-align precedence both directions,
independent of the WINDOW default; WINDOW stretch default applies
and is overridable.

## 7. Resolved questions (v1 → review)

1. **Cross-axis default**: WINDOW → STRETCH; ROW/COLUMN keep
   START; `align_self` deferred as the enabling facility for any
   future revisit. Precedence fix lands now regardless.
2. **Basis + grow**: confirmed — explicit size is the basis, the
   child still grows; omit `grow` to stay fixed.
3. **Naming**: `grow` (and the doc says "weighted growth", never
   bare "flex" — §10's narrower-promise rule).
4. Recorded scoping position: weight cap `[0, 4096]` instead of
   64-bit intermediates (C89 honesty; the `lk_revision`
   precedent).
