"""Build a self-contained HTML report of the classification results.

Renders the classification, PoC linkage, syntactic groups, matchertext-prevention
analysis, and sub-classification into a single styled, theme-aware HTML file
(data/exports/report.html). Payloads are heuristically extracted from linked PoC
files; payload sampling is seeded, so the report is reproducible.

Usage: python3 pipeline/report.py [--samples 5] [--seed 42]
"""
import argparse
import ast
import base64
import html
import random
import re
import sqlite3
import warnings
from pathlib import Path

import matchertext

ROOT = Path(__file__).resolve().parents[1]
DB = ROOT / "data" / "cve.db"
RAW = ROOT / "data" / "raw"
OUT = ROOT / "data" / "exports" / "report.html"

# Per-syntax payload signatures, ordered most-specific first. Applied to the
# text of a linked exploit file to pull out the actual injection string.
_SIGNS = {
    "sql": [r"UNION(?:\s+ALL)?\s+SELECT\b[^\n]*",
            # \)? catches the paren-breakout form `admin') OR '1'='1`, which is
            # precisely the unmatched-closer case the containment split turns on.
            r"'\)?\s*(?:OR|AND)\s*'?\d+'?\s*=\s*'?\d+[^\n]*",
            r"'\)?\s*(?:OR|AND)\b[^\n]*--", r"\bAND\s+\d+=\d+[^\n]*", r"';?\s*WAITFOR\s+DELAY[^\n]*",
            r"\bextractvalue\s*\([^\n]*",
            # Stops at the closing paren: running to end of line dragged in the
            # enclosing query's closers, as in `SLEEP(5)))vltp)`, whose stray
            # `)`s turn a balanced payload into an apparent rejection.
            r"\bSLEEP\s*\(\d+\)", r"\[sql\]"],
    # A bare <script> tag is not a payload. In curated exploit files that was
    # harmless, but a linked repo is often the vulnerable application itself,
    # whose own source is full of benign script tags and jQuery includes. Require
    # the body to actually do something an attack does.
    "html_dom": [r"<script\b[^>]*>[^<]{0,150}?(?:alert|prompt|confirm|eval"
                 r"|document\s*\.\s*(?:cookie|domain|location|write))\s*[({][^<]{0,120}?</script>",
                 r"<img[^>]*onerror\s*=[^\n]*?>",
                 r"<svg[^>]*on\w+\s*=[^\n]*?>",
                 # Both of these fired on ordinary page source: `"><script` on an
                 # attribute boundary, `javascript:` on a benign inline handler.
                 r'"><script[^\n]{0,120}?(?:alert|prompt|confirm|eval|document\s*\.)',
                 r"javascript:\s*(?:alert|prompt|confirm|eval|document\s*\.)\S{0,80}",
                 r"%3[Cc]script[^\n]*"],
    "shell_command": [r"`(?:[ \t]*|[^`\n]{0,80}[;&|][ \t]*)(?:/[\w.-]+/)*"
                      r"(?:cat|id|whoami|pwd|ping|curl|wget|sleep|touch|rm|mkdir|uname|nc|bash|sh|echo|reboot|telnetd?|calc(?:\.exe)?|powershell|cmd(?:\.exe)?|certutil)"
                      r"(?![\w(./-])[^`\n]*`",
                      r"\$\([ \t]*(?:/[\w.-]+/)*(?:cat|id|whoami|pwd|ping|curl|wget|sleep|touch|rm|mkdir|uname|nc|bash|sh|echo|reboot|telnetd?|calc(?:\.exe)?|powershell|cmd(?:\.exe)?|certutil)(?![\w(./-])[^)\n]*\)",
                      r"[;&|][ \t]*(?:/[\w.-]+/)*(?:cat|id|whoami|pwd|ping|curl|wget|sleep|touch|rm|mkdir|uname|nc|bash|sh|echo|reboot|telnetd?|calc(?:\.exe)?|powershell|cmd(?:\.exe)?|certutil)(?![\w(./-])[^`\n]*"],
    # These once matched any call named system()/eval(); in prose and diagrams
    # that is a false positive, so require the argument to look like an injected
    # command or a variable rather than an identifier.
    "code_eval": [r"<\?php\b[^\n]{0,140}?(?:system|exec|eval|passthru|shell_exec|assert|\$_)[^\n]{0,60}",
                  r"\b(?:eval|system|passthru|shell_exec|popen)\s*\(\s*"
                  r"(?:[\"'`]?\s*(?:cat|ls|id|whoami|uname|curl|wget|nc|sh|bash|echo|ping|dir|type|net)\b"
                  r"|\$|`|base64_decode)[^\n)]{0,80}\)"],
    "crlf_header": [r"(?:%0[dD]%0[aA]|\\r\\n)[A-Za-z][\w-]{1,40}:[^\n]*"],
    "ldap": [r"\*\)\([^\n]*", r"\)\(\w+=[^\n]*"],
    "xpath_xquery": [r"'?\s*or\s*'?1'?\s*=\s*'?1[^\n]*", r"count\(/[^\n]*", r"//\*[^\n]*"],
    "template": [r"\{\{[^\n]*?\}\}", r"<%[^\n]*?%>", r"#\{[^\n]*?\}"],
    "expression_language": [r"\$\{[^\n]*?\}", r"#\{[^\n]*?\}", r"%\{[^\n]*?\}"],
    "formula_csv": [r"[=+@\-](?:cmd|CMD|SUM|HYPERLINK|WEBSERVICE)[^\n]*", r"=\d+[+\-*][^\n]*"],
    "nosql": [r"\{\s*[\"']?\$(?:gt|ne|where|regex)[^\n]*", r"\[\$ne\][^\n]*"],
    "xml": [r"<!DOCTYPE[^\n]*\[[^\n]*", r"<!ENTITY\b[^\n]*", r"SYSTEM\s+[\"']file:[^\n]*"],
    # A bare `-flag=value` is just a command line. Argument injection needs the
    # value to carry a command substitution or separator.
    "argument": [r"-[A-Za-z][\w-]*=`[^\n]*", r"-[A-Za-z][\w-]*=\$\([^\n]*",
                 r"-[A-Za-z][\w-]*=[^\s\n]*[;|&][^\n]*"],
}
SIGNS = {s: [re.compile(p, re.I) for p in ps] for s, ps in _SIGNS.items()}


