#pragma once

#include <optional>
#include <string_view>
#include "type.hpp"

// what kind of construct a yield can target
enum class TargetKind {
    Function,
    Block,
    Arm,
    Match,
    Loop,
    Chunk
};

// one entry on the yield-target stack: every function, block, match arm, match, loop, and chunk pushes one while it's checked, functions know type to be yielded and everything else learns from first yield
struct Target {
    TargetKind kind;
    std::optional<std::string_view> label;
    // the type every yield to a function has to have, or null when the type comes from the yields
    SemaType* expected;
    // the type of the first yield that reached this target, and where it was, or null if nothing has yet
    SemaType* yielded;
    Span first;
    // whether a loop has no header, so it only ends by a yield
    bool infinite;
    // where the construct is, for notes
    Span span;
};
