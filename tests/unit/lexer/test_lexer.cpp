#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <deque>
#include <set>

#include "lexer.hpp"

// deque of constant-address managers since token text fields are views into the manager buffer
static std::deque<SourceManager> managers;

// lexes a string with a manager of its own, which outlives the tokens that point into it, and reports whether it had errors and how many
static std::vector<Token> lexStr(const std::string& src, bool* hadErr = nullptr, size_t* errCount = nullptr) {
    managers.emplace_back();
    managers.back().loadString(src);
    DiagnosticEngine de(managers.back());
    Lexer lx(de);
    auto toks = lx.lex();
    if (hadErr) *hadErr = de.hadErrors();
    if (errCount) *errCount = de.errorCount();
    return toks;
}

// creates a vector of the kinds of tokens in input vector ignoring EOF
static std::vector<TokenKind> kindsOf(const std::vector<Token>& toks) {
    std::vector<TokenKind> ks;
    for (auto& t : toks) if (t.kind != TokenKind::Eof) ks.push_back(t.kind);
    return ks;
}

// checks if a vector has a token with the given type and text
static bool has(const std::vector<Token>& toks, TokenKind k, std::string_view text) {
    for (auto& t : toks) if (t.kind == k && t.text == text) return true;
    return false;
}

// lexes the string and returns how many errors it reported
static size_t errorsOf(const std::string& src) {
    size_t n = 0;
    lexStr(src, nullptr, &n);
    return n;
}

// collects every way to write the spelling as a sequence of built-in (non-extra, non-compound) operators
static void splitsOf(std::string_view s, std::vector<const OpInfo*>& cur, std::vector<std::vector<const OpInfo*>>& out) {
    if (s.empty()) { out.push_back(cur); return; }
    for (size_t len = 1; len <= s.size(); len++) {
        auto op = lookupOperator(s.substr(0, len));
        if (op == nullptr || op->category == OpCategory::Extra || op->category == OpCategory::Compound) continue;
        cur.push_back(op);
        splitsOf(s.substr(len), cur, out);
        cur.pop_back();
    }
}

using K = TokenKind;

// checks if the operators lex correctly and don't stop prematurely
TEST_CASE("Lexer: operators lex with maximal munch", "[lexer]") {
    CHECK(kindsOf(lexStr("<<<-")) == std::vector<K>{K::CompoundAssign});
    CHECK(kindsOf(lexStr("<<<"))  == std::vector<K>{K::ExtraOp});
    CHECK(kindsOf(lexStr("<<"))   == std::vector<K>{K::Shl});
    CHECK(kindsOf(lexStr("<=>"))  == std::vector<K>{K::ExtraOp});
    CHECK(kindsOf(lexStr("<="))   == std::vector<K>{K::Le});
    CHECK(kindsOf(lexStr("<-"))   == std::vector<K>{K::LArrow});
    CHECK(kindsOf(lexStr("<|"))   == std::vector<K>{K::PipeBack});
    CHECK(kindsOf(lexStr("<|>"))  == std::vector<K>{K::ExtraOp});
    CHECK(kindsOf(lexStr("<"))    == std::vector<K>{K::Lt});
    CHECK(kindsOf(lexStr("->"))   == std::vector<K>{K::RArrow});
    CHECK(kindsOf(lexStr("-|"))   == std::vector<K>{K::Yield});
    CHECK(kindsOf(lexStr("--"))   == std::vector<K>{K::ExtraOp});
    CHECK(kindsOf(lexStr("-"))    == std::vector<K>{K::Minus});
    CHECK(kindsOf(lexStr("!=="))  == std::vector<K>{K::NeEq});
    CHECK(kindsOf(lexStr("!="))   == std::vector<K>{K::Ne});
    CHECK(kindsOf(lexStr("!"))    == std::vector<K>{K::Bang});
    CHECK(kindsOf(lexStr("|>"))   == std::vector<K>{K::PipeFwd});
    CHECK(kindsOf(lexStr("a<-b")) == (std::vector<K>{K::Identifier, K::LArrow, K::Identifier}));
    CHECK(kindsOf(lexStr("x+<-1")) == (std::vector<K>{K::Identifier, K::CompoundAssign, K::IntLiteral}));
}

