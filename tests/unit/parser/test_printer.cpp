#include <catch2/catch_test_macros.hpp>

#include <string>
#include <memory>

#include "parser.hpp"

// prints the AST back as source: 4-space indents, one statement per line, and only the parentheses the grammar needs, full mode parenthesizes every operator's operands, so two trees print the same in full mode only if they have the same shape
class Printer {
    bool full;

    // indentation for a nesting depth
    std::string pad(int depth) {
        return std::string(depth * 4, ' ');
    }

    // puts a prefix in front of text, with a space if the text starts with a symbol that would merge into an operator
    std::string glue(const std::string& prefix, const std::string& s) {
        bool merges = !s.empty() && std::string("+-*/%&|^~!<>=").find(s[0]) != std::string::npos;
        return prefix + (merges ? " " : "") + s;
    }

    // the operator table entry for an operator node
    const OpInfo* opOf(const Expr& e) {
        switch (e.kind) {
            case ExprKind::Binary: return static_cast<const BinaryExpr&>(e).op.op;
            case ExprKind::ExtraBinary: return static_cast<const ExtraBinaryExpr&>(e).op.op;
            case ExprKind::Pipe: return static_cast<const PipeExpr&>(e).op.op;
            default: return nullptr;
        }
    }

    // how loosely an expression binds: an operator's level, 10 for chains, 3 for prefix forms, and 1 for everything postfix or atomic
    int levelOf(const Expr& e) {
        if (auto op = opOf(e)) return op->level;
        if (e.kind == ExprKind::Chain) return 10;
        if (e.kind == ExprKind::Unary || e.kind == ExprKind::Old) return 3;
        return 1;
    }

    // whether an expression is an operator node at all, which is what full mode parenthesizes
    bool isOperator(const Expr& e) {
        return levelOf(e) > 1;
    }

    // a binary operand needs parentheses when it binds looser, ties on level without matching associativity on its side, or is the other pipe
    bool needsParens(const Expr& child, const OpInfo& parent, bool right) {
        if (full && isOperator(child)) return true;
        if (child.kind == ExprKind::Pipe && parent.category == OpCategory::Pipe && opOf(child)->kind != parent.kind) return true;
        int level = levelOf(child);
        if (level != parent.level) return level > parent.level;
        auto assoc = opOf(child) ? opOf(child)->assoc : Assoc::Chain;
        if (assoc != parent.assoc || assoc == Assoc::None || assoc == Assoc::Chain) return true;
        return right ? assoc != Assoc::Right : assoc != Assoc::Left;
    }

    // an operand that has to bind at least as tightly as level, parenthesized otherwise
    std::string operand(const Expr& e, int level, int depth, bool arrow) {
        if ((full && isOperator(e)) || levelOf(e) > level) return "(" + expr(e, depth, false) + ")";
        return expr(e, depth, arrow);
    }

    // the object of a postfix form, where anything looser than postfix and a type in value position need parentheses
    std::string object(const Expr& e, int depth, bool arrow) {
        if (e.kind == ExprKind::TypeExpr) return "(" + expr(e, depth, false) + ")";
        return operand(e, 1, depth, arrow);
    }

    // a pack element or comprehension head, where an unnamed construction from a name would read back as a named element
    std::string element(const Expr& e, int depth) {
        if (e.kind == ExprKind::Construct && static_cast<const ConstructExpr&>(e).target->kind == ExprKind::Identifier) {
            return "(" + expr(e, depth, false) + ")";
        }
        return expr(e, depth, false);
    }

    // checks whether an expression is a scope, which stands as its own statement without a semicolon
    bool isScope(const Expr& e) {
        return e.kind == ExprKind::Block || e.kind == ExprKind::Match || e.kind == ExprKind::Repeat || e.kind == ExprKind::Chunk;
    }

    // follows the left edge of an expression to see if it starts with a scope, which would end a statement early
    bool startsWithScope(const Expr& e) {
        switch (e.kind) {
            case ExprKind::Block:
            case ExprKind::Match:
            case ExprKind::Repeat:
            case ExprKind::Chunk:
                return true;
            case ExprKind::Binary: return startsWithScope(*static_cast<const BinaryExpr&>(e).lhs);
            case ExprKind::ExtraBinary: return startsWithScope(*static_cast<const ExtraBinaryExpr&>(e).lhs);
            case ExprKind::Pipe: return startsWithScope(*static_cast<const PipeExpr&>(e).lhs);
            case ExprKind::Chain: return startsWithScope(*static_cast<const ChainExpr&>(e).operands[0]);
            case ExprKind::Call: return startsWithScope(*static_cast<const CallExpr&>(e).callee);
            case ExprKind::Index: return startsWithScope(*static_cast<const IndexExpr&>(e).object);
            case ExprKind::Member: return startsWithScope(*static_cast<const MemberExpr&>(e).object);
            case ExprKind::Construct: return startsWithScope(*static_cast<const ConstructExpr&>(e).target);
            default: return false;
        }
    }

