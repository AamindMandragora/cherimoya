#pragma once

#include "diagnostics.hpp"

// the codes of the rules the lexer reports, each with the section of the reference that explains it
namespace codes {
// a number's digits have to be digits of its base
inline constexpr Code numberDigit{"L001", "1"};
// a string or char literal has to be closed
inline constexpr Code unclosedLiteral{"L002", "1"};
// an escape has to be one of the escapes a literal holds
inline constexpr Code unknownEscape{"L003", "1"};
// a \u{...} escape has to name a Unicode scalar value
inline constexpr Code scalarEscape{"L004", "1"};
// a literal's characters have to be UTF-8
inline constexpr Code invalidUtf8{"L005", "1"};
// a char literal holds exactly one character
inline constexpr Code charLiteralLength{"L006", "5.7"};
// every character outside a literal has to start a token
inline constexpr Code unknownCharacter{"L007", "1"};
// touching operators need whitespace when either is a flow token, a pipe, a compound, or an extra operator
inline constexpr Code operatorSpacing{"L008", "1"};
}
