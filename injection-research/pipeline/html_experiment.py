r"""Stage 3f: run the HTML attack corpus through the matchertext parse5 fork.

Each untrusted value goes into one host context (element text, raw text,
attribute values, a comment) by one arm, and the parsed tree is compared with
the same host and arm holding a benign value. Arms:

  concat                 the value spliced into the host, raw. The control.
  escape                 one escaper everywhere: entities escapeText /
                         escapeAttribute, as parse5 uses, in every host.
  escape_context         the OWASP rule of each context: entities in element
                         content, &#xHH; in attributes, \xHH in the JavaScript
                         string, \HHHHHH in CSS.
  autoescape             Go's html/template, which escapes for the context
                         (test/matchertext/gosrc/autoescape).
  dom_stock              the value set in a tree, written by the HTML fragment
                         serialization algorithm and parsed back.
  dom_guarded            the same, written by the default (guarded) serializer.
  dom_mt                 the same, written by the serializer with matchertext
                         output, and parsed again with the option.
  mt_verify              VERIFY, then the element hole <tag [v]> or the
                         attribute hole name=[v].
  mt_section             VERIFY, then <![MDATA[v]]> in normal element content.
  mt_encode              balanced runs in MDATA sections and the other
                         matchers as character references; never refuses.
  compose                composeHtml, which verifies and inserts the value.
  compose_js             the value encoded as a JavaScript string body, then
                         composeHtml: one delimiting per layer, inside out.
  compose_dom            composeHtml in the page, whose guarded output an
                         innerHTML assignment parses as a fragment (the client
                         path of DOM-based XSS).

The script host also gets a JavaScript verdict: whether the string literal
that holds the value ends where the host ends it. Chromium, Firefox, and
WebKit (Playwright builds, pipeline/browsers.py) parse every document that a
browser receives: each stock arm's document, the guarded output of each
matchertext tree, and the fragment of the client path. The mXSS fixtures of
DOMPurify (test/matchertext/jssrc/mxss_probes.json) run apart as values, to
show that the writers that keep a value as text keep them as text through the
round trip (L2).

The hole-targeted set (adaptive.py) runs apart, with two more arms that send
the mt_verify document as UTF-8 bytes (A3 at the byte level): mt_utf8 decodes
them with the WHATWG UTF-8 decoder, mt_sjis with Shift_JIS, a charset the
writer did not declare.

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

import adaptive
import browsers
from export import write
from matchertext import verify

ROOT = Path(__file__).resolve().parents[1]
DB = ROOT / "data" / "cve.db"
EXPORTS = ROOT / "data" / "exports"
BUILD = ROOT / "data" / "build"
FORK = ROOT / "parse5"
DIST = FORK / "packages" / "parse5" / "dist"
DRIVER = ROOT / "test" / "matchertext" / "jssrc" / "html_driver.mjs"
STAMP = BUILD / "parse5.stamp"
AUTOESCAPE = BUILD / "autoescape"
PROBES = ROOT / "test" / "matchertext" / "jssrc" / "mxss_probes.json"

VERDICTS = ("INERT", "ALTERED", "BREAKOUT", "REJECTED", "REFUSED", "MALFORMED")
ENGINES = browsers.ENGINES
MT_ARMS = ("dom_mt", "mt_verify", "mt_section", "mt_encode", "compose", "compose_js", "compose_dom", "mt_utf8")
# The matchertext arms whose tree the default serializer writes back for a browser (trip).
TRIP_ARMS = ("dom_mt", "mt_verify", "mt_section", "mt_encode", "compose", "compose_js", "mt_utf8")
# The arms that must never let a value change the structure of the host, in parse5 or in a browser.
CONTAINED_ARMS = MT_ARMS + ("dom_guarded",)
CONTROL_HOSTS = ("text", "attr_dq")
SELFCHECK = {
    "concat_breakout": "BREAKOUT",
    "mt_rejects": "REJECTED",
    "compose_inert": "INERT",
    "dom_stock_breakout": "BREAKOUT",
    "dom_guarded_refuses": "REFUSED",
    "dom_mt_inert": "INERT",
    "dom_mt_refuses": "REFUSED",
    "encode_inert": "INERT",
    "js_breakout": "BREAKOUT",
    "js_contained": "INERT",
    "js_escaped": "INERT",
}

# Deliberately broken forks, to prove that the verdicts can see a defect at all: per sabotage, the file and its
# (signature, replacement) edits.
SABOTAGE = {
    "verify_always_true": ("common/matchertext.js", [(
        "export function verifyMatchertext(value) {",
        "export function verifyMatchertext(value) {\n    return true;",
    )]),
    "hole_closes_at_first_closer": ("tokenizer/index.js", [(
        "    _closesHole(cp) {",
        "    _closesHole(cp) {\n        return matcherKind(cp) < 0;",
    )]),
    "option_ignored": ("tokenizer/index.js", [("this.options.matchertext", "false")]),
    "guard_off": ("serializer/index.js", [(
        "function rawTextEndsEarly(tn, text) {",
        "function rawTextEndsEarly(tn, text) {\n    return false;",
    )]),
}
# Sabotages that let a value out of its hole or raw text element: the run fails
# unless they turn some contained case into a breakout.
BREAKOUT_SABOTAGE = ("hole_closes_at_first_closer", "guard_off")
# Values whose verdicts depend on matcher nesting, which the corpus rarely has.
SABOTAGE_PROBES = ["a[b]", "x]><b>y</b>", "(a]", "[a](b){c}", "]]><img src=x onerror=alert(1)>"]


def _run(cmd, what, cwd):
    r = subprocess.run(cmd, cwd=cwd, capture_output=True, text=True)
    if r.returncode:
        sys.exit(f"{what} failed (exit {r.returncode}):\n{r.stderr[-4000:]}")
    return r.stdout


def build_autoescape():
    """Build the html/template writer of the auto-escaping arm; Go rebuilds only what changed."""
    BUILD.mkdir(parents=True, exist_ok=True)
    _run(["go", "build", "-o", str(AUTOESCAPE), "./test/matchertext/gosrc/autoescape"], "go build", ROOT)


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

    def __init__(self, dist=DIST, selfcheck=True, args=()):
        self.p = subprocess.Popen(["node", str(DRIVER), str(dist / "index.js"), str(AUTOESCAPE), *args],
                                  stdin=subprocess.PIPE, stdout=subprocess.PIPE, text=True, bufsize=1 << 20,
                                  env=browsers.env())
        meta = self._read("its tables")
        self.hosts = meta["hosts"]
        self.arms = meta["arms"]
        self.browsers = meta["browsers"]
        self.page_check = None
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
            elif "page_check" in o:
                self.page_check = o["page_check"]
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
        # The browsers read most documents by an inert write; a sample read by a page load must give the same
        # trees, or the browser verdicts do not stand for a page.
        check = self.page_check
        if check and check["differ"]:
            sys.exit(f"a page load reads {check['differ']} of {check['n']:,} sampled documents otherwise than the "
                     f"inert write, e.g. {check['examples'][0]}")


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
                          reader TEXT, trip TEXT, effective INTEGER, js TEXT,
                          chromium TEXT, firefox TEXT, webkit TEXT);
CREATE TABLE mt_html_combo(host TEXT, arm TEXT, source TEXT, n INTEGER, n_inert INTEGER,
                           n_altered INTEGER, n_breakout INTEGER, n_rejected INTEGER,
                           n_refused INTEGER, n_malformed INTEGER, n_eff INTEGER,
                           n_eff_breakout INTEGER);
"""
RESULT_TABLES = ("mt_html_host", "mt_html_arm", "mt_html_value", "mt_html_case", "mt_html_combo")
OUTPUTS = ("html_arm_outcomes.csv", "html_by_arm.csv", "html_reader.csv", "html_exemplars.csv",
           "html_adaptive.csv", "html_sabotage.csv", "html_browser.csv", "html_js.csv", "html_mxss.csv",
           "html_browsers.csv")


