"""Stage 3e: run the SQL attack corpus through the matchertext SQLite driver.

The driver is an additive matchertext build: each untrusted value is placed
into the SQL text inside a matcher-delimited hole, an M'(...)' literal for a
value or [...] for an identifier, and the statement is prepared through the
ordinary sqlite3_prepare_v2(). Naive concatenation into a quote stays available
and is the breakout control.

This exercises the deployed parser that doc/threat.tex leaves to implementation
work, and it does so on the paper's own in-band holes rather than by
simulation. Arms:

  concat / ident_concat   the value spliced into a quote / an identifier, raw.
                          The control: a real attack breaks out here.
  quote / escape / bind   the legacy value defences (%Q, %q, a bound ?).
  ident_dq                the legacy quoted identifier ("%w").
  mt                      the value hole, %m -> M'(...)'.
  mt_strict               the verify-or-refuse route, M'(%M)'.
  ident_mt                the name hole, [...] over the encoded identifier.

A payload counts against a defence only where the control actually breaks out
(the effective set); a fragment that never parses is not an attack.
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
BIN_STRICT = BUILD / "sqli_driver_strict"

FIELDS = ("outcome", "rc", "skel", "ro", "tail", "nrow", "name", "err")
IDX = {name: i for i, name in enumerate(FIELDS)}
# The value/identifier is the only thing p4 may differ by; every other signal
# must match the benign baseline for the parse to be inert.
STRUCTURAL = ("skel", "ro")

MT_ARMS = ("mt", "mt_strict", "ident_mt")
TYPED_ARMS = ("mt_v", "ident_i")           # the strict-mode ?V / ?I paths
# Arms whose inertness rests on the scanner, so a broken scanner must flip them.
# mt_v is a bound parameter, safe like the additive bind arm, so it is excluded.
SCORE_ARMS = MT_ARMS + ("ident_i",)
LEGACY_ARMS = ("concat", "quote", "escape", "bind", "ident_concat")
CONTROL_ARMS = ("concat", "ident_concat")

PAIR = {")": "(", "]": "[", "}": "{"}
OPEN = {v: k for k, v in PAIR.items()}
ESCAPE_OF = {"(": "o()", ")": "c()", "[": "o[]", "]": "c[]", "{": "o{}", "}": "c{}"}
UNESCAPE = {v: k for k, v in ESCAPE_OF.items()}


def verify(s):
    stack = []
    for ch in s:
        if ch in OPEN:
            stack.append(ch)
        elif ch in PAIR:
            if not stack or stack.pop() != PAIR[ch]:
                return False
    return not stack


def name_decode(s):
    """A [...] identifier is read with the name alphabet: matcher escapes decode,
    every other byte (backslash included) is verbatim."""
    out, i = [], 0
    while i < len(s):
        if s[i] == "\\" and s[i + 1:i + 4] in UNESCAPE:
            out.append(UNESCAPE[s[i + 1:i + 4]])
            i += 4
        else:
            out.append(s[i])
            i += 1
    return "".join(out)


def mt_encode(s):
    """TOMATCHERTEXT: escape the unmatched matchers and double every backslash."""
    esc, stack = [False] * len(s), []
    for i, ch in enumerate(s):
        if ch in OPEN:
            stack.append(i)
        elif ch in PAIR:
            if stack and s[stack[-1]] == PAIR[ch]:
                stack.pop()
            else:
                esc[i] = True
    for i in stack:
        esc[i] = True
    return "".join("\\" + ESCAPE_OF[c] if e else ("\\\\" if c == "\\" else c)
                   for c, e in zip(s, esc))


def ident_name(s):
    """The name a [...] hole yields for a value put through the public encoder:
    matcher escapes round-trip, but the encoder's doubled backslash is not
    undone by the name alphabet, so a backslash comes back doubled."""
    return name_decode(mt_encode(s))


def _compile(binary, extra):
    cc = os.environ.get("CC", "cc")
    cmd = [cc, "-O2", "-o", str(binary), str(SRC), str(AMALG), "-I", str(AMALG.parent),
           "-DSQLITE_ENABLE_MATCHERTEXT", *extra, "-DSQLITE_THREADSAFE=1",
           "-DHAVE_USLEEP=1", "-DSQLITE_OMIT_LOAD_EXTENSION", "-lm", "-lpthread"]
    r = subprocess.run(cmd, capture_output=True, text=True)
    if r.returncode:
        sys.exit(f"cannot build {binary.name}:\n{r.stderr}")


def build_driver():
    """Build both the additive (default) driver and the strict driver, the
    latter to measure that the same delivery paths are refused wholesale."""
    BUILD.mkdir(parents=True, exist_ok=True)
    if not AMALG.exists():
        sys.exit(f"missing {AMALG}\nregenerate with:\n"
                 f"  cd {AMALG.parent} && make sqlite3.h sqlite3.c "
                 f'OPTS="-DSQLITE_ENABLE_MATCHERTEXT"')
    fresh = max(SRC.stat().st_mtime, AMALG.stat().st_mtime)
    if not (BIN.exists() and BIN.stat().st_mtime > fresh):
        print("building sqli_driver ...", flush=True)
        _compile(BIN, [])
    if not (BIN_STRICT.exists() and BIN_STRICT.stat().st_mtime > fresh):
        print("building sqli_driver (strict) ...", flush=True)
        _compile(BIN_STRICT, ["-DSQLITE_MATCHERTEXT_STRICT"])


# Deliberately broken scanners, to prove the oracle can see a defect at all.
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
    def __init__(self, binary=None, selfcheck=True, strict=False):
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
                # The additive build must prepare legacy concatenation and the
                # %m hole; the strict build must refuse both (rc 21). A stale or
                # misconfigured binary invalidates the run.
                concat, mt = (int(x.split("=")[1]) for x in f[1:3])
                want = 21 if strict else 0
                if selfcheck and (concat != want or mt != want):
                    sys.exit(f"driver self-check failed (strict={strict}, "
                             f"concat_rc={concat}, mt_rc={mt}); rebuild it")
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


def verdict(arm, res, base, payload, is_ident):
    """INERT / BREAKOUT / REJECTED / MALFORMED.

    A breakout is a prepared statement whose parse differs from the benign
    baseline in structure, leaves SQL after the first statement (a stacked
    query), or returns more rows than the baseline. REJECTED is %M refusing an
    unbalanced piece. For a name hole an identifier that resolves nowhere is
    inert when the whole payload became that name.
    """
    outcome = res[IDX["outcome"]]
    if outcome == "rejected":
        return "REJECTED"
    if outcome == "parse":
        # A name hole that resolves nowhere is contained when the whole payload
        # became that one name. ident_mt encodes first, so its expected name is
        # the encoder round-trip; ident_concat's is the payload itself.
        if is_ident and res[IDX["name"]] != "-":
            got = unhex(res[IDX["name"]])
            if got in (payload, name_decode(payload), ident_name(payload)):
                return "INERT"
        return "MALFORMED"
    if is_ident:
        # A name that resolves is one identifier by construction; naming a
        # different real column than the baseline compiles differently, so the
        # skeleton is not the oracle. Row count and no tail carry it.
        if res[IDX["tail"]] == "0" and res[IDX["nrow"]] == base[IDX["nrow"]] \
           and res[IDX["ro"]] == base[IDX["ro"]]:
            return "INERT"
        return "BREAKOUT"
    if any(res[IDX[k]] != base[IDX[k]] for k in STRUCTURAL):
        return "BREAKOUT"
    if int(res[IDX["tail"]]) > 0 or int(res[IDX["nrow"]]) > int(base[IDX["nrow"]]):
        return "BREAKOUT"
    return "INERT"


def strict_sweep(con, payloads):
    """Run the corpus through the strict build. There the legacy prepare is
    refused, so the typed API is the only admitted path: a value goes to ?V,
    verified then bound as a parameter, and an identifier to ?I, verified then
    delimited by the host as [ ] read to matcher balance
    (sqlite3_matchertext_prepare_v3). Measure that this path contains every
    payload, exactly as the additive holes do, and that every text-splicing
    path is refused wholesale. Stores per-arm (inert, breakout, rejected,
    refused, malformed)."""
    drv = Driver(BIN_STRICT, strict=True)
    base = drv.baselines()
    ident_hosts = {h for h, (_, hole, _) in drv.hosts.items() if hole == 2}
    tally = defaultdict(lambda: [0, 0, 0, 0, 0])  # inert, breakout, rejected, refused, malformed
    pending, cur = {}, -1

    def score(idx, rows):
        o = payloads[idx][1]
        for (h, a), r in rows.items():
            arm = drv.arms[a][0]
            if arm in TYPED_ARMS:
                v = verdict(arm, r, base[(h, a)], o, h in ident_hosts)
                tally[arm][{"INERT": 0, "BREAKOUT": 1, "REJECTED": 2,
                            "MALFORMED": 4}[v]] += 1
            else:
                tally[arm][3] += 1        # legacy path: refused before the parser

    for i, h, a, r in drv.sweep(p for _, p in payloads):
        if i != cur:
            if cur >= 0:
                score(cur, pending)
            cur, pending = i, {}
        pending[(h, a)] = r
    if cur >= 0:
        score(cur, pending)

    con.executescript("""DROP TABLE IF EXISTS mt_sqli_strict;
        CREATE TABLE mt_sqli_strict(arm TEXT PRIMARY KEY, inert INTEGER,
            breakout INTEGER, rejected INTEGER, refused INTEGER, malformed INTEGER);""")
    con.executemany("INSERT INTO mt_sqli_strict VALUES(?,?,?,?,?,?)",
                    [(a, *c) for a, c in tally.items()])
    con.commit()

    print("\nstrict build: the typed ?V/?I API is the only admitted path")
    for arm in TYPED_ARMS:
        i, b, rej, _, m = tally[arm]
        print(f"  {arm:13} inert={i:>7,}  breakout={b:>7,}  rejected={rej:>7,}"
              f"  malformed={m:>6,}")
    print("  legacy paths, all refused before the parser:")
    for arm in LEGACY_ARMS:
        print(f"  {arm:13} refused={tally[arm][3]:>7,}")

    fail = []
    for arm in TYPED_ARMS:
        if tally[arm][1]:
            fail.append(f"strict {arm} produced {tally[arm][1]:,} breakouts")
    for arm in LEGACY_ARMS:
        admitted = tally[arm][0] + tally[arm][1]
        if admitted:
            fail.append(f"strict build admitted {admitted:,} {arm} cases")
    for f in fail:
        print("  GUARD: " + f)
    return fail


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
CREATE TABLE mt_sqli_host(host_id INTEGER PRIMARY KEY, name TEXT, hole INTEGER,
                          is_echo INTEGER);
CREATE TABLE mt_sqli_arm(arm_id INTEGER PRIMARY KEY, name TEXT, hole INTEGER,
                         conv INTEGER);
CREATE TABLE mt_sqli_value(value_id INTEGER PRIMARY KEY, source TEXT, family TEXT,
                           value TEXT, is_mt INTEGER);
CREATE TABLE mt_sqli_case(value_id INTEGER, host_id INTEGER, arm_id INTEGER,
                          verdict TEXT, outcome TEXT, tail INTEGER, nrow INTEGER,
                          effective INTEGER);
CREATE TABLE mt_sqli_combo(host TEXT, arm TEXT, source TEXT, n INTEGER,
                           n_inert INTEGER, n_breakout INTEGER, n_rejected INTEGER,
                           n_malformed INTEGER, n_eff INTEGER, n_eff_breakout INTEGER);
"""

