# Commit 9.2: Heap Arrays and Pointers

## The Problem

A fixed array's size has to be known when the program compiles, so a program can't hold as many values as its input asks for, and nothing can be made in one function and kept alive past it. Commit 9.2 brings handles: `arr T`, an array whose length is decided at run time, and `ptr T`, one value. Both own memory on the heap, so 9.2 also brings what owning means: each one is freed exactly once, when its owner is done with it, and using one that's already been handed away is a compile error.

## Placement

Everything goes on the stack unless its size is truly variable, and only that goes on the heap. A value whose type bounds its size, like `arr[N] T`, is inline, wherever its binding lives, and a size bounded by a refinement will be too, once refinements arrive with Z3. That includes growth: a list declared to hold at most 64 elements will reserve 64 on the stack. Until then, a size that's only known at run time goes on the heap. A size the program knows when it compiles goes on the stack already, when the handle never leaves its function: a binding made straight from `arr int(xs)`, whose fixed array knows its size, or `ptr T(v)`, which is always one value, that's never moved or replaced, and a temporary of one, like the array in `total(arr int([1, 2]))`, each up to 4 KiB, so a loop of them or deep recursion can't run out of stack. Its memory is part of the function's frame, so it's never allocated or freed, while its elements still free what they own. That's an optimization, not a rule a program can see: the handle behaves the same either way.

## The Types

| Type | What it is |
|---|---|
| `arr T` | a handle to `T`s on the heap: their address and how many there are |
| `ptr T` | a handle to one `T` on the heap, never null |

`mut` means what its position says, the way it does for fixed arrays. `arr mut T` can write its elements and `mut arr T` can be rebound to another array, and the same goes for `ptr mut T`, whose pointee can be written, and `mut ptr T`. Handles nest, `arr arr int`, and a fixed array can hold them, `arr[3] ptr int`.

## Making Them

Calling a type makes one, where an array's length goes in brackets, the same place a fixed array's size does, and the parentheses only hold what fills it:

| Call | What it makes |
|---|---|
| `arr[n] mut T()` | `n` elements of `T`'s default: `0`, `0.0`, `false`, `'\0'`, and an empty array for an element that's an array, where a pointer has none, since it's never null |
| `arr[n] mut T(fill)` | `n` deep copies of `fill` |
| `arr[n] mut T(_)` | `n` elements left uninitialized, for elements that own nothing and can be written, as a fixed array's `<- _` is |
| `arr T(xs)` | the elements of the fixed array `xs`, on the heap, whose size `xs` already knows |
| `ptr T(v)` | `v`, on the heap |

The size alone decides where an array goes: a length the program knows when it compiles makes a fixed array, so `arr[3] int(0)` is `[0, 0, 0]`, an `arr[3] int`, and one it only knows at run time makes an `arr int` on the heap. That's the placement rule in the syntax: `let n <- 3;` is a constant, which makes `arr[n] int()` fixed, and `let n :: mut <- 3;` isn't. A type never holds a run-time length, so an annotation still needs a constant, `arr[3] int`, and an array on the heap is `arr int` whatever its length.

Brackets read a value the way they read a size, so a conversion or a call works there, `arr[i64u(n)] int(0)` and `arr[xs.len() + 1] int(0)`, where cheri's parser and the tree-sitter grammar used to read anything starting with a type's name or a dotted path as a type: a type is never called in brackets, so a `(` after one makes it a value, and a plain path, `std.mem.Allocator`, is still a type. The old form, a length in the parentheses, is `cherimoya uses brackets for an array's length instead of an argument as follows: 'arr[n] int(fill)'`, with a fix that moves it. Calling one through a pipe works for a fixed array, `xs |> arr int`. A run-time length is any unsigned integer, the way an index is, and a signed one is `cherimoya requires an array's length to be unsigned, please convert it, like 'i64u(n)'`, with the fix that wraps it, while a constant can be any integer that folds, like a fixed array's size. `fill` is borrowed, and when it owns something, each element gets a deep copy of it, since `n` elements sharing one handle would free it `n` times, so `p` still holds its value after `arr[n] mut ptr int(p)`. The compiler copies its own handles, arrays and pointers all the way down, and `Clone` copies other types once protocols arrive in commit 12. `arr T(xs)` and `ptr T(v)` move what they're given, when it owns anything. A pointer has no default, so `arr[n] mut ptr int()` is `cherimoya requires the elements of 'arr[n] mut ptr int()' to have a default, please give it a fill, like 'arr[n] mut ptr int(fill)'`.

