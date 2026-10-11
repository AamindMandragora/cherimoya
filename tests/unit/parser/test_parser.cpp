#include <catch2/catch_test_macros.hpp>

#include <string>
#include <memory>

#include "parser.hpp"

// converts an expression to text
static std::string dump(const Expr& e) {
    switch (e.kind) {
        case ExprKind::IntLiteral:
            return std::string(static_cast<const IntLiteralExpr&>(e).literal);
        case ExprKind::FloatLiteral:
            return std::string(static_cast<const FloatLiteralExpr&>(e).literal);
        case ExprKind::BoolLiteral:
            return static_cast<const BoolLiteralExpr&>(e).literal ? "true" : "false";
        case ExprKind::StringLiteral:
            return std::string(static_cast<const StringLiteralExpr&>(e).literal);
        case ExprKind::CharLiteral:
            return std::string(static_cast<const CharLiteralExpr&>(e).literal);
        case ExprKind::Identifier:
            return std::string(static_cast<const IdentifierExpr&>(e).name);
        case ExprKind::Wildcard:
            return "_";
        case ExprKind::Unary: {
            auto& u = static_cast<const UnaryExpr&>(e);
            return "(pre:" + std::string(u.op.text) + " " + dump(*u.operand) + ")";
        }
        case ExprKind::Binary: {
            auto& b = static_cast<const BinaryExpr&>(e);
            return "(" + std::string(b.op.text) + " " + dump(*b.lhs) + " " + dump(*b.rhs) + ")";
        }
        case ExprKind::ExtraBinary: {
            auto& b = static_cast<const ExtraBinaryExpr&>(e);
            return "(" + std::string(b.op.text) + " " + dump(*b.lhs) + " " + dump(*b.rhs) + ")";
        }
        case ExprKind::Chain: {
            auto& c = static_cast<const ChainExpr&>(e);
            std::string s = "(chain " + dump(*c.operands[0]);
            for (size_t i = 0; i < c.ops.size(); i++) s += " " + std::string(c.ops[i].text) + " " + dump(*c.operands[i + 1]);
            return s + ")";
        }
        case ExprKind::Pipe: {
            auto& p = static_cast<const PipeExpr&>(e);
            return "(" + std::string(p.op.text) + " " + dump(*p.lhs) + " " + dump(*p.rhs) + ")";
        }
        case ExprKind::OpRef:
            return "(op " + std::string(static_cast<const OpRefExpr&>(e).op.text) + ")";
        case ExprKind::Old:
            return "(old " + dump(*static_cast<const OldExpr&>(e).operand) + ")";
        case ExprKind::Call: {
            auto& c = static_cast<const CallExpr&>(e);
            std::string s = "(call " + dump(*c.callee);
            for (auto& a : c.args) s += " " + dump(*a);
            return s + ")";
        }
        case ExprKind::Index: {
            auto& i = static_cast<const IndexExpr&>(e);
            std::string s = "(index " + dump(*i.object);
            for (auto& a : i.args) s += " " + dump(*a);
            return s + ")";
        }
        case ExprKind::Member: {
            auto& m = static_cast<const MemberExpr&>(e);
            return "(. " + dump(*m.object) + " " + std::string(m.field) + ")";
        }
        case ExprKind::Construct: {
            auto& c = static_cast<const ConstructExpr&>(e);
            return "(<- " + dump(*c.target) + " " + dump(*c.pack) + ")";
        }
        case ExprKind::Pack: {
            auto& p = static_cast<const PackExpr&>(e);
            std::string s = "(pack";
            for (auto& el : p.elems) {
                s += " ";
                if (el.name) s += std::string(el.name->text) + "<-";
                s += dump(*el.value);
            }
            return s + ")";
        }
        case ExprKind::Range: {
            auto& r = static_cast<const RangeExpr&>(e);
            std::string s = "(range";
            for (auto& el : r.elems) {
                s += " ";
                if (!el.value) { s += "..."; continue; }
                if (el.marker) s += std::string(el.marker->text);
                s += dump(*el.value);
            }
            return s + ")";
        }
        case ExprKind::Compr: {
            auto& c = static_cast<const ComprExpr&>(e);
            std::string s = "(compr " + dump(*c.head);
            for (auto& cl : c.clauses) {
                if (cl.pattern) s += " (in " + dump(*cl.pattern) + " " + dump(*cl.expr) + ")";
                else s += " " + dump(*cl.expr);
            }
            return s + ")";
        }
        case ExprKind::Fold: {
            auto& f = static_cast<const FoldExpr&>(e);
            return "(fold " + dump(*f.reducer) + " " + dump(*f.seed) + " " + dump(*f.sequence) + ")";
        }
        case ExprKind::Error:
            return "<error>";
    }
    return "<unknown>";
}