    // an expression at the top of a statement, where <- is an assignment and a leading scope ends the statement
    std::string stmtExpr(const Expr& e, int depth) {
        if (startsWithScope(e)) return "(" + expr(e, depth, false) + ")";
        return expr(e, depth, true);
    }

    // a filter clause or a membership clause's source in a comprehension, which is parsed just above && and so needs parentheses from && down
    std::string clause(const Expr& e, int depth, bool arrow) {
        if (levelOf(e) >= 12) return "(" + expr(e, depth, false) + ")";
        return expr(e, depth, arrow);
    }

    // joins a list of expressions with commas
    std::string list(const std::vector<std::unique_ptr<Expr>>& es, int depth) {
        std::string s;
        for (size_t i = 0; i < es.size(); i++) s += (i ? ", " : "") + expr(*es[i], depth, false);
        return s;
    }

    // the inside of a range's brackets, shared by range expressions and range patterns
    template <typename Elems>
    std::string rangeElems(const Elems& elems, int depth) {
        std::string s;
        for (size_t i = 0; i < elems.size(); i++) {
            if (i) s += ", ";
            if (!elems[i].value) { s += "..."; continue; }
            auto value = expr(*elems[i].value, depth, false);
            s += elems[i].marker ? glue(std::string(elems[i].marker->text), value) : value;
        }
        return s;
    }

    // name :: type pairs in parentheses
    std::string params(const std::vector<Param>& ps, int depth) {
        std::string s = "(";
        for (size_t i = 0; i < ps.size(); i++) {
            if (i) s += ", ";
            s += std::string(ps[i].name.text);
            if (ps[i].type) s += " :: " + type(*ps[i].type, depth);
        }
        return s + ")";
    }

    // a generic argument, either a type or a value
    std::string arg(const GenericArg& a, int depth) {
        return a.type ? type(*a.type, depth) : expr(*a.value, depth, false);
    }

    // [name :: bound & bound <- fallback, ...]
    std::string generics(const std::vector<GenericParam>& gs, int depth) {
        std::string s = "[";
        for (size_t i = 0; i < gs.size(); i++) {
            if (i) s += ", ";
            s += std::string(gs[i].name.text);
            for (size_t j = 0; j < gs[i].bounds.size(); j++) s += (j ? " & " : " :: ") + type(*gs[i].bounds[j], depth);
            if (gs[i].fallback) s += " <- " + arg(*gs[i].fallback, depth);
        }
        return s + "]";
    }

    // [name, name[field, field]]
    std::string items(const std::vector<ModuleItem>& its) {
        std::string s = "[";
        for (size_t i = 0; i < its.size(); i++) {
            if (i) s += ", ";
            s += std::string(its[i].name.text);
            if (its[i].fields.empty()) continue;
            s += "[";
            for (size_t j = 0; j < its[i].fields.size(); j++) s += (j ? ", " : "") + std::string(its[i].fields[j].text);
            s += "]";
        }
        return s + "]";
    }

    // a list of lines inside braces, kept on one line when there's exactly one item that fits on one line
    std::string braces(const std::vector<std::string>& lines, int depth) {
        if (lines.empty()) return "{ }";
        if (lines.size() == 1 && lines[0].find('\n') == std::string::npos) return "{ " + lines[0] + " }";
        std::string s = "{\n";
        for (auto& line : lines) s += pad(depth + 1) + line + "\n";
        return s + pad(depth) + "}";
    }

    // a block's statements in braces
    std::string block(const BlockExpr& b, int depth) {
        std::vector<std::string> lines;
        for (auto& st : b.stmts) lines.push_back(stmt(*st, depth + 1));
        return braces(lines, depth);
    }

public:
    explicit Printer(bool f = false) : full(f) {}

