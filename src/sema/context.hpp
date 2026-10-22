#pragma once

#include <limits>
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
        made["int"] = made.at("i64");
        return made;
    }

    // makes all fourteen float types, each encoding NaN-free and NaN-carrying (n), so f8e4m3, f8e4m3n, f8e5m2, and f8e5m2n up through f128n
    std::unordered_map<std::string, FloatSemaType*> makeFloats() {
        // each encoding's name, the alias its NaN-free type prints as, if it has one, its precision, and its largest finite value, where f128's is long double's, which shares its range but holds a hair less
        struct Row { const char* name; const char* alias; FloatFormat format; unsigned precision; long double max; };
        const Row rows[] = {
            {"f8e4m3", "f8", FloatFormat::E4M3, 4, 448.0L},
            {"f8e5m2", nullptr, FloatFormat::E5M2, 3, 57344.0L},
            {"f16", nullptr, FloatFormat::Half, 11, 65504.0L},
            {"f16e8m7", nullptr, FloatFormat::BFloat, 8, 3.38953138925153547590e38L},
            {"f32", nullptr, FloatFormat::Single, 24, 3.40282346638528859812e38L},
            {"f64", "float", FloatFormat::Double, 53, 1.79769313486231570815e308L},
            {"f128", nullptr, FloatFormat::Quad, 113, std::numeric_limits<long double>::max()},
        };
        std::unordered_map<std::string, FloatSemaType*> made;
        for (const Row& row : rows) {
            std::string name = row.name;
            // an alias names only the NaN-free type, so f8 is f8e4m3 and the NaN-carrying one is only ever f8e4m3n
            made[name] = make<FloatSemaType>(row.format, true, row.precision, row.max, row.alias ? row.alias : name);
            if (row.alias) made[row.alias] = made.at(name);
            made[name + "n"] = make<FloatSemaType>(row.format, false, row.precision, row.max, name + "n");
        }
        return made;
    }
public:
    // every integer type by the name it's written with, int included, made before semaInt reads it, since members are made in the order they're declared
    std::unordered_map<std::string, IntSemaType*> ints = makeInts();
    IntSemaType* semaInt = ints.at("int");
    PrimitiveSemaType* semaBool = make<PrimitiveSemaType>(PrimitiveKind::Bool);
    // every float type by the name it's written with, float included
    std::unordered_map<std::string, FloatSemaType*> floats = makeFloats();
    FloatSemaType* semaFloat = floats.at("float");
    PrimitiveSemaType* semaVoid = make<PrimitiveSemaType>(PrimitiveKind::Void);
    PrimitiveSemaType* semaNever = make<PrimitiveSemaType>(PrimitiveKind::Never);
    ErrorSemaType* semaError = make<ErrorSemaType>();

    // finds the integer type written with this name, or null if it isn't one
    IntSemaType* intType(std::string_view name) {
        auto it = ints.find(std::string(name));
        return it == ints.end() ? nullptr : it->second;
    }

    // finds the float type written with this name, or null if it isn't one
    FloatSemaType* floatType(std::string_view name) {
        auto it = floats.find(std::string(name));
        return it == floats.end() ? nullptr : it->second;
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
