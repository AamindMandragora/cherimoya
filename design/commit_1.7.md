# Commit 1.7: Yields, Control Flow and Places

## The Feature

- **Yield targets.** A bare `-|` leaves the innermost scope, `-| match` and `-| repeat` the innermost `match` or loop, and `-| name` the scope or function with that label. A yield with nowhere to go is `cherimoya expected a function or scope named 'outer' around this yield, got none instead`.
- **Function results.** Every yield to a function has to match its output, and a non-void function that can fall off its end is `cherimoya requires every path through 'f' to yield 'int', please end it with a yield`, with a note at the declaration.
- **Scopes as values.** A block, `match`, loop, or `chunk` takes the type of what's yielded to it, and every path through it has to yield that type: `cherimoya requires every path through this block to yield 'int', please end it with a yield`.
- **The never type.** A yield, a scope nothing leaves normally, and an operator or call with a `never` operand are all `never`, so

```
match (n) {
    0 -> { 1 -| f }
    _ -> { 2 -| f }
}
```

  counts as yielding on every path out of `f`.
- **`match`.** Arms with literals, `_`, binders, and guards, which have to cover every value when it matters: `cherimoya requires a match on 'int' to cover every value, please add a '_ -> { ... }' arm`.
- **Pattern equality.** `match (=, x)` compares patterns shallowly, and `match (==, x)` spells out the default, deep.
- **Loops and chunks.** A loop's condition and invariant are bools, a loop with a condition is `void`, and one without takes the type of what leaves it. A `chunk` yields like a block.
- **Mutability.** Only a `mut` binding can be assigned: `cherimoya requires 'x' to be mutable to assign to it, please declare it with 'let x :: mut <- ...;'`, or `please declare it as 'n :: mut int'` for a parameter.
- **Discards.** A value can't be dropped on the floor: `cherimoya requires a value to be used or discarded, please write '_ <- add(1, 2);'`.
- **Warnings.** A diagnostic level that renders as `warning at line N:` and never stops a build, used for code that can't run: `cherimoya never runs this statement, since every path before it yields`, and `cherimoya never reaches this arm, since the arms before it match every value`.

## Why

After commit 1.6, sema knows what every name means and what type every expression has, but not where control goes. A function declared `-> (r :: int)` can have an empty body, `-| outer` can name nothing, `let x <- 1; x <- 2;` changes a binding that was never `mut`, and `add(1, 2);` throws its result away. This commit adds those rules, and with it sema covers every construct a program over the core types can use.

- **No control-flow graph yet.** Whether a function always yields needs one answer per statement, whether control can continue past it, and folding those answers over a list says whether control can fall off its end. The graph itself waits for the ownership passes, which need it for drops.
- **`never` fits anywhere and never disagrees,** since a path that doesn't come back has no value. `{ 1 -| f } + 1` leaves `f` before anything is added, so the operator is `never` too.
- **Labels live on a stack, not in scopes,** so they never collide with variables, and the search stops at the nearest function, so a yield can't reach into an enclosing one.
- **A function's body is the function's own scope,** not a block inside it, so a bare `-|` at its top yields the function's result. A bare `-|` in a nested block yields only from that block, and there are no implicit exits, so leaving a function early is always written `-| name`.
- **The first yield sets a scope's type** and later ones have to agree, with a note pointing back at the first, while a function knows its type up front and checks each yield where it's written.
- **Coverage is a simple rule** until variants and ranges bring real exhaustiveness: a match on an int or a float needs a catch-all, a match on a bool is also complete with both `true` and `false`, and any match that yields a value has to be complete.
- **Deep equality by default.** For ints and bools the two agree. For floats, deep uses the total order, so `-0.0` matches `0.0` and all NaNs match each other, while shallow is bitwise. Once patterns can hold strings and handles, deep compares contents and shallow compares addresses.
- **An uninitialized binding has to be `mut`,** since assigning to it later is the only way it can get a value.
- **Warnings for code that can't run,** since it's a mistake but harms nothing.

## Implementation

### Sema

