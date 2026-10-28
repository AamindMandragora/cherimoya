# Commit 8.1: Characters and Annotations

## The Problem

After commit 7.1, the numeric model is complete, but the lexer already knows two more basic types that sema rejects: `char` and `rune`. A char literal like `'a'` lexes, and so does an escape like `'\n'`, but the lexer never looks inside one, so `'\q'` and `'ab'` lex just as happily, and sema reports every char literal as unsupported. Annotations, `(5 :: i8)`, parse and are rejected the same way. Commit 8 finishes the basic types in two parts, and this first part does the characters and annotations: what a quoted literal can hold, the two character types, their literals, comparisons, matches, and conversions, and the checks that keep a character a character. Commit 8.2 does `boolf`.

## Two Character Types

A `char` is one ASCII character, 0 to 7F, held in a byte. A `rune` is one Unicode scalar value, which is every code point from 0 to 10FFFF but the surrogates, D800 to DFFF, the halves UTF-16 pairs up, which aren't characters on their own. That's what Rust's `char` and Swift's `Unicode.Scalar` hold. A char is ASCII and not any byte so that a char is always a whole character: a byte above 7F is part of some longer UTF-8 sequence, and holding half of an `é` isn't holding a character. Text, which is UTF-8 bytes, comes with strings in commit 9.

Both are primitives, like `bool`, so each is one interned type and two characters have the same type exactly when they're the same pointer. A char and a rune never mix without a conversion, the same as two integers of different widths.

## What a Quoted Literal Holds

The lexer now reads every character between the quotes of a string or char literal, so a malformed one is a lex error at the exact bytes that are wrong, and the literal becomes an error token the parser already skips. A character is either written as itself, in UTF-8, or an escape:

| Escape | Means |
|---|---|
| `\n` `\t` `\r` `\0` | newline, tab, carriage return, and the zero character |
| `\\` `\'` `\"` | the character itself |
| `\xHH` | the code point HH, two hex digits, up to FF |
| `\u{H...}` | the code point H..., 1 to 6 hex digits in braces, any Unicode scalar value |

Anything else after a `\` is an error that lists every escape there is. A `\u{...}` that names a surrogate or anything past 10FFFF is an error too, and so are bytes that aren't UTF-8: a continuation byte where a character should start, a sequence cut short, a character written with more bytes than it needs, like `C0 80` for zero, and a surrogate written out in UTF-8. Decoding one character is one function, `decodeCharacter`, shared by the lexer, which reports, and by sema and codegen, which read the value of a literal the lexer already let through.

A char literal holds exactly one character, so `''` and `'ab'` are errors, each one error over the whole literal. String literals get the same escape checks now, though strings themselves wait for commit 9.

## Char Literals Take the Type Around Them

A char literal adapts the way a number literal does. In a rune context it's a rune, in a char context it's a char, and with no context it's a char, unless its value isn't ASCII, and then it's a rune, so `'a'` is a char and `'é'` and `'\u{1F600}'` are runes. A char context only takes an ASCII value, so `let c :: char <- 'é';` is `cherimoya requires 'é' to fit in 'char', which is ASCII, please use a 'rune'`. Char literals count as literals on either side of an operator, so `'a' < r` and `r > 'a'` both make the `'a'` a rune when `r` is one, and a chain like `'a' <= c <= 'z'` takes `c`'s type.

## Comparisons and Matches

Characters compare with `= != < <= > >=`, by code point, and `==` and `!==` are the same as `=` and `!=`, since a character's bits are its value. There's no arithmetic, so `'a' + 1` is an error, and stepping through characters is written as a conversion: `char(int('a') + 1)`. A character can be a match subject, with literal patterns that adapt to it, and like an integer it needs a catch-all, since its arms can't list every value. Range patterns like `['a', ..., 'z']` come with ranges in commit 10.

## Conversions

A character converts to and from any integer, and a char and a rune convert to each other, but a character never converts to or from a float or a bool. The idea that makes every one of these work is that a character is the unsigned integer holding its code point: a char is converted as an 8-bit unsigned integer, and a rune as a 32-bit one. So a conversion first goes through the integer conversion commit 6.1 already has, which traps when the value doesn't fit in the target's bits, and a conversion into a character then checks what a character can't hold: a char traps above 7F, and a rune traps above 10FFFF or on a surrogate. Subtracting D800 puts every surrogate between 0 and 7FF, and wraps anything below D800 around to a huge value, so one unsigned comparison finds them. A conversion into a character always checks, even from a wrapping integer, since there's no character to wrap to.

```
trap at line 4: 'char(n)' doesn't fit in char
```

Comparisons go through the integer comparisons the same way, as unsigned code points, and in LLVM a char is an `i8` and a rune an `i32`.

## Annotations

An annotation, `(value :: T)`, checks the value against `T` and never converts it. The value is checked with `T` as the type its context wants, so a literal takes `T`'s type, and anything else has to already be a `T`: `(5 :: i8)` is an `i8`, `('a' :: rune)` is a rune, `(300 :: i8)` is the usual literal that doesn't fit, and `(x :: i8)` on an `int` is a mismatch, not a narrowing. That makes an annotation the way to give a literal a type where nothing else would, like `(1 :: i8u) < 2`. The annotation's type is the result either way, so nothing after it reports again, and codegen emits the value itself, since the annotation only told sema its type.

## What Was Built

`src/lexer/escape.hpp` decodes one character of a quoted literal, and `src/lexer/lexer.hpp` checks every literal's contents with it. `src/parser/expr.hpp` gained the value of a char literal. `src/sema/type.hpp` gained the `char` and `rune` primitives and a check for the character types, and `src/sema/context.hpp` makes them. `src/sema/sema.hpp` gained char literals and how they adapt, character comparisons, matches, and conversions, and annotations. `src/codegen/codegen.hpp` gained char literals, characters as the integers holding their code points, the checks a conversion into a character makes, and annotations. `src/lsp/features.hpp` says what each character type holds.

`tests/unit/lexer/test_lexer.cpp` gained every escape and UTF-8 character a literal can hold and every way one can be malformed, `tests/unit/sema/test_sema.cpp` gained char literals, character operators, character conversions, and annotations, and `tests/run/` gained a program that uses all of them and a trap for each of a char that isn't ASCII and a rune that's a surrogate. 14184 unit assertions and 55 programs, 0 failures.

## Current Limitations

Characters have no range patterns yet, so a match on a character lists its cases one at a time. A conversion into a rune says the value doesn't fit in `rune` whether it's past 32 bits, past 10FFFF, or a surrogate, without saying which. Literals still don't take their type from a scope's value.

## What's Next

Commit 8.2 does `boolf`, the fuzzy boolean: a byte where 0 to 255 stands for 0 to 1, `&&` as the minimum, `||` as the maximum, `*` as the product, and `!` as one minus the value, with conversions to and from floats and bytes, and a cutoff that's always written out to turn one back into a `bool`.
