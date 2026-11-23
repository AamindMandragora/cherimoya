# Commit 12.10: cheri test

## The Feature

- **Test files.** Tests go in `.toya` files, which hold tests and the helpers they share, any functions and bindings without `@test`. A `.toya` file has no `start`, since `cheri test` runs its tests instead.
- **`@test`.** A test is a function that takes and yields nothing, with `@test` on it. It takes two named arguments, both optional: `traps`, a string the trap's message has to hold, for a test that passes by trapping, and `timeout`, a whole number of seconds after which the test fails.

```
@test(traps <- "overflowed int", timeout <- 5)
let () -> overflows -> () {
    _ <- square(4294967296);
}
```

- **Nested tests.** A test can be nested in a function or a block, and it's named the way a debugger names it, `outer.inner`. Tests are reported in the order they're written, nested ones included.
- **`cheri test`.** `cheri test math.toya` runs one file's tests, `cheri test tests/` every `.toya` file's under a directory, in the order of their paths, and a name after the path picks only the tests whose names hold it, or with `--exact`, the one named exactly that. Tests build the way `cheri build` does, at `-O3` unless an `-O` says otherwise, and with `-g`, a failing test can be run under a debugger.
- **Outcomes.** Once every test in a file has run, it prints them in their order, with what a failing one printed under it:

```
tests/test/outcomes.toya
  ok       adds_negatives
  ok       overflows (trapped as expected)
  FAIL     wrong
           trap at line 17: assert (add(2, 2) = 5);
  TIMEOUT  stuck (after 1s)

2 passed, 2 failed
```

`cheri test` exits with 1 when any test failed or any file didn't build, and the summary counts the files that didn't. `cheri build` and `cheri run` refuse a `.toya` file, `a .toya file holds tests, run it with 'cheri test'`, and `cheri check` checks one as tests.

- **Errors.** Each mistake with `@test` has its own:
  - `cherimoya requires tests to be in a .toya file, please move it to one`
  - `cherimoya requires '@test' to be on a function, please put it before one like 'let () -> name -> ()'`
  - `cherimoya doesn't support tests with inputs and outputs yet, which it will fuzz against their contracts`
  - `cherimoya requires '@test' arguments to be named, please write 'traps <- "message"' or 'timeout <- 5'`
  - `cherimoya requires '@test' arguments to be 'traps' or 'timeout', please remove 'retries <-'`
  - `cherimoya requires each '@test' argument once, please remove the second 'traps'`
  - `cherimoya requires 'traps' to be a string, please write the text its trap's message should contain, like 'traps <- "overflowed"'`
  - `cherimoya requires 'timeout' to be a whole number of seconds above 0, please write one like 'timeout <- 5'`
  - `cherimoya requires a .toya file to have no 'start', since cheri test runs its tests instead, please rename it`
  - and a warning for a file without any, `cherimoya found no tests in this file, please mark one with '@test'`.
- **A Run test lens.** The language server puts one on every test's name, and VS Code and Emacs run it.

## Why

A cherimoya program can check itself with `assert`, but the only way to run those checks would be to build a program around them, and the first one that fails would stop the rest. A program's author needs tests of their own, named, run each on its own, and reported.

- **A file extension of their own** keeps tests out of a program that ships. A `.toya` file stands alone until modules can import, so for now it holds the code it tests too.
- **`@test` marks a test** rather than its shape, since a helper can take and yield nothing too.
- **`traps`** exists because traps are how cherimoya reports overflow and failed checks, so a test of a check has to expect one. It matches text in the message rather than all of it, so a test doesn't break when its line moves.
- **No inputs or outputs,** since nothing could call a test with arguments. Contracts, once they exist, are what fuzzing generates them from.
- **A process per test** means a trap, a crash, or a test that never ends only fails itself.
- **Polling for timeouts.** LLVM can run a process with a time limit, but on Unix its limit is one alarm for the whole process, which tests running at once can't share, so `cheri test` starts each one and polls it, killing it once it runs past its own deadline. A test without a timeout can run as long as it takes.
- **Three roles for sema.** A file is checked as a module, which is how the language server and the unit tests check a `.moya` file, a program, which needs a `start`, or a file of tests. The driver and the language server both pick the role from the extension.

## Implementation

### Sema

