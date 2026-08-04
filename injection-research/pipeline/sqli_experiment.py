"""Stage 3e: run the SQL attack corpus through the matchertext SQLite driver.

The fork now enforces one mode. External SQL enters only through
sqlite3_matchertext_prepare_v3(): a template carries ?V for a bound value and
?I for a delimited identifier, each argument is checked before composition, and
the legacy raw entry points (sqlite3_prepare_v2, sqlite3_exec) are refused with
a migration error. The untrusted value therefore never appears in the SQL text
at all.

That inverts the old question. There is no in-band hole to break out of, so the
measurement is no longer "does the payload escape its delimiter" but "what does
the checked API do with each recorded attack string":

  mtv       the value through ?V, unchanged. Bound as a parameter if it is
            valid matchertext, refused otherwise.
  mtv_enc   the value through ?V after sqlite3_matchertext_encode. The encoder
            is total, so every payload is accepted and bound as data.
  mti       the value through ?I, unchanged. Verified, then delimited by SQLite
            as one identifier, or refused.
  legacy_v  the value concatenated into raw SQL and handed to
  legacy_i   sqlite3_prepare_v2, which the fork refuses whatever the payload is.

The validate.py containment number is a simulation on skeletons; this exercises
the deployed parser, which is what doc/threat.tex leaves to implementation work.
"""
import argparse
import os
import sqlite3
import subprocess
import sys
import threading
from collections import Counter, defaultdict
from pathlib import Path

from export import write

ROOT = Path(__file__).resolve().parents[1]
DB = ROOT / "data" / "cve.db"
EXPORTS = ROOT / "data" / "exports"
BUILD = ROOT / "data" / "build"
AMALG = ROOT / "sqlite" / "sqlite3.c"
SRC = ROOT / "test" / "matchertext" / "csrc" / "sqli_driver.c"
BIN = BUILD / "sqli_driver"

# Driver result fields, in order.
FIELDS = ("outcome", "rc", "skel", "ro", "nrow", "name", "err")
IDX = {name: i for i, name in enumerate(FIELDS)}

MT_ARMS = ("mtv", "mtv_enc", "mti")
LEGACY_ARMS = ("legacy_v", "legacy_i")

PAIR = {")": "(", "]": "[", "}": "{"}
OPEN = {v: k for k, v in PAIR.items()}
ESCAPE_OF = {"(": "o()", ")": "c()", "[": "o[]", "]": "c[]", "{": "o{}", "}": "c{}"}
UNESCAPE = {v: k for k, v in ESCAPE_OF.items()}


def verify(s):
    """VERIFY over raw bytes, the check the ?V and ?I gates apply."""
    stack = []
    for ch in s:
        if ch in OPEN:
            stack.append(ch)
        elif ch in PAIR:
            if not stack or stack.pop() != PAIR[ch]:
                return False
    return not stack


def name_decode(s):
    """A [...] identifier is read with the name alphabet: the six matcher
    escapes decode, every other byte (backslash included) is verbatim. This is
    what a raw ?I argument becomes as a name, and it equals the payload unless
    the payload spells a matcher with an escape."""
    out, i = [], 0
    while i < len(s):
        if s[i] == "\\" and s[i + 1:i + 4] in UNESCAPE:
            out.append(UNESCAPE[s[i + 1:i + 4]])
            i += 4
        else:
            out.append(s[i])
            i += 1
    return "".join(out)


def build_driver():
    BUILD.mkdir(parents=True, exist_ok=True)
    if not AMALG.exists():
        sys.exit(f"missing {AMALG}\nregenerate with:\n"
                 f"  cd {AMALG.parent} && make sqlite3.h sqlite3.c "
                 f'OPTS="-DSQLITE_ENABLE_MATCHERTEXT"')
    if BIN.exists() and BIN.stat().st_mtime > max(SRC.stat().st_mtime,
                                                  AMALG.stat().st_mtime):
        return
    cc = os.environ.get("CC", "cc")
    cmd = [cc, "-O2", "-o", str(BIN), str(SRC), str(AMALG), "-I", str(AMALG.parent),
           "-DSQLITE_ENABLE_MATCHERTEXT", "-DSQLITE_THREADSAFE=1",
           "-DHAVE_USLEEP=1", "-DSQLITE_OMIT_LOAD_EXTENSION", "-lm", "-lpthread"]
    print("building sqli_driver ...", flush=True)
    r = subprocess.run(cmd, capture_output=True, text=True)
    if r.returncode:
        sys.exit(f"cannot build the driver:\n{r.stderr}")


