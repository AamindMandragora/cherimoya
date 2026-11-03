# cherimoya

Cherimoya is a compiled systems language for the gap between C++ and Rust, built around **progressive disclosure of safety**. Plain code is as easy to write as C. Stronger guarantees, like ownership tracking, refinement contracts, verified arithmetic, and exhaustive matching, layer on top without changing the base language, and you choose how much of them you want per function, module, or project.

`cheri` is its compiler. It's written in C++17 and generates code through LLVM.

```
# counts the steps the Collatz sequence takes to reach 1
let (n :: int {n > 0}) -> steps -> (count :: int) {
    let x :: mut <- n;
    let taken :: mut <- 0;
    repeat (x != 1) {
        x <- match (x % 2) {
            0 -> { x / 2 -| }
            _ -> { 3 * x + 1 -| }
        };
        taken +<- 1;
    }
    taken -|
}

let () -> start -> (code :: int) {
    27 |> steps -|
}
```

```
$ cheri run collatz.moya; echo $?
111
```

## The Language at a Glance

The syntax follows the way data moves:

- **`<-`** flows values backward into whatever receives them: bindings, assignments, construction, a named argument, `f(b <- 2)`, and an input's default, `(b :: int <- 1)`.
- **`->`** flows definitions forward: from a function's inputs to its name to its outputs, and from a match pattern to its arm.
- **`-|`** yields a value out of a scope or a function, where a scope is a block, a `match` or one of its arms, a loop, or a `chunk`. There's no `return`, `break`, or `continue`. A bare `-|` leaves the innermost scope, and `-| name` leaves the scope or function with that name, so every early exit is visible.
- **`|>` and `<|`** pipe a value into a function, or into a call that gives the rest of the arguments: `x |> clamp(hi <- 10)` is `clamp(x, hi <- 10)`.

Brackets have fixed jobs: `{ }` makes scopes, `[ ]` packs data together, and `( )` is for calls, math, and conditions. There's no `if`: every conditional is a `match`, with a subject or with guards.

```
let (n :: int) -> clamp -> (r :: int) {
    match { (n > 100) -> { 100 -| clamp } }
    match { (n < 0) -> { 0 -| clamp } }
    n -|
}
```

Bindings are immutable unless they're declared `mut`, a value can't be silently dropped (`_ <- f(x);` discards one on purpose), and comments start with `#`. The design of every compiler stage is written up in [`design/`](design/).

## Status

Cherimoya is early. What works today:

- **The whole front end:** the lexer, the parser for the whole grammar, and diagnostics that say what was expected, what the language does instead, or which rule was broken and how to fix it.
- **Name resolution and type checking**: functions (nested, and in any order), bindings, operators, calls and pipes with named arguments and defaults, blocks, `match`, loops, yields and their labels, mutability, discards, and the entry point, `start`, which a program defines the way C's runtime calls `main`.
- **Characters**: `char`, one ASCII character, and `rune`, one Unicode scalar value, with escapes like `'\n'`, `'\x41'`, and `'\u{1F600}'`, comparisons, `match`, and conversions to and from integers, plus annotations, `(5 :: i8)`, which give a value a type without converting it.
- **Fixed arrays**: `arr[N] T`, held inline, written as `[1, 2, 3]`, with sizes known when the program compiles, from a literal or a constant like `let N <- 4;`, indexing by unsigned integers that traps past the end, `xs.len()`, the method arrays have as `Sized`, which a pipe reaches as `xs |> .len` or `.len <| xs`, `=` and `==` on whole arrays, and `mut` on the elements, `arr[N] mut T`, or in front, to replace the whole array.
- **Fuzzy booleans**: `boolf`, a truth value from 0 to 1 held in a byte, where `&&` is the minimum, `||` the maximum, `*` the product, and `!x` is 1 - x, with conversions to and from floats, its raw byte, and `bool`, and a cutoff like `x > 0.5` to turn one back into a `bool`.
- **The numeric model**: every integer from 8 to 128 bits, signed or unsigned, trapping or wrapping, and every float from `f16` to `f128`, including bfloat16, NaN-free or NaN-carrying, and both FP8 encodings, stored as bytes and computed in f32. Literals take the type their context wants, and conversions go between any two numbers.
- **Code generation through LLVM** for all of that, with optimization, native object files, cross-compilation, and linking with a small runtime. Overflow, division by zero, out-of-range shifts, a NaN made in or converted into a NaN-free float, an `f8` that overflows, a conversion into a trapping integer, a character, or a boolf that doesn't hold the value, and a failed `assert` trap with their line.
- **Tests** in `.toya` files, run by `cheri test`, each in a process of its own, with tests that expect a trap and timeouts.
- **Debug info** with `-g`: DWARF, or CodeView on Windows, for gdb and lldb, with source names, every variable and its type, and printers that gdb loads from the binary for the values C's rules would print wrong, like FP8 and `rune`.