`src/sema/sema.hpp` adds `Role`, `Module`, `Program`, or `Tests`, which `check` takes in place of a flag for whether a `start` is needed. A `Test` holds its `FnDecl`, its dotted name, and an optional `traps` and `timeout`. `hoist` walks the attributes over each statement and calls `checkTest` for `@test`, which checks the role, the function, and its shape, then each argument, reading a `traps` string through `stringLiteralValue` and a `timeout` integer literal through `literalDigits` and `hexValue`, capped at 1000000000. A test that passes every check is recorded. `enclosing` holds the names of the functions around the one being checked, pushed in `checkFn`, so a nested test's name starts with them. With `Role::Tests`, `check` reports a `start`, warns when `tests` is empty, and sorts the tests by where they're written, since a nested one is found after the top-level ones. `allTests` hands them out.

`stringLiteralValue` in `src/parser/expr.hpp` gives a string literal's text without its quotes and with its escapes read, using `encodeUtf8`, new in `src/lexer/escape.hpp`.

### Codegen

`emitTestRunner` in `src/codegen/codegen.hpp` makes `cheri_run_test`, which takes a test's number, its place among the file's tests, and switches over them, calling that test and returning, where a number that's no test's does nothing. `generate` takes the tests and emits the runner when there are any.

### Runtime

The runtime's C entry point splits in two. `runtime/start.c` is a program's, whose `main` runs `cheri_init` and then `cheri_start`. `runtime/test.c` is a test binary's, whose `main` runs `cheri_init` and then the one test its argument numbers through `cheri_run_test`. `runtime/runtime.c` keeps the trap every binary uses. `CMakeLists.txt` builds each entry point as a library of its own, `cheri_rt_start` and `cheri_rt_test`, and bakes their paths into cheri as `CHERI_START` and `CHERI_TEST`.

### Driver

`tools/cheri/main.cpp` adds `test`, a filter, and `--exact`, which only goes with `test`, and the checks that keep `.toya` files and commands apart. Its build splits into `Checked`, a file read and checked in a role, `buildModule`, which lowers, verifies, and optimizes it, and `linkExecutable`, which links it with an entry point ahead of `CHERI_RUNTIME`, which `build`, `run`, and `test` share. `testFile` checks and builds one file's tests into a temporary binary numbering every test, then runs the picked ones on as many threads as there are cores, up to the number of tests, and prints each `Outcome` in order under the file's name. `runTest` starts the binary with a test's number through `llvm::sys::ExecuteNoWait`, sends its stderr to a log, polls it with `llvm::sys::Wait` every 10 milliseconds when it has a deadline, and kills it past that. It judges each by how it ended:

- **A test passes** by finishing, or, when it has `traps`, by trapping with that text in the message the runtime prints before it aborts.
- **A test fails** by trapping or crashing, or, when it has `traps`, by finishing or trapping for some other reason.
- **A test times out** once it runs past its `timeout`, and is killed.

`runTests` collects the `.toya` files under a directory, sorted, adds up a `Tally` across them, and prints the summary.

### Language Server

`src/lsp/document.hpp` checks a document ending in `.toya` as `Role::Tests` and anything else as `Role::Module`, since the editor's open file isn't always the program's. `codeLenses` in `src/lsp/features.hpp` puts a Run test lens on every test's name, naming the command `cherimoya.runTest` with the document and the test, and `src/lsp/server.hpp` announces `codeLensProvider` and answers `textDocument/codeLens`.

### Editors

VS Code's extension registers `cherimoya.runTest`, which runs `cheri test` with the file, the test, and `--exact` as a task through `vscode.ProcessExecution`, which passes each argument as it is, so a path with spaces needs no quoting. It gives `.toya` files a language of their own, Cherimoya Tests, which is how they get their own icon, `icons/toya.svg`, the cherimoya's with a check on it. The Emacs mode opens `.toya` files too, and gives lsp-mode a handler for the command, `cherimoya-run-test`, which runs the test with `compile`, while eglot shows no lenses.

## Tests

`tests/unit/sema/test_sema.cpp` checks tests and their names, traps, and timeouts, every way to get `@test` wrong, and the roles, and `tests/unit/lsp/test_lsp.cpp` checks the lens and that a `.moya` document gets none. `tests/test/` holds `.toya` files with every outcome a test can have, which `scripts/test_test.sh` runs through `cheri test`, checking that it exits with 1 and prints exactly `expected.out`, and ctest runs it as `Test: cheri test`. 14503 unit assertions, 61 programs, 5 debug programs, and the run of `tests/test/`, 0 failures.

## Current Limitations

- **No imports.** A `.toya` file can't test a `.moya` file's code until modules can import it.
- **No inputs.** Tests with inputs wait for contracts and fuzzing.
- **No output.** A test's output beyond its trap goes nowhere, since programs can't print yet.
- **One file at a time.** Tests in different files don't run at once, only tests in the same file do.
