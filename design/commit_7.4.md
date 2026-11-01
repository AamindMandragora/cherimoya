# Commit 7.4: Quick Fixes

## The Problem

Most of cherimoya's errors already say how to fix them: `cherimoya requires 'x' to be mutable to assign to it, please declare it with 'let x :: mut <- ...;'`. For a good share of them, that fix is one exact edit, the same every time, and typing it by hand is busywork. Commit 7.4 attaches those edits to the diagnostics themselves, so an editor can offer each one as a quick fix, applied in one click, and `cheri check --fix` can apply them all from the command line.

## Fixes Live on Diagnostics

A fix is a title and a list of edits, and an edit replaces a span's text, so an empty span inserts and empty text deletes. The stage that reports a mistake is the one that knows exactly where it is, so it builds the fix right there, from the same spans it reports, and the diagnostic carries it to whoever asks. Nothing reads a message back to work out what it meant. A diagnostic can have more than one fix when there's a real choice to make, and a diagnostic whose fix would be a guess, like "please use a wider type", has none.

Two helpers keep the edits tidy. `insertion` puts text at a location. `removal` deletes a span, and the whole line it's on when nothing else is, so removing a statement doesn't leave an empty line behind. `applyEdits` applies a fix's edits last first, so each one's offsets still mean what they did in the original text.

## The Fixes

| Diagnostic | Fix |
|---|---|
| touching operators that need a space | `x <--1` becomes `x <- -1` |
| `()` for an empty pack, `(a, b)` for a tuple | the parentheses become a pack's brackets |
| a binding with no initializer | `<- _` before its `;` |
| a name on a capture, an index, or a range element | the name and its `<-` come out |
| two operators that can't share a level without parentheses | two fixes, `(x a y) b z` and `x a (y b z)` |
| a value that's dropped | `_ <- ` in front of it |
| an assignment to a binding or parameter that isn't `mut` | `mut` in front of its type, or `:: mut` after its name when it has none |
| an incomplete match | a `_ -> { }` arm before the closing brace, on its own line when the brace is on its own |
| a `boolf` used as a condition, or converted to a `bool` | a cutoff, `x > 0.5`, with the value in parentheses unless it's a name, a call, or a literal, since `>` binds tighter than `&&` |
| statements after a yield, or arms after a catch-all | every statement or arm from there on comes out, but the functions, which are hoisted, so something before the yield can still call them |
| a program with no `main` | `let () -> main -> (code :: int) { 0 -| }` at the end |

A mix of operators that needs parentheses is reported once the operand after its second operator is parsed, since its fixes need to know where that operand ends, along with where the left side started and where its last operand began.

## In the Editor

The language server answers `textDocument/codeAction` with a quick fix for every fix of every diagnostic that touches the range the editor asks about, each one an edit to the document that names the diagnostic it resolves. A fix that's the only one its diagnostic has is preferred, which is what VS Code's Auto Fix applies, and what fix-all on save applies when that's turned on. The parentheses fix comes in two, so neither is preferred, and the editor asks which one.

## On the Command Line

`cheri check --fix prog.moya` rewrites the file with every preferred fix, then checks it again, since one fix can uncover another: a missing space can hide everything after it from the parser. A fix whose edits touch one already chosen waits for the next round, so two fixes never edit the same text at once. It stops when nothing's left to fix, says how many fixes it applied, and then checks the file as `check` always does, reporting whatever's left. `--fix` only goes with `check`, since it rewrites the file instead of building it.

## What Was Built

`src/diagnostics/diagnostics.hpp` gained edits, fixes, `insertion`, `removal`, and `applyEdits`, and every diagnostic carries its fixes. `src/lexer/lexer.hpp`, `src/parser/parser.hpp`, and `src/sema/sema.hpp` attach fixes to the diagnostics above, and the parser reports mixed operators once their right side is parsed. `src/lsp/features.hpp` turns fixes into code actions, `src/lsp/server.hpp` announces and answers them, and `tools/cheri/main.cpp` gained `--fix`.

`tests/unit/sema/test_sema.cpp` applies every fix and checks the text it gives, including both parentheses, a multi-line match, a cutoff that needs parentheses, and unreachable code removed whole lines at a time, and `tests/unit/lsp/test_lsp.cpp` checks that fixes come back as quick fixes, preferred or not. 14419 unit assertions and 59 programs, 0 failures.

## Current Limitations

A fix that would be a guess has no fix: a literal that doesn't fit, a parameter with no type, or a mismatch, where only the programmer knows which side is wrong. The missing `main` fix only comes from `cheri check --fix`, since the language server checks a file as a module, not as a program. There's no fix-all request in the language server itself, so an editor applies fixes one diagnostic at a time unless it does fix-all on its own.