    // prints an expression, where arrow means a top-level <- would be read as something other than construction
    std::string expr(const Expr& e, int depth, bool arrow) {
        switch (e.kind) {
            case ExprKind::IntLiteral:
                return std::string(static_cast<const IntLiteralExpr&>(e).literal);
            case ExprKind::FloatLiteral:
                return std::string(static_cast<const FloatLiteralExpr&>(e).literal);
            case ExprKind::BoolLiteral:
                return static_cast<const BoolLiteralExpr&>(e).literal ? "true" : "false";
            case ExprKind::StringLiteral:
                return std::string(static_cast<const StringLiteralExpr&>(e).literal);
            case ExprKind::CharLiteral:
                return std::string(static_cast<const CharLiteralExpr&>(e).literal);
            case ExprKind::Identifier:
                return std::string(static_cast<const IdentifierExpr&>(e).name);
            case ExprKind::Wildcard:
                return "_";
            case ExprKind::Unary: {
                auto& u = static_cast<const UnaryExpr&>(e);
                return glue(std::string(u.op.text), operand(*u.operand, 3, depth, arrow));
            }
            case ExprKind::Old:
                return "old " + operand(*static_cast<const OldExpr&>(e).operand, 3, depth, arrow);
            case ExprKind::Binary:
            case ExprKind::ExtraBinary:
            case ExprKind::Pipe: {
                const Expr* lhs;
                const Expr* rhs;
                std::string_view text;
                if (e.kind == ExprKind::Binary) {
                    auto& b = static_cast<const BinaryExpr&>(e);
                    lhs = b.lhs.get(); rhs = b.rhs.get(); text = b.op.text;
                } else if (e.kind == ExprKind::ExtraBinary) {
                    auto& b = static_cast<const ExtraBinaryExpr&>(e);
                    lhs = b.lhs.get(); rhs = b.rhs.get(); text = b.op.text;
                } else {
                    auto& b = static_cast<const PipeExpr&>(e);
                    lhs = b.lhs.get(); rhs = b.rhs.get(); text = b.op.text;
                }
                auto& op = *opOf(e);
                auto l = needsParens(*lhs, op, false) ? "(" + expr(*lhs, depth, false) + ")" : expr(*lhs, depth, arrow);
                auto r = needsParens(*rhs, op, true) ? "(" + expr(*rhs, depth, false) + ")" : expr(*rhs, depth, arrow);
                return l + " " + std::string(text) + " " + r;
            }
            case ExprKind::Chain: {
                auto& c = static_cast<const ChainExpr&>(e);
                std::string s = operand(*c.operands[0], 9, depth, arrow);
                for (size_t i = 0; i < c.ops.size(); i++) s += " " + std::string(c.ops[i].text) + " " + operand(*c.operands[i + 1], 9, depth, arrow);
                return s;
            }
            case ExprKind::OpRef:
                return std::string(static_cast<const OpRefExpr&>(e).op.text);
            case ExprKind::Call: {
                auto& c = static_cast<const CallExpr&>(e);
                return operand(*c.callee, 1, depth, arrow) + "(" + list(c.args, depth) + ")";
            }
            case ExprKind::Index: {
                auto& i = static_cast<const IndexExpr&>(e);
                bool slice = i.args.size() == 1 && i.args[0]->kind == ExprKind::Range;
                auto inside = slice ? rangeElems(static_cast<const RangeExpr&>(*i.args[0]).elems, depth) : list(i.args, depth);
                return object(*i.object, depth, arrow) + "[" + inside + "]";
            }
            case ExprKind::Member: {
                auto& m = static_cast<const MemberExpr&>(e);
                return object(*m.object, depth, arrow) + "." + std::string(m.field);
            }
            case ExprKind::Construct: {
                auto& c = static_cast<const ConstructExpr&>(e);
                auto s = object(*c.target, depth, false) + " <- " + expr(*c.pack, depth, false);
                return arrow ? "(" + s + ")" : s;
            }
            case ExprKind::Pack: {
                auto& p = static_cast<const PackExpr&>(e);
                std::string s = "[";
                for (size_t i = 0; i < p.elems.size(); i++) {
                    if (i) s += ", ";
                    if (p.elems[i].name) s += std::string(p.elems[i].name->text) + " <- " + expr(*p.elems[i].value, depth, false);
                    else s += element(*p.elems[i].value, depth);
                }
                return s + "]";
            }
            case ExprKind::Range:
                return "[" + rangeElems(static_cast<const RangeExpr&>(e).elems, depth) + "]";
            case ExprKind::Compr: {
                auto& c = static_cast<const ComprExpr&>(e);
                std::string s = "[" + element(*c.head, depth) + " : ";
                for (size_t i = 0; i < c.clauses.size(); i++) {
                    if (i) s += " && ";
                    auto& cl = c.clauses[i];
                    if (cl.pattern) s += pattern(*cl.pattern, depth) + " <- " + clause(*cl.expr, depth, false);
                    else s += clause(*cl.expr, depth, true);
                }
                return s + "]";
            }
            case ExprKind::Fold: {
                auto& f = static_cast<const FoldExpr&>(e);
                auto bracketed = f.sequence->kind == ExprKind::Range || f.sequence->kind == ExprKind::Compr;
                auto seq = expr(*f.sequence, depth, false);
                return "(" + expr(*f.reducer, depth, false) + ", " + expr(*f.seed, depth, false) + ")" + (bracketed ? seq : "[" + seq + "]");
            }
            case ExprKind::Ann: {
                auto& a = static_cast<const AnnExpr&>(e);
                return "(" + expr(*a.expr, depth, false) + " :: " + type(*a.annotation, depth) + ")";
            }
            case ExprKind::TypeExpr:
                return type(*static_cast<const TypeExprExpr&>(e).typeExpr, depth);
            case ExprKind::Closure: {
                auto& c = static_cast<const ClosureExpr&>(e);
                std::string s = c.captures.empty() ? "" : "[" + list(c.captures, depth) + "] ";
                s += params(c.params, depth) + " -> ";
                if (c.outputs) s += params(*c.outputs, depth) + " ";
                return s + block(*c.body, depth);
            }
            case ExprKind::Block: {
                auto& b = static_cast<const BlockExpr&>(e);
                return (b.name ? std::string(b.name->text) + " " : "") + block(b, depth);
            }
            case ExprKind::Match: {
                auto& m = static_cast<const MatchExpr&>(e);
                std::string s = "match";
                if (m.name) s += " " + std::string(m.name->text);
                if (m.subject) s += " (" + (m.equality ? std::string(m.equality->text) + ", " : "") + expr(*m.subject, depth, false) + ")";
                std::vector<std::string> arms;
                for (auto& arm : m.arms) arms.push_back(pattern(*arm.pattern, depth + 1) + " -> " + block(*arm.body, depth + 1));
                return s + " " + braces(arms, depth);
            }
            case ExprKind::Repeat: {
                auto& r = static_cast<const RepeatExpr&>(e);
                std::string s = "repeat";
                if (r.name) s += " " + std::string(r.name->text);
                if (r.binder) s += " (" + pattern(*r.binder, depth) + ") <- " + expr(*r.source, depth, false);
                if (r.cond) s += " (" + expr(*r.cond, depth, false) + ")";
                if (r.invariant) s += " wit {" + expr(*r.invariant, depth, false) + "}";
                return s + " " + block(*r.body, depth);
            }
            case ExprKind::Chunk: {
                auto& c = static_cast<const ChunkExpr&>(e);
                return "chunk " + (c.name ? std::string(c.name->text) + " " : "") + block(*c.body, depth);
            }
            case ExprKind::Error:
                return "<error>";
        }
        return "<unknown>";
    }

