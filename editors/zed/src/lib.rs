// starts cheri's language server for cherimoya files, as `cheri lsp`, where cheri is the binary the user's Zed settings name under lsp.cheri.binary, or else the one on the worktree's PATH
use zed_extension_api::{self as zed, settings::LspSettings, LanguageServerId, Result};

struct Cherimoya;

impl zed::Extension for Cherimoya {
    fn new() -> Self {
        Cherimoya
    }

    // the command that starts the server, where a path and arguments in the settings replace the ones found by default, and the server gets the worktree's shell environment, so it runs the way cheri does in a terminal there
    fn language_server_command(&mut self, id: &LanguageServerId, worktree: &zed::Worktree) -> Result<zed::Command> {
        let binary = LspSettings::for_worktree(id.as_ref(), worktree).ok().and_then(|settings| settings.binary);
        let command = binary
            .as_ref()
            .and_then(|binary| binary.path.clone())
            .or_else(|| worktree.which("cheri"))
            .ok_or("cherimoya needs cheri to check its files, please put it on your PATH or set lsp.cheri.binary.path in Zed's settings")?;
        let args = binary.and_then(|binary| binary.arguments).unwrap_or_else(|| vec!["lsp".to_string()]);
        Ok(zed::Command { command, args, env: worktree.shell_env() })
    }
}

zed::register_extension!(Cherimoya);
