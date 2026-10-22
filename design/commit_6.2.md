# Commit 6.2: Floats

## The Problem

After commit 6.1, cherimoya has every integer type and one float, `float`, which is NaN-free in name only: `0.0 / 0.0` makes a NaN that nothing notices, `=` and `==` both compare by value, and floats can't be converted to anything. Commit 6.2 builds the floats the same way 6.1 built the integers: every type in sema and codegen, literals that adapt to them, conversions between any two numbers, and the run-time checks that make NaN-free floats keep their promise. The two FP8 encodings are the odd ones out, since no hardware computes in them, and they arrive on their own in 6.3.

## Ten Types

A float type is two facts: its encoding and whether it's NaN-free. There are five encodings: IEEE half (`f16`), bfloat16 (`f16e8m7`, named for its 8 exponent bits and 7 mantissa bits), and IEEE single, double, and quad (`f32`, `f64`, and `f128`). The plain name is NaN-free, and an `n` makes it NaN-carrying, so `f32` never holds a NaN and `f32n` can. `float` is `f64`, and `f64` prints as `float`, the same way `i64` prints as `int`.

Like integers, floats of different types never mix, and NaN-free and NaN-carrying floats of one encoding are different types too, so `x + y` with an `f32` and an `f32n` is the usual "both sides have to have the same type" error. Arithmetic and comparisons take any float, and bitwise operators take none.

## NaN-Free Floats Trap

Every arithmetic result on a NaN-free float is checked, and a NaN traps through the same helper as every other run-time check:

```
trap at line 2: 'x / x' made a NaN, which float can't hold
```

The check is the one test that only a NaN passes: a NaN is the one value that isn't ordered with itself, so `fcmp uno x, x` is true exactly when `x` is a NaN. Infinities are fine, since they're numbers that compare and order like any other, so `1.0 / 0.0` is infinity, not a trap. Negation only flips the sign bit and can't make a NaN, so it isn't checked.

LLVM has a fast-math flag, `nnan`, that promises an operation never sees a NaN, and it's tempting on a type that never holds one. But the promise is exactly what the check tests, so the optimizer would be entitled to fold the check to false and delete it. NaN-free floats get no fast-math flags, and the checks survive `-O3`.

## Equality and the Total Order

The two equalities split for floats. Shallow equality, `=` and `!=`, compares bits, for every float: `-0.0 = 0.0` is false, since the sign bit differs, and a NaN is `=` to itself when the bits match. Deep equality and the orderings, `==`, `!==`, `<`, and the rest, compare values, since order is only ever about values, so the "or equal" in `<=` and `>=` is deep equality: `a <= b` is exactly `a < b || a == b`.

- **On a NaN-free float**, they're IEEE's comparisons, which are already right, since a NaN never reaches them.
- **On a NaN-carrying float**, they're the total order: NaN is below every number, every NaN is equal to every other, and `-0.0 == 0.0`. That makes NaN-carrying floats sort, deduplicate, and match predictably. Codegen builds each comparison from IEEE's ordered ones plus the NaN cases, which those leave out: `a < b` is "a is NaN and b isn't, or a is less", `a <= b` is "a is NaN, or b isn't and a is at most b", and `a == b` is "both are NaN, or they're equal".

A match compares its literals deeply by default, so a `0.0` arm catches `-0.0`, and `match (=, x)` compares bits instead, as it has since commit 5.

## Literals

A float literal takes the type its context wants, the same way integer literals do, and is `float` when the context doesn't want anything in particular. Its value can't be past the type's largest value: `cherimoya requires 65536.0 to fit in 'f16', please use a wider type`. A value just past it would round down to the largest value, and one further past would round to infinity, and either way the literal wouldn't mean what it says, so the rule is the plain one. Sema reads the value as a `long double`, which is precise enough to check any range.

An integer literal can stand for a float when the float holds it exactly, so `x * 2` works on a float `x`. Exactly means its odd part fits in the significand and the whole number fits in the range: `f16` holds every whole number up to 2048, then only the even ones up to 4096, so `let x :: f16 <- 2049;` is `cherimoya requires 2049 to be exact in 'f16', please write it as a float literal or use a wider type`. When the two sides of an operator are literals of different kinds, the float side decides, so `1 + 2.5` is a `float`, and a chain of nothing but literals takes a float literal's type if it has one.

