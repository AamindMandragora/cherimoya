#pragma once

#include "lexer.hpp"
#include "ast.hpp"

// a parser holds references to a vector of tokens and the diagnostic engine, as well as a cursor pointer into the tokens vector
class Parser {
    std::vector<Token>& tokens;
    DiagnosticEngine& engine;
    size_t cursor;
    // set while parsing the top of a comprehension clause, where <- starts a membership clause instead of a construction
    bool arrowStops;

    // returns the token being pointed at by the cursor
    Token& peek() {
        return tokens[cursor];
    }

    // returns the token n past the cursor, or EOF if that's past the end
    Token& peekAt(size_t n) {
        return tokens[std::min(cursor + n, tokens.size() - 1)];
    }

    // returns the token being pointed at by the cursor and increments the latter, never moving past EOF
    Token& advance() {
        auto& tok = tokens[cursor];
        if (tok.kind != TokenKind::Eof) cursor++;
        return tok;
    }

    // advances and checks if the returned token is what we expected, where what names the token and where it belongs
    Token expect(TokenKind kind, std::string_view what) {
        auto tok = advance();
        if (tok.kind != kind) {
            expected(what, tok);
        }
        return tok;
    }

    // reports that something else belonged where tok is
    void expected(std::string_view what, const Token& tok) {
        engine.print_error("cherimoya expected " + std::string(what) + ", got " + describe(tok) + " instead", tokSpan(tok));
    }

    // names a token for a diagnostic: its text, or the end of the file
    std::string describe(const Token& tok) {
        if (tok.kind == TokenKind::Eof) return "the end of the file";
        return "'" + std::string(tok.text) + "'";
    }

    // checks if we've reached the EOF token
    bool atEnd() {
        return tokens[cursor].kind == TokenKind::Eof;
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
            // Level 1: call, index, member
            case TokenKind::LParen:
            case TokenKind::LBracket:
            case TokenKind::Dot:
                return bindingPower(1);

            // Level 1: construction, unless the <- starts a membership clause
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
        engine.print_error("cherimoya requires parentheses to mix '" + a + "' and '" + b + "', please write '(x " + a + " y) " + b + " z' or 'x " + a + " (y " + b + " z)'", tokSpan(op));
    }

    // finds the token that closes the bracket at index open, or EOF if it's never closed
    size_t findClose(size_t open) {
        int depth = 0;
        for (size_t i = open; i < tokens.size(); i++) {
            switch (tokens[i].kind) {
                case TokenKind::LParen:
                case TokenKind::LBracket:
                case TokenKind::LBrace:
                    depth++;
                    break;
                case TokenKind::RParen:
                case TokenKind::RBracket:
                case TokenKind::RBrace:
                    if (--depth == 0) return i;
                    break;
                case TokenKind::Eof:
                    return i;
                default:
                    break;
            }
        }
        return tokens.size() - 1;
    }

    // checks for a comma between open and close that isn't nested inside another bracket
    bool hasTopLevelComma(size_t open, size_t close) {
        int depth = 0;
        for (size_t i = open + 1; i < close; i++) {
            switch (tokens[i].kind) {
                case TokenKind::LParen:
                case TokenKind::LBracket:
                case TokenKind::LBrace:
                    depth++;
                    break;
                case TokenKind::RParen:
                case TokenKind::RBracket:
                case TokenKind::RBrace:
                    depth--;
                    break;
                case TokenKind::Comma:
                    if (depth == 0) return true;
                    break;
                default:
                    break;
            }
        }
        return false;
    }

    // parses an expression inside brackets or parentheses, where the outside's comprehension state doesn't apply
    std::unique_ptr<Expr> parseNested() {
        bool saved = arrowStops;
        arrowStops = false;
        auto expr = parseExpr(0);
        arrowStops = saved;
        return expr;
    }

