# Commit 4.5: Typed Addresses and Equality on Handles

## The Feature

- **`=` and `==` on handles compare what they hold.** `=` is the weaker check, what's held, bit for bit, and `==` the stronger one, what it means, so whenever two values are `=`, they're `==` too:

  | | `=` | `==` |
  |---|---|---|
  | floats | bit for bit, so `-0.0 = +0.0` is false | by value, so `-0.0 == +0.0` is true |
  | arrays, fixed, on the heap, or viewed | the length, then each element with `=` | the length, then each element with `==` |
  | pointers | the pointees, with `=` | the pointees, with `==` |

  An array on the heap compares with a fixed one across lengths, as in commit 3.2, with either operator, and a `match (=, s)` on a string compares its bytes.
- **Addresses.** `addr x` is the address of a place, a binding, an element, or what a pointer points at, as an `addr[T]`, and whether two handles are the same memory is `addr xs = addr ys`:

  ```
  let n <- 1;
  let a :: addr[int] <- addr n;
  let same <- addr xs = addr ys;      # the same elements, not just equal ones
  ```

  `addr` is a prefix keyword that binds as tightly as `!`, so `addr xs = addr ys` compares two addresses, and `addr xs[0]` is the address of an element.
- **A handle's address is what it holds.** `addr xs` for an array on the heap is an `addr[arr[T]]`, the address of its elements and how many there are, and `addr p` for a pointer is the address of what it points at, through every pointer to a pointer, so `addr p` is an `addr[int]` for a `ptr[ptr[int]]`. An address never names a pointer: `addr[ptr[int]]` is `cherimoya requires an address to name what a pointer points at, please write 'addr[int]'`. A view is transparent, so `addr v` is the address of what `v` views. Taking the address of something that isn't a place is `cherimoya requires an address to be of a place, please bind the value first and take the binding's address`, and `addr[3, int]` is `cherimoya expected only the type the address names in 'addr[T]', got a size or an allocator too`.
- **Addresses only name memory.** They can't be read or written through, have no arithmetic, and don't convert to integers. They compare with `=` and `!=`, which for an array's address compare the address and then the length, and order with `<`, `<=`, `>`, and `>=`, by address and then length. `==` on an address is `=`.
- **An address never outlives its place.** An address is checked the way a view is: while it's live, nothing that overlaps its place's path can be moved, replaced, or written over where what's written owns memory:

  ```
  cherimoya requires 'xs' to stay where it is while 'a' holds its address, please use 'a' for the last time before this
  ```

- **Addresses are never `mut`.** `cherimoya requires 'a' to be immutable, please remove 'mut', since an address names one place for as long as it's used`. An array or a pointer holding addresses, `[addr a, addr b]`, `arr[n, addr[int]](addr a)`, or `ptr[addr[int]](addr a)`, keeps every place it was made from where it is, and since it's never `mut` either, what it names is known once it's made. An address has to be traced to where it was taken: `cherimoya requires an address to be taken of a place, please take it with 'addr' where it's bound`.
- **Addresses in signatures.** An input can be an address, which its caller's place stays put for, and an output can be one when it names one of the function's inputs that are views or addresses, the way a view is returned: `cherimoya requires 'own' to yield an address of one of its inputs, please yield the address of one`. At the call, the result names every place given to such an input.
- **Views and addresses only in functions.** A top-level binding lives as long as the program, while any function can free what it views or names:

  ```
  cherimoya requires 'v' to be in a function to be a view, please view it where it's used, since a top-level binding lives as long as the program
  ```

  and the same for an address, `... to be an address, please take it where it's used, ...`.
- **gdb** shows an address as hex, and an array's with its length:

  ```
  $1 = addr 0x7ffd5c2e1a4c
  $2 = addr 0x5555555592a0 of 3
  ```

## Why

On every type but handles, `=` compares what's held, while on an array on the heap or a pointer it would ask whether two handles are the same memory. Two owning handles are never the same memory, so that question only means anything once views exist, and it would hide under the operator a program reaches for to compare contents. This commit makes `=` mean the same thing everywhere and gives identity a way of its own, typed addresses.

- **An address is checked like a view,** so two addresses that are equal always name the same memory, never memory freed and allocated again. An `addr[T]` owns nothing, so it's copied like an `int`.
- **A handle's address names a place inside it,** its elements or its pointee, since the binding holding a handle is never shared, and replacing the handle frees exactly what the address names.
- **No reading, writing, or casting** keeps an address from being turned back into a pointer, the cast C's `(long*)&x` tricks need. That's `std.unsafe`'s job, with `Raw[T]`, once FFI exists.
- **Ordering** sorts addresses and tells whether two slices could overlap, which is why an array's address orders by address and then length.
- **Never `mut`,** so an address names one place for as long as it's used, and a handle of addresses never gets new ones written in.
- **Top-level views and addresses would escape the checks.** Ownership is checked one function at a time, so a top-level view of a global, `let v :: peek <- g[1][0];`, could never be checked against the function that replaces `g[1]`, and reading `v` after that would read freed memory.

