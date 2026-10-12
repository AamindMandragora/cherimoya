#pragma once

#include <map>
#include "type.hpp"

// the type context owns every sema type, and everything else refers to them by raw pointer
class TypeContext {
    std::vector<std::unique_ptr<SemaType>> types;
    std::map<std::vector<SemaType*>, FunctionSemaType*> map;

    // takes ownership of a new type and returns a pointer to it that stays valid as long as the context does
    template <typename T, typename... Args>
    T* make(Args&&... args) {
        auto type = std::make_unique<T>(std::forward<Args>(args)...);
        T* raw = type.get();
        types.push_back(std::move(type));
        return raw;
    }
public:
    PrimitiveSemaType* sema_int = make<PrimitiveSemaType>(PrimitiveKind::Int);
    PrimitiveSemaType* sema_bool = make<PrimitiveSemaType>(PrimitiveKind::Bool);
    PrimitiveSemaType* sema_f64 = make<PrimitiveSemaType>(PrimitiveKind::F64);
    PrimitiveSemaType* sema_void = make<PrimitiveSemaType>(PrimitiveKind::Void);
    PrimitiveSemaType* sema_never = make<PrimitiveSemaType>(PrimitiveKind::Never);
    ErrorSemaType* sema_error = make<ErrorSemaType>();

    // interns a function type keyed by its parameter types followed by its return type
    FunctionSemaType* get(std::vector<SemaType*> signature) {
        auto it = map.find(signature);
        if (it != map.end()) return it->second;
        auto fn = make<FunctionSemaType>(std::vector<SemaType*>(signature.begin(), signature.end() - 1), signature.back());
        map.emplace(std::move(signature), fn);
        return fn;
    }
};