Float literals can be written in any base, like integers: `0x1.8` is 1.5 and `0b10.1` is 2.5. Codegen builds every float constant in its type's own encoding through LLVM's `APFloat`, rounding to nearest even. A hexadecimal, octal, or binary literal is first rewritten as a hex float with an exponent, which `APFloat` reads exactly: every digit is a whole number of bits, so the digits read as one integer, and each digit after the point moves the exponent down by that many bits, so `0o7.4` is `0x3cp-3`.

## Conversions

Calling a number type on any number converts it, so 6.1's rule that conversions are between integers is now that they're between numbers.

- **Integer to float** rounds to nearest even, and a value past the float's range rounds to infinity: `f16(100000)` is infinity. It can't fail.
- **Float to integer** truncates toward zero. A trapping target traps on a NaN or a value outside its range, which is at least the minimum and below 2 to the bits - 1 for a signed type, and above -1 and below 2 to the bits for an unsigned one, since `-0.5` truncates to `0`. Both bounds are powers of two, which a float holds exactly unless it's past the float's range, like -2 to the 31 in `f16`, which becomes -infinity, so the comparison against the minimum is strict whenever the minimum isn't exact, and -infinity itself is out of range. A NaN fails both of the ordered comparisons, so one check catches everything. A wrapping target saturates instead, through LLVM's `fptosi.sat` and `fptoui.sat`: a value past either end becomes that end, and a NaN becomes 0, so `i8w(1000.0)` is 127.
- **Float to float** extends or truncates, rounding to nearest even. Half and bfloat16 are both 16 bits with different layouts, so one becomes the other through `f32`, which holds either exactly. A NaN-carrying float becoming a NaN-free one is the one float conversion that can fail, so it traps on a NaN: `'f64(x / x)' is a NaN, which float can't hold`.

## Lowering

Each encoding is an LLVM type: `half`, `bfloat`, `float`, `double`, and `fp128`. Codegen keeps one table, from each encoding to its `APFloat` semantics, and gets both the LLVM type and every constant from it, so a type and its constants can't disagree.

Codegen's numbers go through four entry points, `arithmetic`, `compare`, `negate`, and `convert`, each of which looks at the sema type and hands off to the integer or the float version: `intArithmetic` and `floatArithmetic`, `intCompare` and `floatCompare`, and `intToInt`, `intToFloat`, `floatToInt`, and `floatToFloat`. Every helper takes the operator as its token, so the choice of instruction lives in one place. Sema and codegen read a literal's digits through one shared function, which takes off the prefix and the separators, and find the integer or float type a sema type is through `asInt` and `asFloat`, which are null for anything else.

## What Was Built

`src/sema/type.hpp` gained `FloatSemaType`, the encodings, and `asInt` and `asFloat`, and `float` stopped being a primitive. `src/sema/context.hpp` makes all ten float types and finds both kinds of number by name, `int` and `float` included. `src/sema/sema.hpp` gained float literals and their range, integer literals standing for floats, the float side deciding between two literals, and conversions between any two numbers. `src/parser/expr.hpp` gained the shared reading of a literal's digits. `src/codegen/codegen.hpp` gained every float type, NaN traps, both equalities and the total order, float literals in every base, and every conversion, with its numbers reorganized around the four entry points.

`tests/unit/sema/test_sema.cpp` gained float types, float literals and exactness, and float conversions, and `tests/run/` gained programs for arithmetic in every encoding with literals in other bases, the total order, conversions between floats and from integers, truncation and saturation into integers, and a trap for each of a NaN from arithmetic, a NaN converted into a NaN-free float, a float that doesn't fit in an integer, and an infinity converted from `f16`. 3397 unit assertions and 48 programs, 0 failures.

## Current Limitations

There's no FP8 yet. Float literals have no exponent, so very large and very small values have to be written out. There are no float functions, like square roots or a NaN test, until the standard library's `std.float` arrives with modules. Sema checks a literal's range in `long double`, whose largest value is a hair below `f128`'s, so the last sliver of `f128`'s range can't be written as a literal. Literals still don't take their type from a scope's value.

## What's Next

Commit 6.3 does FP8: `f8`, the E4M3 encoding with 4 exponent bits, 3 mantissa bits, and no infinity, and `f8e5m2`, with 5 and 2, each NaN-free and NaN-carrying. No hardware computes in them, so they're stored as bytes and computed in `f32`, through conversions in the runtime.
