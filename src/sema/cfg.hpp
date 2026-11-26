#pragma once

#include <algorithm>
#include <functional>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include "ast.hpp"
#include "codes.hpp"
#include "scope.hpp"
#include "target.hpp"

// one step along a place past the binding it starts at: an element at an index, which the program may only know at run time, or the value a pointer points at
struct PathStep {
    bool isDeref;
    std::optional<uint64_t> index;
};

// exactly what a place is, the binding at its base and every step after it, grid[2][0] or p[], which is what a view remembers about what it views
struct Path {
    const Symbol* root;
    std::vector<PathStep> steps;
};

// checks whether a view of one path points into what happens to another, so freeing or moving that would leave the view pointing at memory that's gone: the view has to run at least as far as the place does, along the same steps, where two indices the program knows tell elements apart and one it only knows at run time might be any element, and replacing a place leaves a view of that very place pointing at it still, while moving it doesn't
inline bool reaches(const Path& view, const Path& place, bool isMove) {
    if (view.root != place.root || view.steps.size() < place.steps.size()) return false;
    if (!isMove && view.steps.size() == place.steps.size()) return false;
    for (size_t i = 0; i < place.steps.size(); i++) {
        const PathStep& a = place.steps[i];
        const PathStep& b = view.steps[i];
        if (a.index && b.index && *a.index != *b.index) return false;
    }
    return true;
}

// what sema learned about ownership while it checked a function, which the graph is built from: every name read where it moves, every argument moved when its call happens, the places each call or assignment could free or move what a view points into, every view and the paths it views, every match that covers every value, and every binding or input that has to be moved before its scope ends
struct Facts {
    std::unordered_set<const Expr*> moves;
    std::unordered_set<const Expr*> callMoves;
    std::unordered_map<const Expr*, std::vector<std::pair<Path, bool>>> callHarms;
    std::unordered_map<const AssignStmt*, std::vector<Path>> assignHarms;
    std::unordered_map<const Symbol*, std::vector<Path>> views;
    std::unordered_set<const MatchExpr*> covered;
    std::unordered_set<const Symbol*> mustMove;
};

// a function's control-flow graph, basic blocks of the steps it takes joined by every way control goes on, and the ownership dataflow over it: moves flow forward, and where ways join that disagree, the ones still holding a value free it on their edge, views are live backward from every use, and nothing a live view points into can be freed or moved, where what's worked out goes into the frees and replacements codegen reads
class Ownership {
    // one thing a function does that ownership cares about: reading a name, moving one, binding an owner, replacing what one holds, doing what could free or move a place, taking a view, or leaving scopes, which frees the owners in them still holding a value
    struct Step {
        enum Kind { Use, Move, Bind, Replace, Harm, View, Leave } kind;
        const Symbol* symbol = nullptr;
        Span span{};
        const AssignStmt* assign = nullptr;
        std::vector<Path> paths;
        bool isMove = false;
        std::vector<const Symbol*> left;
        const void* site = nullptr;
    };

    // a way control goes from one block to another, keyed by where codegen frees what this way has to, when it does, and whether it goes back to the top of a loop
    struct Edge {
        size_t to;
        const void* site;
        Span span;
        bool isBack;
    };

    struct Block {
        std::vector<Step> steps;
        std::vector<Edge> next;
    };

    // a construct a yield can leave by, mirroring sema's targets, with the block control goes on to after it and how many scopes were open outside it
    struct Exit {
        TargetKind kind;
        std::optional<std::string_view> label;
        size_t block;
        size_t scopes;
    };

    DiagnosticEngine& engine;
    const Facts& facts;
    std::unordered_map<const void*, std::vector<Symbol*>>& drops;
    std::unordered_set<const AssignStmt*>& replaces;
    std::vector<Block> blocks;
    size_t current = 0;
    // the owners of every open scope, innermost last, and every construct a yield could leave by
    std::vector<std::vector<const Symbol*>> scopes;
    std::vector<Exit> exits;
    std::string_view function;

    size_t newBlock() {
        blocks.emplace_back();
        return blocks.size() - 1;
    }

    void link(size_t from, size_t to, const void* site, Span span, bool isBack = false) {
        blocks[from].next.push_back({to, site, span, isBack});
    }

    void add(Step step) {
        blocks[current].steps.push_back(std::move(step));
    }

