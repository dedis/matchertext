"""Stage 3f: run the HTML attack corpus through the matchertext parse5 fork.

Each untrusted value goes into one host context (element text, raw text,
attribute values, a comment) by one arm, and the parsed tree is compared with
the same host and arm holding a benign value. Arms:

  concat                 the value spliced into the host, raw. The control.
  escape                 entities escapeText / escapeAttribute, as parse5 uses.
  dom_stock              the value set in a tree, written by the HTML fragment
                         serialization algorithm and parsed back.
  dom_guarded            the same, written by the default (guarded) serializer.
  mt_verify              VERIFY, then the element hole <tag [v]> or the
                         attribute hole name=[v].
  mt_section             VERIFY, then <![MDATA[v]]> in normal element content.
  mt_encode              balanced runs in MDATA sections and the other
                         matchers as character references; never refuses.
  compose                composeHtml, which verifies and inserts the value.

A payload counts against an arm only where the control breaks out on the same
host (the effective set). Hosts that execute their value by design (href,
onclick) are reported as semantic: containment there does not prevent the
attack. The driver is test/matchertext/jssrc/html_driver.mjs.
"""
import argparse
import hashlib
import json
import os
import shutil
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
FORK = ROOT / "parse5"
DIST = FORK / "packages" / "parse5" / "dist"
DRIVER = ROOT / "test" / "matchertext" / "jssrc" / "html_driver.mjs"
STAMP = BUILD / "parse5.stamp"

VERDICTS = ("INERT", "ALTERED", "BREAKOUT", "REJECTED", "REFUSED", "MALFORMED")
MT_ARMS = ("mt_verify", "mt_section", "mt_encode", "compose")
# The arms that must never let a value change the structure of the host.
CONTAINED_ARMS = MT_ARMS + ("dom_guarded",)
CONTROL_HOSTS = ("text", "attr_dq")
SELFCHECK = {
    "concat_breakout": "BREAKOUT",
    "mt_rejects": "REJECTED",
    "compose_inert": "INERT",
    "dom_stock_breakout": "BREAKOUT",
    "dom_guarded_refuses": "REFUSED",
    "encode_inert": "INERT",
}
MAX_MATCHER_DEPTH = 1000
PAIR = {")": "(", "]": "[", "}": "{"}

# Deliberately broken forks, to prove that the verdicts can see a defect at all.
SABOTAGE = {
    "verify_always_true": (
        "common/matchertext.js",
        "export function verifyMatchertext(value) {",
        "export function verifyMatchertext(value) {\n    return true;",
    ),
    "hole_closes_at_first_closer": (
        "tokenizer/index.js",
        "    _closesHole(cp) {",
        "    _closesHole(cp) {\n        return matcherKind(cp) < 0;",
    ),
    "option_ignored": (
        "tokenizer/index.js",
        "this.options.matchertext",
        "false",
    ),
}
# Values whose verdicts depend on matcher nesting, which the corpus rarely has.
SABOTAGE_PROBES = ["a[b]", "x]><b>y</b>", "(a]", "[a](b){c}", "]]><img src=x onerror=alert(1)>"]


def verify(s):
    """VERIFY with the depth limit of the C and JavaScript implementations."""
    stack = []
    for ch in s:
        if ch in "([{":
            if len(stack) == MAX_MATCHER_DEPTH:
                return False
            stack.append(ch)
        elif ch in PAIR:
            if not stack or stack.pop() != PAIR[ch]:
                return False
    return not stack


def _run(cmd, what, cwd):
    r = subprocess.run(cmd, cwd=cwd, capture_output=True, text=True)
    if r.returncode:
        sys.exit(f"{what} failed (exit {r.returncode}):\n{r.stderr[-4000:]}")
    return r.stdout


def build_fork():
    """Build the parse5 fork when its sources or lockfile changed since the last build."""
    if not (FORK / "package.json").exists():
        sys.exit(f"missing {FORK}: run `git submodule update --init injection-research/parse5`")
    head = _run(["git", "rev-parse", "HEAD"], "git rev-parse", FORK)
    diff = _run(["git", "diff", "HEAD", "--", "packages", "package-lock.json"], "git diff", FORK)
    stamp = hashlib.sha256((head + diff).encode()).hexdigest()
    if DIST.joinpath("index.js").exists() and STAMP.exists() and STAMP.read_text() == stamp:
        return
    BUILD.mkdir(parents=True, exist_ok=True)
    if not (FORK / "node_modules").exists():
        print("installing parse5 dependencies ...", flush=True)
        _run(["npm", "ci", "--no-audit", "--no-fund"], "npm ci", FORK)
    print("building parse5 ...", flush=True)
    _run(["npm", "run", "build"], "npm run build", FORK)
    STAMP.write_text(stamp)


