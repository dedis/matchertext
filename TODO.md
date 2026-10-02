# Current TODOs

- [ ] Clean way to separate XML and HTML flavored XML
- [ ] Fix MinML → HTML: `script` and `style` content is HTML-escaped (`<` → `&lt;`, `&&` → `&amp;&amp;`, `>` → `&gt;`). Browsers, Vue and Svelte read it as raw text and do not decode entities, so the JavaScript and CSS break. Cause: `element()` in `go/markup/html/tree.go` escapes the content of every element.
- [ ] CLI Rich Text for matchertext
- [ ] MathML in MinML
- [ ] Test SVG → XML → SVG for no loss

# Potential Student project

## Bachelor Project: MinML --> LaTeX

Syntactic converter: LaTeX handles layout, references, bibliographies, packages, classes, engines and the build.

> LaTeX → MinML is possible only for a fixed subset: documents that use known commands and environments and no catcode changes. Reading arbitrary LaTeX requires running TeX macros.

- [ ] **Basic Text:** Ordinary paragraphs, whitespace collapsing, paragraph breaks, special characters, escaping, comments and raw literal text.
- [ ] **LaTeX command peculiarities:** mandatory arguments `{}`, optional arguments `[]`, starred forms such as `\section*`, commands whose arguments are delimited in unusual ways, optional arguments appearing in unusual locations, package-defined parsers and Beamer overlay specifications such as `<2->`.
- [ ] **Key/value configuration**
- [ ] **Scope and grouping:** `{ ... }` in TeX isn't merely argument syntax. Braces also create groups, and assignments made inside a group are normally local.
- [ ] **Verbatim/code/raw content:** Inline verbatim, block verbatim, characters that normally have special meaning, and code that changes category codes (catcodes), such as expl3. LaTeX does not allow verbatim content inside command arguments.
  > provide a raw latex block option: `raw_tex { ... }` ?
- [ ] **Math:** work with MathML
- [ ] **Layout Environment:** Alignment points such as `&` and row breaks `\\` are not merely cosmetic; they carry layout semantics.
- [ ] **Tables:** Cell separators `&` and row breaks `\\`.
- [ ] **User macros/functions:** Parameters such as `#1` in macro bodies.

## Master Project: MinML based Web Framework

- [ ] Define JSX and TSX flavored MinML, and it's syntax: MSX?
- [ ] Compile MSX to clean ESM JavaScript with imports, exports, dynamic imports, asset references, and source maps
> Example compilation process:
<table><tr><th>MSX code</th><th>JS compiled version</th></tr>
<tr><td>
<pre>function Greeting({ name }) {
   return (
       div{class=greeting}[
           Hello
           +[name]
       ]
   );
}</pre></td>
<td><pre>import { jsx as _jsx } from "react/jsx-runtime";

export function Greeting({ name }) {
  return _jsx("button", {
    children: "Hello " + matchertext(name)
  });
}</pre></td></tr>
</table>

- [ ] Should cover lint, format and tests
- [ ] Extend LSP to MSX
- [ ] JSX and MSX should be interchangeable in the project and auto convertible from one format to the other
  - [ ] Make JSX <---> MSX conversion be byte identical on round trips and also correct :)
  - [ ] Any JSX files in the MSX project would get converted to MSX before compilation (rewritten in place gated by a flag?)
- [ ] Make sure the core of React: SSR, React Server Components and hydration; still function as expected
- [ ] Compare disk, allocations, peak RSS, RAM usage, parsing speeds, build time, bundle size, runtime cost
- [ ] Build the pipeline to prove injection and XSS resistance
- [ ] Comparisons against: vanilla React, React with DOMPurify, and browser Trusted Types
- [ ] Validation: convert open-source project, deploy on vercel/cloudflare
- [ ] If time allows: add a lowering API to support more frameworks than only React
  - [ ] Create an interface/API that can be overriden per web framework
  - [ ] Extend it with frameworks like: Angular, Svelte, SolideJS, ...

---