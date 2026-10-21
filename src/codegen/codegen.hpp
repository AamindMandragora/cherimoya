#pragma once

#include "sema.hpp"

#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/Module.h>
#include <llvm/IR/IRBuilder.h>
#include <llvm/IR/Function.h>
#include <llvm/IR/BasicBlock.h>
#include <llvm/Support/raw_ostream.h>
#include <llvm/Support/ErrorHandling.h>
#include <llvm/IR/Intrinsics.h>

// codegen lowers a checked program to an LLVM module, reading the types and symbols sema wrote into the AST
class Codegen {
    // the context owns every LLVM type and constant, so it's declared first and outlives the module and builder that point into it
    llvm::LLVMContext context;
    std::unique_ptr<llvm::Module> module;
    llvm::IRBuilder<> irbuilder;
    // what every symbol the module defines starts with, cheri. and the module's name, so main in prog.moya is cheri.prog.main, which nothing the runtime or LLVM defines can collide with
    std::string symbolPrefix;
    // the source, so a runtime trap can say which line it came from
    SourceManager& manager;
    // sema interns its types, so each one is lowered once and looked up by pointer after that
    std::unordered_map<SemaType*, llvm::Type*> lowered;

    // turns a sema type into an LLVM type: int is i64, bool is i1, float is double, and void and never are void
    llvm::Type* lower(SemaType* sematype) {
        if (lowered.find(sematype) != lowered.end()) return lowered[sematype];
        switch (sematype->kind) {
            case SemaTypeKind::Primitive:
                switch (static_cast<PrimitiveSemaType*>(sematype)->primitive) {
                    case PrimitiveKind::Bool:
                        return lowered[sematype] = llvm::Type::getInt1Ty(context);
                    case PrimitiveKind::Float:
                        return lowered[sematype] = llvm::Type::getDoubleTy(context);
                    case PrimitiveKind::Void:
                    case PrimitiveKind::Never:
                        return lowered[sematype] = llvm::Type::getVoidTy(context);
                    default:
                        llvm_unreachable("unknown primitive");
                        return nullptr;
                }
            case SemaTypeKind::Int:
                return lowered[sematype] = llvm::Type::getIntNTy(context, static_cast<IntSemaType*>(sematype)->bits);
            case SemaTypeKind::Function: {
                auto fn = static_cast<FunctionSemaType*>(sematype);
                std::vector<llvm::Type*> params;
                for (auto p : fn->params) {
                    params.push_back(lower(p));
                }
                return lowered[sematype] = llvm::FunctionType::get(lower(fn->returns), params, false);
            }
            case SemaTypeKind::Error:
                llvm_unreachable("error type reached codegen");
                return nullptr;
            default:
                llvm_unreachable("unknown type");
                return nullptr;
        }
    }

    // the LLVM function for every function symbol, filled in before any body is emitted so calls can reach functions defined later
    std::unordered_map<Symbol*, llvm::Function*> declared;
    // every function in the order it was declared, nested ones included, so they can all be defined once every signature exists
    std::vector<FnDecl*> functions;

    // creates the LLVM function for a declaration, named prefix.name, then declares the functions nested in its body under its own name
    void declare(FnDecl& fn, std::string prefix) {
        auto type = llvm::cast<llvm::FunctionType>(lower(fn.decl->type));
        llvm::Function* f = llvm::Function::Create(type, llvm::Function::ExternalLinkage, prefix + "." + std::string(fn.decl->name), *module);
        declared[fn.decl] = f;
        functions.push_back(&fn);
        if (fn.body) collectStmts(fn.body->stmts, prefix + "." + std::string(fn.decl->name));
    }

    // finds the functions declared anywhere in a list of statements, including inside scopes nested in expressions, and declares them under prefix
    void collectStmts(std::vector<std::unique_ptr<Stmt>>& stmts, const std::string& prefix) {
        for (auto& stmt : stmts) collectStmt(*stmt, prefix);
    }

    // looks through one statement for nested functions, following any expression that can hold statements
    void collectStmt(Stmt& s, const std::string& prefix) {
        switch (s.kind) {
            case StmtKind::Decl: {
                auto& decl = *static_cast<DeclStmt&>(s).decl;
                if (decl.kind == DeclKind::Fn) declare(static_cast<FnDecl&>(decl), prefix);
                break;
            }
            case StmtKind::Attr: {
                auto& attr = static_cast<AttrStmt&>(s);
                if (attr.target) collectStmt(*attr.target, prefix);
                break;
            }
            case StmtKind::Let:
                collectExpr(*static_cast<LetStmt&>(s).init, prefix);
                break;
            case StmtKind::Assign:
                collectExpr(*static_cast<AssignStmt&>(s).value, prefix);
                break;
            case StmtKind::Expr:
                collectExpr(*static_cast<ExprStmt&>(s).expr, prefix);
                break;
            case StmtKind::Yield: {
                auto& yield = static_cast<YieldStmt&>(s);
                if (yield.value) collectExpr(*yield.value, prefix);
                break;
            }
            case StmtKind::Assert:
                collectExpr(*static_cast<AssertStmt&>(s).cond, prefix);
                break;
            default:
                break;
        }
    }