// check if operator pairs and triples lex as themselves
TEST_CASE("Lexer: single, double, and triple operators are distinct", "[lexer]") {
    CHECK(kindsOf(lexStr("&"))   == std::vector<K>{K::Amp});
    CHECK(kindsOf(lexStr("&&"))  == std::vector<K>{K::AmpAmp});
    CHECK(kindsOf(lexStr("&&&")) == std::vector<K>{K::ExtraOp});
    CHECK(kindsOf(lexStr("|"))   == std::vector<K>{K::Bar});
    CHECK(kindsOf(lexStr("||"))  == std::vector<K>{K::BarBar});
    CHECK(kindsOf(lexStr("|||")) == std::vector<K>{K::ExtraOp});
    CHECK(kindsOf(lexStr("^"))   == std::vector<K>{K::Caret});
    CHECK(kindsOf(lexStr("^^"))  == std::vector<K>{K::CaretCaret});
    CHECK(kindsOf(lexStr("^^^")) == std::vector<K>{K::ExtraOp});
    CHECK(kindsOf(lexStr("*"))   == std::vector<K>{K::Star});
    CHECK(kindsOf(lexStr("**"))  == std::vector<K>{K::ExtraOp});
    CHECK(kindsOf(lexStr("***")) == std::vector<K>{K::ExtraOp});
    CHECK(kindsOf(lexStr("="))   == std::vector<K>{K::Eq});
    CHECK(kindsOf(lexStr("=="))  == std::vector<K>{K::EqEq});
    CHECK(kindsOf(lexStr("===")) == (std::vector<K>{K::EqEq, K::Eq}));
}

// checks if macro-related tokens lex correctly
TEST_CASE("Lexer: macro tokens @ and $", "[lexer]") {
    CHECK(kindsOf(lexStr("@"))  == std::vector<K>{K::At});
    CHECK(kindsOf(lexStr("$"))  == std::vector<K>{K::Dollar});
    CHECK(kindsOf(lexStr("@foo")) == (std::vector<K>{K::At, K::Identifier}));
    CHECK(kindsOf(lexStr("$x"))   == (std::vector<K>{K::Dollar, K::Identifier}));
    CHECK(kindsOf(lexStr("@tier(strict);")) == (std::vector<K>{
        K::At, K::Identifier, K::LParen, K::Identifier, K::RParen, K::Semi}));
}

// checks if flow, annotation, and the rest of the punctuation lex correctly
TEST_CASE("Lexer: flow, annotation, and punctuation tokens", "[lexer]") {
    CHECK(kindsOf(lexStr(":"))   == std::vector<K>{K::Colon});
    CHECK(kindsOf(lexStr("::"))  == std::vector<K>{K::ColonColon});
    CHECK(kindsOf(lexStr("."))   == std::vector<K>{K::Dot});
    CHECK(kindsOf(lexStr("..."))  == std::vector<K>{K::Ellipsis});
    CHECK(kindsOf(lexStr(".."))   == (std::vector<K>{K::Dot, K::Dot}));
    CHECK(kindsOf(lexStr("_"))    == std::vector<K>{K::Underscore});
    CHECK(has(lexStr("_x"), K::Identifier, "_x"));
    CHECK(has(lexStr("x_"), K::Identifier, "x_"));
    CHECK(kindsOf(lexStr("let x :: int <- 5;")) == (std::vector<K>{
        K::KwLet, K::Identifier, K::ColonColon, K::TypeName, K::LArrow, K::IntLiteral, K::Semi}));
    CHECK(kindsOf(lexStr("let v :: mut arr[16] char <- _;")) == (std::vector<K>{
        K::KwLet, K::Identifier, K::ColonColon, K::KwMut, K::KwArr, K::LBracket, K::IntLiteral, K::RBracket,
        K::TypeName, K::LArrow, K::Underscore, K::Semi}));
    CHECK(kindsOf(lexStr("[x::1.0]")) == (std::vector<K>{
        K::LBracket, K::Identifier, K::ColonColon, K::FloatLiteral, K::RBracket}));
    CHECK(kindsOf(lexStr("[0, ..., <n]")) == (std::vector<K>{
        K::LBracket, K::IntLiteral, K::Comma, K::Ellipsis, K::Comma, K::Lt, K::Identifier, K::RBracket}));
}

