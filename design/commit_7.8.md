# Commit 7.8: Comments for Groups of Declarations

## The Problem

Commit 7.3 documents a name with the comments right above its declaration, so a comment written over several, like one over three lengths, `two`, `three`, and `four`, only reached the first, and a file's header, written right above its first declaration, showed up in that declaration's hover. A test's comment, written above its `@test`, wasn't its function's at all, since the attribute sat between them. Commit 7.8 gives a group of declarations a comment of its own, `##`, lets a declaration's comments reach over its attributes, and says what an attribute does when it's hovered.

## Which Comments Document a Name

A `##` comment documents every declaration below it at its level, up to a blank line:

```
## lengths the program only knows at run time
# the smallest
let two :: mut i64u <- 2;      # lengths…, then the smallest
let three :: mut i64u <- 3;    # lengths…
## big enough for the table
let four :: mut i64u <- 4;     # lengths…, then big enough for the table
```

A declaration's documentation is, in order:

- **Every `##` above it,** from the top of its run, the lines down to it with no blank line between, where `##` lines one after another are one comment, and each comment is a paragraph of its own. A `##` below a declaration is never its, so `four`'s doesn't reach `two` or `three`.
- **The `#` comments right above it,** the run of them with nothing between them and the declaration but its attributes, so a test's comment can sit above its `@test`.
- **The comment at the end of its line,** as before.

A `##` is at a declaration's level when every bracket opened between the two closes there too, so a `##` over a function documents the declarations beside it, not its inputs or its locals, and one inside an earlier function's body doesn't reach past the body's `}`. A run reaches over whatever's in it, so a `##` over two functions documents both, however long the first one's body is, until a blank line ends it.

## Attributes

Hovering an attribute's name shows it with its `@` and what it does:

```
@test
Marks a function in a `.toya` file as a test, which `cheri test` runs in its own process. `traps <- "text"` makes it pass by trapping with a message that holds the text, and `timeout <- 5` fails it after that many seconds.
```

## Highlighting

A `##` comment is a `docComment` in the tree-sitter grammar, which Zed colors as `comment.doc`, and the language server marks it with the `documentation` modifier, so it stands out from a `#` before and after the server answers. Emacs colors both as comments.

## Comments in the Tests

Every `.moya` and `.toya` file under `tests/` follows the same shape: a one-line header and a blank line, so the header is no declaration's documentation, then a short comment over each test and helper saying what it checks or does, where the header used to list everything at once, and a `##` over each group of declarations one comment describes.

## What Was Built

`src/lsp/features.hpp`'s `docOf` gathers a declaration's `##` comments at its level and reaches over its attributes, `attributeHelp` says what `@test` does, hover shows it, and semantic tokens mark a `##` as documentation. `tree-sitter/grammar.js` reads `##` as a `docComment`, with a case in the corpus, and Zed's highlights and semantic token rules color it. `tests/unit/lsp/test_lsp.cpp` checks a group's reach, a blank line ending it, a later `##` joining an earlier one, a function's inputs and locals left out, a comment reaching over `@test`, the help on `@test`, and the documentation modifier. The tests' comments follow the shape above. The spec and the README describe `##`. 14801 unit assertions, 9 programs, the run of `tests/test/`, 200 feature tests, 9 debug programs, all under valgrind, and the grammar's corpus, 0 failures.

## Current Limitations

`##` documents declarations in hover, completion, and signature help, but there's no `cheri doc` yet to turn it into pages. A `#` right above a declaration is still read as its documentation, so a note meant only for whoever reads the code has to be separated from it by a blank line.