    // prints a type
    std::string type(const Type& t, int depth) {
        switch (t.kind) {
            case TypeKind::Named: {
                auto& n = static_cast<const NamedType&>(t);
                std::string s;
                for (size_t i = 0; i < n.path.size(); i++) s += (i ? "." : "") + std::string(n.path[i].text);
                if (n.args.empty()) return s;
                s += "[";
                for (size_t i = 0; i < n.args.size(); i++) s += (i ? ", " : "") + arg(n.args[i], depth);
                return s + "]";
            }
            case TypeKind::Qualified: {
                auto& q = static_cast<const QualifiedType&>(t);
                return std::string(q.qualifier.text) + (q.inner ? " " + type(*q.inner, depth) : "");
            }
            case TypeKind::Handle: {
                auto& h = static_cast<const HandleType&>(t);
                return std::string(h.handle.text) + (h.arg ? "[" + arg(*h.arg, depth) + "]" : "") + " " + type(*h.inner, depth);
            }
            case TypeKind::Mode: {
                auto& m = static_cast<const ModeType&>(t);
                return std::string(m.mode.text) + (m.inner ? " " + type(*m.inner, depth) : "");
            }
            case TypeKind::Tuple: {
                auto& tu = static_cast<const TupleType&>(t);
                std::string s = "[";
                for (size_t i = 0; i < tu.elems.size(); i++) s += (i ? ", " : "") + type(*tu.elems[i], depth);
                return s + "]";
            }
            case TypeKind::Record: {
                auto& r = static_cast<const RecordType&>(t);
                std::string s = "[";
                for (size_t i = 0; i < r.fields.size(); i++) s += (i ? ", " : "") + std::string(r.fields[i].name.text) + " :: " + type(*r.fields[i].type, depth);
                return s + "]";
            }
            case TypeKind::Refined: {
                auto& r = static_cast<const RefinedType&>(t);
                std::string binder = r.binder ? std::string(r.binder->text) + " : " : "";
                return type(*r.inner, depth) + " {" + binder + expr(*r.predicate, depth, false) + "}";
            }
            case TypeKind::Error:
                return "<error-type>";
        }
        return "<unknown-type>";
    }

