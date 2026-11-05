# Commit 9.4: Views and Modes

## The Problem

Commit 9.2 gave handles owners and checked that each one is freed exactly once, but borrowing was only a stand-in: an input borrows the handle it's given, and nothing else can. A program can't hold on to part of an array without copying it, can't let a function write one of its own `int`s, and can't make a binding that has to be handed on. Worse, an input that writes an array's elements could be given an array whose elements were declared immutable. Commit 9.4 brings the modes the spec describes: `peek`, a view of a place that never owns or frees anything, `unscoped` on bindings and outputs, and the checks that keep every view pointing at memory that's still there. Those checks need to know where control can go and what's used later, so ownership moves onto a control-flow graph, which Z3 will check contracts on too.

## Views

`peek T` is a view of a place of any type: a binding, an element, or what a pointer points at. A view never owns what it views, so it never frees it, and it's transparent: it reads and writes as the thing it views, with no dereference to write.

```
let x :: mut <- 1;
let v :: peek mut <- x;
v <- v + 1;              # x is 2
let grid <- arr[n, mut arr[mut int]](arr[m, mut int](0));
let row :: peek arr[mut int] <- grid[1];
row[0] <- 5;             # grid[1][0] is 5, with nothing copied
```

`mut` sits where it does on any type: `peek mut int` can write the `int`, `peek arr[mut int]` the elements, and writing through a view needs the place it views to be writable too, so a `peek mut` of an immutable binding is an error. A view is made from a place, so viewing a value that isn't one, like a call's result, is `cherimoya requires a view to be of a place, please bind the value first and view the binding`, unless the call returns a view itself. A view can't be rebound to view something else, so a `peek` binding is never `mut` on its own level.

An input is a view when it says so, `(n :: peek mut int)`, which is how a function writes its caller's `int`, and an input of an owning type, like `arr[int]`, is a view without saying so, the way it borrowed in 9.2, so either one is passed the address of the place it's given. An input of a plain value type, like `int`, is still a copy. Passing a place where a view is wanted takes the view, and passing a value that isn't a place views it for the call. What a view input can write has to be writable where it comes from: an array whose elements can't be written passed to an input that writes them, `(xs :: arr[mut int])`, is `cherimoya requires the elements of 'xs' to be mutable to pass it to 'double', please declare it as 'arr[mut int]'`, and an immutable `int` passed to a `peek mut int` is `cherimoya requires 'x' to be mutable to pass it to 'bump', please declare it with 'let x :: mut <- ...;'`, while a value that isn't a place is a copy nothing else sees, so it can always be written. A written view input, unlike a borrowed handle, can be written whole, which replaces what the caller's place holds.

## What Keeps a View Safe

A view points into memory something else owns, so the one thing that can go wrong is that memory going away while the view is still used. Any number of views can read and write the same place, since writing an `int` through one view and reading it through another can't touch freed memory, so the only thing views rule out is freeing or moving what one points into.

A view remembers exactly what it views, its path: the binding at its base and every index and pointer after it, `grid[2][0]` or `p[]`. A view holds the address of the place it views, so what breaks it is freeing or moving something it points into: replacing `grid[1]` frees the old row, which a view of `grid[1][0]` points into, while a view of `grid[1]` itself points at the element, which holds the new row after, so it's still fine. A view of `xs[1]` is kept in place by what happens to `xs`, but not to `xs[0]`. An index the program knows when it compiles tells elements apart exactly, and one it only knows at run time might be any element, so `xs[i]` overlaps every element of `xs`, until Z3 can prove two indices differ. While a view is live, from where it's made to where it's last used, nothing that overlaps its path can be:

- moved, `let ys <- xs;`, or passed to an `unscoped` input
- replaced or rebound, `xs <- ...`, which frees what it held, so every view into it, though not a view of `xs` itself
- written over where what's written owns memory, `grid[0] <- ...`, which frees the row a view of `grid[0][j]` points into, while `grid[1] <- ...` leaves it be
- passed to an input that could do any of those, one that writes elements that own memory

Each is an error where it happens, with a note where the view is used later:

```
cherimoya requires 'grid' to stay where it is while 'e' views it, please use 'e' for the last time before this
```

A view is only live until its last use, so `let e :: peek <- grid[1][0]; total(grid[1]); _ <- e + 1; grid[1] <- arr[m, int](1);` is fine, since `e` isn't used after the replacement. A view also can't outlive what it views: it's made from a place that's already in scope, it can't be rebound, and only a function's output can hand one on, which has to view one of the function's inputs.

## Returning Views

