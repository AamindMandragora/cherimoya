#include <catch2/catch_test_macros.hpp>

#include <sstream>
#include <string>
#include <vector>

#include "server.hpp"

// the offset of the nth occurrence of a piece of text, counting from 0, which is how the tests point at a name
static size_t offsetOf(const std::string& text, const std::string& piece, size_t nth = 0) {
    size_t at = text.find(piece);
    for (size_t i = 0; i < nth; i++) at = text.find(piece, at + 1);
    REQUIRE(at != std::string::npos);
    return at;
}

// a string field of a JSON object, or "" if it isn't there
static std::string stringOf(const Value& value, const std::string& key) {
    auto* object = value.getAsObject();
    return object && object->getString(key) ? object->getString(key)->str() : "";
}

// frames messages the way an editor sends them, each with its Content-Length header
static std::string framed(const std::vector<std::string>& messages) {
    std::string out;
    for (auto& message : messages) out += "Content-Length: " + std::to_string(message.size()) + "\r\n\r\n" + message;
    return out;
}

// runs a server over a whole session and returns every message it sent back, in order, with its exit code
struct Session {
    std::vector<Value> sent;
    int exitCode;
};
static Session serve(const std::vector<std::string>& messages) {
    std::istringstream in(framed(messages));
    std::ostringstream out;
    int exitCode = Server(in, out).run();
    Session session{{}, exitCode};
    std::istringstream replies(out.str());
    while (auto body = readMessage(replies)) {
        auto parsed = llvm::json::parse(*body);
        REQUIRE(bool(parsed));
        session.sent.push_back(std::move(*parsed));
    }
    return session;
}

// checks that positions count lines from 0 and columns in UTF-16 code units, and that converting back gives the same offset
TEST_CASE("LSP: positions", "[lsp]") {
    std::string text = "ab\nc\xC3\xA9" "d\n\xF0\x9F\x98\x80x";
    LineIndex lines(text);
    CHECK(lines.position(0).line == 0);
    CHECK(lines.position(4).line == 1);
    CHECK(lines.position(4).character == 1);
    // é is two bytes and one unit, and the emoji is four bytes and two units
    CHECK(lines.position(6).character == 2);
    CHECK(lines.position(12).character == 2);
    for (size_t offset : {0, 1, 3, 4, 6, 7, 8, 12, 13}) CHECK(lines.offset(lines.position(offset)) == offset);
    // a column past a line's end is that line's end, and a line past the last is the end of the text
    CHECK(lines.offset({0, 99}) == 2);
    CHECK(lines.offset({9, 0}) == text.size());
}

// checks that each diagnostic carries its range, its severity, and its notes
TEST_CASE("LSP: diagnostics", "[lsp]") {
    Document document("file:///t.moya", "let x <- 1; let x <- 2;\nlet y <- z;");
    auto result = diagnostics(document);
    REQUIRE(result.size() == 2);
    auto& clash = *result[0].getAsObject();
    CHECK(clash.getInteger("severity") == 1);
    CHECK(clash.getArray("relatedInformation")->size() == 1);
    auto& undeclared = *result[1].getAsObject();
    CHECK(undeclared.getObject("range")->getObject("start")->getInteger("line") == 1);
    Document warned("file:///w.moya", "let () -> f -> () { -| f let y <- 1; }");
    CHECK(diagnostics(warned)[0].getAsObject()->getInteger("severity") == 2);
}

