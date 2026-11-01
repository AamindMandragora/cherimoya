# Commit 1.2: No Macros, and Protocols

## The Problem

The lexer reserves two keywords for macros, `macro` and `moya`, and a token for splicing into one, `$`, but nothing past the lexer reads them, and nothing ever will. Cherimoya gets what macros are for from compile-time evaluation instead: a type is a value known while compiling, so generics are ordinary functions run by the compiler, and a function that takes a string and parses it at compile time, like `regex("a+b")`, checks an embedded language and reports its mistakes on the right line, without new syntax and without text replacement's pitfalls. Commit 1.2 takes macros out of the lexer, so `macro` and `moya` are names like any other, and `$` becomes an extra operator. It also names what a type definition made of signatures is: a protocol, which `wit {T :: Ord}` says a type follows.

## What Changed

`macro` and `moya` lex as identifiers. `$` is an extra operator like `<$>`, with no meaning of its own until a program gives it one, at the level of the combinators, 15, and right-associative, so `f $ g $ x` is `f $ (g $ x)`, which is how application reads in the languages it comes from. Like every single-symbol operator, it also comes wrapped, as `|$|` and `<$<` and the rest. `@` stays, since attributes use it.

A type definition whose right side is a pack of signatures, like `type Ord[T] <- [let (a :: peek T, b :: peek T) -> cmp -> (r :: int)];`, is a protocol: a set of operations that any number of types can each provide. The parser's `TypeForm` calls it `Protocol`, and sema reports that protocols wait for a later commit. The language server colors keywords by their token kinds, which now end at `false`, and has no help for the two keywords that are gone.

## What Was Built

`src/lexer/token.hpp` and `src/lexer/keywords.hpp` lost the macro keywords and the `$` token, `src/lexer/operators.hpp` gained `$` as an extra operator, and `src/lsp/features.hpp` dropped the keywords' help. `src/parser/decl.hpp`, `src/parser/parser.hpp`, and `src/sema/sema.hpp` call protocols by name, and so do the parser and printer tests, the README, and the design notes. `tests/unit/lexer/test_lexer.cpp` checks that `macro` and `moya` are names and that `$` is a right-associative extra operator.
