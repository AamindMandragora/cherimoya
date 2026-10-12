# Commit 4.1: Names, Types & Expressions

## The Problem

After commit 3, the parser accepts any program that's shaped right. `let x <- y + true;` parses fine: nothing knows that `y` was never declared, or that `+` can't add a bool. Sema is the pass that knows what names mean and what values are. Commit 4 builds its core in two parts. This first part resolves every name to its declaration and gives every expression a type, over the core types `int`, `float`, and `bool`. Commit 4.2 adds control flow: yields and their targets, `match`, `repeat`, function results, mutability, and discards.

## Sema Types

The parser's `Type` nodes are syntax: `mut int {a >= 0}` is a tree with spans. A `SemaType` is meaning: this value is an `int`. The hierarchy is small: `PrimitiveSemaType` (`int`, `bool`, `float`, `void`, and `never`), `FunctionSemaType` (parameter types and a return type), and `ErrorSemaType`. Sema types have no spans, since one `int` is shared by every `int` value in the program.

Types are interned. The `TypeContext` creates each distinct type exactly once, owns all of them in a `vector<unique_ptr<SemaType>>`, and hands out raw pointers. Primitives are made up front, and a function type is made the first time it's asked for and cached in a map keyed by its parameter and return types. Since each type exists once, type equality is pointer equality, and no rule ever compares two types structurally.

Two types are special:

- **`error`** is the type of anything whose checking failed. Every rule treats it as fitting anywhere and reports nothing about it, so one mistake produces one diagnostic instead of a cascade. `let x <- y + 1 + 2.5;` with `y` undeclared reports `y` and nothing else.
- **`never`** is the type of a yield, since control doesn't continue past one. It fits anywhere too, so a `match` arm that yields elsewhere still typechecks. 4.2 is where yields produce it.

## Symbols and Lexical Scopes

A `Symbol` is one declared name: its type, whether it's `mut`, its span, its kind (binding, parameter, or function), and its owner, the function body that declared it. Sema owns every symbol in a `std::deque`, which never moves an element once it's added, so a `Symbol*` stays valid for as long as sema does.

A `Scope` is one lexical scope: a map from names to symbols and a pointer to the enclosing scope. Lookup walks outward, so an inner `x` shadows an outer one, and declaring a name twice in one scope is an error. Scopes live on the C++ stack for exactly as long as the code they cover, while the symbols they point at outlive them. That split matters, because the AST keeps pointing at symbols after their scope is gone.

Lexical scopes are sema's, and they're separate from the scopes in the grammar (blocks, `match`, `repeat`, `chunk`). A block, a function, and a refinement's binder each get a lexical scope.

## Annotating the AST

Sema writes its results straight into the tree. Every `Expr` has a `SemaType* type`, and every `IdentifierExpr` has a `Symbol* decl`. After checking, each identifier knows exactly which declaration it means, and each expression knows its type, so later passes read the answers instead of recomputing them. `AnnExpr` and `TypeExprExpr` call their parsed types `annotation` and `typeExpr`, so they don't hide the base class's `type`.

## Diagnostics with Notes

A diagnostic can carry notes: a message and a span each, rendered as their own block with carets under the error. A redeclaration points at the first declaration, a call with the wrong number of arguments points at the function, and a type mismatch between two operands shows each side's type:

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

The ownership passes will lean on the same mechanism for "moved here" and "borrowed here".

## The Passes

A list of statements is checked in three passes, and the top of a file is just the outermost list:

1. Every function's signature is declared, so functions can call each other in any order, including mutual recursion.
2. Every other statement is checked in order, so a `let` is only visible after it.
3. Every function's body is checked, so a body sees every name declared around it, wherever it is. A function can use a constant declared below it.

Functions defined inside a body follow the same passes within their block. They can't use the enclosing function's locals: each function body gets an owner number, and an identifier whose symbol belongs to a different function's body is an error that points at closures, the way to capture. Globals and functions are visible everywhere.

A function's parameters are declared in its own scope, with each refinement checked right after its parameter. Outputs get an inner scope that only the output refinements and the `wit` obligations are checked in. The body is outside it, since outputs are produced by yields, not read. Parameters and outputs need types, outputs give the function's type (`()` is `void`, and one output is that output's type), and a function's type is written `(int, int) : bool`. A signature is resolved once, when its function is declared, and its body's check reuses it, so a mistake in a signature is reported once.

## Bindings

A `let`'s type comes from its annotation, its initializer, or both when they agree. A bare `mut` leaves the type to the initializer. `<- _` needs an annotation, since there's nothing to infer from. A binding can't hold `void`. The initializer is checked before the name is declared, so `let x <- x + 1;` sees an outer `x` or nothing.

Refinements, `wit` obligations, and assertions are type-checked but not proven: their names have to resolve and they have to be bools, so a typo in a contract is caught now. Proving them is Z3's job in a later commit. A refinement on a parameter or a `let` refers to it by its own name, since the name is declared before its refinement is checked, and one with a binder (`{p : p > 0}`) gets a scope with the binder in it.

## Expressions

Each expression's type is worked out from its parts:

- **Literals:** integers are `int`, floats are `float`, and `true` and `false` are `bool`.
- **Operators** read their rule from the operator table's category. Arithmetic takes ints or floats and gives the same type, comparisons take ints or floats and give `bool`, bitwise operators take ints, logical operators take bools, and equality takes any two values and gives `bool`. Both sides must have the same type, since conversions are explicit. A chain checks each neighboring pair.
- **Calls** must name a function. The argument count and each argument's type are checked against the signature, and the call's type is the function's return type. A function's name can only be called for now, since function values arrive with closures.
- **Pipes** are calls: `x |> f` and `f <| x` both check as `f(x)`.
- **Assignments** must fit the place's type. A compound assignment applies its base operator, so `x +<- 2` checks as `x + 2` and has to give back `x`'s type. `_ <- v` takes any value.

## What 4.1 Doesn't Check Yet

Anything sema can't check yet is reported once with `cherimoya doesn't support <x> yet` and given the error type, so the rest of the program still checks without a cascade. That covers types other than the core ones, handles, modes, tuples and records, type definitions, modules, generics, operator overloading, extra operators, conversions, strings and chars, packs, ranges, comprehensions, folds, closures, `match`, `repeat`, and `chunk`. Each one names what it is, so the message says which feature is missing.

## What Was Built

`src/sema/type.hpp` defines `SemaType`, `PrimitiveSemaType`, `FunctionSemaType`, and `ErrorSemaType`. `src/sema/context.hpp` has the `TypeContext` arena. `src/sema/scope.hpp` has `Symbol` and `Scope`. `src/sema/sema.hpp` is the `Sema` class: type resolution, the passes, functions, bindings, refinements, and expression typing. `src/diagnostics/diagnostics.hpp` gained notes, and `src/parser/expr.hpp` gained the `type` and `decl` annotations.

`tests/unit/sema/test_sema.cpp` has `semaErrors` and `symbolType` helpers that run the whole pipeline, with test cases for binding types, name resolution and lexical scopes, the passes, nested functions, refinements and contracts, operators, calls and pipes, assignments, and unsupported forms. 3214 assertions across lexer, parser, printer, and sema, 0 failures.

## Current Limitations

Yields check their value but don't resolve their target, function bodies aren't checked against their outputs, blocks are typed `void`, and conditionals and loops aren't checked at all. Mutability is recorded but not enforced, and a call's unused result doesn't need a discard. Those are commit 4.2.

## What's Next

Commit 4.2 adds the yield-target stack and label resolution, the never type, `match` and `repeat` as typed expressions, function results, place and mutability checks, and discard enforcement.
