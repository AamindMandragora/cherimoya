#pragma once

#include <optional>
#include <string_view>
#include <unordered_map>
#include "token.hpp"

inline const std::unordered_map<std::string_view, TokenKind>& keyword_table() {
    static const std::unordered_map<std::string_view, TokenKind> table = {
        // ---- Keywords ----
        // Declarations
        {"let",       TokenKind::KwLet},
        {"type",      TokenKind::KwType},
        {"export",    TokenKind::KwExport},
        {"import",    TokenKind::KwImport},
        // Scopes
        {"match",     TokenKind::KwMatch},
        {"repeat",    TokenKind::KwRepeat},
        {"chunk",     TokenKind::KwChunk},
        // Modes, qualifiers, and handles
        {"unscoped",  TokenKind::KwUnscoped},
        {"peek",      TokenKind::KwPeek},
        {"mut",       TokenKind::KwMut},
        {"ptr",       TokenKind::KwPtr},
        {"arr",       TokenKind::KwArr},
        // Place intrinsics
        {"take",      TokenKind::KwTake},
        {"swap",      TokenKind::KwSwap},
        {"replace",   TokenKind::KwReplace},
        // Verification
        {"wit",       TokenKind::KwWit},
        {"assert",    TokenKind::KwAssert},
        {"old",       TokenKind::KwOld},
        // Literals
        {"true",      TokenKind::KwTrue},
        {"false",     TokenKind::KwFalse},
        // Macros
        {"macro",     TokenKind::KwMacro},
        {"moya",      TokenKind::KwMoya},

        // ---- Built-in types ----
        // Signed trapping integers
        {"i8",    TokenKind::TypeName},
        {"i16",   TokenKind::TypeName},
        {"i32",   TokenKind::TypeName},
        {"i64",   TokenKind::TypeName},
        {"i128",  TokenKind::TypeName},
        // Unsigned trapping (u)
        {"i8u",   TokenKind::TypeName},
        {"i16u",  TokenKind::TypeName},
        {"i32u",  TokenKind::TypeName},
        {"i64u",  TokenKind::TypeName},
        {"i128u", TokenKind::TypeName},
        // Signed wrapping (w)
        {"i8w",   TokenKind::TypeName},
        {"i16w",  TokenKind::TypeName},
        {"i32w",  TokenKind::TypeName},
        {"i64w",  TokenKind::TypeName},
        {"i128w", TokenKind::TypeName},
        // Unsigned wrapping (uw)
        {"i8uw",   TokenKind::TypeName},
        {"i16uw",  TokenKind::TypeName},
        {"i32uw",  TokenKind::TypeName},
        {"i64uw",  TokenKind::TypeName},
        {"i128uw", TokenKind::TypeName},
        // NaN-free floats
        {"f8",       TokenKind::TypeName},  // f8e4m3
        {"f8e4m3",   TokenKind::TypeName},
        {"f8e5m2",   TokenKind::TypeName},
        {"f16",      TokenKind::TypeName},
        {"f16e8m7",  TokenKind::TypeName},  // bfloat16
        {"f32",      TokenKind::TypeName},
        {"f64",      TokenKind::TypeName},
        {"f128",     TokenKind::TypeName},
        // NaN-carrying floats (n)
        {"f8e4m3n",  TokenKind::TypeName},
        {"f8e5m2n",  TokenKind::TypeName},
        {"f16n",     TokenKind::TypeName},
        {"f16e8m7n", TokenKind::TypeName},
        {"f32n",     TokenKind::TypeName},
        {"f64n",     TokenKind::TypeName},
        {"f128n",    TokenKind::TypeName},
        // Named / alias types
        {"bool",  TokenKind::TypeName},
        {"int",   TokenKind::TypeName},  // i64
        {"float", TokenKind::TypeName},  // f64
        {"char",  TokenKind::TypeName},  // 1-byte fixed-width
        {"rune",  TokenKind::TypeName},  // 4-byte Unicode code point
        {"boolf", TokenKind::TypeName},  // TODO: fuzzy boolean
    };
    return table;
}

// helper function to look up a keyword in the above table
inline std::optional<TokenKind> lookup_keyword(std::string_view word) {
    const auto& table = keyword_table();
    auto it = table.find(word);
    if (it == table.end()) return std::nullopt;
    return it->second;
}
