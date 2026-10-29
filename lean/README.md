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

The three results compose. Closure says a delimited value stays in the language.
`embed_boundary` says a reader counting balance stops in the right place.
