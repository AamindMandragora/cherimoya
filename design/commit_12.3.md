# Commit 12.3: Emacs

## The Feature

- **`cherimoya-mode`.** A major mode for Emacs, loaded from the load path with `(require 'cherimoya-mode)`, which opens every `.moya` file. It knows comments, strings, brackets, and indentation, and hands everything else to `cheri lsp`.
- **Comments and literals.** `#` starts a comment that runs to the end of the line, and `'` quotes a char literal the way `"` quotes a string, so Emacs moves over them, fills comments correctly, and doesn't match a bracket inside one.
- **Indentation by braces.** A line indents 4 columns for each brace open at its start, one level less when it starts by closing a brace. Typing a `}` indents its line again, so a closing brace lands under the line that opened it.
- **Both language server clients.** eglot, built into Emacs since 29, starts the server with `M-x eglot` or `eglot-ensure`, and lsp-mode with `M-x lsp`. Either starts `cheri lsp`, where `cherimoya-compiler-path` says which `cheri`, `cheri` on the path by default, like VS Code's `cherimoya.compilerPath`.

## Why

Any editor that speaks the Language Server Protocol can start `cheri lsp`, but Emacs doesn't know what a `.moya` file is, so there's nothing to start the server for. A small mode that only covers what the server doesn't, the syntax table and indentation, is all Emacs needs.

- **The same indentation rule as VS Code.** Indenting by open braces is what VS Code's indentation follows, so a file looks the same in both.
- **Only braces indent.** A call or a pack that runs over a line continues the line it's in rather than opening a scope, so parentheses and square brackets don't indent.
- **Both clients, neither required.** The mode registers the server with each client only once that client loads, so it works with whichever one a user has.
- **No highlighting of its own.** The server colors the code through semantic tokens, so the mode doesn't keep a second list of keywords and types that could fall out of step with the lexer.

## Implementation

### Editors

`editors/emacs/cherimoya-mode.el` is one file.

- **Settings.** The `cherimoya` group holds `cherimoya-compiler-path` and `cherimoya-indent-offset`, 4 by default. `cherimoya-server-command` gives the command, `cheri lsp`, as a list.
- **Syntax.** `cherimoya-mode-syntax-table` makes `#` a comment start and newline its end, `'` a string quote, and `_` a symbol constituent.
- **Indentation.** `cherimoya-indent-line` reads the open brackets at the line's start from `syntax-ppss`, counts the ones that are `{`, takes one off when the line's first character is `}`, and indents to that times `cherimoya-indent-offset`, moving point to the indentation if it was before it.
- **The mode.** `cherimoya-mode` derives from `prog-mode`, sets `comment-start` to `# `, `indent-line-function`, `indent-tabs-mode` off, and adds `}` to `electric-indent-chars`. An autoloaded entry in `auto-mode-alist` maps `.moya` to it.
- **eglot.** `with-eval-after-load 'eglot` adds `(cherimoya-mode . cherimoya-server-command)` to `eglot-server-programs`.
- **lsp-mode.** `with-eval-after-load 'lsp-mode` maps the mode to the language id `cherimoya` in `lsp-language-id-configuration` and registers a client with server id `cheri` that connects over stdio and activates on `cherimoya`. `declare-function` and `defvar` forms keep the byte compiler quiet when lsp-mode isn't installed.

The README says how to load the mode with either client.

## Tests

The mode byte-compiles with no warnings and indents the programs in `tests/run/` as they're written. In a headless Emacs, eglot starts `cheri lsp`, shows its diagnostics through flymake, and gets hovers back from it. lsp-mode's registration is checked by the byte compiler but not run against the server.

## Current Limitations

- **Highlighting needs semantic tokens.** eglot reads them from Emacs 31 on, and lsp-mode once `lsp-semantic-tokens-enable` is set. An older eglot leaves keywords and types uncolored, apart from comments and strings, though everything else works.
- **Brace-only indentation.** A long match arm or a wrapped call is indented by hand.
- **Not on MELPA.** The mode is loaded from a checkout.
