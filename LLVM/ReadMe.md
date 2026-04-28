# LLVM MatcherText Metrics Tool

This tool parses C and C++ source trees with LLVM/Clang and reports:

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

Only C and C++ files are processed when scanning directories:

- `.c`
- `.h`
- `.cc`
- `.cpp`
- `.cxx`
- `.hpp`
- `.hh`
- `.hxx`

### Optional flags

```sh
./parser --log-strings <file-or-directory>...
```

`--log-strings` prints the max-string diagnostics in addition to the metrics tables.

```sh
./parser --debug-languages <file-or-directory>...
```

`--debug-languages` writes up to 1,000 sampled string literals per detected
language into `./result/<input-path>/<language>.txt`.

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

The tool prints three sections:

1. Embedded statistics for `Strings`, `Documentation`, and `Documentation Relaxed`
2. File-level violation statistics
3. Nesting histograms
4. String-language breakdowns

When `--debug-languages` is enabled, the parser also writes per-language sample
files under `./result/`.

It finishes with the total parsing time in milliseconds.

## Metric definitions

See [Metrics.md](Metrics.md) for the meaning of each reported metric and example outputs on analysed repositories.