VMAP = {"INERT": 0, "BREAKOUT": 1, "REJECTED": 2, "MALFORMED": 3}


def run(args):
    build_driver()
    EXPORTS.mkdir(parents=True, exist_ok=True)
    con = sqlite3.connect(DB)

    payloads = load_payloads(con, getattr(args, "limit", None))
    print(f"payloads: {len(payloads):,} distinct, "
          f"{len({family(p) for _, p in payloads}):,} families", flush=True)

    drv = Driver()
    base = drv.baselines()
    control = {drv.arm_id["concat"], drv.arm_id["ident_concat"]}
    ident_hosts = {h for h, (_, hole, _) in drv.hosts.items() if hole == 2}

    con.executescript(DDL)
    con.executemany("INSERT INTO mt_sqli_host VALUES(?,?,?,?)",
                    [(k, *v) for k, v in drv.hosts.items()])
    con.executemany("INSERT INTO mt_sqli_arm VALUES(?,?,?,?)",
                    [(k, *v) for k, v in drv.arms.items()])
    con.executemany("INSERT INTO mt_sqli_value VALUES(?,?,?,?,?)",
                    [(i, s, family(p), p, int(verify(p)))
                     for i, (s, p) in enumerate(payloads)])

    guard = Counter()
    raw, eff_pairs = [], set()   # raw: (idx,h,a,verdict,outcome,tail,nrow)
    pending, cur = {}, -1

    def flush_value(idx, rows):
        o = payloads[idx][1]
        for (h, a), r in rows.items():
            if drv.arms[a][0] in TYPED_ARMS:
                continue          # the strict paths are measured by strict_sweep
            v = verdict(drv.arms[a][0], r, base[(h, a)], o, h in ident_hosts)
            if a in control and v == "BREAKOUT":
                eff_pairs.add((h, o))
            guard[(drv.arms[a][0], v)] += 1
            raw.append((idx, h, a, v, r[IDX["outcome"]],
                        int(r[IDX["tail"]]) if r[IDX["tail"]] != "-" else None,
                        int(r[IDX["nrow"]]) if r[IDX["nrow"]] != "-" else None))

    for i, h, a, r in drv.sweep(p for _, p in payloads):
        if i != cur:
            if cur >= 0:
                flush_value(cur, pending)
            cur, pending = i, {}
        pending[(h, a)] = r
    if cur >= 0:
        flush_value(cur, pending)

    # Combo and case rows need the final effective set, known only after every
    # control has been seen.
    # c = [n, inert, breakout, rejected, malformed, effective, eff_breakout]
    combo = defaultdict(lambda: [0] * 7)
    cases = []
    for idx, h, a, v, outcome, tail, nrow in raw:
        eff = int((h, payloads[idx][1]) in eff_pairs)
        c = combo[(drv.hosts[h][0], drv.arms[a][0], payloads[idx][0])]
        c[0] += 1
        c[1 + VMAP[v]] += 1
        c[5] += eff
        c[6] += int(v == "BREAKOUT" and eff)
        cases.append((idx, h, a, v, outcome, tail, nrow, eff))

    con.executemany("INSERT INTO mt_sqli_case VALUES(?,?,?,?,?,?,?,?)", cases)
    con.executemany("INSERT INTO mt_sqli_combo VALUES(?,?,?,?,?,?,?,?,?,?)",
                    [(host, arm, src, *c) for (host, arm, src), c in sorted(combo.items())])
    con.commit()

    export(con)
    fail = report(guard, eff_pairs, payloads, len(cases))
    fail += strict_sweep(con, payloads)
    if not getattr(args, "skip_sabotage", False):
        fail += run_sabotage(payloads[:args.sabotage_n])
    if fail:
        sys.exit(f"\n{len(fail)} guard failure(s): the run is not usable")
    con.close()


