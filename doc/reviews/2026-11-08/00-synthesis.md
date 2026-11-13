# Constructive review synthesis and revision agenda

Manuscript: *Matchertext: Towards Verbatim Interlanguage Embedding*.
Reviewed source: `c81714bc3f74d3c2d0863f0153d401a879fa1267`, 8 November 2026.
See [review method and scope](README.md) and the five individual reports.

## Working assessment

The strongest submission is a language-design paper about a shared lexical
contract: fixed boundaries, guest-language independence, recursive verbatim
composition, and incremental host support. The elementary underlying mathematics
is appropriate to that contract. The submission needs to make its practical
value and empirical evidence equally precise.

The highest-leverage near-term work is to align the evaluation with what was
actually measured, expose the contract through a concrete example early, and
demonstrate one real implementation end to end. Adding more speculative syntax
extensions would contribute less than completing those three tasks.

This synthesis is a planning judgment from simulated reviews, not a prediction
of a program committee decision. No manuscript revisions, experiment reruns, or
proof rebuilds were performed. In particular, the checked-in analyzer's behavior
does not establish which analyzer revision generated the manuscript's numbers.

| Perspective | Provisional assessment | What would most improve the paper |
|---|---|---|
| Theory / semantics | Lean reject / substantial revision | Precise operational and representation contracts, valid measurements, and a focused composition comparison; harder mathematics is not required |
| Compilers / implementation | Weak reject | Demonstrate the existing Go integration and migration, accounting for lexical phases and special contexts |
| Empirical PL | Lean reject / substantial revision | Correct metric semantics and coverage, establish corpus/result provenance, and isolate the distinctive benefit |
| Language design | Lean reject / substantial revision | Show why the common three-pair convention is worth adopting relative to competent raw-string alternatives |
| Advanced-student readability | Suitable for guided discussion; not yet reliably standalone | Put the adoption conditions, operational trace, and source/value explanation before readers need them |

These are qualified research judgments, not estimated acceptance probabilities.
The individual reports include strengths, evidence boundaries, and specific
conditions that would change each assessment.

## Findings checked against the frozen source

### 1. Escape reduction needs an operational definition and provenance

The paper defines a toothpick as a single backslash and describes conversion
to an equivalent escapeless raw literal (`doc/eval.tex:59–69`). The current code
subtracts numeric/named encoding-escape backslashes before computing the totals
(`LLVM/src/Parser.cpp:518–525`). `RawRewrite::Counter::encoding` preserves those
escapes' source spelling (`LLVM/src/RawRewrite.cpp:131–136`), rather than decoding
their value for literal insertion. Conversion accounting is gated on extracted
source-body compliance (`Parser.cpp:527–538`); the separate decoded-content
holdability hint does not gate that accounting.

For example, the ordinary source body `\x28` has no matcher, but its decoded
value is an unmatched open parenthesis. Copying the source body into a raw
literal changes the value; copying the decoded value makes a non-matchertext
body. This is a representation distinction the method must address explicitly.
It does not imply a known error magnitude in the archived aggregate numbers.

**Repair:** distinguish source spelling, decoded value, literal insertion
eligibility, and retained escaping. Identify the exact code revision and inputs
behind the reported totals. Either recompute a semantic-preserving conversion
metric, or narrow the metric and all claims to the approximation actually
supported. Report numeric/binary data separately from textual embeddings.

**Completion criterion:** a reader can reproduce the before/after counts on a
small published example table; examples preserve their original values whenever
the method claims equivalent conversion; every headline percentage has a stated
denominator and identifiable provenance.

### 2. File compliance and nesting need names that match their measurements

The paper interprets 86.77% as file-level matchertext compliance
(`doc/eval.tex:82–85`). `GatherStatistics` instead sums violations in extracted
strings and comments (`LLVM/src/Parser.cpp:455–507`). It is not a matcher scan
of the whole source file. A file's independently noncompliant fragments can
balance globally; conversely, characters or constructs outside the extracted
fragments can break whole-source balance. Neither metric substitutes for the
other, and their difference need not have a fixed direction.

Likewise, matcher nesting in a sample (`doc/eval.tex:285–306`) measures bracket
structure, not the number of cross-language embedding layers. It is relevant
to scanning costs, but cannot alone establish how often multilevel language
embedding occurs.

**Repair:** rename the file metric to reflect fragment-level aggregation and
state the nesting proxy's limits. Add a separate direct whole-source scan if
the raw source remains accessible. Include diagnostic examples of both metrics.

**Completion criterion:** the methods and table captions permit no inference
that unmeasured whole-file or cross-language structure was directly observed.

### 3. Extraction coverage needs visible accounting