    // looks through one expression for scopes, since a block, match, loop, or chunk anywhere in it can declare functions
    void collectExpr(Expr& e, const std::string& prefix) {
        switch (e.kind) {
            case ExprKind::Block:
                collectStmts(static_cast<BlockExpr&>(e).stmts, prefix);
                break;
            case ExprKind::Match: {
                auto& m = static_cast<MatchExpr&>(e);
                if (m.subject) collectExpr(*m.subject, prefix);
                for (auto& arm : m.arms) {
                    if (arm.pattern->kind == PatternKind::Guard) collectExpr(*static_cast<GuardPattern&>(*arm.pattern).cond, prefix);
                    collectExpr(*arm.body, prefix);
                }
                break;
            }
            case ExprKind::Repeat: {
                auto& r = static_cast<RepeatExpr&>(e);
                if (r.cond) collectExpr(*r.cond, prefix);
                collectStmts(r.body->stmts, prefix);
                break;
            }
            case ExprKind::Chunk:
                collectStmts(static_cast<ChunkExpr&>(e).body->stmts, prefix);
                break;
            case ExprKind::Unary:
                collectExpr(*static_cast<UnaryExpr&>(e).operand, prefix);
                break;
            case ExprKind::Binary: {
                auto& b = static_cast<BinaryExpr&>(e);
                collectExpr(*b.lhs, prefix);
                collectExpr(*b.rhs, prefix);
                break;
            }
            case ExprKind::Chain:
                for (auto& operand : static_cast<ChainExpr&>(e).operands) collectExpr(*operand, prefix);
                break;
            case ExprKind::Call:
                for (auto& arg : static_cast<CallExpr&>(e).args) collectExpr(*arg, prefix);
                break;
            case ExprKind::Pipe: {
                auto& p = static_cast<PipeExpr&>(e);
                collectExpr(*p.lhs, prefix);
                collectExpr(*p.rhs, prefix);
                break;
            }
            default:
                break;
        }
    }

    // checks whether a sema type is float, which decides between integer and float instructions
    bool isFloat(SemaType* type) {
        return type->kind == SemaTypeKind::Primitive && static_cast<PrimitiveSemaType*>(type)->primitive == PrimitiveKind::Float;
    }

    // the integer type a sema type is, or null if it isn't one
    IntSemaType* asInt(SemaType* type) {
        return type != nullptr && type->kind == SemaTypeKind::Int ? static_cast<IntSemaType*>(type) : nullptr;
    }

    // checks whether a sema type is an unsigned integer, which decides between signed and unsigned instructions
    bool isUnsigned(SemaType* type) {
        IntSemaType* integer = asInt(type);
        return integer != nullptr && !integer->isSigned;
    }

    // emits one built-in comparison, signed or unsigned for ints, and ordered for floats, which can't be NaN
    llvm::Value* compare(TokenKind op, llvm::Value* lhs, llvm::Value* rhs, bool floats, bool unsignedInts = false) {
        switch (op) {
            case TokenKind::Lt:
                if (floats) return irbuilder.CreateFCmp(llvm::FCmpInst::FCMP_OLT, lhs, rhs);
                return irbuilder.CreateICmp(unsignedInts ? llvm::CmpInst::ICMP_ULT : llvm::CmpInst::ICMP_SLT, lhs, rhs);
            case TokenKind::Le:
                if (floats) return irbuilder.CreateFCmp(llvm::FCmpInst::FCMP_OLE, lhs, rhs);
                return irbuilder.CreateICmp(unsignedInts ? llvm::CmpInst::ICMP_ULE : llvm::CmpInst::ICMP_SLE, lhs, rhs);
            case TokenKind::Gt:
                if (floats) return irbuilder.CreateFCmp(llvm::FCmpInst::FCMP_OGT, lhs, rhs);
                return irbuilder.CreateICmp(unsignedInts ? llvm::CmpInst::ICMP_UGT : llvm::CmpInst::ICMP_SGT, lhs, rhs);
            case TokenKind::Ge:
                if (floats) return irbuilder.CreateFCmp(llvm::FCmpInst::FCMP_OGE, lhs, rhs);
                return irbuilder.CreateICmp(unsignedInts ? llvm::CmpInst::ICMP_UGE : llvm::CmpInst::ICMP_SGE, lhs, rhs);
            case TokenKind::Eq:
            case TokenKind::EqEq:
                if (floats) return irbuilder.CreateFCmp(llvm::FCmpInst::FCMP_OEQ, lhs, rhs);
                return irbuilder.CreateICmp(llvm::CmpInst::ICMP_EQ, lhs, rhs);
            case TokenKind::Ne:
            case TokenKind::NeEq:
                if (floats) return irbuilder.CreateFCmp(llvm::FCmpInst::FCMP_ONE, lhs, rhs);
                return irbuilder.CreateICmp(llvm::CmpInst::ICMP_NE, lhs, rhs);
            default:
                llvm_unreachable("not a comparison");
        }
    }

    // stops the program with a message when fails is true at run time, and otherwise carries on in a fresh block, which every runtime check goes through
    void trapIf(llvm::Value* fails, const std::string& message, Span span) {
        llvm::Function* f = irbuilder.GetInsertBlock()->getParent();
        llvm::BasicBlock* fail = llvm::BasicBlock::Create(context, "trap", f);
        llvm::BasicBlock* ok = llvm::BasicBlock::Create(context, "ok", f);
        irbuilder.CreateCondBr(fails, fail, ok);
        irbuilder.SetInsertPoint(fail);
        uint64_t line = manager.getLineCol(span.start).line;
        irbuilder.CreateCall(trap(), {irbuilder.CreateGlobalString(message), irbuilder.getInt64(line)});
        irbuilder.CreateUnreachable();
        irbuilder.SetInsertPoint(ok);
    }

    // the source text of a span, quoted, for trap messages like 'x * y' overflowed i8
    std::string quoted(Span span) {
        return "'" + std::string(manager.text().substr(span.start, span.length())) + "'";
    }

    // emits + - or * on a trapping integer through an overflow intrinsic, which gives the result and whether it overflowed, and traps when it did
    llvm::Value* checked(llvm::Intrinsic::ID id, llvm::Value* lhs, llvm::Value* rhs, IntSemaType& type, Span span) {
        llvm::Function* intrinsic = llvm::Intrinsic::getOrInsertDeclaration(module.get(), id, {lhs->getType()});
        llvm::Value* pair = irbuilder.CreateCall(intrinsic, {lhs, rhs});
        llvm::Value* result = irbuilder.CreateExtractValue(pair, 0);
        llvm::Value* overflowed = irbuilder.CreateExtractValue(pair, 1);
        trapIf(overflowed, quoted(span) + " overflowed " + type.toString(), span);
        return result;
    }