    // a step that could free or move each of some places, a move when it takes what's there away entirely
    static Step harm(std::vector<Path> paths, bool isMove, Span span) {
        Step step = stepOf(Step::Harm, nullptr, span);
        step.paths = std::move(paths);
        step.isMove = isMove;
        return step;
    }

    // a step of a kind, about a symbol, at a span, with nothing else to it yet
    static Step stepOf(typename Step::Kind kind, const Symbol* symbol, Span span) {
        Step step;
        step.kind = kind;
        step.symbol = symbol;
        step.span = span;
        return step;
    }

    // whether a binding owns what it holds, which only a binding in a function does, where a view never owns anything, while a handle holding addresses owns its memory
    bool isOwner(const Symbol* symbol) {
        return symbol->isOwned && owns(symbol->type) && symbol->owner != 0 && !symbol->isView;
    }

    // the owners of every scope from depth in, innermost first and last declared first, which is the order they're freed in
    std::vector<const Symbol*> ownersFrom(size_t depth) {
        std::vector<const Symbol*> left;
        for (size_t i = scopes.size(); i-- > depth;) {
            for (size_t j = scopes[i].size(); j-- > 0;) left.push_back(scopes[i][j]);
        }
        return left;
    }

    // leaves every scope from depth in, at the site codegen frees what's left in them at
    void leave(size_t depth, const void* site, Span span) {
        Step step = stepOf(Step::Leave, nullptr, span);
        step.left = ownersFrom(depth);
        step.site = site;
        add(step);
    }

    // the steps an expression takes, in the order codegen evaluates it
    void expr(Expr& e) {
        switch (e.kind) {
            case ExprKind::Identifier: {
                const Symbol* symbol = static_cast<IdentifierExpr&>(e).decl;
                if (symbol == nullptr || symbol->kind == SymbolKind::Function) return;
                if (facts.moves.count(&e)) {
                    add(stepOf(Step::Move, symbol, e.span));
                    add(harm({Path{symbol, {}}}, true, e.span));
                } else {
                    add(stepOf(Step::Use, symbol, e.span));
                }
                return;
            }
            case ExprKind::Unary:
                expr(*static_cast<UnaryExpr&>(e).operand);
                return;
            case ExprKind::Addr:
                expr(*static_cast<AddrExpr&>(e).operand);
                return;
            case ExprKind::Binary: {
                auto& binary = static_cast<BinaryExpr&>(e);
                expr(*binary.lhs);
                expr(*binary.rhs);
                return;
            }
            case ExprKind::ExtraBinary: {
                auto& extra = static_cast<ExtraBinaryExpr&>(e);
                expr(*extra.lhs);
                expr(*extra.rhs);
                return;
            }
            case ExprKind::Chain:
                for (auto& operand : static_cast<ChainExpr&>(e).operands) expr(*operand);
                return;
            case ExprKind::Ann:
                expr(*static_cast<AnnExpr&>(e).expr);
                return;
            case ExprKind::Pack:
                for (auto& element : static_cast<PackExpr&>(e).elems) expr(*element.value);
                return;
            case ExprKind::Member: {
                auto& member = static_cast<MemberExpr&>(e);
                if (member.object) expr(*member.object);
                return;
            }
            case ExprKind::Index: {
                auto& index = static_cast<IndexExpr&>(e);
                expr(*index.object);
                for (auto& arg : index.args) expr(*arg);
                return;
            }
            case ExprKind::Call: {
                auto& call = static_cast<CallExpr&>(e);
                std::vector<Expr*> args;
                if (call.callee->kind == ExprKind::Member && static_cast<MemberExpr&>(*call.callee).object) args.push_back(static_cast<MemberExpr&>(*call.callee).object.get());
                for (auto& arg : call.args) args.push_back(arg.value.get());
                callSteps(e, args);
                return;
            }
            case ExprKind::Pipe: {
                auto& pipe = static_cast<PipeExpr&>(e);
                bool isForward = pipe.op.kind == TokenKind::PipeFwd;
                Expr& callee = isForward ? *pipe.rhs : *pipe.lhs;
                std::vector<Expr*> args{isForward ? pipe.lhs.get() : pipe.rhs.get()};
                if (callee.kind == ExprKind::Call) {
                    for (auto& arg : static_cast<CallExpr&>(callee).args) args.push_back(arg.value.get());
                }
                callSteps(e, args);
                return;
            }
            case ExprKind::Block: {
                auto& block = static_cast<BlockExpr&>(e);
                construct(block.stmts, TargetKind::Block, block.name);
                return;
            }
            case ExprKind::Chunk: {
                auto& chunk = static_cast<ChunkExpr&>(e);
                construct(chunk.body->stmts, TargetKind::Chunk, chunk.name);
                return;
            }
            case ExprKind::Match:
                match(static_cast<MatchExpr&>(e));
                return;
            case ExprKind::Repeat:
                repeat(static_cast<RepeatExpr&>(e));
                return;
            default:
                return;
        }
    }

