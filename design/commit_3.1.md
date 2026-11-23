# Commit 3.1: Characters and Annotations

## The Feature

- **Two character types.** A `char` is one ASCII character, 0 to 7F, held in a byte. A `rune` is one Unicode scalar value, every code point from 0 to 10FFFF but the surrogates, D800 to DFFF, the same set Rust's `char` and Swift's `Unicode.Scalar` hold. A char and a rune never mix without a conversion, the same as two integers of different widths.
- **Checked literals.** The lexer reads every character between the quotes of a string or char literal, so a malformed one is a lex error at the exact bytes that are wrong. A character is written as itself, in UTF-8, or as an escape:

| Escape | Means |
|---|---|
| `\n` `\t` `\r` `\0` | newline, tab, carriage return, and the zero character |
| `\\` `\'` `\"` | the character itself |
| `\xHH` | the code point HH, two hex digits, up to FF |
| `\u{H...}` | the code point H..., 1 to 6 hex digits in braces, any Unicode scalar value |

- **Malformed literals.** Anything else after a `\` is `cherimoya expected an escape, which is \n, \t, \r, \0, \\, \', \", \xHH, or \u{...}, got '\q' instead`. A `\u{...}` naming a surrogate or anything past 10FFFF is an error, and so are bytes that aren't UTF-8: a continuation byte where a character should start, a sequence cut short, a character written with more bytes than it needs, like `C0 80` for zero, and a surrogate written out in UTF-8, each `cherimoya expected a character in UTF-8, got bytes that aren't UTF-8 instead`. A char literal holds exactly one character, so `''` and `'ab'` are each one error over the whole literal, `cherimoya expected one character in a char literal, got 2 instead`. String literals get the same checks.
- **Literals take the type around them.** In a rune context a char literal is a rune, in a char context a char, and with no context a char unless its value isn't ASCII, so `'a'` is a char and `'é'` and `'\u{1F600}'` are runes. A char context only takes ASCII: `let c :: char <- 'é';` is `cherimoya requires 'é' to fit in 'char', which is ASCII, please use a 'rune'`. Char literals count as literals on either side of an operator, so `'a' < r` and `r > 'a'` both make the `'a'` a rune when `r` is one, and `'a' <= c <= 'z'` takes `c`'s type.
- **Comparisons and matches.** Characters compare with `= != < <= > >=` by code point, and `==` and `!==` are the same as `=` and `!=`, since a character's bits are its value. There's no arithmetic, so `'a' + 1` is an error, and stepping through characters is a conversion: `char(int('a') + 1)`. A character can be a match subject with literal patterns that adapt to it, and like an integer it needs a catch-all.
- **Conversions.** A character converts to and from any integer, and a char and a rune convert to each other, but a character never converts to or from a float or a bool. A conversion into a character traps on a value it can't hold: `trap at line 4: 'char(n)' doesn't fit in char`, and `'rune(n)' doesn't fit in rune` for a surrogate.
- **Annotations.** `(value :: T)` checks the value against `T` and never converts it. The value is checked with `T` as the type its context wants, so `(5 :: i8)` is an `i8`, `('a' :: rune)` is a rune, `(300 :: i8)` is the usual literal that doesn't fit, and `(x :: i8)` on an `int` is a mismatch, not a narrowing. That makes an annotation the way to give a literal a type where nothing else would, like `(1 :: i8u) < 2`.

## Why

The numbers are complete after commit 2.4, but the lexer already knows two more basic types that sema rejects, `char` and `rune`. A char literal like `'a'` lexes, but nothing looks inside one, so `'\q'` and `'ab'` would lex just as happily. Annotations, `(5 :: i8)`, parse and would be rejected the same way. This commit gives the characters their types, literals, comparisons, matches, and conversions, along with the checks that keep a character a character, and gives annotations their meaning. Strings, which are UTF-8 bytes, build on this.

