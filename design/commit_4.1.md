# Commit 4.1: Fixed-Width Arrays

## The Feature

- **Fixed arrays.** `arr[N, T]` holds `N` values of type `T` inline, copied when bound, passed, or returned, like an `int`. Arrays nest, `arr[2, arr[3, int]]`.
- **Sizes known when compiling.** `N` is an integer literal or an immutable binding whose initializer folds to one through `+ - * / %`, even one declared after the signature that uses it.
- **Literals.** `[1, 2, 3]`, whose elements share one type, picked the way an operator picks its operands'.
- **Indexing.** `xs[i]` by any unsigned integer, checked when compiling if the index folds, and at run time otherwise.
- **Mutability per level.** `arr[3, mut int]` writes elements, `mut arr[3, int]` replaces the whole, and a bare `mut` on an inferred type allows both at every level.
- **`len`.** `xs.len()`, `xs |> .len`, or `len(xs)`, a method from the built-in protocol `Sized`.
- **Equality.** `=` and `==` compare element by element, each with the same operator.

## Why

Every value so far is one scalar, so a program working on a list has to keep each element in a binding of its own. A fixed array is the smallest step past that: its elements sit inline, so it needs no memory management and reuses the existing value machinery, slots, copies, and stores. Heap arrays, pointers, and strings all build on it.

- **Copying** means nothing aliases and nothing is freed, which keeps ownership out of this commit.
- **Folded sizes** make the type fully known when compiling. Signatures are resolved before any `let` is checked, so constants are hoisted per scope ahead of functions.
- **Mutability on the binding, not the type,** keeps `arr[3, mut int]` and `mut arr[3, int]` the same type, so type comparison stays a pointer comparison instead of stripping qualifiers everywhere.
- **Unsigned indices** make a negative index impossible to write, so only the upper bound needs checking.
- **`len` as a method** settles the dispatch rules now, a method winning over a function of the same name, before protocols can be written.

## Implementation

### Parser

`parseExpr` reads a prefix `.name` with no object, giving a `MemberExpr` whose `object` is null, which is what a pipe's right side is in `xs |> .len`. Array literals and indexing already parsed as `PackExpr` and `IndexExpr`.

### Sema

- **Types.** `src/sema/type.hpp` adds `ArraySemaType`, its element and size, and `asArray`. `src/sema/context.hpp` interns array types in `arrayTypes`, keyed by element and size, so two `arr[3, int]`s are the same pointer and type equality stays a pointer comparison.
- **Constants.** `Scope` gains `constants`, filled by `insertConstant` in the first pass over a block, before functions are declared. `lookupConstant` stops at a scope that declares the name as an ordinary symbol, so shadowing works. `fold` evaluates literals, constant names, and `+ - * / %`, finding names through their symbol once checked and by name while hoisting, and refuses anything below zero. `arraySize` turns the `N` of `arr[N, T]` into a number, or reports `cherimoya requires an array's size to be known when the program compiles, please use an integer literal or an immutable binding of one`.
- **Mutability.** `Symbol::isMutable` becomes `Symbol::mutability`, a vector with the binding first and then each level of elements, and `isMutable()` reads `mutability[0]`. `mutabilityOf` builds it from where `mut` sits in the written type, and a bare `mut` on an inferred type sets every level. `checkElementPlace` checks `xs[i]` and `m[i][j]` as assignment targets against the flag at that depth, and the error names the type to declare, `arr[3, mut int]`, with a fix adding `:: mut` when the binding has no written type.
- **Literals.** `checkPack` picks the element type the way binary operators pick theirs: the expected element type, else the first non-literal element's, else a float literal's. A mismatched element reports that packs of different types, tuples, aren't supported yet, and an empty literal needs an expected type.
- **Indexing.** `checkIndex` requires an unsigned index, with a fix wrapping a signed one in `i64u(...)`, and checks a folded index against the size: `cherimoya expected an index below 3 into 'xs', got 5 instead`.
- **Methods.** `hasMethod` answers from `Sized` alone. `checkMethod` checks a method call on its object, and `checkCall` tries a call by a method's name as a method on its first argument before looking for a function, so `len(xs)` finds the method beside a function named `len`. `checkMember` rejects `xs.len` without parentheses, since a bare member will be a field once records exist, and a pipe hands its value to an object-less `.len`.

### Codegen

- **Types.** An array lowers to LLVM's `[N x T]`, a first-class aggregate that slots, arguments, returns, and globals hold directly.
- **Literals.** `emitPack` makes a `ConstantArray` when every element is constant, which lets a global start out holding it, and otherwise builds the value with `insertvalue`.
- **Places.** `placeAddress` gives the address of a binding, an element, or, for a value that isn't a place such as a call's result, a fresh slot it's stored into first, so anything can be indexed. `elementAddress` checks the index with `icmp uge` against the size and does a `getelementptr`, and LLVM folds the check away for a constant index. Assignment now goes through `placeAddress`, so elements and bindings share one path, and the place is evaluated before the value, the order they're written.
- **Traps.** `trapIf` takes a callback that emits the report, so a check can call a trap with run-time arguments. The bounds check calls `cheri_trap_index` with the index, the length, the array's source text, and the line.
- **Equality.** `compareArrays` stores both sides in slots and loops over the elements with a phi, calling `compare` with `=` or `==` on each pair and leaving at the first that differs. The pair's result is taken from whatever block comparing it ended in, since a nested array's comparison adds blocks of its own.
- **`len`.** `isLen` recognizes the method in all three call forms, where a callee with no symbol named `len` is the method, and `emitLen` evaluates the array for its effects and gives its size as a constant `i64`.

### Runtime

`runtime/runtime.c` adds `cheri_trap_index`, which prints `trap at line 6: index 7 is past the end of 'xs', which holds 3` and aborts.

### Debug Info

`typeOf` in `src/codegen/debug.hpp` describes an array as a DWARF array type with one subrange from 0, so gdb prints `{{1, 2, 3}, {4, 5, 6}}`. `src/codegen/printer.hpp` becomes `src/codegen/printers.py`, embedded through `printers.hpp.in`, and prints an array of 1-byte integers as numbers, `{-1, 0, 1}`, where gdb would print a C string.

### Language Server

`src/lsp/features.hpp` gives `arr` its hover text.

## Tests

`tests/unit/sema/test_sema.cpp` covers literals and their element types, sizes, including a constant declared after the signature using it, indexing, equality, every position of `mut`, every call form of a method and every way to misuse one, and the fixes. `tests/unit/parser/` covers `.len` with no object. `tests/run/` adds `arrays.moya`, `array_returns.moya`, which passes arrays in and out of functions as copies, and `array_trap.moya`, an index past the end. `tests/debug/arrays.moya` prints arrays in gdb. 14560 unit assertions, 64 programs, 6 debug programs, and the run of `tests/test/`, 0 failures.

## Current Limitations

- **Copies.** Every copy of an array copies all of it, until views can lend one without copying.
- **Bounds checks.** An index that can't be folded is always checked at run time, until contracts can prove it in range.
- **Methods.** `len` is the only method until protocols can be written, and the language server doesn't complete methods after a dot.
- **Later features.** Slicing needs ranges, walking an array needs for-each loops, and an array of mixed types needs tuples.
