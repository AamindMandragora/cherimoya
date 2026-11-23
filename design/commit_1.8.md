# Commit 1.8: Named Arguments and Defaults

## The Feature

- **Named arguments.** `f(b <- 2)` gives the input `b` the value 2. Unnamed arguments fill the inputs in order, then named ones go to the inputs they name, in any order, so `sum(c <- 10, a <- 2)` is fine.
- **Defaults.** An input's default comes after its type, `(b :: int <- a * 2)`, and a call that leaves the input out gets it instead. A default sees the inputs before it, so `(a :: int, b :: int <- a * 2, c :: int <- a + b)` works.
- **Pipes into calls.** `x |> clamp(hi <- 10)` is `clamp(x, hi <- 10)`, and `clamp(hi <- 10) <| x` is the same call. The piped value is the first unnamed argument.
- **Written order.** Arguments run in the order they're written, not their inputs' order, so in `pick(b <- 1 / z, a <- i8(300))` the division traps first.
- **Errors.** Each way to get it wrong says what to do:
  - an unnamed argument after a named one: `cherimoya requires unnamed arguments to come before named ones, please name this one too`
  - two arguments for one input: `cherimoya requires each input to get one argument, please remove one of the arguments for 'a'`, with a note at the first
  - a name that isn't an input: `cherimoya requires a named argument to name an input of 'f', please use one of its inputs: 'a', 'b'`
  - the wrong number of arguments: `cherimoya expected 2 arguments in the call to 'add', got 1 instead`, or as a range when the function has defaults, `expected at least 1` or `expected at most 3`
  - enough arguments that still leave out an input without a default, which only naming can do, like `f(b <- 3)`: `cherimoya requires the call to 'f' to pass 'a', please add an argument for it`
  - a default on an output: `cherimoya requires defaults to be on a function's inputs, please remove '<- 1'`, with a fix that removes it
  - a named conversion: `cherimoya requires a conversion's value to be unnamed, please remove 'x <-'`, with a fix
- **Language server.** Signatures show defaults, `let (a :: int, b :: int <- 10) -> f -> (r :: int)`, and hints show the defaults a call leaves out.

## Why

Positional calls only say what they do to someone who remembers the signature: `clamp(x, 0, 10)`. And a function with an input that's almost always the same value makes every caller write it anyway. Naming an argument says where it goes, and a default lets a caller leave out what it doesn't care about.

- **`<-` names an argument** the way it gives anything else a value. A name and `<-` in front of an argument always name it, the same as in a pack, so a construction inside a call goes in parentheses: `f((Point <- [0, 0]))`.
- **Unnamed before named,** since an unnamed argument after a named one could only mean "the next input", and which input that is depends on which ones were named. The error has no fix, since which name it should get is the programmer's call.
- **Defaults anywhere in the signature.** Leaving out one that isn't last means naming the ones after it: with `(lo :: int <- 0, x :: int)`, `f(x <- 5)` uses `lo`'s default, and `f(5)` is `lo`, with `x` missing.
- **Every input still has a type,** so a signature always says what it takes, and the default is checked against it.
- **A default sees only the inputs before it,** and nothing a yield could target, since it runs before the function's body does.
- **Written order** is the order a reader sees, so it's the order things run.
- **Each default is a function of its own** instead of a copy of its expression at every call, so a default that calls its own function, like `(n :: int, m :: int <- inc(n, 1))`, is ordinary recursion at run time, not an expansion that never ends at compile time. At `-O3` LLVM inlines the small ones, so a default costs nothing over writing the argument.

## Implementation

### Parser

- **Arguments.** `src/parser/expr.hpp` adds `Elem`, a value with an optional name, which packs, calls, and attributes share in place of the pack's own element type. `parseArg` reads `name <-` in front of an argument, and `parseArgList` gives back `Elem`s, so `CallExpr::args` and `AttrStmt::args` hold them. Attributes carry their named arguments until attributes are checked.
- **Defaults.** `Param` in `src/parser/type.hpp` gains `fallback`, the input's default, and `parseParamList` reads `<- value` after the type. Record fields have none.
- **Filled-in inputs.** `CallExpr` and `PipeExpr` gain `inputs`, which sema fills with the value each of the function's inputs takes, null where it takes its default.