    // prints a pattern
    std::string pattern(const Pattern& p, int depth) {
        switch (p.kind) {
            case PatternKind::Literal:
                return expr(*static_cast<const LiteralPattern&>(p).literal, depth, false);
            case PatternKind::Wildcard:
                return "_";
            case PatternKind::Binder:
                return std::string(static_cast<const BinderPattern&>(p).name.text);
            case PatternKind::Variant: {
                auto& v = static_cast<const VariantPattern&>(p);
                std::string s = std::string(v.name.text) + "[";
                for (size_t i = 0; i < v.payload.size(); i++) s += (i ? ", " : "") + pattern(*v.payload[i], depth);
                return s + "]";
            }
            case PatternKind::Pack: {
                auto& pk = static_cast<const PackPattern&>(p);
                std::string s = "[";
                for (size_t i = 0; i < pk.elems.size(); i++) {
                    if (i) s += ", ";
                    if (pk.elems[i].name) s += std::string(pk.elems[i].name->text) + " <- ";
                    s += pattern(*pk.elems[i].pattern, depth);
                }
                return s + "]";
            }
            case PatternKind::Range:
                return "[" + rangeElems(static_cast<const RangePattern&>(p).elems, depth) + "]";
            case PatternKind::SetBuilder: {
                auto& sb = static_cast<const SetBuilderPattern&>(p);
                std::string s = "[" + std::string(sb.binder.text) + " : ";
                for (size_t i = 0; i < sb.predicates.size(); i++) s += (i ? " && " : "") + clause(*sb.predicates[i], depth, true);
                return s + "]";
            }
            case PatternKind::Guard:
                return "(" + expr(*static_cast<const GuardPattern&>(p).cond, depth, false) + ")";
            case PatternKind::Error:
                return "<error-pattern>";
        }
        return "<unknown-pattern>";
    }

    // prints a statement, without indentation for its first line
    std::string stmt(const Stmt& s, int depth) {
        switch (s.kind) {
            case StmtKind::Assign: {
                auto& a = static_cast<const AssignStmt&>(s);
                auto op = a.op ? std::string(a.op->text) : "<-";
                return stmtExpr(*a.place, depth) + " " + op + " " + expr(*a.value, depth, false) + ";";
            }
            case StmtKind::Expr: {
                auto& e = *static_cast<const ExprStmt&>(s).expr;
                if (isScope(e)) return expr(e, depth, false);
                return stmtExpr(e, depth) + ";";
            }
            case StmtKind::Yield: {
                auto& y = static_cast<const YieldStmt&>(s);
                std::string r;
                if (y.value) {
                    auto& v = *y.value;
                    r += (isScope(v) ? expr(v, depth, false) : stmtExpr(v, depth)) + " ";
                }
                r += "-|";
                if (y.target) r += " " + std::string(y.target->text);
                return r;
            }
            case StmtKind::Let: {
                auto& l = static_cast<const LetStmt&>(s);
                std::string r = "let " + pattern(*l.pattern, depth);
                if (l.type) r += " :: " + type(*l.type, depth);
                return r + " <- " + expr(*l.init, depth, false) + ";";
            }
            case StmtKind::Wit: {
                auto& w = static_cast<const WitStmt&>(s);
                std::string r = "wit {" + expr(*w.predicate, depth, false);
                if (w.bound) r += " :: " + type(*w.bound, depth);
                return r + "};";
            }
            case StmtKind::Assert:
                return "assert (" + expr(*static_cast<const AssertStmt&>(s).cond, depth, false) + ");";
            case StmtKind::Decl:
                return decl(*static_cast<const DeclStmt&>(s).decl, depth);
            case StmtKind::Attr: {
                auto& a = static_cast<const AttrStmt&>(s);
                std::string r = "@" + std::string(a.name.text);
                auto target = a.target ? stmt(*a.target, depth) : "";
                if (!a.args.empty() || (a.target && target[0] == '(')) r += "(" + list(a.args, depth) + ")";
                return r + (a.target ? " " + target : ";");
            }
            case StmtKind::Error:
                return "<error-stmt>";
        }
        return "<unknown-stmt>";
    }

    // prints a declaration, where an interface member has no body and no ;
    std::string decl(const Decl& d, int depth, bool member = false) {
        switch (d.kind) {
            case DeclKind::Fn: {
                auto& f = static_cast<const FnDecl&>(d);
                std::string s = "let ";
                if (!f.generics.empty()) s += generics(f.generics, depth) + " ";
                s += params(f.inputs, depth) + " -> " + std::string(f.name.text) + " -> " + params(f.outputs, depth);
                if (f.obligations) s += " wit {" + expr(*f.obligations, depth, false) + "}";
                if (f.body) return s + " " + block(*f.body, depth);
                return member ? s : s + ";";
            }
            case DeclKind::Type: {
                auto& t = static_cast<const TypeDecl&>(d);
                std::string s = "type " + std::string(t.name.text);
                if (!t.generics.empty()) s += generics(t.generics, depth);
                s += " <- ";
                if (t.form == TypeForm::Interface) {
                    s += "[ ";
                    for (size_t i = 0; i < t.members.size(); i++) s += (i ? ", " : "") + decl(*t.members[i], depth, true);
                    s += " ]";
                } else if (t.form == TypeForm::Sum || t.form == TypeForm::Enum) {
                    for (size_t i = 0; i < t.variants.size(); i++) {
                        auto& v = t.variants[i];
                        s += (i ? " | " : "") + std::string(v.name.text);
                        if (v.payload.empty()) continue;
                        s += "[";
                        for (size_t j = 0; j < v.payload.size(); j++) s += (j ? ", " : "") + type(*v.payload[j], depth);
                        s += "]";
                    }
                } else {
                    s += type(*t.rhs, depth);
                }
                return s + ";";
            }
            case DeclKind::Export:
                return "export " + items(static_cast<const ExportDecl&>(d).items) + ";";
            case DeclKind::Import: {
                auto& i = static_cast<const ImportDecl&>(d);
                std::string path;
                for (size_t j = 0; j < i.path.size(); j++) path += (j ? "." : "") + std::string(i.path[j].text);
                return "import " + items(i.items) + " <- " + path + ";";
            }
            case DeclKind::Error:
                return "<error-decl>";
        }
        return "<unknown-decl>";
    }

