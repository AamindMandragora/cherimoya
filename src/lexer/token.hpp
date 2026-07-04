#pragma once

#include <string_view>
#include "source.hpp"

// operator table entry, defined in operators.hpp
struct OpInfo;

// every kind of token, where an operator's kind is the one its operator table entry gives it
enum class TokenKind {
    // malformed input, and the end of the file
    Error, Eof,
    // names like foo and main, and every built-in type spelling, like i64, i8uw, f16e8m7n, and char
    Identifier, TypeName,
    // literals, whose text is kept raw: 42 and 0xFF, 3.14 and 0x1.8, "hello", and '\t'
    IntLiteral, FloatLiteral, StringLiteral, CharLiteral,
    // declaration keywords
    KwLet, KwType, KwExport, KwImport,
    // scope keywords
    KwMatch, KwRepeat, KwChunk,
    // modes, qualifiers, and handles
    KwUnscoped, KwPeek, KwMut, KwPtr, KwArr,
    // place intrinsics
    KwTake, KwSwap, KwReplace,
    // verification keywords
    KwWit, KwAssert, KwOld,
    // the bool literals
    KwTrue, KwFalse,
    // macro keywords
    KwMacro, KwMoya,
    // flow: <- -> -|
    LArrow, RArrow, Yield,
    // pipes: |> <|
    PipeFwd, PipeBack,
    // arithmetic: + - * / %, where unary minus is Minus too
    Plus, Minus, Star, Slash, Percent,
    // bitwise: & | ^ ~ << >>
    Amp, Bar, Caret, Tilde, Shl, Shr,
    // logical: && || ^^ !
    AmpAmp, BarBar, CaretCaret, Bang,
    // comparison: < <= > >=
    Lt, Le, Gt, Ge,
    // equality: = and != are shallow, and == and !== are deep
    Eq, Ne, EqEq, NeEq,
    // what the operator table generates: every X<- and every extra operator
    CompoundAssign, ExtraOp,
    // macro tokens: @ $
    At, Dollar,
    // punctuation: ( ) [ ] { } , ; . : :: ... _
    LParen, RParen, LBracket, RBracket, LBrace, RBrace,
    Comma, Semi, Dot, Colon, ColonColon, Ellipsis, Underscore,
};

// tokens hold their kind, the actual text, their location, and their operator table entry if they're an operator
struct Token {
    TokenKind kind;
    std::string_view text;
    SourceLoc loc;
    const OpInfo* op;
    Token(TokenKind k, std::string_view t, SourceLoc l, const OpInfo* o = nullptr) : kind(k), text(t), loc(l), op(o) {}
};
