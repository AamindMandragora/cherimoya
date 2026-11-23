# Commit 4.3: Views, Modes, and the Control-Flow Graph

## The Feature

- **Views.** `peek T` is a view of a place of any type: a binding, an element, or what a pointer points at. It never owns or frees what it views, and it's transparent, reading and writing as the thing it views, with no dereference:

  ```
  let x :: mut <- 1;
  let v :: peek mut <- x;
  v <- v + 1;              # x is 2
  let grid <- arr[n, mut arr[mut int]](arr[m, mut int](0));
  let row :: peek arr[mut int] <- grid[1];
  row[0] <- 5;             # grid[1][0] is 5, with nothing copied
  ```

- **`mut` through a view.** `peek mut int` can write the `int` and `peek arr[mut int]` the elements, and only when the place it views can be written too: `cherimoya requires what 'v' views to be mutable to write it through 'v', please make it mutable or view it with 'peek'`. A view can't be rebound to view something else, so a `peek` binding is never `mut` on its own level.
- **Views are of places.** Viewing a value that isn't one, like a call's result, or leaving a view uninitialized, is `cherimoya requires a view to be of a place, please bind the value first and view the binding`, unless the call returns a view itself.
- **View inputs.** `(n :: peek mut int)` is how a function writes its caller's `int`, and an input of an owning type, like `arr[int]`, is a view without saying so, borrowing as in commit 4.2. Either one is passed the address of the place it's given, while an input of a plain value type, like `int`, is still a copy. Passing a value that isn't a place views it for the call, a copy nothing else sees, so it can always be written. A written `peek` input, unlike a borrowed handle, can be written whole, which replaces what the caller's place holds.
- **Passing needs what the input writes.** An array whose elements can't be written passed to `(xs :: arr[mut int])` is `cherimoya requires the elements of 'xs' to be mutable to pass it to 'double', please declare it as 'arr[mut int]'`, and an immutable `int` passed to a `peek mut int` is `cherimoya requires 'x' to be mutable to pass it to 'bump', please declare it with 'let x :: mut <- ...;'`.
- **Nothing a live view points into can go away.** A view remembers its path, the binding at its base and every index and pointer after it, `grid[2][0]` or `p[]`. While it's live, from where it's made to its last use, nothing that overlaps its path can be:
  - moved, `let ys <- xs;`, or passed to an `unscoped` input;
  - replaced or rebound, `xs <- ...`, which frees what it held, so every view into it, though not a view of `xs` itself;
  - written over where what's written owns memory, `grid[0] <- ...`, which frees the row a view of `grid[0][j]` points into, while `grid[1] <- ...` leaves it be;
  - passed to an input that could do any of those, one that writes elements that own memory.

  Each is an error where it happens, with a note where the view is used later:

  ```
  cherimoya requires 'grid' to stay where it is while 'e' views it, please use 'e' for the last time before this
  ```

  A view is only live until its last use, so `let e :: peek <- grid[1][0]; total(grid[1]); _ <- e + 1; grid[1] <- arr[m, int](1);` is fine.
- **Returning views.** An output can be a view, `-> (r :: peek arr[int])`, as long as everything the function yields views one of its view inputs. At a call, the result views every argument passed to a view input, so `longest(a, b)` keeps both `a` and `b` in place while its result is live, and a call whose view would be of a value that isn't a place is the error a view of a value is. Yielding a view of anything else, like one of the function's own bindings, is `cherimoya requires 'longest' to yield a view of one of its inputs, please yield a view of one, or a copy`.
- **`unscoped` bindings and outputs.** `let buf :: unscoped arr[int] <- ...` owns heap memory that has to be moved before its scope ends, by yielding it, moving it into another binding, or passing it to an `unscoped` input: `cherimoya requires 'buf' to be moved before its scope ends, please yield it or pass it to an unscoped input`. On an output, `-> (r :: unscoped arr[int])`, it says the value outlives the function. Only heap memory can be unscoped, `cherimoya requires an unscoped binding to hold an array or a pointer, please remove 'unscoped'` (or `output`, or `input`), and only in a function: `cherimoya requires 'buf' to be in a function to be unscoped, please remove 'unscoped', since a top-level binding is freed when the program ends`.

## Why

