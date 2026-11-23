# Commit 1.6: Names, Types and Expressions

## The Feature

- **Name resolution.** Every identifier resolves to the declaration it means. An undeclared name is `cherimoya requires names to be declared before they're used, please declare 'y' with 'let y <- ...;'`, and declaring a name twice in one scope is `cherimoya requires names in one scope to be distinct, please rename this 'x' or remove the first one`, with a note at the first declaration.
- **Lexical scopes.** A block, a function, and a refinement's binder each get a scope, and an inner `x` shadows an outer one.
- **Functions in any order.** Functions can call each other before they're defined, mutual recursion included, and a body can use a binding declared below it.
- **No reaching into the enclosing function.** A nested function can't use its parent's locals: `cherimoya uses closures to capture 'x' instead of nested functions as follows: let f <- [x] (...) -> { ... };`, with a note at the local. Globals and functions are visible everywhere.
- **Types.** Every expression gets one of the core types, `int`, `float`, or `bool`, or a function type, written `(int, int) : bool`.
- **Bindings.** A `let` takes its type from its annotation, its initializer, or both when they agree. `<- _` needs an annotation, and a binding can't hold `void`.
- **Operators, calls, pipes, assignments.** Each is checked against its operands' types, and both sides of an operator must have the same type, since conversions are explicit.
- **Contracts are typed, not proven.** Refinements, `wit` obligations, and assertions have to resolve and be bools, so a typo in a contract is caught now.
- **Notes on diagnostics.** An error can point at a second place that explains it:

```
error at line 1:
let x <- 1 + 2.5;
         ^^^^^^^
cherimoya requires both sides of '+' to have the same type, please convert one of them
note at line 1:
let x <- 1 + 2.5;
         ^
this side is 'int'
note at line 1:
let x <- 1 + 2.5;
             ^^^
this side is 'float'
```

## Why

After the parser of commits 1.3 through 1.5, any program that's shaped right is accepted. `let x <- y + true;` parses fine: nothing knows that `y` was never declared, or that `+` can't add a bool. Sema is the pass that knows what names mean and what values are. This commit builds its core: every name resolved, every expression typed, over the core types. Control flow, yields, `match`, loops, mutability, and discards come next.