## Implementation

### Lexer

`src/lexer/keywords.hpp` and `src/lexer/token.hpp` add `addr` as `TokenKind::KwAddr`.

### Parser

`src/parser/expr.hpp` adds `AddrExpr`, holding its `operand`. The prefix case for `KwAddr` in `src/parser/parser.hpp` parses its operand at `bindingPower(3)`, the prefix operators' level. `parseType` reads `addr[T]` as a `HandleType`, with only the named type, reporting a size or allocator and dropping it so what's after it still parses.

### Sema

- **Types.** `src/sema/type.hpp` adds `AddressSemaType`, with what it `named`, `asAddress`, and `holdsAddress`, which is true for an address or an array or a pointer holding them. `src/sema/context.hpp` interns them in `addressTypes`. `resolveAddress` resolves `addr[T]` and refuses a pointer.
- **Taking an address.** `checkAddress` requires a place, through `pathsOf`, and gives the address type of what it names, looking through every pointer. `binaryType` lets the comparison operators take addresses, and the message for a wrong operand lists them: `an int, a float, a character, a boolf, or an address`.
- **Tracing what an address names.** `addressesOf` gives the paths a value holding addresses names: for `addr x`, the paths of `x` with a dereference step for every pointer and an element step for a heap array, since a handle's address names a place inside it; a binding or input of addresses, what it was given, and a view of one, what that names; an element, whatever its array names; a pack, what each element names; a handle made by calling its type, through `madeFrom`, what its sources holding addresses name; and a call yielding addresses, from `addressCalls`.
- **Bindings.** `checkLet` records what a binding holding addresses names in `facts.views`, so the graph keeps those places in place while it's live, rejects one with any `mut`, and rejects a view or an address at the top level. `pathsOf` reads a view's paths through `isView`.
- **Inputs, outputs, and calls.** `checkFn` gives an input holding addresses its own path, like a view input. `passArguments` adds what each argument holding addresses names to what a call yielding addresses names, along with every place viewed, in `addressCalls`. A yield of addresses from a function checks every named path roots at one of the function's view or address inputs, then still consumes the value, since a handle holding addresses moves out.

### Control-Flow Graph

`isOwner` in `src/sema/cfg.hpp` asks `isView` instead of whether a binding has paths, and a `let` of a binding with paths and an owning type adds both a `View` and a `Bind` step, so a handle holding addresses keeps what they name in place and still owns and frees its memory. `expr` walks into an `AddrExpr`. The harm error says `holds its address` instead of `views it` for a binding that isn't a view.

### Codegen

`lower` makes an address a `ptr`, or `{ ptr, i64 }` when it names an array on the heap. `emitAddress` takes `placeAddress` of the operand, loads through every pointer, and loads a heap array's handle, which is already its elements' address and length. `compareAddresses` compares addresses as unsigned numbers with `intCompare` and `addressBits`, where an array's address and length compare as one 128-bit number, the address above the length. `compareHandles` compares what both handles hold, with `=` or `==` on each element or pointee as the whole comparison asks, through `compareHeapArrays` or `compare`.

### Debug Info

`typeOf` in `src/codegen/debug.hpp` describes an address as an unsigned number named the way the source names it, `addr[int]`, or a structure of `address` and `len` for an array's. `AddressPrinter` in `src/codegen/printers.py` prints it in hex, with its length when it has one.

### Language Server

`keywordHelp` in `src/lsp/features.hpp` describes `addr`, and `ptr`'s help says `=` and `==` compare values while `addr p = addr q` asks about memory.

### Editors

`editors/zed/languages/cherimoya/highlights.scm` highlights `addr`.

### Tree-sitter

`tree-sitter/grammar.js` adds `addr` to the keywords, an `addressExpression` at level 2 beside the unary operators and `old`, left out of the `argument` family so a generic argument reads `addr[...]` as a type, and an `addressType`, `addr[T]`.

## Tests

`tests/unit/sema/test_sema.cpp` checks addresses, their places, their types, where they can go, and top-level views. `tests/unit/lexer/test_lexer.cpp` and the parser's unit tests read `addr`. `tests/features/handles.toya` compares handles and addresses, and `tests/debug/handles.moya` prints addresses in gdb. The spec and the README describe addresses and the new `=`. 14781 unit assertions, 9 programs, the run of `tests/test/`, 200 feature tests, 9 debug programs, all under valgrind, and the grammar's corpus, 0 failures.

## Current Limitations

- **Hashing.** An address can't be hashed, so it can't be a map's key, until protocols exist.
- **Integers.** An address can't be converted to an integer until `std.unsafe` comes with FFI.
- **Records** can't hold one until peek-like records exist.
- **Tracing.** An address taken in a block or a match and yielded out of it can't be traced to its place yet, so it's taken where it's bound.
