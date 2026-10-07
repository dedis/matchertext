-- MinML file detection and language server activation.
-- .minml and .m are MinML's extensions; .m takes precedence over Neovim's own .m detection
-- (Objective-C, MATLAB, and others).

vim.filetype.add({ extension = { minml = "minml", m = "minml" } })
vim.lsp.enable("minml")
