# Commit 5: LLVM Lowering & the First Executable

## The Problem

After commit 4, cherimoya can tell whether a program is right, but it can't run one. Sema has resolved every name and typed every expression, and nothing turns that into instructions. Commit 5 does: `cheri build prog.moya` checks the program, lowers it to LLVM IR, optimizes it, writes an object file, and links it with a small runtime into an executable whose exit code is what `main` yields.

## The Pipeline

`cheri build <file>` runs every stage in order:

1. Lex, parse, and check, stopping on any error, since codegen only ever sees programs sema accepted.
2. Lower the checked AST to an LLVM module.
3. Verify the module. Broken IR is a codegen bug, so it's printed next to the verifier's complaint and the build stops.
4. Optimize with LLVM's standard pipeline.
5. Write a native object file through LLVM's target machine, as position-independent code, since `cc` links executables as PIE by default.
6. Link the object with the runtime through `cc`, and remove the object.

Every setting LLVM offers on the way passes through the command line, and `cheri help` lists them:

- **What to produce:** `--emit` stops at an object file, assembly, IR, or bitcode instead of an executable, with `-c`, `-S`, and `--emit-llvm` as shorthands. IR goes to the terminal unless `-o` names a file. `--emit-llvm -O0` shows exactly what codegen wrote, which is the version to read when debugging codegen.
- **How hard to optimize:** `-O0` through `-O3`, `-Os`, and `-Oz`, with `-O3` by default.
- **What to build for:** `--target` cross-compiles to any triple LLVM supports, and `--cpu` and `--features` tune the code, with `--cpu native` meaning the machine cheri runs on.
- **How to link:** `--linker` picks the program that links, `--static` makes a static executable, and `-L`, `-l`, and `--link-arg` pass straight through.

`cheri run` builds into the temporary directory, runs the program, removes it, and exits the way the program did, where a program killed by a signal exits with 128 plus the signal, as a shell reports it. `cheri check` stops after the front end.

The module is named after the file, and every function's symbol is its module and name joined with dots: `main` in `prog.moya` is `prog.main`, and a function `g` nested in `f` is `prog.f.g`. Dots are legal in LLVM and ELF symbols, they stay readable in a debugger, and modules slot into the same scheme later.

## The Runtime

Every program links with `libcheri_rt`, a small C library built with the compiler. It defines C's `main`, which calls the program's init function and then its `main`, and hands the result back as the exit code. Cherimoya's `main` and init aren't legal C names, so the runtime reaches them through asm labels:

```c
extern long long cheri_entry(void) __asm__("cheri.main");
```

Codegen exports the entry module's `main` and init as `cheri.main` and `cheri.init` with aliases, so the runtime never needs to know the module's name. The runtime also defines `cheri.trap`, which reports a failed runtime check with its line and aborts. It's where every future trap goes: overflow, division by zero, NaN, and bounds checks.

The runtime's path is baked into `cheri` when it's built, so `cheri build` works from any directory.

## Types

Sema's core types map directly: `int` is `i64`, `bool` is `i1`, `float` is `double`, and `void` and `never` are `void`. The error type never reaches codegen, since a program with an error stops before it, so lowering one is a codegen bug, reported with `llvm_unreachable`. Sema interns its types, so each one is lowered once and cached by pointer.

## Declaring Before Defining

Functions are declared before any body is emitted, the same order sema checks them in, so a call can reach a function defined later. Declaring walks the whole program, including inside function bodies and the blocks, matches, loops, and chunks in their expressions, so a function nested anywhere is found and named under the function around it. Nested functions can't capture their parent's locals, so each one is compiled as its own LLVM function, exactly like a top-level one. Externs are declared and never defined, for the linker to fill in.

Sema writes the symbol it resolved onto the AST, and codegen reads it. Identifiers already had theirs, and function declarations, parameters, and binder patterns gained one, so codegen never looks a name up again.

## Locals as Stack Slots

Every local and parameter gets a stack slot, made with `alloca` at the top of the function's entry block. A binding stores into its slot, a name loads from it, and an assignment stores again. Parameters are copied into slots on entry, so they behave exactly like bindings, including a `mut` parameter being assigned.

This keeps codegen free of SSA bookkeeping: it never has to track which value a variable has on which path. The optimizer's mem2reg pass turns every slot back into registers, with phi nodes where control joins. Slots live in the entry block because mem2reg only promotes those, and because a slot made inside a loop's body would take new stack space on every iteration. A factorial loop written with two `mut` locals comes out of the optimizer with no memory traffic at all:

```llvm
repeat.body:
  %i.07 = phi i64 [ %1, %repeat.body ], [ 2, %definition ]
  %product.06 = phi i64 [ %0, %repeat.body ], [ 1, %definition ]
  %0 = mul i64 %i.07, %product.06
  %1 = add i64 %i.07, 1
```

## Top-Level Bindings

A binding outside every function is a global, named like a function: `LIMIT` in `prog.moya` is `prog.LIMIT`. Everything at the top of a file that isn't a function, bindings and statements alike, runs once, in order, in a generated `prog.init`, which the runtime calls before `main`. A binding whose initializer folds to a constant needs no code: the constant becomes the global's starting value, and if the binding isn't `mut`, the global is marked constant so the optimizer can replace every read with the value. Anything else is computed in init and stored. Functions read and assign globals exactly like locals, through the same lookup.

