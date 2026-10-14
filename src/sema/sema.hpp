#pragma once

#include <deque>
#include "scope.hpp"
#include "target.hpp"
#include "ast.hpp"

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

    // reports something 4.1 parses but can't check yet, and poisons the result
    SemaType* unsupported(std::string_view what, Span span) {
        engine.print_error("cherimoya doesn't support " + std::string(what) + " yet", span);
        return context.sema_error;
    }

    // makes a symbol that lives as long as sema does
    Symbol* makeSymbol(std::string_view name, SymbolKind kind, SemaType* type, bool mut, Span span) {
        symbols.push_back(Symbol{name, kind, type, mut, span, owner});
        return &symbols.back();
    }

    // checks body with a new lexical scope pushed on top of the current one
    template <typename F>
    void inScope(F body) {
        Scope inner(scope, engine);
        Scope* saved = scope;
        scope = &inner;
        body();
        scope = saved;
    }

    // checks whether a type is mutable as a whole value: mut int, or mut with the type left to inference
    bool isMut(const Type& t) {
        if (t.kind == TypeKind::Refined) return isMut(*static_cast<const RefinedType&>(t).inner);
        if (t.kind != TypeKind::Qualified) return false;
        return static_cast<const QualifiedType&>(t).position == QualPosition::Value;
    }

    // turns a parsed type into a sema type, or null when the type is left to inference (let x :: mut <- 5), where refinements resolve to their base type and their predicates are checked once the subject is in scope
    SemaType* resolveType(const Type& t) {
        switch (t.kind) {
            case TypeKind::Named: {
                auto& named = static_cast<const NamedType&>(t);
                if (named.path.size() == 1 && named.args.empty()) {
                    auto name = named.path[0].text;
                    if (name == "int" || name == "i64") return context.sema_int;
                    if (name == "float" || name == "f64") return context.sema_float;
                    if (name == "bool") return context.sema_bool;
                }
                std::string text;
                for (size_t i = 0; i < named.path.size(); i++) text += (i ? "." : "") + std::string(named.path[i].text);
                return unsupported("the type '" + text + "'", t.span);
            }
            case TypeKind::Qualified: {
                auto& q = static_cast<const QualifiedType&>(t);
                if (q.position != QualPosition::Value) return unsupported("handles", t.span);
                return q.inner ? resolveType(*q.inner) : nullptr;
            }
            case TypeKind::Refined:
                return resolveType(*static_cast<const RefinedType&>(t).inner);
            case TypeKind::Handle:
                return unsupported("handles", t.span);
            case TypeKind::Mode:
                return unsupported("'" + std::string(static_cast<const ModeType&>(t).mode.text) + "'", t.span);
            case TypeKind::Tuple:
                return unsupported("tuple types", t.span);
            case TypeKind::Record:
                return unsupported("record types", t.span);
            default:
                return context.sema_error;
        }
    }

    // checks every refinement predicate in a type against its subject: the subject's own name on parameters, or the binder
    void checkRefinements(const Type& t, Symbol* subject) {
        if (t.kind == TypeKind::Qualified) {
            auto& q = static_cast<const QualifiedType&>(t);
            if (q.inner) checkRefinements(*q.inner, subject);
            return;
        }
        if (t.kind != TypeKind::Refined) return;
        auto& r = static_cast<const RefinedType&>(t);
        checkRefinements(*r.inner, subject);
        inScope([&] {
            if (r.binder) scope->insert(makeSymbol(r.binder->text, SymbolKind::Binding, subject->type, false, {r.binder->loc, r.binder->loc + r.binder->text.length()}));
            expectBool(*r.predicate, "a refinement");
        });
    }

    // checks a predicate and requires it to be a bool
    void expectBool(Expr& e, std::string_view what) {
        auto type = checkExpr(e);
        if (type == context.sema_bool || type == context.sema_error) return;
        engine.print_error("cherimoya requires " + std::string(what) + " to be a bool, please compare it to something", e.span);
    }

    // turns a function's outputs into the type it yields: () is void, and one output is that output's type
    SemaType* outputType(const FnDecl& fn) {
        if (fn.outputs.empty()) return context.sema_void;
        if (fn.outputs.size() > 1) return unsupported("multiple outputs", fn.span);
        if (!fn.outputs[0].type) return context.sema_error;
        auto type = resolveType(*fn.outputs[0].type);
        return type ? type : context.sema_error;
    }

    // the first pass over a list of statements: declares every function in it, so they can be used before they're defined
    void hoist(std::vector<std::unique_ptr<Stmt>>& stmts) {
        for (auto& s : stmts) {
            auto found = fnOf(s.get());
            if (found == nullptr) continue;
            auto& fn = *found;
            if (fn.name.kind != TokenKind::Identifier) {
                unsupported("operator overloading", {fn.name.loc, fn.name.loc + fn.name.text.length()});
                continue;
            }
            if (!fn.generics.empty()) unsupported("generic functions", fn.span);
            std::vector<SemaType*> signature;
            for (auto& p : fn.inputs) {
                auto type = p.type ? resolveType(*p.type) : nullptr;
                if (type == nullptr) {
                    engine.print_error("cherimoya requires every parameter to have a type, please write '" + std::string(p.name.text) + " :: int'", {p.name.loc, p.name.loc + p.name.text.length()});
                    type = context.sema_error;
                }
                signature.push_back(type);
            }
            signature.push_back(outputType(fn));
            auto span = Span{fn.name.loc, fn.name.loc + fn.name.text.length()};
            auto symbol = makeSymbol(fn.name.text, SymbolKind::Function, context.get(signature), false, span);
            scope->insert(symbol);
            fn.decl = symbol;
        }
    }

    // the last pass for a function: its parameters and their refinements, its obligations, then its body in a scope of its own
    void checkFn(FnDecl& fn) {
        if (fn.name.kind != TokenKind::Identifier) return;
        size_t saved = owner;
        owner = ++functions;
        inScope([&] {
            for (auto& p : fn.inputs) {
                auto type = p.type ? resolveType(*p.type) : nullptr;
                bool mut = p.type && isMut(*p.type);
                auto param = makeSymbol(p.name.text, SymbolKind::Param, type ? type : context.sema_error, mut, {p.name.loc, p.name.loc + p.name.text.length()});
                p.decl = param;
                scope->insert(param);
                if (p.type) checkRefinements(*p.type, param);
            }
            // outputs are only visible to contracts, so they get a scope the body never sees
            inScope([&] {
                for (auto& o : fn.outputs) {
                    auto type = o.type ? resolveType(*o.type) : nullptr;
                    auto output = makeSymbol(o.name.text, SymbolKind::Param, type ? type : context.sema_error, false, {o.name.loc, o.name.loc + o.name.text.length()});
                    scope->insert(output);
                    if (o.type) checkRefinements(*o.type, output);
                }
                if (fn.obligations) expectBool(*fn.obligations, "a function's obligations");
            });
            // the body is the function's own scope, so a yield at its top level yields the function's result
            if (fn.body) {
                auto output = outputType(fn);
                auto name = Span{fn.name.loc, fn.name.loc + fn.name.text.length()};
                targets.push_back(Target{TargetKind::Function, fn.name.text, output, nullptr, {}, false, name});
                bool completes = checkStmts(fn.body->stmts);
                targets.pop_back();
                fn.body->type = output;
                if (completes && output != context.sema_void && !poisoned(output)) {
                    engine.print_error("cherimoya requires every path through '" + std::string(fn.name.text) + "' to yield '" + output->to_string() + "', please end it with a yield",
                        Span{fn.body->span.end - 1, fn.body->span.end}, {{"'" + std::string(fn.name.text) + "' is declared to yield '" + output->to_string() + "' here", name}});
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
                engine.print_error("cherimoya requires an uninitialized binding to have a type, please write 'let x :: mut int <- _;'", let.span);
                type = context.sema_error;
            } else if (!isMut(*let.type)) {
                engine.print_error("cherimoya requires an uninitialized binding to be mutable, please write 'let x :: mut int <- _;'", let.span);
            }
        } else {
            auto init = checkExpr(*let.init);
            if (declared == nullptr) {
                type = init;
            } else if (!compatible(declared, init)) {
                mismatch(declared, init, let.init->span);
            }
        }
        if (type == context.sema_void) {
            engine.print_error("cherimoya requires a binding to hold a value, please call this as its own statement instead", let.init->span);
            type = context.sema_error;
        }
        if (let.pattern->kind == PatternKind::Wildcard) return;
        if (let.pattern->kind != PatternKind::Binder) {
            unsupported("destructuring", let.pattern->span);
            return;
        }
        auto& binder = static_cast<BinderPattern&>(*let.pattern);
        auto symbol = makeSymbol(binder.name.text, SymbolKind::Binding, type, let.type && isMut(*let.type), binder.span);
        binder.decl = symbol;
        if (let.type) checkRefinements(*let.type, symbol);
        scope->insert(symbol);
    }

    // checks whether a value of type actual can go where expected is wanted, where errors and never fit anywhere
    bool compatible(SemaType* expected, SemaType* actual) {
        return expected == actual || expected == context.sema_error || actual == context.sema_error || actual == context.sema_never;
    }

    // reports a value of the wrong type
    void mismatch(SemaType* expected, SemaType* actual, Span span) {
        engine.print_error("cherimoya expected a value of type '" + expected->to_string() + "', got '" + actual->to_string() + "' instead", span);
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
        for (auto& s : stmts) {
            if (fnOf(s.get()) != nullptr) continue;
            if (!completes && !warned) {
                engine.print_warning("cherimoya never runs this statement, since every path before it yields", s->span);
                warned = true;
            }
            if (checkStmt(*s)) completes = false;
        }
        for (auto& s : stmts) {
            if (auto fn = fnOf(s.get())) checkFn(*fn);
        }
        return completes;
    }

    // checks a block in a new lexical scope, and gives it the type of what reaches its end
    SemaType* checkBlock(BlockExpr& block, TargetKind kind = TargetKind::Block) {
        std::optional<std::string_view> label = std::nullopt;
        if (block.name) label = block.name->text;
        targets.push_back(Target{kind, label, nullptr, nullptr, {}, false, block.span});
        bool completes = true;
        inScope([&] { completes = checkStmts(block.stmts); });
        auto target = targets.back();
        targets.pop_back();
        block.type = finish(target, completes, "this block");
        return block.type;
    }

    // works out a scope's type once it's checked: what was yielded to it, joined with void if control can fall off its end, or never if nothing reaches past it at all
    SemaType* finish(const Target& target, bool completes, std::string_view what) {
        if (target.yielded == nullptr) return completes ? context.sema_void : context.sema_never;
        if (!completes || target.yielded == context.sema_void || poisoned(target.yielded)) return target.yielded;
        engine.print_error("cherimoya requires every path through " + std::string(what) + " to yield '" + target.yielded->to_string() + "', please end it with a yield",
            Span{target.span.end - 1, target.span.end}, {{"this yield gives it '" + target.yielded->to_string() + "'", target.first}});
        return context.sema_error;
    }

    // finds the target a yield goes to, searching outward from the innermost and never past the nearest function
    Target* resolveTarget(const YieldStmt& y) {
        for (size_t i = targets.size(); i-- > 0;) {
            auto& t = targets[i];
            if (!y.target) return &t;
            auto kind = y.target->kind;
            if (kind == TokenKind::KwMatch && t.kind == TargetKind::Match) return &t;
            if (kind == TokenKind::KwRepeat && t.kind == TargetKind::Loop) return &t;
            if (kind == TokenKind::Identifier && t.label == y.target->text) return &t;
            if (t.kind == TargetKind::Function) break;
        }
        return nullptr;
    }

    // checks a yield against its target, and records its type for the target's own type
    void checkYield(YieldStmt& y) {
        auto value = y.value ? checkExpr(*y.value) : context.sema_void;
        auto span = y.value ? y.value->span : y.span;
        auto target = resolveTarget(y);
        if (target == nullptr) {
            if (!y.target) {
                engine.print_error("cherimoya requires a yield to be inside a function or scope, please move it into one", y.span);
            } else {
                std::string name(y.target->text);
                engine.print_error("cherimoya expected a function or scope named '" + name + "' around this yield, got none instead", {y.target->loc, y.target->loc + y.target->text.length()});
            }
            return;
        }
        if (poisoned(value)) return;
        // a function's yields have to match its output
        if (target->kind == TargetKind::Function) {
            if (!compatible(target->expected, value)) {
                engine.print_error("cherimoya expected a yield of type '" + target->expected->to_string() + "' to '" + std::string(*target->label) + "', got '" + value->to_string() + "' instead",
                    span, {{"'" + std::string(*target->label) + "' is declared to yield '" + target->expected->to_string() + "' here", target->span}});
            }
            return;
        }
        if (target->kind == TargetKind::Loop && !target->infinite && value != context.sema_void) {
            unsupported("yielding a value out of a loop with a header", span);
            return;
        }
        contribute(*target, value, span);
    }

    // gives a target one more way to finish: the first one sets its type, and every later one has to agree
    void contribute(Target& target, SemaType* value, Span span) {
        if (value == context.sema_never) return;
        if (target.yielded == nullptr || poisoned(target.yielded)) {
            target.yielded = value;
            target.first = span;
        } else if (target.yielded != value && !poisoned(value)) {
            engine.print_error("cherimoya requires every yield to one scope to have the same type, please make this one '" + target.yielded->to_string() + "'",
                span, {{"this yield gives it '" + target.yielded->to_string() + "'", target.first}});
        }
    }

    // checks one arm's pattern against the subject's type, declaring what it binds, and returns whether it matches every value
    bool checkPattern(Pattern& p, SemaType* subject) {
        switch (p.kind) {
            case PatternKind::Wildcard:
                return true;
            case PatternKind::Binder: {
                auto& b = static_cast<BinderPattern&>(p);
                b.decl = makeSymbol(b.name.text, SymbolKind::Binding, subject, false, b.span);
                scope->insert(b.decl);
                return true;
            }
            case PatternKind::Literal: {
                auto type = checkExpr(*static_cast<LiteralPattern&>(p).literal);
                if (!poisoned(subject) && !poisoned(type) && type != subject) {
                    engine.print_error("cherimoya expected a pattern of type '" + subject->to_string() + "', got '" + type->to_string() + "' instead", p.span);
                }
                return false;
            }
            case PatternKind::Guard:
                expectBool(*static_cast<GuardPattern&>(p).cond, "a guard");
                return false;
            case PatternKind::Variant:
                unsupported("variant patterns", p.span);
                return true;
            case PatternKind::Pack:
                unsupported("pack patterns", p.span);
                return true;
            case PatternKind::Range:
                unsupported("range patterns", p.span);
                return true;
            case PatternKind::SetBuilder:
                unsupported("set-builder patterns", p.span);
                return true;
            default:
                return true;
        }
    }

    // checks a match: each arm is a target of its own whose value goes to the match, and the arms have to cover every value that the match needs, which for now means a catch-all, or both true and false on a bool
    SemaType* checkMatch(MatchExpr& m) {
        auto subject = m.subject ? checkExpr(*m.subject) : nullptr;
        std::optional<std::string_view> label = std::nullopt;
        if (m.name) label = m.name->text;
        targets.push_back(Target{TargetKind::Match, label, nullptr, nullptr, {}, false, m.span});
        bool covered = false;
        bool seenTrue = false;
        bool seenFalse = false;
        const MatchArm* catchAll = nullptr;
        bool warned = false;
        for (auto& arm : m.arms) {
            if (covered && !warned) {
                std::vector<Note> notes;
                if (catchAll) notes.push_back({"this arm matches every value", catchAll->pattern->span});
                engine.print_warning("cherimoya never reaches this arm, since the arms before it match every value", arm.pattern->span, notes);
                warned = true;
            }
            SemaType* type = nullptr;
            inScope([&] {
                bool all = checkPattern(*arm.pattern, subject ? subject : context.sema_error);
                if (all && !covered) catchAll = &arm;
                if (arm.pattern->kind == PatternKind::Literal) {
                    auto& literal = *static_cast<LiteralPattern&>(*arm.pattern).literal;
                    if (literal.kind == ExprKind::BoolLiteral) (static_cast<BoolLiteralExpr&>(literal).literal ? seenTrue : seenFalse) = true;
                }
                covered = covered || all || (subject == context.sema_bool && seenTrue && seenFalse);
                type = checkBlock(*arm.body, TargetKind::Arm);
            });
            contribute(targets.back(), type, arm.body->span);
        }
        auto target = targets.back();
        targets.pop_back();
        if (covered || poisoned(subject ? subject : context.sema_bool)) return finish(target, false, "this match");
        bool value = target.yielded != nullptr && target.yielded != context.sema_void && !poisoned(target.yielded);
        if (numeric(subject) || value) {
            std::string what = subject ? "a match on '" + subject->to_string() + "'" : "a match that yields a value";
            engine.print_error("cherimoya requires " + what + " to cover every value, please add a '_ -> { ... }' arm", Span{m.span.start, m.span.start + 5});
            return context.sema_error;
        }
        return finish(target, true, "this match");
    }

    // checks a loop: one with a condition is void, and one without only ends by a yield, so it takes that yield's type
    SemaType* checkRepeat(RepeatExpr& r) {
        if (r.binder) return unsupported("for-each loops", r.binder->span);
        if (r.cond) expectBool(*r.cond, "a loop's condition");
        if (r.invariant) expectBool(*r.invariant, "a loop's invariant");
        std::optional<std::string_view> label = std::nullopt;
        if (r.name) label = r.name->text;
        targets.push_back(Target{TargetKind::Loop, label, nullptr, nullptr, {}, r.cond == nullptr, r.span});
        inScope([&] { checkStmts(r.body->stmts); });
        auto target = targets.back();
        targets.pop_back();
        r.body->type = context.sema_void;
        if (r.cond) return context.sema_void;
        return target.yielded ? target.yielded : context.sema_never;
    }

    // checks a chunk, which is a block that can be named until allocators exist
    SemaType* checkChunk(ChunkExpr& c) {
        std::optional<std::string_view> label = std::nullopt;
        if (c.name) label = c.name->text;
        targets.push_back(Target{TargetKind::Chunk, label, nullptr, nullptr, {}, false, c.span});
        bool completes = true;
        inScope([&] { completes = checkStmts(c.body->stmts); });
        auto target = targets.back();
        targets.pop_back();
        c.body->type = finish(target, completes, "this chunk");
        return c.body->type;
    }

    // checks one statement, and returns whether control never continues past it
    bool checkStmt(Stmt& s) {
        switch (s.kind) {
            case StmtKind::Let: {
                auto& let = static_cast<LetStmt&>(s);
                checkLet(let);
                return let.init->type == context.sema_never;
            }
            case StmtKind::Assign: {
                auto& a = static_cast<AssignStmt&>(s);
                auto value = checkExpr(*a.value);
                if (a.place->kind == ExprKind::Wildcard) {
                    a.place->type = value;
                    return value == context.sema_never;
                }
                auto place = checkPlace(*a.place);
                // place op<- value is place <- place op value, which has to give back the place's type
                if (a.op && a.op->op->base->category == OpCategory::Extra) {
                    unsupported("extra operators", Span{a.op->loc, a.op->loc + a.op->text.length()});
                } else if (a.op) {
                    auto result = binaryType(*a.op->op->base, place, a.place->span, value, a.value->span);
                    if (!compatible(place, result)) mismatch(place, result, a.value->span);
                } else if (!compatible(place, value)) {
                    mismatch(place, value, a.value->span);
                }
                return value == context.sema_never;
            }
            // a value can't be dropped on the floor: it has to be used or explicitly discarded
            case StmtKind::Expr: {
                auto& e = *static_cast<ExprStmt&>(s).expr;
                auto type = checkExpr(e);
                if (type == context.sema_void || poisoned(type)) return type == context.sema_never;
                auto text = engine.get_manager().text().substr(e.span.start, e.span.length());
                engine.print_error("cherimoya requires a value to be used or discarded, please write '_ <- " + std::string(text) + ";'", e.span);
                return false;
            }
            case StmtKind::Yield:
                checkYield(static_cast<YieldStmt&>(s));
                return true;
            case StmtKind::Wit: {
                auto& w = static_cast<WitStmt&>(s);
                if (w.bound) {
                    unsupported("interfaces", w.span);
                    break;
                }
                expectBool(*w.predicate, "a 'wit' obligation");
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
                auto& a = static_cast<AttrStmt&>(s);
                return a.target && checkStmt(*a.target);
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
            engine.print_error("cherimoya requires names to be declared before they're used, please declare '" + name + "' with 'let " + name + " <- ...;'", id.span);
            return context.sema_error;
        }
        if (symbol->kind != SymbolKind::Function && symbol->owner != 0 && symbol->owner != owner) {
            std::string name(id.name);
            engine.print_error("cherimoya uses closures to capture variables instead of nested functions as follows: let f <- [" + name + "] (x :: int) -> { x + " + name + " -| };",
                id.span, {{"'" + name + "' is declared here, in the function around this one", symbol->s}});
            return context.sema_error;
        }
        id.decl = symbol;
        if (symbol->kind == SymbolKind::Function && !callee) return unsupported("functions as values", id.span);
        return symbol->type;
    }

    // checks the left side of an assignment: it has to be a binding declared mut, and gives the type it holds
    SemaType* checkPlace(Expr& place) {
        if (place.kind != ExprKind::Identifier) return checkExpr(place);
        auto& id = static_cast<IdentifierExpr&>(place);
        auto type = checkIdentifier(id, true);
        place.type = type;
        auto symbol = id.decl;
        if (symbol == nullptr || symbol->mut) return type;
        std::string name(id.name);
        if (symbol->kind == SymbolKind::Function) {
            engine.print_error("cherimoya requires the left side of an assignment to be a binding, please assign to a 'let' instead of the function '" + name + "'", place.span);
            return context.sema_error;
        }
        auto fix = symbol->kind == SymbolKind::Param ? "declare it as '" + name + " :: mut " + type->to_string() + "'" : "declare it with 'let " + name + " :: mut <- ...;'";
        engine.print_error("cherimoya requires '" + name + "' to be mutable to assign to it, please " + fix, place.span, {{"'" + name + "' is declared here", symbol->s}});
        return type;
    }

    // checks whether a type carries an earlier error or comes from a yield, so nothing built from it reports again
    bool poisoned(SemaType* type) {
        return type == context.sema_error || type == context.sema_never;
    }

    // checks whether a type is one of the numeric types
    bool numeric(SemaType* type) {
        return type == context.sema_int || type == context.sema_float;
    }

    // reports an operand of the wrong type for an operator
    void badOperand(std::string_view wanted, std::string_view side, const OpInfo& op, SemaType* actual, Span span) {
        engine.print_error("cherimoya expected " + std::string(wanted) + " " + std::string(side) + " '" + op.spelling + "', got '" + actual->to_string() + "' instead", span);
    }

    // works out what a prefix operator gives: - on numbers, ! on bools, and ~ on ints
    SemaType* unaryType(const OpInfo& op, SemaType* operand, Span span) {
        if (poisoned(operand)) return context.sema_error;
        if (op.spelling == "-" && numeric(operand)) return operand;
        if (op.spelling == "!" && operand == context.sema_bool) return operand;
        if (op.spelling == "~" && operand == context.sema_int) return operand;
        auto wanted = op.spelling == "-" ? "an int or a float" : op.spelling == "!" ? "a bool" : "an int";
        badOperand(wanted, "after", op, operand, span);
        return context.sema_error;
    }

    // works out what a built-in binary operator gives, where both sides must have the same type
    SemaType* binaryType(const OpInfo& op, SemaType* lhs, Span lspan, SemaType* rhs, Span rspan) {
        if (poisoned(lhs) || poisoned(rhs)) return context.sema_error;
        // what each side has to be, and what the operator gives back
        bool ok;
        std::string_view wanted;
        SemaType* result;
        switch (op.category) {
            case OpCategory::Arithmetic:
                ok = numeric(lhs) && numeric(rhs);
                wanted = "an int or a float";
                result = lhs;
                break;
            case OpCategory::Comparison:
                ok = numeric(lhs) && numeric(rhs);
                wanted = "an int or a float";
                result = context.sema_bool;
                break;
            case OpCategory::Bitwise:
                ok = lhs == context.sema_int && rhs == context.sema_int;
                wanted = "an int";
                result = lhs;
                break;
            case OpCategory::Logical:
                ok = lhs == context.sema_bool && rhs == context.sema_bool;
                wanted = "a bool";
                result = lhs;
                break;
            case OpCategory::Equality:
                ok = lhs != context.sema_void && rhs != context.sema_void && lhs->kind == SemaTypeKind::Primitive && rhs->kind == SemaTypeKind::Primitive;
                wanted = "a value";
                result = context.sema_bool;
                break;
            default:
                return context.sema_error;
        }
        if (!ok) {
            bool left = op.category == OpCategory::Equality ? (lhs == context.sema_void || lhs->kind != SemaTypeKind::Primitive)
                : op.category == OpCategory::Bitwise ? lhs != context.sema_int
                : op.category == OpCategory::Logical ? lhs != context.sema_bool
                : !numeric(lhs);
            badOperand(wanted, left ? "on the left of" : "on the right of", op, left ? lhs : rhs, left ? lspan : rspan);
            return context.sema_error;
        }
        if (lhs != rhs) {
            engine.print_error("cherimoya requires both sides of '" + op.spelling + "' to have the same type, please convert one of them", Span{lspan.start, rspan.end},
                {{"this side is '" + lhs->to_string() + "'", lspan}, {"this side is '" + rhs->to_string() + "'", rspan}});
            return context.sema_error;
        }
        return result;
    }

    // checks a call: the callee has to be a function's name, and the arguments have to match its parameters
    SemaType* checkCall(Expr& callee, const std::vector<Expr*>& args, Span span) {
        SemaType* type;
        if (callee.kind == ExprKind::Identifier) {
            type = checkIdentifier(static_cast<IdentifierExpr&>(callee), true);
        } else if (callee.kind == ExprKind::TypeExpr) {
            type = unsupported("conversions", callee.span);
        } else {
            type = unsupported("calling anything but a function's name", callee.span);
        }
        callee.type = type;
        std::vector<SemaType*> actual;
        for (auto arg : args) actual.push_back(checkExpr(*arg));
        if (type == context.sema_error) return context.sema_error;
        if (type->kind != SemaTypeKind::Function) {
            engine.print_error("cherimoya requires a call's callee to be a function, please call a function by name", callee.span);
            return context.sema_error;
        }
        auto fn = static_cast<FunctionSemaType*>(type);
        auto decl = static_cast<IdentifierExpr&>(callee).decl;
        if (args.size() != fn->params.size()) {
            engine.print_error("cherimoya expected " + std::to_string(fn->params.size()) + " argument" + (fn->params.size() == 1 ? "" : "s") + " in the call to '"
                + std::string(decl->name) + "', got " + std::to_string(args.size()) + " instead", span, {{"'" + std::string(decl->name) + "' is declared here", decl->s}});
            return fn->returns;
        }
        for (size_t i = 0; i < args.size(); i++) {
            if (!compatible(fn->params[i], actual[i])) mismatch(fn->params[i], actual[i], args[i]->span);
        }
        return fn->returns;
    }

    // names the kinds of expression 4.1 doesn't check yet, for their diagnostic
    std::string_view unsupportedName(const Expr& e) {
        switch (e.kind) {
            case ExprKind::StringLiteral: return "strings";
            case ExprKind::CharLiteral: return "chars";
            case ExprKind::OpRef: return "operators as values";
            case ExprKind::Old: return "'old'";
            case ExprKind::Index: return "indexing";
            case ExprKind::Member: return "fields";
            case ExprKind::Construct: return "construction";
            case ExprKind::Pack: return "packs";
            case ExprKind::Range: return "ranges";
            case ExprKind::Compr: return "comprehensions";
            case ExprKind::Fold: return "folds";
            case ExprKind::Ann: return "annotations";
            case ExprKind::TypeExpr: return "types as values";
            case ExprKind::Closure: return "closures";
            case ExprKind::Match: return "'match'";
            case ExprKind::Repeat: return "'repeat'";
            case ExprKind::Chunk: return "'chunk'";
            default: return "this expression";
        }
    }

    // gives an expression its type and returns it
    SemaType* checkExpr(Expr& e) {
        e.type = typeOf(e);
        return e.type;
    }

    // works out an expression's type, checking its parts along the way
    SemaType* typeOf(Expr& e) {
        switch (e.kind) {
            case ExprKind::IntLiteral:
                return context.sema_int;
            case ExprKind::FloatLiteral:
                return context.sema_float;
            case ExprKind::BoolLiteral:
                return context.sema_bool;
            case ExprKind::Identifier:
                return checkIdentifier(static_cast<IdentifierExpr&>(e), false);
            case ExprKind::Wildcard:
                engine.print_error("cherimoya requires '_' to be a place or an initializer, please use a value here", e.span);
                return context.sema_error;
            case ExprKind::Unary: {
                auto& u = static_cast<UnaryExpr&>(e);
                return unaryType(*u.op.op, checkExpr(*u.operand), u.operand->span);
            }
            case ExprKind::Binary: {
                auto& b = static_cast<BinaryExpr&>(e);
                auto lhs = checkExpr(*b.lhs);
                auto rhs = checkExpr(*b.rhs);
                return binaryType(*b.op.op, lhs, b.lhs->span, rhs, b.rhs->span);
            }
            // a chain is each neighboring pair compared, all joined with &&
            case ExprKind::Chain: {
                auto& c = static_cast<ChainExpr&>(e);
                std::vector<SemaType*> types;
                for (auto& o : c.operands) types.push_back(checkExpr(*o));
                SemaType* result = context.sema_bool;
                for (size_t i = 0; i < c.ops.size(); i++) {
                    auto pair = binaryType(*c.ops[i].op, types[i], c.operands[i]->span, types[i + 1], c.operands[i + 1]->span);
                    if (pair == context.sema_error) result = pair;
                }
                return result;
            }
            case ExprKind::ExtraBinOp: {
                auto& b = static_cast<ExtraBinOpExpr&>(e);
                checkExpr(*b.lhs);
                checkExpr(*b.rhs);
                return unsupported("extra operators", Span{b.op.loc, b.op.loc + b.op.text.length()});
            }
            case ExprKind::Call: {
                auto& c = static_cast<CallExpr&>(e);
                std::vector<Expr*> args;
                for (auto& a : c.args) args.push_back(a.get());
                return checkCall(*c.callee, args, e.span);
            }
            // x |> f and f <| x are both f(x)
            case ExprKind::Pipe: {
                auto& p = static_cast<PipeExpr&>(e);
                bool forward = p.op.kind == TokenKind::PipeFwd;
                auto& fn = forward ? *p.rhs : *p.lhs;
                auto& arg = forward ? *p.lhs : *p.rhs;
                return checkCall(fn, {&arg}, e.span);
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
                return context.sema_error;
            default:
                return unsupported(unsupportedName(e), e.span);
        }
    }
    // checks the entry point of a program that will run: a top-level main, declared the way the runtime calls it
    void checkMain(Scope& global) {
        Symbol* main = global.lookup("main");
        if (main == nullptr) {
            engine.print_error("cherimoya requires a program to have a 'main' function, please add 'let () -> main -> (code :: int) { 0 -| }'", Span{0, 0});
            return;
        }
        if (poisoned(main->type)) return;
        if (main->kind != SymbolKind::Function || main->type != context.get({context.sema_int})) {
            engine.print_error("cherimoya requires 'main' to be declared as 'let () -> main -> (code :: int)', please change its inputs and outputs", main->s);
        }
    }
public:
    Sema(DiagnosticEngine& e) : engine(e), scope(nullptr), owner(0), functions(0) {}

    // checks a whole program as one list of statements, and when entry is set, that it has a main to run
    void check(std::vector<std::unique_ptr<Stmt>>& program, bool entry = false) {
        Scope global(nullptr, engine);
        scope = &global;
        checkStmts(program);
        if (entry) checkMain(global);
        scope = nullptr;
    }

    // every symbol sema declared, in order
    const std::deque<Symbol>& all_symbols() const { return symbols; }
};