An output can be a view, `-> (r :: peek arr[int])`, as long as every view the function yields views one of its inputs that are views, with no annotation saying which: at a call, the result views every argument passed to an input that's a view, written or not, so it can be the arguments' paths and nothing else, and a call whose view would be of a value that isn't a place, which is gone once its statement ends, is the error a view of a value is. `longest(a, b)` returns a view of `a` or of `b`, so the result keeps both from being freed or moved while it's live. A function that yields a view of something else, one of its own bindings, is `cherimoya requires 'longest' to yield a view of one of its inputs, please yield a view of one, or a copy`.

## unscoped Bindings and Outputs

`unscoped` on a binding, `let buf :: unscoped arr[int] <- ...`, means what it means on an input: the binding owns heap memory that has to be moved before its scope ends, by yielding it, moving it into another binding, or passing it to an `unscoped` input, and letting it go is `cherimoya requires 'buf' to be moved before its scope ends, please yield it or pass it to an unscoped input`. On an output, `-> (r :: unscoped arr[int])`, it says the value outlives the function, so it's never placed on the stack. A plain output still moves its value out, as in 9.2. Only heap memory can be `unscoped`, `cherimoya requires an unscoped binding to hold an array or a pointer, please remove 'unscoped'`, and only in a function, since a top-level binding is a global every function can use, which is never moved: `cherimoya requires 'buf' to be in a function to be unscoped, please remove 'unscoped', since a top-level binding is freed when the program ends`.

## The Control-Flow Graph

Sema lowers each function, once it's checked, to a control-flow graph: basic blocks of the steps the function takes, each a use, a move, a binding, an assignment, a view taken, or a scope left, joined by edges wherever control can go next, a match's arms, a loop's way back and its ways out, and every yield. Ownership is dataflow over the graph:

- **Moves** flow forward: a binding moved on any way into a block is moved there, and where ways join that disagree, the ones that still hold the value free it on their edge, which is how 9.2's matches and loops already worked, now for any shape of control.
- **Liveness** flows backward: a view is live wherever a later use of it can still be reached, which is what the rules above check against.
- **Drops** come from both: wherever control leaves a scope, every binding still holding a value is freed.

Codegen reads the same answers it did in 9.2, which bindings to free at each way out of a scope, which values are temporaries, and which assignments replace a value, so it barely changes, except to lower views: a view is the address of what it views, its slot holds that address, and reading or writing one goes through it, a view input is passed the address of the place it's given, a function that yields a view yields an address, and reading a call that does reads what it views. A handle that a view can replace whole might be freed through it, so the stack optimization leaves it on the heap. With `-g`, a view is a reference, which gdb follows to print what it views: `n = (int &) @0x7ffd…: 1`.

## What Was Built

`src/sema/cfg.hpp` is new: `Path` and `reaches`, which tell whether a view points into what happens to a place, `Facts`, what sema learns about ownership, and `Ownership`, which builds each function's graph and works out its moves, its frees, and its views' liveness. `src/sema/sema.hpp` no longer tracks ownership as it checks, and instead records the facts, checks views and their places, the mutability what's passed needs, modes on bindings and outputs, and the inputs a yielded view views, and runs the graph once each function is checked. `src/sema/type.hpp` and `src/sema/context.hpp` gained `ViewSemaType`, which signatures use, `src/sema/scope.hpp` gave symbols `isView`, and `src/sema/target.hpp` lost the flows 9.2 joined. `src/codegen/codegen.hpp` lowers views, passes view inputs by address, and reads views returned by calls, `src/codegen/debug.hpp` describes views as references, and `src/lsp/features.hpp` shows `peek`. `tests/unit/sema/test_sema.cpp` checks views, their places and paths, every way to break one, loops, returning views, and unscoped bindings, `tests/features/views.toya` runs them, and `tests/debug/views.moya` prints them in gdb. 14743 unit assertions, 9 programs, the run of `tests/test/`, 199 feature tests, 9 debug programs, all under valgrind, and the grammar's corpus, 0 failures.

## Current Limitations

Views can't be stored in records or packs, since those come in commit 11, so a view lives in a binding or an input. A call that yields a view keeps every argument passed to a view input in place, not just the ones it could really yield, until sema works out which inputs each function's views come from. A move inside the right side of `&&` or `||` is treated as if it always happens, since the graph doesn't branch there yet. An index only known at run time overlaps every element, so a view of `xs[i]` keeps every element of `xs` in place until Z3 can tell indices apart. Slicing, `xs[a, ..., <b]`, comes with ranges. Threads aren't here yet, so views don't stop two of them racing: once concurrency arrives, sharing across threads will need its own rules.