- **A language server**, `cheri lsp`, with extensions for VS Code and Emacs, and a tree-sitter grammar that follows cheri's parser exactly: diagnostics as you type, highlighting by meaning, hover with the documentation your comments give a name, go to definition, references, rename, an outline, completion ranked by the type a position expects, signature help, quick fixes for the mistakes that have one, which `cheri check --fix` applies from the command line too, and inlay hints: the types cherimoya inferred, the input each argument goes to, the defaults a call leaves out, the type of each step of a pipe spread over lines, and the name of a long function at its closing brace, along with a Run test lens above every test in a `.toya` file.

Not yet: strings, packs of different types, ranges, comprehensions, and folds, for-each loops, arrays on the heap and pointers, records, sums, protocols, generics, closures and functions as values, operator overloading and the extra operators, modules, ownership checking, and verification with Z3.

## Building

You need:

- a C++17 compiler (the build uses clang)
- CMake 3.20 or newer, and Ninja if you have it
- LLVM 21.1 with its development files (`llvm21-devel` on Void, `llvm-21-dev` on Debian and Ubuntu), with `llvm-config` on your path

Catch2 is downloaded during the first build.

```
./scripts/build.sh      # configures with clang on the first run, then builds into build/
./scripts/test.sh       # builds, then runs every test
./scripts/clean.sh      # removes the build
```

The compiler ends up at `build/cheri`.

## Using cheri

```
cheri build prog.moya            # an executable named prog
cheri run prog.moya              # build, run, and exit with the program's exit code
cheri check prog.moya            # check without building
cheri test tests/                # run the tests in every .toya file under tests/
cheri lsp                        # the language server, which an editor starts
cheri help                       # every command and option
```

The most useful options:

| Option | What it does |
|---|---|
| `-o <path>` | where the output goes |
| `--emit exe\|obj\|asm\|llvm\|bc` | what to produce; `-c`, `-S`, and `--emit-llvm` are shorthands |
| `-O0` … `-O3`, `-Os`, `-Oz` | how hard to optimize, `-O3` by default |
| `--target <triple>` | cross-compile, like `--target aarch64-linux-gnu -c` |
| `--cpu <name>`, `--features <list>` | tune for a cpu, or `--cpu native` for this machine |
| `-g`, `-gline-tables-only` | debug info: everything, or just lines and function names, for backtraces |
| `--linker <program>`, `--static` | how to link |
| `-L<dir>`, `-l<lib>`, `--link-arg <arg>` | passed straight to the linker |

`cheri build prog.moya --emit-llvm -O0` prints the IR exactly as the compiler wrote it, before any optimization.

## Writing Tests

Tests go in `.toya` files, which hold functions marked `@test`, along with any helpers they share, and `cheri test` runs them: `cheri test math.toya` runs one file's, `cheri test tests/` every `.toya` file's under a directory, and `cheri test math.toya overflow` only the tests whose names hold `overflow`, or with `--exact`, the one named exactly that.

```
let (n :: int) -> square -> (r :: int) { n * n -| }

@test
let () -> squares -> () {
    assert (square(-3) = 9);
}

@test(traps <- "overflowed int", timeout <- 5)
let () -> overflows -> () {
    _ <- square(4294967296);
}
```

A test passes when it finishes, and fails when it traps, which a failed `assert` does. A test given `traps` passes only by trapping with that text in the trap's message, and one given `timeout` fails once it runs longer than that many seconds. Each test runs in a process of its own, as many at once as there are cores, so a trap or a crash only fails its own test, and `cheri test` exits with 1 when anything failed:

```
math.toya
  ok       squares
  ok       overflows (trapped as expected)

2 passed, 0 failed
```

A `.toya` file stands alone until modules can import, so it holds the code it tests, and it has no `start`, since `cheri test` runs its tests instead. Tests build like `cheri build`, at `-O3` unless an `-O` says otherwise, and with `-g`, a failing test can be run under a debugger.

## Debugging

