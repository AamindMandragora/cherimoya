# Commit 3.2: Strings

## The Feature

- **Three kinds of string.** A string is an array of characters, so it's no new type at all, and everything arrays do, strings do too. Cherimoya has three kinds of character, so it has three kinds of string:

| String | `"héllo"` is | `s[1]` | `s.len()` |
|---|---|---|---|
| `arr[char]` | an error, since `é` isn't ASCII | `'e'` in `"hello"` | the characters, which are bytes too |
| `arr[i8u]` | 6 UTF-8 bytes | `195`, the first half of `é` | the bytes |
| `arr[rune]` | 5 runes | `'é'` | the characters |

- **Literals take the kind their context wants.** `let r :: arr[5, rune] <- "héllo";` is five runes, a literal passed to an `arr[i8u]` input is bytes, and a pattern is the kind its subject is. Without a context, a literal is chars when every character is ASCII, so `"hello"` is an `arr[5, char]`, and its UTF-8 bytes when one isn't, so `"héllo"` is an `arr[6, i8u]`. An empty literal, `""`, is an `arr[0, char]`. A literal that isn't ASCII where chars are wanted is `cherimoya requires "héllo" to be ASCII to be a string of chars, please make it 'arr[i8u]' or 'arr[rune]'`.
- **Escapes.** A string literal holds the escapes a character literal does, checked the same way, and each character is encoded in UTF-8, so every literal is valid UTF-8. Bytes that aren't are written as an array, `[0xFF] :: arr[1, i8u]`.
- **Storage.** Like any constant fixed array, a literal bound to an immutable binding lives in read-only data, and one bound as `mut`, `let word :: arr[3, mut i8u] <- "abc";`, is copied into the binding, whose bytes can then be written.
- **Chars widen into bytes.** A string of chars binds, compares, and is lent as bytes wherever bytes are wanted, through arrays and pointers of them too, without being written and without a copy. The one place it doesn't go is an input that can write bytes, which is a type error.
- **Conversions between kinds.** Every other change of kind makes a new array on the heap, so it's written as a conversion:

| Conversion | From | Fails when |
|---|---|---|
| `arr[rune](s)` | bytes or chars, decoded | the bytes aren't valid UTF-8: `trap at line 4: 'arr[rune](s)' isn't valid UTF-8` |
| `arr[i8u](s)` | runes, encoded, or chars, copied | never, since a rune is always a Unicode scalar value |
| `arr[char](s)` | bytes or runes | a character isn't ASCII: `trap at line 4: 'arr[char](s)' isn't ASCII` |

  Decoding is strict, so a sequence cut short, a continuation byte where none goes, an encoding longer than its character needs, a surrogate, or a code point past `10FFFF` all trap.
- **Lending.** A function that takes an `arr[T]` input borrows it, so any fixed array of `T` can go there: `(s :: arr[i8u])` takes a string of any length, and `(xs :: arr[int])` takes `[1, 2, 3]`. Nothing is copied. A function that writes the elements, `(s :: arr[mut i8u])`, writes the binding's own, and gets a copy of anything whose elements can't be written, a literal, an immutable binding, or a value that isn't a binding, so the caller's value stays as it was. An `unscoped` input isn't lent to.
- **Comparing across lengths.** `=` and `==` compare an array on the heap with a fixed array of the same elements by length and then each element with the same operator, so `cmd == "go"` works on any `arr[i8u]`, `xs == [1, 2]` on any `arr[int]`, and strings of chars compare with strings of bytes. Two fixed arrays of different lengths are different types, so comparing them is still an error, and nothing orders strings, so `<` on one is an error.
- **String patterns.** A string is a literal pattern, which matches when the subject has the same length and the same bytes:

```
match (cmd) {
    "go" -> { 1 -| code }
    "stop" -> { 2 -| code }
    _ -> { 0 -| code }
}
```

  A match on an array needs a catch-all, the way one on a number does, and a pattern that can never match a fixed subject, since its length differs, is a warning: `cherimoya never matches this arm, since the subject holds 3 elements and this pattern 2`.
- **Debugging.** With `-g`, gdb shows each kind of string the way cherimoya writes it, with its escapes, on the heap or not: chars as their text, runes as theirs after a `U`, and bytes as their numbers and then, when they're valid UTF-8, the text they spell. Bytes that aren't valid UTF-8, and signed bytes, show only their numbers.

```
$1 = "abc"
$2 = U"héllo"
$3 = {104, 195, 169, 10} "hé\n"
$4 = arr[i8u] of 2 = {103, 111} "go"
```

## Why

A string literal could be written, and `@test(traps <- "...")` read one, but sema had no type for one anywhere else. Making a string an array of characters means it needs no new type, only the two things arrays couldn't do yet and strings most need: a function that takes a string of any length, and a comparison or a match against a literal of a different length.

- **Three kinds, picked by the program.** Chars are ASCII, one byte each, so a string of them is compact and indexes by character, but holds only ASCII. Bytes are UTF-8, compact, and what files, terminals, and C use, so they never convert at the edges, but a character can take several of them. Runes are each a whole character, four bytes each.
- **Chars widen for free,** since ASCII is valid UTF-8, byte for byte, so nothing changes and nothing is copied. Writing bytes into chars could put a byte that isn't ASCII there, which is why an input that writes is the exception.
- **Other changes are written,** since each makes a new array, and an allocation is never hidden.
- **Lending** is the free view of a fixed array the spec gives: its elements' address and how many there are, with the length the type already knows. A fixed array's length is part of its type, so without lending a function taking `arr[5, i8u]` would take only five-byte strings.
- **Copying for writers** keeps a function from ever writing read-only data.
- **Bytes show their text** in gdb as well as their numbers, since bytes are also binary data.

