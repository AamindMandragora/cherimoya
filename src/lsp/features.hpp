#pragma once

#include <cctype>
#include <map>
#include <string>
#include <vector>
#include <llvm/Support/JSON.h>
#include "document.hpp"

using llvm::json::Array;
using llvm::json::Object;
using llvm::json::Value;

// the kinds of token the server colors, in the order the legend tells the editor, which is what a token's type number indexes
enum class TokenType : uint32_t { Keyword, Type, Number, String, Comment, Operator, Function, Parameter, Variable };
inline const std::vector<std::string> tokenTypes = {"keyword", "type", "number", "string", "comment", "operator", "function", "parameter", "variable"};

// the modifiers a token can have, as bits: where a name is declared, and a name that can't be assigned to, which is every binding and parameter that isn't mut
enum TokenModifier : uint32_t { Declaration = 1, Readonly = 2 };
inline const std::vector<std::string> tokenModifiers = {"declaration", "readonly"};

// a span as the editor's range, from the position it starts at to the one it ends at
inline Value rangeOf(const Document& document, Span span) {
    auto toJson = [](Position position) { return Object{{"line", position.line}, {"character", position.character}}; };
    return Object{{"start", toJson(document.lines->position(span.start))}, {"end", toJson(document.lines->position(span.end))}};
}

// a span in a document as the editor's location
inline Value locationOf(const Document& document, Span span) {
    return Object{{"uri", document.uri}, {"range", rangeOf(document, span)}};
}

// the use of a name at an offset, or null if there's no name there, where an offset just past a name still counts, since that's where the cursor sits after typing it
inline const Use* useAt(const Document& document, size_t offset) {
    for (auto& use : document.sema.allUses()) {
        if (use.span.start <= offset && offset <= use.span.end) return &use;
    }
    return nullptr;
}

// checks whether one span is inside another and smaller, the way an inner scope is inside an outer one
inline bool isInside(Span inner, Span outer) {
    return outer.start <= inner.start && inner.end <= outer.end && inner.length() < outer.length();
}

// the owner number of the function body an offset is in, the innermost one, or 0 at the top of the file
inline size_t ownerAt(const Document& document, size_t offset) {
    const Body* innermost = nullptr;
    for (auto& body : document.sema.allBodies()) {
        if (body.span.start <= offset && body.span.end >= offset) {
            if (!innermost || isInside(body.span, innermost->span)) innermost = &body;
        }
    }
    return innermost ? innermost->owner : 0;
}

// checks whether a symbol can be named at an offset: inside its scope, after its declaration unless it's a function, which is hoisted, and, for a local, only inside its own function, since a nested function can't reach the one around it
inline bool isVisible(const Document& document, const Symbol& symbol, size_t offset) {
    if (offset < symbol.scope.start || offset > symbol.scope.end) return false;
    if (symbol.kind != SymbolKind::Function && offset < symbol.span.end) return false;
    if (symbol.kind == SymbolKind::Function || symbol.owner == 0) return true;
    return ownerAt(document, offset) == symbol.owner;
}

// shows a symbol the way it's declared: a function as let (a :: int) -> f -> (r :: int), a binding as let x :: mut i8, and a parameter as n :: int
inline std::string render(const Symbol& symbol) {
    std::string name(symbol.name);
    std::string mut = symbol.isMutable ? "mut " : "";
    if (symbol.kind == SymbolKind::Param) return std::string(symbol.name) + " :: " + mut + symbol.type->toString();
    if (symbol.kind == SymbolKind::Binding) return "let " + std::string(symbol.name) + " :: " + mut + symbol.type->toString();
    auto& fn = *symbol.function;
    auto& type = static_cast<const FunctionSemaType&>(*symbol.type);
    std::string inputs;
    for (size_t i = 0; i < fn.inputs.size(); i++) {
        inputs += std::string(fn.inputs[i].decl->name) + " :: " + type.params[i]->toString() + (i + 1 == fn.inputs.size() ? "" : ", ");
    }
    std::string outputs;
    for (size_t i = 0; i < fn.outputs.size(); i++) {
        if (fn.outputs.size() == 1) {
            outputs = std::string(fn.outputs[i].name.text) + " :: " + type.returns->toString();
        } else {
            outputs += std::string(fn.outputs[i].name.text) + (i + 1 == fn.outputs.size() ? "" : ", ");
        }
    }
    return "let (" + inputs + ") -> " + name + " -> (" + outputs + ")";
}