A binding left to inference can write what the call that made it says, so `let b <- arr[n] mut int(0);` can write `b[0]`, and `let b <- arr[n] int(0);` can't, which makes the `mut` in the call mean something.

Allocating can fail, and when it does, or when `n` elements wouldn't fit in memory at all, the program traps:

```
trap at line 4: out of memory making 'arr[n] mut int()'
```

## Using Them

`xs[i]` is an element, bounds-checked against the length at run time with the same trap a fixed array has, and `xs.len()` is the length. `p[]` is a pointer's value, and `p[] <- v` writes it, which needs `ptr mut T`. Writing an element or a pointee that owns something frees what it held first.

`=` asks whether two handles are the same memory: the same address, and for arrays the same length. `==` compares what they hold: arrays by length and then element by element, and pointers by their values, each with `==`.

## Ownership

A binding owns the handle it holds, and frees it at the end of its scope: at its `}`, or wherever a yield leaves the scope early, last-declared first. A handle in a top-level binding is freed when the program ends, after `start` returns, so a run under valgrind ends with nothing leaked. An array that owns its elements frees them first. A trap frees nothing, since the process is ending. Since a binding always holds a value it can free, one that owns something can't start out uninitialized, `let xs :: mut arr int <- _;`, until contracts can prove it's written first: `cherimoya requires an uninitialized binding to own nothing, please give it a value`.

**Moving.** An owned handle has one owner, so binding it somewhere else moves it, and the place it came from no longer holds anything. Binding it, `let b <- a;`, assigning it, `b <- a;`, yielding it, and putting it in a pack, `ptr`, or `arr T(xs)` all move it, and using it after that is an error:

```
cherimoya requires 'a' to hold a value to use it, please assign it again after moving it on line 4
```

Assigning to a moved binding makes it hold a value again, and assigning to one that still holds a value frees that value first. A handle made and never bound, like `arr[n] int().len()` or the argument in `total(arr[n] int())`, is freed at the end of its statement.

**What can't move.** An element can't be moved out of its array, `let p <- ps[0];`, since that would leave a hole the array would free: `cherimoya doesn't support moving out of an element yet, which take will do`. A top-level binding can't be moved, since any function can use it at any time: `cherimoya requires 'table' to be owned by a function to move it, please use it in place, since every function can use it`. And a borrowed input can't, as below, and neither can a match's binder, which borrows the subject the same way: `cherimoya requires 'b' to be owned to move it, please move what it matched instead`.

**Matches and loops.** A handle moved in some arms of a match is moved after the match, and each arm that didn't move it frees it at its end, so after the match it's moved whichever arm ran. A handle declared outside a loop can only be moved inside the loop if it's assigned again before the iteration ends, since the next iteration would find it moved: `cherimoya requires 'xs' to hold a value at the end of every iteration, please assign it again after moving it on line 7`. So `repeat (...) { xs <- grow(xs); }` works. The other way round, a binding moved before the loop that the body assigns is freed at the end of each iteration, so every iteration starts with it moved, the way the first one did.

## Inputs

**An input borrows.** `total(xs)` lends `xs` to `total` for the call, so the caller still owns it and frees it, and the call works on it in place: it can read it, write its elements when they're `mut`, and pass it on to another call. It can't keep it, since the caller still frees it, so moving it, rebinding it, and yielding it are all errors: `cherimoya requires 'xs' to be owned to move it, please declare it 'unscoped' to take it over`. Borrowing stands in for `peek` until views arrive with ownership in commit 16.

