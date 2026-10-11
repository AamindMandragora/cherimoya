#pragma once

#include <memory>
#include <optional>
#include <vector>
#include "token.hpp"
#include "diagnostics.hpp"

// every kind of expression, grouped as what's left after an error, names and literals, operators, postfix forms, and packs
enum class ExprKind {
    Error,

    IntLiteral,
    FloatLiteral,
    BoolLiteral,
    StringLiteral,
    CharLiteral,
    Identifier,
    Wildcard,

    Unary,
    Binary,
    ExtraBinary,
    Chain,
    Pipe,
    OpRef,
    Old,

    Call,
    Index,
    Member,
    Construct,

    Pack,
    Range,
    Compr,
    Fold,
};

// an expression holds its span and the kind of expression it is
class Expr {
public:
    ExprKind kind;
    Span span;
    Expr(ExprKind k, Span s) : kind(k), span(s) {}
    virtual ~Expr() = default;
};

// literal expressions hold their literal value (in string_view form or bool) as well
class IntLiteralExpr : public Expr {
public:
    std::string_view literal;
    IntLiteralExpr(std::string_view l, Span s) : Expr(ExprKind::IntLiteral, s), literal(l) {}
};
class FloatLiteralExpr : public Expr {
public:
    std::string_view literal;
    FloatLiteralExpr(std::string_view l, Span s) : Expr(ExprKind::FloatLiteral, s), literal(l) {}
};
class BoolLiteralExpr : public Expr {
public:
    bool literal;
    BoolLiteralExpr(bool l, Span s) : Expr(ExprKind::BoolLiteral, s), literal(l) {}
};
class StringLiteralExpr : public Expr {
public:
    std::string_view literal;
    StringLiteralExpr(std::string_view l, Span s) : Expr(ExprKind::StringLiteral, s), literal(l) {}
};
class CharLiteralExpr : public Expr {
public:
    std::string_view literal;
    CharLiteralExpr(std::string_view l, Span s) : Expr(ExprKind::CharLiteral, s), literal(l) {}
};

// identifiers hold their name as well
class IdentifierExpr : public Expr {
public:
    std::string_view name;
    IdentifierExpr(std::string_view n, Span s) : Expr(ExprKind::Identifier, s), name(n) {}
};

// wildcards are a lone _, which means uninitialized or discard depending on where it is
class WildcardExpr : public Expr {
public:
    WildcardExpr(Span s) : Expr(ExprKind::Wildcard, s) {}
};

// unary expressions hold their prefix operation and the operand
class UnaryExpr : public Expr {
public:
    Token op;
    std::unique_ptr<Expr> operand;
    UnaryExpr(Token o, std::unique_ptr<Expr> e, Span s) : Expr(ExprKind::Unary, s), op(o), operand(std::move(e)) {}
};

// binary expressions hold their built-in operation and both left and right hand operands
class BinaryExpr : public Expr {
public:
    Token op;
    std::unique_ptr<Expr> lhs;
    std::unique_ptr<Expr> rhs;
    BinaryExpr(Token o, std::unique_ptr<Expr> l, std::unique_ptr<Expr> r, Span s) : Expr(ExprKind::Binary, s), op(o), lhs(std::move(l)), rhs(std::move(r)) {}
};

// extra binary operations hold their extra operator and both operands, and only mean something once a user defines them
class ExtraBinaryExpr : public Expr {
public:
    Token op;
    std::unique_ptr<Expr> lhs;
    std::unique_ptr<Expr> rhs;
    ExtraBinaryExpr(Token o, std::unique_ptr<Expr> l, std::unique_ptr<Expr> r, Span s) : Expr(ExprKind::ExtraBinary, s), op(o), lhs(std::move(l)), rhs(std::move(r)) {}
};

// chains hold every operand and the comparisons between them: a < b <= c has operands a b c and ops < <=
class ChainExpr : public Expr {
public:
    std::vector<std::unique_ptr<Expr>> operands;
    std::vector<Token> ops;
    ChainExpr(std::vector<std::unique_ptr<Expr>> o, std::vector<Token> p, Span s) : Expr(ExprKind::Chain, s), operands(std::move(o)), ops(std::move(p)) {}
};

// pipes hold their direction token, and the value and function in source order: x |> f and f <| x
class PipeExpr : public Expr {
public:
    Token op;
    std::unique_ptr<Expr> lhs;
    std::unique_ptr<Expr> rhs;
    PipeExpr(Token o, std::unique_ptr<Expr> l, std::unique_ptr<Expr> r, Span s) : Expr(ExprKind::Pipe, s), op(o), lhs(std::move(l)), rhs(std::move(r)) {}
};

