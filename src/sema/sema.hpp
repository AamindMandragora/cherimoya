#pragma once

#include <algorithm>
#include <deque>
#include "context.hpp"
#include "scope.hpp"
#include "target.hpp"
#include "ast.hpp"

// one place a name appears, where it's declared or used, and the symbol it means
struct Use {
    Span span;
    Symbol* symbol;
    bool isDeclaration;
};

// the type an expression's context wanted, which the language server ranks completions by
struct Expectation {
    Span span;
    SemaType* type;
};

// one argument of a call by name: its span, name included, whether it's named, and the input it goes to, when it goes to one
struct CallArg {
    Span span;
    bool isNamed;
    std::optional<size_t> input;
};

// a call of a function by name, with its arguments and where its ) is, which the language server's signature help and parameter hints read, where a pipe's first argument is the piped value, outside any parentheses, and a pipe of a bare name has no )
struct Call {
    Span span;
    Symbol* function;
    std::vector<CallArg> args;
    std::optional<size_t> close;
    bool isPipe;
};

// a function's span and the owner number its own locals get, so the language server can tell which locals a position can reach
struct Body {
    Span span;
    size_t owner;
};

// a step of a forward pipe, or the value the pipe starts from, and the type it gives, which the language server shows at the end of each line of a pipe spread over several
struct Step {
    Span span;
    SemaType* type;
};

// a scope with a name, a function's body or a named block, match, loop, or chunk, whose closing brace the language server labels with that name
struct Label {
    Span span;
    std::string_view name;
};

// what a file is checked as: a module, which is how the language server and the tests check a .moya file, a program that will run, which needs a start, or a .toya file of tests, which can't have one, since cheri test runs its tests instead
enum class Role { Module, Program, Tests };

// a function marked @test, named the way a debugger names it, outer.inner for a nested one, with the text its trap's message has to hold when it's expected to trap, and the seconds it gets when it has a limit
struct Test {
    const FnDecl* fn;
    std::string name;
    std::optional<std::string> traps;
    std::optional<uint64_t> timeout;
};

// sema resolves every name to its symbol and gives every expression a type, writing both straight into the AST
class Sema {
    TypeContext context;
    DiagnosticEngine& engine;
    // every symbol lives here, so the pointers in scopes and identifiers never dangle
    std::deque<Symbol> symbols;
    // the innermost lexical scope
    Scope* scope;
    // which function body is being checked, 0 at the top of the file, and how many have been numbered so far
    size_t owner;
    size_t functions;
    // every construct a yield could target right now, innermost last
    std::vector<Target> targets;
    // what the language server reads once checking is done: every use of a name, every expected type, every call, every function body, every binding whose type was inferred, every pipe step, and every named scope
    std::vector<Use> uses;
    std::vector<Expectation> expectations;
    std::vector<Call> calls;
    std::vector<Body> bodies;
    std::vector<const Symbol*> inferred;
    std::vector<Step> steps;
    std::vector<Label> labels;
    // what the file is checked as, every test found so far, and the functions around the one being checked, outermost first, which a nested test's name starts with
    Role role = Role::Module;
    std::vector<Test> tests;
    // the value of every immutable binding whose initializer folds to an integer, which array sizes and constant indices can use
    std::unordered_map<const Symbol*, uint64_t> constants;
    std::vector<std::string_view> enclosing;

    // reports something sema doesn't check yet, and poisons the result
    SemaType* unsupported(std::string_view what, Span span) {
        engine.printError("cherimoya doesn't support " + std::string(what) + " yet", span);
        return context.semaError;
    }

    // the span of a token, from where it starts to where its text ends
    Span tokSpan(const Token& tok) {
        return {tok.loc, tok.loc + tok.text.length()};
    }

    // the label a scope can be yielded to by, which is its name if it has one
    std::optional<std::string_view> labelOf(const std::optional<Token>& name) {
        if (name) return name->text;
        return std::nullopt;
    }

    // makes a symbol that lives as long as sema does
    Symbol* makeSymbol(std::string_view name, SymbolKind kind, SemaType* type, std::vector<bool> mutability, Span span) {
        symbols.push_back(Symbol{name, kind, type, std::move(mutability), span, owner, {}, nullptr});
        uses.push_back({span, &symbols.back(), true});
        return &symbols.back();
    }

    // checks body with a new lexical scope pushed on top of the current one, covering span
    template <typename F>
    void inScope(Span span, F body) {
        Scope inner(scope, engine, span);
        Scope* saved = scope;
        scope = &inner;
        body();
        scope = saved;
    }

    // checks whether a type is mutable as a whole value: mut int, mut arr[3] int, which can be replaced, or mut with the type left to inference
    bool isMut(const Type& t) {
        if (t.kind == TypeKind::Refined) return isMut(*static_cast<const RefinedType&>(t).inner);
        if (t.kind != TypeKind::Qualified) return false;
        return static_cast<const QualifiedType&>(t).position != QualPosition::Elements;
    }

    // what a binding of a type can write, the binding itself first and then each level of an array's elements, outermost first, from where mut sits: mut arr[2] arr[3] int replaces the whole, arr[2] mut arr[3] int writes the outer level, and a bare mut on a type left to inference writes everything
    std::vector<bool> mutabilityOf(const Type* t, SemaType* type) {
        size_t depth = 0;
        for (auto array = asArray(type); array; array = asArray(array->element)) depth++;
        std::vector<bool> levels(depth + 1, false);
        if (t) levels[0] = isMut(*t);
        while (t && t->kind == TypeKind::Refined) t = static_cast<const RefinedType&>(*t).inner.get();
        if (t && t->kind == TypeKind::Qualified) {
            auto& qualified = static_cast<const QualifiedType&>(*t);
            if (!qualified.inner) return std::vector<bool>(depth + 1, true);
            t = qualified.inner.get();
        }
        for (size_t level = 1; level <= depth && t && t->kind == TypeKind::Handle; level++) {
            const Type* inner = static_cast<const HandleType&>(*t).inner.get();
            if (inner && inner->kind == TypeKind::Qualified && static_cast<const QualifiedType&>(*inner).position == QualPosition::Elements) {
                levels[level] = true;
                inner = static_cast<const QualifiedType&>(*inner).inner.get();
            }
            t = inner;
        }
        return levels;
    }

    // the value of an integer expression the program knows when it compiles, which is all an array's size or a constant index can be: an integer literal, an immutable binding whose initializer folds, and + - * / % over those, or nothing for anything else, including a result below zero
    std::optional<uint64_t> fold(const Expr& e, bool isHoisting = false) {
        switch (e.kind) {
            case ExprKind::IntLiteral: {
                auto [radix, digits] = literalDigits(static_cast<const IntLiteralExpr&>(e).literal);
                uint64_t value = 0;
                for (char c : digits) {
                    if (value > (UINT64_MAX - hexValue(c)) / radix) return std::nullopt;
                    value = value * radix + hexValue(c);
                }
                return value;
            }
            // a name is found by its symbol once it's checked, and by name while constants are hoisted, before anything has a symbol
            case ExprKind::Identifier: {
                auto& identifier = static_cast<const IdentifierExpr&>(e);
                if (isHoisting) return scope->lookupConstant(identifier.name);
                auto found = constants.find(identifier.decl);
                if (found == constants.end()) return std::nullopt;
                return found->second;
            }
            case ExprKind::Binary: {
                auto& binary = static_cast<const BinaryExpr&>(e);
                auto lhs = fold(*binary.lhs, isHoisting);
                auto rhs = fold(*binary.rhs, isHoisting);
                if (!lhs || !rhs) return std::nullopt;
                switch (binary.op.kind) {
                    case TokenKind::Plus: return *lhs + *rhs >= *lhs ? std::optional<uint64_t>(*lhs + *rhs) : std::nullopt;
                    case TokenKind::Minus: return *lhs >= *rhs ? std::optional<uint64_t>(*lhs - *rhs) : std::nullopt;
                    case TokenKind::Star: return *rhs == 0 || *lhs <= UINT64_MAX / *rhs ? std::optional<uint64_t>(*lhs * *rhs) : std::nullopt;
                    case TokenKind::Slash: return *rhs ? std::optional<uint64_t>(*lhs / *rhs) : std::nullopt;
                    case TokenKind::Percent: return *rhs ? std::optional<uint64_t>(*lhs % *rhs) : std::nullopt;
                    default: return std::nullopt;
                }
            }
            default:
                return std::nullopt;
        }
    }

    // the N in arr[N] T, which has to be known when the program compiles: an integer literal, or an immutable binding whose initializer folds to one, where a bare name reaches sema as a type, since arr[A] T could name an allocator
    std::optional<uint64_t> arraySize(const GenericArg& arg, Span span) {
        std::optional<uint64_t> size;
        if (arg.value) {
            checkExpr(*arg.value);
            size = fold(*arg.value);
        } else if (arg.type && arg.type->kind == TypeKind::Named) {
            auto& named = static_cast<const NamedType&>(*arg.type);
            if (named.path.size() == 1 && named.args.empty()) {
                if (Symbol* symbol = scope->lookup(named.path[0].text)) uses.push_back({arg.type->span, symbol, false});
                size = scope->lookupConstant(named.path[0].text);
            }
        }
        if (!size) engine.printError("cherimoya requires an array's size to be known when the program compiles, please use an integer literal or an immutable binding of one", span);
        return size;
    }

    // turns a parsed type into a sema type, or null when the type is left to inference (let x :: mut <- 5), where refinements resolve to their base type and their predicates are checked once the subject is in scope
    SemaType* resolveType(const Type& t) {
        switch (t.kind) {
            case TypeKind::Named: {
                auto& named = static_cast<const NamedType&>(t);
                if (named.path.size() == 1 && named.args.empty()) {
                    auto name = named.path[0].text;
                    if (IntSemaType* integer = context.intType(name)) return integer;
                    if (FloatSemaType* floating = context.floatType(name)) return floating;
                    if (name == "bool") return context.semaBool;
                    if (name == "boolf") return context.semaBoolf;
                    if (name == "char") return context.semaChar;
                    if (name == "rune") return context.semaRune;
                }
                std::string text;
                for (size_t i = 0; i < named.path.size(); i++) text += (i ? "." : "") + std::string(named.path[i].text);
                return unsupported("the type '" + text + "'", t.span);
            }
            // where mut sits decides what a binding can write, which mutabilityOf reads, so the type is the same either way
            case TypeKind::Qualified: {
                auto& qualified = static_cast<const QualifiedType&>(t);
                return qualified.inner ? resolveType(*qualified.inner) : nullptr;
            }
            case TypeKind::Refined:
                return resolveType(*static_cast<const RefinedType&>(t).inner);
            // arr[N] T is a fixed array, and a handle without a size, or a ptr, holds memory that has to be freed
            case TypeKind::Handle: {
                auto& handle = static_cast<const HandleType&>(t);
                if (handle.handle.kind == TokenKind::KwPtr) return unsupported("pointers", t.span);
                if (!handle.arg) return unsupported("arrays without a fixed size", t.span);
                auto size = arraySize(*handle.arg, handle.arg->value ? handle.arg->value->span : handle.arg->type->span);
                SemaType* element = handle.inner ? resolveType(*handle.inner) : nullptr;
                if (!size || element == nullptr || isPoisoned(element)) return context.semaError;
                return context.arrayType(element, *size);
            }
            case TypeKind::Mode:
                return unsupported("'" + std::string(static_cast<const ModeType&>(t).mode.text) + "'", t.span);
            case TypeKind::Tuple:
                return unsupported("tuple types", t.span);
            case TypeKind::Record:
                return unsupported("record types", t.span);
            default:
                return context.semaError;
        }
    }