The current tree-sitter path returns success with an empty sample array for
oversized inputs, NUL-containing inputs, and timeouts
(`LLVM/src/TreeSitterParser.cpp:612–645`). Such paths need explicit skip/failure
counts rather than silently participating as files with no fragment violations.
The C/C++ extractor also reads a token beyond a string run, assigns it to `tok`,
then continues into another lexer read (`LLVM/src/Parser.cpp:401–443`). By static
inspection, this can discard a comment immediately following a string. Neither
effect's aggregate magnitude has been measured here.

**Repair:** distinguish successful extraction, genuine empty files, unsupported
inputs, skips, timeouts, and parse errors. Add targeted extraction fixtures for
comments following literals, comments between adjacent literals, literal kinds,
and malformed inputs. State whether character literals are in scope.

**Completion criterion:** explicit coverage denominators and passing targeted
fixtures, followed by sensitivity checks or appropriately narrowed conclusions.

### 4. The reader needs the host/guest contract earlier

The paper contains the essential qualifications, but the reader must assemble
them across the introduction, hosting section, and interpolation appendix.
Move an operational summary into the introduction:

- The host adds a region whose contents it treats literally, finding the end
  with matcher balance and validating matcher kinds.
- The inserted string must be matchertext in the representation being inserted.
- Hosts can add these regions while retaining conventional syntax elsewhere.
- Matcher balancing protects these regions' boundaries; conventional SQL
  quoting or active HTML contexts do not acquire that property merely because
  their input has balanced brackets.

Use one example showing source spelling, its value, the embedding boundary,
and a second level of embedding. Pair it with one counterexample requiring
adaptation, such as a value containing a lone closing bracket. This would help
both non-experts and specialist reviewers understand what is being claimed.

### 5. Useful implementation material is not yet part of the paper's evidence

`doc/impl.tex` describes Scheme and MinML. The repository also contains a Go
toolchain patch, acceptance fixtures, a rewriter, and an experiment harness in
`go/toolchain/`. The harness compares stock Go, unchanged code with patched Go,
and rewritten code with patched Go. The rewriter deliberately restricts actual
conversion more than the aggregate accounting; it checks decoded values and
excludes several special cases (`go/toolchain/mrewrite/main.go:1–15`).

**Repair:** audit and run this existing machinery on a small pinned corpus,
then report actual conversion coverage, preserved values, package/test outcomes,
limitations, and implementation surface. Include failures and exclusions.

**Completion criterion:** archived run results tied to toolchain, repository,
and patch revisions; explicit exclusions; unchanged-program compatibility and
converted-program behavior measured separately. A harness is a promising
starting point, not evidence that these outcomes have already been achieved.

### 6. A localized abstract configuration claim needs correction

The appendix says adding matcher pairs tightens the language
(`doc/appendix/configuration_variants.tex:23–31`). Adding the pair `('(', ']')`
to the standard pair relation admits `(]`, which the standard configuration
rejects. The opener and closer sets remain disjoint. Thus set inclusion on
pair relations alone does not imply the asserted language inclusion.

**Repair:** restrict the variation to introducing pairs over previously neutral
characters with suitable endpoint constraints, or define tightening directly
using language inclusion and prove the chosen sufficient condition. This is a
small generic-appendix correction; it does not refute the standard configuration
or the core closure and promised-input boundary results.

## Prioritized submission work

An additional provenance question cuts across the measurement repairs.
The collector defaults to nonarchived repositories from selected organizations,
including forks, rather than an explicit selection of their principal production
repositories (`LLVM/clone-orgs.sh`). The reviewed extraction path deduplicates
paths rather than content. No exact historical corpus manifest or global result
tree was present in the tracked export. This is not evidence that they are
absent from the separately hosted artifact, or that duplication materially
changes the results. It does require a ledger of what was actually collected,
an accurate population description, and a sensitivity analysis where practical.

Effort is an approximate focused person-day estimate, not elapsed runtime.
Tasks overlap; estimates must not simply be summed. Corpus access, compute,
dependency setup, and staffing may dominate. No deadline date is assumed.

