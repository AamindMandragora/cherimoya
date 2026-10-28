#pragma once

#include <cstdint>
#include <string>
#include <string_view>

// one character read out of a quoted literal: its code point, how many bytes of the literal it took, and what's wrong with it, which is empty when nothing is
struct Decoded {
    uint32_t value;
    size_t length;
    std::string error;
};

// checks whether a code point is a Unicode scalar value, which is every code point but the surrogates, D800 to DFFF, the halves UTF-16 pairs up, which aren't characters on their own
inline bool isScalar(uint32_t value) {
    return value <= 0x10FFFF && !(0xD800 <= value && value <= 0xDFFF);
}

// the value of a hex digit, or -1 if it isn't one
inline int hexValue(char c) {
    if ('0' <= c && c <= '9') return c - '0';
    char lower = c | 0x20;
    return ('a' <= lower && lower <= 'f') ? 10 + (lower - 'a') : -1;
}

// reads the character at the start of a quoted literal's contents: an escape, which is \n \t \r \0 \\ \' \", \xHH for a code point up to FF, or \u{...} for any Unicode scalar value, or a character written as itself in UTF-8
inline Decoded decodeCharacter(std::string_view text) {
    // what the literal holds from one byte to another, quoted, for a diagnostic
    auto got = [&](size_t from, size_t to) {
        return from >= text.size() ? std::string("the end of the literal") : "'" + std::string(text.substr(from, to - from)) + "'";
    };
    unsigned char lead = text[0];
    if (lead != '\\') {
        // the lead byte's high bits say how many bytes the character takes, and each byte after it is a continuation byte, 10xxxxxx, holding 6 more bits
        size_t length = lead < 0x80 ? 1 : (lead & 0xE0) == 0xC0 ? 2 : (lead & 0xF0) == 0xE0 ? 3 : (lead & 0xF8) == 0xF0 ? 4 : 0;
        std::string invalid = "cherimoya expected a character in UTF-8, got bytes that aren't UTF-8 instead";
        if (length == 0 || length > text.size()) return {0, 1, invalid};
        uint32_t value = length == 1 ? lead : lead & (0x7F >> length);
        for (size_t i = 1; i < length; i++) {
            unsigned char next = text[i];
            if ((next & 0xC0) != 0x80) return {0, i, invalid};
            value = value << 6 | (next & 0x3F);
        }
        // a character written with more bytes than it needs, or a surrogate, isn't UTF-8 either
        const uint32_t smallest[] = {0, 0, 0x80, 0x800, 0x10000};
        if (value < smallest[length] || !isScalar(value)) return {0, length, invalid};
        return {value, length, ""};
    }
    switch (text[1]) {
        case 'n': return {'\n', 2, ""};
        case 't': return {'\t', 2, ""};
        case 'r': return {'\r', 2, ""};
        case '0': return {0, 2, ""};
        case '\\':
        case '\'':
        case '"':
            return {static_cast<unsigned char>(text[1]), 2, ""};
        case 'x': {
            size_t end = std::min<size_t>(text.size(), 4);
            if (end < 4 || hexValue(text[2]) < 0 || hexValue(text[3]) < 0) {
                return {0, end, "cherimoya expected two hex digits after '\\x', got " + got(2, end) + " instead"};
            }
            return {static_cast<uint32_t>(hexValue(text[2]) * 16 + hexValue(text[3])), 4, ""};
        }
        case 'u': {
            // up to 6 hex digits in braces, which is enough for 10FFFF, the largest code point
            size_t end = 3;
            uint32_t value = 0;
            while (end < text.size() && end < 9 && hexValue(text[end]) >= 0) value = value * 16 + hexValue(text[end++]);
            if (text.size() < 3 || text[2] != '{' || end == 3 || end >= text.size() || text[end] != '}') {
                size_t through = std::min(end + 1, text.size());
                return {0, through, "cherimoya expected 1 to 6 hex digits in braces after '\\u', as in '\\u{1F600}', got " + got(2, through) + " instead"};
            }
            if (!isScalar(value)) {
                return {0, end + 1, "cherimoya requires " + got(0, end + 1) + " to be a Unicode scalar value, please use a code point from 0 to 10FFFF outside D800 to DFFF"};
            }
            return {value, end + 1, ""};
        }
        default: {
            // the bad escape takes in the whole character after the \, which can be several bytes
            size_t end = 2;
            while (end < text.size() && (static_cast<unsigned char>(text[end]) & 0xC0) == 0x80) end++;
            return {0, end, "cherimoya expected an escape, which is \\n, \\t, \\r, \\0, \\\\, \\', \\\", \\xHH, or \\u{...}, got " + got(0, end) + " instead"};
        }
    }
}