def tally(guard, arm, o, value):
    """Count one row of the driver into `guard`."""
    guard[(arm, o["verdict"])] += 1
    guard[(arm, "trip", o["trip"])] += 1
    guard[(arm, "js", o["js"])] += 1
    for e, b in zip(ENGINES, o["browsers"] if o["browsers"] != "-" else ()):
        guard[(arm, e, b)] += 1
    if arm == "mt_verify" and (o["verdict"] == "REJECTED") == verify(value):
        guard["verify_mismatch"] += 1


def run(args):
    build_fork()
    build_autoescape()
    versions = browsers.install()
    EXPORTS.mkdir(parents=True, exist_ok=True)
    con = sqlite3.connect(DB)

    payloads = load_payloads(con, getattr(args, "limit", None))
    print(f"html payloads: {len(payloads):,} distinct", flush=True)

    drv = Driver(args=("--browsers",))
    hosts = [h["name"] for h in drv.hosts]
    arms = [a["name"] for a in drv.arms]
    raw, guard = [], Counter()
    for i, h, a, o in drv.sweep(p for _, p in payloads):
        b = tuple(o["browsers"]) if o["browsers"] != "-" else ("-",) * len(ENGINES)
        raw.append((i, h, a, o["verdict"], o["reader"], o["trip"], o["js"], b))
        tally(guard, arms[a], o, payloads[i][1])

    print(f"page-load check: {drv.page_check['n']:,} sampled documents, the same trees as the inert write",
          flush=True)

    # The effective set is known only after every control has been seen.
    concat = arms.index("concat")
    effective = {(i, h) for i, h, a, v, *_ in raw if a == concat and v == "BREAKOUT"}

    combo = defaultdict(lambda: [0] * 9)   # n, the six verdicts, effective, effective breakout
    cases = []
    for i, h, a, v, reader, trip, js, b in raw:
        eff = int((i, h) in effective)
        c = combo[(hosts[h], arms[a], payloads[i][0])]
        c[0] += 1
        c[1 + VERDICTS.index(v)] += 1
        c[7] += eff
        c[8] += int(v == "BREAKOUT" and eff)
        cases.append((i, h, a, v, reader, trip, eff, js, *b))

    fail = guards(guard, raw, hosts, arms)
    targeted, targeted_fail = run_adaptive({payloads[i][1] for i, _ in effective},
                                           {p for _, p in payloads})
    fail += targeted_fail
    mxss, mxss_fail = run_probes()
    fail += mxss_fail
    sabotage = []
    if not fail and not getattr(args, "skip_sabotage", False):
        sabotage, sabotage_fail = run_sabotage(payloads[:getattr(args, "sabotage_n", None)])
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
    con.executemany("INSERT INTO mt_html_case VALUES(?,?,?,?,?,?,?,?,?,?,?)", cases)
    con.executemany("INSERT INTO mt_html_combo VALUES(?,?,?,?,?,?,?,?,?,?,?,?)",
                    [(host, arm, src, *c) for (host, arm, src), c in sorted(combo.items())])
    con.commit()
    export(con)
    write("html_adaptive.csv",
          ["arm", "values", "n", "inert", "altered", "breakout", "rejected", "refused", "malformed",
           "effective", "eff_breakout"], targeted)
    if sabotage:
        write("html_sabotage.csv",
              ["sabotage", "cases", "changed_verdict", "to_breakout", "detection"], sabotage)
    write("html_mxss.csv", ["arm", "probes", "n", *(v.lower() for v in VERDICTS), *ENGINES], mxss)
    write("html_browsers.csv", ["engine", "version", "page_checked"],
          [(e, v, drv.page_check["n"]) for e, v in sorted(versions.items())])
    report(guard, arms, len(payloads), len(effective))
    con.close()


