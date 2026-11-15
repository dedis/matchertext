# Matchertext: simulated advanced-student readability review

## Scope and reader

This is an independent, simulated reading from the perspective of an advanced bachelor's or early master's student: a competent programmer familiar with common languages, stacks, and basic algorithms, but without specialist knowledge of programming-language theory or Lean. It is not a user study, and the observations below do not establish how actual students will perform.

I read `doc/main.tex` and every included main section and appendix in the supplied frozen snapshot `/tmp/matchertext-review-20261108`, identified by the task as commit `c81714bc3f74d3c2d0863f0153d401a879fa1267`. Anchors below refer to that snapshot's source lines. I did not read implementation code, README explanations, other reviews, or the injection-research paper. I did not build the paper; observations concern the source-visible text, tables, algorithms, and figure captions. I cannot assess rendered figure legibility or page layout. Hidden `\xxx` and `\com` material is not counted as explanation available to a paper reader. No additional AGENTS.md files were found at the review destination or in the frozen snapshot.

## Readiness and confidence

The core idea is understandable and memorable for this reader. After the full paper, I can explain the balancing rule, implement the stack checker, and explain why a balanced payload cannot close its surrounding matcher. The first pass is less successful at explaining exactly which languages must change, which string representation is measured, and what security property follows. Those are central adoption and evidence questions, not merely difficult terminology.

I would call the paper **ready for a guided advanced-student discussion, but not yet reliably self-contained for an independent first reading**. Confidence is high in the observed ordering and explanation gaps, moderate in how widespread the resulting misunderstandings would be, and deliberately limited on empirical correctness and proof correctness: neither was independently audited here. A short explanatory revision could substantially improve readiness without a new language implementation or full user study.

## My reconstruction, in ordinary programming terms

Matchertext is a restriction on written text: ignore everything except the six ASCII characters `()[]{}`, and check that they form properly nested pairs. Quotes, comments, and backslashes do not switch this check off. Thus `"("` is not matchertext even though it is a valid string literal in many languages; `"\x28"` is matchertext as written even though evaluating it may produce an unmatched opening parenthesis. This difference between source spelling and resulting string value matters throughout the proposal.

A language can add a new kind of raw string, such as `M"(payload)"`. Inside its wrapper the host treats every character literally and uses matcher balance to find the end. The host does not need to understand whether the payload is SQL, a regular expression, or prose. The payload's own brackets can only close brackets opened inside the payload; the wrapper's closing parenthesis is the first closer left over after those internal pairs have matched. The host should also check matcher kinds, so a bracket cannot close a parenthesis.

The gain is that a payload which already satisfies the rule can be inserted unchanged into any host region implementing the same convention, and wrapped repeatedly without repeatedly escaping its internal characters. The surrounding wrappers still take space. The claim of no expansion concerns the inner payload, not zero extra delimiters or zero total growth of the composed text.

The prerequisites I understand are:

1. Participants agree on the character alphabet and matcher pairs. The concrete proposal uses Unicode characters and all three ASCII pairs.
2. The actual payload being inserted is valid matchertext. Being valid code in its original language is insufficient.
3. The receiving host provides an appropriate matcher-delimited region and suspends its own escaping, terminators, preprocessing transformations, and character bans inside it.
4. The program uses that region for the intended data. Merely adding balanced strings to ordinary SQL quotations or ordinary HTML does not supply the proposed boundary.
5. Runtime interpolation validates each inserted value or reports an error. Its output needs separate attention from the source spelling of its template.

A host can add such a region without requiring all of its old programs to become matchertext. Conversely, a source fragment can be prepared for embedding using existing numeric escapes without adopting a new host literal syntax. This independence is stated particularly clearly in `doc/host.tex:4–25`, but deserves to appear earlier.

Costs include rewriting unmatched punctuation in strings and comments, changing language processors and tools, handling invalid runtime values, and deciding what to do with non-text/binary values or source characters legacy tooling rejects. New visual escapes such as `\o[]` preserve the balancing rule in source while evaluating to one bracket. Those are optional convenience proposals; numeric escapes often provide an existing alternative. MinML is a larger syntax experiment, and the CHICKEN extension is a concrete host implementation. Neither means the displayed C/HTML proposals are accepted by today's compilers or browsers.

The corpus suggests substantial compatibility with the balancing discipline and a large potential reduction in backslashes under its conversion model. It does not demonstrate reduced editing time, reduced bugs, or a security outcome. The paper explicitly leaves those questions open.

