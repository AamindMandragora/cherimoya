#include <catch2/catch_test_macros.hpp>

#include <string>
#include <memory>
#include <algorithm>

#include "parser.hpp"

// forward declare dump helpers
static std::string dumpType(const Type& t);
static std::string dumpPattern(const Pattern& p);
static std::string dumpStmt(const Stmt& s);
static std::string dumpParams(const std::vector<Param>& ps);
static std::string dumpDecl(const Decl& d);

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
        case ExprKind::ExtraBinOp: {
            auto& b = static_cast<const ExtraBinOpExpr&>(e);
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
        case StmtKind::Decl:
            return dumpDecl(*static_cast<const DeclStmt&>(s).decl);
        case StmtKind::Attr: {
            auto& a = static_cast<const AttrStmt&>(s);
            std::string r = "(@" + std::string(a.name.text);
            for (auto& arg : a.args) r += " " + dumpExpr(*arg);
            if (a.target) r += " " + dumpStmt(*a.target);
            return r + ")";
        }
        case StmtKind::Error:
            return "<error-stmt>";
    }
    return "<unknown-stmt>";
}

// converts generic parameters to text
static std::string dumpGenerics(const std::vector<GenericParam>& gs) {
    std::string s = "[";
    for (size_t i = 0; i < gs.size(); i++) {
        if (i) s += " ";
        s += std::string(gs[i].name.text);
        for (size_t j = 0; j < gs[i].bounds.size(); j++) s += (j ? "&" : "::") + dumpType(*gs[i].bounds[j]);
        if (gs[i].fallback) s += "<-" + dumpArg(*gs[i].fallback);
    }
    return s + "]";
}

// converts an export or import list to text
static std::string dumpItems(const std::vector<ModuleItem>& items) {
    std::string s;
    for (auto& it : items) {
        s += " ";
        if (it.fields.empty()) { s += std::string(it.name.text); continue; }
        s += "(" + std::string(it.name.text);
        for (auto& f : it.fields) s += " " + std::string(f.text);
        s += ")";
    }
    return s;
}

// converts a declaration to text
static std::string dumpDecl(const Decl& d) {
    switch (d.kind) {
        case DeclKind::Fn: {
            auto& f = static_cast<const FnDecl&>(d);
            std::string s = "(fn-decl " + std::string(f.name.text);
            if (!f.generics.empty()) s += " " + dumpGenerics(f.generics);
            s += " " + dumpParams(f.inputs) + " -> " + dumpParams(f.outputs);
            if (f.obligations) s += " (wit " + dumpExpr(*f.obligations) + ")";
            s += f.body ? " " + dumpExpr(*f.body) : " no-body";
            return s + ")";
        }
        case DeclKind::Type: {
            auto& t = static_cast<const TypeDecl&>(d);
            std::string s = "(type " + std::string(t.name.text);
            if (!t.generics.empty()) s += " " + dumpGenerics(t.generics);
            switch (t.form) {
                case TypeForm::Newtype: s += " newtype"; break;
                case TypeForm::Refinement: s += " refinement"; break;
                case TypeForm::Product: s += " product"; break;
                case TypeForm::Sum: s += " sum"; break;
                case TypeForm::Enum: s += " enum"; break;
                case TypeForm::Interface: s += " interface"; break;
            }
            if (t.rhs) s += " " + dumpType(*t.rhs);
            for (auto& v : t.variants) {
                if (v.payload.empty()) { s += " " + std::string(v.name.text); continue; }
                s += " (" + std::string(v.name.text);
                for (auto& p : v.payload) s += " " + dumpType(*p);
                s += ")";
            }
            for (auto& m : t.members) s += " " + dumpDecl(*m);
            return s + ")";
        }
        case DeclKind::Export:
            return "(export" + dumpItems(static_cast<const ExportDecl&>(d).items) + ")";
        case DeclKind::Import: {
            auto& i = static_cast<const ImportDecl&>(d);
            std::string path;
            for (size_t j = 0; j < i.path.size(); j++) path += (j ? "." : "") + std::string(i.path[j].text);
            return "(import" + dumpItems(i.items) + " <- " + path + ")";
        }
        case DeclKind::Error:
            return "<error-decl>";
    }
    return "<unknown-decl>";
}

