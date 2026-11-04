# Cherimoya Language Specification

## 0. Identity and context

Cherimoya is a compiled systems language aimed at the gap between C++ and Rust, and general purpose enough to reach past it, into scripting and the web. It is the language where you have options. Its organizing principle is **progressive disclosure of safety**: plain code is as easy to write as C, and stronger guarantees (ownership tracking, refinement contracts, verified arithmetic, exhaustive matching) layer on top without changing the base language. The programmer chooses how much verification they want, per function, per module, per project. Ownership is the default way memory is managed; reference counting and garbage collection exist only as opt-in options.

The surface syntax is built on a **data-flow metaphor**, carried by the **flow tokens**:

- **`<-` (values flow backward)** into whatever receives them: bindings, assignment, named arguments and defaults, construction from a pack, membership clauses, generic defaults, and imports.
- **`->` (definitions flow forward):** inputs, then a body. It appears only in function definitions, closures, and match arms.
- **`-|`** yields: it leaves a scope, optionally with a value.

The pipes **`|>` and `<|`** feed a value into a function, forward or backward.

**Brackets have fixed roles:**

- `{ }` segments code into scopes: function bodies, blocks, match arm bodies and arm lists, loop bodies, chunks, and the predicates of refinements and `wit`.
- `[ ]` packs data together: arrays, tuples, records, sum payloads, ranges, comprehensions, indexing, slicing, dereference, generic arguments, capture lists, and export and import lists.
- `( )` is for execution and math: calls, input and output lists, conditions, assertions, grouping, annotations, fold reducers, and a match's operator slot.

No operator token contains a bracket. The language keeps keywords to a minimum.

| | |
|---|---|
| Toolchain binary | `cheri` |
| Source files | `.moya` (modules and programs), `.toya` (tests) |
| Comments | `#` to the end of the line |
| Entry point | `let () -> start -> (code :: int) { ... }` |
| Formatter | `cheri fmt`, canonical, with no configuration (gofmt-style) |
| Verification | Z3 through its C API |
| Standard library | stdcheri, with modules under `std` |

---

## 1. Lexical structure

**Source and tokens:**

- Source is UTF-8. A byte-order mark is an error.
- A non-ASCII character outside a literal is an error, and so is any character no token starts with, and reading carries on past each one.

**Comments** start with `#` and run to the end of the line. There are no block comments. Comments are kept for the formatter and for documentation (14.3).

**Every operator** has a spelling, a category (flow, arithmetic, bitwise, logical, comparison, equality, pipe, compound, extra), whether it can be overloaded, its binary level, its prefix level, its associativity (none, left, right, or chain), and, for a compound, its base operator.

**Maximal munch** applies everywhere, as a longest match over the operator table, trying every length from the longest spelling down: `<<<-` before `<<<` before `<<` before `<-` before `<`.

- `a<-b` always lexes as `a`, `<-`, `b`.
- Unary minus is not a separate token. `-5` is `-` and a literal, which reads as a negative literal where one fits.
- Anything the table doesn't know falls through to punctuation: `( ) [ ] { } , ; . ... : :: @`. There is no `..`: ranges are packs with `...` in them, so `..` is two dots, which is an error.

**Compounds** are generated, not listed by hand. Every overloadable binary operator that takes two `T`s and gives a `T` gets `X<-`: the arithmetic and bitwise built-ins, the named doubles and triples, and wrapped tokens whose inside is arithmetic or bitwise. The logical operators get `&&<-`, `||<-`, and `^^<-` too, since they give back their operands' type even though they can't be overloaded. Comparisons and equality give `bool`, so there is no `==<-` or `<=><-`, and the combinators don't fit the shape.

**Spacing rule.** When two operator tokens touch and either one is a flow token, a pipe, a compound, or an extra operator, that is a compile error, spanning both tokens: `a**-b` is rejected, `a ** -b` is fine. Letters, digits, brackets, commas, dots, and colons can't merge into an operator, so they never need a space: `x<-1` is an assignment, `(<+>, 0)[xs]` is a fold, and `[x<-1.0]` is a record. An exclusivity marker on a negative range end is written with a space, `< -1`, since `<-1` lexes as a flow token.

**Punctuation and operators:**

| Group | Tokens |
|---|---|
| Punctuation | `(` `)` `[` `]` `{` `}` `,` `;` `.` `...` `:` `::` `_` `@` |
| Flow | `<-` `->` `-\|` |
| Pipes | `\|>` `<\|` |
| Arithmetic | `+` `-` `*` `/` `%` |
| Bitwise | `&` `\|` `^` `~` `<<` `>>` |
| Logical | `&&` `\|\|` `^^` `!` |
| Comparison | `<` `<=` `>` `>=` |
| Equality | `=` `!=` (shallow), `==` `!==` (deep) |
| Extra, named | `**` `***` `//` `%%` `++` `+++` `--` `<<<` `>>>` `&&&` `^^^` `\|\|\|` `<=>` `>>=` `=<<` `$` `<$>` `<~>` `~>` `<~` |
| Extra, wrapped | every single-symbol binary operator inside `<>`, `<<`, `>>`, `\|\|`, `\|>`, `<\|`, `\|<`, or `>\|`, such as `<+>`, `\|*\|`, `<\|>`, `\|$\|` |
| Compound | generated, as above |

**Extra operators** are a fixed set with no built-in meaning, for programs to define (2.13). A wrapped token takes the level and associativity of the operator inside it, except that a wrapped comparison never chains. Generation skips any spelling that contains `<-`, `->`, or `-|`, and any spelling already named. No extra operator is longer than three symbols. A lexer test splits every extra operator into built-ins and checks whether the split could be a valid fragment (a binary operator followed only by prefix operators). Exactly two have one: `a--b` could mean `a - -b`, and `a<~b` could mean `a < ~b`. Maximal munch picks the extra operator, the other reading is a space away, and the test pins the list.

**Literals:**

- **Integers:** decimal, plus `0x`, `0o`, and `0b`, with `_` separators anywhere in a digit run (`1_000_000`). A lone `_` is its own token. A letter or digit the base doesn't have makes the rest of the word one error: `0o78` gets `cherimoya expected an octal digit (0 to 7), got '8' instead`, and `123abc` is one error.
- **Floats:** a fractional part in any base (`3.14`, `0x1.8`, `0b10.1`). The `.` is consumed only when a digit of the base follows it, so `1.foo` is a member access and `1...` is an integer before `...`. Leading-dot floats like `.5` are not supported.
- **Characters:** `'a'`, exactly one character (5.7).
- **Strings:** `"..."` (5.8).
- **Booleans:** `true` and `false`, keywords at the token level and literals in the AST.

Every number, character, and boolean literal is untyped and adapts to the type its context expects (5.5–5.9).

> Open: whether float literals get an exponent (`1e9`); none is defined.

**Quoted literals.** A string or character literal holds every character between its quotes. A character is written as itself, in UTF-8, or as an escape:

| Escape | Means |
|---|---|
| `\n` `\t` `\r` `\0` | newline, tab, carriage return, the zero character |
| `\\` `\'` `\"` | the character itself |
| `\xHH` | the code point HH, two hex digits |
| `\u{H...}` | the code point H..., 1 to 6 hex digits, any Unicode scalar value |

