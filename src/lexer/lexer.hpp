#pragma once

#include <cctype>
#include <string>
#include <vector>
#include "diagnostics.hpp"
#include "escape.hpp"
#include "keywords.hpp"
#include "operators.hpp"

// the lexer holds the diagnostic engine, a view of the code, the tokens so far, the comments, which aren't tokens but are kept on the side for the language server, and the current position
class Lexer {
    DiagnosticEngine& engine;
    std::string_view code;
    std::vector<Token> tokens;
    std::vector<Span> comments;
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
    void skipWhitespaceAndComments() {
        while (true) {
            while (pos < code.size() && std::isspace(static_cast<unsigned char>(code[pos]))) pos++;
            if (pos < code.size() && code[pos] == '#') {
                SourceLoc start = pos;
                while (pos < code.size() && code[pos] != '\n') pos++;
                comments.push_back({start, pos});
                continue;
            }
            break;
        }
    }

    // reads the word fully, then checks if it's a lone underscore or a keyword and lexes accordingly
    void scanIdentOrKeyword(SourceLoc start) {
        while (pos < code.size() && (std::isalnum(static_cast<unsigned char>(code[pos])) || code[pos] == '_')) pos++;
        auto word = code.substr(start, pos - start);
        auto kind = lookupKeyword(word);
        if (word == "_") {
            tokens.push_back({TokenKind::Underscore, word, start});
        } else if (kind != std::nullopt) {
            tokens.push_back({kind.value(), word, start});
        } else {
            tokens.push_back({TokenKind::Identifier, word, start});
        }
    }

    // scans a number: a 0b, 0o, or 0x prefix picks the base, then come digits and _ separators, and a point followed by a digit makes it a float, where a letter or digit the base doesn't have right after it is part of the same mistake, so 0o78 and 123abc are each one error
    void scanNumber(SourceLoc start) {
        unsigned radix = 10;
        if (code[pos] == '0' && pos + 1 < code.size()) {
            char prefix = code[pos + 1] | 0x20;
            radix = prefix == 'b' ? 2 : prefix == 'o' ? 8 : prefix == 'x' ? 16 : 10;
            if (radix != 10) pos += 2;
        }
        auto isDigit = [&](SourceLoc p) {
            if (p >= code.size()) return false;
            unsigned char c = code[p];
            if (radix == 16) return std::isxdigit(c) != 0;
            return std::isdigit(c) != 0 && unsigned(c - '0') < radix;
        };
        auto isWordChar = [&](SourceLoc p) {
            return p < code.size() && (std::isalnum(static_cast<unsigned char>(code[p])) || code[p] == '_');
        };
        // reads a run of digits and separators, and says whether it had a digit in it
        auto digits = [&]() {
            bool seenDigit = false;
            while (pos < code.size() && (isDigit(pos) || code[pos] == '_')) {
                if (code[pos] != '_') seenDigit = true;
                pos++;
            }
            return seenDigit;
        };
        bool seenDigit = digits();
        auto kind = TokenKind::IntLiteral;
        if (pos < code.size() && code[pos] == '.' && isDigit(pos + 1)) {
            kind = TokenKind::FloatLiteral;
            pos++;
            digits();
        }
        if (!seenDigit || isWordChar(pos)) {
            std::string name = radix == 2 ? "a binary digit (0 or 1)" : radix == 8 ? "an octal digit (0 to 7)" : radix == 16 ? "a hex digit (0 to 9 or a to f)" : "a decimal digit";
            std::string after = seenDigit ? "" : " after '" + std::string(code.substr(start, 2)) + "'";
            std::string got = describe(pos);
            while (isWordChar(pos)) pos++;
            engine.printError("cherimoya expected " + name + after + ", got " + got + " instead", {start, pos});
            tokens.push_back({TokenKind::Error, code.substr(start, pos - start), start});
            return;
        }
        tokens.push_back({kind, code.substr(start, pos - start), start});
    }