// lexes and parses one input as an expression, owning the source and tokens the tree points into
struct Parsing {
    SourceManager sm;
    DiagnosticEngine diag{sm};
    std::vector<Token> tokens;
    std::unique_ptr<Expr> expr;
    explicit Parsing(const std::string& input) {
        sm.loadString(input);
        tokens = Lexer(diag).lex();
        expr = Parser(tokens, diag).parseExpr();
    }
};

// parses input and returns text representation
static std::string parseDump(const std::string& input) {
    return dump(*Parsing(input).expr);
}

// parses input and returns the outer expression's kind
static ExprKind parseKind(const std::string& input) {
    return Parsing(input).expr->kind;
}

// parses input and returns how many errors lexing and parsing reported
static size_t parseErrors(const std::string& input) {
    return Parsing(input).diag.errorCount();
}

// checks if literals and identifiers parse correctly
TEST_CASE("Parser: literals and identifiers", "[parser]") {
    CHECK(parseDump("42") == "42");
    CHECK(parseDump("0xFF") == "0xFF");
    CHECK(parseDump("0o17") == "0o17");
    CHECK(parseDump("0b1010") == "0b1010");
    CHECK(parseDump("3.14") == "3.14");
    CHECK(parseDump("0b101.11") == "0b101.11");
    CHECK(parseDump("0xdead.beef") == "0xdead.beef");
    CHECK(parseDump("true") == "true");
    CHECK(parseDump("false") == "false");
    CHECK(parseDump("\"hello\"") == "\"hello\"");
    CHECK(parseDump("'a'") == "'a'");
    CHECK(parseDump("foo") == "foo");
    CHECK(parseDump("_") == "_");
    CHECK(parseKind("\"hello\"") == ExprKind::StringLiteral);
    CHECK(parseKind("'a'") == ExprKind::CharLiteral);
    CHECK(parseKind("_") == ExprKind::Wildcard);
}

// checks if binding orders follow the levels in the operator table
TEST_CASE("Parser: binding orders", "[parser]") {
    CHECK(parseDump("a + b * c") == "(+ a (* b c))");
    CHECK(parseDump("a * b + c") == "(+ (* a b) c)");
    CHECK(parseDump("a + b = c") == "(= (+ a b) c)");
    CHECK(parseDump("a < b = c > d") == "(= (chain a < b) (chain c > d))");
    CHECK(parseDump("a = b && c != d") == "(&& (= a b) (!= c d))");
    CHECK(parseDump("a == b && c !== d") == "(&& (== a b) (!== c d))");
    CHECK(parseDump("a && b || c") == "(|| (&& a b) c)");
    CHECK(parseDump("a && b ^^ c") == "(^^ (&& a b) c)");
    CHECK(parseDump("a << b + c") == "(<< a (+ b c))");
    CHECK(parseDump("a | b ^ c & d") == "(| a (^ b (& c d)))");
    CHECK(parseDump("-a ** b") == "(pre:- (** a b))");
    CHECK(parseDump("a ** -b") == "(** a (pre:- b))");
    CHECK(parseDump("a * b ** c") == "(* a (** b c))");
}

// checks if associativity is as expected
TEST_CASE("Parser: associativity", "[parser]") {
    CHECK(parseDump("a - b - c") == "(- (- a b) c)");
    CHECK(parseDump("a * b / c") == "(/ (* a b) c)");
    CHECK(parseDump("a ** b ** c") == "(** a (** b c))");
    CHECK(parseDump("a ** b *** c") == "(** a (*** b c))");
    CHECK(parseDump("a =<< b =<< c") == "(=<< a (=<< b c))");
    CHECK(parseDump("a >>= b >>= c") == "(>>= (>>= a b) c)");
    CHECK(parseDump("a * b <*> c") == "(<*> (* a b) c)");
}

