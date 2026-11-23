# Commit 1.9: LLVM Lowering and the First Executable

## The Feature

- **`cheri build prog.moya`** checks the program, lowers it to LLVM IR, optimizes it, writes an object file, and links it with a small runtime into an executable whose exit code is what `main` yields.
- **Every stage on request.** `--emit` stops at an object file, assembly, IR, or bitcode, with `-c`, `-S`, and `--emit-llvm` as shorthands. IR goes to the terminal unless `-o` names a file, and `--emit-llvm -O0` shows exactly what codegen wrote.
- **Optimization.** `-O0` through `-O3`, `-Os`, and `-Oz`, with `-O3` by default, setting both the optimizer's pipeline and how hard the backend works when it selects and schedules instructions.
- **Targets.** `--target` cross-compiles to any triple LLVM supports, and `--cpu` and `--features` tune the code, with `--cpu native` meaning the machine cheri runs on.
- **Linking.** `--linker` picks the program that links, `cc` by default, `--static` makes a static executable, and `-L`, `-l`, and `--link-arg` pass straight through.
- **`cheri run`** builds into the temporary directory, runs the program, removes it, and exits the way the program did, where a program killed by a signal exits with 128 plus the signal, as a shell reports it. **`cheri check`** stops after the front end, and **`cheri help`** lists every option. The command comes first, and the file can go before, between, or after the options.
- **The entry point.** A program that's built needs a top-level `let () -> main -> (code :: int)`: `cherimoya requires a program to have a 'main' function, please add 'let () -> main -> (code :: int) { 0 -| }'`, and one declared differently is `cherimoya requires 'main' to be declared as 'let () -> main -> (code :: int)', please change its inputs and outputs`.
- **Assertions trap.** A false `assert` stops the program with its own source text and line:

```
trap at line 3: assert (n % 2 = 0);
```

- **Program tests.** Whole programs in `tests/run/` are built and run, and their exit codes checked.

## Why

After commit 1.8, cherimoya can tell whether a program is right, but it can't run one. Sema has resolved every name and typed every expression, and nothing turns that into instructions. This commit does, through LLVM, so optimization and every target LLVM supports come with it.

- **Codegen only sees checked programs.** The driver stops on any error before lowering, so the error type reaching codegen is a codegen bug, reported with `llvm_unreachable`, and so is broken IR, which is printed next to the verifier's complaint.
- **Position-independent code,** since `cc` links executables as PIE by default.
- **Dotted symbols.** Every function's symbol is `cheri`, its module, and its name joined with dots: `main` in `prog.moya` is `cheri.prog.main`, and `g` nested in `f` is `cheri.prog.f.g`. Dots are legal in LLVM and ELF symbols, stay readable in a debugger, and leave room for modules. The `cheri.` in front keeps clear of LLVM's intrinsics, which own every symbol starting with `llvm.`, so a file can be called `llvm.moya`.
- **The runtime never collides with a program.** Every symbol a program defines has a dot in it and the runtime's names don't, even in a file called `cheri.moya`.
- **Locals are stack slots.** Codegen never tracks which value a variable has on which path. The optimizer's mem2reg pass turns every slot back into registers, with phis where control joins. Slots go in the entry block because mem2reg only promotes those, and because a slot made inside a loop would take new stack space on every iteration. A factorial loop with two `mut` locals comes out with no memory traffic at all:

```llvm
repeat.body:
  %i.07 = phi i64 [ %1, %repeat.body ], [ 2, %definition ]
  %product.06 = phi i64 [ %0, %repeat.body ], [ 1, %definition ]
  %0 = mul i64 %i.07, %product.06
  %1 = add i64 %i.07, 1
```

- **Nested functions are ordinary functions.** They can't capture their parent's locals, so each one compiles as its own LLVM function.
- **Top-level code runs in an init function,** once, in order, before `main`. An immutable binding whose initializer folds to a constant needs no code at all.
- **Codegen mirrors sema's yield-target stack,** with the same rules, so a yield is just a branch.
- **`main` is checked only when asked,** since a program checked on its own, like every sema test, doesn't need one. The driver always asks.
- **Tests run at `-O0` and `-O3`,** since the two can disagree, and a trap has to be the one the test names, so a test can't pass by trapping for some other reason.

