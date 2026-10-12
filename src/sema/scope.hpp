#pragma once

#include <string_view>
#include <unordered_map>
#include "diagnostics.hpp"
#include "type.hpp"

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
    bool isMutable;
    Span span;
    size_t owner;
};

// a lexical scope maps names to the symbols declared in it, and falls back to its parent for everything else
class Scope {
    DiagnosticEngine& engine;
    std::unordered_map<std::string_view, Symbol*> symbols;
    Scope* parent;
public:
    Scope(Scope* p, DiagnosticEngine& e) : engine(e), parent(p) {}

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
    }
};