Anything else after a `\` is an error that lists the escapes. A `\u{...}` naming a surrogate or anything past 10FFFF is an error, and so are bytes that aren't UTF-8: a stray continuation byte, a cut-short sequence, an overlong encoding, and an encoded surrogate. An unclosed literal is reported at its opening quote.

**Keywords (the complete list):**

| Group | Keywords |
|---|---|
| Declarations | `let` `type` `export` `import` |
| Scopes | `match` `repeat` `chunk` |
| Modes, qualifiers, handles | `unscoped` `peek` `mut` `ptr` `arr` |
| Place intrinsics | `take` `swap` `replace` |
| Verification | `wit` `assert` `old` |
| Literals | `true` `false` |

There is no `if` and no `Self`. `if` lexes as an identifier, and `if (` gets a dedicated diagnostic pointing at `match`. `and`, `or`, and `not` are ordinary names. A keyword where a name is expected is named as a keyword: `cherimoya expected a function name (an identifier or an overloadable operator) in '...', got the keyword 'wit' instead`.

**Built-in type names** lex as `TypeName` through the keyword table:

| Family | Types |
|---|---|
| Signed trapping | `i8` `i16` `i32` `i64` `i128` |
| Unsigned trapping | `i8u` `i16u` `i32u` `i64u` `i128u` |
| Signed wrapping | `i8w` `i16w` `i32w` `i64w` `i128w` |
| Unsigned wrapping | `i8uw` `i16uw` `i32uw` `i64uw` `i128uw` |
| Floats, NaN-free | `f8` (= `f8e4m3`) `f8e5m2` `f16` `f16e8m7` `f32` `f64` `f128` |
| Floats, NaN-carrying | `f8e4m3n` `f8e5m2n` `f16n` `f16e8m7n` `f32n` `f64n` `f128n` |
| Aliases | `int` = `i64`, `float` = `f64` |
| Other | `bool`, `char` (ASCII), `rune` (Unicode scalar), `boolf` (fuzzy boolean) |

Misspellings such as `u64`, `i64wu`, or `f8n` lex as identifiers and are caught at name resolution.

---

## 2. Expressions

### 2.1 Precedence

Levels run from 1, the tightest, to 17:

| Level | Operators | Associativity |
|---|---|---|
| 1 | postfix: call `(args)`, `.member`, `[index / slice / generic args]`, `[]` deref, construction `T <- [pack]` | left |
| 2 | `**` `***` | right |
| 3 | prefix `-` `!` `~` `old` | prefix |
| 4 | `*` `/` `%` `//` `%%`, wrapped `*` `/` `%` (`<*>`, `\|*\|`, ...) | left |
| 5 | `+` `-` `++` `+++` `--`, wrapped `+` `-` | left |
| 6 | `<<` `>>` `<<<` `>>>` | left |
| 7 | `&`, wrapped `&` | left |
| 8 | `^`, wrapped `^` | left |
| 9 | `\|`, wrapped `\|` (`<\|>`, ...) | left |
| 10 | `<` `<=` `>` `>=` (chain); `<=>` and wrapped comparisons | chain; none |
| 11 | `=` `!=` `==` `!==`, wrapped `=` | none |
| 12 | `&&` `&&&` | left |
| 13 | `^^` `^^^` | left |
| 14 | `\|\|` `\|\|\|` | left |
| 15 | `>>=` `<$>` `<~>` `~>` `<~` (left); `=<<` `$` and wrapped `$` (right) | combinators |
| 16 | `\|>` | left |
| 17 | `<\|` | right |

- The Pratt parser keeps a single `parseExpr` with NUD/LED dispatch. A level becomes a binding power as `(18 - level) * 10`. Moving an extra operator is an edit to its table entry.
- `-` is level 5 as a binary operator and level 3 as a prefix, so `-a ** b` is `-(a ** b)`.
- Assignment, compound assignment, and yields are statements, so they have no level.

**Mixing rule.** Two operators on the same level next to each other need parentheses when their associativities differ or either one is non-associative: `a = b = c`, `a <=> b <= 0`, and `a >>= b =<< c` are errors. Inside the right side of a right-associative operator, each operator is also compared with that parent, so `a $ b + c >>= d` needs parentheses as well. The error offers both groupings as fixes: `(a $ b + c) >>= d` and `a $ (b + c >>= d)`.

**Chained comparisons.** `a < b <= c` means `a < b && b <= c`, with `b` evaluated once. Built-in comparisons chain only with each other.

### 2.2 Calls and methods

A call is `(` in LED position, at level 1, so it binds tighter than every prefix and infix operator: `f(g(x))` nests as written, `xs.len() - 1` subtracts from the result, `-f(x)` negates the call, and postfix forms chain onto results: `slot(xs, i)[]`, `get(l, i).name`, `f(a)(b)`.

**Named arguments.** An argument is named with `<-`: `clamp(x, hi <- 10)`. Unnamed arguments go to the inputs in order, then named ones go to the inputs they name, in any order. Every unnamed argument comes before every named one: `cherimoya requires unnamed arguments to come before named ones, please name this one too`. An input gets one argument, and a name that isn't an input lists the inputs there are. A name and `<-` in front of an argument always name it, so a construction as an argument goes in parentheses: `f((Point <- [0, 0]))`. Arguments run in the order they're written, not their inputs' order.

**Defaults** are covered with functions (6.1). A call that leaves out an input without a default says which: `cherimoya requires the call to 'f' to pass 'a', please add an argument for it`. A wrong count names the expected count, exactly or as a range: `cherimoya expected 2 arguments in the call to 'add', got 1 instead`.

**Methods** are what protocols give the types that implement them (5.3). A method is called on its object, `xs.len()`, piped into with its dot, `xs |> .len` or `.len <| xs`, or called like a function with its object first, `len(xs)`. The dot is for what a type owns (fields, methods) and for module paths, so a plain function is never called as `x.f()`. When a name could be either, the method wins: `len(xs)` calls the method even beside a function named `len`, as long as `xs` has the method. A method without its parentheses is `cherimoya requires a method to be called, please write 'xs.len()'`.

**Overloading** follows from protocols and calls:

- Ordinary calls resolve by the first input's type, then the remaining inputs.
- Nullary functions resolve by the expected type, so `default()` resolves against it.
- More than one viable candidate is a compile error, which suggests one of three explicit selections:
  - **by module:** `geo.cmp(a, b)`
  - **by protocol:** `Ord.cmp(a, b)` calls `cmp` as `Ord`'s member
  - **by ascription:** `(p :: Ord)` treats `p` as a value of protocol `Ord`, which forces dynamic dispatch through it.

**Closures are ordinary callees,** including at statement level: `check(5);`.

**Operators as values.** `(op)`, an operator alone in parentheses, is the operator as a function value. Folds and a match's operator slot take bare operators and don't need it.

### 2.3 Pipes

- **`x |> f`** is `f(x)`. It is left-associative at level 16, so `xs |> f |> g` is `g(f(xs))`.
- **`f <| expr`** is `f(expr)`, where the argument is the entire expression to its right. It is right-associative and the loosest operator, so `print <| format(a + b)` and `double <| inc <| 2 + 3` need no parentheses.
- **Into a call:** the right side can be a call that gives every argument but the piped one. `x |> clamp(hi <- 10)` is `clamp(x, hi <- 10)`, and so is `clamp(hi <- 10) <| x`. The piped value is the first unnamed argument.
- **Into a method:** `xs |> .len` is `xs.len()`.
- Mixing `|>` and `<|` in one expression without parentheses is an error. Parentheses, brackets, and arguments start fresh, so `f <| (x |> g)` is fine.

### 2.4 Construction, conversion, and annotation

**Construction from a pack** builds aggregate values, at the postfix level:

- `Point <- [1.0, 2.0]` is positional.
- `Point <- [y <- 2.0, x <- 1.0]` uses named fields, in any order.
- `Some <- [x]` builds a sum variant. A payload-free variant is a bare name: `None`.
- `Vec2 <- [1.0, 2.0] <+> Vec2 <- [3.0, 4.0]` needs no parentheses.

In an expression, an infix `<-` is only ever construction, so it must be followed by `[`. Its left side has to be a type or a variant. At the top of a statement or a comprehension clause, `<-` ends the expression instead (3.3, 2.7).

**Calling a type** converts or makes a value:

- `i32(x)`, `f16(n)`, `char(r)`, `boolf(t)`: a conversion between numbers, characters, and fuzzy booleans, with the rules in 5.5–5.9. Widening is free, and anything that can lose a value traps unless it's proven safe, or wraps or saturates into a wrapping type.
- `NodeId(3)` and `i64u(id)`: a newtype conversion, both ways.
- `arr[n] mut T()`, `arr[n] mut T(fill)`, `arr[n] mut T(_)`, `arr T(seq)`, and `ptr T(v)` make arrays and handles (4.3).
- A conversion takes one unnamed value, and `x |> i8` converts too.

**Annotation.** `(value :: T)` checks the value against `T` and never converts it. The value is checked with `T` as its expected type, so a literal takes `T`'s type and anything else must already be a `T`: `(5 :: i8)` is an `i8`, `(300 :: i8)` doesn't fit, and `(x :: i8)` on an `int` is a mismatch. It is the way to type a literal where nothing else would, as in `(1 :: i8u) < 2`. Annotating with a protocol is ascription (2.2).

**Bit reinterpretation** is a stdlib intrinsic in the unsafe tier. **Size** is `size[T]()`, an intrinsic written as a function.

### 2.5 Equality

- **`=` and `!=` are shallow.** They compare the value a variable holds: handles compare by address (and, for arrays, length), floats compare bitwise, sums compare by tag plus shallow payload, and fixed arrays compare element by element with `=`.
- **`==` and `!==` are deep.** They dereference recursively until no addresses remain, compare packs and records element by element, and compare arrays by length and then element by element, each element with `==`. Floats compare by value: IEEE equality for NaN-free floats, and the total order for NaN-carrying ones, so all NaNs are deep-equal to each other and `-0.0 == +0.0`.
- **The orderings** `<` `<=` `>` `>=` compare values, so the "or equal" in `<=` is deep equality: `a <= b` is exactly `a < b || a == b`. Nothing orders arrays.
- **Characters and integers:** a value's bits are its value, so `==` agrees with `=`.
- **Recursive types** (recursive through a pointer) don't get automatic deep equality. They define `-> == ->`, which is the `Eq` protocol.
- **Shallow implies deep.** Equal bits always compare deep-equal. The reverse doesn't hold: `-0.0` and `+0.0` are deep-equal with different bits. There is no `===`.
- **IEEE comparisons** for NaN-carrying floats are `std.float` intrinsics (`ieeeEq`, `ieeeLt`, and the rest), which lower to a raw `fcmp`.

### 2.6 Ranges

Ranges are lazy values, written as packs containing `...`:

- `[0, ..., n]` is inclusive, and `[0, ..., <n]` stops below `n`.
- `[>0, ..., n]` starts above 0, and `[>0, ..., <n]` excludes both ends.

An **exclusivity marker** is a comparison on the bound, not a fixed start or end symbol: `>0` means "stay above 0", so a countdown ends with `>0`: `[n, n - 1, ..., >0]`. The marker has to agree with the stride's direction.

**Strides** come from the first two elements: `[0, 2, ..., <n]` steps by 2, and `[n, n - 1, ..., >0]` counts down.

- A stride is stored as a direction bit plus an unsigned magnitude of the endpoint type's width. The direction bit shares a flags byte with the endpoint inclusivity bits, and comptime strides are never stored at all.
- The magnitude is `second - first` ascending or `first - second` descending, so descending unsigned ranges never compute a negative value.
- Equal first and second elements are an error.
- **Iteration never overflows:** it stops by comparing the remaining distance to the end against the magnitude, never by adding the stride first. `[0, 2^63 + 1, ..., 2^64 - 1]` in `i64u` yields exactly two elements, and `[255, 128, ..., 0]` in `i8u` yields 255, 128, 1.

**Other forms:**

- **One-sided ranges.** `[0, ...]` is an infinite iteration source. `[..., <0]` and `[>100, ...]` are valid as patterns.
- **Character and `boolf` ranges** work too: `['a', ..., 'z']`, `[0.5, ..., 1]`.
- **Enums are iterable over their variants**, so `[N, ..., W]` works. Cycling uses the prelude's `succ` and `pred`, which wrap modulo the number of variants.
- **Parsing:** `...` is a pack element, not an operator. A range has exactly one `...`, at most two values before it, and one after.

### 2.7 Comprehensions

`[head : clauses]`, for example `[x * x : x <- xs && x % 2 = 0]`. Clauses are joined with `&&` and come in two kinds:

- **Membership clauses:** `x <- xs`. The left side is a pattern, so `Some[d] <- xs` keeps only the elements that match and binds the payload, and `[x, y] <- zip(xs, ys)` and `[i, x] <- enumerate(xs)` destructure.
- **Filter clauses:** every other clause. Its expression is the predicate. A filter clause that is an interval on a binder, `0 <= x < n`, also bounds that binder.

**Rules:**

- Every binder must be bounded by a top-level clause.
- Clauses nest in the order they're written, so later ones can depend on earlier binders.
- `||` between membership clauses and negated membership are rejected. Clauses are parsed just above `&&`'s level, so a `||` inside a filter clause needs parentheses.
- Binders are untyped. Their types come from their sources.
- A comprehension can reference local variables directly, since it's syntax, not a function.
- **The head is any expression,** including `match`, so elements can be transformed without being filtered out:

  ```
  [match (i % 2) { 0 -> { x * 2 -| } _ -> { x -| } } : [i, x] <- enumerate(xs)]
  ```

**Products and zips:**

- **Cartesian product:** `[[x, y] : x <- xs && y <- ys]`.
- **Zip by index:** `[[xs[i], ys[i]] : 0 <= i < min(xs.len(), ys.len())]`, where the interval makes both bounds checks provable.
- **Zip with the prelude:** `[x + y : [x, y] <- zip(xs, ys)]`.

**Laziness.** A comprehension is a recipe, not a list. Creating one computes and allocates nothing. Elements are produced one at a time, only when consumed by a `repeat` loop, a fold, or a materializing constructor, `arr T([...])`.

**Fusion.** A source may be any lazy sequence, including another comprehension. Nested comprehensions fuse into a single loop with no intermediate storage:

```
let revenue <- (+, 0)[v.qty * v.price : v <- [o : o <- orders && o.status = Paid]];
```

A comprehension whose predicate is a pure interval on its binder costs no more than a plain range.

### 2.8 Folds

`(reducer, seed)[sequence or comprehension]`:

```
(+, 0)[x * x : x <- xs && x % 2 = 0]         # sum
(*, 1)[1, ..., n]                            # n!
(max, MIN_INT)[scores]                       # function reducer
(&&, true)[x > 0 : x <- xs]                  # for all
(||, false)[x < 0 : x <- xs]                 # exists
((acc, x) -> { acc + x * rate -| }, 0)[xs]   # closure reducer
```

- **The reducer** is an operator token (built-in or extra), a function, or a closure, of type `(S, T) -> S`.
- **The result** has type `S`, and an empty sequence gives the seed.
- **Parsing:** a `(` in prefix position whose contents have a top-level comma and which is immediately followed by `[` is a fold. A one-element pack in the brackets is unwrapped, so `(+, 0)[[1, 2, 3]]` folds over the inner pack. Calls are postfix, so `inc((+, 0)[xs])` is unambiguous.
- **Lowering:** a comprehension and its fold fuse into one loop.
- **In refinements,** folds with `&&` and `||` are quantifiers, which Z3 encodes as real quantifiers.

A multi-stage reduction with a pack accumulator:

```
let (acc :: [f64, f64, f64], x :: f64) -> moments -> (r :: [f64, f64, f64]) {
    [acc[0] + 1.0, acc[1] + x, acc[2] + x * x] -|
}
let m        <- (moments, [0.0, 0.0, 0.0])[x : x <- samples];
let mean     <- m[1] / m[0];
let variance <- m[2] / m[0] - mean * mean;
```

### 2.9 Indexing, slicing, dereference, views

`[...]` after a value does four jobs: `xs[i]` indexes, `xs[a, ..., <b]` slices, `p[]` dereferences, and `Map[K, V]` or `alloc[T](n)` instantiates.

- **Indexing:** `xs[i]` takes any unsigned integer, wrapping or not, and a literal index is an `i64u`. A signed index is `cherimoya requires an index to be unsigned, please convert it, like 'i64u(i)'`, with a fix. An index known while compiling has to be in range: `cherimoya expected an index below 3 into 'xs', got 5 instead`. Any other index is checked at run time, and the check is elided when it's proven in range:

  ```
  trap at line 6: index 7 is past the end of 'xs', which holds 3
  ```
- **Nested arrays:** `m[i][j]`. An element's index runs before a value assigned to it.
- **Slicing:** `xs[a, ..., <b]` costs O(1): one bounds check at creation, and the result is always a `peek` view. Strided slices produce a separate strided-view type.
- **Dereference:** `p[]` reads the pointee, and `p[] <- v;` writes through the pointer, which needs `ptr mut T`. `p <- q;` rebinds the pointer itself, which needs `mut ptr T`.
- **Field access through pointers** auto-dereferences: `p.x`.
- **Address-of is type-directed.** Where a `peek T` is expected and a `T` place is supplied, a view is taken automatically, and a `ptr T` passed where a `peek T` is expected becomes a view. An `arr[N] T` converts to a `peek arr T` view for free. There is no address-of operator.
- **Raw pointers:** `Raw[T]` in `std.unsafe` is a non-owning pointer with no drop glue. The unsafe-tier intrinsics that convert between pointers and integer addresses (for MMIO and page tables) produce and consume `Raw[T]`, never an owning `ptr T`.

  > Open: how `Raw[T]` relates to `ptr void`, in FFI (11).
- **In-place strided updates:** `repeat (i) <- [0, 2, ..., <xs.len()] { xs[i] *<- 2; }`.

### 2.10 Packs, tuples, and records

- **A pack is `[a, b, c]`.** A homogeneous pack is an `arr[N] T` and a tuple `[T, T, ...]` at once, with identical layout. A heterogeneous pack like `[1, 2.0]` is a tuple only.
- **Element types** of a homogeneous pack are picked the way an operator picks its operands': the element type the context expects, else the first element that isn't a literal, else a float literal's type. `let xs :: arr[3] i8 <- [1, 2, 3];` makes three `i8`s, `[1, x]` takes `x`'s type, and `[1, 2.5]` is floats. An empty pack takes its type from where it goes: `([] :: arr[0] int)`.
- **Element access** is `t[i]`. For a heterogeneous tuple the index must be comptime, so the element type is known.
- **Tuple types** are written `[int, f64]`.
- **A named pack** `[x <- 1.0, y <- 2.0]` is an anonymous record, typed structurally, with type `[x :: f64, y :: f64]`. `::` only ever introduces a type, so record values always use `<-`. Fields are accessed with `.x` and by position with `[0]`. Inside a pack, `name <- value` is always a named element, even when the value is a pack which is a construction when `name` is a type.
- **Record fields** can have defaults, the way inputs do.
- **A pack containing views** (such as `[peek A, peek B]`) is peek-like (4.7).
- **Void** is written `()` in signatures. `()` and `(a, b)` in an expression are errors in the uses form that points at packs, with a fix that turns the parentheses into brackets.

### 2.11 Dictionaries

A dictionary literal is a pack of pairs, converted through the expected type:

```
let ages :: Map[string, int] <- [["ada", 36], ["alan", 41]];
```

`->` is never a key/value separator. Writing one inside a pack gets a uses-form diagnostic pointing at the pair form.

### 2.12 Closures

**An unnamed function** is the definition form without a name. In prefix position, a parenthesized list followed by `->` is always a closure, since `->` has no other expression meaning. `(inputs) -> {` is an inferred closure, and `(inputs) -> (outputs) {` an annotated one:

```
let step <- (acc, x) -> { acc + x * rate -| };                  # types inferred from context
let f    <- (a :: int, b :: int) -> (r :: int) { a * b -| };    # fully annotated
let r    <- f(6, 7);
```

Closures need no parentheses inside packs or as fold reducers: `[(x :: int) -> (r :: int) { x + 1 -| }, g]`. Inside a pattern, `( ... ) ->` is a parenthesized pattern before its arm, never a closure.

**Implicit captures are views.** A closure that mentions a local captures it as a `peek`. Such a closure is peek-like: it can be passed down into calls but can't escape the scope. It costs nothing and allocates nothing.

**Owning captures are explicit,** listed in a capture pack before the inputs. They copy or move values into the closure:

```
let scale <- [factor] (x) -> { x * factor -| };
```

A pack followed by a parenthesized list and `->` is a capture closure, since calling a pack is never valid. A closure with only owning captures is an ordinary value, so it can be returned or stored. Moving a captured `unscoped` value into a closure discharges it.

**Nested functions don't capture.** A named function inside a body can't use the enclosing function's locals: `cherimoya uses closures to capture 'n' instead of nested functions as follows: let f <- [n] (...) -> { ... };`.

**Types:**

- Each closure has its own unique type.
- The lang protocol `Fn[F, [Args], R]` is satisfied by closures and ordinary functions alike.
- Generic code takes `[F :: Fn[[S, T], S]]` and stays monomorphized.
- Storing different closures in one place goes through a protocol value, `unscoped ptr Fn[...]`, with dynamic dispatch. This boxing is the only cost.

**Yields.** A closure body is the closure's own scope, so a bare `-|` at its top level leaves it. To leave from deeper inside, name a block around the body.

**Verification.** Monomorphization inlines closure bodies, so the verification-condition generator sees concrete code. A closure used inside a refinement must be pure.

### 2.13 Extra operators

The extra operators (section 1) have no built-in meaning. Programs give them meaning by definition, exactly like the overloadable built-ins (5.3).

- **Binary only.** Extra operators have no prefix form.
- **Placement** comes from the operator table. Programs can't introduce tokens outside it or change a token's level.
- **Compound assignment** comes for free where the shape fits: `p **<- 2;` means `p <- p ** 2;`, with the place evaluated once.
- **`$`** is right-associative at level 15, so `f $ g $ x` is `f $ (g $ x)`, which is how application reads in the languages it comes from.
- **Generic code** bounds on extra operators through ordinary protocols:

  ```
  type Spaceship[T] <- [let (a :: peek T, b :: peek T) -> <=> -> (r :: int)];
  ```

---

## 3. Statements and scopes

### 3.1 General

- Statements end with `;`.
- Scopes (blocks, `match`, `repeat`, `chunk`) are expressions. At the start of a statement, a scope is a statement of its own and takes no `;`. If `-|` follows it, the statement yields the scope's value: `match (n) { ... } -|`.
- Yield statements end at their target and take no `;`.
- A program, and every body, is a list of statements. Declarations are statements too, so functions can be defined inside bodies and `let LIMIT <- 4096;` and `wit {Vec2 :: Ord};` work at the top of a file.

### 3.2 Bindings

```
let x <- 5;                                  # type inferred
let x :: int <- 5;                           # annotated
let x :: mut int <- 5;                       # mutable binding
let x :: mut <- 5;                           # qualifier only, type inferred
let v :: arr[16] mut int <- _;               # uninitialized (4.8)
let [q, r] <- divmod(17, 5);                 # destructuring a pack
```

- **Every binding needs an initializer,** and `_` explicitly means uninitialized, which needs an annotation, since there's nothing to infer from, and `mut` (on the elements, for an array), since assigning later is the only way it gets a value. A missing initializer gets a fix that adds `<- _`.
- **Bindings are immutable** unless declared `mut`.
- **A binding can't hold `void`:** `cherimoya requires a binding to have a value, please write it as its own statement instead`.
- **The initializer is checked before the name is declared,** so `let x <- x + 1;` sees an outer `x` or nothing.
- **Comptime is folded into `let`.** A `let` without `mut` whose initializer can be evaluated while compiling is comptime-eligible. Contexts that need comptime values (array sizes, generic value arguments) demand eligibility, and diagnostics explain why a binding isn't. Section 7 covers what comptime can evaluate.
- **Shadowing:** an inner scope may shadow an outer name, with a warning. Redeclaring a name in the same scope is an error, with a note at the first declaration.
- **`wit` bindings:** `let n0 :: wit <- nodeCount(g);` creates a verifier-only value. It may appear only in `wit`, `assert`, and refinement contexts, never flows into runtime values, and costs nothing at run time.
- **Top-level bindings** are globals (6.4).

### 3.3 Assignment and expression statements

A statement starts with an expression, which `<-` ends, and what follows it decides what the statement is:

- `<-` or a compound token: an assignment, and the expression must be a place.
- `-|`: a yield of that value.
- otherwise: an expression statement, ending with `;`.

**Places:** names, fields (`a.b`), indexes (`a[i]`), dereferences (`p[]`), and `_` (discard).

**Rules:**

- The right side is a full expression, and a construction on it needs no parentheses: `p <- Point <- [1, 2];`.
- No chained assignment.
- **Mutability:** only a `mut` binding can be assigned, and only `mut` elements written. The error says how to declare it, with a fix: `cherimoya requires 'x' to be mutable to assign to it, please declare it with 'let x :: mut <- ...;'`.
- **Compound assignment:** `place +<- expr;` and the rest of the generated set, with the place evaluated once: `counts[hash(k) % n] +<- 1;`. `&&<-` and `||<-` short-circuit: the place is read first, and the right side only runs when the place doesn't already decide the answer.
- **Overwriting an owning place** frees the old value first (4.10).
- **Discards.** A value can't be dropped. An expression statement whose type isn't `void` or `never` has to be discarded explicitly, with a fix: `cherimoya requires a value to be used or discarded, please write '_ <- add(1, 2);'`. `_ <-` refuses a value that could be an error, which must be handled with a match instead, so every error is non-ignorable.

  > Open: how a value that could be an error is represented (an error sum, a protocol, or something else), which `_ <-` and the "non-ignorable" rule depend on.

### 3.4 Yields (`-|`)

- `expr -|` yields a value to the innermost scope.
- `expr -| target` yields to a specific target.
- `-| target` leaves the target without a value.

**Targets**, resolved statically by walking outward, innermost first:

| Target | Meaning |
|---|---|
| bare `-\|` | the innermost scope (block, match arm, loop body, function or closure body) |
| `-\| match` | the innermost `match` |
| `-\| repeat` | leave the innermost loop (break) |
| `-\| binder` | finish the current iteration of the loop binding `binder` (continue) |
| `-\| name` | the function or scope explicitly named `name` |

A function's label is its name. After `-|`, an identifier, `repeat`, or `match` is always the target.

**Explicit names** go right after the keyword, or before the brace for plain blocks:

- `repeat outer (i) <- ...`
- `match name (x) { }`
- `chunk name { }`
- `name { ... }` for a named block, only at the start of a statement. In an expression, `xs {` is the end of the expression followed by a block, which is what `repeat (x) <- xs { }` needs.

**Rules:**

- **Labels live on the target stack, not in scopes,** so ordinary variables never capture them.
- **The search stops at the nearest function,** so a yield can't reach into an enclosing one. A yield with no target is `cherimoya expected a function or scope named 'outer' around this yield, got none instead`.
- **Shadowed binders:** when nested loops bind the same name, the innermost wins, and an outer one needs an explicit name.
- **No implicit exits.** There is no propagation operator. A function's body is the function's own scope, so a bare `-|` at its top level gives its result, but a bare `-|` in a nested scope yields only to that scope. An early exit is written `-| funname`, which makes `-| name` a reliable grep for a function's early exits.
- **Yields have the `never` type,** and so does anything that doesn't come back: a scope nothing leaves normally, and an operator or call with a `never` operand. `never` fits anywhere, so arms that yield elsewhere still typecheck.
- **For the CFG,** every yield is an edge to a statically known target.

**How scopes get their types.** A function's type is declared, so each yield to it is checked there. Every other scope's type comes from what reaches it: the first yield sets it, every later one must agree (with a note at the first), and when the scope closes:

| Yields to it | Can fall off the end | Type |
|---|---|---|
| none | no | `never` |
| none | yes | `void` |
| some | no | their type |
| some | yes | an error unless their type is `void` |

The last row is the every-path rule: `cherimoya requires every path through this block to yield 'int', please end it with a yield`. A non-void function that can fall off its end is the same error.

**Unreachable statements** after one that never continues get a warning on the first of the run, with a fix that removes them (keeping functions, which are hoisted).

### 3.5 Conditionals

There is no `if`. Every conditional is a `match`, with a subject or subjectless (3.6). A one-arm subjectless match is the guard form:

```
match { (xs[i] < 0) -> { -| i } }                 # continue
match { (n > 100) -> { 100 -| early } }           # early function exit
```

**For several conditions,** use a decision table:

```
match ([x, y]) {
    [true,  true ] -> { ... }
    [true,  false] -> { ... }
    [false, _    ] -> { ... }
}
```

There is no else branch, so a block `{ ... }` directly after a match is always its own statement.

### 3.6 `match`

**Subject form:**

```
match (n) {
    [..., <0]     -> { 0 -| }
    [0, ..., 100] -> { n -| }
    [>100, ...]   -> { 100 -| }
} -|
```

**Subjectless (guard) form:**

```
match {
    (a < b && ready) -> { ... }
    _                -> { ... }
}
```

- `match` is an expression whose value is what its arms yield. The subject is evaluated once.
- **Arms** are `pattern -> { body }`, with nothing between them. They're tried in order, and the first that matches runs.
- **Guards** must be `bool`. A `boolf` is never a condition: `cherimoya requires a guard to be a bool, please compare the boolf with a cutoff, like 'x > 0.5'`, with a fix that writes the cutoff.
- **The equality slot:** patterns compare with deep equality by default. `match (=, x)` compares shallowly, and `match (==, x)` spells the default. For floats, deep equality lets a `0.0` arm catch `-0.0`, and shallow compares bits. For strings and handles, deep compares contents and shallow compares addresses.

**Patterns** (every one is written like an expression):

- literals, including negated numbers, and `_`
- binders, visible only in their own arm. A bare name is a binder unless it names a payload-free variant.
- variants with nested patterns, like `Some[['0', ..., '9']]`
- packs and tuples: `[true, _]`, `[a, b]`
- records: `[x <- 0, y <- py]`
- inclusive, exclusive, and one-sided ranges
- set-builder predicates such as `[x : x % 2 = 0]`, which subsume guard clauses

Integers, floats, characters, runes, and `boolf`s are all valid subjects, with literal patterns that adapt to the subject's type.

**Exhaustiveness.** An incomplete match is an error (a case goes unhandled), with a fix that adds `_ -> { }`: `cherimoya requires a match on 'int' to cover every value, please add a '_ -> { ... }' arm`.

- **When it applies:** a match must be complete whenever a missing case would matter: a match on a number, character, or `boolf` always needs a catch-all (`_` or a binder) or ranges that cover its domain, and any match whose value is used must be complete. A subjectless match whose value is void, like the guard form, needs no `_`.
- **Sums and booleans:** variant coverage, including across packs. `true` and `false` together cover a `bool`.
- **Ranges:** interval arithmetic over the subject's domain. Refinements narrow the domain, so a `Percent` only needs 0 through 100 covered.
- **Set-builder patterns:** need Z3, and until Z3 can decide them they need `_`.
- A pattern the compiler can't reason about covers nothing.
- **Unreachable arms** after full coverage are a warning, with a note at the covering arm and a fix that removes them.

**Payload ownership:** matching a temporary moves its payloads into the arm, and matching a place views them as `peek`. A handle moved in some arms is moved after the match, and each arm that didn't move it frees it at its end.

### 3.7 Every-arm match

A match whose operator slot holds a combining operator instead of an equality runs **every** arm whose pattern holds, in order, and folds the arms' values with that operator. The operator's category tells a comparator from a combiner. A lone operator in the header is the combiner, and a subject can follow it:

```
match (+) {
    (n % 3 = 0) -> { 1 -| }
    (n % 5 = 0) -> { 2 -| }
    _           -> { 0 -| }
}

match (+, n) { patterns }
```

`match (;)` is the void form: every true arm runs, for its side effects only.

- **A bare `-|`** in an arm gives that arm's value, and the match goes on to the next arm.
- **`-| match`** gives the arm's value and stops the match (break). Its value is combined with what came before, not substituted for it, so an arm that must be the whole answer goes first: when it runs, nothing before it ran and nothing after it will.
- **`_`** means no other arm ran.
- **A binder arm** (`m -> { }`) always runs.
- **Completeness:** a value-yielding every-arm match must be complete, so no seed is needed.
- **Guards can't observe each other:** an arm can't assign anything a later arm's guard reads, checked by name, so when guards are tested is unobservable.
- There is no exclusive form and no attribute for one. "Only one runs" is written per arm with `-| match`.

### 3.8 `repeat`

```
repeat (x) <- source { }               # for-each over a range, array, or Iterable
repeat (cond) { }                      # while
repeat { }                             # infinite
repeat (step) <- [0, ...] { }          # counter
repeat outer (i) <- [0, ..., <n] { }   # explicitly named
repeat (i) <- xs wit {inv} { }         # invariant before the body
repeat (cond) wit {inv} { }
repeat ([i, x]) <- enumerate(xs) { }   # pack pattern binder
```

- **For-each versus while:** `(binder) <-` is for-each, and a parenthesized expression without `<-` is a while condition, which must be a `bool`.
- **Pack-pattern binders** destructure each element, and every name a pattern binds is a continue target.
- **A bare `-|` in the body** leaves the loop, the same as `-| repeat`.
- **Counters cost nothing extra:** a counter binder used only as a yield target or in `wit` invariants costs nothing at run time.

**Loop values:**

- An infinite `repeat { }` can only be left by a yield, so its type is the yielded type, or `never` if nothing leaves it.
- A loop with a header (for-each or while) that yields values has type `Option[T]`, with `None` on natural completion.
- A loop that never yields a value is `void`.

**A handle declared outside a loop** can be moved inside it only if it's assigned again before the iteration ends: `cherimoya requires 'xs' to hold a value at the end of every iteration, please assign it again after moving it on line 7`. So `repeat (...) { xs <- grow(xs); }` works.

**Iteration** desugars to `at(source, i)` for i = 0, 1, 2, ... until it gives `None`, through the `Iterable` protocol:

- `at` gives `Option[E]`, where the element type `E` may itself be a view type.
- `List[T]` iterates with `E = peek T`.
- `zip` iterates with `E = [peek A, peek B]`, a pack of views returned by value, which is peek-like and can't escape.
- There are no iterator objects.

### 3.9 `chunk`

```
chunk {
    let scratch :: mut <- listNew[i64u]();
    ...
}
```

- A `chunk` yields like a block and can be named.
- **Lexical default.** Allocations written inside the block whose allocator would default to `Global` default to a `Chunk` allocator type that exists only within that block, giving types like `arr[Chunk] T`. Code in called functions is unaffected.
- **Escaping** the block is a type error.
- At `}`, the bump region resets in one step.
- Nested chunks each get their own type.
- **No reentrancy.** A chunk type is zero-sized, so it can't tell two live instances of the same chunk apart:
  - A function containing a `chunk` may not be part of a recursive cycle in the direct-call graph. This is a compile error.
  - Cycles through indirect calls (closures, protocol values) are caught at run time by a one-byte active flag per chunk type, which traps on re-entry in the default and debug tiers. With concurrency, the flag is thread-local.
- **Revertible chunks.** A chunk can be revertible, like a transaction: its writes are kept only if its postcondition holds.

  > Open: the syntax for a revertible chunk and its postcondition.

### 3.10 Verification statements

- **`wit {p};`** is a verification obligation. When it's unproven, the tier decides: a compile error in strict, a run-time check in default, trusted in unsafe.
- **`assert (p);`** is always enforced. It's elided when proven, and otherwise stays a run-time trap in every tier, never a compile error:

  ```
  trap at line 3: assert (n % 2 = 0);
  ```
- **`wit {T :: P};`** is a compile-time assertion that `T` follows protocol `P`.

Rule of thumb: `assert` when the check must happen, `wit` when the proof must happen. Refinements, `wit`, and assertions are always type-checked: their names resolve and they're `bool`s.

### 3.11 Place intrinsics

These are keywords written like calls, but they take **places, not values**, so their arguments are never moved:

- **`swap(a, b);`** exchanges the values in two places.
- **`replace(slot, new)`** stores `new` in `slot` and gives back the old value.
- **`take(slot)`** moves the value out of `slot` and gives it back. On an array element, the slot becomes unwritten (4.8), which is how a value moves out of an element. On any other place, the place becomes moved-from: it must be assigned again before it's used, and drop glue skips it.

### 3.12 Safety tiers

- `@tier(strict) { ... }` sets the tier for a region.
- `@tier(strict);` at the top of a file sets it for the whole module.
- The tiers are `strict`, `default`, `debug`, and `unsafe`, and section 10 says what each one does with an unproven obligation.

---

## 4. Memory model

### 4.1 Modes

**Modes** (`unscoped`, `peek`, `wit`) say how a binding relates to its memory, and the **qualifier** `mut` says what can change.

**Scoped** (the default, with no keyword) owns its memory, which is freed at the end of its scope unless the value was moved out first.

**`unscoped`** owns heap memory that must be discharged before its scope ends. The ways to discharge it:

- yield it
- move it into something that outlives the scope
- pass it to an `unscoped` input (4.6)
- capture it by move in a closure
- pass it to an extern input declared `unscoped`

Anything else is a compile error: `cherimoya requires 'xs' to be moved before 'grow' ends, please yield it or pass it to an unscoped input`.

**`peek`** is a non-owning view. It never frees memory and can't escape, except as 4.7 allows.

**Across modes:**

- Stack memory is never `unscoped`, and nothing moves from the stack to the heap after the fact.
- Received heap values can change mode freely. Binding a received `unscoped` value as scoped means "I'll take it from here."

### 4.2 Qualifier positions

For arrays and handles, the position of `mut` decides what it applies to:

| Written | Means |
|---|---|
| `mut arr T`, `mut arr[N] T`, `mut ptr T` | the binding can be rebound or replaced |
| `arr mut T`, `arr[N] mut T` | the elements can be written |
| `ptr mut T` | the pointee can be written |
| `mut T` on a value type | a mutable binding |

Both positions can apply at once. Nested arrays take one per level, so `arr[2] mut arr[3] int` can replace its rows but not their elements. A bare `mut` on a type left to inference, `let m :: mut <- [[1, 2], [3, 4]];`, allows everything at every level. The type is the same wherever `mut` sits, since `mut` says what a binding can do, not what the value is. A binding left to inference can write what the call that made its handle says, so `let b <- arr[n] mut int(0);` can write `b[0]`.

**Fields** carry no `mut` of their own. Their mutability comes from the container: through a `peek mut S`, every field is writable.

### 4.3 Arrays and handles

| Type | What it is |
|---|---|
| `arr[N] T` | an inline fixed-size array, `N` a comptime integer, the same thing as a homogeneous pack. A value, copied when bound, passed, or returned |
| `arr T` | a handle: the elements' address and their run-time length (and an allocator instance pointer when the allocator is stateful) |
| `arr[A] T` | a handle whose memory came from allocator type `A`. A bare `arr T` is `arr[Global] T` |
| `ptr T` | a single-object owning handle, never null |
| `Option[ptr T]` | a nullable pointer, `None` stored as null, with the size and ABI of a C pointer. There is no `nullptr` |
| `Raw[T]` (`std.unsafe`) | a non-owning raw pointer, no drop glue, unsafe tier |

Arrays and handles nest, `arr arr int`, and a fixed array can hold handles, `arr[3] ptr int`.

**`arr[A]` versus `arr[N]`:** the argument's kind decides. A type argument is an allocator, and a comptime integer is a size: an integer literal or an immutable binding that folds to one, as in `let N <- 2 + 1;`. Anything else, including a size below zero, is `cherimoya requires an array's size to be known when the program compiles, please use an integer literal or an immutable binding of one`.

**`Option` is built into the compiler,** ahead of user generics, so handles, iteration, and loop values can use it from the start. It is still written and behaves as an ordinary generic sum, `Some[T] | None`.

**Handle size** is two words for zero-sized allocators (`Global`, `Chunk`). A stateful allocator adds a third word pointing at its instance. **Moving** a handle costs O(1) and copies no elements.

**Making arrays and handles.** Elements are always initialized unless `_` is written:

| Written | Makes |
|---|---|
| `[1, 2, 3]` | a fixed array. Bound immutably, it lives in read-only data, and bound as `mut`, it's copied into the scope |
| `arr[n] mut T()` | `n` elements of `T`'s default (`Default`): `0`, `0.0`, `false`, `'\0'`, and an empty array for an array element |
| `arr[n] mut T(fill)` | `n` deep copies of `fill`, which is borrowed, so `p` still holds its value after `arr[n] mut ptr int(p)` |
| `arr[n] mut T(_)` | `n` uninitialized elements (4.8) |
| `arr T(seq)` | the elements of a fixed array, range, or comprehension, materialized |
| `ptr T(v)` | `v`, in its own allocation |

An array's length goes in brackets, where a fixed array's size does, and the size alone decides what's made: a length known when the program compiles makes a fixed array, `arr[3] int(0)` is `[0, 0, 0]`, and a run-time length makes an `arr T` on the heap, which is any unsigned integer: `cherimoya requires an array's length to be unsigned, please convert it, like 'i64u(n)'`. A type never holds a run-time length. Brackets read a value the way they read a size, so `arr[xs.len() + 1] int(0)` works, since a type is never called there. `arr T(xs)` and `ptr T(v)` move what they're given when it owns anything. Allocation failure, or a length whose size overflows, traps:

```
trap at line 4: out of memory making 'arr[n] mut int()'
```

**Length** is `xs.len()`, an `i64u`, from the `Sized` protocol, which every array implements.

**Growable lists** are the stdlib type `List[T, A :: Allocator <- Global]`.

### 4.4 Placement

Everything goes on the stack unless its size is truly variable, and only that goes on the heap. Placement is decided at the allocation site and never changes afterward.

| Value | Placement | Freed |
|---|---|---|
| size bounded by its type: `arr[N] T`, scalars, records of them | inline, wherever its binding lives | with its binding |
| size bounded by a refinement, including growth up to a stated bound | the bound is reserved on the stack | at the end of its scope |
| size truly variable (no bound) | heap | when its owner is done with it |
| `peek` | wherever the original lives | never by the view |

A list declared to hold at most 64 elements reserves 64 on the stack. There is no run-time split between stack and heap: placement never depends on a value seen while running.

A handle whose size the program knows when it compiles, and that never leaves its function, is placed on the stack too: one that's never moved or replaced, or a temporary, up to 4 KiB each. That's an optimization a program can't observe, since the handle behaves the same either way.

**Why type-based.** Placement depends on types, including their refinement bounds, never on what Z3 happens to prove, so it stays stable across compiler versions and unrelated edits.

**Moving out.** Moving a value that lives on the stack out of its scope is a compile error, and `unscoped` is how a value is made to outlive it.

**Unbound constructions** take their placement from the expected type: a construction flowing into an output declared `Option[unscoped ptr Expr]` is allocated on the heap.

**Fields:** owning `ptr` fields always point to the heap, and unbounded array fields are always on the heap.

> Open: how stack placement of a refinement-bounded `arr T` meets `unscoped` and outputs: whether `unscoped` (and every `ptr T`) always means the heap, and what happens when a function yields a bounded `arr T` it made.

> Open: whether a flag forbids implicit heap use (a size with no bound that the program didn't mark), now that there is no run-time split for one to report.

### 4.5 Ownership and moves

A binding owns the handle it holds. **Moving:** an owned handle has one owner, so binding it, `let b <- a;`, assigning it, `b <- a;`, yielding it, and putting it in a pack, a `ptr`, or `arr T(xs)` all move it, and the place it came from holds nothing:

```
cherimoya requires 'a' to hold a value to use it, please assign it again after moving it on line 4
```

- Assigning to a moved binding makes it hold a value again, and assigning to one that still holds a value frees that value first.
- A handle made and never bound, like `arr[n] int().len()` or the argument in `total(arr[n] int())`, is freed at the end of its statement.
- **Elements:** moving out of an element, `let p <- ps[0];`, is an error, since it would leave a hole the array frees. `take(ps[0])` does it (3.11).
- **Top-level bindings** can't be moved, since any function can use them at any time: `cherimoya requires 'table' to be owned by a function to move it, please use it in place, since every function can use it`.
- **Binders** borrow the subject they match, like an input, so moving one is `cherimoya requires 'b' to be owned to move it, please move what it matched instead`.
- **Matches and loops:** see 3.6 and 3.8.
- Ownership violations are errors.

### 4.6 Inputs and outputs

**An input borrows.** `total(xs)` lends `xs` for the call, so the caller still owns and frees it, and the callee works on it in place: it can read it, write its elements when they're `mut`, and pass it on to another call. It can't keep it, so moving, rebinding, or yielding it is an error: `cherimoya requires 'xs' to be owned to move it, please declare it 'unscoped' to take it over`. A borrowed input follows the peek rules (4.7).

> Open: whether a plain input is implicitly a `peek` once views exist, or `peek` is written on inputs that borrow.

**An `unscoped` input takes ownership.** `(xs :: unscoped arr mut int)` owns what it's given, so a call moves the caller's argument in, without saying so, the way a binding moves, and the caller can't use it until it's assigned again. The callee must discharge it (4.1), and binding it is one way, so `let given <- xs;` makes the callee's own binding the owner, which frees it at the end of its scope. One moved in only some arms of a match is an error on the others, since freeing it there would be letting it go. That is how a function hands an array back changed: `xs <- grow(xs);`.

**Outputs move out.** A function can yield a handle it owns, one it made or took over, and the caller owns it from then on.

### 4.7 Peek rules

- **Escape ban:** a peek may not outlive the scope of any of its roots.
- **Root sets.** Every projected peek (field, element, slice, returned projection) carries the set of roots it may derive from. Mutating any root in the set invalidates the peek, and using an invalidated peek is a compile error. This is intraprocedural dataflow over the lattice valid → invalidated.
- **Return elision:** a function may return a peek (alone or inside a type such as `Option[peek T]` or `[peek A, peek B]`) when every view in it derives from peek inputs. There are no lifetime annotations:
  - Each view position in the result is assigned the set of inputs it may derive from. `zip` returns `[peek A, peek B]` with position 0 from `xs` and position 1 from `ys`, and `longest(a, b)` returns a view from `{a, b}`.
  - At the call site, the result is a projection of every argument in its set. It is invalidated when any of them is mutated, and it can't outlive the shortest-lived one.
- **Call-site exclusivity:** a `peek mut` argument conflicts with every other live view of the same root, including views passed as other arguments of the same call. `pushAll(v, v[0, ..., <2])` is a compile error, since the callee could reallocate `v` while reading the slice.
- **Peek-like types:** a type with a peek field is itself peek-like, and so are packs containing views and closures with implicit captures. Values of peek-like types follow the same rules.

### 4.8 Uninitialized storage

Arrays may be created uninitialized, with `<- _` or `arr[n] mut T(_)`. Until Z3 can prove the elements are written before they're dropped, only elements that own nothing can start out uninitialized, and a binding that owns something can't: `cherimoya requires an uninitialized binding to own nothing, please give it a value`.

**Reads need proof.** Every read of an element requires proof that it was written. Z3 proves this using invariants, for example `wit {written(a) == [0, ..., <i]}`. When it can't, the tier decides:

- **strict:** compile error
- **debug:** run-time bitmap check, one bit per element, set on write and checked on read
- **unsafe:** trusted

**Drops need a known initialized range,** which only matters for element types that need dropping:

- **Trivial element types** (numbers, characters, records with no owning fields) have nothing to drop.
- **Owning element types:** the containing type states the initialized range in its invariant, with the verifier intrinsic `written(a)`. Z3 checks that every operation keeps it true, and drop glue evaluates that range at run time and drops exactly those elements:

  ```
  type List[T, A :: Allocator <- Global] <- [data :: arr[A] mut T, count :: i64u]
      {l : l.count <= l.data.len() && written(l.data) == [0, ..., <l.count]};
  ```
- **Without a stated range,** the array must be proven fully initialized at every exit from its scope.

**`take` on an element** marks it unwritten, so `take` doesn't require `Default`.

**Staging:** until Z3 can check `written`, uninitialized arrays are allowed only for trivial element types and in the unsafe tier.

### 4.9 Allocators

- **`Allocator[A]`** is a lang protocol: `alloc[E]` gives `Option[unscoped arr[A] mut E]` (uninitialized), and `release` takes memory back.
- **`Global`** is zero-sized and the program-wide default. Its methods are compiler-provided, and use the system's allocator, or whatever a kernel registers with `@globalAllocator`. Running out of memory traps.
- **The allocator is part of the handle type,** so drop glue always releases memory to the right allocator.
- **Generic defaults** use `<-`: `A :: Allocator <- Global`.

### 4.10 Drops and copies

- **Order:** at the end of a scope, at its `}` or wherever a yield leaves it early, owned values are dropped, last-declared first. A custom `Drop` runs first, then the field drop glue, which recurses, so an array that owns its elements frees them first.
- **Moved-from and unwritten** places are skipped.
- **Overwriting an owning place,** including writing an element or a pointee that owns something, frees the old value first.
- **Top-level bindings** are freed when the program ends, after `start` returns (and after each test), so a run under valgrind ends with nothing leaked.
- **A trap frees nothing,** since the process is ending.
- **Copies:** trivial types copy implicitly, and owning types move. A deep copy uses the `Clone` protocol. The compiler deep-copies its own handles (arrays and pointers all the way down) where it needs to, as in `arr[n] mut T(fill)`.
- **`Raw[T]`** is trivial: it copies and is never dropped.
- There is no `defer`.

---

## 5. Types

### 5.1 Definitions

Every type definition has the form `type Name[generics] <- RHS;`:

| Form | Example | Meaning |
|---|---|---|
| newtype | `type NodeId <- i64u;` | a distinct type, explicit conversion both ways, not inheriting the underlying type's protocol implementations |
| refinement | `type Percent <- int {p : 0 <= p <= 100};` | a subtype that substitutes into its base. Values flow in when the predicate is proven, or are checked per tier. Inherits protocol implementations |
| product | `type Point <- [x :: f64, y :: f64];` | a newtype over a record, optionally with an invariant `[...] {p : ...};` |
| sum | `type Shape <- Circle[f64] \| Rect[f64, f64];` | tagged, two or more alternatives |
| enum | `type Dir <- N \| E \| S \| W;` | a payload-free sum, iterable over its variants |
| protocol | `type Ord[T] <- [let (a :: peek T, b :: peek T) -> cmp -> (r :: int)];` | a pack of signatures, separated by commas, recognized by the leading `let` |
| generic | `type Option[T] <- Some[T] \| None;` | |

A single alternative is never a sum, so `type Wrapper <- Foo[int];` is a newtype of `Foo[int]`. Type aliases and unsafe unions don't exist.

### 5.2 Refinements

- **In type definitions,** the binder is named explicitly: `{p : 0 <= p <= 100}`.
- **On inputs, outputs, and bindings,** the predicate uses the binding's own name: `a :: int {a >= 0}`.
- **Input refinements are preconditions,** and output refinements are postconditions. Outputs may mention inputs, sibling outputs, and `old p`:

  ```
  let (a :: int {a >= 0}, b :: int {b > 0})
      -> divmod
      -> (q :: int, r :: int {0 <= r < b && a = q * b + r}) { [a / b, a % b] -| }
  ```
- **Entry values:** inside an output refinement, `old p` means input `p` as it was on entry: `{nodeCount(g) = nodeCount(old g)}`.
- **Pure functions only.** Functions called inside refinements must be pure and terminating. Purity is inferred.
- **No recursion yet.** Until termination measures exist, a recursive function may not appear in a refinement. Non-recursive pure functions and folds are allowed.
- **Quantifiers** are folds: `(&&, true)[...]` and `(||, false)[...]`.
- **Flow sensitivity:** match subjects, patterns, and guards establish facts for Z3 inside their arms.
- **Bounds and placement:** a refinement that bounds a size decides stack placement (4.4).

### 5.3 Protocols

A protocol is a set of operations that any number of types can each provide.

- **Implementer parameter:** a protocol's first type parameter is the implementer. A bound fills it in implicitly: `T :: Ord` means `Ord[T]`, and `L :: Iterable[peek int]` means `Iterable[L, peek int]`.
- **Satisfaction is structural:** `T` follows `P` if functions matching every signature in `P` are visible. There's no declaration and no `impl` block.
- **Methods:** each signature whose first input is the implementer is a method of every type that follows the protocol, called as `x.m()` or `m(x)` (2.2).
- **Small protocols:** each protocol is one capability. A call on a known type dispatches statically, so many small protocols cost nothing.
- **Assertion:** `wit {T :: P};` asserts satisfaction at the definition site.
- **Ambiguity:** more than one visible candidate implementation is an error, resolved by module, protocol, or ascription (2.2).
- **Retrofitting** a protocol onto a type you didn't write is just defining the functions. Witness identity keeps this coherent.
- **Dispatch** is static when the type is known, and goes through a vtable for protocol values.

**Witnesses are part of type identity.** Satisfaction depends on what's visible, so two modules can see different implementations of the same protocol for the same type. The type system keeps them apart:

- **Which bounds count:** only bounds declared on a type itself. `type Map[K :: Ord, V]` records a witness for `Ord[K]`, and `type List[T]` records none. Function-level bounds never split types.
- **Resolution:** the witness is resolved where the type is instantiated, against the implementations visible there.
- **Identity:** the instantiation key is the head, the arguments, and the witness for each type-level bound. `Map[Key, int]` built with module A's `cmp` and with module B's `cmp` are different types with different mangled names.
- **Witnesses flow from arguments into bounds.** A generic function receiving a `Map[K, V]` satisfies its own `K :: Ord` with the witness that argument carries. A bound is resolved fresh only when no argument carries a witness for it, and two arguments carrying different witnesses for the same bound are an error.
- **Diagnostics** show witnesses only when they are the difference, with notes at both implementations and both instantiation sites:

  ```
  cherimoya expected Map[Key, int] with Ord[Key] from module desc, got Map[Key, int] with Ord[Key] from module asc instead
  ```

**Operator overloading:** the operator goes in the name slot of a definition, and arity tells unary from binary:

```
let (a :: Vec2, b :: Vec2) -> + -> (r :: Vec2) { [a.x + b.x, a.y + b.y] -| }
let (a :: Vec2) -> - -> (r :: Vec2) { [-a.x, -a.y] -| }
```

- **Overloadable:** the built-ins `+ - * / % & | ^ << >> ~ ! ==`, and every extra operator.
- **Comparisons** are derived from `Ord.cmp`, which keeps chained comparisons consistent. Number comparisons are built in.
- **The logical operators** can't be overloaded, since an overload is a call that evaluates both sides.

**Lang protocols** (in the prelude):

| Protocol | Used by |
|---|---|
| `Ord[T]` (`cmp`) | `<` `<=` `>` `>=` |
| `Eq[T]` (`-> == ->`) | deep equality override, required for recursive types |
| `Add`, `Sub`, `Mul`, `Div`, `Rem`, `BitAnd`, `BitOr`, `BitXor`, `Shl`, `Shr`, `Neg`, `BitNot`, `Not` | operators, each defined as `-> op ->` |
| `Sized[T]` (`len`) | `xs.len()`, lengths in refinements, slices |
| `Default[T]` (`default`) | filled constructors, generic defaults |
| `Clone[T]` (`clone`) | deep copies |
| `Drop[T]` (`drop`) | custom cleanup at the end of a scope |
| `Iterable[T, E]` (`at(s, i) -> Option[E]`, `E` may be a view type) | `repeat (x) <- xs`, membership clauses |
| `Allocator[A]` (`alloc`, `release`) | `unscoped` allocation, handles, `chunk` |
| `Fn[F, [Args], R]` | closures and functions as values |

Every built-in method a type has comes from a protocol it follows: arrays have `len` because they follow `Sized`.

### 5.4 Generics

```
let [T :: Ord] (a :: T, b :: T) -> max -> (m :: T) { ... }
let [T, A :: Allocator & Default <- Global] () -> listNew -> (l :: List[T, A]) { ... }
type List[T, A :: Allocator <- Global] <- [...];
```

- **Generics are built on comptime.** A type is a value known while compiling, so a generic is an ordinary function or type the compiler evaluates with its comptime arguments (section 7).
- **Bounds** use `::`. Intersection bounds use `&` and are compile-time only.
- **Defaults** use `<-`.
- **Parameters** may be types or comptime values. A lone name in a generic argument is a type when it names one. A literal or an expression like `N * 2` is a value.
- **Explicit instantiation:** `alloc[T](...)`, `List[int]`.
- **Monomorphization:** every concrete use generates its own copy, one per combination of arguments and type-level witnesses.

### 5.5 Integers

An integer type is three facts, spelled in its name: width (8 to 128), signed or unsigned (`u`), and trapping or wrapping (`w`). `int` is `i64` and prints as `int`.

- **Integers of different types never mix.** `x + y` with an `i8` and an `i16` is `cherimoya requires both sides of '+' to have the same type, please convert one of them`, with notes on each side's type. Conversions are explicit.
- **Operators:** arithmetic and comparisons take any number, and bitwise operators take any integer. Prefix `-` takes a signed or wrapping integer. Negating a trapping unsigned value is a compile error that suggests the signed or wrapping type.
- **Trapping integers** trap on overflow unless it's proven not to happen: `trap at line 3: 'x * 2' overflowed i8`.
- **Wrapping integers** are two's complement.
- **Division:** dividing by zero traps for every integer, wrapping ones included. A signed minimum divided by -1 traps for a trapping type, and gives the minimum for `/` and zero for `%` for a wrapping one.
- **Shifts:** an amount outside 0 to the width minus one traps for a trapping type, and is taken modulo the width for a wrapping one. A right shift is arithmetic for signed types and logical for unsigned ones.

  > Open: whether a trapping left shift traps when it shifts set bits out of the top.

**Literals** take the type their context expects, and are `int` otherwise. The expected type flows from a binding's annotation, an assignment's place, an input, a function's output, the other side of an operator (whichever side the literal is on, with a chain taking its first typed operand's), a match's subject, and an annotation. A literal that doesn't fit is a compile error, for wrapping types too: `cherimoya requires 300 to fit in 'i8', please use a wider type or convert it explicitly`. A negated literal is checked whole, so `-128` fits in an `i8`. Literals are read with 128 bits of headroom.

> Open: whether a literal takes its type from a scope it's yielded to, as in `let x :: i8 <- { 5 -| };`.

**Conversions** between integers: into a trapping type, the value must survive the round trip and keep its sign, or it traps. Into a wrapping type, the low bits are kept, so `i8w(300)` is `44`, which is the explicit way to ask for modular arithmetic.

### 5.6 Floats

A float type is an encoding and whether it's NaN-free. The plain name is NaN-free and an `n` makes it NaN-carrying. `float` is `f64` and prints as `float`.

| Types | Encoding | Notes |
|---|---|---|
| `f8` = `f8e4m3`, `f8e4m3n` | OCP E4M3FN, bias 7 | no infinity, max finite 448. Overflow traps in `f8` and gives NaN in `f8e4m3n` |
| `f8e5m2`, `f8e5m2n` | OCP E5M2, bias 15 | IEEE-shaped, has infinity, max finite 57344 |
| `f16`, `f16n` | IEEE binary16 | |
| `f16e8m7`, `f16e8m7n` | bfloat16 | f32's exponent range |
| `f32` `f64` `f128` (and `n` forms) | IEEE binary32/64/128 | |

`f8e4m3n` is never shortened to `f8n`, which would hide its encoding. AMD's FNUZ FP8 variants are not supported. Saturating FP8 conversion is a stdlib intrinsic.

- **Floats of different types never mix,** and a NaN-free float and its NaN-carrying twin are different types. Arithmetic and comparisons take any float, and bitwise operators take none.
- **NaN-free floats** trap on any operation that would make a NaN (0/0, inf - inf, 0 * inf, square root of a negative): `trap at line 2: 'x / x' made a NaN, which float can't hold`. Infinities are numbers, so `1.0 / 0.0` is +inf. The check is one `fcmp uno` plus a never-taken branch, elided where Z3 proves it can't fire and absent in the unsafe tier. NaN-free floats get no fast-math flags, so the checks survive optimization. Negation flips the sign bit and isn't checked.
- **NaN-carrying floats** use IEEE arithmetic, and their deep equality and orderings use the total order: every NaN sits below every number (including -inf), all NaNs are equal, and `-0.0 == +0.0`. `a < b` is "a is NaN and b isn't, or a is less", `a <= b` is "a is NaN, or b isn't and a is at most b", and `a == b` is "both are NaN, or they're equal". Where Z3 proves an operand non-NaN, the comparison is a plain `fcmp`.
- **Shallow equality** compares bits for every float.

**Literals** take the expected float type, and are `float` otherwise. A value past the type's largest finite value is an error: `cherimoya requires 65536.0 to fit in 'f16', please use a wider type`. An integer literal stands for a float when the float holds it exactly: `cherimoya requires 2049 to be exact in 'f16', please write it as a float literal or use a wider type`. When the two sides of an operator are literals of different kinds, the float side decides, so `1 + 2.5` is a `float`. Constants are built in their type's own encoding, rounding to nearest even.

**Conversions:**

| From → to | Rule |
|---|---|
| integer → float | round to nearest even, past the range to infinity. Never fails |
| float → integer | truncate toward zero. A trapping target traps on NaN or out of range, and a wrapping target saturates, with NaN becoming 0, so `i8w(1000.0)` is 127 |
| float → float | round to nearest even. NaN-carrying into NaN-free traps on NaN: `'f64(x / x)' is a NaN, which float can't hold`. NaN-free into NaN-carrying is free |

**FP8** values are stored as bytes, widened to `f32` for arithmetic and comparison, and narrowed back with round-to-nearest-even, which rounds exactly as FP8 arithmetic would. Narrowing takes a double, so a conversion from `f64` rounds once. Shallow equality compares the bytes, and negation flips the top bit.

### 5.7 Characters

| Type | Holds | Size |
|---|---|---|
| `char` | one ASCII character, 0 to 7F | 1 byte |
| `rune` | one Unicode scalar value: 0 to 10FFFF, minus the surrogates D800 to DFFF | 4 bytes |

A `char` is always a whole character: a byte above 7F is part of a longer UTF-8 sequence, so it's never a `char`. A `char` and a `rune` never mix without a conversion.

- **Literals** adapt: a rune where a rune is expected, a char where a char is expected, and with no context a char unless the value isn't ASCII, then a rune. So `'a'` is a char and `'é'` a rune. A non-ASCII literal in a char context is `cherimoya requires 'é' to fit in 'char', which is ASCII, please use a 'rune'`. A literal holds exactly one character, so `''` and `'ab'` are errors.
- **Comparisons:** `= != < <= > >=`, by code point, and `==` and `!==` agree with `=` and `!=`.
- **No arithmetic.** Stepping is a conversion: `char(int('a') + 1)`.
- **Conversions:** to and from any integer, and between `char` and `rune`, never to or from a float or a bool. A character converts as the unsigned integer holding its code point (`i8u` for a char, `i32u` for a rune), and a conversion into a character always checks, even from a wrapping integer: a char traps above 7F, and a rune traps above 10FFFF or on a surrogate: `trap at line 4: 'char(n)' doesn't fit in char`.

### 5.8 Strings

Text is UTF-8, so a string is an array of `i8u` bytes, not of `char`s, since a `char` is ASCII only. String literals hold the same escapes as character literals and are checked the same way. The stdlib provides `string` (text with string functions) and `cstring` (null-terminated, for C interop).

> Open: the exact type of a string literal (a read-only `arr[N] i8u`, a `peek arr i8u`, or `string`), and how `string` and `cstring` are defined.

> Open: how strings concatenate. `+` on arrays is elementwise in stdcheri (8), which would make `"Fizz" + "Buzz"` elementwise addition rather than concatenation, and the concatenation operator's spelling is chosen with array math.

### 5.9 `boolf`

A `boolf` is a fuzzy truth value from 0 to 1 in 256 even steps, held in one byte, where the byte k stands for k / 255.

- **Literals:** a number literal from 0 to 1, and `true` and `false` (1 and 0), become a `boolf` where one is expected, rounding to the nearest step, halves going up, so `0.7` is 179/255. Anything outside 0 to 1 is `cherimoya requires 1.5 to be from 0 to 1 to be a 'boolf', please use a value in that range`. Logical operators pass their expected type into their operands, so `let x :: boolf <- 0.2 && 0.9;` makes both literals `boolf`s.

| Operator | Means |
|---|---|
| `a && b` | the minimum |
| `a \|\| b` | the maximum |
| `!a` | 1 - a |
| `a ^^ b` | `max(min(a, !b), min(!a, b))`, how much exactly one side holds |
| `a * b` | the product, rounded to the nearest step, `(a·b + 127) / 255` |

- `&&` and `||` short-circuit at 0 and 1, the way they do on bools. `*` is the only arithmetic a `boolf` takes, since `+` and `-` can leave 0 to 1.
- **Comparisons** work like numbers' and give a `bool`.
- **Compounds:** `&&<-`, `||<-`, and `^^<-`, on bools and `boolf`s alike.
- **Conversions:** to and from any float (`boolf(t)` rounds to the nearest step and traps on a value outside 0 to 1 or NaN: `trap at line 4: 'boolf(t)' doesn't fit in boolf`), to and from the raw byte with `i8u` and `i8uw` (never trapping), and from a `bool` with `boolf(b)`.
- **The cutoff.** A `boolf` never becomes a `bool` by a conversion and is never a condition. Turning 0.7 into a yes or a no is a choice, so the program writes it: `x > 0.5`, or whatever cutoff it means. A `boolf` can still be a match subject, with literal patterns and a required catch-all.

### 5.10 Units

Units are distinct subtypes of number types: `ms` of `float`, `radian` of `float`. Values of different units don't mix by accident, and stdcheri carries the ISO and SI conversions, so the compiler converts between units, and to SI, automatically.

> Open: how a unit is declared (a refinement, a newtype, or a new form) and how its conversions are stated in stdcheri.

---

## 6. Functions

### 6.1 Signatures

```
let [generics] (inputs) -> name -> (outputs) wit {obligations} { body }
```

- **Inputs** are `(name :: Type {refinement} <- default, ...)`, or `()` for none. Every input has a type.
- **Outputs** are named, `(name :: Type {refinement}, ...)`, or `()` for void. The names exist for contracts and documentation, and values are produced by yields. One output gives the function that output's type. A function's type is written `(int, int) : bool`.
- **Multiple outputs** are yielded as a pack: `[q, r] -|`.
- **Qualifiers on output types** decide placement for what flows into them: `(e :: Option[unscoped ptr Expr])`.
- **The name slot** holds an identifier or any overloadable operator (5.3).
- **The optional `wit { }`** holds function-level obligations, and later termination measures.
- **The body** is the function's own scope. Its final yield is the result, and early exits are `-| name`.
- **Protocol members** have neither a body nor `;`, and **externs** have `;` instead of a body.

**Defaults.** An input's default comes after its type, `(lo :: int <- 0)`, and an input with one can be left out of a call wherever it is in the signature. Leaving out one that isn't last means naming the ones after it: with `(lo :: int <- 0, x :: int)`, `f(x <- 5)` uses `lo`'s default. A default runs when the call leaves its input out, sees the inputs before it (`(a :: int, b :: int <- a * 2)`), and sees nothing a yield could target. It compiles to a function of its own, so a default that calls its own function is ordinary recursion. A default on an output is `cherimoya requires defaults to be on a function's inputs, please remove '<- 1'`.

### 6.2 Inputs and the body

- Inputs are declared in the function's own scope, each refinement checked right after its input. Outputs get an inner scope that only output refinements and `wit` obligations see.
- Ownership of inputs and outputs is in 4.6.
- A `mut` input can be assigned in the body: `n :: mut int`.

### 6.3 Order and nesting

A list of statements is checked in passes:

1. Each scope folds its constant `let`s, so an array size anywhere in the scope can use one, even one declared after it.
2. Every function's signature is declared, so functions can call each other in any order, including mutual recursion.
3. Every other statement is checked in order, so a `let` is only visible after it.
4. Every function's body is checked, so a body sees every name declared around it.

Functions defined inside bodies follow the same passes within their block and can't capture (2.12). Globals and functions are visible everywhere.

### 6.4 Programs and the top level

A `.moya` file is checked as a module, or as a program when it's built. A program defines its entry at the top of the file:

```
let () -> start -> (code :: int) { 0 -| }
```

Its result is the exit code. It's called `start` so it never shares a name with the C `main` the runtime defines. A program without one is `cherimoya requires a program to have a 'start' function, please add 'let () -> start -> (code :: int) { 0 -| }'`, with a fix. A `start` nested in a body or block doesn't count.

Bindings at the top of a file are globals. Everything at the top that isn't a function runs once, in order, before `start`, and top-level handles are freed after it. An immutable top-level binding whose initializer folds to a constant needs no code at run time.

### 6.5 Externs

```
@extern("c") let (fd :: i32, buf :: arr i8u, n :: i64u) -> write -> (r :: i64);
```

---

## 7. Comptime

Cherimoya has no macros. Compile-time evaluation, in the manner of Zig, does what macros are for:

- **Types are values** known while compiling, so generics are ordinary functions the compiler runs (5.4).
- **Embedded languages** are checked by functions that take a string and parse it while compiling. `regex("a+b")` checks its pattern and reports a mistake on the right line, with no new syntax and none of text replacement's pitfalls.
- **Generated code:** stdcheri generates JSON stringify and parse for a type at comptime.
- **Comptime `let`s** (3.2): an immutable binding whose initializer can be evaluated while compiling is comptime-eligible. Constant folding over integer literals through `+ - * / %` gives array sizes, and the full evaluator extends it to any pure code.

> Open: how a program asks for comptime evaluation explicitly (outside the contexts that demand it), and how the evaluator's limits are reported.

---

## 8. Modules and the standard library

- **One file is one module,** named by its path relative to the source root, with `.` separating directories.
- **The export list** goes at the top of the file, and everything unlisted is private:

  ```
  export [List, listNew, push, Result[dist, prev], <+>];
  ```

  Exporting a type doesn't export its fields. List the fields explicitly, as in `Result[dist, prev]`. An item can be an operator, which exports the overload.
- **Imports:**

  ```
  import [List, push] <- list;
  import [Allocator] <- std.mem;
  import [Raw] <- std.unsafe;
  ```
- **The prelude** (`core`) is imported everywhere. It holds `Option` and the lang protocols, `Global`, `max`, `min`, `succ`, `pred`, `zip` (a lazy lockstep sequence over two iterables, yielding `[peek A, peek B]` and stopping at the shorter one), and `enumerate` (a lazy sequence yielding `[i, peek E]`).

**stdcheri** is the standard library:

| Module or area | Holds |
|---|---|
| `std.float` | `isNan`, square roots and the other float functions, and the IEEE comparison intrinsics |
| `std.unsafe` | `Raw[T]`, bit reinterpretation, pointer/address conversions |
| `std.mem` | allocators |
| collections | `List`, `Map` |
| text | `string`, `cstring` |
| array math | below |
| big integers | arbitrary-precision integers |
| units | ISO and SI units and their conversions (5.10) |
| JSON | stringify and parse, generated at comptime |

**Array math** is library code built on overloading, protocols, and generics, not built in:

- `+ - * /` on same-shape arrays are elementwise, with scalars broadcast (`xs * 2`, `2 * xs`), nested arrays recursing, and each element trapping like a scalar.
- An operation shaped `T × T → T` (where `T` can itself be an array: matrix add and multiply, concatenation, cross product) or `coll[T] × coll[T] → T` (like dot product) is an operator. Everything else (transpose, norms, reductions, reshaping) is a named function.
- Also provided: elementwise comparisons giving `arr[N] bool`, norms (length, distance, normalize), reshaping (reverse, rotate, flatten, fill), and reductions (sum, product, min, max, any, all).

> Open: the spellings of the array-math operators and functions.

---

## 9. Attributes

An attribute is `@name(args)` followed by what it applies to, and attributes stack, since the target is the next statement. Arguments can be named, as in calls.

| Attribute | Applies to | Meaning |
|---|---|---|
| `@tier(strict)` | a file (with `;`) or a block | safety tier (3.12) |
| `@extern("c")` | a function without a body | foreign declaration (11) |
| `@globalAllocator` | an allocator | replaces `Global`'s implementation (4.9) |
| `@test(traps <- "...", timeout <- 5)` | a function in a `.toya` file | a test (12) |

---

## 10. Verification

- **Engine:** Z3 through its C API, which proves the weakest preconditions of refinements, `wit`, `assert`, invariants, and entry values over the CFG. UNSAT means proven. SAT means a counterexample, which maps back to source as the diagnostic, for example "violated when `n = -1`."
- **Integer encoding:** trapping integers become Z3 Ints with range constraints, and wrapping integers become bitvectors. Conversions between the two theories are a known Z3 performance cliff, so kernel code may need a bitvectors-everywhere mode.
- **Float encoding:** Z3's floating-point theory, with `!isNaN` as a standing fact for NaN-free types. E4M3FN reuses IEEE infinity patterns as finite values, so it is modeled as a wider float with a range constraint, or at the bitvector level. E5M2 maps directly.
- **Quantifiers:** folds over `&&` and `||`.
- **Safety tiers** decide what unproven obligations become: compile errors in `strict`, run-time checks in `default`, run-time checks plus bitmap initialization tracking in `debug`, and nothing in `unsafe`. Tiers apply per project, file, or braced region.
- **Contracts optimize.** Bounds, overflow, narrowing, division, NaN, and initialization checks are removed wherever they're proven, so a contract makes code faster as well as safer.
- **Expectation:** strict-tier algorithmic code needs real invariants. The default tier turns those obligations into run-time checks, by design.
- **Fuzzing:** a test with inputs is fuzzed against its contracts, and Z3's counterexamples become test cases (12).

---

## 11. FFI

- **`@extern("c")`** declares a foreign function. The FFI is universal: C is the first target, not the only one.
- **ABI mapping:**
  - Handles lower to their data pointer at C boundaries.
  - `Option[ptr T]` has the ABI of a nullable C pointer.
  - `Raw[T]` has the ABI of a C pointer.
  - `ptr void` is an untyped pointer for foreign code.
  - An `unscoped` handle passed to an extern input declared `unscoped` is discharged.
- **Strings** cross as `cstring` (5.8).

---

## 12. Tests

Tests go in `.toya` files, so they never ship in a program. A `.toya` file holds tests and the helpers they share, and has no `start`, since `cheri test` runs its tests instead: `cherimoya requires a .toya file to have no 'start', since cheri test runs its tests instead, please rename it`. `@test` anywhere else is `cherimoya requires tests to be in a .toya file, please move it to one`. A `.toya` file with no tests gets a warning. Once modules exist, a `.toya` file imports the code it tests.

A test is a function marked `@test`:

```
@test(traps <- "overflowed int", timeout <- 5)
let () -> overflows -> () {
    _ <- square(4294967296);
}
```

- **`traps`** (optional) is text the trap's message must hold, for a test that passes by trapping.
- **`timeout`** (optional) is a whole number of seconds above 0, after which the test fails.
- Anything else is an error: an unnamed argument, another name, the same name twice, or a value of the wrong kind.
- **A test with no inputs** runs once. **A test with inputs** is fuzzed against its contracts, with Z3 counterexamples as cases.
- **Nested tests** are named the way a debugger names them, `outer.inner`. Tests are reported in the order they're written.

**Running.** `cheri test file.toya` runs one file, `cheri test dir/` every `.toya` file under a directory in path order, and a name after the path picks the tests whose names hold it, or exactly that one with `--exact`, and `--runner` runs each test binary under a command, like valgrind or qemu. Each test runs in its own process, as many at once as there are cores, so a trap, a crash, or a hang only fails itself:

- **passes** by finishing, or, with `traps`, by trapping with that text
- **fails** by trapping or crashing, or, with `traps`, by finishing or trapping for another reason
- **times out** past its `timeout`, and is killed

```
tests/test/outcomes.toya
  ok       addsNegatives
  ok       overflows (trapped as expected)
  FAIL     wrong
           trap at line 17: assert (add(2, 2) = 5);
  TIMEOUT  stuck (after 1s)

2 passed, 2 failed
```

`cheri test` exits with 1 when any test failed or any file didn't build. `cheri build` and `cheri run` refuse a `.toya` file, and `cheri check` checks one as tests.

---

## 13. Diagnostics

Every diagnostic uses one of three forms:

| Form | For | Example |
|---|---|---|
| `cherimoya expected <what> <where>, got <other> instead` | the wrong thing in a place, naming the construct's shape where it helps | `cherimoya expected ')' after the arguments in 'f(a, b)', got ';' instead` |
| `cherimoya uses <x> for <y> instead of <z> as follows: <example>` | habits from other languages (`if`, tuples in parentheses, map arrows, nested functions capturing) | `cherimoya uses closures to capture 'n' instead of nested functions as follows: let f <- [n] (...) -> { ... };` |
| `cherimoya requires <rule>, please <fix>` | broken rules | `cherimoya requires whitespace between the operators '**' and '-', please write '** -'` |

**Severity.** A diagnostic is an error if and only if the problem is, or can cause, real harm: a leak, an unhandled error, a segfault, an out-of-bounds access, a value used before it's set, an ownership violation, a dropped value, an incomplete match. It's a warning if and only if it's just bad code that runs correctly: unreachable statements and arms, unused bindings, shadowing, a `.toya` file without tests. Warnings never stop a build.

**Structure.** A diagnostic has a primary span, notes (each a message and span, such as "declared here" or "moved here"), and fixes. A fix is a title and a list of edits, built by the stage that reports the mistake, from the same spans. A diagnostic whose fix would be a guess has none, and one with a real choice has several (the two groupings of a mixed-operator error). Diagnostics render with caret underlines:

```
error at line 1:
let x <- 1 + 2.5;
         ^^^^^^^
cherimoya requires both sides of '+' to have the same type, please convert one of them
note at line 1:
let x <- 1 + 2.5;
         ^
this side is 'int'
```

**Recovery.** Every stage keeps going after an error, so one run reports every independent mistake, and one mistake is one diagnostic: the error type poisons silently, recovery never consumes `;`, `)`, `]`, or `}`, and a failed binding is still declared.

**Traps** are the run-time half: `trap at line N: <what went wrong in the program's own words>`, then the process aborts.