## Implementation

### Codegen

- **Types.** `Codegen` in `src/codegen/codegen.hpp` owns the `llvm::LLVMContext`, declared first so it outlives the module and builder, and `lower` maps `int` to `i64`, `bool` to `i1`, `float` to `double`, `void` and `never` to `void`, and a function type to an LLVM function type, caching each by pointer in `lowered`, since sema interns its types.
- **Declaring before defining.** `generate` first runs `collectStmts`, which walks the whole program through `collectStmt` and `collectExpr`, into function bodies and the blocks, matches, guards, loops, and chunks in any expression or assertion, and `declare`s every function under the name of the one around it, recording it in `declared` and `functions`. Then `define` emits each body, and externs stay declarations for the linker.
- **Slots and globals.** `makeSlot` puts a symbol's `alloca` at the top of the entry block, and `define` copies each parameter into one, so a `mut` parameter assigns like a binding. A binding with `owner` 0 is a global from `makeGlobal`, `cheri.prog.LIMIT` with internal linkage: an immutable one whose initializer is an LLVM constant becomes the global's initializer and is marked constant, and anything else is stored each time its `let` runs, which a binding in a top-level loop does once per iteration. `addressOf` finds either, so functions read and assign globals exactly like locals.
- **Init.** Everything at the top of the file that isn't a function is emitted into `cheri.prog.init`. `generate` exports it as `cheri_init`, and the `main` declared at the top of the file, looking through attributes, as `cheri_main`, both with `GlobalAlias`, so the runtime never needs the module's name. A `main` nested in a top-level block gets the same prefix, so codegen looks for the top-level declaration instead of a symbol ending in `.main`.
- **Literals.** The lexer keeps them raw, so `emitExpr` strips separators and reads the prefix to pick the radix through `llvm::APInt`. Floats are decimal only.
- **Operators.** `arithmetic` picks integer or float instructions from the left operand's type, which sema made match the right, with signed division and remainder, since `int` is. `compare` is signed for ints and ordered for floats, which are NaN-free. A chain emits each operand once and joins the pairs with `and`. `emitShortCircuit` gives the right side of `&&` and `||` its own block, with a phi that takes the decided value from the block the left side finished in, which isn't always where it started.
- **Calls.** A call looks its callee up in `declared`, both pipes are one-argument calls, and `emitCall` gives no value for a void function, so `tick() -|` is a bare yield.
- **Exits.** `Exit` holds a scope's kind, label, the block where control continues, and its `result` slot, from `makeTemp`, when `carriesValue` says its type has a value. Every function, block, match, arm, loop, and chunk pushes one onto `exits`. `resolveExit` finds a yield's target with sema's rules, and a yield to a function returns, while a yield to anything else stores into `result` and branches to `end`, where `emitScope` loads the value.
- **Unreachable code.** `emitStmts` stops once the current block has a terminator, since sema already warned about the rest. A void function that falls off its end returns, and any other function ending in a block nothing reaches ends with `unreachable`.
- **`match`.** `emitMatch` emits the subject once and a chain of tests: catch-alls branch straight to their body, a guard is its own condition, and `matchesLiteral` compares a literal with the subject. A binder stores the subject in its arm's slot. Every arm shares the match's end and slot, so a bare yield in an arm and `-| match` land in the same place. Past the last arm, a match with a value is `unreachable`, since sema made it cover everything, and a void match goes to its end. The equality chosen in commit 1.7 takes effect here: deep compares floats by value, so `-0.0` matches a `0.0` arm, and `match (=, x)` compares their 64 bits as integers, so it doesn't.
- **Loops.** `emitRepeat` builds a header that tests a while loop's condition, where an infinite loop's header is its body, and a body that didn't leave branches back. A bare yield and `-| repeat` branch to the end, and only an infinite loop gets a result slot. Invariants and `wit` statements emit nothing.
- **Assertions.** An `assert` branches on its condition to a block that calls `trap`, the runtime's `cheri_trap`, with the statement's source text and line, then `unreachable`. The trap is marked `NoReturn`, so LLVM drops anything after it.