    // a call's arguments in the order they're written, which is the order they're evaluated, then what the call itself does: an argument an unscoped input takes over moves once every argument is evaluated, so another argument can still read it first, and what the call could free or move happens there
    void callSteps(Expr& call, std::vector<Expr*> args) {
        std::sort(args.begin(), args.end(), [](Expr* a, Expr* b) { return a->span.start < b->span.start; });
        std::vector<Expr*> moved;
        for (Expr* arg : args) {
            if (facts.callMoves.count(arg)) {
                add(stepOf(Step::Use, static_cast<IdentifierExpr&>(*arg).decl, arg->span));
                moved.push_back(arg);
            } else {
                expr(*arg);
            }
        }
        for (Expr* arg : moved) add(stepOf(Step::Move, static_cast<IdentifierExpr&>(*arg).decl, arg->span));
        auto harms = facts.callHarms.find(&call);
        if (harms == facts.callHarms.end()) return;
        for (auto& [path, isMove] : harms->second) add(harm({path}, isMove, call.span));
    }

    // the steps a statement takes
    void stmt(Stmt& s) {
        switch (s.kind) {
            case StmtKind::Let: {
                auto& let = static_cast<LetStmt&>(s);
                if (let.init->kind != ExprKind::Wildcard) expr(*let.init);
                if (let.pattern->kind != PatternKind::Binder) return;
                Symbol* symbol = static_cast<BinderPattern&>(*let.pattern).decl;
                if (symbol == nullptr) return;
                // a handle holding addresses keeps what they name in place and owns its memory too, so it's both
                if (facts.views.count(symbol)) add(stepOf(Step::View, symbol, symbol->span));
                if (isOwner(symbol) && !scopes.empty()) {
                    scopes.back().push_back(symbol);
                    add(stepOf(Step::Bind, symbol, symbol->span));
                }
                return;
            }
            // the place is found before the value, the way codegen does it, where writing a binding isn't a use of it, but writing through a view or into an element is
            case StmtKind::Assign: {
                auto& assign = static_cast<AssignStmt&>(s);
                bool isBinding = assign.place->kind == ExprKind::Identifier;
                const Symbol* symbol = isBinding ? static_cast<IdentifierExpr&>(*assign.place).decl : nullptr;
                if (!isBinding && assign.place->kind != ExprKind::Wildcard) expr(*assign.place);
                if (symbol && (assign.op || facts.views.count(symbol))) add(stepOf(Step::Use, symbol, assign.place->span));
                expr(*assign.value);
                if (symbol && !assign.op && isOwner(symbol)) {
                    Step step = stepOf(Step::Replace, symbol, assign.span);
                    step.assign = &assign;
                    add(step);
                }
                auto harms = facts.assignHarms.find(&assign);
                if (harms != facts.assignHarms.end()) add(harm(harms->second, false, assign.span));
                return;
            }
            case StmtKind::Expr:
                expr(*static_cast<ExprStmt&>(s).expr);
                return;
            case StmtKind::Yield:
                yield(static_cast<YieldStmt&>(s));
                return;
            case StmtKind::Assert:
                expr(*static_cast<AssertStmt&>(s).cond);
                return;
            case StmtKind::Attr: {
                auto& attr = static_cast<AttrStmt&>(s);
                if (attr.target) stmt(*attr.target);
                return;
            }
            default:
                return;
        }
    }

