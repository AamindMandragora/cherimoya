#pragma once

#include <string>
#include <vector>

enum class SemaTypeKind {
    Primitive,
    Int,
    Float,
    Function,
    Error
};

// a sema type is what a value is known to be after checking, interned in the TypeContext so equal types are the same pointer
class SemaType {
public:
    SemaTypeKind kind;
    SemaType(SemaTypeKind k) : kind(k) {}
    virtual ~SemaType() = default;
    virtual std::string toString() const = 0;
};

// never is the type of a yield, since control doesn't continue past one
enum class PrimitiveKind {
    Bool,
    Void,
    Never
};

class PrimitiveSemaType : public SemaType {
public:
    PrimitiveKind primitive;
    PrimitiveSemaType(PrimitiveKind p) : SemaType(SemaTypeKind::Primitive), primitive(p) {}
    std::string toString() const override {
        switch (primitive) {
        case PrimitiveKind::Bool:
            return "bool";
        case PrimitiveKind::Void:
            return "void";
        case PrimitiveKind::Never:
            return "never";
        }
        return "unknown";
    }
};

// an integer type is its width, whether it's signed, and whether it wraps on overflow instead of trapping: i32 is 32 bits, signed, and trapping, and i8uw is 8 bits, unsigned, and wrapping
class IntSemaType : public SemaType {
public:
    unsigned bits;
    bool isSigned;
    bool isWrapping;
    // the name the type is written with, where i64 prints as int, the alias it's usually written as
    std::string name;
    IntSemaType(unsigned b, bool s, bool w, std::string n) : SemaType(SemaTypeKind::Int), bits(b), isSigned(s), isWrapping(w), name(std::move(n)) {}
    std::string toString() const override {
        return name;
    }
};

// the encoding a float type uses, which decides how codegen stores and computes with it: the two FP8 encodings, IEEE half, bfloat16, and IEEE single, double, and quad
enum class FloatFormat { E4M3, E5M2, Half, BFloat, Single, Double, Quad };

// a float type is its encoding and whether it's NaN-free: a NaN-free float traps on any operation that would make a NaN, and a NaN-carrying one, written with an n, follows IEEE and orders NaN below everything
class FloatSemaType : public SemaType {
public:
    FloatFormat format;
    bool isNanFree;
    // the significand's bits including the implicit leading one, and the largest finite value, which literals are checked against
    unsigned precision;
    long double max;
    // the name the type is written with, where f64 prints as float, its alias
    std::string name;
    FloatSemaType(FloatFormat f, bool nf, unsigned p, long double m, std::string n) : SemaType(SemaTypeKind::Float), format(f), isNanFree(nf), precision(p), max(m), name(std::move(n)) {}
    std::string toString() const override {
        return name;
    }
};

// the integer type a sema type is, or null if it isn't one, which is also how a missing type, like a subjectless match's, answers
inline IntSemaType* asInt(SemaType* type) {
    return type != nullptr && type->kind == SemaTypeKind::Int ? static_cast<IntSemaType*>(type) : nullptr;
}

// the float type a sema type is, or null if it isn't one
inline FloatSemaType* asFloat(SemaType* type) {
    return type != nullptr && type->kind == SemaTypeKind::Float ? static_cast<FloatSemaType*>(type) : nullptr;
}

// a function type is its parameters' types and the type it yields
class FunctionSemaType : public SemaType {
public:
    std::vector<SemaType*> params;
    SemaType* returns;
    FunctionSemaType(std::vector<SemaType*> p, SemaType* r) : SemaType(SemaTypeKind::Function), params(p), returns(r) {}
    std::string toString() const override {
        std::string out = "(";
        for (auto it = params.begin(); it != params.end(); it++) {
            out += (*it)->toString();
            if (it + 1 != params.end()) out += ", ";
        }
        out += ") : " + returns->toString();
        return out;
    }
};

// the error type poisons silently: anything built from it is also an error, and nothing reports twice
class ErrorSemaType : public SemaType {
public:
    ErrorSemaType() : SemaType(SemaTypeKind::Error) {}
    std::string toString() const override {
        return "error";
    }
};
