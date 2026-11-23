# Commit 5.1: Ranges, For-Each Loops, and Slices

## The Problem

A loop over an array had to be a `repeat (cond)` with a counter declared outside it, bumped by hand at the end of every iteration, and indexed into the array, where forgetting the bump loops forever and an off-by-one reads past the end. The parser has read ranges, `[0, ..., <n]`, and for-each loops, `repeat (x) <- xs`, since commit 1.4, but sema had no type for one and nothing to run the other. Commit 5.1 makes ranges values, runs for-each loops over ranges and arrays, and slices arrays into views of part of them, which 4.3's views made possible.

## Ranges

A range is a value, a sequence it works out from its ends and its stride rather than storing, so it's a fixed size whatever its length, is copied like an `int`, and owns nothing:

| Type | What it is | Written |
|---|---|---|
| `range[T]` | a range with an end, whose length is decided at run time | `[0, ..., <n]`, `[n, n - 2, ..., 0]` |
| `range[..., T]` | a range with no end | `[0, ...]`, `[1, 3, ...]` |

`range[T]` mirrors `arr[T]`: its length isn't part of its type, so every bounded range of `int`s is a `range[int]`, and one with no end is a type of its own, since it can't be counted. `T` is an integer, a character, a float, or a `boolf`, and anything else is `cherimoya expected a range of integers, characters, floats, or boolfs, got 'bool' instead`. A range's elements have one type, worked out the way a binary operator's operands are, so a literal takes the other end's type, `[0, ..., <xs.len()]` is a `range[i64u]`, and two different types are an error, since nothing widens.

A range's elements are what the reference gives:

- `[a, ..., b]` includes both ends, `[a, ..., <b]` stops below `b`, `[>a, ..., b]` starts above `a`, and `[a, ..., >b]` stops above `b`, counting down.
- The stride is the difference of the first two elements, `[0, 2, ..., <n]`, and is one step without a second, which for a character is the next code point and for a `boolf` the next of its 256 steps.
- A range without a second element counts the way its end marker says, `<b` up and `>b` down, and without one, the way its ends do at run time, so `[5, ..., 0]` counts down and `[a, ..., b]` counts whichever way `a` and `b` lie.
- A range with nothing in it, `[0, ..., <0]` or `[5, ..., <0]`, is empty, not an error.
- Its ends are evaluated once, when it's made, so `[0, ..., <xs.len()]` reads the length once.

A float has no next value worth stepping to, so a range of floats needs a stride: `cherimoya requires a range of floats to have a stride, please give it a second element, like '[0.0, 0.5, ..., 2.0]'`. Its `i`th element is computed from the first, `a + i * stride`, rather than by adding the stride over and over, so `[0.0, 0.1, ..., 1.0]` doesn't drift, and a NaN or an infinite end traps. The rest of the reference's rules hold: equal first and second elements are `cherimoya requires a range's first two elements to differ, please give it a stride`, or a trap when they're only known at run time, a marker has to agree with the stride, `cherimoya expected '>' to end a range that counts down, got '<' instead`, and iterating never overflows, since a range knows its last element when it's made instead of finding it by stepping past it.

What a range can do:

| | `range[T]` | `range[..., T]` |
|---|---|---|
| `r.len()`, an `i64u` | yes | no |
| `r[i]`, the `i`th element, checked like an array's index | yes | no |
| `r.has(x)`, whether `x` is one of its elements | yes | yes |
| `=` and `==`, the same elements in the same order | yes | yes |
| `repeat (x) <- r` | yes | yes, until it's left |

`r.has(x)` is exact: `[0, 2, ..., 10].has(3)` is false, and for floats, `x` has to be one of the elements the range computes. Two ranges are equal when they hold the same elements, so `[0, ..., 4] = [0, 1, ..., <5]`, and every empty range equals every other. A length that can't be an `i64u`, only `[0, ..., 2^64 - 1]` in `i64u` and its signed twin, traps when it's taken. Asking an endless range for its length is `cherimoya requires 'r' to have an end to take its length, please give it one, like '[0, ..., <n]'`, and the same goes for indexing it.

## For-Each Loops

`repeat (x) <- xs { ... }` runs its body once for each element of `xs`, a range or an array, fixed, on the heap, or a slice, in order:

```
let total :: mut <- 0;
repeat (x) <- xs { total +<- x; }
repeat (x :: mut) <- ys { x *<- 2; }        # every element of ys is doubled
repeat (i) <- [0, 2, ..., <ys.len()] { ys[i] <- 0; }
```

