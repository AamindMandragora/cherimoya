# Commit 7: The Language Server

## The Problem

After commit 6, cherimoya compiles real programs, but writing one is guesswork until it's built. A mistake only shows up when `cheri check` runs, the editor colors nothing, and there's no way to ask what type a name has or where it was declared. Every one of those answers is already sitting in the front end: the lexer knows every token, and sema knows what every name means and what type every expression has. Commit 7 hands them to the editor through a language server, `cheri lsp`, which speaks the Language Server Protocol, so any editor that speaks it gets diagnostics as you type, highlighting by meaning, hovers, definitions, references, rename, an outline, completion, and signature help.

## The Server

An editor starts `cheri lsp` and talks to it over stdin and stdout in JSON-RPC: every message is a JSON object behind a `Content-Length` header that says how many bytes follow. The editor sends requests, which get exactly one reply, and notifications, which get none, and the server sends its own notifications back, like a document's new diagnostics. JSON is read and written with LLVM's `llvm::json`, which cheri already links.

The first request is `initialize`, and its reply says what the server can do, which is how the editor knows to ask for any of it. The last two are `shutdown` and the `exit` notification, and the exit code follows the protocol: 0 after a shutdown, and 1 when the editor exits without one or the input just ends. A request the server doesn't handle gets the protocol's method-not-found error, and a notification it doesn't know is ignored, as the protocol says.

stdout carries the protocol, so nothing else can be printed to it. The diagnostic engine now takes where to print, stdout by default, and the server passes nowhere, then reads what was reported. On Windows, stdin and stdout are switched to binary, since the protocol counts bytes and a translated line ending would throw every count off.

## Documents

The editor sends a document's whole text when it opens it, and from then on only what changed: a range and the text that replaces it. The server keeps each open document's text and applies every change to it, then runs the whole front end again, lexer, parser, and sema, on the new text. Checking a file of today's size again is cheap, so there's no need for anything incremental past the text itself, and every answer comes from one consistent version of the document.

The editor counts positions differently from the compiler. Cherimoya talks in byte offsets, and LSP talks in lines and columns from 0, where a column counts UTF-16 code units, since that's how VS Code measures text: `é` is two bytes and one unit, and an emoji is four bytes and two. A line index, built once per version, finds a byte offset's line by binary search over where the lines start, then counts units across the line, and turns a position back into an offset by walking the line, where a column past a line's end means that line's end.

## What the Front End Records

Three stages gained a way to share what they already work out:

- **The lexer** keeps every comment's span on the side. Comments still aren't tokens, so the parser never sees them, but the server can color them.
- **Every symbol** remembers the span of the lexical scope it was declared in, which is where it can be named, and a function's symbol points at its declaration, which hovers and signature help read.
- **Sema** records every place a name is declared or used, with the symbol it means, the type each expression's context expected, every call by name with its arguments' spans, and every function body with the owner number its locals get. It already worked all of this out to check the program, so recording it is a push onto a list.

## Features

**Diagnostics** are published after every change. An error is severity 1 and a warning is 2, and each note becomes related information pointing at its own span, so `'x' was first declared here` is a link.

**Semantic highlighting** colors every token by what it is, using a legend of token types the server announces: keywords, types, numbers, strings, comments, operators, and names, where a name is colored by what sema says it means, a function, a parameter, or a variable. Two modifiers mark where a name is declared, and which names are readonly, which is every binding and parameter that isn't `mut`. The protocol packs the tokens into five numbers each: the line relative to the previous token's, the start relative to the previous token's when they share a line, the length, the type, and the modifiers.

**Hover** shows the name under the cursor the way it's declared, in a cherimoya code block: a function as `let (a :: int, b :: int) -> add -> (r :: int)`, a binding as `let x :: mut i8`, and a parameter as `n :: int`. Over a built-in type or a keyword, it says what that means instead. A type's help is worked out from its spelling, so `i8uw` is an 8-bit unsigned integer that wraps and `f16n` is IEEE half precision that can hold a NaN, and a keyword's comes from the language's description of it, saying so when it waits for a later feature. Completion shows the same help next to every type and keyword. **Go to definition** jumps to the declaration, and **find references** lists every use, with or without the declaration.

**Rename** changes a name and every use of it, after three checks:

- The new name has to lex as exactly one identifier, so `let`, `int`, `_`, and `two words` are refused.
- It can't clash with another name declared in the same scope.
- It can't change what any use means. A use of the renamed name can't end up finding another declaration further in, and a use of the other name can't end up finding the renamed one:

```
let x <- 1;
{
    let z <- 2;
    let w <- x;    # renaming x to z would make this the inner z
}
```

**The outline** lists every top-level function and binding, with the functions nested in a body under the function around them, so VS Code's outline and its go-to-symbol work.

**Completion** offers every name visible at the cursor, then the keywords and type names. A name is visible inside its scope, after its declaration unless it's a function, which is hoisted, and a local is only visible inside its own function, since a nested function can't reach the one around it. When a name is visible from more than one scope, the innermost one wins. Names are ranked by the type the position expects, the narrowest expectation around the cursor, so inside `add(` the integers come first: a name fits when it has the expected type, and a function fits when calling it gives that type.

**Signature help** shows the call the cursor is in, the innermost one, with the parameter it's at highlighted. Each parameter is a range of the rendered signature, found in order, and the cursor is at the parameter after every argument that ends before it.

## The VS Code Client

`editors/vscode/` is the extension that starts the server for every `.moya` file: a manifest that declares the language, its file extension, and its icon, a cherimoya, the comment and bracket configuration, and a client built on `vscode-languageclient` that runs `cheri lsp`. The setting `cherimoya.compilerPath` says which `cheri` to start, `cheri` on the path by default.

VS Code only loads extensions it installed itself, so the extension is packaged as a `.vsix` and installed from that. `npm run package` bundles the client and its one dependency into a single file with esbuild, then packages it with `vsce`, and `code --install-extension cherimoya.vsix` installs it.

## What Was Built

`src/lsp/transport.hpp` reads and writes messages. `src/lsp/document.hpp` has the line index and a document checked by the whole front end. `src/lsp/features.hpp` answers every request from a checked document. `src/lsp/server.hpp` is the server, its lifecycle, and its document sync. `tools/cheri/main.cpp` gained `cheri lsp`. `src/diagnostics/diagnostics.hpp` gained an optional output stream, `src/lexer/lexer.hpp` keeps comments, `src/sema/scope.hpp` gave symbols their scope and declaration, and `src/sema/sema.hpp` records uses, expected types, calls, and bodies.

`tests/unit/lsp/test_lsp.cpp` checks positions across multibyte characters, diagnostics with notes and warnings, semantic tokens and their encoding, hover, including the help for types and keywords, definition, and references, rename and each of its refusals, the outline, completion's visibility and ranking, signature help, and a whole session from `initialize` through incremental changes to `exit`. 14083 unit assertions and 52 programs, 0 failures.

## Current Limitations

A top-level binding declared below a function isn't offered in that function, though sema lets the function use it, and a function's output names are offered in its body, though only its contracts can see them. Every change re-checks the whole document, which is instant at today's sizes. Programs are a single file, so every answer is about one document. There's no formatting, no code actions, and no quick fixes for the diagnostics that already say how to fix them.