# Metavariable placeholders mark *where* to inject; they are not attack payloads.
# Exploit-DB entries derived from SecurityFocus write the injection point as
# "[SQL]", Nuclei templates use variables like "{{BaseURL}}", and Metasploit
# modules carry un-interpolated Ruby source such as "`#{cmd}`". Accepting these
# yields payloads that are trivially valid matchertext and would bias the
# containment measurement. The bare-identifier restriction keeps genuine
# expression payloads such as "{{7*7}}", which are not placeholders.
_PLACEHOLDER = re.compile(
    r"^(?:\[[A-Za-z_][A-Za-z0-9_ ]*\]|\{\{[A-Za-z_][A-Za-z0-9_-]*\}\})$")


def is_placeholder(frag):
    return bool(_PLACEHOLDER.match(frag)) or frag in ("${}", "#{}", "{{}}") or "#{" in frag


# Payloads recorded as URLs are the single largest extraction gap: the literal
# signatures above require real whitespace, so `union+select`, `UNION/**/SELECT`
# and `%20UNION%20SELECT` all miss. These match the URL forms, and their matches
# are decoded before use, because a percent-escape is only a percent-escape in
# transit -- the web server decodes it, so the SQL or HTML parser really does see
# `(` where the recorded PoC wrote `%28`. Skeletonizing the undecoded form would
# hide matchers that are genuinely present and bias the containment split toward
# inert embedding.
_SEP = r"(?:%20|\+|/\*\*?/|\s)"
_TAIL = r"[^\s\"'<>\n]*"
# \b is wrong here: after a percent-escape the preceding character is a digit
# (the `0` of %20), so \bunion never fires on %20UNION. Guard on letters only.
_W = r"(?<![A-Za-z_])"
_URL_SIGNS = {
    "sql": [rf"{_W}union{_SEP}+(?:all{_SEP}+)?select{_TAIL}",
            rf"{_W}(?:and|or){_SEP}+\d+{_SEP}*={_SEP}*\d+{_TAIL}",
            rf"{_W}(?:and|or){_SEP}*\((?:ascii|substring|substr|length|count){_TAIL}",
            rf"%27{_TAIL}(?:union|select|or|and){_TAIL}",
            rf"{_W}(?:sleep|benchmark|pg_sleep)(?:%28|\()\d+(?:%29|\))",
            rf"{_W}concat_ws(?:%28|\(){_TAIL}"],
    "html_dom": [r"[\"'][^\"'\n]{0,24}\son\w+\s*=\s*(?:alert|prompt|confirm)\([^)\n]*\)[^\n]{0,40}",
                 r"[\"']\s*;\s*(?:alert|prompt|confirm)\([^)\n]*\)\s*;?\s*(?://|<)",
                 r"%22[^\n]{0,60}?on\w+\s*(?:%3[Dd]|=)\s*(?:alert|prompt|confirm)[^\n]{0,40}",
                 rf"%3[Cc](?:script|img|svg){_TAIL}",
                 r"(?:&lt;|&#(?:0*60|x0*3c);)(?:script\b[^`\n]{0,80}"
                 r"(?:alert|prompt|confirm|document)[^`\n]{0,160}|(?:img|svg)\b[^`\n]{0,80}"
                 r"on(?:error|load)[^`\n]{0,160})"],
    # Balancing the non-hostable syntaxes matters for the containment measure:
    # improving extraction only for SQL and XSS would skew the payload-backed
    # set toward matcher-hostable contexts and inflate the contained share.
    "shell_command": [
        rf"(?:%3[Bb]|%7[Cc]|%26){_SEP}*(?:echo|cat|id|whoami|ping|curl|wget|uname|nc|ls|dir|sleep){_TAIL}",
        r"%24(?:%28|\()[^\n]{0,60}",
        r"%60[^\n]{0,60}%60"],
    "code_eval": [
        r"T\(java\.lang\.Runtime\)[^\n]{0,80}",
        r"\bgetRuntime\(\)\s*\.\s*exec\([^\n)]{0,60}\)",
        rf"%3[Cc]%3[Ff]php{_TAIL}",
        rf"[?&]\w+=https?(?::|%3[Aa])(?://|%2[Ff]%2[Ff]){_TAIL}\.(?:txt|php)\?"],
}
URL_SIGNS = {s: [re.compile(p, re.I) for p in ps] for s, ps in _URL_SIGNS.items()}
_PCT = re.compile(r"%([0-9A-Fa-f]{2})")


def urlform_decode(frag, rounds=2):
    """Undo the transport encoding a recorded URL payload carries.

    Two rounds because double-encoding (%2527 -> %27 -> ') is common in the
    corpus; `+` is a query-string space, and an emptied SQL comment (/**/) is a
    whitespace substitute used to evade naive filters.
    """
    for _ in range(rounds):
        decoded = _PCT.sub(lambda m: chr(int(m.group(1), 16)), frag)
        if decoded == frag:
            break
        frag = decoded
    return re.sub(r"/\*\*?/", " ", frag).replace("+", " ")


_PAIR = {")": "(", "]": "[", "}": "{"}


def is_truncated(frag):
    """Did the signature cut the payload mid-expression?

    A breakout emits an unmatched *closer*: closing the slot early is the whole
    manoeuvre, so `'r0t')</script>` is a real payload. An unmatched *opener* left
    at the tail is the regex stopping inside a function call instead, as in
    `ASCII(SUBSTRING(passwd,`. Keeping those would count a regex artifact as a
    VERIFY rejection -- they accounted for 37% of all rejections before this
    check -- so treat them as failed extractions rather than payloads.
    """
    stack = []
    for ch in frag:
        if ch in "([{":
            stack.append(ch)
        elif ch in _PAIR and stack and stack[-1] == _PAIR[ch]:
            stack.pop()
    return bool(stack)


