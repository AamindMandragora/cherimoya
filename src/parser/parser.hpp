#pragma once

#include <algorithm>
#include "lexer.hpp"
#include "ast.hpp"

// a parser reads a vector of tokens with a cursor, the index of the next token, and reports to the diagnostic engine
class Parser {
    const std::vector<Token>& tokens;
    DiagnosticEngine& engine;
    size_t cursor;
    // set while parsing the top of a statement or a comprehension clause, where <- is an assignment or a membership clause instead of a construction
    bool arrowStops;

    // returns the token at an index, or EOF if that's past the end
    const Token& at(size_t index) {
        return tokens[std::min(index, tokens.size() - 1)];
    }

    // returns the token being pointed at by the cursor
    const Token& peek() {
        return tokens[cursor];
    }

    // returns the token n past the cursor, or EOF if that's past the end
    const Token& peekAt(size_t n) {
        return at(cursor + n);
    }

    // returns the token being pointed at by the cursor and increments the latter, never moving past EOF
    const Token& advance() {
        auto& tok = tokens[cursor];
        if (tok.kind != TokenKind::Eof) cursor++;
        return tok;
    }

    // checks whether a token closes something or ends a statement, which is where the code around a mistake picks back up, so recovery leaves it for that code to find
    bool isBoundary(TokenKind kind) {
        return kind == TokenKind::Semi || kind == TokenKind::RParen || kind == TokenKind::RBracket || kind == TokenKind::RBrace || kind == TokenKind::Eof;
    }

    // consumes the token if it's what we expected, and otherwise reports it, where what names the token and where it belongs
    Token expect(TokenKind kind, std::string_view what) {
        if (peek().kind == kind) return advance();
        expected(what, peek());
        return isBoundary(peek().kind) ? peek() : advance();
    }

    // reports that something else belonged where tok is
    void expected(std::string_view what, const Token& tok) {
        engine.printError("cherimoya expected " + std::string(what) + ", got " + describe(tok) + " instead", tokSpan(tok));
    }

    // names a token for a diagnostic: its text up to the end of its first line, or the end of the file
    std::string describe(const Token& tok) {
        if (tok.kind == TokenKind::Eof) return "the end of the file";
        return "'" + std::string(tok.text.substr(0, tok.text.find('\n'))) + "'";
    }

    // names an expression for a diagnostic by its kind
    std::string describe(const Expr& e) {
        switch (e.kind) {
            case ExprKind::Unary: return "a prefix operation";
            case ExprKind::Binary:
            case ExprKind::ExtraBinary: return "a binary operation";
            case ExprKind::Chain: return "a comparison chain";
            case ExprKind::Pipe: return "a pipe";
            case ExprKind::OpRef: return "an operator";
            case ExprKind::Old: return "an 'old' expression";
            case ExprKind::Call: return "a call";
            case ExprKind::Index: return "an index";
            case ExprKind::Member: return "a field access";
            case ExprKind::Construct: return "a construction";
            case ExprKind::Compr: return "a comprehension";
            case ExprKind::Fold: return "a fold";
            case ExprKind::Ann: return "an annotation";
            case ExprKind::TypeExpr: return "a type";
            case ExprKind::Closure: return "a closure";
            case ExprKind::Block: return "a block";
            case ExprKind::Match: return "a match";
            case ExprKind::Repeat: return "a repeat";
            case ExprKind::Chunk: return "a chunk";
            default: return "an expression";
        }
    }

    // gets the token's span from its location and length
    Span tokSpan(const Token& tok) {
        return {tok.loc, tok.loc + tok.text.length()};
    }

    // gets where the last consumed token ends
    SourceLoc prevEnd() {
        auto& tok = tokens[cursor == 0 ? 0 : cursor - 1];
        return tok.loc + tok.text.length();
    }

    // turns a precedence level from the operator table (1 tightest, 17 loosest) into a binding power, spaced by 10
    int bindingPower(int level) {
        return (18 - level) * 10;
    }

    // returns how strongly a token binds to the expression to its left
    int leftBindingPower(const Token& tok) {
        switch (tok.kind) {
            // level 1: call, index, and member
            case TokenKind::LParen:
            case TokenKind::LBracket:
            case TokenKind::Dot:
                return bindingPower(1);

            // level 1: construction, unless the <- starts a membership clause
            case TokenKind::LArrow:
                return arrowStops ? 0 : bindingPower(1);

            default:
                break;
        }
        // every other operator reads its level from the table, and compounds and the other flow tokens have none
        if (tok.op == nullptr || tok.op->level == 0) return 0;
        return bindingPower(tok.op->level);
    }

    // two operators on the same level need parentheses when their associativities differ or either one is non-associative
    void checkMixing(const OpInfo* prev, const Token& op) {
        if (prev == nullptr || prev->level != op.op->level) return;
        if (prev->assoc == op.op->assoc && prev->assoc != Assoc::None) return;
        std::string a = prev->spelling, b(op.text);
        engine.printError("cherimoya requires parentheses to mix '" + a + "' and '" + b + "', please write '(x " + a + " y) " + b + " z' or 'x " + a + " (y " + b + " z)'", tokSpan(op));
    }

    // how a token changes the bracket depth: an opening bracket goes one deeper and a closing one comes back out
    int depthChange(TokenKind kind) {
        if (kind == TokenKind::LParen || kind == TokenKind::LBracket || kind == TokenKind::LBrace) return 1;
        if (kind == TokenKind::RParen || kind == TokenKind::RBracket || kind == TokenKind::RBrace) return -1;
        return 0;
    }

    // finds the token that closes the bracket at index open, or EOF if it's never closed
    size_t findClose(size_t open) {
        int depth = 0;
        for (size_t i = open; i < tokens.size(); i++) {
            if (tokens[i].kind == TokenKind::Eof) return i;
            depth += depthChange(tokens[i].kind);
            if (depth == 0) return i;
        }
        return tokens.size() - 1;
    }

    // checks for a comma between open and close that isn't nested inside another bracket
    bool hasTopLevelComma(size_t open, size_t close) {
        int depth = 0;
        for (size_t i = open + 1; i < close; i++) {
            depth += depthChange(tokens[i].kind);
            if (depth == 0 && tokens[i].kind == TokenKind::Comma) return true;
        }
        return false;
    }

    // parses with arrowStops set one way, then puts it back, for the places that decide whether <- ends an expression
    template <typename F>
    auto withArrowStops(bool stops, F parse) {
        bool saved = arrowStops;
        arrowStops = stops;
        auto result = parse();
        arrowStops = saved;
        return result;
    }

    // parses an expression inside brackets or parentheses, where the outside's comprehension state doesn't apply
    std::unique_ptr<Expr> parseNested() {
        return withArrowStops(false, [&] { return parseExpr(0); });
    }

    // parses a parenthesized argument list, a call's or an attribute's
    std::vector<std::unique_ptr<Expr>> parseArgList() {
        std::vector<std::unique_ptr<Expr>> args;
        if (peek().kind != TokenKind::RParen) {
            args.push_back(parseNested());
            while (peek().kind == TokenKind::Comma) {
                advance();
                args.push_back(parseNested());
            }
        }
        expect(TokenKind::RParen, "')' after the arguments in 'f(a, b)'");
        return args;
    }

