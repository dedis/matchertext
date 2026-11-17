# Injection research pipeline

This pipeline downloads the pinned research corpus, classifies injection CVEs,
extracts payload groups, replays SQL payloads, and generates the paper data.

Run all commands from this directory:

```sh
cd injection-research
```

## Requirements

- Python 3.11 or later
- Git
- A C compiler available as `cc`, or set with `CC`
- `make` and Tcl (`tclsh`), to generate the SQLite amalgamation
- Internet access for an initial download
- The GitHub CLI (`gh`) for optional GitHub payload recovery
- Node.js and npm, for the HTML replay through the `parse5/` submodule

The SQL replay generates the MatcherText amalgamation from `sqlite/` in
`data/build/sqlite/` and compiles `test/matchertext/csrc/sqli_driver.c` with it.
It does not use the Python SQLite driver for the replay.

## Reproduce the complete pipeline

> **Run `perf.py` and `compat.py` first.** `run_all.py` does not run these two
> scripts, but its last stage, `latex.py`, needs their output files
> (`data/exports/perf.csv` and `data/exports/compat.csv`) for the cost and
> SQLite test-suite numbers of the paper. Without these files the paper
> does not build. The two scripts take about 3 and 30 minutes.
>
> ```sh
> python3 pipeline/perf.py
> LDFLAGS=-L/opt/homebrew/lib python3 pipeline/compat.py   # macOS with Homebrew Tcl
> ```

Then run the pipeline with the manifest pins and the immutable snapshot archive:

```sh
python3 pipeline/run_all.py
```

If all raw inputs are already present, omit the acquisition check:

```sh
python3 pipeline/run_all.py --skip-fetch
```

The full run executes these stages in order:

1. Acquire and verify inputs.
2. Build `data/cve.db`.
3. Classify and subclass injection CVEs.
4. Build syntactic payload groups.
5. Validate the general payload corpus.
6. Replay SQL payloads through the MatcherText SQLite drivers.
7. Replay HTML payloads through the MatcherText parse5 fork.
8. Generate CSV, HTML, and LaTeX results, with the files of `perf.py` and
   `compat.py`.

Use `--years` only for a reduced CVE-year run:

```sh
python3 pipeline/run_all.py --years 2024
```

## Run each stage

### 1. Acquire the pinned inputs

```sh
python3 pipeline/fetch.py
```

Git repositories are checked out at the exact commits in `manifest.json`.
HTTP files are checked against their SHA-256 values. Missing HTTP files are
restored from `snapshot-archive/sha256/`.

Upstream servers do not keep old versions of these files. On a new machine,
use the published archive. Export the setting before any pipeline command that
reads snapshots:

```sh
export MATCHERTEXT_SNAPSHOT_ARCHIVE=https://anonymous.4open.science/api/repo/matchertext-security-D2DC/file/archive
python3 pipeline/run_all.py
```

The setting also accepts another local archive or HTTP mirror with the same
`sha256/<digest>` layout. `juliet-c` is not in the published archive, because
it is larger than the GitHub file limit. `fetch.py` downloads the same bytes
from Zenodo record 4701387.

Do not use `--refresh` for a reproduction run. It replaces the manifest pins
with current upstream data. Use it only to create a new corpus snapshot:

```sh
python3 pipeline/fetch.py --refresh
```

A new snapshot needs a local archive. Push its new `sha256/` objects, except
`juliet-c`, to the published archive before you publish the new
`manifest.json`.

### 2. Build the database

```sh
python3 pipeline/build_db.py
```

This command parses `data/raw/`, imports the pinned remote-payload sidecar, and
recreates the source tables in `data/cve.db`. Each payload of the general
payload lists is stored canonical, as premise A3 of the paper requires:
`matchertext.canon` undoes the URL encoding (two rounds) and applies NFKC.
`syntactic_group.py` does the same for each CVE payload, so the skeletons,
Verify, and both replays all read one value.

### 3. Classify injection CVEs

```sh
python3 pipeline/classify.py
```

The classifier uses CWE anchors, phrase rules, and a deterministic word
unigram and bigram Naive Bayes fallback. The fallback also learns a
not-injection class from the CVEs the gate rejects; a CVE in that class stays
`unknown`. The default acceptance margin is `3.0`. To change it:

```sh
python3 pipeline/classify.py --nb-margin 4.0
```

Run the dependent classification stages next:

```sh
python3 pipeline/subclass.py
python3 pipeline/syntactic_group.py
```