    // prints a whole program, one top-level statement per line
    std::string program(const std::vector<std::unique_ptr<Stmt>>& stmts) {
        std::string s;
        for (auto& st : stmts) s += stmt(*st, 0) + "\n";
        return s;
    }
};

// a parsed program that keeps its source manager and tokens alive alongside the tree, and remembers if anything went wrong
struct Parsed {
    SourceManager sm;
    std::vector<Token> tokens;
    std::vector<std::unique_ptr<Stmt>> program;
    size_t errors;
};

// lexes and parses a whole program
static std::unique_ptr<Parsed> parseProgram(const std::string& src) {
    auto p = std::make_unique<Parsed>();
    p->sm.loadString(src);
    DiagnosticEngine diag(p->sm);
    Lexer lexer(diag);
    p->tokens = lexer.lex();
    Parser parser(p->tokens, diag);
    p->program = parser.parseProgram();
    p->errors = diag.errorCount();
    return p;
}

// prints, re-parses, and prints again, checking that nothing changes: the text, and the tree's shape through full mode
static std::string roundTrip(const std::string& src) {
    auto first = parseProgram(src);
    REQUIRE(first->errors == 0);
    auto printed = Printer().program(first->program);
    auto second = parseProgram(printed);
    CHECK(second->errors == 0);
    CHECK(Printer().program(second->program) == printed);
    CHECK(Printer(true).program(second->program) == Printer(true).program(first->program));
    return printed;
}

// checks that canonical source prints back exactly as written
static void canonical(const std::string& src) {
    CHECK(roundTrip(src) == src);
}

// checks that expressions print with only the parentheses they need
TEST_CASE("Printer: parentheses follow precedence, associativity, and mixing", "[printer]") {
    canonical("x <- a + b * c;\n");
    canonical("x <- (a + b) * c;\n");
    canonical("x <- a - (b - c);\n");
    canonical("x <- a - b - c;\n");
    canonical("x <- a ** b ** c;\n");
    canonical("x <- (a ** b) ** c;\n");
    canonical("x <- -a ** b;\n");
    canonical("x <- (-a) ** b;\n");
    canonical("x <- - -a;\n");
    canonical("x <- -(a + b);\n");
    canonical("x <- (a = b) != c;\n");
    canonical("x <- (a <=> b) <= 0;\n");
    canonical("x <- a < b <= c;\n");
    canonical("x <- (a < b) < c;\n");
    canonical("x <- a < b = c > d;\n");
    canonical("x <- a * b <*> c;\n");
    canonical("x <- (a >>= b) =<< c;\n");
    canonical("x <- xs |> f |> g;\n");
    canonical("x <- double <| inc <| 2 + 3;\n");
    canonical("x <- f <| (x |> g);\n");
    canonical("x <- (f <| x) |> g;\n");
    canonical("x <- (a + b).c;\n");
    canonical("x <- (-f)(x);\n");
    canonical("x <- old p.x + old (a + b);\n");
    canonical("x <- Vec2 <- [1.0, 2.0] <+> Vec2 <- [3.0, 4.0];\n");
    canonical("x <- [(Point <- [1, 2]), q <- 3];\n");
    canonical("x <- (arr T)[0];\n");
    canonical("x <- a < -1;\n");
}

// checks expressions whose source form has extra parentheses or spacing print canonically
TEST_CASE("Printer: non-canonical input normalizes", "[printer]") {
    CHECK(roundTrip("x<-((a+b));") == "x <- a + b;\n");
    CHECK(roundTrip("x <- (((a)));") == "x <- a;\n");
    CHECK(roundTrip("x <- (a * b) + c;") == "x <- a * b + c;\n");
    CHECK(roundTrip("x <- [0, ..., < -1];") == "x <- [0, ..., < -1];\n");
    CHECK(roundTrip("x <- (max, MIN)[[scores]];") == "x <- (max, MIN)[[scores]];\n");
    CHECK(roundTrip("x <- (+, 0)[[1, ..., n]];") == "x <- (+, 0)[1, ..., n];\n");
    CHECK(roundTrip("let   y  <-  1 ;") == "let y <- 1;\n");
}

