#pragma once

#include <optional>
#include <string_view>
#include <unordered_map>
#include "token.hpp"

// every keyword and built-in type spelling, and the kind each one lexes as
inline const std::unordered_map<std::string_view, TokenKind>& keywordTable() {
    static const std::unordered_map<std::string_view, TokenKind> table = {
        // declaration keywords
        {"let",       TokenKind::KwLet},
        {"type",      TokenKind::KwType},
        {"export",    TokenKind::KwExport},
        {"import",    TokenKind::KwImport},
        // scope keywords
        {"match",     TokenKind::KwMatch},
        {"repeat",    TokenKind::KwRepeat},
        {"chunk",     TokenKind::KwChunk},
        // modes, qualifiers, and handles
        {"unscoped",  TokenKind::KwUnscoped},
        {"peek",      TokenKind::KwPeek},
        {"mut",       TokenKind::KwMut},
        {"ptr",       TokenKind::KwPtr},
        {"arr",       TokenKind::KwArr},
        // place intrinsics
        {"take",      TokenKind::KwTake},
        {"swap",      TokenKind::KwSwap},
        {"replace",   TokenKind::KwReplace},
        // verification keywords
        {"wit",       TokenKind::KwWit},
        {"assert",    TokenKind::KwAssert},
        {"old",       TokenKind::KwOld},
        // the bool literals
        {"true",      TokenKind::KwTrue},
        {"false",     TokenKind::KwFalse},
        // macro keywords
        {"macro",     TokenKind::KwMacro},
        {"moya",      TokenKind::KwMoya},

        // signed trapping integers
        {"i8",    TokenKind::TypeName},
        {"i16",   TokenKind::TypeName},
        {"i32",   TokenKind::TypeName},
        {"i64",   TokenKind::TypeName},
        {"i128",  TokenKind::TypeName},
        // unsigned trapping integers, u
        {"i8u",   TokenKind::TypeName},
        {"i16u",  TokenKind::TypeName},
        {"i32u",  TokenKind::TypeName},
        {"i64u",  TokenKind::TypeName},
        {"i128u", TokenKind::TypeName},
        // signed wrapping integers, w
        {"i8w",   TokenKind::TypeName},
        {"i16w",  TokenKind::TypeName},
        {"i32w",  TokenKind::TypeName},
        {"i64w",  TokenKind::TypeName},
        {"i128w", TokenKind::TypeName},
        // unsigned wrapping integers, uw
        {"i8uw",   TokenKind::TypeName},
        {"i16uw",  TokenKind::TypeName},
        {"i32uw",  TokenKind::TypeName},
        {"i64uw",  TokenKind::TypeName},
        {"i128uw", TokenKind::TypeName},
        // NaN-free floats, where f8 is f8e4m3 and f16e8m7 is bfloat16
        {"f8",       TokenKind::TypeName},
        {"f8e4m3",   TokenKind::TypeName},
        {"f8e5m2",   TokenKind::TypeName},
        {"f16",      TokenKind::TypeName},
        {"f16e8m7",  TokenKind::TypeName},
        {"f32",      TokenKind::TypeName},
        {"f64",      TokenKind::TypeName},
        {"f128",     TokenKind::TypeName},
        // NaN-carrying floats, n
        {"f8e4m3n",  TokenKind::TypeName},
        {"f8e5m2n",  TokenKind::TypeName},
        {"f16n",     TokenKind::TypeName},
        {"f16e8m7n", TokenKind::TypeName},
        {"f32n",     TokenKind::TypeName},
        {"f64n",     TokenKind::TypeName},
        {"f128n",    TokenKind::TypeName},
        // bool, the aliases int for i64 and float for f64, a 1-byte char, a 4-byte rune for a Unicode code point, and boolf, a fuzzy boolean
        {"bool",  TokenKind::TypeName},
        {"int",   TokenKind::TypeName},
        {"float", TokenKind::TypeName},
        {"char",  TokenKind::TypeName},
        {"rune",  TokenKind::TypeName},
        {"boolf", TokenKind::TypeName},
    };
    return table;
}

// looks up a word in the table, which is a keyword or type spelling, or nothing
inline std::optional<TokenKind> lookupKeyword(std::string_view word) {
    const auto& table = keywordTable();
    auto it = table.find(word);
    if (it == table.end()) return std::nullopt;
    return it->second;
}