    // a yield leaves every scope between it and its target, freeing what's left in them, and goes on after the target, where leaving the function goes nowhere
    void yield(YieldStmt& y) {
        if (y.value) expr(*y.value);
        Exit* target = nullptr;
        for (size_t i = exits.size(); i-- > 0;) {
            Exit& exit = exits[i];
            if (!y.target) { target = &exit; break; }
            auto kind = y.target->kind;
            if (kind == TokenKind::KwMatch && exit.kind == TargetKind::Match) { target = &exit; break; }
            if (kind == TokenKind::KwRepeat && exit.kind == TargetKind::Loop) { target = &exit; break; }
            if (kind == TokenKind::Identifier && exit.label == y.target->text) { target = &exit; break; }
            if (exit.kind == TargetKind::Function) break;
        }
        if (target) {
            leave(target->scopes, &y, y.span);
            if (target->kind != TargetKind::Function) link(current, target->block, &y, y.span);
        }
        current = newBlock();
    }

    // the statements of a scope, which leave it at their end, freeing what's left in it, and go on to after, unless control never gets there
    void body(std::vector<std::unique_ptr<Stmt>>& stmts, size_t after, Span end, bool isBack = false) {
        scopes.emplace_back();
        for (auto& s : stmts) stmt(*s);
        leave(scopes.size() - 1, &stmts, end);
        link(current, after, &stmts, end, isBack);
        scopes.pop_back();
    }

    // a block or a chunk: a scope a yield can leave by, which goes on after it
    void construct(std::vector<std::unique_ptr<Stmt>>& stmts, TargetKind kind, const std::optional<Token>& name) {
        size_t after = newBlock();
        exits.push_back({kind, name ? std::optional<std::string_view>(name->text) : std::nullopt, after, scopes.size()});
        Span end = stmts.empty() ? Span{} : Span{stmts.back()->span.end, stmts.back()->span.end};
        body(stmts, after, end);
        exits.pop_back();
        current = after;
    }

    // a match tests each arm in turn, so every arm's test comes from the one before, and each arm's body is a scope that goes on after the match, along with the way out when no arm matched, which only a match that doesn't cover every value has
    void match(MatchExpr& m) {
        if (m.subject) expr(*m.subject);
        size_t after = newBlock();
        exits.push_back({TargetKind::Match, m.name ? std::optional<std::string_view>(m.name->text) : std::nullopt, after, scopes.size()});
        for (auto& arm : m.arms) {
            size_t armBlock = newBlock();
            size_t next = newBlock();
            if (arm.pattern->kind == PatternKind::Guard) expr(*static_cast<GuardPattern&>(*arm.pattern).cond);
            bool isCatchAll = arm.pattern->kind == PatternKind::Wildcard || arm.pattern->kind == PatternKind::Binder;
            link(current, armBlock, nullptr, arm.span);
            if (!isCatchAll) link(current, next, nullptr, arm.span);
            current = armBlock;
            exits.push_back({TargetKind::Arm, std::nullopt, after, scopes.size()});
            body(arm.body->stmts, after, Span{arm.body->span.end - 1, arm.body->span.end});
            exits.pop_back();
            current = next;
        }
        if (!facts.covered.count(&m)) link(current, after, &m, Span{m.span.end - 1, m.span.end});
        exits.pop_back();
        current = after;
    }

    // a loop's header decides whether the body runs again, where a loop with a condition goes on after it when the condition fails, and the body's end goes back to the header
    void repeat(RepeatExpr& r) {
        size_t header = newBlock();
        link(current, header, nullptr, r.span);
        current = header;
        if (r.cond) expr(*r.cond);
        size_t loop = newBlock();
        size_t after = newBlock();
        link(current, loop, nullptr, r.span);
        if (r.cond) link(current, after, &r, r.span);
        loopHeaders.push_back({header, loop});
        exits.push_back({TargetKind::Loop, r.name ? std::optional<std::string_view>(r.name->text) : std::nullopt, after, scopes.size()});
        current = loop;
        body(r.body->stmts, header, Span{r.body->span.end - 1, r.body->span.end}, true);
        exits.pop_back();
        current = after;
    }

    // every loop's header and the block its body starts in
    std::vector<std::pair<size_t, size_t>> loopHeaders;

    // what's known about each owner at a point: in scope and holding a value, or moved, and where
    using State = std::unordered_map<const Symbol*, std::optional<Span>>;

    std::string lineOf(Span span) {
        return std::to_string(engine.getManager().getLineCol(span.start).line);
    }

    void usedAfterMove(const Symbol& symbol, Span use, Span moved) {
        std::string name(symbol.name);
        engine.printError(codes::useAfterMove, "cherimoya requires '" + name + "' to hold a value to use it, please assign it again after moving it on line " + lineOf(moved), use, {{"'" + name + "' moved here", moved}});
    }

