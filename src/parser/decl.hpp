#pragma once

#include <memory>
#include <optional>
#include <vector>
#include "token.hpp"
#include "diagnostics.hpp"
#include "stmt.hpp"

enum class DeclKind {
    Fn,
    Type,
    Export,
    Import,
    Error
};

// a declaration introduces a name: a function, a type, or a module's exports and imports
class Decl {
public:
    DeclKind kind;
    Span span;
    Decl(DeclKind k, Span s) : kind(k), span(s) {}
    virtual ~Decl() = default;
};

// name :: bounds[0] & bounds[1] & ... <- fallback, where the fallback is a default type or comptime value
struct GenericParam {
    Token name;
    std::vector<std::unique_ptr<Type>> bounds;
    std::optional<GenericArg> fallback;
};

/*
let [generics] (inputs) -> name -> (outputs) wit {obligations} body
where name is an identifier or an overloadable operator, and body is null for externs and interface members
*/
class FnDecl : public Decl {
public:
    std::vector<GenericParam> generics;
    std::vector<Param> inputs;
    Token name;
    std::vector<Param> outputs;
    std::unique_ptr<Expr> obligations;
    std::unique_ptr<BlockExpr> body;
    FnDecl(std::vector<GenericParam> g, std::vector<Param> i, Token n, std::vector<Param> o, std::unique_ptr<Expr> ob, std::unique_ptr<BlockExpr> b, Span s) : Decl(DeclKind::Fn, s), generics(std::move(g)), inputs(std::move(i)), name(n), outputs(std::move(o)), obligations(std::move(ob)), body(std::move(b)) {}
};

// which form of type definition the right side is
enum class TypeForm {
    Newtype,     // type NodeId <- i64u;
    Refinement,  // type Percent <- int {p : 0 <= p <= 100};
    Product,     // type Point <- [x :: f64, y :: f64];  optionally with an invariant
    Sum,         // type Shape <- Circle[f64] | Rect[f64, f64];
    Enum,        // type Dir <- N | E | S | W;
    Interface,   // type Ord[T] <- [ let (a :: peek T, b :: peek T) -> cmp -> (r :: int) ];
};

// a sum's variant and its payload types: Rect[f64, f64]
struct Variant {
    Token name;
    std::vector<std::unique_ptr<Type>> payload;
};

/*
type name[generics] <- rhs;
where rhs is the type for newtypes, refinements, and products, the variants for sums and enums, and the members for interfaces
*/
class TypeDecl : public Decl {
public:
    Token name;
    std::vector<GenericParam> generics;
    TypeForm form;
    std::unique_ptr<Type> rhs;
    std::vector<Variant> variants;
    std::vector<std::unique_ptr<FnDecl>> members;
    TypeDecl(Token n, std::vector<GenericParam> g, TypeForm f, std::unique_ptr<Type> r, std::vector<Variant> v, std::vector<std::unique_ptr<FnDecl>> m, Span s) : Decl(DeclKind::Type, s), name(n), generics(std::move(g)), form(f), rhs(std::move(r)), variants(std::move(v)), members(std::move(m)) {}
};

// a name in an export or import list, with the fields that go with it: Result[dist, prev]
struct ModuleItem {
    Token name;
    std::vector<Token> fields;
};

// export [items];
class ExportDecl : public Decl {
public:
    std::vector<ModuleItem> items;
    ExportDecl(std::vector<ModuleItem> i, Span s) : Decl(DeclKind::Export, s), items(std::move(i)) {}
};

// import [items] <- path;
class ImportDecl : public Decl {
public:
    std::vector<ModuleItem> items;
    std::vector<Token> path;
    ImportDecl(std::vector<ModuleItem> i, std::vector<Token> p, Span s) : Decl(DeclKind::Import, s), items(std::move(i)), path(std::move(p)) {}
};

// error declarations don't hold anything special
class ErrorDecl : public Decl {
public:
    ErrorDecl(Span s) : Decl(DeclKind::Error, s) {}
};
