# How cheri Works

This is the implementation spec: how the compiler is built and why. It's written for contributors and for anyone who wants to know how cherimoya works inside. To learn the language, read [the book](../docs/book/); for what each construct means, read [the reference](../docs/reference.md).

Each commit has a design doc, `commit_N.M.md`, where `N` is the concept it changes most and `M` counts commits within that concept. Each doc follows the same order:

1. **The Feature**: what was added or fixed.
2. **Why**: what it's for, and the reason behind each decision.
3. **Implementation**: one subsection per part of the compiler the commit touched.
4. **Tests**.
5. **Current Limitations**.

The concepts below run from least to most advanced. A concept's later commits list the work planned for it, so the index is also the roadmap. Commits within a concept aren't always built in order, and git history shows how far along each one is.

## The Pipeline

`cheri` is a header-only C++17 program over LLVM. A source file goes through these stages:

| Stage | Files | Does |
|---|---|---|
| Source | `src/source/source.hpp` | loads files, maps offsets to lines and columns |
| Lexer | `src/lexer/` | tokens, the operator table, keywords, escapes |
| Parser | `src/parser/` | a Pratt parser for expressions, recursive descent for everything else, and a round-trip printer |
| Sema | `src/sema/` | names, types, places, modes, and the control-flow graph that ownership is checked on |
| Codegen | `src/codegen/` | LLVM IR, DWARF, and the embedded gdb printers |
| Runtime | `runtime/runtime.c` | traps and allocation |
| Diagnostics | `src/diagnostics/diagnostics.hpp` | errors, notes, and fixes, shared by every stage |
| Language server | `src/lsp/` | the protocol, open documents, and every editor feature |
| Driver | `tools/cheri/` | the commands, `build`, `run`, `check`, `test`, `lsp`, and linking |

Editor clients live in `editors/` (VS Code, Emacs, Zed), and `tree-sitter/` holds a grammar that reads programs exactly the way `cheri` does.

## Architecture

- **Arena ownership for interned objects.** `TypeContext` owns every `SemaType` in a `vector<unique_ptr<SemaType>>`, and everything outside it refers to types by raw pointer. Each distinct type exists once, so type equality is pointer equality.
- **`SemaType` has no `Span`.** Source locations live on AST nodes.
- **AST annotations are direct.** Every `Expr` gets a `SemaType* type`. Identifiers, declarations, inputs and binders get their resolved `Symbol*`, so later passes read the answers instead of working them out again.
- **Symbols** live in a `std::deque`, so a `Symbol*` stays valid after its lexical scope is gone. Each symbol records one mutability per level.
- **Lexical scopes** are sema's name-resolution scopes. They're separate from the grammar's scopes.
- **The yield-target stack** has one entry per enclosing scope: its kind, an optional label, the loop's binder names and the expected yield type. Every `-|` resolves against it, and codegen mirrors it.
- **Error poisoning.** The error type fits anywhere and propagates silently, so one mistake gives one diagnostic.
- **Codegen.** Locals are entry-block allocas that mem2reg promotes, and every run-time check goes through one trap helper.
- **Diagnostics** are structured: a span, notes and fixes, built by the stage that finds the mistake. For the ownership passes, the diagnostics are the product.
- **Testing** has three tiers:
  1. unit tests in C++ (`tests/unit/`);
  2. the run-time basics only a whole program can show (`tests/run/`, `tests/test/`, `tests/debug/`);
  3. feature tests in `.toya` files (`tests/features/`), which run at `-O0` and `-O3` only once the first two tiers pass.

## Concepts

### 1. Foundations

The pipeline itself, from tokens to the first executable.

- [1.1](commit_1.1.md) Lexer
- [1.2](commit_1.2.md) No Macros, and Protocols
- [1.3](commit_1.3.md) The Pratt Parser and the Expression Tree
- [1.4](commit_1.4.md) Types, Statements, Scopes and Patterns
- [1.5](commit_1.5.md) Declarations and the Round-Trip Printer
- [1.6](commit_1.6.md) Names, Types and Expressions
- [1.7](commit_1.7.md) Yields, Control Flow and Places
- [1.8](commit_1.8.md) Named Arguments and Defaults
- [1.9](commit_1.9.md) LLVM Lowering and the First Executable