    // reports an owner that has to be moved still holding its value where control leaves its scope, an unscoped input at the end of its function or an unscoped binding at the end of its scope
    void unmoved(const Symbol& symbol, Span span) {
        std::string name(symbol.name);
        std::string what = symbol.kind == SymbolKind::Param ? "before '" + std::string(function) + "' ends" : "before its scope ends";
        engine.printError(codes::unscopedUnmoved, "cherimoya requires '" + name + "' to be moved " + what + ", please yield it or pass it to an unscoped input", span, {{"'" + name + "' is declared here", symbol.span}});
    }

    // what a step does to the owners: a use of one that moved is an error, reported once, a move leaves it holding nothing, binding or replacing gives it a value, where replacing one that still held a value frees that first, and leaving scopes frees their owners still holding values, which an owner that has to be moved can't be left with
    void forward(const Step& step, State& state) {
        auto found = step.symbol ? state.find(step.symbol) : state.end();
        switch (step.kind) {
            case Step::Use:
                if (found != state.end() && found->second) {
                    usedAfterMove(*step.symbol, step.span, *found->second);
                    found->second.reset();
                }
                return;
            case Step::Move:
                if (found == state.end()) return;
                if (found->second) {
                    usedAfterMove(*step.symbol, step.span, *found->second);
                    return;
                }
                found->second = step.span;
                return;
            case Step::Bind:
                state[step.symbol] = std::nullopt;
                return;
            case Step::Replace:
                if (found == state.end()) return;
                if (!found->second) replaces.insert(step.assign);
                found->second.reset();
                return;
            case Step::Leave:
                for (const Symbol* symbol : step.left) {
                    auto it = state.find(symbol);
                    if (it == state.end()) continue;
                    if (!it->second) {
                        if (facts.mustMove.count(symbol)) {
                            unmoved(*symbol, step.span);
                        } else {
                            drops[step.site].push_back(const_cast<Symbol*>(symbol));
                        }
                    }
                    state.erase(it);
                }
                return;
            default:
                return;
        }
    }

    // the blocks in an order where every block comes after every block with a way into it but a loop's way back, which control flow without goto always has
    std::vector<size_t> order() {
        std::vector<size_t> post;
        std::vector<bool> seen(blocks.size(), false);
        std::function<void(size_t)> visit = [&](size_t b) {
            seen[b] = true;
            for (auto& edge : blocks[b].next) {
                if (!edge.isBack && !seen[edge.to]) visit(edge.to);
            }
            post.push_back(b);
        };
        visit(0);
        std::reverse(post.begin(), post.end());
        return post;
    }

    // moves flow forward: each block starts from what every way into it but a loop's way back agrees on, where a way that still holds a value another way moved frees it on its edge, so after the join it's moved whichever way control came, and a loop's way back has to bring every owner back the way the loop started, an owner moved in the body being an error and one assigned in the body that was moved before being freed at the body's end
    void moves() {
        auto sorted = order();
        std::vector<std::optional<State>> out(blocks.size());
        std::vector<std::vector<std::pair<size_t, const Edge*>>> into(blocks.size());
        for (size_t b = 0; b < blocks.size(); b++) {
            for (auto& edge : blocks[b].next) into[edge.to].push_back({b, &edge});
        }
        std::vector<State> in(blocks.size());
        for (size_t b : sorted) {
            State state;
            if (b != 0) {
                std::vector<std::pair<const State*, const Edge*>> ways;
                for (auto& [from, edge] : into[b]) {
                    if (!edge->isBack && out[from]) ways.push_back({&*out[from], edge});
                }
                if (ways.empty()) continue;
                state = *ways[0].first;
                for (auto& [way, edge] : ways) {
                    for (auto& [symbol, moved] : *way) {
                        if (moved && state.count(symbol) && !state[symbol]) state[symbol] = moved;
                    }
                }
                if (ways.size() > 1) {
                    for (auto& [way, edge] : ways) {
                        for (auto& [symbol, moved] : state) {
                            auto mine = way->find(symbol);
                            if (!moved || mine == way->end() || mine->second || edge->site == nullptr) continue;
                            if (facts.mustMove.count(symbol)) {
                                unmoved(*symbol, edge->span);
                            } else {
                                drops[edge->site].push_back(const_cast<Symbol*>(symbol));
                            }
                        }
                    }
                }
            }
            in[b] = state;
            for (auto& step : blocks[b].steps) forward(step, state);
            out[b] = state;
        }
        for (auto& [header, start] : loopHeaders) {
            for (auto& [from, edge] : into[header]) {
                if (!edge->isBack || !out[from]) continue;
                for (auto& [symbol, before] : in[header]) {
                    auto now = out[from]->find(symbol);
                    if (now == out[from]->end()) continue;
                    if (!before && now->second) {
                        std::string name(symbol->name);
                        engine.printError(codes::loopMove, "cherimoya requires '" + name + "' to hold a value at the end of every iteration, please assign it again after moving it on line " + lineOf(*now->second), *now->second, {{"'" + name + "' is declared here, outside the loop", symbol->span}});
                    } else if (before && !now->second) {
                        if (facts.mustMove.count(symbol)) {
                            unmoved(*symbol, edge->span);
                        } else {
                            drops[edge->site].push_back(const_cast<Symbol*>(symbol));
                        }
                    }
                }
            }
        }
    }

