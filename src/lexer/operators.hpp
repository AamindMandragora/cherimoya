#pragma once

#include <algorithm>
#include <deque>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>
#include "token.hpp"

// what kind of operator an entry is, which also decides whether it needs spacing
enum class OpCategory { Flow, Arithmetic, Bitwise, Logical, Comparison, Equality, Pipe, Compound, Extra };

// how operators on the same level group: chain is for the built-in comparisons only
enum class Assoc { None, Left, Right, Chain };

// one entry of the operator table, levels go from 1 (tightest) to 17 (loosest) and 0 means no such form
struct OpInfo {
    std::string spelling;
    TokenKind kind;
    OpCategory category;
    bool isOverloadable;
    // the binary level, which for <- is construction's, and the prefix level
    int level;
    int prefixLevel;
    Assoc assoc;
    // for a compound X<-, the entry for X
    const OpInfo* base;
};

// flow tokens, pipes, compounds, and extra operators can't touch another operator without whitespace in between
inline bool needsSpacing(const OpInfo& op) {
    return op.category == OpCategory::Flow || op.category == OpCategory::Pipe
        || op.category == OpCategory::Compound || op.category == OpCategory::Extra;
}

// builds the table once: hand-listed built-ins and named extra operators, then wrapped extra operators, then every compound
inline const std::deque<OpInfo>& operatorTable() {
    static const std::deque<OpInfo> table = [] {
        std::deque<OpInfo> t;
        std::vector<const OpInfo*> compoundable;
        // adds an entry and remembers it if it should get an X<- form
        auto add = [&](std::string s, TokenKind k, OpCategory c, bool over, int lvl, int pre, Assoc a, bool comp) {
            t.push_back({std::move(s), k, c, over, lvl, pre, a, nullptr});
            if (comp) compoundable.push_back(&t.back());
        };
        auto has = [&](std::string_view s) {
            for (auto& e : t) if (e.spelling == s) return true;
            return false;
        };
        using K = TokenKind;
        using C = OpCategory;
        using A = Assoc;
        // the built-ins, starting with flow, where <- inside an expression is construction, at the postfix level
        add("<-",  K::LArrow,      C::Flow,       false, 1,  0, A::Left,  false);
        add("->",  K::RArrow,      C::Flow,       false, 0,  0, A::None,  false);
        add("-|",  K::Yield,       C::Flow,       false, 0,  0, A::None,  false);
        // pipes
        add("|>",  K::PipeFwd,     C::Pipe,       false, 16, 0, A::Left,  false);
        add("<|",  K::PipeBack,    C::Pipe,       false, 17, 0, A::Right, false);
        // arithmetic
        add("+",   K::Plus,        C::Arithmetic, true,  5,  0, A::Left,  true);
        add("-",   K::Minus,       C::Arithmetic, true,  5,  3, A::Left,  true);
        add("*",   K::Star,        C::Arithmetic, true,  4,  0, A::Left,  true);
        add("/",   K::Slash,       C::Arithmetic, true,  4,  0, A::Left,  true);
        add("%",   K::Percent,     C::Arithmetic, true,  4,  0, A::Left,  true);
        // bitwise
        add("&",   K::Amp,         C::Bitwise,    true,  7,  0, A::Left,  true);
        add("|",   K::Bar,         C::Bitwise,    true,  9,  0, A::Left,  true);
        add("^",   K::Caret,       C::Bitwise,    true,  8,  0, A::Left,  true);
        add("~",   K::Tilde,       C::Bitwise,    true,  0,  3, A::None,  false);
        add("<<",  K::Shl,         C::Bitwise,    true,  6,  0, A::Left,  true);
        add(">>",  K::Shr,         C::Bitwise,    true,  6,  0, A::Left,  true);
        // logical
        add("&&",  K::AmpAmp,      C::Logical,    false, 12, 0, A::Left,  false);
        add("||",  K::BarBar,      C::Logical,    false, 14, 0, A::Left,  false);
        add("^^",  K::CaretCaret,  C::Logical,    false, 13, 0, A::Left,  false);
        add("!",   K::Bang,        C::Logical,    true,  0,  3, A::None,  false);
        // comparison
        add("<",   K::Lt,          C::Comparison, false, 10, 0, A::Chain, false);
        add("<=",  K::Le,          C::Comparison, false, 10, 0, A::Chain, false);
        add(">",   K::Gt,          C::Comparison, false, 10, 0, A::Chain, false);
        add(">=",  K::Ge,          C::Comparison, false, 10, 0, A::Chain, false);
        // equality, where == is overloadable but gives a bool, so it gets no compound
        add("=",   K::Eq,          C::Equality,   false, 11, 0, A::None,  false);
        add("!=",  K::Ne,          C::Equality,   false, 11, 0, A::None,  false);
        add("==",  K::EqEq,        C::Equality,   true,  11, 0, A::None,  false);
        add("!==", K::NeEq,        C::Equality,   false, 11, 0, A::None,  false);
        // the named extra operators, starting with the doubles and triples, which sit with the double they extend
        add("**",  K::ExtraOp,     C::Extra,      true,  2,  0, A::Right, true);
        add("***", K::ExtraOp,     C::Extra,      true,  2,  0, A::Right, true);
        add("//",  K::ExtraOp,     C::Extra,      true,  4,  0, A::Left,  true);
        add("%%",  K::ExtraOp,     C::Extra,      true,  4,  0, A::Left,  true);
        add("++",  K::ExtraOp,     C::Extra,      true,  5,  0, A::Left,  true);
        add("+++", K::ExtraOp,     C::Extra,      true,  5,  0, A::Left,  true);
        add("--",  K::ExtraOp,     C::Extra,      true,  5,  0, A::Left,  true);
        add("<<<", K::ExtraOp,     C::Extra,      true,  6,  0, A::Left,  true);
        add(">>>", K::ExtraOp,     C::Extra,      true,  6,  0, A::Left,  true);
        add("&&&", K::ExtraOp,     C::Extra,      true,  12, 0, A::Left,  true);
        add("^^^", K::ExtraOp,     C::Extra,      true,  13, 0, A::Left,  true);
        add("|||", K::ExtraOp,     C::Extra,      true,  14, 0, A::Left,  true);
        // three-way comparison, which never chains
        add("<=>", K::ExtraOp,     C::Extra,      true,  10, 0, A::None,  false);
        // combinators
        add(">>=", K::ExtraOp,     C::Extra,      true,  15, 0, A::Left,  false);
        add("=<<", K::ExtraOp,     C::Extra,      true,  15, 0, A::Right, false);
        add("<$>", K::ExtraOp,     C::Extra,      true,  15, 0, A::Left,  false);
        add("<~>", K::ExtraOp,     C::Extra,      true,  15, 0, A::Left,  false);
        add("~>",  K::ExtraOp,     C::Extra,      true,  15, 0, A::Left,  false);
        add("<~",  K::ExtraOp,     C::Extra,      true,  15, 0, A::Left,  false);
        // the wrapped extra operators: every single-symbol binary operator inside a wrapper takes that operator's level and associativity, except that a wrapped comparison never chains
        const std::vector<std::pair<char, char>> wrappers = {
            {'<', '>'}, {'<', '<'}, {'>', '>'}, {'|', '|'}, {'|', '>'}, {'<', '|'}, {'|', '<'}, {'>', '|'},
        };
        std::vector<const OpInfo*> inners;
        for (auto& e : t) if (e.spelling.size() == 1 && e.level > 0) inners.push_back(&e);
        for (auto [l, r] : wrappers) {
            for (auto in : inners) {
                std::string s = std::string(1, l) + in->spelling + r;
                if (s.find("<-") != std::string::npos || s.find("->") != std::string::npos
                    || s.find("-|") != std::string::npos || has(s)) continue;
                Assoc a = in->assoc == A::Chain ? A::None : in->assoc;
                bool comp = in->category == C::Arithmetic || in->category == C::Bitwise;
                add(s, K::ExtraOp, C::Extra, true, in->level, 0, a, comp);
            }
        }
        // the compounds: every overloadable binary operator from T and T to T gets X<-
        for (auto op : compoundable) {
            t.push_back({op->spelling + "<-", K::CompoundAssign, C::Compound, false, 0, 0, A::None, op});
        }
        return t;
    }();
    return table;
}

// maps every spelling to its entry for maximal munch
inline const std::unordered_map<std::string_view, const OpInfo*>& operatorLookupTable() {
    static const std::unordered_map<std::string_view, const OpInfo*> table = [] {
        std::unordered_map<std::string_view, const OpInfo*> m;
        for (auto& e : operatorTable()) m.emplace(e.spelling, &e);
        return m;
    }();
    return table;
}

// length of the longest spelling, where the munch starts
inline size_t maxOperatorLength() {
    static const size_t len = [] {
        size_t n = 0;
        for (auto& e : operatorTable()) n = std::max(n, e.spelling.size());
        return n;
    }();
    return len;
}

// helper function to look up an operator in the above table
inline const OpInfo* lookupOperator(std::string_view s) {
    const auto& table = operatorLookupTable();
    auto it = table.find(s);
    if (it == table.end()) return nullptr;
    return it->second;
}
