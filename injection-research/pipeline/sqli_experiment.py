"""Stage 3e: run the SQL attack corpus through the matchertext SQLite driver.

The driver is an additive matchertext build: each untrusted value is placed
into the SQL text inside a matcher-delimited hole, an M'(...)' literal for a
value or [...] for an identifier, and the statement is prepared through the
ordinary sqlite3_prepare_v2(). Naive concatenation into quoted and raw value
contexts stays available as the breakout control.

This exercises the deployed parser that doc/threat.tex leaves to implementation
work, and it does so on the paper's own in-band holes rather than by
simulation. Arms:

  concat / ident_concat   the value spliced into host SQL / an identifier, raw.
                          The control: a real attack breaks out here.
  quote / escape / bind   the legacy value defences (%Q, %q, a bound ?).
  mt                      the value hole, %m -> M'(...)'.
  mt_strict               the verify-or-refuse route, VERIFY then %m.
  ident_mt                the name hole, [...] over the encoded identifier.

A payload counts against a defence only where the control actually breaks out
(the effective set); a fragment that never parses is not an attack.
"""
import argparse
import os
import re
import sqlite3
import subprocess
import sys
import threading
from collections import Counter, defaultdict
from pathlib import Path
from urllib.parse import unquote_to_bytes

from export import write

ROOT = Path(__file__).resolve().parents[1]
DB = ROOT / "data" / "cve.db"
EXPORTS = ROOT / "data" / "exports"
BUILD = ROOT / "data" / "build"
SQLITE_TREE = ROOT / "sqlite"
AMALG = BUILD / "sqlite" / "sqlite3.c"
# Lemon reads this flag too, so the amalgamation is generated with it.
AMALG_OPTIONS = "-DSQLITE_ENABLE_MATCHERTEXT"
SRC = ROOT / "test" / "matchertext" / "csrc" / "sqli_driver.c"
BIN = BUILD / "sqli_driver"
BIN_STRICT = BUILD / "sqli_driver_strict"

FIELDS = ("outcome", "rc", "skel", "ro", "tail", "nrow", "name", "err", "fid")
IDX = {name: i for i, name in enumerate(FIELDS)}
# The value/identifier is the only thing p4 may differ by; every other signal
# must match the benign baseline for the parse to be inert.
STRUCTURAL = ("skel", "ro")

MT_ARMS = ("mt", "mt_strict", "ident_mt")
# Value holes whose value must come back unchanged on the echo fixture.
FAITHFUL_ARMS = ("mt", "mt_strict", "mt_v")
TYPED_ARMS = ("mt_v", "ident_i")           # the strict-mode ?V / ?I paths
# Arms whose inertness rests on the scanner, so a broken scanner must flip them.
# mt_v is a bound parameter, safe like the additive bind arm, so it is excluded.
SCORE_ARMS = MT_ARMS + TYPED_ARMS
LEGACY_ARMS = ("concat", "quote", "escape", "bind", "ident_concat", "ident_quote")
# The columns of the fixture table t1, its rowid, and a name that matches none:
# the names an identifier hole may hold without adding SQL. A double-quoted
# name that matches no column is a string literal to SQLite.
FIXTURE_NAMES = ("name", "id", "rowid", "no_such_column")
CONTROL_ARMS = ("concat", "ident_concat")
RAW_HOSTS = {"numeric_eq", "numeric_paren", "value_in", "expr_order", "expr_limit"}
SQLITE_ATOM = re.compile(
    r"(?:[+-]?(?:(?:\d+(?:\.\d*)?|\.\d+)(?:e[+-]?\d+)?|0x[0-9a-f]+)"
    r"|null|true|false|'(?:''|[^'])*'|x'[0-9a-f]*')\Z", re.I | re.S)
SQLITE_VOLATILE = re.compile(r"\b(?:random|randomblob)\s*\(", re.I)

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
    """A [...] identifier is read with the escape alphabet: matcher escapes
    decode, every other byte (backslash included) is verbatim."""
    out, i = [], 0
    while i < len(s):
        if s[i] == "\\" and s[i + 1:i + 4] in UNESCAPE:
            out.append(UNESCAPE[s[i + 1:i + 4]])
            i += 4
        else:
            out.append(s[i])
            i += 1
    return "".join(out)