// parses input and returns text representation
static std::string parseDump(const std::string& input) {
    SourceManager sm;
    sm.load_string(input);
    DiagnosticEngine diag(sm);
    Lexer lexer(diag);
    auto tokens = lexer.lex();
    Parser parser(tokens, diag);
    auto expr = parser.parseExpr();
    return dumpExpr(*expr);
}

// parses input and returns the outer expression's kind
static ExprKind parseKind(const std::string& input) {
    SourceManager sm;
    sm.load_string(input);
    DiagnosticEngine diag(sm);
    Lexer lexer(diag);
    auto tokens = lexer.lex();
    Parser parser(tokens, diag);
    auto expr = parser.parseExpr();
    return expr->kind;
}

// parses input and returns how many diagnostics lexing and parsing produced
static size_t parseErrors(const std::string& input) {
    SourceManager sm;
    sm.load_string(input);
    DiagnosticEngine diag(sm);
    Lexer lexer(diag);
    auto tokens = lexer.lex();
    Parser parser(tokens, diag);
    parser.parseExpr();
    return diag.error_count();
}

// parses input as one statement and returns text representation
static std::string stmtDump(const std::string& input) {
    SourceManager sm;
    sm.load_string(input);
    DiagnosticEngine diag(sm);
    Lexer lexer(diag);
    auto tokens = lexer.lex();
    Parser parser(tokens, diag);
    auto stmt = parser.parseStmt();
    return dumpStmt(*stmt);
}

// parses input as a type and returns text representation
static std::string typeDump(const std::string& input) {
    SourceManager sm;
    sm.load_string(input);
    DiagnosticEngine diag(sm);
    Lexer lexer(diag);
    auto tokens = lexer.lex();
    Parser parser(tokens, diag);
    auto type = parser.parseType();
    return dumpType(*type);
}

// parses input as a pattern and returns text representation
static std::string patternDump(const std::string& input) {
    SourceManager sm;
    sm.load_string(input);
    DiagnosticEngine diag(sm);
    Lexer lexer(diag);
    auto tokens = lexer.lex();
    Parser parser(tokens, diag);
    auto pattern = parser.parsePattern();
    return dumpPattern(*pattern);
}

// parses input as a whole program and returns text representation, one statement per line
static std::string programDump(const std::string& input) {
    SourceManager sm;
    sm.load_string(input);
    DiagnosticEngine diag(sm);
    Lexer lexer(diag);
    auto tokens = lexer.lex();
    Parser parser(tokens, diag);
    auto program = parser.parseProgram();
    std::string s;
    for (size_t i = 0; i < program.size(); i++) s += (i ? "\n" : "") + dumpStmt(*program[i]);
    return s;
}

// parses input as statements up to the end and returns how many diagnostics lexing and parsing produced
static size_t stmtErrors(const std::string& input) {
    SourceManager sm;
    sm.load_string(input);
    DiagnosticEngine diag(sm);
    Lexer lexer(diag);
    auto tokens = lexer.lex();
    Parser parser(tokens, diag);
    while (!parser.atEnd()) parser.parseStmt();
    return diag.error_count();
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
    CHECK(parseKind("a <+> b") == ExprKind::ExtraBinOp);
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
}