---

## 14. Toolchain

### 14.1 Commands

| Command | Does |
|---|---|
| `cheri build <file>` | checks, optimizes, compiles, and links an executable |
| `cheri run <file>` | builds into a temporary directory, runs, and exits the way the program did (128 plus the signal when killed) |
| `cheri check <file>` | the front end only. `--fix` applies every preferred fix, re-checking until nothing is left |
| `cheri test <path> [name]` | runs tests (12) |
| `cheri lsp` | the language server |
| `cheri fmt <file>` | canonical formatting to stdout, `-w` in place |
| `cheri help` | every option |

**Build options:** `--emit` (object, assembly, IR, bitcode) with `-c`, `-S`, and `--emit-llvm`; `-O0` to `-O3`, `-Os`, `-Oz` (default `-O3`); `--target`, `--cpu` (`native`), `--features`; `--linker`, `--static`, `-L`, `-l`, `--link-arg`; `-g` and `-gline-tables-only`.

**`cheri fmt`** keeps comments, aligns columns gofmt-style, uses 4-space indentation, and has no configuration. It refuses to format a file with errors, since it can't know which reading of a spacing violation was meant.

**Planned tools:** `cheri doc` (documentation from doc comments), `cheri bench` (benchmarks, including against other languages), a REPL with a JIT (after modules), hot reload, and custard, the package manager.

