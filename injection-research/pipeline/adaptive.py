"""Hole-targeted values for the SQL and HTML replays: the adaptive attacker of
doc/threat.tex. The recorded corpus aims at the native closers, so this set
aims at the hole itself: a grammar of hole-boundary atoms around breakout
cores, mutations of the payloads that break out of the control (made
matchertext so that VERIFY accepts them, led or trailed by a lone closer,
wrapped, backslash-escaped, given a zero byte), and nesting at and past the
depth limit. Each value is canonicalized as a writer does (A3). The set is
deterministic.
"""
import random

from matchertext import MAX_MATCHER_DEPTH, PAIR, canon

CLOSER = {v: k for k, v in PAIR.items()}

# Atoms an attacker places at a hole boundary: the matchers, native quotes,
# the matchertext escapes, a zero byte, and URL-encoded and fullwidth matchers,
# which canonicalization turns into the matchers themselves.
COMMON = [")", "]", "}", "(", "[", "{", "'", '"', "`", "\\", " ", "\n", "\0",
          "\\o()", "\\c()", "\\o[]", "\\c[]", "\\o{}", "\\c{}",
          "%29", "%5D", "）", "］"]
SQL_ATOMS = COMMON + [
    "''", ")'", "]'", "M'(", "--", "/*", "*/", "#", ";", "N'", "$$",
    # Overlong UTF-8 forms of ) and ], as raw bytes: no UTF-8 reader decodes
    # them to a matcher, and neither VERIFY nor the tokenizer does.
    "\udcc0\udca9", "\udce0\udc80\udca9", "\udcc1\udc9d"]
HTML_ATOMS = COMMON + [
    "]>", "]]>", ">", "<", "/", "=", "<!--", "-->", "<![MDATA[", "&#93;", "&rsqb;",
    "</script>", "</textarea>", "</title>", "</style>", "<!--<script>",
    # The last UTF-8 byte of U+0101 is a Shift_JIS lead byte: a reader that
    # decodes the document as Shift_JIS joins the next byte to it.
    "\u0101"]
SQL_CORES = ["OR 1=1", "OR '1'='1'", "UNION SELECT 1", "; SELECT 1", "AND 0",
             "|| (SELECT 1)", ", (SELECT 1)", "OR rowid>0",
             "UNION SELECT name FROM sqlite_master"]
HTML_CORES = ["<img src=x onerror=alert(1)>", "<script>alert(1)</script>",
              '" onmouseover=alert(1) x="', "' onfocus=alert(1) autofocus x='",
              "<svg onload=alert(1)>", "<b>x</b>", "<!-- x -->"]
# A Shift_JIS reader joins the [ to the byte before it and ends the hole at
# the ]; VERIFY on the UTF-8 text sees a balanced pair.
HTML_PROBES = ["\u0101[x]><img src=x onerror=alert(1)>"]
SYNTAX = {"sql": (SQL_ATOMS, SQL_CORES, []), "html": (HTML_ATOMS, HTML_CORES, HTML_PROBES)}


def balance(p):
    """p made matchertext by adding matchers: an opener at the front for each
    closer that closes nothing, an opener before each closer that crosses the
    open matcher, and a closer at the end for each open matcher."""
    front, out, stack = "", [], []
    for ch in p:
        if ch in CLOSER:
            stack.append(ch)
        elif ch in PAIR:
            if not stack:
                front = PAIR[ch] + front
            elif stack[-1] != PAIR[ch]:
                out.append(PAIR[ch])
            else:
                stack.pop()
        out.append(ch)
    return front + "".join(out) + "".join(CLOSER[c] for c in reversed(stack))


def mutations(p):
    b = balance(p)
    yield b
    for c in PAIR:
        yield c + p
        yield p + c
    yield "(" + b + ")"
    yield "[" + b + "]"
    yield "".join("\\" + ch if ch in "()[]{}" else ch for ch in b)
    yield b + "\0"


def deep(core):
    """core nested at the depth limit, which VERIFY accepts, and one past it."""
    for d in (MAX_MATCHER_DEPTH, MAX_MATCHER_DEPTH + 1):
        yield "(" * d + core + ")" * d
        yield "[" * d + core + "]" * d


def values(syntax, seeds, known, n_grammar, n_seeds):
    """The hole-targeted set for `syntax`: `n_grammar` grammar values and their
    matchertext forms, the mutations of up to `n_seeds` of the effective
    payloads `seeds`, and the depth probes. Values in `known` (the recorded
    corpus) are left out."""
    atoms, cores, probes = SYNTAX[syntax]
    rng = random.Random(0)
    out = list(probes)
    for _ in range(n_grammar):
        v = ("".join(rng.choices(atoms, k=rng.randint(1, 3))) + rng.choice(cores)
             + "".join(rng.choices(atoms, k=rng.randint(0, 2))))
        out += [v, balance(v)]
    seeds = sorted(seeds)
    for p in seeds[::max(1, len(seeds) // n_seeds)][:n_seeds]:
        out += mutations(p)
    for core in cores[:2]:
        out += deep(core)
    seen, vals = set(known), []
    for v in map(canon, out):
        if v not in seen:
            seen.add(v)
            vals.append(v)
    return vals