// every diagnostic as the editor's, where a warning is severity 2, an error is 1, and each note is related information pointing at its own span
inline Array diagnostics(const Document& document) {
    Array result;
    for (auto& diagnostic : document.engine.all()) {
        Array related;
        for (auto& note : diagnostic.notes) {
            related.push_back(Object{{"location", locationOf(document, note.span)}, {"message", note.message}});
        }
        result.push_back(Object{{"range", rangeOf(document, diagnostic.span)}, {"severity", diagnostic.isWarning ? 2 : 1}, {"source", "cheri"}, {"message", diagnostic.message}, {"relatedInformation", std::move(related)}});
    }
    return result;
}

// every token's color, as the editor wants them: five numbers per token, the line it's on relative to the last token's, its start relative to the last token's when they share a line, its length, its type, and its modifiers
inline Array semanticTokens(const Document& document) {
    struct Piece {
        Span span;
        TokenType type;
        uint32_t modifiers;
    };
    std::vector<Piece> pieces;
    for (auto& comment : document.comments) pieces.push_back({comment, TokenType::Comment, 0});
    // a name is colored by what sema says it is, and a name sema couldn't resolve is just a variable
    std::map<size_t, const Use*> uses;
    for (auto& use : document.sema.allUses()) uses[use.span.start] = &use;
    for (auto& token : document.tokens) {
        Span span{token.loc, token.loc + token.text.size()};
        auto kind = static_cast<int>(token.kind);
        if (kind >= static_cast<int>(TokenKind::KwLet) && kind <= static_cast<int>(TokenKind::KwMoya)) {
            pieces.push_back({span, TokenType::Keyword, 0});
        } else if (token.kind == TokenKind::TypeName) {
            pieces.push_back({span, TokenType::Type, 0});
        } else if (token.kind == TokenKind::IntLiteral || token.kind == TokenKind::FloatLiteral) {
            pieces.push_back({span, TokenType::Number, 0});
        } else if (token.kind == TokenKind::CharLiteral || token.kind == TokenKind::StringLiteral) {
            pieces.push_back({span, TokenType::String, 0});
        } else if (token.op != nullptr) {
            pieces.push_back({span, TokenType::Operator, 0});
        } else if (token.kind == TokenKind::Identifier) {
            auto found = uses.find(token.loc);
            if (found == uses.end()) {
                pieces.push_back({span, TokenType::Variable, 0});
                continue;
            }
            const Symbol& symbol = *found->second->symbol;
            uint32_t mod = 0;
            if (found->second->isDeclaration) mod |= TokenModifier::Declaration;
            if (!symbol.isMutable && !(symbol.kind == SymbolKind::Function)) mod |= TokenModifier::Readonly;
            if (symbol.kind == SymbolKind::Binding) pieces.push_back({span, TokenType::Variable, mod});
            else if (symbol.kind == SymbolKind::Param) pieces.push_back({span, TokenType::Parameter, mod});
            else if (symbol.kind == SymbolKind::Function) pieces.push_back({span, TokenType::Function, mod});
        }
    }
    std::sort(pieces.begin(), pieces.end(), [](const Piece& a, const Piece& b) { return a.span.start < b.span.start; });
    Array data;
    Position previous{0, 0};
    for (auto& piece : pieces) {
        Position start = document.lines->position(piece.span.start);
        Position end = document.lines->position(piece.span.end);
        // a token is one line long, so one that runs past its line, like an unclosed string, is colored to the end of its first line
        uint32_t length = end.line == start.line ? end.character - start.character : document.lines->position(document.lines->offset({start.line, UINT32_MAX})).character - start.character;
        data.push_back(start.line - previous.line);
        data.push_back(start.line == previous.line ? start.character - previous.character : start.character);
        data.push_back(length);
        data.push_back(static_cast<int>(piece.type));
        data.push_back(piece.modifiers);
        previous = start;
    }
    return data;
}