// checks that tokens come five numbers each, sorted and relative, with names colored by what they mean
TEST_CASE("LSP: semantic tokens", "[lsp]") {
    std::string text = "# a comment\nlet (n :: int) -> f -> (r :: int) { let x :: mut <- n; x -| }";
    Document document("file:///t.moya", text);
    auto data = semanticTokens(document);
    REQUIRE(data.size() % 5 == 0);
    // the comment comes first, on line 0, as long as the comment
    CHECK(data[0].getAsInteger() == 0);
    CHECK(data[2].getAsInteger() == 11);
    CHECK(data[3].getAsInteger() == static_cast<int64_t>(TokenType::Comment));
    // finds the token that starts at an offset and returns its type and modifiers
    auto tokenAt = [&](size_t offset) {
        Position target = document.lines->position(offset);
        Position at{0, 0};
        for (size_t i = 0; i < data.size(); i += 5) {
            at.line += static_cast<uint32_t>(*data[i].getAsInteger());
            at.character = (data[i].getAsInteger() == 0 ? at.character : 0) + static_cast<uint32_t>(*data[i + 1].getAsInteger());
            if (at.line == target.line && at.character == target.character) return std::make_pair(*data[i + 3].getAsInteger(), *data[i + 4].getAsInteger());
        }
        return std::make_pair(int64_t(-1), int64_t(-1));
    };
    CHECK(tokenAt(offsetOf(text, "let")) == std::make_pair(int64_t(TokenType::Keyword), int64_t(0)));
    CHECK(tokenAt(offsetOf(text, "int")) == std::make_pair(int64_t(TokenType::Type), int64_t(0)));
    CHECK(tokenAt(offsetOf(text, "f ->")) == std::make_pair(int64_t(TokenType::Function), int64_t(Declaration)));
    CHECK(tokenAt(offsetOf(text, "n ::")) == std::make_pair(int64_t(TokenType::Parameter), int64_t(Declaration | Readonly)));
    CHECK(tokenAt(offsetOf(text, "n;")) == std::make_pair(int64_t(TokenType::Parameter), int64_t(Readonly)));
    CHECK(tokenAt(offsetOf(text, "x -|")) == std::make_pair(int64_t(TokenType::Variable), int64_t(0)));
    CHECK(tokenAt(offsetOf(text, "<-")) == std::make_pair(int64_t(TokenType::Operator), int64_t(0)));
}

// checks that hover shows a name as it's declared, that definition jumps there, and that references find every use
TEST_CASE("LSP: hover, definition, and references", "[lsp]") {
    std::string text = "let (a :: int, b :: int) -> add -> (r :: int) { a + b -| }\nlet () -> main -> (code :: int) { let x :: mut <- add(1, 2); x <- add(x, 3); x -| }";
    Document document("file:///t.moya", text);
    auto shown = [&](size_t offset) { return stringOf(*hover(document, offset).getAsObject()->get("contents"), "value"); };
    CHECK(shown(offsetOf(text, "add(1")) == "```cherimoya\nlet (a :: int, b :: int) -> add -> (r :: int)\n```");
    CHECK(shown(offsetOf(text, "x <-")) == "```cherimoya\nlet x :: mut int\n```");
    CHECK(shown(offsetOf(text, "a +")) == "```cherimoya\na :: int\n```");
    CHECK(hover(document, offsetOf(text, "{")).kind() == Value::Null);
    // a built-in type or keyword shows what it means
    CHECK(shown(offsetOf(text, "int")) == "```cherimoya\nint\n```\n`int` is `i64`, a 64-bit signed integer that traps on overflow.");
    CHECK(shown(offsetOf(text, "let")) == "```cherimoya\nlet\n```\nDeclares a binding, `let x <- 5;`, or a function, `let (n :: int) -> f -> (r :: int) { ... }`.");
    CHECK(typeHelp("i8uw") == "An 8-bit unsigned integer that wraps on overflow, keeping the low bits like the hardware does.");
    CHECK(typeHelp("i128u") == "A 128-bit unsigned integer that traps on overflow.");
    CHECK(typeHelp("f16n") == "IEEE half precision, which can hold a NaN and orders it below every number.");
    CHECK(typeHelp("f8") == typeHelp("f8e4m3"));
    CHECK(typeHelp("char") == "One ASCII character, 0 to 7F, held in a byte.");
    CHECK(typeHelp("boolf").find("fuzzy boolean") != std::string::npos);
    CHECK(builtinHelp("add").empty());
    auto jump = definition(document, offsetOf(text, "add(x"));
    CHECK(jump.getAsObject()->getObject("range")->getObject("start")->getInteger("character") == 28);
    CHECK(references(document, offsetOf(text, "add(1"), true).size() == 3);
    CHECK(references(document, offsetOf(text, "add(1"), false).size() == 2);
    CHECK(references(document, offsetOf(text, "x <-"), true).size() == 4);
}

