#pragma once

#include <string_view>
#include <unordered_map>
#include "context.hpp"

enum class SymbolKind {
    Binding,
    Param,
    Function
};

// a symbol is one declared name, owned by sema so pointers to it never dangle
// owner is the function body that declared it, 0 at the top of the file, so nested functions can't reach their parent's locals
struct Symbol {
    std::string_view name;
    SymbolKind kind;
    SemaType* type;
    bool mut;
    Span s;
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
            engine.print_error("cherimoya requires names in one scope to be distinct, please rename this '" + name + "' or remove the first one",
                symbol->s, {{"'" + name + "' was first declared here", it->second->s}});
        } else {
            symbols[symbol->name] = symbol;
        }
    }
};