_JS_STUB = re.compile(r"^javascript:(?:alert|prompt|confirm|eval)?$", re.I)
_SHELL_STUB = re.compile(r"^(?:\$\([A-Za-z_][\w.]*\)|`[^`]*\$\{[^}]+\}[^`]*`)$")
_EVAL_STUB = re.compile(r"^(?:getRuntime\(\)\s*\.\s*)?exec\(\s*\)$", re.I)
_LISTENER = re.compile(r"^`?nc\s+-[^\n`]*l", re.I)
_SHELL_BARE = re.compile(r"^`?(?:(?:/bin/)?(?:sh|bash)|curl|wget|rm|cat|nc)`?$", re.I)
_SHELL_WHOLE = re.compile(
    r"^[`\"']*(?:[;&|]|(?:/[\w.-]+/)*(?:cat|id|whoami|pwd|ping|curl|wget|sleep|touch|rm|mkdir|"
    r"uname|nc|bash|sh|echo|reboot|telnetd?|calc(?:\.exe)?|powershell|cmd(?:\.exe)?|certutil)(?![\w(./-]))", re.I)
_SOURCEISH = re.compile(r"\b(?:snprintf|sprintf|system)\s*\(|%[a-z](?:\W|$)", re.I)
# `&id=62` is a URL parameter named id, not the id command.
_URL_PARAM = re.compile(r"^[&;|]\w+=")


def _accept(frag, truncation_check=True):
    return (3 <= len(frag) <= 200
            and not is_placeholder(frag)
            and not (truncation_check and is_truncated(frag))
            and not _JS_STUB.match(frag)
            and not _SHELL_STUB.match(frag)
            and not _EVAL_STUB.match(frag)
            and not _LISTENER.match(frag)
            and not _SHELL_BARE.match(frag)
            and not _URL_PARAM.match(frag))


def extend_balance(text, end, frag, limit=8):
    """Reclaim closers a regex could not count.

    A pattern cannot match nested parentheses, so `eval(base64_decode($x))` is
    captured one `)` short and the truncation guard would throw it away. If the
    characters immediately following the match are closing matchers, they belong
    to the payload: append them until it balances.
    """
    while is_truncated(frag) and limit and end < len(text) and text[end] in ")]}":
        frag += text[end]
        end += 1
        limit -= 1
    return frag


# Exploit-DB write-ups and sqlmap transcripts often label the payload outright.
# The label is a locator the per-syntax signatures cannot supply: it marks a
# string as the attack even when that string uses a command or sink the
# signatures do not enumerate, as in `...&currentTSREmailTo=|date>/tmp/x`.
_LABELLED = [
    re.compile(r"^[ \t]*#?[ \t]*Payload[ \t]*:[ \t]*(\S.{4,180})$", re.I | re.M),
    re.compile(r"^[ \t]*#?[ \t]*(?:PoC|Proof of Concept)[ \t]*:[ \t]*(\S.{4,180})$", re.I | re.M),
    re.compile(r"^[ \t]*(?:GET|POST)[ \t]+(\S{8,180})[ \t]+HTTP", re.M),
    re.compile(r"\bpayload(?:\s+such\s+as|\s+like)?[ \t]+([;&|]\s*\S.{2,100}?)"
               r"(?=\s+(?:into|through|in|to)\b|[.,\n])", re.I),
]
# Trusting a label still needs the value to look like an attack rather than a
# bare path or a separator line: it must carry a delimiter, metacharacter or
# escape, and it must not be only structure.
_ATTACKISH = re.compile(r"""['"<>;|`]|\$\(|%[0-9A-Fa-f]{2}|&&|\|\||[(){}\[\]]""")
_ONLY_PUNCT = re.compile(r"^[\W_]+$")

# Static construction forms used by exploit scripts. Only constant parts are
# folded; no PoC code is executed. A reconstructed string must still match the
# syntax-specific signatures below, which keeps ordinary program strings out.
_STR = re.compile(r'''"(?:\\.|[^"\\])*"|'(?:\\.|[^'\\])*'|`(?:\\.|[^`\\])*`''', re.S)
_CONCAT = re.compile(r"[\s+.&()\\]*")
_B64 = re.compile(r"[A-Za-z0-9_+/=-]{12,684}")
_B64_HINT = re.compile(r"base64|b64decode|atob|frombase64", re.I)
_ESC = re.compile(r"\\(?:x([0-9A-Fa-f]{2})|u([0-9A-Fa-f]{4})|([nrt\\'\"]))")
_FIELD = re.compile(r"(?:[\"'][\w.-]+[\"']|[\w.-]+)[ \t]*[:=][ \t]*$")
_BUILD_HINT = re.compile(
    r"base64|b64decode|atob|frombase64|\b(?:f|rf|fr)[\"']|\.format\s*\(|"
    r"[\"'][^\n]{0,200}[\"'][ \t]*(?:\+|\.|&)[ \t]*(?:[frbu]{0,2}[\"']|[A-Za-z_])",
    re.I)


def _unescape(value):
    def sub(m):
        if m.group(1):
            return chr(int(m.group(1), 16))
        if m.group(2):
            return chr(int(m.group(2), 16))
        return {"n": "\n", "r": "\r", "t": "\t"}.get(m.group(3), m.group(3))
    return _ESC.sub(sub, value[1:-1])