    // scans to the matching ) and decides from what's inside and what follows whether this is a fold, a grouping, or an error
    std::unique_ptr<Expr> parseParen(const Token& open) {
        size_t openIdx = cursor - 1;
        size_t close = findClose(openIdx);
        auto after = at(close + 1).kind;
        bool closed = tokens[close].kind == TokenKind::RParen;
        // skips the whole parenthesized list after reporting an error about it
        auto skip = [&](std::string_view error) -> std::unique_ptr<Expr> {
            auto span = Span{open.loc, tokSpan(tokens[close]).end};
            engine.printError(error, span);
            cursor = closed ? close + 1 : close;
            return std::make_unique<ErrorExpr>(span);
        };
        // a pattern's -> starts its arm's body, so ( ) -> is only a closure outside a pattern
        if (closed && after == TokenKind::RArrow && !arrowStops) {
            return parseClosure(open.loc, {});
        }
        if (closed && close == openIdx + 1) {
            return skip("cherimoya uses '[]' for an empty pack instead of '()' as follows: let empty <- [];");
        }
        if (closed && close == openIdx + 2 && tokens[openIdx + 1].op != nullptr) {
            std::string op(tokens[openIdx + 1].text);
            return skip("cherimoya requires a bare operator to be a fold's reducer, please write '(" + op + ", seed)[xs]' since '(" + op + ")' on its own is reserved");
        }
        if (hasTopLevelComma(openIdx, close)) {
            if (closed && after == TokenKind::LBracket) return parseFold(open, close);
            return skip("cherimoya uses packs for tuples instead of parentheses as follows: [a, b]");
        }
        // a grouping, or an annotation if :: follows, whose span takes in its parentheses
        auto inner = parseNested();
        if (peek().kind == TokenKind::ColonColon) {
            advance();
            auto type = parseType();
            auto end = expect(TokenKind::RParen, "')' after the annotation in '(value :: Type)'");
            return std::make_unique<AnnExpr>(std::move(inner), std::move(type), Span{open.loc, end.loc + end.text.length()});
        }
        auto end = expect(TokenKind::RParen, "')' to close the parenthesized expression");
        inner->span = {open.loc, tokSpan(end).end};
        return inner;
    }

    // parses (reducer, seed)[sequence], where the reducer may be a bare operator and a one-element sequence is unwrapped
    std::unique_ptr<Expr> parseFold(const Token& open, size_t close) {
        std::unique_ptr<Expr> reducer;
        if (peek().op != nullptr && peekAt(1).kind == TokenKind::Comma) {
            auto op = advance();
            reducer = std::make_unique<OpRefExpr>(op, tokSpan(op));
        } else {
            reducer = parseNested();
        }
        expect(TokenKind::Comma, "',' after the fold's reducer in '(reducer, seed)[xs]'");
        auto seed = parseNested();
        if (peek().kind != TokenKind::RParen) {
            engine.printError("cherimoya requires a fold to take exactly a reducer and a seed, please write '(reducer, seed)[xs]'", tokSpan(peek()));
            if (close > cursor) cursor = close;
        }
        advance();
        auto bracket = expect(TokenKind::LBracket, "'[' after the fold's seed in '(reducer, seed)[xs]'");
        auto sequence = parsePack(bracket);
        if (sequence->kind == ExprKind::Pack) {
            auto& pack = static_cast<PackExpr&>(*sequence);
            if (pack.elems.size() == 1 && !pack.elems[0].name) sequence = std::move(pack.elems[0].value);
        }
        auto span = Span{open.loc, prevEnd()};
        return std::make_unique<FoldExpr>(std::move(reducer), std::move(seed), std::move(sequence), span);
    }

    // one element of a pack before it's known whether the pack is a range: a value, a name, an exclusivity marker, or ...
    struct RawElem {
        std::optional<Token> name;
        std::unique_ptr<Expr> value;
        std::optional<Token> marker;
        bool hasEllipsis;
        Span span;
    };

    // parses one pack element, catching the arrows that don't belong in a pack
    RawElem parseElem() {
        RawElem elem{std::nullopt, nullptr, std::nullopt, false, tokSpan(peek())};
        auto start = peek().loc;
        if (peek().kind == TokenKind::Ellipsis) {
            advance();
            elem.hasEllipsis = true;
        } else if (peek().kind == TokenKind::Gt || peek().kind == TokenKind::Lt) {
            elem.marker = advance();
            elem.value = parseNested();
        } else if (peek().kind == TokenKind::LArrow) {
            engine.printError("cherimoya uses '< -' for an exclusivity marker on a negative bound instead of '<-' as follows: [5, ..., < -1]", tokSpan(peek()));
            advance();
            elem.value = parseNested();
        } else if (peek().kind == TokenKind::Identifier && peekAt(1).kind == TokenKind::LArrow) {
            elem.name = advance();
            advance();
            elem.value = parseNested();
        } else {
            elem.value = parseNested();
        }
        if (peek().kind == TokenKind::RArrow) {
            engine.printError("cherimoya uses a pack of pairs for a map instead of '->' as follows: [[key, value], [key, value]]", tokSpan(peek()));
            advance();
            parseNested();
        }
        elem.span = {start, prevEnd()};
        return elem;
    }

    // parses the clauses after the colon of a comprehension, where a top-level <- starts a membership clause, and a || between clauses is reported and read as && so the rest still parses
    std::vector<ComprClause> parseClauses() {
        std::vector<ComprClause> clauses;
        while (true) {
            auto expr = withArrowStops(true, [&] { return parseExpr(bindingPower(12)); });
            if (peek().kind == TokenKind::LArrow) {
                advance();
                auto source = withArrowStops(false, [&] { return parseExpr(bindingPower(12)); });
                clauses.push_back({toPattern(std::move(expr)), std::move(source)});
            } else {
                clauses.push_back({nullptr, std::move(expr)});
            }
            if (peek().kind == TokenKind::BarBar) {
                engine.printError("cherimoya requires a comprehension's clauses to be joined with '&&', please wrap the '||' in parentheses, as in [x : x <- xs && (a || b)]", tokSpan(peek()));
            } else if (peek().kind != TokenKind::AmpAmp) {
                break;
            }
            advance();
        }
        return clauses;
    }