// tests the lexing of int and float literals across the four bases with fractional values and underscore separators
TEST_CASE("Lexer: numeric literals: bases, separators, fractions", "[lexer]") {
    CHECK(has(lexStr("42"),      K::IntLiteral, "42"));
    CHECK(has(lexStr("0b1010"),  K::IntLiteral, "0b1010"));
    CHECK(has(lexStr("0o17"),    K::IntLiteral, "0o17"));
    CHECK(has(lexStr("0O7_7"),   K::IntLiteral, "0O7_7"));
    CHECK(has(lexStr("0xFF"),    K::IntLiteral, "0xFF"));
    CHECK(has(lexStr("0xdead"),  K::IntLiteral, "0xdead"));
    CHECK(has(lexStr("1_000_000"), K::IntLiteral, "1_000_000"));
    CHECK(has(lexStr("1__0_"),   K::IntLiteral, "1__0_"));
    CHECK(has(lexStr("0b1010_0101"), K::IntLiteral, "0b1010_0101"));
    CHECK(has(lexStr("3.14"),   K::FloatLiteral, "3.14"));
    CHECK(has(lexStr("0.5"),    K::FloatLiteral, "0.5"));
    CHECK(has(lexStr("1_0.2_5"),K::FloatLiteral, "1_0.2_5"));
    CHECK(has(lexStr("0x1.8"),  K::FloatLiteral, "0x1.8"));
    CHECK(has(lexStr("0b1.1"),  K::FloatLiteral, "0b1.1"));
    CHECK(has(lexStr("0o1.4"),  K::FloatLiteral, "0o1.4"));
    CHECK(has(lexStr("0B42"),  K::Error, "0B42"));
    CHECK(has(lexStr("0bb1"),  K::Error, "0bb1"));
    CHECK(has(lexStr("0o8"),   K::Error, "0o8"));
    CHECK(has(lexStr("0XX3"),  K::Error, "0XX3"));
    CHECK(has(lexStr("0o78"),  K::Error, "0o78"));
    CHECK(has(lexStr("123abc"), K::Error, "123abc"));
    CHECK(has(lexStr("0B10.012"), K::Error, "0B10.012"));
    CHECK(has(lexStr("0xb3.1z"), K::Error, "0xb3.1z"));
    CHECK(errorsOf("0o78") == 1);
    CHECK(errorsOf("let x <- 123abc;") == 1);
}

// ensures that a number followed by a dot lex separately unless followed by another digit
TEST_CASE("Lexer: dot after number is member-access unless a digit follows", "[lexer]") {
    CHECK(kindsOf(lexStr("1.foo")) == (std::vector<K>{K::IntLiteral, K::Dot, K::Identifier}));
    CHECK(kindsOf(lexStr("1.")) == (std::vector<K>{K::IntLiteral, K::Dot}));
    CHECK(kindsOf(lexStr("3.14")) == std::vector<K>{K::FloatLiteral});
    CHECK(kindsOf(lexStr("1...")) == (std::vector<K>{K::IntLiteral, K::Ellipsis}));
}

// checks if string and char literals lex correctly despite escapes and that comments don't alter lexing
TEST_CASE("Lexer: strings, chars, escapes, and comments", "[lexer]") {
    CHECK(has(lexStr("\"a\\\"b\""), K::StringLiteral, "\"a\\\"b\""));
    CHECK(has(lexStr("'x'"), K::CharLiteral, "'x'"));
    CHECK(has(lexStr("'\\n'"), K::CharLiteral, "'\\n'"));
    CHECK(kindsOf(lexStr("42 # this is a comment\n7")) == (std::vector<K>{K::IntLiteral, K::IntLiteral}));
    CHECK(kindsOf(lexStr("42 # trailing")) == std::vector<K>{K::IntLiteral});
    CHECK(kindsOf(lexStr("42# no space needed")) == std::vector<K>{K::IntLiteral});
    CHECK(kindsOf(lexStr("a // b")) == (std::vector<K>{K::Identifier, K::ExtraOp, K::Identifier}));
    CHECK(kindsOf(lexStr("/* a */")) == (std::vector<K>{K::Slash, K::Star, K::Identifier, K::Star, K::Slash}));
}

