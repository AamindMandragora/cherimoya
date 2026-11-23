# Commit 1.4: Types, Statements, Scopes, and Patterns

## The Feature

- **Types.** Named types with dotted paths and generic arguments (`int`, `std.mem.Allocator`, `Map[K, V]`, `Fn[F, [S, T], S]`), handles (`ptr[T]`, `arr[T]`, `arr[16, T]`, `arr[Chunk, T]`), `mut` wherever it sits, the modes `unscoped`, `peek`, and `wit`, tuples `[int, f64]`, records `[x :: f64, y :: f64]`, and refinements `int {p : 0 <= p <= 100}` and `a :: int {a >= 0}`. A bare qualifier or mode leaves the type to inference: `let buf :: unscoped <- ...`, `let x :: mut <- 5`.
- **Types in value position.** Calling a type converts or allocates: `i32(x)`, `arr[mut T](n)`, `ptr[T](v)`. An annotation `(p :: Ord)` gives a value a type.
- **Patterns.** Literals including a negated number, `_`, names, variants `Some[d]`, packs and records `[x <- 0, y <- py]`, ranges `[..., <0]`, set-builders `[x : x % 2 = 0]`, and guards `(cond)` in a subjectless match.
- **Statements.** `let pattern :: type <- init;`, where `<- _` leaves it uninitialized, assignment `place <- value;` and compound assignment `x +<- 1;`, yields, expression statements, `wit {p};`, `wit {T :: Ord};`, and `assert (p);`. The place intrinsics `take`, `swap`, and `replace` are called like functions.
- **Yields.** An optional value and an optional target: `count -|`, `-| i`, `100 -| early`, `-| repeat`.
- **Scopes are expressions.** Blocks `{ }` and named blocks `name { }`, `match name (subject) { pattern -> { body } }`, `repeat` for-each `(binder) <- source`, while `(cond)`, and infinite with nothing, with an optional `wit {invariant}`, and `chunk name { }`. So `let p <- match (n % 2) { ... };` and `let v <- repeat { 1 -| };` work, and `match (n) { ... } -|` yields a scope's value.
- **Closures.** `(params) -> (outputs) { body }`, where types and outputs are optional, and capture closures `[captures] (params) -> { body }`. They need no extra parentheses in packs or as fold reducers.
- **Diagnostics.** A binding with no initializer gets `cherimoya requires a binding to have an initializer, please write '<- _' to leave it uninitialized`. Assigning to a non-place gets `cherimoya requires the left side of an assignment to be a place, please assign to a name, field, index, dereference, or _`. A subjectless match with another kind of arm gets `cherimoya requires a subjectless match's arms to be '(condition)' or '_', please write '(condition) -> { body }'`. A missing `;` gets `cherimoya expected ';' after the statement, got <token> instead`, and something that isn't a pattern gets `cherimoya expected a pattern (a literal, a name, _, Variant[p], a pack, a range, or a set-builder), got <it> instead`. `if (` at the start of a statement gets the same diagnostic pointing at `match` as in an expression.

## Why

After commit 1.3 the parser reads `(+, 0)[x * x : x <- xs]` but nothing with a semicolon or a brace in it: no `let`, no assignment, no `-|`, no `match`, and no closures, since a closure's body is a scope of statements and its parameters have types. This commit builds what function bodies are made of, so only the declarations around them remain.

