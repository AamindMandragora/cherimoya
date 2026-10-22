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

let () -> main -> (code :: int) {
    27 |> steps -|
}
```

```
$ cheri run collatz.moya; echo $?
111
```

## The Language at a Glance

The syntax follows the way data moves:

- **`<-`** flows values backward into whatever receives them: bindings, assignments, construction.
- **`->`** flows definitions forward: from a function's inputs to its name to its outputs, and from a match pattern to its arm.
- **`-|`** yields a value out of a scope or a function, where a scope is a block, a `match` or one of its arms, a loop, or a `chunk`. There's no `return`, `break`, or `continue`. A bare `-|` leaves the innermost scope, and `-| name` leaves the scope or function with that name, so every early exit is visible.
- **`|>` and `<|`** pipe a value into a function.

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

- **The whole front end:** the lexer, the parser for the whole grammar except macros, and diagnostics that say what was expected, what the language does instead, or which rule was broken and how to fix it.
- **Name resolution and type checking**: functions (nested, and in any order), bindings, operators, calls and pipes, blocks, `match`, loops, yields and their labels, mutability, discards, and the entry point.
- **The numeric model**: every integer from 8 to 128 bits, signed or unsigned, trapping or wrapping, and every float from `f16` to `f128`, including bfloat16, NaN-free or NaN-carrying, and both FP8 encodings, stored as bytes and computed in f32. Literals take the type their context wants, and conversions go between any two numbers.
- **Code generation through LLVM** for all of that, with optimization, native object files, cross-compilation, and linking with a small runtime. Overflow, division by zero, out-of-range shifts, a NaN made in or converted into a NaN-free float, an `f8` that overflows, a conversion into a trapping integer that doesn't hold the value, and a failed `assert` trap with their line.

- **A language server**, `cheri lsp`, with a VS Code extension: diagnostics as you type, highlighting by meaning, hover, go to definition, references, rename, an outline, completion ranked by the type a position expects, and signature help.

Not yet: strings and chars, packs, ranges, comprehensions, and folds, for-each loops, handles and arrays, records, sums, interfaces, generics, closures and functions as values, operator overloading and the extra operators, modules, ownership checking, and verification with Z3.

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
| `--linker <program>`, `--static` | how to link |
| `-L<dir>`, `-l<lib>`, `--link-arg <arg>` | passed straight to the linker |

`cheri build prog.moya --emit-llvm -O0` prints the IR exactly as the compiler wrote it, before any optimization.

## Editor Support

`editors/vscode/` is a VS Code extension that runs `cheri lsp` for every `.moya` file and gives them a cherimoya icon. To build and install it:

```
cd editors/vscode
npm install
npm run package
code --install-extension cherimoya.vsix
```

Then set `cherimoya.compilerPath` to your `build/cheri`, unless `cheri` is on your path, and restart VS Code. Any other editor that speaks the Language Server Protocol can start `cheri lsp` the same way.

## Layout

| Path | What's there |
|---|---|
| `src/source`, `src/diagnostics` | the source buffer, and diagnostics with notes |
| `src/lexer` | the lexer and the operator table |
| `src/parser` | the parser and the AST |
| `src/sema` | name resolution and type checking |
| `src/codegen` | lowering to LLVM IR |
| `src/lsp` | the language server |
| `runtime/` | the C runtime every program links with |
| `tools/cheri/` | the `cheri` driver |
| `editors/vscode/` | the VS Code extension |
| `tests/unit/` | Catch2 tests for the lexer, parser, sema, runtime, and language server |
| `tests/run/` | whole programs, each checked by its exit code |
| `design/` | a design note for every commit |

## Tests

The lexer, parser, sema, runtime, and language server have unit tests in C++, under `tests/unit/`, and codegen is tested through whole programs. Those live in `tests/run/`: each one is a `.moya` file whose first line is the exit code it should produce, and the test suite builds it with `cheri` at `-O0` and at `-O3`, runs it, and compares. A program that should trap names the trap on a `# trap:` line, and its stderr has to contain it.

```
# exit: 134
# trap: trap at line 5: assert (n % 2 = 0);
# checks that a false assertion traps with its line, which aborts the program
let (n :: int) -> half -> (r :: int) { ... }
```

Adding a program test means adding a file.
