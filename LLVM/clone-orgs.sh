#!/usr/bin/env bash
#
# clone-orgs.sh — clone every (non-archived) repository in one or more GitHub
# orgs in parallel, parse each repo as it lands, and emit a whole-org parse
# before discarding the clone tree. Designed to stay well under GitHub's API
# and abuse limits.
#
# Usage:
#   ./clone-orgs.sh [-o <output-dir>] <org1> [<org2> ...]
#
# Args / flags:
#   -o, --output DIR  output root directory (overrides $OUTPUT, default ./repos)
#
# Tunables (env vars; flag wins if both are set):
#   JOBS          parallelism (default 8 ; >16 starts tripping secondary
#                 abuse limits and disk I/O caps the benefit anyway)
#   DEPTH         shallow-clone depth   (default 1 ; set 0 for full history)
#   OUTPUT        output root dir       (default ./repos)
#   SKIP_ARCHIVED skip archived repos   (default 1)
#   SKIP_FORKS    skip forked repos     (default 0)
#   RETRIES       per-repo retry count  (default 3, exponential backoff)
#
# Per-org flow:
#   1. List the org's repos.
#   2. Clone them in parallel (bounded by JOBS). The instant a repo is on disk
#      its tree is handed to `parser <repo>` in the background, so parsing
#      overlaps with the next download. The org's special `.github` repo is
#      never parsed.
#   3. Once every clone and every per-repo parse has finished, run one more
#      `parser` pass over the whole org folder, then delete that folder and
#      move on to the next org.
#
# Requirements: gh (authenticated — run `gh auth login` once), git, jq, and the
# built `parser` binary next to this script.
# For private repos also run `gh auth setup-git` so plain `git clone` picks
# up the gh token via git's credential helper.
#
# Rate-limit strategy:
#   1. `gh repo list` uses GraphQL and paginates internally → cheapest per
#      repo on the metadata side.
#   2. `git clone` over HTTPS is rate-limited *separately* from the REST/
#      GraphQL quotas, with much more lenient ceilings, so the clone storm
#      doesn't touch the 5000/hr API budget.
#   3. JOBS bounds in-flight HTTPS connections to a number GitHub tolerates
#      without secondary throttling.
#   4. Failures are retried with quadratic backoff (2 s, 8 s, 18 s …).

set -euo pipefail

JOBS=${JOBS:-8}
DEPTH=${DEPTH:-1}
OUTPUT=${OUTPUT:-./repos}
SKIP_ARCHIVED=${SKIP_ARCHIVED:-1}
SKIP_FORKS=${SKIP_FORKS:-0}
RETRIES=${RETRIES:-3}
NO_CLEANUP=${NO_CLEANUP:-0}
PARSE_ORG=${PARSE_ORG:-0}
PARSE_ONLY=${PARSE_ONLY:-0}

die() { printf 'error: %s\n' "$*" >&2; exit 1; }
log() { printf '%s\n' "$*" >&2; }
usage() { die "usage: $0 [-o <output-dir>] [--no-cleanup] [--parse-org] [--parse-only] <org> [<org> ...]"; }

# Collect orgs into an array so flags and org names may appear in any order
# (e.g. `... google --parse-only` works the same as `... --parse-only google`).
ORGS=()
while [ $# -gt 0 ]; do
  case $1 in
    -o|--output)
      [ $# -ge 2 ] || usage
      OUTPUT=$2; shift 2 ;;
    --output=*)
      OUTPUT=${1#*=}; shift ;;
    --no-cleanup)
      NO_CLEANUP=1; shift ;;
    --parse-org)
      PARSE_ORG=1; shift ;;
    --parse-only)
      PARSE_ONLY=1; shift ;;
    -h|--help)
      usage ;;
    --)
      shift
      while [ $# -gt 0 ]; do ORGS+=("$1"); shift; done
      break ;;
    -*)
      die "unknown flag: $1" ;;
    *)
      ORGS+=("$1"); shift ;;
  esac
done