- **Four hierarchies.** Types, patterns, and statements each get a base class with a kind and a span, a virtual destructor, and derived nodes held through `unique_ptr`, the same shape as expressions. They reference each other, a refinement holding a predicate, a closure a block, a statement expressions and patterns, so each header forward-declares what it points at, and everything that includes the tree includes all of it, so every node is complete by the time anything is destroyed.
- **Where `mut` sits is the point.** `mut arr[T]` rebinds the handle, `arr[mut T]` mutates the elements, and `mut int` is a mutable value. The position also falls out of the nesting, but recording it once saves every later pass from working it out again.
- **No function-type node.** `Fn` is an ordinary protocol, so a function type is a named type.
- **Handles don't know their argument.** `arr[16, T]` and `arr[Chunk, T]` look the same, and whether the argument is a size or an allocator is decided by its kind during type checking.
- **`::` only introduces a type.** A record type is recognized by `name ::` as its first element, and record values are written with `<-`, as in `[x <- 1.0]`.
- **Two ways to bind a refinement.** Type definitions name the binder, `int {p : 0 <= p <= 100}`, and parameters use their own name, `a :: int {a >= 0}`. The brace opening with `name :` tells them apart.
- **Generic arguments.** A lone name is parsed as a type, since only type checking knows whether `N` is a type or a constant. A literal or an expression like `N * 2` is a value.
- **Patterns are expressions, reinterpreted.** Every pattern is written like an expression: `Some[d]` like an index, `[x <- 0, y <- py]` like a named pack, `[..., <0]` like a range, `[x : x % 2 = 0]` like a comprehension. Parsing an expression and converting it means no second grammar, and since the parser doesn't know `[x, y]` is a pattern until it sees the `<-` after it, converting afterward is deterministic, with no backtracking and no diagnostics from a failed first attempt.
- **Names are left to type checking.** `None` and `n` look identical, so whether a bare name is a payload-free variant or a new binding is decided later. User type names are identifiers too, so `NodeId(3)` is a call and `List[peek T]` an index whose argument is a type.
- **Scopes as expressions.** It's what lets `match` and `repeat` give values and gives loops their results. A block is always a plain block, since there's no `else` for it to belong to.
- **Intrinsics as keywords.** `take`, `swap`, and `replace` are keywords called like functions, and since no user name can collide with a keyword, type checking can always tell an intrinsic call apart.
- **A target always follows `-|`.** After `-|`, an identifier, `repeat`, or `match` is the target. A statement after a bare yield in the same scope would be unreachable anyway.
- **Capture closures as a call shape.** Calling a pack is never valid, so a pack followed by `(params) ->` can only be a capture closure.

## Implementation

### Tree

`src/parser/type.hpp`, `src/parser/pattern.hpp`, and `src/parser/stmt.hpp` are new, and `src/parser/expr.hpp` takes over the expression nodes. `src/parser/ast.hpp` includes all four.