// what a built-in type spelling means, worked out from the spelling itself: i8uw is an 8-bit unsigned integer that wraps, and f16n is IEEE half precision that can hold a NaN
inline std::string typeHelp(std::string_view type) {
    if (type == "int") return "`int` is `i64`, a 64-bit signed integer that traps on overflow.";
    if (type == "float") return "`float` is `f64`, IEEE double precision, which traps when an operation makes a NaN.";
    if (type == "bool") return "`true` or `false`.";
    if (type == "char") return "One ASCII character, 0 to 7F, held in a byte.";
    if (type == "rune") return "One Unicode scalar value, 0 to 10FFFF but not the surrogates D800 to DFFF, held in 4 bytes.";
    if (type == "boolf") return "Reserved for a fuzzy boolean, which has no meaning yet.";
    if (type[0] == 'i') {
        // the width is the digits after the i, and the suffix is u for unsigned and w for wrapping
        size_t digits = 1;
        while (digits < type.size() && std::isdigit(static_cast<unsigned char>(type[digits]))) digits++;
        std::string_view suffix = type.substr(digits);
        bool isSigned = suffix.find('u') == std::string_view::npos;
        bool isWrapping = suffix.find('w') != std::string_view::npos;
        std::string behavior = isWrapping ? "that wraps on overflow, keeping the low bits like the hardware does" : "that traps on overflow";
        std::string width(type.substr(1, digits - 1));
        // 8 is the one width said with a vowel sound
        return (width == "8" ? "An " : "A ") + width + "-bit " + (isSigned ? "signed" : "unsigned") + " integer " + behavior + ".";
    }
    // a float is its encoding, then an n if it can hold a NaN
    bool isNanFree = type.back() != 'n';
    std::string_view encoding = isNanFree ? type : type.substr(0, type.size() - 1);
    std::string what;
    if (encoding == "f8" || encoding == "f8e4m3") what = "An 8-bit float with 4 exponent bits and 3 mantissa bits, E4M3, which has no infinity and reaches 448";
    else if (encoding == "f8e5m2") what = "An 8-bit float with 5 exponent bits and 2 mantissa bits, E5M2, laid out like IEEE and reaching 57344";
    else if (encoding == "f16") what = "IEEE half precision";
    else if (encoding == "f16e8m7") what = "bfloat16, a 16-bit float with f32's 8 exponent bits and 7 mantissa bits";
    else if (encoding == "f32") what = "IEEE single precision";
    else if (encoding == "f64") what = "IEEE double precision";
    else what = "IEEE quad precision";
    return what + (isNanFree ? ", which traps when an operation makes a NaN." : ", which can hold a NaN and orders it below every number.");
}