// pass/calls_and_pipes: golden dumps
TEST_CASE("Parser: pass/calls_and_pipes", "[parser]") {
    CHECK(parseDump("3 |> inc |> double") == "(|> (|> 3 inc) double)");
    CHECK(parseDump("double <| inc <| 2 + 3") == "(<| double (<| inc (+ 2 3)))");
    CHECK(parseDump("xs |> filter_pos |> sum |> print") == "(|> (|> (|> xs filter_pos) sum) print)");
    CHECK(parseDump("print <| format(a + b)") == "(<| print (call format (+ a b)))");
    CHECK(parseDump("len(xs) - 1 |> f") == "(|> (- (call len xs) 1) f)");
    CHECK(parseDump("slot(xs, i)[] + get(l, i).name") == "(+ (index (call slot xs i)) (. (call get l i) name))");
    CHECK(parseDump("Ord.cmp(a, b) = 0") == "(= (call (. Ord cmp) a b) 0)");
    CHECK(parseDump("count_pairs([3, -1, 4, 1, 5], 5)") == "(call count_pairs (pack 3 (pre:- 1) 4 1 5) 5)");
}

// pass/user_operators: golden dumps
TEST_CASE("Parser: pass/user_operators", "[parser]") {
    CHECK(parseDump("Vec2 <- [x <- 1.0, y <- 2.0] <+> Vec2 <- [3.0, 4.0]") == "(<+> (<- Vec2 (pack x<-1.0 y<-2.0)) (<- Vec2 (pack 3.0 4.0)))");
    CHECK(parseDump("a ** b ** 2") == "(** a (** b 2))");
    CHECK(parseDump("(a <=> b) < 0 && c |+| d = e") == "(&& (chain (<=> a b) < 0) (= (|+| c d) e))");
    CHECK(parseDump("xs ++ ys ++ zs") == "(++ (++ xs ys) zs)");
    CHECK(parseDump("m >>= f >=> g") == "(>>= m (>=> f g))");
    CHECK(parseDump("p <$> q <|> r") == "(<$> p (<|> q r))");
    CHECK(parseDump("a <~ b") == "(<~ a b)");
    CHECK(parseDump("a < ~b") == "(chain a < (pre:~ b))");
}

// pass/pairs_and_packs: golden dumps
TEST_CASE("Parser: pass/pairs_and_packs", "[parser]") {
    CHECK(parseDump("[[\"ada\", 36], [\"alan\", 41]]") == "(pack (pack \"ada\" 36) (pack \"alan\" 41))");
    CHECK(parseDump("[acc[0] + 1.0, acc[1] + x, acc[2] + x * x]") == "(pack (+ (index acc 0) 1.0) (+ (index acc 1) x) (+ (index acc 2) (* x x)))");
    CHECK(parseDump("m[2] / m[0] - mean * mean") == "(- (/ (index m 2) (index m 0)) (* mean mean))");
    CHECK(parseDump("(+, 0)[1 : x <- xs && y <- ys]") == "(fold (op +) 0 (compr 1 (in x xs) (in y ys)))");
    CHECK(parseDump("(+, 0)[x + y : [x, y] <- zip(xs, ys)]") == "(fold (op +) 0 (compr (+ x y) (in (pack x y) (call zip xs ys))))");
    CHECK(parseDump("[i, x]") == "(pack i x)");
    CHECK(parseDump("t[0]") == "(index t 0)");
}

// fail/pipe_mix: the two pipes can't share a depth
TEST_CASE("Parser: fail/pipe_mix", "[parser]") {
    CHECK(parseErrors("x |> f <| y") == 1);
    CHECK(parseErrors("f <| x |> g") == 1);
    CHECK(parseErrors("f <| a + x |> g") == 1);
}

// fail/user_op_nonassoc: non-associative operators on one level need parentheses
TEST_CASE("Parser: fail/user_op_nonassoc", "[parser]") {
    CHECK(parseErrors("a <=> b <=> c") == 1);
    CHECK(parseErrors("a <<> b <<> c") == 1);
    CHECK(parseErrors("a <<> b < c") == 1);
    CHECK(parseErrors("a |=| b = c") == 1);
}

