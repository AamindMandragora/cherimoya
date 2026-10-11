#include <catch2/catch_test_macros.hpp>

#include <string>
#include <memory>

#include "parser.hpp"

// forward declare dump helpers
static std::string dumpType(const Type& t);
static std::string dumpPattern(const Pattern& p);
static std::string dumpStmt(const Stmt& s);
static std::string dumpParams(const std::vector<Param>& ps);

// converts an expression to text
static std::string dumpExpr(const Expr& e) {
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
            return "(pre:" + std::string(u.op.text) + " " + dumpExpr(*u.operand) + ")";
        }
        case ExprKind::Binary: {
            auto& b = static_cast<const BinaryExpr&>(e);
            return "(" + std::string(b.op.text) + " " + dumpExpr(*b.lhs) + " " + dumpExpr(*b.rhs) + ")";
        }
        case ExprKind::ExtraBinary: {
            auto& b = static_cast<const ExtraBinaryExpr&>(e);
            return "(" + std::string(b.op.text) + " " + dumpExpr(*b.lhs) + " " + dumpExpr(*b.rhs) + ")";
        }
        case ExprKind::Chain: {
            auto& c = static_cast<const ChainExpr&>(e);
            std::string s = "(chain " + dumpExpr(*c.operands[0]);
            for (size_t i = 0; i < c.ops.size(); i++) s += " " + std::string(c.ops[i].text) + " " + dumpExpr(*c.operands[i + 1]);
            return s + ")";
        }
        case ExprKind::Pipe: {
            auto& p = static_cast<const PipeExpr&>(e);
            return "(" + std::string(p.op.text) + " " + dumpExpr(*p.lhs) + " " + dumpExpr(*p.rhs) + ")";
        }
        case ExprKind::OpRef:
            return "(op " + std::string(static_cast<const OpRefExpr&>(e).op.text) + ")";
        case ExprKind::Old:
            return "(old " + dumpExpr(*static_cast<const OldExpr&>(e).operand) + ")";
        case ExprKind::Call: {
            auto& c = static_cast<const CallExpr&>(e);
            std::string s = "(call " + dumpExpr(*c.callee);
            for (auto& a : c.args) s += " " + dumpExpr(*a);
            return s + ")";
        }
        case ExprKind::Index: {
            auto& i = static_cast<const IndexExpr&>(e);
            std::string s = "(index " + dumpExpr(*i.object);
            for (auto& a : i.args) s += " " + dumpExpr(*a);
            return s + ")";
        }
        case ExprKind::Member: {
            auto& m = static_cast<const MemberExpr&>(e);
            return "(. " + dumpExpr(*m.object) + " " + std::string(m.field) + ")";
        }
        case ExprKind::Construct: {
            auto& c = static_cast<const ConstructExpr&>(e);
            return "(<- " + dumpExpr(*c.target) + " " + dumpExpr(*c.pack) + ")";
        }
        case ExprKind::Pack: {
            auto& p = static_cast<const PackExpr&>(e);
            std::string s = "(pack";
            for (auto& el : p.elems) {
                s += " ";
                if (el.name) s += std::string(el.name->text) + "<-";
                s += dumpExpr(*el.value);
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
                s += dumpExpr(*el.value);
            }
            return s + ")";
        }
        case ExprKind::Compr: {
            auto& c = static_cast<const ComprExpr&>(e);
            std::string s = "(compr " + dumpExpr(*c.head);
            for (auto& cl : c.clauses) {
                if (cl.pattern) s += " (in " + dumpPattern(*cl.pattern) + " " + dumpExpr(*cl.expr) + ")";
                else s += " " + dumpExpr(*cl.expr);
            }
            return s + ")";
        }
        case ExprKind::Fold: {
            auto& f = static_cast<const FoldExpr&>(e);
            return "(fold " + dumpExpr(*f.reducer) + " " + dumpExpr(*f.seed) + " " + dumpExpr(*f.sequence) + ")";
        }
        case ExprKind::Ann: {
            auto& a = static_cast<const AnnExpr&>(e);
            return "(:: " + dumpExpr(*a.expr) + " " + dumpType(*a.type) + ")";
        }
        case ExprKind::TypeExpr:
            return "(type " + dumpType(*static_cast<const TypeExprExpr&>(e).type) + ")";
        case ExprKind::Closure: {
            auto& c = static_cast<const ClosureExpr&>(e);
            std::string s = "(fn";
            if (!c.captures.empty()) {
                s += " [";
                for (size_t i = 0; i < c.captures.size(); i++) s += (i ? " " : "") + dumpExpr(*c.captures[i]);
                s += "]";
            }
            s += " " + dumpParams(c.params);
            if (c.outputs) s += " -> " + dumpParams(*c.outputs);
            return s + " " + dumpExpr(*c.body) + ")";
        }
        case ExprKind::Block: {
            auto& b = static_cast<const BlockExpr&>(e);
            std::string s = "(block";
            if (b.name) s += ":" + std::string(b.name->text);
            for (auto& st : b.stmts) s += " " + dumpStmt(*st);
            return s + ")";
        }
        case ExprKind::Match: {
            auto& m = static_cast<const MatchExpr&>(e);
            std::string s = "(match";
            if (m.name) s += ":" + std::string(m.name->text);
            if (m.subject) s += " " + dumpExpr(*m.subject);
            for (auto& arm : m.arms) s += " (" + dumpPattern(*arm.pattern) + " -> " + dumpExpr(*arm.body) + ")";
            return s + ")";
        }
        case ExprKind::Repeat: {
            auto& r = static_cast<const RepeatExpr&>(e);
            std::string s = "(repeat";
            if (r.name) s += ":" + std::string(r.name->text);
            if (r.binder) s += " (for " + dumpPattern(*r.binder) + " " + dumpExpr(*r.source) + ")";
            if (r.cond) s += " (while " + dumpExpr(*r.cond) + ")";
            if (r.invariant) s += " (wit " + dumpExpr(*r.invariant) + ")";
            return s + " " + dumpExpr(*r.body) + ")";
        }
        case ExprKind::Chunk: {
            auto& c = static_cast<const ChunkExpr&>(e);
            std::string s = "(chunk";
            if (c.name) s += ":" + std::string(c.name->text);
            return s + " " + dumpExpr(*c.body) + ")";
        }
        case ExprKind::Error:
            return "<error>";
    }
    return "<unknown>";
}

