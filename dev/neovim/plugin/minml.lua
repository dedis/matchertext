-- MinML file detection and language server activation.

-- Objective-C and MATLAB also use .m, so a .m file is MinML only if
-- its first non-blank line starts with an element, attributes, or a MinML construct.
local function detect_m(path, bufnr)
  if vim.g.filetype_m then
    return vim.g.filetype_m
  end
  for _, line in ipairs(vim.api.nvim_buf_get_lines(bufnr, 0, 100, false)) do
    if line:find("%S") then
      if line:find("^%s*<?[%w_:.-]+[%[{]") or line:find("^%s*[?+%-\"'][%[]") then
        return "minml"
      end
      break
    end
  end
  return require("vim.filetype.detect").m(path, bufnr)
end

vim.filetype.add({ extension = { minml = "minml", m = detect_m } })
vim.lsp.enable("minml")