// what a keyword means, from the language's own description of it, where the ones that wait for a later feature say so
inline std::string keywordHelp(std::string_view keyword) {
    static const std::map<std::string_view, std::string> help = {
        {"let", "Declares a binding, `let x <- 5;`, or a function, `let (n :: int) -> f -> (r :: int) { ... }`."},
        {"type", "Defines a type, which cheri doesn't support yet."},
        {"export", "Lists what a module shares with other modules, which cheri doesn't support yet."},
        {"import", "Brings in names from another module, which cheri doesn't support yet."},
        {"match", "Picks the first arm whose pattern matches its subject, `match (x) { 0 -> { ... } _ -> { ... } }`, or, with no subject, the first arm whose guard holds."},
        {"repeat", "A loop: `repeat (cond) { ... }` runs while `cond` holds, and `repeat { ... }` runs until a yield leaves it."},
        {"chunk", "A scope that runs like a block, and will get its own allocator."},
        {"unscoped", "Owns heap memory that has to be handed off before its scope ends, by yielding it, storing it somewhere that lives longer, or passing it to something that takes ownership. cheri doesn't support it yet."},
        {"peek", "A non-owning view, which never frees memory and can't escape. cheri doesn't support it yet."},
        {"mut", "Makes a binding or parameter assignable: `let x :: mut <- 0;`. Before a handle it lets the handle be rebound, and after one, `arr mut T`, it makes the elements mutable."},
        {"ptr", "An owning handle to one object, `ptr T(value)`. cheri doesn't support handles yet."},
        {"arr", "A handle to an array, a pointer and its length, `arr mut T(n)`. cheri doesn't support handles yet."},
        {"take", "`take(slot)` moves the value out of a place and returns it, leaving the place to be reassigned. cheri doesn't support it yet."},
        {"swap", "`swap(a, b);` exchanges the values in two places. cheri doesn't support it yet."},
        {"replace", "`replace(slot, new)` stores `new` in a place and returns what was there. cheri doesn't support it yet."},
        {"wit", "A value or obligation only the verifier sees, which is checked like any other but never runs."},
        {"assert", "Checks a condition when the program runs, and traps with its line when it's false: `assert (n > 0);`."},
        {"old", "In an output refinement, a parameter's value on entry: `{count(g) = count(old g)}`."},
        {"true", "One of the two `bool` values."},
        {"false", "One of the two `bool` values."},
        {"macro", "Reserved for macros, which come later."},
        {"moya", "Reserved for macros, which come later."},
    };
    auto found = help.find(keyword);
    return found == help.end() ? "" : found->second;
}

// what a built-in word means, a type spelling or a keyword, or "" for anything else
inline std::string builtinHelp(std::string_view word) {
    auto kind = lookupKeyword(word);
    if (!kind) return "";
    return *kind == TokenKind::TypeName ? typeHelp(word) : keywordHelp(word);
}

// what a name under the cursor is, shown as its declaration in a cherimoya code block, or what a built-in type or keyword means, or null where there's neither
inline Value hover(const Document& document, size_t offset) {
    const Use* use = useAt(document, offset);
    if (use == nullptr) {
        for (auto& token : document.tokens) {
            Span span{token.loc, token.loc + token.text.size()};
            if (span.start > offset || offset > span.end) continue;
            std::string help = builtinHelp(token.text);
            if (help.empty()) continue;
            return Object{{"contents", Object{{"kind", "markdown"}, {"value", "```cherimoya\n" + std::string(token.text) + "\n```\n" + help}}}, {"range", rangeOf(document, span)}};
        }
        return nullptr;
    }
    return Object{{"contents", Object{{"kind", "markdown"}, {"value", "```cherimoya\n" + render(*use->symbol) + "\n```"}}}, {"range", rangeOf(document, use->span)}};
}

// where the name under the cursor is declared, or null where there's no name
inline Value definition(const Document& document, size_t offset) {
    const Use* use = useAt(document, offset);
    if (use == nullptr) return nullptr;
    return locationOf(document, use->symbol->span);
}

// every place the name under the cursor appears, with or without its declaration
inline Array references(const Document& document, size_t offset, bool includeDeclaration) {
    Array result;
    const Use* target = useAt(document, offset);
    if (target == nullptr) return result;
    for (auto& use : document.sema.allUses()) {
        if (use.symbol == target->symbol && !(use.isDeclaration && !includeDeclaration)) {
            result.push_back(locationOf(document, use.span));
        }
    }
    return result;
}

