#include <iostream>
#include <string_view>

#include "lexer.hpp"

static std::string_view kind_name(TokenKind k) {
    switch (k) {
        case TokenKind::Error: return "Error";
        case TokenKind::Eof: return "Eof";
        case TokenKind::Identifier: return "Identifier";
        case TokenKind::TypeName: return "TypeName";
        case TokenKind::IntLiteral: return "IntLiteral";
        case TokenKind::FloatLiteral: return "FloatLiteral";
        case TokenKind::StringLiteral: return "StringLiteral";
        case TokenKind::CharLiteral: return "CharLiteral";
        default: return "Op/Keyword";
    }
}

int main(int argc, char** argv) {
    if (argc < 2) {
        std::cerr << "usage: cheri <file.moya>\n";
        return 1;
    }

    SourceManager manager;
    manager.load_file(argv[1]);

    DiagnosticEngine engine(manager);
    Lexer lexer(engine);
    auto tokens = lexer.lex();

    for (const auto& t : tokens) {
        std::cout << kind_name(t.kind) << " '" << t.text << "' @" << t.loc << "\n";
    }

    if (engine.had_errors()) {
        std::cerr << "\n" << engine.error_count() << " lexing error(s).\n";
        return 1;
    }
    return 0;
}