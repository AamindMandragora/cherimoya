# Commit 12.1: Debug Info

## The Feature

- **Two levels of debug info.** `-g` gives a debugger every function by name, the line every instruction came from, every binding and input with its type, and every scope, so a variable only shows inside it. `-gline-tables-only` gives function names and lines, enough for breakpoints by name and backtraces, and is smaller. Without either flag a build is exactly what it was. `cheri run` takes `-g` too.
- **Any optimization level.** `-O3` is the default, and a crash in an optimized build is the one that most needs a backtrace. At `-O3` a function inlined into its caller has a breakpoint location in each, and a variable the optimizer removed prints as `<optimized out>`. `-g -O0` shows everything.
- **Source names.** A debugger says `clamp`, not `cheri.prog.clamp`, and a nested function is `outer.inner`. An input's default is its own function, named the way a default is written, `clamp.lo<-`, both in the debugger and as its symbol, `cheri.prog.clamp.lo<-`. gdb takes it unquoted, `break clamp.lo<-`. The function that runs the top level is `init`, marked artificial.
- **The entry point is `start`.** A program's entry is `let () -> start -> (code :: int)`, which the runtime's C `main` calls after `init`. A program without one gets `cherimoya requires a program to have a 'start' function, please add 'let () -> start -> (code :: int) { 0 -| }'`, with a fix that adds it, and a `start` declared any other way gets `cherimoya requires 'start' to be declared as 'let () -> start -> (code :: int)', please change its inputs and outputs`.
- **Types under their cherimoya names.** Every value is described as `i8u` or `f16`, and immutable bindings and inputs are `const`, so gdb refuses `set var` on one:

| Type | Described as |
|---|---|
| integers | signed or unsigned integers of their width |
| `bool` | a boolean |
| `char`, `rune` | an unsigned character, and a UTF character |
| `boolf` | fixed-point, scaled by 1/255, so 179 prints as 0.70196078431 |
| `f16`, `f32`, `float` | floats |
| FP8, `f16e8m7`, `f128` | their raw bits, which the embedded printers decode |

- **Printers inside the binary.** A `-g` build on ELF carries gdb pretty-printers in its `.debug_gdb_scripts` section, the way Rust ships its own, so they go wherever the binary goes. gdb runs them once the binary's directory is in its auto-load safe-path, for example `add-auto-load-safe-path ~/projects` in `~/.gdbinit`. FP8, bfloat16, and `f128` decode exactly, `f128` keeping its 34 significant digits. `i8`, `i8u`, `i8w`, and `i8uw` print as numbers, `-5`, like every wider integer. A `rune` prints as its code point and character, `955 'λ'`, with cherimoya's escape for one that doesn't print, `'\u{7}'`.

```
$ gdb ./prog
(gdb) break clamp
(gdb) run
Breakpoint 1, clamp (n=150, lo=0) at prog.moya:3
(gdb) info locals
fuzzy = 0.70196078431
tiny = 1.75
```

## Why

A program that misbehaves can only be read, since a debugger sees its binary as machine code with mangled symbols and no lines. A trap says which line it came from, but not how the program got there or what any value was. Debug info lets gdb and lldb break on a function by name, step through lines, print every binding and input with its type, and give a backtrace in the source's terms.

- **The line-table level still names functions.** LLVM's own line-table level leaves out functions, which a C program doesn't miss, since its symbols are already its functions' names. Cherimoya's symbols are `cheri.prog.clamp`, so `-gline-tables-only` keeps each function's entry with its name and nothing else.
- **No linkage names.** gdb shows a C unit's linkage name as the function's name, which would bring back the mangled symbol, so the debug info gives none and gdb finds each function's code by its address.
- **`start`, not `main`.** With the program's entry and the runtime's C `main` both called `main`, `break main` would stop in both.
- **`<-` in a default's name.** No name can hold `<-`, so a default's function can't collide with a nested function, and a breakpoint on `clamp` doesn't also catch its default.
- **C as the source language.** C's expressions are the ones gdb parses best, and cherimoya's simple ones read the same. The cost is that C's rules print a 1-byte integer as a char and a 32-bit character as a hex escape, which the printers fix.
- **Raw bits for some floats.** DWARF has no encoding for FP8 or bfloat16, and gdb picks a float's encoding by its size and C name, so it would read a 16-bit float as IEEE half and a 128-bit one as x87's extended format. Describing them as their bits and decoding them in a printer is the only way to print them right.
- **Printers embedded, not installed.** A script inside the binary can't fall out of step with it or be forgotten on another machine.
- **Breakpoints after the inputs are stored.** Storing a function's inputs into their slots has no line, so a breakpoint on a function lands after them, the way clang does it, and `info args` shows real values.

## Implementation

### Sema

`checkMain` in `src/sema/sema.hpp` becomes `checkStart`, which looks up `start` in the global scope and checks it has the type `() -> int`. `check` calls it when `entry` is set.

### Codegen