// renames the name under the cursor and every use of it, or explains why not: the new name has to be a name, it can't clash with another in the same scope, and it can't change what any use means by shadowing or being shadowed
inline Value rename(const Document& document, size_t offset, const std::string& newName, std::string& error) {
    const Use* target = useAt(document, offset);
    if (!target) {
        error = "there's no name here to rename";
        return nullptr;
    }
    const Symbol& symbol = *target->symbol;
    // the new name has to lex as exactly one identifier, which rules out keywords, types, _, and anything with spaces or symbols in it
    SourceManager manager;
    manager.loadString(newName);
    DiagnosticEngine engine(manager, nullptr);
    auto tokens = Lexer(engine).lex();
    if (!(tokens.size() == 2 && tokens[0].kind == TokenKind::Identifier && tokens[1].kind == TokenKind::Eof && tokens[0].text == newName)) {
        error = "'" + newName + "' isn't a name, so it can't replace '" + std::string(symbol.name) + "'";
        return nullptr;
    }
    for (auto& other : document.sema.allSymbols()) {
        if (&other == &symbol || other.name != newName) continue;
        if (other.scope.start == symbol.scope.start && other.scope.end == symbol.scope.end) {
            error = "'" + newName + "' is already declared in the same scope";
            return nullptr;
        }
        for (auto& use : document.sema.allUses()) {
            if (use.symbol == &symbol && isVisible(document, other, use.span.start) && isInside(other.scope, symbol.scope)) {
                error = "renaming would make a use of '" + std::string(symbol.name) + "' mean the other '" + newName + "' instead";
                return nullptr;
            }
            if (use.symbol == &other && isVisible(document, symbol, use.span.start) && isInside(symbol.scope, other.scope)) {
                error = "renaming would make a use of '" + newName + "' mean the renamed '" + std::string(symbol.name) + "' instead";
                return nullptr;
            }
        }
    }
    Array edits;
    for (auto& use : document.sema.allUses()) {
        if (use.symbol == &symbol) {
            edits.push_back(Object{{"range", rangeOf(document, use.span)}, {"newText", newName}});
        }
    }
    return Object{{"changes", Object{{document.uri, std::move(edits)}}}};
}

// one function in the outline, with the functions nested in its body as its children
inline Value outlineOf(const Document& document, const FnDecl& fn);

// the functions and bindings among some statements, looking through attributes, for the outline
inline Array outlineOf(const Document& document, const std::vector<std::unique_ptr<Stmt>>& stmts) {
    Array result;
    for (auto& stmt : stmts) {
        const Stmt* top = stmt.get();
        while (top && top->kind == StmtKind::Attr) top = static_cast<const AttrStmt&>(*top).target.get();
        if (!top) continue;
        if (top->kind == StmtKind::Decl) {
            auto& decl = *static_cast<const DeclStmt&>(*top).decl;
            // a function sema declared, which is one whose decl is set, is its own entry
            if (decl.kind == DeclKind::Fn && static_cast<const FnDecl&>(decl).decl) result.push_back(outlineOf(document, static_cast<const FnDecl&>(decl)));
        } else if (top->kind == StmtKind::Let) {
            auto& let = static_cast<const LetStmt&>(*top);
            if (let.pattern->kind != PatternKind::Binder) continue;
            auto& binder = static_cast<const BinderPattern&>(*let.pattern);
            if (!binder.decl) continue;
            result.push_back(Object{{"name", std::string(binder.name.text)}, {"detail", render(*binder.decl)}, {"kind", 13}, {"range", rangeOf(document, let.span)}, {"selectionRange", rangeOf(document, binder.span)}});
        }
    }
    return result;
}

inline Value outlineOf(const Document& document, const FnDecl& fn) {
    Array children = fn.body ? outlineOf(document, fn.body->stmts) : Array{};
    // kind 12 is a function, its range is the whole declaration, and its selection is the name, which is where the cursor jumps
    return Object{{"name", std::string(fn.name.text)}, {"detail", render(*fn.decl)}, {"kind", 12}, {"range", rangeOf(document, fn.span)}, {"selectionRange", rangeOf(document, fn.decl->span)}, {"children", std::move(children)}};
}