// checks the statement-level traps: constructions at the top of a statement or clause, and scopes that start an expression
TEST_CASE("Printer: statement and clause contexts", "[printer]") {
    canonical("(Point <- [1, 2]);\n");
    canonical("(Point <- [1, 2]) -|\n");
    canonical("p <- Point <- [1, 2];\n");
    canonical("x <- [p : p <- ps && p = (Point <- [0, 0])];\n");
    canonical("x <- [x : x <- xs && (a || b)];\n");
    canonical("x <- [d : Some[d] <- xs && (x |> f)];\n");
    canonical("(match (x) { _ -> { 1 -| } } + 1);\n");
    canonical("({ 1 -| } + 1) -|\n");
    canonical("match (x) { _ -> { 1 -| } } -|\n");
    canonical("match (=, x) { _ -> { 1 -| } } -|\n");
}

// checks every statement form and scope layout
TEST_CASE("Printer: statements and scopes", "[printer]") {
    canonical(
        "{\n"
        "    let count :: mut i64u <- 0;\n"
        "    let [q, r] <- divmod(17, 5);\n"
        "    let v :: mut arr[16] char <- _;\n"
        "    let buf :: unscoped <- arr mut T(n, _);\n"
        "    xs[i] *<- 2;\n"
        "    _ <- write(1, s, len(s));\n"
        "    v <+><- w;\n"
        "    swap(a, b);\n"
        "    wit {i <= n};\n"
        "    wit {T :: Ord};\n"
        "    assert (q = 3 && r = 2);\n"
        "    outer { -| outer }\n"
        "    chunk scratch { let s :: mut <- list_new[i64u](); }\n"
        "    count -|\n"
        "}\n");
    canonical(
        "repeat outer (i) <- [0, ..., <n] {\n"
        "    match { (xs[i] < 0) -> { -| i } }\n"
        "    repeat (j) <- [>i, ..., <n] wit {j <= n} {\n"
        "        match { (count = 1000) -> { -| outer } }\n"
        "        count +<- 1;\n"
        "    }\n"
        "}\n");
    canonical(
        "match (n) {\n"
        "    [..., <0] -> { 0 -| }\n"
        "    [0, ..., 100] -> { n -| }\n"
        "    [>100, ...] -> { 100 -| }\n"
        "} -|\n");
    canonical(
        "match {\n"
        "    (a < b && ready) -> { 1 -| }\n"
        "    _ -> { 2 -| }\n"
        "}\n");
    canonical("repeat (n > 0) { n -<- 1; }\n");
    canonical("repeat ([i, x]) <- enumerate(xs) { }\n");
    canonical("repeat { x -| repeat }\n");
    canonical("match (s) { Some[[x <- 0, y <- py]] -> { } }\n");
    canonical("match (c) { Some[['0', ..., '9']] -> { } }\n");
    canonical("match (x) { [x : x % 2 = 0 && x > 0] -> { } }\n");
}

// checks closures, folds, comprehensions, and types in value position
TEST_CASE("Printer: closures, folds, and comprehensions", "[printer]") {
    canonical("let step <- (acc, x) -> { acc + x * rate -| };\n");
    canonical("let f <- (a :: int, b :: int) -> (r :: int) { a * b -| };\n");
    canonical("let scale <- [factor] (x) -> { x * factor -| };\n");
    canonical("let fs <- [(x :: int) -> (r :: int) { x + 1 -| }, g];\n");
    canonical("let doubled <- ((acc, x) -> { acc + x * rate -| }, 0)[[1, 2, 3]];\n");
    canonical("let total <- (+, 0)[x * x : x <- [1, ..., 10] && x % 2 = 0];\n");
    canonical(
        "let mixed <- (+, 0)[match (i % 2) {\n"
        "    0 -> { x * 2 -| }\n"
        "    _ -> { x -| }\n"
        "} : [i, x] <- enumerate(xs)];\n");
    canonical("let any <- (||, false)[x < 0 : x <- xs];\n");
    canonical("let s <- (<+>, zero)[vs];\n");
    canonical("let n <- i32(x) + size[i32]() + len(arr i64u([x : x <- xs]));\n");
    canonical("let p <- ptr T(value);\n");
    canonical("let o <- cmp((p :: Ord), q);\n");
}

