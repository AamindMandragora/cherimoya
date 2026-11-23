# Commit 12.9: Comments for Groups of Declarations

## The Feature

- **`##` comments.** A `##` documents every declaration below it at its level, up to a blank line:

```
## lengths the program only knows at run time
# the smallest
let two :: mut i64u <- 2;      # lengths…, then the smallest
let three :: mut i64u <- 3;    # lengths…
## big enough for the table
let four :: mut i64u <- 4;     # lengths…, then big enough for the table
```

- **Comments reach over attributes.** The `#` comments above a declaration still document it with its attributes between them, so a test's comment can sit above its `@test`.
- **Help on attributes.** Hovering an attribute's name shows it with its `@` and what it does:

```
@test
Marks a function in a `.toya` file as a test, which `cheri test` runs in its own process. `traps <- "text"` makes it pass by trapping with a message that holds the text, and `timeout <- 5` fails it after that many seconds.
```

- **Highlighting.** A `##` is a `docComment` in the tree-sitter grammar, which Zed colors as `comment.doc`, and the language server marks it with the `documentation` modifier, so it stands out from a `#` before and after the server answers. Emacs colors both as comments.

## Why

Commit 12.4 documents a name with the comments right above its declaration. That leaves no way to write one comment over several, like one over three lengths, `two`, `three`, and `four`, and a file's header, written right above its first declaration, would show up in that declaration's hover. A test's comment, written above its `@test`, wouldn't be its function's at all, since the attribute sits between them.

- **The order of a declaration's documentation** is every `##` above it, from the top of its run, the lines down to it with no blank line between, then the `#` comments right above it, the run of them with nothing between them and the declaration but its attributes, then the comment at the end of its line. `##` lines one after another are one comment, and each comment is a paragraph of its own.
- **A `##` below a declaration is never its,** so `four`'s doesn't reach `two` or `three`.
- **Levels by brackets.** A `##` is at a declaration's level when every bracket opened between the two closes there too, so a `##` over a function documents the declarations beside it, not its inputs or its locals, and one inside an earlier function's body doesn't reach past the body's `}`.
- **A run reaches over whatever's in it,** so a `##` over two functions documents both, however long the first one's body is, until a blank line ends it.
- **A blank line ends a group** because it's the separation a reader already sees, and it lets a file's header stand apart: every `.moya` and `.toya` file under `tests/` now has a one-line header and a blank line, so the header is no declaration's documentation, then a short comment over each test and helper saying what it checks or does, and a `##` over each group of declarations one comment describes.

## Implementation

### Language Server

`docOf` in `src/lsp/features.hpp` builds a declaration's documentation in three parts. `isGroup` tells a `##` from a `#`, and `words` strips either mark and the space after it. `alone` maps each line to the comment alone on it. Walking up from the declaration's line until `firstOn` finds a blank one, it gathers every `##` that `isLevelWith` places at its level, counting brackets in `document.tokens` between the comment and the declaration, joining consecutive lines with a newline and separate comments with a blank line. A second walk gathers the `#` lines right above, skipping lines that start with `@`, and stopping at anything else. The trailing comment follows as before.

`TokenModifier` gains `Documentation`, and `semanticTokens` marks a `##` comment with it. `attributeHelp` gives the text for `@test`, and `hover` uses it for a name right after an `@` token that touches it, showing the name with its `@`.

### Tree-sitter

`tree-sitter/grammar.js` adds `docComment`, `##` to the end of the line at a higher precedence than `comment`, which would read it too, and lists it in `extras`.

### Editors

`editors/zed/languages/cherimoya/highlights.scm` colors `docComment` as `comment.doc`, and `semantic_token_rules.json` maps a comment with the `documentation` modifier to `comment.doc` ahead of the plain comment rule. The spec and the README describe `##`.

## Tests

`tests/unit/lsp/test_lsp.cpp` checks a group's reach, a blank line ending it, a later `##` joining an earlier one, a function's inputs and locals left out, a comment reaching over `@test`, the help on `@test`, and the documentation modifier. `tree-sitter/test/corpus/declarations.txt` reads `##` as a `docComment`, a trailing one included. The tests' own comments follow the shape above. 14801 unit assertions, 9 programs, the run of `tests/test/`, 200 feature tests, 9 debug programs, all under valgrind, and the grammar's corpus, 0 failures.

## Current Limitations

- **No pages.** `##` documents declarations in hover, completion, and signature help, but nothing turns it into pages until there's a `cheri doc`.
- **A `#` above is documentation.** A `#` right above a declaration is still read as its documentation, so a note meant only for whoever reads the code has to be separated from it by a blank line.
