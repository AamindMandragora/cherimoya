#pragma once

#include <memory>
#include <optional>
#include "token.hpp"
#include "diagnostics.hpp"
#include "expr.hpp"

enum class StmtKind {
    Assign,
    Expr,
    Yield,
    Let,
    Wit,
    Assert,
    Error
};

// a statement is a unit of code inside a scope, and every scope (block, match, repeat, chunk) is an expression
class Stmt {
public:
    StmtKind kind;
    Span span;
    Stmt(StmtKind k, Span s) : kind(k), span(s) {}
    virtual ~Stmt() = default;
};

// place <- value; or place op<- value; where op is the compound token if there is one
class AssignStmt : public Stmt {
public:
    std::unique_ptr<Expr> place;
    std::optional<Token> op;
    std::unique_ptr<Expr> value;
    AssignStmt(std::unique_ptr<Expr> p, std::optional<Token> o, std::unique_ptr<Expr> v, Span s) : Stmt(StmtKind::Assign, s), place(std::move(p)), op(o), value(std::move(v)) {}
};

// expr; or a scope on its own, which takes no ;
class ExprStmt : public Stmt {
public:
    std::unique_ptr<Expr> expr;
    ExprStmt(std::unique_ptr<Expr> e, Span s) : Stmt(StmtKind::Expr, s), expr(std::move(e)) {}
};

// value -| target, where both are optional and a missing target means the innermost scope
class YieldStmt : public Stmt {
public:
    std::unique_ptr<Expr> value;
    std::optional<Token> target;
    YieldStmt(std::unique_ptr<Expr> v, std::optional<Token> t, Span s) : Stmt(StmtKind::Yield, s), value(std::move(v)), target(t) {}
};

// let pattern :: type <- init; where init is a wildcard when uninitialized
class LetStmt : public Stmt {
public:
    std::unique_ptr<Pattern> pattern;
    std::unique_ptr<Type> type;
    std::unique_ptr<Expr> init;
    LetStmt(std::unique_ptr<Pattern> p, std::unique_ptr<Type> t, std::unique_ptr<Expr> i, Span s) : Stmt(StmtKind::Let, s), pattern(std::move(p)), type(std::move(t)), init(std::move(i)) {}
};

// wit {predicate}; or wit {T :: Interface}; where bound is the interface for the latter
class WitStmt : public Stmt {
public:
    std::unique_ptr<Expr> predicate;
    std::unique_ptr<Type> bound;
    WitStmt(std::unique_ptr<Expr> p, std::unique_ptr<Type> b, Span s) : Stmt(StmtKind::Wit, s), predicate(std::move(p)), bound(std::move(b)) {}
};

// assert (cond);
class AssertStmt : public Stmt {
public:
    std::unique_ptr<Expr> cond;
    AssertStmt(std::unique_ptr<Expr> c, Span s) : Stmt(StmtKind::Assert, s), cond(std::move(c)) {}
};

// error statements don't hold anything special
class ErrorStmt : public Stmt {
public:
    ErrorStmt(Span s) : Stmt(StmtKind::Error, s) {}
};
