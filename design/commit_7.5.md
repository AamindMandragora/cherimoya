# Commit 7.5: Inlay Hints

## The Problem

Cherimoya infers a lot that the code doesn't say. `let total <- add(a, double(2));` never writes `int`, a call never names the parameter each argument goes to, and a pipe spread over several lines only says the type it ends with, wherever that goes. Hover answers each of those one name at a time. Commit 7.5 gives the editor inlay hints, the gray text it draws into the code without changing it, so all of that shows at once.

## The Hints

| Hint | Where | Example |
|---|---|---|
| an inferred type | right after the name of a binding that has no annotation | `let a :: int <- 1;` |
| a parameter's name | in front of each argument of a call | `add(b <- double(n <- 2))` |
| a pipe step's type | at the end of each line of a pipe spread over lines | `total :: int` then `\|> double :: int` |
| a scope's name | after the closing brace of a long function or named scope | `} # start` |

**Inferred types** are written the way an annotation is, so double-clicking one, in editors that support it, writes it into the file as a real `:: int`. A binding whose type is poisoned, because its initializer had an error or never finishes, gets no hint, since it would only show the error.

**Parameter names** are written the way a binding's initializer is, `b <-`, since that's what an argument does to its parameter. Calls don't take named arguments, so the hint is only for reading and never inserts anything. An argument that's just a name equal to its parameter's, like the `a` in `add(a, double(2))`, gets no hint, since it would say the same thing twice, and neither does a pipe's argument, which has no parentheses to sit inside.

**Pipe steps** are hinted for a forward pipe only, which reads top to bottom: the value it starts from and each step after it, where a step gets a hint only when it ends its line, with nothing after it but spaces and a comment. A step's hint goes before its comment. A pipe written on one line gets none, and neither does its last step, which gives the whole pipe's value, so that type already shows wherever the value goes, as a binding's inferred type or a parameter's name.

**Scope names** go after the closing brace of a function's body, and of a block, match, loop, or chunk with a name, once it spans at least 10 lines, since a shorter one's opening line is usually still on the screen. It reads like a comment, `# start`, since it is one in all but being there.

## How the Server Finds Them

Sema already records what the language server needs from it, and it records three more things now: every binding whose type was inferred, every step of a forward pipe with the type it gives, and every scope with a name. Calls were already recorded, for signature help, and now say whether they're a pipe. A chain like `x |> f |> g` is `(x |> f) |> g`, so its steps all start where the chain does, and the one that ends last is the whole pipe. The server answers `textDocument/inlayHint` with the hints in the range the editor asks about, in order, where kind 1 is a type and kind 2 a parameter, so an editor can style or turn off each.

## What Was Built

`src/sema/sema.hpp` gained `Step` and `Label`, records inferred bindings, pipe steps, and named scopes, and marks a pipe's call. `src/lsp/features.hpp` gained `positionAt`, which `rangeOf` uses too, `endsLine`, and `inlayHints`, and `src/lsp/server.hpp` announces and answers inlay hint requests. The README and the Emacs mode mention the hints, which eglot shows on its own and lsp-mode shows with `lsp-inlay-hint-enable`.

`tests/unit/lsp/test_lsp.cpp` checks every kind of hint in one program: an inferred type and its edit, parameter names that skip an argument with the parameter's name and a pipe's argument, the steps of a pipe spread over lines, including one before a comment and none for a pipe on one line or for the last step, a label on a long function and none on short ones, and a range that leaves hints out. 14432 unit assertions and 59 programs, 0 failures.

## Current Limitations

A pipe going the other way, `g <| f <| x`, reads from the bottom up, and gets no step hints. The 10-line threshold for a scope's name is fixed rather than a setting. Destructuring isn't checked yet, so a pattern's names get their hints once commit 11 brings it, and a for-each loop's binder once commit 10 brings those.
