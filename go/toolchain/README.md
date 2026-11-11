# Matchertext literals in Go

A patch to the Go toolchain that adds matchertext literals `M"(...)"`, `M"[...]"`,
and `M"{...}"`, and an experiment that runs real repositories' tests before and
after rewriting their string literals into this form.

| File                | Contents                                                                                                  |
|---------------------|-----------------------------------------------------------------------------------------------------------|
| `matchertext.patch` | The extension, against go1.27.1 (`cmd/compile/internal/syntax` and `go/scanner`)                          |
| `build.sh`          | Clones go1.27.1, applies the patch, and builds the toolchain                                              |
| `mrewrite/`         | Rewrites interpreted literals `"..."` as `M"(...)"` where the value stays the same, and counts toothpicks |
| `testdata/`         | Acceptance tests (`mlit/`) and literals the patched compiler must reject (`bad/`)                         |
| `experiment.sh`     | Runs the experiment                                                                                       |
| `results/`          | Results of the run reported in the paper                                                                  |

## Requirements

Go 1.27.1 (bootstraps the patched build), `git`, `jq`, and network access to clone the repositories and download their modules.

## Running the experiment

```sh
./experiment.sh <work-dir> \
  https://github.com/cockroachdb/uniseg.git@f77ce1977fd80e61b34e51bf509394656d1182a8 \
  https://github.com/golang/text.git@6070ac32abcdd1670599eeb3ee4d513c8184a513 \
  https://github.com/golang/net.git@3de643db506517b3e652a13cc0b56034cea6b8e6 \
  https://github.com/golang/tools.git@588ada3c4d4d6d24ab1758e86bd2768beba5946d
```

The script builds the patched toolchain and runs its acceptance tests. Then, for
each repository, it runs `go test -short ./...` three times: with stock Go, with
patched Go on the unchanged code, and with patched Go after `mrewrite` has rewritten
the repository's own code (`testdata/` and `vendor/` excluded; dependencies come
unchanged from the module cache). It compares the pass, fail, and skip outcome of
every package across the three runs.

The commands run each repository's tests on the local machine; use a container or
virtual machine if that is a concern.

## Results

`results/summary.txt` has one line per repository: package counts, whether the
patched and rewritten runs match the stock run (`same` or `DIFFERENT`), and the
`mrewrite` counts. `toothpicks` and `converted` count every interpreted literal the
way the analyzer does (`data/src/RawRewrite.cpp`); `written`, `writtenBefore`, and
`writtenAfter` count the literals actually rewritten and their toothpicks before
and after. `results/<repo>.*.outcomes` list the per-package outcomes, and
`results/<repo>.commit` the commit tested. The raw `go test -json` logs (156 MB) are
not included; the run regenerates them in `<work-dir>/results`.

| Repository         | Packages (pass / no tests / fail) | Patched | Rewritten | Literals written | Toothpicks before → after |
|--------------------|-----------------------------------|---------|-----------|------------------|---------------------------|
| cockroachdb/uniseg | 1 / 0 / 0                         | same    | same      | 7,739            | 24,831 → 6                |
| golang/text        | 48 / 15 / 0                       | same    | same      | 3,042            | 6,470 → 40                |
| golang/net         | 36 / 5 / 0                        | same    | same      | 924              | 1,630 → 9                 |
| golang/tools       | 157 / 60 / 0                      | same    | same      | 2,692            | 4,731 → 33                |
