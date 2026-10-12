#pragma once

#include <deque>
#include "scope.hpp"
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

    // turns a parsed type into a sema type, or null when the type is left to inference (let x :: mut <- 5)
    // refinements resolve to their base type here, and their predicates are checked once the subject is in scope
    SemaType* resolveType(const Type& t) {
        switch (t.kind) {
            case TypeKind::Named: {
                auto& named = static_cast<const NamedType&>(t);
                if (named.path.size() == 1 && named.args.empty()) {
                    auto name = named.path[0].text;
                    if (name == "int" || name == "i64") return context.sema_int;
                    if (name == "float" || name == "f64") return context.sema_f64;
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
            scope->insert(makeSymbol(fn.name.text, SymbolKind::Function, context.get(signature), false, span));
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
            if (fn.body) checkBlock(*fn.body);
        });
        owner = saved;
    }

    // a let binds one name for now, typed by its annotation, its initializer, or both when they agree
    void checkLet(LetStmt& let) {
        SemaType* declared = let.type ? resolveType(*let.type) : nullptr;
        SemaType* type = declared;
        if (let.init->kind == ExprKind::Wildcard) {
            if (declared == nullptr) {
                engine.print_error("cherimoya requires an uninitialized binding to have a type, please write 'let x :: int <- _;'", let.span);
                type = context.sema_error;
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

    // checks a list of statements in three passes: function signatures, then everything else in order, then function bodies,
    // so a body sees every name declared around it no matter where it is
    void checkStmts(std::vector<std::unique_ptr<Stmt>>& stmts) {
        hoist(stmts);
        for (auto& s : stmts) {
            if (fnOf(s.get()) == nullptr) checkStmt(*s);
        }
        for (auto& s : stmts) {
            if (auto fn = fnOf(s.get())) checkFn(*fn);
        }
    }

    // checks a block in a new lexical scope
    void checkBlock(BlockExpr& block) {
        inScope([&] { checkStmts(block.stmts); });
        block.type = context.sema_void;
    }

    // checks one statement
    void checkStmt(Stmt& s) {
        switch (s.kind) {
            case StmtKind::Let:
                checkLet(static_cast<LetStmt&>(s));
                break;
            case StmtKind::Assign: {
                auto& a = static_cast<AssignStmt&>(s);
                auto value = checkExpr(*a.value);
                if (a.place->kind == ExprKind::Wildcard) {
                    a.place->type = value;
                    break;
                }
                auto place = checkExpr(*a.place);
                // place op<- value is place <- place op value, which has to give back the place's type
                if (a.op) {
                    auto result = binaryType(*a.op->op->base, place, a.place->span, value, a.value->span);
                    if (!compatible(place, result)) mismatch(place, result, a.value->span);
                } else if (!compatible(place, value)) {
                    mismatch(place, value, a.value->span);
                }
                break;
            }
            case StmtKind::Expr:
                checkExpr(*static_cast<ExprStmt&>(s).expr);
                break;
            case StmtKind::Yield: {
                auto& y = static_cast<YieldStmt&>(s);
                if (y.value) checkExpr(*y.value);
                break;
            }
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
                if (a.target) checkStmt(*a.target);
                break;
            }
            default:
                break;
        }
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

    // checks whether a type carries an earlier error or comes from a yield, so nothing built from it reports again
    bool poisoned(SemaType* type) {
        return type == context.sema_error || type == context.sema_never;
    }

    // checks whether a type is one of the numeric types
    bool numeric(SemaType* type) {
        return type == context.sema_int || type == context.sema_f64;
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
                return context.sema_f64;
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
                checkBlock(static_cast<BlockExpr&>(e));
                return context.sema_void;
            case ExprKind::Error:
                return context.sema_error;
            default:
                return unsupported(unsupportedName(e), e.span);
        }
    }
public:
    Sema(DiagnosticEngine& e) : engine(e), scope(nullptr), owner(0), functions(0) {}

    // checks a whole program as one list of statements
    void check(std::vector<std::unique_ptr<Stmt>>& program) {
        Scope global(nullptr, engine);
        scope = &global;
        checkStmts(program);
        scope = nullptr;
    }

    // every symbol sema declared, in order
    const std::deque<Symbol>& all_symbols() const { return symbols; }
};