    // parses everything after a [ into a pack, a range (any ...), or a comprehension (a : after the first element)
    std::unique_ptr<Expr> parsePack(const Token& open) {
        size_t close = findClose(cursor - 1);
        std::vector<RawElem> elems;
        if (peek().kind != TokenKind::RBracket) {
            elems.push_back(parseElem());
            // a comprehension, once a colon follows the first element
            if (peek().kind == TokenKind::Colon) {
                advance();
                auto& head = elems[0];
                if (head.name || head.marker || head.hasEllipsis) {
                    engine.printError("cherimoya requires a comprehension's head to be a plain expression, please remove the name, exclusivity marker, or '...' before ':'", head.span);
                }
                auto clauses = parseClauses();
                if (peek().kind != TokenKind::RBracket) {
                    expected("']' after the clauses in '[head : clauses]'", peek());
                    if (close > cursor) cursor = close;
                }
                advance();
                auto value = head.value ? std::move(head.value) : std::make_unique<ErrorExpr>(head.span);
                return std::make_unique<ComprExpr>(std::move(value), std::move(clauses), Span{open.loc, prevEnd()});
            }
            while (peek().kind == TokenKind::Comma) {
                advance();
                elems.push_back(parseElem());
            }
        }
        if (peek().kind != TokenKind::RBracket) {
            expected("']' after the last element in '[a, b]'", peek());
            if (close > cursor) cursor = close;
        }
        advance();
        auto span = Span{open.loc, prevEnd()};

        // a range has exactly one ..., at most two values before it and one after, and exclusivity markers only on its endpoints
        size_t ellipses = 0;
        size_t at = 0;
        for (size_t i = 0; i < elems.size(); i++) {
            if (elems[i].hasEllipsis) { ellipses++; at = i; }
        }
        if (ellipses > 0) {
            if (ellipses > 1) {
                engine.printError("cherimoya requires a range to have exactly one '...', please write '[first, ..., last]'", span);
            } else if (at > 2 || elems.size() - at - 1 > 1) {
                engine.printError("cherimoya requires a range to have at most two values before '...' and one after, please write '[first, second, ..., last]'", span);
            } else if (elems.size() == 1) {
                engine.printError("cherimoya requires a range to have at least one bound, please write '[first, ...]' or '[..., last]'", span);
            }
            std::vector<RangeElem> range;
            for (size_t i = 0; i < elems.size(); i++) {
                auto& e = elems[i];
                if (e.name) engine.printError("cherimoya requires range elements to be unnamed, please remove '" + std::string(e.name->text) + " <-'", e.span);
                if (e.marker && i != 0 && i != elems.size() - 1) {
                    engine.printError("cherimoya requires exclusivity markers to be on a range's endpoints, please move it to the first or last value", e.span);
                }
                range.push_back({std::move(e.value), e.marker});
            }
            return std::make_unique<RangeExpr>(std::move(range), span);
        }

        // anything else is a pack
        std::vector<PackElem> pack;
        for (auto& e : elems) {
            if (e.marker) engine.printError("cherimoya requires exclusivity markers to be inside a range, please add '...' or remove the marker", e.span);
            pack.push_back({e.name, std::move(e.value)});
        }
        return std::make_unique<PackExpr>(std::move(pack), span);
    }

    // checks whether a token can start a type
    bool startsType(const Token& tok) {
        switch (tok.kind) {
            case TokenKind::TypeName:
            case TokenKind::Identifier:
            case TokenKind::KwPtr:
            case TokenKind::KwArr:
            case TokenKind::KwMut:
            case TokenKind::KwPeek:
            case TokenKind::KwUnscoped:
            case TokenKind::KwWit:
            case TokenKind::LBracket:
                return true;
            default:
                return false;
        }
    }

    // parses one generic argument: a type, or a comptime value when it can't be a type (arr[16] T, arr[N * 2] T)
    GenericArg parseGenericArg() {
        bool nameOnly = peek().kind == TokenKind::Identifier;
        if (nameOnly) {
            auto next = peekAt(1).kind;
            nameOnly = next == TokenKind::Comma || next == TokenKind::RBracket || next == TokenKind::LBracket || next == TokenKind::Dot;
        }
        if (nameOnly || (startsType(peek()) && peek().kind != TokenKind::Identifier)) {
            return {parseType(), nullptr};
        }
        return {nullptr, parseNested()};
    }

    // parses a dotted path and its generic arguments: std.mem.Allocator, Map[K, V]
    std::unique_ptr<Type> parseNamedType() {
        auto start = peek().loc;
        std::vector<Token> path{advance()};
        while (peek().kind == TokenKind::Dot && peekAt(1).kind == TokenKind::Identifier) {
            advance();
            path.push_back(advance());
        }
        std::vector<GenericArg> args;
        if (peek().kind == TokenKind::LBracket) {
            advance();
            if (peek().kind != TokenKind::RBracket) {
                args.push_back(parseGenericArg());
                while (peek().kind == TokenKind::Comma) {
                    advance();
                    args.push_back(parseGenericArg());
                }
            }
            expect(TokenKind::RBracket, "']' after the generic arguments in 'Name[A, B]'");
        }
        return std::make_unique<NamedType>(std::move(path), std::move(args), Span{start, prevEnd()});
    }

    // parses a refinement after its base type, naming the binder in type definitions: {p : 0 <= p}, or not on parameters: {a >= 0}
    std::unique_ptr<Type> parseRefinement(std::unique_ptr<Type> inner) {
        auto start = inner->span.start;
        expect(TokenKind::LBrace, "'{' to start the refinement in 'T {predicate}'");
        std::optional<Token> binder = std::nullopt;
        if (peek().kind == TokenKind::Identifier && peekAt(1).kind == TokenKind::Colon) {
            binder = advance();
            advance();
        }
        auto predicate = parseNested();
        expect(TokenKind::RBrace, "'}' after the refinement's predicate in 'T {predicate}'");
        return std::make_unique<RefinedType>(std::move(inner), binder, std::move(predicate), Span{start, prevEnd()});
    }

    // parses name :: type pairs up to the closing ), where a missing type is left to inference
    std::vector<Param> parseParamList() {
        std::vector<Param> params = {};
        while (peek().kind != TokenKind::RParen && peek().kind != TokenKind::Eof) {
            auto name = expect(TokenKind::Identifier, "a parameter name in '(name :: Type)'");
            std::unique_ptr<Type> type = nullptr;
            if (peek().kind == TokenKind::ColonColon) {
                advance();
                type = parseType();
            }
            params.push_back(Param{name, std::move(type)});
            if (peek().kind != TokenKind::Comma) break;
            advance();
        }
        expect(TokenKind::RParen, "')' after the parameters in '(name :: Type, name :: Type)'");
        return params;
    }

    // parses (params) -> (outputs) { body } after the (, with outputs optional and captures already parsed
    std::unique_ptr<Expr> parseClosure(SourceLoc start, std::vector<std::unique_ptr<Expr>> captures) {
        auto params = parseParamList();
        expect(TokenKind::RArrow, "'->' after the closure's parameters in '(params) -> (outputs) { body }'");
        std::optional<std::vector<Param>> outputs = std::nullopt;
        if (peek().kind == TokenKind::LParen) {
            advance();
            outputs = parseParamList();
        }
        auto body = parseBlock(std::nullopt);
        return std::make_unique<ClosureExpr>(std::move(captures), std::move(params), std::move(outputs), std::move(body), Span{start, prevEnd()});
    }

