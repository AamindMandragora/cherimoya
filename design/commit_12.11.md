# Commit 12.11: Feature Tests

## The Feature

- **Three tiers of tests.** The suite is ordered so a feature test only runs once what it leans on is known to work:

| Tier | Where | What it checks |
|---|---|---|
| unit tests | `tests/unit/` | the lexer, parser, sema, runtime, and language server, in C++ |
| runtime basics | `tests/run/`, `tests/test/` | what only a whole program can show, and `cheri test` itself |
| feature tests | `tests/features/` | every part of the language, one test per behavior |

- **Feature tests.** One `.toya` file per part of the language, integers, conversions, floats, FP8, characters, `boolf`, logic, control, functions, and arrays, each check its own test asserting the value it should have. `arrays.moya`'s single exit code of 91 is seventeen tests, from `nestedWrite` to `indexPastEnd`.
- **Keywords named as keywords.** Naming a function `wit` is one error, from the parser, which names a keyword as one: `cherimoya expected a function name (an identifier or an overloadable operator) in 'let (inputs) -> name -> (outputs) { body }', got the keyword 'wit' instead`. Every error that names a token does the same. `and`, `or`, and `not` aren't keywords, so they're names like any other.

## Why

Codegen is tested through whole programs, and a whole program says one thing: its exit code. A program that checks fifteen things folds them into one number, `arrays.moya` into 91, so when it fails, the number says something broke but not what. Commit 12.10 gave cherimoya tests of its own, which name every check, so the language's own tests move onto them, keeping as programs only what a test can't check.

- **The basics are checked from outside.** A feature test checks itself: it asserts with `=`, and runs under `cheri test`. A bug in `=`, in `assert`, or in the test runner could make every one of them pass without checking anything, or fail all at once, so the first two tiers check those by exit codes and by the exact output `cheri test` prints.
- **Some programs stay programs.** A test has no `start` and no exit code, and runs under the test runtime's entry point instead of a program's, so `tests/run/` keeps the programs that check what a test can't:

| Program | What it checks |
|---|---|
| `entry_start` | the entry point is the `start` at the top of the file, not one nested in a block |
| `void_call` | void functions called as statements, returning early and yielding a void call |
| `globals`, `toplevel_loop` | top-level bindings, computed before `start` and changed by functions, and a top-level loop's binding starting over each time round |
| `early_exit`, `repeat_exit` | leaving a function from a nested block, and a loop yielding its value or leaving an outer loop |
| `assert_pass`, `assert_fail` | an assert that holds goes on, and one that doesn't traps with its line |
| `int_overflow` | a trap's message, seen from outside |

- **Traps match from the quoted expression on.** A trap is a test with `traps`, matching the message without its line, `@test(traps <- "'a / b' divided by zero")`, so a test can move in its file without breaking.
- **Both optimization levels,** since `-O0` and `-O3` can disagree.
- **One error per mistake.** Naming a test `wit` turned up two errors for one mistake: the parser's, and sema's, which took any name that wasn't an identifier for an operator and called it overloading. The parser's is the one that says what's wrong, and naming the keyword as one explains why something that looks like a name didn't work.

## Implementation

### Parser

`describe` in `src/parser/parser.hpp` prefixes `the keyword ` when a token's text is in `keywordTable`, isn't a type name, and lexed as that keyword.

### Sema

`hoist` in `src/sema/sema.hpp` reports `operator overloading` as unsupported only when the function's name is an operator, `fn.name.op`, since the parser already reported anything else.

### Build

`CMakeLists.txt` marks every unit test, every program in `tests/run/`, and `Test: cheri test` with `FIXTURES_SETUP basics`, and adds each file in `tests/features/` as `Feature: <name>`, `Feature: arrays` for instance, with `FIXTURES_REQUIRED basics`, so ctest leaves a feature test unrun when a fixture it requires fails to set up. `scripts/feature_test.sh` runs a file with `cheri test` at `-O0` and then `-O3`, failing on the first level where a test doesn't pass. The README describes the three tiers.

## Tests

`tests/features/` holds 155 tests in 10 files, converted from 55 programs in `tests/run/`, which keeps 9. `tests/unit/sema/test_sema.cpp` checks that a keyword as a function's name is one error naming the keyword, that an operator's name is still unsupported overloading, and that `and`, `or`, and `not` are plain names. 14573 unit assertions, 9 programs, the run of `tests/test/`, 155 feature tests at both levels, 6 debug programs, and the grammar's tests, 0 failures, and a failing program in `tests/run/` leaves every feature test unrun.

## Current Limitations

- **Slow single runs.** Running one feature test from ctest also runs every test it requires first, all of the unit tests and the basics, so running a file directly with `cheri test` is quicker while working on it.
- **No infinity literal.** A check that a value is infinite still compares it against the largest finite value, until there's an infinity literal.
