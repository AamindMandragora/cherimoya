# Commit 4.3: Named Arguments and Defaults

## The Problem

Commit 4.1's calls are positional: `clamp(x, 0, 10)` only says what it does to someone who remembers `clamp`'s signature, and a function with an input that's almost always the same value makes every caller write it anyway. Commit 4.3 lets a call name its arguments, `clamp(x, hi <- 10)`, and lets an input have a default, `(lo :: int <- 0)`, which a call that leaves the input out gets instead.

## Named Arguments

An argument is named the way anything else gets a value, with `<-`: `f(b <- 2)` gives `b` the value 2. Unnamed arguments go to the inputs in order, then named ones go to the inputs they name, in any order, so `sum(c <- 10, a <- 2)` is fine. Every unnamed argument comes before every named one, since an unnamed argument after a named one could only mean "the next input", and which input that is depends on which ones were named. Breaking that rule is `cherimoya requires unnamed arguments to come before named ones, please name this one too`, with no fix, since which name it should get is the programmer's call. An input gets one argument, so `f(1, a <- 2)` is an error, and a name that isn't one of the inputs says which ones are.

A name and `<-` in front of an argument always name it, the same as in a pack, so a construction inside a call goes in parentheses: `f((Point <- [0, 0]))`. A conversion takes one unnamed value, and attributes take named arguments too, which they carry along until attributes are checked.

The arguments run in the order they're written, not their inputs' order, so in `pick(b <- 1 / z, a <- i8(300))` the division traps first. The reader sees `b`'s argument first, so that's the one that runs first.

## Defaults

An input's default comes after its type, `(b :: int <- a * 2)`, and an input with one can be left out of a call, wherever it is in the signature. Since unnamed arguments fill the inputs in order, leaving out one that isn't last means naming the ones after it: with `(lo :: int <- 0, x :: int)`, `f(x <- 5)` uses `lo`'s default, and `f(5)` is `lo`, with `x` missing. Every input still has a type, so a signature always says what it takes, and the default is checked against it.

A default runs when the call leaves its input out, and it sees the inputs before it, so `(a :: int, b :: int <- a * 2, c :: int <- a + b)` works, and the inputs after it are out of scope. It sees nothing a yield could target, since it isn't inside the function's body. A default on an output is `cherimoya requires defaults to be on a function's inputs, please remove '<- 1'`, with a fix that removes it.

A call with the wrong number of arguments says how many the function takes: exactly, `cherimoya expected 2 arguments in the call to 'add', got 1 instead`, or as a range when it has defaults, `expected at least 1` or `expected at most 3`. A call that has enough arguments but still leaves out an input without a default, which only naming can do, like `f(b <- 3)`, says which: `cherimoya requires the call to 'f' to pass 'a', please add an argument for it`.

## Pipes Into Calls

A pipe's right side can be a call that gives every argument but the piped one: `x |> clamp(hi <- 10)` is `clamp(x, hi <- 10)`, and `clamp(hi <- 10) <| x` is the same call, since `f <| x` is `x |> f`. The piped value is the first unnamed argument, so `x |> f(a <- 2)` gives `a` two arguments. A bare name's pipe, `x |> f`, works with defaults too, since everything after the first input can be one.

## Codegen

Each default becomes a function of its own, which takes the inputs before it and gives the default's value, named after its function and its input the way the default is written, `cheri.prog.sum.b<-`. No cherimoya name holds `<-`, so it can't collide with a nested function. A call evaluates its arguments in the order they're written, then fills its inputs in order, where an input with no argument calls its default with the inputs filled so far. Making the default a function instead of copying its expression into every call means a default that calls its own function, like `(n :: int, m :: int <- inc(n, 1))`, is ordinary recursion at run time, not an expansion that never ends at compile time. At `-O3`, LLVM inlines the small ones, so a default costs nothing over writing the argument.

## The Language Server

Signatures show defaults, `let (a :: int, b :: int <- 10) -> f -> (r :: int)`, in hover, completion, the outline, and signature help, and hovering over an input shows its own, `b :: int <- 10`, where a default written over several lines shows its first line and `...`. Signature help highlights the input an argument names, and between arguments, the first input nothing before the cursor has gone to.

Commit 7.5's parameter hints follow the new rules. A named argument gets no hint, since it already says where it goes. The hint on the last unnamed argument writes itself in on a double-click, since naming any earlier one would put an unnamed argument after a named one. The defaults a call leaves out show before its `)`, as `, c <- a + b`, so a call says everything it passes. A pipe of a bare name has no `)` to put them in.

## What Was Built

`src/parser/expr.hpp` gained `Elem`, a value that can be named, which packs, calls, and attributes share, and calls and pipes gained the inputs sema fills in. `src/parser/type.hpp` gave `Param` its default, and `src/parser/parser.hpp` parses named arguments and defaults. `src/sema/sema.hpp` gained `Argument`, `matchArguments`, `countMismatch`, and `checkDefault`, checks pipes into calls, and records each argument's input for the language server. `src/codegen/codegen.hpp` gained `emitArguments`, `emitConversion`, and `defineDefault`, and declares every default as a function. `src/lsp/features.hpp` gained `excerpt`, `inputText`, and `defaultOf`, and shows defaults in signatures, follows names in signature help, and gives the new hints.

`tests/unit/parser/test_parser.cpp` checks named arguments, a construction in parentheses, a pipe into a call, and defaults. `tests/unit/sema/test_sema.cpp` checks every way to pass arguments and every way to get them wrong, defaults that see the inputs before them and not after, and the fixes. `tests/unit/lsp/test_lsp.cpp` checks the hints, signature help, and hover on an input, and `tests/run/` gained a program that uses all of it, including a default that calls its own function, and one that traps in the argument written first. 14476 unit assertions and 61 programs, 0 failures.

## Current Limitations

A default runs in a function of its own, so one that would yield out of the function around it can't. Records will want defaults on their fields, which come with records in commit 11.