class Driver:
    """One html_driver.mjs process for one dist directory."""

    def __init__(self, dist=DIST, selfcheck=True):
        self.p = subprocess.Popen(["node", str(DRIVER), str(dist / "index.js")], stdin=subprocess.PIPE,
                                  stdout=subprocess.PIPE, text=True, bufsize=1 << 20)
        meta = self._read("its tables")
        self.hosts = meta["hosts"]
        self.arms = meta["arms"]
        check = self._read("its self-check")["selfcheck"]
        if selfcheck and check != SELFCHECK:
            self.fail(f"self-check gave {check}")

    def _read(self, what):
        line = self.p.stdout.readline()
        if not line:
            self.fail(f"stopped before {what}")
        return json.loads(line)

    def fail(self, what):
        self.p.kill()
        sys.exit(f"html_driver.mjs {what} (exit {self.p.wait()})")

    def sweep(self, values):
        """Yield (value index, host, arm, row). Every value must end with its row count and the driver must
        exit cleanly, or the run is unusable."""
        values = list(values)

        def feed():
            w = self.p.stdin
            try:
                for i, v in enumerate(values):
                    w.write(json.dumps({"i": i, "v": v}) + "\n")
                w.close()
            except BrokenPipeError:
                pass              # the driver died; the reader reports it

        threading.Thread(target=feed, daemon=True).start()
        done, rows, end = 0, 0, None
        for line in self.p.stdout:
            o = json.loads(line)
            if "end" in o:
                end = o["end"]
            elif "done" in o:
                if o["i"] != done or o["done"] != rows:
                    self.fail(f"gave {rows} rows for value {o['i']}, which needs {o['done']}")
                done, rows = done + 1, 0
            else:
                rows += 1
                yield o["i"], o["h"], o["a"], o
        rc = self.p.wait()
        if rc or end != len(values) or done != len(values):
            sys.exit(f"html_driver.mjs stopped after {done:,} of {len(values):,} values (exit {rc})")


def load_payloads(con, limit=None):
    rows = [("corpus", p) for (p,) in con.execute(
        "SELECT DISTINCT payload FROM payload WHERE syntax_type='html_dom'")]
    seen = {p for _, p in rows}
    for (p,) in con.execute(
            "SELECT DISTINCT example FROM syntactic_group WHERE syntax_type='html_dom'"):
        if p not in seen:
            rows.append(("cve", p))
            seen.add(p)
    rows.sort()
    return rows[:limit] if limit else rows


DDL = """
DROP TABLE IF EXISTS mt_html_host;
DROP TABLE IF EXISTS mt_html_arm;
DROP TABLE IF EXISTS mt_html_value;
DROP TABLE IF EXISTS mt_html_case;
DROP TABLE IF EXISTS mt_html_combo;
CREATE TABLE mt_html_host(host_id INTEGER PRIMARY KEY, name TEXT, kind TEXT, semantic INTEGER);
CREATE TABLE mt_html_arm(arm_id INTEGER PRIMARY KEY, name TEXT, matchertext INTEGER);
CREATE TABLE mt_html_value(value_id INTEGER PRIMARY KEY, source TEXT, value TEXT, is_mt INTEGER);
CREATE TABLE mt_html_case(value_id INTEGER, host_id INTEGER, arm_id INTEGER, verdict TEXT,
                          reader TEXT, trip TEXT, effective INTEGER);
CREATE TABLE mt_html_combo(host TEXT, arm TEXT, source TEXT, n INTEGER, n_inert INTEGER,
                           n_altered INTEGER, n_breakout INTEGER, n_rejected INTEGER,
                           n_refused INTEGER, n_malformed INTEGER, n_eff INTEGER,
                           n_eff_breakout INTEGER);
"""
RESULT_TABLES = ("mt_html_host", "mt_html_arm", "mt_html_value", "mt_html_case", "mt_html_combo")
OUTPUTS = ("html_arm_outcomes.csv", "html_by_arm.csv", "html_reader.csv", "html_exemplars.csv",
           "html_sabotage.csv")


