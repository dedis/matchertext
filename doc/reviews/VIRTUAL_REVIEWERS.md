# Virtual PLDI 2027 Reviewers for the Matchertext Paper

Source: [PLDI 2027 Review Committee](https://pldi27.sigplan.org/track/pldi-2027-papers),
11 chairs + 140 members (151 names, checked against the page HTML).
Submission deadline: 12 Nov 2026 AoE.

Member research areas come from knowledge of their publications, not from a per-person check.
Line references are to files in `doc/`.

## 1. Research-field buckets (whole committee)

| Bucket | Theory ↔ practice | Representative members |
|---|---|---|
| Semantics, proofs, type theory | Theory | Sammler, Jacobs, Allain, de Vilhena, Dardinier, Kavvos, Vazou, Breitner, Norrish, Y.K. Tan, Lammich, Sewell, Yuting Wang |
| Automata, formal languages, verification | Theory | Madhusudan, Holík, Lengál, Silva, Seidl, Balasubramanian, Shankaranarayanan, Jonsson, Kobayashi, Goharshady |
| Static analysis, program analysis | Middle | Møller, Ryu, Jensen, Thakur, Schwarz, Qirun Zhang, Charles Zhang, Tian Tan, Yue Li, Heo |
| Security, language-based security | Middle | Zdancewic, Devriese, Stefan, Mitropoulos, Chida, Giuffrida, Duck, Hofmann |
| Language design, metaprogramming, multi-language | Middle | Ballantyne, Hemann, Thiemann, Patterson, Weisenburger, Montesi, Drossopoulou, Parkinson, Shaikhha, Brahmakshatriya |
| Compilers, runtimes, IR infrastructure | Practice | N. Lopes, Grosser, Fehr, Rastello, Sivaramakrishnan, Bruno, Podkopaev (JetBrains), Tardieu, Schkufza |
| Testing, empirical SE | Practice | Su (PC Chair), Sotiropoulos, Chaliasos, Legunsen, Shan Lu, C. Sun, Ting Su, Donaldson, Raychev, Fonseca |
| HPC, ML systems, sparse/tensor | Practice, out of area | Kjolstad, Hsu, Jia, Zhai, Mendis, Hall, Dehnavi, Bauer, Yadav |
| Synthesis, ML for code | Middle, out of area | Dillig, Xinyu Wang, Ruyi Ji, Xiong, Jingxuan He, Jocelyn Chen |
| Probabilistic, quantum | Theory, out of area | Holtzen, Zaiser, Gehr, Baudart, Amorim, Ross, Yu, Tao, Tannu, Wang Fang |

Verification/semantics and compilers/systems are the largest groups.
No bucket is about syntax or parsing.
Expert reviewers for this paper will come from automata, language design, static analysis, and security.

## 2. Personas and attack lists

Based on a full read of the paper and all appendices.
Language facts marked "checked" were verified with local `node`, `cc`, `c++ -std=c++23`, and `javac`.

Severity:
- **[Reject]**: attacks the core contribution or a headline number. Can cause rejection.
- **[Fix]**: affects the score, not the decision alone. Usually one sentence, one citation, or one paragraph.

### R1: Formal-language theorist
Draws from: Madhusudan, Holík/Lengál, Seidl, Qirun Zhang, Silva.
Values: precise definitions, novelty against known results.

1. **[Fix, done]** No mention of visibly pushdown or input-driven languages (Alur & Madhusudan; Mehlhorn). Matchertext in the standard configuration is a well-matched VPL. "Find the end by matcher balance alone" (`rel.tex:16-17`) is the input-driven property. Fix: cite and position.
2. **[Fix, done]** Definition 1 (`design.tex:141-150`) admits `(ε,ε)` and pairs with no matchers. The embedding rule (`design.tex:80-84`) forbids them. Theorem 1 does not formalize the rule it claims to justify. Fix: align the definition with the rule.
3. **[Fix, done]** Theorem 2 (`design.tex:261-266`) is about `m||c`, not `m||s_c`. Definition 1 allows `s_c = x)` with a leading nonmatcher. Because `m||x` is also matchertext, the host cannot split `m` from `s_c`. `design.tex:271-272` does not address this. Fix: require `s_c` to start with a closer.
4. **[Fix, done]** `rel.tex:9-11` says nothing is formally new, but `intro.tex:239-242` lists the Lean proofs as the first contribution. Theorem 1 and its lemmas are textbook Dyck closure facts. Fix: move the Lean bullet after the design and evaluation bullets.

### R2: Parser / compiler engineer
Draws from: Seidl, C. Sun, Y.K. Tan (CakeML parser), N. Lopes, Grosser, Fehr, Podkopaev.
Values: effect on real tokenizers and compilers.

1. **[Fix, done]** `appendix/grammar.tex:149-157` claims C and C++ code outside literals and comments is already matchertext. False: `#define BEGIN {`, `#if` branches that each open `{`, and digraphs like `a<:0]` compile today (checked with `cc`). Fix: restrict the claim or name the exceptions.
2. **[Fix, done]** Translation phases: C trigraphs `??)` (before C23), line splicing, and Java `\uXXXX` apply before lexing. Java `"x";` compiles (checked), so `)` would close an `M"(` literal. `design.tex:100-104` forbids host transformations. Fix: one sentence, "M-literals revert these phases as C++ raw strings do."
3. **[Fix, done]** The escapes are not conservative. In JavaScript today, `"\[a]"` is `"[a]"` and `"\o()"` is `"o()"` (checked with `node`). Clang compiles `"\["` as `"["` (checked). The paper names only the Python conflict (`host.tex:172-180`) and says these escapes are "technically not in conflict" (`embed.tex:94-96`). Partial defense already in the paper: languages may choose their own syntax (`host.tex:181-187`). Fix: name JavaScript and C as conflicts.
4. **[Reject, done]** The only implementation is a third-party CHICKEN Scheme extension that accepts parentheses only (`impl.tex:10-23`). No C, Go, Rust, or tree-sitter prototype. The appendix grammar is not implemented. Combines with R6.1. Fix: plan step 3.

### R3: Language designer / metaprogramming
Draws from: Ballantyne, Thiemann, Patterson, Hemann, Weisenburger.
Values: comparison with prior designs, generality.

1. **[Fix, done]** Related work misses:
   - Scribble @-expressions (balanced-brace text).
   - TeX brace-balanced arguments. TeX does not count `\{`; matchertext does (`embed.tex:69-74`).
   - Token trees and skeleton syntax: Rust `macro_rules`, Dylan D-expressions, Honu.
   - Language composition: Copper/Silver, Eco language boxes, SDF.
   - Haskell QuasiQuotes, Julia string macros, JS tagged templates, Python t-strings (PEP 750) for the interpolation appendix.
2. **[Reject]** The difference from Wyvern (`rel.tex:91-106`) is a standardization request, not a mechanism. Expect: "What is the PL contribution beyond Wyvern's rule applied to three pairs?" Combines with R6.1.

### R4: Security
Draws from: Møller, Mitropoulos, Chida, Stefan, Zdancewic, Devriese, Vasilakis.
Values: an explicit threat model.

1. **[Fix, done]** `appendix/string_interpolation.tex:112-126` described a value "interpolated into an SQL query between quotes". That read as legacy quote-delimited SQL, where `' OR '1'='1` still breaks out. The claim holds under the premises of the injection paper (`injection-research/doc/threat.tex`, A1 and A2): the receiving language delimits the hole with matchers, as in the SQLite fork's `M'(...)'`. Wording problem, not an overclaim. Reworded to state the premise (plan step 1).
2. **[Fix, done]** Parser differentials: `<code [...]>` and `attr=[...]` (`host.tex:322-336`, `host.tex:378-386`). A legacy HTML5 tokenizer reads `[` as an attribute name and ends the tag at the first `>` in the content (the `<b>` in Fig. `ml-emb`(a)). A sanitizer and a browser that disagree give mutation-XSS. Fix: one paragraph on coexistence with legacy parsers.

### R5: Empirical methodologist
Draws from: Su, Sotiropoulos, Chaliasos, Legunsen, Shan Lu, Raychev, Fonseca.
Values: sound sampling, threats to validity, an artifact.

1. **[Reject, done]** The rewrite to `R"(...)"` (`eval.tex:59-65`) counts escapes for non-printable characters and binary data as removed. Escaped binary data holds 84.0M of the 178.1M toothpicks (`eval.tex:207-208`). The JavaScript gain comes mostly from binary data (`eval.tex:243-245`). A raw `"\x00\xff"` needs a NUL byte and invalid UTF-8 in the source file. The headline 94.78% (abstract, `intro.tex:250`, `concl.tex:18`) includes these escapes. "84.0 million of these" is also ambiguous: total or removed. The analyzer also has defects that affect the same figures (see plan step 2). Fix: plan step 2.
2. **[Fix, done]** No baseline against existing raw strings (Go backticks, Python `r""`, Rust `r#""#`, C# `"""`, C++ `R"d(...)d"`). Matchertext literals extend and generalize raw strings, so the 94.78% is the raw-string benefit that matchertext extends to every compliant literal. The paper must say so and add the matchertext-specific number. Fix: plan step 2.
3. **[Fix, done]** Corpus (`eval.tex:39-47`): no criteria for selecting the 79 organizations, no deduplication, no filter for vendored, generated, minified, or fixture code. JavaScript gives 44% of samples, and its max depth of 11,777 points to bundles. Fix: threats-to-validity paragraph, or filter and rerun.
4. **[Fix, partly done]** Classifier (`eval.tex:29-34`): naive Bayes, no reported accuracy. The unclassified 73% of samples carry 6.24M of 8.97M violations (`eval.tex:190-193`), so Q2 explains about 30% of violations. The AI Use section says Claude wrote the classifier. Only Q2 depends on it (`eval.tex:35-37`). Fix: report accuracy on a labeled sample.
5. **[Fix, done]** `eval.tex:310-313` uses near-identical strict and relaxed rates to argue that the lone-matcher escapes fix violations. The relaxed rate was not measured for string literals (`eval.tex:107-109`). Equal rates say nothing about fix cost. Fix: rephrase.

### R6: Out-of-area PC member
Draws from: Kjolstad, Jia, Holtzen, Dillig, Jingxuan He.
Values: PLDI fit, a clear contribution in the first two pages.

1. **[Reject]** Expect a "position paper" verdict: "proposals for discussion" (`host.tex:69-71`), no committed design choices, usability and bug rate left open (`eval.tex:314-318`), formal part admitted not new (`rel.tex:9-11`), and the headline numbers measure existing code, not the proposal. Fix: plan step 3.

### R7: Web / markup practitioner (MinML)
Draws from: Møller, Thiemann, Ryu, Stefan.
Values: evidence for the usability claims.

1. **[Fix, done]** MinML has no evaluation (`impl.tex:25-83`), but the abstract calls it "concise but general". No size measurements, no round-trip results, attribute syntax not shown. Fix: tone down the abstract, or add a size and round-trip measurement.

## 3. Triage summary

**[Reject]**, ordered by cost to fix:

| Point | Cost to fix |
|---|---|
| R5.1: toothpick metric and analyzer defects | Analyzer fixes, then one full rerun (plan step 2) |
| R6.1, R3.2, R2.4: unclear PLDI contribution, no C-like implementation | One prototype and a rewrite experiment (plan step 3) |

**[Fix]**: R3.1, R4.2, R5.2-R5.5, R7.1. R1.1-R1.4, R2.1-R2.3, and R4.1 are done.

R1 fixes (2026-11-05): `rel.tex` cites VPLs and input-driven languages; Definition 1 now has base case `(o,c)` for each pair, so `s_o` ends with an opener and `s_c` begins with a closer; the embedding rule requires `s_c` to begin with a closer; the Lean bullet is last in the contributions. `lean/always_embeddable.lean` and `appendix/always_embeddability.tex` changed `AllMatch.nil` to `AllMatch.pair`; the Lean change is not compiled (no Lean toolchain locally). Body ends at line 979 (was 982 in HEAD): AI Use trimmed to 3 lines and the DSEL paragraph of `rel.tex` lost the aside "that ASCII matchers must match" and the word "programming".

R2 fixes (2026-11-05): `appendix/grammar.tex` names the C/C++ preprocessor and digraph exceptions; `host.tex` says M-literals revert trigraphs, line splices, and Java `\u` escapes like C++ raw strings, and lists JavaScript and C (GCC, Clang) as `\[m]` conflicts; `embed.tex` notes JavaScript reads `"\o()"` as `"o()"`. Body still ends at line 979.

## 4. Plan

### Step 1: reword the SQL interpolation claim (done)
- `doc/appendix/string_interpolation.tex:118-126` now states that the receiving language must also delimit the hole with matchers, as in `M'(...)'`.
- Optional: cite the injection work as anonymized concurrent work.
- Fix two misstatements of this paper in the injection paper:
  - `injection-research/doc/threat.tex:136` says Verify is machine-checked in this paper. It is not: Theorem 3 (`design.tex:228-237`) has a proof sketch only, and `lean/` has no Verify.
  - `injection-research/doc/threat.tex:121` cites "Algorithm 2" of this paper. This paper has only Algorithm 1. The boundary locator is prose (`design.tex:268-272`).

### Step 2: fix the analyzer, then one full rerun (R5.1, R5.2)
Rewrite rule (agreed 2026-11-04):
- Removed: `\n` (a real line break), `\t`, `\"`, `\'`, and identity escapes such as `\[`.
- `\\` leaves one backslash.
- Kept, one toothpick each: simple escapes for characters a raw literal cannot hold: `\r`, `\a`, `\b`, `\f`, `\v`, `\e`.
- Not counted, before or after: encoding escapes stay as written (hex, `\u`/`\U` code points, octal and `\0`, `\N{...}`, Lua/OCaml/Haskell decimal, Haskell names, `\^X`, Ruby/Perl `\cX`, `\C-x`, `\M-x`). Example: `"caf\u00e9 \x41"` becomes `M"(caf\u00e9 \x41)"` with 0 toothpicks.

Analyzer fixes (all confirmed by building the analyzer and running one-literal fixtures, see "Verification" below):
1. C/C++ path: `CountRawStringToothpicks` (`LLVM/src/Parser.cpp:284-430`) keeps only escapes that decode to a backslash. Also keep the toothpick for escapes that decode to non-printable characters.
2. Tree-sitter path (all languages except C/C++, 90% of samples):
   - `TreeSitterParser.cpp:184-186` emits no `convertedToothpicks`, so `Parser.cpp:549-551` defaults it to 0. Every backslash in a compliant literal counts as removed, including `\\`. The JS (99.82%), Java (99.93%), and Go (99.66%) reductions come from this default. Fix: an escape decoder for the tree-sitter path.
   - `stringBody` (`TreeSitterParser.cpp:157-182`) concatenates only `content`/`fragment` children and drops sibling `escape_sequence` nodes. Toothpicks are undercounted in mixed strings, and compliance runs on text without its escapes.
3. In the same pass, add the item 3 metric: the share of compliant literals that the language's existing raw form cannot hold (backtick in Go, quote or trailing backslash in Python `r""`, `)"` in C++ `R"(`).
4. Build: `LLVM/extern/Serde/src/Log.cpp:46` uses `std::ranges::find` without `#include <algorithm>`. It does not compile with Homebrew LLVM 23.
5. Tests cover only C/C++ (`LLVM/tests/*.cpp`). No fixture covers a tree-sitter language.

Status: items 1 to 5 are done (`LLVM/src/RawRewrite.cpp`, `LLVM/tests/language_escapes_test.cpp`; `make test` passes 9 of 9). Item 3 adds the statistics "Raw-Form Holdable" and "Matchertext-Only" to `strings.md`. The escape rules were checked against primary sources and 10 local toolchains. Two more defects were found and fixed: the TypeScript `string` keyword token was emitted as a string sample, and OCaml dropped unknown escapes (`ERROR` nodes) from the body. The escape rules are listed for review before the rerun.

Verification before the fix (analyzer built in a scratch copy, one literal per file, values read from `strings.md`):

| Literal | Languages | Toothpicks | Converted | Correct converted |
|---|---|---|---|---|
| `"ab\\d+cd\x00"` | C, C++ | 3 | 1 | 2 |
| `"\\\\"` | C, C++ | 4 | 2 | 2 |
| `"\\\\"` | all 19 tree-sitter languages tested | 4 | 0 | 2 |
| `"ab\\d+cd\n"` | JS, TS, Go, Java, C#, Rust, Ruby, PHP, Elixir, Objective-C | 0 (sample `abd+cd`) | 0 | 1 |
| `"ab\\d+cd\n"` | Python, Swift, Kotlin, Scala, Dart, OCaml, Lua, Perl, Haskell, R | 3 | 0 | 1 |
| `"a\[b"` | JS, TS | compliant (sample `ab`) | | non-compliant, as in Python, C, C++ |

The languages that drop escapes hold 350.0M of the 469.6M string samples in Tab. `eval:hostlang` (75%), plus Ruby, PHP, Elixir, and Objective-C.

Then update every figure that depends on toothpicks or compliance: 94.78%, Tab. `eval:hostlang`, Tab. `eval:categories`, 84.0M binary-data toothpicks, the abstract, `intro.tex:250`, and `concl.tex:18`.

### Step 3: one prototype and a rewrite experiment (R2.4, R3.2, R6.1)
Status: Go prototype done in `go/toolchain/` (`matchertext.patch` against go1.27.1, `build.sh`, `mrewrite/`, `experiment.sh`, `testdata/`). Go's own tests for the patched packages pass, and the acceptance tests pass. The experiment run (build and test of external repositories) is not done: it needs permission to build and run code from cloned repositories. Rewrite counts without building, on five repositories, show that most toothpicks sit in literals that cannot be rewritten because one escape needs a non-holdable character; the analyzer still counts the other escapes of such literals as removed.

Pick one compiler:

| Target | Work | Estimate |
|---|---|---|
| Go (`cmd/compile/internal/syntax/scanner.go`, plus `go/scanner`) | `M"(` case next to the backtick raw-string scanner. `M"` is a syntax error today, so the change is conservative. | 100-200 lines and tests, 2-4 days. Builds in minutes. |
| Clang (`Lexer.cpp`, `LiteralSupport.cpp`) | Copy `LexRawStringLiteral`, which already reverts trigraphs and line splices. `\[m]` escape adds about 100 lines. | 200-400 lines, 1-2 weeks if new to Clang. First LLVM build 30-60 min. |

Go is the cheapest. Clang matches the paper's `R"(` model and the analyzer. A tree-sitter grammar alone is not needed: it is weak evidence at PLDI. Estimates are not measurements.

Checked facts:
- Go 1.27.1: `rawString()` (`cmd/compile/internal/syntax/scanner.go:706`) is 20 lines. `M"(x)"` is a syntax error today (`expected ';', found "(x)"`).
- Go spec: "Carriage return characters ('\r') inside raw string literals are discarded from the raw string value".
- Clang (`-std=c++14 -trigraphs`): `R"(a??)b\<newline>c)"` keeps `??)` and the line splice; the normal literal `"a??)b"` becomes `a]b`.
- Java: `"x";` compiles, so `\u` escapes apply before lexing.

Experiment:
1. Build some corpus repositories unchanged with the modified compiler. This shows the extension is conservative.
2. Rewrite their compliant literals to `M"(...)"` automatically.
3. Run their tests and count the toothpicks removed.

This measures the proposal, not only existing code, and answers the "position paper" verdict (R6.1).

## 5. Dropped points

Removed because they apply equally to any syntax extension, or have no real impact:
- An unclosed `M"(` consumes the rest of the file. Any unclosed string or bracket does the same.
- The lexer becomes non-regular. C++ raw strings, Lua long brackets, and Rust nested comments already require this.
- Encoding differentials, stack overflow on deep nesting, CRLF line endings.
- Existing XML parsers reject `<![MDATA[`. Every new syntax is rejected by old parsers.
- C++23 `\o{}`. It supports the paper, so it is not an attack.
- Lean proof of Theorem 3, unmentioned `lean/to_matchertext.lean`, pinned commit hashes, "adoption cost" wording, MinML bracket text, Haml/Pug citations, background length, Wikipedia links.

## 6. Not checked

- Bibliography, figures, and the rendered PDF.
- Fixtures covered typical escapes only. Language-specific forms (Rust `\u{...}`, Swift `\(...)`, Python byte strings) were not tested.
- The effect of the fixes on the published figures. That needs the full rerun.

## 7. Status 2026-11-10

- Step 2 done: full-conversion analyzer rerun; the paper's figures come from `doc/generated/numbers.tex` (`LLVM/latex.py`). The methodology states the normalization of control characters (R5.1).
- Step 3 done: Go experiment run on 4 repositories (`go/toolchain/results/`); §6.2 reports it and appendix H gives the details (R2.4). Contribution bullet and abstract mention it. R6.1 and R3.2 are answered by the same evidence, not by new text.
- R5.2: Q3 reports that the host's raw form could hold 81.0% of compliant literals and that 18.8% need a matchertext literal (`\dataRawHoldableShare`, `\dataMtOnlyShare`).
- Per-host-language table moved to appendix D to make room. Body ends at line 970.
- Item 2 fixes (2026-11-10): R3.1 (TeX, Scribble, Rust token trees, Honu, SDF, Silver, Eco, quasiquoters in `rel.tex`), R4.2 (legacy HTML5 tokenizer and mXSS paragraph in `host.tex`), R5.3 (corpus described as every non-archived repository, forks included; threats sentence in §7.6), R5.5 (strict/relaxed rephrased), R7.1 ("concise but general" removed from abstract and intro). Body ends at line 979.
- R5.4 (2026-11-10): the old holdout (4–15% top-1) was an evaluation bug: `classify_snippet_nb` lacked the runtime's matched-trigram and match-ratio gates, so the smallest model (Kotlin) won most snippets. Fixed in `LLVM/train/generate_model.py`, which now reports coverage and precision: naive Bayes labels 4.7% of 1,579 held-out Linguist snippets, 84.0% correctly. §7.1 says so in one line and states that the deterministic detectors are untested on labeled samples (still open).
- Item 3, synthesis of 2026-11-08 (2026-11-10):
  - File compliance renamed to what it measures (a file complies when all its literals and comments do; Table 3 row "Files (all fragments)" and caption); Q4 says "bracket nesting" instead of "embedding".
  - Extraction: C/C++ comments right after a string literal were dropped (fixed in `LLVM/src/Parser.cpp`, test `tests/comment_after_string.cpp`); tree-sitter files skipped as oversized, binary, or timed out were counted as compliant files with no samples (now left out of Sample Size and counted in `files.md`, test in `language_escapes_test.cpp`). `make test` passes 11 of 11. The paper's figures still come from the 2026-11-09 run; measuring the effect needs a rerun.
  - Provenance: `LLVM/CORPUS.md` (collection policy, dates, limits) and `LLVM/corpus-repositories.txt` (13,721 repositories). Commits were not recorded.
  - Host/guest contract: added to §3.1.1 (scope of the guarantee, nesting example, lone-closer counterexample). In the introduction, 3 lines cost 13 because the escape table moves to the next page.
  - Configuration variations: tightening now requires added pairs over standard nonmatchers, with the inclusion argument and the `(]` counterexample.
- 2026-11-10: merged with the co-author's commit 3034d7021b: tree-sitter and Clang cited (`brunsfeld26treesitter`, `llvm26clang`, `lattner04llvm`); CHICKEN cited (`chicken26scheme`) and the egg citation commented out until after review, because the egg's documentation names Bryan Ford and Antoine Bastide; no undefined citations. Injection paper: Verify no longer called machine-checked (`threat.tex`, `relevance.tex`), "Algorithm 2" replaced by "the depth-counting scan". Lean development builds (`lake build`).
