# Commit 7.3: Documentation Comments

## The Problem

Commit 7.1's hover shows a name the way it's declared, `let (a :: int, b :: int) -> add -> (r :: int)`, but not what it's for. That's usually written right next to it already, as a comment above the declaration or at the end of its line, and the lexer keeps every comment's span. Commit 7.3 hands those comments to the editor, so hovering over a name, completing it, or calling it shows what its author wrote about it.

## Which Comments Document a Name

There's no special syntax: a name's documentation is the comments people already write.

- **The comments right above it:** the run of comment lines directly above its declaration, each alone on its line, with no blank line between them and the declaration. A blank line ends the run, so a comment that heads a section isn't taken as the docs of whatever comes first in it.
- **The comment at the end of its line:** `let limit <- 4096;  # bytes per chunk`.

```
# adds two numbers
let (
    a :: int,  # the first
    b :: int
) -> add -> (r :: int) { a + b -| }
```

Both kinds go with the declaration they belong to. A function is declared from its `let`, so the comments above that line are the function's, and anything else is declared at its name, which is how a parameter spread onto its own line gets its own comments. A comment at the end of a line belongs to the name on that line unless another declaration sits between the two, so in `let (n :: int) -> f -> (r :: int) { n -| }  # the identity` the comment is `f`'s, not `n`'s. A function's own outputs and locals are part of its declaration, so they don't take a comment away from it. Each line loses its `#` and the one space after it, and the lines join with line breaks, so the comment reaches the editor as markdown, the way its author wrote it.

## Where It Shows

- **Hover** shows the declaration in a cherimoya code block and the documentation under it.
- **Completion** shows each name's documentation next to it, the way built-in types and keywords already show their help.
- **Signature help** shows the function's documentation under its signature, and each parameter's beside that parameter, so while typing `add(1, ` the editor says what `b` is for.

Everything is worked out from the checked document when it's asked for: the comments are already sorted by where they start, so finding the run above a declaration walks back from the last comment before it.

## What Was Built

`src/lsp/features.hpp` gained `docOf`, which finds a symbol's documentation, and hover, completion, and signature help show it. The README's description of the language server mentions it. `tests/unit/lsp/test_lsp.cpp` checks the comments above a function and the blank line that ends them, a parameter on its own line, a comment at the end of a binding's and a function's line, a parameter whose function's comment isn't its own, a name with no documentation, and the same documentation in completion and signature help. 14386 unit assertions and 59 programs, 0 failures.

## Current Limitations

A comment above an attribute isn't the documentation of the declaration the attribute is on. A comment at the end of a binding's line goes to the last name its initializer declares, when the initializer declares any on that line, like `let x <- { let y <- 1; y };  # docs`, since a binding doesn't know where its initializer ends. There's no syntax for documenting one parameter from the function's comment, the way some languages use `@param`.