`cheri build prog.moya -g -O0` builds a program a debugger can see all of: every function by its source name, every binding and input with its type, a nested function as `outer.inner`, and an input's default as `clamp.lo<-`. Immutable bindings show as `const`, a `boolf` prints as its fraction, and the program starts at `start`, which the runtime's C `main` calls. `-g` works at any `-O`, where the optimizer can inline functions and leave some variables `<optimized out>`.

```
$ gdb ./prog
(gdb) break clamp
(gdb) run
Breakpoint 1, clamp (n=150, lo=0) at prog.moya:3
(gdb) info locals
fuzzy = 0.70196078431
tiny = 1.75
```

gdb can't decode FP8, bfloat16, or `f128` on its own, prints 1-byte integers as C chars, and prints a `rune` as a hex escape, so a `-g` build carries pretty-printers for those inside it, which gdb runs once it trusts the binary's directory. Add this to `~/.gdbinit`, with your own path:

```
add-auto-load-safe-path ~/projects
```

## Editor Support

`editors/vscode/` is a VS Code extension that runs `cheri lsp` for every `.moya` and `.toya` file, gives them a cherimoya icon, with a check on a `.toya`, and puts a Run test lens above every test. To build and install it:

```
cd editors/vscode
npm install
npm run package
code --install-extension cherimoya.vsix
```

Then set `cherimoya.compilerPath` to your `build/cheri`, unless `cheri` is on your path, and restart VS Code.

`editors/emacs/cherimoya-mode.el` is an Emacs mode for `.moya` and `.toya` files that starts `cheri lsp` with eglot, which is built into Emacs, or with lsp-mode, if it's installed. Eglot colors the code from Emacs 31 on, and lsp-mode does once `lsp-semantic-tokens-enable` is set, and lsp-mode shows the Run test lenses, which run the test with `compile`. To use it:

```elisp
(add-to-list 'load-path "/path/to/cherimoya/editors/emacs")
(require 'cherimoya-mode)
(setopt cherimoya-compiler-path "/path/to/cherimoya/build/cheri")   ; unless cheri is on your path
(add-hook 'cherimoya-mode-hook #'eglot-ensure)                      ; or #'lsp
```

Any other editor that speaks the Language Server Protocol can start `cheri lsp` the same way.

## Layout

| Path | What's there |
|---|---|
| `src/source`, `src/diagnostics` | the source buffer, and diagnostics with notes |
| `src/lexer` | the lexer and the operator table |
| `src/parser` | the parser and the AST |
| `src/sema` | name resolution and type checking |
| `src/codegen` | lowering to LLVM IR |
| `src/lsp` | the language server |
| `runtime/` | the C runtime every program links with, and the entry points of a program and of a file of tests |
| `tools/cheri/` | the `cheri` driver |
| `editors/vscode/` | the VS Code extension |
| `tree-sitter/` | the tree-sitter grammar, which editors read programs with |
| `tests/unit/` | Catch2 tests for the lexer, parser, sema, runtime, and language server |
| `tests/run/` | whole programs, each checked by its exit code |
| `tests/debug/` | programs built with debug info, checked with llvm-dwarfdump and gdb |
| `tests/test/` | `.toya` files with every outcome a test can have, which `cheri test` has to report exactly as `expected.out` says |
| `design/` | a design note for every commit |

## Tests

The lexer, parser, sema, runtime, and language server have unit tests in C++, under `tests/unit/`, and codegen is tested through whole programs. Those live in `tests/run/`: each one is a `.moya` file whose first line is the exit code it should produce, and the test suite builds it with `cheri` at `-O0` and at `-O3`, runs it, and compares. A program that should trap names the trap on a `# trap:` line, and its stderr has to contain it.

```
# exit: 134
# trap: trap at line 5: assert (n % 2 = 0);
# checks that a false assertion traps with its line, which aborts the program
let (n :: int) -> half -> (r :: int) { ... }
```

Debug info is tested the same way, through programs in `tests/debug/`, each built with `-g -O0` unless a `# flags:` line says otherwise. Every `# dwarf:` line has to appear in what llvm-dwarfdump shows of the debug info, and the `# gdb:` lines run as one gdb session whose output has to contain every `# expect:` line, which is skipped when gdb isn't installed.

```
# gdb: break clamp
# gdb: run
# expect: clamp (n=150, lo=0) at
# checks that a breakpoint on a function stops once its inputs are stored
let (n :: int, lo :: int <- 0) -> clamp -> (r :: int) { ... }
```

`cheri test` itself is tested on `tests/test/`, whose `.toya` files have every outcome a test can have, and whose run has to print exactly what `expected.out` holds.

Adding a program test means adding a file.
