# Commit 12.2: The Language Server and VS Code

## The Feature

- **`cheri lsp`.** A language server that speaks the Language Server Protocol over stdin and stdout, so any editor that speaks it gets diagnostics as you type, highlighting by meaning, hover, go to definition, find references, rename, an outline, completion, and signature help.
- **Diagnostics** are published after every change. An error is severity 1 and a warning 2, and each note becomes related information pointing at its own span, so `'x' was first declared here` is a link.
- **Semantic highlighting** colors keywords, types, numbers, strings, comments, operators, and names, where a name is colored by what sema says it means: a function, a parameter, or a variable. Two modifiers mark where a name is declared and which names are readonly, which is every binding and parameter that isn't `mut`.
- **Hover** shows a name the way it's declared, in a cherimoya code block: a function as `let (a :: int, b :: int) -> add -> (r :: int)`, a binding as `let x :: mut i8`, and a parameter as `n :: int`. Over a built-in type or a keyword it says what that means instead: `i8uw` is an 8-bit unsigned integer that wraps, `f16n` is IEEE half precision that can hold a NaN, and a keyword that waits for a later feature says so. Completion shows the same help next to every type and keyword.
- **Go to definition and find references.** Definition jumps to the declaration, and references lists every use, with or without the declaration.
- **Rename** changes a name and every use of it. The new name has to lex as exactly one identifier, so `let`, `int`, `_`, and `two words` are refused, it can't clash with another name declared in the same scope, and it can't change what any use means:

```
let x <- 1;
{
    let z <- 2;
    let w <- x;    # renaming x to z would make this the inner z
}
```

- **The outline** lists every top-level function and binding, with the functions nested in a body under the function around them, so VS Code's outline and go-to-symbol work.
- **Completion** offers every name visible at the cursor, then the keywords and type names, ranked by the type the position expects, so inside `add(` the integers come first.
- **Signature help** shows the innermost call the cursor is in, with the parameter it's at highlighted.
- **A VS Code extension.** `editors/vscode/` starts the server for every `.moya` file and gives them a cherimoya icon. The setting `cherimoya.compilerPath` says which `cheri` to start, `cheri` on the path by default.

## Why

Once cherimoya compiles real programs, writing one is still guesswork until it's built: a mistake only shows up when `cheri check` runs, the editor colors nothing, and there's no way to ask what type a name has or where it was declared. Every one of those answers already sits in the front end. The lexer knows every token, and sema knows what every name means and what type every expression has. A language server hands them to any editor at once instead of writing a plugin per editor.

- **Re-check everything on every change.** The server keeps each document's text, applies every change to it, and runs the lexer, parser, and sema again. Checking a file of today's size is cheap, so nothing past the text needs to be incremental, and every answer comes from one consistent version of the document.
- **Record, don't recompute.** Sema already works out every use, expected type, and call to check the program, so recording each is a push onto a list, and the server never re-derives meaning from the tree.
- **Comments on the side.** Comments still aren't tokens, so the parser never sees them, but the lexer keeps their spans so the server can color them.
- **UTF-16 columns.** LSP counts columns in UTF-16 code units, since that's how VS Code measures text, while the compiler counts bytes: `é` is two bytes and one unit, and an emoji is four bytes and two.
- **Rename refuses rather than guesses.** A rename that would make a use find a different declaration changes the program's meaning silently, so it's refused with a reason instead.
- **Completion follows sema's visibility.** A name is visible inside its scope, after its declaration unless it's a function, which is hoisted, and a local only inside its own function, since a nested function can't reach the one around it. When a name is visible from more than one scope, the innermost one wins.
- **Ranking by the narrowest expectation.** The expected type nearest the cursor is the one for the position it's actually in. A name fits when it has that type, and a function fits when calling it gives that type.
- **A packaged extension.** VS Code only loads extensions it installed itself, so the client is bundled into one file and installed from a `.vsix`.

## Implementation

### Lexer

`src/lexer/lexer.hpp` pushes every comment's span onto `comments` as `skipWhitespaceAndComments` skips it, and `allComments` returns them.

### Diagnostics

`DiagnosticEngine` in `src/diagnostics/diagnostics.hpp` takes an `std::ostream* output`, `&std::cout` by default, and prints a diagnostic only when it isn't null. The server passes `nullptr`, since its stdout carries the protocol, and reads what was reported through the new `all`.

### Sema

- **Symbols.** `Symbol` in `src/sema/scope.hpp` gains `scope`, the span of the lexical scope it's declared in, which `Scope::insert` fills from the scope's own span, and `function`, the `FnDecl` a function symbol comes from. `Scope` takes its span in its constructor, and `inScope` takes the span to give the new scope: a function's, a block's, a match arm's, a loop's, or a refinement's, with the whole file for the global scope.
- **Records.** `src/sema/sema.hpp` adds `Use`, a span, its symbol, and `isDeclaration`; `Expectation`, a span and the type its context wanted; `Call`, a call by name with each argument's span; and `Body`, a function's span and the `owner` number its locals get. `makeSymbol` records every declaration as a use, `checkIdentifier` records every use, `checkExpr` records every expected type, `checkCall` records every call whose callee is a function's name, and `checkFn` records every function body. `allUses`, `allExpectations`, `allCalls`, and `allBodies` return them.

### Language Server