def export(con):
    write("sqli_arm_outcomes.csv",
          ["host", "arm", "source", "n", "inert", "breakout", "rejected",
           "malformed", "effective", "eff_breakout"],
          con.execute("""SELECT host, arm, source, n, n_inert, n_breakout,
                                n_rejected, n_malformed, n_eff, n_eff_breakout
                         FROM mt_sqli_combo ORDER BY arm, host, source"""))
    write("sqli_by_arm.csv",
          ["arm", "n", "inert", "breakout", "rejected", "malformed",
           "effective", "eff_breakout"],
          con.execute("""SELECT arm, SUM(n), SUM(n_inert), SUM(n_breakout),
                                SUM(n_rejected), SUM(n_malformed), SUM(n_eff),
                                SUM(n_eff_breakout)
                         FROM mt_sqli_combo GROUP BY arm ORDER BY arm"""))
    write("sqli_exemplars.csv",
          ["arm", "host", "verdict", "source", "value"],
          con.execute("""SELECT a.name, h.name, c.verdict, v.source, v.value
                         FROM mt_sqli_case c JOIN mt_sqli_value v USING(value_id)
                         JOIN mt_sqli_arm a USING(arm_id)
                         JOIN mt_sqli_host h USING(host_id)
                         WHERE c.verdict IN ('BREAKOUT','MALFORMED')
                         ORDER BY a.name, c.verdict, v.value LIMIT 4000"""))


