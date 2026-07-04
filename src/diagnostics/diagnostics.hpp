#pragma once

#include <algorithm>
#include <iostream>
#include <string>
#include <vector>
#include "source.hpp"

struct Span {
    SourceLoc start;
    SourceLoc end;
    size_t length() const { return end - start; }
};

// a diagnostic has a message and a location
struct Diagnostic {
    std::string message;
    Span span;
};

// the diagnostic engine holds the source manager and every diagnostic reported so far
class DiagnosticEngine {
    SourceManager& manager;
    std::vector<Diagnostic> diagnostics;
public:
    explicit DiagnosticEngine(SourceManager& m) : manager(m) {}
    SourceManager& getManager() { return manager; }
    
    // formats the nth diagnostic: the line or lines it's on with its span underlined, then its message
    std::string getErrorN(size_t n) {
        if (n >= diagnostics.size()) return "";
        auto [message, span] = diagnostics[n];
        LineCol startLc = manager.getLineCol(span.start);
        // a span ends just past its last character, which is on the next line when the span ends with a newline, so the last line is its last character's
        LineCol endLc = manager.getLineCol(span.length() > 0 ? span.end - 1 : span.end);
        std::string out;
        // writes the line(s) the error happened at
        if (startLc.line == endLc.line) {
            out += "error at line " + std::to_string(startLc.line) + ":\n";
        } else {
            out += "error at lines " + std::to_string(startLc.line)
                + "-" + std::to_string(endLc.line) + ":\n";
        }
        // loops through all the lines, underline portions belonging to the error span
        for (uint32_t ln = startLc.line; ln <= endLc.line; ln++) {
            std::string_view line = manager.getLineN(ln);
            out += line;
            out += '\n';
            size_t colBegin = (ln == startLc.line) ? startLc.col : 1;
            size_t colEnd = (ln == endLc.line) ? endLc.col + (span.length() > 0 ? 1 : 0) : line.size() + 1;
            for (size_t i = 1; i < colBegin; i++) {
                if (i - 1 < line.size() && line[i - 1] == '\t') out += '\t';
                else out += ' ';
            }
            // calculates how many carets to write
            size_t carets = (colEnd > colBegin) ? (colEnd - colBegin) : 0;
            size_t room = (line.size() + 1 > colBegin) ? (line.size() + 1 - colBegin) : 0;
            carets = std::min(carets, room);
            if (carets == 0) carets = 1;
            out.append(carets, '^');
            out += '\n';
        }
        out += message;
        out += '\n';
        return out;
    }

    // records an error, then prints it formatted to stdout
    void printError(std::string_view message, Span span) {
        diagnostics.push_back({std::string(message), span});
        std::cout << getErrorN(diagnostics.size() - 1) << std::endl;
    }

    bool hadErrors() const { return !diagnostics.empty(); }
    size_t errorCount() const { return diagnostics.size(); }
};