// operator references are a bare operator used as a fold reducer: the + in (+, 0)[xs]
class OpRefExpr : public Expr {
public:
    Token op;
    OpRefExpr(Token o, Span s) : Expr(ExprKind::OpRef, s), op(o) {}
};

// old expressions hold the expression whose value on entry they mean, like the p.x in old p.x
class OldExpr : public Expr {
public:
    std::unique_ptr<Expr> operand;
    OldExpr(std::unique_ptr<Expr> e, Span s) : Expr(ExprKind::Old, s), operand(std::move(e)) {}
};

// call expressions hold the expression that called them and the arguments being passed in
class CallExpr : public Expr {
public:
    std::unique_ptr<Expr> callee;
    std::vector<std::unique_ptr<Expr>> args;
    CallExpr(std::unique_ptr<Expr> c, std::vector<std::unique_ptr<Expr>> a, Span s) : Expr(ExprKind::Call, s), callee(std::move(c)), args(std::move(a)) {}
};

// index expressions hold the object and everything in the brackets: no args is a dereference, a range is a slice, and sema decides whether the rest is an index or an instantiation
class IndexExpr : public Expr {
public:
    std::unique_ptr<Expr> object;
    std::vector<std::unique_ptr<Expr>> args;
    IndexExpr(std::unique_ptr<Expr> o, std::vector<std::unique_ptr<Expr>> a, Span s) : Expr(ExprKind::Index, s), object(std::move(o)), args(std::move(a)) {}
};

// member expressions hold the object and its field being accessed
class MemberExpr : public Expr {
public:
    std::unique_ptr<Expr> object;
    std::string_view field;
    MemberExpr(std::unique_ptr<Expr> o, std::string_view f, Span s) : Expr(ExprKind::Member, s), object(std::move(o)), field(f) {}
};

// constructions hold the type or variant being built and the pack it's built from: T <- [pack]
class ConstructExpr : public Expr {
public:
    std::unique_ptr<Expr> target;
    std::unique_ptr<Expr> pack;
    ConstructExpr(std::unique_ptr<Expr> t, std::unique_ptr<Expr> p, Span s) : Expr(ExprKind::Construct, s), target(std::move(t)), pack(std::move(p)) {}
};

// a pack element is a value, optionally named: the x in [x <- 1.0]
struct PackElem {
    std::optional<Token> name;
    std::unique_ptr<Expr> value;
};

// packs hold their elements in order
class PackExpr : public Expr {
public:
    std::vector<PackElem> elems;
    PackExpr(std::vector<PackElem> e, Span s) : Expr(ExprKind::Pack, s), elems(std::move(e)) {}
};

// a range element is either ... (no value) or a value, and an endpoint's exclusivity marker is a comparison on the bound: >0 means stay above 0
struct RangeElem {
    std::unique_ptr<Expr> value;
    std::optional<Token> marker;
};

// ranges hold their raw elements, so the stride stays as the first two values for sema to work out
class RangeExpr : public Expr {
public:
    std::vector<RangeElem> elems;
    RangeExpr(std::vector<RangeElem> e, Span s) : Expr(ExprKind::Range, s), elems(std::move(e)) {}
};

// a comprehension clause is a membership clause (pattern <- source) or a filter clause (no pattern)
struct ComprClause {
    std::unique_ptr<Expr> pattern;
    std::unique_ptr<Expr> expr;
};

// comprehensions hold their head and the clauses after the colon: [head : clause && clause]
class ComprExpr : public Expr {
public:
    std::unique_ptr<Expr> head;
    std::vector<ComprClause> clauses;
    ComprExpr(std::unique_ptr<Expr> h, std::vector<ComprClause> c, Span s) : Expr(ExprKind::Compr, s), head(std::move(h)), clauses(std::move(c)) {}
};

// folds hold the reducer, the seed, and the sequence: (reducer, seed)[sequence]
class FoldExpr : public Expr {
public:
    std::unique_ptr<Expr> reducer;
    std::unique_ptr<Expr> seed;
    std::unique_ptr<Expr> sequence;
    FoldExpr(std::unique_ptr<Expr> r, std::unique_ptr<Expr> d, std::unique_ptr<Expr> q, Span s) : Expr(ExprKind::Fold, s), reducer(std::move(r)), seed(std::move(d)), sequence(std::move(q)) {}
};

// error expressions don't hold anything special
class ErrorExpr : public Expr {
public:
    ErrorExpr(Span s) : Expr(ExprKind::Error, s) {}
};