def guards(guard, raw, hosts, arms, controls=True):
    """The run fails when a control cannot see a breakout or a contained arm admits one."""
    concat = arms.index("concat")
    fail = []
    for host in CONTROL_HOSTS if controls else ():
        h = hosts.index(host)
        if not any(a == concat and v == "BREAKOUT" and hh == h for _, hh, a, v, *_ in raw):
            fail.append(f"the {host} control never broke out; the harness is blind")
    browsed = any(b != "-" for *_, bs in raw for b in bs)
    for e in ENGINES if browsed and controls else ():
        if not guard[("concat", e, "BREAKOUT")]:
            fail.append(f"the control never broke out in {e}; the browser replay is blind")
    for arm in CONTAINED_ARMS:
        if guard[(arm, "BREAKOUT")]:
            fail.append(f"{arm} produced {guard[(arm, 'BREAKOUT')]:,} breakouts")
        for e in ENGINES:
            if guard[(arm, e, "BREAKOUT")]:
                fail.append(f"{arm} produced {guard[(arm, e, 'BREAKOUT')]:,} breakouts in {e}")
    for arm in MT_ARMS:
        if guard[(arm, "MALFORMED")]:
            fail.append(f"{arm} wrote {guard[(arm, 'MALFORMED')]:,} documents the parser refused")
    for arm in TRIP_ARMS:
        if guard[(arm, "trip", "DIFFERENT")]:
            fail.append(f"{arm}: {guard[(arm, 'trip', 'DIFFERENT')]:,} trees serialize to other trees")
    # The JavaScript layer: the control must end the string, and the value delimited for it must not.
    if controls and not guard[("concat", "js", "BREAKOUT")]:
        fail.append("the control never broke out of the JavaScript string; the JavaScript verdict is blind")
    bad = sum(guard[("compose_js", "js", v)] for v in ("ALTERED", "BREAKOUT", "MALFORMED"))
    if bad:
        fail.append(f"compose_js: {bad:,} JavaScript strings do not hold the value")
    if guard[("mt_encode", "REJECTED")]:
        fail.append("mt_encode refused a value, but encoding is total")
    if guard["verify_mismatch"]:
        fail.append(f"{guard['verify_mismatch']:,} mt_verify refusals disagree with VERIFY")
    for f in fail:
        print("  GUARD: " + f)
    return fail