// converts a generic argument to text
static std::string dumpArg(const GenericArg& a) {
    return a.type ? dumpType(*a.type) : dumpExpr(*a.value);
}

// converts a type to text
static std::string dumpType(const Type& t) {
    switch (t.kind) {
        case TypeKind::Named: {
            auto& n = static_cast<const NamedType&>(t);
            std::string name;
            for (size_t i = 0; i < n.path.size(); i++) name += (i ? "." : "") + std::string(n.path[i].text);
            if (n.args.empty()) return name;
            std::string s = "(" + name;
            for (auto& a : n.args) s += " " + dumpArg(a);
            return s + ")";
        }
        case TypeKind::Qualified: {
            auto& q = static_cast<const QualifiedType&>(t);
            const char* pos = q.position == QualPosition::Handle ? "@handle" : q.position == QualPosition::Elements ? "@elems" : "@value";
            std::string s = "(" + std::string(q.qualifier.text) + pos;
            if (q.inner) s += " " + dumpType(*q.inner);
            return s + ")";
        }
        case TypeKind::Handle: {
            auto& h = static_cast<const HandleType&>(t);
            std::string s = "(" + std::string(h.handle.text);
            if (h.arg) s += " [" + dumpArg(*h.arg) + "]";
            return s + " " + dumpType(*h.inner) + ")";
        }
        case TypeKind::Mode: {
            auto& m = static_cast<const ModeType&>(t);
            std::string s = "(" + std::string(m.mode.text);
            if (m.inner) s += " " + dumpType(*m.inner);
            return s + ")";
        }
        case TypeKind::Tuple: {
            auto& tu = static_cast<const TupleType&>(t);
            std::string s = "(tuple";
            for (auto& el : tu.elems) s += " " + dumpType(*el);
            return s + ")";
        }
        case TypeKind::Record: {
            auto& r = static_cast<const RecordType&>(t);
            std::string s = "(record";
            for (auto& f : r.fields) s += " " + std::string(f.name.text) + ":" + dumpType(*f.type);
            return s + ")";
        }
        case TypeKind::Refined: {
            auto& r = static_cast<const RefinedType&>(t);
            std::string s = "(refine " + dumpType(*r.inner);
            if (r.binder) s += " " + std::string(r.binder->text) + " :";
            return s + " " + dumpExpr(*r.predicate) + ")";
        }
        case TypeKind::Error:
            return "<error-type>";
    }
    return "<unknown-type>";
}

// converts a list of name :: type params to text
static std::string dumpParams(const std::vector<Param>& ps) {
    std::string s = "(";
    for (size_t i = 0; i < ps.size(); i++) {
        if (i) s += " ";
        s += std::string(ps[i].name.text);
        if (ps[i].type) s += ":" + dumpType(*ps[i].type);
    }
    return s + ")";
}