// checks that a declaration's documentation is the comments right above it and the one at the end of its line, and that hover, completion, and signature help show it
TEST_CASE("LSP: documentation comments", "[lsp]") {
    std::string text =
        "# not the docs, since a blank line follows\n"
        "\n"
        "# adds two numbers\n"
        "#   and gives their sum\n"
        "let (\n"
        "    a :: int,  # the first\n"
        "    b :: int\n"
        ") -> add -> (r :: int) { a + b -| }\n"
        "let limit <- 4096;  # bytes per chunk\n"
        "let (n :: int) -> f -> (r :: int) { n -| }  # the identity\n"
        "let plain <- 1;\n"
        "let () -> main -> (code :: int) { add(limit, ) -| }";
    Document document("file:///t.moya", text);
    auto shown = [&](size_t offset) { return stringOf(*hover(document, offset).getAsObject()->get("contents"), "value"); };
    CHECK(shown(offsetOf(text, "add ->")) == "```cherimoya\nlet (a :: int, b :: int) -> add -> (r :: int)\n```\nadds two numbers\n  and gives their sum");
    CHECK(shown(offsetOf(text, "a ::")) == "```cherimoya\na :: int\n```\nthe first");
    CHECK(shown(offsetOf(text, "b ::")) == "```cherimoya\nb :: int\n```");
    CHECK(shown(offsetOf(text, "limit <-")) == "```cherimoya\nlet limit :: int\n```\nbytes per chunk");
    // the identity is f's, since n is declared before f on that line
    CHECK(shown(offsetOf(text, "f ->")) == "```cherimoya\nlet (n :: int) -> f -> (r :: int)\n```\nthe identity");
    CHECK(shown(offsetOf(text, "n ::")) == "```cherimoya\nn :: int\n```");
    CHECK(shown(offsetOf(text, "plain")) == "```cherimoya\nlet plain :: int\n```");
    // completion and signature help carry the same documentation
    std::string completed;
    for (auto& item : completion(document, offsetOf(text, "limit, )"))) {
        if (stringOf(item, "label") == "limit") completed = stringOf(*item.getAsObject()->get("documentation"), "value");
    }
    CHECK(completed == "bytes per chunk");
    auto help = signatureHelp(document, offsetOf(text, ", )") + 2);
    auto& signature = *(*help.getAsObject()->getArray("signatures"))[0].getAsObject();
    CHECK(stringOf(*signature.get("documentation"), "value") == "adds two numbers\n  and gives their sum");
    auto& parameters = *signature.getArray("parameters");
    CHECK(stringOf(*parameters[0].getAsObject()->get("documentation"), "value") == "the first");
    CHECK(parameters[1].getAsObject()->get("documentation") == nullptr);
}

// checks that a diagnostic's fixes come back as quick fixes on the range it touches, where a diagnostic's only fix is preferred and one with two choices has neither preferred
TEST_CASE("LSP: quick fixes", "[lsp]") {
    std::string text = "let () -> g -> () { let x <- 1; x <- 2; }\nlet a <- 1; let b <- 2; let c <- 3; let y <- a = b != c;";
    Document document("file:///t.moya", text);
    auto at = [&](const std::string& what) {
        size_t offset = offsetOf(text, what);
        return codeActions(document, Span{offset, offset});
    };
    auto mut = at("x <- 2");
    REQUIRE(mut.size() == 1);
    auto& action = *mut[0].getAsObject();
    CHECK(stringOf(mut[0], "title") == "Make 'x' mutable");
    CHECK(stringOf(mut[0], "kind") == "quickfix");
    CHECK(action.getBoolean("isPreferred") == true);
    auto& edits = *action.getObject("edit")->getObject("changes")->getArray("file:///t.moya");
    REQUIRE(edits.size() == 1);
    CHECK(stringOf(edits[0], "newText") == " :: mut");
    CHECK(edits[0].getAsObject()->getObject("range")->getObject("start")->getInteger("character") == 25);
    auto mixed = at("!= c");
    REQUIRE(mixed.size() == 2);
    CHECK(mixed[0].getAsObject()->getBoolean("isPreferred") == false);
    CHECK(at("let () ->").empty());
}