    // checks every refinement predicate in a type against its subject: the subject's own name on parameters, or the binder
    void checkRefinements(const Type& t, Symbol* subject) {
        if (t.kind == TypeKind::Qualified) {
            auto& qualified = static_cast<const QualifiedType&>(t);
            if (qualified.inner) checkRefinements(*qualified.inner, subject);
            return;
        }
        if (t.kind != TypeKind::Refined) return;
        auto& refined = static_cast<const RefinedType&>(t);
        checkRefinements(*refined.inner, subject);
        inScope(refined.span, [&] {
            if (refined.binder) scope->insert(makeSymbol(refined.binder->text, SymbolKind::Binding, subject->type, {false}, tokSpan(*refined.binder)));
            expectBool(*refined.predicate, "a refinement");
        });
    }

    // the source text of a span, which a fix quotes or rewrites
    std::string textOf(Span span) {
        return std::string(engine.getManager().text().substr(span.start, span.length()));
    }

    // turns a boolf into a bool with the usual cutoff, where anything but a name, a call, or a literal goes in parentheses, since > binds tighter than && and ||
    Fix cutoff(const Expr& e, Span replaced) {
        bool isAtom = e.kind == ExprKind::Identifier || e.kind == ExprKind::Call || isLiteral(e);
        std::string value = isAtom ? textOf(e.span) : "(" + textOf(e.span) + ")";
        return {"Compare it with a cutoff, '> 0.5'", {{replaced, value + " > 0.5"}}};
    }

    // makes a binding or parameter mutable where it's declared: mut goes in front of its type, or :: mut after its name when it has none, where making its elements writable only has a fix when it has no type, since a bare mut writes every level
    std::vector<Fix> mutability(const Symbol& symbol, bool isElements = false) {
        std::string_view text = engine.getManager().text();
        size_t at = symbol.span.end;
        while (at < text.size() && text[at] == ' ') at++;
        std::string title = "Make '" + std::string(symbol.name) + "' mutable";
        if (text.compare(at, 2, "::") == 0) {
            at += 2;
            while (at < text.size() && text[at] == ' ') at++;
            if (isElements) return {};
            return {{title, {insertion(at, "mut ")}}};
        }
        if (text.compare(at, 2, "<-") == 0) return {{title, {insertion(symbol.span.end, " :: mut")}}};
        return {};
    }

    // adds a catch-all arm before a match's closing brace: on a line of its own, one level in from the brace, when the brace is alone on its line, and right before it otherwise
    std::vector<Fix> catchAllArm(const MatchExpr& m) {
        std::string_view text = engine.getManager().text();
        size_t brace = m.span.end - 1;
        if (m.span.length() == 0 || text[brace] != '}') return {};
        size_t lineStart = brace;
        while (lineStart > 0 && (text[lineStart - 1] == ' ' || text[lineStart - 1] == '\t')) lineStart--;
        std::string title = "Add a '_ -> { }' arm";
        if (lineStart == 0 || text[lineStart - 1] == '\n') {
            std::string indent(text.substr(lineStart, brace - lineStart));
            return {{title, {insertion(lineStart, indent + "    _ -> { }\n")}}};
        }
        return {{title, {insertion(brace, "_ -> { } ")}}};
    }

    // checks a predicate and requires it to be a bool
    void expectBool(Expr& e, std::string_view what) {
        auto type = checkExpr(e);
        if (type == context.semaBool || type == context.semaError) return;
        // a boolf is never a condition, since a match or a loop has to go one way, so it gets its own fix, the cutoff
        if (type == context.semaBoolf) {
            engine.printError("cherimoya requires " + std::string(what) + " to be a bool, please compare the boolf with a cutoff, like 'x > 0.5'", e.span, {}, {cutoff(e, e.span)});
            return;
        }
        engine.printError("cherimoya requires " + std::string(what) + " to be a bool, please compare it to something", e.span);
    }

    // turns a function's outputs into the type it yields: () is void, and one output is that output's type
    SemaType* outputType(const FnDecl& fn) {
        if (fn.outputs.empty()) return context.semaVoid;
        if (fn.outputs.size() > 1) return unsupported("multiple outputs", fn.span);
        auto& output = fn.outputs[0];
        if (!output.type) {
            engine.printError("cherimoya requires every output to have a type, please write '" + std::string(output.name.text) + " :: int'", tokSpan(output.name));
            return context.semaError;
        }
        auto type = resolveType(*output.type);
        return type ? type : context.semaError;
    }

    // the first pass over a list of statements: finds the value of every constant let in it, then declares every function in it, so a signature's array sizes can use the constants, and the functions can be used before they're defined
    void hoist(std::vector<std::unique_ptr<Stmt>>& stmts) {
        for (auto& s : stmts) {
            if (s->kind != StmtKind::Let) continue;
            auto& let = static_cast<LetStmt&>(*s);
            if (let.pattern->kind != PatternKind::Binder || (let.type && isMut(*let.type))) continue;
            if (auto value = fold(*let.init, true)) scope->insertConstant(static_cast<BinderPattern&>(*let.pattern).name.text, *value);
        }
        for (auto& s : stmts) {
            auto found = fnOf(s.get());
            for (Stmt* attr = s.get(); attr && attr->kind == StmtKind::Attr; attr = static_cast<AttrStmt&>(*attr).target.get()) {
                if (static_cast<AttrStmt&>(*attr).name.text == "test") checkTest(static_cast<AttrStmt&>(*attr), found);
            }
            if (found == nullptr) continue;
            auto& fn = *found;
            if (fn.name.kind != TokenKind::Identifier) {
                unsupported("operator overloading", tokSpan(fn.name));
                continue;
            }
            if (!fn.generics.empty()) unsupported("generic functions", fn.span);
            std::vector<SemaType*> signature;
            for (auto& p : fn.inputs) {
                auto type = p.type ? resolveType(*p.type) : nullptr;
                if (type == nullptr) {
                    engine.printError("cherimoya requires every parameter to have a type, please write '" + std::string(p.name.text) + " :: int'", tokSpan(p.name));
                    type = context.semaError;
                }
                signature.push_back(type);
            }
            signature.push_back(outputType(fn));
            auto symbol = makeSymbol(fn.name.text, SymbolKind::Function, context.functionType(signature), {false}, tokSpan(fn.name));
            symbol->function = &fn;
            scope->insert(symbol);
            fn.decl = symbol;
        }
    }

    // checks an input's default against the input's type, where it sees the inputs before it and nothing a yield could target, since it runs before the function does
    void checkDefault(Expr& fallback, SemaType* type) {
        auto saved = std::move(targets);
        targets.clear();
        auto actual = checkExpr(fallback, type);
        if (!isCompatible(type, actual)) mismatch(type, actual, fallback.span);
        targets = std::move(saved);
    }

    // checks a @test: it goes in a .toya file, on a function that takes and yields nothing, and its arguments are named, traps a string the trap's message has to hold, and timeout a whole number of seconds, where a test that passes all of that is recorded
    void checkTest(AttrStmt& attr, FnDecl* fn) {
        if (role != Role::Tests) {
            engine.printError("cherimoya requires tests to be in a .toya file, please move it to one", tokSpan(attr.name));
            return;
        }
        if (fn == nullptr) {
            engine.printError("cherimoya requires '@test' to be on a function, please put it before one like 'let () -> name -> ()'", tokSpan(attr.name));
            return;
        }
        if (!fn->inputs.empty() || !fn->outputs.empty()) {
            engine.printError("cherimoya doesn't support tests with inputs and outputs yet, which it will fuzz against their contracts", tokSpan(fn->name));
            return;
        }
        std::string name;
        for (auto outer : enclosing) name += std::string(outer) + ".";
        Test test{fn, name + std::string(fn->name.text), std::nullopt, std::nullopt};
        bool isValid = true;
        std::vector<std::string_view> seen;
        for (auto& arg : attr.args) {
            if (!arg.name) {
                engine.printError("cherimoya requires '@test' arguments to be named, please write 'traps <- \"message\"' or 'timeout <- 5'", arg.value->span);
                isValid = false;
                continue;
            }
            std::string_view key = arg.name->text;
            bool isTraps = key == "traps";
            if (!isTraps && key != "timeout") {
                engine.printError("cherimoya requires '@test' arguments to be 'traps' or 'timeout', please remove '" + std::string(key) + " <-'", tokSpan(*arg.name));
                isValid = false;
                continue;
            }
            if (std::find(seen.begin(), seen.end(), key) != seen.end()) {
                engine.printError("cherimoya requires each '@test' argument once, please remove the second '" + std::string(key) + "'", tokSpan(*arg.name));
                isValid = false;
                continue;
            }
            seen.push_back(key);
            if (isTraps) {
                if (arg.value->kind == ExprKind::StringLiteral) {
                    test.traps = stringLiteralValue(static_cast<StringLiteralExpr&>(*arg.value).literal);
                } else {
                    engine.printError("cherimoya requires 'traps' to be a string, please write the text its trap's message should contain, like 'traps <- \"overflowed\"'", arg.value->span);
                    isValid = false;
                }
                continue;
            }
            // a timeout is an integer literal above 0, which a day's worth of seconds or a little more always fits
            std::optional<uint64_t> seconds;
            if (arg.value->kind == ExprKind::IntLiteral) {
                auto [radix, digits] = literalDigits(static_cast<IntLiteralExpr&>(*arg.value).literal);
                uint64_t value = 0;
                for (char c : digits) {
                    value = value * radix + hexValue(c);
                    if (value > 1000000000) break;
                }
                if (value > 0 && value <= 1000000000) seconds = value;
            }
            if (seconds) {
                test.timeout = seconds;
            } else {
                engine.printError("cherimoya requires 'timeout' to be a whole number of seconds above 0, please write one like 'timeout <- 5'", arg.value->span);
                isValid = false;
            }
        }
        if (isValid) tests.push_back(test);
    }

