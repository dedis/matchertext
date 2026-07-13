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
            "code_eval", "xml", "formula_csv", "unknown")

# Nearest labeled ancestor in the CWE-1000 ChildOf hierarchy decides the label.
FAMILY_ANCHORS = {
    89: QUERY, 564: QUERY, 943: QUERY, 90: QUERY, 643: QUERY, 652: QUERY,
    77: COMMAND, 78: COMMAND, 88: COMMAND,
    79: RENDERING, 93: RENDERING, 113: RENDERING,
    94: CODE, 95: CODE, 96: CODE, 917: CODE, 1336: CODE,
    74: OTHER, 75: OTHER, 91: OTHER, 99: OTHER, 1236: OTHER,
}

SYNTAX_ANCHORS = {
    89: "sql", 564: "sql",
    90: "ldap", 643: "xpath_xquery", 652: "xpath_xquery",
    77: "shell_command", 78: "shell_command", 88: "argument",
    79: "html_dom", 93: "crlf_header", 113: "crlf_header",
    917: "expression_language", 1336: "template",
    94: "code_eval", 95: "code_eval", 96: "code_eval",
    91: "xml", 1236: "formula_csv",
}

FAMILY_OF_SYNTAX = {
    "sql": QUERY, "nosql": QUERY, "ldap": QUERY, "xpath_xquery": QUERY,
    "shell_command": COMMAND, "argument": COMMAND,
    "html_dom": RENDERING, "crlf_header": RENDERING,
    "template": CODE, "expression_language": CODE, "code_eval": CODE,
    "xml": OTHER, "formula_csv": OTHER,
}

# CWE-74 itself and CWE-75 carry no syntax information; a family derived only
# from these may be refined by phrase rules or the NB classifier.
GENERIC_ANCHORS = {74, 75}

# Ordered phrase rules over descriptions; first match wins.
_RULES = [
    (r"\bsql injection\b|\bsqli\b", QUERY, "sql"),
    (r"\bnosql injection\b|\bmongo(?:db)? injection\b", QUERY, "nosql"),
    (r"\bldap injection\b", QUERY, "ldap"),
    (r"\bxpath injection\b|\bxquery injection\b", QUERY, "xpath_xquery"),
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
