# Commit 2.1: Integers

## The Feature

- **Twenty integer types.** Every width from 8 to 128 bits, signed or unsigned, trapping or wrapping, named by those three facts: `i32` is 32 bits, signed, and trapping, `i32u` is unsigned, `i32w` wraps, and `i32uw` is both. `int` is `i64`, and `i64` prints as `int`.
- **No mixing.** `x + y` with an `i8` and an `i16` is the same `cherimoya requires both sides of '+' to have the same type, please convert one of them` as an `int` and a `float`. Bitwise operators take any integer, and comparisons and arithmetic take any number.
- **Negation.** `-x` takes a signed or a wrapping integer. Negating a trapping unsigned one is `cherimoya requires negation to be on a signed or wrapping integer, please use an 'i32' or an 'i32uw' instead of 'i32u'`.
- **Literals take the type around them.** A literal has the type its context wants, and `int` when the context wants nothing in particular:
  - a binding's annotation: `let x :: i8 <- 5;`
  - an assignment's place: `x <- 5`, and a compound one: `x +<- 1`
  - a call's parameter: `f(5)`
  - a function's output: `5 -| f`
  - the other side of an operator: `x + 1` and `1 + x`, and in a chain, its first operand that isn't a literal
  - a match's subject: `0 -> { }`
- **Literals have to fit.** `cherimoya requires 300 to fit in 'i8', please use a wider type or convert it explicitly`, for wrapping types too. A negated literal is checked as a whole, so `-128` fits in an `i8`.
- **Conversions.** `i8(x)`, `i128u(n)`, or `x |> i8` converts between integers. Into a trapping type it traps if the value didn't survive, and into a wrapping type it keeps the low bits, so `i8w(300)` is `44`.
- **Run-time traps.** Overflow, division by zero, a shift out of range, and a conversion that doesn't fit stop the program, quoting the code that failed:

```
trap at line 3: 'x * 2' overflowed i8
trap at line 4: 'a / b' divided by zero
trap at line 4: 'x << n' shifted by an amount outside 0 to 31
trap at line 4: 'i8(x)' doesn't fit in i8
```

- **Windows.** The driver builds `.exe` files with `clang`, and `--target x86_64-pc-windows-msvc -c` writes a Windows object file from Linux.

## Why

After commit 1.9, cherimoya has one integer type: `int` is 64 bits, its arithmetic wraps silently on overflow, and dividing by zero is undefined. This commit builds the full set of integers in sema and codegen, with literals that adapt to them, conversions, and the run-time checks that make trapping integers trap. Floats and FP8 follow the same way.

- **Three facts in the name,** so a type says how it behaves where it's written, and wrapping is something a program asks for.
- **Interned like every type,** so two integers have the same type exactly when they're the same pointer.
- **No implicit conversions.** Integers of different types never mix, and the fix is a conversion that says what happens to a value that doesn't fit.
- **Negating a trapping unsigned value** overflows for every value but zero, so it's a compile error that suggests the signed or wrapping type instead.
- **Only literals use the expected type.** Checking is told what type the context wants, and a literal is the one thing with no type of its own, so the type flows from wherever it's known without any other rule changing.
- **A literal that doesn't fit is a compile error,** since its value is known while compiling, even for a wrapping type, since a literal is written on purpose. Literals are read with 128 bits of headroom, so every value of every type can be written, up to `i128u`'s maximum.
- **Signedness lives in the instructions.** LLVM integers have no sign: whether `0xFF` is 255 or -1 depends on the instruction that reads it, so codegen picks `sdiv` or `udiv`, `srem` or `urem`, `ashr` or `lshr`, signed or unsigned comparisons, and sign or zero extension by the type.
- **Wrapping is free.** Two's complement is what the hardware does already, so a wrapping type's `+`, `-`, and `*` are plain instructions, and a trapping type's go through LLVM's overflow intrinsics.
- **Division by zero traps for every integer,** wrapping ones included, since there's no value to wrap to. The other overflowing division, a signed minimum divided by -1, is undefined in LLVM, so codegen handles it: a trapping type traps, and a wrapping one gives the minimum for `/` and zero for `%`, which is exactly what dividing by 1 gives.
- **Out-of-range shifts are undefined in LLVM too.** A trapping type traps on an amount outside 0 to the width minus one, and a wrapping type takes the amount modulo the width, so `1 << 33` as an `i32w` is `1 << 1`.
- **A conversion checks by converting back.** If the round trip doesn't give the original value, the conversion lost bits. That misses a change of sign, since -1 as an `i8` and 255 as an `i8u` are the same bits, so a conversion that changes signedness also checks that the value is at least zero on the signed side.
- **One place for platform differences,** so the rest of the driver reads the same everywhere. Code generation already worked for any target.

## Implementation

### Sema