def verdicts_for(payloads, binary=None, selfcheck=True):
    drv = Driver(binary, selfcheck)
    base = drv.baselines()
    ident_hosts = {h for h, (_, hole, _) in drv.hosts.items() if hole == 2}
    out, pending, cur = {}, {}, -1

    def score(idx, rows):
        o = payloads[idx][1]
        for (h, a), r in rows.items():
            arm = drv.arms[a][0]
            if arm in SCORE_ARMS:
                out[(idx, h, arm)] = verdict(arm, r, base[(h, a)], o, h in ident_hosts)

    for i, h, a, r in drv.sweep(p for _, p in payloads):
        if i != cur:
            if cur >= 0:
                score(cur, pending)
            cur, pending = i, {}
        pending[(h, a)] = r
    if cur >= 0:
        score(cur, pending)
    return out


# Identifiers whose [...] boundary is decided by matcher balance, so the
# FINDEMBEDEND sabotage has something to change. The corpus rarely nests
# brackets, so these are supplied explicitly.
SABOTAGE_PROBES = ["a[b]", "x[]y", "id[z]", "p[q[r]s]", "n[a[m]e]", "a[b]c[d]"]


def run_sabotage(payloads):
    """G1. Break the scanner and require the matchertext arms to change verdict."""
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
        for arm in SCORE_ARMS:
            n, d = per_arm[arm]
            rows.append((name, arm, n, d, round(d / n, 4) if n else 0))
    write("sqli_sabotage.csv",
          ["sabotage", "arm", "cases", "changed_verdict", "detection_rate"], rows)
    print("\nG1 sabotage: a deliberately broken scanner must change the verdict")
    for name, arm, n, d, rate in rows:
        print(f"  {name:28} {arm:10} {d:>7,}/{n:<8,} {rate:>7.1%}")
    for name in SABOTAGE:
        if not any(r[3] for r in rows if r[0] == name):
            fail.append(f"sabotage {name} went unnoticed")
            print(f"  G1: sabotage {name} went unnoticed by every arm")
    return fail


