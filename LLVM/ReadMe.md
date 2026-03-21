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

## Output

The tool prints three sections:

1. Embedded statistics for `Strings`, `Documentation`, and `Documentation Relaxed`
2. File-level violation statistics
3. Nesting histograms

It finishes with the total parsing time in milliseconds.

## Metric definitions

See [Metrics.md](Metrics.md) for the meaning of each reported metric and example outputs on analysed repositories.
