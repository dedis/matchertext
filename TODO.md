# Current TODOs

- [ ] Fix MinML → HTML: `script` and `style` content is HTML-escaped (`<` → `&lt;`, `&&` → `&amp;&amp;`, `>` → `&gt;`). Browsers, Vue and Svelte read it as raw text and do not decode entities, so the JavaScript and CSS break. Cause: `element()` in `go/markup/html/tree.go` escapes the content of every element.
- [ ] CLI Rich Text for matchertext
- [ ] Test SVG → XML → SVG for no loss
- [ ] MathML in MinML
  - [ ] Embedded in the the minml transpiler directly?
  - [ ] As an extension to the transpiler?
    - [ ] Needs an extension system added into the transpiler

# Human Computer Interface studies

Since MinML is smaller than html, study the usability of:
- [ ] MinML vs HTML
- [ ] MSX vs JSX (web framework)
- [ ] Static Websites with MinML vs HTML
- [ ] MinML LaTeX vs plan LaTeX

Allows us to make minml and matchertext easier to use and how to make it easier to use