## What works well

- The introduction starts from a recognizable task. The regex quoting example (`doc/intro.tex:21–26`) and repeated backslash example (`doc/bg.tex:25–47`) explain the problem using programming knowledge this reader already has.
- Positive and negative matchertext examples appear early (`doc/intro.tex:72–76`), including wrongly nested pairs. This prevents the mistaken interpretation that only counts need agree.
- The paper acknowledges the real cost of `printf("{")` and gives existing and proposed repairs (`doc/intro.tex:209–226`). That makes the restriction concrete rather than presenting a universal improvement without tradeoffs.
- The host/embedded-language split is excellent once reached. `doc/host.tex:21–25` makes incremental use plausible, and `doc/embed.tex:65–74` clearly explains why a backslash alone cannot neutralize a matcher.
- The stack algorithm (`doc/design.tex:196–244`) is accessible to the intended reader and provides an independently executable mental model. The formal material is much easier after it.
- Evaluation questions Q1–Q4, an explicit Unknown category, and the statement that classification does not affect the global metrics help separate kinds of evidence (`doc/eval.tex:8–38`). The file-level compliance figure usefully tempers the very high literal-level figure.
- The final discussion and conclusion explicitly leave usability and security effects open (`doc/eval.tex:326–330`, `doc/concl.tex:20–21`). The review should preserve that restraint rather than demand these effects have already been proved.

## Three most consequential understandability concerns

### 1. The adoption contract and security boundary need one early, end-to-end example

**Anchors:** `doc/intro.tex:38–66`, `doc/intro.tex:106–148`, `doc/bg.tex:198–221`, `doc/design.tex:242–248`, and `doc/rel.tex:49–52`. Relevant explanations supplied later: `doc/host.tex:4–25`, `doc/appendix/string_interpolation.tex:90–128`, especially lines 118–125.

**Where I get stuck:** The motivating examples span ordinary string literals, SQL injection, and HTML. The promise of copy/paste or concatenation can initially sound like “check bracket balance and then safely concatenate into today's SQL/HTML.” The background acknowledges continued sanitization, but a reader can still miss the exact condition that the *receiving* syntax must delimit the value by matchers. The interpolation appendix finally states it explicitly for SQL. That condition arrives too late for a limitation defining the main security story.

This is not a complaint that the paper omits the condition entirely. It is a complaint about where the reader learns it, and about the broad wording “requires only this passive linear check” in `doc/design.tex:246`. The linear check establishes the proposed boundary contract once the host implements it; it is not an explanation of all application validation or of safe behavior in legacy receivers. A perfectly balanced script can also be executable content if the application deliberately inserts it into an executable slot.

**Concrete repair:** Put a small “what must change” box after the first `M"(...)"` example. Give a three-column comparison: today's SQL string construction; a balanced but still ordinary quoted SQL construction; a hypothetical matchertext-aware receiver. Label the third as proposed syntax, not executable SQL today. Trace a quote-bearing value and a non-matchertext value through validation and show the resulting character sequence. Include a valid balanced payload containing a quote to demonstrate why matching alone is insufficient in the second case.

Suggested opening wording:

> A balance check protects an embedding only when the receiving language uses a matcher-delimited data region. It does not make concatenation into an ordinary quoted SQL string safe. Our proposal requires a new receiving syntax as well as a valid matchertext value; it preserves that region's boundary, while application-specific meaning and permissions remain the application's responsibility.

Then distinguish “host support” from “making source embeddable” with two bullets. Reuse this example at interpolation rather than introducing the essential receiver condition there for the first time.

**Success criterion:** After the introduction/background, a reader can correctly predict all three cases: legacy quotes plus a balance check; a new matcher-delimited literal plus a valid value; and a new literal plus `Eve(`. They can name which language processor must change and what validation rejects, without consulting an appendix.

### 2. The practical reason the delimiter works is buried behind a large formal vocabulary

**Anchors:** `doc/design.tex:20–52`, `doc/design.tex:70–99`, `doc/design.tex:141–193`, and `doc/design.tex:253–272`. Existing helpful material: the intuition at `doc/design.tex:54–65`, the algorithm at lines 196–244, and the annotated Lean declarations in the first two appendices.