def sweep(values, args):
    """Every legal (host, arm) of `values` in one driver: the rows, the tallies, and the host and arm names."""
    drv = Driver(args=args)
    hosts = [h["name"] for h in drv.hosts]
    arms = [a["name"] for a in drv.arms]
    raw, guard = [], Counter()
    for i, h, a, o in drv.sweep(values):
        b = tuple(o["browsers"]) if o["browsers"] != "-" else ("-",) * len(ENGINES)
        raw.append((i, h, a, o["verdict"], o["reader"], o["trip"], o["js"], b))
        tally(guard, arms[a], o, values[i])
    return raw, guard, hosts, arms


def run_adaptive(seeds, known):
    """The hole-targeted set through every arm, the two byte arms, and the browsers, under the guards of the
    corpus. The Shift_JIS reader must break out: otherwise the byte arms cannot see a charset the writer did not
    declare."""
    values = adaptive.values("html", seeds, known, 2000, 500)
    raw, guard, hosts, arms = sweep(values, ("--bytes", "--browsers"))
    concat = arms.index("concat")
    effective = {(i, h) for i, h, a, v, *_ in raw if a == concat and v == "BREAKOUT"}
    per_arm = defaultdict(lambda: [0] * 9)   # n, the six verdicts, effective, effective breakout
    for i, h, a, v, *_ in raw:
        c = per_arm[arms[a]]
        c[0] += 1
        c[1 + VERDICTS.index(v)] += 1
        c[7] += (i, h) in effective
        c[8] += (i, h) in effective and v == "BREAKOUT"
    print("\nhole-targeted set (adaptive.py), with the byte arms:")
    report(guard, arms, len(values), len(effective))
    fail = guards(guard, raw, hosts, arms)
    if not guard[("mt_sjis", "BREAKOUT")]:
        fail.append("the Shift_JIS reader never broke out; the byte arms are blind")
        print("  GUARD: " + fail[-1])
    return [(arm, len(values), *per_arm[arm]) for arm in arms], fail


