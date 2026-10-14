#pragma once

#include <memory>
#include <string>
#include <vector>
#include "diagnostics.hpp"

enum class SemaTypeKind {
    Primitive,
    Function,
    Error
};

// a sema type is what a value is known to be after checking, interned in the TypeContext so equal types are the same pointer
class SemaType {
public:
    SemaTypeKind kind;
    SemaType(SemaTypeKind k) : kind(k) {}
    virtual ~SemaType() = default;
    virtual std::string to_string() const = 0;
};

// never is the type of a yield, since control doesn't continue past one
enum class PrimitiveKind {
    Int,
    Bool,
    Float,
    Void,
    Never
};

class PrimitiveSemaType : public SemaType {
public:
    PrimitiveKind primitive;
    PrimitiveSemaType(PrimitiveKind p) : SemaType(SemaTypeKind::Primitive), primitive(p) {}
    std::string to_string() const override {
        switch (primitive) {
        case PrimitiveKind::Int:
            return "int";
        case PrimitiveKind::Bool:
            return "bool";
        case PrimitiveKind::Float:
            return "float";
        case PrimitiveKind::Void:
            return "void";
        case PrimitiveKind::Never:
            return "never";
        default:
            return "unimplemented";
        }
    }
};

class FunctionSemaType : public SemaType {
public:
    std::vector<SemaType*> params;
    SemaType* returns;
    FunctionSemaType(std::vector<SemaType*> p, SemaType* r) : SemaType(SemaTypeKind::Function), params(p), returns(r) {}
    std::string to_string() const override {
        std::string out = "(";
        for (auto it = params.begin(); it != params.end(); it++) {
            out += (*it)->to_string();
            if (it + 1 != params.end()) out += ", ";
        }
        out += ") : " + returns->to_string();
        return out;
    }
};

// the error type poisons silently: anything built from it is also an error, and nothing reports twice
class ErrorSemaType : public SemaType {
public:
    ErrorSemaType() : SemaType(SemaTypeKind::Error) {}
    std::string to_string() const override {
        return "error";
    }
};