| Priority | Concrete change | Likely value | Effort / dependency | Completion evidence |
|---|---|---|---|---|
| Submission-critical | Reconcile escape definitions, semantic equivalence, headline claims, and result provenance | Protects central empirical credibility | 1–2 days diagnosis/specification; 2–5+ for corrected analysis, plus corpus/runtime | Worked fixtures, documented denominators, attributable results; or narrowed claims |
| Submission-critical | Correct file-compliance and nesting interpretations | Removes unsupported inferences cheaply | 0.5–1 day wording; separate scan depends on retained sources | Revised definitions/captions and examples |
| Submission-critical | Audit extraction skips and token handling | Establishes measurement coverage | 1–3 days targeted fixes/fixtures; rerun cost separate | Skip/error counts and sensitivity evidence |
| Submission-critical | Locate and publish historical corpus/analyzer/model provenance; state the actual collection policy | Makes the large result auditable | 1–3 days if records exist; a new pinned replication is additional work | Exact identities and result hashes, or explicit limits on historical reproducibility |
| Submission-critical | Add early host/guest contract and one complete example | Improves comprehension and claim precision | 0.5–1 day | Reader can explain conditions, benefits, and failure case |
| Submission-critical | Repair configuration-variation statement and clarify promised-input boundary vs validation | Removes localized correctness ambiguity | 0.5–1 day | Valid inclusion condition and explicit validation contract |
| Valuable if feasible | Turn existing Go patch/harness into a reported case study | Stronger implementation and adoption evidence | 2–5+ days, patched toolchain and representative pinned projects | Conversion coverage and three-arm build/test outcomes |
| Valuable if feasible | Compare eligible textual values with native raw-string alternatives | Demonstrates marginal practical value | 2–4+ days after metric repair | Matched sample set, explicit delimiter policy, collision/adaptation counts |
| Valuable if feasible | Add stratified manual audit, deduplication/generated-code sensitivity, and organization-level summaries | Tests robustness beyond sheer corpus size | 2–5+ days depending on retained samples | Published sample protocol, uncertainty and sensitivity results |
| Valuable if feasible | Compare one shared matcher pair with all three on meaningful guest-text subsets | Tests a central design choice directly | 2–4 days if samples are available | Per-pair costs and migration examples; acknowledge the loss of delimiter choice |
| Valuable if feasible | Add category sample counts/rates and separate binary/encoding-heavy data | Makes tables interpretable | 0.5–2 days if aggregate inputs suffice | Denominators visible; no confusion between violating samples and matcher counts |
| Submission-critical | Resolve citations, source/build documentation, anonymity, cross-references, and layout | Completes reviewable submission package | 0.5–1.5 days after content settles | Fresh build and artifact instructions checked against actual contents |

If time is very short, prioritize accurate narrower claims over retaining a
large percentage whose interpretation remains unsettled. A smaller validated
experiment can be more persuasive than an expansive approximation presented
as equivalent rewriting. Keep the large descriptive compliance study if its
definitions and extraction coverage can be justified independently.

## Larger next-update work

| Project | Question it would answer | Scope and near-term precursor |
|---|---|---|
| Controlled usability study | Does the discipline improve editing accuracy, comprehension, or task completion? | Weeks to months including study design; now prepare tasks and hypotheses, without treating simulated readers as participants |
| Multiple host implementations and parser integration | Does the same contract survive different lexical pipelines and tooling ecosystems? | Several weeks or more; now finish and document one host |
| Stronger machine-checked connection to executable recognition | Does an executable validator/scanner implement the abstract language and boundary contract? | Several days to weeks depending on proof scope; now state which results are mechanized and which are paper proof sketches |
| Typed matchertext values and checked interpolation | How can programs maintain and compose compliance with usable error handling? | Several weeks of design and implementation; now clarify distinctions among literal syntax, values, templates, and receiver contexts |
| Cross-language security experiments | Which attacks are prevented in matcher-aware receiving contexts, and which remain semantic or context-specific? | Separate substantial project; reuse the injection study selectively with explicit provenance, rather than implying this paper already establishes those results |
| Pinned reproducible corpus and validated classification benchmark | How stable and representative are the descriptive findings? | Weeks plus storage/compute; now preserve the exact available revisions, logs, exclusions, samples, and generated tables |

## How to use the individual reviews

Keep substantive tensions visible. The theory perspective need not demand more
advanced mathematics to make a language-design contribution; the implementation
perspective can still reasonably demand evidence that real lexical pipelines
honor the contract. Similarly, a readable explanation and a valid quantitative
measurement are separate requirements: improving either does not resolve the
other.

There are real differences in priority, even where the diagnoses overlap.
The compiler reviewer treats a successful Go case study as submission-critical;
the theory reviewer regards precise claims and a small composition experiment
as sufficient near-term progress without a production compiler. The design
reviewer treats a competent raw-string baseline as critical; the empirical
reviewer puts correction and provenance of existing measurements first. The
empirical reviewer also gives deduplication and distribution checks a higher
priority than this synthesis's conditional experiment list. These differences
should guide a budget decision rather than become a requirement to complete
every proposed experiment.

My recommended order is: (1) representation/measurement specification and
provenance triage, (2) early contract/example rewrite and inexpensive correctness
repairs, (3) one demonstrated host/migration/composition case with an appropriate
raw-string comparison, and (4) robustness analyses sized to the remaining time.
If step 1 cannot recover the historical evidence, choose a smaller pinned
replication or substantially narrower empirical claims before investing in
additional features.

Treat proposed experiments as options chosen for evidential value and budget,
not a checklist requiring all five reviewers' suggestions. Start with the
measurement specification and contract/example rewrite, then choose the smallest
implementation and comparison experiments that address the remaining central
claims. Expand syntax variants and broad usability/security studies in the
next update once the core evidence is stable.
