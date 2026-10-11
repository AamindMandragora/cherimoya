#pragma once

#include <memory>
#include <optional>
#include <vector>
#include "token.hpp"
#include "diagnostics.hpp"

class Expr;

enum class TypeKind {
    Named,
    Qualified,
    Handle,
    Mode,
    Tuple,
    Record,
    Refined,
    Error
};

// a type expression, as written in annotations, signatures, and type definitions
class Type {
public:
    TypeKind kind;
    Span span;
    Type(TypeKind k, Span s) : kind(k), span(s) {}
    virtual ~Type() = default;
};

// a generic argument is either a type or a comptime value: the int in List[int], the 16 in arr[16] T
struct GenericArg {
    std::unique_ptr<Type> type;
    std::unique_ptr<Expr> value;
};

// named types hold their dotted path and any generic arguments: std.mem.Allocator, Map[K, V]
class NamedType : public Type {
public:
    std::vector<Token> path;
    std::vector<GenericArg> args;
    NamedType(std::vector<Token> p, std::vector<GenericArg> a, Span s) : Type(TypeKind::Named, s), path(std::move(p)), args(std::move(a)) {}
};

// where a qualifier sits decides what it applies to: mut arr T rebinds the handle, arr mut T mutates the elements, mut T is a mutable value
enum class QualPosition { Handle, Elements, Value };

// qualified types hold mut, where it sits, and the type it applies to, which is null for a bare qualifier like let x :: mut <- 5;
class QualifiedType : public Type {
public:
    Token qualifier;
    QualPosition position;
    std::unique_ptr<Type> inner;
    QualifiedType(Token q, QualPosition p, std::unique_ptr<Type> i, Span s) : Type(TypeKind::Qualified, s), qualifier(q), position(p), inner(std::move(i)) {}
};

// handles hold ptr or arr, the optional size or allocator in brackets, and the element type: arr[16] T, arr[Chunk] T, ptr T
class HandleType : public Type {
public:
    Token handle;
    std::optional<GenericArg> arg;
    std::unique_ptr<Type> inner;
    HandleType(Token h, std::optional<GenericArg> a, std::unique_ptr<Type> i, Span s) : Type(TypeKind::Handle, s), handle(h), arg(std::move(a)), inner(std::move(i)) {}
};

// modes hold unscoped, peek, or wit and the type they apply to, which is null when the type is inferred: let buf :: unscoped <- ...;
class ModeType : public Type {
public:
    Token mode;
    std::unique_ptr<Type> inner;
    ModeType(Token m, std::unique_ptr<Type> i, Span s) : Type(TypeKind::Mode, s), mode(m), inner(std::move(i)) {}
};

// tuple types hold their element types: [int, f64]
class TupleType : public Type {
public:
    std::vector<std::unique_ptr<Type>> elems;
    TupleType(std::vector<std::unique_ptr<Type>> e, Span s) : Type(TypeKind::Tuple, s), elems(std::move(e)) {}
};

// name :: type, used for record fields and for parameters, where the type is null when a closure leaves it to inference
struct Param {
    Token name;
    std::unique_ptr<Type> type;
};

// record types hold their named fields: [x :: f64, y :: f64]
class RecordType : public Type {
public:
    std::vector<Param> fields;
    RecordType(std::vector<Param> f, Span s) : Type(TypeKind::Record, s), fields(std::move(f)) {}
};

// refined types hold the base type, the binder if one is named, and the predicate: int {p : 0 <= p <= 100}, int {a >= 0}
class RefinedType : public Type {
public:
    std::unique_ptr<Type> inner;
    std::optional<Token> binder;
    std::unique_ptr<Expr> predicate;
    RefinedType(std::unique_ptr<Type> i, std::optional<Token> b, std::unique_ptr<Expr> p, Span s) : Type(TypeKind::Refined, s), inner(std::move(i)), binder(b), predicate(std::move(p)) {}
};

// error types don't hold anything special
class ErrorType : public Type {
public:
    ErrorType(Span s) : Type(TypeKind::Error, s) {}
};