Over an array, `x` is a view of each element in turn, the way `let x :: peek <- xs[i];` is, so nothing is copied, and `(x :: mut)` is a view that writes, which needs `xs`'s elements to be writable: `cherimoya requires the elements of 'xs' to be mutable to write them through 'x', please declare it as 'arr[mut int]'`. A view of `xs[i]` overlaps every element of `xs`, so 4.3's rules keep `xs` where it is while the loop runs: moving or replacing it, or writing over an element that owns memory, is an error in the body, while writing an `int` element is fine. Over a range, `x` is each element's value, since a range has no elements in memory to view, and `(x :: mut)` is an error, `cherimoya requires 'x' to be a view of an array's element to be 'mut', please remove 'mut', since a range's elements are values`. The binder is a name, with or without `mut`: binding packs, `([i, x]) <- enumerate(xs)`, comes with destructuring in 10.2.

A source that isn't a place, like `repeat (x) <- make(n)`, lives until the loop ends, where it's freed. A loop's label and its binder work as targets the way they do in a `repeat (cond)`, so `-| repeat` leaves it and `-| x` skips to the next element. A loop over an endless range runs until something leaves it, and its counter traps if it passes the largest value of its type, the way `+` does. A for-each loop that yields a value has the type `Option[T]`, which waits for sum types, so until then a yield with a value out of one is `cherimoya requires a for-each loop to yield nothing until sum types arrive, please yield from a block around it`.

## Slices

`xs[a, ..., <b]` is a view of the elements of `xs` from `a` up to `b`, a `peek arr[T]`, so nothing is copied, and writing `s[0]` writes `xs[a]`. A slice is a range in an array's brackets, so every marker works, `xs[a, ..., b]` includes `b`, and `xs[a, ...]` runs to the end, while a stride, `xs[0, 2, ..., <n]`, is `cherimoya requires a slice to be contiguous, please index with a range in a loop instead, like 'repeat (i) <- [0, 2, ..., <n]'`, until strided views arrive. Its ends are unsigned indices, like any index, and they're checked once, when it's made:

```
trap at line 6: the slice 2 to 5 is past the end of 'xs', which holds 3
```

A slice that counts down, `xs[5, ..., 1]`, would be strided, so it's an error when its ends are constants and empty when they're only known at run time, the way the range is. Slicing a slice works, and so does slicing a fixed array, which gives a `peek arr[T]` too. A slice is always a view, so a binding of one is a view without saying `peek`, `let s <- xs[1, ..., <3];`, which 4.3's rules keep from outliving `xs`, and since a slice's path is `xs` with an index the program doesn't know, it keeps every element of `xs` in place while it's live. A slice writes when `xs`'s elements do, and a copy is a conversion, `arr[int](xs[1, ..., <3])`, which works from any array now, not just a fixed one.

## Debugging

With `-g`, gdb shows a range the way it's written, and a slice the way an array on the heap is:

```
$1 = [0, 2, ..., 10]
$2 = [0, ...]
$3 = peek arr[int] of 2 = {20, 30}
```

## Codegen

A range lowers to a struct of its first element, its stride's size as an unsigned integer of the element's width, the index of its last element, an `i64u`, and a byte of flags, whether it counts down and whether it's empty, so its `i`th element is the first plus or minus `i` strides, which can't overflow, since the result is in range. An endless range has no last index. Making a range works out its direction, stride, and last index, and traps on what can't be; `len`, indexing, `has`, and equality read them. A for-each loop over a range counts an index from 0 to the last one, checking for the last before stepping so it never overflows, and over an array, an index from 0 to its length, viewing each element by address. A slice is a handle to its first element and its length, with no owner, so nothing frees it, and it's made after one check that its ends are in order and in bounds. In the control-flow graph, a loop's element is a view taken at the top of every iteration, and its source a use, so 4.3's dataflow checks it with nothing new.

## What Was Built

`src/sema/type.hpp` and `src/sema/context.hpp` gained `RangeSemaType`, `src/sema/sema.hpp` checks ranges and their methods, for-each loops, and slices, and `src/sema/cfg.hpp` lowers for-each loops into the graph. `src/codegen/codegen.hpp` emits ranges, loops, and slices, `src/codegen/debug.hpp` describes ranges, `src/codegen/printers.py` prints them, and `src/lsp/features.hpp` says what `range` is. `tests/unit/sema/test_sema.cpp` checks ranges, their types, strides, markers, and methods, loops over each kind of source, and slices, `tests/features/ranges.toya` runs them, and `tests/debug/ranges.moya` prints them in gdb. `editors/zed/extension.toml` points at 4.4's grammar.

## Current Limitations

Strided slices and slices that count down wait for a strided view type. Walking a string of bytes by its characters, without converting it to runes first, waits for protocols, which give a source its own way to iterate, as do loops over a source that isn't a range or an array. Destructuring binders, `([i, x])`, come with tuples, and range patterns in a match with patterns. A for-each loop can't yield a value until `Option` arrives. A slice keeps every element of what it slices in place until Z3 can tell indices apart, and an endless range has no length or indexing.