- **Transport.** `src/lsp/transport.hpp` has `readMessage`, which reads headers up to a blank line, keeping only `Content-Length`, then exactly that many bytes, and `writeMessage`, which writes the one header and the body through `llvm::json` and flushes.
- **Documents.** `src/lsp/document.hpp` has `Position`, a line and a UTF-16 column from 0, and `LineIndex`, which records where each line starts. `position` finds a byte offset's line by binary search and counts units across it, and `offset` walks a line back to a byte offset, where a column past a line's end is that line's end. `Document` owns one version's text, its tokens, comments, tree, `Sema`, and `LineIndex`, with a `DiagnosticEngine` that prints nowhere, and runs the whole front end in its constructor.
- **The server.** `Server` in `src/lsp/server.hpp` keeps every open document's text in `texts` and its checked version in `documents`. `didOpen` and `didChange` call `analyze`, which rebuilds the `Document` and publishes its diagnostics, where `applyChange` replaces the text between two positions, or the whole text when a change has no range. `didClose` drops the document and publishes empty diagnostics. `capabilities` announces incremental sync, change kind 2, and every feature, with signature help triggered by `(` and `,` and the semantic token legend. `handleRequest` replies to a request about a document that isn't open with null, to an unknown method with error -32601, and to a failed rename with -32803. `run` replies -32700 to a body that isn't JSON, ignores unknown notifications, and returns 0 on `exit` after `shutdown`, or 1 on `exit` without one or when the input ends.
- **Features.** `src/lsp/features.hpp` answers every request from a `Document`.
  - `rangeOf` and `locationOf` turn spans into the editor's ranges and locations. `useAt` finds the use under an offset, counting an offset just past a name, where the cursor sits after typing it.
  - `diagnostics` converts the engine's diagnostics.
  - `semanticTokens` encodes each token as five numbers, the line relative to the previous token's, the start relative to the previous token's on the same line, the length, the type, and the modifiers, from `TokenType` and `TokenModifier`, whose names are `tokenTypes` and `tokenModifiers`. A name sema couldn't resolve is a variable.
  - `render` shows a symbol as it's declared. `typeHelp` works out a type's meaning from its spelling, its width, a `u` for unsigned and a `w` for wrapping, or a float's encoding and a trailing `n` for one that holds a NaN. `keywordHelp` describes each keyword, and `builtinHelp` picks between them. `hover` shows `render` in a cherimoya code block, or a built-in's help.
  - `definition` returns the symbol's span, and `references` every use of the same symbol.
  - `rename` lexes the new name and refuses it unless it's one identifier, with `'<name>' isn't a name, so it can't replace '<old>'`. It refuses a name already declared in the same scope, `'<name>' is already declared in the same scope`, and a rename that changes a use's meaning, `renaming would make a use of '<old>' mean the other '<name>' instead` or `renaming would make a use of '<name>' mean the renamed '<old>' instead`, using `isVisible` and `isInside` on the two symbols' scopes. Otherwise it returns an edit for every use.
  - `documentSymbols` builds the outline through `outlineOf`, looking through attributes, with nested functions as children, kind 12.
  - `completion` takes the narrowest `Expectation` around the cursor, offers each visible name once, the innermost, with a `sortText` that puts a fitting name first, then every keyword and type with its help. `isVisible` uses the symbol's scope and `ownerAt`, the innermost `Body` around an offset.
  - `signatureHelp` finds the innermost `Call` around the cursor, finds each parameter as a range of the rendered label in order, so a parameter's text that also appears earlier isn't found twice, and makes the active parameter the one after every argument that ends before the cursor.

### Driver

`tools/cheri/main.cpp` runs `Server(std::cin, std::cout).run()` for `cheri lsp`, which takes no file and no options. `platform::binaryStdio` switches stdin and stdout to binary on Windows, since the protocol counts bytes and a translated line ending would throw every count off.

### Editors

`editors/vscode/package.json` declares the `cherimoya` language, its `.moya` extension, its icon, `icons/moya.svg`, and the `cherimoya.compilerPath` setting. `language-configuration.json` gives the comment and bracket configuration. `extension.js` starts a `vscode-languageclient` `LanguageClient` running `cheri lsp`. `npm run package` bundles the client and its one dependency into `dist/extension.js` with esbuild and packages it with `vsce` as `cherimoya.vsix`, which `code --install-extension cherimoya.vsix` installs.

### Build

`CMakeLists.txt` adds `src/lsp` to the include path and `tests/unit/lsp/test_lsp.cpp` to the unit tests. `.gitignore` ignores the extension's `node_modules`, lock file, `dist`, and `.vsix`.

## Tests

`tests/unit/lsp/test_lsp.cpp` checks positions across multibyte characters, diagnostics with notes and warnings, semantic tokens and their encoding, hover, including the help for types and keywords, definition and references, rename and each of its refusals, the outline, completion's visibility and ranking, signature help, and a whole session from `initialize` through incremental changes to `exit`. 14083 unit assertions and 52 programs, 0 failures.

## Current Limitations

- **Visibility gaps.** A top-level binding declared below a function isn't offered in that function, though sema lets the function use it, and a function's output names are offered in its body, though only its contracts can see them.
- **Whole-document checks.** Every change re-checks the whole document, which is instant at today's sizes.
- **One file.** Programs are a single file until modules exist, so every answer is about one document.
- **Missing features.** There's no formatting and no code actions, so the diagnostics that say how to fix a mistake can't apply the fix yet.