**An `unscoped` input takes it over.** `(xs :: unscoped arr mut int)` owns what it's given, so the call moves the caller's argument in, without saying so, the way a binding moves, and the caller can't use it until it's assigned again. `unscoped` is the mode for heap memory that has to be moved out before its scope ends, so the function has to yield it, move it, or pass it to another `unscoped` input, and letting it go is `cherimoya requires 'xs' to be moved before 'grow' ends, please yield it or pass it to an unscoped input`. That's what a function needs to hand an array back changed, `xs <- grow(xs);`. Binding it is moving it too, so `let given <- xs;` makes the function's own binding the owner, which frees it at the end of its scope, the way `grow` lets the old array go once it's copied the elements into a bigger one. An unscoped input moved in only some arms of a match is the same error on the others, since freeing it there would be letting it go. Only heap memory can be unscoped, `cherimoya requires an unscoped input to hold an array or a pointer, please remove 'unscoped'`, and only inputs, `cherimoya doesn't support 'unscoped' on anything but an input yet`, and hovering over one in an editor says it's `unscoped`.

**Outputs move out.** A function can yield a handle it owns, one it made or took over, and the caller owns it from then on.

## Codegen

An `arr T` is `{ ptr, i64 }`, its address and length, and a `ptr T` is a `ptr`. Making an array goes through the runtime's `cheri_alloc`, which multiplies the length by the element size, checking for overflow, and traps when it overflows or `malloc` fails. A default fill allocates zeroed memory with `calloc`, since every default is zero bits, and so does a fill that's a constant of all zero bits, like `0` or `false`, which makes `arr[n] int(0)` the same as `arr[n] int()`. Any other fill is a loop storing a copy of it in each element, which at `-O3` LLVM turns into a `memset` when every byte of the fill is the same, and vectorizes otherwise. Freeing is `free`. Sema works out which bindings own what at each point, so codegen frees exactly the ones that still hold a value wherever a scope ends, and needs no flags at run time. A handle on the stack is an `alloca` of its elements in the function's entry block instead, zeroed for a default fill, and freeing it skips the `free`. The top-level bindings are freed by a function of their own, `fini`, which the runtime calls after `start` and after each test.

With `-g`, an `arr T` is a structure of its address and length, and the embedded printers show it by its elements, `arr int of 3 = {1, 2, 3}`, while a `ptr T` is a pointer gdb shows as usual.

## Testing

valgrind checks that everything is freed exactly once. When it's installed, `scripts/run_test.sh` runs every program in `tests/run/` under it, and `scripts/feature_test.sh` runs every feature test under it with `cheri test`'s new `--runner`, which puts a command in front of each test binary, split at its spaces, `cheri test tests/features --runner 'valgrind -q'`, the way a cross-compiled test could run under qemu. A leak, a double free, or a read of memory that was never written fails the test with valgrind's exit code, 213, which no test expects, where only memory nothing points to counts as leaked, since a trap aborts with everything still in place.

`tests/unit/sema/test_sema.cpp` checks making, indexing, dereferencing, comparing, and writing handles, every move and what can't move, borrowed and unscoped inputs, matches and loops, and the fix for a signed length, and which handles go on the stack, and `tests/unit/lsp/test_lsp.cpp` checks hovering over an unscoped input. `tests/features/handles.toya` runs every way to make, use, move, and free them, including the traps for an index past the end and for running out of memory, and `tests/debug/handles.moya` prints them in gdb.

## What Was Built

`src/sema/type.hpp` and `src/sema/context.hpp` gained `HandleSemaType`, `elementOf`, and `owns`, `src/sema/scope.hpp` gave symbols `isOwned`, and `src/sema/target.hpp` gave targets the ways control leaves them. `src/parser/parser.hpp` and `tree-sitter/grammar.js` read a conversion or a call in brackets as a value, and `src/sema/sema.hpp` resolves handles and unscoped inputs, checks making them and arrays of a length in brackets, indexing and dereferencing them, and works out ownership as it checks each function: `consume` moves a value, `join` puts the ways out of a match, a loop, or a block back together, and it tells codegen what to free where, what's a temporary, which assignments replace a value, and which handles go on the stack. `src/codegen/codegen.hpp` lowers handles, makes them through `cheri_alloc`, frees and deep-copies them, compares them, frees temporaries per statement and every binding sema says wherever a scope ends, and defines `fini`. `runtime/runtime.c` gained `cheri_alloc`, `runtime/start.c` and `runtime/test.c` call `fini`, `src/codegen/debug.hpp` and `src/codegen/printers.py` describe and print handles, `src/lsp/features.hpp` says what `arr`, `ptr`, and `unscoped` are, and `tools/cheri/main.cpp` gained `--runner`. The README tells Zed's settings to pass `lsp` along with cheri's path, since Zed starts a binary its settings name with exactly the arguments they give, and a `.clangd` gives clangd cheri's include directories for every header, which `src/sema/sema.hpp` now includes everything it uses for. 14672 unit assertions, 9 programs, the run of `tests/test/`, 175 feature tests, 7 debug programs, all under valgrind, and the grammar's 164 corpus tests, 0 failures.

## Current Limitations

A handle whose size is only known at run time is on the heap until refinements can bound it, and one that moves stays on the heap even when it never leaves its function. Inputs borrow without views' rules, so passing the same array twice, once with `mut` elements, isn't caught, until `peek` and its exclusivity arrive in commit 16. `unscoped` is only for inputs, and a deep copy of anything but a fill waits for `Clone`. `--runner` splits its command at spaces, without quoting. `arr T(...)` only takes fixed arrays until ranges and comprehensions arrive in commit 10. Arrays can't grow in place, and `List` comes with the standard library. A pointer can't be null, and `Option[ptr T]` comes with sums in commit 11.
