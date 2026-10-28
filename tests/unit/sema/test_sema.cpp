#include <catch2/catch_test_macros.hpp>

#include <string>

#include "parser.hpp"
#include "sema.hpp"

// lexes, parses, and checks one input as a whole program, owning everything the symbols and types point into
struct Checked {
    SourceManager sm;
    DiagnosticEngine diag{sm};
    std::vector<Token> tokens;
    std::vector<std::unique_ptr<Stmt>> program;
    Sema sema{diag};
    explicit Checked(const std::string& input) {
        sm.loadString(input);
        tokens = Lexer(diag).lex();
        program = Parser(tokens, diag).parseProgram();
        sema.check(program);
    }
};

// checks input as a whole program and returns how many errors lexing, parsing, and sema reported
static size_t semaErrors(const std::string& input) {
    return Checked(input).diag.errorCount();
}

// checks input as a whole program and returns how many warnings sema reported
static size_t semaWarnings(const std::string& input) {
    return Checked(input).diag.warningCount();
}

// checks input as a whole program that will run, so it also needs a main, and returns how many diagnostics it produced
static size_t entryErrors(const std::string& input) {
    SourceManager sm;
    sm.loadString(input);
    DiagnosticEngine diag(sm);
    Lexer lexer(diag);
    auto tokens = lexer.lex();
    Parser parser(tokens, diag);
    auto program = parser.parseProgram();
    Sema sema(diag);
    sema.check(program, true);
    return diag.errorCount();
}

