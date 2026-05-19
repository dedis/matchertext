# LLVM MatcherText Metrics Tool

This tool parses multi-language source trees with LLVM/Clang and reports:

- Embedded string and documentation statistics
- File-level MatcherText violation statistics
- Nesting histograms
- Total parsing time

## Build

From the `LLVM/` directory:

```sh
cmake -B build -S .
cmake --build build
```

The build produces:

- `build/LLVM_MinML_Parser`
- `parser` at the repository root as a convenience copy

### macOS notes

On macOS, the project automatically checks the standard Homebrew prefixes:

- `/opt/homebrew/opt/llvm`
- `/opt/homebrew/opt/libomp`

If they are available, CMake adds them to `CMAKE_PREFIX_PATH`. OpenMP is enabled only when it is actually found; otherwise the tool still builds and runs without parallel parsing.

## Usage

Run the parser on one or more files or directories:

```sh
./parser <file-or-directory>...
```

Examples:

```sh
./parser tests
./parser ../some-project/src
./parser file1.cpp file2.hpp
```

By default the tool scans for all registered languages (C, C++, Python, Go).
Use `--language` to restrict analysis to a single language:

```sh
./parser ../some-project/src --language cpp
./parser ../some-project/src --language python
```

Known language identifiers: `c`, `cpp`, `go`, `python`.

### Optional flags

| Flag | Description |
|------|-------------|
| `--language <lang>` | Restrict to a single language (default: all languages) |
| `--output <dir>` | Output directory for all result files (default: `./result`) |
| `--log-strings` | Write max-string diagnostics to `<output>/max_strings.md` |
| `--compiler <path>` | Override the compiler/interpreter used for the selected language |
| `--extensions <a,b,...>` | Add extra file extensions to match (single-language mode only) |

## Language Classification

Embedded strings are classified through `ClassifyString` in
`include/LanguageClassifier.hpp`.

The classifier uses two layers:

- Fast structural detectors for precise categories such as `URL`, `FilePath`,
  `JSON`, `HTML`, `SQL`, `Regex`, `Shell`, `YAML`, and `PlainText`
- A trigram Naive Bayes model for broader programming-language and
  domain-specific-language detection when the string is long enough and the
  match is unambiguous

The generated model is already checked in as
`include/LanguageModel.generated.hpp`, so normal builds and tests do not need
to retrain it.

Retrain the model when you change the language enum, the trainable language
mapping, or the snippet-extraction / feature-selection heuristics:

The generator needs:

- Python 3.8 or newer
- Git, because `make train` fetches or updates `github-linguist/linguist`

It does not require any third-party Python packages.

```sh
make train
```

`make train` clones or updates `ignore/linguist`, runs
`train/generate_model.py`, rewrites `include/LanguageModel.generated.hpp`, and
rebuilds the project. It auto-detects `python3` first, then `python`, and you
can override the interpreter explicitly if needed:

```sh
make train PYTHON=/path/to/python
```

To run the generator manually:

```sh
/path/to/python train/generate_model.py ignore/linguist/samples \
  -o include/LanguageModel.generated.hpp \
  --eval-holdout 0.1
```

Keep `LANGUAGE_MAP` in `train/generate_model.py` aligned with `Language` in
`include/LanguageClassifier.hpp`; the generated header stores those numeric ids
directly.

## Output

All results are written to the output directory (default `./result`):

| File | Contents |
|------|----------|
| `strings.md` | Embedded string and documentation statistics |
| `files.md` | File-level violation statistics |
| `nesting.md` | Nesting depth histograms |
| `language_stats.md` | String language classification breakdown |
| `max_strings.md` | Max-string diagnostics (`--log-strings` only) |

Each markdown file contains a description table followed by the data table.
Timing is printed to stderr on completion.

## Metric definitions

See [Metrics.md](Metrics.md) for the meaning of each reported metric and example outputs on analysed repositories.