Commit 4.2 gave handles owners, but borrowing was only a stand-in: only an input borrows, so a program can't hold part of an array without copying it, can't let a function write one of its `int`s, and can't make a binding that has to be handed on. Worse, an input that writes an array's elements could be given an array whose elements were declared immutable. This commit brings the modes the spec describes, `peek` and `unscoped` everywhere, and the checks that keep every view pointing at memory that's still there.

- **Only freeing and moving are ruled out.** Writing an `int` through one view and reading it through another can't touch freed memory, so any number of views can read and write the same place. What breaks a view is memory going away under it.
- **Paths tell places apart.** A view holds the address of the place it views, so replacing `grid[1]` frees the old row, which a view of `grid[1][0]` points into, while a view of `grid[1]` itself still points at the element, which holds the new row. A view of `xs[1]` is kept in place by what happens to `xs`, not to `xs[0]`. An index known when the program compiles tells elements apart exactly, and one only known at run time might be any element, so `xs[i]` overlaps every element.
- **Liveness, not scope,** decides how long a view holds things in place, so code doesn't have to be rearranged around views it's done with.
- **No annotation on returned views.** A call's result views every argument it borrows, which is always safe, and keeps signatures plain.
- **Views can't outlive what they view.** A view is made from a place already in scope, can't be rebound, and only an output can hand one on, which has to view an input.
- **A control-flow graph.** These checks need to know where control can go and what's used later, which a walk in source order can't answer for every shape of control, so ownership moves onto a graph, which contracts will be checked on too.

## Implementation

### Sema

- **Types.** `src/sema/type.hpp` adds `ViewSemaType`, with what it `viewed`, `asView`, and `unview`, which looks through one. A signature's input or output that's a view has a `ViewSemaType`, so it's passed by address, while everywhere else a view has the type of what it views. `src/sema/context.hpp` interns them in `viewTypes`. `Symbol` in `src/sema/scope.hpp` gains `isView`.
- **Modes.** `resolveType` accepts `peek` and `unscoped` on an input, a binding, or an output, which `isResolvingInput` and `modeOwner` say, and nothing inside them; `wit` is unsupported until contracts. `modeOf` reads the mode a written type starts with. `outputType` makes a `peek` output a view type, and signatures make every `peek` input, and every input of an owning type that isn't `unscoped`, a view type.
- **Places and paths.** `pathsOf` gives the paths an expression is, a binding (or what a view views) and each index and dereference after it, a `PathStep` with a folded index when there is one, or the paths a view-yielding call could view, from `viewCalls`, or nothing for a value that isn't a place. `placeLevels` gives what of a place can be written, from a binding's `mutability`, or from `viewLevels` for a view-yielding call, or everything for a value.
- **Views.** `checkLet` records a `peek` binding's paths in `facts.views`, checks its mutability against `placeLevels`, and doesn't consume its initializer. `checkFn` marks view inputs `isView`, with their own path, and keeps `peek` ones in `writtenViews`. Assigning to a view, unless it's a borrowed input, records a replacement, and assigning over what owns memory records `facts.assignHarms`. `consume` refuses to move a view, `please move what it views instead`.
- **Calls.** `checkCall` takes `unview`ed parameter and return types, and `passArguments` works out what each argument does: an `unscoped` input consumes it at the call (`facts.callMoves`) and records a moving harm, and a view input checks every writable level of the input against `placeLevels`, records a harm in `facts.callHarms` when it writes something that owns memory, and adds the argument's paths to what a view-yielding call views. `typeAt` gives a type's level `depth` in.
- **Yields.** A yield to a function that returns a view checks that every path it yields roots at one of the function's view inputs, and otherwise consumes the value.
- **Facts.** Instead of tracking ownership as it checks, sema records `Facts` in `src/sema/cfg.hpp`: names read where they move (`moves`), arguments moved at their call (`callMoves`), what each call or assignment could free or move (`callHarms`, `assignHarms`), each view's paths (`views`), matches that cover every value (`covered`), and owners that have to be moved (`mustMove`). `Target` in `src/sema/target.hpp` loses the flows it used to join.
- **The stack.** A view that can replace an owning place whole might free it, so its roots go in `escaped` and stay on the heap, and `unscoped` bindings and views are never stack candidates.