    // reinterprets an expression as a pattern, since every pattern is written like an expression
    std::unique_ptr<Pattern> toPattern(std::unique_ptr<Expr> e) {
        auto span = e->span;
        switch (e->kind) {
            case ExprKind::IntLiteral:
            case ExprKind::FloatLiteral:
            case ExprKind::BoolLiteral:
            case ExprKind::StringLiteral:
            case ExprKind::CharLiteral:
                return std::make_unique<LiteralPattern>(std::move(e), span);
            case ExprKind::Unary: {
                auto& u = static_cast<UnaryExpr&>(*e);
                auto inner = u.operand->kind;
                if (u.op.kind == TokenKind::Minus && (inner == ExprKind::IntLiteral || inner == ExprKind::FloatLiteral)) {
                    return std::make_unique<LiteralPattern>(std::move(e), span);
                }
                break;
            }
            case ExprKind::Wildcard:
                return std::make_unique<WildcardPattern>(span);
            case ExprKind::Identifier: {
                auto& id = static_cast<IdentifierExpr&>(*e);
                return std::make_unique<BinderPattern>(Token(TokenKind::Identifier, id.name, span.start), span);
            }
            // a variant with a payload
            case ExprKind::Index: {
                auto& ix = static_cast<IndexExpr&>(*e);
                if (ix.object->kind != ExprKind::Identifier) break;
                auto& id = static_cast<IdentifierExpr&>(*ix.object);
                std::vector<std::unique_ptr<Pattern>> payload;
                for (auto& a : ix.args) payload.push_back(toPattern(std::move(a)));
                return std::make_unique<VariantPattern>(Token(TokenKind::Identifier, id.name, id.span.start), std::move(payload), span);
            }
            case ExprKind::Pack: {
                std::vector<PackPatternElem> elems;
                for (auto& el : static_cast<PackExpr&>(*e).elems) elems.push_back({el.name, toPattern(std::move(el.value))});
                return std::make_unique<PackPattern>(std::move(elems), span);
            }
            case ExprKind::Range: {
                std::vector<RangePatternElem> elems;
                for (auto& el : static_cast<RangeExpr&>(*e).elems) elems.push_back({std::move(el.value), el.marker});
                return std::make_unique<RangePattern>(std::move(elems), span);
            }
            // a set-builder, with a lone binder head and only filter clauses
            case ExprKind::Compr: {
                auto& c = static_cast<ComprExpr&>(*e);
                bool filters = std::all_of(c.clauses.begin(), c.clauses.end(), [](const ComprClause& cl) { return cl.pattern == nullptr; });
                if (c.head->kind != ExprKind::Identifier || !filters) break;
                auto& id = static_cast<IdentifierExpr&>(*c.head);
                std::vector<std::unique_ptr<Expr>> predicates;
                for (auto& cl : c.clauses) predicates.push_back(std::move(cl.expr));
                return std::make_unique<SetBuilderPattern>(Token(TokenKind::Identifier, id.name, id.span.start), std::move(predicates), span);
            }
            case ExprKind::Error:
                return std::make_unique<ErrorPattern>(span);
            default:
                break;
        }
        engine.printError("cherimoya expected a pattern (a literal, a name, _, Variant[p], a pack, a range, or a set-builder), got " + describe(*e) + " instead", span);
        return std::make_unique<ErrorPattern>(span);
    }

    // parses -| and its optional target after an optional value: an identifier, repeat, or match
    std::unique_ptr<Stmt> parseYield(std::unique_ptr<Expr> value) {
        auto arrow = advance();
        auto start = value ? value->span.start : arrow.loc;
        std::optional<Token> target = std::nullopt;
        auto next = peek().kind;
        if (next == TokenKind::Identifier || next == TokenKind::KwRepeat || next == TokenKind::KwMatch) {
            target = advance();
        }
        return std::make_unique<YieldStmt>(std::move(value), target, Span{start, prevEnd()});
    }

    // expects the ; that ends a statement, without consuming something else in its place or piling onto an earlier error
    void expectSemi(const Expr* expr) {
        if (peek().kind == TokenKind::Semi) {
            advance();
            return;
        }
        if (expr != nullptr && expr->kind == ExprKind::Error) return;
        expected("';' after the statement", peek());
    }

    // checks if an expression can be assigned to: names, fields, indexes, dereferences, and _
    bool isPlace(const Expr& e) {
        switch (e.kind) {
            case ExprKind::Identifier:
            case ExprKind::Member:
            case ExprKind::Index:
            case ExprKind::Wildcard:
                return true;
            default:
                return false;
        }
    }

    // a scope at the start of a statement takes no ; and yields its value if -| follows it
    std::unique_ptr<Stmt> scopeStmt(std::unique_ptr<Expr> scope) {
        if (peek().kind == TokenKind::Yield) return parseYield(std::move(scope));
        auto span = scope->span;
        return std::make_unique<ExprStmt>(std::move(scope), span);
    }

    // parses let pattern :: type <- init; or a function, which starts let ( or let [generics] (
    std::unique_ptr<Stmt> parseLet() {
        auto start = advance();
        bool function = peek().kind == TokenKind::LParen;
        if (peek().kind == TokenKind::LBracket) {
            size_t close = findClose(cursor);
            function = at(close + 1).kind == TokenKind::LParen;
        }
        if (function) return declStmt(parseFnDecl(start, false));
        auto pattern = parsePattern();
        std::unique_ptr<Type> type = nullptr;
        if (peek().kind == TokenKind::ColonColon) {
            advance();
            type = parseType();
        }
        std::unique_ptr<Expr> init;
        if (peek().kind == TokenKind::LArrow) {
            advance();
            init = parseNested();
        } else {
            engine.printError("cherimoya requires a binding to have an initializer, please write '<- _' to leave it uninitialized", tokSpan(peek()));
            init = std::make_unique<ErrorExpr>(tokSpan(peek()));
        }
        expectSemi(init.get());
        return std::make_unique<LetStmt>(std::move(pattern), std::move(type), std::move(init), Span{start.loc, prevEnd()});
    }

    // parses wit {predicate}; or wit {T :: Interface};
    std::unique_ptr<Stmt> parseWit() {
        auto start = advance();
        expect(TokenKind::LBrace, "'{' after 'wit' in 'wit {predicate};'");
        auto predicate = parseNested();
        std::unique_ptr<Type> bound = nullptr;
        if (peek().kind == TokenKind::ColonColon) {
            advance();
            bound = parseType();
        }
        expect(TokenKind::RBrace, "'}' after the obligation in 'wit {predicate};'");
        expectSemi(nullptr);
        return std::make_unique<WitStmt>(std::move(predicate), std::move(bound), Span{start.loc, prevEnd()});
    }

    // parses assert (cond);
    std::unique_ptr<Stmt> parseAssert() {
        auto start = advance();
        expect(TokenKind::LParen, "'(' after 'assert' in 'assert (condition);'");
        auto cond = parseNested();
        expect(TokenKind::RParen, "')' after the condition in 'assert (condition);'");
        expectSemi(nullptr);
        return std::make_unique<AssertStmt>(std::move(cond), Span{start.loc, prevEnd()});
    }

    // takes the explicit name that goes right after a scope keyword, if the token after it is one of the given kinds
    std::optional<Token> parseScopeName(TokenKind a, TokenKind b) {
        if (peek().kind == TokenKind::Identifier && (peekAt(1).kind == a || peekAt(1).kind == b)) return advance();
        return std::nullopt;
    }

    // parses match name (equality, subject) { arms }, where a subjectless match's arms are (condition) or _, deep by default
    std::unique_ptr<Expr> parseMatch(const Token& start) {
        auto name = parseScopeName(TokenKind::LParen, TokenKind::LBrace);
        std::unique_ptr<Expr> subject = nullptr;
        std::optional<Token> equality = std::nullopt;
        if (peek().kind == TokenKind::LParen) {
            advance();
            if (peek().op != nullptr && peekAt(1).kind == TokenKind::Comma) {
                equality = advance();
                advance();
                if (equality->kind != TokenKind::Eq && equality->kind != TokenKind::EqEq) {
                    engine.printError("cherimoya requires a match's equality to be '=' or '==', please write 'match (=, subject)' or 'match (==, subject)'", tokSpan(*equality));
                }
            }
            subject = parseNested();
            expect(TokenKind::RParen, "')' after the subject in 'match (subject) { arms }'");
        }
        expect(TokenKind::LBrace, "'{' before the arms in 'match (subject) { arms }'");
        std::vector<MatchArm> arms = {};
        while (peek().kind != TokenKind::RBrace && peek().kind != TokenKind::Eof) {
            auto armStart = peek().loc;
            std::unique_ptr<Pattern> pattern;
            if (subject == nullptr && peek().kind == TokenKind::LParen) {
                advance();
                auto cond = parseNested();
                expect(TokenKind::RParen, "')' after the guard in '(condition) -> { body }'");
                pattern = std::make_unique<GuardPattern>(std::move(cond), Span{armStart, prevEnd()});
            } else {
                pattern = parsePattern();
                if (subject == nullptr && pattern->kind != PatternKind::Wildcard && pattern->kind != PatternKind::Error) {
                    engine.printError("cherimoya requires a subjectless match's arms to be '(condition)' or '_', please write '(condition) -> { body }'", pattern->span);
                }
            }
            expect(TokenKind::RArrow, "'->' after the pattern in 'pattern -> { body }'");
            auto body = parseBlock(std::nullopt);
            arms.push_back(MatchArm{std::move(pattern), std::move(body), Span{armStart, prevEnd()}});
        }
        expect(TokenKind::RBrace, "'}' after the arms in 'match (subject) { arms }'");
        return std::make_unique<MatchExpr>(name, equality, std::move(subject), std::move(arms), Span{start.loc, prevEnd()});
    }

