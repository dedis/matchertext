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
- Internet access for an initial download
- The GitHub CLI (`gh`) for optional GitHub payload recovery

The SQL replay compiles `test/matchertext/csrc/sqli_driver.c` with the local
`sqlite/sqlite3.c`. It does not use the Python SQLite driver for the replay.

## Reproduce the complete pipeline

Use the manifest pins and the immutable snapshot archive:

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
7. Generate CSV, HTML, and LaTeX results.

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

To use another local archive or an HTTP mirror, export the setting before any
pipeline command that reads snapshots:

```sh
export MATCHERTEXT_SNAPSHOT_ARCHIVE=/path/to/archive
python3 pipeline/run_all.py

export MATCHERTEXT_SNAPSHOT_ARCHIVE=https://example.org/archive
python3 pipeline/run_all.py
```

Do not use `--refresh` for a reproduction run. It replaces the manifest pins
with current upstream data. Use it only to create a new corpus snapshot:

```sh
python3 pipeline/fetch.py --refresh
```

### 2. Build the database

```sh
python3 pipeline/build_db.py
```

This command parses `data/raw/`, imports the pinned remote-payload sidecar, and
recreates the source tables in `data/cve.db`.

### 3. Classify injection CVEs

```sh
python3 pipeline/classify.py
```

The classifier uses CWE anchors, phrase rules, and a deterministic trigram
Naive Bayes fallback. The default acceptance margin is `3.0`. To change it:

```sh
python3 pipeline/classify.py --nb-margin 4.0
```

Run the dependent classification stages next:

```sh
python3 pipeline/subclass.py
python3 pipeline/syntactic_group.py
```

`subclass.py` adds the independent privilege, vector, interaction, technique,
engine, injection-point, and impact labels. `syntactic_group.py` groups
payload-backed CVEs by their normalized attack structure.

### 4. Validate the payload corpus

```sh
python3 pipeline/validate.py
```

This stage measures MatcherText prevention over the general payload corpora and
checks the CWE taxonomy against OWASP Benchmark and NIST SARD data. It writes
`data/exports/payload_prevention.csv` and
`data/exports/ground_truth_anchors.csv`.

### 5. Replay SQL payloads

```sh
python3 pipeline/sqli_experiment.py
```

This command builds the normal and strict MatcherText SQLite drivers. It then
replays every distinct SQL payload through all control and defense arms. It
stores detailed results in `data/cve.db` and writes these principal files:

- `data/exports/sqli_arm_outcomes.csv`
- `data/exports/sqli_by_arm.csv`
- `data/exports/sqli_exemplars.csv`
- `data/exports/sqli_sabotage.csv`

Useful reduced runs are:

```sh
python3 pipeline/sqli_experiment.py --limit 100
python3 pipeline/sqli_experiment.py --skip-sabotage
python3 pipeline/sqli_experiment.py --sabotage-n 1000
```

### 6. Generate exports and paper data

Run these commands after classification and replay:

```sh
python3 pipeline/export.py
python3 pipeline/report.py --samples 5 --seed 42
python3 pipeline/latex.py
```

They write the analysis CSV files and `report.html` under `data/exports/`.
They also write 147 macros and six tables under `doc/generated/` for the paper.

## Update the remote payload corpus

This step is optional. A normal reproduction imports the pinned sidecar and
does not contact GitHub or advisory sites.

First build and classify the database. Authenticate `gh` if it is available:

```sh
gh auth login
python3 pipeline/build_db.py
python3 pipeline/classify.py
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