// checks every kind of inlay hint: an inferred type with the edit that writes it in, parameter names but not for an argument that's already the parameter's name, a named argument, or a pipe's argument, the defaults a call leaves out, the type of each step of a pipe spread over lines but not its last, and labels on a long function's closing brace but not a short one's
TEST_CASE("LSP: inlay hints", "[lsp]") {
    std::string text =
        "let (a :: int, b :: int) -> add -> (r :: int) { a + b -| }\n"
        "let (n :: int) -> double -> (r :: int) { n * 2 -| }\n"
        "let () -> main -> (code :: int) {\n"
        "    let a <- 1;\n"
        "    let total :: int <- add(a, double(2));\n"
        "    let piped <- total\n"
        "        |> double  # twice\n"
        "        |> double;\n"
        "    let flat <- piped |> double;\n"
        "\n"
        "\n"
        "    flat -|\n"
        "}\n";
    Document document("file:///t.moya", text);
    auto hints = inlayHints(document, Span{0, text.size()});
    std::vector<std::string> labels;
    for (auto& hint : hints) labels.push_back(stringOf(hint, "label"));
    CHECK(labels == std::vector<std::string>{":: int", "b <-", "n <-", ":: int", ":: int", ":: int", ":: int", "# main"});
    // the first hint is a's type, right after its name, with the edit that writes it in
    auto& first = *hints[0].getAsObject();
    CHECK(first.getInteger("kind") == 1);
    CHECK(first.getObject("position")->getInteger("line") == 3);
    CHECK(first.getObject("position")->getInteger("character") == 9);
    CHECK(stringOf((*first.getArray("textEdits"))[0], "newText") == " :: int");
    CHECK(hints[1].getAsObject()->getInteger("kind") == 2);
    // the steps are total, at the end of its line, and the first double, before its comment
    CHECK(hints[4].getAsObject()->getObject("position")->getInteger("line") == 5);
    CHECK(hints[5].getAsObject()->getObject("position")->getInteger("line") == 6);
    CHECK(hints[5].getAsObject()->getObject("position")->getInteger("character") == 17);
    CHECK(hints[7].getAsObject()->getObject("position")->getInteger("line") == 12);
    // a range only gets the hints inside it
    CHECK(inlayHints(document, Span{0, offsetOf(text, "let () -> main")}).empty());
    // a named argument gets no hint, only the last unnamed argument's hint writes itself in, and the defaults a call leaves out go before its ), which a pipe of a bare name doesn't have
    std::string named =
        "let (a :: int, b :: int <- 10, c :: int <- a + b) -> f -> (r :: int) { a + b + c -| }\n"
        "let x <- f(1, 2);\n"
        "let y <- f(c <- 3, a <- 1);\n"
        "let z <- 4 |> f;\n"
        "let w <- 5 |> f(c <- 1);\n";
    Document defaults("file:///t.moya", named);
    auto defaultHints = inlayHints(defaults, Span{0, named.size()});
    std::vector<std::string> defaultLabels;
    for (auto& hint : defaultHints) defaultLabels.push_back(stringOf(hint, "label"));
    CHECK(defaultLabels == std::vector<std::string>{":: int", "a <-", "b <-", ", c <- a + b", ":: int", ", b <- 10", ":: int", ":: int", ", b <- 10"});
    CHECK(defaultHints[1].getAsObject()->getArray("textEdits") == nullptr);
    CHECK(stringOf((*defaultHints[2].getAsObject()->getArray("textEdits"))[0], "newText") == "b <- ");
    CHECK(defaultHints[3].getAsObject()->getObject("position")->getInteger("character") == 15);
}

// checks that rename edits every use, and refuses a non-name, a clash in one scope, and a rename that would change what a use means
TEST_CASE("LSP: rename", "[lsp]") {
    std::string text = "let (n :: int) -> f -> (r :: int) { let x <- n; let y <- 2; { let z <- x; } x + y -| }";
    Document document("file:///t.moya", text);
    std::string error;
    auto edit = rename(document, offsetOf(text, "x <-"), "total", error);
    CHECK(error.empty());
    CHECK(edit.getAsObject()->getObject("changes")->getArray("file:///t.moya")->size() == 3);
    for (const char* bad : {"let", "int", "_", "two words", "1x"}) {
        error.clear();
        rename(document, offsetOf(text, "x <-"), bad, error);
        CHECK_FALSE(error.empty());
    }
    error.clear();
    rename(document, offsetOf(text, "x <-"), "y", error);
    CHECK(error == "'y' is already declared in the same scope");
    // z is declared further in and visible where x is used inside the block, so x can't become z
    error.clear();
    rename(document, offsetOf(text, "x <-"), "z", error);
    CHECK(error == "renaming would make a use of 'x' mean the other 'z' instead");
    // the other way round, z can't become x, since z's block would then hide the outer x from the use beside it
    std::string shadow = "let () -> f -> () { let x <- 1; { let z <- 2; let w <- x + z; } }";
    Document other("file:///s.moya", shadow);
    error.clear();
    rename(other, offsetOf(shadow, "z <-"), "x", error);
    CHECK(error == "renaming would make a use of 'x' mean the renamed 'z' instead");
}

