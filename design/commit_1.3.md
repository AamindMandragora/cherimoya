# Commit 1.3: Pratt Parser and Expression Tree

## The Feature

- **Expressions become trees.** The token stream from commit 1.1 is parsed into a tree that makes precedence and associativity explicit: `a + b * c` is `a + (b * c)`, `a - b - c` is `(a - b) - c`, and `a ** b ** c` is `a ** (b ** c)`.
- **Prefix operators.** `-`, `!`, and `~` sit at level 3, and `old` parses at that level too. Since `**` is level 2, `-a ** b` is `-(a ** b)`, and `a ** -b` still works.
- **Postfix forms.** Calls `f(x)`, indexing `xs[i]`, and members `p.name` bind tightest, at level 1, so `f(g(x))` nests as written, `len(xs) - 1` subtracts from the result, `-f(x)` negates the call, and `slot(xs, i)[]`, `get(l, i).name`, and `f(a)(b)` chain onto results. One `[` covers indexing `xs[i]`, slicing `xs[a, ..., <b]`, dereferencing `p[]`, and instantiating `Map[K, V]` or `alloc[T](n)`.
- **Construction.** `T <- [pack]` builds an aggregate at level 1, so `Vec2 <- [1.0, 2.0] <+> Vec2 <- [3.0, 4.0]` needs no parentheses. An infix `<-` in an expression must be followed by `[`: otherwise `cherimoya requires construction to build from a pack, please write 'T <- [...]'`.
- **Chained comparisons.** `a < b <= c` is one node holding three operands and two operators, so `b` is evaluated once.
- **The mixing rule.** Two operators on one level next to each other need parentheses when their associativities differ or either is non-associative. `a = b = c` is an error since equality is non-associative, `a <=> b <= 0` since `<=>` sits with the comparisons but never chains, and `a >>= b =<< c` since one is left- and the other right-associative. `a = b != c` gets `cherimoya requires parentheses to mix '=' and '!=', please write '(x = y) != z' or 'x = (y != z)'`.
- **Pipes.** `x |> f` is left-associative at level 16 and `f <| x` right-associative at level 17. Mixing them without parentheses, even across levels as in `f <| a + x |> g`, gets `cherimoya requires parentheses to mix '|>' and '<|', please write '(x |> f) <| y' or 'x |> (f <| y)'`. Parentheses, brackets, and arguments start fresh, so `f <| (x |> g)` is fine.
- **Extra operators** parse exactly like built-ins.
- **Packs, ranges, and comprehensions.** `[a, b]`, `[x <- 1.0]` with named elements, `[0, ..., <n]` and `[n, n - 1, ..., >0]` with `...` and exclusivity markers, and `[x * x : x <- xs && x % 2 = 0]` with membership and filter clauses.
- **Folds.** `(reducer, seed)[sequence]`, where the reducer may be a bare operator: `(max, MIN_INT)[scores]`, `(+, 0)[[1, 2, 3]]`, `(*, 1)[1, ..., n]`.
- **Diagnostics for habits from other languages.** `->` in a pack gets `cherimoya uses a pack of pairs for a map instead of '->' as follows: [[key, value], [key, value]]`. `<-` at the start of an element gets `cherimoya uses '< -' for an exclusivity marker on a negative bound instead of '<-' as follows: [5, ..., < -1]`. `if (` gets `cherimoya uses 'match' for conditional flow instead of 'if' as follows: match { (condition) -> { ... } _ -> { ... } }`.

## Why

The lexer's stream is flat: `a + b * c` is five tokens in a row, and nothing in the sequence says whether `+` or `*` binds first. The parser imposes that structure, so every later pass walks a tree.

