# Style System — Design

## 0. Core Principle

The view tree is a reification of **meaning**, not appearance. The view
function's job is `Data -> Tree`: it describes what things are, their
structure, and their semantic relationships. It does not describe how things
look, how they are spaced, or what color they are.

Style is a **projection** of meaning into appearance. It is computed after the
tree is built, by a resolver that maps semantic information (kind, tags, state)
to a visual+spatial property set. This is the CLIM model: the application
describes, the system presents.

## 1. Pipeline Order

The design_draft.md and layers.md documents previously placed style resolution
before the layer pipeline. This is revised: layers emit **tags** (semantic
annotations), and the theme maps tags to style. Therefore style resolution must
run after layers.

```
App Data
   |
   v
View Function -> Base Tree (structure + meaning)
   |
   v
Layer Pipeline (emits tags, overlays, translators, semantic enrichments)
   |
   v
Style Resolution (kind + tags + state -> lk_style per node)
   |
   v
Layout (reads style for font/spacing, tree props as overrides)
   |
   v
Render (reads style for colors/borders, layout for geometry)
```

Style resolution is a **terminal projection phase**: it consumes the same
semantic vocabulary that layers produce (tags, state) and projects it into
concrete visual+spatial parameters. It shares the data model with layers — both
operate on interned tags and semantic annotations — but it is not a pluggable
layer. It runs in a fixed pipeline position, produces a typed struct with hard
performance constraints, and is consumed directly by layout and render. The
unity with layers is in the **data model**, not the execution model.

## 2. What Style Owns

Style owns **all** visual and spatial properties. The tree is semantic; style
is presentational.

### 2.1 The `lk_style` Struct

A compact, fixed-layout struct computed per node:

```c
typedef struct lk_style {
    lk_color fg;           /* text / foreground color */
    lk_color bg;           /* background fill color */
    lk_u32   font_id;      /* interned font name (0 = default) */
    lk_i32   font_size;    /* font size in pixels (0 = default) */
    lk_i32   padding;      /* content inset */
    lk_i32   gap;          /* spacing between children */
    lk_i32   border_width; /* border thickness */
    lk_color border_color; /* border color */
    lk_i32   border_radius;/* corner radius */
    lk_u8    align;        /* cross-axis alignment (lk_align) */
    lk_u8    justify;      /* main-axis alignment (lk_align) */
} lk_style;
```

This is not CSS. It is a small, fixed vocabulary — no arbitrary properties, no
string-keyed bags. Every field has a concrete type and a default.

Styles are stored per-node in a parallel array (`lk_style[]` indexed by
`lk_ix`, like `lk_rect[]`). Future versions may deduplicate identical styles
for render batching and layout caching, but v0 stores one struct per node.

### 2.2 What Stays on the Tree

Tree props are for **semantic** information:

- `text` — the content of a label or button (meaning)
- `focusable` — whether this node participates in focus (behavior)
- `disabled` — whether this node is inert (state)
- `presentation type/value` — CLIM-style semantic attachment (meaning)

Layout-affecting properties (`padding`, `gap`, `w`, `h`, `align`, `justify`)
move conceptually from "tree props the view function sets" to "style properties
the theme provides." The tree prop keys remain in the API as an **override
mechanism** (see section 4), but the default authoring pattern is: don't set
them in the view function. Let the theme decide.

## 3. Theme and Rules

A theme is an ordered list of style rules. Each rule has a selector and a
(partial) style. The resolver evaluates rules against each node and merges
matches to produce a complete `lk_style`.

### 3.1 Selectors (v0)

Selectors match on three axes:

| Axis   | Matches against        | Example         |
|--------|------------------------|-----------------|
| kind   | node kind (or `*`)     | `button`, `*`   |
| tag    | tag presence on node   | `primary`, `toolbar` |
| state  | interaction state mask | `focused`, `hovered`, `disabled` |

A rule matches a node if **all** specified axes match. Unspecified axes are
wildcards.

No ancestry/structural selectors in v0. If a button inside a toolbar needs
different styling, the view function (or a layer) tags it `toolbar-button`.
The selector matches the tag directly. Structural selectors can be added later
if a real need arises.

### 3.2 Rule Priority

Rules are evaluated in declaration order. Later rules override earlier ones for
the same fields. More specific selectors (more axes specified) do not
automatically win — order is the tiebreaker. This is simple, predictable, and
debuggable.

A built-in default theme provides base rules for every kind. User themes are
appended after the defaults, so they naturally override.

Because rule order is semantically significant, **style tracing** is a required
debugging affordance (see section 10.1).

### 3.3 Partial Styles and Field Mask

Each rule provides a **partial** style — only the fields it sets, indicated by
a `lk_u32 field_mask` bitmask. One bit per field in `lk_style`. A rule only
overwrites fields whose bit is set in its mask.

### 3.4 Example Rules

