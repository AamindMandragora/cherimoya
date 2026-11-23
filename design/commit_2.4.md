# Commit 2.4: Fuzzy Booleans

## The Feature

- **`boolf`.** A truth value from 0 to 1 in 256 even steps, held in one byte, where the byte k stands for k / 255, so 0 is false, 255 is true, and every step is the same size.
- **Literals.** A number literal from 0 to 1 becomes a `boolf` where one is expected, and so do `true` and `false`, which are 1 and 0. A literal between two steps rounds to the nearest one, halves going up, so `0.7` is 179 / 255, about 0.702. Anything outside 0 to 1, negative literals included, is `cherimoya requires 1.5 to be from 0 to 1 to be a 'boolf', please use a value in that range`.
- **Literals take their type from the operator.** Bool literals count as literals, so they take the type of the other side of an operator, and logical operators pass the type their context expects into their operands, so `let x :: boolf <- 0.2 && 0.9;` makes both literals `boolf`s.
- **Fuzzy operators.** Each agrees with the bool operator of the same name at 0 and 1:

| Operator | Means | 0.8 with 0.4 |
|---|---|---|
| `a && b` | the minimum | 0.4 |
| `a \|\| b` | the maximum | 0.8 |
| `!a` | 1 - a | 0.2, for `!0.8` |
| `a ^^ b` | `max(min(a, !b), min(!a, b))`, how much exactly one side holds | 0.6 |
| `a * b` | the product | 0.32 |

- **Short-circuiting.** The minimum of 0 and anything is 0, and the maximum of 1 and anything is 1, so `a && b` only evaluates `b` when `a` isn't 0, and `a || b` only when `a` isn't 1.
- **Comparisons.** `boolf`s compare like numbers, and the comparison gives a `bool`.
- **Logical compounds.** `&&<-`, `||<-`, and `^^<-` work on bools and `boolf`s alike, so `isValid &&<- check(x);` folds a check into a flag, and `best ||<- score;` keeps a running maximum. `&&<-` and `||<-` short-circuit too: the place's value is read first, and the right side is only evaluated when the place doesn't already decide the answer, so `ok &&<- check(x);` never calls `check` once `ok` is false.
- **Conversions.** `f64(x)` is the byte over 255. `boolf(t)` rounds a float to the nearest step, halves going up, and traps on a value outside 0 to 1 or a NaN: `trap at line 4: 'boolf(t)' doesn't fit in boolf`. `boolf(b)` on a bool is 0 or 1, and `i8u(x)` and `boolf(n)` read and write the raw byte, an `i8u` or an `i8uw`, which never traps.
- **The cutoff.** A `boolf` never becomes a `bool` by a conversion and is never a condition. `bool(x)` is `cherimoya requires a boolf to become a bool by a cutoff, please compare it, like 'x > 0.5'`, and a `boolf` in a guard is `cherimoya requires a guard to be a bool, please compare the boolf with a cutoff, like 'x > 0.5'`, with the same fix for a loop's condition, an assertion, and every other place a bool is required.
- **Matches.** A `boolf` can be a match's subject, with literal patterns that adapt to it, and like a number it needs a catch-all.

## Why

A `bool` is true or false, but plenty of what programs reason about is true to a degree: a room is warm at 24 degrees to some extent, a match between two names is partly right, a sensor reading is believable with some confidence. Fuzzy logic gives those degrees operators that behave like `&&`, `||`, and `!` and agree with them at the ends, which is how fuzzy controllers in thermostats and cruise control combine their rules. The lexer already knows `boolf` as a basic type, and this commit gives it a meaning.

- **A byte, not a float.** A float would hold more steps, but a byte is a quarter of an `f32`, and every operator on it is exact integer arithmetic. An FP8 float would be just as small, but its steps crowd toward 0, with only 16 between a half and 1, and every operation on it is a call into the runtime, as commit 2.3 shows.
- **The standard fuzzy operators,** minimum and maximum, are the ones that agree with `&&` and `||` at 0 and 1 and keep short-circuiting.
- **Only `*` among arithmetic,** since the product of two values from 0 to 1 is still from 0 to 1, while `+` and `-` can leave it.
- **Logical compounds without overloading.** The other compounds come from every overloadable operator. The logical ones can't be overloaded, since an overload is a call that evaluates both sides, but they still give back their operands' type, which is all a compound needs.
- **The cutoff is written out.** A match or a loop has to go one way or the other, and turning 0.7 into a yes or a no is a choice, so the program writes it: `x > 0.5`, or whatever cutoff it means.
- **Through a double.** A `boolf` becomes a double on its way to any float, which holds every k / 255 closer than any narrower float can, so the value rounds only once, when it's narrowed.

## Implementation

### Lexer

`src/lexer/operators.hpp` marks `&&`, `||`, and `^^` as compoundable in `operatorTable`, so each gets its `X<-`.

