#pragma once

#include <algorithm>
#include <iostream>
#include <vector>
#include "source.hpp"

struct Span {
    SourceLoc start;
    SourceLoc end;
    size_t length() const { return end - start; }
};

// notes point at a second location that explains an error: "'x' was declared here"
struct Note {
    std::string message;
    Span span;
};

// diagnostics will have a message, a location, any notes that go with it, and whether they're only a warning
struct Diagnostic {
    std::string error;
    Span span;
    std::vector<Note> notes;
    bool warning;
};

// diagnostic engine holds source manager and a vector of diagnostics (errors and warnings)
class DiagnosticEngine {
    SourceManager& manager;
    std::vector<Diagnostic> diagnostics;
public:
    explicit DiagnosticEngine(SourceManager& m) : manager(m) {}
    SourceManager& get_manager() { return manager; }
    
    // calculates line and col for offset loc, then formats the line(s) where it happened with the span underlined, then the message
    std::string render(std::string_view label, std::string_view message, Span span) {
        LineCol start_lc = manager.get_line_col(span.start);
        LineCol end_lc   = manager.get_line_col(span.end);
        std::string out;
        // writes the line(s) the span is on
        if (start_lc.line == end_lc.line) {
            out += std::string(label) + " at line " + std::to_string(start_lc.line) + ":\n";
        } else {
            out += std::string(label) + " at lines " + std::to_string(start_lc.line)
                + "-" + std::to_string(end_lc.line) + ":\n";
        }
        // loops through all the lines, underline portions belonging to the span
        for (uint32_t ln = start_lc.line; ln <= end_lc.line; ln++) {
            std::string_view line = manager.get_line_n(ln);
            out += line;
            out += '\n';
            size_t col_begin = (ln == start_lc.line) ? start_lc.col : 1;
            size_t col_end = (ln == end_lc.line) ? end_lc.col : line.size() + 1;
            for (size_t i = 1; i < col_begin; i++) {
                if (i - 1 < line.size() && line[i - 1] == '\t') out += '\t';
                else out += ' ';
            }
            // calculates how many carets to write
            size_t carets = (col_end > col_begin) ? (col_end - col_begin) : 0;
            size_t room = (line.size() + 1 > col_begin) ? (line.size() + 1 - col_begin) : 0;
            carets = std::min(carets, room);
            if (carets == 0) carets = 1;
            out.append(carets, '^');
            out += '\n';
        }
        out += message;
        out += '\n';
        return out;
    }

    // formats the nth diagnostic followed by each of its notes
    std::string get_error_n(size_t n) {
        if (n >= diagnostics.size()) return "";
        auto& [error, span, notes, warning] = diagnostics[n];
        std::string out = render(warning ? "warning" : "error", error, span);
        for (auto& note : notes) out += render("note", note.message, note.span);
        return out;
    }
    
    // makes diagnostic, adds to vector, then prints formatted error to stdout
    void print_error(std::string_view error, Span span, std::vector<Note> notes = {}) {
        diagnostics.push_back({std::string(error), span, std::move(notes), false});
        std::cout << get_error_n(diagnostics.size() - 1) << std::endl;
    }

    // makes a warning, which is reported like an error but doesn't stop compilation
    void print_warning(std::string_view warning, Span span, std::vector<Note> notes = {}) {
        diagnostics.push_back({std::string(warning), span, std::move(notes), true});
        std::cout << get_error_n(diagnostics.size() - 1) << std::endl;
    }

    bool had_errors() const { return error_count() > 0; }
    size_t error_count() const { return std::count_if(diagnostics.begin(), diagnostics.end(), [](const Diagnostic& d) { return !d.warning; }); }
    size_t warning_count() const { return diagnostics.size() - error_count(); }
};