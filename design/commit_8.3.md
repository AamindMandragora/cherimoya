# Commit 8.3: cheri test

## The Problem

A cherimoya program can check itself with `assert`, but the only way to run those checks is to build a program around them, and the first one that fails stops the rest. The language's own test suite has `tests/run/`, which a shell script drives, and nothing a program's author can use. Commit 8.3 gives cherimoya tests of its own: functions marked `@test` in `.toya` files, which `cheri test` builds, runs each on its own, and reports.

## Test Files

Tests go in `.toya` files, so they never ship in a program: `@test` anywhere else is `cherimoya requires tests to be in a .toya file, please move it to one`. A `.toya` file holds tests and the helpers they share, which are any functions and bindings without `@test`, and stands alone until modules can import, so for now it holds the code it tests too. It has no `start`, since `cheri test` runs its tests instead: `cherimoya requires a .toya file to have no 'start', since cheri test runs its tests instead, please rename it`. A `.toya` file without any tests gets a warning, `cherimoya found no tests in this file, please mark one with '@test'`.

Sema checks every file in one of three roles: a module, which is how the language server and the unit tests check a `.moya` file, a program, which needs a `start`, and a file of tests. The driver picks the role from the extension, and so does the language server.

## @test

A test is a function that takes and yields nothing, with `@test` on it, the attribute that tells it apart from a helper with the same shape:

```
@test(traps <- "overflowed int", timeout <- 5)
let () -> overflows -> () {
    _ <- square(4294967296);
}
```

`@test` takes two named arguments, both optional. `traps` is a string the trap's message has to hold, for a test that passes by trapping, since traps are how cherimoya reports overflow and failed checks. `timeout` is a whole number of seconds, after which the test fails, and a test without one can run as long as it takes. Anything else is an error: an unnamed argument, a name other than those two, the same one twice, a `traps` that isn't a string, and a `timeout` that isn't a whole number above 0.

A test with inputs or outputs is `cherimoya doesn't support tests with inputs and outputs yet, which it will fuzz against their contracts`, since nothing could call it with arguments, and contracts, which arrive with Z3, are what fuzzing generates them from. A test can be nested in a function or a block, and it's named the way a debugger names it, `outer.inner`. Tests are reported in the order they're written, nested ones included.

## Running Tests

`cheri test math.toya` runs one file's tests, `cheri test tests/` every `.toya` file's under a directory, in the order of their paths, and a name after the path picks only the tests whose names hold it, or with `--exact`, the one named exactly that. Tests build the way `cheri build` does, at `-O3` unless an `-O` says otherwise, and with `-g`, a failing test can be run under a debugger.

Each test runs in a process of its own, so a trap, a crash, or a test that never ends only fails itself. A file's tests become one test binary, which links the test runtime's entry point instead of a program's: it runs the file's init, then the one test its argument numbers, through `cheri_run_test`, which codegen makes as a switch over the file's tests. `cheri test` starts the binary once per test, as many at once as there are cores, and judges each by how it ended:

- **A test passes** by finishing, or, when it has `traps`, by trapping with that text in the message the runtime prints before it aborts.
- **A test fails** by trapping or crashing, or, when it has `traps`, by finishing or trapping for some other reason.
- **A test times out** once it runs past its `timeout`, and is killed.

LLVM can run a process with a time limit, but on Unix its limit is one alarm for the whole process, which tests running at once can't share, so `cheri test` starts each one and polls it, killing it once it runs past its own deadline. Once every test in a file has run, it prints them in their order, with what a failing one printed under it:

```
tests/test/outcomes.toya
  ok       adds_negatives
  ok       overflows (trapped as expected)
  FAIL     wrong
           trap at line 17: assert (add(2, 2) = 5);
  TIMEOUT  stuck (after 1s)

2 passed, 2 failed
```

`cheri test` exits with 1 when any test failed or any file didn't build, and the summary counts the files that didn't. `cheri build` and `cheri run` refuse a `.toya` file, and `cheri check` checks one as tests.

## The Runtime

The runtime's C entry point splits in two. `runtime/start.c` is a program's, whose `main` runs the init and then `start`, and `runtime/test.c` is a test binary's, whose `main` runs the init and then one test. Each is a library of its own, and the driver links the one its binary needs ahead of `runtime/runtime.c`, which holds the trap every binary uses.

## Editors

The language server checks a `.toya` document as tests and puts a Run test lens on every test's name, which names the editor command `cherimoya.runTest` with the document and the test. VS Code's extension runs it as a task, `cheri test` with the file and `--exact`, which passes each argument as it is, so a path with spaces needs no quoting, and it gives `.toya` files a language of their own, Cherimoya Tests, which is how they get their own icon, the cherimoya's with a check on it. The Emacs mode opens `.toya` files too, and gives lsp-mode a handler for the command, which runs the test with `compile`, while eglot shows no lenses.

## What Was Built

`src/sema/sema.hpp` gained `Role`, `Test`, and `checkTest`, which checks `@test` and its arguments and names nested tests, the start and warning rules for a file of tests, and `allTests`. `src/parser/expr.hpp` gained `stringLiteralValue` and `src/lexer/escape.hpp` gained `encodeUtf8`, which read a `traps` string. `src/codegen/codegen.hpp` gained `emitTestRunner`. `tools/cheri/main.cpp` gained `cheri test`, `--exact`, and the checks that keep `.toya` files and commands apart, and its build splits into checking a file, building a module, and linking it, which `build`, `run`, and `test` share. `runtime/start.c` and `runtime/test.c` are the two entry points. `src/lsp/` gained the lens, `editors/vscode/` the command, the language, and `icons/toya.svg`, and `editors/emacs/` the file type and the handler.

`tests/unit/sema/test_sema.cpp` checks tests and their names, traps, and timeouts, every way to get `@test` wrong, and the roles, and `tests/unit/lsp/test_lsp.cpp` checks the lens and that a `.moya` document gets none. `tests/test/` holds `.toya` files with every outcome a test can have, which `scripts/test_test.sh` runs through `cheri test` and compares with `expected.out`. 14503 unit assertions, 61 programs, 5 debug programs, and the run of `tests/test/`, 0 failures.

## Current Limitations

A `.toya` file can't test a `.moya` file's code until modules can import it. Tests with inputs wait for contracts and fuzzing. A test's output beyond its trap goes nowhere, since programs can't print yet, and tests in different files don't run at once, only tests in the same file do.
