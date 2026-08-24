# Machine-checked matchertext

Lean 4 development of the properties both papers rest on. What is proved here is
what the implementations are allowed to assume.

## Build

```sh
lake build
```

Requires the toolchain pinned in `lean-toolchain` (`leanprover/lean4:v4.33.0-rc1`)
and Mathlib, which `lake` fetches from `lakefile.toml`.

## What each file establishes

| File                     | Contents                                                                                                                  |
|--------------------------|---------------------------------------------------------------------------------------------------------------------------|
| `core.lean`              | The alphabet, the matcher pair set `Pi`, and the inductive language `MT`                                                  |
| `always_embeddable.lean` | Closure: a matchertext string wrapped in a matched delimiter pair is still matchertext, at any depth and with no escaping |
| `embed_boundary.lean`    | The boundary result: reading to matcher balance ends an embedded value at exactly its last byte                           |
| `to_matchertext.lean`    | The total encoder: any string at all, once encoded, is matchertext                                                        |

The three results compose. Closure says a delimited value stays in the language.
`embed_boundary` says a reader counting balance stops in the right place.
`to_matchertext` removes the precondition, so a caller need not check first.

## What is not proved here

The two papers are explicit that these results constrain **strings**, not
**parsers**. The security property of the injection paper additionally needs two
premises about a particular host, that its grammar derives one terminal from the
hole and that its interpreter treats that terminal as data. Neither is in scope
for this development; they are discharged per host, and for SQLite that work
lives in [`../injection-research/sqlite`](../injection-research/sqlite).

## Known gap

`to_matchertext.lean` derives its output-is-matchertext theorem from

```
hesc : ∀ c x, x ∈ esc c → Nonmatcher Pi x
```

which requires an escape sequence to contain no matcher, establishing
`MT (esc c)` by the flat rule. That is stronger than the theorem needs. The
escapes actually used, `\o()` and `\c()` and their bracket and brace forms,
carry a matched pair and so satisfy `MT (esc c)` by the nesting rule instead.
Generalizing the hypothesis to `∀ c, MT Pi (esc c)` would cover both, with
`esc_mt` becoming one way to discharge it rather than the only way.

Until that generalization lands, the C encoder in
`../injection-research/sqlite/src/matchertext.c` uses escapes the current
statement does not literally cover, though they are matchertext.