def _static_python(node, env=None):
    """Fold static Python string expressions without evaluating code."""
    env = env or {}
    if isinstance(node, ast.Constant) and isinstance(node.value, (str, bytes)):
        return node.value.decode("utf-8", "replace") if isinstance(node.value, bytes) else node.value
    if isinstance(node, ast.Name):
        return env.get(node.id)
    if isinstance(node, ast.BinOp) and isinstance(node.op, ast.Add):
        left, right = _static_python(node.left, env), _static_python(node.right, env)
        return left + right if left is not None and right is not None else None
    if (isinstance(node, ast.BinOp) and isinstance(node.op, ast.Mult)
            and isinstance(node.right, ast.Constant) and isinstance(node.right.value, int)):
        value = _static_python(node.left, env)
        return value * min(node.right.value, 256) if value is not None else None
    if isinstance(node, ast.BinOp) and isinstance(node.op, ast.Mod):
        value = _static_python(node.left, env)
        return re.sub(r"%(?:\([^)]+\))?[#0 +\-]*\d*(?:\.\d+)?[a-zA-Z]", "0", value) \
            if value is not None else None
    if isinstance(node, ast.JoinedStr):
        parts = []
        for value in node.values:
            if isinstance(value, ast.FormattedValue):
                parts.append(_static_python(value.value, env) or "0")
            else:
                parts.append(_static_python(value, env) or "0")
        return "".join(parts)
    if (isinstance(node, ast.Call) and isinstance(node.func, ast.Attribute)
            and node.func.attr == "format"):
        value = _static_python(node.func.value, env)
        return re.sub(r"\{[^{}]*\}", "0", value) if value is not None else None
    if (isinstance(node, ast.Call) and isinstance(node.func, ast.Attribute)
            and node.func.attr in ("encode", "decode")):
        return _static_python(node.func.value, env)
    return None


def constructed_strings(text):
    """Yield decoded or statically joined strings from exploit source."""
    seen = set()

    def emit(value):
        value = value.strip()
        if 3 <= len(value) <= 500 and value not in seen:
            seen.add(value)
            return value
        return None

    with warnings.catch_warnings():
        warnings.simplefilter("ignore", SyntaxWarning)
        trees = []
        fenced = [body for lang, body in re.findall(r"```([^\r\n]*)\r?\n(.*?)```", text,
                                                     re.S)
                  if lang.strip().lower() in ("", "py", "python", "python2", "python3")]
        for source in (text, *fenced):
            try:
                trees.append(ast.parse(source))
            except (SyntaxError, ValueError, MemoryError):
                pass
    for tree in trees:
        env = {}
        for node in ast.walk(tree):
            if (isinstance(node, (ast.Assign, ast.AnnAssign))
                    and isinstance(node.value, ast.AST)):
                targets = node.targets if isinstance(node, ast.Assign) else [node.target]
                value = _static_python(node.value, env)
                if value is not None:
                    for target in targets:
                        if isinstance(target, ast.Name):
                            env[target.id] = value
            if isinstance(node, (ast.BinOp, ast.JoinedStr, ast.Call)):
                value = _static_python(node, env)
                if value and (out := emit(value)):
                    yield out

    # Backtick code fences are Markdown containers, not JavaScript strings.
    string_text = re.sub(r"```[^\n]*\n|```", "", text)
    tokens = [m for m in _STR.finditer(string_text)
              if not (m.group(0).startswith("`") and "\n" in m.group(0))]
    group = []
    for token in tokens:
        if group and not _CONCAT.fullmatch(string_text[group[-1].end():token.start()]):
            if len(group) > 1 and (out := emit("".join(_unescape(x.group(0)) for x in group))):
                yield out
            group = []
        group.append(token)
    if len(group) > 1 and (out := emit("".join(_unescape(x.group(0)) for x in group))):
        yield out

    for token in tokens:
        line_start = max(string_text.rfind("\n", 0, token.start()) + 1,
                         token.start() - 160)
        line = string_text[line_start:token.start()]
        if not line.lstrip().startswith(("#", "//")) and _FIELD.search(line):
            if out := emit(_unescape(token.group(0))):
                yield out
        start, end = max(0, token.start() - 80), min(len(string_text), token.end() + 40)
        if not _B64_HINT.search(string_text[start:end]):
            continue
        encoded = token.group(0)[1:-1]
        if not _B64.fullmatch(encoded):
            continue
        try:
            decoded = base64.b64decode(encoded.replace("-", "+").replace("_", "/")
                                       + "=" * (-len(encoded) % 4), validate=True)
            value = decoded.decode("utf-8")
        except (ValueError, UnicodeDecodeError):
            continue
        if out := emit(value):
            yield out

    for line in text.splitlines():
        if "=" not in line or "&" not in line:
            continue
        for field in re.split(r"&(?=[A-Za-z_][\w.-]*=)", line):
            if "=" not in field:
                continue
            value = urlform_decode(field.split("=", 1)[1].strip())
            cuts = [i for i in (value.find(";"), value.find("|")) if i >= 0]
            if cuts and (out := emit(value[min(cuts):])):
                yield out


def extract_labelled(text, syn):
    for rx in _LABELLED:
        for m in rx.finditer(text):
            raw = " ".join(m.group(1).split())
            if syn == "shell_command":
                raw = re.split(r"&(?:quot|gt|lt);", raw, 1, re.I)[0].rstrip()
            # The whole labelled value is preferred over a signature match inside
            # it: a signature would capture some suffix such as `SLEEP(5)))vltp)`,
            # whose stray closers make a balanced payload look like a rejection.
            # is_truncated is skipped here for the same reason it exists -- it
            # detects a regex stopping mid-expression, and a labelled line is a
            # delimited whole, so a trailing `{` in `'};alert(1);{'` is the real
            # payload rather than a cut.
            if (_accept(raw, truncation_check=False) and _ATTACKISH.search(raw)
                    and not _ONLY_PUNCT.match(raw) and "=" in raw):
                return raw
            inner = extract_payload(raw, syn, labelled=False)
            if inner:
                return inner
    return None