A CVE enters the corpus when it has a CWE in the CWE-74 subtree or its
description matches a phrase rule. The CWEs of the CVE record and NVD decide;
those of GitHub, Red Hat, and Nuclei count only for a CVE that the first two
give none. `NOT_INJECTION` in `pipeline/taxonomy.py` removes CWE-99 and its
seven descendants from the subtree: no untrusted value in them enters an
embedded language. A code-eval label from a CWE alone is dropped when the
description names memory corruption, which NVD long filed under CWE-94.

`classify.py` also writes `data/exports/anchor_audit.csv`. It lists each
labelled CWE and whether it has its own anchor or inherits its labels from an
ancestor. The stage stops when a CWE with CVEs inherits labels that
`REVIEWED_INHERITED` in `pipeline/taxonomy.py` does not list. Review such a CWE
in the CSV, then give it an anchor or add its labels to that list.

`subclass.py` adds the independent privilege, vector, interaction, technique,
engine, injection-point, and impact labels. `syntactic_group.py` groups
payload-backed CVEs by their normalized attack structure.

### 4. Validate the payload corpus

```sh
python3 pipeline/validate.py
```

This stage classifies the general payload corpora under the MatcherText threat
model and checks the CWE taxonomy against OWASP Benchmark and NIST SARD data.
The syntax label decides whether a payload is in scope, except for the
`javascript:` sinks of HTML/DOM. The payload decides only whether it embeds
inert or is rejected. It writes
`data/exports/payload_prevention.csv` and
`data/exports/ground_truth_anchors.csv`.

### 5. Replay SQL payloads

```sh
python3 pipeline/sqli_experiment.py
```

This command generates the amalgamation and builds the normal and strict
MatcherText SQLite drivers. It then replays every distinct SQL payload through
all control and defense arms, `%Q`, `%q`, `"%w"`, and bound parameters among
them. It also measures how often strict `?V` refuses benign text, the CVE
descriptions outside the corpus. A hole-targeted set from
`pipeline/adaptive.py` stands in for an adaptive attacker and runs apart from
the recorded corpus: boundary atoms around breakout cores, mutations of the
effective payloads made matchertext so that Verify accepts them, and nesting at
the depth limit. Stock SQLite (`test/matchertext/csrc/stock_reader.c`, built
from the same tree without MatcherText) then reads the statements that the
text holes compose for both sets, as a reader that was not upgraded would. The run stops, and removes its results, when a
guard fails: a driver exits early, a control never breaks out, a matchertext
arm breaks out or changes a value on either set, the strict build admits a
legacy path, or the matchertext API compiles SQL that is not a string literal.
It
stores detailed results in `data/cve.db` and writes these principal files:

- `data/exports/sqli_arm_outcomes.csv`
- `data/exports/sqli_by_arm.csv`
- `data/exports/sqli_exemplars.csv`
- `data/exports/sqli_adaptive.csv`
- `data/exports/sqli_downgrade.csv`
- `data/exports/sqli_sabotage.csv`

Useful reduced runs are:

```sh
python3 pipeline/sqli_experiment.py --limit 100
python3 pipeline/sqli_experiment.py --skip-sabotage
python3 pipeline/sqli_experiment.py --sabotage-n 1000
```

The sabotage runs rebuild the core with one defect each: the locator, Verify,
the encoder, or the decoder. Each must change a verdict or a value, and a
broken locator or encoder must produce a breakout. The encoder breakouts come
only from two constructed probes in `SABOTAGE_PROBES`: no recorded payload has
an unmatched closer followed by SQL.

### 5b. Replay HTML payloads

```sh
python3 pipeline/html_experiment.py
```

This command builds the MatcherText parse5 fork in `parse5/` when its sources
changed, and runs `test/matchertext/jssrc/html_driver.mjs`. It replays every
distinct HTML payload through 13 host contexts and 9 arms: the raw control,
entity escaping, a tree written by the stock and by the guarded serializer,
and five matchertext arms, one of them a tree written with matchertext output. It also records how a parser without the
matchertext option reads each matchertext document. With both MatcherText
options off, the fork passes parse5's test suite unchanged; the serializer
guard, on by default, changes html5lib serializer tests 18 (`plaintext`) and 27
(a leading line feed in `pre`, `textarea`, `listing`), whose stock output
parses back as another tree. The hole-targeted set runs
apart with two more arms that send the verified-hole document as UTF-8 bytes
and decode it as a reader does: `mt_utf8` with the WHATWG UTF-8 decoder, and
`mt_sjis` with Shift_JIS, a charset the writer did not declare. The run stops, and
removes its results, when a guard fails: the driver exits early, a control
never breaks out, a matchertext arm or the guarded serializer breaks out, a
matchertext document fails to parse or to serialize back, `composeHtml`
accepts markup that is not a template literal, a deliberately broken fork goes
unnoticed, or a fork whose hole ends at the first closer or whose serializer
guard is off produces no breakout, or the Shift_JIS reader never breaks out.
It writes:

