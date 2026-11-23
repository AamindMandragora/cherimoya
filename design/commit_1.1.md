# Commit 1.1: Lexer

## The Feature

- **Tokens.** `cheri <file.moya>` reads a file and turns it into a stream of tokens, printing each one's kind, text, and byte offset, and exits non-zero if anything was malformed.
- **Keywords.** `let type export import`, the scopes `match repeat chunk`, the qualifiers `unscoped peek mut ptr arr`, the place intrinsics `take swap replace`, the verification words `wit assert old`, `true false`, and `macro moya`, reserved for macros. A lone `_` is its own `Underscore` token, and `_x` is an ordinary name. There is no `if`: it lexes as a name.
- **Built-in type names.** `i8` through `i128uw`, the floats in NaN-free and NaN-carrying `n` pairs (`f8`, which is `f8e4m3`, `f8e5m2`, `f16`, `f16e8m7`, `f32`, `f64`, `f128`), `int`, `float`, `bool`, `char`, `rune`, and `boolf` all lex as `TypeName`. A malformed one like `i64wu` or `f8n` lexes as a name.
- **Numbers.** Decimal, `0x` hex, `0o` octal, and `0b` binary, with `_` separators anywhere in a digit run and a fractional part in every base. `3.14` is one float, `1.foo` is `1 . foo`, `1.` is `1 .`, and `1...` is an integer and an ellipsis. There are no leading-dot floats: write `0.5`, not `.5`.
- **Comments.** `#` to the end of the line. There are no block comments, which leaves `//` free to be an operator and means a comment can never swallow code.
- **Operators.** One table holds every operator: 28 built-ins, 96 extra operators, and 78 compounds. Maximal munch picks the longest spelling, so `<<<-` wins over `<<<`, which wins over `<<`, then `<-`, then `<`.
- **Punctuation.** `( ) [ ] { } , ; . ... : :: @ $`. There is no `..`, which lexes as two dots for the parser to reject.
- **The spacing rule.** Two operator tokens that touch, where either is a flow token, a pipe, a compound, or an extra operator, are an error: `a**-b` gets `cherimoya requires whitespace between the operators '**' and '-', please write '** -'`, and `a ** -b` is fine.
- **Errors that don't stop lexing.** A bad number like `0o78` gets `cherimoya expected an octal digit (0 to 7), got '8' instead`, and `0b2` gets `cherimoya expected a binary digit (0 or 1) after '0b', got '2' instead`. An unclosed string or char literal is reported at its opening quote. An unknown character gets `cherimoya expected a name, a literal, an operator, or punctuation, got <it> instead`. Each error is shown with its line and the span underlined in carets.

## Why

The parser wants to think in words, a `let` keyword, a name, a `<-` flow token, but a source file is a flat run of bytes. The lexer turns that into labeled tokens so every later pass works on a structured stream instead of characters. Since every diagnostic the compiler will ever emit points at source, the location model and the diagnostic forms are settled here too.