def extract_payload(text, syn, labelled=True, constructed=True, prefer_constructed=False):
    def built_payload():
        hits = []
        for candidate in constructed_strings(text):
            payload = extract_payload(candidate, syn, labelled=False, constructed=False)
            if payload:
                if (syn == "shell_command" and len(candidate) <= 200
                        and _SHELL_WHOLE.match(candidate.strip())):
                    hits.append((0, -len(candidate), candidate.strip()))
                else:
                    hits.append((1, -len(payload), payload))
        return min(hits)[2] if hits else None

    if constructed and prefer_constructed and (payload := built_payload()):
        return payload
    if labelled and (payload := extract_labelled(text, syn)):
        return payload
    for rx in SIGNS.get(syn, ()):
        for m in rx.finditer(text):
            if (syn == "shell_command"
                    and re.search(r"<(?:COMMAND|CMD)>", text[max(0, m.start() - 100):m.end()], re.I)):
                continue
            frag = " ".join(extend_balance(text, m.end(), m.group(0)).split())
            if syn == "shell_command" and _SOURCEISH.search(frag):
                continue
            if syn == "html_dom":
                if re.match(r"(?:&lt;|&#(?:0*60|x0*3c);)(?:img|svg)\b", frag, re.I):
                    close = re.search(r"&gt;", frag, re.I)
                    if close:
                        frag = frag[:close.end()]
                else:
                    close = re.search(r"&lt;/script&gt;", frag, re.I)
                    if close:
                        frag = frag[:close.end()]
            if syn == "shell_command":
                frag = re.split(r"&(?:quot|gt|lt);", frag, 1, re.I)[0].rstrip()
                frag = re.split(r"\s+HTTP/\d|,\s+then\b", frag, 1, re.I)[0].rstrip()
                if _PCT.search(frag):
                    frag = urlform_decode(frag)
            if _accept(frag):
                return frag
    # Literal forms first, so an unencoded payload is never routed through the
    # decoder; only fall back to the URL forms when nothing else matched.
    for rx in URL_SIGNS.get(syn, ()):
        for m in rx.finditer(text):
            frag = " ".join(urlform_decode(m.group(0)).split())
            if syn == "html_dom":
                close = re.search(r"&gt;", frag, re.I)
                if close and re.match(r"(?:&lt;|&#(?:0*60|x0*3c);)(?:img|svg)\b",
                                      frag, re.I):
                    frag = frag[:close.end()]
                else:
                    close = re.search(r"&lt;/script&gt;", frag, re.I)
                    if close:
                        frag = frag[:close.end()]
            if _accept(frag):
                return frag
    if constructed and not prefer_constructed and _BUILD_HINT.search(text):
        return built_payload()
    return None


def sample_payloads(con, syn, n, rng):
    # Seeding rng only makes the sample reproducible if the list it shuffles has
    # a fixed order to begin with, hence the ORDER BY.
    links = con.execute(
        """SELECT DISTINCT c.cve_id, p.source, p.local_path FROM poc p
           JOIN classification c USING(cve_id)
           WHERE c.syntax_type=? AND p.local_path IS NOT NULL
           ORDER BY c.cve_id, p.source, p.local_path""", (syn,)).fetchall()
    rng.shuffle(links)
    out, seen = [], set()
    for cve_id, source, rel in links:
        try:
            payload = extract_payload((RAW / rel).read_text(encoding="utf-8", errors="replace"), syn)
        except OSError:
            continue
        if payload and payload not in seen:
            seen.add(payload)
            out.append((cve_id, source, payload))
            if len(out) >= n:
                break
    return out, len(links)


# --- HTML helpers -------------------------------------------------------------
def esc(s):
    return html.escape(str(s))


def co(s):
    return f"<code>{esc(s)}</code>"


def pill(s):
    return f'<span class="pill">{esc(s)}</span>'


def bar(frac):
    return f'<span class="bar"><span style="width:{min(frac, 1) * 100:.1f}%"></span></span>'


def tbl(headers, rows, aligns=None):
    aligns = aligns or [""] * len(headers)
    head = "".join(f'<th class="{a}">{esc(h)}</th>' for h, a in zip(headers, aligns))
    body = []
    for r in rows:
        cells = "".join(f'<td class="{a}">{c}</td>' for c, a in zip(r, aligns))
        body.append(f"<tr>{cells}</tr>")
    return (f'<div class="tw"><table><thead><tr>{head}</tr></thead>'
            f'<tbody>{"".join(body)}</tbody></table></div>')


def section(sid, title, *blocks):
    return f'<section id="{sid}"><h2>{esc(title)}</h2>{"".join(blocks)}</section>'


def p(text):
    return f"<p>{text}</p>"


# --- content sections ---------------------------------------------------------
def label_attribution(q, related):
    rows = [(esc(m), f"{n:,}", f"{n / related:.1%}", bar(n / related))
            for m, n in q("SELECT method, COUNT(*) FROM classification GROUP BY 1 ORDER BY 2 DESC")]
    return section("attribution", "Label attribution",
                   p("How each injection CVE's syntax type was determined — CWE map, "
                     "phrase rules, naive-Bayes fallback, or left unknown."),
                   tbl(["method", "count", "share", ""], rows, ["", "num", "num", "barcol"]))


def poc_sources(q):
    rows = [(pill(s), f"{n:,}", f"{d:,}", f"{i:,}") for s, n, d, i in q(
        """SELECT p.source, COUNT(*), COUNT(DISTINCT p.cve_id), COUNT(DISTINCT c.cve_id)
           FROM poc p LEFT JOIN classification c USING(cve_id) GROUP BY 1 ORDER BY 2 DESC""")]
    return section("poc-sources", "Proof-of-concept sources",
                   p("PoC references linked to injection CVEs, by source database."),
                   tbl(["source", "PoC rows", "distinct CVEs", "injection CVEs"], rows,
                       ["", "num", "num", "num"]))


def families(q, related):
    rows = [(esc(f), f"{n:,}", f"{k:,}", f"{n / related:.1%}", bar(n / related)) for f, n, k in q(
        """SELECT c.weakness_family, COUNT(*), COUNT(k.cve_id) FROM classification c
           LEFT JOIN kev k USING(cve_id) GROUP BY 1 ORDER BY 2 DESC""")]
    return section("families", "Weakness families",
                   tbl(["family", "count", "KEV", "share", ""], rows,
                       ["", "num", "num", "num", "barcol"]))


def syntax_types(q, related):
    rows = [(pill(s), f"{n:,}", f"{k:,}", f"{pc:,}", f"{n / related:.1%}", bar(n / related))
            for s, n, k, pc in q(
        """SELECT c.syntax_type, COUNT(DISTINCT c.cve_id), COUNT(DISTINCT k.cve_id),
                  COUNT(DISTINCT e.cve_id)
           FROM classification c LEFT JOIN kev k USING(cve_id)
           LEFT JOIN poc e USING(cve_id) GROUP BY 1 ORDER BY 2 DESC""")]
    return section("syntax", "Syntax types",
                   p("The embedded language each injection targets — the primary axis."),
                   tbl(["syntax", "count", "KEV", "with PoC", "share", ""], rows,
                       ["", "num", "num", "num", "num", "barcol"]))