    // the last pass for a function: its parameters, their defaults, and their refinements, its obligations, then its body in a scope of its own
    void checkFn(FnDecl& fn) {
        if (fn.name.kind != TokenKind::Identifier) return;
        // the signature was resolved when the function was hoisted, so its parameters and outputs are only ever reported once
        auto signature = static_cast<FunctionSemaType*>(fn.decl->type);
        size_t saved = owner;
        owner = ++functions;
        bodies.push_back({fn.span, owner});
        if (fn.body) labels.push_back({fn.body->span, fn.name.text});
        inScope(fn.span, [&] {
            for (size_t i = 0; i < fn.inputs.size(); i++) {
                auto& p = fn.inputs[i];
                if (p.fallback) checkDefault(*p.fallback, signature->params[i]);
                auto param = makeSymbol(p.name.text, SymbolKind::Param, signature->params[i], mutabilityOf(p.type.get(), signature->params[i]), tokSpan(p.name));
                p.decl = param;
                scope->insert(param);
                if (p.type) checkRefinements(*p.type, param);
            }
            // outputs are only visible to contracts, so they get a scope the body never sees, and only a single output has a type of its own
            inScope(fn.span, [&] {
                for (auto& o : fn.outputs) {
                    if (o.fallback) {
                        size_t from = o.type ? o.type->span.end : tokSpan(o.name).end;
                        engine.printError("cherimoya requires defaults to be on a function's inputs, please remove '<- " + textOf(o.fallback->span) + "'", o.fallback->span, {}, {{"Remove the default", {{{from, o.fallback->span.end}, ""}}}});
                    }
                    auto type = fn.outputs.size() == 1 ? signature->returns : context.semaError;
                    auto output = makeSymbol(o.name.text, SymbolKind::Param, type, {false}, tokSpan(o.name));
                    scope->insert(output);
                    if (o.type) checkRefinements(*o.type, output);
                }
                if (fn.obligations) expectBool(*fn.obligations, "a function's obligations");
            });
            // the body is the function's own scope, so a yield at its top level yields the function's result
            if (fn.body) {
                auto output = signature->returns;
                auto name = tokSpan(fn.name);
                targets.push_back(Target{TargetKind::Function, fn.name.text, output, nullptr, {}, false, name});
                enclosing.push_back(fn.name.text);
                bool completes = checkStmts(fn.body->stmts);
                enclosing.pop_back();
                targets.pop_back();
                fn.body->type = output;
                if (completes && output != context.semaVoid && !isPoisoned(output)) {
                    engine.printError("cherimoya requires every path through '" + std::string(fn.name.text) + "' to yield '" + output->toString() + "', please end it with a yield",
                        Span{fn.body->span.end - 1, fn.body->span.end}, {{"'" + std::string(fn.name.text) + "' is declared to yield '" + output->toString() + "' here", name}});
                }
            }
        });
        owner = saved;
    }

    // a let binds one name for now, typed by its annotation, its initializer, or both when they agree
    void checkLet(LetStmt& let) {
        SemaType* declared = let.type ? resolveType(*let.type) : nullptr;
        SemaType* type = declared;
        if (let.init->kind == ExprKind::Wildcard) {
            if (declared == nullptr) {
                engine.printError("cherimoya requires an uninitialized binding to have a type, please write 'let x :: mut int <- _;'", let.span);
                type = context.semaError;
            } else if (!isPoisoned(declared)) {
                // an array whose elements can be written can start out uninitialized too, since that's how it gets its values
                auto levels = mutabilityOf(let.type.get(), declared);
                bool isWritable = levels[0] || (levels.size() > 1 && levels[1]);
                if (!isWritable) engine.printError("cherimoya requires an uninitialized binding to be mutable, please write 'let x :: mut int <- _;'", let.span);
            }
        } else {
            auto init = checkExpr(*let.init, declared);
            if (declared == nullptr) {
                type = init;
            } else if (!isCompatible(declared, init)) {
                mismatch(declared, init, let.init->span);
            }
        }
        if (type == context.semaVoid) {
            engine.printError("cherimoya requires a binding to hold a value, please write it as its own statement instead", let.init->span);
            type = context.semaError;
        }
        if (let.pattern->kind == PatternKind::Wildcard) return;
        if (let.pattern->kind != PatternKind::Binder) {
            unsupported("destructuring", let.pattern->span);
            return;
        }
        auto& binder = static_cast<BinderPattern&>(*let.pattern);
        auto symbol = makeSymbol(binder.name.text, SymbolKind::Binding, type, mutabilityOf(let.type.get(), type), binder.span);
        binder.decl = symbol;
        scope->insert(symbol);
        if (let.type) checkRefinements(*let.type, symbol);
        if (!symbol->isMutable() && let.init->kind != ExprKind::Wildcard) {
            if (auto value = fold(*let.init)) constants[symbol] = *value;
        }
        if (!let.type && !isPoisoned(type)) inferred.push_back(symbol);
    }

    // checks whether a value of type actual can go where expected is wanted, where errors and never fit anywhere
    bool isCompatible(SemaType* expected, SemaType* actual) {
        return expected == actual || expected == context.semaError || actual == context.semaError || actual == context.semaNever;
    }

    // reports a value of the wrong type
    void mismatch(SemaType* expected, SemaType* actual, Span span) {
        engine.printError("cherimoya expected a value of type '" + expected->toString() + "', got '" + actual->toString() + "' instead", span);
    }

    // finds the function a statement declares, looking through any attributes on it, or null if it isn't one
    FnDecl* fnOf(Stmt* stmt) {
        while (stmt != nullptr && stmt->kind == StmtKind::Attr) stmt = static_cast<AttrStmt&>(*stmt).target.get();
        if (stmt == nullptr || stmt->kind != StmtKind::Decl) return nullptr;
        auto& decl = *static_cast<DeclStmt&>(*stmt).decl;
        return decl.kind == DeclKind::Fn ? &static_cast<FnDecl&>(decl) : nullptr;
    }

    // checks a list of statements in three passes: function signatures, then everything else in order, then function bodies, so a body sees every name declared around it no matter where it is, then returns whether control can reach the end of the list, warning once about statements after one that always yields
    bool checkStmts(std::vector<std::unique_ptr<Stmt>>& stmts) {
        hoist(stmts);
        bool completes = true;
        bool warned = false;
        for (size_t i = 0; i < stmts.size(); i++) {
            auto& s = stmts[i];
            if (fnOf(s.get()) != nullptr) continue;
            if (!completes && !warned) {
                // the fix removes every statement from here on but the functions, which are hoisted, so something before the yield can still call them
                Fix fix{"Remove the statements that never run", {}};
                for (size_t j = i; j < stmts.size(); j++) {
                    if (fnOf(stmts[j].get()) == nullptr) fix.edits.push_back(removal(engine.getManager().text(), stmts[j]->span));
                }
                engine.printWarning("cherimoya never runs this statement, since every path before it yields", s->span, {}, {fix});
                warned = true;
            }
            if (checkStmt(*s)) completes = false;
        }
        for (auto& s : stmts) {
            if (auto fn = fnOf(s.get())) checkFn(*fn);
        }
        return completes;
    }

    // checks statements as a scope a yield can target, in a new lexical scope, and gives the type of what reaches its end, where what names the scope for diagnostics
    SemaType* checkScope(std::vector<std::unique_ptr<Stmt>>& stmts, TargetKind kind, const std::optional<Token>& name, Span span, std::string_view what) {
        targets.push_back(Target{kind, labelOf(name), nullptr, nullptr, {}, false, span});
        if (name) labels.push_back({span, name->text});
        bool completes = true;
        inScope(span, [&] { completes = checkStmts(stmts); });
        auto target = targets.back();
        targets.pop_back();
        return finish(target, completes, what);
    }

    // checks a block, which is a match arm's body when kind is Arm
    SemaType* checkBlock(BlockExpr& block, TargetKind kind = TargetKind::Block) {
        block.type = checkScope(block.stmts, kind, block.name, block.span, "this block");
        return block.type;
    }

    // works out a scope's type once it's checked: what was yielded to it, joined with void if control can fall off its end, or never if nothing reaches past it at all
    SemaType* finish(const Target& target, bool completes, std::string_view what) {
        if (target.yielded == nullptr) return completes ? context.semaVoid : context.semaNever;
        if (!completes || target.yielded == context.semaVoid || isPoisoned(target.yielded)) return target.yielded;
        engine.printError("cherimoya requires every path through " + std::string(what) + " to yield '" + target.yielded->toString() + "', please end it with a yield",
            Span{target.span.end - 1, target.span.end}, {{"this yield gives it '" + target.yielded->toString() + "'", target.first}});
        return context.semaError;
    }

    // finds the target a yield goes to, searching outward from the innermost and never past the nearest function
    Target* resolveTarget(const YieldStmt& y) {
        for (size_t i = targets.size(); i-- > 0;) {
            auto& target = targets[i];
            if (!y.target) return &target;
            auto kind = y.target->kind;
            if (kind == TokenKind::KwMatch && target.kind == TargetKind::Match) return &target;
            if (kind == TokenKind::KwRepeat && target.kind == TargetKind::Loop) return &target;
            if (kind == TokenKind::Identifier && target.label == y.target->text) return &target;
            if (target.kind == TargetKind::Function) break;
        }
        return nullptr;
    }

    // checks a yield against its target, and records its type for the target's own type
    void checkYield(YieldStmt& y) {
        // the target is found first, so the value can take the type it wants: a function's output, or whatever an earlier yield gave a scope
        auto target = resolveTarget(y);
        SemaType* wanted = nullptr;
        if (target) wanted = target->kind == TargetKind::Function ? target->expected : target->yielded;
        auto value = y.value ? checkExpr(*y.value, wanted) : context.semaVoid;
        auto span = y.value ? y.value->span : y.span;
        // the value can hold scopes that push targets of their own, which can move the stack, so the target is found again instead of kept
        target = resolveTarget(y);
        if (target == nullptr) {
            if (!y.target) {
                engine.printError("cherimoya requires a yield to be inside a function or scope, please move it into one", y.span);
            } else {
                std::string name(y.target->text);
                engine.printError("cherimoya expected a function or scope named '" + name + "' around this yield, got none instead", tokSpan(*y.target));
            }
            return;
        }
        if (isPoisoned(value)) return;
        // a function's yields have to match its output
        if (target->kind == TargetKind::Function) {
            if (!isCompatible(target->expected, value)) {
                engine.printError("cherimoya expected a yield of type '" + target->expected->toString() + "' to '" + std::string(*target->label) + "', got '" + value->toString() + "' instead",
                    span, {{"'" + std::string(*target->label) + "' is declared to yield '" + target->expected->toString() + "' here", target->span}});
            }
            return;
        }
        if (target->kind == TargetKind::Loop && !target->isInfinite && value != context.semaVoid) {
            unsupported("yielding a value out of a loop with a header", span);
            contribute(*target, context.semaError, span);
            return;
        }
        contribute(*target, value, span);
    }