- **Targets.** `src/sema/target.hpp` defines `TargetKind` (`Function`, `Block`, `Arm`, `Match`, `Loop`, `Chunk`) and `Target`: its kind, its `label` (an explicit name, or the function's name), `expected` (the type a function has to yield, or null), `yielded` and `first` (the type of the first yield to reach it and where), `isInfinite` for a loop with no header, and its `span` for notes. `Sema` keeps them in `targets`, innermost last, and `labelOf` reads a scope's label from its name.
- **Where control goes.** `checkStmt` returns whether control never continues past a statement: a yield never does, and neither does a `let`, assignment, or expression statement whose value is `never`. `checkStmts` folds that into whether control reaches the end of the list, and warns once at the first statement after one that never continues.
- **Resolving yields.** `resolveTarget` searches `targets` from the innermost outward: a bare yield takes the innermost, `-| match` and `-| repeat` the innermost of their kind, and `-| name` the innermost with that label, never past a `Function`. `checkYield` reports a yield with no target, as above or as `cherimoya requires a yield to be inside a function or scope, please move it into one`, checks a yield to a function against `expected` (`cherimoya expected a yield of type 'int' to 'f', got 'bool' instead`, with a note at the declaration), and hands anything else to `contribute`.
- **Scope types.** `contribute` records the first yield's type and reports one that disagrees: `cherimoya requires every yield to one scope to have the same type, please make this one 'int'`, with the note `this yield gives it 'int'`. `finish` combines that with whether control can fall off the end:

| Yields to it | Can fall off the end | Type |
|---|---|---|
| none | no | `never` |
| none | yes | `void` |
| some | no | their type |
| some | yes | an error, unless their type is `void` |

- **Scopes.** `checkScope` pushes a target, checks the statements in a new lexical scope, pops it, and calls `finish`. `checkBlock` uses it for blocks and arm bodies, and `checkChunk` for chunks. `checkFn` pushes a `Function` target and checks the body's statements directly, so the body is the function's scope, then reports a non-void function that can complete.
- **`match`.** `checkMatch` pushes a `Match` target, checks each arm's pattern in its own scope with `checkPattern`, and contributes the arm body's type to the match. `checkPattern` gives back whether the pattern matches every value: `_` and binders do, a binder declares the subject under its name for its arm only, a literal has to have the subject's type (`cherimoya expected a pattern of type 'int', got 'bool' instead`), and a guard has to be a bool. Variant, pack, range, and set-builder patterns are unsupported and cover nothing, so the arms after them aren't unreachable. An arm after full coverage gets the unreachable warning with the note `this arm matches every value`. An incomplete match on a number, or one that yields a value, reports the coverage error, or `cherimoya requires a match that yields a value to cover every value, please add a '_ -> { ... }' arm` without a subject.
- **Loops.** `checkRepeat` checks the condition and invariant as bools and the body under a `Loop` target. A loop with a condition is `void`. One without takes what was yielded to it, or `never` if nothing leaves it. A loop with a header that yields a value would have type `Option[T]`, so until sums and generics exist, that yield is reported as unsupported and contributes the error type, so binding the loop doesn't report again. For-each loops are unsupported until ranges exist.
- **`never` through expressions.** `unaryType` passes a poisoned operand through, `binaryType` gives `never` when either side is `never`, a chain gives `never` from any pair that does, and `checkCall` gives `never` when any argument is.
- **Places.** `checkPlace` checks the left of an assignment: a binding that isn't `mut` reports the mutability error with a note at its declaration, the fix depending on whether it's a parameter, and a function is `cherimoya requires the left side of an assignment to be a binding, please assign to a 'let' instead of the function 'f'`. Compound assignments with an extra operator are unsupported.
- **Bindings.** `checkLet` requires `<- _` to be `mut`: `cherimoya requires an uninitialized binding to be mutable, please write 'let x :: mut int <- _;'`, and without a type, `cherimoya requires an uninitialized binding to have a type, please write 'let x :: mut int <- _;'`.
- **Discards.** An expression statement whose type isn't `void` or `never` reports the discard error, quoting the expression's source text in the fix. That covers calls, stray arithmetic, and a `match` at the start of a statement whose arms yield values that go nowhere.

### Parser

`MatchExpr` in `src/parser/expr.hpp` gains `equality`, the operator token, and `parseMatch` reads it from `match (=, x)` when an operator and a comma open the parentheses. Anything but `=` or `==` is `cherimoya requires a match's equality to be '=' or '==', please write 'match (=, subject)' or 'match (==, subject)'`. Codegen reads it once programs are lowered.

### Diagnostics

`Diagnostic` gains `isWarning`, and `printError` and the new `printWarning` share `report`. `getErrorN` labels a warning `warning`, `hadErrors` and `errorCount` count only errors, and `warningCount` counts the rest.

### Build

`CMakeLists.txt` exports `compile_commands.json`, and `.vscode/c_cpp_properties.json` points the editor at it.

## Tests

`tests/unit/sema/test_sema.cpp` adds `semaWarnings`, with test cases for mutability, discards, function results, yield targets and labels, scope values, unreachable statements, `match`, yields through `match`, `repeat`, and `chunk`. The parser and printer tests cover the equality slot. 3311 assertions across lexer, parser, printer, and sema, 0 failures.

## Current Limitations

- **Loop values.** Only infinite loops yield values, until sums can hold a loop's `Option[T]`.
- **No for-each loops,** and so no continue target, until ranges exist.
- **Patterns.** Only literals, `_`, binders, and guards, and coverage is the simple rule above, until variants and ranges bring real exhaustiveness.
- **Places are names only,** until arrays, records, and handles give fields, indexes, and dereferences.
