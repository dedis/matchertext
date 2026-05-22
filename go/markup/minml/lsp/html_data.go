package lsp

// GlobalAttrs are the HTML global attributes accepted on any element.
var GlobalAttrs = []string{
	"class", "id", "style", "title", "lang", "dir",
	"tabindex", "hidden", "accesskey", "contenteditable",
	"draggable", "spellcheck", "role",
}

type ElementInfo struct {
	Description string
	Attributes  []string
}

// HTMLElements maps HTML5 tag names to their descriptions and element-specific
// attributes. Descriptions use MinML syntax: tagname[content] not <tag>content</tag>.
var HTMLElements = map[string]ElementInfo{
	// --- Document structure ---
	"html": {
		Description: "The root element of an HTML document. In MinML: `html[head[...] body[...]]`.",
		Attributes:  append([]string{"lang", "dir"}, GlobalAttrs...),
	},
	"head": {
		Description: "Contains machine-readable metadata about the document.",
		Attributes:  GlobalAttrs,
	},
	"body": {
		Description: "Contains the visible content of the document. In MinML: `body[...]`.",
		Attributes:  append([]string{"onload", "onunload"}, GlobalAttrs...),
	},
	"title": {
		Description: "Defines the document title shown in the browser tab.",
		Attributes:  GlobalAttrs,
	},

	// --- Metadata ---
	"meta": {
		Description: "Represents metadata that cannot be expressed by other meta-related elements.",
		Attributes:  []string{"name", "content", "charset", "http-equiv", "property"},
	},
	"link": {
		Description: "Specifies the relationship between the document and an external resource (e.g. stylesheets).",
		Attributes:  []string{"rel", "href", "type", "media", "as", "crossorigin", "integrity"},
	},
	"style": {
		Description: "Contains CSS style rules. In MinML: `style[+[body { color: red; }]]`.",
		Attributes:  []string{"media", "type"},
	},
	"script": {
		Description: "Embeds or references executable code (usually JavaScript).",
		Attributes:  []string{"src", "type", "async", "defer", "crossorigin", "integrity", "nomodule"},
	},

	// --- Sectioning ---
	"main": {
		Description: "The dominant content of the document body. There must be only one non-hidden `main` element.",
		Attributes:  GlobalAttrs,
	},
	"section": {
		Description: "A generic standalone section of a document, typically with a heading.",
		Attributes:  GlobalAttrs,
	},
	"article": {
		Description: "A self-contained piece of content that could be independently distributed (e.g. a post or comment).",
		Attributes:  GlobalAttrs,
	},
	"aside": {
		Description: "Content only indirectly related to the main content (e.g. a sidebar or pull quote).",
		Attributes:  GlobalAttrs,
	},
	"header": {
		Description: "Introductory content or navigation aids for its nearest sectioning ancestor.",
		Attributes:  GlobalAttrs,
	},
	"footer": {
		Description: "Footer for its nearest sectioning ancestor, typically containing authorship or copyright info.",
		Attributes:  GlobalAttrs,
	},
	"nav": {
		Description: "A section of the page providing navigation links.",
		Attributes:  GlobalAttrs,
	},

	// --- Headings ---
	"h1": {
		Description: "Level-1 section heading — the highest rank. In MinML: `h1[My Title]`.",
		Attributes:  GlobalAttrs,
	},
	"h2": {
		Description: "Level-2 section heading.",
		Attributes:  GlobalAttrs,
	},
	"h3": {
		Description: "Level-3 section heading.",
		Attributes:  GlobalAttrs,
	},
	"h4": {
		Description: "Level-4 section heading.",
		Attributes:  GlobalAttrs,
	},
	"h5": {
		Description: "Level-5 section heading.",
		Attributes:  GlobalAttrs,
	},
	"h6": {
		Description: "Level-6 section heading — the lowest rank.",
		Attributes:  GlobalAttrs,
	},

	// --- Block content ---
	"div": {
		Description: "Generic block-level container with no semantic meaning. Style with CSS. In MinML: `div{class=container}[...]`.",
		Attributes:  GlobalAttrs,
	},
	"p": {
		Description: "A paragraph of text. In MinML: `p[Hello, world.]`.",
		Attributes:  GlobalAttrs,
	},
	"blockquote": {
		Description: "A block of text quoted from another source.",
		Attributes:  append([]string{"cite"}, GlobalAttrs...),
	},
	"pre": {
		Description: "Preformatted text — whitespace is preserved. Pair with `code` for code blocks. In MinML: `pre[code[...]]`.",
		Attributes:  GlobalAttrs,
	},
	"hr": {
		Description: "A thematic break between paragraphs (horizontal rule). Void element in MinML: `hr[]`.",
		Attributes:  GlobalAttrs,
	},
	"figure": {
		Description: "Self-contained content (image, diagram, code) optionally with a caption via `figcaption`.",
		Attributes:  GlobalAttrs,
	},
	"figcaption": {
		Description: "A caption or legend for the content of its parent `figure` element.",
		Attributes:  GlobalAttrs,
	},
	"details": {
		Description: "A disclosure widget that shows additional content on demand.",
		Attributes:  append([]string{"open"}, GlobalAttrs...),
	},
	"summary": {
		Description: "A visible heading for a `details` element; clicking it toggles the details.",
		Attributes:  GlobalAttrs,
	},
	"address": {
		Description: "Contact information for the nearest `article` or `body` ancestor.",
		Attributes:  GlobalAttrs,
	},

	// --- Lists ---
	"ul": {
		Description: "An unordered (bulleted) list. Children must be `li` elements. In MinML: `ul[li[item one] li[item two]]`.",
		Attributes:  GlobalAttrs,
	},
	"ol": {
		Description: "An ordered (numbered) list. Children must be `li` elements.",
		Attributes:  append([]string{"type", "start", "reversed"}, GlobalAttrs...),
	},
	"li": {
		Description: "A list item inside `ul` or `ol`. In MinML: `li[First item]`.",
		Attributes:  append([]string{"value"}, GlobalAttrs...),
	},
	"dl": {
		Description: "A description list of term–description pairs (`dt` / `dd`).",
		Attributes:  GlobalAttrs,
	},
	"dt": {
		Description: "The term part of a description list item.",
		Attributes:  GlobalAttrs,
	},
	"dd": {
		Description: "The description part of a description list item.",
		Attributes:  GlobalAttrs,
	},

	// --- Inline content ---
	"span": {
		Description: "Generic inline container with no semantic meaning. In MinML: `span{class=highlight}[text]`.",
		Attributes:  GlobalAttrs,
	},
	"a": {
		Description: "Anchor element — creates a hyperlink. In MinML: `a{href=https://example.com}[click here]`.",
		Attributes:  append([]string{"href", "target", "rel", "download", "type", "hreflang"}, GlobalAttrs...),
	},
	"strong": {
		Description: "Strong importance — text is typically bold. In MinML: `strong[important]`.",
		Attributes:  GlobalAttrs,
	},
	"em": {
		Description: "Stress emphasis — text is typically italic. In MinML: `em[emphasis]`.",
		Attributes:  GlobalAttrs,
	},
	"b": {
		Description: "Bold text without conveying extra importance (use `strong` for importance).",
		Attributes:  GlobalAttrs,
	},
	"i": {
		Description: "Italic text used for technical terms, foreign phrases, or thoughts (use `em` for stress emphasis).",
		Attributes:  GlobalAttrs,
	},
	"u": {
		Description: "Underlined text (use sparingly; may be confused with hyperlinks).",
		Attributes:  GlobalAttrs,
	},
	"s": {
		Description: "Strikethrough text for content that is no longer accurate.",
		Attributes:  GlobalAttrs,
	},
	"small": {
		Description: "Side comments such as fine print.",
		Attributes:  GlobalAttrs,
	},
	"sub": {
		Description: "Subscript text. In MinML: `sub[2]` in H`sub[2]`O.",
		Attributes:  GlobalAttrs,
	},
	"sup": {
		Description: "Superscript text. In MinML: `sup[2]` in x`sup[2]`.",
		Attributes:  GlobalAttrs,
	},
	"mark": {
		Description: "Highlighted text for reference or notation. In MinML: `mark[search term]`.",
		Attributes:  GlobalAttrs,
	},
	"code": {
		Description: "Inline computer code. In MinML: `code[fmt.Println]`.",
		Attributes:  GlobalAttrs,
	},
	"kbd": {
		Description: "User keyboard input. In MinML: `kbd[Ctrl+C]`.",
		Attributes:  GlobalAttrs,
	},
	"samp": {
		Description: "Sample output from a computer program.",
		Attributes:  GlobalAttrs,
	},
	"var": {
		Description: "A variable in a mathematical expression or programming context.",
		Attributes:  GlobalAttrs,
	},
	"abbr": {
		Description: "An abbreviation or acronym with an optional expansion in the `title` attribute.",
		Attributes:  append([]string{"title"}, GlobalAttrs...),
	},
	"cite": {
		Description: "A citation — the title of a work being referenced.",
		Attributes:  GlobalAttrs,
	},
	"q": {
		Description: "An inline quotation. Browsers typically add quotation marks.",
		Attributes:  append([]string{"cite"}, GlobalAttrs...),
	},
	"time": {
		Description: "A specific period in time, optionally machine-readable via `datetime`.",
		Attributes:  append([]string{"datetime"}, GlobalAttrs...),
	},
	"br": {
		Description: "A line break. Void element in MinML: `br[]`.",
		Attributes:  GlobalAttrs,
	},
	"wbr": {
		Description: "A word-break opportunity — a hint that the browser may break the line here.",
		Attributes:  GlobalAttrs,
	},

	// --- Media ---
	"img": {
		Description: "Embeds an image. Void element in MinML: `img{src=photo.jpg alt=A cat}[]`.",
		Attributes:  []string{"src", "alt", "width", "height", "loading", "decoding", "srcset", "sizes", "crossorigin", "class", "id", "style"},
	},
	"video": {
		Description: "Embeds a video. In MinML: `video{src=clip.mp4 controls=[]}[]` or with `source` children.",
		Attributes:  []string{"src", "autoplay", "controls", "loop", "muted", "poster", "preload", "width", "height", "class", "id"},
	},
	"audio": {
		Description: "Embeds audio content.",
		Attributes:  []string{"src", "autoplay", "controls", "loop", "muted", "preload", "class", "id"},
	},
	"source": {
		Description: "A media source for `picture`, `video`, or `audio`. Void element in MinML: `source{src=clip.webm type=video/webm}[]`.",
		Attributes:  []string{"src", "srcset", "type", "media", "sizes"},
	},
	"picture": {
		Description: "Container for multiple image sources via `source` elements with a fallback `img`.",
		Attributes:  GlobalAttrs,
	},
	"canvas": {
		Description: "A bitmap drawing surface scripted via JavaScript.",
		Attributes:  append([]string{"width", "height"}, GlobalAttrs...),
	},
	"svg": {
		Description: "Inline scalable vector graphic.",
		Attributes:  append([]string{"viewBox", "width", "height", "xmlns"}, GlobalAttrs...),
	},

	// --- Tables ---
	"table": {
		Description: "A data table. In MinML: `table[thead[tr[th[Name] th[Age]]] tbody[tr[td[Alice] td[30]]]]`.",
		Attributes:  GlobalAttrs,
	},
	"thead": {
		Description: "The header section of a table, containing `tr` rows.",
		Attributes:  GlobalAttrs,
	},
	"tbody": {
		Description: "The body section of a table, containing `tr` rows.",
		Attributes:  GlobalAttrs,
	},
	"tfoot": {
		Description: "The footer section of a table.",
		Attributes:  GlobalAttrs,
	},
	"tr": {
		Description: "A row in a table.",
		Attributes:  GlobalAttrs,
	},
	"th": {
		Description: "A header cell in a table. In MinML: `th{scope=col}[Name]`.",
		Attributes:  append([]string{"scope", "colspan", "rowspan", "headers"}, GlobalAttrs...),
	},
	"td": {
		Description: "A data cell in a table.",
		Attributes:  append([]string{"colspan", "rowspan", "headers"}, GlobalAttrs...),
	},
	"caption": {
		Description: "A caption for a `table` element, placed as its first child.",
		Attributes:  GlobalAttrs,
	},
	"colgroup": {
		Description: "A group of columns in a table, used to apply styles to multiple columns.",
		Attributes:  append([]string{"span"}, GlobalAttrs...),
	},
	"col": {
		Description: "A column within a `colgroup`. Void element.",
		Attributes:  append([]string{"span"}, GlobalAttrs...),
	},

	// --- Forms ---
	"form": {
		Description: "An interactive form for user input. In MinML: `form{action=/submit method=post}[...]`.",
		Attributes:  []string{"action", "method", "enctype", "name", "novalidate", "target", "autocomplete", "class", "id"},
	},
	"input": {
		Description: "An interactive control for data entry. Void element in MinML: `input{type=text name=q}[]`.",
		Attributes:  []string{"type", "name", "value", "placeholder", "required", "disabled", "readonly", "autofocus", "autocomplete", "min", "max", "step", "pattern", "multiple", "accept", "checked", "class", "id"},
	},
	"button": {
		Description: "A clickable button. In MinML: `button{type=submit}[Send]`.",
		Attributes:  []string{"type", "name", "value", "disabled", "form", "formaction", "class", "id"},
	},
	"textarea": {
		Description: "A multi-line plain text editing control.",
		Attributes:  []string{"name", "rows", "cols", "placeholder", "required", "disabled", "readonly", "maxlength", "wrap", "class", "id"},
	},
	"select": {
		Description: "A control that provides a menu of options.",
		Attributes:  []string{"name", "multiple", "size", "required", "disabled", "autofocus", "form", "class", "id"},
	},
	"option": {
		Description: "An option in a `select` or `datalist` element.",
		Attributes:  []string{"value", "selected", "disabled", "label"},
	},
	"optgroup": {
		Description: "A group of options in a `select` element.",
		Attributes:  []string{"label", "disabled"},
	},
	"label": {
		Description: "A caption for a form control. In MinML: `label{for=username}[Username]`.",
		Attributes:  append([]string{"for", "form"}, GlobalAttrs...),
	},
	"fieldset": {
		Description: "A group of related form controls, optionally captioned by a `legend`.",
		Attributes:  append([]string{"name", "disabled", "form"}, GlobalAttrs...),
	},
	"legend": {
		Description: "A caption for the content of its parent `fieldset`.",
		Attributes:  GlobalAttrs,
	},
	"datalist": {
		Description: "A set of pre-defined options for an `input` element.",
		Attributes:  GlobalAttrs,
	},
	"output": {
		Description: "The result of a calculation or user action.",
		Attributes:  append([]string{"for", "form", "name"}, GlobalAttrs...),
	},
	"progress": {
		Description: "A progress indicator. In MinML: `progress{max=100 value=70}[]`.",
		Attributes:  append([]string{"max", "value"}, GlobalAttrs...),
	},
	"meter": {
		Description: "A scalar measurement within a known range (e.g. disk usage).",
		Attributes:  append([]string{"value", "min", "max", "low", "high", "optimum"}, GlobalAttrs...),
	},

	// --- Interactive / Scripting ---
	"dialog": {
		Description: "A dialog box or other interactive component. Toggle with the `open` attribute.",
		Attributes:  append([]string{"open"}, GlobalAttrs...),
	},
	"template": {
		Description: "Holds HTML that is not rendered but can be cloned via JavaScript.",
		Attributes:  GlobalAttrs,
	},
	"slot": {
		Description: "A placeholder inside a web component shadow DOM.",
		Attributes:  append([]string{"name"}, GlobalAttrs...),
	},
	"noscript": {
		Description: "Fallback content for when scripting is disabled or unsupported.",
		Attributes:  GlobalAttrs,
	},

	// --- Embedded content ---
	"iframe": {
		Description: "Embeds another HTML document inside the current one.",
		Attributes:  []string{"src", "srcdoc", "name", "width", "height", "allow", "allowfullscreen", "loading", "sandbox", "class", "id"},
	},
	"embed": {
		Description: "Embeds external content (e.g. a plugin). Void element.",
		Attributes:  []string{"src", "type", "width", "height", "class", "id"},
	},
	"object": {
		Description: "Embeds an external resource that can be treated as an image or nested browser context.",
		Attributes:  []string{"data", "type", "name", "form", "width", "height", "class", "id"},
	},

	// --- Ruby annotation ---
	"ruby": {
		Description: "Ruby annotation for East Asian typography.",
		Attributes:  GlobalAttrs,
	},
	"rt": {
		Description: "The ruby text component of a ruby annotation.",
		Attributes:  GlobalAttrs,
	},
	"rp": {
		Description: "Fallback parentheses for browsers that don't support ruby annotations.",
		Attributes:  GlobalAttrs,
	},
}
