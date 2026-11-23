# Commit 12.7: A Tree-sitter Grammar

## The Feature

- **A tree-sitter grammar.** `tree-sitter/` holds a grammar for `.moya` and `.toya` files that any editor reading tree-sitter can use, Zed, Neovim, and Helix among them, to color, indent, outline, and match brackets.
- **It reads programs the way cheri does.** It accepts what cheri's lexer and parser accept, rejects what they reject, and builds the tree cheri's parser builds, so a query written against it sees what the compiler sees, and a mistake shows up in the editor the moment cheri would report it.
- **A missed mix of operators.** `a $ b + c >>= d` is now an error, as `a $ b >>= d` already was, since `$` groups to the right and `>>=` to the left. Its two fixes are the same as for any mix, `(a $ b + c) >>= d` and `a $ (b + c >>= d)`.

## Why

Cheri's language server colors a program by what it means, but editors like Zed read code through tree-sitter: they color, indent, outline, and match brackets from a grammar's tree, and Zed won't define a language without one. A grammar that agrees with cheri exactly means an editor never shows a program as fine that cheri rejects, or the other way around.

- **One rule per level of the operator table.** `grammar.js` builds the table the way `src/lexer/operators.hpp` does, in the same order with the same levels and associativity, so the two move together. Operators on one level only sit next to each other without parentheses when they group the same way, which is how cheri's `needsParens` reads a mix.
- **One copy of the expression rules per place.** Cheri reads an expression differently depending on where it sits, so the grammar keeps a family of rules for each: nested inside brackets, where `T <- [pack]` is construction, at the top of a statement, a pattern, or a clause, where `<-` ends the expression, at the very start of a statement, where a scope is a statement of its own, and in a generic argument, read as a type whenever it starts like one.
- **Every keyword and type spelling is reserved,** since otherwise tree-sitter would read one as a name wherever the keyword can't go.
- **An external scanner for the operators that split.** Tree-sitter's own lexer only tries the tokens the parser can take next, so where cheri's maximal munch reads an operator the parser can't take, it would read shorter ones: `f(x) <- 1` as `f(x) < -1`, and `--a` as `- -a`. Only those operators are external, since tree-sitter only lets two parse states share a lexer when they agree on every external token, and making all 213 operators external made the parser twelve times bigger.
- **The generated parser is checked in,** since an editor builds the grammar straight from the repository at a commit.
- **The parser fix** came from writing the grammar against cheri's parser: `needsParens` compared an operator only with the last one at its depth, and in `a $ b + c >>= d` the `+` took the `$`'s place.

## Implementation

### Tree-sitter

- **`tree-sitter/grammar.js`.** `operatorTable` builds the table, and `SPLITTABLE_OPERATORS` picks every operator that starts with another one followed by `-`, `!`, or `~`, the characters a prefix operator starts with, like `<-`, `--`, and `<<<-`. `levelOperators` splits a level's binary operators by associativity and into built-ins and extras, since cheri builds a `BinaryExpr` for one and an `ExtraBinaryExpr` for the other. `FAMILIES` lists the four places, `go`, `stop`, `lead`, and `argument`, each leaving out some of the `atoms` an expression can start with, and `expressionRules` builds every postfix, prefix, binary, and pipe rule for one family. `reserved` holds `KEYWORDS`, `BUILTIN_TYPES`, and `_`, and `conflicts` lists the places cheri decides by looking ahead, such as a pattern and an expression that look the same until a `<-` or `...` settles it.
- **`tree-sitter/src/scanner.c`.** `buildTable` builds the same table of 213 operators, 28 built-ins, 104 extras, and 81 compounds, per scanner, since editors make parsers on several threads at once. `tree_sitter_cherimoya_external_scanner_scan` reads the run of operator characters and `munch` ends the token at the longest operator it starts with. It lexes a splittable operator itself, as `InvalidOperator` when the parser can't take it, and leaves the rest to tree-sitter's lexer. It also does three more jobs: it reports the spacing rule, through `needsSpacing`, so `a**-b` is an error and `a ** -b` isn't; it reports `if (` wherever an expression starts, through `isIfCall`, so the editor points at `match` the way cheri does; and it lexes the empty `NoArguments` token after an attribute's name when no `(` follows, since cheri always reads that `(` as the attribute's arguments. The scanner measures and compares spellings itself rather than pulling in a libc that a WebAssembly build might not have.
- **Generated files.** `src/parser.c`, `src/grammar.json`, `src/node-types.json`, and `src/tree_sitter/` are checked in. `package.json` pins tree-sitter-cli at 0.27.1 and gives `generate` and `test` scripts, and `tree-sitter.json` names the grammar and its file types.

### Parser

`parseExpr` in `src/parser/parser.hpp` takes `parentStart`, where the parent whose right side it's parsing begins, and each binary operator and pipe passes its own start. Inside the right side of a right-associative operator, an operator on its level can follow a tighter one, so when `needsParens` doesn't hold with the previous operator but does with `parent`, the mix is with the parent. The left side of the first fix then runs from `parentStart`, and the last operand for the second fix is everything at this depth.

### Build

`scripts/grammar_test.sh` parses every `.moya` and `.toya` file under `tests/` with the grammar and fails naming every file whose tree holds an ERROR or MISSING node, then runs the corpus. `CMakeLists.txt` adds it to ctest as `Grammar: tree-sitter`, which skips when `tree-sitter/node_modules` isn't installed. The README lists `tree-sitter/` in its layout, and `tree-sitter/README.md` says how to regenerate and test the grammar.

## Tests

`tree-sitter/test/corpus/` checks every construct with the tree it should have, and every mistake cheri reports as an error. `tests/unit/parser/test_parser.cpp` checks the mix through a tighter operator, and `tests/unit/sema/test_sema.cpp` checks both of its fixes. The grammar was also checked against cheri directly, by a parse-only checker built from cheri's lexer and parser:

- **Parser inputs.** The inputs of `test_parser.cpp`.
- **Mutated programs.** Tens of thousands of programs mutated from the corpus and the tests, which agree with cheri on which have an error.
- **Random expressions.** Thousands of random operator expressions, which agree with it on their trees, down to each operator.

14569 unit assertions, 64 programs, 6 debug programs, the run of `tests/test/`, 163 corpus tests, and every program under `tests/` parsed, 0 failures.

## Current Limitations

- **Bad UTF-8.** A string or char literal holding bytes that aren't UTF-8 parses without an error, since tree-sitter decodes the file itself and hands the grammar a stand-in it can't tell from a character.
- **Byte-order marks.** One at the start of a file is skipped, where cheri reports it.
- **No user yet.** Nothing in the repository uses the grammar until there is a Zed extension.