    // parses all three kinds of loop: (binder) <- source is for-each, (cond) alone is while, and nothing is infinite
    std::unique_ptr<Expr> parseRepeat(const Token& start) {
        auto name = parseScopeName(TokenKind::LParen, TokenKind::LBrace);
        std::unique_ptr<Pattern> binder = nullptr;
        std::unique_ptr<Expr> source = nullptr;
        std::unique_ptr<Expr> cond = nullptr;
        if (peek().kind == TokenKind::LParen) {
            advance();
            auto inner = parseNested();
            expect(TokenKind::RParen, "')' after the loop header in 'repeat (binder) <- source { body }'");
            if (peek().kind == TokenKind::LArrow) {
                advance();
                binder = toPattern(std::move(inner));
                source = parseNested();
            } else {
                cond = std::move(inner);
            }
        }
        std::unique_ptr<Expr> invariant = nullptr;
        if (peek().kind == TokenKind::KwWit) {
            advance();
            expect(TokenKind::LBrace, "'{' after 'wit' in 'wit {invariant}'");
            invariant = parseNested();
            expect(TokenKind::RBrace, "'}' after the invariant in 'wit {invariant}'");
        }
        auto body = parseBlock(std::nullopt);
        return std::make_unique<RepeatExpr>(name, std::move(binder), std::move(source), std::move(cond), std::move(invariant), std::move(body), Span{start.loc, prevEnd()});
    }

    // parses chunk name { body }
    std::unique_ptr<Expr> parseChunk(const Token& start) {
        auto name = parseScopeName(TokenKind::LBrace, TokenKind::LBrace);
        auto body = parseBlock(std::nullopt);
        return std::make_unique<ChunkExpr>(name, std::move(body), Span{start.loc, prevEnd()});
    }

    // parses [name :: bound & bound <- fallback, ...] after a function's let or a type's name
    std::vector<GenericParam> parseGenericParams() {
        std::vector<GenericParam> params;
        expect(TokenKind::LBracket, "'[' to start the generic parameters in '[T :: Bound <- Fallback]'");
        while (peek().kind != TokenKind::RBracket && peek().kind != TokenKind::Eof) {
            auto name = expect(TokenKind::Identifier, "a generic parameter name in '[T :: Bound <- Fallback]'");
            std::vector<std::unique_ptr<Type>> bounds;
            if (peek().kind == TokenKind::ColonColon) {
                advance();
                bounds.push_back(parseType(false));
                while (peek().kind == TokenKind::Amp) {
                    advance();
                    bounds.push_back(parseType(false));
                }
            }
            std::optional<GenericArg> fallback = std::nullopt;
            if (peek().kind == TokenKind::LArrow) {
                advance();
                fallback = parseGenericArg();
            }
            params.push_back(GenericParam{name, std::move(bounds), std::move(fallback)});
            if (peek().kind != TokenKind::Comma) break;
            advance();
        }
        expect(TokenKind::RBracket, "']' after the generic parameters in '[T :: Bound <- Fallback]'");
        return params;
    }

    // checks whether a token can name a function: an identifier or an overloadable operator, built-in or extra
    bool isFnName(const Token& tok) {
        return tok.kind == TokenKind::Identifier || (tok.op != nullptr && tok.op->isOverloadable);
    }

    // parses everything after let in [generics] (inputs) -> name -> (outputs) wit {obligations} body, where a member has no body, an extern ends with ;, and anything else has a block
    std::unique_ptr<FnDecl> parseFnDecl(const Token& start, bool member) {
        std::vector<GenericParam> generics;
        if (peek().kind == TokenKind::LBracket) generics = parseGenericParams();
        expect(TokenKind::LParen, "'(' before the inputs in 'let (inputs) -> name -> (outputs) { body }'");
        auto inputs = parseParamList();
        expect(TokenKind::RArrow, "'->' after the inputs in 'let (inputs) -> name -> (outputs) { body }'");
        // a token that isn't a name stands in for one, unless it's the -> after a name that's missing
        auto name = peek();
        if (!isFnName(name)) expected("a function name (an identifier or an overloadable operator) in 'let (inputs) -> name -> (outputs) { body }'", name);
        if (name.kind != TokenKind::RArrow) advance();
        expect(TokenKind::RArrow, "'->' after the function's name in 'let (inputs) -> name -> (outputs) { body }'");
        expect(TokenKind::LParen, "'(' before the outputs in 'let (inputs) -> name -> (outputs) { body }'");
        auto outputs = parseParamList();
        std::unique_ptr<Expr> obligations = nullptr;
        if (peek().kind == TokenKind::KwWit) {
            advance();
            expect(TokenKind::LBrace, "'{' after 'wit' in 'wit {obligations}'");
            obligations = parseNested();
            expect(TokenKind::RBrace, "'}' after the obligations in 'wit {obligations}'");
        }
        std::unique_ptr<BlockExpr> body = nullptr;
        if (!member) {
            if (peek().kind == TokenKind::Semi) {
                advance();
            } else {
                body = parseBlock(std::nullopt);
            }
        }
        return std::make_unique<FnDecl>(std::move(generics), std::move(inputs), name, std::move(outputs), std::move(obligations), std::move(body), Span{start.loc, prevEnd()});
    }

    // turns one alternative of a sum into a variant: a single name with only types in its brackets
    Variant toVariant(std::unique_ptr<Type> type) {
        if (type->kind == TypeKind::Named) {
            auto& named = static_cast<NamedType&>(*type);
            bool types = std::all_of(named.args.begin(), named.args.end(), [](const GenericArg& a) { return a.type != nullptr; });
            if (named.path.size() == 1 && types) {
                std::vector<std::unique_ptr<Type>> payload;
                for (auto& a : named.args) payload.push_back(std::move(a.type));
                return Variant{named.path[0], std::move(payload)};
            }
        }
        engine.printError("cherimoya requires each variant of a sum to be a name with an optional payload, please write 'Name' or 'Name[T, U]'", type->span);
        return Variant{Token(TokenKind::Error, "", type->span.start), {}};
    }

