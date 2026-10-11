# Commit 3.1: Types, Statements, Scopes & Patterns

## The Problem

After commit 2, the parser can turn `(+, 0)[x * x : x <- xs]` into a tree, but it can't parse anything with a semicolon or a brace in it. There's no `let`, no assignment, no `-|`, no `match`, and no closures, because a closure's body is a scope full of statements and its parameters have types. Commit 3 fills in everything around the expressions, in two parts. This first part builds the pieces that function bodies are made of: types, patterns, statements, and scopes. Commit 3.2 adds declarations.

## Four Hierarchies

Expressions already have their hierarchy from commit 2. Commit 3.1 adds three more: types, patterns, and statements. Each follows the same shape as `Expr`: a base class with a kind discriminator and a span, a virtual destructor, and derived classes held through `unique_ptr`. They live in `type.hpp`, `pattern.hpp`, `expr.hpp` (renamed from commit 2's `ast.hpp`), and `stmt.hpp`, and the new `ast.hpp` includes all four.

The hierarchies reference each other: a refinement holds a predicate expression, a closure holds a block of statements, a statement holds expressions and patterns. Each header forward-declares what it points at, and everything that includes the AST includes all of it, so every node type is complete by the time anything is destroyed.

## Types

The type grammar is built from small pieces that nest:

- `NamedType`: a dotted path and its generic arguments. `int`, `std.mem.Allocator`, `Map[K, V]`, and `Fn[F, [S, T], S]` are all named types. Function types don't need their own node, since `Fn` is an ordinary interface.
- `HandleType`: `ptr` or `arr`, an optional bracketed argument, and the element type. `arr[16] T` and `arr[Chunk] T` look the same to the parser, and sema decides by kind whether the argument is a size or an allocator.
- `QualifiedType`: `mut`, where it sits, and what it applies to. Where `mut` sits is the whole point. `mut arr T` rebinds the handle, `arr mut T` mutates the elements, and `mut int` is a mutable value. The parser records the position when it builds the node: a `mut` in front of a handle is `Handle`, a `mut` directly inside a handle is `Elements`, and anything else is `Value`. The position also falls out of the nesting, but storing it saves every later pass from re-deriving it.
- `ModeType`: `unscoped`, `peek`, or `wit`, and the type it applies to.
- `TupleType` and `RecordType`: `[int, f64]` and `[x :: f64, y :: f64]`. A record is recognized by `name ::` as its first element. `::` only ever introduces a type, so record values are written with `<-`, as in `[x <- 1.0]`.
- `RefinedType`: a base type, an optional binder, and a predicate. Type definitions name the binder (`int {p : 0 <= p <= 100}`), and parameters use their own name instead (`a :: int {a >= 0}`). The parser tells them apart by whether the brace opens with `name :`.

A bare qualifier or mode leaves the type to inference, so `let buf :: unscoped <- ...` and `let x :: mut <- 5` have a null inner type.

A generic argument is either a type or a comptime value. A lone name is parsed as a type, since sema is the one that knows whether `N` is a type or a constant. A literal or an expression like `N * 2` is parsed as a value.

## Types in Value Position

Calling a type converts or allocates: `i32(x)`, `arr mut T(n)`, `ptr T(v)`. When a NUD sees something that can only start a type (a built-in type name, `arr`, `ptr`, `peek`, `mut`, or `unscoped`), it parses a type and wraps it in a `TypeExprExpr`, and the ordinary call LED takes it from there. Types in value position never take a refinement, so the type parser has a flag that leaves a `{` alone. User type names are identifiers, so `NodeId(3)` is just a call, and `List[peek T]` is an index whose argument is a type. Sema sorts them out.

Annotation reuses the prefix parenthesis scan: if `::` follows the grouped expression, `(p :: Ord)` is an `AnnExpr`.

## Patterns

Patterns appear in four places: match arms, `let` destructuring, loop binders, and membership clauses. Every pattern form is also written like an expression: `Some[d]` looks like an index, `[x <- 0, y <- py]` like a named pack, `[..., <0]` like a range, `[x : x % 2 = 0]` like a comprehension. So the parser doesn't have a second grammar for them. It parses an expression and then reinterprets it with `toPattern`:

- literals, including a negated number, become `LiteralPattern`
- `_` becomes `WildcardPattern`
- a bare name becomes `BinderPattern`. `None` and `n` look identical, so sema decides whether a name is a payload-free variant or a new binding.
- a name with brackets becomes `VariantPattern`, with each argument converted in turn
- packs become `PackPattern`, keeping element names for records
- ranges become `RangePattern`, keeping the raw elements and exclusivity markers
- a comprehension with a lone binder head and only filter clauses becomes `SetBuilderPattern`
- anything else is an error

This also solves the membership clause problem from commit 2. The parser doesn't know `[x, y]` is a pattern until it sees the `<-` after it, and by then it has already parsed an expression. Converting it afterward is deterministic, so there's no backtracking and no diagnostics from a failed first attempt.

The one pattern that isn't an expression is the guard: in a subjectless match, `(cond) -> { }` is an arm whose pattern is a condition. The match parser handles that one directly.

## Statements

At statement level, the parser reads an expression first and then looks at what follows:

- `<-` or a compound token: an assignment. The left side has to be a place: a name, a field, an index, a dereference, or `_`.
- `-|`: a yield of that value.
- otherwise: an expression statement, ending with `;`.

The expression has to stop before an assignment's `<-`, or `xs <- [1, 2];` would be read as constructing `xs` from a pack. The flag that turns `<-` into a stop for comprehension clauses in commit 2 does the same job here: it's set at the top of a statement, cleared inside every nested bracket, so `p <- Point <- [1, 2];` still constructs on the right side.

A few statements start with a keyword. `let pattern :: type <- init;` needs an initializer, and `_` means uninitialized. `wit {p};` and `wit {T :: I};` share a node, with the interface as an optional bound. `assert (p);` holds its condition. `swap`, `take`, and `replace` are keywords but are called like functions, so their NUD produces a plain name for the call to wrap. Since no user name can collide with a keyword, sema can still tell an intrinsic call apart.

Yields take an optional value and an optional target: `count -|`, `-| i`, `100 -| early`, `-| repeat`. After `-|`, an identifier, `repeat`, or `match` is always the target. A statement after a bare yield in the same scope would be unreachable anyway.

`if (` gets the same diagnostic at the start of a statement as it does in an expression, pointing at `match`, and the parser skips the condition and the block after it, so one mistake is one error.

## Scopes

Every scope is an expression: blocks, `match`, `repeat`, and `chunk`. That's what lets `let p <- match (n % 2) { ... };` and `let v <- repeat { 1 -| };` work, and it's what gives loops their values. At the start of a statement, a scope is parsed directly and takes no `;`. If `-|` follows it, the statement yields the scope's value: `match (n) { ... } -|`. It isn't run through the LED loop, so a statement after it that starts with `(` can't be mistaken for a call.

- **Blocks**: `{ ... }` is always a plain block, since there's no `else` for it to belong to. `name { ... }` is a named block, but only at the start of a statement. In an expression, `xs {` is the end of the expression followed by a block, which is exactly what `repeat (x) <- xs { }` needs.
- **match**: `match name (subject) { arms }`, where the name and subject are optional. Arms are `pattern -> { body }` with nothing between them. Without a subject, an arm's pattern is a guard or `_`.
- **repeat**: `(binder) <- source` is for-each, a parenthesized expression without `<-` is while, and nothing is infinite. The header is parsed as an expression first, and if `<-` follows, it's converted to a binder pattern. An optional `wit {invariant}` comes before the body.
- **chunk**: `chunk name { }`, with an optional name.

Names go right after the keyword. A name is only taken when what follows it fits the scope, so `match (x)` isn't mistaken for a name.

## Closures

The prefix parenthesis scan from commit 2 already knew that `(...) ->` is a closure. Now it parses one: a parameter list where types are optional, `->`, an optional output list, and a block body. A capture closure is a pack followed by a parameter list and `->`. Since calling a pack is never valid, the call LED checks for exactly that shape and turns the pack's elements into the capture list. Closures in packs and as fold reducers need no extra parentheses.

## Error Recovery

Statements recover the same way expressions do. A missing `;` is reported, in the `cherimoya expected ';' after the statement, got <token> instead` form every expected-token diagnostic uses, without consuming the token that's there instead, so a missing `;` before `}` doesn't also swallow the brace. A missing `;` after an expression that already failed isn't reported at all, so errors don't pile up. A function definition is reported as not supported yet and skipped to the end of its body. A missing type leaves the `<-`, comma, or bracket after it for the code around it, so `let x :: <- 5;` is one error. A block skips a stray closing bracket once it's been reported, so recovery that leaves closers in place can't get stuck on one.

A pattern is parsed as an expression with `<-` and `->` ending it, so `( ... ) ->` in a pattern is a parenthesized pattern followed by its arm, never a closure.

## What Was Built

`src/parser/type.hpp`, `src/parser/pattern.hpp`, and `src/parser/stmt.hpp` are new. `src/parser/expr.hpp` takes over the expression hierarchy from commit 2's `ast.hpp` and adds `AnnExpr`, `TypeExprExpr`, `ClosureExpr`, `BlockExpr`, `MatchExpr`, `RepeatExpr`, and `ChunkExpr`, and membership clauses that hold patterns. `src/parser/ast.hpp` includes all four. The parser gained `parseType`, `parseNamedType`, `parseGenericArg`, `parseRefinement`, `parseParamList`, `parseClosure`, `toPattern`, `parsePattern`, `parseBlock`, `parseStmt`, `parseLet`, `parseWit`, `parseAssert`, `parseYield`, `parseMatch`, `parseRepeat`, and `parseChunk`.

The parser tests split the dump into `dumpExpr`, `dumpType`, `dumpPattern`, and `dumpStmt`, with `parseDump`, `stmtDump`, `typeDump`, and `patternDump` helpers. New test cases cover types, qualifier positions and modes, refinements, every pattern form, bindings, assignment and expression statements, yields, `wit` and `assert`, blocks, `match`, `repeat`, `chunk`, closures, annotation and types in value position, scopes inside expressions, golden dumps of a `count_pairs` body and a `main` body, `if (` at statement level, and statement recovery. 2624 assertions across lexer and parser, 0 failures.

## Current Limitations

There are no declarations yet. Function definitions are reported and skipped, and there's no `type`, `export`, `import`, attributes, or `@tier`, and no way to parse a whole file. That's commit 3.2.

## What's Next

Commit 3.2 adds declarations: functions with refinements, `wit` obligations, and operator names, every form of `type` definition, `export` and `import`, attributes, `@tier` regions, and `parseProgram`, along with a round-trip printer that checks the parser against itself.
