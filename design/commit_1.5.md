# Commit 1.5: Declarations and the Round-Trip Printer

## The Feature

- **Whole files.** A program is a list of statements, so functions, types, bindings like `let LIMIT <- 4096;`, and obligations like `wit {Vec2 :: Ord};` can sit at the top of a file, and functions can be defined inside bodies.
- **Functions.** `let [generics] (inputs) -> name -> (outputs) wit {obligations} { body }`. Generics are `[name :: bound & bound <- fallback]`, where the fallback is a type (`<- Global`) or a value known while compiling. Inputs and outputs take refinements with their types, outputs are always parenthesized, and `()` is void. The name is an identifier or any overloadable operator: the built-in `+ - * / % & | ^ << >> ~ ! ==` and every extra operator. An extern ends with `;` instead of a body.
- **Type definitions.** `type Name[generics] <- right side;`, in six forms: a protocol `type Ord[T] <- [let (a :: peek T, b :: peek T) -> cmp -> (r :: int)];`, a sum `type Shape <- Circle[f64] | Rect[f64, f64];`, an enum `type Dir <- N | E | S | W;`, a product `type Point <- [x :: f64, y :: f64];` with an optional invariant, a refinement `type Percent <- int {p : 0 <= p <= 100};`, and a newtype `type NodeId <- i64u;`. A variant that isn't a plain name with types in its brackets gets `cherimoya requires each variant of a sum to be a name with an optional payload, please write 'Name' or 'Name[T, U]'`.
- **Modules.** `export [List, push, Result[dist, prev]];` and `import [Allocator] <- std.mem;`. An item can be an operator, so `export [Vec2, <+>]` exports the overload.
- **Attributes.** `@name(args)` followed by what it applies to: `@tier(strict);` applies to the whole file, `@extern("c") let ...;` to the function after it, and `@tier(strict) { ... }` to the block after it. Attributes stack.
- **A round-trip printer.** The tests print a tree back as source, parse that, and print it again, checking the parser against itself.

## Why

After commit 1.4 the parser reads a function body but not the function around it, and it can't read a file: there's no way to define a function, a type, or a module's exports and imports, and no attributes. This commit finishes the grammar.

- **A program is statements.** A file isn't just declarations: functions nest inside bodies, `@tier(strict) { ... }` wraps statements, and bindings and `wit` obligations are as valid at the top of a file as inside a function. So the grammar is the same at every level, and rejecting what doesn't belong at the top, like an assignment, is type checking's job.
- **Declarations are their own hierarchy.** Later passes treat them differently: name resolution makes a first pass over them to build symbol tables before any body is checked. A wrapper statement lets one sit wherever a statement can.
- **Telling a function from a binding.** `let (` is always a function, and `let [` is one when the matching `]` is followed by `(`, since `let [q, r] <- ...` is destructuring.
- **Operator names from the table.** The lexer's table from commit 1.1 already records which operators are overloadable, so whether a token can name a function is one check. Unary and binary `-` share a name and are told apart by arity, which is type checking's job.
- **One type-definition node.** Every form is `type Name <- right side;`, so one node records which form the right side took instead of six nodes with the same shape.
- **A single alternative is never a sum.** `type Wrapper <- Foo[int];` is a newtype of `Foo[int]`, not a one-variant sum, which keeps a one-alternative right side unambiguous without asking what `Foo` is.
- **One attribute node.** The target is just the next statement, so whole-file, function, and block attributes are one node, and stacking falls out.
- **Printing the same text twice isn't proof.** If the printer dropped needed parentheses, `a - (b - c)` would print as `a - b - c`, which parses to a different tree that prints the same way. A full mode parenthesizes every operand of every operator, so two trees print the same in full mode only if they have the same shape. The round trip compares the normal printing for stability and the full printing for shape.
- **Minimal parentheses are the parser's rules in reverse.** Putting back only what the grammar needs means encoding precedence, associativity, the mixing rule, pipes, the spacing rule, and every place `<-` changes meaning, so the printer checks all of them.

## Implementation

### Declarations

