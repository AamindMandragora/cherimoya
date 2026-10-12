#pragma once

#include <map>
#include <memory>
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
public:
    PrimitiveSemaType* semaInt = make<PrimitiveSemaType>(PrimitiveKind::Int);
    PrimitiveSemaType* semaBool = make<PrimitiveSemaType>(PrimitiveKind::Bool);
    PrimitiveSemaType* semaFloat = make<PrimitiveSemaType>(PrimitiveKind::Float);
    PrimitiveSemaType* semaVoid = make<PrimitiveSemaType>(PrimitiveKind::Void);
    PrimitiveSemaType* semaNever = make<PrimitiveSemaType>(PrimitiveKind::Never);
    ErrorSemaType* semaError = make<ErrorSemaType>();

    // interns a function type keyed by its parameter types followed by its return type
    FunctionSemaType* functionType(std::vector<SemaType*> signature) {
        auto it = functionTypes.find(signature);
        if (it != functionTypes.end()) return it->second;
        auto fn = make<FunctionSemaType>(std::vector<SemaType*>(signature.begin(), signature.end() - 1), signature.back());
        functionTypes.emplace(std::move(signature), fn);
        return fn;
    }
};