// checks the production of error tokens without stopping to lex
TEST_CASE("Lexer: errors produce Error tokens and lexing continues", "[lexer]") {
    bool err = false;
    auto toks = lexStr("a ? b", &err);
    REQUIRE(err);
    CHECK(kindsOf(toks) == (std::vector<K>{K::Identifier, K::Error, K::Identifier}));
    err = false;
    auto toks2 = lexStr("\"oops", &err);
    REQUIRE(err);
    CHECK(has(toks2, K::Error, "\"oops"));
    err = false;
    lexStr("0x", &err);
    REQUIRE(err);
    err = false;
    auto toks3 = lexStr("a \\ b ` c", &err);
    REQUIRE(err);
    CHECK(kindsOf(toks3) == (std::vector<K>{K::Identifier, K::Error, K::Identifier, K::Error, K::Identifier}));
    // a non-ASCII character is one error however many bytes it takes
    CHECK(kindsOf(lexStr("a \xE2\x82\xAC b")) == (std::vector<K>{K::Identifier, K::Error, K::Identifier}));
    CHECK(errorsOf("a \xE2\x82\xAC b") == 1);
}

// ensures that each keyword and built-in type lexes as either itself or a typename, and that words other languages reserve are identifiers here
TEST_CASE("Lexer: every keyword and built-in type lexes as its own kind", "[lexer]") {
    CHECK(has(lexStr("let"),K::KwLet,"let"));
    CHECK(has(lexStr("type"),K::KwType,"type"));
    CHECK(has(lexStr("export"),K::KwExport,"export"));
    CHECK(has(lexStr("import"),K::KwImport,"import"));
    CHECK(has(lexStr("match"),K::KwMatch,"match"));
    CHECK(has(lexStr("repeat"),K::KwRepeat,"repeat"));
    CHECK(has(lexStr("chunk"),K::KwChunk,"chunk"));
    CHECK(has(lexStr("unscoped"),K::KwUnscoped,"unscoped"));
    CHECK(has(lexStr("peek"),K::KwPeek,"peek"));
    CHECK(has(lexStr("mut"),K::KwMut,"mut"));
    CHECK(has(lexStr("ptr"),K::KwPtr,"ptr"));
    CHECK(has(lexStr("arr"),K::KwArr,"arr"));
    CHECK(has(lexStr("take"),K::KwTake,"take"));
    CHECK(has(lexStr("swap"),K::KwSwap,"swap"));
    CHECK(has(lexStr("replace"),K::KwReplace,"replace"));
    CHECK(has(lexStr("wit"),K::KwWit,"wit"));
    CHECK(has(lexStr("assert"),K::KwAssert,"assert"));
    CHECK(has(lexStr("old"),K::KwOld,"old"));
    CHECK(has(lexStr("true"),K::KwTrue,"true"));
    CHECK(has(lexStr("false"),K::KwFalse,"false"));
    CHECK(has(lexStr("macro"),K::KwMacro,"macro"));
    CHECK(has(lexStr("moya"),K::KwMoya,"moya"));
    for (const char* ty : {
        "i8","i16","i32","i64","i128","i8u","i16u","i32u","i64u","i128u",
        "i8w","i16w","i32w","i64w","i128w","i8uw","i16uw","i32uw","i64uw","i128uw",
        "f8","f8e4m3","f8e5m2","f16","f16e8m7","f32","f64","f128",
        "f8e4m3n","f8e5m2n","f16n","f16e8m7n","f32n","f64n","f128n",
        "int","float","bool","char","rune","boolf"
    }) CHECK(has(lexStr(ty), K::TypeName, ty));
    for (const char* word : {
        "if","else","fun","def","const","fixed","loose","owned","class","struct","interface",
        "return","break","continue","defer","nullptr","extern","Self","f8n","f8e4m3nn",
    }) CHECK(has(lexStr(word), K::Identifier, word));
    CHECK(has(lexStr("i64wu"), K::Identifier, "i64wu"));
    CHECK(has(lexStr("u64"),   K::Identifier, "u64"));
    CHECK(has(lexStr("i7"),    K::Identifier, "i7"));
    CHECK(has(lexStr("types"), K::Identifier, "types"));
    CHECK(has(lexStr("oldest"),K::Identifier, "oldest"));
}