// checks prefix operators and old
TEST_CASE("Parser: prefix operators and old", "[parser]") {
    CHECK(parseDump("-a") == "(pre:- a)");
    CHECK(parseDump("!a") == "(pre:! a)");
    CHECK(parseDump("~a") == "(pre:~ a)");
    CHECK(parseDump("! !a") == "(pre:! (pre:! a))");
    CHECK(parseDump("-!a") == "(pre:- (pre:! a))");
    CHECK(parseDump("-a.b") == "(pre:- (. a b))");
    CHECK(parseDump("!a = b") == "(= (pre:! a) b)");
    CHECK(parseDump("old g") == "(old g)");
    CHECK(parseDump("old p.x") == "(old (. p x))");
    CHECK(parseDump("old a + b") == "(+ (old a) b)");
    CHECK(parseDump("node_count(g) = node_count(old g)") == "(= (call node_count g) (call node_count (old g)))");
    CHECK(parseKind("--a") == ExprKind::Error);
}

// checks that calls are postfix and chain onto their results
TEST_CASE("Parser: calls are postfix", "[parser]") {
    CHECK(parseDump("f()") == "(call f)");
    CHECK(parseDump("f(a)") == "(call f a)");
    CHECK(parseDump("f(a, b, c)") == "(call f a b c)");
    CHECK(parseDump("f(g(x))") == "(call f (call g x))");
    CHECK(parseDump("len(xs) - 1") == "(- (call len xs) 1)");
    CHECK(parseDump("-f(x)") == "(pre:- (call f x))");
    CHECK(parseDump("f(x)[0]") == "(index (call f x) 0)");
    CHECK(parseDump("slot(xs, i)[]") == "(index (call slot xs i))");
    CHECK(parseDump("get(l, i).name") == "(. (call get l i) name)");
    CHECK(parseDump("f(a)(b)") == "(call (call f a) b)");
    CHECK(parseDump("geo.cmp(a, b)") == "(call (. geo cmp) a b)");
}

// checks indexing, slicing, dereference, and generic arguments
TEST_CASE("Parser: indexing", "[parser]") {
    CHECK(parseDump("a[i]") == "(index a i)");
    CHECK(parseDump("a[i][j]") == "(index (index a i) j)");
    CHECK(parseDump("a[i + 1]") == "(index a (+ i 1))");
    CHECK(parseDump("p[]") == "(index p)");
    CHECK(parseDump("p[].x") == "(. (index p) x)");
    CHECK(parseDump("xs[a, ..., <b]") == "(index xs (range a ... <b))");
    CHECK(parseDump("Map[K, V]") == "(index Map K V)");
    CHECK(parseDump("alloc[T](n)") == "(call (index alloc T) n)");
    CHECK(parseDump("(x)[0]") == "(index x 0)");
}

// checks if member access parses correctly
TEST_CASE("Parser: member access", "[parser]") {
    CHECK(parseDump("a.b") == "(. a b)");
    CHECK(parseDump("a.b.c") == "(. (. a b) c)");
}

// checks construction from a pack at the postfix level
TEST_CASE("Parser: construction", "[parser]") {
    CHECK(parseDump("Point <- [1.0, 2.0]") == "(<- Point (pack 1.0 2.0))");
    CHECK(parseDump("Point <- [y <- 2.0, x <- 1.0]") == "(<- Point (pack y<-2.0 x<-1.0))");
    CHECK(parseDump("Some <- [x]") == "(<- Some (pack x))");
    CHECK(parseDump("Vec2 <- [1.0, 2.0] <+> Vec2 <- [3.0, 4.0]") == "(<+> (<- Vec2 (pack 1.0 2.0)) (<- Vec2 (pack 3.0 4.0)))");
    CHECK(parseDump("Vec2 <- [1.0, 2.0].x") == "(. (<- Vec2 (pack 1.0 2.0)) x)");
    CHECK(parseDump("Opt[T] <- [x]") == "(<- (index Opt T) (pack x))");
    CHECK(parseKind("Point <- [1.0, 2.0]") == ExprKind::Construct);
    CHECK(parseErrors("x <- 5") == 1);
}

