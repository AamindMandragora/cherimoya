# Commit 7.6: A Tree-sitter Grammar

## The Problem

Cheri's language server colors a program by what it means, but editors like Zed, Neovim, and Helix read code through tree-sitter: they color, indent, outline, and match brackets from a grammar's tree, and Zed won't define a language without one. Commit 7.6 gives cherimoya a tree-sitter grammar that reads programs exactly the way cheri does, and fixes the one place where writing it showed cheri's parser letting a mistake through.

## The Grammar

`tree-sitter/` holds the grammar, and any editor that reads tree-sitter can use it. It's meant to accept what cheri accepts and reject what it rejects, and to build the same tree. A query written against it then sees what the compiler sees, and a mistake shows up in the editor the moment cheri would report it. `grammar.js` builds the operator table the way `src/lexer/operators.hpp` does, with each level as its own rule. Operators on one level only sit next to each other without parentheses when they group the same way, which is cheri's needsParens.

Cheri reads an expression differently depending on where it sits, so the grammar keeps one copy of the expression rules for each place:

- **Nested inside brackets**, `T <- [pack]` is construction.
- **At the top of a statement, a pattern, or a clause**, `<-` ends the expression.
- **At the very start of a statement**, a scope is a statement of its own.
- **In a generic argument**, it's read as a type whenever it starts like one.

Every keyword and type spelling is reserved. Otherwise tree-sitter would read one as a name wherever the keyword can't go.

Tree-sitter's own lexer only tries the tokens the parser can take next. So where cheri's maximal munch reads an operator the parser can't take, it would read shorter ones: `f(x) <- 1` as `f(x) < -1`, and `--a` as `- -a`. `src/scanner.c` builds the same operator table and lexes the few operators that can split that way itself. It also handles three more jobs:

- it reports the spacing rule, so `a**-b` is an error;
- it reports `if (` wherever an expression starts;
- it tells an attribute's arguments from the statement after it.

Every other operator stays with tree-sitter's lexer. Tree-sitter only lets two parse states share a lexer when they agree on every external token, so making all 213 operators external made the parser twelve times bigger.

The generated parser, `src/parser.c` and what's beside it, is checked in, since an editor builds the grammar straight from the repository at a commit. tree-sitter-cli is pinned at 0.27.1 in `package.json`.

## Where cheri Changed

Writing the grammar against cheri's parser turned up a mix it let through. `a $ b >>= d` needs parentheses, since `$` groups to the right and `>>=` to the left, but `a $ b + c >>= d` passed. needsParens compared an operator only with the last one at its depth, and the `+` took the `$`'s place. Inside the right side of a right-associative operator, an operator on its level can now follow a tighter one, so each operator is also compared with the parent whose right side it's in. The error and its two fixes are the same as for any mix: `(a $ b + c) >>= d`, which needs to know where the parent's left side starts, so parseExpr gets that with the parent, and `a $ (b + c >>= d)`.

## Testing

`tree-sitter/test/corpus/` checks every construct with the tree it should have, and every mistake cheri reports as an error. `scripts/grammar_test.sh` parses every program under `tests/` and fails on any tree with an ERROR or MISSING node, then runs the corpus. ctest runs it as `Grammar: tree-sitter`, skipping it when `tree-sitter/node_modules` isn't installed. The grammar was also checked against cheri directly, by a parse-only checker built from cheri's lexer and parser:

- the inputs of `test_parser.cpp`;
- tens of thousands of programs mutated from the corpus and the tests, which agree with cheri on which have an error;
- thousands of random operator expressions, which agree with it on their trees, down to each operator.

## What Was Built

`tree-sitter/` holds the grammar, its scanner, its generated parser, its corpus, and a README. `src/parser/parser.hpp` compares an operator with its parent too, which `tests/unit/parser/test_parser.cpp` and the fixes in `tests/unit/sema/test_sema.cpp` check. `scripts/grammar_test.sh` and `CMakeLists.txt` run the grammar's tests, and the README lists the grammar in its layout. 14569 unit assertions, 64 programs, 6 debug programs, the run of `tests/test/`, 163 corpus tests, and every program under `tests/` parsed, 0 failures.

## Current Limitations

A string or char literal holding bytes that aren't UTF-8 parses without an error, since tree-sitter decodes the file itself, and a byte-order mark at the start of a file is skipped, where cheri reports it. Nothing in the repository uses the grammar yet, until the Zed extension in commit 7.7.
