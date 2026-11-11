# Corpus record

What the evaluation's corpus contains and how it was collected.

## Collection

`clone-orgs.sh` with its defaults:

- every repository of each organization that is not archived (`SKIP_ARCHIVED=1`),
  forks included (`SKIP_FORKS=0`);
- a shallow clone (`DEPTH=1`) of each repository's default branch at the time of
  cloning;
- the organization's `.github` repository is not parsed.

No repositories were selected by hand within an organization, and nothing is
deduplicated: forks, vendored libraries, generated files, and minified files count
as often as they occur.

`corpus-repositories.txt` lists the 13,721 repositories of the 79 organizations
whose results `result/repos/` holds, one `org/repo` per line.

## Dates

- Per-repository results: run of 2026-11-09.
- Global tables (`result/global/`, `build_global.py`): 2026-11-10.

The commit of each clone was not recorded. Rerunning `clone-orgs.sh` clones the
repositories' current default branches, so results can differ slightly.

## Extraction limits

The tree-sitter path (all languages except C and C++) does not parse a file that:

- is larger than 16 MiB (`kMaxFileBytes`);
- contains a NUL byte, treated as binary;
- is not parsed within 5 seconds (`kParseTimeout`).

The analyzer now reports these files in `files.md` (`Skipped Oversized`,
`Skipped Binary`, `Skipped Timeout`) and leaves them out of `Sample Size`.
The results of 2026-11-09 predate this: they counted such files as parsed files
with no samples, and so as compliant files, and did not count them.

Those results also miss C and C++ comments that directly follow a string literal,
which the lexer loop skipped (`tests/comment_after_string.cpp`); this is fixed.
How many files and comments the two issues affected needs a rerun to measure.
