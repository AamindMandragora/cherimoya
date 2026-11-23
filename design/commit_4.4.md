# Commit 4.4: Brackets for Every Type Argument

## The Feature

- **What a handle holds goes in its brackets,** after a size or an allocator when it has one, the way a generic takes everything in brackets:

  | Type | Written |
  |---|---|
  | a fixed array of 3 `int`s | `arr[3, int]` |
  | an array on the heap | `arr[int]` |
  | a pointer | `ptr[int]` |
  | an array from an allocator | `arr[Chunk, int]` |
  | nested arrays | `arr[2, arr[3, int]]` |

- **`mut` and modes.** `mut` goes inside the brackets for what's held and in front for the handle itself: `arr[3, mut int]` can write its elements, `mut arr[3, int]` can be replaced, and `mut arr[3, mut int]` both. Modes go in front too, `peek arr[int]`, `unscoped ptr[int]`.
- **Making one calls the type the same way,** so the brackets hold the length and the parentheses only what fills it:

  ```
  let grid <- arr[n, mut arr[m, int]](arr[m, int](0));
  let p <- ptr[mut int](5);
  let s :: arr[char] <- "hi";
  ```

- **One argument is what's held.** A handle with one argument holds that type, and one with two has a size or an allocator first, so `arr[N]` alone always names an element type, never a size. A value where the held type goes is `cherimoya expected a type for the handle to hold in 'arr[T]' or 'arr[size, T]', got a value instead`, or `in 'ptr[T]'` for a pointer. `arr[3] int` and `arr int` are plain parse errors: the first is that error, and the second is `cherimoya expected '[' after 'arr' in 'arr[T]' or 'arr[size, T]', got 'int' instead`.
- **Values in brackets** read as they do in commit 4.2: a conversion or a call works there, `arr[i64u(n), int](0)`, `arr[xs.len() + 1, int](0)`, and a path on its own, `std.mem.Allocator`, is still a type.
- **Messages spell types this way,** `arr[3, int]`, `ptr[mut int]`, `peek arr[int]`.

## Why

A type that holds another type could write it after itself, `arr int`, with only a size or an allocator in brackets, while a generic takes everything in brackets, `List[int]`, `Map[K, V]`, and ranges would add a third kind of type holding another one. Writing every such type the way a generic does gives one shape to learn and to read.

- **Size or allocator first,** so a handle reads like a generic with a leading comptime argument, `arr[Chunk, int]` beside `List[T, A]`.
- **`arr[N]` is never a size,** so one argument always means one thing, and a reader never has to know whether `N` is a type or a constant to tell what's held.
- **`mut` inside the brackets** sits right before what it makes writable, which is where it already went for any other type, and in front it applies to the whole, as it does on a binding.

## Implementation

### Parser

`parseType` in `src/parser/parser.hpp` reads `arr` or `ptr`, expects `[`, and parses a first argument with `parseGenericArg`. A `,` after it makes that the size or allocator and parses the held type after it, and otherwise the first argument has to be a type, or it reports the value and holds an `ErrorType`. It then expects `]`. A `mut` on the held type still applies to the elements, `QualPosition::Elements`, and the `HandleType` holds the optional `arg` and the `inner` type, so sema and codegen read a handle the same whichever way it's spelled. The comments in `src/parser/expr.hpp` and `src/parser/type.hpp` give the new spellings.

### Sema

`toString` on `ArraySemaType` and `HandleSemaType` in `src/sema/type.hpp` writes `arr[3, int]`, `arr[int]`, and `ptr[int]`. `writableAt` in `src/sema/sema.hpp` spells the type to declare the new way, `arr[2, arr[3, mut int]]`, and so does every message that spells one: what `checkSizedMake` makes and the fix for an uninitialized array, `please write 'arr[n, mut int](_)'`, the empty array's annotation, `([] :: arr[0, int])`, a non-ASCII string of chars, `please make it 'arr[i8u]' or 'arr[rune]'`, and the length written as an argument, `cherimoya uses brackets for an array's length instead of an argument as follows: 'arr[n, int](fill)'`, whose fix now inserts `n, ` just after the `[`.

### Debug Info

`lookup` in `src/codegen/printers.py` recognizes an array on the heap by a structure tag starting with `arr[`.

### Language Server

`keywordHelp` in `src/lsp/features.hpp` describes `arr`, `ptr`, `mut`, and `unscoped` in the new syntax, and `peek` as what it is: a view of a place, `let v :: peek <- xs[1];`, which reads and writes as what it views and never owns or frees it.

### Tree-sitter

`handleType` in `tree-sitter/grammar.js` is `arr` or `ptr`, `[`, an optional argument and `,`, the held type, and `]`, which removes `_unbracketedType`. The corpus in `tree-sitter/test/corpus/types.txt`, `declarations.txt`, and `statements.txt` uses the new syntax.

### Docs

Every test, every design doc, the spec, and the README use the new syntax.

## Tests

`tests/unit/parser/test_parser.cpp` checks every handle shape, `mut` and modes on each, sizes, allocators, conversions and calls in brackets, and making handles, and `tests/unit/parser/test_printer.cpp` prints handles back in the new syntax. Every unit, feature, and debug test is written in the new syntax. 14743 unit assertions, 9 programs, the run of `tests/test/`, 199 feature tests, 9 debug programs, all under valgrind, and the grammar's corpus, 0 failures.

## Current Limitations

- **Allocators** parse, `arr[Chunk, int]`, but sema reports `cherimoya doesn't support allocators yet` until allocators exist.