### Sema

- **Arguments.** `Argument` is an argument as sema checks it, its name, value, and span, and `argumentsOf` builds them from a call's `Elem`s. A pipe's value is an `Argument` with no name, followed by the arguments of a call in the function's place.
- **Matching.** `matchArguments` works out which input each argument goes to: unnamed ones in order, named ones by name, an unnamed one after a named one to the first input nothing has gone to yet, so the inputs after it aren't reported as missing too. It reports each error above, giving the count error through `countMismatch` with `at least ` or `at most ` when the function has defaults, and only for the first argument past the end. It fills `inputs` and gives back each argument's input.
- **Calls.** `checkCall` takes the arguments, the `inputs` to fill, and where the call's `)` is, and checks each argument against the type of the input it goes to. `checkConversion` takes `Argument`s and rejects a named one.
- **Defaults.** `checkFn` calls `checkDefault` on each input's default before declaring the input, so it sees the inputs before it. `checkDefault` sets `targets` aside while it checks, so nothing inside can yield to the function. A default on an output reports the error with a fix removing it.
- **Language server data.** `CallArg` records each argument's span, whether it's named, and its input, and `Call` gains `close`, the position of its `)`, none for a pipe of a bare name.

### Codegen

- **Default functions.** `declare` creates an internal function for each default in `defaults`, taking the inputs before it and giving the input's type, named after its function and its input the way the call would read it, `cheri.prog.sum(b)`. No name holds parentheses, so it can't collide with a nested function. Functions nested in a default are declared under the function's name.
- **Defining them.** `defineDefault` copies each earlier input into its slot, so the default reads them the way the body does, and returns the default's value. A default that never finishes, like one that traps, ends in `unreachable`. `generate` defines every function's defaults after its body.
- **Calls.** `emitArguments` sorts the written arguments by where they start and evaluates them in that order, then fills the inputs in order, where an input with no argument calls its default with the inputs filled so far. Calls and pipes both go through it. `emitConversion` handles a type called on a value, `i8(x)` and `x |> i8`.

### Language Server

`src/lsp/features.hpp` adds `excerpt`, a span's text cut at its first line break with `...`, `inputText`, which writes an input as `b :: int <- 10`, and `defaultOf`, which finds a parameter's default. `render` takes the document and shows defaults in hover, completion, the outline, and signature help, and hovering over an input shows its own. Signature help highlights the input of the argument the cursor is in, and between arguments, the first input nothing before the cursor has gone to.

The parameter hints follow the new rules. A named argument gets no hint, since it already says where it goes. Only the hint on the last unnamed argument writes itself in on a double-click, since naming any earlier one would put an unnamed argument after a named one. The defaults a call leaves out show before its `)`, as `, c <- a + b`, so a call says everything it passes. A pipe of a bare name has no `)` to put them in.

### Docs

`README.md` lists named arguments, defaults, and pipes into calls.

## Tests

`tests/unit/parser/test_parser.cpp` checks named arguments, a construction in parentheses, a pipe into a call, and defaults, and the printer tests print them. `tests/unit/sema/test_sema.cpp` checks every way to pass arguments and every way to get them wrong, defaults that see the inputs before them and not after, and the fixes. `tests/unit/lsp/test_lsp.cpp` checks the hints, signature help, and hover on an input. `tests/run/arguments.moya` uses all of it, including a default that calls its own function, and `tests/run/argument_order_trap.moya` traps in the argument written first. 14476 unit assertions and 61 programs, 0 failures.

## Current Limitations

- **Defaults can't yield.** A default runs in a function of its own, so one can't yield out of the function around it.
- **Record fields.** Records will want defaults on their fields, which come once records exist.