// fail/dict_arrow: -> isn't a key/value separator
TEST_CASE("Parser: fail/dict_arrow", "[parser]") {
    CHECK(parseErrors("[\"ada\" -> 36]") == 1);
    CHECK(parseErrors("[\"ada\" -> 36, \"alan\" -> 41]") == 2);
}

// fail/if_removed: if ( points at match
TEST_CASE("Parser: fail/if_removed", "[parser]") {
    CHECK(parseErrors("if (x < 0)") == 1);
    CHECK(parseErrors("if") == 0);
    CHECK(parseDump("if") == "if");
}

// fail/spacing_negative_end: <-1 at the end of a range is a flow token
TEST_CASE("Parser: fail/spacing_negative_end", "[parser]") {
    CHECK(parseErrors("[0, ..., <-1]") == 1);
    CHECK(parseErrors("[0, ..., < -1]") == 0);
}

// checks if an unexpected token produces an error expression
TEST_CASE("Parser: unexpected token produces ErrorExpr", "[parser]") {
    CHECK(parseKind("+") == ExprKind::Error);
    CHECK(parseKind("") == ExprKind::Error);
    CHECK(parseKind("...") == ExprKind::Error);
    CHECK(parseKind("> a") == ExprKind::Error);
    CHECK(parseErrors("f(a") > 0);
    CHECK(parseErrors("[a, b") > 0);
}

