#pragma once

#include <optional>
#include <string_view>
#include <unordered_map>
#include <vector>
#include "diagnostics.hpp"
#include "type.hpp"

class FnDecl;

enum class SymbolKind {
    Binding,
    Param,
    Function
};

// a symbol is one declared name, physically owned by sema so pointers to it never dangle, owner numbers the function body that declared it
struct Symbol {
    std::string_view name;
    SymbolKind kind;
    SemaType* type;
    // what of it can be written, the binding itself first and then each level of an array's elements, outermost first, so replacing it checks mutability[0], and writing an element d levels in checks mutability[d]
    std::vector<bool> mutability;
    Span span;
    size_t owner;
    // the span of the lexical scope it's declared in, which is where it can be named, for anything but a function only after its declaration
    Span scope;
    // the declaration a function symbol comes from, or null for anything else
    const FnDecl* function;

    // checks whether the binding itself can be replaced
    bool isMutable() const { return mutability[0]; }
};

// a lexical scope maps names to the symbols declared in it, and falls back to its parent for everything else, and knows the integer every constant let in it folds to from the start, the way it knows its functions
class Scope {
    DiagnosticEngine& engine;
    std::unordered_map<std::string_view, Symbol*> symbols;
    std::unordered_map<std::string_view, uint64_t> constants;
    Scope* parent;
    Span span;
public:
    Scope(Scope* p, DiagnosticEngine& e, Span s) : engine(e), parent(p), span(s) {}

    // the value of the innermost constant with this name, or nothing when the innermost name isn't one, which a symbol declared here can be
    std::optional<uint64_t> lookupConstant(std::string_view name) {
        auto it = constants.find(name);
        if (it != constants.end()) return it->second;
        if (symbols.count(name)) return std::nullopt;
        return parent == nullptr ? std::nullopt : parent->lookupConstant(name);
    }

    // records a constant let's value before it's checked, so array sizes anywhere in the scope can use it
    void insertConstant(std::string_view name, uint64_t value) {
        constants.emplace(name, value);
    }

    // finds the innermost symbol with this name, or null if nothing declares it
    Symbol* lookup(std::string_view name) {
        auto it = symbols.find(name);
        if (it != symbols.end()) return it->second;
        return (parent == nullptr) ? nullptr : parent->lookup(name);
    }

    // declares a symbol here, where an inner scope may shadow an outer name but one scope can't declare a name twice
    void insert(Symbol* symbol) {
        auto it = symbols.find(symbol->name);
        if (it != symbols.end()) {
            std::string name(symbol->name);
            engine.printError("cherimoya requires names in one scope to be distinct, please rename this '" + name + "' or remove the first one",
                symbol->span, {{"'" + name + "' was first declared here", it->second->span}});
        } else {
            symbols[symbol->name] = symbol;
        }
        symbol->scope = span;
    }
};
