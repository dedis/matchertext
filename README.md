# Matchertext

Matchertext is a syntactic discipline that lets a string in one language be
embedded in another **verbatim**, with no escaping, obfuscation, or expansion.
Cut and paste becomes correct by construction.

It is one rule: **matchers must match.** The ASCII matcher pairs `()`, `[]` and
`{}` must appear in properly nested pairs throughout a compliant string.
Everything else is a nonmatcher and is unconstrained.

```
matchertext:      a(b)c      call(item[index])      ' OR '1'='1
not matchertext:  a)b        ([)]                   smile :]
```

The payoff is a boundary that does not depend on the contents. A host language
that delimits a hole with a matcher pair can find the end of an embedded value
by counting balance, so no character inside the value needs escaping and no
character can end the value early.

## Directory map

Each directory documents itself; follow the link rather than looking for
instructions here.

| Directory                                  | Contents                                                                              |
|--------------------------------------------|---------------------------------------------------------------------------------------|
| [`go`](go)                                 | Go reference implementation: the matchertext scanner, MinML, and the language server  |
| [`injection-research`](injection-research) | The injection study: CVE pipeline and corpus                                          |
| [`lean`](lean)                             | Machine-checked proofs of the properties both papers rely on                          |

Smaller items with no README of their own: `perl/escapes.pl` and
`raku/escapes.raku` sketch matcher escape sequences for those languages, and
`test/` holds sample MinML documents.

## Building

The MinML tooling is built from the repository root, which is a single entry
point rather than the home of the work.

| Command                    | Produces                                              |
|----------------------------|-------------------------------------------------------|
| `make build`               | the `minml` CLI                                       |

## Contributing

This is a research repository and a work in progress. Contributions are
welcome; see the paper for the open questions each part is trying to answer.