    // gives a target one more way to finish: the first one sets its type, and every later one has to agree
    void contribute(Target& target, SemaType* value, Span span) {
        if (value == context.semaNever) return;
        if (target.yielded == nullptr || isPoisoned(target.yielded)) {
            target.yielded = value;
            target.first = span;
        } else if (target.yielded != value && !isPoisoned(value)) {
            engine.printError("cherimoya requires every yield to one scope to have the same type, please make this one '" + target.yielded->toString() + "'",
                span, {{"this yield gives it '" + target.yielded->toString() + "'", target.first}});
        }
    }

    // checks one arm's pattern against the subject's type, declaring what it binds, and returns whether it matches every value
    bool checkPattern(Pattern& p, SemaType* subject) {
        switch (p.kind) {
            case PatternKind::Wildcard:
                return true;
            case PatternKind::Binder: {
                auto& binder = static_cast<BinderPattern&>(p);
                binder.decl = makeSymbol(binder.name.text, SymbolKind::Binding, subject, {false}, binder.span);
                scope->insert(binder.decl);
                return true;
            }
            case PatternKind::Literal: {
                auto type = checkExpr(*static_cast<LiteralPattern&>(p).literal, subject);
                if (!isPoisoned(subject) && !isPoisoned(type) && type != subject) {
                    engine.printError("cherimoya expected a pattern of type '" + subject->toString() + "', got '" + type->toString() + "' instead", p.span);
                }
                return false;
            }
            case PatternKind::Guard:
                expectBool(*static_cast<GuardPattern&>(p).cond, "a guard");
                return false;
            case PatternKind::Variant:
                unsupported("variant patterns", p.span);
                return false;
            case PatternKind::Pack:
                unsupported("pack patterns", p.span);
                return false;
            case PatternKind::Range:
                unsupported("range patterns", p.span);
                return false;
            case PatternKind::SetBuilder:
                unsupported("set-builder patterns", p.span);
                return false;
            default:
                return false;
        }
    }

    // checks a match: each arm is a target of its own whose value goes to the match, and the arms have to cover every value that the match needs, which for now means a catch-all, or both true and false on a bool
    SemaType* checkMatch(MatchExpr& m) {
        auto subject = m.subject ? checkExpr(*m.subject) : nullptr;
        targets.push_back(Target{TargetKind::Match, labelOf(m.name), nullptr, nullptr, {}, false, m.span});
        if (m.name) labels.push_back({m.span, m.name->text});
        bool covered = false;
        bool seenTrue = false;
        bool seenFalse = false;
        const MatchArm* catchAll = nullptr;
        bool warned = false;
        for (size_t i = 0; i < m.arms.size(); i++) {
            auto& arm = m.arms[i];
            if (covered && !warned) {
                std::vector<Note> notes;
                if (catchAll) notes.push_back({"this arm matches every value", catchAll->pattern->span});
                // every arm from here on is just as unreachable, so the fix removes them all
                Fix fix{"Remove the arms that never run", {}};
                for (size_t j = i; j < m.arms.size(); j++) fix.edits.push_back(removal(engine.getManager().text(), m.arms[j].span));
                engine.printWarning("cherimoya never reaches this arm, since the arms before it match every value", arm.pattern->span, notes, {fix});
                warned = true;
            }
            SemaType* type = nullptr;
            inScope(arm.span, [&] {
                bool all = checkPattern(*arm.pattern, subject ? subject : context.semaError);
                if (all && !covered) catchAll = &arm;
                if (arm.pattern->kind == PatternKind::Literal) {
                    auto& literal = *static_cast<LiteralPattern&>(*arm.pattern).literal;
                    if (literal.kind == ExprKind::BoolLiteral) (static_cast<BoolLiteralExpr&>(literal).literal ? seenTrue : seenFalse) = true;
                }
                covered = covered || all || (subject == context.semaBool && seenTrue && seenFalse);
                type = checkBlock(*arm.body, TargetKind::Arm);
            });
            contribute(targets.back(), type, arm.body->span);
        }
        auto target = targets.back();
        targets.pop_back();
        if (covered || (subject && isPoisoned(subject))) return finish(target, false, "this match");
        bool yieldsValue = target.yielded != nullptr && target.yielded != context.semaVoid && !isPoisoned(target.yielded);
        if (isNumeric(subject) || isCharacter(subject) || subject == context.semaBoolf || yieldsValue) {
            std::string what = subject ? "a match on '" + subject->toString() + "'" : "a match that yields a value";
            engine.printError("cherimoya requires " + what + " to cover every value, please add a '_ -> { ... }' arm", Span{m.span.start, m.span.start + 5}, {}, catchAllArm(m));
            return context.semaError;
        }
        return finish(target, true, "this match");
    }

    // checks a loop: one with a condition is void, and one without only ends by a yield, so it takes that yield's type
    SemaType* checkRepeat(RepeatExpr& r) {
        if (r.binder) return unsupported("for-each loops", r.binder->span);
        if (r.cond) expectBool(*r.cond, "a loop's condition");
        if (r.invariant) expectBool(*r.invariant, "a loop's invariant");
        targets.push_back(Target{TargetKind::Loop, labelOf(r.name), nullptr, nullptr, {}, r.cond == nullptr, r.span});
        if (r.name) labels.push_back({r.span, r.name->text});
        inScope(r.span, [&] { checkStmts(r.body->stmts); });
        auto target = targets.back();
        targets.pop_back();
        r.body->type = context.semaVoid;
        if (r.cond) return target.yielded == context.semaError ? static_cast<SemaType*>(context.semaError) : context.semaVoid;
        return target.yielded ? target.yielded : context.semaNever;
    }

    // checks a chunk, which is checked like a block, since it only differs once it has an allocator
    SemaType* checkChunk(ChunkExpr& c) {
        c.body->type = checkScope(c.body->stmts, TargetKind::Chunk, c.name, c.span, "this chunk");
        return c.body->type;
    }

    // checks one statement, and returns whether control never continues past it
    bool checkStmt(Stmt& s) {
        switch (s.kind) {
            case StmtKind::Let: {
                auto& let = static_cast<LetStmt&>(s);
                checkLet(let);
                return let.init->type == context.semaNever;
            }
            case StmtKind::Assign: {
                auto& assign = static_cast<AssignStmt&>(s);
                if (assign.place->kind == ExprKind::Wildcard) {
                    auto value = checkExpr(*assign.value);
                    assign.place->type = value;
                    return value == context.semaNever;
                }
                // the place is checked first, so the value can take its type: x <- 5 and x +<- 1 both make the literal x's type
                auto place = checkPlace(*assign.place);
                auto value = checkExpr(*assign.value, place);
                // place op<- value is place <- place op value, which has to give back the place's type
                if (assign.op && assign.op->op->base->category == OpCategory::Extra) {
                    unsupported("extra operators", Span{assign.op->loc, assign.op->loc + assign.op->text.length()});
                } else if (assign.op) {
                    auto result = binaryType(*assign.op->op->base, place, assign.place->span, value, assign.value->span);
                    if (!isCompatible(place, result)) mismatch(place, result, assign.value->span);
                } else if (!isCompatible(place, value)) {
                    mismatch(place, value, assign.value->span);
                }
                return value == context.semaNever;
            }
            // a value can't be dropped on the floor: it has to be used or explicitly discarded
            case StmtKind::Expr: {
                auto& expr = *static_cast<ExprStmt&>(s).expr;
                auto type = checkExpr(expr);
                if (type == context.semaVoid || isPoisoned(type)) return type == context.semaNever;
                engine.printError("cherimoya requires a value to be used or discarded, please write '_ <- " + textOf(expr.span) + ";'", expr.span, {}, {{"Discard it with '_ <- '", {insertion(expr.span.start, "_ <- ")}}});
                return false;
            }
            case StmtKind::Yield:
                checkYield(static_cast<YieldStmt&>(s));
                return true;
            case StmtKind::Wit: {
                auto& wit = static_cast<WitStmt&>(s);
                if (wit.bound) {
                    unsupported("protocols", wit.span);
                    break;
                }
                expectBool(*wit.predicate, "a 'wit' obligation");
                break;
            }
            case StmtKind::Assert:
                expectBool(*static_cast<AssertStmt&>(s).cond, "an assertion");
                break;
            case StmtKind::Decl: {
                auto& decl = *static_cast<DeclStmt&>(s).decl;
                switch (decl.kind) {
                    case DeclKind::Type:
                        unsupported("type definitions", decl.span);
                        break;
                    case DeclKind::Export:
                    case DeclKind::Import:
                        unsupported("modules", decl.span);
                        break;
                    default:
                        break;
                }
                break;
            }
            case StmtKind::Attr: {
                auto& attr = static_cast<AttrStmt&>(s);
                return attr.target && checkStmt(*attr.target);
            }
            default:
                break;
        }
        return false;
    }

    // gives an identifier its symbol, and stops nested functions from reaching the locals of the function around them
    SemaType* checkIdentifier(IdentifierExpr& id, bool callee) {
        auto symbol = scope->lookup(id.name);
        if (symbol == nullptr) {
            std::string name(id.name);
            engine.printError("cherimoya requires names to be declared before they're used, please declare '" + name + "' with 'let " + name + " <- ...;'", id.span);
            return context.semaError;
        }
        if (symbol->kind != SymbolKind::Function && symbol->owner != 0 && symbol->owner != owner) {
            std::string name(id.name);
            engine.printError("cherimoya uses closures to capture '" + name + "' instead of nested functions as follows: let f <- [" + name + "] (...) -> { ... };",
                id.span, {{"'" + name + "' is declared here, in the function around this one", symbol->span}});
            return context.semaError;
        }
        id.decl = symbol;
        uses.push_back({id.span, symbol, false});
        if (symbol->kind == SymbolKind::Function && !callee) return unsupported("functions as values", id.span);
        return symbol->type;
    }