```
/* Default theme (built-in) */
rule  kind:window              -> bg:(30,30,30) fg:(220,220,220) padding:0
rule  kind:column              -> padding:0 gap:0
rule  kind:row                 -> padding:0 gap:0
rule  kind:label               -> fg:(220,220,220)
rule  kind:button              -> bg:(60,60,60) fg:(220,220,220) padding:8
rule  kind:button state:focused -> bg:(80,80,100)
rule  kind:button state:hovered -> bg:(75,75,75)

/* User theme (appended, overrides) */
rule  kind:window              -> bg:(24,24,24)
rule  tag:primary              -> bg:(40,80,160) fg:(255,255,255)
rule  tag:danger               -> fg:(220,60,60)
```

### 3.5 Tags

Tags are interned symbols attached to nodes. They carry no intrinsic visual
meaning — the theme interprets them. Tags are the primary bridge between
layers (which emit them) and style (which consumes them).

A node may have multiple tags. Tags are a **set**, not key-value pairs.

Examples: `primary`, `danger`, `toolbar`, `syntax.keyword`, `origin.germanic`.

In the layer pipeline, layers emit tags based on semantic analysis. The theme
maps those tags to appearance. The layer never knows what color
`syntax.keyword` is.

Without layers (v0), tags are set explicitly in the view function or via
a `class`/`tag` tree prop.

## 4. Tree Prop Overrides

The view function is `Data -> Tree` and should not normally set visual
properties. However, there are cases where the application has structural
knowledge the theme cannot:

- A specific layout that is intrinsic to the data being presented
- A fixed-size region dictated by content constraints
- An explicit override for a one-off case

For these, tree props (`UIP_PADDING`, `UIP_GAP`, `UIP_W`, `UIP_H`,
`UIP_ALIGN`, `UIP_JUSTIFY`) act as **overrides**. If a tree prop is set on a
node, it takes precedence over the style-resolved value. If not set, the
style value applies.

Tree prop overrides take precedence unconditionally — they win over both rule
matching and inheritance (see section 5.2 for the full resolution order).

Most view functions should not set these props.

## 5. Inheritance

There is no cascading. Each node's style is resolved independently by
matching rules.

### 5.1 Per-Field Inheritance

Inheritance is a **per-field property**, not an ad-hoc exception. Each field
in `lk_style` is explicitly declared as either **inherited** or
**non-inherited**. This is tracked as a compile-time bitmask constant
(`LK_STYLE_INHERIT_MASK`).

| Field          | Inherited | Rationale |
|----------------|-----------|-----------|
| `fg`           | yes       | Text color propagates — set it once at the top, children use it. |
| `font_id`      | yes       | Font propagates — same reasoning as fg. |
| `font_size`    | yes       | Font size propagates with font. |
| `bg`           | no        | A container's background says nothing about its children. |
| `padding`      | no        | Spacing is per-node, not propagated. |
| `gap`          | no        | Spacing is per-node, not propagated. |
| `border_width` | no        | Borders are per-node. |
| `border_color` | no        | Borders are per-node. |
| `border_radius`| no        | Borders are per-node. |
| `align`        | no        | Alignment is per-container. |
| `justify`      | no        | Alignment is per-container. |

Adding a new field to `lk_style` **requires** an explicit inheritance decision
and an update to `LK_STYLE_INHERIT_MASK`. This prevents accidental expansion
of inheritance into CSS-like cascading.

For inherited fields: if no rule set the field and no tree prop overrides it,
the node inherits from its parent's **resolved** style. If no ancestor sets it
either, the kind default applies.

### 5.2 Resolution Order

For each node, style is resolved in this order:

1. **Zero-initialize** — all fields start unset (tracked by a "set" bitmask).
2. **Apply matching rules in declaration order** — each matching rule
   overwrites only its specified fields (per its `field_mask`).
3. **Apply kind defaults** — for any field still unset, apply the kind's
   default value (e.g., button gets `bg:(60,60,60)` if no rule set bg).
4. **Apply tree prop overrides** — if a tree prop is present on the node
   (e.g., `UIP_PADDING`), it unconditionally overwrites the corresponding
   style field. Tree props always win.
5. **Apply inheritance** — for each **inherited** field that is still unset
   after steps 1-4, copy the value from the parent's fully-resolved style.

This order is strict. Tree prop overrides block inheritance — if a child has
an explicit `UIP_PADDING` tree prop, it does not inherit padding (not that
padding inherits, but the principle applies to all fields: tree prop > rules >
inheritance > kind defaults).

Inheritance is resolved in a single **top-down** pass (root first, children
after), so parent styles are always available when resolving children.

## 6. Interaction State

The style resolver needs to know about interaction state: focused, hovered,
pressed/active, disabled. These are not tree props — they are transient runtime
state tracked by `lk_ui`:

- **focused**: the node matching `lk_focus_current()`
- **hovered**: the node under the pointer (from hit-testing)
- **disabled**: the `UIP_DISABLED` tree prop (this one IS semantic)

State is passed to the resolver per-node as a bitmask. Rules can match against
it. This enables `kind:button state:hovered -> bg:(75,75,75)` without the
button knowing anything about hover colors.

## 7. Relationship to Layers

Style resolution is downstream of layers in the pipeline. Layers emit tags
and annotations; the theme consumes them.

A syntax highlighting layer tags spans `syntax.keyword`. The theme says
`tag:syntax.keyword -> fg:(100,150,255)`. The layer is semantic; the theme
is aesthetic.