    // emits / or % on integers: a zero divisor always traps, and the one signed case that overflows, the minimum divided by -1, traps or wraps with the type
    llvm::Value* divide(bool isDivide, llvm::Value* lhs, llvm::Value* rhs, IntSemaType& type, Span span) {
        llvm::Type* llvmType = lhs->getType();
        llvm::Value* zero = irbuilder.CreateICmpEQ(rhs, llvm::ConstantInt::get(llvmType, 0));
        trapIf(zero, quoted(span) + " divided by zero", span);
        if (type.isSigned) {
            llvm::Value* minimum = llvm::ConstantInt::get(llvmType, llvm::APInt::getSignedMinValue(type.bits));
            llvm::Value* minusOne = llvm::ConstantInt::getSigned(llvmType, -1);
            llvm::Value* overflows = irbuilder.CreateAnd(irbuilder.CreateICmpEQ(lhs, minimum), irbuilder.CreateICmpEQ(rhs, minusOne));
            if (!type.isWrapping) {
                trapIf(overflows, quoted(span) + " overflowed " + type.toString(), span);
            } else {
                rhs = irbuilder.CreateSelect(overflows, llvm::ConstantInt::getSigned(llvmType, 1), rhs);
            }
        }
        if (isDivide) return type.isSigned ? irbuilder.CreateSDiv(lhs, rhs) : irbuilder.CreateUDiv(lhs, rhs);
        return type.isSigned ? irbuilder.CreateSRem(lhs, rhs) : irbuilder.CreateURem(lhs, rhs);
    }

    // emits << or >>, where a trapping type traps on an amount outside 0 to bits - 1, a wrapping type takes the amount modulo the width, and >> keeps the sign only for signed types
    llvm::Value* shift(bool isLeft, llvm::Value* lhs, llvm::Value* rhs, IntSemaType& type, Span span) {
        llvm::Value* width = llvm::ConstantInt::get(lhs->getType(), type.bits);
        if (type.isWrapping) {
            rhs = irbuilder.CreateAnd(rhs, llvm::ConstantInt::get(lhs->getType(), type.bits - 1));
        } else {
            llvm::Value* outOfRange = irbuilder.CreateICmpUGE(rhs, width);
            trapIf(outOfRange, quoted(span) + " shifted by an amount outside 0 to " + std::to_string(type.bits - 1), span);
        }
        if (isLeft) return irbuilder.CreateShl(lhs, rhs);
        return type.isSigned ? irbuilder.CreateAShr(lhs, rhs) : irbuilder.CreateLShr(lhs, rhs);
    }

    // converts an integer to another integer type: a wrapping target keeps the low bits, and a trapping one checks the value survived and traps if it didn't
    llvm::Value* convert(llvm::Value* value, IntSemaType& source, IntSemaType& target, Span span) {
        llvm::Type* targetType = lower(&target);
        llvm::Value* converted;
        if (source.bits > target.bits) {
            converted = irbuilder.CreateTrunc(value, targetType);
        } else if (source.bits < target.bits) {
            converted = source.isSigned ? irbuilder.CreateSExt(value, targetType) : irbuilder.CreateZExt(value, targetType);
        } else {
            converted = value;
        }
        if (target.isWrapping) return converted;
        // converting back has to give the original value, which catches narrowing that lost bits
        llvm::Type* sourceType = value->getType();
        llvm::Value* back;
        if (target.bits > source.bits) {
            back = irbuilder.CreateTrunc(converted, sourceType);
        } else if (target.bits < source.bits) {
            back = target.isSigned ? irbuilder.CreateSExt(converted, sourceType) : irbuilder.CreateZExt(converted, sourceType);
        } else {
            back = converted;
        }
        llvm::Value* fits = irbuilder.CreateICmpEQ(back, value);
        // and the sign has to survive: -1 as an i8 and 255 as an i8u have the same bits, so a value that changes signedness also has to be at least zero on the signed side
        llvm::Value* zero = llvm::ConstantInt::get(sourceType, 0);
        if (source.isSigned && !target.isSigned) fits = irbuilder.CreateAnd(fits, irbuilder.CreateICmpSGE(value, zero));
        if (!source.isSigned && target.isSigned) fits = irbuilder.CreateAnd(fits, irbuilder.CreateICmpSGE(converted, llvm::ConstantInt::get(targetType, 0)));
        trapIf(irbuilder.CreateNot(fits), quoted(span) + " doesn't fit in " + target.toString(), span);
        return converted;
    }

    // emits one built-in arithmetic or bitwise operator on two values of the same type, which compound assignment shares through the base of X<-
    llvm::Value* arithmetic(TokenKind op, llvm::Value* lhs, llvm::Value* rhs, SemaType* type, Span span) {
        bool floats = isFloat(type);
        IntSemaType* integer = asInt(type);
        // a trapping integer's + - and * go through the overflow intrinsics, signed or unsigned, and a wrapping one's are plain two's complement
        if (integer && !integer->isWrapping && (op == TokenKind::Plus || op == TokenKind::Minus || op == TokenKind::Star)) {
            bool s = integer->isSigned;
            llvm::Intrinsic::ID id = op == TokenKind::Plus ? (s ? llvm::Intrinsic::sadd_with_overflow : llvm::Intrinsic::uadd_with_overflow)
                : op == TokenKind::Minus ? (s ? llvm::Intrinsic::ssub_with_overflow : llvm::Intrinsic::usub_with_overflow)
                : (s ? llvm::Intrinsic::smul_with_overflow : llvm::Intrinsic::umul_with_overflow);
            return checked(id, lhs, rhs, *integer, span);
        }
        switch (op) {
            case TokenKind::Plus:
                if (floats) return irbuilder.CreateFAdd(lhs, rhs);
                return irbuilder.CreateAdd(lhs, rhs);
            case TokenKind::Minus:
                if (floats) return irbuilder.CreateFSub(lhs, rhs);
                return irbuilder.CreateSub(lhs, rhs);
            case TokenKind::Star:
                if (floats) return irbuilder.CreateFMul(lhs, rhs);
                return irbuilder.CreateMul(lhs, rhs);
            case TokenKind::Slash:
                if (floats) return irbuilder.CreateFDiv(lhs, rhs);
                return divide(true, lhs, rhs, *integer, span);
            case TokenKind::Percent:
                if (floats) return irbuilder.CreateFRem(lhs, rhs);
                return divide(false, lhs, rhs, *integer, span);
            case TokenKind::Amp:
                return irbuilder.CreateAnd(lhs, rhs);
            case TokenKind::Bar:
                return irbuilder.CreateOr(lhs, rhs);
            case TokenKind::Caret:
                return irbuilder.CreateXor(lhs, rhs);
            case TokenKind::Shl:
                return shift(true, lhs, rhs, *integer, span);
            case TokenKind::Shr:
                return shift(false, lhs, rhs, *integer, span);
            default:
                llvm_unreachable("not an arithmetic operator");
        }
    }

