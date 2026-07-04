#include <catch2/catch_test_macros.hpp>

#include <deque>
#include <algorithm>
#include <set>

#include "lexer.hpp"

// deque of constant-address managers since token text fields are views into the manager buffer
static std::deque<SourceManager> g_managers;

// creates a new manager, loads the string, creates diagnostic engine and lexer, then lexes the tokens and prints any error messages
static std::vector<Token> lex_str(const std::string& src, bool* had_err = nullptr, size_t* err_count = nullptr) {
    g_managers.emplace_back();
    g_managers.back().load_string(src);
    DiagnosticEngine de(g_managers.back());
    Lexer lx(de);
    auto toks = lx.lex();
    if (had_err) *had_err = de.had_errors();
    if (err_count) *err_count = de.error_count();
    return toks;
}

// creates a vector of the kinds of tokens in input vector ignoring EOF
static std::vector<TokenKind> kinds_of(const std::vector<Token>& toks) {
    std::vector<TokenKind> ks;
    for (auto& t : toks) if (t.kind != TokenKind::Eof) ks.push_back(t.kind);
    return ks;
}

// checks if a vector has a token with the given type and text
static bool has(const std::vector<Token>& toks, TokenKind k, std::string_view text) {
    for (auto& t : toks) if (t.kind == k && t.text == text) return true;
    return false;
}

// lexes the string and returns how many diagnostics it produced
static size_t errors_of(const std::string& src) {
    size_t n = 0;
    lex_str(src, nullptr, &n);
    return n;
}

// collects every way to write the spelling as a sequence of built-in (non-extra, non-compound) operators
static void splits_of(std::string_view s, std::vector<const OpInfo*>& cur, std::vector<std::vector<const OpInfo*>>& out) {
    if (s.empty()) { out.push_back(cur); return; }
    for (size_t len = 1; len <= s.size(); len++) {
        auto op = lookup_operator(s.substr(0, len));
        if (op == nullptr || op->category == OpCategory::Extra || op->category == OpCategory::Compound) continue;
        cur.push_back(op);
        splits_of(s.substr(len), cur, out);
        cur.pop_back();
    }
}

using K = TokenKind;

// checks if the operators lex correctly and don't stop prematurely
TEST_CASE("Lexer: operators lex with maximal munch", "[lexer]") {
    CHECK(kinds_of(lex_str("<<<-")) == std::vector<K>{K::CompoundAssign});
    CHECK(kinds_of(lex_str("<<<"))  == std::vector<K>{K::ExtraOp});
    CHECK(kinds_of(lex_str("<<"))   == std::vector<K>{K::Shl});
    CHECK(kinds_of(lex_str("<=>"))  == std::vector<K>{K::ExtraOp});
    CHECK(kinds_of(lex_str("<="))   == std::vector<K>{K::Le});
    CHECK(kinds_of(lex_str("<-"))   == std::vector<K>{K::LArrow});
    CHECK(kinds_of(lex_str("<|"))   == std::vector<K>{K::PipeBack});
    CHECK(kinds_of(lex_str("<|>"))  == std::vector<K>{K::ExtraOp});
    CHECK(kinds_of(lex_str("<"))    == std::vector<K>{K::Lt});
    CHECK(kinds_of(lex_str("->"))   == std::vector<K>{K::RArrow});
    CHECK(kinds_of(lex_str("-|"))   == std::vector<K>{K::Yield});
    CHECK(kinds_of(lex_str("--"))   == std::vector<K>{K::ExtraOp});
    CHECK(kinds_of(lex_str("-"))    == std::vector<K>{K::Minus});
    CHECK(kinds_of(lex_str("!=="))  == std::vector<K>{K::NeEq});
    CHECK(kinds_of(lex_str("!="))   == std::vector<K>{K::Ne});
    CHECK(kinds_of(lex_str("!"))    == std::vector<K>{K::Bang});
    CHECK(kinds_of(lex_str("|>"))   == std::vector<K>{K::PipeFwd});
    CHECK(kinds_of(lex_str("a<-b")) == (std::vector<K>{K::Identifier, K::LArrow, K::Identifier}));
    CHECK(kinds_of(lex_str("x+<-1")) == (std::vector<K>{K::Identifier, K::CompoundAssign, K::IntLiteral}));
}

