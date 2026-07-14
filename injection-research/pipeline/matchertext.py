"""Matchertext-prevention analysis of a syntactic skeleton (paper section 4.7).

For a skeleton we (1) assume the host language adopts a matchertext syntax that
delimits the untrusted value with a matcher pair, (2) rewrite the skeleton into
that matchertext-equivalent form, running any unmatched matcher through
ToMatchertext (the paper's \\o() / \\c() escapes), and (3) decide whether the
attack still breaks out.

Matchertext prevents the attack when the value sits in a matcher-delimited hole
and the host reads it to matcher balance: a non-matcher breakout (the quote in
' OR '1'='1) becomes inert text, and a matcher breakout (LDAP's unmatched `)`)
is escaped. It does NOT prevent the attack when the sink executes the contained
value by design (templates, expression languages, eval, spreadsheet formulas,
javascript: URLs) or when the context is delimited by non-matchers with no
matcher hosting (shell metacharacters, CR/LF, XML/XXE angle brackets, option
flags). Those return an empty prevention note.
"""
import re

ESC = {"(": r"\o()", ")": r"\c()", "[": r"\o[]", "]": r"\c[]", "{": r"\o{}", "}": r"\c{}"}
PAIR = {")": "(", "]": "[", "}": "{"}
OPEN = set("([{")

# Assumed matchertext hosting per syntax: (opening context, closing delimiter).
HOST = {
    "sql": ("col = [", "]"),
    "xpath_xquery": ("node[", "]"),
    "nosql": ("{ field: [", "]"),
    "html_dom": ("<div [", "]>"),
    "ldap": ("( uid =", ")"),
}
PREVENT = {
    "sql": "Value hosted in a matcher pair `[…]`; whatever would end the value (a "
           "closing quote, or the surrounding query syntax in a numeric context) is now "
           "ordinary text inside the matched region, so the injected clause cannot escape "
           "— the parser reads to the matched `]`, and any `]` in the value is escaped by "
           "ToMatchertext.",
    "xpath_xquery": "Value hosted in a matcher pair `[…]`; the quote closing the XPath "
                    "literal is inert inside the matched region, so the injected "
                    "predicate cannot break out.",
    "nosql": "Value hosted in a matcher pair `[…]`; the injected operator object is "
             "read as inert data to the matched `]`, so it cannot restructure the query.",
    "html_dom": "Element content/attribute hosted in a matcher pair `<div [ … ]>`; the "
                "injected `<script>`/tag is verbatim inert text read to the matched `]`, "
                "never parsed as markup, and an unmatched `]` is escaped.",
    "ldap": "LDAP filters already delimit the value with `()` matchers; the injected "
            "unmatched `)` that closes the assertion is not valid matchertext and is "
            "escaped (RFC 4515 `\\29`), so it stays inside its `(uid=…)` slot.",
}

# javascript: scheme in an XSS payload lands in an execution context, not a
# structural breakout, so matcher-containment cannot stop it.
_SEMANTIC = re.compile(r"\bJAVASCRIPT\b\s*:", re.I)


def escape_unmatched(skeleton):
    """Run ToMatchertext over a space-joined skeleton: escape unmatched matchers."""
    toks = skeleton.split(" ")
    stack, unmatched = [], set()
    for i, t in enumerate(toks):
        if t in OPEN:
            stack.append(i)
        elif t in PAIR:
            if stack and toks[stack[-1]] == PAIR[t]:
                stack.pop()
            else:
                unmatched.add(i)
    unmatched.update(stack)
    return " ".join(ESC[t] if i in unmatched else t for i, t in enumerate(toks))


def assess(syntax, skeleton):
    """Return (matchertext_rewrite, prevention_note); note is '' when not preventable."""
    rewritten = escape_unmatched(skeleton)
    if syntax in HOST and not (syntax == "html_dom" and _SEMANTIC.search(skeleton)):
        opening, closing = HOST[syntax]
        return f"{opening} {rewritten} {closing}", PREVENT[syntax]
    return rewritten, ""
