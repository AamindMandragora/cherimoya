#pragma once

#include <cstdint>
#include <string>
#include <vector>

enum class SemaTypeKind {
    Primitive,
    Int,
    Float,
    Array,
    Handle,
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

// a boolf is a fuzzy boolean, a truth value from 0 to 1, a char is one ASCII character, a rune is one Unicode scalar value, and never is the type of a yield, since control doesn't continue past one
enum class PrimitiveKind {
    Bool,
    Boolf,
    Char,
    Rune,
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
        case PrimitiveKind::Boolf:
            return "boolf";
        case PrimitiveKind::Char:
            return "char";
        case PrimitiveKind::Rune:
            return "rune";
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

// checks whether a sema type is one of the character types, char or rune
inline bool isCharacter(SemaType* type) {
    if (type == nullptr || type->kind != SemaTypeKind::Primitive) return false;
    auto primitive = static_cast<PrimitiveSemaType*>(type)->primitive;
    return primitive == PrimitiveKind::Char || primitive == PrimitiveKind::Rune;
}

// checks whether a sema type is boolf, the fuzzy boolean
inline bool isBoolf(SemaType* type) {
    return type != nullptr && type->kind == SemaTypeKind::Primitive && static_cast<PrimitiveSemaType*>(type)->primitive == PrimitiveKind::Boolf;
}

// a fixed array type is its element type and how many it holds, held inline like any value, so arr[3] int is three ints side by side, where whether its elements can be written is the binding's business, not the type's
class ArraySemaType : public SemaType {
public:
    SemaType* element;
    uint64_t size;
    ArraySemaType(SemaType* e, uint64_t n) : SemaType(SemaTypeKind::Array), element(e), size(n) {}
    std::string toString() const override {
        return "arr[" + std::to_string(size) + "] " + element->toString();
    }
};

// the array type a sema type is, or null if it isn't one
inline ArraySemaType* asArray(SemaType* type) {
    return type != nullptr && type->kind == SemaTypeKind::Array ? static_cast<ArraySemaType*>(type) : nullptr;
}

// a handle owns memory on the heap: an array's elements, as many as it was made with, or a pointer's one value, so arr int is the elements' address and how many there are, and ptr int is one int's address, where like a fixed array's, whether what it holds can be written is the binding's business
enum class HandleKind { Array, Pointer };
class HandleSemaType : public SemaType {
public:
    HandleKind handle;
    SemaType* element;
    HandleSemaType(HandleKind h, SemaType* e) : SemaType(SemaTypeKind::Handle), handle(h), element(e) {}
    std::string toString() const override {
        return (handle == HandleKind::Array ? "arr " : "ptr ") + element->toString();
    }
};

// the handle type a sema type is, or null if it isn't one, and the same for each kind of handle
inline HandleSemaType* asHandle(SemaType* type) {
    return type != nullptr && type->kind == SemaTypeKind::Handle ? static_cast<HandleSemaType*>(type) : nullptr;
}
inline HandleSemaType* asHeapArray(SemaType* type) {
    HandleSemaType* handle = asHandle(type);
    return handle && handle->handle == HandleKind::Array ? handle : nullptr;
}
inline HandleSemaType* asPointer(SemaType* type) {
    HandleSemaType* handle = asHandle(type);
    return handle && handle->handle == HandleKind::Pointer ? handle : nullptr;
}

// what a type holds one level in, which mut can make writable: a fixed or heap array's elements, or a pointer's value, or null for a type that holds nothing
inline SemaType* elementOf(SemaType* type) {
    if (ArraySemaType* array = asArray(type)) return array->element;
    if (HandleSemaType* handle = asHandle(type)) return handle->element;
    return nullptr;
}

// checks whether a value of a type owns memory, which has to be freed exactly once, so it moves instead of copying: a handle, or a fixed array of anything that owns
inline bool owns(SemaType* type) {
    if (asHandle(type)) return true;
    if (ArraySemaType* array = asArray(type)) return owns(array->element);
    return false;
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