### Sema

`Sema::check` takes `entry`, and when it's set, `checkMain` looks `main` up in the global scope and checks it has the type `() : int`. `FnDecl`, `Param`, and `BinderPattern` gain a `decl`, which sema fills in alongside `IdentifierExpr`'s, so codegen never looks a name up again. With the symbol on the declaration, `checkFn` reads the signature from `fn.decl->type` instead of a separate map.

### Runtime

`runtime/runtime.c` is `libcheri_rt`, a small C library built with the compiler. It defines C's `main`, which calls `cheri_init` and then `cheri_main` and hands the result back as the exit code, declaring them like any C function:

```c
void cheri_init(void);
long long cheri_main(void);
```

`cheri_trap` prints `trap at line N: message` to stderr and aborts. It's where every run-time check goes.

### Driver

`tools/cheri/main.cpp` reads the command line into `Options` with `parseOptions`, rejecting an unknown command or option with `cheri: ..., see 'cheri help'`, and `run` with `-o` or `--emit`, since it keeps nothing. It runs the front end, stopping on any error, then generates the module, named after the file's stem, and verifies it. It sets up the `TargetMachine` with every target initialized, PIC unless `--static`, and the backend's level matched to `-O`, then `optimize` runs `PassBuilder`'s per-module default pipeline, nothing at `-O0`. `writeMachineCode` writes an object or assembly, and IR and bitcode are written directly. An executable writes its object next to the output, links it with the runtime through the linker, and removes the object. Linking and running go through the shell, so `quote` quotes every path, and a file or directory with spaces in its name works.

### Build

`CMakeLists.txt` finds LLVM 21.1 through `llvm-config --cmakedir`, since distributions install it under prefixes CMake doesn't search, and links it only into `cheri` through the `cheri_llvm` interface target, whose headers are marked `SYSTEM` so their warnings stay out. It builds `cheri_rt` and bakes its path into `cheri` as `CHERI_RUNTIME`, so builds work from any directory, and registers every file in `tests/run/` with ctest as `Run: <name>`. `scripts/build.sh` configures with clang.

### Docs

`README.md` introduces the language and what works so far.

## Tests

`scripts/run_test.sh` runs one program test. Each file's first line is the exit code it should produce, and a program that should trap names the trap on a `# trap:` line:

```
# exit: 134
# trap: trap at line 5: assert (n % 2 = 0);
# checks that a false assertion traps with its line, which aborts the program
let (n :: int) -> half -> (r :: int) { ... }
```

The script builds it with the real `cheri` at `-O0` and at `-O3`, runs it with a ten-second limit so a loop that never ends fails instead of hanging the run, compares the exit code, and checks that stderr holds the named trap. These test the whole toolchain end to end: the driver, codegen, optimization, the runtime, and linking.

`tests/run/` has 29 programs covering literals in every base and with separators, arithmetic, bitwise operators, short-circuiting, locals and assignment, calls and pipes including void calls, blocks and labeled yields, early exits, every form of match including float equality and guards, while and infinite loops, nested functions, including ones in assertions and guards, chunks, top-level bindings and a top-level loop, the entry point, and assertions that hold and fail. `tests/unit/sema/test_sema.cpp` covers the entry point. 3320 unit assertions and 29 programs, 0 failures.

## Current Limitations

- **Decimal floats only.** Hexadecimal, octal, and binary floats aren't lowered.
- **One `int` and one `float`.** Integer arithmetic wraps instead of trapping on overflow, and dividing by zero is undefined, until the numeric model does both.
- **One file.** Programs are a single file until modules exist.
