#pragma once

#include <memory>
#include <optional>
#include <string>
#include <vector>
#include "token.hpp"
#include "diagnostics.hpp"
#include "escape.hpp"
#include "type.hpp"
#include "pattern.hpp"

class Stmt;
class SemaType;
struct Symbol;

// every kind of expression, grouped as what's left after an error, names and literals, operators, postfix forms, packs, types in value position, and closures and scopes
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

    Ann,
    TypeExpr,

    Closure,
    Block,
    Match,
    Repeat,
    Chunk,
};

// an expression holds its span and the kind of expression it is, and sema fills in its type
class Expr {
public:
    ExprKind kind;
    Span span;
    SemaType* type = nullptr;
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

// a number literal's text with its prefix read off and its separators taken out, which sema and codegen both read values from: 0x1_F is radix 16 and digits 1F, and a float keeps its point
struct LiteralDigits {
    unsigned radix;
    std::string digits;
};
inline LiteralDigits literalDigits(std::string_view literal) {
    unsigned radix = 10;
    if (literal.size() > 2 && literal[0] == '0') {
        char prefix = literal[1] | 0x20;
        if (prefix == 'x') radix = 16;
        if (prefix == 'o') radix = 8;
        if (prefix == 'b') radix = 2;
        if (radix != 10) literal.remove_prefix(2);
    }
    std::string digits;
    for (char c : literal) if (c != '_') digits.push_back(c);
    return {radix, digits};
}

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

// the code point a char literal stands for, which sema and codegen both read, where the lexer already made sure it holds exactly one character
inline uint32_t charLiteralValue(std::string_view literal) {
    return decodeCharacter(literal.substr(1, literal.size() - 2)).value;
}

// identifiers hold their name as well, and sema fills in the symbol it resolves to
class IdentifierExpr : public Expr {
public:
    std::string_view name;
    Symbol* decl = nullptr;
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
    std::unique_ptr<Pattern> pattern;
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

// annotations hold an expression and the type it's treated as: (p :: Ord)
class AnnExpr : public Expr {
public:
    std::unique_ptr<Expr> expr;
    std::unique_ptr<Type> annotation;
    AnnExpr(std::unique_ptr<Expr> e, std::unique_ptr<Type> a, Span s) : Expr(ExprKind::Ann, s), expr(std::move(e)), annotation(std::move(a)) {}
};

// type expressions are a type in value position, which then gets called: the i32 in i32(x), the arr mut T in arr mut T(n)
class TypeExprExpr : public Expr {
public:
    std::unique_ptr<Type> typeExpr;
    TypeExprExpr(std::unique_ptr<Type> t, Span s) : Expr(ExprKind::TypeExpr, s), typeExpr(std::move(t)) {}
};

/*
name {
    stmts[0]
    stmts[1]
    ...
    stmts[stmts.size() - 1]
}
*/
class BlockExpr : public Expr {
public:
    std::optional<Token> name;
    std::vector<std::unique_ptr<Stmt>> stmts;
    BlockExpr(std::optional<Token> n, std::vector<std::unique_ptr<Stmt>> st, Span s) : Expr(ExprKind::Block, s), name(n), stmts(std::move(st)) {}
};

// closures hold their owning captures, their parameters, their outputs if annotated, and their body: [captures] (params) -> (outputs) { body }
class ClosureExpr : public Expr {
public:
    std::vector<std::unique_ptr<Expr>> captures;
    std::vector<Param> params;
    std::optional<std::vector<Param>> outputs;
    std::unique_ptr<BlockExpr> body;
    ClosureExpr(std::vector<std::unique_ptr<Expr>> c, std::vector<Param> p, std::optional<std::vector<Param>> o, std::unique_ptr<BlockExpr> b, Span s) : Expr(ExprKind::Closure, s), captures(std::move(c)), params(std::move(p)), outputs(std::move(o)), body(std::move(b)) {}
};

// pattern -> { body }
struct MatchArm {
    std::unique_ptr<Pattern> pattern;
    std::unique_ptr<BlockExpr> body;
    Span span;
};

/*
match name (equality, subject) {
    arms[0]
    arms[1]
    ...
    arms[arms.size() - 1]
}
where equality is = for shallow pattern comparisons, == or nothing for deep, and both it and subject are optional
*/
class MatchExpr : public Expr {
public:
    std::optional<Token> name;
    std::optional<Token> equality;
    std::unique_ptr<Expr> subject;
    std::vector<MatchArm> arms;
    MatchExpr(std::optional<Token> n, std::optional<Token> eq, std::unique_ptr<Expr> su, std::vector<MatchArm> a, Span s) : Expr(ExprKind::Match, s), name(n), equality(eq), subject(std::move(su)), arms(std::move(a)) {}
};

// repeat name (binder) <- source wit {invariant} body, where a binder means for-each, a cond alone means while, and neither means infinite
class RepeatExpr : public Expr {
public:
    std::optional<Token> name;
    std::unique_ptr<Pattern> binder;
    std::unique_ptr<Expr> source;
    std::unique_ptr<Expr> cond;
    std::unique_ptr<Expr> invariant;
    std::unique_ptr<BlockExpr> body;
    RepeatExpr(std::optional<Token> n, std::unique_ptr<Pattern> b, std::unique_ptr<Expr> so, std::unique_ptr<Expr> c, std::unique_ptr<Expr> i, std::unique_ptr<BlockExpr> bo, Span s) : Expr(ExprKind::Repeat, s), name(n), binder(std::move(b)), source(std::move(so)), cond(std::move(c)), invariant(std::move(i)), body(std::move(bo)) {}
};

// chunk name body, a block whose allocations default to a bump region
class ChunkExpr : public Expr {
public:
    std::optional<Token> name;
    std::unique_ptr<BlockExpr> body;
    ChunkExpr(std::optional<Token> n, std::unique_ptr<BlockExpr> b, Span s) : Expr(ExprKind::Chunk, s), name(n), body(std::move(b)) {}
};

// error expressions don't hold anything special
class ErrorExpr : public Expr {
public:
    ErrorExpr(Span s) : Expr(ExprKind::Error, s) {}
};