// converts a pattern to text
static std::string dumpPattern(const Pattern& p) {
    switch (p.kind) {
        case PatternKind::Literal:
            return dumpExpr(*static_cast<const LiteralPattern&>(p).literal);
        case PatternKind::Wildcard:
            return "_";
        case PatternKind::Binder:
            return std::string(static_cast<const BinderPattern&>(p).name.text);
        case PatternKind::Variant: {
            auto& v = static_cast<const VariantPattern&>(p);
            std::string s = "(" + std::string(v.name.text);
            for (auto& sub : v.payload) s += " " + dumpPattern(*sub);
            return s + ")";
        }
        case PatternKind::Pack: {
            auto& pk = static_cast<const PackPattern&>(p);
            std::string s = "(pack";
            for (auto& el : pk.elems) {
                s += " ";
                if (el.name) s += std::string(el.name->text) + "<-";
                s += dumpPattern(*el.pattern);
            }
            return s + ")";
        }
        case PatternKind::Range: {
            auto& r = static_cast<const RangePattern&>(p);
            std::string s = "(range";
            for (auto& el : r.elems) {
                s += " ";
                if (!el.value) { s += "..."; continue; }
                if (el.marker) s += std::string(el.marker->text);
                s += dumpExpr(*el.value);
            }
            return s + ")";
        }
        case PatternKind::SetBuilder: {
            auto& sb = static_cast<const SetBuilderPattern&>(p);
            std::string s = "(set " + std::string(sb.binder.text);
            for (auto& pr : sb.predicates) s += " " + dumpExpr(*pr);
            return s + ")";
        }
        case PatternKind::Guard:
            return "(guard " + dumpExpr(*static_cast<const GuardPattern&>(p).cond) + ")";
        case PatternKind::Error:
            return "<error-pattern>";
    }
    return "<unknown-pattern>";
}

// converts a statement to text
static std::string dumpStmt(const Stmt& s) {
    switch (s.kind) {
        case StmtKind::Assign: {
            auto& a = static_cast<const AssignStmt&>(s);
            std::string op = a.op ? std::string(a.op->text) : "<-";
            return "(" + op + " " + dumpExpr(*a.place) + " " + dumpExpr(*a.value) + ")";
        }
        case StmtKind::Expr:
            return "(expr " + dumpExpr(*static_cast<const ExprStmt&>(s).expr) + ")";
        case StmtKind::Yield: {
            auto& y = static_cast<const YieldStmt&>(s);
            std::string r = "(-|";
            if (y.value) r += " " + dumpExpr(*y.value);
            if (y.target) r += " to:" + std::string(y.target->text);
            return r + ")";
        }
        case StmtKind::Let: {
            auto& l = static_cast<const LetStmt&>(s);
            std::string r = "(let " + dumpPattern(*l.pattern);
            if (l.type) r += " :: " + dumpType(*l.type);
            return r + " <- " + dumpExpr(*l.init) + ")";
        }
        case StmtKind::Wit: {
            auto& w = static_cast<const WitStmt&>(s);
            std::string r = "(wit " + dumpExpr(*w.predicate);
            if (w.bound) r += " :: " + dumpType(*w.bound);
            return r + ")";
        }
        case StmtKind::Assert:
            return "(assert " + dumpExpr(*static_cast<const AssertStmt&>(s).cond) + ")";
        case StmtKind::Error:
            return "<error-stmt>";
    }
    return "<unknown-stmt>";
}

// lexes one input and makes a parser over it, owning the source and tokens whatever it parses points into
struct Parsing {
    SourceManager sm;
    DiagnosticEngine diag{sm};
    std::vector<Token> tokens;
    std::unique_ptr<Parser> parser;
    explicit Parsing(const std::string& input) {
        sm.loadString(input);
        tokens = Lexer(diag).lex();
        parser = std::make_unique<Parser>(tokens, diag);
    }
};

// parses input and returns text representation
static std::string parseDump(const std::string& input) {
    Parsing parsed(input);
    return dumpExpr(*parsed.parser->parseExpr());
}

// parses input and returns the outer expression's kind
static ExprKind parseKind(const std::string& input) {
    Parsing parsed(input);
    return parsed.parser->parseExpr()->kind;
}

// parses input and returns how many errors lexing and parsing reported
static size_t parseErrors(const std::string& input) {
    Parsing parsed(input);
    parsed.parser->parseExpr();
    return parsed.diag.errorCount();
}

// parses input as one statement and returns text representation
static std::string stmtDump(const std::string& input) {
    Parsing parsed(input);
    return dumpStmt(*parsed.parser->parseStmt());
}

// parses input as a type and returns text representation
static std::string typeDump(const std::string& input) {
    Parsing parsed(input);
    return dumpType(*parsed.parser->parseType());
}

// parses input as a pattern and returns text representation
static std::string patternDump(const std::string& input) {
    Parsing parsed(input);
    return dumpPattern(*parsed.parser->parsePattern());
}