- **Types.** `TypeKind` and `Type`, with `NamedType` (a `path` of tokens and `GenericArg`s, each a type or a value), `HandleType` (`ptr` or `arr`, an optional argument, and the element type), `QualifiedType` (`mut`, its `QualPosition` of `Handle`, `Elements`, or `Value`, and an inner type that's null for a bare qualifier), `ModeType`, `TupleType`, `RecordType` of `Param`s, `RefinedType` (base, optional binder, and predicate), and `ErrorType`.
- **Patterns.** `PatternKind` and `Pattern`, with `LiteralPattern`, `WildcardPattern`, `BinderPattern`, `VariantPattern`, `PackPattern` of `PackPatternElem`s, `RangePattern` of `RangePatternElem`s, `SetBuilderPattern`, `GuardPattern`, and `ErrorPattern`.
- **Statements.** `StmtKind` and `Stmt`, with `AssignStmt` (place, optional compound token, value), `ExprStmt`, `YieldStmt` (optional value and target), `LetStmt`, `WitStmt` (predicate and optional protocol bound), `AssertStmt`, and `ErrorStmt`.
- **New expressions.** `AnnExpr`, `TypeExprExpr`, `BlockExpr`, `ClosureExpr` (captures, parameters, optional outputs, body), `MatchExpr` of `MatchArm`s, `RepeatExpr` (name, binder and source, or condition, invariant, body), and `ChunkExpr`. `ComprClause` now holds a pattern.

### Parser

All in `src/parser/parser.hpp`.

- **Types.** `parseType` reads modes and `mut` in front, then a handle, a tuple or record, or a named type, then an optional refinement through `parseRefinement`. A `mut` before a handle is `Handle` and before anything else `Value`, and a handle sets a qualifier directly inside it to `Elements`. `parseNamedType` reads a dotted path and generic arguments, and `parseGenericArg` takes a type when it's a lone name or can only start a type, and a value otherwise. `startsType` says which tokens can start a type. A missing type is reported, and the `<-`, comma, or bracket after it is left for the code around it, so `let x :: <- 5;` is one error.
- **Types in value position.** In the NUD, a built-in type name, `arr`, `ptr`, `peek`, `mut`, or `unscoped` is parsed with `parseType(false)`, which leaves a `{` alone since a refinement can't follow there, and wrapped in a `TypeExprExpr` for the call LED. `parseParen` makes an `AnnExpr` when `::` follows the grouped expression.
- **Patterns.** `toPattern` converts an expression: literals and a negated number literal to `LiteralPattern`, `_` to `WildcardPattern`, a name to `BinderPattern`, a name with brackets to `VariantPattern` with each argument converted, packs to `PackPattern` keeping element names, ranges to `RangePattern` keeping elements and markers, and a comprehension with a lone name as its head and only filter clauses to `SetBuilderPattern`. `parsePattern` parses with `arrowStops` set, so a top-level `<-` ends it, and `parseParen` only reads `( ) ->` as a closure when `arrowStops` is clear, so in a pattern it's a parenthesized pattern followed by its arm.
- **Statements.** `parseStmt` dispatches on `let`, `wit`, `assert`, a bare `-|`, a scope keyword, `{`, and a name followed by `{`, which is a named block only at the start of a statement. Otherwise it reads an expression with `arrowStops` set, so `xs <- [1, 2];` isn't read as a construction, while `parseNested` clears the flag inside brackets and on the right side, so `p <- Point <- [1, 2];` still constructs. Then `<-` or a `CompoundAssign` makes an `AssignStmt`, checked by `isPlace`, `-|` goes to `parseYield`, and anything else is an `ExprStmt`. `parseLet`, `parseWit`, and `parseAssert` read their statements, and `parseYield` reads the optional target.
- **Scopes.** `parseBlock`, `parseMatch`, `parseRepeat`, and `parseChunk` parse each scope, and `parseScopeName` takes a name after the keyword only when what follows fits the scope, so `match (x)` isn't taken for a name. In `parseMatch`, a subjectless arm starting with `(` is a `GuardPattern`. `parseRepeat` parses its header as an expression and converts it with `toPattern` if `<-` follows. At the start of a statement, `scopeStmt` takes a scope with no `;`, and yields it if `-|` follows. It isn't run through the LED loop, so a statement after it starting with `(` can't be read as a call. In an expression, `xs {` is the end of the expression and then a block, which is what `repeat (x) <- xs { }` needs.
- **Closures.** `parseParen` hands `(...) ->` to `parseClosure`, which reads the parameters with `parseParamList`, `->`, optional outputs, and a block. The call LED turns a pack followed by `(params) ->` into the capture list, reporting a named capture.
- **`if (`.** The NUD reports it and skips the condition and the block after it, so one mistake is one error.
- **Recovery.** `expectSemi` reports a missing `;` without consuming the token in its place, so a missing `;` before `}` doesn't swallow the brace, and stays quiet after an expression that already failed. A function definition is reported as not parsed yet and skipped with `skipItem` to the end of its body. `parseBlock` skips a stray closing bracket once it's been reported, so recovery that leaves closers in place can't get stuck on one.

## Tests

`tests/unit/parser/test_parser.cpp` splits the dump into `dumpExpr`, `dumpType`, `dumpPattern`, and `dumpStmt`, with the `parseDump`, `stmtDump`, `typeDump`, `patternDump`, and `stmtErrors` helpers. New test cases cover types, qualifier positions and modes, refinements, every pattern form, bindings, assignment and expression statements, yields, `wit` and `assert`, blocks, `match`, `repeat`, `chunk`, closures, annotation and types in value position, scopes inside expressions, golden bodies of `count_pairs` and of a program's entry point, `if (` at statement level, and statement recovery. 2624 assertions across lexer and parser, 0 failures.

## Current Limitations

- **No declarations.** Function definitions are reported and skipped, and there's no `type`, `export`, `import`, attributes, or `@tier`, and no way to parse a whole file, until declarations exist.
