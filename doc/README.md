# Matchertext paper

LaTeX source for *Matchertext: Towards Verbatim Interlanguage Embedding*, the
paper that defines the discipline.

## Build

```sh
make
```

The PDF is written to `main.pdf`. Always build through `make`; it sets the
output directory and the bibliography passes that a bare `pdflatex` run misses.

| Target       | Description                            |
|--------------|----------------------------------------|
| `make`       | Full build, including bibliography     |
| `make fast`  | One pass, for checking a small edit    |
| `make link`  | Resolve cross-references and citations |
| `make open`  | Build and open the PDF                 |
| `make clean` | Remove `build/`                        |

## Layout

`main.tex` includes the sections in reading order:

| File                       | Section                                                                         |
|----------------------------|---------------------------------------------------------------------------------|
| `abs.tex`                  | Abstract                                                                        |
| `intro.tex`                | Introduction                                                                    |
| `bg.tex`                   | Background: the needs and pitfalls of interlanguage embedding                   |
| `design.tex`               | The discipline: abstract definition, the standard configuration, and variations |
| `always_embeddability.tex` | The embeddability guarantee and the recognizer                                  |
| `host.tex`                 | Host-language considerations: what a language must do to hold matchertext       |
| `embed.tex`                | Embedded-syntax considerations: what a language must do to be held              |
| `impl.tex`                 | Implementations, including the compiler grammar and MinML                       |
| `eval.tex`                 | Empirical compliance study over production source code                          |
| `rel.tex`                  | Related work                                                                    |
| `concl.tex`                | Conclusion                                                                      |

The proofs the paper cites are in [`../lean`](../lean), and the empirical
figures come from the tool in [`../LLVM`](../LLVM).