def report(guard, eff_pairs, payloads, ncase):
    print(f"\nsqli sweep: {ncase:,} cases over {len(payloads):,} distinct payloads")
    print(f"effective set: {len({p for _, p in eff_pairs}):,} payloads break out undefended")
    arms = ["concat", "quote", "escape", "bind", "mt", "mt_strict",
            "ident_concat", "ident_mt"]
    print(f"\n{'arm':13} {'inert':>8} {'breakout':>9} {'rejected':>9} {'malformed':>10}")
    for a in arms:
        print(f"{a:13} " + "".join(
            f"{guard[(a, v)]:>{w},}" for v, w in
            (("INERT", 9), ("BREAKOUT", 10), ("REJECTED", 10), ("MALFORMED", 11))))
    fail = []
    if guard[("concat", "BREAKOUT")] == 0:
        fail.append("the control never broke out; the harness is blind")
    for a in MT_ARMS:
        if guard[(a, "BREAKOUT")]:
            fail.append(f"{a} produced {guard[(a, 'BREAKOUT')]:,} breakouts")
    if fail:
        print("\nGUARD FAILURES")
        for f in fail:
            print("  " + f)
    return fail


if __name__ == "__main__":
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--limit", type=int, help="use only the first N payloads")
    ap.add_argument("--skip-sabotage", action="store_true")
    ap.add_argument("--sabotage-n", type=int, default=4000)
    run(ap.parse_args())
