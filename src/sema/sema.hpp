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

// a call of a function by name, with each argument's span, which the language server's signature help reads
struct Call {
    Span span;
    Symbol* function;
    std::vector<Span> args;
};

// a function's span and the owner number its own locals get, so the language server can tell which locals a position can reach
struct Body {
    Span span;
    size_t owner;
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
    // what the language server reads once checking is done: every use of a name, every expected type, every call, and every function body
    std::vector<Use> uses;
    std::vector<Expectation> expectations;
    std::vector<Call> calls;
    std::vector<Body> bodies;

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
    Symbol* makeSymbol(std::string_view name, SymbolKind kind, SemaType* type, bool isMutable, Span span) {
        symbols.push_back(Symbol{name, kind, type, isMutable, span, owner, {}, nullptr});
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
                    if (IntSemaType* integer = context.intType(name)) return integer;
                    if (FloatSemaType* floating = context.floatType(name)) return floating;
                    if (name == "bool") return context.semaBool;
                    if (name == "char") return context.semaChar;
                    if (name == "rune") return context.semaRune;
                }
                std::string text;
                for (size_t i = 0; i < named.path.size(); i++) text += (i ? "." : "") + std::string(named.path[i].text);
                return unsupported("the type '" + text + "'", t.span);
            }
            case TypeKind::Qualified: {
                auto& qualified = static_cast<const QualifiedType&>(t);
                if (qualified.position != QualPosition::Value) return unsupported("handles", t.span);
                return qualified.inner ? resolveType(*qualified.inner) : nullptr;
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
            if (refined.binder) scope->insert(makeSymbol(refined.binder->text, SymbolKind::Binding, subject->type, false, tokSpan(*refined.binder)));
            expectBool(*refined.predicate, "a refinement");
        });
    }

    // checks a predicate and requires it to be a bool
    void expectBool(Expr& e, std::string_view what) {
        auto type = checkExpr(e);
        if (type == context.semaBool || type == context.semaError) return;
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

    // the first pass over a list of statements: declares every function in it, so they can be used before they're defined
    void hoist(std::vector<std::unique_ptr<Stmt>>& stmts) {
        for (auto& s : stmts) {
            auto found = fnOf(s.get());
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
            auto symbol = makeSymbol(fn.name.text, SymbolKind::Function, context.functionType(signature), false, tokSpan(fn.name));
            symbol->function = &fn;
            scope->insert(symbol);
            fn.decl = symbol;
        }
    }

    // the last pass for a function: its parameters and their refinements, its obligations, then its body in a scope of its own
    void checkFn(FnDecl& fn) {
        if (fn.name.kind != TokenKind::Identifier) return;
        // the signature was resolved when the function was hoisted, so its parameters and outputs are only ever reported once
        auto signature = static_cast<FunctionSemaType*>(fn.decl->type);
        size_t saved = owner;
        owner = ++functions;
        bodies.push_back({fn.span, owner});
        inScope(fn.span, [&] {
            for (size_t i = 0; i < fn.inputs.size(); i++) {
                auto& p = fn.inputs[i];
                bool isMutable = p.type && isMut(*p.type);
                auto param = makeSymbol(p.name.text, SymbolKind::Param, signature->params[i], isMutable, tokSpan(p.name));
                p.decl = param;
                scope->insert(param);
                if (p.type) checkRefinements(*p.type, param);
            }
            // outputs are only visible to contracts, so they get a scope the body never sees, and only a single output has a type of its own
            inScope(fn.span, [&] {
                for (auto& o : fn.outputs) {
                    auto type = fn.outputs.size() == 1 ? signature->returns : context.semaError;
                    auto output = makeSymbol(o.name.text, SymbolKind::Param, type, false, tokSpan(o.name));
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
                bool completes = checkStmts(fn.body->stmts);
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
            } else if (!isMut(*let.type)) {
                engine.printError("cherimoya requires an uninitialized binding to be mutable, please write 'let x :: mut int <- _;'", let.span);
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
        auto symbol = makeSymbol(binder.name.text, SymbolKind::Binding, type, let.type && isMut(*let.type), binder.span);
        binder.decl = symbol;
        scope->insert(symbol);
        if (let.type) checkRefinements(*let.type, symbol);
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
        for (auto& s : stmts) {
            if (fnOf(s.get()) != nullptr) continue;
            if (!completes && !warned) {
                engine.printWarning("cherimoya never runs this statement, since every path before it yields", s->span);
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
                binder.decl = makeSymbol(binder.name.text, SymbolKind::Binding, subject, false, binder.span);
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
        bool covered = false;
        bool seenTrue = false;
        bool seenFalse = false;
        const MatchArm* catchAll = nullptr;
        bool warned = false;
        for (auto& arm : m.arms) {
            if (covered && !warned) {
                std::vector<Note> notes;
                if (catchAll) notes.push_back({"this arm matches every value", catchAll->pattern->span});
                engine.printWarning("cherimoya never reaches this arm, since the arms before it match every value", arm.pattern->span, notes);
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
        if (isNumeric(subject) || isCharacter(subject) || yieldsValue) {
            std::string what = subject ? "a match on '" + subject->toString() + "'" : "a match that yields a value";
            engine.printError("cherimoya requires " + what + " to cover every value, please add a '_ -> { ... }' arm", Span{m.span.start, m.span.start + 5});
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
                auto text = engine.getManager().text().substr(expr.span.start, expr.span.length());
                engine.printError("cherimoya requires a value to be used or discarded, please write '_ <- " + std::string(text) + ";'", expr.span);
                return false;
            }
            case StmtKind::Yield:
                checkYield(static_cast<YieldStmt&>(s));
                return true;
            case StmtKind::Wit: {
                auto& wit = static_cast<WitStmt&>(s);
                if (wit.bound) {
                    unsupported("interfaces", wit.span);
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

    // checks the left side of an assignment: it has to be a binding declared mut, and gives the type it holds
    SemaType* checkPlace(Expr& place) {
        if (place.kind != ExprKind::Identifier) return checkExpr(place);
        auto& identifier = static_cast<IdentifierExpr&>(place);
        auto type = checkIdentifier(identifier, true);
        place.type = type;
        auto symbol = identifier.decl;
        if (symbol == nullptr || symbol->isMutable) return type;
        std::string name(identifier.name);
        if (symbol->kind == SymbolKind::Function) {
            engine.printError("cherimoya requires the left side of an assignment to be a binding, please assign to a 'let' instead of the function '" + name + "'", place.span);
            return context.semaError;
        }
        auto fix = symbol->kind == SymbolKind::Param && !isPoisoned(type) ? "declare it as '" + name + " :: mut " + type->toString() + "'" : "declare it with 'let " + name + " :: mut <- ...;'";
        engine.printError("cherimoya requires '" + name + "' to be mutable to assign to it, please " + fix, place.span, {{"'" + name + "' is declared here", symbol->span}});
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

    // checks whether an expression is a number literal, possibly negated, or a char literal, which takes its type from around it instead of deciding one
    bool isLiteral(const Expr& e) {
        return literalOf(e) != nullptr || e.kind == ExprKind::CharLiteral;
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
        if (op.spelling == "!" && operand == context.semaBool) return operand;
        if (op.spelling == "~" && integer) return operand;
        auto wanted = op.spelling == "-" ? "an int or a float" : op.spelling == "!" ? "a bool" : "an int";
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
                    return isNumeric(side);
                case OpCategory::Comparison:
                    return isNumeric(side) || isCharacter(side);
                case OpCategory::Bitwise:
                    return asInt(side) != nullptr;
                case OpCategory::Logical:
                    return side == context.semaBool;
                default:
                    return side != context.semaVoid && side->kind != SemaTypeKind::Function;
            }
        };
        std::string_view wanted;
        switch (op.category) {
            case OpCategory::Arithmetic:
                wanted = "an int or a float";
                break;
            case OpCategory::Comparison:
                wanted = "an int, a float, or a character";
                break;
            case OpCategory::Bitwise:
                wanted = "an int";
                break;
            case OpCategory::Logical:
                wanted = "a bool";
                break;
            case OpCategory::Equality:
                wanted = "a value";
                break;
            default:
                return context.semaError;
        }
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

    // checks a conversion, a type called on one value: i32(x) converts any number or character to i32, f32(x) any number to f32, and rune(x) any integer or character to rune, where codegen checks the ones that can lose the value
    SemaType* checkConversion(TypeExprExpr& callee, const std::vector<Expr*>& args, Span span) {
        auto target = resolveType(*callee.typeExpr);
        callee.type = target;
        for (auto arg : args) checkExpr(*arg);
        if (target == nullptr || isPoisoned(target)) return context.semaError;
        if (args.size() != 1) {
            engine.printError("cherimoya expected 1 value to convert in the call to '" + target->toString() + "', got " + std::to_string(args.size()) + " instead", span);
            return context.semaError;
        }
        auto source = args[0]->type;
        if (isPoisoned(source)) return context.semaError;
        // numbers convert to each other, and characters convert to each other and to and from integers, but never to or from floats
        bool isValid = (isNumeric(target) && isNumeric(source)) || (isCharacter(target) && (isCharacter(source) || asInt(source))) || (asInt(target) && isCharacter(source));
        if (!isValid) {
            engine.printError("cherimoya requires a conversion to be between numbers, or between characters and integers, please convert '" + source->toString() + "' to '" + target->toString() + "' some other way", span);
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

    // checks a call: the callee has to be a function's name, and the arguments have to match its parameters
    SemaType* checkCall(Expr& callee, const std::vector<Expr*>& args, Span span) {
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
        if (function && function->kind == SymbolKind::Function) {
            std::vector<Span> argSpans;
            for (auto arg : args) argSpans.push_back(arg->span);
            calls.push_back({span, function, argSpans});
        }
        // each argument takes its parameter's type when the callee is a function with the right number of them
        auto fnType = type->kind == SemaTypeKind::Function ? static_cast<FunctionSemaType*>(type) : nullptr;
        std::vector<SemaType*> actual;
        for (size_t i = 0; i < args.size(); i++) {
            SemaType* parameter = (fnType && fnType->params.size() > i) ? fnType->params[i] : nullptr;
            actual.push_back(checkExpr(*args[i], parameter));
        }
        if (type == context.semaError) return context.semaError;
        bool neverReturns = std::find(actual.begin(), actual.end(), context.semaNever) != actual.end();
        if (type->kind != SemaTypeKind::Function) {
            engine.printError("cherimoya requires a call's callee to be a function, please call a function by name", callee.span);
            return context.semaError;
        }
        auto fn = static_cast<FunctionSemaType*>(type);
        auto decl = static_cast<IdentifierExpr&>(callee).decl;
        if (args.size() != fn->params.size()) {
            engine.printError("cherimoya expected " + std::to_string(fn->params.size()) + " argument" + (fn->params.size() == 1 ? "" : "s") + " in the call to '"
                + std::string(decl->name) + "', got " + std::to_string(args.size()) + " instead", span, {{"'" + std::string(decl->name) + "' is declared here", decl->span}});
            return fn->returns;
        }
        for (size_t i = 0; i < args.size(); i++) {
            if (!isCompatible(fn->params[i], actual[i])) mismatch(fn->params[i], actual[i], args[i]->span);
        }
        return neverReturns ? context.semaNever : fn->returns;
    }

    // names the kinds of expression sema doesn't check yet, for their diagnostic
    std::string_view unsupportedName(const Expr& e) {
        switch (e.kind) {
            case ExprKind::StringLiteral: return "strings";
            case ExprKind::OpRef: return "operators as values";
            case ExprKind::Old: return "'old'";
            case ExprKind::Index: return "indexing";
            case ExprKind::Member: return "fields";
            case ExprKind::Construct: return "construction";
            case ExprKind::Pack: return "packs";
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
            case ExprKind::BoolLiteral:
                return context.semaBool;
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
            // arithmetic and bitwise operators give their operands' type, so what the context wants flows into them, while comparisons and logic give a bool
            case ExprKind::Binary: {
                auto& binary = static_cast<BinaryExpr&>(e);
                auto category = binary.op.op->category;
                bool passes = category == OpCategory::Arithmetic || category == OpCategory::Bitwise;
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
            case ExprKind::Call: {
                auto& call = static_cast<CallExpr&>(e);
                std::vector<Expr*> args;
                for (auto& a : call.args) args.push_back(a.get());
                return checkCall(*call.callee, args, e.span);
            }
            // x |> f and f <| x are both f(x)
            case ExprKind::Pipe: {
                auto& pipe = static_cast<PipeExpr&>(e);
                bool forward = pipe.op.kind == TokenKind::PipeFwd;
                auto& fn = forward ? *pipe.rhs : *pipe.lhs;
                auto& arg = forward ? *pipe.lhs : *pipe.rhs;
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
                return context.semaError;
            default:
                return unsupported(unsupportedName(e), e.span);
        }
    }
    // checks the entry point of a program that will run: a top-level main, declared the way the runtime calls it
    void checkMain(Scope& global) {
        Symbol* main = global.lookup("main");
        if (main == nullptr) {
            engine.printError("cherimoya requires a program to have a 'main' function, please add 'let () -> main -> (code :: int) { 0 -| }'", Span{0, 0});
            return;
        }
        if (isPoisoned(main->type)) return;
        if (main->kind != SymbolKind::Function || main->type != context.functionType({context.semaInt})) {
            engine.printError("cherimoya requires 'main' to be declared as 'let () -> main -> (code :: int)', please change its inputs and outputs", main->span);
        }
    }
public:
    Sema(DiagnosticEngine& e) : engine(e), scope(nullptr), owner(0), functions(0) {}

    // checks a whole program as one list of statements, and when entry is set, that it has a main to run
    void check(std::vector<std::unique_ptr<Stmt>>& program, bool entry = false) {
        Scope global(nullptr, engine, Span{0, engine.getManager().text().size()});
        scope = &global;
        checkStmts(program);
        if (entry) checkMain(global);
        scope = nullptr;
    }

    // every symbol sema declared, in order
    const std::deque<Symbol>& allSymbols() const { return symbols; }

    // every place a name is declared or used, every expected type, every call by name, and every function body, in the order sema met them
    const std::vector<Use>& allUses() const { return uses; }
    const std::vector<Expectation>& allExpectations() const { return expectations; }
    const std::vector<Call>& allCalls() const { return calls; }
    const std::vector<Body>& allBodies() const { return bodies; }
};
