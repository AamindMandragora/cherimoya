#include <catch2/catch_test_macros.hpp>

#include <string>

#include "parser.hpp"
#include "sema.hpp"

// checks input as a whole program and returns how many diagnostics lexing, parsing, and sema produced
static size_t semaErrors(const std::string& input) {
    SourceManager sm;
    sm.load_string(input);
    DiagnosticEngine diag(sm);
    Lexer lexer(diag);
    auto tokens = lexer.lex();
    Parser parser(tokens, diag);
    auto program = parser.parseProgram();
    Sema sema(diag);
    sema.check(program);
    return diag.error_count();
}

// checks input as a whole program and returns the type of the last symbol named name, or "" if there isn't one
static std::string symbolType(const std::string& input, std::string_view name) {
    SourceManager sm;
    sm.load_string(input);
    DiagnosticEngine diag(sm);
    Lexer lexer(diag);
    auto tokens = lexer.lex();
    Parser parser(tokens, diag);
    auto program = parser.parseProgram();
    Sema sema(diag);
    sema.check(program);
    std::string type;
    for (auto& s : sema.all_symbols()) if (s.name == name) type = s.type->to_string();
    return type;
}

// checks that bindings take their type from the annotation or the initializer, and that the two have to agree
TEST_CASE("Sema: binding types", "[sema]") {
    CHECK(symbolType("let x <- 5;", "x") == "int");
    CHECK(symbolType("let x <- 2.5;", "x") == "float");
    CHECK(symbolType("let x <- true;", "x") == "bool");
    CHECK(symbolType("let x :: int <- 5;", "x") == "int");
    CHECK(symbolType("let x :: i64 <- 5;", "x") == "int");
    CHECK(symbolType("let x :: f64 <- 1.0;", "x") == "float");
    CHECK(symbolType("let x :: mut <- 5;", "x") == "int");
    CHECK(symbolType("let x :: mut int <- _;", "x") == "int");
    CHECK(symbolType("let x <- 5; let y <- x;", "y") == "int");
    CHECK(semaErrors("let x :: bool <- 5;") == 1);
    CHECK(semaErrors("let x <- _;") == 1);
    CHECK(semaErrors("let x :: int <- 2.5;") == 1);
}

// checks that every name resolves, that one scope can't declare a name twice, and that inner scopes can shadow
TEST_CASE("Sema: name resolution and lexical scopes", "[sema]") {
    CHECK(semaErrors("let x <- y;") == 1);
    CHECK(semaErrors("let x <- 1; let x <- 2;") == 1);
    CHECK(semaErrors("let x <- 1; { let x <- true; let y :: bool <- x; }") == 0);
    CHECK(semaErrors("{ let y <- 1; } let z <- y;") == 1);
    CHECK(symbolType("let x <- 1; { let x <- true; let y <- x; }", "y") == "bool");
}

// checks the passes over a list of statements: functions are visible before they're defined, function bodies see every
// name around them, and lets elsewhere are only visible after
TEST_CASE("Sema: functions and the two-pass top level", "[sema]") {
    CHECK(symbolType("let (a :: int, b :: int) -> f -> (r :: bool) { }", "f") == "(int, int) : bool");
    CHECK(symbolType("let () -> f -> () { }", "f") == "() : void");
    CHECK(symbolType("let (a :: int) -> f -> (r :: int) { let y <- a; }", "y") == "int");
    CHECK(semaErrors("let () -> f -> () { h(); } let () -> h -> () { }") == 0);
    CHECK(semaErrors("let () -> f -> () { let y <- LIMIT; } let LIMIT <- 4;") == 0);
    CHECK(semaErrors("let y <- LIMIT; let LIMIT <- 4;") == 1);
    CHECK(semaErrors("let (a) -> f -> () { }") == 1);
    CHECK(semaErrors("let () -> f -> () { } let () -> f -> () { }") == 1);
    CHECK(semaErrors("let () -> f -> (r :: int) { let y <- r; }") == 1);
}

// checks that nested functions are hoisted in their block but can't reach the locals of the function around them
TEST_CASE("Sema: nested functions", "[sema]") {
    CHECK(semaErrors("let () -> f -> () { g(); let () -> g -> () { } }") == 0);
    CHECK(semaErrors("let (n :: int) -> f -> () { let () -> g -> () { let y <- n; } }") == 1);
    CHECK(semaErrors("let N <- 3; let () -> f -> () { let () -> g -> () { let y <- N; } }") == 0);
    CHECK(semaErrors("let () -> f -> () { let () -> g -> () { h(); } let () -> h -> () { } }") == 0);
}

