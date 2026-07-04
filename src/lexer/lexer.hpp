#pragma once

#include <vector>
#include "diagnostics.hpp"
#include "keywords.hpp"
#include "operators.hpp"

// lexer holds source manager, diagnostic engine, a view of the code, a vector of tokens, and the current position
class Lexer {
    SourceManager& manager;
    DiagnosticEngine& engine;
    std::string_view code;
    std::vector<Token> tokens;
    SourceLoc pos;

    // checks whether the code at the current position matches the string
    bool match(std::string_view s) {
        return code.substr(pos, s.size()) == s;
    }

    // adds a token of specified kind and length to the vector
    void emit(TokenKind kind, size_t length) {
        auto text = code.substr(pos, length);
        tokens.push_back({kind, text, pos});
        pos += length;
    }

    // names the character at p for a diagnostic: the character, whitespace, or the end of the file
    std::string describe(SourceLoc p) {
        if (p >= code.size()) return "the end of the file";
        if (std::isspace(static_cast<unsigned char>(code[p]))) return "whitespace";
        return "'" + std::string(1, code[p]) + "'";
    }

    // loops while the current pos is pointing to whitespace or a comment
    void skip_whitespace_and_comments() {
        while (true) {
            while (pos < code.size() && std::isspace(static_cast<unsigned char>(code[pos]))) pos++;
            if (pos < code.size() && code[pos] == '#') {
                while (pos < code.size() && code[pos] != '\n') pos++;
                continue;
            }
            break;
        }
    }

    // reads the word fully, then checks if it's a lone underscore or a keyword and lexes accordingly
    void scan_ident_or_keyword(SourceLoc start) {
        while (pos < code.size() && (std::isalnum(static_cast<unsigned char>(code[pos])) || code[pos] == '_')) pos++;
        auto word = code.substr(start, pos - start);
        auto kind = lookup_keyword(word);
        if (word == "_") {
            tokens.push_back({TokenKind::Underscore, word, start});
        } else if (kind != std::nullopt) {
            tokens.push_back({kind.value(), word, start});
        } else {
            tokens.push_back({TokenKind::Identifier, word, start});
        }
    }

    // scans number by checking for 0b/0o/0x prefix, then reads the succeeding string of valid digits, if it finds a decimal point lexes as a float otherwise lexes as an int
    void scan_number(SourceLoc start) {
        bool seen_digit = false;
        auto kind = TokenKind::IntLiteral;
        auto is_octal = [](char c) { return c >= '0' && c <= '7'; };
        if (code[pos] == '0' && pos + 1 < code.size() && (code[pos + 1] == 'b' || code[pos + 1] == 'B')) {
            pos += 2;
            while (pos < code.size() && (code[pos] == '0' || code[pos] == '1' || code[pos] == '_')) {
                if (code[pos] != '_') seen_digit = true;
                pos++;
            }
            if (pos < code.size() && code[pos] == '.' && pos + 1 < code.size() && (code[pos + 1] == '0' || code[pos + 1] == '1')) {
                kind = TokenKind::FloatLiteral; pos++;
                while (pos < code.size() && (code[pos] == '0' || code[pos] == '1' || code[pos] == '_')) pos++;
            }
            if (!seen_digit) {
                engine.print_error("cherimoya expected a binary digit (0 or 1) after '" + std::string(code.substr(start, 2)) + "', got " + describe(pos) + " instead", {start, pos});
                tokens.push_back({TokenKind::Error, code.substr(start, pos - start), start});
                return;
            }
        } else if (code[pos] == '0' && pos + 1 < code.size() && (code[pos + 1] == 'o' || code[pos + 1] == 'O')) {
            pos += 2;
            while (pos < code.size() && (is_octal(code[pos]) || code[pos] == '_')) {
                if (code[pos] != '_') seen_digit = true;
                pos++;
            }
            if (pos < code.size() && code[pos] == '.' && pos + 1 < code.size() && is_octal(code[pos + 1])) {
                kind = TokenKind::FloatLiteral; pos++;
                while (pos < code.size() && (is_octal(code[pos]) || code[pos] == '_')) pos++;
            }
            if (!seen_digit) {
                engine.print_error("cherimoya expected an octal digit (0 to 7) after '" + std::string(code.substr(start, 2)) + "', got " + describe(pos) + " instead", {start, pos});
                tokens.push_back({TokenKind::Error, code.substr(start, pos - start), start});
                return;
            }
        } else if (code[pos] == '0' && pos + 1 < code.size() && (code[pos + 1] == 'x' || code[pos + 1] == 'X')) {
            pos += 2;
            while (pos < code.size() && (std::isxdigit(static_cast<unsigned char>(code[pos])) || code[pos] == '_')) {
                if (code[pos] != '_') seen_digit = true;
                pos++;
            }
            if (pos < code.size() && code[pos] == '.' && pos + 1 < code.size() && std::isxdigit(static_cast<unsigned char>(code[pos + 1]))) {
                kind = TokenKind::FloatLiteral; pos++;
                while (pos < code.size() && (std::isxdigit(static_cast<unsigned char>(code[pos])) || code[pos] == '_')) pos++;
            }
            if (!seen_digit) {
                engine.print_error("cherimoya expected a hex digit (0 to 9 or a to f) after '" + std::string(code.substr(start, 2)) + "', got " + describe(pos) + " instead", {start, pos});
                tokens.push_back({TokenKind::Error, code.substr(start, pos - start), start});
                return;
            }
        } else {
            while (pos < code.size() && (std::isdigit(static_cast<unsigned char>(code[pos])) || code[pos] == '_')) {
                if (code[pos] != '_') seen_digit = true;
                pos++;
            }
            if (pos < code.size() && code[pos] == '.' && pos + 1 < code.size() && std::isdigit(static_cast<unsigned char>(code[pos + 1]))) {
                kind = TokenKind::FloatLiteral; pos++;
                while (pos < code.size() && (std::isdigit(static_cast<unsigned char>(code[pos])) || code[pos] == '_')) pos++;
            }
            if (!seen_digit) {
                engine.print_error("cherimoya expected a decimal digit, got " + describe(pos) + " instead", {start, pos});
                tokens.push_back({TokenKind::Error, code.substr(start, pos - start), start});
                return;
            }
        }
        tokens.push_back({kind, code.substr(start, pos - start), start});
    }