def _run(cmd, what, cwd=None):
    r = subprocess.run(cmd, cwd=cwd, capture_output=True, text=True)
    if r.returncode:
        sys.exit(f"cannot build {what}:\n{r.stdout[-2000:]}{r.stderr[-2000:]}")


def build_amalgamation():
    """Generate the matchertext amalgamation from the SQLite tree, out of tree.
    make regenerates it only after a source change."""
    AMALG.parent.mkdir(parents=True, exist_ok=True)
    if not (AMALG.parent / "Makefile").exists():
        _run([str(SQLITE_TREE / "configure")], "the SQLite build tree", AMALG.parent)
    _run(["make", f"OPTIONS={AMALG_OPTIONS}", "sqlite3.c", "sqlite3.h"],
         "the matchertext amalgamation", AMALG.parent)


def _compile(binary, extra):
    """Build one driver, unless it is newer than its sources and was built
    with the same command."""
    cc = os.environ.get("CC", "cc")
    cmd = [cc, "-O2", "-o", str(binary), str(SRC), str(AMALG), "-I", str(AMALG.parent),
           "-DSQLITE_ENABLE_MATCHERTEXT", *extra, "-DSQLITE_THREADSAFE=1",
           "-DHAVE_USLEEP=1", "-DSQLITE_OMIT_LOAD_EXTENSION", "-lm", "-lpthread"]
    stamp = binary.with_suffix(".cmd")
    fresh = max(SRC.stat().st_mtime, AMALG.stat().st_mtime)
    if (binary.exists() and binary.stat().st_mtime > fresh and stamp.exists()
            and stamp.read_text() == "\0".join(cmd)):
        return
    print(f"building {binary.name} ...", flush=True)
    _run(cmd, binary.name)
    stamp.write_text("\0".join(cmd))


# A2: each matchertext entry point that takes SQL must compile a literal and
# refuse a run-time string.
A2_PROBES = {
    "prepare_v3": "sqlite3_matchertext_prepare_v3(db, {t}, -1, 0, 0, 0, pp)",
    "sql_append": "sqlite3_matchertext_sql_append(sqlite3_matchertext_sql_new(), {t})",
}


def check_constant_sql():
    cc = os.environ.get("CC", "cc")
    probe = BUILD / "a2_probe.c"
    for name, call in A2_PROBES.items():
        for template, compiles in (('"SELECT 1"', True), ("z", False)):
            probe.write_text('#include "sqlite3.h"\n'
                             "void f(sqlite3 *db, const char *z, sqlite3_stmt **pp){\n"
                             f"  (void)z; (void)pp; (void)db; {call.format(t=template)};\n}}\n")
            r = subprocess.run([cc, "-fsyntax-only", "-DSQLITE_ENABLE_MATCHERTEXT",
                                "-I", str(AMALG.parent), str(probe)],
                               capture_output=True, text=True)
            if (r.returncode == 0) != compiles:
                sys.exit(f"A2 guard: {name} with {template} "
                         f"{'does not compile' if compiles else 'compiles'}:\n{r.stderr[-2000:]}")


def build_driver():
    """Build both the additive (default) driver and the strict driver, the
    latter to measure that the same delivery paths are refused wholesale."""
    build_amalgamation()
    check_constant_sql()
    _compile(BIN, [])
    _compile(BIN_STRICT, ["-DSQLITE_MATCHERTEXT_STRICT"])