    // scans a string or char literal up to its closing quote, skipping over \ escapes, and reports one that's never closed at its opening quote, then checks what it holds
    void scanQuoted(SourceLoc start, char quote) {
        bool isString = quote == '"';
        pos++;
        while (pos < code.size() && code[pos] != quote) {
            if (code[pos] == '\\') pos++;
            pos++;
        }
        if (pos >= code.size()) {
            pos = code.size();
            std::string spelling = isString ? "'\"'" : "'\\''";
            engine.printError("cherimoya expected " + spelling + " to close the " + (isString ? "string" : "char") + " literal, got the end of the file instead", {start, start + 1});
            tokens.push_back({TokenKind::Error, code.substr(start, pos - start), start});
            return;
        }
        pos++;
        // reads every character between the quotes, reporting each one that's malformed, where a char literal has to hold exactly one
        std::string_view contents = code.substr(start + 1, pos - start - 2);
        size_t count = 0;
        bool isValid = true;
        for (size_t at = 0; at < contents.size(); count++) {
            Decoded decoded = decodeCharacter(contents.substr(at));
            if (!decoded.error.empty()) {
                engine.printError(decoded.error, {start + 1 + at, start + 1 + at + decoded.length});
                isValid = false;
            }
            at += decoded.length;
        }
        if (!isString && isValid && count != 1) {
            engine.printError("cherimoya expected one character in a char literal, got " + (count == 0 ? std::string("none") : std::to_string(count)) + " instead", {start, pos});
            isValid = false;
        }
        auto kind = !isValid ? TokenKind::Error : isString ? TokenKind::StringLiteral : TokenKind::CharLiteral;
        tokens.push_back({kind, code.substr(start, pos - start), start});
    }

    // tries every operator spelling from longest to shortest, then falls back to punctuation, errors on no match
    void scanOperatorOrError(SourceLoc start) {
        for (size_t len = std::min(maxOperatorLength(), code.size() - pos); len > 0; len--) {
            auto text = code.substr(pos, len);
            auto op = lookupOperator(text);
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
            case '$': emit(TokenKind::Dollar, 1); break;
            default: {
                // a non-ASCII character is several bytes, a lead byte and its continuation bytes, and it's one mistake
                size_t length = 1;
                if (static_cast<unsigned char>(c) >= 0x80) {
                    while (pos + length < code.size() && (static_cast<unsigned char>(code[pos + length]) & 0xC0) == 0x80) length++;
                }
                std::string got = length == 1 ? describe(start) : "'" + std::string(code.substr(start, length)) + "'";
                engine.printError("cherimoya expected a name, a literal, an operator, or punctuation, got " + got + " instead", {start, start + length});
                emit(TokenKind::Error, length);
            }
        }
    }

    // reports every pair of touching operators where one of them needs spacing, since the pair could have lexed differently
    void checkSpacing() {
        for (size_t i = 0; i + 1 < tokens.size(); i++) {
            const Token& a = tokens[i];
            const Token& b = tokens[i + 1];
            if (a.op == nullptr || b.op == nullptr) continue;
            if (a.loc + a.text.size() != b.loc) continue;
            if (!needsSpacing(*a.op) && !needsSpacing(*b.op)) continue;
            std::string first(a.text), second(b.text);
            engine.printError("cherimoya requires whitespace between the operators '" + first + "' and '" + second
                + "', please write '" + first + " " + second + "'", {a.loc, b.loc + b.text.size()});
        }
    }
public:
    Lexer(DiagnosticEngine& e) : engine(e), code(e.getManager().text()) {}

    // lexes the whole code into tokens, ending with an Eof token
    std::vector<Token> lex() {
        tokens = std::vector<Token>();
        comments = std::vector<Span>();
        pos = 0;
        while (pos < code.size()) {
            skipWhitespaceAndComments();
            // the file can end in whitespace or a comment
            if (pos >= code.size()) break;
            // what starts at the current position decides what's scanned
            auto start = pos;
            char c = code[pos];
            if (std::isalpha(static_cast<unsigned char>(c)) || c == '_') {
                scanIdentOrKeyword(start);
            } else if (std::isdigit(static_cast<unsigned char>(c))) {
                scanNumber(start);
            } else if (c == '"' || c == '\'') {
                scanQuoted(start, c);
            } else {
                scanOperatorOrError(start);
            }
        }
        // checks the spacing rule over the finished stream
        checkSpacing();
        tokens.push_back({TokenKind::Eof, "", pos});
        return std::move(tokens);
    }

    // every comment the last lex skipped, in order
    const std::vector<Span>& allComments() const { return comments; }
};
