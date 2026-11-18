# Prose review fixes (session 2026-11-18)

Constraint: body ends p13, PDF <= 18 pages (baseline: 18 pages). Abstract and title frozen.
Build with `make` in doc/ only.

- [x] intro.tex (defence, since, collection, cite, free, at most, browsers)
- [x] motivation.tex (hazards, largest, garbled scope, sanitizer logic, concat study, unsourced, dangling, DOMPurify, contrast, figure + paragraph, mXSS dup, 28.28%, OWASP count)
- [x] relevance.tex (closure vs def of L, deeper ones, D caption, who checked what)
- [x] threat.tex (assumptions vs premises, merge models, closure repeat, figure pointer, A3 PDF + NFKC, L1 fragment + opener, scope list, garbled 129-130)
- [x] resist.tex (intro recall, quote causality, payload example, cannot embed, dashes, chain, esc tag, round-trip, schema storage, repetition, three contexts escaper, script/style, T-SQL choice, alias, It also, relocates, table PDF row, table A2 column)
- [x] data.tex (archived sources, trickest, forks)  [data.tex:9 finding was wrong: repeats are across CNAs]
- [x] classify.tex (gate sentence, CWE-99, tie rule, delivery rules, non sequitur, 280 graded, NB precision, ceiling vs audit)
- [x] results.tex (exploitation, 93% denominator, fragments, PoC payload, quote placeholder)
- [x] containment.tex (circularity, two measurements, tree caption, canon, LDAP, outside 52, ceiling, 99.8%, KEV, denominators, `[...]` choice, arms intro, NUL sentence, dialect variants, name oracle, printf grammar, value proposition, honest, repetition, compose, fixtures vs hosts, browsers receive guarded output, SJIS + stock SQLite, legacy wording, noscript, JS end)
- [x] pipeline/latex.py: add noscript to DomStock hosts; regenerate
- [x] limits.tex (expand)
- [x] related.tex (neither reaches alone, refused data repeat, asks for, It, subset, heading, move corpus defects)
- [x] concl.tex (limits + future, CVEs, threat model contains, at most)
- [x] embeded_boundary.tex (title / Lemma 1 location)
- [x] to_matchertext.tex (TCB item, two decisions prose)
- [x] systems.tex (enters as built, eleven variants, DOMParser, fail-stop, host -> fixture, test host)
- [x] sensitivity.tex (delivery rules pointer)
- [x] build with make; check 18 pages and conclusion on p13
