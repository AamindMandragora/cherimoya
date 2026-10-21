# Commit 6.1: Integers

## The Problem

After commit 5, cherimoya has one integer type. `int` is 64 bits, its arithmetic wraps silently on overflow, and dividing by zero is undefined. The language has twenty: every width from 8 to 128 bits, signed or unsigned, and trapping or wrapping. Commit 6 builds the full numeric model in three parts, and this first part does the integers: every type in sema and codegen, literals that take the type their context wants, conversions, and the run-time checks that make trapping integers trap. Commit 6.2 does the same for floats, and 6.3 adds FP8.

## Twenty Types

An integer type is three facts: its width, whether it's signed, and whether it wraps. The name spells them out: `i32` is 32 bits, signed, and trapping, `i32u` is unsigned, `i32w` wraps, and `i32uw` is both. The type context makes all twenty up front and interns them like every other sema type, so two integers have the same type exactly when they're the same pointer. `int` is `i64`, and `i64` prints as `int`, since that's how it's almost always written.

Integers of different types never mix. `x + y` with an `i8` and an `i16` is the same "both sides have to have the same type" error as an `int` and a `float`, and the fix is a conversion. Bitwise operators take any integer, comparisons and arithmetic take any number, and `-x` takes a signed integer or a wrapping one. A trapping unsigned value overflows when it's negated for every value but zero, so negating one is a compile error that suggests the signed or wrapping type instead.

## Literals Take the Type Around Them

A literal has no type of its own. It takes the type its context wants, and it's `int` when the context doesn't want anything in particular. Checking an expression can be told what type is expected, and only literals use it, so the type flows from wherever it's known:

- a binding's annotation: `let x :: i8 <- 5;`
- an assignment's place: `x <- 5`, and a compound one: `x +<- 1`
- a call's parameter: `f(5)`
- a function's output: `5 -| f`
- the other side of an operator: `x + 1` and `1 + x`, where the literal side takes the typed side's type whichever side it's on, and a chain takes its first typed operand's
- a match's subject: `0 -> { }`

The value has to fit. It's known while compiling, so a literal that doesn't fit is a compile error instead of a run-time trap: `cherimoya requires 300 to fit in 'i8', please use a wider type or convert it explicitly`. That includes wrapping types, since a literal is written on purpose. A negated literal is checked as a whole, because `-128` fits in an `i8` and `128` doesn't. Literals are read with 128 bits of headroom, so every value of every type can be written, up to `i128u`'s maximum.

## Conversions

Calling an integer type on an integer converts it: `i8(x)`, `i128u(n)`, or `x |> i8`. Converting into a trapping type checks that the value survived and traps if it didn't, and converting into a wrapping type keeps the low bits, so `i8w(300)` is `44`. That's the explicit way to ask for modular arithmetic. Conversions between integers and anything else wait for 6.2.

## Signedness Lives in the Instructions

LLVM integers have no sign: an `i8` is eight bits, and whether `0xFF` is 255 or -1 depends on the instruction that reads it. So codegen chooses instructions by the type's signedness: `sdiv` or `udiv`, `srem` or `urem`, `ashr` or `lshr`, signed or unsigned comparisons, and sign or zero extension when converting. Literals are as wide as their type, through LLVM's arbitrary-width integers.

## Trapping and Wrapping

Wrapping arithmetic is two's complement, which is what the hardware does already, so a wrapping type's `+`, `-`, and `*` are plain instructions. A trapping type's go through LLVM's overflow intrinsics, which give the wrapped result and a bit that says whether it overflowed, and the program traps when the bit is set. Negating a trapping signed integer is `0 - x` through the checked subtraction, since only the minimum overflows. A negated literal is folded instead, so `-128` as an `i8` never negates a `128` that would overflow on the way.

Every run-time check goes through one helper: compute a bool that says something's wrong, and branch to a call to the runtime's trap with a message and the line, or carry on. `assert` goes through it too. A trap names what went wrong in the program's own words:

```
trap at line 3: 'x * 2' overflowed i8
```

## Division

Division by zero traps for every integer, wrapping ones included, since there's no value to wrap to. The one other case where division overflows is a signed type's minimum divided by -1, whose true answer is one more than the maximum. LLVM leaves that case undefined, so codegen has to handle it: a trapping type traps, and a wrapping one gives the minimum again for `/` and zero for `%`, which is exactly what dividing by 1 gives, so the divisor is swapped for 1 in that one case.

## Shifts

Shifting by the width or more is undefined in LLVM too. A trapping type traps on an amount outside 0 to the width minus one, where comparing the amount as unsigned also catches a negative amount, which reads as enormous. A wrapping type takes the amount modulo the width, and since every width is a power of two, that's the amount and-ed with the width minus one, so `1 << 33` as an `i32w` is `1 << 1`. A right shift copies the sign bit in from the left for a signed type and shifts in zeros for an unsigned one.

## Converting Safely

A conversion extends or truncates, then checks by converting back: if the round trip doesn't give the original value, the conversion lost bits. That catches narrowing, but not a change of sign, since -1 as an `i8` and 255 as an `i8u` are the same bits, so a conversion that changes signedness also checks that the value is at least zero on the signed side.

## Windows

The driver keeps everything that differs between platforms in one place: executables are `.exe` and objects `.obj` on Windows, there's no `cc` there so `clang` is the default linker, `std::system` hands back the exit code directly instead of a wait status that has to be decoded, and arguments are quoted the way `cmd` reads them, in double quotes, with one more pair around the whole command, since `cmd` strips the outermost pair. Code generation already worked for any target, so `--target x86_64-pc-windows-msvc -c` writes a Windows object file from Linux.

## What Was Built

`src/sema/type.hpp` gained `IntSemaType`, and `int` stopped being a primitive, and `src/sema/context.hpp` makes all twenty integer types and finds them by name. `src/sema/sema.hpp` gained expected types through checking, literal values and the check that they fit, negated literals, the rule for negation, and conversions, and the yield target is now looked up again after its value is checked, since a value that holds scopes can move the target stack. `src/codegen/codegen.hpp` gained the trap helper, checked arithmetic, division, shifts, conversions, unsigned comparisons, and literals of every width. `tools/cheri/main.cpp` keeps its platform differences in one place.

`tests/unit/sema/test_sema.cpp` gained integer types, literal adaptation, negation, and conversions, and `tests/run/` gained programs for every width at its limits, wrapping arithmetic including the division that overflows, unsigned comparisons and shifts, conversions that widen, narrow, and wrap, and a trap for each of overflow, an overflowing compound assignment, division by zero, an out-of-range shift, a conversion that doesn't fit, and an unsigned value past the signed maximum. 3362 unit assertions and 40 programs, 0 failures.

## Current Limitations

There's still one float type, and conversions between integers and floats aren't supported. Literals don't take their type from a scope's value, so `let x :: i8 <- { 5 -| };` is an `int` and a mismatch. Shifting left doesn't trap when bits are shifted out the top, only when the amount is out of range.

## What's Next

Commit 6.2 does floats: every float type but FP8, NaN-free floats that trap on NaN, NaN-carrying floats and their total order, shallow equality as bit equality, bfloat16 and `f128`, and conversions between floats and integers.
