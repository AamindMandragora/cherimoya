# Commit 2.2: Floats

## The Feature

- **Ten float types.** Five encodings, IEEE half (`f16`), bfloat16 (`f16e8m7`, named for its 8 exponent bits and 7 mantissa bits), and IEEE single, double, and quad (`f32`, `f64`, `f128`), each NaN-free under its plain name and NaN-carrying with an `n`, so `f32` never holds a NaN and `f32n` can. `float` is `f64`, and `f64` prints as `float`, the way `i64` prints as `int`.
- **No mixing.** Floats of different types never mix, and NaN-free and NaN-carrying floats of one encoding are different types too, so `x + y` with an `f32` and an `f32n` is the usual "both sides have to have the same type" error. Arithmetic and comparisons take any float, and bitwise operators take none.
- **NaN-free floats trap.** Every arithmetic result on a NaN-free float is checked: `trap at line 2: 'x / x' made a NaN, which float can't hold`. Infinities are fine, so `1.0 / 0.0` is infinity, not a trap.
- **Two equalities.** `=` and `!=` compare bits on every float, so `-0.0 = 0.0` is false and a NaN is `=` to itself when the bits match. `==`, `!==`, `<`, and the rest compare values, so `a <= b` is exactly `a < b || a == b`. On a NaN-carrying float they follow the total order: NaN is below every number, every NaN equals every other, and `-0.0 == 0.0`.
- **Matches.** A match compares its literals deeply by default, so a `0.0` arm catches `-0.0`, and `match (=, x)` compares bits instead, as it has since commit 1.7.
- **Literals.** A float literal takes the type its context wants and is `float` otherwise. It can't be past the type's largest value: `cherimoya requires 65536.0 to fit in 'f16', please use a wider type`. Float literals can be written in any base, so `0x1.8` is 1.5 and `0b10.1` is 2.5.
- **Integer literals as floats.** An integer literal stands for a float when the float holds it exactly, so `x * 2` works on a float `x`. `f16` holds every whole number up to 2048, then only the even ones up to 4096, so `let x :: f16 <- 2049;` is `cherimoya requires 2049 to be exact in 'f16', please write it as a float literal or use a wider type`. Between two literals of different kinds the float side decides, so `1 + 2.5` is a `float`.
- **Conversions between any two numbers.** Calling a number type on any number converts it. Integer to float rounds to nearest even and never fails, so `f16(100000)` is infinity. Float to integer truncates toward zero, and a trapping target traps on a NaN or a value out of range, `trap at line 4: 'i8(x)' doesn't fit in i8`, while a wrapping target saturates, so `i8w(1000.0)` is 127 and a NaN is 0. Float to float rounds to nearest even, and a NaN-carrying float becoming a NaN-free one traps on a NaN: `trap at line 4: 'f64(x / x)' is a NaN, which float can't hold`.

## Why

After commit 2.1 every integer type exists, but there is one float, `float`, which is NaN-free in name only: `0.0 / 0.0` makes a NaN nothing notices, `=` and `==` both compare by value, and floats convert to nothing. This commit builds the floats the way commit 2.1 built the integers: every type in sema and codegen, literals that adapt to them, conversions between any two numbers, and the run-time checks that make NaN-free floats keep their promise. The two FP8 encodings are left for later, since no hardware computes in them.

- **Negation isn't checked,** since it only flips the sign bit and can't make a NaN.
- **No fast-math flags.** LLVM's `nnan` promises an operation never sees a NaN, which looks right for a type that never holds one, but the promise is exactly what the check tests, so the optimizer could fold the check to false and delete it. NaN-free floats get no fast-math flags, and the checks survive `-O3`.
- **Order is about values,** so the orderings use deep equality for their "or equal", and only `=` looks at bits.
- **The total order** makes NaN-carrying floats sort, deduplicate, and match predictably, where IEEE's comparisons are false for every NaN.
- **No rounding literals.** A literal just past the largest value would round down to it, and one further past would round to infinity, and either way it wouldn't mean what it says, so the rule is the plain one.

## Implementation

### Parser

`src/parser/expr.hpp` adds `LiteralDigits` and `literalDigits`, which read a number literal's prefix off as its radix and take out its separators, so `0x1_F` is radix 16 and digits `1F`, and a float keeps its point. Sema and codegen both read values through it.

### Sema

