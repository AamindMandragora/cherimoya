#pragma once

#include "sema.hpp"

#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/Module.h>
#include <llvm/IR/IRBuilder.h>
#include <llvm/IR/Function.h>
#include <llvm/IR/BasicBlock.h>
#include <llvm/IR/Intrinsics.h>
#include <llvm/ADT/APFloat.h>
#include <llvm/ADT/StringExtras.h>
#include <llvm/Support/raw_ostream.h>
#include <llvm/Support/ErrorHandling.h>
#include <llvm/Support/Error.h>

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

    // the APFloat semantics of a float encoding, which its LLVM type and every one of its constants are built from
    const llvm::fltSemantics& semantics(FloatFormat format) {
        switch (format) {
            case FloatFormat::Half: return llvm::APFloat::IEEEhalf();
            case FloatFormat::BFloat: return llvm::APFloat::BFloat();
            case FloatFormat::Single: return llvm::APFloat::IEEEsingle();
            case FloatFormat::Double: return llvm::APFloat::IEEEdouble();
            case FloatFormat::Quad: return llvm::APFloat::IEEEquad();
        }
        llvm_unreachable("unknown float format");
    }

    // sema interns its types, so each one is lowered once and looked up by pointer after that
    std::unordered_map<SemaType*, llvm::Type*> lowered;

    // turns a sema type into an LLVM type: an integer is as wide as its type, bool is i1, a float is LLVM's type for its encoding, and void and never are void
    llvm::Type* lower(SemaType* type) {
        auto found = lowered.find(type);
        if (found != lowered.end()) return found->second;
        switch (type->kind) {
            case SemaTypeKind::Primitive:
                switch (static_cast<PrimitiveSemaType*>(type)->primitive) {
                    case PrimitiveKind::Bool:
                        return lowered[type] = irbuilder.getInt1Ty();
                    case PrimitiveKind::Void:
                    case PrimitiveKind::Never:
                        return lowered[type] = irbuilder.getVoidTy();
                }
                llvm_unreachable("unknown primitive");
            case SemaTypeKind::Int:
                return lowered[type] = irbuilder.getIntNTy(asInt(type)->bits);
            case SemaTypeKind::Float:
                return lowered[type] = llvm::Type::getFloatingPointTy(context, semantics(asFloat(type)->format));
            case SemaTypeKind::Function: {
                auto& function = static_cast<FunctionSemaType&>(*type);
                std::vector<llvm::Type*> params;
                for (SemaType* param : function.params) params.push_back(lower(param));
                return lowered[type] = llvm::FunctionType::get(lower(function.returns), params, false);
            }
            case SemaTypeKind::Error:
                llvm_unreachable("error type reached codegen");
        }
        llvm_unreachable("unknown type");
    }

    // the LLVM function for every function symbol, filled in before any body is emitted so calls can reach functions defined later
    std::unordered_map<Symbol*, llvm::Function*> declared;
    // every function in the order it was declared, nested ones included, so they can all be defined once every signature exists
    std::vector<FnDecl*> functions;

    // the LLVM function a call reaches, which is always declared by the time any body is emitted
    llvm::Function* functionOf(Symbol* symbol) {
        auto found = declared.find(symbol);
        if (found == declared.end()) llvm_unreachable("every function is declared before any body");
        return found->second;
    }

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
                auto& match = static_cast<MatchExpr&>(e);
                if (match.subject) collectExpr(*match.subject, prefix);
                for (auto& arm : match.arms) {
                    if (arm.pattern->kind == PatternKind::Guard) collectExpr(*static_cast<GuardPattern&>(*arm.pattern).cond, prefix);
                    collectExpr(*arm.body, prefix);
                }
                break;
            }
            case ExprKind::Repeat: {
                auto& repeat = static_cast<RepeatExpr&>(e);
                if (repeat.cond) collectExpr(*repeat.cond, prefix);
                collectStmts(repeat.body->stmts, prefix);
                break;
            }
            case ExprKind::Chunk:
                collectStmts(static_cast<ChunkExpr&>(e).body->stmts, prefix);
                break;
            case ExprKind::Unary:
                collectExpr(*static_cast<UnaryExpr&>(e).operand, prefix);
                break;
            case ExprKind::Binary: {
                auto& binary = static_cast<BinaryExpr&>(e);
                collectExpr(*binary.lhs, prefix);
                collectExpr(*binary.rhs, prefix);
                break;
            }
            case ExprKind::Chain:
                for (auto& operand : static_cast<ChainExpr&>(e).operands) collectExpr(*operand, prefix);
                break;
            case ExprKind::Call:
                for (auto& arg : static_cast<CallExpr&>(e).args) collectExpr(*arg, prefix);
                break;
            case ExprKind::Pipe: {
                auto& pipe = static_cast<PipeExpr&>(e);
                collectExpr(*pipe.lhs, prefix);
                collectExpr(*pipe.rhs, prefix);
                break;
            }
            default:
                break;
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

    // the source text of a span, which an assertion's trap message is
    std::string sourceText(Span span) {
        return std::string(manager.text().substr(span.start, span.length()));
    }

    // the source text of a span, quoted, for trap messages like 'x * y' overflowed i8
    std::string quoted(Span span) {
        return "'" + sourceText(span) + "'";
    }

    // emits + - or * on a trapping integer through an overflow intrinsic, which gives the result and whether it overflowed, and traps when it did
    llvm::Value* checked(TokenKind op, llvm::Value* lhs, llvm::Value* rhs, IntSemaType& type, Span span) {
        llvm::Intrinsic::ID id;
        switch (op) {
            case TokenKind::Plus: id = type.isSigned ? llvm::Intrinsic::sadd_with_overflow : llvm::Intrinsic::uadd_with_overflow; break;
            case TokenKind::Minus: id = type.isSigned ? llvm::Intrinsic::ssub_with_overflow : llvm::Intrinsic::usub_with_overflow; break;
            case TokenKind::Star: id = type.isSigned ? llvm::Intrinsic::smul_with_overflow : llvm::Intrinsic::umul_with_overflow; break;
            default: llvm_unreachable("not a checked operator");
        }
        llvm::Function* intrinsic = llvm::Intrinsic::getOrInsertDeclaration(module.get(), id, {lhs->getType()});
        llvm::Value* pair = irbuilder.CreateCall(intrinsic, {lhs, rhs});
        trapIf(irbuilder.CreateExtractValue(pair, 1), quoted(span) + " overflowed " + type.toString(), span);
        return irbuilder.CreateExtractValue(pair, 0);
    }

    // emits / or % on integers: a zero divisor always traps, and the one signed case that overflows, the minimum divided by -1, traps or wraps with the type
    llvm::Value* divide(TokenKind op, llvm::Value* lhs, llvm::Value* rhs, IntSemaType& type, Span span) {
        llvm::Type* llvmType = lhs->getType();
        trapIf(irbuilder.CreateICmpEQ(rhs, llvm::ConstantInt::get(llvmType, 0)), quoted(span) + " divided by zero", span);
        if (type.isSigned) {
            llvm::Value* minimum = llvm::ConstantInt::get(llvmType, llvm::APInt::getSignedMinValue(type.bits));
            llvm::Value* overflows = irbuilder.CreateAnd(irbuilder.CreateICmpEQ(lhs, minimum), irbuilder.CreateICmpEQ(rhs, llvm::ConstantInt::getSigned(llvmType, -1)));
            // a wrapping type divides by 1 instead, which gives exactly the wrapped answers: the minimum for / and 0 for %
            if (type.isWrapping) {
                rhs = irbuilder.CreateSelect(overflows, llvm::ConstantInt::get(llvmType, 1), rhs);
            } else {
                trapIf(overflows, quoted(span) + " overflowed " + type.toString(), span);
            }
        }
        if (op == TokenKind::Slash) return type.isSigned ? irbuilder.CreateSDiv(lhs, rhs) : irbuilder.CreateUDiv(lhs, rhs);
        return type.isSigned ? irbuilder.CreateSRem(lhs, rhs) : irbuilder.CreateURem(lhs, rhs);
    }

    // emits << or >>, where a trapping type traps on an amount outside 0 to bits - 1, a wrapping type takes the amount modulo the width, and >> keeps the sign only for signed types
    llvm::Value* shift(TokenKind op, llvm::Value* lhs, llvm::Value* rhs, IntSemaType& type, Span span) {
        llvm::Type* llvmType = lhs->getType();
        if (type.isWrapping) {
            rhs = irbuilder.CreateAnd(rhs, llvm::ConstantInt::get(llvmType, type.bits - 1));
        } else {
            // comparing as unsigned catches a negative amount too, since it reads as enormous
            llvm::Value* outOfRange = irbuilder.CreateICmpUGE(rhs, llvm::ConstantInt::get(llvmType, type.bits));
            trapIf(outOfRange, quoted(span) + " shifted by an amount outside 0 to " + std::to_string(type.bits - 1), span);
        }
        if (op == TokenKind::Shl) return irbuilder.CreateShl(lhs, rhs);
        return type.isSigned ? irbuilder.CreateAShr(lhs, rhs) : irbuilder.CreateLShr(lhs, rhs);
    }

    // emits one arithmetic or bitwise operator on integers, where a trapping type's + - and * are checked and a wrapping type's are plain two's complement
    llvm::Value* intArithmetic(TokenKind op, llvm::Value* lhs, llvm::Value* rhs, IntSemaType& type, Span span) {
        bool isChecked = op == TokenKind::Plus || op == TokenKind::Minus || op == TokenKind::Star;
        if (isChecked && !type.isWrapping) return checked(op, lhs, rhs, type, span);
        switch (op) {
            case TokenKind::Plus:
                return irbuilder.CreateAdd(lhs, rhs);
            case TokenKind::Minus:
                return irbuilder.CreateSub(lhs, rhs);
            case TokenKind::Star:
                return irbuilder.CreateMul(lhs, rhs);
            case TokenKind::Slash:
            case TokenKind::Percent:
                return divide(op, lhs, rhs, type, span);
            case TokenKind::Amp:
                return irbuilder.CreateAnd(lhs, rhs);
            case TokenKind::Bar:
                return irbuilder.CreateOr(lhs, rhs);
            case TokenKind::Caret:
                return irbuilder.CreateXor(lhs, rhs);
            case TokenKind::Shl:
            case TokenKind::Shr:
                return shift(op, lhs, rhs, type, span);
            default:
                llvm_unreachable("not an integer operator");
        }
    }

    // emits a comparison on integers, signed or unsigned by the type, where = and == are the same, since an integer's bits are its value
    llvm::Value* intCompare(TokenKind op, llvm::Value* lhs, llvm::Value* rhs, IntSemaType& type) {
        switch (op) {
            case TokenKind::Lt:
                return irbuilder.CreateICmp(type.isSigned ? llvm::CmpInst::ICMP_SLT : llvm::CmpInst::ICMP_ULT, lhs, rhs);
            case TokenKind::Le:
                return irbuilder.CreateICmp(type.isSigned ? llvm::CmpInst::ICMP_SLE : llvm::CmpInst::ICMP_ULE, lhs, rhs);
            case TokenKind::Gt:
                return irbuilder.CreateICmp(type.isSigned ? llvm::CmpInst::ICMP_SGT : llvm::CmpInst::ICMP_UGT, lhs, rhs);
            case TokenKind::Ge:
                return irbuilder.CreateICmp(type.isSigned ? llvm::CmpInst::ICMP_SGE : llvm::CmpInst::ICMP_UGE, lhs, rhs);
            case TokenKind::Eq:
            case TokenKind::EqEq:
                return irbuilder.CreateICmpEQ(lhs, rhs);
            case TokenKind::Ne:
            case TokenKind::NeEq:
                return irbuilder.CreateICmpNE(lhs, rhs);
            default:
                llvm_unreachable("not a comparison");
        }
    }

    // converts an integer to another integer type: a wrapping target keeps the low bits, and a trapping one checks the value survived and traps if it didn't
    llvm::Value* intToInt(llvm::Value* value, IntSemaType& from, IntSemaType& to, Span span) {
        // extends with copies of the sign bit or with zeros, by whether the value being resized is signed, or truncates
        auto resize = [&](llvm::Value* bits, IntSemaType& type, llvm::Type* llvmType) {
            return type.isSigned ? irbuilder.CreateSExtOrTrunc(bits, llvmType) : irbuilder.CreateZExtOrTrunc(bits, llvmType);
        };
        llvm::Value* converted = resize(value, from, lower(&to));
        if (to.isWrapping) return converted;
        // converting back has to give the original value, which catches narrowing that lost bits
        llvm::Value* fits = irbuilder.CreateICmpEQ(resize(converted, to, value->getType()), value);
        // and the sign has to survive: -1 as an i8 and 255 as an i8u have the same bits, so a value that changes signedness also has to be at least zero on the signed side
        if (from.isSigned != to.isSigned) {
            llvm::Value* signedSide = from.isSigned ? value : converted;
            fits = irbuilder.CreateAnd(fits, irbuilder.CreateICmpSGE(signedSide, llvm::ConstantInt::get(signedSide->getType(), 0)));
        }
        trapIf(irbuilder.CreateNot(fits), quoted(span) + " doesn't fit in " + to.toString(), span);
        return converted;
    }

    // checks whether a float is a NaN, which is the one value that isn't ordered with itself
    llvm::Value* isNaN(llvm::Value* value) {
        return irbuilder.CreateFCmpUNO(value, value);
    }

    // emits + - * / or % on floats, where a NaN-free type checks every result for a NaN, which traps
    llvm::Value* floatArithmetic(TokenKind op, llvm::Value* lhs, llvm::Value* rhs, FloatSemaType& type, Span span) {
        llvm::Value* result;
        switch (op) {
            case TokenKind::Plus: result = irbuilder.CreateFAdd(lhs, rhs); break;
            case TokenKind::Minus: result = irbuilder.CreateFSub(lhs, rhs); break;
            case TokenKind::Star: result = irbuilder.CreateFMul(lhs, rhs); break;
            case TokenKind::Slash: result = irbuilder.CreateFDiv(lhs, rhs); break;
            case TokenKind::Percent: result = irbuilder.CreateFRem(lhs, rhs); break;
            default: llvm_unreachable("not a float operator");
        }
        if (type.isNanFree) trapIf(isNaN(result), quoted(span) + " made a NaN, which " + type.toString() + " can't hold", span);
        return result;
    }

    // emits a comparison on floats: = and != compare bits, so -0.0 and 0.0 differ and a NaN can equal itself, a NaN-free type uses IEEE's comparisons, since it never holds a NaN, and a NaN-carrying one uses the total order, where NaN is below everything and equal to itself
    llvm::Value* floatCompare(TokenKind op, llvm::Value* lhs, llvm::Value* rhs, FloatSemaType& type) {
        if (op == TokenKind::Eq || op == TokenKind::Ne) {
            llvm::Type* bitsType = irbuilder.getIntNTy(lhs->getType()->getPrimitiveSizeInBits());
            llvm::Value* lhsBits = irbuilder.CreateBitCast(lhs, bitsType);
            llvm::Value* rhsBits = irbuilder.CreateBitCast(rhs, bitsType);
            return op == TokenKind::Eq ? irbuilder.CreateICmpEQ(lhsBits, rhsBits) : irbuilder.CreateICmpNE(lhsBits, rhsBits);
        }
        if (type.isNanFree) {
            switch (op) {
                case TokenKind::Lt: return irbuilder.CreateFCmpOLT(lhs, rhs);
                case TokenKind::Le: return irbuilder.CreateFCmpOLE(lhs, rhs);
                case TokenKind::Gt: return irbuilder.CreateFCmpOGT(lhs, rhs);
                case TokenKind::Ge: return irbuilder.CreateFCmpOGE(lhs, rhs);
                case TokenKind::EqEq: return irbuilder.CreateFCmpOEQ(lhs, rhs);
                case TokenKind::NeEq: return irbuilder.CreateFCmpONE(lhs, rhs);
                default: llvm_unreachable("not a comparison");
            }
        }
        // the total order: a < b when a is NaN and b isn't, or both are numbers and a is less, a <= b when a is NaN, or b isn't and a is at most b, and a == b when both are NaN or both are the same number
        auto less = [&](llvm::Value* a, llvm::Value* b) {
            return irbuilder.CreateOr(irbuilder.CreateAnd(isNaN(a), irbuilder.CreateNot(isNaN(b))), irbuilder.CreateFCmpOLT(a, b));
        };
        auto atMost = [&](llvm::Value* a, llvm::Value* b) {
            return irbuilder.CreateOr(isNaN(a), irbuilder.CreateAnd(irbuilder.CreateNot(isNaN(b)), irbuilder.CreateFCmpOLE(a, b)));
        };
        auto equal = [&](llvm::Value* a, llvm::Value* b) {
            return irbuilder.CreateOr(irbuilder.CreateAnd(isNaN(a), isNaN(b)), irbuilder.CreateFCmpOEQ(a, b));
        };
        switch (op) {
            case TokenKind::Lt: return less(lhs, rhs);
            case TokenKind::Le: return atMost(lhs, rhs);
            case TokenKind::Gt: return less(rhs, lhs);
            case TokenKind::Ge: return atMost(rhs, lhs);
            case TokenKind::EqEq: return equal(lhs, rhs);
            case TokenKind::NeEq: return irbuilder.CreateNot(equal(lhs, rhs));
            default: llvm_unreachable("not a comparison");
        }
    }

    // converts an integer to a float, rounding to nearest even, which can't fail, since a value past the float's range rounds to infinity, which every float type holds
    llvm::Value* intToFloat(llvm::Value* value, IntSemaType& from, FloatSemaType& to) {
        return from.isSigned ? irbuilder.CreateSIToFP(value, lower(&to)) : irbuilder.CreateUIToFP(value, lower(&to));
    }

    // converts a float to an integer, rounding toward zero: a wrapping target saturates, so a value past either end becomes that end and a NaN becomes 0, and a trapping one traps on a NaN or a value past either end
    llvm::Value* floatToInt(llvm::Value* value, IntSemaType& to, Span span) {
        llvm::Type* llvmType = lower(&to);
        if (to.isWrapping) {
            llvm::Intrinsic::ID id = to.isSigned ? llvm::Intrinsic::fptosi_sat : llvm::Intrinsic::fptoui_sat;
            llvm::Function* intrinsic = llvm::Intrinsic::getOrInsertDeclaration(module.get(), id, {llvmType, value->getType()});
            return irbuilder.CreateCall(intrinsic, {value});
        }
        // the range a value has to be in to truncate into the target: at least the minimum and below 2 to the bits, or to bits - 1 when signed, where an unsigned target takes anything above -1, since -0.5 truncates to 0, and every bound is a power of two, which a float holds exactly unless it's past the float's range, like -2 to the 31 in f16, where it becomes infinity
        const llvm::fltSemantics& floatSemantics = value->getType()->getFltSemantics();
        llvm::APFloat low = llvm::APFloat::getOne(floatSemantics, true);
        if (to.isSigned) low.convertFromAPInt(llvm::APInt::getSignedMinValue(to.bits), true, llvm::APFloat::rmNearestTiesToEven);
        llvm::APFloat high = llvm::scalbn(llvm::APFloat::getOne(floatSemantics), to.isSigned ? to.bits - 1 : to.bits, llvm::APFloat::rmNearestTiesToEven);
        // a NaN fails both ordered comparisons, so it's out of range too
        llvm::Value* lowValue = llvm::ConstantFP::get(context, low);
        // above the minimum is strict when the minimum is -1 or when it became -infinity, so -infinity itself is out of range
        llvm::Value* aboveLow = to.isSigned && low.isFinite() ? irbuilder.CreateFCmpOGE(value, lowValue) : irbuilder.CreateFCmpOGT(value, lowValue);
        llvm::Value* inRange = irbuilder.CreateAnd(aboveLow, irbuilder.CreateFCmpOLT(value, llvm::ConstantFP::get(context, high)));
        trapIf(irbuilder.CreateNot(inRange), quoted(span) + " doesn't fit in " + to.toString(), span);
        return to.isSigned ? irbuilder.CreateFPToSI(value, llvmType) : irbuilder.CreateFPToUI(value, llvmType);
    }

    // converts a float to another float type, rounding to nearest even, where a NaN-carrying float becoming a NaN-free one is the one that can fail, since the NaN has nowhere to go
    llvm::Value* floatToFloat(llvm::Value* value, FloatSemaType& from, FloatSemaType& to, Span span) {
        if (!from.isNanFree && to.isNanFree) trapIf(isNaN(value), quoted(span) + " is a NaN, which " + to.toString() + " can't hold", span);
        llvm::Type* llvmType = lower(&to);
        if (value->getType() == llvmType) return value;
        unsigned fromBits = value->getType()->getPrimitiveSizeInBits();
        unsigned toBits = llvmType->getPrimitiveSizeInBits();
        // half and bfloat16 are both 16 bits with different layouts, so one becomes the other through f32, which holds either exactly
        if (fromBits == toBits) return irbuilder.CreateFPTrunc(irbuilder.CreateFPExt(value, irbuilder.getFloatTy()), llvmType);
        return fromBits < toBits ? irbuilder.CreateFPExt(value, llvmType) : irbuilder.CreateFPTrunc(value, llvmType);
    }

    // emits one built-in arithmetic or bitwise operator on two values of the same type, which compound assignment shares through the base of X<-
    llvm::Value* arithmetic(TokenKind op, llvm::Value* lhs, llvm::Value* rhs, SemaType* type, Span span) {
        if (FloatSemaType* floating = asFloat(type)) return floatArithmetic(op, lhs, rhs, *floating, span);
        return intArithmetic(op, lhs, rhs, *asInt(type), span);
    }

    // emits one built-in comparison on two values of the same type, where a bool, the one other type that's compared, only has equality, and its bits are its value
    llvm::Value* compare(TokenKind op, llvm::Value* lhs, llvm::Value* rhs, SemaType* type) {
        if (FloatSemaType* floating = asFloat(type)) return floatCompare(op, lhs, rhs, *floating);
        if (IntSemaType* integer = asInt(type)) return intCompare(op, lhs, rhs, *integer);
        return op == TokenKind::Eq || op == TokenKind::EqEq ? irbuilder.CreateICmpEQ(lhs, rhs) : irbuilder.CreateICmpNE(lhs, rhs);
    }

    // negates a number: a float flips its sign bit, which never makes a NaN, a wrapping integer wraps, and a trapping one is 0 - x through the checked subtraction, since only the minimum overflows
    llvm::Value* negate(llvm::Value* value, SemaType* type, Span span) {
        if (asFloat(type)) return irbuilder.CreateFNeg(value);
        IntSemaType& integer = *asInt(type);
        if (integer.isWrapping) return irbuilder.CreateNeg(value);
        return checked(TokenKind::Minus, llvm::ConstantInt::get(value->getType(), 0), value, integer, span);
    }

    // converts between any two numbers, by whether each side is an integer or a float
    llvm::Value* convert(llvm::Value* value, SemaType* from, SemaType* to, Span span) {
        IntSemaType* fromInt = asInt(from);
        IntSemaType* toInt = asInt(to);
        if (fromInt && toInt) return intToInt(value, *fromInt, *toInt, span);
        if (fromInt) return intToFloat(value, *fromInt, *asFloat(to));
        if (toInt) return floatToInt(value, *toInt, span);
        return floatToFloat(value, *asFloat(from), *asFloat(to), span);
    }

    // emits an integer literal as a constant of its type, which sema made sure it fits in, or as a float when it stands for one, which sema made sure it's exact in
    llvm::Value* emitIntLiteral(IntLiteralExpr& literal) {
        auto [radix, digits] = literalDigits(literal.literal);
        if (FloatSemaType* floating = asFloat(literal.type)) {
            llvm::APFloat value(semantics(floating->format));
            // the value is exact, so converting it can't round
            (void)value.convertFromAPInt(llvm::APInt(128, digits, radix), false, llvm::APFloat::rmNearestTiesToEven);
            return llvm::ConstantFP::get(context, value);
        }
        return irbuilder.getInt(llvm::APInt(asInt(literal.type)->bits, digits, radix));
    }

    // emits a float literal as a constant in its type's encoding, rounding to nearest even, where a hex, octal, or binary literal is first turned into a hex float with an exponent, which APFloat reads exactly: 0o7.4 is 0x3cp-3
    llvm::Value* emitFloatLiteral(FloatLiteralExpr& literal) {
        auto [radix, digits] = literalDigits(literal.literal);
        std::string text = digits;
        if (radix != 10) {
            // every digit is a whole number of bits, so the digits read as one integer and the point moves to an exponent: each digit after it is worth bitsPerDigit fewer bits
            unsigned bitsPerDigit = radix == 16 ? 4 : radix == 8 ? 3 : 1;
            size_t point = digits.find('.');
            std::string whole = digits.substr(0, point) + digits.substr(point + 1);
            int exponent = -static_cast<int>((digits.size() - point - 1) * bitsPerDigit);
            llvm::APInt mantissa(whole.size() * bitsPerDigit, whole, radix);
            text = "0x" + llvm::toString(mantissa, 16, false) + "p" + std::to_string(exponent);
        }
        llvm::APFloat value(semantics(asFloat(literal.type)->format));
        // sema made sure the text is a valid literal in range, so APFloat failing to read it is a codegen bug
        llvm::Expected<llvm::APFloat::opStatus> status = value.convertFromString(text, llvm::APFloat::rmNearestTiesToEven);
        if (!status) {
            llvm::consumeError(status.takeError());
            llvm_unreachable("sema accepted a float literal APFloat can't read");
        }
        return llvm::ConstantFP::get(context, value);
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

    // emits a call of one value, which is what both pipes are, where a type in the function's place converts the value instead: x |> i8 is i8(x)
    llvm::Value* emitPipe(Expr& callee, Expr& arg, Span span) {
        llvm::Value* value = emitExpr(arg);
        if (callee.kind == ExprKind::TypeExpr) return convert(value, arg.type, callee.type, span);
        return emitCall(functionOf(static_cast<IdentifierExpr&>(callee).decl), {value});
    }

    // calls a function, where a void function's call has no value
    llvm::Value* emitCall(llvm::Function* f, std::vector<llvm::Value*> args) {
        llvm::Value* result = irbuilder.CreateCall(f, args);
        return f->getReturnType()->isVoidTy() ? nullptr : result;
    }

    // emits an expression into the current block and returns its value, or null for void and never, which have none
    llvm::Value* emitExpr(Expr& e) {
        switch (e.kind) {
            case ExprKind::IntLiteral:
                return emitIntLiteral(static_cast<IntLiteralExpr&>(e));
            case ExprKind::FloatLiteral:
                return emitFloatLiteral(static_cast<FloatLiteralExpr&>(e));
            case ExprKind::BoolLiteral:
                return irbuilder.getInt1(static_cast<BoolLiteralExpr&>(e).literal);
            // prefix operators, where ! and ~ are both a bitwise not, which on an i1 is logical not
            case ExprKind::Unary: {
                auto& unary = static_cast<UnaryExpr&>(e);
                llvm::Value* operand = emitExpr(*unary.operand);
                switch (unary.op.kind) {
                    case TokenKind::Minus:
                        // a negated integer literal is a constant sema already checked, like -128 as an i8, whose 128 would overflow if it were negated at run time, unless it stands for a float, which negates like any float
                        if (unary.operand->kind == ExprKind::IntLiteral && asInt(unary.operand->type)) return irbuilder.CreateNeg(operand);
                        return negate(operand, unary.operand->type, e.span);
                    case TokenKind::Tilde:
                    case TokenKind::Bang:
                        return irbuilder.CreateNot(operand);
                    default:
                        llvm_unreachable("sema rejected every other unary operator");
                }
            }
            // binary operators, where both sides have the same type, so the left one decides which instructions to use
            case ExprKind::Binary: {
                auto& binary = static_cast<BinaryExpr&>(e);
                if (binary.op.kind == TokenKind::AmpAmp || binary.op.kind == TokenKind::BarBar) return emitShortCircuit(binary);
                llvm::Value* lhs = emitExpr(*binary.lhs);
                llvm::Value* rhs = emitExpr(*binary.rhs);
                switch (binary.op.kind) {
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
                        return arithmetic(binary.op.kind, lhs, rhs, binary.lhs->type, e.span);
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
                        return compare(binary.op.kind, lhs, rhs, binary.lhs->type);
                    default:
                        llvm_unreachable("sema rejected every other binary operator");
                }
            }
            // a < b <= c is a < b and b <= c, where every operand is emitted once, so unlike &&, nothing is skipped
            case ExprKind::Chain: {
                auto& chain = static_cast<ChainExpr&>(e);
                std::vector<llvm::Value*> operands;
                for (auto& operand : chain.operands) operands.push_back(emitExpr(*operand));
                llvm::Value* result = nullptr;
                for (size_t i = 0; i < chain.ops.size(); i++) {
                    llvm::Value* pair = compare(chain.ops[i].kind, operands[i], operands[i + 1], chain.operands[0]->type);
                    result = result ? irbuilder.CreateAnd(result, pair) : pair;
                }
                return result;
            }
            // a name reads its stack slot or its global
            case ExprKind::Identifier: {
                Symbol* symbol = static_cast<IdentifierExpr&>(e).decl;
                return irbuilder.CreateLoad(lower(symbol->type), addressOf(symbol));
            }
            // a call's callee is always a function's name, so it's looked up instead of emitted, and a type called on a value converts it
            case ExprKind::Call: {
                auto& call = static_cast<CallExpr&>(e);
                if (call.callee->kind == ExprKind::TypeExpr) return emitPipe(*call.callee, *call.args[0], e.span);
                std::vector<llvm::Value*> args;
                for (auto& arg : call.args) args.push_back(emitExpr(*arg));
                return emitCall(functionOf(static_cast<IdentifierExpr&>(*call.callee).decl), args);
            }
            // x |> f and f <| x are both f(x)
            case ExprKind::Pipe: {
                auto& pipe = static_cast<PipeExpr&>(e);
                bool isForward = pipe.op.kind == TokenKind::PipeFwd;
                return emitPipe(isForward ? *pipe.rhs : *pipe.lhs, isForward ? *pipe.lhs : *pipe.rhs, e.span);
            }
            case ExprKind::Block: {
                auto& block = static_cast<BlockExpr&>(e);
                return emitScope(block.stmts, TargetKind::Block, block.name, block.type);
            }
            // a chunk runs as a block, since it only differs once it has an allocator
            case ExprKind::Chunk: {
                auto& chunk = static_cast<ChunkExpr&>(e);
                return emitScope(chunk.body->stmts, TargetKind::Chunk, chunk.name, chunk.type);
            }
            case ExprKind::Match:
                return emitMatch(static_cast<MatchExpr&>(e));
            case ExprKind::Repeat:
                return emitRepeat(static_cast<RepeatExpr&>(e));
            default:
                llvm_unreachable("sema rejected every other expression");
        }
    }

    // one entry per scope a yield could target, mirroring sema's yield-target stack: where the scope continues, and where its value goes
    struct Exit {
        TargetKind kind;
        std::optional<std::string_view> label;
        llvm::BasicBlock* end;
        llvm::AllocaInst* result;
    };
    std::vector<Exit> exits;

    // the label a scope can be yielded to by, which is its name if it has one
    std::optional<std::string_view> labelOf(const std::optional<Token>& name) {
        if (name) return name->text;
        return std::nullopt;
    }

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

    // makes a stack slot at the top of the current function, so a slot made in a loop is one slot instead of one per iteration, and mem2reg, which only promotes those, can turn it into registers
    llvm::AllocaInst* makeAlloca(llvm::Type* type, std::string_view name) {
        llvm::Function* f = irbuilder.GetInsertBlock()->getParent();
        llvm::IRBuilder<> entry(&f->getEntryBlock(), f->getEntryBlock().begin());
        return entry.CreateAlloca(type, nullptr, name);
    }

    // the stack slot of every local and parameter
    std::unordered_map<Symbol*, llvm::AllocaInst*> locals;

    // makes a symbol's stack slot, named after it
    llvm::AllocaInst* makeSlot(Symbol* symbol) {
        return locals[symbol] = makeAlloca(lower(symbol->type), symbol->name);
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
        switch (s.kind) {
            // a yield to the function returns, and a yield to anything else stores its value in the target's result and jumps to its end
            case StmtKind::Yield: {
                auto& yield = static_cast<YieldStmt&>(s);
                llvm::Value* value = yield.value ? emitExpr(*yield.value) : nullptr;
                // the value itself can end in a yield elsewhere, like a match whose arms all leave, and then there's nothing left to do
                if (irbuilder.GetInsertBlock()->getTerminator()) break;
                Exit& exit = resolveExit(yield);
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
                auto& let = static_cast<LetStmt&>(s);
                // let _ <- init binds nothing, but init still has to run
                if (let.pattern->kind != PatternKind::Binder) {
                    emitExpr(*let.init);
                    break;
                }
                Symbol* symbol = static_cast<BinderPattern&>(*let.pattern).decl;
                // a binding outside every function is a global, and an immutable one whose initializer folds to a constant needs no code at all, while anything else is stored each time its let runs, which a top-level loop can make more than once
                if (symbol->owner == 0) {
                    llvm::GlobalVariable* global = makeGlobal(symbol);
                    if (let.init->kind == ExprKind::Wildcard) break;
                    llvm::Value* value = emitExpr(*let.init);
                    auto* constant = llvm::dyn_cast<llvm::Constant>(value);
                    if (constant && !symbol->isMutable) {
                        global->setInitializer(constant);
                        global->setConstant(true);
                    } else {
                        irbuilder.CreateStore(value, global);
                    }
                    break;
                }
                llvm::AllocaInst* slot = makeSlot(symbol);
                if (let.init->kind != ExprKind::Wildcard) irbuilder.CreateStore(emitExpr(*let.init), slot);
                break;
            }
            // _ <- value discards, place <- value stores, and place op<- value is place <- place op value
            case StmtKind::Assign: {
                auto& assign = static_cast<AssignStmt&>(s);
                llvm::Value* value = emitExpr(*assign.value);
                if (assign.place->kind == ExprKind::Wildcard) break;
                Symbol* place = static_cast<IdentifierExpr&>(*assign.place).decl;
                llvm::Value* address = addressOf(place);
                if (assign.op) {
                    llvm::Value* current = irbuilder.CreateLoad(lower(place->type), address);
                    value = arithmetic(assign.op->op->base->kind, current, value, assign.place->type, Span{assign.place->span.start, assign.value->span.end});
                }
                irbuilder.CreateStore(value, address);
                break;
            }
            // an expression statement is only there for what it does, since sema made every value be used or discarded
            case StmtKind::Expr:
                emitExpr(*static_cast<ExprStmt&>(s).expr);
                break;
            // nested functions are defined on their own once every signature exists, so there's nothing to emit where they're written
            case StmtKind::Decl:
                break;
            // an attribute's target is an ordinary statement
            case StmtKind::Attr: {
                auto& attr = static_cast<AttrStmt&>(s);
                if (attr.target) emitStmt(*attr.target);
                break;
            }
            // wit obligations are for the verifier, and never run
            case StmtKind::Wit:
                break;
            // assert (cond) checks cond at run time, and traps with the assertion's line and source text when it's false
            case StmtKind::Assert: {
                llvm::Value* cond = emitExpr(*static_cast<AssertStmt&>(s).cond);
                trapIf(irbuilder.CreateNot(cond), sourceText(s.span), s.span);
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

    // emits a match as a chain of tests where each arm checks its pattern and either runs its body or falls to the next arm's test, and every arm shares the match's end and result so a bare yield and -| match land in the same spot
    llvm::Value* emitMatch(MatchExpr& match) {
        llvm::Function* f = irbuilder.GetInsertBlock()->getParent();
        llvm::Value* subject = match.subject ? emitExpr(*match.subject) : nullptr;
        // a literal pattern compares deeply by default, so floats compare by value, or shallowly with match (=, x), so floats compare bit for bit
        TokenKind equality = match.equality && match.equality->kind == TokenKind::Eq ? TokenKind::Eq : TokenKind::EqEq;
        llvm::BasicBlock* end = llvm::BasicBlock::Create(context, "match.end", f);
        llvm::AllocaInst* result = carriesValue(match.type) ? makeAlloca(lower(match.type), "result") : nullptr;
        exits.push_back(Exit{TargetKind::Match, labelOf(match.name), end, result});
        for (auto& arm : match.arms) {
            llvm::BasicBlock* body = llvm::BasicBlock::Create(context, "match.arm", f);
            llvm::BasicBlock* next = llvm::BasicBlock::Create(context, "match.next", f);
            // the test: catch-alls always run their body, literals compare, and guards are their own condition
            switch (arm.pattern->kind) {
                case PatternKind::Wildcard:
                case PatternKind::Binder:
                    irbuilder.CreateBr(body);
                    break;
                case PatternKind::Literal: {
                    Expr& literal = *static_cast<LiteralPattern&>(*arm.pattern).literal;
                    llvm::Value* matched = compare(equality, subject, emitExpr(literal), literal.type);
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
            if (arm.pattern->kind == PatternKind::Binder) irbuilder.CreateStore(subject, makeSlot(static_cast<BinderPattern&>(*arm.pattern).decl));
            exits.push_back(Exit{TargetKind::Arm, std::nullopt, end, result});
            emitStmts(arm.body->stmts);
            exits.pop_back();
            if (!irbuilder.GetInsertBlock()->getTerminator()) irbuilder.CreateBr(end);
            // the next arm's test goes where a failed test lands
            irbuilder.SetInsertPoint(next);
        }
        exits.pop_back();
        // past the last arm, no arm matched: sema made every match with a value cover everything, so only a void match can get here
        if (carriesValue(match.type)) {
            irbuilder.CreateUnreachable();
        } else {
            irbuilder.CreateBr(end);
        }
        irbuilder.SetInsertPoint(end);
        return result ? irbuilder.CreateLoad(result->getAllocatedType(), result) : nullptr;
    }

    // emits a loop as a header that decides whether to run the body again, where a while loop's header tests its condition and an infinite loop's header is its body
    llvm::Value* emitRepeat(RepeatExpr& repeat) {
        llvm::Function* f = irbuilder.GetInsertBlock()->getParent();
        llvm::BasicBlock* body = llvm::BasicBlock::Create(context, "repeat.body", f);
        llvm::BasicBlock* end = llvm::BasicBlock::Create(context, "repeat.end", f);
        llvm::BasicBlock* header = repeat.cond ? llvm::BasicBlock::Create(context, "repeat.header", f) : body;
        irbuilder.CreateBr(header);
        if (repeat.cond) {
            irbuilder.SetInsertPoint(header);
            irbuilder.CreateCondBr(emitExpr(*repeat.cond), body, end);
        }
        // only an infinite loop can yield a value, so only it needs somewhere to put one
        llvm::AllocaInst* result = carriesValue(repeat.type) ? makeAlloca(lower(repeat.type), "result") : nullptr;
        exits.push_back(Exit{TargetKind::Loop, labelOf(repeat.name), end, result});
        irbuilder.SetInsertPoint(body);
        emitStmts(repeat.body->stmts);
        exits.pop_back();
        if (!irbuilder.GetInsertBlock()->getTerminator()) irbuilder.CreateBr(header);
        irbuilder.SetInsertPoint(end);
        return result ? irbuilder.CreateLoad(result->getAllocatedType(), result) : nullptr;
    }

    // emits a block or a chunk as its own scope: yields to it store into result and jump to end, and its value is loaded there
    llvm::Value* emitScope(std::vector<std::unique_ptr<Stmt>>& stmts, TargetKind kind, std::optional<Token> name, SemaType* type) {
        llvm::Function* f = irbuilder.GetInsertBlock()->getParent();
        llvm::BasicBlock* end = llvm::BasicBlock::Create(context, kind == TargetKind::Chunk ? "chunk.end" : "block.end", f);
        llvm::AllocaInst* result = carriesValue(type) ? makeAlloca(lower(type), "result") : nullptr;
        exits.push_back(Exit{kind, labelOf(name), end, result});
        emitStmts(stmts);
        exits.pop_back();
        if (!irbuilder.GetInsertBlock()->getTerminator()) irbuilder.CreateBr(end);
        irbuilder.SetInsertPoint(end);
        return result ? irbuilder.CreateLoad(result->getAllocatedType(), result) : nullptr;
    }

    // emits a function's body: every parameter is copied into a slot so it acts like a binding, then the statements run in the function's own scope
    void define(FnDecl& fn) {
        llvm::Function* f = functionOf(fn.decl);
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