# Deliberate breakages of the scanner, to measure whether the oracle can see a
# failure at all. A suite that reports no breakout against a broken build is
# evidence of blindness, not of containment.
SABOTAGE = {
    "end_scans_to_first_closer": (
        "SQLITE_PRIVATE i64 sqlite3MatchertextEnd(const unsigned char *z){",
        """SQLITE_PRIVATE i64 sqlite3MatchertextEnd(const unsigned char *z){
  i64 i; int k = mtClass(z[0]);
  if( k<=0 ) return 0;
  for(i=1; z[i]; i++){ if( mtClass(z[i])==-k ) return i+1; }
  return 0;
}"""),
    "verify_always_true": (
        "SQLITE_PRIVATE int sqlite3MatchertextVerify(const unsigned char *z, i64 n){",
        """SQLITE_PRIVATE int sqlite3MatchertextVerify(const unsigned char *z, i64 n){
  (void)z; (void)n; return 1;
}"""),
}


def _replace_fn(src, signature, replacement):
    i = src.index(signature)
    j = src.index("\n}\n", i) + 3
    return src[:i] + replacement + "\n" + src[j:]


def build_sabotage(name):
    sig, rep = SABOTAGE[name]
    out = BUILD / f"sqlite3_{name}.c"
    out.write_text(_replace_fn(AMALG.read_text(), sig, rep))
    binary = BUILD / f"sqli_driver_{name}"
    cc = os.environ.get("CC", "cc")
    r = subprocess.run(
        [cc, "-O1", "-o", str(binary), str(SRC), str(out), "-I", str(AMALG.parent),
         "-DSQLITE_ENABLE_MATCHERTEXT", "-DSQLITE_THREADSAFE=1", "-DHAVE_USLEEP=1",
         "-DSQLITE_OMIT_LOAD_EXTENSION", "-lm", "-lpthread"],
        capture_output=True, text=True)
    if r.returncode:
        sys.exit(f"cannot build sabotage {name}:\n{r.stderr[-2000:]}")
    return binary


class Driver:
    """One process for the whole sweep, results streamed back."""

    def __init__(self, binary=None, selfcheck=True):
        self.p = subprocess.Popen([str(binary or BIN)], stdin=subprocess.PIPE,
                                  stdout=subprocess.PIPE, text=True, bufsize=1 << 20)
        self.hosts, self.arms = {}, {}
        self.p.stdin.write("T\n")
        self.p.stdin.flush()
        for line in self.p.stdout:
            f = line.split()
            if f[0] == "end":
                break
            if f[0] == "host":
                self.hosts[int(f[1])] = (f[2], int(f[3]), int(f[4]))
            elif f[0] == "arm":
                self.arms[int(f[1])] = (f[2], int(f[3]), int(f[4]))
            elif f[0] == "selfcheck":
                # The legacy path must be refused and a known unbalanced value
                # must be refused by the checked path, or the build is not the
                # one this stage measures. A stale binary silently invalidated a
                # whole run once; hence the check rather than the assumption.
                legacy, check = (int(x.split("=")[1]) for x in f[1:3])
                if not (legacy != 0 and (check != 0 or not selfcheck)):
                    sys.exit(f"driver self-check failed (legacy_rc={legacy}, "
                             f"check_rc={check}); rebuild it")
        self.arm_id = {v[0]: k for k, v in self.arms.items()}
        self.host_id = {v[0]: k for k, v in self.hosts.items()}

    def baselines(self):
        self.p.stdin.write("B\n")
        self.p.stdin.flush()
        out = {}
        for line in self.p.stdout:
            f = line.split()
            if f[0] == "end":
                break
            out[(int(f[1]), int(f[2]))] = f[3:]
        return out

    def sweep(self, values):
        def feed():
            w = self.p.stdin
            for v in values:
                w.write("S " + v.encode("utf-8", "surrogateescape").hex() + "\nR\n")
            w.close()

        threading.Thread(target=feed, daemon=True).start()
        i = -1
        for line in self.p.stdout:
            f = line.split()
            if f[0] == "ok":
                i += 1
            elif f[0] != "end":
                yield i, int(f[0]), int(f[1]), f[2:]