// checks declarations
TEST_CASE("Printer: declarations", "[printer]") {
    canonical("type NodeId <- i64u;\n");
    canonical("type Percent <- int {p : 0 <= p <= 100};\n");
    canonical("type Point <- [x :: f64, y :: f64];\n");
    canonical("type Shape <- Circle[f64] | Rect[f64, f64];\n");
    canonical("type Dir <- N | E | S | W;\n");
    canonical("type Option[T] <- Some[T] | None;\n");
    canonical("type Ord[T] <- [ let (a :: peek T, b :: peek T) -> cmp -> (r :: int) ];\n");
    canonical("type Num[T] <- [ let (a :: T, b :: T) -> + -> (r :: T), let (a :: T) -> - -> (r :: T) ];\n");
    canonical("type List[T, A :: Allocator <- Global] <- [data :: arr[A] mut T, count :: i64u] {l : l.count <= len(l.data) && written(l.data) == [0, ..., <l.count]};\n");
    canonical("type Map[K :: Ord, V] <- [keys :: arr K, vals :: mut arr mut V];\n");
    canonical("export [List, list_new, push, Result[dist, prev]];\n");
    canonical("import [Allocator] <- std.mem;\n");
    canonical("@tier(strict);\n");
    canonical("@tier(strict) { x <- 1; }\n");
    canonical("@extern(\"c\") let (fd :: i32, buf :: peek arr char, n :: i64u) -> write -> (r :: i64);\n");
    canonical("@global_allocator let heap <- Bump();\n");
    canonical("let (x :: int) -> inc -> (r :: int) { x + 1 -| }\n");
    canonical("let [T :: Ord] (a :: T, b :: T) -> max -> (m :: T) { }\n");
    canonical("let [T, A :: Allocator & Default <- Global] () -> list_new -> (l :: List[T, A]) { }\n");
    canonical("let (a :: Vec2) -> - -> (r :: Vec2) { [-a.x, -a.y] -| }\n");
    canonical("let (g :: peek mut Graph) -> grow -> () wit {node_count(g) >= node_count(old g)} { }\n");
    canonical(
        "let (a :: int {a >= 0}, b :: int {b > 0}) -> divmod -> (q :: int, r :: int {0 <= r < b && a = q * b + r}) { [a / b, a % b] -| }\n");
    canonical(
        "let (s :: peek Shape) -> area -> (a :: f64) {\n"
        "    let (r :: f64) -> sq -> (o :: f64) { r * r -| }\n"
        "    match (s) {\n"
        "        Circle[r] -> { 3.14159 * sq(r) -| }\n"
        "        Rect[w, h] -> { w * h -| }\n"
        "    } -|\n"
        "}\n");
}

// checks that a reference program survives a round trip, with comments and alignment normalized away
TEST_CASE("Printer: a reference program round trip", "[printer]") {
    const char* src =
        "type Percent <- int {p : 0 <= p <= 100};\n"
        "type Shape   <- Circle[f64] | Rect[f64, f64];\n"
        "let (n :: int) -> clamp -> (p :: Percent) {\n"
        "    match (n) {\n"
        "        [..., <0]     -> { 0 -| }\n"
        "        [0, ..., 100] -> { n -| }        # Z3 proves 0 <= n <= 100: no cast needed\n"
        "        [>100, ...]   -> { 100 -| }\n"
        "    } -|\n"
        "}\n"
        "let (xs :: peek arr int, target :: int) -> count_pairs -> (c :: i64u) {\n"
        "    let count :: mut i64u <- 0;\n"
        "    let n <- len(xs);\n"
        "    repeat outer (i) <- [0, ..., <n] {\n"
        "        match { (xs[i] < 0) -> { -| i } }                     # continue outer\n"
        "        repeat (j) <- [>i, ..., <n] {\n"
        "            match { (xs[j] < 0) -> { -| j } }                 # continue inner\n"
        "            match { (xs[i] + xs[j] = target) -> { count +<- 1; } }\n"
        "            match { (count = 1000) -> { -| outer } }          # break outer\n"
        "        }\n"
        "    }\n"
        "    count -|\n"
        "}\n"
        "let () -> main -> (code :: int) {\n"
        "    let rate    <- 2;\n"
        "    let v       <- Vec2 <- [x <- 1.0, y <- 2.0] <+> Vec2 <- [3.0, 4.0];\n"
        "    let doubled <- ((acc, x) -> { acc + x * rate -| }, 0)[[1, 2, 3]];\n"
        "    let piped   <- 3 |> inc |> double;\n"
        "    let back    <- double <| inc <| 2 + 3;\n"
        "    assert (piped = 8 && back = 12);\n"
        "    match { (v.x = 4.0) -> { 0 -| } _ -> { 1 -| } } -|\n"
        "}\n";
    auto printed = roundTrip(src);
    CHECK(printed.find('#') == std::string::npos);
    CHECK(printed.find("    let rate <- 2;\n") != std::string::npos);
    CHECK(printed.find("        [..., <0] -> { 0 -| }\n") != std::string::npos);
}
