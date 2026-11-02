# Commit 5.2: Debug Info

## The Problem

A cherimoya program that misbehaves can only be read, since a debugger sees its binary as machine code with mangled symbols and no lines: a trap says which line it came from, but not how the program got there or what any value was. Commit 5.2 writes debug info alongside the code, so gdb and lldb can break on a function by name, step through lines, print every binding and input with its type, and give a backtrace in the source's terms.

## Two Levels

Debug info is only written when it's asked for, so a build without it is exactly what it was before:

| Flag | What a debugger gets |
|---|---|
| `-g` | every function by name, the line every instruction came from, every binding and input with its type, and every scope, so a variable only shows inside it |
| `-gline-tables-only` | function names and lines, enough for breakpoints by name and backtraces, and smaller |

Either works at any optimization level, since `-O3` is the default and a crash in an optimized build is the one that most needs a backtrace. At `-O3`, the optimizer can inline a function into its caller, so a breakpoint on it has a location in each, and a variable it optimized away prints as `<optimized out>`. `-g -O0` is the build that shows everything. `cheri run` takes `-g` too.

LLVM's own line-table level leaves out functions, which a C program doesn't miss, since its symbols are already its functions' names. Cherimoya's symbols are `cheri.prog.clamp`, so `-gline-tables-only` keeps each function's entry, with its name and nothing else.

## Names

A debugger names everything the way the source does: `clamp`, not `cheri.prog.clamp`, and a nested function as `outer.inner`. An input's default compiles to a function of its own, and its name is the input's, written the way a default is, `clamp.lo<-`, both in the debugger and as its symbol, `cheri.prog.clamp.lo<-`, which no other name can collide with, since no name holds `<-`. gdb takes it unquoted: `break clamp.lo<-`. The function that runs the top level is `init`, marked artificial, since nobody wrote it.

The debug info gives no linkage names, since gdb reads a C unit's linkage name as the function's name and would show the mangled symbol instead. It finds each function's code by its address.

A program's entry is `let () -> start -> (code :: int)`, so it never shares a name with the C `main` the runtime defines, which the C library calls to run `init` and then `start`. With both called `main`, `break main` would stop in both. A program without a `start` gets `cherimoya requires a program to have a 'start' function`, with the fix that adds one.

## Types

Every value's type is described under its cherimoya name, `i8u` or `f16`, and immutable bindings and inputs are `const`, the way the language treats them, so gdb refuses `set var` on one:

| Type | Described as |
|---|---|
| integers | signed or unsigned integers of their width |
| `bool` | a boolean |
| `char`, `rune` | an unsigned character, and a UTF character |
| `boolf` | fixed-point, scaled by 1/255, so 179 prints as 0.70196078431 |
| `f16`, `f32`, `float` | floats |
| FP8, `f16e8m7`, `f128` | their raw bits, which the embedded printers decode |

DWARF has no encoding for FP8 or bfloat16, and gdb picks a float's encoding by its size and C name, so it would read a 16-bit float as IEEE half and a 128-bit one as x87's extended format. The debug info names the source language as C, since C's expressions are the ones gdb parses best, and C's rules print a 1-byte integer as a char and a 32-bit character as a hex escape.

## The Printers

For the values gdb would print wrong, a `-g` build on ELF carries gdb pretty-printers inside the binary, as a Python script in its `.debug_gdb_scripts` section, the way Rust ships its own, so they go wherever the binary goes. gdb runs a script it finds there once the binary's directory is in its auto-load safe-path. The printers find each value by its type's name:

- **FP8, bfloat16, and `f128`** decode their bits exactly, with `f128` keeping its 34 significant digits.
- **`i8`, `i8u`, `i8w`, and `i8uw`** print as numbers, `-5`, like every wider integer.
- **`rune`** prints as its code point and the character, `955 'λ'`, the way a `char` prints, with cherimoya's escape for one that doesn't print, `'\u{7}'`.

## Codegen

`src/codegen/debug.hpp` writes the debug info through LLVM's `DIBuilder` as codegen emits the code. Every function gets a subprogram before its body, every statement and expression points the instructions it emits at its start, and puts the location before it back when it's done, so an operator's own instructions point at the operator once its operands are done. Storing a function's inputs into their slots has no line, which is how a breakpoint on the function lands after them, the way clang does it, and every function's last instruction has a line, its closing brace or a default's value, since a debugger only shows the variables of a function that has one.

With `-g`, every slot gets a variable: an input in its function, numbered from 1, and a binding, including a match arm's binder, in the scope it's declared in. Blocks, chunks, match arms, and loop bodies each get a lexical block, and every top-level binding's global gets a global variable.

DWARF is version 5. A Windows MSVC target gets CodeView instead, which Visual Studio and WinDbg read, and the linker is asked to write its PDB. CodeView has no fixed-point types, so there a `boolf` is its byte, and the gdb printers are only embedded in ELF binaries.

## Testing

`tests/debug/` holds programs built with debug info, each one built with `-g -O0` unless a `# flags:` line says otherwise. `scripts/debug_test.sh` checks every `# dwarf:` line against what llvm-dwarfdump shows of the debug info, then runs the `# gdb:` lines as one gdb session and checks its output for every `# expect:` line, where a machine without gdb only gets the first half. The programs check breakpoints by name, inputs once they're stored, a default's function, the backtrace up to `start`, every type's value through the printers, `const`, a binder's scope, a line table alone, a trap's backtrace and the values that made it trap, and a breakpoint in an `-O3` build.

## What Was Built

`src/codegen/debug.hpp` gained `DebugInfo`, and `src/codegen/printers.py` holds the printers' script, which CMake embeds as a raw string in a generated `printers.hpp`, so it stays a Python file of its own. `src/codegen/codegen.hpp` creates the debug info, points every statement and expression at its span, describes every slot and global, opens a lexical block for every scope, and names defaults `name.input<-`. `src/sema/sema.hpp` checks for `start`, `runtime/runtime.c` calls it, and `tools/cheri/main.cpp` gained `-g` and `-gline-tables-only`. `CMakeLists.txt` and `scripts/debug_test.sh` run the debug tests, and the README has a section on debugging.

14476 unit assertions, 61 programs, and 5 debug programs, 0 failures, and every program in `tests/run/` builds with both levels at `-O0` and `-O3` with IR that verifies.

## Current Limitations

lldb reads the same DWARF but doesn't run gdb's printers, so it shows FP8, bfloat16, `f128`, and 1-byte integers as gdb would without them, and the CodeView build is only checked to produce its sections, not debugged. `ptype` of a `boolf` shows its fixed-point layout rather than its name. The printers read values as little-endian. Expressions in the debugger follow C's rules, so `print a + b` on two `i8`s is C arithmetic, not cherimoya's, which would trap.