// checks the levels and associativity the parser will read out of the operator table
TEST_CASE("Lexer: operator table holds the precedence ladder", "[lexer]") {
    auto level = [](const char* s) { return lookupOperator(s)->level; };
    auto assoc = [](const char* s) { return lookupOperator(s)->assoc; };
    CHECK(level("<-") == 1);
    CHECK((level("**") == 2 && assoc("**") == Assoc::Right));
    CHECK((lookupOperator("-")->prefixLevel == 3 && lookupOperator("!")->prefixLevel == 3 && lookupOperator("~")->prefixLevel == 3));
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
    CHECK((lookupOperator("==")->isOverloadable && !lookupOperator("=")->isOverloadable && !lookupOperator("&&")->isOverloadable));
}

// lexes every generated compound between two operands and checks that it is one token pointing back at its base
TEST_CASE("Lexer: every generated compound token lexes as one token", "[lexer]") {
    size_t compounds = 0;
    for (auto& e : operatorTable()) {
        if (e.category != OpCategory::Compound) continue;
        compounds++;
        REQUIRE(e.base != nullptr);
        CHECK(e.spelling == e.base->spelling + "<-");
        bool err = true;
        auto toks = lexStr("x " + e.spelling + " 1", &err);
        CHECK_FALSE(err);
        REQUIRE(kindsOf(toks) == (std::vector<K>{K::Identifier, K::CompoundAssign, K::IntLiteral}));
        CHECK(toks[1].op == &e);
    }
    CHECK(compounds > 0);
    for (const char* s : {"+<-", "-<-", "*<-", "/<-", "%<-", "&<-", "|<-", "^<-", "<<<-", ">><-",
                          "**<-", "<+><-", "|*|<-", "--<-", "&&&<-", "<<<<-"}) {
        CHECK(lookupOperator(s) != nullptr);
    }
    for (const char* s : {"==<-", "=<-", "<=><-", "<<-", "&&<-", "!<-", "~<-", ">>=<-", "<<><-"}) {
        CHECK(lookupOperator(s) == nullptr);
    }
}

// checks the extra operators against their constraints, and that only the known ones could also be read as a valid split
TEST_CASE("Lexer: extra operators are valid and their splits are known", "[lexer]") {
    std::set<std::string> spellings;
    std::set<std::string> validSplits;
    for (auto& e : operatorTable()) {
        CHECK(spellings.insert(e.spelling).second);
        if (e.category != OpCategory::Extra) continue;
        CHECK(e.spelling.size() <= 3);
        CHECK(e.isOverloadable);
        CHECK(e.level > 0);
        CHECK(e.prefixLevel == 0);
        for (const char* bad : {"<-", "->", "-|", "::", "(", ")", "[", "]", "{", "}"}) {
            CHECK(e.spelling.find(bad) == std::string::npos);
        }
        // a split could be valid as a binary operator followed by prefix operators
        std::vector<const OpInfo*> cur;
        std::vector<std::vector<const OpInfo*>> splits;
        splitsOf(e.spelling, cur, splits);
        for (auto& sp : splits) {
            if (sp.size() < 2 || sp[0]->level == 0) continue;
            bool restPrefix = std::all_of(sp.begin() + 1, sp.end(), [](const OpInfo* o) { return o->prefixLevel > 0; });
            if (restPrefix) validSplits.insert(e.spelling);
        }
    }
    CHECK(validSplits == std::set<std::string>{"--", "<~"});
    CHECK(kindsOf(lexStr("a--b"))  == (std::vector<K>{K::Identifier, K::ExtraOp, K::Identifier}));
    CHECK(kindsOf(lexStr("a - -b")) == (std::vector<K>{K::Identifier, K::Minus, K::Minus, K::Identifier}));
    CHECK(kindsOf(lexStr("a<~b"))  == (std::vector<K>{K::Identifier, K::ExtraOp, K::Identifier}));
    CHECK(kindsOf(lexStr("a < ~b")) == (std::vector<K>{K::Identifier, K::Lt, K::Tilde, K::Identifier}));
}

