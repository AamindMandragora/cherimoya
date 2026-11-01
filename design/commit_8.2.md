# Commit 8.2: Fuzzy Booleans

## The Problem

After commit 8.1, the lexer knows one basic type that sema still rejects: `boolf`, the fuzzy boolean. A `bool` is true or false, but plenty of what programs reason about is true to a degree: a room is warm at 24 degrees to some extent, a match between two names is partly right, and a sensor reading is believable with some confidence. Fuzzy logic gives those degrees operators that behave like `&&`, `||`, and `!` and agree with them at the ends, which is how fuzzy controllers in thermostats and cruise control combine their rules. Commit 8.2 gives cherimoya that type: what a `boolf` holds, its literals, its operators and comparisons, its conversions, and the rule that keeps it out of conditions.

## A Truth Value in a Byte

A `boolf` is a truth value from 0 to 1 in 256 even steps, held in one byte, where the byte k stands for k / 255, so 0 is false, 255 is true, and every step is the same size. A float would hold more steps, but a byte is a quarter of an `f32`, and every operator on it is exact integer arithmetic. An FP8 float would be just as small, but its steps crowd toward 0, with only 16 between a half and 1, and every operation on it is a call into the runtime.

## Literals

A number literal from 0 to 1 becomes a `boolf` where one is expected, and so do `true` and `false`, which are 1 and 0. A literal between two steps rounds to the nearest one, halves going up, so `0.7` is 179 / 255, about 0.702. Anything outside 0 to 1 is `cherimoya requires 1.5 to be from 0 to 1 to be a 'boolf', please use a value in that range`, and that includes negative literals. Bool literals count as literals now, so they take the type of the other side of an operator, and logical operators pass the type their context expects into their operands, the way arithmetic does, so `let x :: boolf <- 0.2 && 0.9;` makes both literals `boolf`s.

## Operators

The logical operators are the standard fuzzy ones, so each agrees with the bool operator of the same name at 0 and 1:

| Operator | Means | 0.8 with 0.4 |
|---|---|---|
| `a && b` | the minimum | 0.4 |
| `a \|\| b` | the maximum | 0.8 |
| `!a` | 1 - a | 0.2, for `!0.8` |
| `a ^^ b` | `max(min(a, !b), min(!a, b))`, how much exactly one side holds | 0.6 |
| `a * b` | the product | 0.32 |

`&&` and `||` short-circuit the way they do on bools. The minimum of 0 and anything is 0, and the maximum of 1 and anything is 1, so `a && b` only evaluates `b` when `a` isn't 0, and `a || b` only when `a` isn't 1. `*` is the one arithmetic operator a `boolf` takes, since the product of two values from 0 to 1 is still from 0 to 1, while `+` and `-` can leave it. Comparisons work on `boolf`s like on numbers, and they give a `bool`.

The logical operators get compound forms, `&&<-`, `||<-`, and `^^<-`, for bools and `boolf`s alike, so `isValid &&<- check(x);` folds a check into a flag, and `best ||<- score;` keeps a running maximum. The other compounds come from every overloadable operator, and the logical ones can't be overloaded, since an overload is a call that evaluates both sides, but they still give back their operands' type, which is all a compound needs. `&&<-` and `||<-` short-circuit too: the place's value is read first, and the value on the right is only evaluated when the place doesn't already decide the answer, so `ok &&<- check(x);` never calls `check` once `ok` is false.

## Conversions, and the Cutoff

A `boolf` converts to and from any float, and to and from its raw byte, an `i8u` or an `i8uw`, and a `bool` converts into one:

- `f64(x)` is the byte over 255, computed in a double, which rounds only once on its way to a narrower float.
- `boolf(t)` rounds a float to the nearest step, halves going up, and traps on a value outside 0 to 1 or a NaN.
- `boolf(b)` is 0 or 1, and `i8u(x)` and `boolf(n)` read and write the byte as it is, which never traps.

A `boolf` never becomes a `bool` by a conversion, and it's never a condition. A match or a loop has to go one way or the other, and turning 0.7 into a yes or a no is a choice, so the program writes it: `x > 0.5`, or whatever cutoff it means. `bool(x)` and a `boolf` in a guard or a loop's condition both say so: `cherimoya requires a guard to be a bool, please compare the boolf with a cutoff, like 'x > 0.5'`. A `boolf` can still be a match's subject, with literal patterns that adapt to it, and like a number it needs a catch-all.

## Codegen

In LLVM a `boolf` is an `i8`. `!` is the bitwise not that bools already use, since flipping every bit of a byte is exactly 255 - x. `&&` and `||` are LLVM's unsigned minimum and maximum, behind the same short-circuit branch as bools, which tests for 0 or 255 instead of false or true. Either way the answer the left side decides is the left side itself, so one phi serves both types, and the left side comes in already evaluated, so `&&<-` and `||<-` hand it the place's value. The product is a * b / 255 in 16 bits, with 127 added before dividing so it rounds to the nearest step, and 255 being odd means it never lands on a tie. Comparisons are unsigned comparisons on the byte, since the steps are in order. A float converts into a `boolf` through a double, where one unordered comparison on each side catches a NaN along with everything out of range:

```
trap at line 4: 'boolf(t)' doesn't fit in boolf
```

## What Was Built

`src/sema/type.hpp` gained the `boolf` primitive and a check for it, and `src/sema/context.hpp` makes it. `src/sema/sema.hpp` gained `boolf` literals and the range they need, bool literals that adapt, the fuzzy operators and comparisons, the conversions and the cutoff rule, matches on a `boolf`, and its own message for a `boolf` in a condition. `src/lexer/operators.hpp` gives the logical operators their compounds. `src/codegen/codegen.hpp` gained the byte's literals, the product, the xor, the fuzzy short-circuit, the logical compounds, comparisons, and conversions. `src/lsp/features.hpp` says what a `boolf` is.

`tests/unit/sema/test_sema.cpp` gained `boolf` literals, operators, and conversions, and the logical compounds, `tests/unit/lexer/test_lexer.cpp` checks that those compounds lex, and `tests/run/` gained a program that uses all of them, one for the logical compounds on bools and `boolf`s, both with right sides that would trap if short-circuiting didn't skip them, and a trap for each of a float outside 0 to 1 and a NaN. 14366 unit assertions and 59 programs, 0 failures.

## Current Limitations

A `boolf` has 256 steps, so repeated products lose precision quickly, and there's no wider fuzzy type for code that needs more. The fuzzy operators are the minimum and maximum, and other common families, like the product's own `||`, `a + b - a * b`, are only a formula away rather than built in. Range patterns over a `boolf`, like `[0.5, ..., 1]`, come with ranges in commit 10.

## What's Next

The basic types are complete. Next come the addenda 7.3, 7.4, and 7.5, which give the language server doc comments, quick fixes, and inlay hints, then 5.2 for debug info and 8.3 for `cheri test`, and then commit 9.1: arrays and pointers.