    // a && b only evaluates b when a is true, and a || b only when a is false, so the right side gets its own block and a phi picks the answer
    llvm::Value* emitShortCircuit(BinaryExpr& binary) {
        bool isAnd = binary.op.kind == TokenKind::AmpAmp;
        llvm::Function* f = irbuilder.GetInsertBlock()->getParent();
        llvm::BasicBlock* rhsBlock = llvm::BasicBlock::Create(context, isAnd ? "and.rhs" : "or.rhs", f);
        llvm::BasicBlock* end = llvm::BasicBlock::Create(context, isAnd ? "and.end" : "or.end", f);
        llvm::Value* lhs = emitExpr(*binary.lhs);
        // the block the left side finished in, which isn't always where it started
        llvm::BasicBlock* lhsEnd = irbuilder.GetInsertBlock();
        if (isAnd) {
            irbuilder.CreateCondBr(lhs, rhsBlock, end);
        } else {
            irbuilder.CreateCondBr(lhs, end, rhsBlock);
        }
        irbuilder.SetInsertPoint(rhsBlock);
        llvm::Value* rhs = emitExpr(*binary.rhs);
        llvm::BasicBlock* rhsEnd = irbuilder.GetInsertBlock();
        irbuilder.CreateBr(end);
        irbuilder.SetInsertPoint(end);
        llvm::PHINode* phi = irbuilder.CreatePHI(irbuilder.getInt1Ty(), 2);
        phi->addIncoming(irbuilder.getInt1(!isAnd), lhsEnd);
        phi->addIncoming(rhs, rhsEnd);
        return phi;
    }

    // calls a function, where a void function's call has no value
    llvm::Value* emitCall(llvm::Function* f, std::vector<llvm::Value*> args) {
        llvm::Value* result = irbuilder.CreateCall(f, args);
        return f->getReturnType()->isVoidTy() ? nullptr : result;
    }