// checks comparison chains and the mixing rule
TEST_CASE("Parser: chained comparisons and mixing", "[parser]") {
    CHECK(parseDump("a < b") == "(chain a < b)");
    CHECK(parseDump("a < b <= c") == "(chain a < b <= c)");
    CHECK(parseDump("0 <= x < n + 1") == "(chain 0 <= x < (+ n 1))");
    CHECK(parseKind("a < b <= c") == ExprKind::Chain);
    CHECK(parseDump("(a <=> b) <= 0") == "(chain (<=> a b) <= 0)");
    CHECK(parseErrors("a < b <= c") == 0);
    CHECK(parseErrors("(a <=> b) <= 0") == 0);
    CHECK(parseErrors("a <=> b <= 0") == 1);
    CHECK(parseErrors("a <= b <=> 0") == 1);
    CHECK(parseErrors("a = b = c") == 1);
    CHECK(parseErrors("a = b != c") == 1);
    CHECK(parseErrors("(a = b) != c") == 0);
    CHECK(parseErrors("a >>= b =<< c") == 1);
    CHECK(parseErrors("a =<< b >>= c") == 1);
    CHECK(parseErrors("a ** b *** c") == 0);
    CHECK(parseErrors("a + b <+> c") == 0);
}

// checks extra operators against the levels and associativity in the table
TEST_CASE("Parser: extra operators", "[parser]") {
    CHECK(parseKind("a <+> b") == ExprKind::ExtraBinary);
    CHECK(parseKind("a + b") == ExprKind::Binary);
    CHECK(parseDump("a <+> b <*> c") == "(<+> a (<*> b c))");
    CHECK(parseDump("a <*> b <+> c") == "(<+> (<*> a b) c)");
    CHECK(parseDump("a <+> b <+> c") == "(<+> (<+> a b) c)");
    CHECK(parseDump("a ++ b == c") == "(== (++ a b) c)");
    CHECK(parseDump("a <=> b") == "(<=> a b)");
    CHECK(parseDump("a |&| b ^ c") == "(^ (|&| a b) c)");
    CHECK(parseDump("a &&& b ||| c") == "(||| (&&& a b) c)");
    CHECK(parseDump("a <<< 2 + 1") == "(<<< a (+ 2 1))");
    CHECK(parseDump("x >>= f >>= g") == "(>>= (>>= x f) g)");
    CHECK(parseDump("a // b") == "(// a b)");
    CHECK(parseDump("a -- b") == "(-- a b)");
}

// checks both pipes and that they stay out of each other's way
TEST_CASE("Parser: pipes", "[parser]") {
    CHECK(parseDump("x |> f") == "(|> x f)");
    CHECK(parseDump("xs |> f |> g") == "(|> (|> xs f) g)");
    CHECK(parseDump("f <| x") == "(<| f x)");
    CHECK(parseDump("double <| inc <| 2 + 3") == "(<| double (<| inc (+ 2 3)))");
    CHECK(parseDump("a || b |> f") == "(|> (|| a b) f)");
    CHECK(parseDump("print <| format(a + b)") == "(<| print (call format (+ a b)))");
    CHECK(parseDump("f <| (x |> g)") == "(<| f (|> x g))");
    CHECK(parseKind("x |> f") == ExprKind::Pipe);
    CHECK(parseErrors("f <| (x |> g)") == 0);
    CHECK(parseErrors("h(x |> g) <| y") == 0);
}

// checks packs, named packs, and that :: and -> don't belong in them
TEST_CASE("Parser: packs", "[parser]") {
    CHECK(parseDump("[]") == "(pack)");
    CHECK(parseDump("[a, b, c]") == "(pack a b c)");
    CHECK(parseDump("[1, 2.0]") == "(pack 1 2.0)");
    CHECK(parseDump("[x <- 1.0, y <- 2.0]") == "(pack x<-1.0 y<-2.0)");
    CHECK(parseDump("[[1, 2], [3, 4]]") == "(pack (pack 1 2) (pack 3 4))");
    CHECK(parseDump("[a + b, f(c)]") == "(pack (+ a b) (call f c))");
    CHECK(parseDump("[x <- [1, 2]]") == "(pack x<-(pack 1 2))");
    CHECK(parseDump("pair(1, 2)[1]") == "(index (call pair 1 2) 1)");
    CHECK(parseKind("[a, b]") == ExprKind::Pack);
    CHECK(parseErrors("[x :: 1.0]") > 0);
}

