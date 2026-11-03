# Commit 8.4: Feature Tests

## The Problem

Codegen is tested through whole programs, and a whole program says one thing: its exit code. A program that checks fifteen things folds them into one number, `arrays.moya` into 91, so when it fails, the number says something broke but not what. Commit 8.3 gave cherimoya tests of its own, which name every check, so commit 8.4 moves the language's own tests onto them, keeping as programs only what a test can't check, and orders the suite so a feature test only runs once what it leans on is known to work.

## Three Tiers

| Tier | Where | What it checks |
|---|---|---|
| unit tests | `tests/unit/` | the lexer, parser, sema, runtime, and language server, in C++ |
| runtime basics | `tests/run/`, `tests/test/` | what only a whole program can show, and `cheri test` itself |
| feature tests | `tests/features/` | every part of the language, one test per behavior |

A feature test checks itself: it asserts with `=`, and runs under `cheri test`. A bug in `=`, in `assert`, or in the test runner could make every one of them pass without checking anything, or fail all at once, so the first two tiers check those from outside, by exit codes and by the exact output `cheri test` prints. ctest runs the feature tests only once the first two tiers pass: every unit test, every program in `tests/run/`, and `Test: cheri test` set up a fixture, `basics`, which every feature test requires, and ctest leaves a test unrun when a fixture it requires fails to set up.

## The Runtime Basics

`tests/run/` keeps the programs that check what a test can't, since a test has no `start` and no exit code, and runs under the test runtime's entry point instead of a program's:

| Program | What it checks |
|---|---|
| `entry_start` | the entry point is the `start` at the top of the file, not one nested in a block |
| `void_call` | void functions called as statements, returning early and yielding a void call |
| `globals`, `toplevel_loop` | top-level bindings, computed before `start` and changed by functions, and a top-level loop's binding starting over each time round |
| `early_exit`, `repeat_exit` | leaving a function from a nested block, and a loop yielding its value or leaving an outer loop |
| `assert_pass`, `assert_fail` | an assert that holds goes on, and one that doesn't traps with its line |
| `int_overflow` | a trap's message, seen from outside |

## The Feature Tests

Every other program became tests in a `.toya` file, one file per part of the language: integers, conversions, floats, FP8, characters, `boolf`, logic, control, functions, and arrays. Each check a program folded into its exit code is its own test now, asserting the value it should have, so `arrays.moya`'s 91 is seventeen tests, from `nestedWrite` to `indexPastEnd`. A trap is a test with `traps`, matching the message from its quoted expression on, `@test(traps <- "'a / b' divided by zero")`, without its line, so a test can move in its file without breaking. `scripts/feature_test.sh` runs each file with `cheri test` at `-O0` and at `-O3`, since the two can disagree, and ctest runs every file in `tests/features/` as a test of its own, `Feature: arrays`.

## Keywords as Names

Naming a test `wit` turned up two errors for one mistake: the parser's, that a keyword isn't a function name, and sema's, which took any name that wasn't an identifier for an operator and called it overloading. Sema now only says that for an operator, since the parser already reported anything else. The parser's error names a keyword as one, since it looks like a name that should have worked, `cherimoya expected a function name (an identifier or an overloadable operator) in '...', got the keyword 'wit' instead`, and every error that names a token does the same. `and`, `or`, and `not` aren't keywords, so they're names like any other.

## What Was Built

`tests/features/` holds 155 tests in 10 files, converted from 55 programs in `tests/run/`, which keeps 9. `scripts/feature_test.sh` runs a file of them, and `CMakeLists.txt` runs every file once the `basics` fixture is set up. `src/parser/parser.hpp` names keywords in its errors and `src/sema/sema.hpp` reports overloading only for an operator, which `tests/unit/sema/test_sema.cpp` checks. The README describes the three tiers. 14573 unit assertions, 9 programs, the run of `tests/test/`, 155 feature tests at both levels, 6 debug programs, and the grammar's tests, 0 failures, and a failing program in `tests/run/` leaves every feature test unrun.

## Current Limitations

Running one feature test from ctest also runs every test it requires first, all of the unit tests and the basics, so running a file directly with `cheri test` is quicker while working on it. A check that a value is infinite still compares it against the largest finite value, since there's no infinity literal yet.