    // emits an expression into the current block and returns its value, or null for void and never, which have none
    llvm::Value* emitExpr(Expr& e) {
        switch (e.kind) {
            // literals, where the text is kept raw by the lexer, so separators come out here and the prefix picks the radix
            case ExprKind::IntLiteral: {
                std::string_view literal = static_cast<IntLiteralExpr*>(&e)->literal;
                std::string cleaned;
                for (char c : literal) if (c != '_') cleaned.push_back(c);
                unsigned bits = static_cast<IntSemaType*>(static_cast<IntLiteralExpr*>(&e)->type)->bits;
                llvm::APInt llvmValue;
                if (cleaned.substr(0, 2) == "0x" || cleaned.substr(0, 2) == "0X") {
                    llvmValue = llvm::APInt(bits, cleaned.substr(2), 16);
                } else if (cleaned.substr(0, 2) == "0o" || cleaned.substr(0, 2) == "0O") {
                    llvmValue = llvm::APInt(bits, cleaned.substr(2), 8);
                } else if (cleaned.substr(0, 2) == "0b" || cleaned.substr(0, 2) == "0B") {
                    llvmValue = llvm::APInt(bits, cleaned.substr(2), 2);
                } else {
                    llvmValue = llvm::APInt(bits, cleaned, 10);
                }
                return irbuilder.getInt(llvmValue);
            }
            // only decimal floats for now, since APFloat reads other bases with an exponent cherimoya doesn't write
            case ExprKind::FloatLiteral: {
                std::string_view literal = static_cast<FloatLiteralExpr*>(&e)->literal;
                std::string cleaned;
                for (char c : literal) if (c != '_') cleaned.push_back(c);
                if (cleaned.substr(0, 2) == "0x" || cleaned.substr(0, 2) == "0X") {
                    llvm_unreachable("hex floats are currently unimplemented");
                    return nullptr;
                } else if (cleaned.substr(0, 2) == "0o" || cleaned.substr(0, 2) == "0O") {
                    llvm_unreachable("octal floats are currently unimplemented");
                    return nullptr;
                } else if (cleaned.substr(0, 2) == "0b" || cleaned.substr(0, 2) == "0B") {
                    llvm_unreachable("binary floats are currently unimplemented");
                    return nullptr;
                } else {
                    return llvm::ConstantFP::get(irbuilder.getDoubleTy(), cleaned);
                }
            }
            case ExprKind::BoolLiteral: {
                return irbuilder.getInt1(static_cast<BoolLiteralExpr*>(&e)->literal);
            }
            // prefix operators, where ! and ~ are both a bitwise not, which on an i1 is logical not
            case ExprKind::Unary: {
                auto* unary = static_cast<UnaryExpr*>(&e);
                auto* operand = emitExpr(*unary->operand.get());
                bool floats = isFloat(unary->operand->type);
                IntSemaType* integer = asInt(unary->operand->type);
                switch (unary->op.kind) {
                    case TokenKind::Minus:
                        if (floats) return irbuilder.CreateFNeg(operand);
                        // a negated literal is a constant sema already checked, like -128 as an i8, whose 128 would overflow if it were negated at run time
                        if (unary->operand->kind == ExprKind::IntLiteral) return irbuilder.CreateNeg(operand);
                        // negating a trapping signed integer overflows only for the minimum, so it's 0 - x through the checked subtraction
                        if (integer && !integer->isWrapping) return checked(llvm::Intrinsic::ssub_with_overflow, llvm::ConstantInt::get(operand->getType(), 0), operand, *integer, e.span);
                        return irbuilder.CreateNeg(operand);
                    case TokenKind::Tilde:
                    case TokenKind::Bang:
                        return irbuilder.CreateNot(operand);
                    default:
                        llvm_unreachable("sema rejected every other unary operator");
                        return nullptr;
                }
            }
            // binary operators, where both sides have the same type, so the left one decides integer or float
            case ExprKind::Binary: {
                auto* binary = static_cast<BinaryExpr*>(&e);
                if (binary->op.kind == TokenKind::AmpAmp || binary->op.kind == TokenKind::BarBar) return emitShortCircuit(*binary);
                auto* lhs = emitExpr(*binary->lhs.get());
                auto* rhs = emitExpr(*binary->rhs.get());
                bool floats = isFloat(binary->lhs->type);
                switch (binary->op.kind) {
                    case TokenKind::Plus:
                    case TokenKind::Minus:
                    case TokenKind::Star:
                    case TokenKind::Slash:
                    case TokenKind::Percent:
                    case TokenKind::Amp:
                    case TokenKind::Bar:
                    case TokenKind::Caret:
                    case TokenKind::Shl:
                    case TokenKind::Shr:
                        return arithmetic(binary->op.kind, lhs, rhs, binary->lhs->type, e.span);
                    case TokenKind::CaretCaret:
                        return irbuilder.CreateXor(lhs, rhs);
                    case TokenKind::Lt:
                    case TokenKind::Le:
                    case TokenKind::Gt:
                    case TokenKind::Ge:
                    case TokenKind::Eq:
                    case TokenKind::EqEq:
                    case TokenKind::Ne:
                    case TokenKind::NeEq:
                        return compare(binary->op.kind, lhs, rhs, floats, isUnsigned(binary->lhs->type));
                    default:
                        llvm_unreachable("sema rejected every other binary operator");
                        return nullptr;
                }
            }
            // a < b <= c is a < b and b <= c, where every operand is emitted once, so unlike &&, nothing is skipped
            case ExprKind::Chain: {
                auto* chain = static_cast<ChainExpr*>(&e);
                bool floats = isFloat(chain->operands[0]->type);
                std::vector<llvm::Value*> operands;
                for (auto& operand : chain->operands) {
                    operands.push_back(emitExpr(*operand));
                }
                llvm::Value* result = nullptr;
                for (size_t i = 0; i < chain->ops.size(); i++) {
                    auto* pair = compare(chain->ops[i].kind, operands[i], operands[i + 1], floats, isUnsigned(chain->operands[0]->type));
                    result = result ? irbuilder.CreateAnd(result, pair) : pair;
                }
                return result;
            }
            // a name reads its stack slot or its global
            case ExprKind::Identifier: {
                Symbol* symbol = static_cast<IdentifierExpr*>(&e)->decl;
                return irbuilder.CreateLoad(lower(symbol->type), addressOf(symbol));
            }
            // a call's callee is always a function's name, so it's looked up instead of emitted
            case ExprKind::Call: {
                auto* call = static_cast<CallExpr*>(&e);
                // a type called on a value converts it
                if (call->callee->kind == ExprKind::TypeExpr) {
                    llvm::Value* value = emitExpr(*call->args[0]);
                    return convert(value, *asInt(call->args[0]->type), *asInt(call->callee->type), e.span);
                }
                auto* callee = static_cast<IdentifierExpr*>(call->callee.get());
                std::vector<llvm::Value*> args;
                for (auto& arg : call->args) {
                    args.push_back(emitExpr(*arg));
                }
                auto fun = declared.find(callee->decl);
                if (fun == declared.end()) {
                    llvm_unreachable("the given function was not declared");
                    return nullptr;
                }
                return emitCall(fun->second, args);
            }
            // scopes
            case ExprKind::Block: {
                auto& block = static_cast<BlockExpr&>(e);
                return emitScope(block.stmts, TargetKind::Block, block.name, block.type);
            }
            // a chunk runs as a block, since it only differs once it has an allocator
            case ExprKind::Chunk: {
                auto& chunk = static_cast<ChunkExpr&>(e);
                return emitScope(chunk.body.get()->stmts, TargetKind::Chunk, chunk.name, chunk.type);
            }
            case ExprKind::Match:
                return emitMatch(static_cast<MatchExpr&>(e));
            case ExprKind::Repeat:
                return emitRepeat(static_cast<RepeatExpr&>(e));
            // x |> f and f <| x are both f(x)
            case ExprKind::Pipe: {
                auto* pipe = static_cast<PipeExpr*>(&e);
                switch (pipe->op.kind) {
                    // a pipe into a type converts, x |> i8 being i8(x)
                    case TokenKind::PipeFwd: {
                        if (pipe->rhs->kind == ExprKind::TypeExpr) return convert(emitExpr(*pipe->lhs), *asInt(pipe->lhs->type), *asInt(pipe->rhs->type), e.span);
                        auto* lhs = emitExpr(*pipe->lhs.get());
                        auto* rhs = static_cast<IdentifierExpr*>(pipe->rhs.get());
                        auto fun = declared.find(rhs->decl);
                        if (fun == declared.end()) {
                            llvm_unreachable("the given function was not declared");
                            return nullptr;
                        }
                        return emitCall(fun->second, {lhs});
                    }
                    case TokenKind::PipeBack: {
                        if (pipe->lhs->kind == ExprKind::TypeExpr) return convert(emitExpr(*pipe->rhs), *asInt(pipe->rhs->type), *asInt(pipe->lhs->type), e.span);
                        auto* lhs = static_cast<IdentifierExpr*>(pipe->lhs.get());
                        auto* rhs = emitExpr(*pipe->rhs.get());
                        auto fun = declared.find(lhs->decl);
                        if (fun == declared.end()) {
                            llvm_unreachable("the given function was not declared");
                            return nullptr;
                        }
                        return emitCall(fun->second, {rhs});
                    }
                    default: {
                        llvm_unreachable("the given symbol used to pipe is invalid");
                        return nullptr;
                    }
                }
            }
            default:
                llvm_unreachable("sema rejected every other expression");
                return nullptr;
        }
    }
    // finds or declares the runtime's trap, which prints a message with its line and aborts, and never returns
    llvm::Function* trap() {
        llvm::FunctionType* type = llvm::FunctionType::get(irbuilder.getVoidTy(), {irbuilder.getPtrTy(), irbuilder.getInt64Ty()}, false);
        llvm::Function* f = llvm::cast<llvm::Function>(module->getOrInsertFunction("cheri_trap", type).getCallee());
        // telling LLVM the call never returns lets it drop everything after a trap
        f->addFnAttr(llvm::Attribute::NoReturn);
        return f;
    }