    // the type an array's elements have to be written as so a level of them can be written: arr[2] arr[3] int at level 1 is arr[2] arr[3] mut int
    std::string writableAt(SemaType* type, size_t level) {
        auto array = asArray(type);
        if (!array) return type->toString();
        std::string head = "arr[" + std::to_string(array->size) + "] ";
        return head + (level == 0 ? "mut " + array->element->toString() : writableAt(array->element, level - 1));
    }

    // checks an element as the left side of an assignment, xs[i] or m[i][j], where the array has to be a binding whose elements at that depth can be written
    SemaType* checkElementPlace(IndexExpr& place) {
        auto type = checkExpr(place);
        size_t depth = 0;
        Expr* root = &place;
        while (root->kind == ExprKind::Index) {
            root = static_cast<IndexExpr&>(*root).object.get();
            depth++;
        }
        auto symbol = root->kind == ExprKind::Identifier ? static_cast<IdentifierExpr&>(*root).decl : nullptr;
        if (symbol == nullptr || symbol->kind == SymbolKind::Function) {
            if (!isPoisoned(type)) engine.printError("cherimoya requires the left side of an assignment to be a binding or its elements, please store the value in a 'let' first", place.span);
            return type;
        }
        auto& levels = symbol->mutability;
        if (isPoisoned(type) || (depth < levels.size() && levels[depth])) return type;
        std::string name(symbol->name);
        engine.printError("cherimoya requires the elements of '" + name + "' to be mutable to assign to them, please declare it as '" + writableAt(symbol->type, depth - 1) + "'",
            place.span, {{"'" + name + "' is declared here", symbol->span}}, mutability(*symbol, true));
        return type;
    }

    // checks the left side of an assignment: it has to be a binding declared mut, or an element it can write, and gives the type it holds
    SemaType* checkPlace(Expr& place) {
        if (place.kind == ExprKind::Index) return checkElementPlace(static_cast<IndexExpr&>(place));
        if (place.kind != ExprKind::Identifier) return checkExpr(place);
        auto& identifier = static_cast<IdentifierExpr&>(place);
        auto type = checkIdentifier(identifier, true);
        place.type = type;
        auto symbol = identifier.decl;
        if (symbol == nullptr || symbol->isMutable()) return type;
        std::string name(identifier.name);
        if (symbol->kind == SymbolKind::Function) {
            engine.printError("cherimoya requires the left side of an assignment to be a binding, please assign to a 'let' instead of the function '" + name + "'", place.span);
            return context.semaError;
        }
        auto fix = symbol->kind == SymbolKind::Param && !isPoisoned(type) ? "declare it as '" + name + " :: mut " + type->toString() + "'" : "declare it with 'let " + name + " :: mut <- ...;'";
        engine.printError("cherimoya requires '" + name + "' to be mutable to assign to it, please " + fix, place.span, {{"'" + name + "' is declared here", symbol->span}}, mutability(*symbol));
        return type;
    }

    // checks whether a type carries an earlier error or comes from a yield, so nothing built from it reports again
    bool isPoisoned(SemaType* type) {
        return type == context.semaError || type == context.semaNever;
    }

    // checks whether a type is one of the numeric types
    bool isNumeric(SemaType* type) {
        return asInt(type) || asFloat(type);
    }

    // checks whether a type is an unsigned byte, i8u or i8uw, which holds a boolf's raw bits
    bool isByte(SemaType* type) {
        IntSemaType* integer = asInt(type);
        return integer && integer->bits == 8 && !integer->isSigned;
    }

    // the value of one digit in any base up to 16
    unsigned digitValue(char c) {
        char lower = c | 0x20;
        return ('a' <= lower && lower <= 'f') ? 10 + (lower - 'a') : (c - '0');
    }

    // reads an integer literal's value from its raw text, or nothing if it's wider than 128 bits, which no type holds
    std::optional<unsigned __int128> intLiteralValue(std::string_view text) {
        auto [radix, digits] = literalDigits(text);
        unsigned __int128 value = 0;
        for (char c : digits) {
            unsigned digit = digitValue(c);
            if (value > (~(unsigned __int128)0 - digit) / radix) return std::nullopt;
            value = value * radix + digit;
        }
        return value;
    }

    // reads a float literal's value from its raw text as precisely as a long double can, which is plenty for checking it against a type's range, in any base, since 0x1.8 is 1.5
    long double floatLiteralValue(std::string_view text) {
        auto [radix, digits] = literalDigits(text);
        long double value = 0;
        // the place value of the next digit after the point, which starts at 1 / radix and shrinks by radix each digit, and is 0 before the point
        long double place = 0;
        for (char c : digits) {
            if (c == '.') {
                place = 1.0L / radix;
                continue;
            }
            unsigned digit = digitValue(c);
            if (place == 0) {
                value = value * radix + digit;
            } else {
                value += digit * place;
                place /= radix;
            }
        }
        return value;
    }

    // checks whether a whole number is exact in a float type, so an integer literal can stand for it: its odd part has to fit in the significand, and the whole number in the range
    bool isExactIn(unsigned __int128 value, const FloatSemaType& type) {
        if ((long double)value > type.max) return false;
        unsigned __int128 odd = value;
        while (odd != 0 && (odd & 1) == 0) odd >>= 1;
        return odd < ((unsigned __int128)1 << type.precision);
    }

    // checks whether a literal's value, negated or not, fits in an integer type
    bool isInRange(unsigned __int128 value, bool negated, const IntSemaType& type) {
        // the largest magnitude the type holds in each direction: an unsigned type holds nothing below zero but zero itself
        unsigned __int128 one = 1;
        unsigned __int128 maxPositive = (type.isSigned) ? (one << (type.bits - 1)) - 1 : (type.bits == 128) ? ~(unsigned __int128)0 : (one << type.bits) - 1;
        unsigned __int128 maxNegative = (type.isSigned) ? (one << (type.bits - 1)) : 0;
        return negated ? value <= maxNegative : value <= maxPositive;
    }

    // types an integer literal, adapting to the expected type when that's an integer and defaulting to int otherwise, and checks that its value fits there
    SemaType* intLiteral(IntLiteralExpr& literal, SemaType* expected, bool negated, Span span) {
        auto value = intLiteralValue(literal.literal);
        std::string text = (negated ? "-" : "") + std::string(literal.literal);
        // a boolf literal is a truth value, so the only whole numbers it takes are 0 and 1
        if (expected == context.semaBoolf) {
            if (!value || *value > 1 || (negated && *value != 0)) {
                engine.printError("cherimoya requires " + text + " to be from 0 to 1 to be a 'boolf', please use a value in that range", span);
                literal.type = context.semaError;
                return context.semaError;
            }
            literal.type = context.semaBoolf;
            return context.semaBoolf;
        }
        // a whole number can stand for a float when the float holds it exactly, so x * 2 works on a float x
        if (FloatSemaType* floating = asFloat(expected)) {
            if (!value || !isExactIn(*value, *floating)) {
                engine.printError("cherimoya requires " + text + " to be exact in '" + floating->toString() + "', please write it as a float literal or use a wider type", span);
                literal.type = context.semaError;
                return context.semaError;
            }
            literal.type = floating;
            return floating;
        }
        IntSemaType* integer = asInt(expected) ? asInt(expected) : context.semaInt;
        if (!value || !isInRange(*value, negated, *integer)) {
            engine.printError("cherimoya requires " + text + " to fit in '" + integer->toString() + "', please use a wider type or convert it explicitly", span);
            literal.type = context.semaError;
            return context.semaError;
        }
        literal.type = integer;
        return integer;
    }

    // types a float literal, adapting to the expected type when that's a float and defaulting to float otherwise, and checks that it's at most the type's largest value, so a literal never quietly rounds to the largest value or to infinity
    SemaType* floatLiteral(FloatLiteralExpr& literal, SemaType* expected, bool negated, Span span) {
        // a boolf literal is a truth value from 0 to 1, which rounds to the nearest of its 256 steps
        if (expected == context.semaBoolf) {
            long double value = floatLiteralValue(literal.literal);
            if (value > 1 || (negated && value != 0)) {
                std::string text = (negated ? "-" : "") + std::string(literal.literal);
                engine.printError("cherimoya requires " + text + " to be from 0 to 1 to be a 'boolf', please use a value in that range", span);
                literal.type = context.semaError;
                return context.semaError;
            }
            literal.type = context.semaBoolf;
            return context.semaBoolf;
        }
        FloatSemaType* floating = asFloat(expected) ? asFloat(expected) : context.semaFloat;
        if (floatLiteralValue(literal.literal) > floating->max) {
            std::string text = (negated ? "-" : "") + std::string(literal.literal);
            engine.printError("cherimoya requires " + text + " to fit in '" + floating->toString() + "', please use a wider type", span);
            literal.type = context.semaError;
            return context.semaError;
        }
        literal.type = floating;
        return floating;
    }

    // types a char literal, adapting to the expected type when that's a char or a rune and otherwise being a char, or a rune when it isn't ASCII, and checks that a char's value is ASCII
    SemaType* charLiteral(CharLiteralExpr& literal, SemaType* expected) {
        uint32_t value = charLiteralValue(literal.literal);
        if (expected == context.semaRune) return context.semaRune;
        if (expected == context.semaChar && value > 0x7F) {
            engine.printError("cherimoya requires " + std::string(literal.literal) + " to fit in 'char', which is ASCII, please use a 'rune'", literal.span);
            return context.semaError;
        }
        return value > 0x7F ? context.semaRune : context.semaChar;
    }

    // the literal an expression is, looking through a minus, or null if it isn't a number literal: -128 is the literal 128, negated
    const Expr* literalOf(const Expr& e) {
        const Expr* inner = &e;
        if (e.kind == ExprKind::Unary && static_cast<const UnaryExpr&>(e).op.kind == TokenKind::Minus) inner = static_cast<const UnaryExpr&>(e).operand.get();
        return inner->kind == ExprKind::IntLiteral || inner->kind == ExprKind::FloatLiteral ? inner : nullptr;
    }

    // checks whether an expression is a number literal, possibly negated, a char literal, or a bool literal, which takes its type from around it instead of deciding one
    bool isLiteral(const Expr& e) {
        return literalOf(e) != nullptr || e.kind == ExprKind::CharLiteral || e.kind == ExprKind::BoolLiteral;
    }

    // checks whether an expression is a float literal, possibly negated, which decides the type of the whole numbers around it, since they can be exact in a float and never the other way round
    bool isFloatLiteral(const Expr& e) {
        const Expr* literal = literalOf(e);
        return literal != nullptr && literal->kind == ExprKind::FloatLiteral;
    }

