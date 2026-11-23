# Commit 4.2: Heap Arrays and Pointers

## The Feature

- **Handles.** `arr[T]` is an array on the heap whose length is decided at run time, its elements' address and how many there are, and `ptr[T]` is one value on the heap, never null. `mut` means what its position says, as it does for fixed arrays: `arr[mut T]` writes its elements, `mut arr[T]` can be rebound, and the same goes for `ptr[mut T]`, whose pointee can be written, and `mut ptr[T]`. Handles nest, `arr[arr[int]]`, and a fixed array can hold them, `arr[3, ptr[int]]`.
- **Making them.** Calling a type makes one, with an array's length in brackets, where a fixed array's size goes, and only what fills it in the parentheses:

  | Call | What it makes |
  |---|---|
  | `arr[n, mut T]()` | `n` elements of `T`'s default: `0`, `0.0`, `false`, `'\0'`, and an empty array for an element that's an array, where a pointer has none, since it's never null |
  | `arr[n, mut T](fill)` | `n` deep copies of `fill` |
  | `arr[n, mut T](_)` | `n` elements left uninitialized, for elements that own nothing and can be written, as a fixed array's `<- _` is |
  | `arr[T](xs)` | the elements of the fixed array `xs`, on the heap, whose size `xs` already knows |
  | `ptr[T](v)` | `v`, on the heap |

- **The length decides the placement.** A length the program knows when it compiles makes a fixed array, so `arr[3, int](0)` is `[0, 0, 0]`, an `arr[3, int]`, and one it only knows at run time makes an `arr[int]` on the heap. `let n <- 3;` is a constant, which makes `arr[n, int]()` fixed, and `let n :: mut <- 3;` isn't. A type never holds a run-time length, so an annotation still needs a constant, `arr[3, int]`, and an array on the heap is `arr[int]` whatever its length.
- **Values in brackets.** A conversion or a call works as a length, `arr[i64u(n), int](0)` and `arr[xs.len() + 1, int](0)`, while a plain path, `std.mem.Allocator`, is still a type. A run-time length is any unsigned integer, and a signed one is `cherimoya requires an array's length to be unsigned, please convert it, like 'i64u(n)'`, with a fix that wraps it, while a constant can be any integer that folds. A length written as an argument is `cherimoya uses brackets for an array's length instead of an argument as follows: 'arr[n, int](fill)'`, with a fix that moves it. A fixed array can be made into a handle through a pipe, `xs |> arr[int]`.
- **Fills.** `fill` is borrowed, and when it owns something, each element gets a deep copy of it, so `p` still holds its value after `arr[n, mut ptr[int]](p)`. `arr[T](xs)` and `ptr[T](v)` move what they're given. A pointer has no default, so `arr[n, mut ptr[int]]()` is `cherimoya requires the elements of 'arr[n, mut ptr[int]]()' to have a default, please give it a fill, like 'arr[n, mut ptr[int]](fill)'`.
- **Inferred bindings take the call's `mut`.** `let b <- arr[n, mut int](0);` can write `b[0]`, and `let b <- arr[n, int](0);` can't.
- **Running out of memory.** When allocating fails, or `n` elements wouldn't fit in memory at all, the program traps:

  ```
  trap at line 4: out of memory making 'arr[n, mut int]()'
  ```

- **Using them.** `xs[i]` is an element, bounds-checked at run time with the trap a fixed array has, and `xs.len()` is the length. `p[]` is a pointer's value, and `p[] <- v` writes it, which needs `ptr[mut T]`. Writing an element or a pointee that owns something frees what it held first. `=` asks whether two handles are the same memory, the same address and for arrays the same length, and `==` compares what they hold, arrays by length and then element by element, pointers by their values, each with `==`.
- **Ownership.** A binding owns the handle it holds and frees it at the end of its scope, at its `}` or wherever a yield leaves early, last declared first. An array frees its elements first. A top-level binding's handle is freed after `start` returns, so a run under valgrind ends with nothing leaked, and a trap frees nothing, since the process is ending. A binding that owns something can't start out uninitialized, `let xs :: mut arr[int] <- _;`: `cherimoya requires an uninitialized binding to own nothing, please give it a value`.
- **Moving.** Binding a handle, `let b <- a;`, assigning it, `b <- a;`, yielding it, and putting it in a pack, a `ptr`, or `arr[T](xs)` all move it, and using it after that is an error:

  ```
  cherimoya requires 'a' to hold a value to use it, please assign it again after moving it on line 4
  ```

  Assigning to a moved binding makes it hold a value again, and assigning to one that still holds a value frees that value first. A handle made and never bound, like `arr[n, int]().len()` or the argument in `total(arr[n, int]())`, is freed at the end of its statement.
