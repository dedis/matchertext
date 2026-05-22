; Tags
(tag_name) @tag

; Attributes
(attr_name) @tag.attribute
(attr_block "{" @punctuation.bracket "}" @punctuation.bracket)

; Content blocks
(content_block "[" @punctuation.bracket "]" @punctuation.bracket)

; Character references
(char_ref "[" @punctuation.bracket "]" @punctuation.bracket)
(named_ref) @constant.builtin
(decimal_ref) @constant.builtin
(hex_ref) @constant.builtin

; Strings and Raw blocks
(quoted_string) @string
(raw_block) @string.special

; Comments
(comment) @comment

; Processing instructions
(processing_instruction) @keyword.directive

; Space-suckers
["<" ">"] @punctuation.delimiter

; Escape sequences
(matcher_escape) @string.escape
