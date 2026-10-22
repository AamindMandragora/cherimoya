#pragma once

#include <istream>
#include <optional>
#include <ostream>
#include <string>
#include <llvm/Support/JSON.h>
#include <llvm/Support/raw_ostream.h>

// reads one message: headers up to a blank line, of which only Content-Length matters, then a body exactly that long, or nothing once the input ends
inline std::optional<std::string> readMessage(std::istream& in) {
    size_t length = 0;
    std::string header;
    while (std::getline(in, header)) {
        if (!header.empty() && header.back() == '\r') header.pop_back();
        if (header.empty()) break;
        const std::string name = "Content-Length:";
        if (header.compare(0, name.size(), name) == 0) length = std::stoul(header.substr(name.size()));
    }
    if (!in || length == 0) return std::nullopt;
    std::string body(length, '\0');
    in.read(&body[0], static_cast<std::streamsize>(length));
    if (!in) return std::nullopt;
    return body;
}

// writes one message, with the one header the protocol needs, and flushes it so the editor sees it right away
inline void writeMessage(std::ostream& out, const llvm::json::Value& message) {
    std::string body;
    llvm::raw_string_ostream text(body);
    text << message;
    text.flush();
    out << "Content-Length: " << body.size() << "\r\n\r\n" << body;
    out.flush();
}
