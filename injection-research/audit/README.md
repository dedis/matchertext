# Label audit

300 injection CVEs, 75 per labeling layer (CWE map, phrase rules, naive Bayes,
unknown), checked by two raters. `sample.csv` holds the frozen sample and the
pipeline labels; do not open it before you finish labeling.

## Files

- `rater_a.csv`, `rater_b.csv`: one per rater. Fill `injection`, `syntax`,
  and optionally `notes`. The order is shuffled and carries no layer or label.
- `gold.csv`: settled labels, created by `python3 pipeline/audit.py gold`.
- `sample.csv`: frozen sample (`cve_id`, `method`, `syntax_type`).
- `strata.csv`: CVEs per layer when the sample was drawn. `report` scores each
  sampled CVE against its current label and weights it by these sizes, so the
  audit stays valid after the classifier changes.

## Rules

Label each row alone, from `description`. Open
`https://nvd.nist.gov/vuln/detail/<cve_id>` only if the description is too vague.
Do not discuss rows with the other rater until both sheets are complete.

**`injection`**: `yes` if attacker-controlled text is parsed or run as syntax by
another language or interpreter. `no` otherwise: memory corruption, kernel bugs,
path traversal, IDOR or missing authorization, SSRF, open redirect, plain
deserialization, crashes.

**`syntax`**: empty when `injection` is `no`. Otherwise the language that first
parses the attacker's text:

| Value | Text lands in |
|---|---|
| `sql` | an SQL or HQL query |
| `nosql` | a MongoDB-style query or operator |
| `ldap` | an LDAP filter |
| `xpath_xquery` | an XPath or XQuery expression |
| `html_dom` | a page or DOM (any XSS, HTML injection) |
| `shell_command` | an OS command run through a shell |
| `argument` | options or arguments of a program run without a shell |
| `crlf_header` | an HTTP header or response through CR/LF |
| `template` | a server-side template (Jinja, Twig, FreeMarker, Velocity, shortcodes) |
| `expression_language` | OGNL, SpEL, JSP EL, MVEL |
| `code_eval` | `eval`/`exec` of PHP, Python, JavaScript, or Ruby |
| `xml` | an XML document (XML injection, XXE) |
| `formula_csv` | a spreadsheet formula |
| `llm_prompt` | an LLM prompt |
| `other` | an injection into a language not listed (AT commands, SMTP, format strings, regex, JNDI) |
| `unclear` | the description does not say what is injected |

The first five count as matcher-delimitable. If you are unsure between two
values, the side of that boundary matters most.

- Label where the text is first parsed, not the effect: "SQL injection leading
  to code execution" is `sql`.
- Note in `notes` an XSS where the attacker supplies a whole file (SVG,
  Markdown, HTML upload) rather than a value inside a page.

## After labeling

```sh
python3 pipeline/audit.py gold     # agreed rows filled, disagreements blank
# settle each blank row of gold.csv together
python3 pipeline/audit.py report   # kappa, per-layer precision, adjusted ceiling
python3 pipeline/latex.py          # quotes the results as \dataAudit* macros
```

A misspelled value stops `gold` and `report` with the file and line.