// checks that the outline has every top-level function and binding, with nested functions under theirs
TEST_CASE("LSP: outline", "[lsp]") {
    Document document("file:///t.moya", "let LIMIT <- 3;\nlet () -> f -> () { let () -> g -> () { } }\n@inline let () -> h -> () { }");
    auto outline = documentSymbols(document);
    REQUIRE(outline.size() == 3);
    CHECK(stringOf(outline[0], "name") == "LIMIT");
    CHECK(outline[0].getAsObject()->getInteger("kind") == 13);
    CHECK(stringOf(outline[1], "name") == "f");
    CHECK(stringOf(outline[1], "detail") == "let () -> f -> ()");
    REQUIRE(outline[1].getAsObject()->getArray("children")->size() == 1);
    CHECK(stringOf((*outline[1].getAsObject()->getArray("children"))[0], "name") == "g");
    CHECK(stringOf(outline[2], "name") == "h");
}

// checks that completion offers only what's visible, with names of the type the position wants first
TEST_CASE("LSP: completion", "[lsp]") {
    std::string text = "let (b :: bool, n :: int) -> f -> (r :: int) { let later <- 1; { let inner <- 2; } n -| }\nlet () -> g -> (r :: bool) { true -| }";
    Document document("file:///t.moya", text);
    // finds an item by its label, or returns null
    auto items = completion(document, offsetOf(text, "n -|"));
    auto item = [&](const std::string& label) -> const Object* {
        for (auto& candidate : items) if (stringOf(candidate, "label") == label) return candidate.getAsObject();
        return nullptr;
    };
    REQUIRE(item("n"));
    REQUIRE(item("b"));
    CHECK(item("later"));
    CHECK_FALSE(item("inner"));
    CHECK(item("f"));
    CHECK(item("let"));
    CHECK(item("i8"));
    // the yield wants f's output, an int, so n and f come before b and g
    CHECK(item("n")->getString("sortText")->str() == "0n");
    CHECK(item("f")->getString("sortText")->str() == "0f");
    CHECK(item("b")->getString("sortText")->str() == "1b");
    CHECK(item("g")->getString("sortText")->str() == "1g");
    CHECK(item("let")->getString("sortText")->str() == "2let");
    CHECK(item("i8")->getObject("documentation")->getString("value")->str() == "An 8-bit signed integer that traps on overflow.");
}

