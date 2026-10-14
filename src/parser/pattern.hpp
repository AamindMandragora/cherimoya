#pragma once

#include <memory>
#include <optional>
#include <vector>
#include "token.hpp"
#include "diagnostics.hpp"

class Expr;
struct Symbol;

enum class PatternKind {
    Literal,
    Wildcard,
    Binder,
    Variant,
    Pack,
    Range,
    SetBuilder,
    Guard,
    Error
};

// a pattern is what a value gets matched against: match arms, let destructuring, loop binders, and membership clauses
class Pattern {
public:
    PatternKind kind;
    Span span;
    Pattern(PatternKind k, Span s) : kind(k), span(s) {}
    virtual ~Pattern() = default;
};

// literal patterns hold the literal expression, including a negated number: 0, 'a', -1
class LiteralPattern : public Pattern {
public:
    std::unique_ptr<Expr> literal;
    LiteralPattern(std::unique_ptr<Expr> l, Span s) : Pattern(PatternKind::Literal, s), literal(std::move(l)) {}
};

// wildcard patterns match anything and bind nothing: _
class WildcardPattern : public Pattern {
public:
    WildcardPattern(Span s) : Pattern(PatternKind::Wildcard, s) {}
};

// binder patterns hold a bare name, which sema resolves to a payload-free variant like None or else binds as a new name, filling in its symbol
class BinderPattern : public Pattern {
public:
    Token name;
    Symbol* decl = nullptr;
    BinderPattern(Token n, Span s) : Pattern(PatternKind::Binder, s), name(n) {}
};

// variant patterns hold the variant's name and the patterns for its payload: Some[d], Rect[w, h]
class VariantPattern : public Pattern {
public:
    Token name;
    std::vector<std::unique_ptr<Pattern>> payload;
    VariantPattern(Token n, std::vector<std::unique_ptr<Pattern>> p, Span s) : Pattern(PatternKind::Variant, s), name(n), payload(std::move(p)) {}
};

// a pack pattern element, optionally named for records: the x in [x <- 0, y <- py]
struct PackPatternElem {
    std::optional<Token> name;
    std::unique_ptr<Pattern> pattern;
};

// pack patterns hold their elements, covering tuples, decision tables, and records: [true, _], [x <- 0, y <- py]
class PackPattern : public Pattern {
public:
    std::vector<PackPatternElem> elems;
    PackPattern(std::vector<PackPatternElem> e, Span s) : Pattern(PatternKind::Pack, s), elems(std::move(e)) {}
};

// a range pattern element is ... (no value) or an endpoint with an optional exclusivity marker
struct RangePatternElem {
    std::unique_ptr<Expr> value;
    std::optional<Token> marker;
};

// range patterns hold their raw elements like range expressions do: [0, ..., 100], [..., <0], [>100, ...]
class RangePattern : public Pattern {
public:
    std::vector<RangePatternElem> elems;
    RangePattern(std::vector<RangePatternElem> e, Span s) : Pattern(PatternKind::Range, s), elems(std::move(e)) {}
};

// set-builder patterns hold the binder and the predicates it must satisfy: [x : x % 2 = 0]
class SetBuilderPattern : public Pattern {
public:
    Token binder;
    std::vector<std::unique_ptr<Expr>> predicates;
    SetBuilderPattern(Token b, std::vector<std::unique_ptr<Expr>> p, Span s) : Pattern(PatternKind::SetBuilder, s), binder(b), predicates(std::move(p)) {}
};

// guard patterns are the (condition) arms of a subjectless match
class GuardPattern : public Pattern {
public:
    std::unique_ptr<Expr> cond;
    GuardPattern(std::unique_ptr<Expr> c, Span s) : Pattern(PatternKind::Guard, s), cond(std::move(c)) {}
};

// error patterns don't hold anything special
class ErrorPattern : public Pattern {
public:
    ErrorPattern(Span s) : Pattern(PatternKind::Error, s) {}
};