- **Pratt parsing over recursive descent.** Recursive descent needs one function per level, 17 almost identical functions that bury precedence in call structure. A Pratt parser is one recursive function given a binding power, "only parse things that bind at least this tightly", with precedence as a data lookup.
- **NUD and LED.** A null denotation handles a token at the start of an expression: literals, names, prefix operators, `(` and `[`. A left denotation handles a token after a complete subexpression: binary operators, calls, indexing, members, construction. The parser reads a NUD for `left`, then loops over LEDs that bind tightly enough, wrapping `left` each time.
- **Associativity is one integer.** The LED loop stops when the next operator's binding power is at most the current threshold. A left-associative operator recurses with its own power, so the next operator on its level stops and becomes the parent. A right-associative one recurses with one less, so the next one on its level is taken as a child.
- **No table of its own.** The lexer's operator table from commit 1.1 already has every level and associativity, and each operator token points at its entry, so moving an operator is an edit to its entry, not to the parser.
- **One node for indexing and instantiation.** Telling `xs[i]` from `Map[K, V]` needs to know what the name is, so the parser builds one node holding whatever was in the brackets and leaves it to type checking.
- **Extra operators get their own node.** They mean nothing until a program defines them, and a separate node saves type checking from asking every binary node whether its operator is built in.
- **Decide brackets after reading them.** A `(` could start a grouping, a fold, or a closure, and a `[` a pack, range, or comprehension, and neither is known until the end. Looking at the whole bracket first, or parsing its elements first and classifying them after, avoids backtracking.
- **Exclusivity markers are comparisons on the bound.** `>0` means "stay above 0", not a fixed start or end symbol, so the same marker works counting up or down. The parser keeps the marker as written, and whether it agrees with the stride's direction is type checking's job, since that needs the endpoint type.
- **`<-` means membership inside a clause.** `x <- [1, ..., 10]` after a comprehension's colon has exactly the shape of a construction, so a flag makes `<-` end the expression there instead.
- **Children as `unique_ptr`.** Holding derived nodes by value in a base-class field would slice them, losing everything the derived class adds. `std::unique_ptr<Expr>` keeps the whole node and gives the parent sole ownership, which is the tree's ownership model. The base has a virtual destructor, or deleting a derived node through it would skip its destructor and leak its subtrees.
- **Operators stored as tokens.** A node keeps the operator's `Token`, not a string, so later passes get its level, category, and overloadability from the table entry for free.
- **A tree that's always well-formed.** A mistake becomes an `ErrorExpr`, so later passes walk the tree without null checks and skip the error nodes, and one parse reports several independent errors.

## Implementation

### Expression Tree

`src/parser/ast.hpp` defines `ExprKind`, the `Expr` base with its `kind` and `span`, and the derived nodes:

- **Literals.** `IntLiteralExpr`, `FloatLiteralExpr`, `BoolLiteralExpr`, `StringLiteralExpr`, `CharLiteralExpr`, holding the raw text or value.
- **Names.** `IdentifierExpr`, and `WildcardExpr` for a lone `_`.
- **Operators.** `UnaryExpr`, prefix only since no operator is postfix, `BinaryExpr`, `ExtraBinaryExpr`, `ChainExpr` with its operands and operators, `PipeExpr` with its direction token and both sides in source order, `OpRefExpr` for a bare operator as a fold's reducer, and `OldExpr`.
- **Postfix.** `CallExpr`, `IndexExpr` with its object and argument list, `MemberExpr`, and `ConstructExpr` with its target and pack.
- **Packs.** `PackExpr` of `PackElem`s with optional names, `RangeExpr` of `RangeElem`s with optional markers, `ComprExpr` with a head and `ComprClause`s, where a clause with no pattern is a filter, and `FoldExpr` with its reducer, seed, and sequence.
- **`ErrorExpr`**, the sentinel for recovery.

### Parser

`src/parser/parser.hpp` is the `Parser`, which reads a token vector with a cursor and reports to the `DiagnosticEngine`.