def run_probes():
    """The mXSS fixtures of DOMPurify as values, through every arm, in parse5 and the browsers. The stock writer must
    let some of them out on the round trip, or the run cannot see mutation XSS; the contained arms must not."""
    values = [p["payload"] for p in json.loads(PROBES.read_text())["probes"]]
    raw, guard, hosts, arms = sweep(values, ("--browsers",))
    rows = [(arm, len(values), sum(guard[(arm, v)] for v in VERDICTS), *(guard[(arm, v)] for v in VERDICTS),
             *(guard[(arm, e, "BREAKOUT")] for e in ENGINES)) for arm in arms]
    print("\nmXSS fixtures of DOMPurify, as values:")
    report(guard, arms, len(values))
    fail = guards(guard, raw, hosts, arms, controls=False)
    if not guard[("dom_stock", "BREAKOUT")]:
        fail.append("the stock writer let no mXSS fixture out; the round trip is blind")
        print("  GUARD: " + fail[-1])
    return rows, fail

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
    write("html_browser.csv",
          ["arm", "semantic", "engine", "n", *(v.lower() for v in VERDICTS)],
          [(arm, sem, e, *row) for e in ENGINES for arm, sem, *row in con.execute(
              f"""SELECT a.name, h.semantic, COUNT(*),
                         {", ".join(f"SUM(c.{e} = '{v}')" for v in VERDICTS)}
                  FROM mt_html_case c JOIN mt_html_arm a USING(arm_id) JOIN mt_html_host h USING(host_id)
                  WHERE c.{e} != '-' GROUP BY a.name, h.semantic ORDER BY a.name, h.semantic""")])
    write("html_js.csv",
          ["arm", "n", "inert", "altered", "breakout", "malformed"],
          con.execute("""SELECT a.name, COUNT(*), SUM(js = 'INERT'), SUM(js = 'ALTERED'), SUM(js = 'BREAKOUT'),
                                SUM(js = 'MALFORMED')
                         FROM mt_html_case c JOIN mt_html_arm a USING(arm_id)
                         WHERE js != '-' GROUP BY a.name ORDER BY a.name"""))
    write("html_exemplars.csv",
          ["arm", "host", "verdict", "source", "value"],
          con.execute("""SELECT a.name, h.name, c.verdict, v.source, v.value
                         FROM mt_html_case c JOIN mt_html_value v USING(value_id)
                         JOIN mt_html_arm a USING(arm_id) JOIN mt_html_host h USING(host_id)
                         WHERE c.verdict IN ('BREAKOUT', 'MALFORMED') AND c.effective = 1
                         ORDER BY a.name, h.name, v.value LIMIT 4000"""))


def build_sabotage(name):
    """A copy of the built fork with one defect, resolving its dependencies through the fork."""
    path, edits = SABOTAGE[name]
    target = BUILD / f"parse5-sabotage-{name}"
    shutil.rmtree(target, ignore_errors=True)
    shutil.copytree(DIST, target / "dist")
    # The package file declares the module type, and node_modules resolves the fork's dependencies.
    shutil.copy(DIST.parent / "package.json", target / "package.json")
    os.symlink(FORK / "node_modules", target / "node_modules")
    source = target / "dist" / path
    text = source.read_text()
    for signature, replacement in edits:
        if signature not in text:
            sys.exit(f"sabotage {name}: `{signature}` is not in {path}")
        text = text.replace(signature, replacement)
    source.write_text(text)
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
        r = subprocess.run(["node", str(DRIVER), str(dist / "index.js"), str(AUTOESCAPE)], input="",
                           capture_output=True, text=True)
        if r.returncode:
            rows.append((name, 0, 0, 0, "baseline refused"))
            continue
        broken = verdicts(values, dist, selfcheck=False)
        changed = sum(broken[k] != v for k, v in honest.items())
        escaped = sum(broken[k] == "BREAKOUT" != v for k, v in honest.items())
        rows.append((name, len(honest), changed, escaped,
                     "verdicts changed" if changed else "unnoticed"))
        if not changed:
            fail.append(f"sabotage {name} went unnoticed")
        if name in BREAKOUT_SABOTAGE and not escaped:
            fail.append(f"sabotage {name} produced no breakout")
    print("\nsabotage: a broken fork must change the verdicts")
    for name, n, changed, escaped, detection in rows:
        print(f"  {name:28} {changed:>7,}/{n:<8,} {escaped:>7,} to breakout  {detection}")
    return rows, fail


def report(guard, arms, n_payloads, n_effective=None):
    effective = "" if n_effective is None else f", {n_effective:,} effective (value, host) pairs"
    print(f"\nhtml sweep: {n_payloads:,} payloads{effective}")
    print(f"{'arm':15}" + "".join(f"{v.lower():>10}" for v in VERDICTS)
          + "".join(f"{e[:6] + ' brk':>12}" for e in ENGINES) + f"{'js brk':>9}{'js malf':>9}")
    for arm in arms:
        print(f"{arm:15}" + "".join(f"{guard[(arm, v)]:>10,}" for v in VERDICTS)
              + "".join(f"{guard[(arm, e, 'BREAKOUT')]:>12,}" for e in ENGINES)
              + f"{guard[(arm, 'js', 'BREAKOUT')]:>9,}{guard[(arm, 'js', 'MALFORMED')]:>9,}")


if __name__ == "__main__":
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--limit", type=int, help="use only the first N payloads")
    ap.add_argument("--skip-sabotage", action="store_true")
    ap.add_argument("--sabotage-n", type=int, help="sabotage only the first N payloads")
    run(ap.parse_args())
