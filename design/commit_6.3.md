# Commit 6.3: FP8

## The Problem

After commit 6.2, cherimoya has every float but the two smallest. FP8 is the 8-bit float machine learning uses for weights and activations, and it's different from every other float in one way that decides everything else: nothing computes in it. No CPU has FP8 arithmetic, and LLVM has no FP8 type, only `APFloat` semantics for building constants. Commit 6.3 adds both FP8 encodings anyway, by storing them as bytes and computing with them in `f32`, through conversions in the runtime.

## Two Encodings

Both are a sign bit, an exponent, and a mantissa, like any float, and they split the other seven bits differently:

| | `f8`, or `f8e4m3` | `f8e5m2` |
|---|---|---|
| layout | `s eeee mmm` | `s eeeee mm` |
| bias | 7 | 15 |
| largest value | 448 | 57344 |
| smallest subnormal | 2⁻⁹ | 2⁻¹⁶ |
| infinity | none | all-ones exponent, zero mantissa |
| NaN | `s1111111` only | all-ones exponent, any other mantissa |
| an overflow becomes | NaN | infinity |

E5M2 is laid out exactly like a small IEEE float. E4M3 trades IEEE's infinities for range: its all-ones exponent is an ordinary one, so it reaches 448 instead of 240, and it keeps only the one NaN pattern per sign. Each encoding comes NaN-free and NaN-carrying, like every other float, so there are four types. `f8` is E4M3's NaN-free type and prints as `f8`, the same way `f64` prints as `float`, and its NaN-carrying type is only ever written `f8e4m3n`, since `f8n` would hide which encoding it is.

Sema needed nothing new: a float type's precision and largest value are all literals and exactness are checked against, so `let x :: f8 <- 449.0;` is out of range and `let x :: f8 <- 17;` isn't exact, from the same rules as every other float.

## Bytes, Computed in f32

An FP8 value is an `i8` in IR. Arithmetic widens both sides to `f32`, computes there, and narrows the result back, and comparisons widen both sides and compare in `f32`, with the total order for a NaN-carrying type, like any other float. Computing in `f32` rounds exactly as if the arithmetic were done in FP8 itself: rounding twice, once to `f32` and once to FP8, is only ever wrong when the first rounding has less than twice the second's precision plus two bits, and `f32` has 24 bits to FP8's 4 at most.

Three operations work on the byte directly. Shallow equality compares the bytes, since bits are what `=` compares. Negation flips the top bit, since a float's sign is its own bit, which also means it can't make a NaN. And a constant is built in the encoding's `APFloat` semantics, rounded to nearest even, and emitted as the byte holding its bits.

## The Runtime's Conversions

The runtime gains four functions, two per encoding, under plain C names like `cheri_main` and `cheri_trap`:

- `cheri_e4m3_widen` and `cheri_e5m2_widen` take a byte to the `f32` it stands for, which always holds it exactly. They split the byte into its fields, handle the bytes that aren't numbers, and build the value with `ldexpf`, where a zero exponent field is subnormal: no implicit leading one, and the smallest normal exponent.
- `cheri_e4m3_narrow` and `cheri_e5m2_narrow` take a double to the nearest byte, ties to even. They find the value's exponent, never below the smallest normal one, scale the value so one unit is the last mantissa bit, and round that to an integer with `nearbyint`, which rounds to nearest even. Rounding up can carry into the next exponent, a significand without its implicit one is subnormal, and anything past the largest value overflows.

Narrowing takes a double, not an `f32`, so a conversion from `f64` to FP8 rounds once instead of twice. Both encodings share one decoder and one encoder, parameterized by the three things that differ between them: the mantissa's width, the bias, and whether the all-ones exponent means infinity.

The math functions they use live in `libm` on Unix, so the driver links it after the runtime there, and Windows' C runtime has them already. The conversions are their own file, `runtime/fp8.c`, so the unit tests can compile them in without the runtime's `main`.

## Overflow

A NaN-free E4M3 can't overflow into an infinity it doesn't have, and it can't hold the NaN it overflows into either, so narrowing into `f8` traps when the byte that comes back is a NaN:

```
trap at line 3: 'x * 2' overflowed f8
```

Every NaN a NaN-free value could make is checked in `f32` before narrowing, so a NaN after narrowing can only mean an overflow. A NaN-free E5M2 overflows into infinity, which it holds, and a NaN-carrying E4M3 overflows into NaN, which it holds.

## Conversions

Converting from FP8 widens first, which is exact, so the conversion continues from an `f32`, with the same rules as any other float. Converting to FP8 makes a double and narrows it: an integer converts to a double, which holds every integer FP8 can hold exactly, so it rounds only once, and a NaN-carrying float becoming a NaN-free FP8 traps on a NaN like any other conversion into a NaN-free float.

## What Was Built

`src/sema/type.hpp` gained the two FP8 encodings, and `src/sema/context.hpp` makes their four types, with each encoding's alias in the table. `runtime/fp8.c` is the four conversions. `src/codegen/codegen.hpp` gained FP8 as bytes: widening and narrowing, the overflow trap, FP8 in arithmetic, comparisons, negation, and conversions, and FP8 constants. `tools/cheri/main.cpp` links `libm` on Unix, and `CMakeLists.txt` builds `fp8.c` into the runtime and the unit tests.

`tests/unit/runtime/test_fp8.cpp` checks both encodings against LLVM's `APFloat`: every byte widened, and narrowing for every value, every midpoint between neighbors and just either side of it, the midpoint past the largest value, signed zeros, infinities, NaN, and two thousand pseudo-random doubles across every exponent. `tests/unit/sema/test_sema.cpp` gained the FP8 types, their ranges, and exactness, and `tests/run/` gained programs for FP8 arithmetic with ties, subnormals, overflow into infinity, and conversions, the total order and both equalities on a NaN-carrying `f8`, and a trap for each of an overflowing `f8` and a NaN converted into one. 13950 unit assertions and 52 programs, 0 failures.

## Current Limitations

Every FP8 operation is a call into the runtime and back, so FP8 is far slower than the floats hardware computes in. `f128` narrows to FP8 through a double, so it can round twice. There are no saturating FP8 conversions, which some machine learning code wants in place of overflowing. Literals still don't take their type from a scope's value.

## What's Next

The numeric model is complete: every integer and every float, with the checks that keep each one honest. The next commits build on it with the rest of the type system.
