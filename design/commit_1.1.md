# Commit 1.1: Lexer

## The Problem

The parser wants to think in words: a `let` keyword, an identifier, a `<-` flow token. But a source file is a flat run of bytes. Somebody has to turn that byte soup into labeled tokens, so every later pass works with a structured stream instead of doing character-level surgery. That's the lexer, and it's the only thing commit 1.1 builds.

## Locations

The first question isn't how to scan tokens, it's how to talk about *where* things are. Every diagnostic the compiler ever emits needs to point at source code, so the location model has to be right from the start.

The obvious choice is line and column numbers, but that's wasteful to store and expensive to compute on every token. An integer byte offset into the source buffer is smaller, faster, and sufficient: one `size_t` per token. Line and column can be derived lazily by counting newlines backward from the offset, and in practice only diagnostics ever need that conversion. So `SourceLoc` is just a byte offset, and `Span` is a half-open pair of them marking a range of source text. The `SourceManager` owns the file's text buffer, loads it in binary mode (so CRLF isn't translated and offsets match the disk on Windows), and provides the offset-to-line/col conversion when diagnostics need it.

The important implication: tokens hold `string_view`s that point into the SourceManager's buffer. They're *peeks*, not copies. The SourceManager must outlive every token derived from it, or those views dangle. This is, fittingly, the same owner-outlives-peek discipline the language itself enforces. It bit the test harness once already: a helper returned tokens after its local SourceManager died. Dangling views, crash, lesson learned.

## Keywords and Types

Identifiers and keywords share the same shape: a run of alphanumeric characters. The lexer scans the run first, then looks it up in a keyword table. If the table contains it, the token gets the keyword's kind. If not, it's an `Identifier`. A run that is exactly `_` is neither: it's its own `Underscore` token, because it means discard, uninitialized, and wildcard in three different places, none of which is a name. `_x` is still an ordinary identifier.

The keyword list is deliberately small: `let type export import`, the scopes `match repeat chunk`, the qualifiers `unscoped peek mut ptr arr`, the place intrinsics `take swap replace`, the verification words `wit assert old`, `true false`, and `macro moya` for later. Everything else people expect to be a keyword is either a function (`size[T]()`), a sigil (`@extern`), or not in the language. In particular there is no `if`. It lexes as an identifier on purpose, so the parser can see `if (` and point the user at `match` instead of failing on an unknown keyword.

The same table handles all built-in type spellings. Every valid form (`i8` through `i128uw`, the floats, `int`, `float`, `bool`, `char`, `rune`, `boolf`) maps to `TokenKind::TypeName`. Floats come in pairs: a NaN-free type and a NaN-carrying `n` twin, across `f8` (which is `f8e4m3`), `f8e5m2`, `f16`, `f16e8m7`, `f32`, `f64`, and `f128`. Malformed forms like `i64wu` or `f8n` are simply absent from the table, so they lex as `Identifier` and are left for a later stage to reject. The alternative, teaching the lexer to validate suffix orderings, would scatter type-system knowledge into the wrong layer.

`true` and `false` are in the keyword table as `KwTrue` and `KwFalse`. They're keywords at the token level, literal values at the AST level. The parser bridges the gap.

## Numbers

Numeric literals support four bases (decimal, `0x` hex, `0o` octal, `0b` binary), underscore separators anywhere in a digit run (permissive, stripped later), and fractional parts in every base. The one subtle case is the dot: `3.14` should be a single `FloatLiteral`, but `1.foo` should be `IntLiteral Dot Identifier`, and `1.` should be `IntLiteral Dot`. The guard is simple: consume the `.` only if a base-appropriate digit follows it. One lookahead character resolves every case, including `1...`, which is an integer followed by an ellipsis.

A letter or digit right after a number is one its base doesn't have, so it's part of the same mistake. The scanner takes the rest of the word into one Error token and reports it once: `0o78` gets `cherimoya expected an octal digit (0 to 7), got '8' instead`, and `123abc` is one error, not a number and then a stray name.

Literal text is kept raw, never parsed to a host integer. The lexer doesn't know the target type, and parsing to a 64-bit host value would silently truncate `i128` literals. The actual value parse happens later in the type checker, with the target width in hand.

Leading-dot floats like `.5` are not supported. Write `0.5`. This keeps the lexer's dot handling unambiguous and avoids a grammar conflict with member access.

## Comments

Comments start with `#` and run to the end of the line. There are no block comments. That frees `//` to be an operator, and it means a comment can never swallow code by accident.

## The Operator Table

A hand-written switch per first character doesn't survive contact with the extra operators: there are close to a hundred of them, and every overloadable operator also gets a compound form. So operators live in one table, `src/lexer/operators.hpp`, and it is the single source of truth. Each entry records its spelling, its token kind, a category (flow, arithmetic, bitwise, logical, comparison, equality, pipe, compound, extra), whether it's overloadable, its binary level, its prefix level, its associativity, and for compounds the base operator. The parser reads levels and associativity straight from the entry hanging off each token, so moving an extra operator is a one-line edit.

The table is built in three passes. First, the hand-listed entries: the built-in operators with their place on the 17-level ladder, then the named extra operators. Second, the wrapped extra operators are generated. Third, the compounds are generated from everything before them.

Built-ins carry their own kinds (`LArrow`, `Yield`, `PipeFwd`, `Shl`, `NeEq`, ...), so the parser can switch on them. Every extra operator shares `ExtraOp` and every compound shares `CompoundAssign`, since what distinguishes them is data, not grammar. `-` is the one operator with two forms: level 5 as a binary operator and level 3 as a prefix, so entries hold both levels. Built-in comparisons get a fourth associativity, `Chain`, because `a < b <= c` is neither left nor right associative. It's one node.

## Extra Operators

The extra operators are a fixed set of operator tokens with no built-in meaning, for users to define. Users can't invent new operators. They pick from the extra ones.

The named extra operators are the doubles and triples that don't already mean something: `** *** // %% ++ +++ -- <<< >>> &&& ^^^ |||`. A triple sits on the same level as the double it extends, so `&&&` is with `&&` and `<<<` is with `<<`. `<=>` sits with the comparisons but never chains. The combinators `>>= =<< <$> <~> ~> <~` share level 15.

The rest is generated: every single-symbol binary operator wrapped in one of eight wrappers, `<>`, `<<`, `>>`, `||`, `|>`, `<|`, `|<`, `>|`. A wrapped token takes the level and associativity of whatever is inside it, so `<+>` adds and `<*>` multiplies. A wrapped comparison like `<<>` never chains. Generation skips anything that contains `<-`, `->`, or `-|`, and anything already named. No extra operator is longer than three symbols, and no operator anywhere contains a bracket.

Some extra operators could also be read as two built-ins. A test splits every extra operator into built-in operators and checks whether the split could be a valid fragment: a binary operator followed only by prefix operators. Exactly two tokens have one. `a--b` could mean `a - -b`, and `a<~b` could mean `a < ~b`. Maximal munch always picks the extra operator, and the other reading is a space away: `a - -b` and `a < ~b` lex as written. The test pins that list, so a new extra operator with a hidden second reading fails the build instead of surprising someone.

## Compounds

Compound assignment isn't hand-listed either. Every overloadable binary operator that takes two `T`s and returns a `T` gets `X<-`: the arithmetic and bitwise built-ins, the doubles and triples, and wrapped tokens whose inside is arithmetic or bitwise. That's where `+<-`, `<<<-`, `>><-`, `**<-`, and `<+><-` come from. Comparisons and equality return `bool`, so there is no `==<-` or `<=><-`. The combinators don't fit the shape either. Each compound points back at its base, so the parser never has to take a spelling apart.

## Maximal Munch

Maximal munch is the scanning rule, and with a table it's mechanical. Starting from the longest spelling in the table, try every length down to one and take the first hit. `<<<-` before `<<<` before `<<` before `<-` before `<`. Anything the table doesn't know falls through to punctuation: `( ) [ ] { } , ; . ... : :: @ $`. There is no `..`: ranges are packs with `...` in them, so `..` lexes as two dots and the parser rejects it.

Unary minus is not a separate token. `-5` lexes as `Minus IntLiteral`. Whether the minus is unary or binary is the parser's call, and it handles it naturally via the NUD/LED split.

## Spacing

Munch decides how touching operators split, but the writer might have meant the other split. The spacing rule makes the lexer refuse to guess where the guess matters: when two operator tokens touch and either one is a flow token, a pipe, a compound, or an extra operator, that's an error. `a**-b` is rejected, and `a ** -b` is fine.

The rule only looks at operators touching operators. Letters, digits, brackets, commas, dots, and colons can't merge into an operator, so they never need a space. `x<-1` is an assignment, `(<+>, 0)[xs]` is a fold, `[x::1.0]` is a record, and none of them is an error. Spaces between operators are always legal. The check runs once over the finished token stream and reports one diagnostic per touching pair, spanning both tokens.

The flip side is that `[0, ..., <-1]` isn't a lexer error. It lexes as a flow token, and the parser rejects it. An exclusivity marker on a negative end is written `< -1`.

## Error Recovery

The lexer never aborts. A malformed token emits `TokenKind::Error` through the `DiagnosticEngine` and lexing continues, so a single run can surface multiple errors. The critical invariant is that every path through the scanner advances the position. Without it, an unrecognized character would infinite-loop. The `default` case in the operator scanner must emit an Error token for the character and advance past it, or everything breaks. A character outside ASCII is several bytes, and it's one Error token and one diagnostic, not one per byte. A string or char literal that's never closed is reported at its opening quote.

Diagnostics are span-based with multi-line caret rendering. The `DiagnosticEngine` holds the `SourceManager` by reference (one shared source of truth, no copies) and renders each error with the source line and a caret pointing at the relevant span.

Every diagnostic, from the lexer on, is written in one of three forms:

- **Expected something else**: `cherimoya expected <what> <where>, got <other> instead`, naming the construct's shape where it helps. `0b2` gets `cherimoya expected a binary digit (0 or 1) after '0b', got '2' instead`.
- **A known misunderstanding**: `cherimoya uses <x> for <y> instead of <z> as follows: <example>`, for mistakes that come from another language's habits.
- **A broken rule**: `cherimoya requires <rule>, please <fix>`. `a**-b` gets `cherimoya requires whitespace between the operators '**' and '-', please write '** -'`.

## What Was Built

`src/source/source.hpp` defines `SourceLoc`, `LineCol`, and `SourceManager`. `src/diagnostics/diagnostics.hpp` provides `Span`, `Diagnostic`, and `DiagnosticEngine`. `src/lexer/token.hpp` has the `TokenKind` enum and the `Token` struct, which carries a pointer to its operator table entry. `src/lexer/keywords.hpp` contains the keyword and type lookup table. `src/lexer/operators.hpp` is the operator table: 28 built-ins, 96 extra operators, and 78 generated compounds. `src/lexer/lexer.hpp` is the `Lexer` class itself. `tools/cheri/main.cpp` is the driver: load file, lex, dump tokens, exit non-zero on errors.

`tests/unit/lexer/test_lexer.cpp` has seventeen Catch2 test cases covering maximal munch, single/double/triple operators, macro tokens, flow and punctuation, numeric literals in all four bases, the dot guard, strings and `#` comments, error recovery, keyword and type completeness, the precedence ladder in the table, every generated compound lexing as one token, extra operator validation, the spacing rule, operators touching across three classes, `<-` on a negative range end, and golden sequences of a whole function and a macro. 2205 assertions, 0 failures.

## What's Next

Commit 2 turns the flat token stream into an expression tree with a Pratt parser that reads its levels from the operator table.
