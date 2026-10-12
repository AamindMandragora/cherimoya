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

// notes point at a second location that explains an error: "'x' was declared here"
struct Note {
    std::string message;
    Span span;
};

// a diagnostic has a message, a location, any notes that go with it, and whether it's only a warning
struct Diagnostic {
    std::string message;
    Span span;
    std::vector<Note> notes;
    bool isWarning;
};

// the diagnostic engine holds the source manager and every diagnostic reported so far
class DiagnosticEngine {
    SourceManager& manager;
    std::vector<Diagnostic> diagnostics;

    // records a diagnostic, then prints it formatted to stdout
    void report(std::string_view message, Span span, std::vector<Note> notes, bool isWarning) {
        diagnostics.push_back({std::string(message), span, std::move(notes), isWarning});
        std::cout << getErrorN(diagnostics.size() - 1) << std::endl;
    }
public:
    explicit DiagnosticEngine(SourceManager& m) : manager(m) {}
    SourceManager& getManager() { return manager; }

    // formats the line or lines a span is on with the span underlined, then the message, under a label like error or note
    std::string render(std::string_view label, std::string_view message, Span span) {
        LineCol startLc = manager.getLineCol(span.start);
        // a span ends just past its last character, which is on the next line when the span ends with a newline, so the last line is its last character's
        LineCol endLc = manager.getLineCol(span.length() > 0 ? span.end - 1 : span.end);
        std::string out;
        // writes the line(s) the span is on
        if (startLc.line == endLc.line) {
            out += std::string(label) + " at line " + std::to_string(startLc.line) + ":\n";
        } else {
            out += std::string(label) + " at lines " + std::to_string(startLc.line)
                + "-" + std::to_string(endLc.line) + ":\n";
        }
        // loops through all the lines, underline portions belonging to the span
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

    // formats the nth diagnostic followed by each of its notes
    std::string getErrorN(size_t n) {
        if (n >= diagnostics.size()) return "";
        auto& [message, span, notes, isWarning] = diagnostics[n];
        std::string out = render(isWarning ? "warning" : "error", message, span);
        for (auto& note : notes) out += render("note", note.message, note.span);
        return out;
    }

    // records an error with any notes that explain it, then prints it formatted to stdout
    void printError(std::string_view message, Span span, std::vector<Note> notes = {}) {
        report(message, span, std::move(notes), false);
    }

    // records a warning, which is reported like an error but doesn't stop compilation
    void printWarning(std::string_view message, Span span, std::vector<Note> notes = {}) {
        report(message, span, std::move(notes), true);
    }

    bool hadErrors() const { return errorCount() > 0; }
    size_t errorCount() const { return std::count_if(diagnostics.begin(), diagnostics.end(), [](const Diagnostic& d) { return !d.isWarning; }); }
    size_t warningCount() const { return diagnostics.size() - errorCount(); }
};
