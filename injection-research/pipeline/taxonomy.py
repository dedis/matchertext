"""Injection taxonomy: CWE anchors, phrase rules, and label vocabularies.

Labels follow the two-axis schema from deep-research-report.md:
weakness family (CWE-based) and injection syntax type (embedded language).
"""
import re

QUERY = "query_injection"
COMMAND = "command_injection"
RENDERING = "rendering_injection"
CODE = "code_or_expression_injection"
OTHER = "other_or_ambiguous_injection"

FAMILIES = (QUERY, COMMAND, RENDERING, CODE, OTHER)

SYNTAXES = ("sql", "nosql", "ldap", "xpath_xquery", "shell_command", "argument",
            "html_dom", "crlf_header", "template", "expression_language",
            "code_eval", "xml", "formula_csv", "llm_prompt", "unknown")

# Nearest labeled ancestor in the CWE-1000 ChildOf hierarchy decides the label.
FAMILY_ANCHORS = {
    89: QUERY, 564: QUERY, 943: QUERY, 90: QUERY, 643: QUERY, 652: QUERY,
    77: COMMAND, 78: COMMAND, 88: COMMAND,
    79: RENDERING, 93: RENDERING, 113: RENDERING,
    94: CODE, 95: CODE, 96: CODE, 917: CODE, 1336: CODE,
    # Both are children of CWE-77 that are not command injection: an
    # executable regular expression is code evaluation, and LLM prompt
    # injection has its own syntax. Without these anchors both resolve to 77.
    624: CODE, 1427: OTHER,
    74: OTHER, 75: OTHER, 91: OTHER, 1236: OTHER,
}

SYNTAX_ANCHORS = {
    89: "sql", 564: "sql",
    90: "ldap", 643: "xpath_xquery", 652: "xpath_xquery",
    77: "shell_command", 78: "shell_command", 88: "argument",
    79: "html_dom", 93: "crlf_header", 113: "crlf_header",
    917: "expression_language", 1336: "template",
    94: "code_eval", 95: "code_eval", 96: "code_eval", 624: "code_eval",
    91: "xml", 1236: "formula_csv", 1427: "llm_prompt",
}

FAMILY_OF_SYNTAX = {
    "sql": QUERY, "nosql": QUERY, "ldap": QUERY, "xpath_xquery": QUERY,
    "shell_command": COMMAND, "argument": COMMAND,
    "html_dom": RENDERING, "crlf_header": RENDERING,
    "template": CODE, "expression_language": CODE, "code_eval": CODE,
    "xml": OTHER, "formula_csv": OTHER, "llm_prompt": OTHER,
}

# CWEs without their own anchor, with the (family, family_is_generic, syntax)
# they inherit, as reviewed against CWE v5.0. classify.py stops when a CWE with
# CVEs inherits labels not listed here, so a CWE that is new or moved in the
# catalogue is reviewed before it is counted.
REVIEWED_INHERITED = {
    # CWE-79 children: XSS variants.
    **dict.fromkeys((80, 81, 82, 83, 84, 85, 86, 87), (RENDERING, False, "html_dom")),
    # CWE-96 child: server-side include directives that the server evaluates.
    97: (CODE, False, "code_eval"),
    # CWE-75 child: no embedded language is named.
    76: (OTHER, True, None),
}

# CWE-99 (resource injection) and its descendants are not injection: the value
# names a resource, a file, an identifier, or a variable, and enters no embedded
# language. They carry no labels and do not admit a CVE to the corpus.
NOT_INJECTION = {99, 102, 462, 621, 627, 641, 694, 914}

# NVD long gave CWE-94 to memory corruption that ends in code execution. A
# code-eval label from a CWE alone is dropped when the description names memory
# corruption and no phrase rule names an injection.
MEMORY_CORRUPTION = re.compile(
    r"\b(?:memory corruption|corrupt(?:s|ion of)? memory|(?:buffer|heap|stack|integer) overflow"
    r"|use[- ]after[- ]free|double free|out[- ]of[- ]bounds|dangling pointer)\b", re.I)

# A language model in the attack path: the attacker writes a prompt, and the
# model writes the host syntax. A matcher boundary cannot confine what a model
# does with the instructions in a value (premise A4), so a CVE of a delimitable
# syntax whose description says this gets the llm_prompt label instead.
LLM_MEDIATED = re.compile(
    r"\bprompt[- ]injection\b|\btext-to-sql\b"
    r"|\b(?:LLM|AI|model)[- ](?:generated|produced|written)\b"
    r"|\b(?:outputs?|responses?) (?:of|from) (?:the |an? )?(?:underlying )?"
    r"(?:LLM|large language model|language model|AI model)s?\b"
    r"|\b(?:LLM|language model)(?:'s|s')? (?:outputs?|responses?)\b"
    r"|\binstruct\w* the (?:underlying )?(?:LLM|large language model|language model|model|AI agent|agent)\b",
    re.I)