    // the views still to be used after a point, each with where it's used next
    using Live = std::unordered_map<const Symbol*, Span>;

    // what a step does to the views live after it, walking backward: a use of a view makes it live, and taking it is where it starts, so it isn't live before
    void backward(const Step& step, Live& live) {
        if (step.kind == Step::Use && step.symbol && facts.views.count(step.symbol)) live[step.symbol] = step.span;
        if (step.kind == Step::View) live.erase(step.symbol);
    }

    // views are live backward from every use, to a fixed point around loops, and nothing a live view points into can be freed or moved, which is an error where that happens
    void views() {
        std::vector<Live> liveIn(blocks.size());
        bool changed = true;
        while (changed) {
            changed = false;
            for (size_t b = blocks.size(); b-- > 0;) {
                Live live;
                for (auto& edge : blocks[b].next) {
                    for (auto& [symbol, span] : liveIn[edge.to]) live.emplace(symbol, span);
                }
                for (size_t i = blocks[b].steps.size(); i-- > 0;) backward(blocks[b].steps[i], live);
                if (live.size() != liveIn[b].size()) {
                    liveIn[b] = live;
                    changed = true;
                }
            }
        }
        for (size_t b = 0; b < blocks.size(); b++) {
            Live live;
            for (auto& edge : blocks[b].next) {
                for (auto& [symbol, span] : liveIn[edge.to]) live.emplace(symbol, span);
            }
            for (size_t i = blocks[b].steps.size(); i-- > 0;) {
                const Step& step = blocks[b].steps[i];
                if (step.kind == Step::Harm) {
                    for (auto& [view, use] : live) {
                        for (auto& viewed : facts.views.at(view)) {
                            bool isHit = std::any_of(step.paths.begin(), step.paths.end(), [&](const Path& place) { return reaches(viewed, place, step.isMove); });
                            if (!isHit) continue;
                            std::string root(viewed.root->name);
                            std::string name(view->name);
                            std::string how = view->isView ? "views it" : "holds its address";
                            engine.printError(codes::viewedMoved, "cherimoya requires '" + root + "' to stay where it is while '" + name + "' " + how + ", please use '" + name + "' for the last time before this", step.span, {{"'" + name + "' is used here, after it", use}});
                            break;
                        }
                    }
                }
                backward(step, live);
            }
        }
    }
public:
    Ownership(DiagnosticEngine& e, const Facts& f, std::unordered_map<const void*, std::vector<Symbol*>>& d, std::unordered_set<const AssignStmt*>& r)
        : engine(e), facts(f), drops(d), replaces(r) {}

    // builds a function's graph from its body, with its unscoped inputs as the owners of its own scope along with its body's bindings, and works out its ownership
    void check(FnDecl& fn, std::string_view name) {
        function = name;
        current = newBlock();
        scopes.emplace_back();
        for (auto& input : fn.inputs) {
            if (input.decl && facts.mustMove.count(input.decl)) {
                scopes.back().push_back(input.decl);
                add(stepOf(Step::Bind, input.decl, input.decl->span));
            }
        }
        exits.push_back({TargetKind::Function, name, 0, 0});
        for (auto& s : fn.body->stmts) stmt(*s);
        leave(0, &fn.body->stmts, Span{fn.body->span.end - 1, fn.body->span.end});
        exits.pop_back();
        moves();
        views();
    }
};