`src/parser/decl.hpp` defines `DeclKind`, the `Decl` base, `GenericParam` (name, bounds, optional fallback `GenericArg`), `FnDecl` (generics, inputs, name token, outputs, obligations, and a body that's null for externs and protocol members), `TypeForm`, `Variant` (a name and payload types), `TypeDecl` (name, generics, form, and the right side as a type, variants, or members), `ModuleItem` (a name and its fields), `ExportDecl`, `ImportDecl` with its module path, and `ErrorDecl`. `src/parser/ast.hpp` includes it. `src/parser/stmt.hpp` adds `DeclStmt`, which wraps a declaration, and `AttrStmt`, with the attribute's name, arguments, and target statement, null when `;` follows.

### Parser

In `src/parser/parser.hpp`:

- **Programs.** `parseProgram` calls `parseStmts` until the end of the file. `parseStmts` is shared with `parseBlock`, which reads up to `}`, and skips a stray closing bracket once it's been reported. `parseStmt` dispatches `type`, `export`, `import`, and `@` to their parsers, and `declStmt` wraps each declaration.
- **Functions.** `parseLet` hands a function to `parseFnDecl`, which reads optional generics with `parseGenericParams`, the inputs with `parseParamList`, `->`, the name, `->`, the outputs, an optional `wit {obligations}`, and then a block, or `;` for an extern, or nothing in member mode. `isFnName` accepts an identifier or a token whose entry `isOverloadable`. A missing name is reported, and the `->` after it is left in place, so the rest still parses.
- **Generics.** `parseGenericParams` reads each name, bounds after `::` joined by `&`, and a fallback after `<-` through `parseGenericArg`.
- **Type definitions.** `parseTypeDecl` reads the name, optional generics, and `<-`. A right side opening with `[ let` is a protocol, whose members are `parseFnDecl` in member mode separated by commas. Otherwise it parses a type: more than one alternative separated by `|` is a sum, or an enum when no variant has a payload, with each alternative turned into a `Variant` by `toVariant`, which rejects a dotted name or a value argument. A record, refined or not, is a product, any other refinement is a refinement, and anything else is a newtype.
- **Modules.** `parseModuleItems` reads the shared item list, where a name can be an identifier, a built-in type name, or an overloadable operator, with optional fields in brackets. `parseExport` and `parseImport` read their declarations, the latter ending with a dotted module path.
- **Attributes.** `parseAttr` reads the name, an optional argument list through `parseArgList`, and then `;` or the target statement.

### Round-Trip Printer

The printer lives with its tests in `tests/unit/parser/test_printer.cpp`. It isn't a formatter: it keeps no comments and aligns nothing. `Printer` has a `full` flag and prints expressions, types, patterns, statements, declarations, and whole programs.

- **Operands.** `needsParens` parenthesizes an operand that binds looser than its operator, or ties on level without the same associativity on its side. Built-in comparisons inside a chain and anything non-associative always tie badly. That one rule covers precedence, associativity, and the mixing rule. An operand that's the other pipe always gets parentheses, even though the pipes sit on different levels. `levelOf` gives each node its level, 10 for chains, 3 for prefix forms, and 1 for anything postfix or atomic, and in full mode every operator node is parenthesized.
- **Spacing.** `glue` puts a space between a prefix and an operand starting with an operator symbol, so `- -a` doesn't print as the extra operator `--`, and `< -1` doesn't print as `<-1`.
- **Contexts.** At the top of a statement or a comprehension clause, a construction would read as an assignment or a membership clause, so it gets parentheses, and so does an unnamed construction from a name inside a pack, which would read back as a named element. `stmtExpr` parenthesizes an expression statement that starts with a scope but isn't one, found by `startsWithScope`, since a scope at the start of a statement ends it. `clause` parenthesizes a filter clause or a membership clause's source from `&&`'s level down, since clauses are parsed just above it.
- **Layout.** 4-space indentation, one statement per line, and `braces` keeps a brace list on one line when it holds exactly one item that fits on one line. That keeps `{ x + 1 -| }` and `match { (n > 100) -> { 100 -| early } }` compact, while a match with several arms gets a line each.
- **The check.** `roundTrip` parses source, prints it, parses the printing, and checks it has no errors, prints the same again, and matches the original in full mode. `canonical` checks that canonical source prints back exactly as written.

### Build

`CMakeLists.txt` adds `tests/unit/parser/test_printer.cpp` to `cheri_tests`.

## Tests

`tests/unit/parser/test_parser.cpp` gains `dumpDecl` and `programDump`, with test cases for functions, type definitions, modules, attributes, and two whole files, a reference program and a module header. `tests/unit/parser/test_printer.cpp` has test cases for parentheses following precedence, associativity, and mixing, normalizing non-canonical input, statement and clause contexts, statements and scopes, closures, folds, and comprehensions, declarations, and a round trip of the reference program with its comments and alignment normalized away. 3125 assertions across lexer, parser, and printer, 0 failures.

## Current Limitations

- **Comments and layout.** Comments and blank lines don't survive printing, and nothing is aligned, until a formatter exists.
- **Only a tree.** The driver still only dumps tokens, and nothing checks the tree until name resolution and type checking exist.