- **What can't move.** An element, `let p <- ps[0];`, is `cherimoya doesn't support moving out of an element yet, which take will do`. A top-level binding is `cherimoya requires 'table' to be owned by a function to move it, please use it in place, since every function can use it`. A match's binder borrows its subject: `cherimoya requires 'b' to be owned to move it, please move what it matched instead`.
- **Matches and loops.** A handle moved in some arms of a match is moved after it, and each arm that didn't move it frees it at its end. A handle from outside a loop moved inside it has to be assigned again before the iteration ends: `cherimoya requires 'xs' to hold a value at the end of every iteration, please assign it again after moving it on line 7`, so `repeat (...) { xs <- grow(xs); }` works. A binding moved before the loop that the body assigns is freed at the end of each iteration, so every iteration starts with it moved, as the first did.
- **Inputs borrow.** `total(xs)` lends `xs` for the call: the caller still owns and frees it, and the call can read it, write its `mut` elements, and pass it on, but not move, rebind, or yield it: `cherimoya requires 'xs' to be owned to move it, please declare it 'unscoped' to take it over`.
- **`unscoped` inputs take over.** `(xs :: unscoped arr[mut int])` owns what it's given, so the call moves the caller's argument in. The function has to yield it, move it, or pass it to another `unscoped` input, and letting it go is `cherimoya requires 'xs' to be moved before 'grow' ends, please yield it or pass it to an unscoped input`. That's how a function hands an array back changed, `xs <- grow(xs);`, and `let given <- xs;` makes the function's own binding the owner, which frees it at its scope's end. Moving it in only some arms of a match is the same error on the others. Only heap memory can be unscoped, `cherimoya requires an unscoped input to hold an array or a pointer, please remove 'unscoped'`, and only inputs, `cherimoya doesn't support 'unscoped' on anything but an input yet`.
- **Outputs move out.** A function can yield a handle it made or took over, and the caller owns it from then on.
- **`cheri test --runner`.** Puts a command in front of each test binary, split at its spaces, `cheri test tests/features --runner 'valgrind -q'`, the way a cross-compiled test could run under qemu.

## Why

A fixed array's size has to be known when the program compiles, so a program can't hold as many values as its input asks for, and nothing made in one function can outlive it. Handles fix both, and since they own heap memory, they bring what owning means: each one is freed exactly once, when its owner is done with it, and using one that's been handed away is a compile error.

- **Stack unless truly variable.** A value whose type bounds its size, like `arr[N, T]`, is inline wherever its binding lives, and a size bounded by a refinement will be too once refinements exist, including growth: a list declared to hold at most 64 elements will reserve 64 on the stack. Only a size known just at run time goes on the heap, which is why the length alone decides between `arr[N, T]` and `arr[T]`.
- **Handles that never leave go on the stack anyway.** A binding made straight from `arr[int](xs)` or `ptr[T](v)` that's never moved or replaced, and a temporary of one, like the array in `total(arr[int]([1, 2]))`, each up to 4 KiB so a loop of them or deep recursion can't run out of stack, lives in the function's frame. It's never allocated or freed, while its elements still free what they own. That's an optimization, not a rule a program can see.
- **Brackets read values.** A type is never called in brackets, so a `(` after a type spelling or a dotted path makes it a value, which a run-time length often needs.
- **Deep copies of a fill,** since `n` elements sharing one handle would free it `n` times. The compiler copies its own handles, arrays and pointers all the way down, and `Clone` will copy other types once protocols exist.
- **One owner, moves instead of copies,** so freeing is exact and needs no flags at run time: sema knows which bindings hold a value at each point, and codegen frees exactly those.
- **No uninitialized owners,** since a binding always has to hold something it can free, until contracts can prove it's written first.
- **No moving out of an element,** which would leave a hole the array frees; `take` will do that. A top-level binding can't move because any function can use it at any time.
- **Inputs borrow by default,** standing in for `peek` until views exist. `unscoped` is the mode for heap memory that has to be moved out before its scope ends, which is exactly what a function that takes an array over needs.

## Implementation

### Parser

`parseGenericArg` in `src/parser/parser.hpp` reads a type spelling followed by `(` as a conversion, a value, like the `i64u(n)` in `arr[i64u(n), int](0)`, and a dotted path whose last name is followed by `(` as a call or method call, like `xs.len()`, while a path on its own stays a type.

### Sema