def run(args):
    build_fork()
    EXPORTS.mkdir(parents=True, exist_ok=True)
    con = sqlite3.connect(DB)

    payloads = load_payloads(con, getattr(args, "limit", None))
    print(f"html payloads: {len(payloads):,} distinct", flush=True)

    drv = Driver()
    hosts = [h["name"] for h in drv.hosts]
    arms = [a["name"] for a in drv.arms]
    raw = []
    for i, h, a, o in drv.sweep(p for _, p in payloads):
        raw.append((i, h, a, o["verdict"], o["reader"], o["trip"]))

    # The effective set is known only after every control has been seen.
    concat = arms.index("concat")
    effective = {(i, h) for i, h, a, v, _, _ in raw if a == concat and v == "BREAKOUT"}

    combo = defaultdict(lambda: [0] * 9)   # n, the six verdicts, effective, effective breakout
    cases, guard = [], Counter()
    for i, h, a, v, reader, trip in raw:
        eff = int((i, h) in effective)
        c = combo[(hosts[h], arms[a], payloads[i][0])]
        c[0] += 1
        c[1 + VERDICTS.index(v)] += 1
        c[7] += eff
        c[8] += int(v == "BREAKOUT" and eff)
        cases.append((i, h, a, v, reader, trip, eff))
        guard[(arms[a], v)] += 1
        guard[(arms[a], "trip", trip)] += 1
        if arms[a] == "mt_verify" and (v == "REJECTED") == verify(payloads[i][1]):
            guard["verify_mismatch"] += 1

    fail = guards(guard, raw, hosts, concat)
    sabotage = []
    if not fail and not getattr(args, "skip_sabotage", False):
        sabotage, sabotage_fail = run_sabotage(payloads[:getattr(args, "sabotage_n", 2000)])
        fail += sabotage_fail
    if fail:
        # Leave no result that a later stage could read as valid.
        con.executescript("".join(f"DROP TABLE IF EXISTS {t};" for t in RESULT_TABLES))
        con.commit()
        for name in OUTPUTS:
            (EXPORTS / name).unlink(missing_ok=True)
        sys.exit(f"\n{len(fail)} guard failure(s): the run is not usable")

    con.executescript(DDL)
    con.executemany("INSERT INTO mt_html_host VALUES(?,?,?,?)",
                    [(k, x["name"], x["kind"], int(x["semantic"])) for k, x in enumerate(drv.hosts)])
    con.executemany("INSERT INTO mt_html_arm VALUES(?,?,?)",
                    [(k, x["name"], int(x["matchertext"])) for k, x in enumerate(drv.arms)])
    con.executemany("INSERT INTO mt_html_value VALUES(?,?,?,?)",
                    [(i, s, p, int(verify(p))) for i, (s, p) in enumerate(payloads)])
    con.executemany("INSERT INTO mt_html_case VALUES(?,?,?,?,?,?,?)", cases)
    con.executemany("INSERT INTO mt_html_combo VALUES(?,?,?,?,?,?,?,?,?,?,?,?)",
                    [(host, arm, src, *c) for (host, arm, src), c in sorted(combo.items())])
    con.commit()
    export(con)
    if sabotage:
        write("html_sabotage.csv", ["sabotage", "cases", "changed_verdict", "detection"], sabotage)
    report(guard, arms, len(payloads), len(effective))
    con.close()


def guards(guard, raw, hosts, concat):
    fail = []
    for host in CONTROL_HOSTS:
        h = hosts.index(host)
        if not any(a == concat and v == "BREAKOUT" and hh == h for _, hh, a, v, _, _ in raw):
            fail.append(f"the {host} control never broke out; the harness is blind")
    for arm in CONTAINED_ARMS:
        if guard[(arm, "BREAKOUT")]:
            fail.append(f"{arm} produced {guard[(arm, 'BREAKOUT')]:,} breakouts")
    for arm in MT_ARMS:
        if guard[(arm, "MALFORMED")]:
            fail.append(f"{arm} wrote {guard[(arm, 'MALFORMED')]:,} documents the parser refused")
        if guard[(arm, "trip", "DIFFERENT")]:
            fail.append(f"{arm}: {guard[(arm, 'trip', 'DIFFERENT')]:,} trees serialize to other trees")
    if guard[("mt_encode", "REJECTED")]:
        fail.append("mt_encode refused a value, but encoding is total")
    if guard["verify_mismatch"]:
        fail.append(f"{guard['verify_mismatch']:,} mt_verify refusals disagree with VERIFY")
    for f in fail:
        print("  GUARD: " + f)
    return fail