def syntactic_groups(q, groups):
    grouped = sum(n for *_, n in groups)
    singletons = sum(1 for *_, n in groups if n == 1)
    top = tbl(["size", "syntax", "skeleton", "example"],
              [(f"{n:,}", pill(s), co(sk), co(ex)) for s, sk, ex, n in groups[:14]],
              ["num", "", "", ""])
    seen, per_syn = set(), []
    for s, sk, ex, n in groups:
        if s not in seen:
            seen.add(s)
            per_syn.append((pill(s), f"{n:,}", co(sk), co(ex)))
    largest = tbl(["syntax", "size", "largest skeleton", "example"], sorted(per_syn), ["", "num", "", ""])
    return section("groups", "Syntactic groups",
                   p(f"PoC-backed CVEs bucketed by an exact <em>syntactic skeleton</em> — the "
                     f"payload with literals, numbers and identifiers abstracted to "
                     f"{co('<q> <n> <id>')} and only the structural grammar (matchers, "
                     f"operators, tags, keywords) kept. <strong>{grouped:,}</strong> CVEs fall "
                     f"into <strong>{len(groups):,}</strong> distinct skeletons "
                     f"({singletons:,} singletons)."),
                   "<h3>Largest groups</h3>", top,
                   "<h3>Largest skeleton per syntax type</h3>", largest)


def stat(big, klass, label, sub, frac):
    return (f'<div class="callout {klass}"><div class="big">{big}</div>'
            f'<div class="cbody"><div class="clabel">{label}</div>'
            f'<div class="csub">{sub}</div>{bar(frac)}</div></div>')


MECH_BADGE = {
    "inert": '<span class="badge yes">inert&nbsp;embed</span>',
    "reject": '<span class="badge rej">rejected</span>',
    "": '<span class="badge no">no</span>',
}


def matchertext_section(q, groups, mech_counts, prev_cves, total_cves, related):
    # Show a representative mix of all three verdicts rather than only the
    # largest (which are all inert), so the reject/not-preventable cases appear.
    assessed = [(s, sk, ex, n, *matchertext.assess(s, sk)) for s, sk, ex, n in groups]
    pick, seen = [], set()
    for want in ("inert", "reject", ""):
        for row in assessed:
            key = row[1]
            if row[5] == want and key not in seen and sum(r[5] == want for r in pick) < 7:
                seen.add(key)
                pick.append(row)
    rows = []
    for s, sk, ex, n, ok, mech, why in sorted(pick, key=lambda r: -r[3]):
        cls = {"inert": "prevent", "reject": "reject"}.get(mech, "noprevent")
        verdict = f"{MECH_BADGE[mech]} {esc(why)}" if ok else MECH_BADGE[""]
        rows.append((f"{n:,}", pill(s), co(sk), verdict, cls))
    body = "".join(
        f"<tr class='{cls}'>" + f'<td class="num">{a}</td><td>{b}</td>'
        f'<td class="skel">{c}</td><td class="verdict">{d}</td></tr>'
        for a, b, c, d, cls in rows)
    table = (f'<div class="tw"><table><thead><tr><th class="num">size</th>'
             f'<th>syntax</th><th>skeleton</th><th>matchertext verdict</th>'
             f'</tr></thead><tbody>{body}</tbody></table></div>')

    proj = sum(n for s, n in q("SELECT syntax_type, COUNT(*) FROM classification GROUP BY 1")
               if s in matchertext.HOST)
    inert, reject = mech_counts["inert"], mech_counts["reject"]
    callouts = ('<div class="callouts">' + stat(
        f"{prev_cves / total_cves:.0%}", "", "Prevented, payload-backed",
        f"<strong>{prev_cves:,}</strong> of <strong>{total_cves:,}</strong> CVEs with an "
        "extractable PoC land in a matcher-delimited slot their payload cannot escape",
        prev_cves / total_cves) + stat(
        f"{proj / related:.0%}", "proj", "Projected, all injection CVEs",
        f"<strong>{proj:,}</strong> of <strong>{related:,}</strong> target a "
        "matchertext-hostable syntax (payload not required)", proj / related) + "</div>")
    mechbars = ('<div class="callouts">' + stat(
        f"{inert / (inert + reject):.0%}", "", "Inert embedding",
        f"<strong>{inert:,}</strong> payloads are valid matchertext — they embed verbatim "
        "and the quote / angle-bracket breakout is inert data", inert / (inert + reject)) + stat(
        f"{reject / (inert + reject):.0%}", "rejalt", "Rejected by VERIFY",
        f"<strong>{reject:,}</strong> payloads carry an unmatched matcher — the delimiter they "
        "would escape with — so VERIFY (Alg. 1) rejects them", reject / (inert + reject)) + "</div>")
    return section("matchertext", "Matchertext prevention by skeleton",
                   p("Matchertext prevents an injection under two conditions (paper §2.4): "
                     "the value lands in a slot a matchertext-aware host delimits with a "
                     f"matcher pair (\\eg {co('WHERE name = [ v ]')} for SQL), and the value "
                     "itself is valid matchertext, checked by the passive VERIFY scan "
                     "(Algorithm 1) rather than by escaping. The canonical "
                     f"{co('x&#39; OR &#39;1&#39;=&#39;1')} then fails two ways."),
                   callouts,
                   p("Running VERIFY on each payload-backed skeleton in a matcher-hostable "
                     "context splits prevention into the paper's two mechanisms:"),
                   mechbars,
                   p(f"The {co('inert')} case is SQL/XSS (§4.7.1–2): the payload is valid "
                     "matchertext, so its quote or `<` is ordinary data read to the matched "
                     f"close. The {co('rejected')} case is LDAP/PDF (§4.7.3–4): the native "
                     "delimiter is already a matcher, so the real payload carries the unmatched "
                     "one it would break out with, and VERIFY rejects it. A verdict is empty "
                     "only where matchertext structurally cannot help — execution/semantic "
                     f"sinks ({co('template')}, {co('eval')}, {co('javascript:')} URLs) or "
                     "non-matcher-delimited contexts (shell, CR/LF, XML angle brackets)."),
                   table)