    // reports an operand of the wrong type for an operator
    void badOperand(std::string_view wanted, std::string_view side, const OpInfo& op, SemaType* actual, Span span) {
        engine.printError("cherimoya expected " + std::string(wanted) + " " + std::string(side) + " '" + op.spelling + "', got '" + actual->toString() + "' instead", span);
    }

    // works out what a prefix operator gives: - on numbers, ! on bools, and ~ on ints
    SemaType* unaryType(const OpInfo& op, SemaType* operand, Span span) {
        if (isPoisoned(operand)) return operand;
        // negating a trapping unsigned value overflows for everything but 0, so only signed and wrapping integers can be negated
        IntSemaType* integer = asInt(operand);
        if (op.spelling == "-" && integer) {
            if (!integer->isSigned && !integer->isWrapping) {
                std::string bits = std::to_string(integer->bits);
                engine.printError("cherimoya requires negation to be on a signed or wrapping integer, please use an 'i" + bits + "' or an 'i" + bits + "uw' instead of '" + integer->toString() + "'", span);
                return context.semaError;
            }
            return operand;
        }
        if (op.spelling == "-" && isNumeric(operand)) return operand;
        if (op.spelling == "!" && (operand == context.semaBool || operand == context.semaBoolf)) return operand;
        if (op.spelling == "~" && integer) return operand;
        auto wanted = op.spelling == "-" ? "an int or a float" : op.spelling == "!" ? "a bool or a boolf" : "an int";
        badOperand(wanted, "after", op, operand, span);
        return context.semaError;
    }

    // works out what a built-in binary operator gives, where both sides must have the same type
    SemaType* binaryType(const OpInfo& op, SemaType* lhs, Span lhsSpan, SemaType* rhs, Span rhsSpan) {
        if (lhs == context.semaError || rhs == context.semaError) return context.semaError;
        if (lhs == context.semaNever || rhs == context.semaNever) return context.semaNever;
        // whether a side is a type the operator takes, what it takes, and what it gives back
        auto takes = [&](SemaType* side) {
            switch (op.category) {
                case OpCategory::Arithmetic:
                    // a boolf only takes *, the product, since + and - can leave 0 to 1
                    return isNumeric(side) || (op.spelling == "*" && side == context.semaBoolf);
                case OpCategory::Comparison:
                    // a comparison is how a cutoff turns a boolf into a bool
                    return isNumeric(side) || isCharacter(side) || side == context.semaBoolf;
                case OpCategory::Bitwise:
                    return asInt(side) != nullptr;
                case OpCategory::Logical:
                    // both sides having the same type is checked below, so a bool never mixes with a boolf
                    return side == context.semaBool || side == context.semaBoolf;
                default:
                    return side != context.semaVoid && side->kind != SemaTypeKind::Function;
            }
        };
        std::string_view wanted;
        switch (op.category) {
            case OpCategory::Arithmetic:
                wanted = op.spelling == "*" ? "an int, a float, or a boolf" : "an int or a float";
                break;
            case OpCategory::Comparison:
                wanted = "an int, a float, a character, or a boolf";
                break;
            case OpCategory::Bitwise:
                wanted = "an int";
                break;
            case OpCategory::Logical:
                wanted = "a bool or a boolf";
                break;
            case OpCategory::Equality:
                wanted = "a value";
                break;
            default:
                return context.semaError;
        }
        // comparisons always give a bool, and logic gives whichever of bool and boolf its operands are
        bool givesBool = op.category == OpCategory::Comparison || op.category == OpCategory::Equality;
        SemaType* result = givesBool ? context.semaBool : lhs;
        if (!takes(lhs) || !takes(rhs)) {
            bool left = !takes(lhs);
            badOperand(wanted, left ? "on the left of" : "on the right of", op, left ? lhs : rhs, left ? lhsSpan : rhsSpan);
            return context.semaError;
        }
        if (lhs != rhs) {
            engine.printError("cherimoya requires both sides of '" + op.spelling + "' to have the same type, please convert one of them", Span{lhsSpan.start, rhsSpan.end},
                {{"this side is '" + lhs->toString() + "'", lhsSpan}, {"this side is '" + rhs->toString() + "'", rhsSpan}});
            return context.semaError;
        }
        return result;
    }

    // an argument as sema checks it: the name it's given, if any, its value, and the span of both, where a pipe's value is an argument with no name
    struct Argument {
        std::optional<Token> name;
        Expr* value;
        Span span;
    };

    // a call's arguments, as sema checks them
    std::vector<Argument> argumentsOf(std::vector<Elem>& elems) {
        std::vector<Argument> args;
        for (auto& e : elems) args.push_back({e.name, e.value.get(), Span{e.name ? e.name->loc : e.value->span.start, e.value->span.end}});
        return args;
    }

    // checks a conversion, a type called on one value: i32(x) converts any number or character to i32, f32(x) any number or boolf to f32, rune(x) any integer or character to rune, and boolf(x) a float, a byte, or a bool to boolf, where codegen checks the ones that can lose the value
    SemaType* checkConversion(TypeExprExpr& callee, const std::vector<Argument>& args, Span span) {
        auto target = resolveType(*callee.typeExpr);
        callee.type = target;
        for (auto& arg : args) {
            checkExpr(*arg.value);
            if (arg.name) engine.printError("cherimoya requires a conversion's value to be unnamed, please remove '" + std::string(arg.name->text) + " <-'", tokSpan(*arg.name), {}, {{"Remove '" + std::string(arg.name->text) + " <-'", {{{arg.name->loc, arg.value->span.start}, ""}}}});
        }
        if (target == nullptr || isPoisoned(target)) return context.semaError;
        if (args.size() != 1) {
            engine.printError("cherimoya expected 1 value to convert in the call to '" + target->toString() + "', got " + std::to_string(args.size()) + " instead", span);
            return context.semaError;
        }
        auto source = args[0].value->type;
        if (isPoisoned(source)) return context.semaError;
        // numbers convert to each other, and characters convert to each other and to and from integers, but never to or from floats
        bool isValid = (isNumeric(target) && isNumeric(source)) || (isCharacter(target) && (isCharacter(source) || asInt(source))) || (asInt(target) && isCharacter(source));
        // a boolf converts to and from floats and its raw byte, and a bool becomes one, but a boolf only becomes a bool by a cutoff
        if (target == context.semaBoolf) isValid = asFloat(source) || isByte(source) || source == context.semaBool || source == context.semaBoolf;
        if (source == context.semaBoolf && target != context.semaBoolf) isValid = asFloat(target) || isByte(target);
        if (source == context.semaBoolf && target == context.semaBool) {
            engine.printError("cherimoya requires a boolf to become a bool by a cutoff, please compare it, like 'x > 0.5'", span, {}, {cutoff(*args[0].value, span)});
            return context.semaError;
        }
        if (!isValid) {
            engine.printError("cherimoya requires a conversion to be between numbers, between characters and integers, or between a boolf and a float or a byte, please convert '" + source->toString() + "' to '" + target->toString() + "' some other way", span);
            return context.semaError;
        }
        return callee.type;
    }

    // checks an annotation, (value :: T), which checks the value against T and never converts it, so a literal takes T's type
    SemaType* checkAnnotation(AnnExpr& annotation) {
        auto target = resolveType(*annotation.annotation);
        auto actual = checkExpr(*annotation.expr, target);
        if (target == nullptr) return actual;
        if (!isCompatible(target, actual)) mismatch(target, actual, annotation.expr->span);
        return target;
    }

    // reports a call with the wrong number of arguments, where bound is "at least " or "at most " when the function's defaults leave a range
    void countMismatch(const std::string& name, size_t expected, const std::string& bound, size_t got, Span span, const std::vector<Note>& notes) {
        engine.printError("cherimoya expected " + bound + std::to_string(expected) + " argument" + (expected == 1 ? "" : "s") + " in the call to '" + name + "', got " + std::to_string(got) + " instead", span, notes);
    }

    // works out which input of a function each argument goes to, unnamed ones in order and then named ones by name, reporting an argument that goes nowhere and every input without a default that gets nothing, where inputs gets the value each input takes, null where it takes its default, and the result is each argument's input, if it has one
    std::vector<std::optional<size_t>> matchArguments(const Symbol& function, const std::vector<Argument>& args, Span span, std::vector<Expr*>& inputs) {
        auto& params = function.function->inputs;
        std::string name(function.name);
        std::vector<Note> declared{{"'" + name + "' is declared here", function.span}};
        inputs.assign(params.size(), nullptr);
        std::vector<std::optional<size_t>> slots(args.size());
        // the first input that no argument has gone to yet, which the next unnamed argument goes to
        auto nextFree = [&]() -> std::optional<size_t> {
            for (size_t i = 0; i < params.size(); i++) {
                if (inputs[i] == nullptr) return i;
            }
            return std::nullopt;
        };
        bool hasDefaults = std::any_of(params.begin(), params.end(), [](const Param& p) { return p.fallback != nullptr; });
        size_t required = std::count_if(params.begin(), params.end(), [](const Param& p) { return p.fallback == nullptr; });
        bool isNamed = false;
        size_t position = 0;
        for (size_t i = 0; i < args.size(); i++) {
            auto& arg = args[i];
            std::optional<size_t> slot;
            if (arg.name) {
                isNamed = true;
                auto found = std::find_if(params.begin(), params.end(), [&](const Param& p) { return p.name.text == arg.name->text; });
                if (found == params.end()) {
                    std::string names;
                    for (size_t j = 0; j < params.size(); j++) names += (j ? ", '" : "'") + std::string(params[j].name.text) + "'";
                    std::string fix = params.empty() ? "please remove it, since it has none" : "please use one of its inputs: " + names;
                    engine.printError("cherimoya requires a named argument to name an input of '" + name + "', " + fix, tokSpan(*arg.name), declared);
                    continue;
                }
                slot = found - params.begin();
            } else if (isNamed) {
                // it still goes to the first input nothing has gone to, so the inputs after it aren't reported as missing too
                engine.printError("cherimoya requires unnamed arguments to come before named ones, please name this one too", arg.span);
                slot = nextFree();
            } else if (position < params.size()) {
                slot = position++;
            } else {
                // only the first argument past the end is reported, since the rest are past it too
                if (position++ == params.size()) countMismatch(name, params.size(), hasDefaults ? "at most " : "", args.size(), arg.span, declared);
                continue;
            }
            if (!slot) continue;
            if (inputs[*slot] != nullptr) {
                engine.printError("cherimoya requires each input to get one argument, please remove one of the arguments for '" + std::string(params[*slot].name.text) + "'", arg.span, {{"'" + std::string(params[*slot].name.text) + "' already gets this argument", inputs[*slot]->span}});
                continue;
            }
            inputs[*slot] = arg.value;
            slots[i] = slot;
        }
        std::vector<std::string> missing;
        for (size_t i = 0; i < params.size(); i++) {
            if (inputs[i] == nullptr && params[i].fallback == nullptr) missing.push_back("'" + std::string(params[i].name.text) + "'");
        }
        // a call with fewer arguments than the inputs that need one says how many, and one that has enough but leaves a needed input out, which only naming can do, says which
        if (args.size() < required) {
            countMismatch(name, required, hasDefaults ? "at least " : "", args.size(), span, declared);
        } else if (!missing.empty()) {
            std::string list = missing[0];
            for (size_t i = 1; i < missing.size(); i++) list += (i + 1 == missing.size() ? " and " : ", ") + missing[i];
            engine.printError("cherimoya requires the call to '" + name + "' to pass " + list + ", please add an argument for " + (missing.size() == 1 ? "it" : "each"), span, declared);
        }
        return slots;
    }

