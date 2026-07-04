#pragma once

#include <cstdint>
#include <fstream>
#include <string>
#include <string_view>

// we use byte offsets as our locations
using SourceLoc = size_t;
// a line and a column, both counted from 1
struct LineCol { uint32_t line, col; };

// source manager holds code
class SourceManager {
    std::string code;
public:
    // loads string as code
    void loadString(const std::string& src) {
        code = src;
    }
    // reads a file and loads its contents as code, or no code when it can't be read, like a missing file or a directory
    void loadFile(const std::string& path) {
        code = "";
        std::ifstream file(path, std::ios::binary);
        if (!file.is_open()) return;
        file.seekg(0, std::ios::end);
        std::streamsize size = file.tellg();
        if (size < 0) return;
        file.seekg(0, std::ios::beg);
        code = std::string(size, '\0');
        file.read(&code[0], size);
    }
    std::string_view text() const { return code; }
    // converts byte offset to line + col (1-indexed)
    LineCol getLineCol(SourceLoc loc) {
        auto lineCol = LineCol{1, 1};
        // at every newline, reset col and increment line
        for (size_t i = 0; i < loc; i++) {
            if (code[i] == '\n') {
                lineCol.line++;
                lineCol.col = 1;
            } else {
                lineCol.col++;
            }
        }
        return lineCol;
    }
    // gets n-th line of code
    std::string_view getLineN(uint32_t n) {
        size_t pos = 0;
        for (uint32_t i = 1; i < n; i++) {
            size_t nextPos = code.find('\n', pos);
            if (nextPos == std::string_view::npos) return std::string_view();
            pos = nextPos + 1;
        }
        size_t lineEnd = code.find('\n', pos);
        if (lineEnd == std::string_view::npos) lineEnd = code.size();
        return std::string_view(code).substr(pos, lineEnd - pos);
    }
};