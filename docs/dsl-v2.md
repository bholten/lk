# DSL v2 — Idiomatic Lcl Surface (Design Draft)

The Layer-2 DSL (`lib/lk-dsl.lcl`) inherited Tcl's `-flag value`
option style. Lcl has since diverged from Tcl — lexical scoping,
first-class dicts, quasiquote macros, multiline literals — and the
flag style is now the least idiomatic corner of the system, with the
worst failure modes we've hit in practice. This doc proposes the
replacement.

Decision drivers (2026-07-26 session):

- The `_parse_flags`/`_apply_flags` machinery silently swallows
  unknown keys (bit us shipping `-tooltip`/`-hidden`: both vanished
  until the whitelist was patched). A redesign must make this
  *structurally impossible*, not just narrower.
- Leading-`-` sniffing misparses positional values; trailing flags
  become accidental booleans; `app`'s nested-if parser discards a
  typo'd flag's value as "the body".
- We are Lcl's only users; language additions are on the table, and
  compatibility breaks are free (pre-1.0, private).

## 0. Empirical ground truth (tested against upstream Lcl c1ef73d)

Built upstream and verified before designing — these are facts, not
README inferences:

1. **Bare-level multiline dict literals parse.**
   `let d #{a 1\n b 2}` works outside brackets. The historical
   "each widget call must fit on one line" constraint is dead.
2. **`widget "id" #{...multiline...} { body }` parses today** —
   the exact candidate-A shape below needs zero language changes.
3. **Multi-word `$handler $cmd` still dispatches** under the new
   parse-time dispatch rule; only **one-word** `[$closure]` changed
   (now returns the value, silently — audit required, see stage 0).
4. **`macro` is a real definition form** (quasiquote body, braced
   args arrive unevaluated, bracketed args evaluate first).

**Conclusion: no Lcl feature work is required for any candidate.**
(Brennan is open to adding features if needed; nothing below needs
one.)

## 1. Stage 0 — Lcl submodule bump (independent, do first)

Pinned `a5d2473` → upstream `c1ef73d` (23 commits). Worth taking
regardless of the DSL: fixes UB/UAF/OOM bugs, COW failure, namespace
and quasiquote fixes, adds `cond`/`case`, threading `->`/`->>`,
`isolate`/`require`, versioning, CI, and multiline-in-brackets.

Migration audit checklist:

- [ ] Grep `lib/`, `examples/`, `test/` for **one-word value-form
      dispatch**: `[$var]`, `[[expr]]` used as calls — now return the
      value instead of dispatching. Replace with `[apply $var]`.
      (The multi-word `$handler $cmd` in `_dispatch_command` still
      works; prefer `apply $handler $cmd` anyway for clarity.)
- [ ] `puts` was deprecated then re-added to core — no action, but
      verify test scripts.
- [ ] Run both suites + all four examples under the dummy driver.
- [ ] Commit the bump + audit fixes as one commit, before any
      syntax work.

## 2. Stage 1 — DSL test harness (before syntax changes)

The DSL has zero automated tests; migrating without a net is not on.
Plan: extend `test/lcl-lk-test.c` with a fixture that

1. creates a plain `lk_ui` (no window), registers lk + evals
   `lib/lk-dsl.lcl`,
2. sets `lk_dsl::_ui`/`lk_dsl::_tree` module state directly (they
   are `var`s — scriptable),
3. evals widget-proc snippets and asserts the resulting tree via
   existing `lk::` introspection (kinds, props, tags, presentations),
4. drives `lk_dsl::_frame` for full view bodies.

Write it against the **current** `-flag` syntax first; it becomes
the behavioral spec the migration must preserve (modulo syntax).
Target: ~15 tests covering every widget proc, flag→prop mapping,
tag/present handling, nesting, theme rules, translators, `on`
dispatch.

## 3. Stage 2 — the candidates

Running example, today's syntax (from budget-dsl.lcl):

```tcl
row "r_$rid" -gap 8 {
    text_input "name_$rid" -text $name -w 160 -focusable 1 -present (field_edit ($rid name))
    button "del_$rid" -text "x" -focusable 1 -present (action (remove_row $rid)) -tooltip "Remove row"
}
```

### Candidate A — props dict (recommended)

`kind id ?props-dict? ?body-block?` — both trailing args optional.

