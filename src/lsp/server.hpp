#pragma once

#include <algorithm>
#include <istream>
#include <map>
#include <memory>
#include <ostream>
#include <string>
#include <llvm/Support/JSON.h>
#include "document.hpp"
#include "features.hpp"
#include "transport.hpp"

// the language server: it reads requests and notifications from the editor, keeps every open document checked, and answers with what the front end knows, over JSON-RPC on stdin and stdout
class Server {
    std::istream& in;
    std::ostream& out;
    // every open document's text, which incremental changes edit, and the front end's view of its latest version
    std::map<std::string, std::string> texts;
    std::map<std::string, std::unique_ptr<Document>> documents;
    bool isShutDown = false;

    // sends a request's result, or its error, back to the editor
    void reply(const Value& id, Value result) {
        writeMessage(out, Object{{"jsonrpc", "2.0"}, {"id", id}, {"result", std::move(result)}});
    }
    void replyError(const Value& id, int code, const std::string& message) {
        writeMessage(out, Object{{"jsonrpc", "2.0"}, {"id", id}, {"error", Object{{"code", code}, {"message", message}}}});
    }

    // tells the editor something it didn't ask for, like a document's new diagnostics
    void notify(const std::string& method, Value params) {
        writeMessage(out, Object{{"jsonrpc", "2.0"}, {"method", method}, {"params", std::move(params)}});
    }

    // checks a document's current text and publishes its diagnostics, which replace the ones before
    void analyze(const std::string& uri) {
        documents[uri] = std::make_unique<Document>(uri, texts[uri]);
        notify("textDocument/publishDiagnostics", Object{{"uri", uri}, {"diagnostics", diagnostics(*documents[uri])}});
    }

    // what the server can do, which the editor learns once when it starts the server
    Value capabilities() {
        Array types, modifiers;
        for (auto& type : tokenTypes) types.push_back(type);
        for (auto& modifier : tokenModifiers) modifiers.push_back(modifier);
        return Object{
            // documents are opened and closed, and each change sends only the edited ranges
            {"textDocumentSync", Object{{"openClose", true}, {"change", 2}}},
            {"hoverProvider", true},
            {"definitionProvider", true},
            {"referencesProvider", true},
            {"renameProvider", true},
            {"documentSymbolProvider", true},
            {"completionProvider", Object{}},
            {"signatureHelpProvider", Object{{"triggerCharacters", Array{"(", ","}}}},
            {"semanticTokensProvider", Object{{"legend", Object{{"tokenTypes", std::move(types)}, {"tokenModifiers", std::move(modifiers)}}}, {"full", true}}},
        };
    }

    // reads a position out of a request's parameters
    static Position positionOf(const Object& value) {
        return {static_cast<uint32_t>(value.getInteger("line").value_or(0)), static_cast<uint32_t>(value.getInteger("character").value_or(0))};
    }

    // applies one change the editor sent: the text between two positions replaced, or the whole text when there's no range
    void applyChange(std::string& text, const Object& change) {
        std::string replacement = change.getString("text").value_or("").str();
        const Object* range = change.getObject("range");
        if (!range) {
            text = replacement;
            return;
        }
        LineIndex lines(text);
        size_t start = lines.offset(positionOf(*range->getObject("start")));
        size_t end = lines.offset(positionOf(*range->getObject("end")));
        text.replace(start, end - start, replacement);
    }

    // handles a notification, which gets no reply
    void handleNotification(const std::string& method, const Object& params) {
        const Object* textDocument = params.getObject("textDocument");
        std::string uri = textDocument ? textDocument->getString("uri").value_or("").str() : "";
        if (method == "textDocument/didOpen") {
            texts[uri] = textDocument->getString("text").value_or("").str();
            analyze(uri);
        } else if (method == "textDocument/didChange") {
            if (const Array* changes = params.getArray("contentChanges")) {
                for (auto& change : *changes) {
                    if (const Object* edit = change.getAsObject()) applyChange(texts[uri], *edit);
                }
            }
            analyze(uri);
        } else if (method == "textDocument/didClose") {
            texts.erase(uri);
            documents.erase(uri);
            notify("textDocument/publishDiagnostics", Object{{"uri", uri}, {"diagnostics", Array{}}});
        }
    }

    // handles a request, which always gets a reply, where a request about a document the server doesn't have open gets null
    void handleRequest(const Value& id, const std::string& method, const Object& params) {
        if (method == "initialize") return reply(id, Object{{"capabilities", capabilities()}, {"serverInfo", Object{{"name", "cheri"}}}});
        if (method == "shutdown") {
            isShutDown = true;
            return reply(id, nullptr);
        }
        static const std::vector<std::string> documentMethods = {
            "textDocument/semanticTokens/full", "textDocument/hover", "textDocument/definition", "textDocument/references",
            "textDocument/rename", "textDocument/documentSymbol", "textDocument/completion", "textDocument/signatureHelp",
        };
        if (std::find(documentMethods.begin(), documentMethods.end(), method) == documentMethods.end()) {
            return replyError(id, -32601, "cheri doesn't handle '" + method + "'");
        }
        const Object* textDocument = params.getObject("textDocument");
        std::string uri = textDocument ? textDocument->getString("uri").value_or("").str() : "";
        auto found = documents.find(uri);
        if (found == documents.end()) return reply(id, nullptr);
        const Document& document = *found->second;
        const Object* position = params.getObject("position");
        size_t offset = position ? document.lines->offset(positionOf(*position)) : 0;
        if (method == "textDocument/semanticTokens/full") return reply(id, Object{{"data", semanticTokens(document)}});
        if (method == "textDocument/hover") return reply(id, hover(document, offset));
        if (method == "textDocument/definition") return reply(id, definition(document, offset));
        if (method == "textDocument/references") {
            const Object* context = params.getObject("context");
            bool includeDeclaration = context && context->getBoolean("includeDeclaration").value_or(false);
            return reply(id, references(document, offset, includeDeclaration));
        }
        if (method == "textDocument/rename") {
            std::string error;
            Value edit = rename(document, offset, params.getString("newName").value_or("").str(), error);
            if (!error.empty()) return replyError(id, -32803, error);
            return reply(id, std::move(edit));
        }
        if (method == "textDocument/documentSymbol") return reply(id, documentSymbols(document));
        if (method == "textDocument/completion") return reply(id, completion(document, offset));
        reply(id, signatureHelp(document, offset));
    }
public:
    Server(std::istream& i, std::ostream& o) : in(i), out(o) {}

    // serves messages until the editor says to exit, giving the exit code the protocol wants: 0 after a shutdown, and 1 when the editor exits without one or the input just ends
    int run() {
        while (auto body = readMessage(in)) {
            auto parsed = llvm::json::parse(*body);
            if (!parsed) {
                llvm::consumeError(parsed.takeError());
                replyError(nullptr, -32700, "the message isn't JSON");
                continue;
            }
            const Object* message = parsed->getAsObject();
            if (!message) continue;
            std::string method = message->getString("method").value_or("").str();
            if (method == "exit") return isShutDown ? 0 : 1;
            static const Object none;
            const Object* params = message->getObject("params");
            if (const Value* id = message->get("id")) {
                handleRequest(*id, method, params ? *params : none);
            } else {
                handleNotification(method, params ? *params : none);
            }
        }
        return 1;
    }
};