    // parses type name[generics] <- rhs; and decides the form from the right side
    std::unique_ptr<Decl> parseTypeDecl() {
        auto start = advance();
        auto name = expect(TokenKind::Identifier, "the type's name in 'type Name <- definition;'");
        std::vector<GenericParam> generics;
        if (peek().kind == TokenKind::LBracket) generics = parseGenericParams();
        expect(TokenKind::LArrow, "'<-' after the type's name in 'type Name <- definition;'");
        auto form = TypeForm::Newtype;
        std::unique_ptr<Type> rhs = nullptr;
        std::vector<Variant> variants;
        std::vector<std::unique_ptr<FnDecl>> members;
        // an interface: a pack of signatures, recognized by the leading let
        if (peek().kind == TokenKind::LBracket && peekAt(1).kind == TokenKind::KwLet) {
            form = TypeForm::Interface;
            advance();
            while (peek().kind == TokenKind::KwLet) {
                auto let = advance();
                members.push_back(parseFnDecl(let, true));
                if (peek().kind != TokenKind::Comma) break;
                advance();
            }
            expect(TokenKind::RBracket, "']' after the interface's signatures in 'type Name <- [let ..., let ...];'");
        } else {
            rhs = parseType();
            // a sum or an enum: more than one alternative
            if (peek().kind == TokenKind::Bar) {
                variants.push_back(toVariant(std::move(rhs)));
                while (peek().kind == TokenKind::Bar) {
                    advance();
                    variants.push_back(toVariant(parseType(false)));
                }
                bool payloads = std::any_of(variants.begin(), variants.end(), [](const Variant& v) { return !v.payload.empty(); });
                form = payloads ? TypeForm::Sum : TypeForm::Enum;
            } else if (rhs->kind == TypeKind::Record) {
                form = TypeForm::Product;
            } else if (rhs->kind == TypeKind::Refined) {
                bool record = static_cast<RefinedType&>(*rhs).inner->kind == TypeKind::Record;
                form = record ? TypeForm::Product : TypeForm::Refinement;
            }
        }
        expectSemi(nullptr);
        return std::make_unique<TypeDecl>(name, std::move(generics), form, std::move(rhs), std::move(variants), std::move(members), Span{start.loc, prevEnd()});
    }

    // parses [name, name[field, field], ...] for export and import lists
    std::vector<ModuleItem> parseModuleItems() {
        std::vector<ModuleItem> items;
        expect(TokenKind::LBracket, "'[' before the items in 'export [items];' or 'import [items] <- module;'");
        while (peek().kind != TokenKind::RBracket && peek().kind != TokenKind::Eof) {
            auto name = advance();
            if (!isFnName(name) && name.kind != TokenKind::TypeName) expected("a name to export or import", name);
            std::vector<Token> fields;
            if (peek().kind == TokenKind::LBracket) {
                advance();
                while (peek().kind == TokenKind::Identifier) {
                    fields.push_back(advance());
                    if (peek().kind != TokenKind::Comma) break;
                    advance();
                }
                expect(TokenKind::RBracket, "']' after the fields in 'Name[field, field]'");
            }
            items.push_back(ModuleItem{name, std::move(fields)});
            if (peek().kind != TokenKind::Comma) break;
            advance();
        }
        expect(TokenKind::RBracket, "']' after the items in '[Name, Name[field]]'");
        return items;
    }

    // parses export [items];
    std::unique_ptr<Decl> parseExport() {
        auto start = advance();
        auto items = parseModuleItems();
        expectSemi(nullptr);
        return std::make_unique<ExportDecl>(std::move(items), Span{start.loc, prevEnd()});
    }

    // parses import [items] <- dotted.path;
    std::unique_ptr<Decl> parseImport() {
        auto start = advance();
        auto items = parseModuleItems();
        expect(TokenKind::LArrow, "'<-' before the module in 'import [items] <- module.path;'");
        std::vector<Token> path{expect(TokenKind::Identifier, "a module name in 'import [items] <- module.path;'")};
        while (peek().kind == TokenKind::Dot) {
            advance();
            path.push_back(expect(TokenKind::Identifier, "a module name after '.' in 'module.path'"));
        }
        expectSemi(nullptr);
        return std::make_unique<ImportDecl>(std::move(items), std::move(path), Span{start.loc, prevEnd()});
    }

    // wraps a declaration so it can sit wherever a statement can
    std::unique_ptr<Stmt> declStmt(std::unique_ptr<Decl> decl) {
        auto span = decl->span;
        return std::make_unique<DeclStmt>(std::move(decl), span);
    }

    // parses @name(args) and the statement it applies to, or ; when it applies to the whole file
    std::unique_ptr<Stmt> parseAttr() {
        auto start = advance();
        auto name = expect(TokenKind::Identifier, "an attribute name after '@' in '@name(args)'");
        std::vector<std::unique_ptr<Expr>> args;
        if (peek().kind == TokenKind::LParen) {
            advance();
            args = parseArgList();
        }
        std::unique_ptr<Stmt> target = nullptr;
        if (peek().kind == TokenKind::Semi) {
            advance();
        } else {
            target = parseStmt();
        }
        return std::make_unique<AttrStmt>(name, std::move(args), std::move(target), Span{start.loc, prevEnd()});
    }

public:
    Parser(const std::vector<Token>& t, DiagnosticEngine& e) : tokens(t), engine(e), cursor(0), arrowStops(false) {}

    // checks if we've reached the EOF token
    bool atEnd() {
        return tokens[cursor].kind == TokenKind::Eof;
    }

    // the entry point for the parser, a program is a series of statements, most of them declarations
    std::vector<std::unique_ptr<Stmt>> parseProgram() {
        return parseStmts(TokenKind::Eof);
    }

    // parses a type: modes and qualifiers in front, then a handle, a tuple or record, or a named type, then an optional refinement, where refinable is off for types in value position, since a { can't follow there
    std::unique_ptr<Type> parseType(bool refinable = true) {
        auto start = peek().loc;
        std::unique_ptr<Type> type;
        switch (peek().kind) {
            // modes, with the type left to inference if none follows
            case TokenKind::KwUnscoped:
            case TokenKind::KwPeek:
            case TokenKind::KwWit: {
                auto mode = advance();
                auto inner = startsType(peek()) ? parseType(false) : nullptr;
                type = std::make_unique<ModeType>(mode, std::move(inner), Span{start, prevEnd()});
                break;
            }

            // mut before a handle rebinds it, mut before anything else is a mutable value
            case TokenKind::KwMut: {
                auto qual = advance();
                auto inner = startsType(peek()) ? parseType(false) : nullptr;
                auto pos = inner && inner->kind == TypeKind::Handle ? QualPosition::Handle : QualPosition::Value;
                type = std::make_unique<QualifiedType>(qual, pos, std::move(inner), Span{start, prevEnd()});
                break;
            }

            // handles, where a mut right after the handle applies to the elements
            case TokenKind::KwPtr:
            case TokenKind::KwArr: {
                auto handle = advance();
                std::optional<GenericArg> arg = std::nullopt;
                if (peek().kind == TokenKind::LBracket) {
                    advance();
                    arg = parseGenericArg();
                    expect(TokenKind::RBracket, "']' after the handle's size or allocator in 'arr[size] T'");
                }
                auto inner = parseType(false);
                if (inner->kind == TypeKind::Qualified) static_cast<QualifiedType&>(*inner).position = QualPosition::Elements;
                type = std::make_unique<HandleType>(handle, std::move(arg), std::move(inner), Span{start, prevEnd()});
                break;
            }

            // records if the first element is name ::, and tuples otherwise
            case TokenKind::LBracket: {
                advance();
                if (peek().kind == TokenKind::Identifier && peekAt(1).kind == TokenKind::ColonColon) {
                    std::vector<Param> fields;
                    while (true) {
                        auto name = expect(TokenKind::Identifier, "a field name in '[name :: Type]'");
                        expect(TokenKind::ColonColon, "'::' after the field name in '[name :: Type]'");
                        fields.push_back(Param{name, parseType()});
                        if (peek().kind != TokenKind::Comma) break;
                        advance();
                    }
                    expect(TokenKind::RBracket, "']' after the record's fields in '[name :: Type, name :: Type]'");
                    type = std::make_unique<RecordType>(std::move(fields), Span{start, prevEnd()});
                } else {
                    std::vector<std::unique_ptr<Type>> elems;
                    while (peek().kind != TokenKind::RBracket && peek().kind != TokenKind::Eof) {
                        elems.push_back(parseType());
                        if (peek().kind != TokenKind::Comma) break;
                        advance();
                    }
                    expect(TokenKind::RBracket, "']' after the tuple's types in '[A, B]'");
                    type = std::make_unique<TupleType>(std::move(elems), Span{start, prevEnd()});
                }
                break;
            }

            case TokenKind::TypeName:
            case TokenKind::Identifier:
                type = parseNamedType();
                break;

            default: {
                // what follows a type, a <- or a comma or a closing bracket, is left for the code around it
                auto tok = peek();
                expected("a type", tok);
                if (!isBoundary(tok.kind) && tok.kind != TokenKind::LArrow && tok.kind != TokenKind::Comma) advance();
                return std::make_unique<ErrorType>(tokSpan(tok));
            }
        }
        if (refinable && peek().kind == TokenKind::LBrace) type = parseRefinement(std::move(type));
        return type;
    }

