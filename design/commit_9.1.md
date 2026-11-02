# Commit 9.1: Fixed Arrays

## The Problem

Every value cherimoya has so far is one number, character, or truth value, so a program that works on a list of anything has to hold each one in a binding of its own. Commit 9 brings arrays, pointers, and strings in three steps, since each one leans on something the one before it builds. Commit 9.1 is fixed arrays, `arr[N] T`, whose size the program knows when it compiles and whose elements sit inline, like any value, so nothing has to be freed. Arrays on the heap and pointers, which do, come in 9.2, and strings, as UTF-8 bytes, in 9.3.

## The Type

`arr[N] T` holds `N` values of type `T` side by side, the way the spec's homogeneous pack does, and like an `int`, it's copied when it's bound, passed, or returned, so writing a copy's elements leaves the original as it was. Arrays nest, `arr[2] arr[3] int`, and any type can be an element.

`N` has to be known when the program compiles: an integer literal, or an immutable binding whose initializer folds to one, through `+ - * / %`, like `let N <- 2 + 1;`. Anything else is `cherimoya requires an array's size to be known when the program compiles, please use an integer literal or an immutable binding of one`, and that includes a size below zero. A function's signature is resolved before any `let` in its scope is checked, so a constant let is found the way a function is: each scope folds its constant lets before anything in it is checked, and an array size anywhere in the scope can use one, even one declared after it.

## Literals

`[1, 2, 3]` is an `arr[3] int`. The elements share one type, picked the way an operator picks its operands': the element type the context expects, so `let xs :: arr[3] i8 <- [1, 2, 3];` makes three `i8`s, else the first element that isn't a literal, so `[1, x]` takes `x`'s type, else a float literal's, so `[1, 2.5]` is floats. An element of another type is `cherimoya doesn't support packs whose elements have different types yet, which are tuples, please give every element the type 'int'`, since `[1, true]` is a tuple, which comes with packs. An empty literal needs its type from where it goes, `([] :: arr[0] int)`, and a named element, `[x <- 1]`, is a record, which waits too.

## Indexing and Methods

`xs[i]` is the element at `i`, from 0, and `m[i][j]` reaches into a nested array. An index is any unsigned integer, wrapping or not, so a negative one can't be written, and a literal index is an `i64u`. A signed one is `cherimoya requires an index to be unsigned, please convert it, like 'i64u(i)'`, with a fix that wraps it in `i64u(...)`, which traps on a negative value. An index the program knows when it compiles has to be in range: `cherimoya expected an index below 3 into 'xs', got 5 instead`. Any other index past the end traps, with both numbers:

```
trap at line 6: index 7 is past the end of 'xs', which holds 3
```

`xs.len()` is how many elements an array holds, as an `i64u`, the type that indexes it. It's a method, and methods are what protocols give the types that implement them: every array implements `Sized`, a built-in protocol whose one method is `len`, until protocols can be written in commit 12. Protocols stay small, one capability each, since a call on a known type dispatches statically and costs nothing more for it.

A method is called on its object, `xs.len()`, piped into with its dot, `xs |> .len` or `.len <| xs`, or called like a function with its object first, `len(xs)`. The dot is for what a type owns, so a plain function is never called as `x.f()`, and `3.square()` stays an error. When a name could be either, the method wins: `len(xs)` calls the method even beside a function named `len`, as long as `xs` has the method. A method without its parentheses, `xs.len`, is `cherimoya requires a method to be called, please write 'xs.len()'`, with the fix, since `xs.len` on its own will be a field once records arrive.

## Mutability

`mut` means what its position says, the way it will for handles. On the elements, `arr[3] mut int`, they can be written, `xs[i] <- v`, and in front, `mut arr[3] int`, the whole array can be replaced, `xs <- [4, 5, 6]`. Nested arrays take one per level, so `arr[2] mut arr[3] int` can replace its rows but not their elements, and a bare `mut` on a type left to inference, `let m :: mut <- [[1, 2], [3, 4]];`, allows everything at every level. Writing elements that can't be is `cherimoya requires the elements of 'xs' to be mutable to assign to them, please declare it as 'arr[3] mut int'`, with a fix that adds `:: mut` when the binding has no type. An array can start out uninitialized, `let v :: arr[16] mut int <- _;`, when its elements can be written, since that's how it gets its values. An element's index runs before the value it's assigned, the order they're written.

The type is the same wherever `mut` sits, since it says what a binding can do, not what the value is: each symbol carries one flag per level, the binding itself first and then each level of elements, so replacing it and writing an element at any depth are the same check.

## Equality

`=` and `==` compare whole arrays, element by element, each with the same operator, so `=` compares floats bit for bit and, once arrays hold pointers, pointers by their address, while `==` compares floats by their total order and pointers by what they point to. For an `arr[N] int` the two agree. Nothing orders arrays, so `<` on one is an error.

## Codegen

An `arr[N] T` is LLVM's `[N x T]`, a first-class value that bindings store, functions take and return, and globals hold. A literal whose elements are all constants is a constant array, which a top-level binding starts out holding, and anything else is built one element at a time. An element's address comes from its array's address, a binding's slot or global, or a slot the value is put in first, like a call's result, then a bounds check against the length, which LLVM folds away when the index is a constant, and a `getelementptr`. Comparing arrays loops over the elements, stopping at the first pair that differs. The bounds trap is a runtime function of its own, `cheri_trap_index`, since the index and the length are only known at run time.

With `-g`, an array is a DWARF array type, so gdb prints `{{1, 2, 3}, {4, 5, 6}}`, and the embedded printers print an array of 1-byte integers as its numbers, `{-1, 0, 1}`, where gdb would print a C string.

## What Was Built

`src/sema/type.hpp` and `src/sema/context.hpp` gained `ArraySemaType`. `src/sema/scope.hpp` gave scopes their constants and symbols one mutability per level, the binding itself first, which `isMutable` reads. `src/sema/sema.hpp` gained `fold`, `arraySize`, `mutabilityOf`, `checkPack`, `checkIndex`, `checkElementPlace`, and methods, `hasMethod`, `checkMethod`, and `checkMember`, resolves array types, and hoists constant lets. `src/parser/parser.hpp` parses a method with no object, `.len`, for pipes. `src/codegen/codegen.hpp` gained array types, `emitPack`, `placeAddress`, `elementAddress`, `compareArrays`, and `len`, and assignment goes through a place's address. `src/codegen/debug.hpp` and `src/codegen/printers.py` describe and print arrays, `runtime/runtime.c` gained `cheri_trap_index`, and `src/lsp/features.hpp` says what `arr` is.

`tests/unit/sema/test_sema.cpp` checks literals and their types, sizes, including a constant declared after the signature using it, indexing, equality, every position of `mut`, every way to call a method and get one wrong, and the fixes. `tests/run/` gained a program that uses all of it, one that passes arrays into and out of functions as copies, and a trap past the end, and `tests/debug/` one that prints arrays in gdb. 14560 unit assertions, 64 programs, 6 debug programs, and the run of `tests/test/`, 0 failures.

## Current Limitations

Arrays on the heap, `arr mut T(n)`, and pointers come in 9.2, and strings in 9.3. Slicing needs ranges, and walking an array with `repeat (x) <- xs` needs for-each loops, which both come in commit 10, along with packs of different types. Every copy of an array copies all of it, until views, `peek arr T`, come with ownership in commit 16. An index the program can't fold is always checked at run time, until Z3 can prove it in range. The only method is `len`, until protocols can be written, and the language server doesn't complete methods after a dot yet.
