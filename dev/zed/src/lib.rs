//! Zed extension for MinML: starts the minml-lsp language server.

use zed_extension_api::{self as zed, settings::LspSettings, LanguageServerId, Result, Worktree};

struct Minml;

impl zed::Extension for Minml {
    fn new() -> Self {
        Minml
    }

    /// Uses the `lsp.minml-lsp.binary.path` setting, then `$MINML_LSP_PATH`, then minml-lsp on PATH.
    fn language_server_command(
        &mut self,
        language_server_id: &LanguageServerId,
        worktree: &Worktree,
    ) -> Result<zed::Command> {
        let configured = LspSettings::for_worktree(language_server_id.as_ref(), worktree)
            .ok()
            .and_then(|settings| settings.binary)
            .and_then(|binary| binary.path);
        let env = worktree
            .shell_env()
            .into_iter()
            .find(|(name, value)| name == "MINML_LSP_PATH" && !value.is_empty())
            .map(|(_, value)| value);
        let command = configured
            .or(env)
            .or_else(|| worktree.which("minml-lsp"))
            .ok_or("minml-lsp not found: put it on PATH, set MINML_LSP_PATH, or set lsp.minml-lsp.binary.path")?;
        Ok(zed::Command {
            command,
            args: vec![],
            env: vec![],
        })
    }
}

zed::register_extension!(Minml);