**Where I get stuck:** Before seeing a scan, the reader must manage an alphabet, a pair set, openers, closers, nonmatchers, a language of strings, a set of host/guest languages, two delimiter strings, inductive rules, and closure properties. The sentence “ignore any embedded instances of” delimiter strings is the surprising result; the surrounding notation asks the novice to believe it before giving them an easy way to test it. Lean's axioms are extra vocabulary at precisely that point.

The boundary section later supplies the crucial operational explanation, so this is another ordering problem. Its “depth counter ... or equivalently ... stack” wording also merits a qualifier: a counter finds a boundary for a payload already known to be valid, while a stack is needed to reject a wrong-kind closer. These are not interchangeable validators for arbitrary input.

**Concrete repair:** Begin the design section with a literal broken into prefix, payload, and suffix, then show a stack trace for a payload that itself contains the apparent terminator `)"`, for example a valid payload `(a)"b`. Show which internal opener consumes that `)` and which final `)` closes the wrapper. Follow with `([)]` to show why matching kinds matter. Move the scanner intuition before the abstract definition, or add a half-page informal bridge and keep the formal order. Introduce the alphabet/set generalization as optional precision after the three concrete pairs. Define `||` as concatenation at first use.

Suggested bridge:

> After the host reads `M"(`, it scans the payload with an empty stack. Every opener pushes its kind; every closer must match the top. A closer encountered with an empty stack marks the end of the payload. Quotes and backslashes do nothing to this scan. For valid matchertext, an internal closer can never reach outside the payload.

Add one sentence before the Lean material: these proofs establish balance preservation and the boundary property of the abstract strings, not correctness of every compiler extension, usability, or security of arbitrary applications. The paper already separates closure from boundary; retain that distinction.

**Success criterion:** A reader can hand-scan a literal containing its apparent closing delimiter and explain why it does not terminate early; they can also explain why checking total counts alone accepts invalid examples. They can state the two proved properties without reading Lean syntax.

### 3. The headline empirical benefit lacks a sufficiently explicit representation pipeline

**Anchors:** `doc/abs.tex:24–29`, `doc/eval.tex:21–38`, `doc/eval.tex:52–71`, `doc/eval.tex:212–228`, `doc/eval.tex:242–260`, and `doc/eval.tex:316–325`. Related preconditions: `doc/design.tex:282–289`, `doc/host.tex:153–163`, and the literal-input/value distinction in `doc/appendix/string_interpolation.tex:4–11`.

**Where I get stuck:** “Sample,” “compliant as written,” “equivalent raw string,” and “same toothpicks” leave me uncertain which actual character sequence gets checked and rewritten. Source spelling and resulting value can have different balance: `"\x28"` has balanced source spelling but yields `(`. Conversely, `"\\("` has an unmatched parenthesis in source and value. Numeric escapes can introduce brackets, NULs, non-graphical text, or language-specific binary values. Since the proposal operates on Unicode characters and insists on literal preservation, I need the conversion model before interpreting 94.78% as useful migration relief.

This does not establish that the analyzer is wrong; I have not read it. The paper must make the measurement reconstructible without that code. The regex example at lines 220–228 is helpful but only covers an uncomplicated value. The 84-million-backslash binary contribution is acknowledged at lines 218–219, and Python's limitation is explained later. Those admissions are valuable; their implications should inform the headline presentation. A reader should not infer that almost all human-facing embedding trouble or all source-size overhead disappears.

**Concrete repair:** Add a four-row methodology table with original literal source, decoded value shown in an unambiguous printable notation, sequence tested for compliance, proposed spelling or reason conversion is unavailable, and backslash counts before/after. Include the existing regex, `"\x28"`, a newline escape, and a short byte/binary example. State whether unchanged noncompliant literals stay in the overall denominator, whether counts include syntactic delimiters, how per-language escape semantics are decoded, and what an “equivalent” Unicode/raw representation means for byte strings. Report text-oriented results alongside binary-inclusive results if the existing outputs permit it. Make clear whether compliance is assessed before conversion, after conversion, or both.

Also rename Q4's measured quantity to **matcher nesting**, or explicitly separate it from nested language embedding: a deeply nested expression can be one guest language, and flat quoted strings can represent multiple language levels. `doc/eval.tex:295–309` reports matcher depth, not a direct count of how many parsers a character passes through.

**Success criterion:** Given the four examples, a reader can reproduce the inclusion decision and backslash count by hand. The abstract's percentage has a stated denominator and interpretation. The reader can explain what evidence supports potential migration benefit and what remains a usability hypothesis.