    // parses the arguments passed into some call expression
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
        auto after = tokens[std::min(close + 1, tokens.size() - 1)].kind;
        bool closed = tokens[close].kind == TokenKind::RParen;
        // skips the whole parenthesized list after reporting an error about it
        auto skip = [&](std::string_view error) -> std::unique_ptr<Expr> {
            auto span = Span{open.loc, tokens[close].loc + tokens[close].text.length()};
            engine.print_error(error, span);
            cursor = closed ? close + 1 : close;
            return std::make_unique<ErrorExpr>(span);
        };
        if (closed && after == TokenKind::RArrow) {
            return skip("cherimoya doesn't parse closures yet");
        }
        if (close == openIdx + 1) {
            return skip("cherimoya uses '[]' for an empty pack instead of '()' as follows: let empty <- [];");
        }
        if (close == openIdx + 2 && tokens[openIdx + 1].op != nullptr) {
            std::string op(tokens[openIdx + 1].text);
            return skip("cherimoya requires a bare operator to be a fold's reducer, please write '(" + op + ", seed)[xs]' since '(" + op + ")' on its own is reserved");
        }
        if (hasTopLevelComma(openIdx, close)) {
            if (closed && after == TokenKind::LBracket) return parseFold(open, close);
            return skip("cherimoya uses packs for tuples instead of parentheses as follows: [a, b]");
        }
        // Grouped expression
        auto inner = parseNested();
        auto end = expect(TokenKind::RParen, "')' to close the parenthesized expression");
        // Widen span to include parens
        inner->span = {open.loc, end.loc + end.text.length()};
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
            engine.print_error("cherimoya requires a fold to take exactly a reducer and a seed, please write '(reducer, seed)[xs]'", tokSpan(peek()));
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
        bool ellipsis;
        Span span;
    };

    // parses one pack element, catching the arrows that don't belong in a pack
    RawElem parseElem() {
        RawElem elem{std::nullopt, nullptr, std::nullopt, false, tokSpan(peek())};
        auto start = peek().loc;
        if (peek().kind == TokenKind::Ellipsis) {
            advance();
            elem.ellipsis = true;
        } else if (peek().kind == TokenKind::Gt || peek().kind == TokenKind::Lt) {
            elem.marker = advance();
            elem.value = parseNested();
        } else if (peek().kind == TokenKind::LArrow) {
            engine.print_error("cherimoya uses '< -' for an exclusivity marker on a negative bound instead of '<-' as follows: [5, ..., < -1]", tokSpan(peek()));
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
            engine.print_error("cherimoya uses a pack of pairs for a map instead of '->' as follows: [[key, value], [key, value]]", tokSpan(peek()));
            advance();
            parseNested();
        }
        elem.span = {start, prevEnd()};
        return elem;
    }

    // parses the clauses after the colon of a comprehension, where a top-level <- starts a membership clause
    std::vector<ComprClause> parseClauses() {
        std::vector<ComprClause> clauses;
        bool saved = arrowStops;
        while (true) {
            arrowStops = true;
            auto expr = parseExpr(bindingPower(12));
            arrowStops = false;
            if (peek().kind == TokenKind::LArrow) {
                advance();
                auto source = parseExpr(bindingPower(12));
                clauses.push_back({std::move(expr), std::move(source)});
            } else {
                clauses.push_back({nullptr, std::move(expr)});
            }
            if (peek().kind != TokenKind::AmpAmp) break;
            advance();
        }
        arrowStops = saved;
        if (peek().kind == TokenKind::PipePipe) {
            engine.print_error("cherimoya requires a comprehension's clauses to be joined with '&&', please wrap the '||' in parentheses, as in [x : x <- xs && (a || b)]", tokSpan(peek()));
        }
        return clauses;
    }

    // parses everything after a [ into a pack, a range (any ...), or a comprehension (a : after the first element)
    std::unique_ptr<Expr> parsePack(const Token& open) {
        size_t close = findClose(cursor - 1);
        std::vector<RawElem> elems;
        if (peek().kind != TokenKind::RBracket) {
            elems.push_back(parseElem());
            // Comprehension
            if (peek().kind == TokenKind::Colon) {
                advance();
                auto& head = elems[0];
                if (head.name || head.marker || head.ellipsis) {
                    engine.print_error("cherimoya requires a comprehension's head to be a plain expression, please remove the name, exclusivity marker, or '...' before ':'", head.span);
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

        // Range: exactly one ..., at most two values before it and one after, and only endpoints can have exclusivity markers
        size_t ellipses = 0;
        size_t at = 0;
        for (size_t i = 0; i < elems.size(); i++) {
            if (elems[i].ellipsis) { ellipses++; at = i; }
        }
        if (ellipses > 0) {
            if (ellipses > 1) {
                engine.print_error("cherimoya requires a range to have exactly one '...', please write '[first, ..., last]'", span);
            } else if (at > 2 || elems.size() - at - 1 > 1) {
                engine.print_error("cherimoya requires a range to have at most two values before '...' and one after, please write '[first, second, ..., last]'", span);
            } else if (elems.size() == 1) {
                engine.print_error("cherimoya requires a range to have at least one bound, please write '[first, ...]' or '[..., last]'", span);
            }
            std::vector<RangeElem> range;
            for (size_t i = 0; i < elems.size(); i++) {
                auto& e = elems[i];
                if (e.name) engine.print_error("cherimoya requires range elements to be unnamed, please remove '" + std::string(e.name->text) + " <-'", e.span);
                if (e.marker && i != 0 && i != elems.size() - 1) {
                    engine.print_error("cherimoya requires exclusivity markers to be on a range's endpoints, please move it to the first or last value", e.span);
                }
                range.push_back({std::move(e.value), e.marker});
            }
            return std::make_unique<RangeExpr>(std::move(range), span);
        }

        // Pack
        std::vector<PackElem> pack;
        for (auto& e : elems) {
            if (e.marker) engine.print_error("cherimoya requires exclusivity markers to be inside a range, please add '...' or remove the marker", e.span);
            pack.push_back({e.name, std::move(e.value)});
        }
        return std::make_unique<PackExpr>(std::move(pack), span);
    }

public:
    Parser(std::vector<Token>& t, DiagnosticEngine& e) : tokens(t), engine(e), cursor(0), arrowStops(false) {}

    // repeatedly consumes tokens, recursing with higher and higher rbp until the next token doesn't have a strong enough lbp, at which point the highest level returns and its caller picks up where it left off
    // parent is the operator whose right side this is, so the mixing and pipe rules can see across the recursion
    std::unique_ptr<Expr> parseExpr(int rbp = 0, const OpInfo* parent = nullptr) {
        auto tok = advance();
        std::unique_ptr<Expr> left;

        // ---- NUD ----
        switch (tok.kind) {
            // Literals
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

            // Identifier, where if ( gets pointed at match since there is no if
            case TokenKind::Identifier:
                if (tok.text == "if" && peek().kind == TokenKind::LParen) {
                    engine.print_error("cherimoya uses 'match' for conditional flow instead of 'if' as follows: match { (condition) -> { ... } _ -> { ... } }", tokSpan(tok));
                    left = std::make_unique<ErrorExpr>(tokSpan(tok));
                    break;
                }
                left = std::make_unique<IdentifierExpr>(tok.text, tokSpan(tok));
                break;

            // Wildcard
            case TokenKind::Underscore:
                left = std::make_unique<WildcardExpr>(tokSpan(tok));
                break;

            // Closure, fold, grouping, or annotation
            case TokenKind::LParen:
                left = parseParen(tok);
                break;

            // Pack, range, or comprehension
            case TokenKind::LBracket:
                left = parsePack(tok);
                break;

            // Entry value, at the same level as the prefix operators (level 3)
            case TokenKind::KwOld: {
                auto operand = parseExpr(bindingPower(3));
                auto span = Span{tok.loc, operand->span.end};
                left = std::make_unique<OldExpr>(std::move(operand), span);
                break;
            }

            default:
                // Prefix operators read their level from the table (level 3)
                if (tok.op != nullptr && tok.op->prefix_level > 0) {
                    auto operand = parseExpr(bindingPower(tok.op->prefix_level));
                    auto span = Span{tok.loc, operand->span.end};
                    left = std::make_unique<UnaryExpr>(tok, std::move(operand), span);
                    break;
                }
                expected("an expression", tok);
                left = std::make_unique<ErrorExpr>(tokSpan(tok));
                break;
        }

        // ---- LED loop ----
        // prev is the last binary operator at this depth, pipe is the last pipe
        const OpInfo* prev = parent;
        const OpInfo* pipe = parent != nullptr && parent->category == OpCategory::Pipe ? parent : nullptr;
        while (true) {
            auto op = peek();
            auto lbp = leftBindingPower(op);
            if (lbp <= rbp) break;
            advance();
            auto start = left->span.start;

            switch (op.kind) {
                // Call
                case TokenKind::LParen: {
                    auto args = parseArgList();
                    left = std::make_unique<CallExpr>(std::move(left), std::move(args), Span{start, prevEnd()});
                    break;
                }

                // Index, slice, dereference, or generic arguments
                case TokenKind::LBracket: {
                    auto contents = parsePack(op);
                    std::vector<std::unique_ptr<Expr>> args;
                    if (contents->kind == ExprKind::Pack) {
                        for (auto& e : static_cast<PackExpr&>(*contents).elems) {
                            if (e.name) engine.print_error("cherimoya requires index arguments to be unnamed, please remove '" + std::string(e.name->text) + " <-'", tokSpan(*e.name));
                            args.push_back(std::move(e.value));
                        }
                    } else {
                        args.push_back(std::move(contents));
                    }
                    left = std::make_unique<IndexExpr>(std::move(left), std::move(args), Span{start, prevEnd()});
                    break;
                }

                // Member access
                case TokenKind::Dot: {
                    auto name = expect(TokenKind::Identifier, "a field name after '.' in 'value.field'");
                    left = std::make_unique<MemberExpr>(std::move(left), name.text, Span{start, name.loc + name.text.length()});
                    break;
                }

                // Construction, which always builds from a pack
                case TokenKind::LArrow: {
                    std::unique_ptr<Expr> pack;
                    if (peek().kind == TokenKind::LBracket) {
                        auto bracket = advance();
                        pack = parsePack(bracket);
                    } else {
                        engine.print_error("cherimoya requires construction to build from a pack, please write 'T <- [...]'", tokSpan(op));
                        pack = parseExpr(bindingPower(1));
                    }
                    left = std::make_unique<ConstructExpr>(std::move(left), std::move(pack), Span{start, prevEnd()});
                    break;
                }

                // Binary infix, reading level and associativity from the table
                default: {
                    checkMixing(prev, op);
                    prev = op.op;
                    int nextBP = op.op->assoc == Assoc::Right ? lbp - 1 : lbp;

                    // Pipes, which can't be mixed at the same depth
                    if (op.op->category == OpCategory::Pipe) {
                        if (pipe != nullptr && pipe->kind != op.kind) {
                            engine.print_error("cherimoya requires parentheses to mix '|>' and '<|', please write '(x |> f) <| y' or 'x |> (f <| y)'", tokSpan(op));
                        }
                        pipe = op.op;
                        auto rhs = parseExpr(nextBP, op.op);
                        left = std::make_unique<PipeExpr>(op, std::move(left), std::move(rhs), Span{start, prevEnd()});
                        break;
                    }

                    // Chained comparisons collect every built-in comparison at this depth into one node
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
                        left = std::make_unique<ExtraBinOpExpr>(op, std::move(left), std::move(rhs), span);
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
