# Commit 3.2: Declarations & the Round-Trip Printer

## The Problem

After commit 3.1, the parser can read a function body but not the function around it, and it can't read a file. There's no way to define a function, a type, or a module's exports and imports, and no attributes. Commit 3.2 adds those, which finishes the grammar. It also adds a printer that turns the tree back into source, used by the tests to check the parser against itself.

## A Program Is a List of Statements

A file isn't just a list of declarations: functions can be defined inside bodies, `@tier(strict) { ... }` wraps statements, and `let LIMIT <- 4096;` and `wit {Vec2 :: Ord};` are just as valid at the top of a file as inside a function. So a program is a list of statements, and `parseProgram` calls `parseStmt` until the end of the file. Declarations are their own hierarchy in `decl.hpp`, and a `DeclStmt` wraps one so it can sit wherever a statement can. The grammar is the same at every level. Rejecting what doesn't belong at the top level, like an assignment, is sema's job.

The hierarchy stays separate because later passes treat declarations differently: name resolution makes a first pass over them to build symbol tables before any body is checked.

## Functions

A function is `let [generics] (inputs) -> name -> (outputs) wit {obligations} { body }`. `parseLet` already had to look past the `let` to tell a binding from a function: `let (` is always a function, and `let [` is one when the matching `]` is followed by `(`, since `let [q, r] <- ...` is destructuring. In commit 3.1 that check reported "not supported yet". Now it hands off to `parseFnDecl`.

- **Generics** are `[name :: bound & bound <- fallback]`. Bounds are types joined by `&`, and the fallback is a generic argument, so it can be a type (`<- Global`) or a comptime value.
- **Inputs and outputs** reuse the parameter list from closures, and refinements come along with the types. Outputs are always parenthesized, and `()` is void.
- **The name** is an identifier or any overloadable operator token: the built-in `+ - * / % & | ^ << >> ~ ! ==` and every extra operator. The lexer's table already records which operators are overloadable, so the check is one line. Unary and binary `-` share a name and are told apart by arity, which is sema's job.
- **`wit {obligations}`** is optional and holds an expression.
- **The body** is a block. An extern has `;` instead, and an interface member has neither.

## Type Definitions

Every type definition is `type Name[generics] <- right side;`. There is one `TypeDecl` node, and the parser records which form the right side took:

- **interface**: the right side opens with `[ let`. Members are signatures without bodies, separated by commas, parsed with the same `parseFnDecl` in member mode.
- **sum**: the right side has more than one alternative separated by `|`. Each alternative is parsed as a type, so `Circle[f64]` comes out as a named type with type arguments, and is then turned into a variant with a payload. A dotted name or a value argument isn't a variant and is an error.
- **enum**: a sum where no variant has a payload.
- **product**: a record, optionally refined with an invariant.
- **refinement**: any other refined type.
- **newtype**: anything else.

A single alternative is never a sum, so `type Wrapper <- Foo[int];` is a newtype of `Foo[int]`, not a one-variant sum. That keeps the reading of a one-alternative right side unambiguous without asking sema what `Foo` is.

## Modules and Attributes

`export [List, push, Result[dist, prev]];` and `import [Allocator] <- std.mem;` share an item list. Each item is a name with optional fields in brackets, and a name can be an operator, so `export [Vec2, <+>]` exports the overload. Imports end with a dotted module path.

An attribute is `@name(args)` followed by what it applies to. One `AttrStmt` covers all three uses: `@tier(strict);` applies to the whole file and has no target, `@extern("c") let ...;` targets the function after it, and `@tier(strict) { ... }` targets the block after it. Attributes stack, since the target is just the next statement.

## The Round-Trip Printer

The printer lives with its tests in `tests/unit/parser/test_printer.cpp`. It isn't `cheri fmt`. It keeps no comments and aligns nothing. It's here to check the parser: print a tree, parse the output, and print it again. If anything is lost or misread on the way, the two printings differ.

Printing the same text twice isn't proof that the parse was right, though. If the printer dropped needed parentheses, `a - (b - c)` would come out as `a - b - c`, which parses to a different tree that prints the same way. So the printer also has a full mode that parenthesizes every operand of every operator. Full mode is unambiguous, so two trees print the same in full mode only if they have the same shape. The round-trip check compares the normal printing for stability and the full printing for shape.

Getting the normal printing right means putting back only the parentheses the grammar needs, which turns out to be most of the parser's rules in reverse:

- An operand needs parentheses when it binds looser than its operator, or ties on level without the same associativity on its side. Built-in comparisons inside a chain, and anything non-associative, always tie badly. That one rule covers precedence, associativity, and the mixing rule.
- An operand that's the other pipe always needs parentheses, even though the pipes sit on different levels.
- A prefix operator gets a space before an operand that starts with an operator symbol, so `- -a` doesn't print as the extra operator `--`, and `< -1` doesn't print as `<-1`.
- At the top of a statement or comprehension clause, a construction would be read as an assignment or a membership clause, so it gets parentheses. So does an unnamed construction from a name inside a pack, which would read back as a named element.
- An expression statement that starts with a scope but isn't one gets parentheses, since a scope at the start of a statement ends it.
- A filter clause or a membership clause's source in a comprehension gets parentheses from `&&`'s level down, since clauses are parsed just above it.

The layout is fixed: 4-space indentation, one statement per line, and a brace list kept on one line when it holds exactly one item that fits on one line. That's what makes `{ x + 1 -| }` and `match { (n > 100) -> { 100 -| early } }` stay compact while a match with several arms gets one line each.

## What Was Built

`src/parser/decl.hpp` defines `FnDecl`, `TypeDecl`, `ExportDecl`, `ImportDecl`, and `ErrorDecl`, with `GenericParam`, `Variant`, and `ModuleItem`. `src/parser/stmt.hpp` gained `DeclStmt` and `AttrStmt`. The parser gained `parseProgram`, `parseFnDecl`, `parseGenericParams`, `parseTypeDecl`, `parseExport`, `parseImport`, and `parseAttr`, and `parseLet` now hands functions off instead of skipping them.

`tests/unit/parser/test_parser.cpp` gained `dumpDecl` and `programDump`, with test cases for functions, type definitions, modules, attributes, and two whole files: a reference program and a module header. `tests/unit/parser/test_printer.cpp` has the printer and test cases for parentheses, normalization, statement and clause contexts, statements and scopes, closures and folds, declarations, and a round trip of the reference program with its comments and alignment. 3125 assertions across lexer, parser, and printer, 0 failures.

## Current Limitations

Comments and blank lines don't survive printing, and nothing is aligned. That's the job of `cheri fmt`, which gets its own commit once the driver does more than dump tokens.

Macros (`macro`, `moya`, `$x`) aren't parsed. They come later, and the lexer still reserves their tokens.

## What's Next

Commit 4 adds name resolution and core type checking: monomorphic primitives, three passes over every statement list (signatures, then the other statements, then bodies), the yield-target stack with label resolution, error poisoning, place and mutability checks, and discard enforcement.