[ ${#ORGS[@]} -ge 1 ] || usage

SCRIPT_DIR=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
PARSER="${SCRIPT_DIR}/parser"
[ -x "${PARSER}" ] || die "parser binary not found / not executable at ${PARSER} (build it first)"

# --parse-org / --parse-only read local trees only, so they need neither gh/jq
# nor a network round-trip — skip those requirements (and the API budget check)
# in those modes.
if [ "${PARSE_ORG}" != 1 ] && [ "${PARSE_ONLY}" != 1 ]; then
  command -v gh  >/dev/null || die "gh CLI not found (https://cli.github.com/)"
  command -v git >/dev/null || die "git not found"
  command -v jq  >/dev/null || die "jq not found"
  gh auth status >/dev/null 2>&1 || die "not authenticated — run: gh auth login"

  remaining=$(gh api rate_limit -q '.resources.core.remaining' 2>/dev/null || echo 0)
  log "GitHub REST budget: ${remaining}/5000 remaining"
  [ "${remaining}" -ge 100 ] || die "REST budget too low (${remaining}); wait or use a different token"
fi

mkdir -p "${OUTPUT}"

# Build a jq filter that conditionally drops archived / fork repos.
jq_filter='.[]'
[ "${SKIP_ARCHIVED}" = 1 ] && jq_filter="${jq_filter} | select(.isArchived | not)"
[ "${SKIP_FORKS}"    = 1 ] && jq_filter="${jq_filter} | select(.isFork    | not)"
jq_filter="${jq_filter} | \"\\(.owner.login)/\\(.name)\""

list_repos() {
  local org=$1
  log "listing: ${org}"
  gh repo list "${org}" \
    --limit 100000 \
    --json name,owner,isArchived,isFork \
    -q "${jq_filter}"
}

CLONE_LOG="${OUTPUT}/clone.log"
PARSE_LOG="${OUTPUT}/parse.log"             # one line per repo the consumer has parsed
PARSE_QUEUE_LOG="${OUTPUT}/parse_queue.log" # one line per repo enqueued for parsing
FAIL_LOG="${OUTPUT}/clone_failures.log"     # git stderr for repos that failed all retries
TOTAL_FAIL=0

# Truncate the failure log once per run so it accumulates across orgs (each entry
# is prefixed with its owner/repo slug) without carrying over a previous run.
: > "${FAIL_LOG}"

# Per-repo worker: skip-if-exists, clone, retry on failure with quadratic
# backoff. Records its outcome in CLONE_LOG (one short line, append is atomic
# for the lengths used here) and stays silent on stdout so the progress bar
# rendered by the watcher below is the only thing on the user's terminal.
# Always returns 0 so a single bad repo doesn't sink the batch.
#
# As soon as the repo is on disk its path is pushed onto PARSE_FIFO. A single
# background consumer (see parse_consumer) drains that queue and parses repos
# one at a time, so parsing overlaps with downloads yet never runs more than one
# parser at once. The org's special `.github` repo is never enqueued.
clone_one() {
  local slug=$1
  local dest="${OUTPUT}/${slug}"
  local name=${slug##*/}
  local outcome

  if [ -d "${dest}/.git" ]; then
    outcome="SKIP  ${slug}"
  else
    mkdir -p "$(dirname "${dest}")"
    # Disable the Git LFS filter for the clone. GIT_LFS_SKIP_SMUDGE only works
    # when the git-lfs binary is installed (it makes its smudge a no-op); if it
    # isn't, git still tries to launch the globally-configured `git-lfs
    # filter-process` and, because `filter.lfs.required=true`, aborts the
    # checkout. Overriding the filter config at the git level makes git check
    # out LFS pointer files as plain text instead — works installed or not, and
    # avoids fetching large blobs we don't need for source analysis.
    local -a args=(
      -c filter.lfs.smudge= -c filter.lfs.process= -c filter.lfs.required=false
      clone --quiet "https://github.com/${slug}.git" "${dest}"
    )
    if [ "${DEPTH}" -gt 0 ]; then
      args+=(--depth="${DEPTH}" --single-branch --no-tags)
    fi

    outcome="FAIL  ${slug}"
    local attempt err
    err=$(mktemp -t clone-err.XXXXXX)
    for attempt in $(seq 1 "${RETRIES}"); do
      if git "${args[@]}" 2>"${err}"; then
        outcome="OK    ${slug}"
        break
      fi
      rm -rf "${dest}"
      sleep $(( attempt * attempt * 2 ))
    done
    # On total failure, record git's stderr from the last attempt so the cause
    # (empty repo, DMCA/disabled, auth, network) is recoverable after the run.
    if [ "${outcome}" = "FAIL  ${slug}" ]; then
      {
        printf '=== %s ===\n' "${slug}"
        sed 's/^/  /' "${err}"
        printf '\n'
      } >> "${FAIL_LOG}"
    fi
    rm -f "${err}"
  fi

  printf '%s\n' "${outcome}" >> "${CLONE_LOG}"

  # Account for this repo as one parse unit. Repos with parseable content are
  # enqueued for the single consumer (which ticks the parse log once parsed);
  # everything else — the org `.github` repo, a missing tree, or a failed
  # clone — has nothing to parse, so it ticks the parse log here as a resolved
  # unit. Either way each repo advances the parse bar exactly once.
  case "${outcome}" in
    OK*|SKIP*)
      if [ "${name}" != ".github" ] && [ -d "${dest}" ]; then
        printf 'Q\n' >> "${PARSE_QUEUE_LOG}"
        printf '%s\n' "${dest}" > "${PARSE_FIFO}"
      else
        printf 'S\n' >> "${PARSE_LOG}"
      fi
      ;;
    *)
      printf 'S\n' >> "${PARSE_LOG}"
      ;;
  esac
}
export -f clone_one
export OUTPUT DEPTH RETRIES CLONE_LOG PARSE_LOG PARSE_QUEUE_LOG FAIL_LOG PARSER

# Progress bar matching main.cpp's per-language renderer: 40-wide "[####....]"
# refreshed in place. A background watcher polls the clone log every 200 ms and
# re-renders, so all stdio races stay inside this one process.
draw_bar() {
  local n=$1 tot=$2
  local pct=$(( tot > 0 ? n * 100 / tot : 0 ))
  local W=40
  local filled=$(( pct * W / 100 ))
  local full='########################################'
  local empty='........................................'
  printf '\r  [%s%s] %d/%d (%d%%)   ' \
    "${full:0:filled}" "${empty:0:$((W - filled))}" "$n" "$tot" "$pct" >&2
}
export -f draw_bar

# Render the clone and parse progress side-by-side on one in-place line. Parse's
# denominator is "enqueued so far", which grows during cloning and is final once
# cloning ends, so the bar tracks real outstanding work rather than a guess.
draw_bars() {
  local cn=$1 ct=$2 pn=$3 pt=$4
  local W=20
  local full='########################################'
  local empty='........................................'
  # Guard the divisions explicitly: bash 3.2's $(( a ? b/c : 0 )) still
  # evaluates b/c when c is 0, and the parse denominator starts at 0.
  local cf=0 pf=0
  [ "${ct}" -gt 0 ] && cf=$(( cn * 100 / ct * W / 100 ))
  [ "${pt}" -gt 0 ] && pf=$(( pn * 100 / pt * W / 100 ))
  printf '\r  clone [%s%s] %d/%d  parse [%s%s] %d/%d   ' \
    "${full:0:cf}" "${empty:0:$((W - cf))}" "$cn" "$ct" \
    "${full:0:pf}" "${empty:0:$((W - pf))}" "$pn" "$pt" >&2
}
export -f draw_bars

# Single parse consumer: drains repo paths from PARSE_FIFO and parses them one
# at a time, guaranteeing only one parser process is ever live. Holds the FIFO
# open read-write (fd 3) so enqueuing clones never block on a missing reader,
# and stops on the `__DONE__` sentinel the org driver sends once cloning ends.
parse_consumer() {
  local repo
  exec 3<>"${PARSE_FIFO}"
  while IFS= read -r repo <&3; do
    [ "${repo}" = "__DONE__" ] && break
    [ -d "${repo}" ] && { "${PARSER}" "${repo}" >/dev/null 2>&1 || true; }
    printf 'P\n' >> "${PARSE_LOG}"
  done
  exec 3<&-
}

# Tear down any live progress watcher / parse consumer on exit, even on error.
CURRENT_WATCHER=""
CURRENT_CONSUMER=""
CURRENT_FIFO=""
cleanup() {
  [ -n "${CURRENT_WATCHER}" ] && kill "${CURRENT_WATCHER}" 2>/dev/null
  [ -n "${CURRENT_CONSUMER}" ] && kill "${CURRENT_CONSUMER}" 2>/dev/null
  wait "${CURRENT_WATCHER}" 2>/dev/null || true
  wait "${CURRENT_CONSUMER}" 2>/dev/null || true
  [ -n "${CURRENT_FIFO}" ] && rm -f "${CURRENT_FIFO}"
  rm -f "${CLONE_LOG}" "${PARSE_LOG}" "${PARSE_QUEUE_LOG}"
}
trap cleanup EXIT

# Resolve an org's on-disk clone tree under OUTPUT and echo the matched owner
# directory name (gh's canonical casing). Tries OUTPUT/<org> first, then a
# case-insensitive match against OUTPUT's immediate subdirectories. Echoes
# nothing if no tree is found.
resolve_org_owner() {
  local org=$1
  if [ -d "${OUTPUT}/${org}" ]; then
    printf '%s' "${org}"
    return
  fi
  cd "${OUTPUT}" 2>/dev/null || return
  local d
  for d in */; do
    d=${d%/}
    if [ "$(printf '%s' "$d" | tr '[:upper:]' '[:lower:]')" = "$(printf '%s' "$org" | tr '[:upper:]' '[:lower:]')" ]; then
      printf '%s' "$d"; return
    fi
  done
}

# --parse-org mode: run only the whole-org parse over an existing clone tree
# under OUTPUT, without listing, cloning, or deleting anything.
parse_org_only() {
  local org=$1
  local org_dir org_owner
  org_owner=$(resolve_org_owner "${org}")

  if [ -z "${org_owner}" ] || [ ! -d "${OUTPUT}/${org_owner}" ]; then
    log "${org}: no clone tree found under ${OUTPUT} (nothing to parse)"
    return
  fi
  org_dir="${OUTPUT}/${org_owner}"

  rm -rf "${org_dir}/.github"
  log "${org}: parsing whole org tree ${org_dir} (--parse-org)"
  # Same result location as the normal whole-org pass (OUTPUT/<org>/<org>) so a
  # re-parse overwrites the aggregate rather than writing somewhere new.
  if "${PARSER}" "${org_dir}" --output "${OUTPUT#./}/${org_owner}/${org_owner}"; then
    log "${org}: whole-org parse complete"
  else
    log "warning: whole-org parse failed for ${org}"
    TOTAL_FAIL=$(( TOTAL_FAIL + 1 ))
  fi
}

# --parse-only mode: run the full parse flow (per-repo parses + whole-org pass)
# over an existing clone tree under OUTPUT, skipping the download and the delete.
# Enumerates the repos already on disk and feeds them through the same single
# parse consumer used by the normal flow. The tree is left in place.
parse_only() {
  local org=$1
  local org_dir org_owner
  org_owner=$(resolve_org_owner "${org}")

  if [ -z "${org_owner}" ] || [ ! -d "${OUTPUT}/${org_owner}" ]; then
    log "${org}: no clone tree found under ${OUTPUT} (nothing to parse)"
    return
  fi
  org_dir="${OUTPUT}/${org_owner}"

  # Enumerate the repos already on disk (skip the org `.github` repo, which is
  # never parsed per-repo). Each immediate subdirectory is one repo.
  local repos=() d name
  for d in "${org_dir}"/*/; do
    [ -d "${d}" ] || continue
    d=${d%/}
    name=${d##*/}
    [ "${name}" = ".github" ] && continue
    repos+=("${d}")
  done

  local total=${#repos[@]}
  # Total parse units = each repo + the whole-org pass.
  local denom=$(( total + 1 ))
  log "${org}: parse-only over ${total} repos in ${org_dir} (no download, no delete)"

  : > "${PARSE_LOG}"

  # Single parse consumer + queue, same as the normal flow.
  PARSE_FIFO=$(mktemp -u -t clone-parse.XXXXXX)
  mkfifo "${PARSE_FIFO}"
  export PARSE_FIFO
  CURRENT_FIFO="${PARSE_FIFO}"
  parse_consumer &
  CURRENT_CONSUMER=$!

  # Watcher: a single progress bar tracking parsed/total over the parse log.
  (
    while :; do
      sleep 0.2
      pn=0
      [ -f "${PARSE_LOG}" ] && pn=$(wc -l < "${PARSE_LOG}" 2>/dev/null | tr -d ' ')
      [ -z "${pn}" ] && pn=0
      draw_bar "${pn}" "${denom}"
    done
  ) &
  CURRENT_WATCHER=$!

  # Enqueue every repo; the consumer parses them one at a time. Writes block
  # under backpressure when the consumer falls behind, which is fine.
  for d in "${repos[@]}"; do
    printf '%s\n' "${d}" > "${PARSE_FIFO}"
  done

  # End the per-repo queue and wait for the consumer to drain.
  printf '%s\n' "__DONE__" > "${PARSE_FIFO}"
  wait "${CURRENT_CONSUMER}" 2>/dev/null || true
  CURRENT_CONSUMER=""
  rm -f "${PARSE_FIFO}"
  CURRENT_FIFO=""

  # Whole-org pass (the "+1"). Drop the org `.github` repo first so the walk
  # skips it; same result location as the normal flow.
  rm -rf "${org_dir}/.github"
  if ! "${PARSER}" "${org_dir}" --output "${OUTPUT#./}/${org_owner}/${org_owner}" >/dev/null 2>&1; then
    log "warning: whole-org parse failed for ${org}"
    TOTAL_FAIL=$(( TOTAL_FAIL + 1 ))
  fi
  printf 'P\n' >> "${PARSE_LOG}"

  # Stop the watcher and paint the final bar.
  kill "${CURRENT_WATCHER}" 2>/dev/null
  wait "${CURRENT_WATCHER}" 2>/dev/null || true
  CURRENT_WATCHER=""

  local parsed
  parsed=$(wc -l < "${PARSE_LOG}" 2>/dev/null | tr -d ' '); [ -z "${parsed}" ] && parsed=0
  draw_bar "${parsed}" "${denom}"
  printf '\n' >&2
  log "${org}: parsed ${total} repos + whole-org pass (tree kept at ${org_dir})"
}

# Clone one org (bounded by JOBS), parse each repo as it lands, then run a
# whole-org parse and delete the org's clone tree.
process_org() {
  local org=$1

  # --parse-only: full parse flow (per-repo + whole-org) over an already-present
  # clone tree; no download, no delete.
  if [ "${PARSE_ONLY}" = 1 ]; then
    parse_only "${org}"
    return
  fi

  # --parse-org: skip listing + cloning entirely and only run the whole-org
  # parse over an already-present clone tree (e.g. one kept via --no-cleanup).
  # The tree is never deleted here — this run did not create it.
  if [ "${PARSE_ORG}" = 1 ]; then
    parse_org_only "${org}"
    return
  fi

  local list
  list=$(mktemp -t clone-orgs.XXXXXX)

  if ! list_repos "${org}" > "${list}"; then
    log "warning: skipping ${org} (listing failed)"
    rm -f "${list}"
    return
  fi

  local total
  total=$(wc -l < "${list}" | tr -d ' ')
  if [ "${total}" -eq 0 ]; then
    log "${org}: nothing to clone"
    rm -f "${list}"
    return
  fi

  # The org's clone tree is OUTPUT/<owner>, where <owner> is gh's canonical
  # casing taken from the first slug.
  local org_owner org_dir
  org_owner=$(head -n1 "${list}" | cut -d/ -f1)
  org_dir="${OUTPUT}/${org_owner}"

  log "${org}: queued ${total} repos; JOBS=${JOBS}, DEPTH=${DEPTH}, OUTPUT=${OUTPUT}"

  : > "${CLONE_LOG}"
  : > "${PARSE_LOG}"
  : > "${PARSE_QUEUE_LOG}"

  # Start the lone parse consumer and the queue it reads from.
  PARSE_FIFO=$(mktemp -u -t clone-parse.XXXXXX)
  mkfifo "${PARSE_FIFO}"
  export PARSE_FIFO
  CURRENT_FIFO="${PARSE_FIFO}"
  parse_consumer &
  CURRENT_CONSUMER=$!

  # One watcher renders both bars and stays alive until parsing has drained, so
  # the parse bar keeps advancing through the backlog after cloning finishes.
  (
    while :; do
      sleep 0.2
      cn=0; pn=0
      [ -f "${CLONE_LOG}" ] && cn=$(wc -l < "${CLONE_LOG}" 2>/dev/null | tr -d ' ')
      [ -f "${PARSE_LOG}" ] && pn=$(wc -l < "${PARSE_LOG}" 2>/dev/null | tr -d ' ')
      [ -z "${cn}" ] && cn=0; [ -z "${pn}" ] && pn=0
      [ "${cn}" -gt "${total}" ] && cn=${total}
      # Parse total is fixed: every repo (one unit each) plus the whole-org pass.
      draw_bars "${cn}" "${total}" "${pn}" "$(( total + 1 ))"
    done
  ) &
  CURRENT_WATCHER=$!

  # Fan out one slug per worker, bounded by JOBS. The `bash -c '… "$@"' _`
  # pattern is the portable way to invoke an exported function via xargs.
  < "${list}" xargs -n1 -P "${JOBS}" bash -c 'clone_one "$@"' _

  # Cloning done: signal end-of-queue and let the consumer drain the parse
  # backlog. The watcher keeps painting both bars throughout, so the whole-org
  # pass below stays the only live parser.
  printf '%s\n' "__DONE__" > "${PARSE_FIFO}"
  wait "${CURRENT_CONSUMER}" 2>/dev/null || true
  CURRENT_CONSUMER=""
  rm -f "${PARSE_FIFO}"
  CURRENT_FIFO=""

  # Whole-org pass: the final "+1" parse unit folded into the parse bar. Drop
  # the org `.github` repo first so the walk skips it; output is silenced so the
  # bar stays the only thing on screen, and a parse-log tick lifts the bar to
  # 100% once it completes. The watcher keeps painting throughout.
  local org_parsed=0
  if [ -d "${org_dir}" ]; then
    rm -rf "${org_dir}/.github"
    # Put the whole-org aggregate in its own subdir alongside the per-repo
    # results: ./result/<output-root>/<org>/<org> (e.g. result/repos/dedis/dedis),
    # so it doesn't collide with the per-repo dirs at result/repos/dedis/<repo>.
    "${PARSER}" "${org_dir}" --output "${OUTPUT#./}/${org_owner}/${org_owner}" >/dev/null 2>&1 \
      || log "warning: whole-org parse failed for ${org}"
    printf 'P\n' >> "${PARSE_LOG}"
    org_parsed=1
  fi

  # Stop the watcher and paint the final state of both bars.
  kill "${CURRENT_WATCHER}" 2>/dev/null
  wait "${CURRENT_WATCHER}" 2>/dev/null || true
  CURRENT_WATCHER=""

  local ok sk ko parsed queued
  ok=$(grep -c '^OK   ' "${CLONE_LOG}" || true)
  sk=$(grep -c '^SKIP ' "${CLONE_LOG}" || true)
  ko=$(grep -c '^FAIL ' "${CLONE_LOG}" || true)
  parsed=$(wc -l < "${PARSE_LOG}" 2>/dev/null | tr -d ' '); [ -z "${parsed}" ] && parsed=0
  queued=$(wc -l < "${PARSE_QUEUE_LOG}" 2>/dev/null | tr -d ' '); [ -z "${queued}" ] && queued=0
  draw_bars "$(( ok + sk + ko ))" "${total}" "${parsed}" "$(( total + org_parsed ))"
  printf '\n' >&2
  log "${org}: ${ok} cloned, ${sk} skipped, ${ko} failed (of ${total}); parsed ${queued} repos$( [ "${org_parsed}" -eq 1 ] && printf ' + whole-org pass' )"
  if [ "${ko}" -gt 0 ]; then
    grep '^FAIL ' "${CLONE_LOG}" | sed 's/^FAIL  /  /' >&2
    log "${org}: see ${FAIL_LOG} for git's error output on failed clones"
  fi
  TOTAL_FAIL=$(( TOTAL_FAIL + ko ))

  # Org tree no longer needed (kept when --no-cleanup is passed).
  if [ "${NO_CLEANUP}" = 1 ]; then
    log "${org}: keeping clone tree ${org_dir} (--no-cleanup)"
  else
    [ -d "${org_dir}" ] && rm -rf "${org_dir}"
  fi
  rm -f "${list}"
}

for org in "${ORGS[@]}"; do
  process_org "${org}"
done

log ""
log "all orgs done"
[ "${TOTAL_FAIL}" -eq 0 ]
