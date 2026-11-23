# Commit 2.3: FP8

## The Feature

- **Two 8-bit encodings.** Both are a sign bit, an exponent, and a mantissa, and they split the other seven bits differently:

| | `f8`, or `f8e4m3` | `f8e5m2` |
|---|---|---|
| layout | `s eeee mmm` | `s eeeee mm` |
| bias | 7 | 15 |
| largest value | 448 | 57344 |
| smallest subnormal | 2⁻⁹ | 2⁻¹⁶ |
| infinity | none | all-ones exponent, zero mantissa |
| NaN | `s1111111` only | all-ones exponent, any other mantissa |
| an overflow becomes | NaN | infinity |

- **Four types.** Each encoding comes NaN-free and NaN-carrying, like every other float. `f8` is E4M3's NaN-free type and prints as `f8`, the way `f64` prints as `float`, and its NaN-carrying type is only ever written `f8e4m3n`.
- **Every float operation.** Arithmetic, comparisons with both equalities and the total order, negation, literals, and conversions to and from every other number work on FP8 exactly as on any other float.
- **Literals and exactness.** `let x :: f8 <- 449.0;` is out of range and `let x :: f8 <- 17;` isn't exact, from the same rules as every other float.
- **Overflow traps in `f8`.** A NaN-free E4M3 has no infinity to overflow into and can't hold the NaN it overflows into either, so it traps: `trap at line 4: 'x * 2' overflowed f8`. A NaN-free E5M2 overflows into infinity, which it holds, and a NaN-carrying E4M3 overflows into NaN, which it holds.

## Why

After commit 2.2 every float exists but the two smallest. FP8 is the 8-bit float machine learning uses for weights and activations, and it differs from every other float in the one way that decides everything else: nothing computes in it. No CPU has FP8 arithmetic, and LLVM has no FP8 type, only `APFloat` semantics for building constants. So FP8 values are stored as bytes and computed in `f32`, through conversions in the runtime.

- **E4M3 trades infinities for range.** Its all-ones exponent is an ordinary one, so it reaches 448 instead of 240, and it keeps only one NaN pattern per sign. E5M2 is laid out exactly like a small IEEE float.
- **No `f8n`,** since it would hide which encoding it is.
- **Computing in `f32` is exact.** Rounding twice, once to `f32` and once to FP8, is only ever wrong when the first rounding has less than twice the second's precision plus two bits, and `f32` has 24 bits to FP8's 4 at most, so the result is what arithmetic in FP8 itself would give.
- **Narrowing takes a double,** not an `f32`, so a conversion from `f64` to FP8 rounds once instead of twice.
- **A NaN after narrowing is an overflow.** Every NaN a NaN-free value could make is checked in `f32` before narrowing, so a NaN byte coming back can only mean the value was past the largest.
- **One decoder and one encoder** serve both encodings, parameterized by the three things that differ between them: the mantissa's width, the bias, and whether the all-ones exponent means infinity.

## Implementation

### Sema

`src/sema/type.hpp` adds `E4M3` and `E5M2` to `FloatFormat`. In `src/sema/context.hpp`, each row of `makeFloats` gains an `alias` its NaN-free type prints as, `f8` for `f8e4m3` and `float` for `f64`, and the alias names only the NaN-free type. The rows give E4M3 a `precision` of 4 and a `max` of 448, and E5M2 3 and 57344, which is all that literals and exactness are checked against, so sema needs nothing else new.

### Codegen

- **Bytes.** `isFp8` says whether a float type is one of the two encodings, and `lower` makes one an `i8`. `semantics` maps them to `APFloat::Float8E4M3FN` and `APFloat::Float8E5M2`.
- **Widening and narrowing.** `fp8Conversion` finds or declares the runtime function for an encoding and direction. `widen` calls it to turn a byte into the `f32` it stands for. `narrow` extends or truncates any float to a double and calls it back to a byte, and for a NaN-free E4M3 traps when the byte's low seven bits are all ones, its NaN.
- **Arithmetic.** `arithmetic` widens both sides, computes in `floatArithmetic`, which still traps on a NaN for a NaN-free type, and narrows the result.
- **Comparisons.** `compare` compares the bytes for `=` and `!=`, since bits are what `=` compares, and widens both sides for everything else, with the total order for a NaN-carrying type.
- **Negation.** `negate` flips the byte's top bit with an `xor`, since a float's sign is its own bit, which also means it can't make a NaN.
- **Conversions.** `convert` widens an FP8 source first, which is exact, so the conversion continues from an `f32` under the usual rules. Into FP8, an integer converts to a double through `intToFloat`, which now takes the LLVM target type, since a double holds every integer FP8 can hold exactly and so rounds only once, and a float goes through `trapIfNaNInto`, split out of `floatToFloat`, then `narrow`.
- **Constants.** `floatConstant` builds a constant in the encoding's `APFloat` semantics, rounded to nearest even, and emits an FP8 one as the byte holding its bits. `emitIntLiteral` and `emitFloatLiteral` both go through it.

### Runtime

`runtime/fp8.c` defines `struct Fp8`, with `mantissaBits`, `bias`, and `hasInfinity`, and the two encodings `e4m3` and `e5m2`.

- **`decode`** splits a byte into its fields, handles the bytes that aren't numbers, and builds the value with `ldexpf`, where a zero exponent field is subnormal: no implicit leading one, and the smallest normal exponent.
- **`encode`** finds the value's exponent with `frexp`, never below the smallest normal one, scales the value so one unit is the last mantissa bit, and rounds that to an integer with `nearbyint`, which rounds to nearest even. Rounding up can carry into the next exponent, a significand without its implicit one is subnormal, and anything at or past the overflow byte, E5M2's infinity or E4M3's NaN, becomes that byte.
- **The four conversions** are `cheri_e4m3_widen`, `cheri_e4m3_narrow`, `cheri_e5m2_widen`, and `cheri_e5m2_narrow`, under plain C names like `cheri_trap`, since every symbol a program defines has a dot in it.

The conversions are their own file so the unit tests can compile them in without the runtime's `main`.

### Build

`CMakeLists.txt` builds `runtime/fp8.c` into `cheri_rt`, and compiles it into `cheri_tests` with `tests/unit/runtime/test_fp8.cpp`, which now links LLVM. `tools/cheri/main.cpp` adds `platform::runtimeLibraries` to the link command, ` -lm` on Unix, where the math functions live in `libm`, and nothing on Windows, whose C runtime has them already.

## Tests

`tests/unit/runtime/test_fp8.cpp` checks both encodings against LLVM's `APFloat`: every byte widened, and narrowing for every value, every midpoint between neighbors and just either side of it, the midpoint past the largest value, signed zeros, infinities, NaN, and two thousand pseudo-random doubles across every exponent. `tests/unit/sema/test_sema.cpp` adds the FP8 types, their ranges, and exactness. `tests/run/` adds `fp8.moya`, arithmetic with ties, subnormals, overflow into infinity, and conversions, `fp8_nan_order.moya`, the total order and both equalities on a NaN-carrying E4M3, and a trap for each of an overflowing `f8`, `fp8_overflow_trap.moya`, and a NaN converted into one, `fp8_nan_convert.moya`. 13950 unit assertions and 52 programs, 0 failures.

## Current Limitations

- **Speed.** Every FP8 operation is a call into the runtime and back, so FP8 is far slower than the floats hardware computes in.
- **`f128` rounds twice.** It narrows to FP8 through a double.
- **No saturation.** There are no saturating FP8 conversions, which some machine learning code wants in place of overflowing.
- **Scope values.** Literals don't yet take their type from a scope's value.