// check if operator pairs and triples lex as themselves
TEST_CASE("Lexer: single, double, and triple operators are distinct", "[lexer]") {
    CHECK(kinds_of(lex_str("&"))   == std::vector<K>{K::Amp});
    CHECK(kinds_of(lex_str("&&"))  == std::vector<K>{K::AmpAmp});
    CHECK(kinds_of(lex_str("&&&")) == std::vector<K>{K::ExtraOp});
    CHECK(kinds_of(lex_str("|"))   == std::vector<K>{K::Pipe});
    CHECK(kinds_of(lex_str("||"))  == std::vector<K>{K::PipePipe});
    CHECK(kinds_of(lex_str("|||")) == std::vector<K>{K::ExtraOp});
    CHECK(kinds_of(lex_str("^"))   == std::vector<K>{K::Caret});
    CHECK(kinds_of(lex_str("^^"))  == std::vector<K>{K::CaretCaret});
    CHECK(kinds_of(lex_str("^^^")) == std::vector<K>{K::ExtraOp});
    CHECK(kinds_of(lex_str("*"))   == std::vector<K>{K::Star});
    CHECK(kinds_of(lex_str("**"))  == std::vector<K>{K::ExtraOp});
    CHECK(kinds_of(lex_str("***")) == std::vector<K>{K::ExtraOp});
    CHECK(kinds_of(lex_str("="))   == std::vector<K>{K::Eq});
    CHECK(kinds_of(lex_str("=="))  == std::vector<K>{K::EqEq});
    CHECK(kinds_of(lex_str("===")) == (std::vector<K>{K::EqEq, K::Eq}));
}

// checks if macro-related tokens lex correctly
TEST_CASE("Lexer: macro tokens @ and $", "[lexer]") {
    CHECK(kinds_of(lex_str("@"))  == std::vector<K>{K::At});
    CHECK(kinds_of(lex_str("$"))  == std::vector<K>{K::Buck});
    CHECK(kinds_of(lex_str("@foo")) == (std::vector<K>{K::At, K::Identifier}));
    CHECK(kinds_of(lex_str("$x"))   == (std::vector<K>{K::Buck, K::Identifier}));
    CHECK(kinds_of(lex_str("@tier(strict);")) == (std::vector<K>{
        K::At, K::Identifier, K::LParen, K::Identifier, K::RParen, K::Semi}));
}

// checks if flow, annotation, and the rest of the punctuation lex correctly
TEST_CASE("Lexer: flow, annotation, and punctuation tokens", "[lexer]") {
    CHECK(kinds_of(lex_str(":"))   == std::vector<K>{K::Colon});
    CHECK(kinds_of(lex_str("::"))  == std::vector<K>{K::ColonColon});
    CHECK(kinds_of(lex_str("."))   == std::vector<K>{K::Dot});
    CHECK(kinds_of(lex_str("..."))  == std::vector<K>{K::Ellipsis});
    CHECK(kinds_of(lex_str(".."))   == (std::vector<K>{K::Dot, K::Dot}));
    CHECK(kinds_of(lex_str("_"))    == std::vector<K>{K::Underscore});
    CHECK(has(lex_str("_x"), K::Identifier, "_x"));
    CHECK(has(lex_str("x_"), K::Identifier, "x_"));
    CHECK(kinds_of(lex_str("let x :: int <- 5;")) == (std::vector<K>{
        K::KwLet, K::Identifier, K::ColonColon, K::TypeName, K::LArrow, K::IntLiteral, K::Semi}));
    CHECK(kinds_of(lex_str("let v :: mut arr[16] char <- _;")) == (std::vector<K>{
        K::KwLet, K::Identifier, K::ColonColon, K::KwMut, K::KwArr, K::LBracket, K::IntLiteral, K::RBracket,
        K::TypeName, K::LArrow, K::Underscore, K::Semi}));
    CHECK(kinds_of(lex_str("[x::1.0]")) == (std::vector<K>{
        K::LBracket, K::Identifier, K::ColonColon, K::FloatLiteral, K::RBracket}));
    CHECK(kinds_of(lex_str("[0, ..., <n]")) == (std::vector<K>{
        K::LBracket, K::IntLiteral, K::Comma, K::Ellipsis, K::Comma, K::Lt, K::Identifier, K::RBracket}));
}