- **Byte offsets as locations.** Line and column are wasteful to store and costly to compute for every token. A byte offset is one `size_t`, and only diagnostics ever need it turned into a line and column, so that's done lazily.
- **Tokens peek into the source.** Token text is a `string_view` into the source buffer, not a copy, so the buffer has to outlive every token taken from it, the same owner-outlives-peek discipline the language enforces.
- **Binary mode.** The file is read in binary so CRLF isn't translated and offsets match the bytes on disk on Windows.
- **A small keyword list.** Everything people expect to be a keyword is either a function (`size[T]()`), a sigil (`@extern`), or not in the language. `if` stays a name on purpose, so the parser can see `if (` and point the user at `match` instead of failing on an unknown keyword.
- **Type spellings in the keyword table.** Malformed spellings are just absent from the table, so they lex as names for a later pass to reject. Teaching the lexer to validate suffix orders would put type-system knowledge in the wrong layer.
- **`true` and `false` are keywords.** They're keywords as tokens and literal values in the tree, and the parser bridges the two.
- **`_` is its own token.** It means discard, uninitialized, and wildcard in three places, none of which is a name.
- **Raw literal text.** The lexer doesn't know the target type, and parsing into a 64-bit host integer would silently truncate `i128` literals, so the value is parsed later, with the width in hand.
- **One dot guard.** The `.` after a number is consumed only when a digit of the number's base follows it. One character of lookahead resolves `3.14`, `1.foo`, `1.`, and `1...`, and without leading-dot floats the dot never conflicts with member access.
- **A whole bad word is one error.** A letter or digit right after a number is one its base doesn't have, so it belongs to the same mistake: `123abc` is one error, not a number and then a stray name.
- **An operator table, not a switch.** There are close to a hundred extra operators, and every overloadable binary operator gets a compound form, so a hand-written switch per character doesn't survive. The table is the single source of truth, and the parser reads levels and associativity straight from the entry each token carries, so moving an operator is a one-line edit.
- **Extra operators.** They're a fixed set of operator tokens with no built-in meaning, for programs to define. Programs can't invent new operators; they pick from these.
- **Kinds only where grammar needs them.** Built-ins carry their own kinds (`LArrow`, `Yield`, `PipeFwd`, `Shl`, `NeEq`, ...) so the parser can switch on them. Every extra operator is `ExtraOp` and every compound is `CompoundAssign`, since what tells them apart is data, not grammar.
- **Unary minus isn't a token.** `-5` is `Minus IntLiteral`, and whether the minus is prefix or binary is the parser's call.
- **The spacing rule.** Munch decides how touching operators split, but the writer might have meant the other split, so the lexer refuses to guess where a guess matters. It only looks at operators touching operators: letters, digits, brackets, commas, dots, and colons can't merge into an operator, so `x<-1` is an assignment, `(<+>, 0)[xs]` is a fold, and `[x::1.0]` is a record, and none of them is an error. The flip side is that `[0, ..., <-1]` isn't a lexer error: it lexes a flow token, which the parser rejects, and a negative end is written `< -1`.
- **Never abort.** A malformed token becomes an `Error` token and lexing goes on, so one run reports every mistake.
- **Three diagnostic forms.** Every diagnostic, from the lexer on, takes one of three shapes. Expected something else: `cherimoya expected <what> <where>, got <other> instead`. A known misunderstanding from another language's habits: `cherimoya uses <x> for <y> instead of <z> as follows: <example>`. A broken rule: `cherimoya requires <rule>, please <fix>`.

## Implementation

### Source and Diagnostics

`src/source/source.hpp` defines `SourceLoc`, a byte offset, `LineCol`, a 1-based line and column, and `SourceManager`, which owns the code. `loadFile` reads in binary mode and leaves no code when the path can't be read, `loadString` loads a string for tests, and `text` hands out a view. `getLineCol` counts newlines up to an offset, and `getLineN` returns one line.

`src/diagnostics/diagnostics.hpp` defines `Span`, a half-open pair of `SourceLoc`s, `Diagnostic`, a message and a span, and `DiagnosticEngine`, which holds the `SourceManager` by reference and every diagnostic so far. `printError` records one and prints it, and `getErrorN` formats it: `error at line N:` or `error at lines N-M:`, each line the span covers with carets under its part, keeping tabs so the carets line up, then the message. A span that ends with a newline is placed on the line of its last character.

### Tokens and Keywords

`src/lexer/token.hpp` defines `TokenKind` and `Token`, which holds its kind, its text as a `string_view`, its `SourceLoc`, and `op`, a pointer to its operator table entry, null for anything that isn't an operator. `src/lexer/keywords.hpp` has `keywordTable`, mapping each keyword to its kind and each built-in type spelling to `TypeName`, and `lookupKeyword`.

### Operator Table

`src/lexer/operators.hpp` defines `OpInfo`: its spelling, `kind`, `category` (`OpCategory`: flow, arithmetic, bitwise, logical, comparison, equality, pipe, compound, extra), `isOverloadable`, its binary `level` and `prefixLevel` on the 17-level ladder (1 tightest, 0 meaning no such form), `assoc`, and for a compound its `base`. `Assoc` has `None`, `Left`, `Right`, and `Chain`, the last for the built-in comparisons, since `a < b <= c` is neither left- nor right-associative but one node. `-` is the one operator with both forms, level 5 binary and level 3 prefix. Construction's `<-` sits at level 1.

`operatorTable` builds a `std::deque` once, in three passes, so entries keep their addresses:

1. **Hand-listed entries.** The built-ins, then the named extra operators: the doubles and triples that don't already mean something, `** *** // %% ++ +++ -- <<< >>> &&& ^^^ |||`, each triple on its double's level, `<=>` with the comparisons but `None` so it never chains, and the combinators `>>= =<< <$> <~> ~> <~` at level 15.
2. **Wrapped extra operators.** Every single-symbol binary operator inside each of eight wrappers, `<>`, `<<`, `>>`, `||`, `|>`, `<|`, `|<`, `>|`. A wrapped operator takes its inside's level and associativity, so `<+>` adds and `<*>` multiplies, except that a wrapped comparison like `<<>` never chains. Anything containing `<-`, `->`, or `-|`, or already named, is skipped. No extra operator is longer than three symbols, and no operator contains a bracket.
3. **Compounds.** Every overloadable binary operator from two `T`s to a `T` gets `X<-` pointing back at its base: the arithmetic and bitwise built-ins, the doubles and triples, and wrapped arithmetic or bitwise operators. That's where `+<-`, `<<<-`, `>><-`, `**<-`, and `<+><-` come from. Comparisons and equality give a `bool`, so there's no `==<-` or `<=><-`, and the combinators don't fit the shape.

`operatorLookupTable` maps spellings to entries, `maxOperatorLength` is where munch starts, `lookupOperator` finds one entry, and `needsSpacing` says which categories the spacing rule covers.

### Lexer

`src/lexer/lexer.hpp` is the `Lexer`, built from a `DiagnosticEngine`. `lex` skips whitespace and comments with `skipWhitespaceAndComments`, then picks a scanner by the first character:

- **`scanIdentOrKeyword`** reads the whole word, then makes it `Underscore`, a keyword, or an `Identifier`.
- **`scanNumber`** reads a base prefix, then digits and separators, and takes a `.` only when a digit of the base follows. A missing digit or a word character right after the number takes the rest of the word into one `Error` token and one diagnostic.
- **`scanQuoted`** reads a string or char literal to its closing quote, skipping `\` escapes, and reports an unclosed one at its opening quote.
- **`scanOperatorOrError`** tries every length from `maxOperatorLength` down to one and takes the first hit, then falls back to punctuation. Anything else is an `Error` token, and a non-ASCII character takes its continuation bytes with it, so it's one token and one diagnostic, not one per byte.

Every path advances the position, so an unrecognized character can't loop forever. After the stream is finished, `checkSpacing` reports each touching pair once, spanning both tokens, and `lex` appends an `Eof` token.

### Driver and Build

`tools/cheri/main.cpp` loads a file, lexes it, prints each token, and exits non-zero with an error count if any diagnostic was reported. `CMakeLists.txt` builds `cheri` as C++17 with warnings on and fetches Catch2 for `cheri_tests`. `scripts/build.sh`, `scripts/test.sh`, and `scripts/clean.sh` configure and build, run the tests through `ctest`, and remove the build directory.

## Tests

`tests/unit/lexer/test_lexer.cpp` has 17 Catch2 test cases covering maximal munch, single, double, and triple operators, macro tokens, flow and punctuation, numeric literals in all four bases, the dot guard, strings and `#` comments, error recovery, keyword and type completeness, the precedence ladder in the table, every generated compound lexing as one token, extra operator validation, the spacing rule, touching operators across three classes reporting one diagnostic each, `<-` on a negative range end, and golden sequences of a whole function and a macro. Each test lexes with its own `SourceManager` kept in a `std::deque`, so the managers outlive the tokens that point into them.

The extra operator test splits every extra operator into built-ins and checks whether a split could be a valid fragment, a binary operator followed only by prefix operators. Exactly two have one: `a--b` could be `a - -b`, and `a<~b` could be `a < ~b`. Munch always picks the extra operator, and the other reading is a space away. The test pins that list, so a new extra operator with a hidden second reading fails the build. 2205 assertions, 0 failures.

## Current Limitations

- **Only tokens.** Nothing reads the stream yet. The driver just dumps it, until the parser builds a tree from it.
- **Macros.** `macro`, `moya`, and `$` are reserved, but nothing gives them meaning.
- **Literal values.** Numbers, strings, and chars are kept as raw text, until type checking parses them at their target types.