// checks that signature help shows the call the cursor is in and which parameter it's at
TEST_CASE("LSP: signature help", "[lsp]") {
    std::string text = "let (a :: int, b :: int) -> add -> (r :: int) { a + b -| }\nlet () -> main -> (code :: int) { add(1, 2) -| }";
    Document document("file:///t.moya", text);
    auto help = signatureHelp(document, offsetOf(text, "1,") + 1);
    REQUIRE(help.kind() == Value::Object);
    auto& signature = *(*help.getAsObject()->getArray("signatures"))[0].getAsObject();
    CHECK(signature.getString("label")->str() == "let (a :: int, b :: int) -> add -> (r :: int)");
    auto& second = *(*signature.getArray("parameters"))[1].getAsObject()->getArray("label");
    CHECK(second[0].getAsInteger() == 15);
    CHECK(second[1].getAsInteger() == 23);
    CHECK(help.getAsObject()->getInteger("activeParameter") == 0);
    CHECK(signatureHelp(document, offsetOf(text, "2)")).getAsObject()->getInteger("activeParameter") == 1);
    CHECK(signatureHelp(document, offsetOf(text, "a +")).kind() == Value::Null);
    // a named argument highlights the input it names, the signature shows defaults, and between arguments it's the first input nothing before has gone to
    std::string named = "let (a :: int, b :: int <- 10, c :: int <- a + b) -> f -> (r :: int) { a + b + c -| }\nlet x <- f(c <- 3, a <- 1);";
    Document defaults("file:///t.moya", named);
    auto inNamed = signatureHelp(defaults, offsetOf(named, "3,"));
    CHECK(stringOf((*inNamed.getAsObject()->getArray("signatures"))[0], "label") == "let (a :: int, b :: int <- 10, c :: int <- a + b) -> f -> (r :: int)");
    CHECK(inNamed.getAsObject()->getInteger("activeParameter") == 2);
    CHECK(signatureHelp(defaults, offsetOf(named, " a <- 1")).getAsObject()->getInteger("activeParameter") == 0);
    CHECK(signatureHelp(defaults, offsetOf(named, "1);")).getAsObject()->getInteger("activeParameter") == 0);
    // hovering over an input shows its default
    auto onInput = hover(defaults, offsetOf(named, "b ::"));
    CHECK(onInput.getAsObject()->getObject("contents")->getString("value")->str().find("b :: int <- 10") != std::string::npos);
}

// checks a whole session: the server says what it can do, publishes diagnostics for an opened document, applies incremental changes, answers a request, and exits cleanly after a shutdown
TEST_CASE("LSP: a session", "[lsp]") {
    std::string open = R"({"jsonrpc":"2.0","method":"textDocument/didOpen","params":{"textDocument":{"uri":"file:///t.moya","languageId":"cherimoya","version":1,"text":"let x <- y;"}}})";
    // replaces y with 5, which fixes the error
    std::string change = R"({"jsonrpc":"2.0","method":"textDocument/didChange","params":{"textDocument":{"uri":"file:///t.moya","version":2},"contentChanges":[{"range":{"start":{"line":0,"character":9},"end":{"line":0,"character":10}},"text":"5"}]}})";
    std::string hoverRequest = R"({"jsonrpc":"2.0","id":2,"method":"textDocument/hover","params":{"textDocument":{"uri":"file:///t.moya"},"position":{"line":0,"character":4}}})";
    auto session = serve({
        R"({"jsonrpc":"2.0","id":1,"method":"initialize","params":{}})",
        R"({"jsonrpc":"2.0","method":"initialized","params":{}})",
        open, change, hoverRequest,
        R"({"jsonrpc":"2.0","id":3,"method":"textDocument/unknown","params":{}})",
        R"({"jsonrpc":"2.0","id":4,"method":"shutdown"})",
        R"({"jsonrpc":"2.0","method":"exit"})",
    });
    CHECK(session.exitCode == 0);
    REQUIRE(session.sent.size() == 6);
    auto& capabilities = *session.sent[0].getAsObject()->getObject("result")->getObject("capabilities");
    CHECK(capabilities.getObject("textDocumentSync")->getInteger("change") == 2);
    CHECK(capabilities.getBoolean("inlayHintProvider") == true);
    CHECK(capabilities.getObject("semanticTokensProvider")->getObject("legend")->getArray("tokenTypes")->size() == tokenTypes.size());
    CHECK(stringOf(session.sent[1], "method") == "textDocument/publishDiagnostics");
    CHECK(session.sent[1].getAsObject()->getObject("params")->getArray("diagnostics")->size() == 1);
    CHECK(session.sent[2].getAsObject()->getObject("params")->getArray("diagnostics")->size() == 0);
    CHECK(stringOf(*session.sent[3].getAsObject()->getObject("result")->get("contents"), "value") == "```cherimoya\nlet x :: int\n```");
    REQUIRE(session.sent[4].getAsObject()->getObject("error"));
    CHECK(session.sent[4].getAsObject()->getObject("error")->getInteger("code") == -32601);
    CHECK(session.sent[5].getAsObject()->get("result")->kind() == Value::Null);
    // exiting without a shutdown is exit code 1, as the protocol says
    CHECK(serve({R"({"jsonrpc":"2.0","method":"exit"})"}).exitCode == 1);
}