// tests the lexing of int and float literals across the four bases with fractional values and underscore separators
TEST_CASE("Lexer: numeric literals: bases, separators, fractions", "[lexer]") {
    CHECK(has(lex_str("42"),      K::IntLiteral, "42"));
    CHECK(has(lex_str("0b1010"),  K::IntLiteral, "0b1010"));
    CHECK(has(lex_str("0o17"),    K::IntLiteral, "0o17"));
    CHECK(has(lex_str("0O7_7"),   K::IntLiteral, "0O7_7"));
    CHECK(has(lex_str("0o78"),    K::IntLiteral, "0o7"));
    CHECK(has(lex_str("0xFF"),    K::IntLiteral, "0xFF"));
    CHECK(has(lex_str("0xdead"),  K::IntLiteral, "0xdead"));
    CHECK(has(lex_str("1_000_000"), K::IntLiteral, "1_000_000"));
    CHECK(has(lex_str("1__0_"),   K::IntLiteral, "1__0_"));
    CHECK(has(lex_str("0b1010_0101"), K::IntLiteral, "0b1010_0101"));
    CHECK(has(lex_str("123abc"),  K::IntLiteral, "123"));
    CHECK(has(lex_str("3.14"),   K::FloatLiteral, "3.14"));
    CHECK(has(lex_str("0.5"),    K::FloatLiteral, "0.5"));
    CHECK(has(lex_str("1_0.2_5"),K::FloatLiteral, "1_0.2_5"));
    CHECK(has(lex_str("0x1.8"),  K::FloatLiteral, "0x1.8"));
    CHECK(has(lex_str("0b1.1"),  K::FloatLiteral, "0b1.1"));
    CHECK(has(lex_str("0o1.4"),  K::FloatLiteral, "0o1.4"));
    CHECK(has(lex_str("0B10.012"),  K::FloatLiteral, "0B10.01"));
    CHECK(has(lex_str("0xb3.1z"),  K::FloatLiteral, "0xb3.1"));
    CHECK(has(lex_str("0B42"),  K::Error, "0B"));
    CHECK(has(lex_str("0bb1"),  K::Error, "0b"));
    CHECK(has(lex_str("0o8"),   K::Error, "0o"));
    CHECK(has(lex_str("0XX3"),  K::Error, "0X"));
}

// ensures that a number followed by a dot lex separately unless followed by another digit
TEST_CASE("Lexer: dot after number is member-access unless a digit follows", "[lexer]") {
    CHECK(kinds_of(lex_str("1.foo")) == (std::vector<K>{K::IntLiteral, K::Dot, K::Identifier}));
    CHECK(kinds_of(lex_str("1.")) == (std::vector<K>{K::IntLiteral, K::Dot}));
    CHECK(kinds_of(lex_str("3.14")) == std::vector<K>{K::FloatLiteral});
    CHECK(kinds_of(lex_str("1...")) == (std::vector<K>{K::IntLiteral, K::Ellipsis}));
}

// checks if string and char literals lex correctly despite escapes and that comments don't alter lexing
TEST_CASE("Lexer: strings, chars, escapes, and comments", "[lexer]") {
    CHECK(has(lex_str("\"a\\\"b\""), K::StringLiteral, "\"a\\\"b\""));
    CHECK(has(lex_str("'x'"), K::CharLiteral, "'x'"));
    CHECK(has(lex_str("'\\n'"), K::CharLiteral, "'\\n'"));
    CHECK(kinds_of(lex_str("42 # this is a comment\n7")) == (std::vector<K>{K::IntLiteral, K::IntLiteral}));
    CHECK(kinds_of(lex_str("42 # trailing")) == std::vector<K>{K::IntLiteral});
    CHECK(kinds_of(lex_str("42# no space needed")) == std::vector<K>{K::IntLiteral});
    CHECK(kinds_of(lex_str("a // b")) == (std::vector<K>{K::Identifier, K::ExtraOp, K::Identifier}));
    CHECK(kinds_of(lex_str("/* a */")) == (std::vector<K>{K::Slash, K::Star, K::Identifier, K::Star, K::Slash}));
}

// checks the production of error tokens without stopping to lex
TEST_CASE("Lexer: errors produce Error tokens and lexing continues", "[lexer]") {
    bool err = false;
    auto toks = lex_str("a ? b", &err);
    REQUIRE(err);
    CHECK(kinds_of(toks) == (std::vector<K>{K::Identifier, K::Error, K::Identifier}));
    err = false;
    auto toks2 = lex_str("\"oops", &err);
    REQUIRE(err);
    CHECK(has(toks2, K::Error, "\"oops"));
    err = false;
    lex_str("0x", &err);
    REQUIRE(err);
    err = false;
    auto toks3 = lex_str("a \\ b ` c", &err);
    REQUIRE(err);
    CHECK(kinds_of(toks3) == (std::vector<K>{K::Identifier, K::Error, K::Identifier, K::Error, K::Identifier}));
}

