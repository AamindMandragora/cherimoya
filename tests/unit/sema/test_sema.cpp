#include <catch2/catch_test_macros.hpp>

#include <string>

#include "parser.hpp"
#include "sema.hpp"

// lexes, parses, and checks one input as a whole file, a module unless the role says otherwise, owning everything the symbols and types point into
struct Checked {
    SourceManager sm;
    DiagnosticEngine diag{sm};
    std::vector<Token> tokens;
    std::vector<std::unique_ptr<Stmt>> program;
    Sema sema{diag};
    explicit Checked(const std::string& input, Role role = Role::Module) {
        sm.loadString(input);
        tokens = Lexer(diag).lex();
        program = Parser(tokens, diag).parseProgram();
        sema.check(program, role);
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

// checks input as a whole program that will run, so it also needs a start, and returns how many diagnostics it produced
static size_t entryErrors(const std::string& input) {
    SourceManager sm;
    sm.loadString(input);
    DiagnosticEngine diag(sm);
    Lexer lexer(diag);
    auto tokens = lexer.lex();
    Parser parser(tokens, diag);
    auto program = parser.parseProgram();
    Sema sema(diag);
    sema.check(program, Role::Program);
    return diag.errorCount();
}

// checks input as a whole program and returns the type of the last symbol named name, or "" if there isn't one
static std::string symbolType(const std::string& input, std::string_view name) {
    Checked checked(input);
    std::string type;
    for (auto& symbol : checked.sema.allSymbols()) if (symbol.name == name) type = symbol.type->toString();
    return type;
}

// checks input as a .toya file and describes every test it found, as its name, then the text its trap has to hold and its timeout when it has them
static std::vector<std::string> testsIn(const std::string& input) {
    Checked checked(input, Role::Tests);
    std::vector<std::string> found;
    for (auto& test : checked.sema.allTests()) {
        found.push_back(test.name + (test.traps ? " traps " + *test.traps : "") + (test.timeout ? " timeout " + std::to_string(*test.timeout) : ""));
    }
    return found;
}

// checks input as a .toya file and returns how many errors it reported
static size_t testErrors(const std::string& input) {
    return Checked(input, Role::Tests).diag.errorCount();
}

// checks tests: @test marks them apart from helpers, with a trap's text and a timeout, nested ones are named after the functions around them and come back in the order they're written, and everything @test can get wrong is an error
TEST_CASE("Sema: tests", "[sema]") {
    CHECK(testsIn("@test let () -> a -> () { } let () -> helper -> () { }") == std::vector<std::string>{"a"});
    CHECK(testsIn("@test(traps <- \"overflowed\", timeout <- 3) let () -> b -> () { }") == std::vector<std::string>{"b traps overflowed timeout 3"});
    CHECK(testsIn("@test(traps <- \"\\u{3BB} \\x41\") let () -> c -> () { }") == std::vector<std::string>{"c traps \u03bb A"});
    CHECK(testsIn("let () -> outer -> () { @test let () -> inner -> () { } } @test let () -> z -> () { }") == std::vector<std::string>{"outer.inner", "z"});
    for (const char* bad : {"@test(5)", "@test(flaky <- 1)", "@test(traps <- 3)", "@test(timeout <- 0)", "@test(timeout <- 1.5)", "@test(timeout <- 1, timeout <- 2)"}) {
        CHECK(testErrors(std::string(bad) + " let () -> t -> () { }") == 1);
    }
    CHECK(testErrors("@test let (n :: int) -> t -> () { }") == 1);
    CHECK(testErrors("@test let () -> t -> (r :: int) { 0 -| }") == 1);
    CHECK(testErrors("@test let x <- 1;") == 1);
    CHECK(testErrors("@test let () -> t -> () { } let () -> start -> (code :: int) { 0 -| }") == 1);
    // a test is only a test in a .toya file, and a .toya file without any gets a warning
    CHECK(semaErrors("@test let () -> t -> () { }") == 1);
    CHECK(Checked("let x <- 1;", Role::Tests).diag.warningCount() == 1);
    CHECK(Checked("@test let () -> t -> () { }", Role::Tests).diag.warningCount() == 0);
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
    CHECK(symbolType(fns + "let x <- 2 |> add(1);", "x") == "int");
    CHECK(semaErrors(fns + "let x <- 2 |> add(a <- 1);") == 2);
    CHECK(semaErrors(fns + "let x <- float(1);") == 0);
}

// checks named arguments, which go after the unnamed ones and name an input once, and defaults, which see the inputs before them, have to fit their input, and only go on inputs
TEST_CASE("Sema: named arguments and defaults", "[sema]") {
    const std::string fns = "let (a :: int, b :: int <- 10, c :: int <- a + b) -> f -> (r :: int) { a + b + c -| } ";
    for (const char* call : {"f(1)", "f(1, 2)", "f(1, 2, 3)", "f(1, c <- 3)", "f(c <- 3, a <- 1)", "f(a <- 1)", "1 |> f(c <- 3)", "f(b <- 2) <| 1", "1 |> f"}) {
        CHECK(semaErrors(fns + "let x <- " + call + ";") == 0);
    }
    CHECK(symbolType(fns + "let x <- f(c <- 3, a <- 1);", "x") == "int");
    CHECK(semaErrors(fns + "let x <- f();") == 1);
    CHECK(semaErrors(fns + "let x <- f(b <- 1);") == 1);
    CHECK(semaErrors(fns + "let x <- f(1, d <- 1);") == 1);
    CHECK(semaErrors(fns + "let x <- f(a <- 1, 2);") == 1);
    CHECK(semaErrors(fns + "let x <- f(1, a <- 2);") == 1);
    CHECK(semaErrors(fns + "let x <- f(1, 2, 3, 4);") == 1);
    CHECK(semaErrors(fns + "let x <- f(1, b <- true);") == 1);
    CHECK(semaErrors(fns + "let x <- 1 |> f(a <- 2);") == 1);
    CHECK(semaErrors("let (a :: int <- true) -> g -> () { }") == 1);
    CHECK(semaErrors("let (a :: int <- b, b :: int) -> g -> () { }") == 1);
    CHECK(semaErrors("let (a :: int <- { 1 -| g }) -> g -> () { }") == 1);
    CHECK(semaErrors("let () -> g -> (r :: int <- 1) { 1 -| }") == 1);
    CHECK(semaErrors("let x <- i32(v <- 1);") == 1);
}

// checks array literals, which take their elements' type the way an operator takes its operands', sizes known when the program compiles, including constants declared anywhere in a signature's scope, indexing by unsigned integers, len, and comparing arrays
TEST_CASE("Sema: arrays", "[sema]") {
    CHECK(symbolType("let xs <- [1, 2, 3];", "xs") == "arr[3] int");
    CHECK(symbolType("let xs <- [1, 2.5];", "xs") == "arr[2] float");
    CHECK(symbolType("let x :: i8 <- 1; let xs <- [1, x];", "xs") == "arr[2] i8");
    CHECK(symbolType("let m <- [[1, 2], [3, 4], [5, 6]];", "m") == "arr[3] arr[2] int");
    CHECK(semaErrors("let xs :: arr[3] i8 <- [1, 2, 3];") == 0);
    CHECK(semaErrors("let xs :: arr[3] int <- [1, 2];") == 1);
    CHECK(semaErrors("let xs <- [1, true];") == 1);
    CHECK(semaErrors("let xs <- [];") == 1);
    CHECK(semaErrors("let xs :: arr[0] int <- [];") == 0);
    // sizes
    CHECK(semaErrors("let N <- 2 + 1; let xs :: arr[N] int <- [1, 2, 3];") == 0);
    CHECK(semaErrors("let (xs :: arr[N] int) -> first -> (r :: int) { xs[0] -| } let N <- 2;") == 0);
    CHECK(semaErrors("let n :: mut <- 3; let xs :: arr[n] mut int <- _;") == 1);
    CHECK(semaErrors("let xs :: arr[2 - 3] int <- _;") == 1);
    // indexing and len
    CHECK(symbolType("let xs <- [1, 2]; let a <- xs[1];", "a") == "int");
    CHECK(symbolType("let m <- [[1, 2]]; let a <- m[0][1];", "a") == "int");
    CHECK(symbolType("let xs <- [1, 2]; let n <- len(xs);", "n") == "i64u");
    CHECK(symbolType("let xs <- [1, 2]; let n <- xs |> len;", "n") == "i64u");
    CHECK(semaErrors("let xs <- [1, 2]; let i :: i8u <- 1; let a <- xs[i];") == 0);
    CHECK(semaErrors("let xs <- [1, 2]; let a <- xs[2];") == 1);
    CHECK(semaErrors("let xs <- [1, 2]; let N <- 2; let a <- xs[N];") == 1);
    CHECK(semaErrors("let xs <- [1, 2]; let i <- 1; let a <- xs[i];") == 1);
    CHECK(semaErrors("let xs <- [1, 2]; let a <- xs[0.5];") == 1);
    CHECK(semaErrors("let xs <- [1, 2]; let a <- xs[0, 1];") == 1);
    CHECK(semaErrors("let a <- 5[0];") == 1);
    CHECK(semaErrors("let a <- len(5);") == 1);
    CHECK(semaErrors("let (n :: int) -> len -> (r :: int) { n -| } let a <- len(5);") == 0);
}

// checks methods, which only protocols give a type, where the only one so far is Sized's len, called on its object, piped into with its dot, or called like a function, which picks the method over a function of the same name
TEST_CASE("Sema: methods", "[sema]") {
    CHECK(symbolType("let xs <- [1, 2]; let n <- xs.len();", "n") == "i64u");
    CHECK(symbolType("let xs <- [1, 2]; let n <- xs |> .len;", "n") == "i64u");
    CHECK(symbolType("let xs <- [1, 2]; let n <- .len <| xs;", "n") == "i64u");
    CHECK(symbolType("let m <- [[1, 2]]; let n <- m[0].len();", "n") == "i64u");
    CHECK(symbolType("let (xs :: arr[2] int) -> len -> (r :: int) { 0 -| } let n <- len([1, 2]);", "n") == "i64u");
    CHECK(semaErrors("let xs <- [1, 2]; let n <- xs.len;") == 1);
    CHECK(semaErrors("let xs <- [1, 2]; let n <- xs.len(1);") == 1);
    CHECK(semaErrors("let x <- 5; let n <- x.len();") == 1);
    CHECK(semaErrors("let xs <- [1, 2]; let n <- xs.size();") == 1);
    CHECK(semaErrors("let n <- .len;") == 1);
    CHECK(semaErrors("let x <- 5; let n <- x.field;") == 1);
    // = and == compare arrays, and nothing orders them
    CHECK(semaErrors("let xs <- [1, 2]; let a <- xs = [1, 2] && xs !== [2, 1];") == 0);
    CHECK(semaErrors("let xs <- [1, 2]; let a <- xs < xs;") == 1);
    CHECK(semaErrors("let xs <- [1, 2]; let a <- xs = [1, 2, 3];") == 1);
}

// checks where mut sits on an array: on its elements, they can be written, in front, the whole array can be replaced, a bare mut on an inferred type allows both at every level, and an uninitialized array needs one of them
TEST_CASE("Sema: array mutability", "[sema]") {
    CHECK(semaErrors("let xs :: arr[2] mut int <- [1, 2]; xs[0] <- 3; xs[1] +<- 1;") == 0);
    CHECK(semaErrors("let xs :: arr[2] mut int <- [1, 2]; xs <- [3, 4];") == 1);
    CHECK(semaErrors("let xs :: mut arr[2] int <- [1, 2]; xs <- [3, 4];") == 0);
    CHECK(semaErrors("let xs :: mut arr[2] int <- [1, 2]; xs[0] <- 3;") == 1);
    CHECK(semaErrors("let m :: mut <- [[1, 2], [3, 4]]; m[0][1] <- 5; m[1] <- [0, 0]; m <- m;") == 0);
    CHECK(semaErrors("let m :: arr[2] mut arr[2] int <- [[1, 2], [3, 4]]; m[1] <- [0, 0];") == 0);
    CHECK(semaErrors("let m :: arr[2] mut arr[2] int <- [[1, 2], [3, 4]]; m[0][1] <- 5;") == 1);
    CHECK(semaErrors("let (xs :: arr[2] mut int) -> f -> () { xs[0] <- 1; }") == 0);
    CHECK(semaErrors("let (xs :: arr[2] int) -> f -> () { xs[0] <- 1; }") == 1);
    CHECK(semaErrors("let v :: arr[4] mut int <- _; v[0] <- 1;") == 0);
    CHECK(semaErrors("let v :: arr[4] int <- _;") == 1);
    CHECK(semaErrors("let () -> f -> () { [1, 2][0] <- 3; }") == 1);
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

// checks handles: arrays on the heap and pointers, how they're made, indexed, dereferenced, compared, and what mut means on them
TEST_CASE("Sema: handles", "[sema]") {
    // a length the program only knows at run time, which makes an array on the heap
    const std::string K = "let k :: mut i64u <- 3; ";
    CHECK(symbolType(K + "let xs <- arr[k] mut int();", "xs") == "arr int");
    CHECK(symbolType(K + "let xs <- arr[k] i8(1);", "xs") == "arr i8");
    CHECK(symbolType(K + "let xs <- arr int([1, 2, 3]);", "xs") == "arr int");
    CHECK(symbolType(K + "let p <- ptr int(5);", "p") == "ptr int");
    CHECK(symbolType(K + "let m <- arr[k] mut arr int(arr int([1]));", "m") == "arr arr int");
    CHECK(symbolType(K + "let n :: mut i8u <- 3; let xs <- arr[n] int();", "xs") == "arr int");
    CHECK(symbolType(K + "let xs <- [1, 2] |> arr int;", "xs") == "arr int");
    // a length the program knows when it compiles makes a fixed array
    CHECK(symbolType("let xs <- arr[3] int(7);", "xs") == "arr[3] int");
    CHECK(symbolType("let N <- 2 + 2; let xs <- arr[N] int();", "xs") == "arr[4] int");
    CHECK(semaErrors("let xs <- arr[3] mut int(_); xs[0] <- 1;") == 0);
    CHECK(semaErrors("let xs <- arr[3] int(); xs[0] <- 1;") == 1);
    // making them wrong
    CHECK(semaErrors("let n :: mut <- 3; let xs <- arr[n] int();") == 1);
    CHECK(semaErrors("let xs <- arr[true] int();") == 1);
    CHECK(semaErrors(K + "let xs <- arr mut int(k, 0);") == 1);
    CHECK(semaErrors(K + "let xs <- arr int(true);") == 1);
    CHECK(semaErrors(K + "let xs <- arr int();") == 1);
    CHECK(semaErrors(K + "let xs <- arr[k] int(2, 3);") == 1);
    CHECK(semaErrors(K + "let xs <- arr[k] int(true);") == 1);
    CHECK(semaErrors(K + "let ys <- [1, 2]; let xs <- arr i8(ys);") == 1);
    CHECK(semaErrors(K + "let xs <- arr int([1, 2], 0);") == 1);
    CHECK(semaErrors(K + "let p <- ptr int(1, 2);") == 1);
    CHECK(semaErrors(K + "let p <- ptr int(true);") == 1);
    CHECK(semaErrors(K + "let p <- ptr int(v <- 1);") == 1);
    CHECK(semaErrors(K + "let ps <- arr[k] mut ptr int();") == 1);
    CHECK(semaErrors(K + "let ps <- arr[k] mut ptr int(ptr int(1));") == 0);
    CHECK(semaErrors(K + "let xs <- arr[k] mut int(_);") == 0);
    CHECK(semaErrors(K + "let xs <- arr[k] int(_);") == 1);
    CHECK(semaErrors(K + "let ps <- arr[k] mut ptr int(_);") == 1);
    CHECK(semaErrors(K + "let p :: ptr[A] int <- ptr int(1);") == 1);
    // using them
    CHECK(symbolType(K + "let xs <- arr[k] int(); let a <- xs[2];", "a") == "int");
    CHECK(symbolType(K + "let xs <- arr[k] int(); let n <- xs.len();", "n") == "i64u");
    CHECK(symbolType(K + "let p <- ptr int(5); let v <- p[];", "v") == "int");
    CHECK(semaErrors(K + "let xs <- arr[k] int(); let a <- xs[9];") == 0);
    CHECK(semaErrors(K + "let xs <- arr[k] int(); let a <- xs[];") == 1);
    CHECK(semaErrors(K + "let p <- ptr int(5); let a <- p[0];") == 1);
    CHECK(semaErrors(K + "let p <- ptr int(5); let n <- p.len();") == 1);
    CHECK(semaErrors(K + "let a <- arr[k] int(); let b <- arr[k] int(); let s <- a = b; let d <- a == b;") == 0);
    CHECK(semaErrors(K + "let a <- arr[k] int(); let b <- arr[k] int(); let s <- a < b;") == 1);
    CHECK(semaErrors(K + "let a <- arr[k] int(); let b <- arr[k] i8(); let s <- a = b;") == 1);
    // mut sits where it says, and a binding left to inference writes what the call says
    CHECK(semaErrors(K + "let xs <- arr[k] mut int(); xs[0] <- 1;") == 0);
    CHECK(semaErrors(K + "let xs <- arr[k] int(); xs[0] <- 1;") == 1);
    CHECK(semaErrors(K + "let xs :: arr int <- arr[k] mut int(); xs[0] <- 1;") == 1);
    CHECK(semaErrors(K + "let p <- ptr mut int(1); p[] <- 2;") == 0);
    CHECK(semaErrors(K + "let p <- ptr int(1); p[] <- 2;") == 1);
    CHECK(semaErrors(K + "let p :: mut ptr int <- ptr int(1); p <- ptr int(2);") == 0);
    CHECK(semaErrors(K + "let p <- ptr int(1); p <- ptr int(2);") == 1);
    CHECK(semaErrors(K + "let m <- arr[k] mut arr mut int(arr[k] mut int()); m[0][1] <- 3;") == 0);
    CHECK(semaErrors(K + "let m <- arr[k] mut arr int(arr[k] int()); m[0][1] <- 3;") == 1);
    // nothing proves an uninitialized handle is written before it's freed yet
    CHECK(semaErrors(K + "let xs :: mut arr int <- _;") == 1);
    CHECK(semaErrors(K + "let ps :: arr[2] mut ptr int <- _;") == 1);
}

// checks which handles go on the stack: a binding in a function made with a size the program knows, up to 4 KiB, that never moves or is replaced
TEST_CASE("Sema: handles on the stack", "[sema]") {
    auto isStacked = [](const std::string& input, std::string_view name) {
        Checked checked(input);
        for (auto& symbol : checked.sema.allSymbols()) {
            if (symbol.name == name) return checked.sema.isOnStack(&symbol);
        }
        return false;
    };
    const std::string body = "let () -> f -> () { let k :: mut i64u <- 3; ";
    CHECK(isStacked(body + "let xs <- arr int([1, 2, 3]); }", "xs"));
    CHECK(isStacked(body + "let p <- ptr int(1); }", "p"));
    CHECK(isStacked(body + "let big <- arr[512] int(); let xs <- arr int(big); }", "xs"));
    CHECK(!isStacked(body + "let big <- arr[513] int(); let xs <- arr int(big); }", "xs"));
    CHECK(!isStacked(body + "let xs <- arr[k] int(); }", "xs"));
    CHECK(!isStacked(body + "let xs <- arr int([1, 2]); let ys <- xs; }", "xs"));
    CHECK(!isStacked(body + "let xs :: mut <- arr int([1, 2]); xs <- arr int([3]); }", "xs"));
    CHECK(!isStacked("let () -> f -> (r :: arr int) { let xs <- arr int([1]); xs -| }", "xs"));
    CHECK(!isStacked("let xs <- arr int([1]);", "xs"));
}

// checks ownership: bindings own what they hold and moving hands it on, inputs borrow unless they're unscoped, which have to hand it on, and matches and loops agree on what moved
TEST_CASE("Sema: ownership", "[sema]") {
    const std::string K = "let k :: mut i64u <- 3; ";
    const std::string body = "let () -> f -> () { ";
    // moves and uses after them
    CHECK(semaErrors(K + body + "let a <- arr[k] int(); let b <- a; }") == 0);
    CHECK(semaErrors(K + body + "let a <- arr[k] int(); let b <- a; let n <- a.len(); }") == 1);
    CHECK(semaErrors(K + body + "let a :: mut <- arr[k] int(); let b <- a; a <- arr[k] int(); let n <- a.len(); }") == 0);
    CHECK(semaErrors(K + body + "let a <- ptr int(1); let ps <- [a, a]; }") == 1);
    CHECK(semaErrors(K + body + "let a <- arr[k] int(); let p <- ptr arr int(a); let n <- a.len(); }") == 1);
    CHECK(semaErrors(K + body + "let x <- 1; let y <- x; let z <- x; }") == 0);
    CHECK(semaErrors(K + body + "let xs <- arr[k] int(); let a <- xs[0]; let b <- xs[1]; }") == 0);
    // what can't move
    CHECK(semaErrors(K + body + "let ps <- arr[k] mut ptr int(ptr int(1)); let p <- ps[0]; }") == 1);
    CHECK(semaErrors(K + body + "let p <- ptr ptr int(ptr int(1)); let q <- p[]; }") == 1);
    CHECK(semaErrors(K + "let table <- arr[k] int(); let () -> f -> () { let t <- table; }") == 1);
    CHECK(semaErrors(K + "let a <- arr[k] int(); let b <- a;") == 1);
    // inputs borrow, unscoped ones take over and have to hand it on, and outputs move out
    CHECK(semaErrors(K + "let (xs :: arr int) -> total -> (r :: i64u) { xs.len() -| } " + body + "let a <- arr[k] int(); _ <- total(a); _ <- total(a); }") == 0);
    CHECK(semaErrors(K + "let (xs :: arr int) -> keep -> (r :: arr int) { xs -| }") == 1);
    CHECK(semaErrors(K + "let (xs :: arr int) -> keep -> () { let b <- xs; }") == 1);
    CHECK(semaErrors(K + "let (xs :: mut arr int) -> swap -> () { xs <- arr[k] int(); }") == 1);
    CHECK(semaErrors(K + "let (xs :: arr mut int) -> zero -> () { xs[0] <- 0; }") == 0);
    CHECK(semaErrors(K + "let (xs :: unscoped arr int) -> keep -> (r :: arr int) { xs -| }") == 0);
    CHECK(semaErrors(K + "let (xs :: unscoped arr int) -> drop -> () { let given <- xs; }") == 0);
    CHECK(semaErrors(K + "let (xs :: unscoped arr int) -> lose -> () { }") == 1);
    CHECK(semaErrors(K + "let (xs :: unscoped arr int) -> keep -> (r :: arr int) { xs -| } " + body + "let a :: mut <- arr[k] int(); a <- keep(a); let n <- a.len(); }") == 0);
    CHECK(semaErrors(K + "let (xs :: unscoped arr int) -> keep -> (r :: arr int) { xs -| } " + body + "let a <- arr[k] int(); _ <- keep(a); let n <- a.len(); }") == 1);
    CHECK(semaErrors(K + "let (n :: unscoped int) -> f -> () { }") == 1);
    CHECK(semaErrors(K + "let xs :: unscoped arr int <- arr[k] int();") == 1);
    CHECK(semaErrors(K + "let () -> make -> (r :: arr int) { arr[k] int() -| }") == 0);
    CHECK(semaErrors(K + "let () -> make -> (r :: unscoped arr int) { arr[k] int() -| }") == 1);
    // a move in some arms of a match is a move after it, which an unscoped input can't have
    CHECK(semaErrors(K + "let (c :: bool) -> f -> () { let a <- arr[k] int(); match (c) { true -> { let b <- a; } false -> { } } }") == 0);
    CHECK(semaErrors(K + "let (c :: bool) -> f -> () { let a <- arr[k] int(); match (c) { true -> { let b <- a; } false -> { } } let n <- a.len(); }") == 1);
    CHECK(semaErrors(K + "let (c :: bool, xs :: unscoped arr int) -> f -> (r :: arr int) { match (c) { true -> { xs -| f } false -> { } } arr[k] int() -| }") == 1);
    CHECK(semaErrors(K + "let (c :: bool, xs :: unscoped arr int) -> f -> (r :: arr int) { match (c) { true -> { xs -| f } false -> { xs -| f } } }") == 0);
    // a binding from outside a loop has to hold a value at the end of every iteration
    CHECK(semaErrors(K + body + "let a <- arr[k] int(); let i :: mut <- 0; repeat (i < 3) { let b <- a; i +<- 1; } }") == 1);
    CHECK(semaErrors(K + body + "let a :: mut <- arr[k] int(); let i :: mut <- 0; repeat (i < 3) { let b <- a; a <- arr[k] int(); i +<- 1; } }") == 0);
    CHECK(semaErrors(K + body + "let a <- arr[k] int(); repeat { let b <- a; -| repeat } }") == 0);
    // a binder borrows the subject
    CHECK(semaErrors(K + body + "let a <- arr[k] int(); match (a) { b -> { let c <- b; } } }") == 1);
}

// checks that what sema can't check yet is reported once and poisons silently from there
TEST_CASE("Sema: unsupported forms", "[sema]") {
    CHECK(semaErrors("let x :: Id <- 5;") == 1);
    CHECK(semaErrors("type Id <- int;") == 1);
    CHECK(semaErrors("import [List] <- list;") == 1);
    CHECK(semaErrors("let [a, b] <- p;") == 2);
    CHECK(semaErrors("let x :: Id <- 5; let y <- x; let z <- y;") == 1);
    CHECK(semaErrors("let s <- \"hi\";") == 1);
    CHECK(semaErrors("let p <- [x <- 1, y <- 2];") == 1);
    CHECK(semaErrors("let x <- 0; repeat (i) <- x { }") == 1);
    CHECK(semaErrors("let (a :: int, b :: int) -> + -> (r :: int) { a -| }") == 1);
    // a keyword as a function's name is the parser's one error, which names it as a keyword, while and, or, and not are plain names
    Checked keyword("let () -> wit -> () { }");
    REQUIRE(keyword.diag.errorCount() == 1);
    CHECK(keyword.diag.all()[0].message == "cherimoya expected a function name (an identifier or an overloadable operator) in 'let (inputs) -> name -> (outputs) { body }', got the keyword 'wit' instead");
    CHECK(semaErrors("let () -> and -> () { } let () -> or -> () { } let () -> not -> () { }") == 0);
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

// checks that a program that will run has a top-level start shaped the way the runtime calls it
TEST_CASE("Sema: entry point", "[sema]") {
    CHECK(entryErrors("let () -> start -> (code :: int) { 0 -| }") == 0);
    CHECK(entryErrors("let LIMIT <- 4; let () -> start -> (code :: int) { LIMIT -| }") == 0);
    CHECK(entryErrors("let () -> main -> (code :: int) { 0 -| }") == 1);
    CHECK(entryErrors("let (a :: int) -> start -> (code :: int) { a -| }") == 1);
    CHECK(entryErrors("let () -> start -> () { }") == 1);
    CHECK(entryErrors("let () -> start -> (code :: bool) { true -| }") == 1);
    CHECK(entryErrors("let start <- 5;") == 1);
    CHECK(entryErrors("let () -> f -> () { let () -> start -> (code :: int) { 0 -| } }") == 1);
    CHECK(semaErrors("let () -> main -> (code :: int) { 0 -| }") == 0);
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

// checks that literals from 0 to 1 and true and false become boolfs where one is expected, and nothing outside that range does
TEST_CASE("Sema: boolf literals", "[sema]") {
    CHECK(symbolType("let x :: boolf <- 0.7;", "x") == "boolf");
    CHECK(symbolType("let x :: boolf <- 0;", "x") == "boolf");
    CHECK(symbolType("let x :: boolf <- 1;", "x") == "boolf");
    CHECK(symbolType("let x :: boolf <- 1.0;", "x") == "boolf");
    CHECK(symbolType("let x :: boolf <- true;", "x") == "boolf");
    CHECK(symbolType("let x <- (0.25 :: boolf);", "x") == "boolf");
    CHECK(symbolType("let x <- true;", "x") == "bool");
    CHECK(semaErrors("let x :: boolf <- 2;") == 1);
    CHECK(semaErrors("let x :: boolf <- 1.5;") == 1);
    CHECK(semaErrors("let x :: boolf <- -0.5;") == 1);
    CHECK(semaErrors("let x :: boolf <- -1;") == 1);
}

// checks that && || ^^ ! and * work on boolfs, that comparisons turn one into a bool, and that a boolf is never a condition
TEST_CASE("Sema: boolf operators", "[sema]") {
    CHECK(symbolType("let a :: boolf <- 0.3; let b :: boolf <- 0.6; let x <- a && b;", "x") == "boolf");
    CHECK(symbolType("let a :: boolf <- 0.3; let x <- 0.5 || a;", "x") == "boolf");
    CHECK(symbolType("let a :: boolf <- 0.3; let x <- a ^^ true;", "x") == "boolf");
    CHECK(symbolType("let a :: boolf <- 0.3; let x <- !a;", "x") == "boolf");
    CHECK(symbolType("let a :: boolf <- 0.3; let x <- a * 0.5;", "x") == "boolf");
    CHECK(symbolType("let x :: boolf <- 0.2 && 0.9;", "x") == "boolf");
    CHECK(symbolType("let a :: boolf <- 0.3; let x <- a > 0.5;", "x") == "bool");
    CHECK(symbolType("let a :: boolf <- 0.3; let x <- 0 < a <= 1;", "x") == "bool");
    CHECK(semaErrors("let a :: mut boolf <- 0.3; a *<- 0.5; a &&<- 0.2; a ||<- true; a ^^<- 0.9; let x <- a;") == 0);
    CHECK(semaErrors("let b :: mut <- true; b &&<- 1 < 2; b ||<- false; b ^^<- true;") == 0);
    CHECK(semaErrors("let b :: mut <- true; b &&<- 0.5;") == 1);
    CHECK(semaErrors("let n :: mut <- 3; n &&<- 1;") == 1);
    CHECK(semaErrors("let a :: boolf <- 0.3; let x <- a + 0.5;") == 1);
    CHECK(semaErrors("let a :: boolf <- 0.3; let x <- -a;") == 1);
    CHECK(semaErrors("let a :: boolf <- 0.3; let x <- a && true && (1 < 2);") == 1);
    CHECK(semaErrors("let a :: boolf <- 0.3; let x <- a * 2.5;") == 1);
    CHECK(semaErrors("let a :: boolf <- 0.3; match { (a) -> { } }") == 1);
    CHECK(semaErrors("let a :: boolf <- 0.3; repeat (a) { }") == 1);
    CHECK(semaErrors("let a :: boolf <- 0.3; match { (a > 0.5) -> { } }") == 0);
    CHECK(semaErrors("let a :: boolf <- 0.3; let n <- match (a) { 0.5 -> { 1 -| } true -> { 2 -| } _ -> { 3 -| } };") == 0);
    CHECK(semaErrors("let a :: boolf <- 0.3; match (a) { 0 -> { } 1 -> { } }") == 1);
}

// checks that a boolf converts to and from floats and its raw byte, that a bool becomes one, and that it only becomes a bool by a cutoff
TEST_CASE("Sema: boolf conversions", "[sema]") {
    CHECK(symbolType("let t :: f32 <- 0.4; let x <- boolf(t);", "x") == "boolf");
    CHECK(symbolType("let a :: boolf <- 0.4; let x <- f64(a);", "x") == "float");
    CHECK(symbolType("let b :: i8u <- 200; let x <- boolf(b);", "x") == "boolf");
    CHECK(symbolType("let b :: i8uw <- 200; let x <- boolf(b);", "x") == "boolf");
    CHECK(symbolType("let a :: boolf <- 0.4; let x <- i8uw(a);", "x") == "i8uw");
    CHECK(symbolType("let x <- boolf(1 < 2);", "x") == "boolf");
    CHECK(symbolType("let a :: boolf <- 0.4; let x <- boolf(a);", "x") == "boolf");
    CHECK(semaErrors("let a :: boolf <- 0.4; let x <- bool(a);") == 1);
    CHECK(semaErrors("let a :: boolf <- 0.4; let x <- int(a);") == 1);
    CHECK(semaErrors("let n :: i16u <- 2; let x <- boolf(n);") == 1);
    CHECK(semaErrors("let n :: i8 <- 2; let x <- boolf(n);") == 1);
    CHECK(semaErrors("let x <- boolf('a');") == 1);
}

// checks input as a whole program, then applies one fix of the first diagnostic that has any, giving the fixed text, or "" when nothing has a fix
static std::string fixed(const std::string& input, size_t which = 0, bool isEntry = false) {
    SourceManager sm;
    sm.loadString(input);
    DiagnosticEngine diag(sm, nullptr);
    auto tokens = Lexer(diag).lex();
    auto program = Parser(tokens, diag).parseProgram();
    Sema sema(diag);
    sema.check(program, isEntry ? Role::Program : Role::Module);
    for (auto& diagnostic : diag.all()) {
        if (diagnostic.fixes.size() > which) return applyEdits(input, diagnostic.fixes[which].edits);
    }
    return "";
}

// checks that each fix rewrites the source into what its diagnostic asks for
TEST_CASE("Sema: fixes", "[sema]") {
    // the lexer's spacing and the parser's shapes
    CHECK(fixed("let x <--1;") == "let x <- -1;");
    CHECK(fixed("let e <- ();") == "let e <- [];");
    CHECK(fixed("let t <- (1, 2);") == "let t <- [1, 2];");
    CHECK(fixed("let x :: mut int;") == "let x :: mut int <- _;");
    CHECK(fixed("let r <- [lo <- 1, ..., 5];") == "let r <- [1, ..., 5];");
    // arrays
    CHECK(fixed("let xs <- [1, 2]; xs[0] <- 3;") == "let xs :: mut <- [1, 2]; xs[0] <- 3;");
    CHECK(fixed("let xs :: arr[2] int <- [1, 2]; xs[0] <- 3;") == "");
    CHECK(fixed("let xs <- [1, 2]; let i <- 1; let a <- xs[i];") == "let xs <- [1, 2]; let i <- 1; let a <- xs[i64u(i)];");
    CHECK(fixed("let xs <- [1, 2]; let n <- xs.len;") == "let xs <- [1, 2]; let n <- xs.len();");
    CHECK(fixed("let n :: mut <- 3; let xs <- arr[n] int();") == "let n :: mut <- 3; let xs <- arr[i64u(n)] int();");
    CHECK(fixed("let n :: mut i64u <- 3; let xs <- arr mut int(n, 0);") == "let n :: mut i64u <- 3; let xs <- arr[n] mut int(0);");
    // arguments and defaults
    const std::string add = "let (a :: int, b :: int) -> add -> (r :: int) { a + b -| } ";
    CHECK(fixed(add + "let x <- add(a <- 1, 2);") == "");
    CHECK(fixed("let x <- i32(v <- 1);") == "let x <- i32(1);");
    CHECK(fixed("let () -> g -> (r :: int <- 1) { 1 -| }") == "let () -> g -> (r :: int) { 1 -| }");
    // both ways to parenthesize a mix that needs it
    CHECK(fixed("let a <- 1; let b <- 2; let c <- 3; let x <- a = b != c;") == "let a <- 1; let b <- 2; let c <- 3; let x <- (a = b) != c;");
    CHECK(fixed("let a <- 1; let b <- 2; let c <- 3; let x <- a = b != c;", 1) == "let a <- 1; let b <- 2; let c <- 3; let x <- a = (b != c);");
    CHECK(fixed("let x <- a $ b + c >>= d;") == "let x <- (a $ b + c) >>= d;");
    CHECK(fixed("let x <- a $ b + c >>= d;", 1) == "let x <- a $ (b + c >>= d);");
    // sema's discards, mutability, catch-alls, and cutoffs
    CHECK(fixed("let () -> f -> (r :: int) { 1 -| }\nlet () -> g -> () { f(); }") == "let () -> f -> (r :: int) { 1 -| }\nlet () -> g -> () { _ <- f(); }");
    CHECK(fixed("let () -> g -> () { let x <- 1; x <- 2; }") == "let () -> g -> () { let x :: mut <- 1; x <- 2; }");
    CHECK(fixed("let () -> g -> () { let x :: int <- 1; x <- 2; }") == "let () -> g -> () { let x :: mut int <- 1; x <- 2; }");
    CHECK(fixed("let (n :: int) -> g -> () { n <- 2; }") == "let (n :: mut int) -> g -> () { n <- 2; }");
    CHECK(fixed("let n <- 3; match (n) { 0 -> { } }") == "let n <- 3; match (n) { 0 -> { } _ -> { } }");
    CHECK(fixed("let n <- 3;\nmatch (n) {\n    0 -> { }\n}\n") == "let n <- 3;\nmatch (n) {\n    0 -> { }\n    _ -> { }\n}\n");
    CHECK(fixed("let a :: boolf <- 0.3; match { (a) -> { } }") == "let a :: boolf <- 0.3; match { (a > 0.5) -> { } }");
    CHECK(fixed("let a :: boolf <- 0.3; match { (a && 0.9) -> { } }") == "let a :: boolf <- 0.3; match { ((a && 0.9) > 0.5) -> { } }");
    CHECK(fixed("let a :: boolf <- 0.3; let b <- bool(a);") == "let a :: boolf <- 0.3; let b <- a > 0.5;");
    // code that never runs comes out whole lines at a time, leaving the functions, which are hoisted
    CHECK(fixed("let () -> f -> (r :: int) {\n    1 -|\n    let x <- 2;\n    let y <- 3;\n}\n") == "let () -> f -> (r :: int) {\n    1 -|\n}\n");
    CHECK(fixed("let n <- 3;\nmatch (n) {\n    _ -> { }\n    0 -> { }\n    1 -> { }\n}\n") == "let n <- 3;\nmatch (n) {\n    _ -> { }\n}\n");
    // a program with no start gets one at the end
    CHECK(fixed("let x <- 1;\n", 0, true) == "let x <- 1;\n\nlet () -> start -> (code :: int) { 0 -| }\n");
    // a diagnostic with no single mechanical fix has none
    CHECK(fixed("let x :: i8 <- 300;").empty());
}
