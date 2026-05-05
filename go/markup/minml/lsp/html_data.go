package lsp

type ElementInfo struct {
	Description string
	Attributes  []string
}

var HTMLElements = map[string]ElementInfo{
	"div": {
		Description: "The <div> element is the generic container for flow content. It has no effect on the content or layout until styled in some way using CSS.",
		Attributes:  []string{"class", "id", "style", "title"},
	},
	"p": {
		Description: "The <p> element represents a paragraph.",
		Attributes:  []string{"class", "id", "style", "title"},
	},
	"a": {
		Description: "The <a> (or anchor) element, with its href attribute, creates a hyperlink to web pages, files, email addresses, locations in the same page, or anything else a URL can address.",
		Attributes:  []string{"href", "target", "rel", "class", "id", "style"},
	},
	"img": {
		Description: "The <img> element embeds an image into the document.",
		Attributes:  []string{"src", "alt", "width", "height", "loading", "class", "id"},
	},
	"span": {
		Description: "The <span> element is a generic inline container for phrasing content, which does not inherently represent anything. It can be used to group elements for styling purposes.",
		Attributes:  []string{"class", "id", "style", "title"},
	},
	"h1": {
		Description: "The <h1> through <h6> elements represent six levels of section headings. <h1> is the highest section level and <h6> is the lowest.",
		Attributes:  []string{"class", "id", "style"},
	},
	"ul": {
		Description: "The <ul> element represents an unordered list of items, typically rendered as a bulleted list.",
		Attributes:  []string{"class", "id", "style"},
	},
	"li": {
		Description: "The <li> element is used to represent an item in a list.",
		Attributes:  []string{"class", "id", "style"},
	},
}