    // one entry per scope a yield could target, mirroring sema's yield-target stack: where the scope continues, and where its value goes
    struct Exit {
        TargetKind kind;
        std::optional<std::string_view> label;
        llvm::BasicBlock* end;
        llvm::AllocaInst* result;
    };
    std::vector<Exit> exits;

    // finds a yield's target with sema's rules: bare goes to the innermost, -| match and -| repeat to the innermost of their kind, and a name to that label
    Exit& resolveExit(YieldStmt& yield) {
        for (size_t i = exits.size(); i-- > 0;) {
            Exit& exit = exits[i];
            if (!yield.target) return exit;
            auto kind = yield.target->kind;
            if (kind == TokenKind::KwMatch && exit.kind == TargetKind::Match) return exit;
            if (kind == TokenKind::KwRepeat && exit.kind == TargetKind::Loop) return exit;
            if (kind == TokenKind::Identifier && exit.label == yield.target->text) return exit;
        }
        llvm_unreachable("sema resolved every yield");
    }

    // checks whether a scope's type is a value that has to be carried out of it, which void and never aren't
    bool carriesValue(SemaType* type) {
        if (type->kind != SemaTypeKind::Primitive) return true;
        auto primitive = static_cast<PrimitiveSemaType*>(type)->primitive;
        return primitive != PrimitiveKind::Void && primitive != PrimitiveKind::Never;
    }

    // makes an unnamed stack slot at the top of the current function, for values like a scope's result
    llvm::AllocaInst* makeTemp(llvm::Type* type) {
        llvm::Function* f = irbuilder.GetInsertBlock()->getParent();
        llvm::IRBuilder<> entry(&f->getEntryBlock(), f->getEntryBlock().begin());
        return entry.CreateAlloca(type, nullptr, "result");
    }

    // the stack slot of every local and parameter, which mem2reg turns into registers when optimizing
    std::unordered_map<Symbol*, llvm::AllocaInst*> locals;

    // makes a symbol's stack slot at the top of the current function, so a let in a loop gets one slot instead of one per iteration
    llvm::AllocaInst* makeSlot(Symbol* symbol) {
        llvm::Function* f = irbuilder.GetInsertBlock()->getParent();
        llvm::IRBuilder<> entry(&f->getEntryBlock(), f->getEntryBlock().begin());
        auto* slot = entry.CreateAlloca(lower(symbol->type), nullptr, symbol->name);
        locals[symbol] = slot;
        return slot;
    }

    // the global for every top-level binding, which the init function fills in before main runs
    std::unordered_map<Symbol*, llvm::GlobalVariable*> globals;

    // finds where a symbol's value lives: a stack slot for locals and parameters, or a global for top-level bindings
    llvm::Value* addressOf(Symbol* symbol) {
        auto local = locals.find(symbol);
        if (local != locals.end()) return local->second;
        auto global = globals.find(symbol);
        if (global != globals.end()) return global->second;
        llvm_unreachable("every name has a slot or a global");
        return nullptr;
    }

    // makes the global for a top-level binding, named like a function, cheri.module.name, and starting out as zero until the init function writes it
    llvm::GlobalVariable* makeGlobal(Symbol* symbol) {
        llvm::Type* type = lower(symbol->type);
        auto* global = new llvm::GlobalVariable(*module, type, false, llvm::GlobalValue::InternalLinkage, llvm::Constant::getNullValue(type), symbolPrefix + "." + std::string(symbol->name));
        globals[symbol] = global;
        return global;
    }