def subclass_section(q, related):
    cov = tbl(["dimension", "labeled CVEs", "coverage", "labels", ""],
              [(esc(d), f"{n:,}", f"{n / related:.1%}", k, bar(n / related)) for d, n, k in q(
                  """SELECT dimension, COUNT(DISTINCT cve_id), COUNT(DISTINCT label)
                     FROM subclass GROUP BY 1 ORDER BY 2 DESC""")],
              ["", "num", "num", "num", "barcol"])
    dists = []
    for dim, title in (("privilege", "Privilege required (CVSS)"),
                       ("attack_vector", "Attack vector (CVSS)"),
                       ("impact", "Impact (description)")):
        drows = q("SELECT label, COUNT(*) FROM subclass WHERE dimension=? GROUP BY 1 ORDER BY 2 DESC", dim)
        tot = sum(n for _, n in drows) or 1
        dists.append(f"<h3>{esc(title)}</h3>" + tbl(
            ["label", "count", ""], [(esc(l), f"{n:,}", bar(n / tot)) for l, n in drows],
            ["", "num", "barcol"]))
    tech = ["<h3>Technique by syntax type</h3>"]
    for syn, in q("""SELECT DISTINCT c.syntax_type FROM classification c JOIN subclass s
                     USING(cve_id) WHERE s.dimension='technique' ORDER BY 1"""):
        techs = q("""SELECT s.label, COUNT(*) FROM subclass s JOIN classification c USING(cve_id)
                     WHERE s.dimension='technique' AND c.syntax_type=? GROUP BY 1 ORDER BY 2 DESC""", syn)
        chips = " ".join(f'<span class="chip">{esc(l)} <b>{n:,}</b></span>' for l, n in techs)
        tech.append(f'<div class="techrow"><span class="pill">{esc(syn)}</span> {chips}</div>')
    return section("subclass", "Sub-classification",
                   p("Injection CVEs labelled on orthogonal facets (a CVE can carry one label "
                     "per dimension). Coverage is the share of injection CVEs a facet could be "
                     "determined for; dimensions differ because their signals do."),
                   cov, *dists, *tech)


def payload_samples(con, q, rng, n):
    blocks = [p("Real injection strings extracted from linked PoC files (Exploit-DB, Nuclei, "
                "Metasploit; heuristic). The CVE links the payload to its record.")]
    for syn, in q("SELECT DISTINCT syntax_type FROM classification ORDER BY 1"):
        samples, n_links = sample_payloads(con, syn, n, rng)
        blocks.append(f'<h3>{esc(syn)} <span class="mut">· {n_links:,} extractable PoCs</span></h3>')
        if not samples:
            blocks.append(p('<span class="mut">no payload extracted from linked PoCs</span>'))
            continue
        blocks.append(tbl(["CVE", "payload", "source"],
                          [(esc(cve), co(pl), pill(src)) for cve, src, pl in samples]))
    return section("payloads", "Payload samples per syntax type", *blocks)


NAV = [("attribution", "Attribution"), ("poc-sources", "PoC sources"), ("families", "Families"),
       ("syntax", "Syntax"), ("groups", "Syntactic groups"), ("matchertext", "Matchertext"),
       ("subclass", "Sub-classes"), ("payloads", "Payloads")]


def run(args):
    rng = random.Random(args.seed)
    con = sqlite3.connect(DB)
    q = lambda sql, *pp: con.execute(sql, pp).fetchall()

    total = q("SELECT COUNT(*) FROM cve WHERE state='PUBLISHED'")[0][0]
    related = q("SELECT COUNT(*) FROM classification")[0][0]
    with_poc = q("""SELECT COUNT(DISTINCT cve_id) FROM poc
                    WHERE cve_id IN (SELECT cve_id FROM classification)""")[0][0]
    groups = q("""SELECT syntax_type, skeleton, MIN(example), COUNT(*) c
                  FROM syntactic_group GROUP BY group_id ORDER BY c DESC""")
    grouped = sum(n for *_, n in groups)
    mech_counts = {"inert": 0, "reject": 0}
    prev_cves = 0
    for s, sk, _e, n in groups:
        ok, mech, _ = matchertext.assess(s, sk)
        if ok:
            prev_cves += n
            mech_counts[mech] += n

    kpis = "".join(
        f'<div class="kpi"><div class="kv">{v}</div><div class="kl">{l}</div>'
        f'<div class="ks">{s}</div></div>'
        for v, l, s in (
            (f"{total:,}", "published CVEs", "MITRE cvelistV5"),
            (f"{related:,}", "injection-related", f"{related / total:.1%} of all CVEs"),
            (f"{with_poc:,}", "with a PoC", "linked exploit / template"),
            (f"{prev_cves / grouped:.0%}", "matchertext-preventable",
             f"{prev_cves:,} of {grouped:,} grouped")))
    nav = "".join(f'<a href="#{sid}">{esc(t)}</a>' for sid, t in NAV)

    body = "".join([
        f'<header><h1>CVE injection classification</h1>'
        f'<p class="sub">Syntactic classification of injection vulnerabilities across the '
        f'public CVE corpus, with matchertext-prevention analysis.</p>'
        f'<div class="kpis">{kpis}</div></header>',
        f'<nav class="toc">{nav}</nav>',
        label_attribution(q, related),
        poc_sources(q),
        families(q, related),
        syntax_types(q, related),
        syntactic_groups(q, groups),
        matchertext_section(q, groups, mech_counts, prev_cves, grouped, related),
        subclass_section(q, related),
        payload_samples(con, q, rng, args.samples),
    ])
    OUT.parent.mkdir(parents=True, exist_ok=True)
    OUT.write_text(f"<!doctype html><html lang=en><head><meta charset=utf-8>"
                   f'<meta name=viewport content="width=device-width,initial-scale=1">'
                   f"<title>CVE injection classification</title><style>{CSS}</style></head>"
                   f'<body><main class="wrap">{body}</main></body></html>')
    print(f"wrote {OUT}")
    con.close()


