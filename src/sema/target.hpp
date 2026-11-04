#pragma once

#include <optional>
#include <string_view>
#include <unordered_map>
#include <vector>
#include "diagnostics.hpp"
#include "type.hpp"

struct Symbol;

// the owned bindings that hold nothing at some point in a function, because they were moved, each with where it moved
using Moved = std::unordered_map<const Symbol*, Span>;

// one way control leaves a construct to carry on after it: a yield to it, or reaching its end, keyed by what codegen frees the bindings there for, with what was moved by then and where it leaves, for a diagnostic
struct Flow {
    const void* site;
    Moved moved;
    Span span;
};

// what kind of construct a yield can target
enum class TargetKind {
    Function,
    Block,
    Arm,
    Match,
    Loop,
    Chunk
};

// one entry on the yield-target stack: every function, block, match arm, match, loop, and chunk pushes one while it's checked, where a function knows the type it yields and everything else learns it from the first yield
struct Target {
    TargetKind kind;
    std::optional<std::string_view> label;
    // the type every yield to a function has to have, or null when the type comes from the yields
    SemaType* expected;
    // the type of the first yield that reached this target, and where it was, or null if nothing has yet
    SemaType* yielded;
    Span first;
    // whether a loop has no header, so it only ends by a yield
    bool isInfinite;
    // where the construct is, for notes
    Span span;
    // how many scopes of owned bindings were open outside it, so a yield to it frees the bindings of every scope from there in, and every way control leaves it to carry on after it
    size_t owners = 0;
    std::vector<Flow> flows;
};