    // emits one statement into the current block
    void emitStmt(Stmt& s) {
        auto* stmt = &s;
        switch (stmt->kind) {
            // a yield to the function returns, and a yield to anything else stores its value in the target's result and jumps to its end
            case StmtKind::Yield: {
                auto* yield = static_cast<YieldStmt*>(stmt);
                llvm::Value* value = yield->value ? emitExpr(*yield->value) : nullptr;
                // the value itself can end in a yield elsewhere, like a match whose arms all leave, and then there's nothing left to do
                if (irbuilder.GetInsertBlock()->getTerminator()) break;
                Exit& exit = resolveExit(*yield);
                if (exit.kind == TargetKind::Function) {
                    if (value) {
                        irbuilder.CreateRet(value);
                    } else {
                        irbuilder.CreateRetVoid();
                    }
                    break;
                }
                if (value && exit.result) irbuilder.CreateStore(value, exit.result);
                irbuilder.CreateBr(exit.end);
                break;
            }
            // a binding gets a slot, which <- _ leaves unwritten
            case StmtKind::Let: {
                auto* let = static_cast<LetStmt*>(stmt);
                // let _ <- init binds nothing, but init still has to run
                if (let->pattern->kind != PatternKind::Binder) {
                    emitExpr(*let->init);
                    break;
                }
                auto* symbol = static_cast<BinderPattern*>(let->pattern.get())->decl;
                // a binding outside every function is a global, and an immutable one whose initializer folds to a constant needs no code at all, while anything else is stored each time its let runs, which a top-level loop can make more than once
                if (symbol->owner == 0) {
                    llvm::GlobalVariable* global = makeGlobal(symbol);
                    if (let->init->kind == ExprKind::Wildcard) break;
                    llvm::Value* value = emitExpr(*let->init);
                    auto* constant = llvm::dyn_cast<llvm::Constant>(value);
                    if (constant && !symbol->isMutable) {
                        global->setInitializer(constant);
                        global->setConstant(true);
                    } else {
                        irbuilder.CreateStore(value, global);
                    }
                    break;
                }
                auto* slot = makeSlot(symbol);
                if (let->init->kind != ExprKind::Wildcard) irbuilder.CreateStore(emitExpr(*let->init), slot);
                break;
            }
            // _ <- value discards, place <- value stores, and place op<- value is place <- place op value
            case StmtKind::Assign: {
                auto* assign = static_cast<AssignStmt*>(stmt);
                auto* value = emitExpr(*assign->value);
                if (assign->place->kind == ExprKind::Wildcard) break;
                auto* place = static_cast<IdentifierExpr*>(assign->place.get());
                llvm::Value* address = addressOf(place->decl);
                if (assign->op) {
                    llvm::Value* current = irbuilder.CreateLoad(lower(place->decl->type), address);
                    TokenKind base = assign->op->op->base->kind;
                    value = arithmetic(base, current, value, assign->place->type, Span{assign->place->span.start, assign->value->span.end});
                }
                irbuilder.CreateStore(value, address);
                break;
            }
            // an expression statement is only there for what it does, since sema made every value be used or discarded
            case StmtKind::Expr: {
                auto* expr = static_cast<ExprStmt*>(stmt);
                emitExpr(*expr->expr);
                break;
            }
            // nested functions are defined on their own once every signature exists, so there's nothing to emit where they're written
            case StmtKind::Decl:
                break;
            // an attribute's target is an ordinary statement
            case StmtKind::Attr: {
                auto* attr = static_cast<AttrStmt*>(stmt);
                if (attr->target) emitStmt(*attr->target);
                break;
            }
            // wit obligations are for the verifier, and never run
            case StmtKind::Wit:
                break;
            // assert (cond) checks cond at run time, and traps with the assertion's line and source text when it's false
            case StmtKind::Assert: {
                auto* check = static_cast<AssertStmt*>(stmt);
                llvm::Value* cond = emitExpr(*check->cond);
                trapIf(irbuilder.CreateNot(cond), std::string(manager.text().substr(stmt->span.start, stmt->span.length())), stmt->span);
                break;
            }
            default:
                llvm_unreachable("sema rejected every other statement");
        }
    }

    // emits a list of statements, stopping once one of them leaves, since sema already warned that the rest can't run
    void emitStmts(std::vector<std::unique_ptr<Stmt>>& stmts) {
        for (auto& stmt : stmts) {
            if (irbuilder.GetInsertBlock()->getTerminator()) break;
            emitStmt(*stmt);
        }
    }

    // tests whether the subject matches one literal, deeply by default, so floats compare by value, or shallowly with match (=, x), so floats compare bit for bit
    llvm::Value* matchesLiteral(llvm::Value* subject, Expr& literal, bool shallow) {
        llvm::Value* value = emitExpr(literal);
        bool floats = isFloat(literal.type);
        if (floats && shallow) {
            llvm::Value* subjectBits = irbuilder.CreateBitCast(subject, irbuilder.getInt64Ty());
            llvm::Value* valueBits = irbuilder.CreateBitCast(value, irbuilder.getInt64Ty());
            return compare(TokenKind::Eq, subjectBits, valueBits, false);
        }
        return compare(TokenKind::Eq, subject, value, floats);
    }

    // emits a match as a chain of tests where each arm checks its pattern and either runs its body or falls to the next arm's test, and every arm shares the match's end and result so a bare yield and -| match land in the same spot
    llvm::Value* emitMatch(MatchExpr& m) {
        llvm::Function* f = irbuilder.GetInsertBlock()->getParent();
        llvm::Value* subject = m.subject ? emitExpr(*m.subject) : nullptr;
        bool shallow = m.equality && m.equality->kind == TokenKind::Eq;
        llvm::BasicBlock* end = llvm::BasicBlock::Create(context, "match.end", f);
        llvm::AllocaInst* result = carriesValue(m.type) ? makeTemp(lower(m.type)) : nullptr;
        std::optional<std::string_view> label = std::nullopt;
        if (m.name) label = m.name->text;
        exits.push_back(Exit{TargetKind::Match, label, end, result});
        for (auto& arm : m.arms) {
            llvm::BasicBlock* body = llvm::BasicBlock::Create(context, "match.arm", f);
            llvm::BasicBlock* next = llvm::BasicBlock::Create(context, "match.next", f);
            // the test: catch-alls always run their body, literals compare, and guards are their own condition
            switch (arm.pattern->kind) {
                case PatternKind::Wildcard:
                case PatternKind::Binder:
                    irbuilder.CreateBr(body);
                    break;
                case PatternKind::Literal: {
                    llvm::Value* matched = matchesLiteral(subject, *static_cast<LiteralPattern&>(*arm.pattern).literal, shallow);
                    irbuilder.CreateCondBr(matched, body, next);
                    break;
                }
                case PatternKind::Guard: {
                    llvm::Value* cond = emitExpr(*static_cast<GuardPattern&>(*arm.pattern).cond);
                    irbuilder.CreateCondBr(cond, body, next);
                    break;
                }
                default:
                    llvm_unreachable("sema rejected every other pattern");
            }
            // the body: a binder holds the subject's value, then the statements run as their own scope
            irbuilder.SetInsertPoint(body);
            if (arm.pattern->kind == PatternKind::Binder) {
                auto* slot = makeSlot(static_cast<BinderPattern*>(arm.pattern.get())->decl);
                irbuilder.CreateStore(subject, slot);
            }
            exits.push_back(Exit{TargetKind::Arm, std::nullopt, end, result});
            emitStmts(arm.body->stmts);
            exits.pop_back();
            if (!irbuilder.GetInsertBlock()->getTerminator()) irbuilder.CreateBr(end);
            // the next arm's test goes where a failed test lands
            irbuilder.SetInsertPoint(next);
        }
        exits.pop_back();
        // past the last arm, no arm matched: sema made every match with a value cover everything, so only a void match can get here
        if (carriesValue(m.type)) {
            irbuilder.CreateUnreachable();
        } else {
            irbuilder.CreateBr(end);
        }
        irbuilder.SetInsertPoint(end);
        return result ? irbuilder.CreateLoad(result->getAllocatedType(), result) : nullptr;
    }

