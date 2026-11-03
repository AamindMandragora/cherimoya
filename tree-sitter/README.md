# tree-sitter-cherimoya

A [tree-sitter](https://tree-sitter.github.io) grammar for cherimoya, which editors like Zed, Neovim, and Helix use to color, indent, and outline `.moya` and `.toya` files. It reads every program the way `cheri` does: it accepts what cheri's lexer and parser accept, rejects what they reject, and builds the tree cheri's parser builds, so a query written against it sees what the compiler sees.

## How It Follows cheri

`grammar.js` builds the operator table the way `src/lexer/operators.hpp` does, in the same order and with the same levels and associativity, so moving an operator there means moving it here the same way. Each level of the table is its own rule, which is how the grammar mirrors cheri's needsParens: operators on one level only sit next to each other without parentheses when they group the same way, so `a = b = c` and `a >>= b =<< c` are errors, and the built-in comparisons chain into one `chainExpression`, as cheri's ChainExpr does.

Cheri's parser reads an expression differently depending on where it sits, so the grammar keeps a copy of the expression rules for each place: nested inside brackets, where `T <- [pack]` is construction, at the top of a statement, a pattern, or a comprehension clause, where `<-` ends the expression and `( ) ->` isn't a closure, at the very start of a statement, which can't begin with a scope, and in a generic argument, which is a type whenever it starts like one.

`src/scanner.c` keeps the lexing honest where tree-sitter's own lexer can't. That lexer only tries the tokens the parser can take next, so where cheri's maximal munch reads an operator the parser can't take, it would read shorter ones instead, `f(x) <- 1` as `f(x) < -1` and `--a` as `- -a`. The scanner builds the same operator table, lexes the few operators that can split that way itself, and reports the spacing rule, so `a**-b` is an error and `a ** -b` isn't. It also reports `if (` wherever an expression starts, so the editor points at `match` the way cheri does, and lexes an empty token after an attribute's name when no `(` follows it, since cheri always reads that `(` as the attribute's arguments. The rest of the operators stay with tree-sitter's lexer, since tree-sitter only lets two parse states share a lexer when they agree on every external token, and making all 213 operators external made the parser twelve times bigger.

## Building and Testing

The generated parser, `src/parser.c` and what's beside it, is checked in, since Zed builds it straight from the repository. After changing `grammar.js`:

```
cd tree-sitter
npm install
npm run generate
npm test
```

`npm test` runs the corpus in `test/corpus/`, every construct with the tree it should have and every mistake cheri reports. `scripts/grammar_test.sh` also parses every program under `tests/` and fails on any tree with an ERROR or MISSING node, and ctest runs it as `Grammar: tree-sitter`, skipping it when `node_modules` isn't installed.

## Where It Differs

The corpus, the inputs of cheri's own parser tests, and tens of thousands of programs mutated from both agree with cheri on which programs have an error, and thousands of random operator expressions also agree with it on their trees, down to each operator. What's left is below the level of a grammar:

- A string or char literal holding bytes that aren't UTF-8 parses without an error, since tree-sitter decodes the file itself and hands the grammar a stand-in for a bad byte that it can't tell from a character.
- A byte-order mark at the start of a file is skipped, where cheri reports it.