- **Types.** `src/sema/type.hpp` adds `HandleKind` (`Array`, `Pointer`), `HandleSemaType` with its `element`, and `asHandle`, `asHeapArray`, and `asPointer`. `elementOf` gives what a type holds one level in, an array's elements or a pointer's value, which `mutabilityOf` and `writableAt` walk, and `owns` says whether a value owns memory, a handle or a fixed array of anything that does. `src/sema/context.hpp` interns handles in `handleTypes`, keyed by kind and element. `resolveType` turns a handle with no size into a `HandleSemaType`, and resolves a `ModeType` holding `unscoped` only while `isResolvingInput` is set.
- **Symbols.** `Symbol` in `src/sema/scope.hpp` gains `isOwned`: a binding and an `unscoped` input free what they hold, while any other input and a match's binder borrow. `makeSymbol` takes it.
- **Making handles.** `checkConversion` sends `arr[n, T](...)` to `checkSizedMake` and any other handle to `checkMake`. `checkSizedMake` reads a lone name in the brackets as an `IdentifierExpr` it makes and keeps in `madeNames`, checks the length as an `i64u`, folds it to choose an `ArraySemaType` or an `arr[T]`, records it in `lengths` for codegen, and checks the fill: `hasDefault` for no fill, and for `_`, that the elements own nothing (`cherimoya requires an uninitialized array's elements to own nothing, please give it a fill`) and can be written (`cherimoya requires an uninitialized array's elements to be mutable, please write 'arr[n, mut int](_)'`). `checkMake` checks `ptr[T](v)` and `arr[T](xs)`, consuming what they're made from, and gives the fix that moves a length argument into brackets. `madeAs` finds the handle type a call or pipe makes, which `checkLet` reads to give an inferred binding the call's `mut`.
- **Indexing.** `checkIndex` takes a heap array's index like a fixed array's, with no folded bound, and `p[]` gives a pointer's value, or `cherimoya expected a pointer before '[]', got 'int' instead`. `checkElementPlace` says "the value of" for a pointee, and `writableAt` spells the type to declare, `ptr[mut int]`. `hasMethod` gives heap arrays `len`.
- **Ownership as it checks.** Sema tracks, as it checks each function in order, `moved` (each owned binding holding nothing, with where it moved), `isReachable`, `owners` (each open scope's owned bindings), `takenOver` (the function's `unscoped` inputs), and `fresh` (owning values the current statement made that nothing has moved). `consume` moves a value: it reports a use after a move, a top-level binding, a borrowed input or binder, or an element, and otherwise marks the binding moved, or drops a fresh value from `fresh`. It's called by `let`, assignment, yields, packs, `checkMake`, and arguments to `unscoped` inputs, once every argument is evaluated, so another argument can still read the value first. Reading a moved identifier reports once. Assigning to an owned binding records it in `replaces` unless it moved, and to a borrowed one is `cherimoya requires 'xs' to be owned to replace it, please declare it 'unscoped' to take it over`.
- **Joining control.** `Target` in `src/sema/target.hpp` gains `owners`, how many scopes were open outside it, and `flows`, each a `Flow` of a site, what was `Moved` there, and a span, which `flowOut` records for every way control leaves a construct to carry on after it. `join` takes the union of what's moved, keeps only bindings still in scope, adds each binding moved elsewhere to the `drops` of every flow that didn't move it, and reports an `unscoped` input through `unmoved`. A match starts each arm from what was moved before it, with a flow for no arm matching, and a loop checks each iteration's end against where it started, then carries on from there. `liveOwners` gives the bindings to free leaving scopes from a depth in, last declared first, which the closing brace of a scope, a function, and a loop body, and every yield, record in `drops`. Each statement's leftover `fresh` values become `temporaries`.
- **The stack.** `stackCandidates` holds every function binding made straight from a handle, `escaped` every binding that moved or was replaced, and once the file is checked, the candidates that stayed put and fit, by `madeCount` and `byteSize`, within `stackLimit` (4096) bytes go in `stackOwners` and `stackMade`, along with temporaries that fit.
- **What codegen reads.** `dropsAt`, `isTemporary`, `isOnStack` (for an expression and for a symbol), `stackCount`, `lengthOf`, and `isReplacing`.

### Codegen