- `data/exports/html_arm_outcomes.csv`
- `data/exports/html_by_arm.csv`
- `data/exports/html_reader.csv`
- `data/exports/html_exemplars.csv`
- `data/exports/html_adaptive.csv`
- `data/exports/html_sabotage.csv`

`run_all.py` runs this stage after the SQL replay. `latex.py` writes its
macros and `doc/generated/tbl-html-arms.tex`.

### 5c. Measure cost and compatibility

These two commands run apart from `run_all.py`, since their results depend on
the machine and take minutes. Run them before `latex.py`, which quotes their
files.

```sh
python3 pipeline/perf.py
LDFLAGS=-L/opt/homebrew/lib python3 pipeline/compat.py   # macOS with Homebrew Tcl
```

`perf.py` times SQLite's `speedtest1` on the stock and the additive build of
the fork's tree, one value into a prepared statement by bind, by the `%m`
hole, and by the typed `?V` path (`test/matchertext/csrc/prepare_bench.c`), and
parse5's benchmark page on the upstream parse5 under the fork and on the fork
(`test/matchertext/jssrc/perf.mjs`). It writes `data/exports/perf.csv`.

`compat.py` runs SQLite's full Tcl test suite on the stock and the additive
build and writes `data/exports/compat.csv`.

### 6. Generate exports and paper data

Run these commands after classification and replay:

```sh
python3 pipeline/export.py
python3 pipeline/report.py --samples 5 --seed 42
python3 pipeline/latex.py
```

They write the analysis CSV files and `report.html` under `data/exports/`.
They also write the macros, eight tables and one figure under `doc/generated/`
for the paper.

### 7. Audit the syntax labels

Two raters check a frozen sample of 300 labels, 75 per labeling layer. The
labeling rules are in `audit/README.md`.

```sh
python3 pipeline/audit.py sheets   # once: audit/sample.csv and two blinded sheets
python3 pipeline/audit.py gold     # after both raters finish
python3 pipeline/audit.py report
```

Once `audit/gold.csv` is complete, `latex.py` adds the agreement, per-layer
precision, and label-adjusted ceiling as `\dataAudit*` macros.

## Update the remote payload corpus

This step is optional. A normal reproduction imports the pinned sidecar and
does not contact GitHub or advisory sites.

`build_db.py` imports the pinned sidecar. If the archive does not have that
object, delete the `remote-payloads` entry from `manifest.json` first.

First build and classify the database. Authenticate `gh` if it is available:

```sh
gh auth login
python3 pipeline/build_db.py
python3 pipeline/classify.py
python3 pipeline/subclass.py
python3 pipeline/syntactic_group.py
```

Run the GitHub recovery passes as required:

```sh
python3 pipeline/fetch_github.py
python3 pipeline/fetch_github.py --retry --archives
python3 pipeline/fetch_github.py --trees
python3 pipeline/fetch_github.py --fanout-trees
python3 pipeline/fetch_github.py --alternates
python3 pipeline/fetch_github.py --constructed
```

Recover payloads from advisory pages and GitHub Security Lab:

```sh
python3 pipeline/fetch_web.py
python3 pipeline/fetch_web.py --ghsl
```

Each recovery command exports the complete remote tables to an immutable
sidecar and updates its hash and row counts in `manifest.json`. After the final
recovery pass, rebuild all dependent results:

```sh
python3 pipeline/run_all.py --skip-fetch
```

Payload recovery must use a local snapshot archive because it writes a new
sidecar. To export the current remote tables without another network pass:

```sh
python3 pipeline/remote_sidecar.py
```

These commands access third-party services. Keep the built-in rate limits and
follow the source terms recorded in `manifest.json`.

## Main outputs

| Path                       | Contents                                                 |
|----------------------------|----------------------------------------------------------|
| `manifest.json`            | Git commits, HTTP hashes, and sidecar pin                |
| `snapshot-archive/sha256/` | Immutable HTTP and sidecar objects                       |
| `data/raw/`                | Pinned source repositories and downloaded files          |
| `data/cve.db`              | Parsed corpus, classifications, groups, and replay cases |
| `data/exports/`            | CSV analysis files and the HTML report                   |
| `doc/generated/`           | LaTeX macros and tables used by the paper                |

The large snapshot objects are ignored by Git. Copy or publish the complete
snapshot archive when the corpus must be reproduced on another machine.
