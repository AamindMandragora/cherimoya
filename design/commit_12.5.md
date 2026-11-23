# Commit 12.5: Quick Fixes

## The Feature

- **Fixes on diagnostics.** A diagnostic whose fix is one exact edit carries that edit, so an editor offers it as a quick fix, applied in one click:

| Diagnostic | Fix |
|---|---|
| touching operators that need a space | `x <--1` becomes `x <- -1` |
| `()` for an empty pack, `(a, b)` for a tuple | the parentheses become a pack's brackets |
| a binding with no initializer | `<- _` before its `;` |
| a name on a capture, an index, or a range element | the name and its `<-` come out |
| two operators that can't share a level without parentheses, including `\|>` and `<\|` | two fixes, `(x a y) b z` and `x a (y b z)` |
| a value that's dropped | `_ <- ` in front of it |
| an assignment to a binding or parameter that isn't `mut` | `mut` in front of its type, or `:: mut` after its name when it has none |
| an incomplete match | a `_ -> { }` arm before the closing brace, on its own line when the brace is on its own |
| a `boolf` used as a condition, or converted to a `bool` | a cutoff, `x > 0.5`, with the value in parentheses unless it's a name, a call, or a literal, since `>` binds tighter than `&&` |
| statements after a yield, or arms after a catch-all | every statement or arm from there on comes out, but the functions, which are hoisted, so something before the yield can still call them |
| a program with no `start` | `let () -> start -> (code :: int) { 0 -| }` at the end |

- **Preferred fixes.** A fix that's the only one its diagnostic has is preferred, which is what VS Code's Auto Fix applies, and what fix-all on save applies when that's turned on. The parentheses fix comes in two, so neither is preferred and the editor asks which one.
- **`cheri check --fix prog.moya`** rewrites the file with every preferred fix, checks it again, and repeats until nothing's left to fix, then prints `cheri: applied 3 fixes to 'prog.moya'` and checks the file as `check` always does, reporting whatever's left. `--fix` with any other command fails with `'--fix' only goes with 'check', since it rewrites the file instead of building it`.

## Why

Most of cherimoya's errors already say how to fix them: `cherimoya requires 'x' to be mutable to assign to it, please declare it with 'let x :: mut <- ...;'`. For a good share of them that fix is one exact edit, the same every time, and typing it by hand is busywork.

- **The stage that reports a mistake builds its fix.** It knows exactly where the mistake is, so it builds the edits from the same spans it reports, and the diagnostic carries them to whoever asks. Nothing reads a message back to work out what it meant.
- **No fix for a guess.** A diagnostic like "please use a wider type" has no fix, since only the programmer knows the right answer. A diagnostic has more than one fix only when there's a real choice.
- **Mixed operators are reported late.** A mix that needs parentheses is reported once the operand after its second operator is parsed, since the fixes need to know where that operand ends, along with where the left side started and where its last operand began.
- **Edits applied last first.** Each edit's offsets still mean what they did in the original text.
- **Removing a statement removes its line.** Deleting a statement that's alone on its line deletes the line too, so no empty line is left behind.
- **Repeated rounds.** One fix can uncover another, since a missing space can hide everything after it from the parser. A fix whose edits touch one already chosen in a round waits for the next, so two fixes never edit the same text at once.

## Implementation

### Diagnostics

`src/diagnostics/diagnostics.hpp` adds `Edit`, a span and its replacement text, so an empty span inserts and empty text deletes, and `Fix`, a title and its edits. `insertion` puts text at a location. `removal` deletes a span, widening it to the whole line and its newline when only spaces and tabs share the line. `applyEdits` sorts edits by start, last first, and applies them. `Diagnostic` gains `fixes`, and `printError` and `printWarning` take them after the notes.

### Lexer

The whitespace error between operators carries `Write '<a> <b>'`, which inserts a space before the second operator.

### Parser

- **Packs.** An empty `()` gets `Write '[]'`, and a closed parenthesized list with a top-level comma gets `Write it as a pack, '[...]'`, both replacing the parentheses with brackets.
- **Unnamed elements.** `unnaming` removes an element's name, its `<-`, and the spaces after them, titled `Remove '<name> <-'`, for captures, index arguments, and range elements.
- **Initializers.** A binding with no initializer before its `;` gets `Leave it uninitialized with '<- _'`, which inserts ` <- _`.
- **Mixed operators.** `checkMixing` becomes `needsParens`, which only answers whether two operators on a level need parentheses. The binary operator case records the mix, the left side's span, and `lastOperandStart`, the start of the left side's last operand when it's a binary, extra binary, or pipe expression, then `reportMixed` reports after the right side is parsed, with `groupings` building `Write '(x a y) b z'` and, when there's a last operand, `Write 'x a (y b z)'`. Mixing `|>` and `<|` reports the same way.

### Sema

- **Helpers.** `textOf` gives a span's source text. `cutoff` builds `Compare it with a cutoff, '> 0.5'`, parenthesizing anything that isn't an identifier, a call, or a literal. `mutability` looks past the symbol's name: after `::` it inserts `mut `, before `<-` it inserts ` :: mut`, titled `Make '<name>' mutable`. `catchAllArm` builds `Add a '_ -> { }' arm`, on its own line indented one level past the closing brace when the brace is alone on its line, or right before the brace otherwise.
- **Where they're used.** `expectBool` attaches `cutoff` to a `boolf` condition, and the conversion check to a `boolf` becoming a `bool`. The unreachable statement warning carries `Remove the statements that never run`, a `removal` for every later statement but functions, and the unreachable arm warning `Remove the arms that never run`, a `removal` for every arm from there on. An incomplete match gets `catchAllArm`, a dropped value `Discard it with '_ <- '`, and an assignment to something not `mut` `mutability`. The missing entry point fix appends the function after one or two newlines, depending on how the file ends.

### Language Server

`diagnosticOf` in `src/lsp/features.hpp` converts one diagnostic, shared by `diagnostics` and `codeActions`. `codeActions` returns a `quickfix` for every fix of every diagnostic whose span touches the requested range, with `isPreferred` set when it's the diagnostic's only fix, the diagnostic it resolves, and a workspace edit. `src/lsp/server.hpp` announces `codeActionProvider` with the kind `quickfix` and answers `textDocument/codeAction`, replying with an empty list when the request has no range.

### Driver

`tools/cheri/main.cpp` adds `--fix`, which sets `Options::isFixing`. `fixFile` runs the whole front end as a program, collects every preferred fix whose edits don't touch one already chosen, writes the file with `applyEdits`, and repeats, stopping when a round fixes nothing or after 10 rounds, then prints how many fixes it applied before `check` runs as usual.

## Tests

`tests/unit/sema/test_sema.cpp` applies every fix and checks the text it gives, including both parentheses, a multi-line match, a cutoff that needs parentheses, and unreachable code removed whole lines at a time. `tests/unit/lsp/test_lsp.cpp` checks that fixes come back as quick fixes, preferred or not. 14419 unit assertions and 59 programs, 0 failures.

## Current Limitations

- **Guesses have no fix.** A literal that doesn't fit, a parameter with no type, or a mismatch, where only the programmer knows which side is wrong.
- **The missing `start` fix** only comes from `cheri check --fix`, since the language server checks a file as a module, not as a program.
- **No fix-all request.** The language server has none, so an editor applies fixes one diagnostic at a time unless it does fix-all on its own.