    // emits a loop as a header that decides whether to run the body again, where a while loop's header tests its condition and an infinite loop's header is its body
    llvm::Value* emitRepeat(RepeatExpr& r) {
        llvm::Function* f = irbuilder.GetInsertBlock()->getParent();
        llvm::BasicBlock* body = llvm::BasicBlock::Create(context, "repeat.body", f);
        llvm::BasicBlock* end = llvm::BasicBlock::Create(context, "repeat.end", f);
        llvm::BasicBlock* header = r.cond ? llvm::BasicBlock::Create(context, "repeat.header", f) : body;
        irbuilder.CreateBr(header);
        if (r.cond) {
            irbuilder.SetInsertPoint(header);
            llvm::Value* cond = emitExpr(*r.cond);
            irbuilder.CreateCondBr(cond, body, end);
        }
        // only an infinite loop can yield a value, so only it needs somewhere to put one
        llvm::AllocaInst* result = carriesValue(r.type) ? makeTemp(lower(r.type)) : nullptr;
        std::optional<std::string_view> label = std::nullopt;
        if (r.name) label = r.name->text;
        exits.push_back(Exit{TargetKind::Loop, label, end, result});
        irbuilder.SetInsertPoint(body);
        emitStmts(r.body->stmts);
        exits.pop_back();
        if (!irbuilder.GetInsertBlock()->getTerminator()) irbuilder.CreateBr(header);
        irbuilder.SetInsertPoint(end);
        return result ? irbuilder.CreateLoad(result->getAllocatedType(), result) : nullptr;
    }

    // emits a block or a chunk as its own scope: yields to it store into result and jump to end, and its value is loaded there
    llvm::Value* emitScope(std::vector<std::unique_ptr<Stmt>>& stmts, TargetKind kind, std::optional<Token> name, SemaType* type) {
        llvm::Function* f = irbuilder.GetInsertBlock()->getParent();
        llvm::BasicBlock* end = llvm::BasicBlock::Create(context, kind == TargetKind::Chunk ? "chunk.end" : "block.end", f);
        llvm::AllocaInst* result = carriesValue(type) ? makeTemp(lower(type)) : nullptr;
        std::optional<std::string_view> label = std::nullopt;
        if (name) label = name->text;
        exits.push_back(Exit{kind, label, end, result});
        emitStmts(stmts);
        exits.pop_back();
        if (!irbuilder.GetInsertBlock()->getTerminator()) irbuilder.CreateBr(end);
        irbuilder.SetInsertPoint(end);
        return result ? irbuilder.CreateLoad(result->getAllocatedType(), result) : nullptr;
    }

    // emits a function's body: every parameter is copied into a slot so it acts like a binding, then the statements run in the function's own scope
    void define(FnDecl& fn) {
        llvm::Function* f = declared[fn.decl];
        llvm::BasicBlock* definition = llvm::BasicBlock::Create(context, "definition", f);
        irbuilder.SetInsertPoint(definition);
        for (size_t i = 0; i < fn.inputs.size(); i++) {
            Symbol* symbol = fn.inputs[i].decl;
            (f->args().begin() + i)->setName(symbol->name);
            irbuilder.CreateStore(f->args().begin() + i, makeSlot(symbol));
        }
        // the body is the function's own scope, so a yield at its top level returns
        exits.push_back(Exit{TargetKind::Function, fn.name.text, nullptr, nullptr});
        emitStmts(fn.body->stmts);
        exits.pop_back();
        // a void function can fall off its end, and anything else ending here is a block nothing reaches, like the end of a scope every path left
        if (!irbuilder.GetInsertBlock()->getTerminator()) {
            if (f->getReturnType()->isVoidTy()) {
                irbuilder.CreateRetVoid();
            } else {
                irbuilder.CreateUnreachable();
            }
        }
    }
public:
    // name is the module's name, the source file's stem, and the source manager is only read, for trap messages
    Codegen(std::string_view name, SourceManager& m) : module(std::make_unique<llvm::Module>(name, context)), irbuilder(context), symbolPrefix("cheri." + std::string(name)), manager(m) {}
    // lowers a whole checked program: every function is declared first, nested ones included, then every body is defined, then main is exported for the runtime
    std::unique_ptr<llvm::Module> generate(std::vector<std::unique_ptr<Stmt>>& program) {
        // functions can be declared anywhere at the top, including inside top-level blocks, so the whole program is searched
        collectStmts(program, symbolPrefix);
        // everything at the top that isn't a function runs once, in order, in cheri.module.init, which the runtime calls before main
        llvm::Function* init = llvm::Function::Create(llvm::FunctionType::get(irbuilder.getVoidTy(), false), llvm::Function::ExternalLinkage, symbolPrefix + ".init", *module);
        irbuilder.SetInsertPoint(llvm::BasicBlock::Create(context, "definition", init));
        emitStmts(program);
        irbuilder.CreateRetVoid();
        llvm::GlobalAlias::create("cheri_init", init);
        // externs have no body, so they stay declarations for the linker to fill in
        for (FnDecl* fn : functions) {
            if (fn->body) define(*fn);
        }
        // the entry point is the main declared at the top of the file, looking through attributes, and not one nested in a top-level block, which gets the same prefix
        for (auto& stmt : program) {
            Stmt* top = stmt.get();
            while (top != nullptr && top->kind == StmtKind::Attr) top = static_cast<AttrStmt&>(*top).target.get();
            if (top == nullptr || top->kind != StmtKind::Decl) continue;
            auto& decl = *static_cast<DeclStmt&>(*top).decl;
            if (decl.kind == DeclKind::Fn && static_cast<FnDecl&>(decl).name.text == "main") llvm::GlobalAlias::create("cheri_main", declared[static_cast<FnDecl&>(decl).decl]);
        }
        return std::move(module);
    }
};