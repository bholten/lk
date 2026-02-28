# User-Defined Layers (Weft-style) — Draft Design

## 1) What a "layer" is

A Layer is a plugin-like component that is given:

- a base view tree (retained semantic tree),
- a document surface model (for surface nodes),
- the interaction context (cursor, selection, focus, time),
- and optionally a cache slot (persistent per-layer state),
- ... and returns deltas in a small set of sanctioned channels.

Layers do not mutate the base tree in place.

### Layer outputs (four channels)

A layer may emit any of:

1. Annotations
  - attach metadata to ranges (document) or nodes (tree)
  - ex: syntax highlight spans, word-origin tags, lint warnings
2. Semantic enrichments
  - presentations, roles, accessibility names/values/actions
  - ex: "this span is an identifier bound to symbol X"
  - ex: "this node is 'search results', item has presentation type 'file'"
3. Overlays
  - additional nodes that render "on top" (popups, tooltips, gutters)
  - ex: autocomplete popup near cursor
  - ex: inline error squiggles with hover tooltip
4. Command translators
  - additional gesture → command mappings scoped to tags/presentations
  - ex: clicking a highlighted symbol triggers GoToDefinition(symbol_id)

That's it. If a layer can only do these, you keep coherence.

## 2) Layer pipeline order

Layers are composed into a pipeline:

1. Build Base Tree from app_state (pure)
2. Run Layer pipeline (user + built-in) — emits tags, overlays, translators, semantic enrichments
3. Run Style resolution (theme maps kind + tags + state -> lk_style per node)
4. Layout (uses resolved style for font/spacing)
5. Produce:
  - final tree (base + overlays)
  - final render list
  - final semantics/a11y map
  - final command translators

Style resolution runs after layers because layers emit **tags** and the theme
maps tags to style. See [styles.md](styles.md) for the full style system design.

Important: layers see the base tree + current derived maps, but overlays are appended in an "overlay root" to avoid structural chaos. Layers do not receive computed style — they operate on meaning, not appearance. If a layer needs visual context, it can read the previous frame's resolved styles.

## 3) The two kinds of layers: Tree layers and Surface layers

You'll want both, cleanly separated.

### A) TreeLayer (general UI)

Works on node tree:

- annotate nodes
- add overlays
- add translators
- enrich semantics

### B) SurfaceLayer (document/content)

Targeted at surface nodes (Weft):

- annotate ranges in a document (spans)
- create derived presentations for ranges
- add overlays anchored to document coordinates (cursor, spans, lines)

This is how syntax highlighting and word-origin coloring lives.

### 4) Data model: annotations as first-class values

#### 4.1 Node annotations

Attach tags/attributes to nodes:

- node_tags: Map<NodeId, [Tag]]
- node_attrs: Map<NodeId, Map<Key, Value]]

Example tags:

- class:add("keyword")
- hint("press Ctrl+P")
- debug:layout_bounds(...)

#### 4.2 Document annotations (range-based)

For each surface/document:

- annotations are a list of spans:
- Span = { start, end, tag, attrs }

Where start/end are positions in whatever coordinate system the surface uses (byte offset, rune index, line+col — your call, but be explicit).

Example:

- {start: 120, end: 128, tag: "origin.germanic"}
- {start: 532, end: 540, tag: "syntax.keyword"}
- {start: 900, end: 905, tag: "lint.error", attrs: {message: "..."} }

#### 4.3 Anchors for overlays

Overlays need anchoring:

- anchor to node bounds: Anchor.Node(node_id, corner)
- anchor to surface position: Anchor.Doc(surface_id, pos, gravity)

Autocomplete popup is:

Anchor.Doc(editor_surface, cursor_pos, gravity="below-right")

### 5) Layer API surface (conceptual)

A layer has:

- id
- deps (optional, for ordering)
- capabilities (what it consumes/produces)
- run(context) -> LayerDelta

Context includes

