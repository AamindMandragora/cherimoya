# Commit 9.3: Strings

## The Problem

A string literal could be written, and `@test(traps <- "...")` read one, but sema had no type for one anywhere else. Commit 9.3 gives strings their types, which are no new types at all: a string is an array of characters, and everything arrays already do, strings do too. Two things arrays couldn't do yet are what strings most need: a function that takes a string of any length, and a comparison or a match against a literal of a different length.

## Three Kinds of String

Cherimoya has three kinds of character, so it has three kinds of string, and a program picks the one it needs:

| String | `"héllo"` is | `s[1]` | `s.len()` |
|---|---|---|---|
| `arr[char]` | an error, since `é` isn't ASCII | `'e'` in `"hello"` | the characters, which are bytes too |
| `arr[i8u]` | 6 UTF-8 bytes | `195`, the first half of `é` | the bytes |
| `arr[rune]` | 5 runes | `'é'` | the characters |

Chars are ASCII, one byte each, so a string of them is compact and indexes by character, but holds only ASCII. Bytes are UTF-8, compact, and what files, terminals, and C use, so they never convert at the edges, but a character can take several of them. Runes are each a whole character, four bytes each. Bytes get a name of their own once type aliases arrive.

## The Literal

A literal is whatever kind of string its context wants: `let r :: arr[5, rune] <- "héllo";` is five runes, a literal passed to an `arr[i8u]` input is bytes, and a pattern is the kind its subject is. Without a context, a literal is chars when every character is ASCII, `"hello"` is an `arr[5, char]`, and its UTF-8 bytes when one isn't, `"héllo"` is an `arr[6, i8u]`. A literal that isn't ASCII where chars are wanted is `cherimoya requires "héllo" to be ASCII to be a string of chars, please make it 'arr[i8u]' or 'arr[rune]'`. A literal holds the escapes a character literal does, checked the same way, `\n \t \r \0 \\ \' \"`, `\xHH` up to `7F`, and `\u{...}`, so every literal is valid UTF-8, and bytes that aren't are written as an array, `[0xFF] :: arr[1, i8u]`. An empty literal, `""`, is an `arr[0, char]`. Like any constant fixed array, a literal bound to an immutable binding lives in read-only data, and one bound as `mut`, `let word :: arr[3, mut i8u] <- "abc";`, is copied into the binding, whose bytes can then be written. Indexing gives an element of its kind, and `s.len()` is how many there are.

## Changing Kinds

A string of chars already is a string of bytes, since ASCII is valid UTF-8, byte for byte, so it widens into one without being written: it binds, compares, and is lent as bytes wherever bytes are wanted, through arrays and pointers of them too. Nothing is copied, since nothing changes. The one place it doesn't go is an input that can write bytes, since bytes that aren't ASCII would break the chars, so that's a type error. Nothing else widens, since every other change of kind makes a new array, and an allocation is never hidden, so it's written as a conversion, which makes an array on the heap:

| Conversion | From | Fails when |
|---|---|---|
| `arr[rune](s)` | bytes or chars, decoded | the bytes aren't valid UTF-8: `trap at line 4: 'arr[rune](s)' isn't valid UTF-8` |
| `arr[i8u](s)` | runes, encoded, or chars, copied | never, since a rune is always a Unicode scalar value |
| `arr[char](s)` | bytes or runes | a character isn't ASCII: `trap at line 4: 'arr[char](s)' isn't ASCII` |

Decoding is strict, so a sequence cut short, a continuation byte where none goes, an encoding longer than its character needs, a surrogate, or a code point past `10FFFF` all trap.

## Lending

A fixed array's length is part of its type, so a function taking `arr[5, i8u]` takes only five-byte strings. A function that takes an `arr[T]` input borrows it, so any fixed array of `T` can go there instead: it's lent as an array on the heap's handle to where its elements already are, their address and how many there are, without copying anything, so `(s :: arr[i8u])` takes any string, and `(xs :: arr[int])` takes `[1, 2, 3]`. That's the free view of a fixed array the spec gives, ahead of `peek`, which arrives with the modes in 9.4. An `unscoped` input takes over what it's given, which a fixed array can't hand over, so it isn't lent.