// ensures that each keyword and built-in type lexes as either itself or a typename, and that removed words are identifiers
TEST_CASE("Lexer: every keyword and built-in type lexes as its own kind", "[lexer]") {
    CHECK(has(lex_str("let"),K::KwLet,"let"));
    CHECK(has(lex_str("type"),K::KwType,"type"));
    CHECK(has(lex_str("export"),K::KwExport,"export"));
    CHECK(has(lex_str("import"),K::KwImport,"import"));
    CHECK(has(lex_str("match"),K::KwMatch,"match"));
    CHECK(has(lex_str("repeat"),K::KwRepeat,"repeat"));
    CHECK(has(lex_str("chunk"),K::KwChunk,"chunk"));
    CHECK(has(lex_str("unscoped"),K::KwUnscoped,"unscoped"));
    CHECK(has(lex_str("peek"),K::KwPeek,"peek"));
    CHECK(has(lex_str("mut"),K::KwMut,"mut"));
    CHECK(has(lex_str("ptr"),K::KwPtr,"ptr"));
    CHECK(has(lex_str("arr"),K::KwArr,"arr"));
    CHECK(has(lex_str("take"),K::KwTake,"take"));
    CHECK(has(lex_str("swap"),K::KwSwap,"swap"));
    CHECK(has(lex_str("replace"),K::KwReplace,"replace"));
    CHECK(has(lex_str("wit"),K::KwWit,"wit"));
    CHECK(has(lex_str("assert"),K::KwAssert,"assert"));
    CHECK(has(lex_str("old"),K::KwOld,"old"));
    CHECK(has(lex_str("true"),K::KwTrue,"true"));
    CHECK(has(lex_str("false"),K::KwFalse,"false"));
    CHECK(has(lex_str("macro"),K::KwMacro,"macro"));
    CHECK(has(lex_str("moya"),K::KwMoya,"moya"));
    for (const char* ty : {
        "i8","i16","i32","i64","i128","i8u","i16u","i32u","i64u","i128u",
        "i8w","i16w","i32w","i64w","i128w","i8uw","i16uw","i32uw","i64uw","i128uw",
        "f8","f8e4m3","f8e5m2","f16","f16e8m7","f32","f64","f128",
        "f8e4m3n","f8e5m2n","f16n","f16e8m7n","f32n","f64n","f128n",
        "int","float","bool","char","rune","boolf"
    }) CHECK(has(lex_str(ty), K::TypeName, ty));
    for (const char* word : {
        "if","else","fun","def","const","fixed","loose","owned","class","struct","interface",
        "return","break","continue","defer","nullptr","extern","Self","f8n","f8e4m3nn",
    }) CHECK(has(lex_str(word), K::Identifier, word));
    CHECK(has(lex_str("i64wu"), K::Identifier, "i64wu"));
    CHECK(has(lex_str("u64"),   K::Identifier, "u64"));
    CHECK(has(lex_str("i7"),    K::Identifier, "i7"));
    CHECK(has(lex_str("types"), K::Identifier, "types"));
    CHECK(has(lex_str("oldest"),K::Identifier, "oldest"));
}

// checks the levels and associativity the parser will read out of the operator table
TEST_CASE("Lexer: operator table holds the precedence ladder", "[lexer]") {
    auto level = [](const char* s) { return lookup_operator(s)->level; };
    auto assoc = [](const char* s) { return lookup_operator(s)->assoc; };
    CHECK(level("<-") == 1);
    CHECK((level("**") == 2 && assoc("**") == Assoc::Right));
    CHECK((lookup_operator("-")->prefix_level == 3 && lookup_operator("!")->prefix_level == 3 && lookup_operator("~")->prefix_level == 3));
    CHECK((level("*") == 4 && level("<*>") == 4 && level("|*|") == 4 && level("//") == 4));
    CHECK((level("-") == 5 && level("<+>") == 5 && level("++") == 5 && level("+++") == 5 && level("|+|") == 5 && level("--") == 5));
    CHECK((level("<<") == 6 && level("<<<") == 6 && level(">>>") == 6));
    CHECK((level("&") == 7 && level("|&|") == 7));
    CHECK(level("^") == 8);
    CHECK((level("|") == 9 && level("<|>") == 9));
    CHECK((level("<") == 10 && assoc("<") == Assoc::Chain));
    CHECK((level("<=>") == 10 && assoc("<=>") == Assoc::None));
    CHECK((level("<<>") == 10 && assoc("<<>") == Assoc::None));
    CHECK((level("!==") == 11 && assoc("!==") == Assoc::None && level(">=>") == 11));
    CHECK((level("&&") == 12 && level("&&&") == 12));
    CHECK((level("^^") == 13 && level("^^^") == 13));
    CHECK((level("||") == 14 && level("|||") == 14));
    CHECK((level(">>=") == 15 && level("<~") == 15 && level("=<<") == 15 && assoc("=<<") == Assoc::Right));
    CHECK((level("|>") == 16 && assoc("|>") == Assoc::Left));
    CHECK((level("<|") == 17 && assoc("<|") == Assoc::Right));
    CHECK((lookup_operator("==")->overloadable && !lookup_operator("=")->overloadable && !lookup_operator("&&")->overloadable));
}