// checks ranges and their endpoints, strides, and one-sided forms
TEST_CASE("Parser: ranges", "[parser]") {
    CHECK(parseDump("[0, ..., n]") == "(range 0 ... n)");
    CHECK(parseDump("[0, ..., <n]") == "(range 0 ... <n)");
    CHECK(parseDump("[>0, ..., n]") == "(range >0 ... n)");
    CHECK(parseDump("[>0, ..., <n]") == "(range >0 ... <n)");
    CHECK(parseDump("[0, 2, ..., <n]") == "(range 0 2 ... <n)");
    CHECK(parseDump("[n, n - 1, ..., >0]") == "(range n (- n 1) ... >0)");
    CHECK(parseDump("[0, ...]") == "(range 0 ...)");
    CHECK(parseDump("[..., <0]") == "(range ... <0)");
    CHECK(parseDump("[>100, ...]") == "(range >100 ...)");
    CHECK(parseDump("['a', ..., 'z']") == "(range 'a' ... 'z')");
    CHECK(parseDump("[0, ..., < -1]") == "(range 0 ... <(pre:- 1))");
    CHECK(parseKind("[0, ..., n]") == ExprKind::Range);
    CHECK(parseErrors("[0, ..., n]") == 0);
    CHECK(parseErrors("[...]") == 1);
    CHECK(parseErrors("[0, ..., 5, ...]") == 1);
    CHECK(parseErrors("[0, 1, 2, ..., n]") == 1);
    CHECK(parseErrors("[0, ..., n, m]") == 1);
    CHECK(parseErrors("[0, >1, ..., n]") == 1);
    CHECK(parseErrors("[n, n - 1, ..., >0]") == 0);
    CHECK(parseDump("[<n, n - 1, ..., >0]") == "(range <n (- n 1) ... >0)");
    CHECK(parseErrors("[<n, n - 1, ..., >0]") == 0);
    CHECK(parseErrors("[a, <b]") == 1);
}

// checks comprehensions, where <- after the colon starts a membership clause
TEST_CASE("Parser: comprehensions", "[parser]") {
    CHECK(parseDump("[x * x : x <- xs]") == "(compr (* x x) (in x xs))");
    CHECK(parseDump("[x * x : x <- xs && x % 2 = 0]") == "(compr (* x x) (in x xs) (= (% x 2) 0))");
    CHECK(parseDump("[x : x <- [1, ..., 10]]") == "(compr x (in x (range 1 ... 10)))");
    CHECK(parseDump("[[x, y] : x <- xs && y <- ys]") == "(compr (pack x y) (in x xs) (in y ys))");
    CHECK(parseDump("[x + y : [x, y] <- zip(xs, ys)]") == "(compr (+ x y) (in (pack x y) (call zip xs ys)))");
    CHECK(parseDump("[d : Some[d] <- xs]") == "(compr d (in (index Some d) xs))");
    CHECK(parseDump("[[xs[i], ys[i]] : 0 <= i < n]") == "(compr (pack (index xs i) (index ys i)) (chain 0 <= i < n))");
    CHECK(parseDump("[o : o <- orders && o.status = Paid]") == "(compr o (in o orders) (= (. o status) Paid))");
    CHECK(parseDump("[v : v <- [o : o <- os && f(o)]]") == "(compr v (in v (compr o (in o os) (call f o))))");
    CHECK(parseDump("[p : p <- ps && p = Point <- [0, 0]]") == "(compr p (in p ps) (in (= p Point) (pack 0 0)))");
    CHECK(parseDump("[p : p <- ps && f(Point <- [0, 0])]") == "(compr p (in p ps) (call f (<- Point (pack 0 0))))");
    CHECK(parseDump("[x : x <- xs && (a || b)]") == "(compr x (in x xs) (|| a b))");
    CHECK(parseKind("[x : x <- xs]") == ExprKind::Compr);
    CHECK(parseErrors("[x : x <- xs && a || b]") > 0);
}

