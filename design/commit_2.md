# Commit 2: Pratt Parser & Expression AST

## The Problem

The lexer produces a flat stream of tokens. `a + b * c` comes out as five tokens in order: `Identifier Plus Identifier Star Identifier`. But that sequence is ambiguous without precedence rules. Does `+` bind first, giving `(a + b) * c`? Or does `*` bind first, giving `a + (b * c)`? The token stream doesn't say. The parser's job is to impose structure, turning a flat sequence into a tree that makes precedence and associativity explicit.

## Why Pratt Parsing

The standard approach for parsing expressions in a hand-written compiler is recursive descent: one function per precedence level, each calling the next tighter level for its operands. For 17 precedence levels, that's 17 mutually recursive functions that are almost identical, differing only in which operator tokens they match. It works, but it's tedious, error-prone to extend, and buries the precedence relationships in call structure rather than making them explicit.

Pratt parsing (also called precedence climbing) replaces all of those functions with a single recursive function parameterized by a *binding power*, a number that says "only parse things that bind at least this tightly." Each operator token carries a binding power derived from the precedence table, and the parser uses it to decide whether to keep consuming tokens or return what it has. The result is a single function that handles all 17 levels, with the precedence table expressed as a data lookup rather than code structure.

The key insight is the split between *NUD* (null denotation) and *LED* (left denotation). A NUD handler fires when a token appears at the start of an expression or after an operator: literals, identifiers, prefix operators, opening parentheses and brackets. A LED handler fires when a token appears after a complete sub-expression: binary operators, call parentheses, indexing brackets, member dots, construction arrows. The parser alternates between the two: parse a NUD to get an initial `left`, then loop through LEDs that bind tightly enough, wrapping `left` each time.

Associativity falls out of one detail. The LED loop breaks when the next operator's binding power is `<=` the current threshold. A left-associative operator recurses with its own binding power *p*, so another operator at the same level hits `<=` and breaks out, becoming the parent. A right-associative operator recurses with *p - 1*, so the same-level operator on the right passes the check and gets consumed as a child. `a - b - c` becomes `(- (- a b) c)`. `a ** b ** c` becomes `(** a (** b c))`. One integer controls the difference.

## The Binding Power Table

The parser keeps no binding power table of its own. The lexer's operator table already records each operator's level (1 tightest through 17 loosest) and associativity, and every operator token carries a pointer to its entry. The parser turns a level into a binding power with `(18 - level) * 10`, so level 1 gets 170 and level 17 gets 10. The spacing leaves room between levels, though nothing needs it: moving an operator is an edit to its table entry, not a renumbering.

The only tokens the parser places by hand are the postfix ones that aren't operators: `(` for calls, `[` for indexing, and `.` for members, all at level 1. Construction's `<-` is in the table at level 1 too. Flow tokens other than `<-` and every compound have no level, so they always stop the loop. Assignment and yields are statements, and those arrive in commit 3.

Prefix operators read their prefix level from the same table. `-`, `!`, and `~` sit at level 3, and `old` is parsed at that level too. Since `**` is level 2, `-a ** b` is `-(a ** b)`, and `a ** -b` still works because the right side starts with a NUD.

## Calls Are Postfix

A call is just `(` in LED position, at level 1. It binds tighter than every prefix and infix operator, so `f(g(x))` nests as written, `len(xs) - 1` subtracts from the result, and `-f(x)` negates the call. Postfix forms chain onto results with no special rule: `slot(xs, i)[]`, `get(l, i).name`, `f(a)(b)`.

Indexing is the same `[` LED for four jobs. `xs[i]` indexes, `xs[a, ..., <b]` slices, `p[]` dereferences, and `Map[K, V]` or `alloc[T](n)` instantiates. The parser can't tell an index from an instantiation without knowing what the name is, so it builds one `IndexExpr` holding whatever was in the brackets, and sema decides. An empty argument list is the dereference.

## Construction

`T <- [pack]` builds an aggregate, and it binds at level 1 like the other postfix forms. That's what lets `Vec2 <- [1.0, 2.0] <+> Vec2 <- [3.0, 4.0]` work without parentheses. In an expression, an infix `<-` is only ever construction, so it must be followed by `[`, and anything else is an error. Whether the left side is really a type or a variant is sema's call.

## Comparisons, Mixing, and Pipes

Built-in comparisons chain. `a < b <= c` is a single `ChainExpr` holding three operands and two operators, so `b` is evaluated once. The chain LED keeps consuming comparisons at its level until it sees something else.