# An HTML/DOM CVE whose attacker supplies a whole document, an uploaded SVG,
# HTML, XML, PDF, or Markdown file, rather than a value placed in a page. No
# hole receives the value (premise A2). A file name is a value, so it does not
# count.
_WHOLE_DOCUMENT = re.compile(
    r"\b(?:upload\w*|attach\w*)\b[^.]{0,80}?(?:\bsvgs?\b|\.svg\b"
    r"|\b(?:x?html?|xml|pdf|markdown)\s+(?:files?|documents?|pages?)\b|\.(?:x?html?|xml|pdf|md)\b)"
    r"|(?:\bsvgs?\b|\b(?:x?html?|xml|pdf|markdown)\s+(?:files?|documents?|pages?)\b)[^.]{0,60}?\bupload",
    re.I | re.S)
_FILE_NAME = re.compile(r"\bfile ?names?\b", re.I)


def whole_document(description):
    return bool(_WHOLE_DOCUMENT.search(description)) and not _FILE_NAME.search(description)


# CWE-74 itself and CWE-75 carry no syntax information; a family derived only
# from these may be refined by phrase rules or the NB classifier.
GENERIC_ANCHORS = {74, 75}

# Ordered phrase rules over descriptions; first match wins.
_RULES = [
    (r"\bsql injection\b|\bsqli\b", QUERY, "sql"),
    (r"\bnosql injection\b|\bmongo(?:db)? injection\b", QUERY, "nosql"),
    (r"\bldap injection\b", QUERY, "ldap"),
    (r"\bxpath injection\b|\bxquery injection\b", QUERY, "xpath_xquery"),
    (r"\bprompt injection\b", OTHER, "llm_prompt"),
    (r"\bcross[- ]site scripting\b|\bxss\b", RENDERING, "html_dom"),
    (r"\bhtml injection\b", RENDERING, "html_dom"),
    (r"\b(?:os |shell |operating[- ]system )?command injection\b", COMMAND, "shell_command"),
    (r"\bargument injection\b", COMMAND, "argument"),
    (r"\bcrlf injection\b|\bresponse splitting\b|\bheader (?:injection|splitting)\b", RENDERING, "crlf_header"),
    (r"\b(?:server[- ]side )?template injection\b|\bssti\b", CODE, "template"),
    (r"\bexpression language injection\b|\b(?:el|ognl|spel) injection\b", CODE, "expression_language"),
    (r"\b(?:csv|formula) injection\b", OTHER, "formula_csv"),
    (r"\bxml injection\b", OTHER, "xml"),
    (r"\b(?:code|eval) injection\b", CODE, "code_eval"),
]
RULES = [(re.compile(p, re.I), f, s) for p, f, s in _RULES]


def rule_match(text):
    for rx, fam, syn in RULES:
        if rx.search(text):
            return fam, syn
    return None


def mask_rules(text):
    """Blank every phrase a rule matches. The classifier is applied only to
    descriptions that no rule matches, so it must learn without them."""
    for rx, _, _ in RULES:
        text = rx.sub(" ", text)
    return text


def nearest_anchors(cwe, parents, anchors, max_depth=10):
    """CWE ids of the closest anchors reachable by walking up ChildOf edges."""
    frontier, seen = [cwe], {cwe}
    for _ in range(max_depth + 1):
        hits = sorted(c for c in frontier if c in anchors)
        if hits:
            return hits
        frontier = [p for c in frontier for p in parents.get(c, ()) if p not in seen]
        if not frontier:
            return []
        seen.update(frontier)
    return []


def labels_from_cwes(cwes, parents):
    """Map a CVE's CWE ids (in priority order) to (family, family_is_generic, syntax)."""
    fam, generic, syn = None, False, None
    for cwe in cwes:
        if cwe in NOT_INJECTION:
            continue
        if fam is None or (fam == OTHER and generic):
            hits = nearest_anchors(cwe, parents, FAMILY_ANCHORS)
            specific = [h for h in hits if FAMILY_ANCHORS[h] != OTHER]
            if specific:
                fam, generic = FAMILY_ANCHORS[specific[0]], False
            elif hits:
                fam, generic = OTHER, all(h in GENERIC_ANCHORS for h in hits)
        if syn is None:
            hits = nearest_anchors(cwe, parents, SYNTAX_ANCHORS)
            if hits:
                syn = SYNTAX_ANCHORS[hits[0]]
        if syn is not None and fam is not None and not generic:
            break
    return fam, generic, syn