- **Types.** `src/sema/type.hpp` adds `IntSemaType`, kind `SemaTypeKind::Int`, with `bits`, `isSigned`, `isWrapping`, and the `name` it prints as, and `int` stops being a `PrimitiveKind`. `TypeContext::makeInts` in `src/sema/context.hpp` makes all twenty up front into `table`, keyed by the name they're written with, `semaInt` is `table.at("i64")`, and `intType` finds one by name. `resolveType` resolves `int` and every name in the table.
- **Expected types.** `checkExpr` and `typeOf` take `expected`, the type the context wants. `checkLet` passes the annotation, an assignment checks its place first and passes its type, `checkCall` passes each parameter's type, `checkPattern` passes the subject's, and `checkYield` passes a function's output or the type an earlier yield gave a scope. Arithmetic and bitwise operators pass what they're expected to give into their operands, and `checkOperands` checks the side that isn't a literal first, so a literal on either side takes the other's type. A chain checks its first operand that isn't a literal, then the rest against it.
- **Literals.** `intLiteral` takes the expected type when it's an integer, and `int` otherwise. `literalValue` reads the raw text into an `unsigned __int128`, skipping separators, reading the radix from its prefix, and giving nothing past 128 bits. `isInRange` checks the value against the type's largest magnitude in the direction the literal points. `isIntLiteral` recognizes a literal, negated or not, and a negated one is checked as a whole.
- **Operators.** `isInt` and `isNumeric` admit every integer type, `unaryType` rejects negating a trapping unsigned integer, and equality takes any value that isn't a function.
- **Conversions.** `checkConversion` handles a type in the callee's place: one value, `cherimoya expected 1 value to convert in the call to 'i8', got 2 instead`, between two integers, `cherimoya requires a conversion to be between integers, please convert 'bool' to 'i8' some other way`, and a float on either side is unsupported until floats have conversions.
- **Yield targets.** `checkYield` resolves its target before checking the value, to know the type it wants, then again after, since a value that holds scopes pushes targets of its own and can move the stack.

### Codegen

- **Types and literals.** `lower` gives an integer `llvm::Type::getIntNTy` of its width, and a literal's `APInt` is as wide as its type. `asInt` and `isUnsigned` read a sema type, and `compare` takes `unsignedInts` to pick unsigned comparisons, from the left operand's type or a chain's first operand's.
- **Traps.** `trapIf` branches on a bool that says something's wrong to a block calling `cheri_trap` with a message and the line, and carries on in a fresh block otherwise. Every run-time check goes through it, `assert` included. `quoted` gives a span's source text in quotes for the message.
- **Arithmetic.** `arithmetic` takes the operands' sema type and the span. A trapping integer's `+`, `-`, and `*` go through `checked`, which calls `sadd_with_overflow`, `uadd_with_overflow`, and their `sub` and `mul` siblings, then traps on the overflow bit with `'x * 2' overflowed i8`. A compound assignment's span runs from the place to the value, so it traps as `'x +<- 100' overflowed i8`.
- **Negation.** Negating a trapping signed integer is `0 - x` through `ssub_with_overflow`, since only the minimum overflows. A negated literal is a constant sema already checked, so `-128` as an `i8` never negates a `128` that would overflow on the way.
- **Division.** `divide` traps on a zero divisor with `divided by zero`, then, for a signed type, tests for the minimum over -1: a trapping type traps, and a wrapping one swaps the divisor for 1 with a `select`. It then emits the signed or unsigned instruction.
- **Shifts.** `shift` ands a wrapping type's amount with the width minus one, since every width is a power of two, and traps a trapping type's amount that's `uge` the width, where comparing as unsigned also catches a negative amount, which reads as enormous. `>>` is `ashr` for a signed type and `lshr` for an unsigned one.
- **Conversions.** `convert` truncates, sign-extends, or zero-extends by the source's signedness. A wrapping target keeps the result. A trapping one converts back, compares with the original, and when the signedness changes, also requires the signed side to be at least zero, trapping with `'i8(x)' doesn't fit in i8`. A call or either pipe with a type in the function's place converts.

### Driver

`tools/cheri/main.cpp` keeps everything that differs between platforms in the `platform` namespace: executables are `.exe` and objects `.obj` on Windows, the default linker there is `clang`, since there's no `cc`, `exitCode` passes Windows' exit code on directly instead of decoding a wait status, and `quote` and `shell` quote arguments the way `cmd` reads them, in double quotes, with one more pair around the whole command, since `cmd` strips the outermost pair.

## Tests

`tests/unit/sema/test_sema.cpp` adds integer types, literal adaptation, integer negation, and integer conversions. `tests/run/` adds programs for every width at its limits, wrapping arithmetic including the division that overflows, unsigned comparisons and shifts, conversions that widen, narrow, and wrap, and a trap for each of overflow, an overflowing compound assignment, division by zero, an out-of-range shift, a conversion that doesn't fit, and an unsigned value past the signed maximum. 3362 unit assertions and 40 programs, 0 failures.

## Current Limitations

- **One float.** There's still one float type, and conversions between integers and floats aren't supported, until floats get the same treatment.
- **Scope values don't pass a type in.** `let x :: i8 <- { 5 -| };` is an `int` and a mismatch, since a block's first yield doesn't know the type wanted of the block.
- **Left shifts don't check what falls out.** Shifting left traps only when the amount is out of range, not when bits are shifted out the top.
