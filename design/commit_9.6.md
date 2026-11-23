# Commit 9.6: Addresses and Equality on Handles

## The Problem

`=` on an array on the heap or a pointer compared addresses, so `xs = ys` asked whether two handles were the same memory, while on every other type `=` compares what's held, bit for bit. Two owning handles are never the same memory, so the question only meant anything once views arrived in 9.4, and it hid under the operator a program reaches for to compare contents. Commit 9.6 makes `=` mean the same thing everywhere and gives identity a way of its own: typed addresses. Checking them the way views are checked also turned up a hole in 9.4, a view made at the top level, which 9.6 closes.

## Equality

`=` is the weaker check, what's held, bit for bit, and `==` the stronger one, what it means, so whenever two values are `=`, they're `==` too. On handles, both now compare what they hold:

| | `=` | `==` |
|---|---|---|
| floats | bit for bit, so `-0.0 = +0.0` is false | by value, so `-0.0 == +0.0` is true |
| arrays, fixed, on the heap, or viewed | the length, then each element with `=` | the length, then each element with `==` |
| pointers | the pointees, with `=` | the pointees, with `==` |

An array on the heap compares with a fixed one across lengths the way 9.3's `==` did, now with either operator, and a `match (=, s)` on a string compares its bytes instead of its address. Whether two handles are the same memory is `addr xs = addr ys`.

## Addresses

`addr x` is the address of a place, a binding, an element, or what a pointer points at, as an `addr[T]`:

```
let n <- 1;
let a :: addr[int] <- addr n;
let same <- addr xs = addr ys;      # the same elements, not just equal ones
```

`addr` is a prefix keyword that binds as tightly as `!`, so `addr xs = addr ys` compares two addresses, and `addr xs[0]` is the address of an element. A handle's address is what it holds, since the binding holding it is never shared: `addr xs` for an array on the heap is an `addr[arr[T]]`, the address of its elements and how many there are, and `addr p` for a pointer is the address of what it points at, through every pointer to a pointer, so `addr p` is an `addr[int]` for a `ptr[ptr[int]]`, and an address never names a pointer: `addr[ptr[int]]` is `cherimoya requires an address to name what a pointer points at, please write 'addr[int]'`. A view is transparent, so `addr v` is the address of what `v` views. Taking the address of something that isn't a place is `cherimoya requires an address to be of a place, please bind the value first and take the binding's address`.

An address only names memory: it can't be read or written through, has no arithmetic, and doesn't convert to an integer, so nothing can turn one back into a pointer, which is the cast C's `(long*)&x` tricks need. That's `std.unsafe`'s job, with `Raw[T]`, once FFI arrives. Addresses compare with `=` and `!=`, which for an array's address compare the address and then the length, and order with `<`, `<=`, `>`, and `>=`, by address and then length, which sorts them and tells whether two slices could overlap. `==` on an address is `=`, since an address means nothing past its bits.

## What Keeps an Address True

An `addr[T]` owns nothing, so it's copied like an `int`, but it can't outlive the place it names, so two addresses that are equal always name the same memory, never memory freed and allocated again. 9.4's rules check it the way they check a view: an address remembers its place's path, where a handle's address names a place inside it, since replacing the handle frees what it held, and while it's live, nothing that overlaps that path can be moved, replaced, or written over where what's written owns memory:

```
cherimoya requires 'xs' to stay where it is while 'a' holds its address, please use 'a' for the last time before this
```

An address names one place for as long as it's used, so a binding of one is never `mut`: `cherimoya requires 'a' to be immutable, please remove 'mut', since an address names one place for as long as it's used`. An array or a pointer holding addresses, `[addr a, addr b]`, `arr[n, addr[int]](addr a)`, or `ptr[addr[int]](addr a)`, keeps every place it was made from where it is, and since it's never `mut` either, nothing new is ever written into it, so what it names is known once it's made. An input can be an address, which its caller's place stays put for, and an output can be one too, when it names one of the function's inputs that are views or addresses, the way a view is returned: `cherimoya requires 'own' to yield an address of one of its inputs, please yield the address of one`. At the call, the result names every place given to such an input.

## Top-Level Views and Addresses

Ownership is checked one function at a time, so a top-level view of a global, `let v :: peek <- g[1][0];`, was never checked against the function that replaces `g[1]`, and reading `v` after that read freed memory. A top-level binding lives as long as the program, while any function can free what it views or names, so a view or an address is only made in a function:

```
cherimoya requires 'v' to be in a function to be a view, please view it where it's used, since a top-level binding lives as long as the program
```

## Debugging

With `-g`, gdb shows an address as hex, and an array's with its length:

```
$1 = addr 0x7ffd5c2e1a4c
$2 = addr 0x5555555592a0 of 3
```

## Codegen

`=` on handles goes through `compareHandles` the way `==` did, with each element compared by `=`. `addr` lowers to the place's address, loading through every pointer, and loading the handle of an array on the heap, which is already its elements' address and length, `{ptr, i64}`, while every other address is a plain `ptr`. `compareAddresses` compares two addresses as unsigned numbers, where an array's address and length compare as one 128-bit number, the address above the length.

## What Was Built

`src/lexer/keywords.hpp` makes `addr` a keyword, and `src/parser/parser.hpp` and `tree-sitter/grammar.js` parse it, along with the grammar's corpus, and `editors/zed/languages/cherimoya/highlights.scm` highlights it. `src/sema/type.hpp` and `src/sema/context.hpp` gained `AddressSemaType`, `src/sema/sema.hpp` checks addresses, traces the places they name, and keeps views and addresses out of the top level, and `src/sema/cfg.hpp` keeps an address from outliving its place the way it does a view, while a handle holding addresses still owns and frees its memory. `src/codegen/codegen.hpp` emits addresses, compares them, and compares handles by what they hold, `src/codegen/debug.hpp` and `src/codegen/printers.py` show addresses, and `src/lsp/features.hpp` says what `addr` is. `tests/unit/sema/test_sema.cpp` checks addresses, their places, their types, where they can go, and top-level views, the parser's and lexer's unit tests read `addr`, `tests/features/handles.toya` compares handles and addresses, and `tests/debug/handles.moya` prints them. The spec and the README describe addresses and the new `=`. 14781 unit assertions, 9 programs, the run of `tests/test/`, 200 feature tests, 9 debug programs, all under valgrind, and the grammar's corpus, 0 failures.

## Current Limitations

An address can't be hashed, so it can't be a map's key, until protocols arrive, or converted to an integer until `std.unsafe` comes with FFI. A record can't hold one until peek-like records arrive. An address taken in a block or a match and yielded out of it can't be traced to its place yet, so it's taken where it's bound.