def unhex(s):
    return bytes.fromhex(s).decode("utf-8", "surrogateescape") if s != "-" else ""


def verdict(arm, res, base, payload):
    """INERT / REJECTED / BREAKOUT / REFUSED / MALFORMED.

    REFUSED is the legacy migration gate; REJECTED is a checked input the API
    declined. Both are safe by design and kept apart because they answer
    different questions: one is "you cannot use the unsafe entry point", the
    other is "this value did not pass verification".
    """
    outcome = res[IDX["outcome"]]
    if arm in LEGACY_ARMS:
        return "BREAKOUT" if outcome == "ok" else "REFUSED"
    if outcome == "rejected":
        return "REJECTED"
    if outcome == "legacy":
        return "REFUSED"
    if arm == "mti":
        # A ?I argument is verified, then composed as one bracketed identifier,
        # then the composed SQL is verified again. So a statement that prepares
        # is inert by construction: it resolved to a single column reference.
        # Naming a different real column than the benign baseline (OID, say)
        # compiles differently and is still one identifier, so the skeleton is
        # not the oracle here -- preparing at all is.
        if outcome == "ok":
            # Prepared, so it resolved to a single column reference. Trust that
            # only with an independent check: a scanner defect that let the
            # identifier close its bracket and append SQL would change the row
            # count or turn the read into a writer, so require both to match the
            # benign baseline before calling it inert.
            if (res[IDX["ro"]] == base[IDX["ro"]]
                    and res[IDX["nrow"]] == base[IDX["nrow"]]):
                return "INERT"
            return "BREAKOUT"
        # It named no column: contained, provided the whole payload became that
        # name (the six matcher escapes decode under the name alphabet). A NUL
        # in the argument is rejected upstream now, so no truncation case here.
        got = unhex(res[IDX["name"]])
        if got and got in (payload, name_decode(payload)):
            return "INERT"
        return "MALFORMED"
    if outcome == "ok":
        # A bound value: the parse must match the benign baseline, which for ?V
        # is always the same composed "?" -- a difference would mean the value
        # reached the SQL text, which is what must never happen.
        return "INERT" if res[IDX["skel"]] == base[IDX["skel"]] else "BREAKOUT"
    return "MALFORMED"


def load_payloads(con, limit=None):
    rows = [("corpus", p) for (p,) in con.execute(
        "SELECT DISTINCT payload FROM payload WHERE syntax_type='sql'")]
    seen = {p for _, p in rows}
    for (p,) in con.execute(
            "SELECT DISTINCT example FROM syntactic_group WHERE syntax_type='sql'"):
        if p not in seen:
            rows.append(("cve", p))
    rows.sort()
    return rows[:limit] if limit else rows


def family(p):
    return "".join("#" if c.isdigit() else c for c in p).lower()


DDL = """
DROP TABLE IF EXISTS mt_sqli_host;
DROP TABLE IF EXISTS mt_sqli_arm;
DROP TABLE IF EXISTS mt_sqli_value;
DROP TABLE IF EXISTS mt_sqli_case;
DROP TABLE IF EXISTS mt_sqli_combo;
DROP TABLE IF EXISTS mt_sqli_baseline;

CREATE TABLE mt_sqli_host(host_id INTEGER PRIMARY KEY, name TEXT, hole INTEGER,
                          is_echo INTEGER);
CREATE TABLE mt_sqli_arm(arm_id INTEGER PRIMARY KEY, name TEXT, hole INTEGER,
                         deliver INTEGER);
CREATE TABLE mt_sqli_value(value_id INTEGER PRIMARY KEY, source TEXT, family TEXT,
                           value TEXT, is_mt INTEGER);
CREATE TABLE mt_sqli_case(value_id INTEGER, host_id INTEGER, arm_id INTEGER,
                          verdict TEXT, outcome TEXT, nrow INTEGER, name TEXT);
CREATE TABLE mt_sqli_combo(host TEXT, arm TEXT, source TEXT, n INTEGER,
                           n_inert INTEGER, n_rejected INTEGER, n_refused INTEGER,
                           n_breakout INTEGER, n_malformed INTEGER);
"""