// parses input as statements up to the end and returns how many errors lexing and parsing reported
static size_t stmtErrors(const std::string& input) {
    Parsing parsed(input);
    while (!parsed.parser->atEnd()) parsed.parser->parseStmt();
    return parsed.diag.errorCount();
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
    CHECK(parseDump("[d : Some[d] <- xs]") == "(compr d (in (Some d) xs))");
    CHECK(parseDump("[[xs[i], ys[i]] : 0 <= i < n]") == "(compr (pack (index xs i) (index ys i)) (chain 0 <= i < n))");
    CHECK(parseDump("[o : o <- orders && o.status = Paid]") == "(compr o (in o orders) (= (. o status) Paid))");
    CHECK(parseDump("[v : v <- [o : o <- os && f(o)]]") == "(compr v (in v (compr o (in o os) (call f o))))");
    CHECK(parseDump("[p : p <- ps && p = Point <- [0, 0]]") == "(compr p (in p ps) (in <error-pattern> (pack 0 0)))");
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

// checks named, handle, mode, tuple, and record types
TEST_CASE("Parser: types", "[parser][type]") {
    CHECK(typeDump("int") == "int");
    CHECK(typeDump("Node") == "Node");
    CHECK(typeDump("std.mem.Allocator") == "std.mem.Allocator");
    CHECK(typeDump("List[int]") == "(List int)");
    CHECK(typeDump("Map[K, V]") == "(Map K V)");
    CHECK(typeDump("List[T, A]") == "(List T A)");
    CHECK(typeDump("Option[unscoped ptr Expr]") == "(Option (unscoped (ptr Expr)))");
    CHECK(typeDump("Fn[F, [S, T], S]") == "(Fn F (tuple S T) S)");
    CHECK(typeDump("ptr T") == "(ptr T)");
    CHECK(typeDump("arr T") == "(arr T)");
    CHECK(typeDump("arr[16] T") == "(arr [16] T)");
    CHECK(typeDump("arr[A] T") == "(arr [A] T)");
    CHECK(typeDump("arr[Chunk] mut T") == "(arr [Chunk] (mut@elems T))");
    CHECK(typeDump("arr[N * 2] T") == "(arr [(* N 2)] T)");
    CHECK(typeDump("[int, f64]") == "(tuple int f64)");
    CHECK(typeDump("[x :: f64, y :: f64]") == "(record x:f64 y:f64)");
    CHECK(typeDump("[data :: arr[A] mut T, count :: i64u]") == "(record data:(arr [A] (mut@elems T)) count:i64u)");
}

// checks that where mut sits decides what it applies to, and the modes
TEST_CASE("Parser: qualifiers and modes", "[parser][type]") {
    CHECK(typeDump("mut int") == "(mut@value int)");
    CHECK(typeDump("mut arr T") == "(mut@handle (arr T))");
    CHECK(typeDump("arr mut T") == "(arr (mut@elems T))");
    CHECK(typeDump("mut arr mut T") == "(mut@handle (arr (mut@elems T)))");
    CHECK(typeDump("mut ptr T") == "(mut@handle (ptr T))");
    CHECK(typeDump("peek arr char") == "(peek (arr char))");
    CHECK(typeDump("peek mut S") == "(peek (mut@value S))");
    CHECK(typeDump("peek arr mut i64u") == "(peek (arr (mut@elems i64u)))");
    CHECK(typeDump("unscoped ptr Expr") == "(unscoped (ptr Expr))");
    CHECK(typeDump("unscoped") == "(unscoped)");
    CHECK(typeDump("mut") == "(mut@value)");
    CHECK(typeDump("wit") == "(wit)");
}

// checks refinements with and without a named binder
TEST_CASE("Parser: refinements", "[parser][type]") {
    CHECK(typeDump("int {p : 0 <= p <= 100}") == "(refine int p : (chain 0 <= p <= 100))");
    CHECK(typeDump("int {a >= 0}") == "(refine int (chain a >= 0))");
    CHECK(typeDump("int {0 <= r < b && a = q * b + r}") == "(refine int (&& (chain 0 <= r < b) (= a (+ (* q b) r))))");
    CHECK(typeDump("[x :: f64, y :: f64] {p : p.x >= 0.0}") == "(refine (record x:f64 y:f64) p : (chain (. p x) >= 0.0))");
    CHECK(typeDump("Graph {node_count(g) = node_count(old g)}") == "(refine Graph (= (call node_count g) (call node_count (old g))))");
    CHECK(typeDump("+") == "<error-type>");
}

// checks every pattern form
TEST_CASE("Parser: patterns", "[parser][pattern]") {
    CHECK(patternDump("0") == "0");
    CHECK(patternDump("-1") == "(pre:- 1)");
    CHECK(patternDump("'a'") == "'a'");
    CHECK(patternDump("true") == "true");
    CHECK(patternDump("_") == "_");
    CHECK(patternDump("n") == "n");
    CHECK(patternDump("None") == "None");
    CHECK(patternDump("Some[d]") == "(Some d)");
    CHECK(patternDump("Rect[w, h]") == "(Rect w h)");
    CHECK(patternDump("Some[['0', ..., '9']]") == "(Some (range '0' ... '9'))");
    CHECK(patternDump("Some[Rect[_, h]]") == "(Some (Rect _ h))");
    CHECK(patternDump("[true, _]") == "(pack true _)");
    CHECK(patternDump("[x, y]") == "(pack x y)");
    CHECK(patternDump("[x <- 0, y <- py]") == "(pack x<-0 y<-py)");
    CHECK(patternDump("[..., <0]") == "(range ... <0)");
    CHECK(patternDump("[0, ..., 100]") == "(range 0 ... 100)");
    CHECK(patternDump("[>100, ...]") == "(range >100 ...)");
    CHECK(patternDump("[x : x % 2 = 0]") == "(set x (= (% x 2) 0))");
    CHECK(patternDump("[x : x > 0 && x < 10]") == "(set x (chain x > 0) (chain x < 10))");
    CHECK(patternDump("a + b") == "<error-pattern>");
    CHECK(patternDump("f(x)") == "<error-pattern>");
    CHECK(patternDump("[x : x <- xs]") == "<error-pattern>");
}

// checks every binding form
TEST_CASE("Parser: bindings", "[parser][stmt]") {
    CHECK(stmtDump("let x <- 5;") == "(let x <- 5)");
    CHECK(stmtDump("let x :: int <- 5;") == "(let x :: int <- 5)");
    CHECK(stmtDump("let x :: mut int <- 5;") == "(let x :: (mut@value int) <- 5)");
    CHECK(stmtDump("let buf :: unscoped <- arr mut T(n);") == "(let buf :: (unscoped) <- (call (type (arr (mut@elems T))) n))");
    CHECK(stmtDump("let v :: mut arr[16] char <- _;") == "(let v :: (mut@handle (arr [16] char)) <- _)");
    CHECK(stmtDump("let [q, r] <- divmod(17, 5);") == "(let (pack q r) <- (call divmod 17 5))");
    CHECK(stmtDump("let n0 :: wit <- node_count(g);") == "(let n0 :: (wit) <- (call node_count g))");
    CHECK(stmtDump("let count :: mut <- list_new[i64u]();") == "(let count :: (mut@value) <- (call (index list_new (type i64u))))");
    CHECK(stmtDump("let ages :: Map[string, int] <- [[\"ada\", 36], [\"alan\", 41]];") == "(let ages :: (Map string int) <- (pack (pack \"ada\" 36) (pack \"alan\" 41)))");
    CHECK(stmtDump("let x :: int {x > 0} <- 5;") == "(let x :: (refine int (chain x > 0)) <- 5)");
    CHECK(stmtErrors("let x;") == 1);
    CHECK(stmtErrors("let x :: int;") == 1);
}

// checks that a statement is an assignment when <- or a compound follows its expression
TEST_CASE("Parser: assignment and expression statements", "[parser][stmt]") {
    CHECK(stmtDump("x <- 5;") == "(<- x 5)");
    CHECK(stmtDump("xs <- [1, 2];") == "(<- xs (pack 1 2))");
    CHECK(stmtDump("p <- Point <- [1, 2];") == "(<- p (<- Point (pack 1 2)))");
    CHECK(stmtDump("a.b <- 1;") == "(<- (. a b) 1)");
    CHECK(stmtDump("p[] <- v;") == "(<- (index p) v)");
    CHECK(stmtDump("_ <- write(1, s, len(s));") == "(<- _ (call write 1 s (call len s)))");
    CHECK(stmtDump("count +<- 1;") == "(+<- count 1)");
    CHECK(stmtDump("xs[i] *<- 2;") == "(*<- (index xs i) 2)");
    CHECK(stmtDump("counts[hash(k) % n] +<- 1;") == "(+<- (index counts (% (call hash k) n)) 1)");
    CHECK(stmtDump("p **<- 2;") == "(**<- p 2)");
    CHECK(stmtDump("v <+><- w;") == "(<+><- v w)");
    CHECK(stmtDump("check(5);") == "(expr (call check 5))");
    CHECK(stmtDump("swap(a, b);") == "(expr (call swap a b))");
    CHECK(stmtDump("let old_v <- replace(slot, v);") == "(let old_v <- (call replace slot v))");
    CHECK(stmtDump("let e <- take(xs[i]);") == "(let e <- (call take (index xs i)))");
    CHECK(stmtErrors("f(x) <- 1;") == 1);
    CHECK(stmtErrors("a + b <- 1;") == 1);
    CHECK(stmtErrors("x <- 1") == 1);
    CHECK(stmtErrors("x <- y <- 1;") == 1);
}

// checks yields with and without values and targets
TEST_CASE("Parser: yields", "[parser][stmt]") {
    CHECK(stmtDump("count -|") == "(-| count)");
    CHECK(stmtDump("-| i") == "(-| to:i)");
    CHECK(stmtDump("-|") == "(-|)");
    CHECK(stmtDump("100 -| early") == "(-| 100 to:early)");
    CHECK(stmtDump("-| outer") == "(-| to:outer)");
    CHECK(stmtDump("-| repeat") == "(-| to:repeat)");
    CHECK(stmtDump("x -| match") == "(-| x to:match)");
    CHECK(stmtDump("[q, r] -|") == "(-| (pack q r))");
    CHECK(stmtDump("[a / b, a % b] -|") == "(-| (pack (/ a b) (% a b)))");
}

// checks wit and assert
TEST_CASE("Parser: wit and assert", "[parser][stmt]") {
    CHECK(stmtDump("wit {i <= n};") == "(wit (chain i <= n))");
    CHECK(stmtDump("wit {T :: Ord};") == "(wit T :: Ord)");
    CHECK(stmtDump("wit {int :: Ord};") == "(wit (type int) :: Ord)");
    CHECK(stmtDump("assert (total = 220);") == "(assert (= total 220))");
    CHECK(stmtDump("assert (q = 3 && r = 2);") == "(assert (&& (= q 3) (= r 2)))");
    CHECK(stmtErrors("assert total = 220;") > 0);
}

// checks plain and named blocks, which are always their own statement
TEST_CASE("Parser: blocks", "[parser][stmt]") {
    CHECK(stmtDump("{ x <- 1; }") == "(expr (block (<- x 1)))");
    CHECK(stmtDump("{ }") == "(expr (block))");
    CHECK(stmtDump("outer { -| outer }") == "(expr (block:outer (-| to:outer)))");
    CHECK(stmtDump("{ x -| } -|") == "(-| (block (-| x)))");
    CHECK(stmtDump("let x <- { 5 -| };") == "(let x <- (block (-| 5)))");
    CHECK(stmtErrors("{ x <- 1; } { y <- 2; }") == 0);
}

// checks both forms of match, names, and match as an expression
TEST_CASE("Parser: match", "[parser][stmt]") {
    CHECK(stmtDump("match (n) { [..., <0] -> { 0 -| } [0, ..., 100] -> { n -| } [>100, ...] -> { 100 -| } } -|")
        == "(-| (match n ((range ... <0) -> (block (-| 0))) ((range 0 ... 100) -> (block (-| n))) ((range >100 ...) -> (block (-| 100)))))");
    CHECK(stmtDump("match (s) { Circle[r] -> { 3.14159 * r * r -| } Rect[w, h] -> { w * h -| } }")
        == "(expr (match s ((Circle r) -> (block (-| (* (* 3.14159 r) r)))) ((Rect w h) -> (block (-| (* w h))))))");
    CHECK(stmtDump("match ([x, y]) { [true, true] -> { 3 -| } [false, _] -> { 0 -| } }")
        == "(expr (match (pack x y) ((pack true true) -> (block (-| 3))) ((pack false _) -> (block (-| 0)))))");
    CHECK(stmtDump("match { (xs[i] < 0) -> { -| i } }") == "(expr (match ((guard (chain (index xs i) < 0)) -> (block (-| to:i)))))");
    CHECK(stmtDump("match { (a < b && ready) -> { 1 -| } _ -> { 2 -| } }")
        == "(expr (match ((guard (&& (chain a < b) ready)) -> (block (-| 1))) (_ -> (block (-| 2)))))");
    CHECK(stmtDump("match name (x) { _ -> { -| name } }") == "(expr (match:name x (_ -> (block (-| to:name)))))");
    CHECK(stmtDump("match name { _ -> { } }") == "(expr (match:name (_ -> (block))))");
    CHECK(stmtDump("let p <- match (n % 2) { 0 -> { 1 -| } _ -> { 2 -| } };")
        == "(let p <- (match (% n 2) (0 -> (block (-| 1))) (_ -> (block (-| 2)))))");
    CHECK(stmtDump("match (x) { [x : x % 2 = 0] -> { } }") == "(expr (match x ((set x (= (% x 2) 0)) -> (block))))");
    CHECK(stmtErrors("match { x -> { } }") == 1);
    CHECK(stmtErrors("match (x) { 1 { } }") > 0);
}

// checks every form of repeat
TEST_CASE("Parser: repeat", "[parser][stmt]") {
    CHECK(stmtDump("repeat (x) <- xs { f(x); }") == "(expr (repeat (for x xs) (block (expr (call f x)))))");
    CHECK(stmtDump("repeat (n > 0) { n -<- 1; }") == "(expr (repeat (while (chain n > 0)) (block (-<- n 1))))");
    CHECK(stmtDump("repeat { }") == "(expr (repeat (block)))");
    CHECK(stmtDump("repeat (step) <- [0, ...] { }") == "(expr (repeat (for step (range 0 ...)) (block)))");
    CHECK(stmtDump("repeat outer (i) <- [0, ..., <n] { -| outer }") == "(expr (repeat:outer (for i (range 0 ... <n)) (block (-| to:outer))))");
    CHECK(stmtDump("repeat (i) <- xs wit {i <= n} { }") == "(expr (repeat (for i xs) (wit (chain i <= n)) (block)))");
    CHECK(stmtDump("repeat (ok) wit {inv} { }") == "(expr (repeat (while ok) (wit inv) (block)))");
    CHECK(stmtDump("repeat ([i, x]) <- enumerate(xs) { }") == "(expr (repeat (for (pack i x) (call enumerate xs)) (block)))");
    CHECK(stmtDump("repeat (i) <- [0, 2, ..., <len(xs)] { xs[i] *<- 2; }") == "(expr (repeat (for i (range 0 2 ... <(call len xs))) (block (*<- (index xs i) 2))))");
    CHECK(stmtDump("let v <- repeat { 1 -| };") == "(let v <- (repeat (block (-| 1))))");
    CHECK(stmtDump("repeat { x -| repeat } -|") == "(-| (repeat (block (-| x to:repeat))))");
}

// checks chunk with and without a name
TEST_CASE("Parser: chunk", "[parser][stmt]") {
    CHECK(stmtDump("chunk { let scratch :: mut <- list_new[i64u](); }")
        == "(expr (chunk (block (let scratch :: (mut@value) <- (call (index list_new (type i64u)))))))");
    CHECK(stmtDump("chunk name { -| name }") == "(expr (chunk:name (block (-| to:name))))");
}

// checks inferred, annotated, and capture closures
TEST_CASE("Parser: closures", "[parser][expr]") {
    CHECK(parseDump("(acc, x) -> { acc + x * rate -| }") == "(fn (acc x) (block (-| (+ acc (* x rate)))))");
    CHECK(parseDump("(a :: int, b :: int) -> (r :: int) { a * b -| }") == "(fn (a:int b:int) -> (r:int) (block (-| (* a b))))");
    CHECK(parseDump("() -> { }") == "(fn () (block))");
    CHECK(parseDump("(x) -> () { }") == "(fn (x) -> () (block))");
    CHECK(parseDump("[factor] (x) -> { x * factor -| }") == "(fn [factor] (x) (block (-| (* x factor))))");
    CHECK(parseDump("[(x :: int) -> (r :: int) { x + 1 -| }, g]") == "(pack (fn (x:int) -> (r:int) (block (-| (+ x 1)))) g)");
    CHECK(parseDump("((acc, x) -> { acc + x * rate -| }, 0)[[1, 2, 3]]") == "(fold (fn (acc x) (block (-| (+ acc (* x rate))))) 0 (pack 1 2 3))");
    CHECK(parseDump("(x :: int {x >= 0}) -> { }") == "(fn (x:(refine int (chain x >= 0))) (block))");
    CHECK(parseKind("(x) -> { x -| }") == ExprKind::Closure);
    CHECK(stmtDump("let step <- (acc, x) -> { acc + x * rate -| };") == "(let step <- (fn (acc x) (block (-| (+ acc (* x rate))))))");
    CHECK(parseErrors("[x <- 1] (y) -> { }") == 1);
}

// checks annotation and types in value position
TEST_CASE("Parser: annotation and types in value position", "[parser][expr]") {
    CHECK(parseDump("(p :: Ord)") == "(:: p Ord)");
    CHECK(parseDump("cmp((p :: Ord), q)") == "(call cmp (:: p Ord) q)");
    CHECK(parseDump("i32(x)") == "(call (type i32) x)");
    CHECK(parseDump("f64(x)") == "(call (type f64) x)");
    CHECK(parseDump("arr mut T(n)") == "(call (type (arr (mut@elems T))) n)");
    CHECK(parseDump("arr mut T(n, fill)") == "(call (type (arr (mut@elems T))) n fill)");
    CHECK(parseDump("arr mut T(n, _)") == "(call (type (arr (mut@elems T))) n _)");
    CHECK(parseDump("arr T(seq)") == "(call (type (arr T)) seq)");
    CHECK(parseDump("arr i64u([x : x <- xs])") == "(call (type (arr i64u)) (compr x (in x xs)))");
    CHECK(parseDump("ptr T(value)") == "(call (type (ptr T)) value)");
    CHECK(parseDump("size[i32]()") == "(call (index size (type i32)))");
    CHECK(parseDump("List[peek T]") == "(index List (type (peek T)))");
    CHECK(parseDump("NodeId(3)") == "(call NodeId 3)");
    CHECK(parseDump("i64u(id) + 1") == "(+ (call (type i64u) id) 1)");
}

// checks a match as a comprehension head
TEST_CASE("Parser: scopes inside expressions", "[parser][expr]") {
    CHECK(parseDump("[match (i % 2) { 0 -> { x * 2 -| } _ -> { x -| } } : [i, x] <- enumerate(xs)]")
        == "(compr (match (% i 2) (0 -> (block (-| (* x 2)))) (_ -> (block (-| x)))) (in (pack i x) (call enumerate xs)))");
    CHECK(parseDump("(+, 0)[match (i % 2) { 0 -> { x * 2 -| } _ -> { x -| } } : [i, x] <- enumerate(xs)]")
        == "(fold (op +) 0 (compr (match (% i 2) (0 -> (block (-| (* x 2)))) (_ -> (block (-| x)))) (in (pack i x) (call enumerate xs))))");
}

// checks a golden dump of a count_pairs body
TEST_CASE("Parser: golden count_pairs body", "[parser][stmt]") {
    const char* src =
        "{\n"
        "    let count :: mut i64u <- 0;\n"
        "    let n <- len(xs);\n"
        "    repeat outer (i) <- [0, ..., <n] {\n"
        "        match { (xs[i] < 0) -> { -| i } }                     # continue outer\n"
        "        repeat (j) <- [>i, ..., <n] {\n"
        "            match { (xs[j] < 0) -> { -| j } }                 # continue inner\n"
        "            match { (xs[i] + xs[j] = target) -> { count +<- 1; } }\n"
        "            match { (count = 1000) -> { -| outer } }          # break outer\n"
        "        }\n"
        "    }\n"
        "    count -|\n"
        "}\n";
    CHECK(stmtErrors(src) == 0);
    CHECK(stmtDump(src) ==
        "(expr (block"
        " (let count :: (mut@value i64u) <- 0)"
        " (let n <- (call len xs))"
        " (expr (repeat:outer (for i (range 0 ... <n)) (block"
        " (expr (match ((guard (chain (index xs i) < 0)) -> (block (-| to:i)))))"
        " (expr (repeat (for j (range >i ... <n)) (block"
        " (expr (match ((guard (chain (index xs j) < 0)) -> (block (-| to:j)))))"
        " (expr (match ((guard (= (+ (index xs i) (index xs j)) target)) -> (block (+<- count 1)))))"
        " (expr (match ((guard (= count 1000)) -> (block (-| to:outer)))))))))))"
        " (-| count)))");
}

// checks a golden dump of a main body that uses most of the grammar
TEST_CASE("Parser: golden main body", "[parser][stmt]") {
    const char* src =
        "{\n"
        "    let rate    <- 2;\n"
        "    let xs      <- [1, 2, 3, 4];\n"
        "    let ys      <- [10, 20, 30];\n"
        "    let total   <- (+, 0)[x * x : x <- [1, ..., 10] && x % 2 = 0];\n"
        "    let pairs   <- count_pairs([3, -1, 4, 1, 5], 5);\n"
        "    let [q, r]  <- divmod(17, 5);\n"
        "    let v       <- Vec2 <- [x <- 1.0, y <- 2.0] <+> Vec2 <- [3.0, 4.0];\n"
        "    let doubled <- ((acc, x) -> { acc + x * rate -| }, 0)[[1, 2, 3]];\n"
        "    let sums    <- (+, 0)[x + y : [x, y] <- zip(xs, ys)];\n"
        "    let product <- (+, 0)[1 : x <- xs && y <- ys];\n"
        "    let mixed   <- (+, 0)[match (i % 2) { 0 -> { x * 2 -| } _ -> { x -| } } : [i, x] <- enumerate(xs)];\n"
        "    let piped   <- 3 |> inc |> double;\n"
        "    let back    <- double <| inc <| 2 + 3;\n"
        "    assert (total = 220);\n"
        "    assert (q = 3 && r = 2);\n"
        "    assert (doubled = 12);\n"
        "    assert (sums = 66);\n"
        "    assert (product = 12);\n"
        "    assert (mixed = 14);\n"
        "    assert (piped = 8 && back = 12);\n"
        "    match { (pairs = 1 && v.x = 4.0) -> { 0 -| } _ -> { 1 -| } } -|\n"
        "}\n";
    CHECK(stmtErrors(src) == 0);
}

// checks if ( at statement level: one diagnostic, and the block after it doesn't cascade
TEST_CASE("Parser: if ( as a statement", "[parser][stmt]") {
    CHECK(stmtErrors("if (x < 0) { y <- 1; }") == 1);
    CHECK(stmtErrors("if (x < 0) { y <- 1; } z <- 2;") == 1);
}

// checks statement-level error recovery
TEST_CASE("Parser: statement error recovery", "[parser][stmt]") {
    CHECK(stmtErrors("x <- ; y <- 2;") == 1);
    CHECK(stmtErrors("{ x <- 1 }") == 1);
    CHECK(stmtErrors("let (a :: int) -> f -> (r :: int) { a -| }") == 1);
    CHECK(stmtDump("+;") == "(expr <error>)");
    // a stray closing bracket is one error, and a parenthesized pattern is an arm, not a closure
    CHECK(stmtErrors("{ ) x <- 1; }") == 1);
    CHECK(stmtErrors("match (x) { (1) -> { } _ -> { } }") == 0);
    CHECK(stmtErrors("let x :: <- 5;") == 1);
}