### 14.2 Debug info

| Flag | A debugger gets |
|---|---|
| `-g` | every function by its source name, the line of every instruction, every binding and input with its type, and every scope |
| `-gline-tables-only` | function names and lines, for breakpoints and backtraces |

Either works at any optimization level. Names are the source's: `clamp`, `outer.inner`, `clamp.lo<-`, and `init` (artificial). Types are described under their cherimoya names, immutable bindings are `const`, and a `boolf` is fixed-point scaled by 1/255. DWARF is version 5, and an MSVC target gets CodeView. On ELF, a `-g` binary embeds gdb pretty-printers that decode FP8, bfloat16, and `f128` exactly, print 1-byte integers as numbers, print a `rune` as `955 'λ'`, and show arrays by their elements, `arr int of 3 = {1, 2, 3}`.

### 14.3 Editors

The language server gives diagnostics with notes and quick fixes, semantic highlighting (immutable names marked readonly), hover, definition, references, rename (refusing clashes and changes of meaning), an outline, completion ranked by the expected type, signature help, inlay hints (inferred types, input names, pipe steps, the names of long scopes), documentation from comments, and a Run test lens on every test. A name's documentation is the run of comments directly above its declaration, or the comment at the end of its line, rendered as markdown. Clients exist for VS Code, Emacs (eglot and lsp-mode), and Zed, and a tree-sitter grammar reads programs exactly as `cheri` does.