// checks that an expression's span covers the whole string
TEST_CASE("Parser: spans cover full expression", "[parser]") {
    for (std::string src : {"a + b", "f(x)[0]", "Point <- [1, 2]", "(+, 0)[xs]", "a < b <= c", "x |> f", "old p.x", "[0, ..., <n]"}) {
        SourceManager sm;
        sm.load_string(src);
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

// checks a match as a comprehension head, from the spec
TEST_CASE("Parser: scopes inside expressions", "[parser][expr]") {
    CHECK(parseDump("[match (i % 2) { 0 -> { x * 2 -| } _ -> { x -| } } : [i, x] <- enumerate(xs)]")
        == "(compr (match (% i 2) (0 -> (block (-| (* x 2)))) (_ -> (block (-| x)))) (in (pack i x) (call enumerate xs)))");
    CHECK(parseDump("(+, 0)[match (i % 2) { 0 -> { x * 2 -| } _ -> { x -| } } : [i, x] <- enumerate(xs)]")
        == "(fold (op +) 0 (compr (match (% i 2) (0 -> (block (-| (* x 2)))) (_ -> (block (-| x)))) (in (pack i x) (call enumerate xs))))");
}

// pass/count_pairs: the body of count_pairs from the spec
TEST_CASE("Parser: pass/count_pairs body", "[parser][stmt]") {
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

// pass/main_body: the body of main from the spec's reference examples
TEST_CASE("Parser: pass/main body", "[parser][stmt]") {
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

// fail/if_removed at statement level: one diagnostic, and the block after it doesn't cascade
TEST_CASE("Parser: fail/if_removed statement", "[parser][stmt]") {
    CHECK(stmtErrors("if (x < 0) { y <- 1; }") == 1);
    CHECK(stmtErrors("if (x < 0) { y <- 1; } z <- 2;") == 1);
}

// checks statement-level error recovery
TEST_CASE("Parser: statement error recovery", "[parser][stmt]") {
    CHECK(stmtErrors("x <- ; y <- 2;") == 1);
    CHECK(stmtErrors("{ x <- 1 }") == 1);
    CHECK(stmtDump("+;") == "(expr <error>)");
}

// checks functions: generics, refinements, wit obligations, operator names, externs, and nesting
TEST_CASE("Parser: functions", "[parser][decl]") {
    CHECK(stmtDump("let () -> main -> (code :: int) { 0 -| }") == "(fn-decl main () -> (code:int) (block (-| 0)))");
    CHECK(stmtDump("let (x :: int) -> inc -> (r :: int) { x + 1 -| }") == "(fn-decl inc (x:int) -> (r:int) (block (-| (+ x 1))))");
    CHECK(stmtDump("let (a :: int {a >= 0}, b :: int {b > 0}) -> divmod -> (q :: int, r :: int {0 <= r < b && a = q * b + r}) { [a / b, a % b] -| }")
        == "(fn-decl divmod (a:(refine int (chain a >= 0)) b:(refine int (chain b > 0))) -> (q:int r:(refine int (&& (chain 0 <= r < b) (= a (+ (* q b) r))))) (block (-| (pack (/ a b) (% a b)))))");
    CHECK(stmtDump("let [T :: Ord] (a :: T, b :: T) -> max -> (m :: T) { }") == "(fn-decl max [T::Ord] (a:T b:T) -> (m:T) (block))");
    CHECK(stmtDump("let [T, A :: Allocator & Default <- Global] () -> list_new -> (l :: List[T, A]) { }")
        == "(fn-decl list_new [T A::Allocator&Default<-Global] () -> (l:(List T A)) (block))");
    CHECK(stmtDump("let (a :: Vec2, b :: Vec2) -> + -> (r :: Vec2) { [a.x + b.x, a.y + b.y] -| }")
        == "(fn-decl + (a:Vec2 b:Vec2) -> (r:Vec2) (block (-| (pack (+ (. a x) (. b x)) (+ (. a y) (. b y))))))");
    CHECK(stmtDump("let (a :: Vec2) -> - -> (r :: Vec2) { [-a.x, -a.y] -| }") == "(fn-decl - (a:Vec2) -> (r:Vec2) (block (-| (pack (pre:- (. a x)) (pre:- (. a y))))))");
    CHECK(stmtDump("let (a :: Vec2, b :: Vec2) -> <+> -> (r :: Vec2) { a + b -| }") == "(fn-decl <+> (a:Vec2 b:Vec2) -> (r:Vec2) (block (-| (+ a b))))");
    CHECK(stmtDump("let (a :: T, b :: T) -> == -> (r :: bool) { }") == "(fn-decl == (a:T b:T) -> (r:bool) (block))");
    CHECK(stmtDump("let (g :: peek mut Graph) -> grow -> () wit {node_count(g) >= node_count(old g)} { }")
        == "(fn-decl grow (g:(peek (mut@value Graph))) -> () (wit (chain (call node_count g) >= (call node_count (old g)))) (block))");
    CHECK(stmtDump("let (fd :: i32, buf :: peek arr char, n :: i64u) -> write -> (r :: i64);") == "(fn-decl write (fd:i32 buf:(peek (arr char)) n:i64u) -> (r:i64) no-body)");
    CHECK(stmtDump("let (s :: peek Shape) -> area -> (a :: f64) { let (r :: f64) -> sq -> (o :: f64) { r * r -| } 0.0 -| }")
        == "(fn-decl area (s:(peek Shape)) -> (a:f64) (block (fn-decl sq (r:f64) -> (o:f64) (block (-| (* r r)))) (-| 0.0)))");
    CHECK(stmtErrors("let (a :: int) -> f -> (r :: int) { a -| }") == 0);
    CHECK(stmtErrors("let (a :: int) -> 5 -> (r :: int) { }") == 1);
    CHECK(stmtErrors("let (a :: int) -> && -> (r :: int) { }") == 1);
    CHECK(stmtErrors("let (a :: int) -> f -> r :: int { }") > 0);
}

// checks every form of type definition
TEST_CASE("Parser: type definitions", "[parser][decl]") {
    CHECK(stmtDump("type NodeId <- i64u;") == "(type NodeId newtype i64u)");
    CHECK(stmtDump("type Wrapper <- Foo[int];") == "(type Wrapper newtype (Foo int))");
    CHECK(stmtDump("type Percent <- int {p : 0 <= p <= 100};") == "(type Percent refinement (refine int p : (chain 0 <= p <= 100)))");
    CHECK(stmtDump("type Point <- [x :: f64, y :: f64];") == "(type Point product (record x:f64 y:f64))");
    CHECK(stmtDump("type Pos <- [x :: f64, y :: f64] {p : p.x >= 0.0};") == "(type Pos product (refine (record x:f64 y:f64) p : (chain (. p x) >= 0.0)))");
    CHECK(stmtDump("type Shape <- Circle[f64] | Rect[f64, f64];") == "(type Shape sum (Circle f64) (Rect f64 f64))");
    CHECK(stmtDump("type Dir <- N | E | S | W;") == "(type Dir enum N E S W)");
    CHECK(stmtDump("type Option[T] <- Some[T] | None;") == "(type Option [T] sum (Some T) None)");
    CHECK(stmtDump("type Ord[T] <- [ let (a :: peek T, b :: peek T) -> cmp -> (r :: int) ];")
        == "(type Ord [T] interface (fn-decl cmp (a:(peek T) b:(peek T)) -> (r:int) no-body))");
    CHECK(stmtDump("type Num[T] <- [ let (a :: T, b :: T) -> + -> (r :: T), let (a :: T) -> - -> (r :: T) ];")
        == "(type Num [T] interface (fn-decl + (a:T b:T) -> (r:T) no-body) (fn-decl - (a:T) -> (r:T) no-body))");
    CHECK(stmtDump("type Spaceship[T] <- [ let (a :: peek T, b :: peek T) -> <=> -> (r :: int) ];")
        == "(type Spaceship [T] interface (fn-decl <=> (a:(peek T) b:(peek T)) -> (r:int) no-body))");
    CHECK(stmtDump("type List[T, A :: Allocator <- Global] <- [data :: arr[A] mut T, count :: i64u] {l : l.count <= len(l.data) && written(l.data) == [0, ..., <l.count]};")
        == "(type List [T A::Allocator<-Global] product (refine (record data:(arr [A] (mut@elems T)) count:i64u) l : "
           "(&& (chain (. l count) <= (call len (. l data))) (== (call written (. l data)) (range 0 ... <(. l count))))))");
    CHECK(stmtDump("type Map[K :: Ord, V] <- [keys :: arr K, vals :: arr V];") == "(type Map [K::Ord V] product (record keys:(arr K) vals:(arr V)))");
    CHECK(stmtErrors("type Bad <- Foo | std.Bar;") == 1);
    CHECK(stmtErrors("type Bad <- Foo | Bar[3];") == 1);
    CHECK(stmtErrors("type Bad <- int") == 1);
}

// checks export and import lists, including field exports
TEST_CASE("Parser: modules", "[parser][decl]") {
    CHECK(stmtDump("export [List, list_new, push, Result[dist, prev]];") == "(export List list_new push (Result dist prev))");
    CHECK(stmtDump("export [Vec2, <+>, +];") == "(export Vec2 <+> +)");
    CHECK(stmtDump("import [List, push, len] <- list;") == "(import List push len <- list)");
    CHECK(stmtDump("import [Allocator] <- std.mem;") == "(import Allocator <- std.mem)");
    CHECK(stmtDump("import [Raw] <- std.unsafe;") == "(import Raw <- std.unsafe)");
    CHECK(stmtErrors("import [List] <- ;") > 0);
    CHECK(stmtErrors("export [1];") == 1);
}

// checks attributes, file-level tiers, and tier regions
TEST_CASE("Parser: attributes", "[parser][decl]") {
    CHECK(stmtDump("@tier(strict);") == "(@tier strict)");
    CHECK(stmtDump("@tier(strict) { x <- 1; }") == "(@tier strict (expr (block (<- x 1))))");
    CHECK(stmtDump("@extern(\"c\") let (fd :: i32) -> close -> (r :: i32);") == "(@extern \"c\" (fn-decl close (fd:i32) -> (r:i32) no-body))");
    CHECK(stmtDump("@global_allocator let heap <- Bump();") == "(@global_allocator (let heap <- (call Bump)))");
    CHECK(stmtDump("@a @b let x <- 1;") == "(@a (@b (let x <- 1)))");
    CHECK(stmtErrors("@ let x <- 1;") == 1);
}

// pass/reference: the spec's reference examples parse as a whole program
TEST_CASE("Parser: pass/reference program", "[parser][decl]") {
    const char* src =
        "type Percent <- int {p : 0 <= p <= 100};\n"
        "type Shape   <- Circle[f64] | Rect[f64, f64];\n"
        "type Vec2    <- [x :: f64, y :: f64];\n"
        "let (a :: Vec2, b :: Vec2) -> + -> (r :: Vec2) { [a.x + b.x, a.y + b.y] -| }\n"
        "let (a :: Vec2, b :: Vec2) -> <+> -> (r :: Vec2) { a + b -| }\n"
        "let (n :: int) -> clamp -> (p :: Percent) {\n"
        "    match (n) {\n"
        "        [..., <0]     -> { 0 -| }\n"
        "        [0, ..., 100] -> { n -| }        # Z3 proves 0 <= n <= 100: no cast needed\n"
        "        [>100, ...]   -> { 100 -| }\n"
        "    } -|\n"
        "}\n"
        "let (s :: peek Shape) -> area -> (a :: f64) {\n"
        "    match (s) {\n"
        "        Circle[r]  -> { 3.14159 * r * r -| }\n"
        "        Rect[w, h] -> { w * h -| }\n"
        "    } -|\n"
        "}\n"
        "let (a :: int {a >= 0}, b :: int {b > 0})\n"
        "    -> divmod\n"
        "    -> (q :: int, r :: int {0 <= r < b && a = q * b + r}) {\n"
        "    [a / b, a % b] -|\n"
        "}\n"
        "let (x :: bool, y :: bool) -> describe -> (code :: int) {\n"
        "    match ([x, y]) {\n"
        "        [true,  true ] -> { 3 -| }\n"
        "        [true,  false] -> { 2 -| }\n"
        "        [false, _    ] -> { 0 -| }\n"
        "    } -|\n"
        "}\n"
        "let (x :: int) -> inc -> (r :: int) { x + 1 -| }\n"
        "let (x :: int) -> double -> (r :: int) { x * 2 -| }\n"
        "let (acc :: [f64, f64, f64], x :: f64) -> moments -> (r :: [f64, f64, f64]) {\n"
        "    [acc[0] + 1.0, acc[1] + x, acc[2] + x * x] -|\n"
        "}\n";
    CHECK(stmtErrors(src) == 0);
    auto dump = programDump(src);
    CHECK(std::count(dump.begin(), dump.end(), '\n') == 11);
    CHECK(dump.find("(fn-decl moments (acc:(tuple f64 f64 f64) x:f64) -> (r:(tuple f64 f64 f64))") != std::string::npos);
}

// pass/module_header: a file with a tier, exports, imports, and an extern
TEST_CASE("Parser: pass/module header", "[parser][decl]") {
    const char* src =
        "@tier(strict);\n"
        "export [List, list_new, push, Result[dist, prev]];\n"
        "import [Allocator] <- std.mem;\n"
        "import [Raw] <- std.unsafe;\n"
        "@extern(\"c\") let (fd :: i32, buf :: peek arr char, n :: i64u) -> write -> (r :: i64);\n"
        "let LIMIT <- 4096;\n"
        "wit {Vec2 :: Ord};\n";
    CHECK(stmtErrors(src) == 0);
    CHECK(programDump(src) ==
        "(@tier strict)\n"
        "(export List list_new push (Result dist prev))\n"
        "(import Allocator <- std.mem)\n"
        "(import Raw <- std.unsafe)\n"
        "(@extern \"c\" (fn-decl write (fd:i32 buf:(peek (arr char)) n:i64u) -> (r:i64) no-body))\n"
        "(let LIMIT <- 4096)\n"
        "(wit Vec2 :: Ord)");
}
