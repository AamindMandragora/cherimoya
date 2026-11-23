# Commit 12.4: Doc Comments on Hover

## The Feature

- **Comments document names.** A name's documentation is the comments people already write next to it, with no special syntax: the run of comment lines directly above its declaration, and the comment at the end of its line, `let limit <- 4096;  # bytes per chunk`.

```
# adds two numbers
let (
    a :: int,  # the first
    b :: int
) -> add -> (r :: int) { a + b -| }
```

- **Hover** shows the declaration in a cherimoya code block and the documentation under it.
- **Completion** shows each name's documentation next to it, the way built-in types and keywords already show their help.
- **Signature help** shows the function's documentation under its signature and each parameter's beside that parameter, so while typing `add(1, ` the editor says what `b` is for.

## Why

Hover from commit 12.2 shows a name the way it's declared, `let (a :: int, b :: int) -> add -> (r :: int)`, but not what it's for. That's usually written right next to it already, and the lexer keeps every comment's span, so the server can hand those comments to the editor.

- **A blank line ends the run above.** A comment that heads a section isn't taken as the docs of whatever comes first in it. Each line of the run has to be alone on its line.
- **Functions from their `let`, everything else from its name.** A function is declared from its `let`, so the comments above that line are the function's, and anything else is declared at its name, which is how a parameter spread onto its own line gets its own comments.
- **A trailing comment goes to the nearest declaration.** It belongs to the name on its line unless another declaration sits between the two, so in `let (n :: int) -> f -> (r :: int) { n -| }  # the identity` the comment is `f`'s, not `n`'s. A function's own outputs and locals are part of its declaration, so they don't take a comment away from it.
- **Markdown as written.** Each line loses its `#` and the one space after it, and the lines join with line breaks, so the comment reaches the editor the way its author wrote it.
- **Worked out on request.** The comments are already sorted by where they start, so finding the run above a declaration walks back from the last comment before it, and nothing new is stored.

## Implementation

### Language Server

- **`docOf`** in `src/lsp/features.hpp` finds a symbol's documentation. It starts from `symbol.function->span.start` for a function and `symbol.span.start` otherwise, then walks `document.comments` backwards, collecting each comment that sits alone on the line directly above the last one taken, until a line breaks the run. The lines join with `\n`. It then takes the first comment on the name's line after the name, unless a declaration from `allUses` that isn't the symbol's own, and isn't inside the function for a function symbol, starts between the name and the comment, and appends it after a blank line. `words` strips the `#`, one space, and trailing spaces and `\r`, and `isAlone` checks that only whitespace precedes a comment on its line.
- **`docValue`** wraps the documentation as markdown, or null when there's none.
- **`hover`** appends `docOf` under the code block. **`completion`** sets each name's `documentation` from `docValue`. **`signatureHelp`** sets each parameter's `documentation` from its input's symbol and the signature's from the function's.

The README's description of the language server mentions it.

## Tests

`tests/unit/lsp/test_lsp.cpp` checks the comments above a function and the blank line that ends them, a parameter on its own line, a comment at the end of a binding's and a function's line, a parameter whose function's comment isn't its own, a name with no documentation, and the same documentation in completion and signature help. 14386 unit assertions and 59 programs, 0 failures.

## Current Limitations

- **Attributes.** A comment above an attribute isn't the documentation of the declaration the attribute is on.
- **Initializers that declare names.** A comment at the end of a binding's line goes to the last name its initializer declares on that line, like `let x <- { let y <- 1; y };  # docs`, since a binding doesn't know where its initializer ends.
- **No per-parameter syntax.** A function's comment can't document one parameter, the way some languages use `@param`.