- **Types.** `src/sema/type.hpp` adds `FloatFormat`, the five encodings, and `FloatSemaType`, which holds its `format`, `isNanFree`, `precision` (the significand's bits including the implicit one), `max` (the largest finite value), and the `name` it prints as. `float` stops being a `PrimitiveKind`. `asInt` and `asFloat` give the integer or float type a sema type is, or null for anything else, including a missing type like a subjectless match's.
- **The table.** `src/sema/context.hpp` adds `makeFloats`, which builds all ten from one row per encoding, its name, precision, and largest value, where `f128`'s largest is `long double`'s. `floats` holds them by name with `float` included, `floatType` looks one up, and `semaFloat` is `floats.at("float")`. The integer table is renamed `ints` and gains `int`, so `resolveType` finds both kinds of number through `intType` and `floatType`.
- **Literal values.** `intLiteralValue` reads an integer literal through `literalDigits`, and `floatLiteralValue` reads a float literal into a `long double` in any base, precise enough to check any range.
- **Literal types.** `floatLiteral` takes an expected float type or `float`, and reports a value past the type's `max`. `intLiteral` gives an integer literal an expected float type when `isExactIn` holds: the value fits under `max` and its odd part fits in `precision` bits.
- **Literals as operands.** `literalOf` finds the literal an expression is, looking through a minus, and `isLiteral` and `isFloatLiteral` are built on it. `checkOperands` types the float side first when both sides are literals of different kinds, and a chain of nothing but literals takes a float literal's type if it has one.
- **Conversions.** `checkConversion` accepts any two numbers through `isNumeric`, and otherwise reports `cherimoya requires a conversion to be between numbers, please convert ... some other way`.

### Codegen

- **One table.** `semantics` maps each `FloatFormat` to its `APFloat` semantics, and `lower` gets each LLVM type from it, `half`, `bfloat`, `float`, `double`, and `fp128`, through `llvm::Type::getFloatingPointTy`. Constants are built from the same semantics, so a type and its constants can't disagree.
- **Four entry points.** `arithmetic`, `compare`, `negate`, and `convert` look at the sema type and hand off to `intArithmetic` or `floatArithmetic`, `intCompare` or `floatCompare`, and `intToInt`, `intToFloat`, `floatToInt`, or `floatToFloat`. Every helper takes the operator as its `TokenKind`, so the choice of instruction lives in one place.
- **NaN checks.** `isNaN` is `fcmp uno x, x`, true exactly when `x` is a NaN, since a NaN is the one value not ordered with itself. `floatArithmetic` emits `fadd`, `fsub`, `fmul`, `fdiv`, or `frem`, and on a NaN-free type passes the result to `trapIf`.
- **Comparisons.** `floatCompare` bitcasts both sides to integers for `=` and `!=`. On a NaN-free type the rest are IEEE's ordered comparisons. On a NaN-carrying type, lambdas build the total order from the ordered comparisons plus the NaN cases they leave out: `less` is "a is NaN and b isn't, or a is less", `atMost` is "a is NaN, or b isn't and a is at most b", and `equal` is "both are NaN, or they're equal", with `>` and `>=` swapping the sides.
- **Float to integer.** `floatToInt` uses `fptosi.sat` or `fptoui.sat` for a wrapping target. A trapping target checks the value against a low bound, the minimum for a signed type or -1 for an unsigned one, since `-0.5` truncates to `0`, and a high bound of 2 to the bits, or bits - 1 when signed. Both are powers of two, which a float holds exactly unless they're past its range, like -2 to the 31 in `f16`, which becomes -infinity, so the low comparison is strict when the bound is -1 or became -infinity, and -infinity itself is out of range. A NaN fails both ordered comparisons, so one check catches everything.
- **Float to float.** `floatToFloat` traps on a NaN going into a NaN-free type, then extends or truncates. Half and bfloat16 are both 16 bits with different layouts, so one becomes the other through `f32`, which holds either exactly.
- **Literals.** `emitIntLiteral` builds a literal standing for a float through `APFloat::convertFromAPInt`, which can't round, since sema made sure it's exact. `emitFloatLiteral` builds every float constant in its type's encoding with `convertFromString`, rounding to nearest even. A hexadecimal, octal, or binary literal is first rewritten as a hex float with an exponent, which `APFloat` reads exactly: every digit is a whole number of bits, so the digits read as one integer and each digit after the point moves the exponent down by that many bits, so `0o7.4` is `0x3cp-3`.
- **Matches.** `emitMatch` compares literal patterns with `==` unless the match chose `=`.

## Tests

`tests/unit/sema/test_sema.cpp` adds float types, float literals and exactness, including `f16`'s 2048, 2049, and 4096 and `f32`'s 16777217, and float conversions, and updates the operator tests so `1 + 2.5` is a `float` and `0 <= 1.5 < 2` checks. `tests/run/` adds `floats.moya`, arithmetic in every encoding with literals in other bases, `float_nan_order.moya`, the total order, `float_convert.moya`, conversions between floats and from integers, `float_to_int.moya`, truncation and saturation into integers, and a trap for each of a NaN from arithmetic, `float_nan_trap.moya`, a NaN converted into a NaN-free float, `float_nan_convert.moya`, a float that doesn't fit in an integer, `float_to_int_trap.moya`, and an infinity converted from `f16`, `float_infinity_trap.moya`. 3397 unit assertions and 48 programs, 0 failures.

## Current Limitations

- **No FP8.** The two 8-bit encodings come with FP8.
- **No exponents.** Float literals have no exponent, so very large and very small values have to be written out.
- **No float functions.** Square roots, a NaN test, and the rest wait for the standard library's `std.float`, which arrives with modules.
- **The last sliver of `f128`.** Sema checks a literal's range in `long double`, whose largest value is a hair below `f128`'s, so the very top of `f128`'s range can't be written as a literal.
- **Scope values.** Literals don't yet take their type from a scope's value.
