# Commit 1.2: No Macros, and Protocols

## The Feature

- **No macro keywords.** `macro` and `moya` lex as ordinary names.
- **`$` is an extra operator.** Like `<$>`, it has no meaning until a program gives it one. It sits at level 15 with the combinators and is right-associative, so `f $ g $ x` is `f $ (g $ x)`. Like every single-symbol operator, it also comes wrapped, as `|$|`, `<$<`, and the rest.
- **`@` stays.** It's the attribute token.
- **Protocols.** A type definition whose right side is a pack of signatures, like `type Ord[T] <- [let (a :: peek T, b :: peek T) -> cmp -> (r :: int)];`, is a protocol: a set of operations any number of types can each provide, which `wit {T :: Ord}` says a type follows.

## Why

Cherimoya gets what macros are for from compile-time evaluation instead. A type is a value known while compiling, so generics are ordinary functions the compiler runs, and a function that takes a string and parses it while compiling, like `regex("a+b")`, checks an embedded language and reports its mistakes on the right line, with no new syntax and none of text replacement's pitfalls. So nothing needs `macro`, `moya`, or a splice token.

- **Freeing the names.** Without macros, reserving `macro` and `moya` would only take two names away from programs.
- **`$` as application.** Right associativity is how application reads in the languages `$` comes from, and as an extra operator it costs nothing until a program defines it.
- **The name "protocol".** It says what the definition is, a set of operations a type follows, and reads naturally in `wit {T :: Ord}`.

## Implementation

### Lexer

`src/lexer/token.hpp` drops `KwMacro`, `KwMoya`, and `Dollar`, so the keyword kinds end at `KwFalse` and `At` is the attribute token on its own. `src/lexer/keywords.hpp` drops `macro` and `moya` from `keywordTable`, and `scanOperatorOrError` in `src/lexer/lexer.hpp` no longer has a `$` case. `src/lexer/operators.hpp` adds `$` to `operatorTable` as an `ExtraOp` at level 15, `Assoc::Right`, with no compound. Since it's a single-symbol binary operator, the wrapping pass generates its wrapped forms.

### Parser

`TypeForm` in `src/parser/decl.hpp` names the signature form `Protocol`, and `parseTypeDecl` in `src/parser/parser.hpp` sets it when the right side opens with `[ let`, reporting a missing bracket as `cherimoya expected ']' after the protocol's signatures in 'type Name <- [let ..., let ...];', got <token> instead`. The comments on `FnDecl`, `TypeDecl`, `WitStmt`, and `parseWit` call it a protocol too.

### Language Server

`semanticTokens` in `src/lsp/features.hpp` colors every kind from `KwLet` through `KwFalse` as a keyword, and `keywordHelp` has no entries for `macro` and `moya`.

## Tests

`tests/unit/lexer/test_lexer.cpp` checks that `macro` and `moya` lex as names, that `f $ x` is a name, an `ExtraOp`, and a name, that `$` is right-associative, and that `@` is still the attribute token. The macro declaration test is gone. The parser and printer tests dump and print the signature form as `protocol`.

## Current Limitations

- **Protocols are only syntax.** A protocol parses, but checking that a type follows one waits until protocols can be written.
- **`$` has no meaning.** Like every extra operator, it does nothing until operator overloading lets a program define it.