// lexes every generated compound between two operands and checks that it is one token pointing back at its base
TEST_CASE("Lexer: every generated compound token lexes as one token", "[lexer]") {
    size_t compounds = 0;
    for (auto& e : operator_table()) {
        if (e.category != OpCategory::Compound) continue;
        compounds++;
        REQUIRE(e.base != nullptr);
        CHECK(e.spelling == e.base->spelling + "<-");
        bool err = true;
        auto toks = lex_str("x " + e.spelling + " 1", &err);
        CHECK_FALSE(err);
        REQUIRE(kinds_of(toks) == (std::vector<K>{K::Identifier, K::CompoundAssign, K::IntLiteral}));
        CHECK(toks[1].op == &e);
    }
    CHECK(compounds > 0);
    for (const char* s : {"+<-", "-<-", "*<-", "/<-", "%<-", "&<-", "|<-", "^<-", "<<<-", ">><-",
                          "**<-", "<+><-", "|*|<-", "--<-", "&&&<-", "<<<<-"}) {
        CHECK(lookup_operator(s) != nullptr);
    }
    for (const char* s : {"==<-", "=<-", "<=><-", "<<-", "&&<-", "!<-", "~<-", ">>=<-", "<<><-"}) {
        CHECK(lookup_operator(s) == nullptr);
    }
}

// checks the extra operators against their constraints, and that only the known ones could also be read as a valid split
TEST_CASE("Lexer: extra operators are valid and their splits are known", "[lexer]") {
    std::set<std::string> spellings;
    std::set<std::string> valid_splits;
    for (auto& e : operator_table()) {
        CHECK(spellings.insert(e.spelling).second);
        if (e.category != OpCategory::Extra) continue;
        CHECK(e.spelling.size() <= 3);
        CHECK(e.overloadable);
        CHECK(e.level > 0);
        CHECK(e.prefix_level == 0);
        for (const char* bad : {"<-", "->", "-|", "::", "(", ")", "[", "]", "{", "}"}) {
            CHECK(e.spelling.find(bad) == std::string::npos);
        }
        // a split could be valid as a binary operator followed by prefix operators
        std::vector<const OpInfo*> cur;
        std::vector<std::vector<const OpInfo*>> splits;
        splits_of(e.spelling, cur, splits);
        for (auto& sp : splits) {
            if (sp.size() < 2 || sp[0]->level == 0) continue;
            bool rest_prefix = std::all_of(sp.begin() + 1, sp.end(), [](const OpInfo* o) { return o->prefix_level > 0; });
            if (rest_prefix) valid_splits.insert(e.spelling);
        }
    }
    CHECK(valid_splits == std::set<std::string>{"--", "<~"});
    CHECK(kinds_of(lex_str("a--b"))  == (std::vector<K>{K::Identifier, K::ExtraOp, K::Identifier}));
    CHECK(kinds_of(lex_str("a - -b")) == (std::vector<K>{K::Identifier, K::Minus, K::Minus, K::Identifier}));
    CHECK(kinds_of(lex_str("a<~b"))  == (std::vector<K>{K::Identifier, K::ExtraOp, K::Identifier}));
    CHECK(kinds_of(lex_str("a < ~b")) == (std::vector<K>{K::Identifier, K::Lt, K::Tilde, K::Identifier}));
}