### Sema

- **The type.** `src/sema/type.hpp` adds `PrimitiveKind::Boolf` and `isBoolf`, and `src/sema/context.hpp` makes `semaBoolf`, which `resolveType` returns for `boolf`.
- **Literals.** `intLiteral` and `floatLiteral` take an expected `boolf` and accept only values from 0 to 1, and a bool literal is a `boolf` where one is expected. `isLiteral` now counts bool literals, so `checkOperands` types them from the other side.
- **Operators.** `unaryType` takes `!` on a `boolf`. `binaryType` takes a `boolf` for `*`, for comparisons, which always give a `bool`, and for logical operators, which give their operands' type, where both sides having the same type keeps a bool from mixing with a `boolf`. `checkExpr` passes the expected type into logical operators' operands, as it does for arithmetic and bitwise ones.
- **Conversions.** `isByte` recognizes an unsigned 8-bit integer. `checkConversion` accepts a float, a byte, a bool, or a `boolf` into a `boolf`, and a `boolf` into a float or a byte, and reports a `boolf` into a `bool` with the cutoff message. The general message becomes `cherimoya requires a conversion to be between numbers, between characters and integers, or between a boolf and a float or a byte, please convert ... some other way`.
- **Conditions.** `expectBool` gives a `boolf` its own fix, the cutoff, naming the place that wanted the bool.
- **Matches.** `checkMatch` requires a catch-all on a `boolf` subject.

### Codegen

- **The byte.** `lower` makes a `boolf` an `i8`. `!` is the bitwise not bools already use, since flipping every bit of a byte is exactly 255 - x.
- **Literals.** `emitIntLiteral` gives 0 and 1 as the bytes 0 and 255, `emitFloatLiteral` reads the literal as a double and rounds it times 255 with `std::lround`, and a `true` or `false` that is a `boolf` is 255 or 0.
- **Short-circuiting.** `shortCircuit` replaces `emitShortCircuit` and takes the left side already emitted. For a `boolf` it branches past the right side when the left is 0 for `&&` or 255 for `||`, and otherwise takes LLVM's `umin` or `umax` of both. Either way the answer the left side decides is the left side itself, so one phi of the left side's type serves bools and `boolf`s.
- **The product.** `boolfProduct` is a * b / 255 in 16 bits, with 127 added before dividing so it rounds to the nearest step, and 255 being odd means it never lands on a tie.
- **The xor.** `boolfXor` builds `max(min(a, !b), min(!a, b))` from `umin`, `umax`, and `not`, and `exclusiveOr` picks it or a plain `xor` by type.
- **Comparisons.** `compare` compares a `boolf` through `intCompare` as `boolfBits`, an unsigned 8-bit `IntSemaType`, since the steps are in order.
- **Conversions.** `convert` hands any conversion with a `boolf` side to `convertBoolf`. A byte is itself, and a bool becomes 0 or 255 by a `select`. A `boolf` into a float divides the byte by 255 in a double and converts that through `semaDouble`, a NaN-free `f64`. A float into a `boolf` widens to a double, FP8 included, where one unordered comparison on each side, `fcmp ult 0` or `fcmp ugt 1`, catches a NaN along with everything out of range, then scales by 255 and rounds with `llvm.round`, halves going up.
- **Logical compounds.** The `Assign` case of `emitStmt` loads the place for `&&<-` and `||<-` and hands it to `shortCircuit` as the left side, so the right side is skipped when the place decides the answer, and `^^<-` goes through `exclusiveOr`.

### Language Server

`typeHelp` in `src/lsp/features.hpp` describes `boolf`: a fuzzy boolean in 256 steps, held in a byte, where `&&` is the minimum, `||` the maximum, and `*` the product.

## Tests

`tests/unit/sema/test_sema.cpp` adds `boolf` literals, operators, and conversions, and the logical compounds. `tests/unit/lexer/test_lexer.cpp` checks that `&&<-`, `||<-`, and `^^<-` lex as compounds, and `tests/unit/lsp/test_lsp.cpp` checks the hover text. `tests/run/` adds `boolf.moya`, which uses all of it, `compound_logic.moya`, the logical compounds on bools and `boolf`s with right sides that would trap if short-circuiting didn't skip them, and a trap for each of a float outside 0 to 1, `boolf_trap.moya`, and a NaN, `boolf_nan_trap.moya`. 14366 unit assertions and 59 programs, 0 failures.

## Current Limitations

- **Precision.** A `boolf` has 256 steps, so repeated products lose precision quickly, and there's no wider fuzzy type for code that needs more.
- **One family.** The fuzzy operators are the minimum and maximum, and other common families, like the product's own `||`, `a + b - a * b`, are a formula away rather than built in.
- **Range patterns.** Patterns like `[0.5, ..., 1]` wait for ranges.
