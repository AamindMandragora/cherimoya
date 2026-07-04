#pragma once

#include <iostream>
#include <vector>
#include "source.hpp"

struct Span {
    SourceLoc start;
    SourceLoc end;
    size_t length() const { return end - start; }
};

// errors will have a message and a location
struct Diagnostic {
    std::string error;
    Span span;
};

// diagnostic engine holds source manager and a vector of diagnostics (errors)
class DiagnosticEngine {
    SourceManager& manager;
    std::vector<Diagnostic> diagnostics;
public:
    explicit DiagnosticEngine(SourceManager& m) : manager(m) {}
    SourceManager& get_manager() { return manager; }
    
    // calculates line and col for offset loc, then formats and prints error and line where it happened
    std::string get_error_n(size_t n) {
        if (n >= diagnostics.size()) return "";
        auto [error, span] = diagnostics[n];
        LineCol start_lc = manager.get_line_col(span.start);
        LineCol end_lc   = manager.get_line_col(span.end);
        std::string out;
        // writes the line(s) the error happened at
        if (start_lc.line == end_lc.line) {
            out += "error at line " + std::to_string(start_lc.line) + ":\n";
        } else {
            out += "error at lines " + std::to_string(start_lc.line)
                + "-" + std::to_string(end_lc.line) + ":\n";
        }
        // loops through all the lines, underline portions belonging to the error span
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
        out += error;
        out += '\n';
        return out;
    }
    
    // makes diagnostic, adds to vector, then prints formatted error to stdout
    void print_error(std::string_view error, Span span) {
        diagnostics.push_back({std::string(error), span});
        std::cout << get_error_n(diagnostics.size() - 1) << std::endl;
    }

    bool had_errors() const { return !diagnostics.empty(); }
    size_t error_count() const { return diagnostics.size(); }
};