The mixing rule is one check, driven by the table: two operators on the same level next to each other need parentheses when their associativities differ or either one is non-associative. `a = b = c` is an error because equality is non-associative. `a <=> b <= 0` is an error because `<=>` is non-associative and sits with the comparisons, so it never joins a chain. `a >>= b =<< c` is an error because one is left-associative and the other is right-associative. To see the operator on the other side of a right-associative recursion, `parseExpr` takes the operator whose right side it's parsing as its second argument, and the LED loop starts its "previous operator" from there.

Pipes need a second check, since `|>` and `<|` sit on different levels. `x |> f` is a left-associative level 16, `f <| x` is a right-associative level 17, and mixing them without parentheses is an error. The loop tracks the last pipe it applied at the current depth, seeded from the parent the same way. Only pipes can recurse low enough to contain other pipes, so that's enough to catch `f <| a + x |> g`. Parentheses, brackets, and arguments start fresh, so `f <| (x |> g)` is fine.

Extra operators build an `ExtraBinOpExpr` instead of a `BinaryExpr`. They parse exactly like built-ins, but they mean nothing until a user defines them, and keeping them apart saves sema from asking every binary node whether its operator is built in.

## The Prefix Parenthesis

A `(` in NUD position could start a grouping, a fold, or a closure, and the parser can't know which until it sees the end. So it scans ahead to the matching `)` and looks at what's inside and what follows:

- followed by `->`: a closure. `->` has no other meaning in an expression, so this is unambiguous. Closures need parameter types and scope bodies, so they arrive with commit 3, and for now this is an error.
- a top-level comma, followed by `[`: a fold.
- a top-level comma otherwise: an error, since tuples are packs.
- nothing inside: an error, since `()` only comes before `->`, and an empty pack is `[]`.
- a single operator, like `(+)`: an error, since `(op)` is reserved for operators as values.
- anything else: a grouping. `(x)[0]` then indexes the grouped value.

A fold is `(reducer, seed)[sequence]`. The reducer may be a bare operator, which becomes an `OpRefExpr`. The brackets are parsed like any pack, and a one-element pack is unwrapped, so `(max, MIN_INT)[scores]` folds over `scores`, `(+, 0)[[1, 2, 3]]` folds over the inner pack, and `(*, 1)[1, ..., n]` folds over the range.

## Packs, Ranges, and Comprehensions

A `[` in NUD position parses a list of elements first and decides what it was afterward. An element is a value, a named value (`x <- 1.0`), `...`, or a value with an exclusivity marker, `>` or `<`.

- If any element is `...`, it's a `RangeExpr`. A range has exactly one `...`, at most two values before it (the second sets the stride) and one after, and only its endpoints can have exclusivity markers. An endpoint's exclusivity marker is a comparison on the bound, not a fixed start or end symbol: `>0` means "stay above 0", so `[0, ..., <n]` counts up to just below `n`, and `[n, n - 1, ..., >0]` counts down to just above `0`. The parser keeps the marker as written. Whether it agrees with the stride's direction, and the stride's magnitude, are sema's job, since they need the endpoint type.
- If a `:` follows the first element, it's a `ComprExpr`.
- Otherwise it's a `PackExpr`.

Inside a pack, `name <- value` is always a named element, even when the value is a pack. `[x <- [1, 2]]` is a field `x` holding a pack, not a construction of `x`. When `name` turns out to be a type, sema reinterprets it.

After the colon of a comprehension, `<-` changes meaning. `x <- [1, ..., 10]` has exactly the same shape as a construction, but here it's a membership clause. So while the parser reads the top of a clause, a flag makes `<-` stop the LED loop instead of constructing, and the clause parser picks it up as `pattern <- source`. The pattern is parsed as an expression for now, which covers `x`, `[x, y]`, and `Some[d]`. Nested brackets and parentheses clear the flag, so `f(Point <- [0, 0])` inside a filter clause still constructs. Clauses are parsed just above `&&`'s level and joined by `&&`. A `||` between them needs parentheses, since by precedence it would swallow the membership clauses on its left.

Three habits from other languages get a diagnostic in the `cherimoya uses <x> for <y> instead of <z>` form, with an example of the right way:

- `->` after a pack element: map literals are packs of pairs, `[key, value]`.
- `<-` at the start of an element: `[0, ..., <-1]` lexed as a flow token, and an exclusivity marker on a negative end is written `< -1`.
- `if (`: there is no `if`. `if` is an identifier, so it only gets the diagnostic when followed by `(`, and conditionals use `match`.

## The AST

Every expression becomes a node in a tree. The base class `Expr` carries an `ExprKind` discriminator and a `Span` (start and end byte offsets into the source). The kind lets later passes switch without downcasting. The span lets every pass from type checking through codegen report errors that point at the right piece of source.

Derived node types:

- literals: `IntLiteralExpr`, `FloatLiteralExpr`, `BoolLiteralExpr`, `StringLiteralExpr`, `CharLiteralExpr`, each holding the raw literal text or value
- names: `IdentifierExpr`, and `WildcardExpr` for a lone `_`
- operators: `UnaryExpr` (prefix only, since no operator is postfix), `BinaryExpr`, `ExtraBinOpExpr`, `ChainExpr`, `PipeExpr` (direction token plus both sides in source order), `OpRefExpr`, `OldExpr`
- postfix: `CallExpr`, `IndexExpr` (object plus a list of arguments), `MemberExpr`, `ConstructExpr` (target plus pack)
- packs: `PackExpr` (elements with optional names), `RangeExpr` (raw elements with optional markers), `ComprExpr` (head plus clauses, where a clause without a pattern is a filter clause), `FoldExpr` (reducer, seed, sequence)
- `ErrorExpr`, a sentinel for recovery

Children are held as `std::unique_ptr<Expr>`. This is the only correct option in C++. Storing derived types by value in the base class would slice them: a `BinaryExpr` whose `lhs` field is declared as `Expr` loses everything `CallExpr` adds when a `CallExpr` is assigned to it. The derived portion is simply truncated. `unique_ptr` preserves the full derived object on the heap and gives the parent exclusive ownership, which is exactly the tree's ownership model: each node owns its children.

The base class has a virtual destructor. Without it, `unique_ptr<Expr>` deleting a derived object is undefined behavior: the derived destructor never runs, and nodes holding their own `unique_ptr` members leak their subtrees.

Operators are stored as `Token`, not as strings. The lexer already classified every operator and attached its table entry, so later passes get the level, category, and overloadability for free.

## Error Recovery

When the parser hits a token it doesn't expect in the NUD position (say, a bare `+` or a stray `}`), it emits a diagnostic and returns an `ErrorExpr`. The tree stays well-formed: every node is a valid `Expr`, so later passes can walk the tree without null checks. They skip `ErrorExpr` nodes and keep going, which means a single parse can report multiple independent errors instead of dying on the first one.

The `expect` helper advances and checks the token kind. Every call says which token it wants and where, so on a mismatch the diagnostic reads like `cherimoya expected ')' after the arguments in 'f(a, b)', got ';' instead`. It still returns the token, so the parser can attempt to continue. Rule violations, like mixing `=` and `!=`, use the `cherimoya requires <rule>, please <fix>` form and name both operators. `advance` never moves past `Eof`, so running off the end of an unclosed bracket can't walk out of the token vector. When a pack or fold goes wrong, the parser jumps forward to the matching close bracket it found while scanning, so one bad element doesn't cascade into errors for the rest of the line.

## What Was Built

`src/parser/ast.hpp` defines the `ExprKind` enum, the `Expr` base class, and all derived expression nodes. `src/parser/parser.hpp` implements the `Parser` class: the Pratt loop reading levels from the operator table, the NUD and LED switches, the mixing and pipe checks, the prefix `(` scan, folds, and packs with their ranges and comprehensions. `tests/unit/parser/test_parser.cpp` has 26 Catch2 test cases.

The tests use an S-expression dump function (`dump`) that renders the tree as a string: `a + b * c` becomes `(+ a (* b c))`, `f(x)[0]` becomes `(index (call f x) 0)`, `a < b <= c` becomes `(chain a < b <= c)`, and `(+, 0)[x * x : x <- xs]` becomes `(fold (op +) 0 (compr (* x x) (in x xs)))`. Test cases cover literals, binding orders, associativity, prefix operators and `old`, postfix calls, indexing, member access, construction, chains and the mixing rule, extra operators, pipes, packs, ranges, comprehensions, folds, malformed parentheses, golden dumps for `pass/calls_and_pipes`, `pass/user_operators`, and `pass/pairs_and_packs`, the diagnostics for `fail/pipe_mix`, `fail/user_op_nonassoc`, `fail/dict_arrow`, `fail/if_removed`, and `fail/spacing_negative_end`, error recovery, and span correctness. 2443 assertions across lexer and parser, 0 failures.

## Current Limitations

The parser handles expressions only. There are no statements, no declarations, no `let`, no `match`, and no closures. The test harness feeds raw expression strings, so there's no way to parse a full program yet. That's commit 3.

Anything that needs the type grammar waits for commit 3 too: closures (their parameters are typed), annotation like `(p :: Ord)`, and types in value position, such as `i32(x)`, `arr mut T(n)`, and `size[i32]()`.

Membership clause patterns are parsed as expressions. That covers every pattern a comprehension needs today, but patterns get their own hierarchy in commit 3.

## What's Next

Commit 3 adds the rest of the grammar: statements, bindings, yields, `match`, `repeat`, `chunk`, blocks, patterns, the type grammar, closures, every `type` definition, functions, modules, and attributes. After that, the full AST exists and commit 4 can start binding names and checking types.