## Reading friction log, checked against later text

| First location | First-pass friction | What later text resolves | Remaining repair |
|---|---|---|---|
| `doc/abs.tex:8–18` | “Single rule” sounds like enough for an unchanged host. | `doc/host.tex:14–25` requires a new raw region and distinguishes host support from embeddability. | Add a brief host-contract clause to the abstract/introduction. |
| `doc/intro.tex:72–76` | Quotes visibly surround matchers, so I may expect the checker to understand string literals. | `doc/embed.tex:65–74` explicitly says it does not. | Say “including inside quotes and comments” next to the first definition. |
| `doc/intro.tex:109–148` | Two new syntaxes appear before I can decode their output. | `doc/host.tex:98–114` and 169–190 explain the alternatives. | Annotate one literal with exact input/value; use one primary syntax in the opening. |
| `doc/bg.tex:128–142` | “No expansion with increased depth” may sound like no wrapper cost. | `doc/design.tex:180–187` specifies no expansion of the inner string. | Use that narrower wording consistently. |
| `doc/design.tex:20–52` | Set and inductive notation interrupt the familiar bracket-checking idea. | Algorithm 1 is accessible and formal-language labels are not needed to run it. | Put the operational bridge first; label Dyck terminology as an identification, not a prerequisite. |
| `doc/design.tex:268–272` | Counter and stack appear equivalent even for invalid input. | Verification algorithm checks types. | Add “for an already valid payload”; specify malformed-input behavior. |
| `doc/host.tex:113–114` | “Reverts trigraphs, line splices” presumes compiler-phase knowledge. | No plain-language expansion later. | Explain that preprocessing must not alter the payload; give one `\u` example. |
| `doc/impl.tex:10–25` | “Egg,” “reader,” and “dispatch character” distract a non-Scheme reader. | Concrete `#M` conversion is useful. | Gloss “egg” as package and “reader” as source-to-expression parser; fix visible `XXX` citation. |
| `doc/eval.tex:56–58` | “Relaxed” could sound like another supported matchertext standard. | Later text treats it as a comparison metric. | State it is an analytical variant, not the proposed validity rule. |
| `doc/eval.tex:186–204` | Counts are hard to compare without category sample counts; most samples are unclassified. | Caption supplies the 27% classified coverage and unknown counts. | Add sample counts/rates and put Unknown in the table, not only the caption. |
| `doc/appendix/string_interpolation.tex:73–101` | Many delimiter variants arrive before one complete runtime execution. | Invalid `Eve(` example clarifies validation. | Keep one complete example first; move alternatives to a compact reference table. |
| `doc/appendix/uri.tex:152–259` | Long URI evolution discussion feels like a second design proposal. | Tightened configuration explains why universal copying may fail. | Add an appendix map: optional case studies, with a clear “standard vs restricted” label. |
| `doc/appendix/regular_expression.tex:81–92`, 111–123 | “Tolerable” compatibility conflict asks me to accept behavior changes without knowing their prevalence. | The character-class section honestly states changed meanings. | Label these as exploratory breaking changes, distinguish from conservative host extensions, and defer adoption recommendation pending evidence. |

### Glossary candidates

Keep this short and local rather than adding a theory tutorial:

| Term | Suggested explanation |
|---|---|
| Host / guest (embedded) language | The language containing a region / the language of the region's payload. Roles can change at the next nesting level. |
| Source spelling / string value | Characters written in the program / characters produced after the language interprets the literal. |
| Verbatim | The host copies payload characters unchanged; it adds its own surrounding delimiters. |
| Configuration | The agreed character set and the pairs everyone must balance. |
| Nonmatcher / neutral symbol | A character ignored by the balance checker, even if a language gives it special meaning. |
| Closure | Combining valid pieces by the specified operation produces another valid piece. |
| Inductive definition | Rules for building every valid string from simpler valid strings. |
| Dyck language | The formal-language name for properly nested bracket strings; ordinary characters can be ignored here. |
| Conservative extension | Previously valid programs retain their meanings. Marker choice can depend on the language. |
| Antiquotation | An expression hole in a quoted template, to be evaluated and inserted. |
| Toothpick | One backslash character; a counting unit, not necessarily an unnecessary escape. |

## Prioritized changes and projects

