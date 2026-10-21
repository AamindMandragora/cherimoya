#pragma once

#include <map>
#include <memory>
#include <unordered_map>
#include "type.hpp"

// the type context owns every sema type, and everything else refers to them by raw pointer
class TypeContext {
    std::vector<std::unique_ptr<SemaType>> types;
    std::map<std::vector<SemaType*>, FunctionSemaType*> functionTypes;

    // takes ownership of a new type and returns a pointer to it that stays valid as long as the context does
    template <typename T, typename... Args>
    T* make(Args&&... args) {
        auto type = std::make_unique<T>(std::forward<Args>(args)...);
        T* raw = type.get();
        types.push_back(std::move(type));
        return raw;
    }

    // makes all twenty integer types: every width, signed or unsigned (u), and trapping or wrapping (w), so i8, i8u, i8w, and i8uw up through i128uw
    std::unordered_map<std::string, IntSemaType*> makeInts() {
        std::unordered_map<std::string, IntSemaType*> made;
        for (unsigned bits : {8u, 16u, 32u, 64u, 128u}) {
            for (bool isSigned : {true, false}) {
                for (bool isWrapping : {false, true}) {
                    std::string name = "i" + std::to_string(bits) + (isSigned ? "" : "u") + (isWrapping ? "w" : "");
                    // i64 prints as int, its alias, since that's how it's almost always written
                    made[name] = make<IntSemaType>(bits, isSigned, isWrapping, name == "i64" ? "int" : name);
                }
            }
        }
        return made;
    }
public:
    // every integer type by the name it's written with, made before semaInt reads it, since members are made in the order they're declared
    std::unordered_map<std::string, IntSemaType*> table = makeInts();
    IntSemaType* semaInt = table.at("i64");
    PrimitiveSemaType* semaBool = make<PrimitiveSemaType>(PrimitiveKind::Bool);
    PrimitiveSemaType* semaFloat = make<PrimitiveSemaType>(PrimitiveKind::Float);
    PrimitiveSemaType* semaVoid = make<PrimitiveSemaType>(PrimitiveKind::Void);
    PrimitiveSemaType* semaNever = make<PrimitiveSemaType>(PrimitiveKind::Never);
    ErrorSemaType* semaError = make<ErrorSemaType>();

    // finds the integer type written with this name, or null if it isn't one
    IntSemaType* intType(std::string_view name) {
        auto it = table.find(std::string(name));
        return it == table.end() ? nullptr : it->second;
    }

    // interns a function type keyed by its parameter types followed by its return type
    FunctionSemaType* functionType(std::vector<SemaType*> signature) {
        auto it = functionTypes.find(signature);
        if (it != functionTypes.end()) return it->second;
        auto fn = make<FunctionSemaType>(std::vector<SemaType*>(signature.begin(), signature.end() - 1), signature.back());
        functionTypes.emplace(std::move(signature), fn);
        return fn;
    }
};