# Deliberately broken scanners, to prove the oracle can see a defect at all.
SABOTAGE = {
    "end_scans_to_first_closer": (
        "SQLITE_PRIVATE i64 sqlite3MatchertextEnd(const unsigned char *z){",
        """SQLITE_PRIVATE i64 sqlite3MatchertextEnd(const unsigned char *z){
  i64 i; int k = mtClass(z[0]);
  if( k<=0 ) return 0;
  for(i=1; z[i]; i++){ if( mtClass(z[i])==-k ) return i+1; }
  return -1;
}"""),
    "verify_always_true": (
        "SQLITE_PRIVATE int sqlite3MatchertextVerify(const unsigned char *z, i64 n){",
        """SQLITE_PRIVATE int sqlite3MatchertextVerify(const unsigned char *z, i64 n){
  (void)z; (void)n; return 1;
}"""),
    "encode_identity": (
        "SQLITE_PRIVATE char *sqlite3MatchertextEncode(const char *z, i64 n, i64 *pnOut){",
        """SQLITE_PRIVATE char *sqlite3MatchertextEncode(const char *z, i64 n, i64 *pnOut){
  char *zOut = sqlite3_malloc64(n+1);
  if( zOut==0 ) return 0;
  memcpy(zOut, z, (size_t)n);
  zOut[n] = 0;
  if( pnOut ) *pnOut = n;
  return zOut;
}"""),
    "decode_identity": (
        "SQLITE_PRIVATE i64 sqlite3MatchertextDecodeInPlace(char *z, i64 n){",
        """SQLITE_PRIVATE i64 sqlite3MatchertextDecodeInPlace(char *z, i64 n){
  (void)z; return n;
}"""),
}
# Sabotages that let a value out of its hole: the run fails unless they turn
# some contained case into a breakout. For the encoder only the probes below
# can: no recorded payload has an unmatched closer followed by SQL.
BREAKOUT_SABOTAGE = ("end_scans_to_first_closer", "encode_identity")


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
    _run([cc, "-O1", "-o", str(binary), str(SRC), str(out), "-I", str(AMALG.parent),
          "-DSQLITE_ENABLE_MATCHERTEXT", "-DSQLITE_THREADSAFE=1", "-DHAVE_USLEEP=1",
          "-DSQLITE_OMIT_LOAD_EXTENSION", "-lm", "-lpthread"], f"sabotage {name}")
    return binary