```tcl
row "r_$rid" #{gap 8} {
    text_input "name_$rid" #{text $name w 160 focusable 1
                             present (field_edit ($rid name))}
    button "del_$rid" #{text "x" focusable 1 tooltip "Remove row"
                        present (action (remove_row $rid))}
}
```

- **Unknown keys are hard errors**: `_apply_props` iterates the
  dict; any key not in the schema raises
  `button "del_3": unknown prop 'tooltp' (known: text, w, h, ...)`.
  No whitelist-swallowing by construction.
- **Props are values**: `let compact #{padding 2 gap 2}` …
  `column "c" $compact`; share/merge with
  `[Dict::merge $compact #{gap 4}]`. This is the killer feature —
  themes-in-miniature, computed props, loops building prop dicts.
- **Multiline for free** (fact 0.1/0.2) — no continuation tricks.
- Zero new parser: `_parse_flags` is deleted, `_apply_props` is a
  dict walk. `tag` and `present` stay ordinary keys.
- `app` follows suit: `app "Budget" #{width 900 font $mono} { ... }`
  — the nested-if flag parser is deleted too.

### Candidate B — colon keywords

```tcl
button "del_$rid" text: "x" focusable: 1 tooltip: "Remove row"
```

Design_draft §8's original sketch. Reads nicely, but: it needs a
new hand-rolled parser (trailing-`:` sniffing — same machinery class
as `-flags`, same failure modes to re-plug), props are not
capturable as a value, and multiline needs `\`. Every weakness of
the current system with prettier punctuation. **Rejected.**

### Candidate C — props as body commands (macro-based)

```tcl
button "del_$rid" {
    text "x"
    tooltip "Remove row"
    present action (remove_row $rid)
    ;; children would mix in here too
}
```

Tk/HTML-flavored; now feasible via `macro` + a prop-command scope.
Prettiest for static trees, but: conflates props and children in one
block, loses props-as-values, costs an extra eval per widget per
frame in a per-frame-rebuild architecture, and puts macros (the
newest, least-proven Lcl feature) on the hottest path. **Not now** —
note that C can be layered *on top of* A later as pure sugar
(a macro expanding body prop-commands into an A-style dict), so
choosing A forecloses nothing.

### Recommendation

**A.** It is the only candidate that is simultaneously: idiomatic
Lcl (dicts are the language's option-passing idiom — `theme_rule`
and `overlay_push` already work this way and were the smoothest
parts of recent sessions), zero-parser, error-strict by
construction, and value-composable. B is A with extra machinery and
fewer properties; C is future sugar.

## 4. Stage 3 — migration

One commit, clean break (no dual-syntax period — we are the only
users):

1. Rewrite `lib/lk-dsl.lcl`: `_make_node` takes `(id, props, body)`;
   `_parse_flags`/`_apply_flags`/`_prop_keys` whitelist die;
   `_apply_props` walks the dict with hard unknown-key errors.
   Schema lives in one dict (`_prop_schema`) mapping key →
   validation kind, shared by all widgets; widget-specific keys
   (e.g. `split_ratio`) validated per kind later if wanted.
2. `app`, `theme`/`rule` move to the same shape.
3. Migrate all four examples + the stage-1 harness tests.
4. Docs: CLAUDE.md DSL section, TODO.md, this doc gains DONE marks.

## 5. Resolved questions (2026-07-26)

Candidate A approved (Brennan: multiline dicts make C worth only
"not writing `#{`" — not worth the complexity or losing dumb
composable values). Proposals adopted as defaults:

1. **Empty props**: dict omitted is allowed; disambiguate by shape
   (`#{` = props, `{` = body). `button "x" { ... }` is legal.
2. **Theme rule**: `rule <kind> ?selector-dict? style-dict` — one
   dict = style; two dicts = selector (`#{tag primary state hover}`)
   then style. `-tag`/`-state` flags die with the rest.
3. **`present`**: keeps the compact `(ptype value-or-list)` 2-list.
4. **Migration is in-place** — `lib/lk-dsl.lcl`, clean break.

## 6. Exit criteria

- All four examples run under the dummy driver in the new syntax,
  with zero `-flag` parsing left in `lib/`.
- A typo'd prop key fails loudly with the widget id and known-key
  list (test-asserted).
- A props dict built in a variable and shared across two widgets
  works (test-asserted).
- DSL harness tests (stage 1 count) all green post-migration.
- Submodule pinned at c1ef73d+ with the dispatch audit done.
