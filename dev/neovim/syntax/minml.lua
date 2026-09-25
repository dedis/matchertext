-- The language server colors everything but brackets; a regex on visible lines is cheaper
-- than a semantic token per bracket.
if vim.b.current_syntax then
  return
end
vim.cmd([[syntax match minmlDelimiter /[][{}()]/]])
vim.api.nvim_set_hl(0, "minmlDelimiter", { link = "Delimiter", default = true })
-- Element names and references by their MinML meaning, as in the tree-sitter queries:
-- the default color scheme shows @type and @constant as plain text.
vim.api.nvim_set_hl(0, "@lsp.type.type.minml", { link = "@tag", default = true })
vim.api.nvim_set_hl(0, "@lsp.type.enumMember.minml", { link = "@string.escape", default = true })
vim.b.current_syntax = "minml"