## Expressions

- **Literals** are kept raw by the lexer, so codegen strips the separators and reads the prefix to pick the radix, through LLVM's arbitrary-width integers. Floats are decimal only for now.
- **Arithmetic and bitwise operators** pick integer or float instructions from the left operand's type, which sema made match the right one. Division and remainder are signed, since `int` is.
- **Comparisons** are signed for ints and ordered for floats, which are NaN-free.
- **Chains** emit each operand once and join the neighboring comparisons with `and`.
- **`&&` and `||`** short-circuit. The right side gets its own block, and a phi at the join picks the answer: the decided value when control came straight from the left side, and the right side's value otherwise. The phi names the block the left side finished in, which isn't always the one it started in.
- **Calls and pipes** look their callee up among the declared functions. Both pipes are one-argument calls.

## Scopes and Yields

Codegen mirrors sema's yield-target stack. Every function, block, match, arm, loop, and chunk pushes an exit while its insides are emitted, holding the block where control continues after it and, when it has a value, a stack slot for that value. A yield is resolved with sema's rules and becomes a branch: a yield to the function returns, and a yield to anything else stores its value in the target's slot and jumps to its end. The scope then loads its value there.

A yield ends its block, so the statements after it in the same list can't run, which sema already warned about, and codegen stops emitting them. A block whose last statement didn't leave falls through to its end. A function that ends in a block nothing reaches, like one where every path already returned, ends with `unreachable`, and a void function that falls off its end returns.

## Match

A match is a chain of tests. Each arm tests its pattern and either runs its body or falls to the next arm's test: catch-alls always match, a guard is its own condition, and a literal compares against the subject, which is emitted once. A binder stores the subject in a slot for its arm. Every arm shares the match's end and value slot, so a bare yield in an arm and `-| match` land in the same place. Past the last arm, a match with a value can't get there, since sema made it cover everything, so it's `unreachable`, and a void match goes to its end.

The equality a match chose in commit 4.2 takes effect here. Deep equality compares floats by value, so `-0.0` matches a `0.0` arm, and `match (=, x)` compares their 64 bits as integers, so it doesn't.

## Loops

A loop is a header that decides whether to run the body again. A while loop's header tests its condition and goes to the body or the end, and an infinite loop's header is its body. A body that didn't leave goes back to the header. A bare yield in the body and `-| repeat` both branch to the loop's end, and only an infinite loop has a value slot, since only it can yield a value. Invariants are for the verifier and emit nothing, and so do `wit` statements.

## Assertions

`assert (cond)` branches on its condition. When it holds, the program continues. When it doesn't, it calls `cheri.trap` with the assertion's own source text and line, and the program stops:

```
trap at line 3: assert (n % 2 = 0);
```

The trap is marked as never returning, so LLVM drops anything after it.

## The Entry Point

A program that's built needs a top-level `main` declared as `let () -> main -> (code :: int)`, which is what the runtime calls: `cherimoya requires a program to have a 'main' function, please add 'let () -> main -> (code :: int) { 0 -| }'`. Sema checks it when asked, since a program being checked on its own, like every sema test, doesn't need one, and the driver always asks. A nested function named `main` doesn't count.

## Testing Whole Programs

Unit tests check each stage in C++. Commit 5 adds a second kind of test: whole programs in `tests/run/`, each one a `.moya` file whose first line is the exit code it should produce:

```
# exit: 120
# checks a while loop with mutable locals
let (n :: int) -> factorial -> (r :: int) { ... }
```

Every file is registered with ctest as `Run: <name>`. A script builds it with the real `cheri`, runs it with a ten-second limit, and compares the exit code, so a loop that never ends fails its test instead of hanging the run. These test the whole toolchain end to end: the driver, codegen, optimization, the runtime, and linking.

## What Was Built

`src/codegen/codegen.hpp` is the `Codegen` class. `runtime/runtime.c` is the runtime. `tools/cheri/main.cpp` is the `cheri` driver, with `build`, `run`, `check`, and `help`. `scripts/run_test.sh` runs one program test. `CMakeLists.txt` finds LLVM through `llvm-config`, links it only into what does codegen with its headers marked as system headers, builds the runtime, and registers every program test, and `scripts/build.sh` builds with clang. Sema gained the entry-point check and records the symbols of function declarations, parameters, and binder patterns, and the parser's nodes gained the fields for them.

`tests/run/` has 26 programs covering literals in every base, arithmetic, bitwise operators, short-circuiting, locals and assignment, calls and pipes, blocks and labeled yields, early exits, every form of match including float equality, while and infinite loops, nested functions, chunks, top-level bindings, and assertions that hold and fail. `tests/unit/sema/test_sema.cpp` gained the entry point. 3297 unit assertions and 26 programs, 0 failures.

## Current Limitations

Floats are decimal only, and hexadecimal, octal, and binary floats aren't lowered. Integer arithmetic wraps instead of trapping on overflow, and dividing by zero is undefined, until the numeric model does both. There's one `int` and one `float`. Programs are a single file.

## What's Next

Commit 6 builds the full numeric model: every integer width with trapping and wrapping arithmetic, every float type with NaN traps and the NaN total order, FP8 and bfloat16, conversions, and literals that adapt to their expected type.