// checks that predicates in refinements, obligations, and assertions resolve their names and are bools
TEST_CASE("Sema: refinements and contracts", "[sema]") {
    CHECK(semaErrors("let (a :: int {a >= 0}) -> f -> () { }") == 0);
    CHECK(semaErrors("let (a :: int {b >= 0}) -> f -> () { }") == 1);
    CHECK(semaErrors("let (a :: int {a}) -> f -> () { }") == 1);
    CHECK(semaErrors("let (a :: int) -> f -> (r :: int {r >= a}) { }") == 0);
    CHECK(semaErrors("let (a :: int) -> f -> (r :: int) wit {r >= a} { }") == 0);
    CHECK(semaErrors("let x :: int {p : p > 0} <- 5;") == 0);
    CHECK(semaErrors("assert (true);") == 0);
    CHECK(semaErrors("assert (1);") == 1);
}

// checks that operators take the core types they're defined on, and that both sides have to agree
TEST_CASE("Sema: operators", "[sema]") {
    CHECK(symbolType("let x <- 1 + 2 * 3;", "x") == "int");
    CHECK(symbolType("let x <- 1.5 / 2.0;", "x") == "float");
    CHECK(symbolType("let x <- 7.5 % 2.0;", "x") == "float");
    CHECK(symbolType("let x <- -3;", "x") == "int");
    CHECK(symbolType("let x <- 1 < 2;", "x") == "bool");
    CHECK(symbolType("let x <- 0 <= 1 < 2;", "x") == "bool");
    CHECK(symbolType("let x <- true = false;", "x") == "bool");
    CHECK(symbolType("let x <- 1.0 !== 2.0;", "x") == "bool");
    CHECK(symbolType("let x <- 6 & 3 | 1 << 2;", "x") == "int");
    CHECK(symbolType("let x <- ~0;", "x") == "int");
    CHECK(symbolType("let x <- !true && false ^^ true || false;", "x") == "bool");
    CHECK(semaErrors("let x <- 1 + 2.5;") == 1);
    CHECK(semaErrors("let x <- 1 + true;") == 1);
    CHECK(semaErrors("let x <- 1.0 & 2.0;") == 1);
    CHECK(semaErrors("let x <- 1 && 2;") == 1);
    CHECK(semaErrors("let x <- true < false;") == 1);
    CHECK(semaErrors("let x <- 0 <= 1.5 < 2;") == 2);
    CHECK(semaErrors("let x <- -true;") == 1);
    CHECK(semaErrors("let x <- !1;") == 1);
    CHECK(semaErrors("let x <- y + 1 + 2.5;") == 1);
    CHECK(semaErrors("let x <- 1 <+> 2;") == 1);
}

// checks calls and pipes against the called function's signature
TEST_CASE("Sema: calls and pipes", "[sema]") {
    const std::string fns = "let (a :: int, b :: int) -> add -> (r :: int) { } let (a :: int) -> inc -> (r :: int) { } let () -> tick -> () { } ";
    CHECK(symbolType(fns + "let x <- add(1, 2);", "x") == "int");
    CHECK(symbolType(fns + "let x <- 1 |> inc |> inc;", "x") == "int");
    CHECK(symbolType(fns + "let x <- inc <| inc <| 1 + 2;", "x") == "int");
    CHECK(semaErrors(fns + "let x <- add(1);") == 1);
    CHECK(semaErrors(fns + "let x <- add(1, true);") == 1);
    CHECK(semaErrors(fns + "let x <- tick();") == 1);
    CHECK(semaErrors(fns + "tick();") == 0);
    CHECK(semaErrors(fns + "let x <- 5; x(1);") == 1);
    CHECK(semaErrors(fns + "let g <- inc;") == 1);
    CHECK(semaErrors(fns + "let x <- 2 |> add(1);") == 1);
    CHECK(semaErrors(fns + "let x <- float(1);") == 1);
}

// checks that assignments and compound assignments keep the place's type, and that a discard takes anything
TEST_CASE("Sema: assignments", "[sema]") {
    CHECK(semaErrors("let x :: mut <- 1; x <- 2;") == 0);
    CHECK(semaErrors("let x :: mut <- 1; x <- true;") == 1);
    CHECK(semaErrors("let x :: mut <- 1; x +<- 2;") == 0);
    CHECK(semaErrors("let x :: mut <- 1; x +<- 2.0;") == 1);
    CHECK(semaErrors("let x :: mut <- 1.0; x <<<- 2;") == 1);
    CHECK(semaErrors("_ <- 1 + 2;") == 0);
    CHECK(semaErrors("let x <- _ + 1;") == 1);
}

// checks that what 4.1 can't check yet is reported once and poisons silently from there
TEST_CASE("Sema: unsupported forms", "[sema]") {
    CHECK(semaErrors("let x :: i32 <- 5;") == 1);
    CHECK(semaErrors("type Id <- int;") == 1);
    CHECK(semaErrors("import [List] <- list;") == 1);
    CHECK(semaErrors("let [a, b] <- p;") == 2);
    CHECK(semaErrors("let x :: i32 <- 5; let y <- x; let z <- y;") == 1);
    CHECK(semaErrors("let s <- \"hi\";") == 1);
    CHECK(semaErrors("let p <- [1, 2];") == 1);
    CHECK(semaErrors("let n <- match (1) { _ -> { 0 -| } };") == 1);
}
