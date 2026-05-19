#!/usr/bin/env bash
#
# clone-orgs.sh — clone every (non-archived) repository in one or more GitHub
# orgs in parallel, designed to stay well under GitHub's API and abuse limits.
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
# Requirements: gh (authenticated — run `gh auth login` once), git, jq.
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

die() { printf 'error: %s\n' "$*" >&2; exit 1; }
log() { printf '%s\n' "$*" >&2; }
usage() { die "usage: $0 [-o <output-dir>] <org> [<org> ...]"; }

while [ $# -gt 0 ]; do
  case $1 in
    -o|--output)
      [ $# -ge 2 ] || usage
      OUTPUT=$2; shift 2 ;;
    --output=*)
      OUTPUT=${1#*=}; shift ;;
    -h|--help)
      usage ;;
    --)
      shift; break ;;
    -*)
      die "unknown flag: $1" ;;
    *)
      break ;;
  esac
done

[ $# -ge 1 ] || usage

command -v gh  >/dev/null || die "gh CLI not found (https://cli.github.com/)"
command -v git >/dev/null || die "git not found"
command -v jq  >/dev/null || die "jq not found"
gh auth status >/dev/null 2>&1 || die "not authenticated — run: gh auth login"

remaining=$(gh api rate_limit -q '.resources.core.remaining' 2>/dev/null || echo 0)
log "GitHub REST budget: ${remaining}/5000 remaining"
[ "${remaining}" -ge 100 ] || die "REST budget too low (${remaining}); wait or use a different token"

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

ALL_REPOS=$(mktemp -t clone-orgs.XXXXXX)
trap 'rm -f "${ALL_REPOS}"' EXIT

for org in "$@"; do
  list_repos "${org}" >> "${ALL_REPOS}" || log "warning: skipping ${org} (listing failed)"
done

total=$(wc -l < "${ALL_REPOS}" | tr -d ' ')
log "queued ${total} repos across $# org(s); JOBS=${JOBS}, DEPTH=${DEPTH}, OUTPUT=${OUTPUT}"
[ "${total}" -gt 0 ] || { log "nothing to clone"; exit 0; }

CLONE_LOG="${OUTPUT}/clone.log"
: > "${CLONE_LOG}"

# Per-repo worker: skip-if-exists, clone, retry on failure with quadratic
# backoff. Records its outcome in CLONE_LOG (one short line, append is atomic
# for the lengths used here) and stays silent on stdout so the progress bar
# rendered by the watcher below is the only thing on the user's terminal.
# Always returns 0 so a single bad repo doesn't sink the batch.
clone_one() {
  local slug=$1
  local dest="${OUTPUT}/${slug}"
  local outcome

  if [ -d "${dest}/.git" ]; then
    outcome="SKIP  ${slug}"
  else
    mkdir -p "$(dirname "${dest}")"
    local -a args=(clone --quiet "https://github.com/${slug}.git" "${dest}")
    if [ "${DEPTH}" -gt 0 ]; then
      args+=(--depth="${DEPTH}" --single-branch --no-tags)
    fi

    outcome="FAIL  ${slug}"
    local attempt
    for attempt in $(seq 1 "${RETRIES}"); do
      if git "${args[@]}" 2>/dev/null; then
        outcome="OK    ${slug}"
        break
      fi
      rm -rf "${dest}"
      sleep $(( attempt * attempt * 2 ))
    done
  fi

  printf '%s\n' "${outcome}" >> "${CLONE_LOG}"
}
export -f clone_one
export OUTPUT DEPTH RETRIES CLONE_LOG

# Progress bar matching main.cpp's per-language renderer: 40-wide "[####....]"
# refreshed in place. A background watcher polls the clone log every 200 ms
# and re-renders, so all stdio races stay inside this one process.
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

(
  while :; do
    sleep 0.2
    if [ -f "${CLONE_LOG}" ]; then
      n=$(wc -l < "${CLONE_LOG}" 2>/dev/null | tr -d ' ')
      [ -z "$n" ] && n=0
      [ "$n" -gt "${total}" ] && n=${total}
      draw_bar "$n" "${total}"
    fi
  done
) &
WATCHER_PID=$!
# Make sure the watcher dies even if we exit via an error.
trap 'rm -f "${ALL_REPOS}"; kill "${WATCHER_PID}" 2>/dev/null; wait "${WATCHER_PID}" 2>/dev/null || true' EXIT

# Fan out one slug per worker, bounded by JOBS. The `bash -c '… "$@"' _`
# pattern is the portable way to invoke an exported function via xargs.
< "${ALL_REPOS}" xargs -n1 -P "${JOBS}" bash -c 'clone_one "$@"' _

# Stop the watcher and paint the final state.
kill "${WATCHER_PID}" 2>/dev/null
wait "${WATCHER_PID}" 2>/dev/null || true

ok=$(grep -c '^OK   ' "${CLONE_LOG}" || true)
sk=$(grep -c '^SKIP ' "${CLONE_LOG}" || true)
ko=$(grep -c '^FAIL ' "${CLONE_LOG}" || true)
finished=$(( ok + sk + ko ))
draw_bar "${finished}" "${total}"
printf '\n' >&2

log ""
log "done: ${ok} cloned, ${sk} skipped, ${ko} failed (of ${total})"
if [ "${ko}" -gt 0 ]; then
  log "failed repos (also in ${CLONE_LOG}):"
  grep '^FAIL ' "${CLONE_LOG}" | sed 's/^FAIL  /  /' >&2
fi
[ "${ko}" -eq 0 ]
