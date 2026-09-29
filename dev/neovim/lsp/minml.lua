-- MinML language server configuration, found by vim.lsp.enable("minml").

local exe = vim.fn.has("win32") == 1 and "minml-lsp.exe" or "minml-lsp"

-- Use $MINML_LSP_PATH, then the binary `make neovim-plugin` puts beside this plugin,
-- then minml-lsp on PATH.
local function server_path()
  local env = vim.env.MINML_LSP_PATH
  if env and env ~= "" then
    return env
  end
  local root = vim.fs.dirname(vim.fs.dirname(debug.getinfo(1, "S").source:sub(2)))
  local bundled = vim.fs.joinpath(root, "bin", exe)
  if vim.uv.fs_stat(bundled) then
    return bundled
  end
  return exe
end

return {
  cmd = { server_path() },
  filetypes = { "minml" },
  root_markers = { ".git" },
}