// checks that spacing is only demanded between touching operators when one of them is a flow token, a pipe, a compound, or an extra operator
TEST_CASE("Lexer: spacing rule only applies between operators", "[lexer]") {
    CHECK(errorsOf("x<-1;") == 0);
    CHECK(errorsOf("a<+>b") == 0);
    CHECK(errorsOf("xs|>f") == 0);
    CHECK(errorsOf("a+-b") == 0);
    CHECK(errorsOf("a*-1") == 0);
    CHECK(errorsOf("!-x") == 0);
    CHECK(errorsOf("a ** -b") == 0);
    CHECK(errorsOf("(<+>, 0)[xs]") == 0);
    CHECK(errorsOf("[x::1.0]") == 0);
    CHECK(errorsOf("a**-b") == 1);
    CHECK(errorsOf("x <--1;") == 1);
    CHECK(errorsOf("f <|-x") == 1);
    CHECK(errorsOf("x +<-!y;") == 1);
    CHECK(errorsOf("0 -|-| f") == 1);
    bool err = false;
    auto toks = lexStr("a**-b", &err);
    REQUIRE(err);
    CHECK(kindsOf(toks) == (std::vector<K>{K::Identifier, K::ExtraOp, K::Minus, K::Identifier}));
}

// checks three different classes of operator touching a neighbor, which get one diagnostic each
TEST_CASE("Lexer: touching operators report one diagnostic each", "[lexer]") {
    const char* src =
        "let (a :: int, b :: int) -> f -> (r :: int) {\n"
        "    let x :: mut int <- a**-b;\n"
        "    x +<--1;\n"
        "    print <|-x;\n"
        "    x -|\n"
        "}\n";
    CHECK(errorsOf(src) == 3);
}

// checks that an exclusivity marker on a negative end needs a space, since <-1 lexes as a flow token, which the parser rejects
TEST_CASE("Lexer: a negative range end lexes <- unless it's spaced", "[lexer]") {
    bool err = true;
    auto spaced = lexStr("[0, ..., < -1]", &err);
    CHECK_FALSE(err);
    CHECK(kindsOf(spaced) == (std::vector<K>{
        K::LBracket, K::IntLiteral, K::Comma, K::Ellipsis, K::Comma, K::Lt, K::Minus, K::IntLiteral, K::RBracket}));
    err = true;
    auto unspaced = lexStr("[0, ..., <-1]", &err);
    CHECK_FALSE(err);
    CHECK(kindsOf(unspaced) == (std::vector<K>{
        K::LBracket, K::IntLiteral, K::Comma, K::Ellipsis, K::Comma, K::LArrow, K::IntLiteral, K::RBracket}));
}

// tests lexing a complete function and checking if it returns the expected tokens
TEST_CASE("Lexer: a complete function lexes to the expected sequence", "[lexer]") {
    const char* src = "let () -> main -> (code :: int) {\n    let x <- 42;\n    x -|\n}\n";
    CHECK(kindsOf(lexStr(src)) == (std::vector<K>{
        K::KwLet, K::LParen, K::RParen, K::RArrow, K::Identifier, K::RArrow,
        K::LParen, K::Identifier, K::ColonColon, K::TypeName, K::RParen, K::LBrace,
        K::KwLet, K::Identifier, K::LArrow, K::IntLiteral, K::Semi,
        K::Identifier, K::Yield, K::RBrace,
    }));
    const char* src2 = "let (xs :: peek arr mut i64u, p :: unscoped ptr Node) -> <+> -> (r :: mut arr i32) { }";
    auto ks = kindsOf(lexStr(src2));
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
    auto ks = kindsOf(lexStr(src));
    CHECK(ks.front() == K::KwMacro);
    CHECK(std::count(ks.begin(), ks.end(), K::KwMoya) == 1);
    CHECK(std::count(ks.begin(), ks.end(), K::Dollar) == 2);
}