- **Sema types are separate from parsed types.** A parsed `Type` is syntax, a tree with spans like `mut int {a >= 0}`. A sema type is meaning, this value is an `int`, with no span, since one `int` is shared by every `int` value in the program.
- **Interned types** make type equality a pointer comparison, so no rule ever compares two types structurally.
- **The error type fits anywhere and reports nothing,** so one mistake gives one diagnostic instead of a cascade. `let x <- y + 1 + 2.5;` with `y` undeclared reports `y` and nothing else.
- **`never` fits anywhere too.** It's the type of a yield, since control doesn't continue past one, so a `match` arm that yields elsewhere still typechecks. Yields produce it once their targets are resolved.
- **Symbols outlive scopes.** Scopes live on the C++ stack only as long as the code they cover, but the AST keeps pointing at symbols after their scope is gone, so sema owns every symbol for its whole life.
- **Results go into the AST.** Every identifier knows its declaration and every expression its type, so later passes read the answers instead of recomputing them.
- **Three passes over a statement list** let functions be declared in any order while a `let` stays visible only after itself.
- **A signature is resolved once,** when its function is declared, and the body's check reuses it, so a mistake in a signature is reported once.
- **Lexical scopes are sema's own,** separate from the grammar's scopes (blocks, `match`, `repeat`, `chunk`), which are what yields target.
- **Notes** are the same mechanism the ownership passes will use for "moved here" and "borrowed here".
- **What isn't checked yet is reported once,** as `cherimoya doesn't support <x> yet`, and given the error type, so the rest of the program still checks. Each message names the feature that's missing.

## Implementation

### Sema

- **Types.** `src/sema/type.hpp` defines `SemaType`, tagged by `SemaTypeKind`, with `PrimitiveSemaType` (`PrimitiveKind` is `Int`, `Bool`, `Float`, `Void`, or `Never`), `FunctionSemaType` (`params` and `returns`), and `ErrorSemaType`. `toString` prints a function type as `(int, int) : bool`.
- **The arena.** `TypeContext` in `src/sema/context.hpp` owns every type in a `vector<unique_ptr<SemaType>>` and hands out raw pointers. The primitives and `semaError` are made up front, and `functionType` interns a function type in `functionTypes`, keyed by its parameter types followed by its return type.
- **Symbols and scopes.** `src/sema/scope.hpp` defines `Symbol`: its `name`, `SymbolKind` (`Binding`, `Param`, or `Function`), `type`, `isMutable`, `span`, and `owner`, which numbers the function body that declared it, 0 at the top of the file. `Sema` keeps every symbol in a `std::deque`, which never moves an element once it's added, so a `Symbol*` stays valid. `Scope` maps names to symbols with a pointer to its parent: `lookup` walks outward, and `insert` reports a name declared twice in one scope.
- **The passes.** `checkStmts` runs `hoist`, which declares every function's signature in the list, then `checkStmt` on everything else in order, then `checkFn` on every function. `fnOf` finds the function a statement declares through any attributes. The top of a file is the outermost list, checked by `check`.
- **Signatures.** `hoist` resolves each input's type and `outputType`, where `()` is `void` and one output is that output's type, and stores the interned type in `signatures`. Parameters and outputs need types: `cherimoya requires every parameter to have a type, please write 'n :: int'`, and the same for outputs. More than one output, generic functions, and operator overloading are unsupported for now.
- **Functions.** `checkFn` numbers the body with a fresh `owner`, declares the parameters in their own scope with each refinement checked right after its parameter, then checks the outputs' refinements and the `wit` obligations in an inner scope the body never sees, since outputs are produced by yields, not read. `checkIdentifier` rejects a local whose `owner` belongs to another function with the closure message above.
- **Types from syntax.** `resolveType` turns `int`, `i64`, `float`, `f64`, and `bool` into sema types, and gives back null for a bare `mut` with the type left to inference. `isMut` reads whether a type is `mut` as a whole value. Handles, modes, tuples, and records are unsupported.
- **Bindings.** `checkLet` checks the initializer before declaring the name, so `let x <- x + 1;` sees an outer `x` or nothing. `<- _` without a type is `cherimoya requires an uninitialized binding to have a type, please write 'let x :: int <- _;'`, and a `void` initializer is `cherimoya requires a binding to hold a value, please write it as its own statement instead`. Destructuring is unsupported. A mismatch is `cherimoya expected a value of type 'int', got 'bool' instead`, from `mismatch`, and `isCompatible` lets the error type and `never` fit anywhere.
- **Contracts.** `checkRefinements` checks each predicate with its subject in scope, by its own name, or by the binder in `{p : p > 0}` in a scope of its own. `expectBool` requires a bool, as `cherimoya requires a refinement to be a bool, please compare it to something`, and the same for obligations and assertions.
- **Operators.** `binaryType` reads its rule from the operator table's `OpCategory`: arithmetic takes ints or floats and gives the same type, comparisons take them and give `bool`, bitwise takes ints, logical takes bools, and equality takes any two values and gives `bool`. A wrong operand is `cherimoya expected an int or a float on the left of '+', got 'bool' instead`, from `badOperand`, and mismatched sides give the two-note error above. `unaryType` takes `-` on numbers, `!` on bools, and `~` on ints. A chain checks each neighboring pair. `isPoisoned` stops anything built from an error or `never` from reporting again.
- **Calls and pipes.** `checkCall` requires the callee to name a function, checks each argument against its parameter, and gives the return type. A wrong count is `cherimoya expected 2 arguments in the call to 'add', got 1 instead`, with a note at the function. A function's name can only be called for now, so naming it anywhere else is unsupported as functions as values. `x |> f` and `f <| x` both check as `f(x)`.
- **Assignments.** The value has to fit the place's type. A compound assignment checks its base operator, so `x +<- 2` checks as `x + 2` and has to give back `x`'s type. `_ <- v` takes any value, while `_` anywhere else is `cherimoya requires '_' to be a place or an initializer, please use a value here`.
- **Unsupported forms.** `unsupported` reports and returns the error type, and `unsupportedName` names each expression sema doesn't check yet: strings, chars, packs, ranges, comprehensions, folds, closures, indexing, fields, construction, annotations, types as values, `old`, `match`, `repeat`, and `chunk`. Type definitions, modules, interfaces, conversions, and extra operators are reported the same way. Blocks are typed `void` for now.

### Parser

`src/parser/expr.hpp` gives every `Expr` a `SemaType* type` and every `IdentifierExpr` a `Symbol* decl`, which sema fills in. `AnnExpr` and `TypeExprExpr` call their parsed types `annotation` and `typeExpr`, so they don't hide the base class's `type`.

### Diagnostics

`src/diagnostics/diagnostics.hpp` adds `Note`, a message and a span, and `Diagnostic` holds a list of them. `render` formats a span under a label, so `getErrorN` renders the error and then each note under `note`. `printError` takes the notes as an optional argument.

### Build

`CMakeLists.txt` adds `src/sema` to the include path and `tests/unit/sema/test_sema.cpp` to the unit tests.

## Tests

`tests/unit/sema/test_sema.cpp` has `semaErrors` and `symbolType` helpers that run the whole pipeline, with test cases for binding types, name resolution and lexical scopes, the three passes, nested functions, refinements and contracts, operators, calls and pipes, assignments, and unsupported forms. The parser and printer tests follow the renamed fields. 3214 assertions across lexer, parser, printer, and sema, 0 failures.

## Current Limitations

- **No control flow.** Yields check their value but don't resolve their target, function bodies aren't checked against their outputs, blocks are typed `void`, and `match`, loops, and `chunk` aren't checked, until yields resolve their targets.
- **Mutability and discards.** Mutability is recorded but not enforced, and a call's unused result doesn't need a discard, until places and discards are checked.
- **Contracts aren't proven.** Refinements and obligations are only typed until contracts can prove them.
- **Core types only.** Every other type, and every form that needs one, is reported as unsupported.