---

## 15. Compiler architecture

- **Arena ownership for interned objects.** `TypeContext` owns every `SemaType` in a `vector<unique_ptr<SemaType>>`, and everything outside refers to types by raw pointer. Each distinct type exists once, so type equality is pointer equality.
- **`SemaType` has no `Span`.** Source locations live on AST nodes.
- **Generic instantiations** are interned by head, arguments, and type-level witnesses, so witness identity is still pointer equality.
- **AST annotations are direct:** every `Expr` gets `SemaType* type`, identifiers, declarations, inputs, and binders get their resolved `Symbol*`, and later passes read the answers instead of recomputing them.
- **Symbols** live in a `std::deque`, so a `Symbol*` stays valid after its lexical scope is gone. Each records one mutability per level.
- **Lexical scopes** are sema's name-resolution scopes, separate from the grammar's scopes.
- **The yield-target stack** holds, per entry, a kind (function, closure, loop, match, arm, block, chunk), an optional label, the loop's binder names, and the expected yield type. Every `-|` resolves against it, and codegen mirrors it.
- **Error poisoning:** the error type fits anywhere and propagates silently.
- **Codegen:** locals are entry-block allocas promoted by mem2reg. Every run-time check goes through one trap helper.
- **Diagnostics** are structured (13). For the ownership passes, diagnostics are the product: plan about 30% of the effort for them.
- **Testing** has three tiers: unit tests in C++ (`tests/unit/`), runtime basics that only a whole program can show (`tests/run/`, `tests/test/`), and feature tests in `.toya` files (`tests/features/`), which run at `-O0` and `-O3` only once the first two tiers pass.