- **Types.** `lower` makes an `arr[T]` a `{ ptr, i64 }` and a `ptr[T]` a `ptr`. `Codegen` takes the checked `Sema`, which `tools/cheri/main.cpp` passes.
- **Making.** `emitMake` evaluates the length, then the fill, in written order, and gets memory from `allocate`, which calls the runtime's `cheri_alloc` with the count, the element size from `sizeOf`, whether to zero it, and an `out of memory making '...'` message. A default fill, or a fill that's a constant of all zero bits, like `0` or `false`, allocates zeroed memory, so `arr[n, int](0)` is `arr[n, int]()`. Any other fill is a `forEach` loop storing a `copy` in each element, which at `-O3` LLVM turns into a `memset` when every byte is the same and vectorizes otherwise. A handle sema put on the stack is an `alloca` of its elements in the entry block, zeroed for a default fill. `heapArray` packs an address and a length.
- **Freeing and copying.** `drop` frees a value all the way down, each owning element and then the memory with `free`, skipping the `free` for a handle on the stack, and skipping a null pointer, which only a top-level binding not yet written holds. `copy` makes a deep copy, with `memcpy` for elements that own nothing. `dropAll` frees the bindings sema lists for a site.
- **Temporaries.** `emitStmt` opens a frame in `temporaries` for each statement and frees it at the end, and `withTemporaries` gives a condition, a guard, the right side of `&&` and `||`, and a default its own. `emitExpr` records every value sema marks a temporary, and a call's default that owns something and goes to a borrowing input is the call's temporary. `Exit` gains `frames`, so a yield frees the temporaries of every statement it leaves with `dropTemporaries`, then the bindings in `dropsAt`.
- **Scopes.** Blocks, arms, loop bodies, and functions free their `dropsAt` at their end, a match frees the no-arm drops, and a failed loop condition goes through a `repeat.leave` block that frees what sema says. Assignment frees what an element or pointee held, and what a binding held when `isReplacing`, before storing.
- **Indexing and comparing.** `elementAddress` loads a heap array's handle, checks against its length, and indexes its data, and `p[]` loads the pointer. `compareHandles` compares addresses (and lengths) for `=`, and for `==` uses `compareHeapArrays`, a loop checking lengths and then each pair, or compares the pointees. `emitLen` gives a heap array's stored length.
- **`fini`.** Top-level bindings that own something are kept in `owningGlobals`, and `cheri.<module>.fini`, aliased as `cheri_fini`, frees them last made first. A top-level `let` a loop runs again frees what its global held first.

### Runtime

`runtime/runtime.c` adds `cheri_alloc`, which returns null for zero bytes, traps when `count * size` overflows or `malloc`/`calloc` fails, and zeroes when asked. `runtime/start.c` and `runtime/test.c` call `cheri_fini` after `start` or the test returns.

### Debug Info

`typeOf` in `src/codegen/debug.hpp` describes a `ptr[T]` as a pointer gdb follows, and an `arr[T]` as a structure of `data` and `len`. `HeapArrayPrinter` in `src/codegen/printers.py` shows it by its elements, `arr[int] of 3 = {1, 2, 3}`, with 1-byte elements as numbers.

### Language Server

`src/lsp/features.hpp` shows `unscoped` on an input that takes over what it's given in hover and signatures, through `render` and `inputText`, and gives `arr`, `ptr`, and `unscoped` their help text.

### Tree-sitter

`tree-sitter/grammar.js` adds `_conversion` and `_calledPath`, which read a type spelling or dotted path called in a generic argument as a call, only in the `argument` family, so the grammar keeps following cheri's parser.

### Build

`tools/cheri/main.cpp` adds `--runner` to `Options` and `runTest`, finding the runner on the path. When valgrind is installed, `scripts/run_test.sh` runs every program in `tests/run/` under it, with its timeout raised to 30 seconds, and `scripts/feature_test.sh` runs every feature test under it through `--runner`. A leak, a double free, or a read of memory never written fails with valgrind's exit code, 213, which no test expects, where only memory nothing points to counts as leaked, since a trap aborts with everything in place. The README tells Zed's settings to pass `lsp` as an argument with cheri's path, since Zed starts the binary with exactly the arguments they give, and `.gitignore` ignores a local `.clangd`, for which `src/sema/sema.hpp` now includes everything it uses.

## Tests

`tests/unit/sema/test_sema.cpp` checks making, indexing, dereferencing, comparing, and writing handles, every move and what can't move, borrowed and unscoped inputs, matches and loops, the fix for a signed length, and which handles go on the stack. `tests/unit/parser/test_parser.cpp` checks conversions and called paths in brackets, and `tests/unit/lsp/test_lsp.cpp` hovering over an unscoped input. `tests/features/handles.toya` runs every way to make, use, move, and free them, including the traps for an index past the end and for running out of memory, and `tests/debug/handles.moya` prints them in gdb. 14672 unit assertions, 9 programs, the run of `tests/test/`, 175 feature tests, 7 debug programs, all under valgrind, and the grammar's 164 corpus tests, 0 failures.

## Current Limitations

- **The heap for run-time sizes.** A handle whose size is only known at run time is on the heap until refinements can bound it, and one that moves stays there even when it never leaves its function.
- **Borrowing without views' rules.** Passing the same array twice, once with `mut` elements, isn't caught, until views bring their rules.
- **`unscoped`** is only for inputs.
- **Copies.** A deep copy of anything but a fill waits for `Clone`.
- **`--runner`** splits its command at spaces, without quoting.
- **Sources.** `arr[T](...)` only takes fixed arrays until ranges and comprehensions exist.
- **Growth.** Arrays can't grow in place, and `List` comes with the standard library.
- **Null.** A pointer can't be null, and `Option[ptr[T]]` comes with sums.