- tree
- semantics map so far
- focus/cursor/selection
- time
- resource access
- layer_state (persistent blob per layer instance)
- surface/document accessors (read-only)
- prev_frame_styles (previous frame's resolved styles, for rare cases needing visual context)

LayerDelta includes

- node_tags/attrs updates
- doc_spans updates (per surface)
- overlays (node subtrees with anchors)
- translator additions
- semantic enrichments
- state updates (new layer_state)

Make it purely functional in shape even if implemented imperatively.

### 6) Caching and incremental updates (critical for performance)

Layers like tree-sitter are expensive. You need a story:

Layers declare what they depend on:

- "document text hash"
- "cursor position"
- "viewport range"
- "selection"

UI core gives them:

- doc.revision_id
- cheap slice access to visible ranges
- optional incremental change info (insert/delete ranges)

So the tree-sitter layer can:

- reuse parse tree
- re-highlight only impacted ranges
- emit spans only for visible region

This is the difference between "cute demo" and "Weft rewrite works."

## 7) Safety / boundaries (so layers don't ruin the world)

Rules:

- Layers cannot mutate base tree props directly (only tags/attrs and overlays).
- Layers cannot emit "real app state changes" directly.
- Layers may emit commands; only the app decides what they do.

This preserves determinism and testability.

## 8) How this enables your examples

Syntax highlight = SurfaceLayer

- consumes: document text, optional parse cache
- produces: doc_spans with tags like syntax.keyword, syntax.string
- theme maps tags → colors/fonts

Word-origin coloring = SurfaceLayer

- consumes: document text, dictionary resource
- produces: doc_spans with tags origin.germanic|latin|greek
- theme maps origin tags to palette

Autocomplete = SurfaceLayer + Overlay

- consumes: cursor context + doc text + language service results
- produces:
- overlay popup node tree anchored at cursor
- translators: Up/Down selects item, Enter invokes InsertCompletion(text)
- doc span tags optionally (ghost text)

This is all consistent.

## One design tweak I'd add

To keep styling separate but still layerable:

layers mostly emit tags, not colors.

theme (Lcl) maps tags → style.

So the layer system stays semantic; theme decides appearance.

# Layer System v0 — Specification
## 0. Purpose

The Layer System provides a disciplined mechanism for augmenting a retained semantic UI tree with:
- semantic annotations,
- accessibility enrichments,
- visual overlays,
- interaction behavior,
- without mutating the base view tree or application state directly.

Layers are the primary extension mechanism for:
- syntax highlighting,
- semantic inspection,
- provenance/debug views,
- autocomplete/tooltips,
- accessibility narration,
- Weft-style document intelligence.

## 1. Core Principles

Non-invasive
Layers do not mutate the base tree or document content.

Semantic-first
Layers emit tags, attributes, and presentations — not pixels.

Composable
Multiple layers may coexist; outputs are merged deterministically.

Incremental
Layers can cache state and respond to document deltas.

Inspectable
Layer outputs are queryable and debuggable.

Host-controlled effects
Layers may propose commands; only the application executes them.

2. Layer Execution Model
2.1 Pipeline Overview

Each UI frame executes the following conceptual pipeline:

```
App State
   ↓
View Function → Base Tree
   ↓
Layer Pipeline (tags, overlays, translators, semantic enrichments)
   ↓
Style Resolution (kind + tags + state → lk_style per node)
   ↓
Layout
   ↓
Render List + Command Translators
```

Layers execute after base view construction and before style resolution.
Style resolution runs after layers because layers emit tags that the theme
maps to appearance. See [styles.md](styles.md).

3. Layer Types

Two layer types are defined:

## 3.1 TreeLayer

Operates on the UI node tree.

Capabilities:
- annotate nodes,
- enrich semantics (roles, names, actions),
- add overlays anchored to nodes,
- add command translators.

## 3.2 SurfaceLayer

Operates on document surfaces (surface nodes).

Capabilities:
- annotate document ranges (spans),
- emit presentations tied to content,
- add overlays anchored to document positions,
- add translators scoped to content semantics.

# 4. Layer Interface (Conceptual)

A layer is defined by the following interface:

```
4.1 Metadata
Layer {
  id: LayerId
  kind: TreeLayer | SurfaceLayer
  deps: [LayerId]          // optional ordering constraints
  capabilities: {
    reads:  [Capability]
    writes: [Capability]
  }
}
```

Capabilities are declarative (e.g. document-text, cursor, node-tree).

## 4.2 Execution Function

```
run(context: LayerContext) -> LayerDelta
```

LayerContext includes:
- tree (read-only semantic tree)
- surfaces (read-only document access)
- semantics (current semantic map)

interaction:
- focused node
- cursor position
- selection
- time
- resources
- layer_state (opaque, persistent)
- diff_hints (optional incremental change info)

## 4.3 LayerDelta (Outputs)

A layer may emit any subset of the following:

```
LayerDelta {
  node_tags: Map<NodeId, [Tag]]
  node_attrs: Map<NodeId, Map<Key, Value>>

  doc_spans: Map<SurfaceId, [Span]>

  semantic_enrichments: Map<NodeId | SpanId, SemanticRecord>

  overlays: [OverlayNode]

  translators: [TranslatorRule]

  state_update: LayerState
}

```

All outputs are additive.

## 5. Annotations and Tags

### 5.1 Tags

Tags are interned symbolic labels.

Examples:
- syntax.keyword
- origin.germanic
- lint.error
- debug.bounds

Tags have no intrinsic meaning — interpretation is deferred to:
- themes,
- accessibility layers,
- inspection tools.

### 5.2 Node Attributes

Attributes attach small typed metadata:

```
AttrValue ::= string | int | bool | symbol | bytes
```

Examples:
```
{message: "unused variable"}

{confidence: 0.82}
```

## 6. Document Spans (Surface Annotations)
### 6.1 Span Definition

```
Span {
  start: Position
  end: Position
  tag: Tag
  attrs: Map<Key, Value>?
}
```

### 6.2 Coordinate System (v0)

Positions are byte offsets into UTF-8 text.

Surfaces provide:
- byte → line/column mapping,
- grapheme helpers (best effort).
- (Byte offsets are chosen for determinism and incremental parsing.)

# 7. Overlays
## 7.1 Overlay Nodes

Overlays are normal UI subtrees with anchors.

```
OverlayNode {
  anchor: Anchor
  subtree: Node
  layer: LayerId
}
```

## 7.2 Anchors

```
Anchor ::=
  NodeAnchor(node_id, gravity)
| SurfaceAnchor(surface_id, position, gravity)

```

Used for:
- autocomplete popups,
- tooltips,
- inline diagnostics,
- gutters.

8. Semantic Enrichment

Layers may enrich semantics for:
- accessibility,
- command routing,
- inspection.

```
SemanticRecord {
  role: Role?
  name: string?
  value: string | number | enum?
  actions: [Action]
}
```

Actions map directly to commands.

9. Command Translators

Layers may propose gesture → command mappings.

```
TranslatorRule {
  trigger: EventPattern
  selector: NodeSelector | SpanSelector
  presentation_type?: Symbol
  command: CommandTemplate
}
```

Example:

Activate any span tagged origin.* → ExplainWordOrigin(word_id)

10. Caching and Incrementality

Layers may persist opaque layer_state.

The UI core supplies:
- document revision IDs,
- incremental edit ranges,
- viewport ranges (optional).

Layers are responsible for:
- invalidating their own caches,
- emitting minimal deltas.

11. Safety and Boundaries

Layers may not:
- mutate base tree props,
- mutate document text,
- execute commands directly,
- perform I/O without host permission.

Layers may:
- emit commands,
- attach semantic meaning,
- request overlays.

12. Debugging and Inspection

All layer outputs are introspectable:

- ui.layers.list
- ui.layers.dump <layer-id>
- ui.query tag:syntax.keyword
- ui.query span:origin.germanic

## Worked Example: Document Surface with Three Layers
Base View (simplified)

```
(ui window id:"editor" {
  (ui surface id:"doc"
    document:$document)
})
```

### Layer 1: Syntax Highlighting

Purpose: Tag tokens with syntax categories.

Type: SurfaceLayer
Reads: document-text
Writes: doc_spans
```
Span {
  start: 120
  end: 128
  tag: syntax.keyword
}
```

Theme decides:

syntax.keyword → blue

syntax.string → green

No rendering logic in the layer.

### Layer 2: Word Origin Coloring

Purpose: Semantic annotation for prose analysis.

Type: SurfaceLayer
Reads: document-text
Writes: doc_spans

```
Span {
  start: 532
  end: 540
  tag: origin.germanic
  attrs: {word: "blood"}
}
```

Theme decides:

origin.germanic → red

origin.latin → purple

Accessibility layer may add:

description: "Germanic-origin word"

### Layer 3: Autocomplete

Purpose: Contextual suggestion UI.

Type: SurfaceLayer
Reads: document-text, cursor
Writes: overlays, translators

```
Overlay Emission
OverlayNode {
  anchor: SurfaceAnchor("doc", cursor_pos, "below-left")
  subtree: (ui column class:"autocomplete" {
    (ui label text:"bloodshed")
    (ui label text:"bloodline")
  })
}
```

Translator
On Activate(label)
→ Command InsertText("bloodshed")

Accessibility Layer (Built-in)

Consumes:
- tags
- presentations
- roles

Produces:
- spoken descriptions
- keyboard navigation rules
- screen-reader actions

No coupling to syntax/origin layers required.

Inspect Overlay (Optional)

Debug layer that:
- draws span bounds,
- shows active tags on hover,
- prints layer provenance.

## Why This Works (and Why It's Not Crazy)

- Syntax highlight = semantics, not paint
- Autocomplete = overlay + commands, not widget mutation
- Prose analysis = just another semantic pass
- Accessibility = derived, not bolted on
- Weft rewrite fits naturally

Most importantly:
- everything is explainable.
- You can always answer "why is this on screen?"

## One Explicit Design Constraint (Worth Writing in Stone)

Layers should mostly emit tags, not styles.

This keeps:

- layers semantic,
- themes aesthetic,
- accessibility interpretable,
- testing sane.
