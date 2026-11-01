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

// an edit replaces a span's text, so an empty span inserts and empty text deletes
struct Edit {
    Span span;
    std::string text;
};

// a fix is one way to resolve a diagnostic: what an editor calls it, and the edits that make it, which never overlap
struct Fix {
    std::string title;
    std::vector<Edit> edits;
};

// inserts text at a location
inline Edit insertion(SourceLoc at, std::string text) {
    return {{at, at}, std::move(text)};
}

// removes a span, and the whole line it's on when nothing else is, so removing a statement doesn't leave an empty line behind
inline Edit removal(std::string_view text, Span span) {
    auto isBlank = [](char c) { return c == ' ' || c == '\t'; };
    size_t lineStart = span.start;
    while (lineStart > 0 && isBlank(text[lineStart - 1])) lineStart--;
    size_t lineEnd = span.end;
    while (lineEnd < text.size() && isBlank(text[lineEnd])) lineEnd++;
    bool isAlone = (lineStart == 0 || text[lineStart - 1] == '\n') && (lineEnd == text.size() || text[lineEnd] == '\n');
    if (!isAlone) return {span, ""};
    return {{lineStart, lineEnd < text.size() ? lineEnd + 1 : lineEnd}, ""};
}

// applies edits to text, last first, so each one's offsets still mean what they did in the original
inline std::string applyEdits(std::string text, std::vector<Edit> edits) {
    std::sort(edits.begin(), edits.end(), [](const Edit& a, const Edit& b) { return a.span.start > b.span.start; });
    for (auto& edit : edits) text.replace(edit.span.start, edit.span.length(), edit.text);
    return text;
}

// a diagnostic has a message, a location, any notes that go with it, the fixes that would resolve it, and whether it's only a warning
struct Diagnostic {
    std::string message;
    Span span;
    std::vector<Note> notes;
    std::vector<Fix> fixes;
    bool isWarning;
};

// the diagnostic engine holds the source manager, every diagnostic reported so far, and where they're printed, which is nowhere for the language server, since its stdout carries the protocol
class DiagnosticEngine {
    SourceManager& manager;
    std::vector<Diagnostic> diagnostics;
    std::ostream* output;

    // records a diagnostic, then prints it formatted, if there's anywhere to print it
    void report(std::string_view message, Span span, std::vector<Note> notes, std::vector<Fix> fixes, bool isWarning) {
        diagnostics.push_back({std::string(message), span, std::move(notes), std::move(fixes), isWarning});
        if (output) *output << getErrorN(diagnostics.size() - 1) << std::endl;
    }
public:
    explicit DiagnosticEngine(SourceManager& m, std::ostream* o = &std::cout) : manager(m), output(o) {}
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
        auto& diagnostic = diagnostics[n];
        std::string out = render(diagnostic.isWarning ? "warning" : "error", diagnostic.message, diagnostic.span);
        for (auto& note : diagnostic.notes) out += render("note", note.message, note.span);
        return out;
    }

    // records an error with any notes that explain it and any fixes that resolve it, then prints it
    void printError(std::string_view message, Span span, std::vector<Note> notes = {}, std::vector<Fix> fixes = {}) {
        report(message, span, std::move(notes), std::move(fixes), false);
    }

    // records a warning, which is reported like an error but doesn't stop compilation
    void printWarning(std::string_view message, Span span, std::vector<Note> notes = {}, std::vector<Fix> fixes = {}) {
        report(message, span, std::move(notes), std::move(fixes), true);
    }

    // every diagnostic reported so far, in order
    const std::vector<Diagnostic>& all() const { return diagnostics; }

    bool hadErrors() const { return errorCount() > 0; }
    size_t errorCount() const { return std::count_if(diagnostics.begin(), diagnostics.end(), [](const Diagnostic& d) { return !d.isWarning; }); }
    size_t warningCount() const { return diagnostics.size() - errorCount(); }
};