// checks that spacing is only demanded between touching operators when one of them is a flow token, a pipe, a compound, or an extra operator
TEST_CASE("Lexer: spacing rule only applies between operators", "[lexer]") {
    CHECK(errors_of("x<-1;") == 0);
    CHECK(errors_of("a<+>b") == 0);
    CHECK(errors_of("xs|>f") == 0);
    CHECK(errors_of("a+-b") == 0);
    CHECK(errors_of("a*-1") == 0);
    CHECK(errors_of("!-x") == 0);
    CHECK(errors_of("a ** -b") == 0);
    CHECK(errors_of("(<+>, 0)[xs]") == 0);
    CHECK(errors_of("[x::1.0]") == 0);
    CHECK(errors_of("a**-b") == 1);
    CHECK(errors_of("x <--1;") == 1);
    CHECK(errors_of("f <|-x") == 1);
    CHECK(errors_of("x +<-!y;") == 1);
    CHECK(errors_of("0 -|-| f") == 1);
    bool err = false;
    auto toks = lex_str("a**-b", &err);
    REQUIRE(err);
    CHECK(kinds_of(toks) == (std::vector<K>{K::Identifier, K::ExtraOp, K::Minus, K::Identifier}));
}

// fail/operator_tokens: three different classes of operator touching a neighbor, one diagnostic each
TEST_CASE("Lexer: fail/operator_tokens reports three diagnostics", "[lexer]") {
    const char* src =
        "let (a :: int, b :: int) -> f -> (r :: int) {\n"
        "    let x :: mut int <- a**-b;\n"
        "    x +<--1;\n"
        "    print <|-x;\n"
        "    x -|\n"
        "}\n";
    CHECK(errors_of(src) == 3);
}

// fail/spacing_negative_end: an exclusivity marker on a negative end needs a space, since <-1 lexes as a flow token
TEST_CASE("Lexer: fail/spacing_negative_end lexes <- before the parser rejects it", "[lexer]") {
    bool err = true;
    auto spaced = lex_str("[0, ..., < -1]", &err);
    CHECK_FALSE(err);
    CHECK(kinds_of(spaced) == (std::vector<K>{
        K::LBracket, K::IntLiteral, K::Comma, K::Ellipsis, K::Comma, K::Lt, K::Minus, K::IntLiteral, K::RBracket}));
    err = true;
    auto unspaced = lex_str("[0, ..., <-1]", &err);
    CHECK_FALSE(err);
    CHECK(kinds_of(unspaced) == (std::vector<K>{
        K::LBracket, K::IntLiteral, K::Comma, K::Ellipsis, K::Comma, K::LArrow, K::IntLiteral, K::RBracket}));
}

// tests lexing a complete function and checking if it returns the expected tokens
TEST_CASE("Lexer: a complete function lexes to the expected sequence", "[lexer]") {
    const char* src = "let () -> main -> (code :: int) {\n    let x <- 42;\n    x -|\n}\n";
    CHECK(kinds_of(lex_str(src)) == (std::vector<K>{
        K::KwLet, K::LParen, K::RParen, K::RArrow, K::Identifier, K::RArrow,
        K::LParen, K::Identifier, K::ColonColon, K::TypeName, K::RParen, K::LBrace,
        K::KwLet, K::Identifier, K::LArrow, K::IntLiteral, K::Semi,
        K::Identifier, K::Yield, K::RBrace,
    }));
    const char* src2 = "let (xs :: peek arr mut i64u, p :: unscoped ptr Node) -> <+> -> (r :: mut arr i32) { }";
    auto ks = kinds_of(lex_str(src2));
    CHECK(ks.front() == K::KwLet);
    CHECK(std::count(ks.begin(), ks.end(), K::KwPeek) == 1);
    CHECK(std::count(ks.begin(), ks.end(), K::KwUnscoped) == 1);
    CHECK(std::count(ks.begin(), ks.end(), K::KwMut) == 2);
    CHECK(std::count(ks.begin(), ks.end(), K::KwArr) == 2);
    CHECK(std::count(ks.begin(), ks.end(), K::KwPtr) == 1);
    CHECK(std::count(ks.begin(), ks.end(), K::ExtraOp) == 1);
    CHECK(std::count(ks.begin(), ks.end(), K::RArrow) == 2);
}

// tests lexing a macro declaration
TEST_CASE("Lexer: macro declaration lexes correctly", "[lexer]") {
    const char* src = "macro (a :: expr, b :: expr) -> twice -> (stmt) { moya { $a; $b; } -| }";
    auto ks = kinds_of(lex_str(src));
    CHECK(ks.front() == K::KwMacro);
    CHECK(std::count(ks.begin(), ks.end(), K::KwMoya) == 1);
    CHECK(std::count(ks.begin(), ks.end(), K::Buck) == 2);
}