---

## 16. Reference examples

```
type Percent <- int {p : 0 <= p <= 100};
type Shape   <- Circle[f64] | Rect[f64, f64];
type Vec2    <- [x :: f64, y :: f64];

let (a :: Vec2, b :: Vec2) -> + -> (r :: Vec2) { [a.x + b.x, a.y + b.y] -| }
let (a :: Vec2, b :: Vec2) -> <+> -> (r :: Vec2) { a + b -| }

let (n :: int) -> clamp -> (p :: Percent) {
    match (n) {
        [..., <0]     -> { 0 -| }
        [0, ..., 100] -> { n -| }        # Z3 proves 0 <= n <= 100: no cast needed
        [>100, ...]   -> { 100 -| }
    } -|
}

let (s :: Shape) -> area -> (a :: f64) {
    match (s) {
        Circle[r]  -> { 3.14159 * r * r -| }
        Rect[w, h] -> { w * h -| }
    } -|
}

let (a :: int {a >= 0}, b :: int {b > 0})
    -> divmod
    -> (q :: int, r :: int {0 <= r < b && a = q * b + r}) {
    [a / b, a % b] -|
}

# counts the pairs of non-negative elements that add up to target, stopping at 1000
let (xs :: arr int, target :: int) -> countPairs -> (c :: i64u) {
    let count :: mut i64u <- 0;
    let n <- xs.len();
    repeat outer (i) <- [0, ..., <n] {
        match { (xs[i] < 0) -> { -| i } }                     # continue outer
        repeat (j) <- [>i, ..., <n] {
            match { (xs[j] < 0) -> { -| j } }                 # continue inner
            match { (xs[i] + xs[j] = target) -> { count +<- 1; } }
            match { (count = 1000) -> { -| outer } }          # break outer
        }
    }
    count -|
}

let (x :: bool, y :: bool) -> describe -> (code :: int) {
    match ([x, y]) {
        [true,  true ] -> { 3 -| }
        [true,  false] -> { 2 -| }
        [false, _    ] -> { 0 -| }
    } -|
}

# 1 for multiples of 3, plus 2 for multiples of 5
let (n :: int) -> fizzBuzzCode -> (c :: int) {
    match (+) {
        (n % 3 = 0) -> { 1 -| }
        (n % 5 = 0) -> { 2 -| }
        _           -> { 0 -| }
    } -|
}

# takes the array over and hands it back with every element doubled
let (xs :: unscoped arr mut int) -> doubled -> (r :: arr mut int) {
    repeat (i) <- [0, ..., <xs.len()] { xs[i] *<- 2; }
    xs -|
}

let (x :: int) -> inc -> (r :: int) { x + 1 -| }
let (x :: int) -> double -> (r :: int) { x * 2 -| }
let (x :: int, lo :: int <- 0, hi :: int <- 100) -> bound -> (r :: int) {
    match { (x < lo) -> { lo -| bound } (x > hi) -> { hi -| bound } }
    x -|
}

let () -> start -> (code :: int) {
    let rate    <- 2;
    let xs      <- [1, 2, 3, 4];
    let ys      <- [10, 20, 30];
    let total   <- (+, 0)[x * x : x <- [1, ..., 10] && x % 2 = 0];
    let pairs   <- countPairs(arr int([3, -1, 4, 1, 5]), 5);
    let [q, r]  <- divmod(17, 5);
    let v       <- Vec2 <- [x <- 1.0, y <- 2.0] <+> Vec2 <- [3.0, 4.0];
    let folded  <- ((acc, x) -> { acc + x * rate -| }, 0)[[1, 2, 3]];
    let sums    <- (+, 0)[x + y : [x, y] <- zip(xs, ys)];
    let product <- (+, 0)[1 : x <- xs && y <- ys];
    let mixed   <- (+, 0)[match (i % 2) { 0 -> { x * 2 -| } _ -> { x -| } } : [i, x] <- enumerate(xs)];
    let piped   <- 3 |> inc |> double;
    let back    <- double <| inc <| 2 + 3;
    let capped  <- 250 |> bound(hi <- 200);
    let heap    :: mut <- arr mut int(3, 7);
    heap <- doubled(heap);
    assert (total = 220);
    assert (q = 3 && r = 2);
    assert (folded = 12);
    assert (sums = 66);
    assert (product = 12);
    assert (mixed = 14);
    assert (piped = 8 && back = 12);
    assert (capped = 200 && fizzBuzzCode(15) = 3);
    assert (heap[0] = 14 && heap.len() = 3);
    match { (pairs = 1 && v.x = 4.0) -> { 0 -| } _ -> { 1 -| } } -|
}
```