    // scans a string literal and checks if it's unterminated while ignoring \ escapes
    void scan_string(SourceLoc start) {
        pos++;
        while (pos < code.size() && code[pos] != '"') {
            if (code[pos] == '\\') {
                pos++;
                if (pos >= code.size()) break;
            }
            pos++;
        }
        if (pos >= code.size()) {
            engine.print_error("cherimoya expected '\"' to close the string literal, got the end of the file instead", {start, pos});
            tokens.push_back({TokenKind::Error, code.substr(start, pos - start), start});
            return;
        }
        pos++;
        auto text = code.substr(start, pos - start);
        tokens.push_back({TokenKind::StringLiteral, text, start});
    }

    // scans a char literal and checks if it's unterminated while ignoring \ escapes
    void scan_char(SourceLoc start) {
        pos++;
        while (pos < code.size() && code[pos] != '\'') {
            if (code[pos] == '\\') {
                pos++;
                if (pos >= code.size()) break;
            }
            pos++;
        }
        if (pos >= code.size()) {
            engine.print_error("cherimoya expected '\\'' to close the char literal, got the end of the file instead", {start, pos});
            tokens.push_back({TokenKind::Error, code.substr(start, pos - start), start});
            return;
        }
        pos++;
        auto text = code.substr(start, pos - start);
        tokens.push_back({TokenKind::CharLiteral, text, start});
    }

    // tries every operator spelling from longest to shortest, then falls back to punctuation, errors on no match
    void scan_operator_or_error(SourceLoc start) {
        for (size_t len = std::min(max_operator_length(), code.size() - pos); len > 0; len--) {
            auto text = code.substr(pos, len);
            auto op = lookup_operator(text);
            if (op != nullptr) {
                tokens.push_back({op->kind, text, pos, op});
                pos += len;
                return;
            }
        }
        char c = code[pos];
        switch (c) {
            case '.':
                if (match("...")) {
                    emit(TokenKind::Ellipsis, 3);
                } else {
                    emit(TokenKind::Dot, 1);
                }
                break;
            case ':':
                if (match("::")) {
                    emit(TokenKind::ColonColon, 2);
                } else {
                    emit(TokenKind::Colon, 1);
                }
                break;
            case '(': emit(TokenKind::LParen, 1); break;
            case ')': emit(TokenKind::RParen, 1); break;
            case '[': emit(TokenKind::LBracket, 1); break;
            case ']': emit(TokenKind::RBracket, 1); break;
            case '{': emit(TokenKind::LBrace, 1); break;
            case '}': emit(TokenKind::RBrace, 1); break;
            case ',': emit(TokenKind::Comma, 1); break;
            case ';': emit(TokenKind::Semi, 1); break;
            case '@': emit(TokenKind::At, 1); break;
            case '$': emit(TokenKind::Buck, 1); break;
            default:
                engine.print_error("cherimoya expected a name, a literal, an operator, or punctuation, got " + describe(start) + " instead", {start, start + 1});
                emit(TokenKind::Error, 1);
        }
    }

    // reports every pair of touching operators where one of them needs spacing, since the pair could have lexed differently
    void check_spacing() {
        for (size_t i = 0; i + 1 < tokens.size(); i++) {
            const Token& a = tokens[i];
            const Token& b = tokens[i + 1];
            if (a.op == nullptr || b.op == nullptr) continue;
            if (a.loc + a.text.size() != b.loc) continue;
            if (!needs_spacing(*a.op) && !needs_spacing(*b.op)) continue;
            std::string first(a.text), second(b.text);
            engine.print_error("cherimoya requires whitespace between the operators '" + first + "' and '" + second
                + "', please write '" + first + " " + second + "'", {a.loc, b.loc + b.text.size()});
        }
    }
public:
    Lexer(DiagnosticEngine& e) : manager(e.get_manager()), engine(e) { code = manager.text(); }

    // initializes token vector and pos, then loops until we reach the end of the code
    std::vector<Token> lex() {
        tokens = std::vector<Token>();
        pos = 0;
        while (pos < code.size()) {
            // always skip whitespace and comments
            skip_whitespace_and_comments();
            // if file ended with that break
            if (pos >= code.size()) break;
            // saves the currrent position and switches based on the char at that position
            auto start = pos;
            char c = code[pos];
            if (std::isalpha(static_cast<unsigned char>(c)) || c == '_') {
                scan_ident_or_keyword(start);
            } else if (std::isdigit(static_cast<unsigned char>(c))) {
                scan_number(start);
            } else if (c == '"') {
                scan_string(start);
            } else if (c == '\'') {
                scan_char(start);
            } else {
                scan_operator_or_error(start);
            }
        }
        // checks the spacing rule over the finished stream
        check_spacing();
        // adds an EOF token and returns
        tokens.push_back({TokenKind::Eof, "", pos});
        return tokens;
    }
};