def run(args):
    build_driver()
    EXPORTS.mkdir(parents=True, exist_ok=True)
    con = sqlite3.connect(DB)

    payloads = load_payloads(con, getattr(args, "limit", None))
    print(f"payloads: {len(payloads):,} distinct, "
          f"{len({family(p) for _, p in payloads}):,} families", flush=True)

    drv = Driver()
    base = drv.baselines()

    con.executescript(DDL)
    con.executemany("INSERT INTO mt_sqli_host VALUES(?,?,?,?)",
                    [(k, *v) for k, v in drv.hosts.items()])
    con.executemany("INSERT INTO mt_sqli_arm VALUES(?,?,?,?)",
                    [(k, *v) for k, v in drv.arms.items()])
    con.executemany("INSERT INTO mt_sqli_value VALUES(?,?,?,?,?)",
                    [(i, s, family(p), p, int(verify(p)))
                     for i, (s, p) in enumerate(payloads)])

    combo = defaultdict(lambda: [0] * 6)
    guard = Counter()
    cases, disagree = [], []
    IDXV = {"INERT": 0, "REJECTED": 1, "REFUSED": 2, "BREAKOUT": 3, "MALFORMED": 4}

    for i, h, a, r in drv.sweep(p for _, p in payloads):
        arm = drv.arms[a][0]
        src, payload = payloads[i]
        v = verdict(arm, r, base[(h, a)], payload)
        # The ?V gate refuses exactly what Python VERIFY rejects; a disagreement
        # is a bug in one side, not a property of the payload.
        if arm == "mtv":
            if (r[IDX["outcome"]] == "rejected") == verify(payload):
                disagree.append(payload)
        guard[(arm, v)] += 1
        c = combo[(drv.hosts[h][0], arm, src)]
        c[0] += 1
        c[1 + IDXV[v]] += 1
        cases.append((i, h, a, v, r[IDX["outcome"]],
                      int(r[IDX["nrow"]]) if r[IDX["nrow"]] != "-" else None,
                      unhex(r[IDX["name"]]) or None))

    con.executemany("INSERT INTO mt_sqli_case VALUES(?,?,?,?,?,?,?)", cases)
    con.executemany("INSERT INTO mt_sqli_combo VALUES(?,?,?,?,?,?,?,?,?)",
                    [(host, arm, src, c[0], c[1], c[2], c[3], c[4], c[5])
                     for (host, arm, src), c in sorted(combo.items())])
    con.commit()

    export(con)
    fail = report(guard, disagree, payloads, len(cases))
    if not getattr(args, "skip_sabotage", False):
        fail += run_sabotage(payloads[:args.sabotage_n])
    if fail:
        sys.exit(f"\n{len(fail)} guard failure(s): the run is not usable")
    con.close()


def export(con):
    write("sqli_arm_outcomes.csv",
          ["host", "arm", "source", "n", "inert", "rejected", "refused",
           "breakout", "malformed"],
          con.execute("""SELECT host, arm, source, n, n_inert, n_rejected,
                                n_refused, n_breakout, n_malformed
                         FROM mt_sqli_combo ORDER BY arm, host, source"""))
    write("sqli_by_arm.csv",
          ["arm", "n", "inert", "rejected", "refused", "breakout", "malformed"],
          con.execute("""SELECT arm, SUM(n), SUM(n_inert), SUM(n_rejected),
                                SUM(n_refused), SUM(n_breakout), SUM(n_malformed)
                         FROM mt_sqli_combo GROUP BY arm ORDER BY arm"""))
    write("sqli_exemplars.csv",
          ["arm", "host", "verdict", "source", "value", "settled_name"],
          con.execute("""SELECT a.name, h.name, c.verdict, v.source, v.value, c.name
                         FROM mt_sqli_case c
                         JOIN mt_sqli_value v USING(value_id)
                         JOIN mt_sqli_arm a USING(arm_id)
                         JOIN mt_sqli_host h USING(host_id)
                         WHERE c.verdict IN ('BREAKOUT','MALFORMED')
                         ORDER BY a.name, c.verdict, v.value LIMIT 4000"""))


def verdicts_for(payloads, binary=None, selfcheck=True):
    drv = Driver(binary, selfcheck)
    base = drv.baselines()
    out = {}
    for i, h, a, r in drv.sweep(p for _, p in payloads):
        arm = drv.arms[a][0]
        if arm in MT_ARMS:
            out[(i, h, arm)] = verdict(arm, r, base[(h, a)], payloads[i][1])
    return out