    // checks whether a type has a method by a protocol it implements, where the only protocol so far is the built-in Sized, whose len every array has, until protocols can be written
    bool hasMethod(SemaType* type, std::string_view name) {
        return asArray(type) != nullptr && name == "len";
    }

    // reports a method with no object, .len, anywhere but a pipe, which is what gives it one
    SemaType* unpipedMethod(MemberExpr& member) {
        engine.printError("cherimoya requires a method without its object, '." + std::string(member.field) + "', to be piped into, please write 'x |> ." + std::string(member.field) + "'", member.span);
        return context.semaError;
    }

    // checks a method used without calling it, xs.len, which needs its parentheses, or a field, which records bring
    SemaType* checkMember(MemberExpr& member) {
        if (!member.object) return unpipedMethod(member);
        auto type = checkExpr(*member.object);
        if (isPoisoned(type)) return type;
        if (hasMethod(type, member.field)) {
            engine.printError("cherimoya requires a method to be called, please write '" + textOf(member.span) + "()'", member.span, {}, {{"Call it with '()'", {insertion(member.span.end, "()")}}});
            return context.semaError;
        }
        return unsupported("fields", member.span);
    }

    // checks whether a name is a method of any protocol, which is what makes a call by that name worth checking as a method first
    bool isMethodName(std::string_view name) {
        return name == "len";
    }

    // checks a method call on an object, the rest of its arguments given, which calls the method a protocol the object's type implements gives it, where Sized's len takes nothing more and gives how many elements an array holds, as an i64u, the type that indexes it
    SemaType* checkMethod(Expr& object, std::string_view name, const std::vector<Argument>& args, Span span) {
        auto type = object.type ? object.type : checkExpr(object);
        for (auto& arg : args) checkExpr(*arg.value);
        if (isPoisoned(type)) return type;
        if (!hasMethod(type, name)) {
            engine.printError("cherimoya expected a method that '" + type->toString() + "' has, got '" + std::string(name) + "' instead", span);
            return context.semaError;
        }
        if (!args.empty()) {
            engine.printError("cherimoya expected 0 arguments in the call to '" + std::string(name) + "', got " + std::to_string(args.size()) + " instead", span);
            return context.semaError;
        }
        return context.intType("i64u");
    }

    // checks an array literal, [1, 2, 3], whose elements share one type, picked the way an operator picks its operands': the element type the context expects, else the first element that isn't a literal, else a float literal's, so [1, 2.5] is floats, where an empty one needs its type from where it goes
    SemaType* checkPack(PackExpr& pack, SemaType* expected) {
        for (auto& e : pack.elems) {
            if (e.name) return unsupported("named packs", tokSpan(*e.name));
        }
        auto expectedArray = asArray(expected);
        SemaType* element = expectedArray ? expectedArray->element : nullptr;
        bool isExpected = element != nullptr;
        if (pack.elems.empty()) {
            if (isExpected) return context.arrayType(element, 0);
            engine.printError("cherimoya requires an empty array's type to be known, please annotate it, like '([] :: arr[0] int)'", pack.span);
            return context.semaError;
        }
        for (auto& e : pack.elems) {
            if (element == nullptr && !isLiteral(*e.value)) element = checkExpr(*e.value);
        }
        for (auto& e : pack.elems) {
            if (element == nullptr && isFloatLiteral(*e.value)) element = checkExpr(*e.value);
        }
        if (element == nullptr) element = checkExpr(*pack.elems[0].value);
        bool isNever = false;
        for (auto& e : pack.elems) {
            auto type = e.value->type ? e.value->type : checkExpr(*e.value, element);
            isNever = isNever || type == context.semaNever;
            if (isCompatible(element, type) || isPoisoned(element)) continue;
            if (isExpected) {
                mismatch(element, type, e.value->span);
            } else {
                engine.printError("cherimoya doesn't support packs whose elements have different types yet, which are tuples, please give every element the type '" + element->toString() + "'", e.value->span);
            }
            return context.semaError;
        }
        if (isPoisoned(element)) return element;
        return isNever ? static_cast<SemaType*>(context.semaNever) : context.arrayType(element, pack.elems.size());
    }

    // checks indexing an array, xs[i], by one unsigned integer, where a literal index is an i64u, and one known when the program compiles has to be below the array's length, and gives the element's type
    SemaType* checkIndex(IndexExpr& index) {
        if (index.args.empty()) return unsupported("dereferencing", index.span);
        if (index.args.size() == 1 && index.args[0]->kind == ExprKind::Range) return unsupported("slicing", index.span);
        auto object = checkExpr(*index.object);
        std::vector<SemaType*> positions;
        for (auto& arg : index.args) positions.push_back(checkExpr(*arg, context.intType("i64u")));
        if (isPoisoned(object)) return object;
        auto array = asArray(object);
        if (!array) {
            engine.printError("cherimoya expected an array before '[...]', got '" + object->toString() + "' instead", index.object->span);
            return context.semaError;
        }
        if (index.args.size() != 1) {
            engine.printError("cherimoya expected 1 index into an array, got " + std::to_string(index.args.size()) + " instead", index.span);
            return context.semaError;
        }
        auto position = positions[0];
        auto& at = *index.args[0];
        if (isPoisoned(position)) return position;
        auto integer = asInt(position);
        if (!integer) {
            engine.printError("cherimoya expected an unsigned integer as an index, got '" + position->toString() + "' instead", at.span);
            return context.semaError;
        }
        if (integer->isSigned) {
            engine.printError("cherimoya requires an index to be unsigned, please convert it, like 'i64u(" + textOf(at.span) + ")'", at.span, {},
                {{"Convert it with 'i64u(...)'", {insertion(at.span.start, "i64u("), insertion(at.span.end, ")")}}});
            return context.semaError;
        }
        if (auto value = fold(at); value && *value >= array->size) {
            engine.printError("cherimoya expected an index below " + std::to_string(array->size) + " into '" + textOf(index.object->span) + "', got " + std::to_string(*value) + " instead", at.span);
            return context.semaError;
        }
        return array->element;
    }

    // checks a call: the callee has to be a function's name, and each argument has to match the input it goes to, where close is where the call's ) is, if it has one, and inputs gets the value each input takes
    SemaType* checkCall(Expr& callee, const std::vector<Argument>& args, Span span, std::vector<Expr*>& inputs, std::optional<size_t> close, bool isPipe = false) {
        // object.name(args) calls a method, and so does a call by a method's name whose first argument's type has that method, which wins over a function by the same name, so len(xs) is xs.len() even beside a function named len
        if (callee.kind == ExprKind::Member) {
            auto& member = static_cast<MemberExpr&>(callee);
            if (!member.object) return unpipedMethod(member);
            return checkMethod(*member.object, member.field, args, span);
        }
        if (callee.kind == ExprKind::Identifier && !args.empty() && !args[0].name && isMethodName(static_cast<IdentifierExpr&>(callee).name)) {
            std::string_view name = static_cast<IdentifierExpr&>(callee).name;
            auto first = checkExpr(*args[0].value);
            if (hasMethod(first, name) || scope->lookup(name) == nullptr) return checkMethod(*args[0].value, name, {args.begin() + 1, args.end()}, span);
        }
        SemaType* type;
        if (callee.kind == ExprKind::Identifier) {
            type = checkIdentifier(static_cast<IdentifierExpr&>(callee), true);
        } else if (callee.kind == ExprKind::TypeExpr) {
            return checkConversion(static_cast<TypeExprExpr&>(callee), args, span);
        } else {
            type = unsupported("calling anything but a function's name", callee.span);
        }
        callee.type = type;
        Symbol* function = callee.kind == ExprKind::Identifier ? static_cast<IdentifierExpr&>(callee).decl : nullptr;
        bool isFunction = function && function->kind == SymbolKind::Function && type->kind == SemaTypeKind::Function;
        std::vector<std::optional<size_t>> slots(args.size());
        if (isFunction) {
            slots = matchArguments(*function, args, span, inputs);
            std::vector<CallArg> recorded;
            for (size_t i = 0; i < args.size(); i++) recorded.push_back({args[i].span, args[i].name.has_value(), slots[i]});
            calls.push_back({span, function, recorded, close, isPipe});
        }
        // each argument takes the type of the input it goes to, when it goes to one
        auto fnType = isFunction ? static_cast<FunctionSemaType*>(type) : nullptr;
        std::vector<SemaType*> actual;
        for (size_t i = 0; i < args.size(); i++) {
            SemaType* parameter = slots[i] ? fnType->params[*slots[i]] : nullptr;
            // the first argument is already checked when the call was looked at as a method's first
            actual.push_back(args[i].value->type ? args[i].value->type : checkExpr(*args[i].value, parameter));
            if (parameter && !isCompatible(parameter, actual.back())) mismatch(parameter, actual.back(), args[i].value->span);
        }
        if (type == context.semaError) return context.semaError;
        if (type->kind != SemaTypeKind::Function) {
            engine.printError("cherimoya requires a call's callee to be a function, please call a function by name", callee.span);
            return context.semaError;
        }
        bool neverReturns = std::find(actual.begin(), actual.end(), context.semaNever) != actual.end();
        return neverReturns ? context.semaNever : static_cast<FunctionSemaType*>(type)->returns;
    }

    // names the kinds of expression sema doesn't check yet, for their diagnostic
    std::string_view unsupportedName(const Expr& e) {
        switch (e.kind) {
            case ExprKind::StringLiteral: return "strings";
            case ExprKind::OpRef: return "operators as values";
            case ExprKind::Old: return "'old'";
            case ExprKind::Construct: return "construction";
            case ExprKind::Range: return "ranges";
            case ExprKind::Compr: return "comprehensions";
            case ExprKind::Fold: return "folds";
            case ExprKind::TypeExpr: return "types as values";
            case ExprKind::Closure: return "closures";
            default: return "this expression";
        }
    }

