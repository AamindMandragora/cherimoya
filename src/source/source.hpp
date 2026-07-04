#pragma once

#include <string>
#include <fstream>
#include <cstdint>
#include <unordered_map>

// we use byte offsets as our locations
using SourceLoc = size_t;
// line + col struct
struct LineCol { uint32_t line, col; };

// source manager holds code
class SourceManager {
    std::string code;
public:
    // loads string as code
    void load_string(const std::string& src) {
        code = src;
    }
    // reads a file and loads contents as code
    void load_file(const std::string& path) {
        std::ifstream file(path, std::ios::binary);
        if (!file.is_open()) { code = ""; return; }
        file.seekg(0, std::ios::end);
        std::streamsize size = file.tellg();
        file.seekg(0, std::ios::beg);
        code = std::string(size, '\0');
        file.read(&code[0], size);
    }
    std::string_view text() const { return code; }
    // converts byte offset to line + col (1-indexed)
    LineCol get_line_col(SourceLoc loc) {
        auto line_col = LineCol{1, 1};
        // at every newline, reset col and increment line
        for (size_t i = 0; i < loc; i++) {
            if (code[i] == '\n') {
                line_col.line++;
                line_col.col = 1;
            } else {
                line_col.col++;
            }
        }
        return line_col;
    }
    // gets n-th line of code
    std::string_view get_line_n(long n) {
        size_t pos = 0;
        for (long i = 1; i < n; i++) {
            size_t next_pos = code.find('\n', pos);
            if (next_pos == std::string_view::npos) return std::string_view();
            pos = next_pos + 1;
        }
        size_t line_end = code.find('\n', pos);
        if (line_end == std::string_view::npos) line_end = code.size();
        return std::string_view(code).substr(pos, line_end - pos);
    }
};