Effort estimates are rough person-days for someone familiar with the paper and artifact, not calendar commitments. The deadline is unknown. “Submission-critical” means necessary to interpret the current claims clearly, not a prediction about venue decisions. Avoid starting a large new experiment merely to satisfy this reader if there is insufficient time to complete it credibly.

| Priority | Concrete change | Expected impact | Rough effort | Prerequisites / feasibility |
|---|---|---|---|---|
| Submission-critical | Add the early host/guest contract and legacy/new SQL comparison from concern 1; align security wording in background, design, related work. | Prevents the most consequential incorrect use and interpretation. | 1–2 days | Author agreement on exact boundary claim; no new implementation required. |
| Submission-critical | Add source/value/conversion methodology table and define all metric denominators; qualify abstract if needed. | Makes the headline empirical claim interpretable and auditable. | 1–3 days | Someone must confirm actual analyzer behavior. If documentation exposes a mismatch, correction/rerun may take longer. |
| Submission-critical | Introduce one delimiter stack trace before abstract notation; distinguish validation from boundary finding. | Makes the core contribution independently understandable. | 0.5–1.5 days | Existing algorithm and examples suffice. |
| Submission-critical | Resolve visible placeholder citations (`doc/impl.tex:11`, `doc/eval.tex:26`); consistently label proposed versus implemented forms. | Removes avoidable doubt about maturity and what can be tried today. | 0.5–1 day | Bibliography/source-status check; no source-code rewrite needed. |
| Valuable-if-feasible | Add category sample counts, Unknown row, violation rates, and binary-excluded/text-oriented reduction. | Reveals practical tradeoffs hidden by aggregate size. | 1–3 days if existing outputs suffice | Available category/denominator data; do not silently exclude Unknown or conflate violations with violating samples. |
| Valuable-if-feasible | Add a compact implemented-feature matrix for CHICKEN/MinML and proposals, plus one copy/paste example and error diagnostic. | Gives students a concrete starting point and limits accidental assumptions about support. | 1–2 days | Maintainer verifies features; preserve distinction between published implementation and aspirations. |
| Valuable-if-feasible | Exercise a small example corpus against an existing implementation: internal apparent closers, wrong-kind pair, unmatched quote-bearing value, Unicode/NUL where supported. Publish expected versus actual outcomes. | Anchors syntax exposition to behavior and catches misleading examples. | 2–4 days | Existing implementation and reproducible runner; report unsupported forms instead of expanding scope. |
| Valuable-if-feasible | Conduct 3–5 short formative reading sessions with comparable students; ask them to trace a nested delimiter and predict the three security cases. | Tests whether the revised explanation fixes these specific confusions. | 3–5 days plus recruitment/permissions | Human-participant process appropriate to institution; report as formative, not proof of usability or security improvement. |
| Post-submission / next major update | Implement a small typed matchertext value API plus one actual receiver pipeline, explicit rejection/error paths, and round-trip tests. | Makes runtime composition and source/value differences tangible. | 10–20 days for a narrow prototype | Fixed text/encoding model and one supported host/receiver; production compiler/browser adoption is a larger project. |
| Post-submission / next major update | Build editor support that distinguishes host/guest regions, previews literal values, diagnoses the exact unmatched matcher, and assists conversion of lone matchers. | Addresses the real migration and debugging costs, not only delimiter syntax. | 15–30 days for one language/editor | Stable grammar, validated diagnostics, and a chosen implementation target. Existing MinML tooling may reduce effort. |
| Post-submission / next major update | Run a preregistered comparative editing/debugging study against conventional and modern raw strings, including raw-friendly and matcher-hostile tasks. Measure correctness, time, corrections, and invalid-value handling. | Directly addresses the presently open cognitive-load benefit. | 20–40 days plus recruitment/review time | Working comparable tools, pilot, power/design decisions, participant process; cannot credibly promise a near-deadline result. |
| Post-submission / next major update | Evaluate real multi-language applications with traceable context transitions and migration patches; separately assess boundary preservation and application-level behavior. | Tests the strongest practical promise while exposing adoption work beyond literal counts. | 20–40+ days | One deployed-style receiver implementation and carefully scoped tasks; corpus statistics alone cannot substitute. |

The highest-return immediate revision is a small number of concrete examples placed before the reader needs them. The mathematical and corpus sections already contain much of the needed material; the main work is to expose the contract and measurement model early enough that the reader interprets the rest correctly.
