# Commit 9.5: Brackets for Every Type Argument

## The Problem

A type that holds another type wrote it two ways: an array or a pointer took it after itself, `arr[3] int`, `arr int`, `ptr int`, with only a size or an allocator in its brackets, while a generic takes everything in brackets, `List[int]`, `Map[K, V]`. Ranges, coming in commit 10, would have been a third kind of type holding another one. Commit 9.5 writes every type that holds another type the way a generic does, so there's one shape to learn and to read.

## The Syntax

What a handle holds goes in its brackets, after a size or an allocator when it has one:

| Type | Written |
|---|---|
| a fixed array of 3 `int`s | `arr[3, int]` |
| an array on the heap | `arr[int]` |
| a pointer | `ptr[int]` |
| an array from an allocator | `arr[Chunk, int]` |
| nested arrays | `arr[2, arr[3, int]]` |

`mut` sits where it does on any type, so it goes inside the brackets for what's held and in front for the handle itself: `arr[3, mut int]` can write its elements, `mut arr[3, int]` can be replaced, and `mut arr[3, mut int]` both. Modes go in front too, `peek arr[int]`, `unscoped ptr[int]`.

Making one calls the type the same way, so the brackets hold the length, and the parentheses only what fills it:

```
let grid <- arr[n, mut arr[m, int]](arr[m, int](0));
let p <- ptr[mut int](5);
let s :: arr[char] <- "hi";
```

A handle with one argument holds that type, and one with two has a size or an allocator first, so `arr[N]` alone always names an element type, never a size. Anything else in a handle's brackets is an error, like a value where the type it holds goes: `cherimoya expected a type for the handle to hold in 'arr[T]' or 'arr[size, T]', got a value instead`. The old spellings, `arr[3] int` and `arr int`, are plain parse errors.

## Values in Brackets

Brackets read a size or another value the way they read a type, as they have since 9.2, so a conversion or a call works there, which a length only known at run time often needs: `arr[i64u(n), int](0)`, `arr[xs.len() + 1, int](0)`. A type is never called in brackets, so a type spelling followed by `(` is a conversion, and a dotted path followed by `(` is a call or a method call, while a path on its own, `std.mem.Allocator`, is still a type.

## What Was Built

`src/parser/parser.hpp` parses a handle's brackets, and `tree-sitter/grammar.js` does the same, along with its corpus. `src/sema/type.hpp` writes types the new way, and so does every message that spells one, `src/codegen/printers.py` recognizes an array on the heap by its new name, and `src/lsp/features.hpp` says what `arr`, `ptr`, and `peek` are in the new syntax, where `peek`'s note no longer says it isn't supported. Every test, every design doc, the spec, and the README use the new syntax. 14743 unit assertions, 9 programs, the run of `tests/test/`, 199 feature tests, 9 debug programs, all under valgrind, and the grammar's corpus, 0 failures.
