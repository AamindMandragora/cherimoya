# Commit 12.6: Inlay Hints

## The Feature

- **Inferred types.** Right after the name of a binding with no annotation, `let a :: int <- 1;`. Double-clicking one, in editors that support it, writes it into the file as a real `:: int`.
- **Parameter names.** In front of each argument of a call, `add(b <- double(n <- 2))`, written the way a binding's initializer is, since that's what an argument does to its parameter.
- **Pipe steps.** At the end of each line of a forward pipe spread over lines, `total :: int` then `|> double :: int`, before the line's comment if it has one.
- **Scope names.** After the closing brace of a function's body, or of a named block, match, loop, or chunk, once it spans at least 10 lines, `} # start`.

## Why

Cherimoya infers a lot that the code doesn't say. `let total <- add(a, double(2));` never writes `int`, a call never names the parameter each argument goes to, and a pipe spread over several lines only says the type it ends with, wherever that goes. Hover answers each of those one name at a time. Inlay hints, the gray text an editor draws into the code without changing it, show all of it at once.

- **Types written as annotations** make the hint the exact text to insert, so accepting it is one edit. A binding whose type is poisoned, because its initializer had an error or never finishes, gets no hint, since it would only show the error.
- **Parameter hints never insert anything,** since calls don't take named arguments. An argument that's just a name equal to its parameter's, like the `a` in `add(a, double(2))`, gets no hint, since it would say the same thing twice, and neither does a pipe's argument, which has no parentheses to sit inside.
- **Only forward pipes get step hints,** since they read top to bottom. A step gets one only when it ends its line, with nothing after it but spaces and a comment. A pipe on one line gets none, and neither does its last step, which gives the whole pipe's value, so that type already shows wherever the value goes, as a binding's inferred type or a parameter's name.
- **The 10-line threshold** skips short scopes, whose opening line is usually still on the screen. The label reads like a comment, `# start`, since it is one in all but being there.
- **Hint kinds** are 1 for a type and 2 for a parameter, so an editor can style or turn off each.

## Implementation

### Sema

`src/sema/sema.hpp` records three more things for the language server, read through `allInferred`, `allSteps`, and `allLabels`:

- **`inferred`.** `checkLet` pushes a binding's symbol when it has no written type and its type isn't poisoned.
- **`steps`.** A `Step` is a span and the `SemaType` it gives. Checking a forward pipe pushes the pipe itself, and the value it starts from when that isn't a forward pipe too, skipping poisoned types. A chain like `x |> f |> g` is `(x |> f) |> g`, so its steps all start where the chain does, and the one that ends last is the whole pipe.
- **`labels`.** A `Label` is a span and a name. A function's body pushes one with the function's name, and `checkScope`, `checkMatch`, and `checkRepeat` push one for a scope with a name.

`Call` gains `isPipe`, which `checkCall` takes as a defaulted parameter and the pipe case passes as true.

### Language Server

`src/lsp/features.hpp` adds `positionAt`, an offset as the editor's position, which `rangeOf` now uses too, and `endsLine`, which checks that nothing but spaces, tabs, and a comment follows an offset on its line. `inlayHints` builds the hints from the four records: a type hint with `paddingLeft` and a `textEdits` entry inserting ` :: T` at the end of the name, a parameter hint `name <-` with `paddingRight` for each non-pipe call's argument whose text isn't the parameter's name, a type hint for each step another step continues, one starting at the same offset and ending later, when `endsLine` holds after it, and a `# name` label for each scope spanning at least `labelLines` lines. It sorts them by offset with `stable_sort` and keeps those inside the requested range. `src/lsp/server.hpp` announces `inlayHintProvider` and answers `textDocument/inlayHint` through the same range handling as `textDocument/codeAction`.

### Editors

The README lists the hints. `editors/emacs/cherimoya-mode.el` mentions them: eglot shows them on its own, and lsp-mode with `lsp-inlay-hint-enable`.

## Tests

`tests/unit/lsp/test_lsp.cpp` checks every kind of hint in one program: an inferred type and its edit, parameter names that skip an argument with the parameter's name and a pipe's argument, the steps of a pipe spread over lines, including one before a comment and none for a pipe on one line or for the last step, a label on a long function and none on short ones, and a range that leaves hints out. The session test checks that `inlayHintProvider` is announced. 14432 unit assertions and 59 programs, 0 failures.

## Current Limitations

- **Backward pipes.** `g <| f <| x` reads from the bottom up and gets no step hints.
- **Fixed threshold.** The 10-line threshold for a scope's name is a constant rather than a setting.
- **Later binders.** A destructured pattern's names get their hints once destructuring is checked, and a for-each loop's binder once for-each loops exist.
