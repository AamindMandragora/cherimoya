#pragma once

#include <algorithm>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>
#include "parser.hpp"
#include "sema.hpp"

// a position the way the editor counts it: a line and a column from 0, where the column counts UTF-16 code units, since that's how VS Code measures text
struct Position {
    uint32_t line;
    uint32_t character;
};

// where every line of a text starts, which turns byte offsets into positions and back
class LineIndex {
    std::string_view text;
    std::vector<size_t> starts;

    // how many UTF-16 code units a UTF-8 byte adds: a continuation byte adds none, a 4-byte character's lead byte adds a surrogate pair, and anything else adds one
    static uint32_t units(unsigned char byte) {
        if ((byte & 0xC0) == 0x80) return 0;
        return byte >= 0xF0 ? 2 : 1;
    }
public:
    explicit LineIndex(std::string_view t) : text(t) {
        starts.push_back(0);
        for (size_t i = 0; i < text.size(); i++) {
            if (text[i] == '\n') starts.push_back(i + 1);
        }
    }

    // the position of a byte offset, found by the last line starting at or before it
    Position position(size_t offset) const {
        offset = std::min(offset, text.size());
        size_t line = std::upper_bound(starts.begin(), starts.end(), offset) - starts.begin() - 1;
        uint32_t character = 0;
        for (size_t i = starts[line]; i < offset; i++) character += units(text[i]);
        return {static_cast<uint32_t>(line), character};
    }

    // the byte offset of a position, where a position past a line's end is that line's end and a line past the last is the end of the text
    size_t offset(Position position) const {
        if (position.line >= starts.size()) return text.size();
        size_t i = starts[position.line];
        uint32_t character = 0;
        while (i < text.size() && text[i] != '\n' && character < position.character) {
            character += units(text[i]);
            i++;
            while (i < text.size() && units(text[i]) == 0) i++;
        }
        return i;
    }
};

// one version of an open document and everything the front end learned about it, which owns the text that its tokens, its tree, and its symbols all point into
class Document {
public:
    std::string uri;
    SourceManager manager;
    // the server prints nothing, since its stdout carries the protocol, so diagnostics are only recorded
    DiagnosticEngine engine{manager, nullptr};
    std::vector<Token> tokens;
    std::vector<Span> comments;
    std::vector<std::unique_ptr<Stmt>> program;
    Sema sema{engine};
    std::unique_ptr<LineIndex> lines;

    // runs the whole front end over a version of the document
    Document(std::string u, const std::string& text) : uri(std::move(u)) {
        manager.loadString(text);
        Lexer lexer(engine);
        tokens = lexer.lex();
        comments = lexer.allComments();
        program = Parser(tokens, engine).parseProgram();
        sema.check(program);
        lines = std::make_unique<LineIndex>(manager.text());
    }
};