## Implementation

### Sema

- **Literals.** `codePoints` decodes a literal's characters with `decodeCharacter`. `stringElement` gives the kind of string a type holds, chars, `i8u`, or runes, from a fixed array's or an array on the heap's elements. `stringLiteral` types a literal as a fixed array of the expected kind, sized by its code points for chars and runes and by `stringLiteralValue`'s UTF-8 bytes for bytes, and otherwise chars or bytes by whether it's all ASCII. `checkPack` takes the element type from an expected array on the heap too, so a pack lent to an `arr[T]` input knows its elements.
- **Widening.** `isWidening` holds when one type already is another bit for bit: the same type, a char where an `i8u` is wanted, or arrays of the same size and handles of the same kind whose elements widen. `isCompatible` accepts a widening.
- **Lending.** `isLendable` holds for a fixed array whose elements widen into an array on the heap's. When checking a call, an argument going to an input that writes its elements, read through `mutabilityOf` and `isUnscoped`, is a mismatch if it only fits by widening chars into bytes, and otherwise a lendable argument to a scoped input is recorded in `lent`, which codegen reads through `isLent`.
- **Comparisons.** `isComparableArrays` lets an array on the heap and a fixed one, or chars and bytes, compare, and `binaryType` accepts them for equality.
- **Patterns.** A literal pattern that's a fixed array of the subject's elements and a different length gets the warning, one that compares with the subject passes, and `checkMatch` requires a catch-all on an array subject.
- **Conversions.** `checkMake` treats a call of an array type on a string of another kind as a conversion, which takes exactly one value, and its error for anything else becomes `cherimoya expected a fixed array or another kind of string in the call to ...`. `madeCount`, which knows how many elements a made handle holds when the program compiles, only takes a fixed source's size when its elements are the handle's, since a conversion between kinds can change the count.
- **Codegen's view.** `runeType` and `intType` let codegen tell the kinds of string apart.

### Codegen

- **Literals.** A string literal is a `ConstantDataArray` of its runes when sema made it runes, and otherwise of its UTF-8 bytes from `stringLiteralValue`, which for an ASCII string of chars are its chars. A string of chars widens into bytes with no code at all, since both lower to the same bytes.
- **Lending.** `emitArguments` calls `lend` for every argument sema lent, passing whether the input can write its elements. `lend` makes an array on the heap's handle from the address of the elements, a binding's or an element's through `placeAddress`, or a slot of their own for a value that isn't a place, or for an input that writes a binding whose elements can't be written, and the length the type knows. Nothing frees it, since it's borrowed.
- **Comparisons.** `compareAny` compares two values sema let a comparison have. The same type goes to `compare`. Two fixed arrays of the same size compare as one type, chars and bytes being the same bytes, and of different sizes are false, which only a string pattern can reach. A heap array and a fixed one put the fixed one in a slot and compare it as a handle to that slot through `compareHeapArrays`. Equality operators and literal patterns in `emitMatch` both go through it.
- **Conversions.** `emitMake` tries `convertString` on a call with no length. It puts a fixed source in a slot or takes a heap source's data and length, copies chars into bytes with a `memcpy`, and calls the runtime for the rest, with an out-of-memory message and, for decoding or ASCII, the trap message. `arrayElement` gives the elements of either kind of array. Whether a make is sized is now read from the written type, so `arr[rune](s)` isn't mistaken for one.

### Runtime

`runtime/runtime.c` adds `cheri_decode_utf8`, which decodes bytes into runes and traps on anything that isn't valid UTF-8, checking overlong encodings against the smallest code point each length holds, `cheri_encode_utf8`, which encodes runes into bytes and can't fail, and `cheri_to_ascii`, which copies bytes or runes, by their width, into chars and traps on one past `7F`. Each makes its array through `cheri_alloc`, and the first two report how many elements they made through a count.

### Debug Info

`src/codegen/printers.py` adds `stringText`, which renders the elements of a string kind, `i8u`, `i8uw`, `char`, or `rune`, as cherimoya writes them, quoted with cherimoya's escapes and `\u{...}` for anything unprintable, or gives nothing when bytes aren't valid UTF-8 or runes aren't scalar values. `TextPrinter` prints that one line. `lookup` uses it for fixed arrays and for arrays on the heap, whose line starts with the array's type and length, and falls back to the byte and heap printers otherwise.

## Tests

`tests/unit/sema/test_sema.cpp` checks literals and their kinds, widening, conversions, lending, comparing, and patterns. `tests/features/strings.toya` runs them, including writing through a lent array and the copy a read-only one gets, and `tests/debug/strings.moya` prints every kind in gdb. 14706 unit assertions, 9 programs, the run of `tests/test/`, 190 feature tests, 8 debug programs, all under valgrind, and the grammar's corpus, 0 failures.

## Current Limitations

- **No output.** Programs can't print yet, so a string can only be compared, matched, and passed around until output comes with the standard library.
- **No joining.** Strings can't be joined until array math chooses its operator, so a joined string is built with `arr[n, mut i8u](_)` and a loop.
- **Walking characters.** Walking a string of bytes by its characters, without converting it to runes first, waits for for-each loops.
- **Writable lending.** A binding passed to an input that writes its elements isn't checked to be writable until modes arrive, where the check covers arrays on the heap and pointers too.
- **No order.** Nothing orders strings until protocols can give text an order.
- **Names.** Bytes get a name of their own once type aliases arrive, and `string` and `cstring`, text with its functions and C's null-terminated strings, come with the standard library and FFI.