This means layers do not need to know the current theme. The `LayerContext`
does **not** include computed style (revising layers.md). Layers operate on
meaning; style operates on the output of meaning.

If a layer needs visual context (rare), it can read the previous frame's
resolved styles from `lk_ui`. This is cheap and avoids circular dependency.

## 8. Relationship to Render

The render system (`lk_render_build`) currently reads colors from hardcoded
functions in widget vtables. With style resolution:

- Widget render functions receive the resolved `lk_style` for the node.
- They use `style->bg` and `style->fg` instead of calling `color_window_bg()`.
- The hardcoded color functions are removed.

The widget vtable render signature becomes:

```c
void (*render)(const lk_tree *t, lk_ix n, lk_rect content_rect,
               const lk_style *style, lk_render_list *rl);
```

Similarly, layout receives style for font metrics:

```c
/* Layout reads font from style, spacing from style (or tree override) */
```

## 9. Default Theme

The system ships with a built-in default theme that:

- Provides sensible rules for every built-in kind
- Produces a dark-mode palette (the current hardcoded colors, refined)
- Sets default font to the system/fallback font
- Is always present — user themes layer on top, they don't replace it
  (unless explicitly cleared)

The default theme is the "zero-config" experience. An application that
builds a tree and calls `lk_window_run` gets a reasonable-looking UI
without mentioning style.

## 10. C API Surface (Sketch)

```c
/* Style struct */
typedef struct lk_style { ... } lk_style;

/* Per-field inheritance mask (compile-time constant) */
#define LK_STYLE_INHERIT_MASK ( ... )

/* Field mask bits (one per lk_style field) */
#define LK_SF_FG            (1u << 0)
#define LK_SF_BG            (1u << 1)
#define LK_SF_FONT_ID       (1u << 2)
/* ... etc ... */

/* Theme (opaque) */
typedef struct lk_theme lk_theme;

lk_theme *lk_theme_new(void);
void      lk_theme_destroy(lk_theme *th);

/* Add a rule. selector fields: kind (0=any), tag_id (0=any),
   state_mask (0=any). partial_style: only set fields are applied,
   indicated by field_mask. */
void lk_theme_add_rule(lk_theme *th,
                        lk_u16 kind,
                        lk_u32 tag_id,
                        lk_u8 state_mask,
                        const lk_style *partial_style,
                        lk_u32 field_mask);

/* Resolve styles for an entire tree. Writes into styles[] (parallel
   array indexed by lk_ix, like rects[]). */
void lk_style_resolve(const lk_theme *th,
                       const lk_tree *t,
                       const lk_u8 *node_states,
                       lk_style *styles);

/* lk_ui owns the theme and resolved styles */
void      lk_ui_set_theme(lk_ui *ui, lk_theme *th);
lk_theme *lk_ui_theme(lk_ui *ui);
```

### 10.1 Style Tracing

Because rule priority is order-based, debugging requires the ability to
inspect which rules matched a given node and in what order. This is a
**required** API, not a nice-to-have:

```c
typedef struct lk_style_trace_entry {
    lk_u32 rule_index;   /* index in theme's rule list */
    lk_u32 field_mask;   /* which fields this rule set */
} lk_style_trace_entry;

typedef struct lk_style_trace {
    lk_style_trace_entry *entries;
    lk_u32 count;
    lk_u32 cap;
} lk_style_trace;

/* Trace which rules matched a specific node, in application order. */
void lk_style_trace_node(const lk_theme *th,
                          const lk_tree *t,
                          lk_ix node,
                          lk_u8 node_state,
                          lk_style_trace *out);
```

This answers "why does this node look this way?" deterministically.

## 11. Lcl Bindings (Sketch)

```tcl
/* Set theme rules from script */
lk::theme_rule $ui "button" "" "" {bg {60 60 60} fg {220 220 220} padding 8}
lk::theme_rule $ui "button" "" "focused" {bg {80 80 100}}
lk::theme_rule $ui "*" "primary" "" {bg {40 80 160} fg {255 255 255}}

/* Tag a node */
lk::tag $tree $node "primary"
```

## 12. Performance

The resolver is **O(N * R)** where N is node count and R is rule count. For
each node, every rule is tested for match, and matching rules are applied in
order.

For v0 this is acceptable — typical UIs have tens to low hundreds of nodes and
tens of rules.

If R grows large (e.g., many syntax-highlight tag rules), rules can be
pre-bucketed by kind and/or tag for O(N * R_matching) amortized. This
optimization is deferred until measured need arises.

## 13. What This Enables

- **hello.lcl becomes pure meaning**: `node + append_child + text`. No
  padding, no gap, no colors. The theme handles it.
- **Themeable applications**: swap the theme, the whole app re-skins.
- **Layer integration**: syntax highlighting, semantic coloring, and
  diagnostic display all work through tags -> theme rules.
- **Debuggable**: for any node, `lk_style_trace_node` returns exactly which
  rules matched and in what order.
- **Future DSL**: a Layer 2 Lcl DSL where `button "click me"` is all
  you write, and the theme provides every visual detail.