- **The Pratt loop.** `parseExpr(rbp, parent)` reads a NUD, then loops while `leftBindingPower` of the next token beats `rbp`. `bindingPower` turns a level into `(18 - level) * 10`, so level 1 is 170 and level 17 is 10. `leftBindingPower` places `(`, `[`, and `.` at level 1 by hand, since they aren't operators, and construction's `<-` at level 1 unless `arrowStops` is set. Every other operator reads its level from its entry, and flow tokens other than `<-` and every compound have none, so they end the loop.
- **Mixing.** `checkMixing` compares the operator with the previous one at the same depth. `parent` is the operator whose right side is being parsed, so the loop starts its previous operator from there and sees across a right-associative recursion. Pipes are checked separately against the last pipe at this depth, seeded the same way. Only pipes recurse low enough to contain other pipes, so that's enough to catch `f <| a + x |> g`.
- **Chains.** A `Chain` operator collects every built-in comparison at its depth into one `ChainExpr`.
- **The prefix parenthesis.** `parseParen` finds the matching `)` with `findClose` and decides from what's inside and after. Followed by `->` is a closure, an error until closures can be written. A top-level comma, found by `hasTopLevelComma`, followed by `[` is a fold, handed to `parseFold`, and otherwise gets `cherimoya uses packs for tuples instead of parentheses as follows: [a, b]`. `()` gets `cherimoya uses '[]' for an empty pack instead of '()' as follows: let empty <- [];`. A single operator like `(+)` is reserved for operators as values. Anything else is a grouping whose span takes in its parentheses, so `(x)[0]` indexes the grouped value.
- **Folds.** `parseFold` reads the reducer, a bare operator becoming an `OpRefExpr`, then the seed and the bracketed sequence, and unwraps a one-element pack, so `(+, 0)[[1, 2, 3]]` folds over the inner pack.
- **Packs.** `parsePack` reads `RawElem`s with `parseElem`, each a value, a named value, `...`, or a value with a `>` or `<` marker, and decides afterward. Any `...` makes a `RangeExpr`, which must have exactly one `...`, at most two values before it, the second setting the stride, and one after, with markers only on its endpoints. A `:` after the first element makes a `ComprExpr`. Anything else is a `PackExpr`. Inside a pack, `name <- value` is always a named element, even when the value is a pack, so `[x <- [1, 2]]` is a field `x` holding a pack, and type checking reinterprets it when `name` is a type.
- **Clauses.** `parseClauses` reads each clause with `withArrowStops` set, so a top-level `<-` ends it and becomes `pattern <- source`. The pattern is parsed as an expression, which covers `x`, `[x, y]`, and `Some[d]`. `parseNested` clears the flag inside every bracket, so `f(Point <- [0, 0])` in a filter still constructs. Clauses are parsed just above `&&`'s level and joined by `&&`, and a `||` between them gets `cherimoya requires a comprehension's clauses to be joined with '&&', please wrap the '||' in parentheses, as in [x : x <- xs && (a || b)]`, since by precedence it would swallow the clauses to its left.
- **Recovery.** `expect` consumes the token it wants, or reports it in the expected form, naming the token and where it belongs: `cherimoya expected ')' after the arguments in 'f(a, b)', got ';' instead`. A closing bracket, `;`, or the end of the file is where the code around a mistake picks back up, so `isBoundary` keeps `expect` and the NUD switch from consuming one, and `f(1, )` is one error instead of three. An `Error` token from the lexer becomes an `ErrorExpr` with no second diagnostic. `advance` never moves past `Eof`, so an unclosed bracket can't walk off the token vector, and a pack or fold that goes wrong jumps to the close bracket it found, so one bad element doesn't cascade through the rest of the line.

### Build

`CMakeLists.txt` adds `src/parser` to the include path and `tests/unit/parser/test_parser.cpp` to `cheri_tests`.

## Tests

`tests/unit/parser/test_parser.cpp` has 26 Catch2 test cases, using `dump` to render a tree as an S-expression: `a + b * c` is `(+ a (* b c))`, `-a ** b` is `(pre:- (** a b))`, `f(x)[0]` is `(index (call f x) 0)`, `a < b <= c` is `(chain a < b <= c)`, and `(+, 0)[x * x : x <- xs && x % 2 = 0]` is `(fold (op +) 0 (compr (* x x) (in x xs) (= (% x 2) 0)))`. They cover literals, binding orders, associativity, prefix operators and `old`, calls, indexing, members, construction, chains and the mixing rule, extra operators, pipes, packs, ranges, comprehensions, folds, malformed parentheses, golden dumps of calls and pipes, extra operators, and pairs and packs, the diagnostics for mixed pipes, non-associative extra operators, `->` in a pack, `if (`, and an unspaced negative range end, error recovery, and spans. 2453 assertions across lexer and parser, 0 failures.

## Current Limitations

- **Expressions only.** There are no statements, declarations, `let`, `match`, or closures, and the tests feed expression strings, so there's no way to parse a whole program until statements and declarations exist.
- **No type grammar.** Closures, whose parameters are typed, annotations like `(p :: Ord)`, and types in value position such as `i32(x)`, `arr[mut T](n)`, and `size[i32]()` wait for the type grammar.
- **Clause patterns are expressions.** That covers every pattern a comprehension needs, until patterns get their own hierarchy.
