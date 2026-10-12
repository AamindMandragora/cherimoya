# Commit 4.2: Yields, Control Flow & Places

## The Problem

After commit 4.1, sema knows what every name means and what type every expression has, but not where control goes. A function declared `-> (r :: int)` can have an empty body, `-| outer` can name nothing, `let x <- 1; x <- 2;` changes a binding that was never `mut`, and `add(1, 2);` throws its result away. `match`, `repeat`, and `chunk` aren't checked at all. Commit 4.2 adds the rules for all of that: the yield-target stack and its labels, the never type, function results, `match` and loops as typed expressions, mutability, and discards. With it, sema covers every construct a program over the core types can use.

## Where Control Can Go

The type checker doesn't need a control-flow graph to know whether a function always yields. It needs one answer per statement: can control continue past it? Checking a statement returns that answer. A yield never continues, and neither does a statement whose expression has the never type: a `let` whose initializer always leaves, or a `match` whose every arm does. Checking a list folds those answers into whether control can fall off the end of the list. The graph itself is a later commit's job, where drops and ownership need it.

A statement after one that never continues can't run. Sema warns about the first statement of each such run: `cherimoya never runs this statement, since every path before it yields`. Warnings are a diagnostic level of their own. They render as `warning at line N:`, and they don't count as errors, so they never stop a build.

## The Never Type

`never` is the type of anything that doesn't come back. A yield has it, and so does a scope that nothing exits normally. It fits anywhere, so a `match` arm that yields out of the function still typechecks next to arms that produce an `int`, and it never disagrees with another type, since a path that doesn't come back has no value. That's what lets a statement like

```
match (n) {
    0 -> { 1 -| f }
    _ -> { 2 -| f }
}
```

count as yielding on every path: both arms are `never`, so the match is `never`, so control can't fall off the end of `f`.

## The Yield-Target Stack

Every function, block, match arm, `match`, loop, and `chunk` pushes a target while its insides are checked, and pops it after. A target records its kind, its label (an explicit name, or the function's name), and, for a function, the type it has to yield. Everything else learns its type from what's yielded to it.

Every yield searches the stack from the innermost target outward:

- **bare `-|`:** the innermost target
- **`-| match` and `-| repeat`:** the innermost `match` or loop
- **`-| name`:** the innermost target with that label

The search stops at the nearest function, so a yield can't reach into an enclosing one, and labels never collide with variables, since they're looked up on the stack instead of in scopes. A yield with no target to reach is an error that names what it looked for: `cherimoya expected a function or scope named 'outer' around this yield, got none instead`.

## How Scopes Get Their Types

A function knows its type up front, so each yield to it is checked against the declared output where it's written, with a note at the declaration. Every other scope's type comes from what reaches it. The first yield sets it, and every later one has to agree, with a note pointing back at the first. When the scope closes, that's combined with whether control can fall off its end:

| Yields to it | Can fall off the end | Type |
|---|---|---|
| none | no | `never` |
| none | yes | `void` |
| some | no | their type |
| some | yes | an error, unless their type is `void` |

The last row is the "every path" rule, `cherimoya requires every path through this block to yield 'int', please end it with a yield`, and the same rule makes a non-void function that can fall off its end an error.

A function's body is the function's own scope, not a block inside it, so a bare `-|` at the top of the body yields the function's result. A bare `-|` in a nested block yields only from that block. There are no implicit exits, so leaving a function early is always written `-| name`.

## Match

Each arm is a target, and its value goes to the match, so the match's type is what its arms agree on. `-| match` reaches the match directly from inside an arm's nested scopes.

Patterns over the core types are literals, `_`, binders, and guards. A literal has to have the subject's type. A binder declares the subject's value under its name, visible only in its own arm. A guard has to be a bool.

Exhaustiveness proper comes with variants and ranges. Until then, the rule is that a match has to cover every value whenever a missing case would matter: a match on an int or a float needs a catch-all (`_` or a binder), a match on a bool is also complete with both `true` and `false`, and any match that yields a value has to be complete. A match that's incomplete is told so: `cherimoya requires a match on 'int' to cover every value, please add a '_ -> { ... }' arm`. An arm after full coverage can never be reached, which is a warning with a note on the arm that covers everything.

Patterns compare with deep equality by default. A match can choose its equality the way a fold chooses its reducer: `match (=, x)` compares patterns shallowly, and `match (==, x)` spells out the default. For ints and bools the two agree. For floats, deep equality uses the total order, so `-0.0` matches `0.0` and all NaNs match each other, while shallow equality is bitwise. Once patterns can hold strings and handles, deep compares contents and shallow compares addresses. The parser keeps the operator on the match, and codegen reads it.

## Loops and Chunks

A loop's condition and invariant have to be bools. A bare `-|` in a loop's body leaves the loop, the same as `-| repeat`. A loop with a condition is `void`. A loop without one only ends by a yield, so it takes that yield's type, or `never` if nothing leaves it. A loop with a header that yields a value would have type `Option[T]`, which arrives with aggregates and generics, so until then that yield is reported as not supported yet. For-each loops need ranges or `Iterable`, so they're not supported yet either.

A `chunk` yields like a block and can be named. Its allocator behavior comes with allocators.

## Places and Discards

Only a binding declared `mut` can be assigned to, and the error says how to declare it: `please declare it with 'let x :: mut <- ...;'`, or `please declare it as 'n :: mut int'` for a parameter. A binding left uninitialized with `<- _` has to be `mut`, since assigning to it later is the only way it can get a value. Fields, indexes, and dereferences arrive with aggregates and handles.

A value can't be dropped on the floor. An expression statement whose type isn't `void` or `never` has to be discarded explicitly, and the fix quotes the code: `cherimoya requires a value to be used or discarded, please write '_ <- add(1, 2);'`. That covers calls, stray arithmetic, and a `match` at the start of a statement whose arms yield values that go nowhere.

## What Was Built

`src/sema/target.hpp` defines `TargetKind` and `Target`. `src/sema/sema.hpp` gained the target stack, `checkYield`, `resolveTarget`, `contribute`, `finish`, `checkMatch`, `checkPattern`, `checkRepeat`, `checkChunk`, and `checkPlace`, and checking a statement or a list now reports whether control continues. `src/diagnostics/diagnostics.hpp` gained warnings. `MatchExpr` gained its equality operator, which the parser reads from `match (=, x)`.

`tests/unit/sema/test_sema.cpp` gained `semaWarnings`, with test cases for mutability, discards, function results, yield targets and labels, scope values, unreachable statements, `match`, yields through `match`, `repeat`, and `chunk`. The parser and printer tests cover the equality slot. 3288 assertions across lexer, parser, printer, and sema, 0 failures.

## Current Limitations

Only infinite loops yield values, and there are no for-each loops, so there's no continue target yet. Match patterns are literals, `_`, binders, and guards, and coverage is the simple rule above instead of real exhaustiveness. Places are names only.

## What's Next

Commit 5 lowers the checked AST to LLVM IR and produces the first executable: functions, yields as branches, loops, `match` on integers and booleans, `cheri build`, linking, and an exit-code test harness.
