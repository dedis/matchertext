-- The language server colors everything but brackets; a regex on visible lines is cheaper
-- than a semantic token per bracket.
if vim.b.current_syntax then
  return
end
vim.cmd([[syntax match minmlDelimiter /[][{}()]/]])
vim.api.nvim_set_hl(0, "minmlDelimiter", { link = "Delimiter", default = true })
vim.b.current_syntax = "minml"