### 2. Numbers

- [2.1](commit_2.1.md) Integers
- [2.2](commit_2.2.md) Floats
- [2.3](commit_2.3.md) FP8
- [2.4](commit_2.4.md) Fuzzy Booleans
- Planned: units, which are distinct subtypes of number types with conversions to SI carried by stdcheri

### 3. Text

- [3.1](commit_3.1.md) Characters and Annotations
- [3.2](commit_3.2.md) Strings
- Planned: formatting and printing, joining

### 4. Arrays and Pointers

- [4.1](commit_4.1.md) Fixed-Width Arrays
- [4.2](commit_4.2.md) Heap Arrays and Pointers
- [4.3](commit_4.3.md) Views, Modes and the Control-Flow Graph
- [4.4](commit_4.4.md) Brackets for Every Type Argument
- [4.5](commit_4.5.md) Typed Addresses and Equality on Handles
- Planned: uninitialized storage, allocators

### 5. Collections

- [5.1](commit_5.1.md) Ranges, For-Each Loops and Slices (next)
- 5.2 Tuples and destructuring
- 5.3 Range, pack and set-builder patterns, and intervals
- Planned: comprehensions, folds and loops whose headers yield values; dictionaries

### 6. Types

- 6.1 Records, sums (`Maybe[T]`, `Maybe[T, E]`), enums, type definitions, multiple outputs, full exhaustiveness
- 6.2 Operator overloading, protocols, comptime with an evaluator in the compiler, extra operators, effect types, and generics as comptime functions over `type`, a type whose values exist only when compiling, so a type constructor like `arr` or `Maybe` is a function from `type` to `type` and protocols can take one, `Mappable[F]`, with no universe levels
- Planned: generic instantiations interned by head, arguments, and type-level witnesses, so witness identity stays pointer equality

### 7. Functions

- 7.1 Closures, `Fn`, functions and operators as values

### 8. Contracts

- 8.1 Z3 contracts on integers, booleans, arrays, and ranges: preconditions and postconditions with `wit`, loop invariants, counterexamples, and checks that are proven removed (built right after 5.3)
- Planned: refinements and `old`, assumptions, fuzzing against contracts, the verifier moving onto the control-flow graph

### 9. Modules and stdcheri

- Planned: modules and `std.float`, stdcheri, then the launch: a WASM playground, benchmarks and a community

### 10. Concurrency

- Planned: parallel folds over a work-stealing pool, then actors, green threads and threads

### 11. Interop

- Planned: FFI with `extern` and `ptr[void]`, WASM, a GPU backend

### 12. Tooling

- [12.1](commit_12.1.md) Debug Info
- [12.2](commit_12.2.md) The Language Server and VS Code
- [12.3](commit_12.3.md) Emacs
- [12.4](commit_12.4.md) Doc Comments on Hover
- [12.5](commit_12.5.md) Quick Fixes
- [12.6](commit_12.6.md) Inlay Hints
- [12.7](commit_12.7.md) A Tree-sitter Grammar
- [12.8](commit_12.8.md) A Zed Extension
- [12.9](commit_12.9.md) Comments for Groups of Declarations
- [12.10](commit_12.10.md) cheri test
- [12.11](commit_12.11.md) Feature Tests
- [12.12](commit_12.12.md) The Spec, the Reference and the Book
- 12.13 Agent tooling: `cheri guide`, printing sections of the book and the reference, and `--diagnostics=json` with fixes
- Planned: `cheri fmt`, `cheri doc`, `cheri bench`, a REPL with a JIT, custard (the package manager), hot reload

### Not Placed Yet

Safety tiers, freestanding targets, and the ML compiler that would grow from the GPU backend.