    // parses a pattern, written like an expression and then reinterpreted, where a top-level <- ends it
    std::unique_ptr<Pattern> parsePattern() {
        return toPattern(withArrowStops(true, [&] { return parseExpr(0); }));
    }

    // parses statements up to end or the end of the file, where a stray closing bracket, which recovery leaves in place, is skipped once it's been reported
    std::vector<std::unique_ptr<Stmt>> parseStmts(TokenKind end) {
        std::vector<std::unique_ptr<Stmt>> stmts;
        while (peek().kind != end && !atEnd()) {
            size_t before = cursor;
            stmts.push_back(parseStmt());
            if (cursor == before) advance();
        }
        return stmts;
    }

    // parses the sequence of statements in between {}
    std::unique_ptr<BlockExpr> parseBlock(std::optional<Token> name) {
        auto start = name ? name->loc : peek().loc;
        expect(TokenKind::LBrace, "'{' to start a block in '{ statements }'");
        auto stmts = parseStmts(TokenKind::RBrace);
        expect(TokenKind::RBrace, "'}' after the statements in '{ statements }'");
        return std::make_unique<BlockExpr>(name, std::move(stmts), Span{start, prevEnd()});
    }

    // wrapper function to parse any kind of statement, where an expression comes first and decides between assignment, yield, and expression statement
    std::unique_ptr<Stmt> parseStmt() {
        switch (peek().kind) {
            case TokenKind::KwLet:
                return parseLet();
            case TokenKind::KwWit:
                return parseWit();
            case TokenKind::KwAssert:
                return parseAssert();
            case TokenKind::KwType:
                return declStmt(parseTypeDecl());
            case TokenKind::KwExport:
                return declStmt(parseExport());
            case TokenKind::KwImport:
                return declStmt(parseImport());
            case TokenKind::At:
                return parseAttr();
            // a bare yield has no value
            case TokenKind::Yield:
                return parseYield(nullptr);
            // scopes at the start of a statement
            case TokenKind::LBrace:
                return scopeStmt(parseBlock(std::nullopt));
            case TokenKind::KwMatch: {
                auto tok = advance();
                return scopeStmt(parseMatch(tok));
            }
            case TokenKind::KwRepeat: {
                auto tok = advance();
                return scopeStmt(parseRepeat(tok));
            }
            case TokenKind::KwChunk: {
                auto tok = advance();
                return scopeStmt(parseChunk(tok));
            }
            default:
                break;
        }
        // an identifier followed by { at the start of a statement can only be a named block
        if (peek().kind == TokenKind::Identifier && peekAt(1).kind == TokenKind::LBrace) {
            auto name = advance();
            return scopeStmt(parseBlock(name));
        }
        auto expr = withArrowStops(true, [&] { return parseExpr(0); });
        auto start = expr->span.start;
        // an assignment or a compound assignment
        if (peek().kind == TokenKind::LArrow || peek().kind == TokenKind::CompoundAssign) {
            auto arrow = advance();
            std::optional<Token> op = std::nullopt;
            if (arrow.kind == TokenKind::CompoundAssign) op = arrow;
            if (!isPlace(*expr)) engine.printError("cherimoya requires the left side of an assignment to be a place, please assign to a name, field, index, dereference, or _", expr->span);
            auto value = parseNested();
            expectSemi(value.get());
            return std::make_unique<AssignStmt>(std::move(expr), op, std::move(value), Span{start, prevEnd()});
        }
        // a yield with a value
        if (peek().kind == TokenKind::Yield) return parseYield(std::move(expr));
        // an expression statement
        expectSemi(expr.get());
        return std::make_unique<ExprStmt>(std::move(expr), Span{start, prevEnd()});
    }

