"""Matchertext-prevention analysis of a syntactic skeleton (paper sections 3.1, 4.7).

Matchertext prevents an injection under two conditions (paper section 2.4):
  (1) the slot the untrusted value lands in is delimited by a matcher pair in a
      matchertext-aware host, and
  (2) the value itself is valid matchertext.
Given (1), condition (2) is a passive check -- VERIFY, Algorithm 1 -- not a
content-modifying transformation. This yields the paper's "fails two ways"
result for the canonical `x' OR '1'='1` (section 4.7.1): supplied verbatim it is
valid matchertext and embeds as an inert literal (its quote is a nonmatcher, so
it is ordinary data inside the matched slot); adapted to attack the new `[]`
delimiter it must emit an unmatched `]`, which is not valid matchertext, so
VERIFY rejects it.

We therefore classify each matcher-hostable skeleton by which mechanism does the
work, running VERIFY on the skeleton (whose matchers are preserved and whose
values are abstracted to nonmatcher placeholders):
  INERT   -- payload is valid matchertext; embeds verbatim, breakout char inert.
  REJECT  -- payload carries an unmatched matcher; VERIFY rejects it (or, for a
             legitimately-unbalanced value, ToMatchertext escapes only the
             unmatched matchers and decodes on read -- same benign outcome).

Prevention does not apply -- verdict empty -- when the sink executes the
contained value by design (templates, expression languages, eval, spreadsheet
formulas, LLM prompts, HTML/DOM execution contexts: see html_sink) or the context is delimited by non-matchers with
no matcher hosting (shell metacharacters, CR/LF, XML/XXE angle brackets).
"""
import html
import re
import unicodedata
from urllib.parse import unquote, unquote_plus

PAIR = {")": "(", "]": "[", "}": "{"}
OPEN = set("([{")
# The nesting limit of the C and JavaScript VERIFY.
MAX_MATCHER_DEPTH = 1000

_PERCENT = re.compile(r"%[0-9A-Fa-f]{2}")


def percent_decode(text, rounds=2):
    """Undo the URL transport encoding of a recorded payload. A form-encoded
    `+` is a space; the second round undoes the double encoding (%2527) common
    in the corpus. Text with no percent-escape is not URL-encoded."""
    if not _PERCENT.search(text):
        return text
    text = unquote_plus(text)
    for _ in range(rounds - 1):
        decoded = unquote(text)
        if decoded == text:
            break
        text = decoded
    return text


def canon(payload):
    """canon(v) of premise A3: the value as the host language reads it, with
    the transport decoding undone and Unicode normalized (NFKC). Every
    measurement of a payload uses this value."""
    return unicodedata.normalize("NFKC", percent_decode(payload))

# Contexts a matchertext host can delimit with a matcher pair (paper 4.7.1-4.7.4).
HOST = {"sql", "xpath_xquery", "nosql", "html_dom", "ldap"}
# Sinks that execute the contained value regardless of delimiting.
# An LLM acts on the instructions in a prompt however the text is delimited.
SEMANTIC = {"template", "expression_language", "code_eval", "formula_csv", "llm_prompt"}
# Contexts whose structural delimiters are non-matchers, with no matcher hosting.
NONMATCHER = {
    "shell_command": "breakout via shell metacharacters (; | &), which are not matchers",
    "crlf_header": "breakout via CR/LF line breaks, which are not matcher characters",
    "xml": "structure delimited by < > angle brackets, excluded from the matcher set",
    "argument": "breakout via option-flag delimiters, which are not matchers",
}
# The only syntax whose verdict reads the payload; elsewhere the syntax label
# alone decides whether a payload is contained.
PAYLOAD_DEPENDENT = {"html_dom"}
# First token of the skeleton of an HTML/DOM payload that runs in an execution
# context. No payload yields it: the skeleton writes "<" as a token of its own.
SINK = "<sink>"

# An HTML/DOM payload runs in an execution context, not a structural slot, when
# it is a client-side template expression, or, with no markup or attribute of
# its own, a script URL or JavaScript code (a string breakout or a handler
# body). A hole contains the markup, not the script that later reads the value.
_TEMPLATE_EXPR = re.compile(r"\{\{[^{}]*[(.*+\[][^{}]*\}\}")
_ATTRIBUTE = re.compile(r"[\s\"'/][\w-]+\s*=")
_SCRIPT_URL = re.compile(
    r"(?:java|vb)script:|data:(?:text/html|image/svg\+xml|\w+/(?:x-)?javascript)", re.I)
_LEADING_SCRIPT_URL = re.compile(r"\s*(?:java|vb)script:", re.I)
_JS_CODE = re.compile(
    r"(?:^|[;+\-*,|&(])\s*(?:alert|prompt|confirm|eval|fetch|import|setTimeout|setInterval"
    r"|Function|document\.|window\.|top\.|parent\.|self\.|location\b|\$\.)|\$\{", re.I)


def html_sink(payload):
    """Does this canonical HTML/DOM payload run in an execution context?
    Character references are decoded, and tab and line breaks dropped, as a
    browser does before it reads a URL scheme."""
    text = re.sub(r"[\t\n\r]", "", html.unescape(payload))
    if _TEMPLATE_EXPR.search(text) or _LEADING_SCRIPT_URL.match(text):
        return True
    if "<" in text or _ATTRIBUTE.search(text):
        return False
    return bool(_SCRIPT_URL.search(text) or _JS_CODE.search(text))

MECHANISM = {
    "inert": "The payload is valid matchertext, so it embeds verbatim in the "
             "matcher-delimited slot and the character that drove the breakout "
             "(a nonmatcher such as the quote or `<`) is ordinary data read to "
             "the matched close (paper §4.7.1–§4.7.2).",
    "reject": "The payload carries an unmatched matcher — the very delimiter it "
              "would use to close the slot — so it is not valid matchertext and "
              "VERIFY (Algorithm 1) rejects it, or ToMatchertext escapes it to "
              "inert data (paper §4.7.3–§4.7.4).",
}


def verify(skeleton):
    """VERIFY (Algorithm 1): is the skeleton valid matchertext? Matchers must match."""
    stack = []
    for t in skeleton.split():
        if t in OPEN:
            stack.append(t)
            if len(stack) > MAX_MATCHER_DEPTH:
                return False
        elif t in PAIR:
            if not stack or stack.pop() != PAIR[t]:
                return False
    return not stack


def assess(syntax, skeleton):
    """Return (preventable: bool, mechanism: 'inert'|'reject'|'', explanation)."""
    if syntax in HOST and not skeleton.startswith(SINK + " "):
        mech = "inert" if verify(skeleton) else "reject"
        return True, mech, MECHANISM[mech]
    return False, "", ""