// checks folds, including bare operator reducers and how the sequence is unwrapped
TEST_CASE("Parser: folds", "[parser]") {
    CHECK(parseDump("(+, 0)[x * x : x <- xs && x % 2 = 0]") == "(fold (op +) 0 (compr (* x x) (in x xs) (= (% x 2) 0)))");
    CHECK(parseDump("(*, 1)[1, ..., n]") == "(fold (op *) 1 (range 1 ... n))");
    CHECK(parseDump("(max, MIN_INT)[scores]") == "(fold max MIN_INT scores)");
    CHECK(parseDump("(&&, true)[x > 0 : x <- xs]") == "(fold (op &&) true (compr (chain x > 0) (in x xs)))");
    CHECK(parseDump("(||, false)[x < 0 : x <- xs]") == "(fold (op ||) false (compr (chain x < 0) (in x xs)))");
    CHECK(parseDump("(<+>, zero)[vs]") == "(fold (op <+>) zero vs)");
    CHECK(parseDump("(moments, [0.0, 0.0])[xs]") == "(fold moments (pack 0.0 0.0) xs)");
    CHECK(parseDump("(+, 0)[[1, 2, 3]]") == "(fold (op +) 0 (pack 1 2 3))");
    CHECK(parseDump("inc((+, 0)[xs])") == "(call inc (fold (op +) 0 xs))");
    CHECK(parseDump("(+, 0)[xs] / n") == "(/ (fold (op +) 0 xs) n)");
    CHECK(parseKind("(+, 0)[xs]") == ExprKind::Fold);
    CHECK(parseErrors("(+, 0, 1)[xs]") == 1);
}

// checks the prefix ( scan for everything that isn't a fold or a grouping
TEST_CASE("Parser: grouping and malformed parentheses", "[parser]") {
    CHECK(parseDump("(a + b) * c") == "(* (+ a b) c)");
    CHECK(parseDump("((a))") == "a");
    CHECK(parseErrors("(a + b) * c") == 0);
    CHECK(parseErrors("(a, b)") == 1);
    CHECK(parseErrors("()") == 1);
    CHECK(parseErrors("(+)") == 1);
    CHECK(parseErrors("(<+>)") == 1);
    CHECK(parseErrors("(x) -> { x -| }") == 1);
    CHECK(parseErrors("(") == 2);
    CHECK(parseErrors("(+") == 2);
}

// checks golden dumps of calls and pipes
TEST_CASE("Parser: golden calls and pipes", "[parser]") {
    CHECK(parseDump("3 |> inc |> double") == "(|> (|> 3 inc) double)");
    CHECK(parseDump("double <| inc <| 2 + 3") == "(<| double (<| inc (+ 2 3)))");
    CHECK(parseDump("xs |> filter_pos |> sum |> print") == "(|> (|> (|> xs filter_pos) sum) print)");
    CHECK(parseDump("print <| format(a + b)") == "(<| print (call format (+ a b)))");
    CHECK(parseDump("len(xs) - 1 |> f") == "(|> (- (call len xs) 1) f)");
    CHECK(parseDump("slot(xs, i)[] + get(l, i).name") == "(+ (index (call slot xs i)) (. (call get l i) name))");
    CHECK(parseDump("Ord.cmp(a, b) = 0") == "(= (call (. Ord cmp) a b) 0)");
    CHECK(parseDump("count_pairs([3, -1, 4, 1, 5], 5)") == "(call count_pairs (pack 3 (pre:- 1) 4 1 5) 5)");
}

// checks golden dumps of extra operators
TEST_CASE("Parser: golden extra operators", "[parser]") {
    CHECK(parseDump("Vec2 <- [x <- 1.0, y <- 2.0] <+> Vec2 <- [3.0, 4.0]") == "(<+> (<- Vec2 (pack x<-1.0 y<-2.0)) (<- Vec2 (pack 3.0 4.0)))");
    CHECK(parseDump("a ** b ** 2") == "(** a (** b 2))");
    CHECK(parseDump("(a <=> b) < 0 && c |+| d = e") == "(&& (chain (<=> a b) < 0) (= (|+| c d) e))");
    CHECK(parseDump("xs ++ ys ++ zs") == "(++ (++ xs ys) zs)");
    CHECK(parseDump("m >>= f >=> g") == "(>>= m (>=> f g))");
    CHECK(parseDump("p <$> q <|> r") == "(<$> p (<|> q r))");
    CHECK(parseDump("a <~ b") == "(<~ a b)");
    CHECK(parseDump("a < ~b") == "(chain a < (pre:~ b))");
}

