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
formulas, `javascript:` URLs) or the context is delimited by non-matchers with
no matcher hosting (shell metacharacters, CR/LF, XML/XXE angle brackets).
"""
import re

PAIR = {")": "(", "]": "[", "}": "{"}
OPEN = set("([{")

# Contexts a matchertext host can delimit with a matcher pair (paper 4.7.1-4.7.4).
HOST = {"sql", "xpath_xquery", "nosql", "html_dom", "ldap"}
# Sinks that execute the contained value regardless of delimiting.
SEMANTIC = {"template", "expression_language", "code_eval", "formula_csv"}
# Contexts whose structural delimiters are non-matchers, with no matcher hosting.
NONMATCHER = {
    "shell_command": "breakout via shell metacharacters (; | &), which are not matchers",
    "crlf_header": "breakout via CR/LF line breaks, which are not matcher characters",
    "xml": "structure delimited by < > angle brackets, excluded from the matcher set",
    "argument": "breakout via option-flag delimiters, which are not matchers",
}
# javascript: URL lands in an execution context, not a structural slot.
_JS = re.compile(r"\bJAVASCRIPT\b", re.I)

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
        elif t in PAIR:
            if not stack or stack.pop() != PAIR[t]:
                return False
    return not stack


def assess(syntax, skeleton):
    """Return (preventable: bool, mechanism: 'inert'|'reject'|'', explanation)."""
    if syntax in HOST and not (syntax == "html_dom" and _JS.search(skeleton)):
        mech = "inert" if verify(skeleton) else "reject"
        return True, mech, MECHANISM[mech]
    return False, "", ""