def export(con):
    write("html_arm_outcomes.csv",
          ["host", "arm", "source", "n", "inert", "altered", "breakout", "rejected", "refused",
           "malformed", "effective", "eff_breakout"],
          con.execute("""SELECT host, arm, source, n, n_inert, n_altered, n_breakout, n_rejected,
                                n_refused, n_malformed, n_eff, n_eff_breakout
                         FROM mt_html_combo ORDER BY arm, host, source"""))
    write("html_by_arm.csv",
          ["arm", "semantic", "n", "inert", "altered", "breakout", "rejected", "refused", "malformed",
           "effective", "eff_breakout"],
          con.execute("""SELECT c.arm, h.semantic, SUM(n), SUM(n_inert), SUM(n_altered), SUM(n_breakout),
                                SUM(n_rejected), SUM(n_refused), SUM(n_malformed), SUM(n_eff),
                                SUM(n_eff_breakout)
                         FROM mt_html_combo c JOIN mt_html_host h ON h.name = c.host
                         GROUP BY c.arm, h.semantic ORDER BY c.arm, h.semantic"""))
    write("html_reader.csv",
          ["host", "arm", "same", "attributes", "elements"],
          con.execute("""SELECT h.name, a.name, SUM(reader = 'SAME'), SUM(reader = 'ATTRIBUTES'),
                                SUM(reader = 'ELEMENTS')
                         FROM mt_html_case c JOIN mt_html_host h USING(host_id)
                         JOIN mt_html_arm a USING(arm_id)
                         WHERE a.matchertext = 1 AND reader != '-'
                         GROUP BY h.name, a.name ORDER BY a.name, h.name"""))
    write("html_exemplars.csv",
          ["arm", "host", "verdict", "source", "value"],
          con.execute("""SELECT a.name, h.name, c.verdict, v.source, v.value
                         FROM mt_html_case c JOIN mt_html_value v USING(value_id)
                         JOIN mt_html_arm a USING(arm_id) JOIN mt_html_host h USING(host_id)
                         WHERE c.verdict IN ('BREAKOUT', 'MALFORMED') AND c.effective = 1
                         ORDER BY a.name, h.name, v.value LIMIT 4000"""))


def build_sabotage(name):
    """A copy of the built fork with one defect, resolving its dependencies through the fork."""
    path, signature, replacement = SABOTAGE[name]
    target = BUILD / f"parse5-sabotage-{name}"
    shutil.rmtree(target, ignore_errors=True)
    shutil.copytree(DIST, target / "dist")
    # The package file declares the module type, and node_modules resolves the fork's dependencies.
    shutil.copy(DIST.parent / "package.json", target / "package.json")
    os.symlink(FORK / "node_modules", target / "node_modules")
    source = target / "dist" / path
    text = source.read_text()
    if signature not in text:
        sys.exit(f"sabotage {name}: `{signature}` is not in {path}")
    source.write_text(text.replace(signature, replacement))
    return target / "dist"


def verdicts(payloads, dist=DIST, selfcheck=True):
    drv = Driver(dist, selfcheck)
    return {(i, h, a): o["verdict"] for i, h, a, o in drv.sweep(payloads)}


def run_sabotage(payloads):
    """Break the fork and require the verdicts to change."""
    values = SABOTAGE_PROBES + [p for _, p in payloads]
    honest = verdicts(values)
    rows, fail = [], []
    for name in SABOTAGE:
        dist = build_sabotage(name)
        # A sabotaged fork may fail the driver's baseline, which is a detection too.
        r = subprocess.run(["node", str(DRIVER), str(dist / "index.js")], input="",
                           capture_output=True, text=True)
        if r.returncode:
            rows.append((name, 0, 0, "baseline refused"))
            continue
        broken = verdicts(values, dist, selfcheck=False)
        changed = sum(broken[k] != v for k, v in honest.items())
        rows.append((name, len(honest), changed, "verdicts changed" if changed else "unnoticed"))
        if not changed:
            fail.append(f"sabotage {name} went unnoticed")
    print("\nsabotage: a broken fork must change the verdicts")
    for name, n, changed, detection in rows:
        print(f"  {name:28} {changed:>7,}/{n:<8,} {detection}")
    return rows, fail


def report(guard, arms, n_payloads, n_effective):
    print(f"\nhtml sweep: {n_payloads:,} payloads, {n_effective:,} effective (value, host) pairs")
    print(f"{'arm':12}" + "".join(f"{v.lower():>10}" for v in VERDICTS))
    for arm in arms:
        print(f"{arm:12}" + "".join(f"{guard[(arm, v)]:>10,}" for v in VERDICTS))


if __name__ == "__main__":
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--limit", type=int, help="use only the first N payloads")
    ap.add_argument("--skip-sabotage", action="store_true")
    ap.add_argument("--sabotage-n", type=int, default=2000)
    run(ap.parse_args())
