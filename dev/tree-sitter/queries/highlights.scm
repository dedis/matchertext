; Capture names that Helix, Zed, and Neovim themes all define, directly or by prefix.

(tag_name) @tag

(attr_name) @attribute

"=" @punctuation.delimiter

(reference) @constant.character.escape

(comment) @comment

(raw) @string

; Only brackets that delimit structure: brackets inside comments and raw text keep their color.
(content ["[" "]"] @punctuation.bracket)
(attributes ["{" "}"] @punctuation.bracket)
(quoted_value ["[" "]"] @punctuation.bracket)
(literal ["[" "]" "{" "}" "(" ")"] @punctuation.bracket)
