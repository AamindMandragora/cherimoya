# Commit 7.7: Zed

## The Problem

Cherimoya's language server works in any editor that speaks the protocol, but Zed only starts a server for a language an extension defines, and only defines a language with a tree-sitter grammar. Commit 7.6 gave cherimoya that grammar, and commit 7.7 builds a Zed extension on it, which colors, indents, and outlines `.moya` and `.toya` files from the grammar, runs `cheri lsp` for them, and puts a Run test button beside every test.

## The Extension

`editors/zed/` is a Rust crate that Zed compiles to WebAssembly. Its one job is to start the server: it runs the binary `lsp.cheri.binary.path` names in Zed's settings, with its arguments, or `cheri lsp` from the worktree's path, in the worktree's shell environment. `extension.toml` names the grammar by the repository and a commit, 7.6's, with `path = "tree-sitter"`, since the grammar lives in a subdirectory. Zed fetches that one commit and builds the grammar from it, even for an extension installed from a local directory, so the grammar is always the one named. A commit can't name itself, since its hash covers its own contents, which is why the grammar and the extension are two commits, and a later change to the grammar moves the commit `extension.toml` names to the one that made it.

`languages/cherimoya/` tells Zed about the language:

| File | What it gives Zed |
|---|---|
| `config.toml` | the name, both file types, `#` comments, four-space indentation, and the brackets typed in pairs |
| `highlights.scm` | colors from the tree, where a later pattern wins, so a name starts as a variable and becomes a function, a type, a field, or an input by where it sits |
| `brackets.scm` | the pairs Zed matches, including a string's quotes |
| `indents.scm` | one level inside any bracket, and back out on the line that closes it |
| `outline.scm` | the language server's outline: every top-level function and binding, the functions and bindings in a function's body under it, and types |
| `runnables.scm`, `tasks.json` | a Run test button on every top-level function under `@test`, which runs `cheri test` with the file, the test, and `--exact` |
| `semantic_token_rules.json` | how the server's tokens map onto Zed's theme |

Zed colors from the grammar alone unless a language's `semantic_tokens` setting is `combined`, which layers the server's tokens on top. The server marks an immutable binding or input `readonly`, and Zed uses the first rule that matches a token, so the readonly rules come first and style it as a plain variable or input. A mutable one falls through to `variable.special`, so it stands out the way the language wants it to.

A nested test has no Run test button. Its name holds the function around it, `outer.inner`, which a query on one function's tree can't build, while the language server's lens has it.

## What Was Built

`editors/zed/` holds the extension: `extension.toml`, the crate in `Cargo.toml` and `src/lib.rs`, and the language files in `languages/cherimoya/`. The README says how to install it and turn on the server's colors. The queries were each run against a `.toya` file with `tree-sitter query`, and the crate builds with `cargo check`.

## Current Limitations

The grammar's WebAssembly build, which Zed makes when it installs the extension, hasn't been tried on this machine, since tree-sitter's toolchain needs a library it doesn't have. A nested test only runs from the language server's lens, in the editors that show lenses, or from the command line.