# Identifiers whose [...] boundary is decided by matcher balance, not by the
# first closer. The corpus almost never nests brackets, so without these the
# FINDEMBEDEND sabotage would exercise nothing and pass unseen. Each is valid
# matchertext, so it clears the ?I gate and reaches the boundary scan.
SABOTAGE_PROBES = ["a[b]", "x[]y", "id[z]", "p[q[r]s]", "n[a[m]e]", "[[]]",
                   "a[b]c[d]", "t1[()]"]


def run_sabotage(payloads):
    """G1. Break the scanner and require the checked path to notice. Detection
    is a changed verdict on the identifier arm, which is the one whose safety
    rides on the scanner; a bound value stays a bound value even when VERIFY is
    disabled, so it cannot witness the break. Scanner-exercising identifiers are
    added so the boundary break has something to act on."""
    payloads = [("probe", p) for p in SABOTAGE_PROBES] + list(payloads)
    honest = verdicts_for(payloads)
    rows, fail = [], []
    for name in SABOTAGE:
        broken = verdicts_for(payloads, build_sabotage(name), selfcheck=False)
        per_arm = defaultdict(lambda: [0, 0])
        for key, v in honest.items():
            c = per_arm[key[2]]
            c[0] += 1
            c[1] += int(broken.get(key, v) != v)
        for arm in MT_ARMS:
            n, d = per_arm[arm]
            rows.append((name, arm, n, d, round(d / n, 4) if n else 0))
    write("sqli_sabotage.csv",
          ["sabotage", "arm", "cases", "changed_verdict", "detection_rate"], rows)
    print("\nG1 sabotage: a deliberately broken scanner must change the verdict")
    for name, arm, n, d, rate in rows:
        print(f"  {name:28} {arm:8} {d:>7,}/{n:<8,} {rate:>7.1%}")
    for name in SABOTAGE:
        if not any(r[3] for r in rows if r[0] == name):
            fail.append(f"sabotage {name} went unnoticed")
            print(f"  G1: sabotage {name} went unnoticed by every arm")
    return fail


def report(guard, disagree, payloads, ncase):
    print(f"\nsqli sweep: {ncase:,} cases over {len(payloads):,} distinct payloads")
    arms = ["mtv", "mtv_enc", "mti", "legacy_v", "legacy_i"]
    print(f"\n{'arm':10} {'inert':>8} {'rejected':>9} {'refused':>8} "
          f"{'breakout':>9} {'malformed':>10}")
    for a in arms:
        print(f"{a:10} " + "".join(
            f"{guard[(a, v)]:>{w},}" for v, w in
            (("INERT", 9), ("REJECTED", 10), ("REFUSED", 9),
             ("BREAKOUT", 10), ("MALFORMED", 11))))
    print(f"\nVERIFY cross-check: C and Python agree on "
          f"{'every value' if not disagree else f'all but {len(disagree)}'}")

    fail = []
    for v in disagree[:5]:
        fail.append(f"VERIFY disagreement on {v!r}")
    for a in MT_ARMS + LEGACY_ARMS:
        if guard[(a, "BREAKOUT")]:
            fail.append(f"{a} produced {guard[(a, 'BREAKOUT')]:,} breakouts")
    # The total encoder must never refuse, and the legacy gate must never let a
    # statement through.
    if guard[("mtv_enc", "REJECTED")]:
        fail.append(f"mtv_enc refused {guard[('mtv_enc', 'REJECTED')]:,} "
                    "values, but the encoder is total")
    for a in LEGACY_ARMS:
        n = sum(guard[(a, v)] for v in IDXV_KEYS)
        if n and guard[(a, "REFUSED")] != n:
            fail.append(f"{a} did not refuse every call")
    if fail:
        print("\nGUARD FAILURES")
        for f in fail:
            print("  " + f)
    return fail


IDXV_KEYS = ("INERT", "REJECTED", "REFUSED", "BREAKOUT", "MALFORMED")


if __name__ == "__main__":
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--limit", type=int, help="use only the first N payloads")
    ap.add_argument("--skip-sabotage", action="store_true")
    ap.add_argument("--sabotage-n", type=int, default=4000)
    run(ap.parse_args())
