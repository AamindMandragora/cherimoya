# Commit 7.2: Emacs

## The Problem

Commit 7.1 gave cherimoya a language server and a VS Code client for it, and any editor that speaks the Language Server Protocol can start `cheri lsp` too, but Emacs doesn't know what a `.moya` file is, so there's nothing to start the server for. Commit 7.2 is a major mode for Emacs, `cherimoya-mode`, which knows what the server doesn't, comments, strings, brackets, and indentation, and hands everything else to the server. It comes after 8.1 in the history, since it only adds a client.

## The Mode

`editors/emacs/cherimoya-mode.el` is one file, loaded from the load path with `(require 'cherimoya-mode)`, which opens every `.moya` file in `cherimoya-mode`. Its syntax table makes `#` start a comment that runs to the end of the line, and `'` quote a char literal the way `"` quotes a string, so Emacs moves over them and fills comments correctly, and doesn't match a bracket inside one.

A line indents by how many braces are open at its start, 4 columns each, and one level less when it starts by closing a brace, which is the same rule VS Code's indentation follows. Parentheses and square brackets don't indent, since a call or a pack that runs over a line continues the line it's in rather than opening a scope. Typing a `}` indents its line again, so a closing brace lands under the line that opened it.

## Two Clients

Emacs has two language server clients, and the mode registers the server with both, each only once that client loads, so neither is required:

- **eglot**, built into Emacs since 29, gets an entry in `eglot-server-programs`, and `M-x eglot` or `eglot-ensure` starts it.
- **lsp-mode**, a package, gets a client named `cheri` that activates on the `cherimoya` language, and `M-x lsp` starts it.

Both start `cherimoya-server-command`, which is `cheri lsp`, where `cherimoya-compiler-path` says which `cheri`, `cheri` on the path by default, like VS Code's `cherimoya.compilerPath`.

The server colors the code through semantic tokens, so the mode has no highlighting of its own. Eglot reads semantic tokens from Emacs 31 on, and lsp-mode does once `lsp-semantic-tokens-enable` is set. With an older eglot, a file is uncolored apart from its comments and strings, and everything else still works.

## What Was Built

`editors/emacs/cherimoya-mode.el` is the mode, and the README says how to load it with either client. The mode byte-compiles with no warnings, indents the run programs as they're written, and in a headless Emacs, eglot starts `cheri lsp`, shows its diagnostics through flymake, and gets hovers back from it. lsp-mode's registration is checked by the byte compiler, but not run against the server.

## Current Limitations

There's no highlighting without semantic tokens, so an older Emacs shows keywords and types uncolored. Indentation only follows braces, so a long match arm or a wrapped call is indented by hand. The mode isn't on MELPA, so it's loaded from a checkout.