// the outline: every top-level function and binding, with nested functions under the function around them
inline Array documentSymbols(const Document& document) {
    return outlineOf(document, document.program);
}

// everything that could be typed at the cursor: the names visible there, then keywords and types, where a name whose type is what the position expects comes first, and a function counts when calling it gives that type
inline Array completion(const Document& document, size_t offset) {
    // the narrowest expected type around the cursor, which is the one for the position it's actually in
    const Expectation* expected = nullptr;
    for (auto& expectation : document.sema.allExpectations()) {
        if (expectation.span.start <= offset && expectation.span.end >= offset && (expected == nullptr || isInside(expectation.span, expected->span))) expected = &expectation;
    }
    // a name visible in more than one scope means the innermost one
    std::map<std::string_view, const Symbol*> visible;
    for (auto& symbol : document.sema.allSymbols()) {
        if (isVisible(document, symbol, offset)) {
            // a later symbol only replaces the kept one when it's declared further in, so an outer name declared later can't hide an inner one
            auto found = visible.find(symbol.name);
            if (found == visible.end() || isInside(symbol.scope, found->second->scope)) visible[symbol.name] = &symbol;
        }
    }
    Array result;
    for (auto& [name, symbol] : visible) {
        // what the name gives, where a function gives what calling it does, so it fits when its return type is the expected one
        SemaType* gives = symbol->kind == SymbolKind::Function ? static_cast<FunctionSemaType*>(symbol->type)->returns : symbol->type;
        bool fits = expected && gives == expected->type;
        result.push_back(Object{{"label", std::string(name)}, {"kind", symbol->kind == SymbolKind::Function ? 3 : 6}, {"detail", render(*symbol)}, {"sortText", (fits ? "0" : "1") + std::string(name)}});
    }
    for (auto& [word, kind] : keywordTable()) {
        Object documentation{{"kind", "markdown"}, {"value", builtinHelp(word)}};
        result.push_back(Object{{"label", std::string(word)}, {"kind", kind == TokenKind::TypeName ? 22 : 14}, {"sortText", "2" + std::string(word)}, {"documentation", std::move(documentation)}});
    }
    return result;
}

// the signature of the call the cursor is in, the innermost one, with the parameter the cursor is at highlighted, or null outside any call
inline Value signatureHelp(const Document& document, size_t offset) {
    const Call* call = nullptr;
    for (auto& candidate : document.sema.allCalls()) {
        if (candidate.span.start < offset && offset <= candidate.span.end && (!call || isInside(candidate.span, call->span))) call = &candidate;
    }
    if (!call) return nullptr;
    const Symbol& function = *call->function;
    std::string label = render(function);
    // each parameter is a range of the label, found in order, so a parameter's text that also appears earlier can't be found twice
    Array parameters;
    size_t from = label.find('(') + 1;
    auto& type = static_cast<const FunctionSemaType&>(*function.type);
    for (size_t i = 0; i < function.function->inputs.size(); i++) {
        std::string text = std::string(function.function->inputs[i].name.text) + " :: " + type.params[i]->toString();
        size_t at = label.find(text, from);
        parameters.push_back(Object{{"label", Array{static_cast<int64_t>(at), static_cast<int64_t>(at + text.size())}}});
        from = at + text.size();
    }
    // the cursor is at the parameter after every argument that ends before it
    size_t active = 0;
    for (auto& arg : call->args) {
        if (arg.end < offset) active++;
    }
    if (!parameters.empty()) active = std::min(active, parameters.size() - 1);
    return Object{
        {"signatures", Array{Object{{"label", label}, {"parameters", std::move(parameters)}}}},
        {"activeSignature", 0},
        {"activeParameter", static_cast<int64_t>(active)},
    };
}