// checks input as a whole program and returns the type of the last symbol named name, or "" if there isn't one
static std::string symbolType(const std::string& input, std::string_view name) {
    Checked checked(input);
    std::string type;
    for (auto& symbol : checked.sema.allSymbols()) if (symbol.name == name) type = symbol.type->toString();
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

// checks the passes over a list of statements: functions are visible before they're defined, function bodies see every name around them, and lets elsewhere are only visible after
TEST_CASE("Sema: functions and the three passes over a statement list", "[sema]") {
    CHECK(symbolType("let (a :: int, b :: int) -> f -> (r :: bool) { a < b -| }", "f") == "(int, int) : bool");
    CHECK(symbolType("let () -> f -> () { }", "f") == "() : void");
    CHECK(symbolType("let (a :: int) -> f -> (r :: int) { let y <- a; y -| }", "y") == "int");
    CHECK(semaErrors("let () -> f -> () { h(); } let () -> h -> () { }") == 0);
    CHECK(semaErrors("let () -> f -> () { let y <- LIMIT; } let LIMIT <- 4;") == 0);
    CHECK(semaErrors("let y <- LIMIT; let LIMIT <- 4;") == 1);
    CHECK(semaErrors("let (a) -> f -> () { }") == 1);
    CHECK(semaErrors("let () -> f -> () { } let () -> f -> () { }") == 1);
    CHECK(semaErrors("let () -> f -> (r :: int) { let y <- r; 0 -| }") == 1);
    // a signature is resolved once, so each of its mistakes is reported once
    CHECK(semaErrors("let (x :: string) -> f -> (r :: string) { }") == 2);
    CHECK(semaErrors("let () -> f -> (a :: int, b :: int) { }") == 1);
    CHECK(semaErrors("let () -> f -> (r) { }") == 1);
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
    CHECK(semaErrors("let (a :: int) -> f -> (r :: int {r >= a}) { a -| }") == 0);
    CHECK(semaErrors("let (a :: int) -> f -> (r :: int) wit {r >= a} { a -| }") == 0);
    CHECK(semaErrors("let x :: int {p : p > 0} <- 5;") == 0);
    CHECK(semaErrors("let x :: int {x > 0} <- 5;") == 0);
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
    CHECK(symbolType("let x <- 1 + 2.5;", "x") == "float");
    CHECK(semaErrors("let x <- 1 + true;") == 1);
    CHECK(semaErrors("let x <- 1.0 & 2.0;") == 1);
    CHECK(semaErrors("let x <- 1 && 2;") == 1);
    CHECK(semaErrors("let x <- true < false;") == 1);
    CHECK(semaErrors("let x <- 0 <= 1.5 < 2;") == 0);
    CHECK(semaErrors("let x <- -true;") == 1);
    CHECK(semaErrors("let x <- !1;") == 1);
    CHECK(semaErrors("let x <- y + 1 + 2.5;") == 1);
    CHECK(semaErrors("let x <- 1 <+> 2;") == 1);
}

// checks calls and pipes against the called function's signature
TEST_CASE("Sema: calls and pipes", "[sema]") {
    const std::string fns = "let (a :: int, b :: int) -> add -> (r :: int) { a + b -| } let (a :: int) -> inc -> (r :: int) { a + 1 -| } let () -> tick -> () { } ";
    CHECK(symbolType(fns + "let x <- add(1, 2);", "x") == "int");
    CHECK(symbolType(fns + "let x <- 1 |> inc |> inc;", "x") == "int");
    CHECK(symbolType(fns + "let x <- inc <| inc <| 1 + 2;", "x") == "int");
    CHECK(semaErrors(fns + "let x <- add(1);") == 1);
    CHECK(semaErrors(fns + "let x <- inc(1, 2, 3);") == 1);
    CHECK(semaErrors(fns + "let x <- add(1, true);") == 1);
    CHECK(semaErrors(fns + "let x <- tick();") == 1);
    CHECK(semaErrors(fns + "tick();") == 0);
    CHECK(semaErrors(fns + "let x <- 5; x(1);") == 1);
    CHECK(semaErrors(fns + "let g <- inc;") == 1);
    CHECK(semaErrors(fns + "let x <- 2 |> add(1);") == 1);
    CHECK(semaErrors(fns + "let x <- float(1);") == 0);
}

// checks that assignments and compound assignments keep the place's type, and that a discard takes anything
TEST_CASE("Sema: assignments", "[sema]") {
    CHECK(semaErrors("let x :: mut <- 1; x <- 2;") == 0);
    CHECK(semaErrors("let x :: mut <- 1; x <- true;") == 1);
    CHECK(semaErrors("let x :: mut <- 1; x +<- 2;") == 0);
    CHECK(semaErrors("let x :: mut <- 1; x +<- 2.0;") == 1);
    CHECK(semaErrors("let x :: mut <- 1.0; x <<<- 2;") == 1);
    CHECK(semaErrors("let x :: mut <- 1; x **<- 2;") == 1);
    CHECK(semaErrors("_ <- 1 + 2;") == 0);
    CHECK(semaErrors("let x <- _ + 1;") == 1);
}

// checks that only mut bindings and parameters can be assigned to, and that uninitialized bindings have to be mut
TEST_CASE("Sema: mutability", "[sema]") {
    CHECK(semaErrors("let x <- 1; x <- 2;") == 1);
    CHECK(semaErrors("let x <- 1; x +<- 2;") == 1);
    CHECK(semaErrors("let (n :: int) -> f -> () { n <- 2; }") == 1);
    CHECK(semaErrors("let (n :: mut int) -> f -> () { n <- 2; }") == 0);
    CHECK(semaErrors("let () -> f -> () { } f <- 2;") == 1);
    CHECK(semaErrors("let x :: int <- _;") == 1);
    CHECK(semaErrors("let x :: mut int <- _; x <- 5;") == 0);
    CHECK(semaErrors("let x :: mut <- 1; { x <- 2; }") == 0);
    CHECK(semaErrors("let (n :: string) -> f -> () { n <- 2; }") == 2);
}

// checks that a value can't be dropped without an explicit discard
TEST_CASE("Sema: discards", "[sema]") {
    const std::string fns = "let (a :: int) -> inc -> (r :: int) { a + 1 -| } let () -> tick -> () { } ";
    CHECK(semaErrors(fns + "inc(1);") == 1);
    CHECK(semaErrors(fns + "_ <- inc(1);") == 0);
    CHECK(semaErrors(fns + "tick();") == 0);
    CHECK(semaErrors(fns + "1 + 2;") == 1);
    CHECK(semaErrors(fns + "1 |> inc;") == 1);
    CHECK(semaErrors(fns + "y;") == 1);
}

// checks that what sema can't check yet is reported once and poisons silently from there
TEST_CASE("Sema: unsupported forms", "[sema]") {
    CHECK(semaErrors("let x :: Id <- 5;") == 1);
    CHECK(semaErrors("type Id <- int;") == 1);
    CHECK(semaErrors("import [List] <- list;") == 1);
    CHECK(semaErrors("let [a, b] <- p;") == 2);
    CHECK(semaErrors("let x :: Id <- 5; let y <- x; let z <- y;") == 1);
    CHECK(semaErrors("let s <- \"hi\";") == 1);
    CHECK(semaErrors("let p <- [1, 2];") == 1);
    CHECK(semaErrors("let x <- 0; repeat (i) <- x { }") == 1);
}

// checks function results: every yield has to match the output, and every path has to yield
TEST_CASE("Sema: function results", "[sema]") {
    CHECK(semaErrors("let (a :: int) -> f -> (r :: int) { a -| }") == 0);
    CHECK(semaErrors("let (a :: int) -> f -> (r :: int) { }") == 1);
    CHECK(semaErrors("let (a :: int) -> f -> (r :: int) { true -| }") == 1);
    CHECK(semaErrors("let () -> f -> () { -| }") == 0);
    CHECK(semaErrors("let () -> f -> () { 5 -| }") == 1);
    CHECK(semaErrors("let () -> f -> (r :: int) { -| }") == 1);
    CHECK(semaErrors("let (a :: int) -> f -> (r :: int) { { a -| f } }") == 0);
    CHECK(semaErrors("let (a :: int) -> f -> (r :: int) { { true -| f } 0 -| }") == 1);
    CHECK(semaErrors("let (a :: int) -> f -> (r :: int) { y -| }") == 1);
    // control never gets past an operand or argument that yields, so the operator or call is never too
    CHECK(semaErrors("let () -> f -> (r :: int) { let b <- { 1 -| f } + 1; }") == 0);
    CHECK(semaErrors("let (n :: int) -> h -> (r :: int) { n -| } let () -> g -> (r :: int) { _ <- h({ 2 -| g }); }") == 0);
}

// checks that bare yields go to the innermost scope, labels name an enclosing one, and nothing crosses a function
TEST_CASE("Sema: yield targets and labels", "[sema]") {
    CHECK(symbolType("let x <- { 5 -| };", "x") == "int");
    CHECK(symbolType("let x <- { outer { { 5 -| outer } } -| };", "x") == "int");
    CHECK(symbolType("let x <- { { 5 -| } -| };", "x") == "int");
    CHECK(semaErrors("let x <- { };") == 1);
    CHECK(semaErrors("let x <- { 5 -| outer };") == 1);
    CHECK(semaErrors("-| ;") == 2);
    CHECK(semaErrors("5 -|") == 1);
    CHECK(semaErrors("let x <- { 5 -| } ;") == 0);
    CHECK(semaErrors("let x <- { outer { { 5 -| outer } true -| } -| };") == 1);
    CHECK(semaErrors("let () -> f -> () { let () -> g -> () { -| f } }") == 1);
    CHECK(semaErrors("let x :: mut <- 0; let f <- 1; let () -> f2 -> () { f { x <- 1; -| f } }") == 0);
}

// checks that a scope used as a value has to yield on every path, and that every yield to it agrees
TEST_CASE("Sema: scope values", "[sema]") {
    CHECK(semaErrors("let x <- { 5 -| };") == 0);
    CHECK(semaErrors("let x <- { b { { 5 -| b } let y <- 1; } -| };") == 0);
    CHECK(semaWarnings("let x <- { b { { 5 -| b } let y <- 1; } -| };") == 1);
    CHECK(semaErrors("let x <- { b { { 5 -| b } true -| } -| };") == 1);
    CHECK(semaErrors("{ 5 -| }") == 1);
    CHECK(semaErrors("_ <- { 5 -| };") == 0);
}

// checks the warning for statements that can never run, which doesn't count as an error
TEST_CASE("Sema: unreachable statements", "[sema]") {
    CHECK(semaErrors("let () -> f -> () { -| let x <- 1; }") == 0);
    CHECK(semaWarnings("let () -> f -> () { -| let x <- 1; let y <- 2; }") == 1);
    CHECK(semaWarnings("let () -> f -> () { { -| f } let x <- 1; }") == 1);
    CHECK(semaWarnings("let () -> f -> () { let x <- 1; -| }") == 0);
}

// checks matches: arm values join into the match's type, patterns fit the subject, and the arms cover what they need to
TEST_CASE("Sema: match", "[sema]") {
    CHECK(symbolType("let n <- 3; let x <- match (n) { 0 -> { 10 -| } _ -> { 20 -| } };", "x") == "int");
    CHECK(symbolType("let b <- true; let x <- match (b) { true -> { 1.0 -| } false -> { 2.0 -| } };", "x") == "float");
    CHECK(symbolType("let n <- 3; let x <- match (n) { -1 -> { false -| } m -> { m > 0 -| } };", "x") == "bool");
    CHECK(symbolType("let n <- 3; let x <- match { (n < 0) -> { 0 -| } _ -> { n -| } };", "x") == "int");
    CHECK(symbolType("let n <- 3; let x <- match (=, n) { 0 -> { 1 -| } _ -> { 2 -| } };", "x") == "int");
    CHECK(symbolType("let f <- 0.5; let x <- match (f) { 0.5 -> { 1 -| } _ -> { 2 -| } };", "x") == "int");
    CHECK(semaErrors("let n <- 3; let x <- match (n) { 0 -> { 10 -| } _ -> { true -| } };") == 1);
    CHECK(semaErrors("let n <- 3; let x <- match (n) { true -> { 1 -| } _ -> { 2 -| } };") == 1);
    CHECK(semaErrors("let n <- 3; match (n) { 0 -> { } }") == 1);
    CHECK(semaErrors("let b <- true; match (b) { true -> { } }") == 0);
    CHECK(semaErrors("let b <- true; let x <- match (b) { true -> { 1 -| } };") == 1);
    CHECK(semaErrors("let n <- 3; match { (n < 0) -> { } }") == 0);
    CHECK(semaErrors("let n <- 3; let x <- match { (n < 0) -> { 0 -| } };") == 1);
    CHECK(semaErrors("let n <- 3; match { (n) -> { } }") == 1);
    CHECK(semaErrors("let n <- 3; let x <- match (n) { m -> { m -| } }; let y <- m;") == 1);
    CHECK(semaWarnings("let n <- 3; match (n) { _ -> { } 0 -> { } }") == 1);
    CHECK(semaWarnings("let b <- true; match (b) { true -> { } false -> { } _ -> { } }") == 1);
    // a pattern sema can't check yet doesn't count as a catch-all, so the arm after it isn't unreachable
    CHECK(semaErrors("let (x :: int) -> f -> () { match (x) { Some[v] -> { } _ -> { } } }") == 1);
    CHECK(semaWarnings("let (x :: int) -> f -> () { match (x) { Some[v] -> { } _ -> { } } }") == 0);
}

// checks yields that leave a match, from an arm or past it, and the paths that do or don't yield
TEST_CASE("Sema: yields through match", "[sema]") {
    CHECK(semaErrors("let (n :: int) -> f -> (r :: int) { match (n) { 0 -> { 1 -| f } _ -> { 2 -| f } } }") == 0);
    CHECK(semaErrors("let (n :: int) -> f -> (r :: int) { match { (n > 100) -> { 100 -| f } } n -| }") == 0);
    CHECK(semaErrors("let (n :: int) -> f -> (r :: int) { match { (n > 100) -> { 100 -| f } } }") == 1);
    CHECK(semaErrors("let (n :: int) -> f -> (r :: int) { match (n) { 0 -> { 1 -| } _ -> { 2 -| } } -| }") == 0);
    CHECK(semaErrors("let n <- 3; let x <- match (n) { 0 -> { { 5 -| match } } _ -> { 6 -| } };") == 0);
    CHECK(semaErrors("let n <- 3; let x <- { match { (n > 0) -> { 1 -| } } 2 -| };") == 1);
    CHECK(semaWarnings("let (n :: int) -> f -> () { match (n) { 0 -> { -| f } _ -> { -| f } } let y <- 1; }") == 1);
}

// checks loops: conditions are bools, a loop without one takes its exit's type, and a loop with one can't yield a value yet
TEST_CASE("Sema: repeat", "[sema]") {
    CHECK(semaErrors("let x :: mut <- 0; repeat (x < 10) { x +<- 1; }") == 0);
    CHECK(semaErrors("let x :: mut <- 0; repeat (x) { }") == 1);
    CHECK(semaErrors("let x :: mut <- 0; repeat (x < 10) wit {x >= 0} { x +<- 1; }") == 0);
    CHECK(symbolType("let x <- repeat { 1 -| };", "x") == "int");
    CHECK(symbolType("let x :: mut <- 0; let y <- repeat { x +<- 1; match { (x > 5) -> { x -| repeat } } };", "y") == "int");
    CHECK(semaErrors("let x :: mut <- 0; repeat (x < 10) { -| repeat }") == 0);
    CHECK(semaErrors("let x :: mut <- 0; repeat (x < 10) { 5 -| repeat }") == 1);
    CHECK(semaErrors("let x :: mut <- 0; repeat outer (x < 10) { repeat { -| outer } }") == 0);
    CHECK(semaErrors("-| repeat") == 1);
    CHECK(semaWarnings("let () -> f -> () { repeat { } let y <- 1; }") == 1);
    CHECK(semaErrors("let () -> f -> (r :: int) { repeat { } }") == 0);
    // a header loop that yields a value is reported once, and binding it doesn't report again
    CHECK(semaErrors("let q :: mut <- 0; let s <- repeat (q < 3) { 5 -| };") == 1);
}

// checks chunks, which yield like blocks and can be named
TEST_CASE("Sema: chunk", "[sema]") {
    CHECK(symbolType("let x <- chunk { 4 -| };", "x") == "int");
    CHECK(semaErrors("let x <- chunk scratch { { 4 -| scratch } };") == 0);
    CHECK(semaErrors("chunk { let y <- 1; }") == 0);
}

// checks that a program that will run has a top-level main shaped the way the runtime calls it
TEST_CASE("Sema: entry point", "[sema]") {
    CHECK(entryErrors("let () -> main -> (code :: int) { 0 -| }") == 0);
    CHECK(entryErrors("let LIMIT <- 4; let () -> main -> (code :: int) { LIMIT -| }") == 0);
    CHECK(entryErrors("let () -> start -> (code :: int) { 0 -| }") == 1);
    CHECK(entryErrors("let (a :: int) -> main -> (code :: int) { a -| }") == 1);
    CHECK(entryErrors("let () -> main -> () { }") == 1);
    CHECK(entryErrors("let () -> main -> (code :: bool) { true -| }") == 1);
    CHECK(entryErrors("let main <- 5;") == 1);
    CHECK(entryErrors("let () -> f -> () { let () -> main -> (code :: int) { 0 -| } }") == 1);
    CHECK(semaErrors("let () -> start -> (code :: int) { 0 -| }") == 0);
}

// checks that every integer type resolves, and that integers of different types never mix without a conversion
TEST_CASE("Sema: integer types", "[sema]") {
    CHECK(symbolType("let x :: i8 <- 1;", "x") == "i8");
    CHECK(symbolType("let x :: i128uw <- 1;", "x") == "i128uw");
    CHECK(symbolType("let x :: i64 <- 1;", "x") == "int");
    CHECK(symbolType("let x :: i32u <- 1; let y <- x + x;", "y") == "i32u");
    CHECK(symbolType("let x :: i16w <- 1; let y <- x << x & ~x;", "y") == "i16w");
    CHECK(symbolType("let x :: i8 <- 1; let y <- x < x;", "y") == "bool");
    CHECK(semaErrors("let x :: i8 <- 1; let y :: i16 <- 2; let z <- x + y;") == 1);
    CHECK(semaErrors("let x :: i8 <- 1; let y :: i8u <- 2; let z <- x = y;") == 1);
    CHECK(semaErrors("let x :: u64 <- 1;") == 1);
}

// checks that a literal takes the type its context wants, and has to fit there
TEST_CASE("Sema: literal adaptation", "[sema]") {
    CHECK(symbolType("let x :: i8 <- 127;", "x") == "i8");
    CHECK(symbolType("let x :: i8 <- -128;", "x") == "i8");
    CHECK(symbolType("let x :: i8u <- ~5;", "x") == "i8u");
    CHECK(symbolType("let x :: i8u <- 255;", "x") == "i8u");
    CHECK(symbolType("let x :: i8u <- 0xFF;", "x") == "i8u");
    CHECK(symbolType("let x :: i128u <- 0xFFFF_FFFF_FFFF_FFFF_FFFF_FFFF_FFFF_FFFF;", "x") == "i128u");
    CHECK(semaErrors("let x :: i8 <- 128;") == 1);
    CHECK(semaErrors("let x :: i8 <- -129;") == 1);
    CHECK(semaErrors("let x :: i8u <- 256;") == 1);
    CHECK(semaErrors("let x :: i8uw <- 256;") == 1);
    CHECK(semaErrors("let x <- 9223372036854775808;") == 1);
    CHECK(semaErrors("let x :: i128u <- 0x1_0000_0000_0000_0000_0000_0000_0000_0000;") == 1);
    CHECK(symbolType("let x :: i8 <- 1; let y <- x + 2;", "y") == "i8");
    CHECK(symbolType("let x :: i8 <- 1; let y <- 2 * x;", "y") == "i8");
    CHECK(symbolType("let x :: i8 <- 1; let y <- 0 < x <= 100;", "y") == "bool");
    CHECK(semaErrors("let x :: i8 <- 1; let y <- x + 200;") == 1);
    CHECK(symbolType("let y :: i16 <- 1 + 2 * 3;", "y") == "i16");
    CHECK(semaErrors("let x :: mut i8 <- 0; x <- 100; x +<- 27;") == 0);
    CHECK(semaErrors("let x :: mut i8 <- 0; x +<- 128;") == 1);
    CHECK(semaErrors("let (a :: i8) -> f -> (r :: i8) { a + 1 -| } let y <- f(5);") == 0);
    CHECK(semaErrors("let (a :: i8) -> f -> (r :: i8) { 300 -| }") == 1);
    CHECK(semaErrors("let (n :: i8u) -> f -> () { match (n) { 255 -> { } _ -> { } } }") == 0);
}

// checks negation, which only signed and wrapping integers allow
TEST_CASE("Sema: integer negation", "[sema]") {
    CHECK(semaErrors("let x :: i32 <- 1; let y <- -x;") == 0);
    CHECK(semaErrors("let x :: i32uw <- 1; let y <- -x;") == 0);
    CHECK(semaErrors("let x :: i32u <- 1; let y <- -x;") == 1);
    CHECK(semaErrors("let x :: i32u <- -1;") == 1);
}

// checks conversions between integer types
TEST_CASE("Sema: integer conversions", "[sema]") {
    CHECK(symbolType("let x :: i64 <- 5; let y <- i8(x);", "y") == "i8");
    CHECK(symbolType("let x :: i8 <- 5; let y <- i128u(x);", "y") == "i128u");
    CHECK(symbolType("let y <- i8w(300);", "y") == "i8w");
    CHECK(semaErrors("let y <- i8(true);") == 1);
    CHECK(semaErrors("let y <- i8(1, 2);") == 1);
    CHECK(semaErrors("let y <- bool(1);") == 1);
    CHECK(semaErrors("let y <- i8(1.5);") == 0);
}

// checks that every float type resolves, and that floats of different types never mix without a conversion
TEST_CASE("Sema: float types", "[sema]") {
    CHECK(symbolType("let x :: f32 <- 1.5;", "x") == "f32");
    CHECK(symbolType("let x :: f64 <- 1.5;", "x") == "float");
    CHECK(symbolType("let x :: float <- 1.5;", "x") == "float");
    CHECK(symbolType("let x :: f8 <- 1.5;", "x") == "f8");
    CHECK(symbolType("let x :: f8e4m3 <- 1.5;", "x") == "f8");
    CHECK(symbolType("let x :: f8e4m3n <- 1.5;", "x") == "f8e4m3n");
    CHECK(symbolType("let x :: f8e5m2n <- 1.5;", "x") == "f8e5m2n");
    CHECK(symbolType("let x :: f16e8m7n <- 1.5;", "x") == "f16e8m7n");
    CHECK(symbolType("let x :: f128 <- 1.5; let y <- x * x;", "y") == "f128");
    CHECK(symbolType("let x :: f16 <- 1.5; let y <- x < x;", "y") == "bool");
    CHECK(semaErrors("let x :: f32 <- 1.0; let y :: f64 <- 2.0; let z <- x + y;") == 1);
    CHECK(semaErrors("let x :: f32 <- 1.0; let y :: f32n <- 2.0; let z <- x + y;") == 1);
    CHECK(semaErrors("let x :: f8n <- 1.0;") == 1);
    CHECK(semaErrors("let x :: f8 <- 1.0; let y :: f8e5m2 <- 1.0; let z <- x + y;") == 1);
    CHECK(semaErrors("let x :: f32 <- 1.0; let y <- x & x;") == 1);
}

// checks that float literals take a float context's type and have to be within its range, and that integer literals can stand for a float when they're exact
TEST_CASE("Sema: float literals", "[sema]") {
    CHECK(symbolType("let x :: f32 <- 1.0; let y <- x * 2.5;", "y") == "f32");
    CHECK(symbolType("let x :: f16 <- 65504.0;", "x") == "f16");
    CHECK(semaErrors("let x :: f16 <- 65536.0;") == 1);
    CHECK(semaErrors("let x :: f16n <- -65504.0;") == 0);
    CHECK(semaErrors("let x :: f8 <- 448.0;") == 0);
    CHECK(semaErrors("let x :: f8 <- 449.0;") == 1);
    CHECK(semaErrors("let x :: f8e5m2 <- -57344.0;") == 0);
    CHECK(symbolType("let x :: f32 <- 0x1.8;", "x") == "f32");
    CHECK(symbolType("let x :: f32 <- 0b1.1;", "x") == "f32");
    CHECK(symbolType("let x :: f64 <- 2;", "x") == "float");
    CHECK(symbolType("let x :: f32 <- 1.0; let y <- x * 2;", "y") == "f32");
    CHECK(symbolType("let x :: f32 <- 1.0; let y <- 2 * x;", "y") == "f32");
    CHECK(semaErrors("let x :: f16 <- 2048;") == 0);
    CHECK(semaErrors("let x :: f16 <- 2049;") == 1);
    CHECK(semaErrors("let x :: f16 <- 4096;") == 0);
    CHECK(semaErrors("let x :: f32 <- 16777217;") == 1);
    CHECK(semaErrors("let x :: f16 <- 65536;") == 1);
    CHECK(semaErrors("let x :: f8 <- 16;") == 0);
    CHECK(semaErrors("let x :: f8 <- 17;") == 1);
    CHECK(semaErrors("let x :: f8 <- 1000;") == 1);
    CHECK(semaErrors("let x :: f32 <- -3;") == 0);
    CHECK(semaErrors("let x :: i8 <- 1.5;") == 1);
    CHECK(symbolType("let y <- 0.5 < 1;", "y") == "bool");
}

// checks conversions between floats, and between floats and integers
TEST_CASE("Sema: float conversions", "[sema]") {
    CHECK(symbolType("let x :: f32 <- 1.5; let y <- f64(x);", "y") == "float");
    CHECK(symbolType("let x :: f64n <- 1.5; let y <- f64(x);", "y") == "float");
    CHECK(symbolType("let x :: i32 <- 7; let y <- f16(x);", "y") == "f16");
    CHECK(symbolType("let x :: i32 <- 7; let y <- f8(x);", "y") == "f8");
    CHECK(symbolType("let x :: f8e5m2 <- 1.5; let y <- f128(x);", "y") == "f128");
    CHECK(symbolType("let x <- 2.7; let y <- i32(x);", "y") == "i32");
    CHECK(symbolType("let x <- 2.7; let y <- i8uw(x);", "y") == "i8uw");
    CHECK(semaErrors("let y <- f32(true);") == 1);
    CHECK(semaErrors("let y <- bool(1.0);") == 1);
}

// checks that a char literal adapts to a char or a rune, is a char when nothing decides and a rune when it isn't ASCII, and that a char only holds ASCII
TEST_CASE("Sema: char literals", "[sema]") {
    CHECK(symbolType("let c <- 'a';", "c") == "char");
    CHECK(symbolType("let c <- '\\n';", "c") == "char");
    CHECK(symbolType("let c <- '\xC3\xA9';", "c") == "rune");
    CHECK(symbolType("let c <- '\\u{1F600}';", "c") == "rune");
    CHECK(symbolType("let c :: rune <- 'a';", "c") == "rune");
    CHECK(symbolType("let c :: char <- '\\x7F';", "c") == "char");
    CHECK(semaErrors("let c :: char <- '\\x80';") == 1);
    CHECK(semaErrors("let c :: char <- '\\u{E9}';") == 1);
    CHECK(semaErrors("let c :: int <- 'a';") == 1);
    CHECK(semaErrors("let c :: char <- 97;") == 1);
}

// checks that characters compare and match like integers, but have no arithmetic and never mix with each other or with numbers
TEST_CASE("Sema: character operators", "[sema]") {
    CHECK(symbolType("let r :: rune <- 'z'; let b <- 'a' < r;", "b") == "bool");
    CHECK(symbolType("let c <- 'q'; let b <- 'a' <= c <= 'z';", "b") == "bool");
    CHECK(symbolType("let r :: rune <- 'x'; let b <- r = 'x';", "b") == "bool");
    CHECK(semaErrors("let c <- 'a'; let r :: rune <- 'b'; let b <- c < r;") == 1);
    CHECK(semaErrors("let c <- 'a'; let b <- c < 5;") == 1);
    CHECK(semaErrors("let c <- 'a' + 'b';") == 1);
    CHECK(semaErrors("let c <- -'a';") == 1);
    CHECK(semaErrors("let c <- 'a'; let d <- !c;") == 1);
    CHECK(semaErrors("let c <- 'a'; let n <- match (c) { 'a' -> { 1 -| } 'b' -> { 2 -| } _ -> { 3 -| } };") == 0);
    CHECK(semaErrors("let r :: rune <- 'a'; let n <- match (r) { '\\u{E9}' -> { 1 -| } _ -> { 2 -| } };") == 0);
    CHECK(semaErrors("let c <- 'a'; let n <- match (c) { '\\u{E9}' -> { 1 -| } _ -> { 2 -| } };") == 1);
    CHECK(semaErrors("let c <- 'a'; match (c) { 'a' -> { } }") == 1);
}

// checks that characters convert to each other and to and from integers, but never to or from floats or bools
TEST_CASE("Sema: character conversions", "[sema]") {
    CHECK(symbolType("let c <- 'a'; let n <- int(c);", "n") == "int");
    CHECK(symbolType("let c <- char(97);", "c") == "char");
    CHECK(symbolType("let r <- rune(0x1F600);", "r") == "rune");
    CHECK(symbolType("let c <- 'a'; let r <- rune(c);", "r") == "rune");
    CHECK(symbolType("let r :: rune <- 'a'; let c <- char(r);", "c") == "char");
    CHECK(symbolType("let n <- 'a' |> i8u;", "n") == "i8u");
    CHECK(semaErrors("let c <- char(1.5);") == 1);
    CHECK(semaErrors("let f <- f32('a');") == 1);
    CHECK(semaErrors("let c <- char(true);") == 1);
    CHECK(semaErrors("let b <- bool('a');") == 1);
}

// checks that an annotation checks its value against the type without converting it, so a literal takes the type
TEST_CASE("Sema: annotations", "[sema]") {
    CHECK(symbolType("let x <- (5 :: i8);", "x") == "i8");
    CHECK(symbolType("let x <- (2.5 :: f32);", "x") == "f32");
    CHECK(symbolType("let r <- ('a' :: rune);", "r") == "rune");
    CHECK(symbolType("let x :: i16 <- 3; let y <- (x :: i16) + 1;", "y") == "i16");
    CHECK(symbolType("let b <- (1 :: i8u) < 2;", "b") == "bool");
    CHECK(semaErrors("let x :: int <- 3; let y <- (x :: i8);") == 1);
    CHECK(semaErrors("let x <- (300 :: i8);") == 1);
    CHECK(semaErrors("let x :: i8 <- (5 :: i16);") == 1);
    CHECK(semaErrors("let c <- ('\\u{E9}' :: char);") == 1);
}