// checks golden dumps of pairs and packs
TEST_CASE("Parser: golden pairs and packs", "[parser]") {
    CHECK(parseDump("[[\"ada\", 36], [\"alan\", 41]]") == "(pack (pack \"ada\" 36) (pack \"alan\" 41))");
    CHECK(parseDump("[acc[0] + 1.0, acc[1] + x, acc[2] + x * x]") == "(pack (+ (index acc 0) 1.0) (+ (index acc 1) x) (+ (index acc 2) (* x x)))");
    CHECK(parseDump("m[2] / m[0] - mean * mean") == "(- (/ (index m 2) (index m 0)) (* mean mean))");
    CHECK(parseDump("(+, 0)[1 : x <- xs && y <- ys]") == "(fold (op +) 0 (compr 1 (in x xs) (in y ys)))");
    CHECK(parseDump("(+, 0)[x + y : [x, y] <- zip(xs, ys)]") == "(fold (op +) 0 (compr (+ x y) (in (pack x y) (call zip xs ys))))");
    CHECK(parseDump("[i, x]") == "(pack i x)");
    CHECK(parseDump("t[0]") == "(index t 0)");
}

// checks that the two pipes can't share a depth
TEST_CASE("Parser: pipes don't mix", "[parser]") {
    CHECK(parseErrors("x |> f <| y") == 1);
    CHECK(parseErrors("f <| x |> g") == 1);
    CHECK(parseErrors("f <| a + x |> g") == 1);
}

// checks that non-associative operators on one level need parentheses
TEST_CASE("Parser: non-associative extra operators need parentheses", "[parser]") {
    CHECK(parseErrors("a <=> b <=> c") == 1);
    CHECK(parseErrors("a <<> b <<> c") == 1);
    CHECK(parseErrors("a <<> b < c") == 1);
    CHECK(parseErrors("a |=| b = c") == 1);
}

// checks that -> isn't a key/value separator
TEST_CASE("Parser: -> isn't a map's separator", "[parser]") {
    CHECK(parseErrors("[\"ada\" -> 36]") == 1);
    CHECK(parseErrors("[\"ada\" -> 36, \"alan\" -> 41]") == 2);
}

// checks that if ( points at match, while if on its own is a name
TEST_CASE("Parser: if ( points at match", "[parser]") {
    CHECK(parseErrors("if (x < 0)") == 1);
    CHECK(parseErrors("if") == 0);
    CHECK(parseDump("if") == "if");
}

// checks that <-1 at the end of a range is a flow token
TEST_CASE("Parser: a negative range end needs a space", "[parser]") {
    CHECK(parseErrors("[0, ..., <-1]") == 1);
    CHECK(parseErrors("[0, ..., < -1]") == 0);
}

// checks if an unexpected token produces an error expression
TEST_CASE("Parser: unexpected token produces ErrorExpr", "[parser]") {
    CHECK(parseKind("+") == ExprKind::Error);
    CHECK(parseKind("") == ExprKind::Error);
    CHECK(parseKind("...") == ExprKind::Error);
    CHECK(parseKind("> a") == ExprKind::Error);
    CHECK(parseErrors("f(a") == 1);
    CHECK(parseErrors("[a, b") == 1);
    // recovery leaves a closing bracket for the code around the mistake, and a malformed token the lexer reported isn't reported again
    CHECK(parseErrors("f(1, )") == 1);
    CHECK(parseErrors("0x") == 1);
    CHECK(parseErrors("\"abc") == 1);
    CHECK(parseErrors("[x : x <- xs || y]") == 1);
}

// checks that an expression's span covers the whole string
TEST_CASE("Parser: spans cover full expression", "[parser]") {
    for (std::string src : {"a + b", "f(x)[0]", "Point <- [1, 2]", "(+, 0)[xs]", "a < b <= c", "x |> f", "old p.x", "[0, ..., <n]"}) {
        SourceManager sm;
        sm.loadString(src);
        DiagnosticEngine diag(sm);
        Lexer lexer(diag);
        auto tokens = lexer.lex();
        Parser parser(tokens, diag);
        auto expr = parser.parseExpr();
        CHECK(expr->span.start == 0);
        CHECK(expr->span.end == src.size());
    }
}