class Driver:
    def __init__(self, binary=None, selfcheck=True, strict=False):
        self.binary = binary or BIN
        self.p = subprocess.Popen([str(self.binary)], stdin=subprocess.PIPE,
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
                # %m hole; the strict build must refuse both (rc 21). Both must
                # prepare the typed ?V path. A stale or misconfigured binary
                # invalidates the run.
                concat, mt, typed = (int(x.split("=")[1]) for x in f[1:4])
                want = 21 if strict else 0
                if selfcheck and (concat != want or mt != want or typed != 0):
                    sys.exit(f"driver self-check failed (strict={strict}, "
                             f"concat_rc={concat}, mt_rc={mt}, typed_rc={typed}); "
                             f"rebuild it")
        else:
            self.fail("stopped before its tables")
        self.arm_id = {v[0]: k for k, v in self.arms.items()}
        self.host_id = {v[0]: k for k, v in self.hosts.items()}

    def fail(self, what):
        self.p.kill()
        sys.exit(f"{self.binary.name} {what} (exit {self.p.wait()})")

    def baselines(self):
        self.p.stdin.write("B\n")
        self.p.stdin.flush()
        out = {}
        for line in self.p.stdout:
            f = line.split()
            if f[0] == "end":
                break
            out[(int(f[1]), int(f[2]))] = f[3:]
        else:
            self.fail("stopped before its baselines")
        return out

    def name_skeletons(self, ident_hosts):
        """For each identifier pair, the skeletons of the statement that names a
        real column of the fixture."""
        out = defaultdict(set)
        for v in FIXTURE_NAMES:
            self.p.stdin.write(f"S {v.encode().hex()}\nR\n")
            self.p.stdin.flush()
            for line in self.p.stdout:
                f = line.split()
                if f[0] == "end":
                    break
                if f[0] != "ok" and int(f[0]) in ident_hosts \
                        and f[2 + IDX["outcome"]] == "ok":
                    out[(int(f[0]), int(f[1]))].add(f[2 + IDX["skel"]])
            else:
                self.fail("stopped in the name skeletons")
        return out

    def sweep(self, values):
        """Yield (value index, host, arm, fields). Every value must end with its
        "end" line and the driver must exit cleanly, or the run is unusable."""
        values = list(values)

        def feed():
            w = self.p.stdin
            try:
                for v in values:
                    w.write("S " + v.encode("utf-8", "surrogateescape").hex() + "\nR\n")
                w.close()
            except BrokenPipeError:
                pass              # the driver died; the reader reports it

        threading.Thread(target=feed, daemon=True).start()
        i, ended = -1, 0
        for line in self.p.stdout:
            f = line.split()
            if f[0] == "ok":
                i += 1
            elif f[0] == "end":
                ended += 1
            else:
                yield i, int(f[0]), int(f[1]), f[2:]
        rc = self.p.wait()
        if rc or ended != len(values):
            sys.exit(f"{self.binary.name} stopped after {ended:,} of "
                     f"{len(values):,} values (exit {rc})")


def unhex(s):
    return bytes.fromhex(s).decode("utf-8", "surrogateescape") if s != "-" else ""


def unfaithful(drv, h, a, res):
    """A matchertext value hole on the echo fixture returned other bytes than
    the arm handed to SQLite."""
    return (drv.hosts[h][2] and drv.arms[a][0] in FAITHFUL_ARMS
            and res[IDX["outcome"]] == "ok" and res[IDX["fid"]] != "1")


def refused(res):
    """The strict build refused this legacy prepare: SQLITE_MISUSE with the
    matchertext migration message, before any parse."""
    return (res[IDX["outcome"]] == "parse" and res[IDX["rc"]] == "21"
            and unhex(res[IDX["err"]]).startswith("matchertext requires"))


def verdict(arm, res, base, payload, is_ident, raw=False, names=frozenset()):
    """INERT / BREAKOUT / REJECTED / MALFORMED.

    A breakout is a prepared statement whose parse differs from the benign
    baseline in structure, leaves SQL after the first statement (a stacked
    query), or returns more rows than the baseline. REJECTED is VERIFY
    refusing an unbalanced piece. For a name hole an identifier that resolves
    nowhere is inert when the whole payload became that name, and one that
    resolves is inert when the statement compiles as it does for a real column
    of the fixture (names, from Driver.name_skeletons).
    """
    outcome = res[IDX["outcome"]]
    if outcome == "rejected":
        return "REJECTED"
    if outcome == "parse":
        # A name hole that resolves nowhere is contained when the whole payload
        # became that one name. The encoder and ?I give back the payload
        # exactly; raw concatenation is read with the escape alphabet.
        if is_ident and res[IDX["name"]] != "-":
            got = unhex(res[IDX["name"]])
            if got == payload or (arm == "ident_concat" and got == name_decode(payload)):
                return "INERT"
        return "MALFORMED"
    if is_ident:
        # Naming another real column compiles differently from the baseline,
        # so the skeleton is compared with that of every real column: SQL the
        # value added, such as a subquery, compiles to none of them.
        if res[IDX["tail"]] == "0" and res[IDX["skel"]] in names:
            return "INERT"
        return "BREAKOUT"
    if raw and arm == "concat" and SQLITE_ATOM.fullmatch(payload.strip()) \
            and res[IDX["tail"]] == "0":
        return "INERT"
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
    names = drv.name_skeletons(ident_hosts)
    tally = defaultdict(lambda: [0, 0, 0, 0, 0])  # inert, breakout, rejected, refused, malformed
    lost = Counter()
    pending, cur = {}, -1

    def score(idx, rows):
        o = payloads[idx][1]
        expected = verify(o)
        for (h, a), r in rows.items():
            arm = drv.arms[a][0]
            if arm in TYPED_ARMS:
                accepted = r[IDX["outcome"]] != "rejected"
                assert accepted == expected, (
                    f"VERIFY mismatch for payload {idx} ({arm}/{drv.hosts[h][0]}): "
                    f"python={expected} c={accepted} value={o[:160]!r}")
                v = verdict(arm, r, base[(h, a)], o, h in ident_hosts,
                            drv.hosts[h][0] in RAW_HOSTS, names[(h, a)])
                tally[arm][{"INERT": 0, "BREAKOUT": 1, "REJECTED": 2,
                            "MALFORMED": 4}[v]] += 1
                lost[arm] += unfaithful(drv, h, a, r)
            elif refused(r):
                tally[arm][3] += 1        # legacy path: refused before the parser
            elif r[IDX["outcome"]] == "rejected":
                tally[arm][2] += 1        # VERIFY refused the value before any prepare
            else:
                tally[arm][0] += 1        # admitted: the guard below fails the run

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
    print("  text-splicing paths, all refused before the parser:")
    for arm in LEGACY_ARMS + MT_ARMS:
        print(f"  {arm:13} refused={tally[arm][3]:>7,}")

    fail = []
    for arm in TYPED_ARMS:
        if tally[arm][1]:
            fail.append(f"strict {arm} produced {tally[arm][1]:,} breakouts")
        if lost[arm]:
            fail.append(f"strict {arm} changed {lost[arm]:,} values")
    for arm in LEGACY_ARMS + MT_ARMS:
        if tally[arm][0]:
            fail.append(f"strict build admitted {tally[arm][0]:,} {arm} cases")
    for f in fail:
        print("  GUARD: " + f)
    return fail


def benign_rejection(con):
    """Strict ?V refuses a value that is not matchertext. Measure how often
    that refuses benign text: the descriptions of the CVEs outside the corpus,
    prose that no one wrote as an attack."""
    texts = rejected = 0
    for (desc,) in con.execute(
            """SELECT description FROM cve WHERE state='PUBLISHED'
               AND description IS NOT NULL
               AND cve_id NOT IN (SELECT cve_id FROM classification)"""):
        texts += 1
        rejected += not verify(desc)
    print(f"\nbenign text: strict ?V refuses {rejected:,} of {texts:,} "
          f"CVE descriptions ({rejected / max(texts, 1):.2%})")
    write("sqli_benign_rejection.csv", ["texts", "rejected"], [(texts, rejected)])


def _sqlite_literals(payload):
    """Map exact PostgreSQL/SQL Server text literals to SQLite text literals."""
    out = []
    quote = None
    i = 0
    while i < len(payload):
        c = payload[i]
        if quote:
            out.append(c)
            if c == "\\" and quote != "`" and i + 1 < len(payload):
                i += 1
                out.append(payload[i])
            elif c == quote:
                if i + 1 < len(payload) and payload[i + 1] == quote:
                    i += 1
                    out.append(payload[i])
                else:
                    quote = None
        elif c in "'\"`":
            quote = c
            out.append(c)
        elif c in "Nn" and i + 1 < len(payload) and payload[i + 1] == "'" \
                and (i == 0 or not (payload[i - 1].isalnum() or payload[i - 1] in "_$")):
            quote = "'"
            out.append("'")
            i += 1
        elif c == "$":
            j = payload.find("$", i + 1)
            tag = payload[i + 1:j] if j >= 0 else "-"
            valid = not tag or (tag[0].isalpha() or tag[0] == "_") \
                and all(x.isalnum() or x == "_" for x in tag[1:])
            end = payload.find("$" + tag + "$", j + 1) if valid else -1
            if end >= 0:
                out.append("'" + payload[j + 1:end].replace("'", "''") + "'")
                i = end + len(tag) + 1
            else:
                out.append(c)
        else:
            out.append(c)
        i += 1
    return "".join(out)


def sqlite_equivalent(payload):
    """Apply only transport, literal, and comment rewrites with equal meaning."""
    try:
        decoded = unquote_to_bytes(payload).decode("utf-8")
        if "\0" not in decoded:
            payload = decoded
    except UnicodeError:
        pass

    payload = _sqlite_literals(payload)

    quote = None
    i = 0
    while i < len(payload):
        c = payload[i]
        if quote:
            if c == "\\" and quote != "`":
                i += 2
                continue
            if c == quote:
                if i + 1 < len(payload) and payload[i + 1] == quote:
                    i += 2
                    continue
                quote = None
        elif c in "'\"`":
            quote = c
        elif c == "#" and (i + 1 == len(payload) or payload[i + 1].isspace()):
            return payload[:i] + "-- " + payload[i + 1:]
        i += 1
    return payload


def load_payloads(con, limit=None):
    rows = [("corpus", p) for (p,) in con.execute(
        "SELECT DISTINCT payload FROM payload WHERE syntax_type='sql'")]
    seen = {p for _, p in rows}
    for (p,) in con.execute(
            "SELECT DISTINCT example FROM syntactic_group WHERE syntax_type='sql'"):
        if p not in seen:
            rows.append(("cve", p))
            seen.add(p)
    for source, payload in list(rows):
        adapted = sqlite_equivalent(payload)
        if adapted != payload and adapted not in seen:
            rows.append((source + "_sqlite", adapted))
            seen.add(adapted)
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
RESULT_TABLES = ("mt_sqli_host", "mt_sqli_arm", "mt_sqli_value", "mt_sqli_case",
                 "mt_sqli_combo", "mt_sqli_strict")
OUTPUTS = ("sqli_arm_outcomes.csv", "sqli_by_arm.csv", "sqli_exemplars.csv",
           "sqli_benign_rejection.csv",
           "sqli_sabotage.csv")


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
    names = drv.name_skeletons(ident_hosts)

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
            v = verdict(drv.arms[a][0], r, base[(h, a)], o, h in ident_hosts,
                        drv.hosts[h][0] in RAW_HOSTS, names[(h, a)])
            if a in control and v == "BREAKOUT":
                eff_pairs.add((h, o))
            guard[(drv.arms[a][0], v)] += 1
            guard[(drv.arms[a][0], "UNFAITHFUL")] += unfaithful(drv, h, a, r)
            nrow = int(r[IDX["nrow"]]) if r[IDX["nrow"]] != "-" else None
            if SQLITE_VOLATILE.search(o):
                nrow = None
            raw.append((idx, h, a, v, r[IDX["outcome"]],
                        int(r[IDX["tail"]]) if r[IDX["tail"]] != "-" else None,
                        nrow))

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

    fail = report(guard, eff_pairs, payloads, len(cases))
    fail += strict_sweep(con, payloads)
    benign_rejection(con)
    sabotage = []
    if not getattr(args, "skip_sabotage", False):
        sabotage, sabotage_fail = run_sabotage(payloads[:getattr(args, "sabotage_n", None)])
        fail += sabotage_fail
    if fail:
        # Leave no result that a later stage could read as valid.
        con.executescript("".join(f"DROP TABLE IF EXISTS {t};" for t in RESULT_TABLES))
        for name in OUTPUTS:
            (EXPORTS / name).unlink(missing_ok=True)
        sys.exit(f"\n{len(fail)} guard failure(s): the run is not usable")
    export(con)
    if sabotage:
        write("sqli_sabotage.csv",
              ["sabotage", "arm", "cases", "detected", "to_breakout", "to_breakout_probe",
               "detection_rate"],
              sabotage)
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
    names = drv.name_skeletons(ident_hosts)
    out, pending, cur = {}, {}, -1

    def score(idx, rows):
        o = payloads[idx][1]
        for (h, a), r in rows.items():
            arm = drv.arms[a][0]
            if arm in SCORE_ARMS:
                out[(idx, h, arm)] = (
                    verdict(arm, r, base[(h, a)], o, h in ident_hosts,
                            drv.hosts[h][0] in RAW_HOSTS, names[(h, a)]),
                    unfaithful(drv, h, a, r))

    for i, h, a, r in drv.sweep(p for _, p in payloads):
        if i != cur:
            if cur >= 0:
                score(cur, pending)
            cur, pending = i, {}
        pending[(h, a)] = r
    if cur >= 0:
        score(cur, pending)
    return out


# Values the corpus lacks. Identifiers whose [...] boundary is decided by
# matcher balance, so the FINDEMBEDEND sabotage has something to change, and an
# unmatched closer then SQL, which a broken encoder lets out of its hole.
SABOTAGE_PROBES = ["a[b]", "x[]y", "id[z]", "p[q[r]s]", "n[a[m]e]", "a[b]c[d]",
                   ")' OR '1'='1' --", "name], (SELECT 1) --"]


def run_sabotage(payloads):
    """G1. Break the locator, VERIFY, the encoder, or the decoder, and require
    the matchertext arms to notice: a changed verdict or a changed value. A
    sabotage that lets a value out of its hole must also produce breakouts."""
    payloads = [("probe", p) for p in SABOTAGE_PROBES] + list(payloads)
    honest = verdicts_for(payloads)
    rows, fail = [], []
    for name in SABOTAGE:
        broken = verdicts_for(payloads, build_sabotage(name), selfcheck=False)
        # cases, detected, to breakout from recorded payloads, from probes
        per_arm = defaultdict(lambda: [0, 0, 0, 0])
        for key, (v, lost) in honest.items():
            bv, blost = broken.get(key, (v, lost))
            c = per_arm[key[2]]
            c[0] += 1
            c[1] += int(bv != v or blost != lost)
            c[3 if key[0] < len(SABOTAGE_PROBES) else 2] += int(
                bv == "BREAKOUT" and v != "BREAKOUT")
        for arm in SCORE_ARMS:
            n, d, b, bp = per_arm[arm]
            rows.append((name, arm, n, d, b, bp, round(d / n, 4) if n else 0))
    print("\nG1 sabotage: a broken matchertext core must change a verdict or a value")
    print(f"  {'sabotage':28} {'arm':10} {'detected':>16} {'rate':>7}"
          f" {'to breakout':>12} {'of probes':>10}")
    for name, arm, n, d, b, bp, rate in rows:
        print(f"  {name:28} {arm:10} {d:>7,}/{n:<8,} {rate:>7.1%} {b:>12,} {bp:>10,}")
    for name in SABOTAGE:
        if not any(r[3] for r in rows if r[0] == name):
            fail.append(f"sabotage {name} went unnoticed")
        if name in BREAKOUT_SABOTAGE and not any(r[4] + r[5] for r in rows if r[0] == name):
            fail.append(f"sabotage {name} produced no breakout")
    for f in fail:
        print("  G1: " + f)
    return rows, fail


def report(guard, eff_pairs, payloads, ncase):
    print(f"\nsqli sweep: {ncase:,} cases over {len(payloads):,} distinct payloads")
    print(f"effective set: {len({p for _, p in eff_pairs}):,} payloads break out undefended")
    arms = ["concat", "quote", "escape", "bind", "mt", "mt_strict",
            "ident_concat", "ident_quote", "ident_mt"]
    print(f"\n{'arm':13} {'inert':>8} {'breakout':>9} {'rejected':>9} {'malformed':>10}")
    for a in arms:
        print(f"{a:13} " + "".join(
            f"{guard[(a, v)]:>{w},}" for v, w in
            (("INERT", 9), ("BREAKOUT", 10), ("REJECTED", 10), ("MALFORMED", 11))))
    fail = []
    for a in CONTROL_ARMS:
        if guard[(a, "BREAKOUT")] == 0:
            fail.append(f"the {a} control never broke out; the harness is blind")
    for a in MT_ARMS:
        if guard[(a, "BREAKOUT")]:
            fail.append(f"{a} produced {guard[(a, 'BREAKOUT')]:,} breakouts")
        if guard[(a, "UNFAITHFUL")]:
            fail.append(f"{a} changed {guard[(a, 'UNFAITHFUL')]:,} values")
    if fail:
        print("\nGUARD FAILURES")
        for f in fail:
            print("  " + f)
    return fail


if __name__ == "__main__":
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--limit", type=int, help="use only the first N payloads")
    ap.add_argument("--skip-sabotage", action="store_true")
    ap.add_argument("--sabotage-n", type=int, help="sabotage only the first N payloads")
    run(ap.parse_args())