CSS = """
*{box-sizing:border-box}
:root{--bg:#f6f8fb;--panel:#fff;--ink:#1b2733;--mut:#5f6b78;--line:#e3e8ef;--accent:#3b6ea5;
--code-bg:#eef2f7;--code-ink:#294057;--good-bg:#e6f6ec;--good-ink:#1c7a44;--good-line:#c2e6d0;
--pill:#eef2f7;--pill-ink:#41566b;--bar:#3b6ea5;--bar-bg:#e3e8ef}
@media (prefers-color-scheme:dark){:root{--bg:#0d1117;--panel:#161b22;--ink:#e6edf3;--mut:#8b949e;
--line:#2a313a;--accent:#6cb0ef;--code-bg:#1c2430;--code-ink:#cdd9e5;--good-bg:#12261a;
--good-ink:#4cc76e;--good-line:#20492f;--pill:#20272f;--pill-ink:#adbac7;--bar:#4f8fd6;--bar-bg:#262d36}}
html{-webkit-text-size-adjust:100%}
body{margin:0;background:var(--bg);color:var(--ink);
font:15px/1.55 system-ui,-apple-system,Segoe UI,Roboto,sans-serif}
.wrap{max-width:1080px;margin:0 auto;padding:28px 20px 80px}
h1{font-size:26px;margin:0 0 6px}h2{font-size:20px;margin:0 0 14px;padding-bottom:8px;
border-bottom:1px solid var(--line)}h3{font-size:15px;margin:22px 0 8px;color:var(--mut)}
.sub{color:var(--mut);margin:0 0 20px;max-width:70ch}
p{max-width:78ch}code{font:13px/1.5 ui-monospace,SFMono-Regular,Menlo,Consolas,monospace;
background:var(--code-bg);color:var(--code-ink);padding:1px 5px;border-radius:5px;
white-space:pre-wrap;overflow-wrap:anywhere}
section{background:var(--panel);border:1px solid var(--line);border-radius:12px;
padding:20px 22px;margin:16px 0}
.kpis{display:grid;grid-template-columns:repeat(auto-fit,minmax(180px,1fr));gap:12px;margin-top:18px}
.kpi{background:var(--panel);border:1px solid var(--line);border-radius:12px;padding:14px 16px}
.kv{font-size:26px;font-weight:700;letter-spacing:-.02em}
.kl{font-size:13px;margin-top:2px}.ks{font-size:12px;color:var(--mut);margin-top:2px}
.toc{position:sticky;top:0;z-index:5;display:flex;flex-wrap:wrap;gap:6px;padding:10px 0;
margin:4px 0 8px;background:var(--bg)}
.toc a{font-size:13px;text-decoration:none;color:var(--pill-ink);background:var(--pill);
padding:5px 11px;border-radius:999px;border:1px solid var(--line)}
.toc a:hover{color:var(--accent);border-color:var(--accent)}
.tw{overflow-x:auto;margin:4px 0}
table{border-collapse:collapse;width:100%;font-size:13.5px}
th,td{text-align:left;padding:8px 12px;border-bottom:1px solid var(--line);vertical-align:top}
th{position:sticky;top:0;background:var(--panel);font-size:12px;text-transform:uppercase;
letter-spacing:.04em;color:var(--mut)}
tbody tr:hover{background:color-mix(in srgb,var(--accent) 6%,transparent)}
td.num,th.num{text-align:right;font-variant-numeric:tabular-nums;white-space:nowrap}
td.skel{max-width:340px}td.verdict{max-width:460px;font-size:13px}
.barcol{width:120px}
.bar{display:inline-block;width:110px;height:7px;border-radius:4px;background:var(--bar-bg);vertical-align:middle}
.bar>span{display:block;height:100%;border-radius:4px;background:var(--bar)}
.pill{display:inline-block;background:var(--pill);color:var(--pill-ink);border-radius:999px;
padding:1px 9px;font-size:12px;white-space:nowrap}
.badge{display:inline-block;border-radius:6px;padding:1px 8px;font-size:12px;font-weight:600;margin-right:6px}
.badge.yes{background:var(--good-bg);color:var(--good-ink);border:1px solid var(--good-line)}
.badge.rej{background:color-mix(in srgb,var(--accent) 16%,var(--panel));color:var(--accent);
border:1px solid var(--accent)}
.badge.no{background:var(--pill);color:var(--mut)}
tr.prevent{box-shadow:inset 3px 0 var(--good-ink)}
tr.reject{box-shadow:inset 3px 0 var(--accent)}
tr.noprevent td{color:var(--mut)}
.callouts{display:grid;grid-template-columns:1fr 1fr;gap:12px;margin:14px 0}
@media (max-width:640px){.callouts{grid-template-columns:1fr}}
.callout{display:flex;align-items:center;gap:16px;background:var(--good-bg);
border:1px solid var(--good-line);border-radius:12px;padding:14px 18px;margin:0}
.callout .big{font-size:34px;font-weight:800;color:var(--good-ink);line-height:1;white-space:nowrap}
.callout.proj,.callout.rejalt{background:color-mix(in srgb,var(--accent) 12%,var(--panel));
border-color:var(--accent)}
.callout.proj .big,.callout.rejalt .big{color:var(--accent)}
.callout.proj .bar>span,.callout.rejalt .bar>span{background:var(--accent)}
.cbody{min-width:0}.clabel{font-weight:600;font-size:14px}
.csub{font-size:12.5px;color:var(--mut);margin:2px 0 8px}
.callout .bar{width:100%}
.chip{display:inline-block;background:var(--pill);border:1px solid var(--line);border-radius:6px;
padding:1px 8px;font-size:12px;margin:2px 3px 2px 0;color:var(--pill-ink)}
.chip b{color:var(--ink)}.techrow{margin:6px 0}.mut{color:var(--mut);font-weight:400}
"""

if __name__ == "__main__":
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--samples", type=int, default=5, help="payload examples per category")
    ap.add_argument("--seed", type=int, default=42, help="sampling seed")
    run(ap.parse_args())