### Control-Flow Graph

`src/sema/cfg.hpp` is new. `checkFn` runs `Ownership(engine, facts, drops, replaces).check(fn, ...)` once a function is checked.

- **Steps and blocks.** `Ownership` lowers the body to `Block`s of `Step`s, each a `Use`, `Move`, `Bind`, `Replace`, `Harm`, `View`, or `Leave`, joined by `Edge`s, each with the site codegen frees at and whether it's a loop's way back. `expr` and `stmt` emit steps in the order codegen evaluates, `callSteps` sorts a call's arguments by position and moves the `unscoped` ones after all of them, and `match`, `repeat`, `construct`, `body`, and `yield` build the edges: each arm's test from the one before, a way out when no arm matched for a match not in `covered`, a loop's header, body, and way back, and every yield to its `Exit`. `leave` frees every scope from a depth in, through `ownersFrom`. `check` makes a function's `unscoped` inputs owners of its outermost scope.
- **Moves** flow forward in `moves`, over the blocks in `order`, a reverse postorder ignoring ways back. Each block starts from what its ways in agree on; a binding moved on any of them is moved there, and each way that still holds it frees it on its edge, through `drops`, or reports it through `unmoved` when it's in `mustMove`. `forward` reports a use after a move through `usedAfterMove`, records `replaces` for a binding that still held a value, and frees what's left at a `Leave`. Each loop's way back is then checked against its header: an owner moved in the body is `cherimoya requires 'xs' to hold a value at the end of every iteration, please assign it again after moving it on line 7`, and one moved before and assigned in the body is freed at the body's end.
- **Liveness** flows backward in `views`, to a fixed point around loops, through `backward`: a use of a view makes it live and its `View` step ends it. Every `Harm` step is checked against the views live after it with `reaches`, which is true when the view runs at least as far as the place along the same steps, where two folded indices that differ tell elements apart, and replacing a place spares a view of that very place while moving it doesn't.
- **Drops** come from both: wherever control leaves a scope, every binding still holding a value is freed. Codegen reads the same answers it read in commit 4.2.

### Codegen

`lower` makes a `ViewSemaType` a `ptr`. `slotType` gives a view's slot the address of what it views, and `addressOf` loads it, so reading and writing a view goes through it, and a `let` of a view stores `placeAddress` of its initializer. `emitArguments` passes a view input `placeAddress` of its argument, which spills a value that isn't a place, a lent fixed array as the address of the handle made for it, and a default as the address of a slot. A function that yields a view (`returnsView`) yields `placeAddress` of its value, `viewCallee` finds a call to one, `readView` loads its result where a value is wanted, and `placeAddress` of such a call is the address `emitView` gives. A view global isn't freed by `fini`.

### Debug Info

`typeOf` in `src/codegen/debug.hpp` describes a view in a signature, and a view binding, as a reference, which gdb follows to print what it views: `n = (int &) @0x7ffd…: 1`.

### Language Server

`render` in `src/lsp/features.hpp` shows `peek` on a view binding's hover.

## Tests

`tests/unit/sema/test_sema.cpp` checks views, their places and paths, every way to break one, loops, returning views, unscoped bindings, and that a handle a view can replace whole stays on the heap. `tests/features/views.toya` runs them, and `tests/debug/views.moya` prints a view input, a borrowed handle, and a view of an element in gdb. 14743 unit assertions, 9 programs, the run of `tests/test/`, 199 feature tests, 9 debug programs, all under valgrind, and the grammar's corpus, 0 failures.

## Current Limitations

- **Where views live.** Views can't be stored in records or packs, so a view lives in a binding or an input, until records exist.
- **Returned views keep too much.** A call that yields a view keeps every argument passed to a view input in place, not just the ones it could really yield, until sema works out which inputs each function's views come from.
- **`&&` and `||`.** A move inside the right side is treated as if it always happens, since the graph doesn't branch there yet.
- **Run-time indices.** A view of `xs[i]` keeps every element of `xs` in place until contracts can tell indices apart.
- **Slicing,** `xs[a, ..., <b]`, comes with ranges.
- **Threads.** Views don't stop two threads racing, and sharing across threads will need its own rules once concurrency exists.