- **A char is ASCII, not any byte,** so a char is always a whole character: a byte above 7F is part of some longer UTF-8 sequence, and holding half of an `é` isn't holding a character.
- **A rune excludes surrogates,** since they're the halves UTF-16 pairs up and aren't characters on their own.
- **Primitives,** like `bool`, so each is one interned type, and two characters have the same type exactly when they're the same pointer.
- **Errors at the bytes.** Checking a literal in the lexer reports the exact bytes that are wrong, and the bad literal becomes an error token the parser already skips.
- **One decoder.** Decoding one character is one function, shared by the lexer, which reports, and by sema and codegen, which read the value of a literal the lexer already let through.
- **A character is the unsigned integer holding its code point.** A char converts and compares as an 8-bit unsigned integer and a rune as a 32-bit one, so every conversion reuses the integer conversion from commit 2.1, which already traps when the value doesn't fit in the target's bits, and only adds what a character can't hold.
- **Always checked.** A conversion into a character checks even from a wrapping integer, since there's no character to wrap to.
- **Annotations never convert,** so they can't hide a narrowing. The annotation's type is the result either way, so nothing after a mismatch reports again.

## Implementation

### Lexer

`src/lexer/escape.hpp` adds `decodeCharacter`, which reads one character from the start of a quoted literal's contents into a `Decoded`, its code point, how many bytes it took, and its error, if any. For UTF-8 it reads the lead byte's length, checks each continuation byte, and rejects overlong encodings against the smallest value each length can hold and anything `isScalar` rejects. For an escape it reads the escapes in the table, `\xHH` through `hexValue`, and `\u{...}` with up to 6 hex digits. `scanQuoted` in `src/lexer/lexer.hpp` decodes every character between the quotes, reports each error over its own bytes, counts the characters of a char literal, and pushes a `TokenKind::Error` token for a literal that isn't valid.

### Parser

`charLiteralValue` in `src/parser/expr.hpp` gives the code point a char literal stands for, by decoding its contents.

### Sema

- **Types.** `src/sema/type.hpp` adds `PrimitiveKind::Char` and `PrimitiveKind::Rune` and `isCharacter`, and `src/sema/context.hpp` makes `semaChar` and `semaRune`, which `resolveType` returns for `char` and `rune`.
- **Literals.** `charLiteral` adapts a char literal to an expected rune or char, reports a non-ASCII value in a char context, and defaults by whether the value is ASCII. `isLiteral` counts char literals, so `checkOperands` and chains type them from the other side.
- **Operators.** `binaryType` takes characters for comparisons only, and its wanted text becomes "an int, a float, or a character".
- **Matches.** `checkMatch` requires a catch-all on a character subject.
- **Conversions.** `checkConversion` accepts numbers to numbers, characters to characters, and characters to and from integers, and otherwise reports `cherimoya requires a conversion to be between numbers, or between characters and integers, please convert ... some other way`.
- **Annotations.** `checkAnnotation` resolves `T`, checks the value with `T` as its expected type, reports a mismatch if the value isn't compatible, and gives `T`. Chars and annotations leave `unsupportedName`.

### Codegen

- **Types.** `lower` makes a char an `i8` and a rune an `i32`, and a char literal is a constant of its type holding `charLiteralValue`.
- **Code points.** `charBits` and `runeBits` are the 8-bit and 32-bit unsigned `IntSemaType`s a char and a rune are converted and compared as, named after the character so a trap says which one a value didn't fit in. `codePointOf` gives a type's integer, or its character's. `compare` passes characters to `intCompare` as their code points.
- **Conversions.** `convert` hands any conversion with a character side to `convertCharacter`, which goes through `intToInt` first, and into a character then checks what a character can't hold: above 7F for a char, and above 10FFFF or a surrogate for a rune. Subtracting D800 puts every surrogate between 0 and 7FF and wraps anything below D800 around to a huge value, so one unsigned comparison finds them.
- **Annotations.** `emitExpr` emits an annotation's value itself, since the annotation only told sema its type, and `collectExpr` looks inside one for nested functions.

### Language Server

`typeHelp` in `src/lsp/features.hpp` says what each character type holds.

## Tests

`tests/unit/lexer/test_lexer.cpp` adds every escape and UTF-8 character a literal can hold and every way one can be malformed. `tests/unit/sema/test_sema.cpp` adds char literals, character operators, character conversions, and annotations. `tests/run/` adds `chars.moya`, which uses all of them, and a trap for each of a char that isn't ASCII, `char_trap.moya`, and a rune that's a surrogate, `rune_trap.moya`. 14184 unit assertions and 55 programs, 0 failures.

## Current Limitations

- **Range patterns.** A match on a character lists its cases one at a time, until ranges allow `['a', ..., 'z']`.
- **Rune traps.** A conversion into a rune says the value doesn't fit in `rune` whether it's past 32 bits, past 10FFFF, or a surrogate, without saying which.
- **Scope values.** Literals don't yet take their type from a scope's value.