A lent binding's own elements are what the function sees, so a function that writes them, `(s :: arr[mut i8u])`, writes the binding's. A function that can write gets a copy of anything whose elements can't be written, a literal, an immutable binding, or a value that isn't a binding, so it never writes read-only data, and the caller's value stays as it was. Checking that a binding passed to an input that writes its elements can be written is the modes' job, in 9.4, where it covers arrays on the heap and pointers too.

## Comparing

`=` and `==` compare an array on the heap with a fixed array of the same elements by length and then each element, with the same operator, so `cmd == "go"` works on any `arr[i8u]`, and `xs == [1, 2]` on any `arr[int]`, and strings of chars compare with strings of bytes, since they're the same bytes. Two fixed arrays of different lengths are different types, so comparing them is still an error, since the answer is always false. Nothing orders strings, so `<` on one is an error, until protocols can give text an order.

A string is a literal pattern in a match, which matches when the subject has the same length and the same bytes:

```
match (cmd) {
    "go" -> { 1 -| code }
    "stop" -> { 2 -| code }
    _ -> { 0 -| code }
}
```

A match on an array needs a catch-all, the way one on a number does, and a pattern that can never match a fixed subject, since its length differs, is a warning: `cherimoya never matches this arm, since the subject holds 3 elements and this pattern 2`.

## Debugging

With `-g`, gdb shows each kind of string the way cherimoya writes it, with its escapes, on the heap or not: chars as their text, runes as theirs after a `U`, and bytes as their numbers and then, when they're valid UTF-8, the text they spell, since bytes are also binary data:

```
$1 = "abc"
$2 = U"héllo"
$3 = {104, 195, 169, 10} "hé\n"
$4 = arr[i8u] of 2 = {103, 111} "go"
```

Bytes that aren't valid UTF-8, and signed bytes, show only their numbers.

## Codegen

A literal is a constant array of its chars, bytes, or runes. A string of chars widens into bytes with no code at all, since both lower to the same bytes. The conversions go through the runtime, `cheri_decode_utf8`, `cheri_encode_utf8`, and `cheri_to_ascii`, which make the new array and trap on what it can't hold, except chars into bytes, which is a copy. Lending makes a handle from the address of the lent elements, a binding's slot or global, or a slot of their own for a value that isn't a place, or one that's copied for an input that can write it, and the length the type knows, and nothing frees it, since it's borrowed. Comparing an array on the heap with a fixed one puts the fixed one in a slot and compares it as a handle to that slot, and a fixed subject and a pattern of different lengths compare as false.

## What Was Built

`src/sema/sema.hpp` types string literals by their context, widens chars into bytes, checks the conversions between kinds, lends fixed arrays to borrowed `arr[T]` inputs, compares arrays across lengths, and matches string patterns, and gives a pack the element type of an array on the heap it's expected to be. `src/codegen/codegen.hpp` gained `lend`, `compareAny`, and `convertString`, and emits literals, `runtime/runtime.c` gained the conversions, and `src/codegen/printers.py` shows strings as text. `tests/unit/sema/test_sema.cpp` checks literals and their kinds, widening, conversions, lending, comparing, and patterns, `tests/features/strings.toya` runs them, including writing through a lent array and the copy a read-only one gets, and `tests/debug/strings.moya` prints them in gdb. 14706 unit assertions, 9 programs, the run of `tests/test/`, 190 feature tests, 8 debug programs, all under valgrind, and the grammar's corpus, 0 failures.

## Current Limitations

Programs can't print yet, so a string can only be compared, matched, and passed around until output comes with the standard library. Strings can't be joined until array math chooses its operator in commit 12, so a joined string is built with `arr[n, mut i8u](_)` and a loop. Walking a string of bytes by its characters, without converting it to runes first, waits for for-each loops. A string passed to an input that writes it isn't checked to be writable until 9.4. Bytes get a name of their own once type aliases arrive, and `string` and `cstring`, text with its functions and C's null-terminated strings, come with the standard library and FFI.