- **Locations.** `src/codegen/codegen.hpp` gains `debug`, a `std::unique_ptr<DebugInfo>` that is null for a build without debug info. `Located` is an RAII guard that points every instruction the builder emits at a span and puts the previous location back when it goes. `emitExpr` and `emitStmt` each open one, so an operator's own instructions point at the operator once its operands are done.
- **Functions.** `beginFunction` names the subprogram by its symbol without `symbolPrefix`, which gives `outer.inner` and `clamp.lo<-`, and clears the current location, so storing the inputs has no line. `define` opens a `Located` at the body's closing brace before the final return, and `defineDefault` one at the default's value, since a debugger only shows the variables of a function whose last instruction has a line. `generate` opens `init` with `isArtificial` set and calls `debug->finish()` at the end.
- **Default names.** `declare` names a default's function `name + "." + input + "<-"`.
- **Variables and scopes.** `makeSlot` takes the input's number from 1, or 0 for a binding, and calls `declareVariable`. A match arm's binder gets its slot inside the arm's scope. `emitScope`, which now takes the scope's span, `emitMatch` for each arm, and `emitRepeat` for its body call `enterScope` and `leaveScope` around their statements. `makeGlobal` calls `declareGlobal` for every top-level binding.
- **Entry.** `generate` exports the top-level `start` as `cheri_start`.

### Debug Info

`src/codegen/debug.hpp` is new.

- **Options.** `DebugLevel` is `None`, `Lines`, or `Full`. `DebugOptions` holds the level, the source path, `isOptimized`, `isCodeView`, and `isELF`.
- **`DebugInfo`** wraps LLVM's `DIBuilder`. Its constructor builds `lineStarts`, so `lineCol` finds a span's line and column by binary search, and creates the compile unit as `DW_LANG_C11` with LLVM's full emission kind at both levels. It sets DWARF version 5, or the `CodeView` module flag for a Windows MSVC target.
- **Types.** `typeOf(SemaType*)` makes each debug type once, cached by sema's interned pointer in `types`. Integers are `DW_ATE_signed` or `DW_ATE_unsigned`, `f16`, `f32`, and `f64` are `DW_ATE_float`, and FP8, bfloat16, and `f128` are `DW_ATE_unsigned` of their width. `bool` is `DW_ATE_boolean`, `char` `DW_ATE_unsigned_char`, `rune` 32-bit `DW_ATE_UTF`, and `boolf` a `createRationalFixedPointType` with factor 1/255, or its plain byte under CodeView, which has no fixed-point types. `typeOf(const Symbol&)` wraps the type in `DW_TAG_const_type` unless the symbol is mutable. The function overload gives a subroutine type with the result and inputs only at `Full`, so a line table's functions share the empty one.
- **Scopes.** `scopes` is a stack: `beginFunction` sets it to the new subprogram, `enterScope` pushes a lexical block, and `leaveScope` pops it, both only at `Full`. `location` builds a `DILocation` in the innermost one.
- **Variables.** `declareVariable` makes a parameter variable in the function's scope for an input, or an auto variable in the current scope for a binding, and inserts a `dbg.declare` on its slot. `declareGlobal` attaches a global variable expression. Both do nothing below `Full`.
- **Printers.** `embedPrinters` writes the script into an internal global, `cheri.gdb.printers`, in section `.debug_gdb_scripts`, prefixed by the inline-script byte `\x04` and the name `cherimoya-printers`, and adds it to `llvm.used` so the section isn't dropped. `finish` calls it only at `Full` on ELF, then finalizes the builder.

`src/codegen/printer.hpp` holds the printers as a Python raw string, `gdbPrinters`. `decode` turns a float's bits into its value through `Fraction`, using a `Decimal` context of 34 digits for `f128`. `FloatPrinter`, `BytePrinter`, and `RunePrinter` are picked by `lookup` from the type's name with `const` stripped, and the lookup is appended to the objfile's printers.

### Runtime

`runtime/runtime.c` declares `cheri_start` in place of `cheri_main`, and its C `main` calls `cheri_init` then `cheri_start`.

### Driver

`tools/cheri/main.cpp` gains `-g` and `-gline-tables-only`, which set `Options::debug`. The target triple is resolved before codegen, since it decides `isCodeView` and `isELF`. A build with debug info passes `-g` to the linker, which a CodeView build needs to write its PDB.

### Build

`CMakeLists.txt` finds `llvm-dwarfdump` and adds a `Debug:` test for every file in `tests/debug/`. `scripts/debug_test.sh` builds a test with its `# flags:` line, `-g -O0` by default, checks every `# dwarf:` line against `llvm-dwarfdump --debug-info --debug-line`, then runs the `# gdb:` lines as one batch gdb session with `set auto-load safe-path /` and checks its output for every `# expect:` line. Without gdb only the first half runs.

## Tests

`tests/debug/` adds `functions.moya`, breakpoints by name, inputs once they're stored, a default's function, and the backtrace up to `start`; `variables.moya`, every type's value through the printers, `const`, a top-level binding, and a binder's scope; `lines.moya`, a line table alone; `trap.moya`, a trap's backtrace and the values that made it trap; and `optimized.moya`, a breakpoint in an `-O3` build. Every program in `tests/run/` is renamed from `main` to `start`, and `entry_main.moya` becomes `entry_start.moya`. 14476 unit assertions, 61 programs, and 5 debug programs, 0 failures, and every program in `tests/run/` builds with both levels at `-O0` and `-O3` with IR that verifies.

## Current Limitations

- **lldb.** lldb reads the same DWARF but doesn't run gdb's printers, so it shows FP8, bfloat16, `f128`, and 1-byte integers the way gdb would without them.
- **CodeView.** The CodeView build is only checked to produce its sections, not debugged, and the printers are only embedded in ELF binaries.
- **`ptype` of a `boolf`** shows its fixed-point layout rather than its name.
- **Endianness.** The printers read values as little-endian.
- **C expressions.** Expressions in the debugger follow C's rules, so `print a + b` on two `i8`s is C arithmetic, not cherimoya's, which would trap.