    // gives an expression its type and returns it, where expected is the type the context wants, if it knows, which literals adapt to
    SemaType* checkExpr(Expr& e, SemaType* expected = nullptr) {
        if (expected) expectations.push_back({e.span, expected});
        e.type = typeOf(e, expected);
        return e.type;
    }

    // checks both operands of a binary operator, where a literal on one side takes the other side's type, so in 1 + x the 1 is x's type whichever side x is on
    std::pair<SemaType*, SemaType*> checkOperands(Expr& lhs, Expr& rhs, SemaType* expected) {
        // two literals of different kinds, like 1 + 2.5, are typed from the float side
        if (isLiteral(lhs) && (!isLiteral(rhs) || isFloatLiteral(rhs))) {
            auto right = checkExpr(rhs, expected);
            return {checkExpr(lhs, right), right};
        }
        auto left = checkExpr(lhs, expected);
        return {left, checkExpr(rhs, left)};
    }

    // works out an expression's type, checking its parts along the way
    SemaType* typeOf(Expr& e, SemaType* expected) {
        switch (e.kind) {
            case ExprKind::IntLiteral:
                return intLiteral(static_cast<IntLiteralExpr&>(e), expected, false, e.span);
            case ExprKind::FloatLiteral:
                return floatLiteral(static_cast<FloatLiteralExpr&>(e), expected, false, e.span);
            // true and false are 1 and 0 where a boolf is expected
            case ExprKind::BoolLiteral:
                return expected == context.semaBoolf ? context.semaBoolf : context.semaBool;
            case ExprKind::CharLiteral:
                return charLiteral(static_cast<CharLiteralExpr&>(e), expected);
            case ExprKind::Ann:
                return checkAnnotation(static_cast<AnnExpr&>(e));
            case ExprKind::Identifier:
                return checkIdentifier(static_cast<IdentifierExpr&>(e), false);
            case ExprKind::Wildcard:
                engine.printError("cherimoya requires '_' to be a place or an initializer, please use a value here", e.span);
                return context.semaError;
            // a negated literal is checked as a whole, since -128 fits in an i8 when 128 doesn't
            case ExprKind::Unary: {
                auto& unary = static_cast<UnaryExpr&>(e);
                if (const Expr* literal = literalOf(e)) {
                    auto type = literal->kind == ExprKind::FloatLiteral ? floatLiteral(static_cast<FloatLiteralExpr&>(*unary.operand), expected, true, e.span)
                        : intLiteral(static_cast<IntLiteralExpr&>(*unary.operand), expected, true, e.span);
                    return unaryType(*unary.op.op, type, unary.operand->span);
                }
                return unaryType(*unary.op.op, checkExpr(*unary.operand, expected), unary.operand->span);
            }
            // arithmetic, bitwise, and logical operators give their operands' type, so what the context wants flows into them, which is how 0.5 && x makes the 0.5 a boolf, while comparisons give a bool
            case ExprKind::Binary: {
                auto& binary = static_cast<BinaryExpr&>(e);
                auto category = binary.op.op->category;
                bool passes = category == OpCategory::Arithmetic || category == OpCategory::Bitwise || category == OpCategory::Logical;
                auto [lhs, rhs] = checkOperands(*binary.lhs, *binary.rhs, passes ? expected : nullptr);
                return binaryType(*binary.op.op, lhs, binary.lhs->span, rhs, binary.rhs->span);
            }
            // a chain is each neighboring pair compared, all joined with &&
            case ExprKind::Chain: {
                auto& chain = static_cast<ChainExpr&>(e);
                // the first operand that isn't a literal decides the type every literal in the chain takes
                SemaType* common = nullptr;
                for (auto& operand : chain.operands) {
                    if (!isLiteral(*operand)) {
                        common = checkExpr(*operand);
                        break;
                    }
                }
                // a chain of nothing but literals takes a float literal's type if it has one
                if (common == nullptr) {
                    for (auto& operand : chain.operands) {
                        if (isFloatLiteral(*operand)) {
                            common = checkExpr(*operand);
                            break;
                        }
                    }
                }
                std::vector<SemaType*> types;
                for (auto& operand : chain.operands) types.push_back(operand->type ? operand->type : checkExpr(*operand, common));
                SemaType* result = context.semaBool;
                for (size_t i = 0; i < chain.ops.size(); i++) {
                    auto pair = binaryType(*chain.ops[i].op, types[i], chain.operands[i]->span, types[i + 1], chain.operands[i + 1]->span);
                    if (isPoisoned(pair) && result != context.semaError) result = pair;
                }
                return result;
            }
            case ExprKind::ExtraBinary: {
                auto& extra = static_cast<ExtraBinaryExpr&>(e);
                checkExpr(*extra.lhs);
                checkExpr(*extra.rhs);
                return unsupported("extra operators", tokSpan(extra.op));
            }
            case ExprKind::Pack:
                return checkPack(static_cast<PackExpr&>(e), expected);
            case ExprKind::Member:
                return checkMember(static_cast<MemberExpr&>(e));
            case ExprKind::Index:
                return checkIndex(static_cast<IndexExpr&>(e));
            case ExprKind::Call: {
                auto& call = static_cast<CallExpr&>(e);
                return checkCall(*call.callee, argumentsOf(call.args), e.span, call.inputs, e.span.end - 1);
            }
            // x |> f and f <| x are both f(x), and x |> f(b <- 2) is f(x, b <- 2)
            case ExprKind::Pipe: {
                auto& pipe = static_cast<PipeExpr&>(e);
                bool forward = pipe.op.kind == TokenKind::PipeFwd;
                auto& fn = forward ? *pipe.rhs : *pipe.lhs;
                auto& arg = forward ? *pipe.lhs : *pipe.rhs;
                // a call in the function's place gives the rest of the arguments, after the piped value, and a method with no object, x |> .len, is the piped value's
                std::vector<Argument> args{{std::nullopt, &arg, arg.span}};
                Expr* callee = &fn;
                std::optional<size_t> close = std::nullopt;
                if (fn.kind == ExprKind::Call) {
                    auto& call = static_cast<CallExpr&>(fn);
                    for (auto& a : argumentsOf(call.args)) args.push_back(a);
                    callee = call.callee.get();
                    close = call.span.end - 1;
                }
                bool isMethod = callee->kind == ExprKind::Member && !static_cast<MemberExpr&>(*callee).object;
                auto type = isMethod ? checkMethod(arg, static_cast<MemberExpr&>(*callee).field, {args.begin() + 1, args.end()}, e.span)
                    : checkCall(*callee, args, e.span, pipe.inputs, close, true);
                if (fn.kind == ExprKind::Call) fn.type = type;
                // a forward pipe reads top to bottom, so each one is a step, and so is the value it starts from, when that isn't a forward pipe itself, where a step with a poisoned type has nothing to show
                if (forward) {
                    bool continues = arg.kind == ExprKind::Pipe && static_cast<PipeExpr&>(arg).op.kind == TokenKind::PipeFwd;
                    if (!continues && !isPoisoned(arg.type)) steps.push_back({arg.span, arg.type});
                    if (!isPoisoned(type)) steps.push_back({e.span, type});
                }
                return type;
            }
            case ExprKind::Block:
                return checkBlock(static_cast<BlockExpr&>(e));
            case ExprKind::Match:
                return checkMatch(static_cast<MatchExpr&>(e));
            case ExprKind::Repeat:
                return checkRepeat(static_cast<RepeatExpr&>(e));
            case ExprKind::Chunk:
                return checkChunk(static_cast<ChunkExpr&>(e));
            case ExprKind::Error:
                return context.semaError;
            default:
                return unsupported(unsupportedName(e), e.span);
        }
    }
    // checks the entry point of a program that will run: a top-level start, declared the way the runtime calls it
    void checkStart(Scope& global) {
        Symbol* entry = global.lookup("start");
        if (entry == nullptr) {
            std::string_view text = engine.getManager().text();
            std::string lead = text.empty() ? "" : text.back() == '\n' ? "\n" : "\n\n";
            engine.printError("cherimoya requires a program to have a 'start' function, please add 'let () -> start -> (code :: int) { 0 -| }'", Span{0, 0}, {},
                {{"Add a 'start' function", {insertion(text.size(), lead + "let () -> start -> (code :: int) { 0 -| }\n")}}});
            return;
        }
        if (isPoisoned(entry->type)) return;
        if (entry->kind != SymbolKind::Function || entry->type != context.functionType({context.semaInt})) {
            engine.printError("cherimoya requires 'start' to be declared as 'let () -> start -> (code :: int)', please change its inputs and outputs", entry->span);
        }
    }
public:
    Sema(DiagnosticEngine& e) : engine(e), scope(nullptr), owner(0), functions(0) {}

    // checks a whole file as one list of statements, as the role says: a program has to have a start to run, and a file of tests can't have one, and gets a warning when it has no tests
    void check(std::vector<std::unique_ptr<Stmt>>& program, Role checked = Role::Module) {
        role = checked;
        Scope global(nullptr, engine, Span{0, engine.getManager().text().size()});
        scope = &global;
        checkStmts(program);
        if (role == Role::Program) checkStart(global);
        if (role == Role::Tests) {
            if (Symbol* entry = global.lookup("start")) {
                engine.printError("cherimoya requires a .toya file to have no 'start', since cheri test runs its tests instead, please rename it", entry->span);
            }
            if (tests.empty()) engine.printWarning("cherimoya found no tests in this file, please mark one with '@test'", Span{0, 0});
            // a nested test is found when the function around it is checked, after the top-level ones, so they're put back in the order they're written
            std::sort(tests.begin(), tests.end(), [](const Test& a, const Test& b) { return a.fn->span.start < b.fn->span.start; });
        }
        scope = nullptr;
    }

    // every test the file holds, in the order sema found them
    const std::vector<Test>& allTests() const { return tests; }

    // every symbol sema declared, in order
    const std::deque<Symbol>& allSymbols() const { return symbols; }

    // every place a name is declared or used, every expected type, every call by name, every function body, every binding with an inferred type, every pipe step, and every named scope, in the order sema met them
    const std::vector<Use>& allUses() const { return uses; }
    const std::vector<Expectation>& allExpectations() const { return expectations; }
    const std::vector<Call>& allCalls() const { return calls; }
    const std::vector<Body>& allBodies() const { return bodies; }
    const std::vector<const Symbol*>& allInferred() const { return inferred; }
    const std::vector<Step>& allSteps() const { return steps; }
    const std::vector<Label>& allLabels() const { return labels; }
};
