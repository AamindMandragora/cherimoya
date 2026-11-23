# Commit 12.8: A Zed Extension

## The Feature

- **A Zed extension.** `editors/zed/` colors, indents, and outlines `.moya` and `.toya` files from the tree-sitter grammar of commit 12.7, and runs `cheri lsp` for them.
- **A Run test button.** Beside every top-level function under `@test`, running `cheri test` with the file, the test, and `--exact`.
- **The server's colors on request.** With `"semantic_tokens": "combined"` in the language's settings, Zed layers the server's tokens over the grammar's, which is also what sets a mutable binding apart from an immutable one.
- **A clear error without cheri.** When no binary is found, the extension reports `cherimoya needs cheri to check its files, please put it on your PATH or set lsp.cheri.binary.path in Zed's settings`.

## Why

Cherimoya's language server works in any editor that speaks the protocol, but Zed only starts a server for a language an extension defines, and only defines a language with a tree-sitter grammar. With the grammar in place, the extension is mostly configuration.

- **The grammar is named by a commit.** `extension.toml` names the grammar by the repository, a commit, commit 12.7's, and `path = "tree-sitter"`, since it lives in a subdirectory. Zed fetches that one commit and builds the grammar from it, even for an extension installed from a local directory, so the grammar is always the one named. A commit can't name itself, since its hash covers its own contents, which is why the grammar and the extension are two commits, and a later change to the grammar moves the commit `extension.toml` names to the one that made it.
- **Readonly rules come first.** The server marks an immutable binding or input `readonly`, and Zed uses the first rule that matches a token, so the readonly rules style it as a plain variable or input, and a mutable one falls through to `variable.special`, so it stands out the way the language wants it to.
- **Settings win over the path.** A binary path and arguments in Zed's settings replace the defaults, so a build of cheri that isn't installed can be used directly.
- **The worktree's environment.** The server gets the worktree's shell environment, so it runs the way cheri does in a terminal there.

## Implementation

### Editors

- **The crate.** `editors/zed/Cargo.toml` builds a `cdylib` on `zed_extension_api` 0.7.0, which Zed compiles to WebAssembly. `src/lib.rs` implements `zed::Extension` for `Cherimoya`, whose `language_server_command` reads `LspSettings::for_worktree` for `lsp.cheri.binary`, takes its `path` or else `worktree.which("cheri")`, takes its `arguments` or else `lsp`, and passes `worktree.shell_env()`.
- **`extension.toml`.** Declares the `cheri` language server for `Cherimoya` and the `cherimoya` grammar at `rev = "2af0e8d50cb97c5b759b3de7df35cb5f5226dddb"`.
- **`languages/cherimoya/`.** Tells Zed about the language:

| File | What it gives Zed |
|---|---|
| `config.toml` | the name, both file types, `# ` comments, four-space indentation, and the brackets typed in pairs, quotes except inside a string or comment |
| `highlights.scm` | colors from the tree, where a later pattern wins, so a name starts as a variable and becomes a function, a type, a field, or an input by where it sits |
| `brackets.scm` | the pairs Zed matches, including a string's and a character's quotes |
| `indents.scm` | one level inside any bracket, and back out on the line that closes it |
| `outline.scm` | the language server's outline: every top-level function and binding, the functions and bindings in a function's body under it, and types |
| `runnables.scm`, `tasks.json` | a Run test button on every top-level function under `@test`, tagged `cherimoya-test`, whose task runs `cheri test $ZED_FILE $ZED_CUSTOM_test --exact` |
| `semantic_token_rules.json` | how the server's tokens map onto Zed's theme |

The README says how to install the extension, with `zed: install dev extension`, and how to turn on the server's colors and point Zed at a `cheri` binary.

## Tests

The queries were each run against a `.toya` file with `tree-sitter query`, and the crate builds with `cargo check`.

## Current Limitations

- **Nested tests.** A nested test has no Run test button. Its name holds the function around it, `outer.inner`, which a query on one function's tree can't build, so it only runs from the language server's lens, in the editors that show lenses, or from the command line.
- **The WebAssembly build is untried.** The grammar's WebAssembly build, which Zed makes when it installs the extension, hasn't been tried on this machine, since tree-sitter's toolchain needs a library it doesn't have.