    // repeatedly consumes tokens, recursing with higher and higher rbp until the next token doesn't have a strong enough lbp, at which point the highest level returns and its caller picks up where it left off, where parent is the operator whose right side this is, so the mixing and pipe rules can see across the recursion
    std::unique_ptr<Expr> parseExpr(int rbp = 0, const OpInfo* parent = nullptr) {
        auto tok = advance();
        std::unique_ptr<Expr> left;

        // the token that starts an expression, its null denotation
        switch (tok.kind) {
            // a malformed token, which the lexer already reported
            case TokenKind::Error:
                left = std::make_unique<ErrorExpr>(tokSpan(tok));
                break;
            case TokenKind::IntLiteral:
                left = std::make_unique<IntLiteralExpr>(tok.text, tokSpan(tok));
                break;
            case TokenKind::FloatLiteral:
                left = std::make_unique<FloatLiteralExpr>(tok.text, tokSpan(tok));
                break;
            case TokenKind::KwTrue:
                left = std::make_unique<BoolLiteralExpr>(true, tokSpan(tok));
                break;
            case TokenKind::KwFalse:
                left = std::make_unique<BoolLiteralExpr>(false, tokSpan(tok));
                break;
            case TokenKind::StringLiteral:
                left = std::make_unique<StringLiteralExpr>(tok.text, tokSpan(tok));
                break;
            case TokenKind::CharLiteral:
                left = std::make_unique<CharLiteralExpr>(tok.text, tokSpan(tok));
                break;

            // a name, where if ( gets pointed at match since there is no if
            case TokenKind::Identifier:
                if (tok.text == "if" && peek().kind == TokenKind::LParen) {
                    engine.printError("cherimoya uses 'match' for conditional flow instead of 'if' as follows: match { (condition) -> { ... } _ -> { ... } }", tokSpan(tok));
                    // skips the condition and the block after it so they don't report errors of their own
                    size_t close = findClose(cursor);
                    if (tokens[close].kind == TokenKind::RParen) cursor = close + 1;
                    if (peek().kind == TokenKind::LBrace) {
                        close = findClose(cursor);
                        if (tokens[close].kind == TokenKind::RBrace) cursor = close + 1;
                    }
                    left = std::make_unique<ErrorExpr>(Span{tok.loc, prevEnd()});
                    break;
                }
                left = std::make_unique<IdentifierExpr>(tok.text, tokSpan(tok));
                break;

            // the place intrinsics are keywords, but they're called like functions, so they become the callee's name
            case TokenKind::KwTake:
            case TokenKind::KwSwap:
            case TokenKind::KwReplace:
                left = std::make_unique<IdentifierExpr>(tok.text, tokSpan(tok));
                break;

            case TokenKind::Underscore:
                left = std::make_unique<WildcardExpr>(tokSpan(tok));
                break;

            // a closure, fold, grouping, or annotation
            case TokenKind::LParen:
                left = parseParen(tok);
                break;

            // a pack, range, or comprehension
            case TokenKind::LBracket:
                left = parsePack(tok);
                break;

            // a type in value position, which gets called to convert or allocate: i32(x), arr mut T(n), ptr T(v)
            case TokenKind::TypeName:
            case TokenKind::KwArr:
            case TokenKind::KwPtr:
            case TokenKind::KwPeek:
            case TokenKind::KwMut:
            case TokenKind::KwUnscoped: {
                cursor--;
                auto type = parseType(false);
                auto span = type->span;
                left = std::make_unique<TypeExprExpr>(std::move(type), span);
                break;
            }

            // scopes
            case TokenKind::LBrace:
                cursor--;
                left = parseBlock(std::nullopt);
                break;
            case TokenKind::KwMatch:
                left = parseMatch(tok);
                break;
            case TokenKind::KwRepeat:
                left = parseRepeat(tok);
                break;
            case TokenKind::KwChunk:
                left = parseChunk(tok);
                break;

            // an entry value, at the same level as the prefix operators, level 3
            case TokenKind::KwOld: {
                auto operand = parseExpr(bindingPower(3));
                auto span = Span{tok.loc, operand->span.end};
                left = std::make_unique<OldExpr>(std::move(operand), span);
                break;
            }

            default:
                // a prefix operator reads its level from the table, which is level 3
                if (tok.op != nullptr && tok.op->prefixLevel > 0) {
                    auto operand = parseExpr(bindingPower(tok.op->prefixLevel));
                    auto span = Span{tok.loc, operand->span.end};
                    left = std::make_unique<UnaryExpr>(tok, std::move(operand), span);
                    break;
                }
                expected("an expression", tok);
                left = std::make_unique<ErrorExpr>(tokSpan(tok));
                // a closing bracket or semicolon is left for the code around this to find
                if (isBoundary(tok.kind) && tok.kind != TokenKind::Eof) cursor--;
                break;
        }

        // the loop over infix operators, their left denotations, where prev is the last binary operator at this depth and pipe is the last pipe
        const OpInfo* prev = parent;
        const OpInfo* pipe = parent != nullptr && parent->category == OpCategory::Pipe ? parent : nullptr;
        while (true) {
            auto op = peek();
            auto lbp = leftBindingPower(op);
            if (lbp <= rbp) break;
            advance();
            auto start = left->span.start;

            switch (op.kind) {
                // a call, or a capture closure when a pack is followed by (params) ->
                case TokenKind::LParen: {
                    size_t close = findClose(cursor - 1);
                    bool arrow = tokens[close].kind == TokenKind::RParen && at(close + 1).kind == TokenKind::RArrow;
                    if (left->kind == ExprKind::Pack && arrow) {
                        std::vector<std::unique_ptr<Expr>> captures;
                        for (auto& e : static_cast<PackExpr&>(*left).elems) {
                            if (e.name) engine.printError("cherimoya requires captures to be unnamed, please remove '" + std::string(e.name->text) + " <-'", tokSpan(*e.name));
                            captures.push_back(std::move(e.value));
                        }
                        left = parseClosure(start, std::move(captures));
                        break;
                    }
                    auto args = parseArgList();
                    left = std::make_unique<CallExpr>(std::move(left), std::move(args), Span{start, prevEnd()});
                    break;
                }

                // an index, slice, dereference, or generic arguments
                case TokenKind::LBracket: {
                    auto contents = parsePack(op);
                    std::vector<std::unique_ptr<Expr>> args;
                    if (contents->kind == ExprKind::Pack) {
                        for (auto& e : static_cast<PackExpr&>(*contents).elems) {
                            if (e.name) engine.printError("cherimoya requires index arguments to be unnamed, please remove '" + std::string(e.name->text) + " <-'", tokSpan(*e.name));
                            args.push_back(std::move(e.value));
                        }
                    } else {
                        args.push_back(std::move(contents));
                    }
                    left = std::make_unique<IndexExpr>(std::move(left), std::move(args), Span{start, prevEnd()});
                    break;
                }

                case TokenKind::Dot: {
                    auto name = expect(TokenKind::Identifier, "a field name after '.' in 'value.field'");
                    left = std::make_unique<MemberExpr>(std::move(left), name.text, Span{start, tokSpan(name).end});
                    break;
                }

                // construction, which always builds from a pack
                case TokenKind::LArrow: {
                    std::unique_ptr<Expr> pack;
                    if (peek().kind == TokenKind::LBracket) {
                        auto bracket = advance();
                        pack = parsePack(bracket);
                    } else {
                        engine.printError("cherimoya requires construction to build from a pack, please write 'T <- [...]'", tokSpan(op));
                        pack = parseExpr(bindingPower(1));
                    }
                    left = std::make_unique<ConstructExpr>(std::move(left), std::move(pack), Span{start, prevEnd()});
                    break;
                }

                // a binary operator, reading its level and associativity from the table
                default: {
                    checkMixing(prev, op);
                    prev = op.op;
                    int nextBP = op.op->assoc == Assoc::Right ? lbp - 1 : lbp;

                    // pipes, which can't be mixed at the same depth
                    if (op.op->category == OpCategory::Pipe) {
                        if (pipe != nullptr && pipe->kind != op.kind) {
                            engine.printError("cherimoya requires parentheses to mix '|>' and '<|', please write '(x |> f) <| y' or 'x |> (f <| y)'", tokSpan(op));
                        }
                        pipe = op.op;
                        auto rhs = parseExpr(nextBP, op.op);
                        left = std::make_unique<PipeExpr>(op, std::move(left), std::move(rhs), Span{start, prevEnd()});
                        break;
                    }

                    // chained comparisons collect every built-in comparison at this depth into one node
                    if (op.op->assoc == Assoc::Chain) {
                        std::vector<std::unique_ptr<Expr>> operands;
                        std::vector<Token> ops{op};
                        operands.push_back(std::move(left));
                        operands.push_back(parseExpr(nextBP, op.op));
                        while (peek().op != nullptr && peek().op->assoc == Assoc::Chain) {
                            ops.push_back(advance());
                            operands.push_back(parseExpr(nextBP, op.op));
                        }
                        left = std::make_unique<ChainExpr>(std::move(operands), std::move(ops), Span{start, prevEnd()});
                        break;
                    }

                    auto rhs = parseExpr(nextBP, op.op);
                    auto span = Span{start, rhs->span.end};
                    if (op.op->category == OpCategory::Extra) {
                        left = std::make_unique<ExtraBinaryExpr>(op, std::move(left), std::move(rhs), span);
                    } else {
                        left = std::make_unique<BinaryExpr>(op, std::move(left), std::move(rhs), span);
                    }
                    break;
                }
            }
        }
        return left;
    }
};
