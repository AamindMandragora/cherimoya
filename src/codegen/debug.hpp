#pragma once

#include <algorithm>
#include <filesystem>
#include <string>
#include <unordered_map>
#include <vector>
#include <llvm/BinaryFormat/Dwarf.h>
#include <llvm/IR/Constants.h>
#include <llvm/IR/DIBuilder.h>
#include <llvm/IR/Instructions.h>
#include <llvm/IR/Module.h>
#include <llvm/Transforms/Utils/ModuleUtils.h>
#include "printers.hpp"
#include "sema.hpp"

// how much debug info a build carries: none, the source line each instruction came from, or everything a debugger shows, which adds functions' types, variables, and scopes
enum class DebugLevel { None, Lines, Full };

// what debug info needs to know about a build: how much to write, the source file it describes, whether it's optimized, and its format, CodeView for Windows' debuggers or DWARF everywhere else, where only ELF carries the embedded gdb printers
struct DebugOptions {
    DebugLevel level = DebugLevel::None;
    std::string path;
    bool isOptimized = false;
    bool isCodeView = false;
    bool isELF = false;
};

// writes a module's debug info as codegen emits it: a subprogram for every function, a location for every instruction, and, with everything, a type for every value, a variable for every binding and input, and a lexical block for every scope
class DebugInfo {
    llvm::Module& module;
    llvm::DIBuilder builder;
    DebugOptions options;
    llvm::DIFile* file;
    llvm::DICompileUnit* unit;
    // the offset every line starts at, so a span's line and column come from a binary search instead of a count from the top
    std::string_view text;
    std::vector<size_t> lineStarts;
    // sema interns its types, so each one's debug type is made once and looked up by pointer after that
    std::unordered_map<SemaType*, llvm::DIType*> types;
    // the scope new locations and variables belong to: the function being emitted, then each scope inside it, innermost last
    std::vector<llvm::DIScope*> scopes;

    // the line and column of an offset, both counted from 1, where a column counts bytes
    std::pair<unsigned, unsigned> lineCol(size_t offset) const {
        size_t line = std::upper_bound(lineStarts.begin(), lineStarts.end(), offset) - lineStarts.begin() - 1;
        return {static_cast<unsigned>(line + 1), static_cast<unsigned>(offset - lineStarts[line] + 1)};
    }

    // a value's debug type: integers by their sign, arrays as their elements, bool and characters as themselves, a boolf as fixed-point with a scale of 1/255, so gdb prints 179 as 0.702, the floats a debugger decodes as floats, and FP8, bfloat16, and f128, which gdb would decode wrong, as their raw bits, which the embedded printers decode, where void and never have none
    llvm::DIType* typeOf(SemaType* type) {
        auto found = types.find(type);
        if (found != types.end()) return found->second;
        std::string name = type->toString();
        llvm::DIType* made = nullptr;
        if (auto integer = asInt(type)) {
            made = builder.createBasicType(name, integer->bits, integer->isSigned ? llvm::dwarf::DW_ATE_signed : llvm::dwarf::DW_ATE_unsigned);
        } else if (auto floating = asFloat(type)) {
            bool isDecoded = floating->format == FloatFormat::Half || floating->format == FloatFormat::Single || floating->format == FloatFormat::Double;
            unsigned bits = floating->format == FloatFormat::E4M3 || floating->format == FloatFormat::E5M2 ? 8
                : floating->format == FloatFormat::Half || floating->format == FloatFormat::BFloat ? 16
                : floating->format == FloatFormat::Single ? 32
                : floating->format == FloatFormat::Double ? 64 : 128;
            made = builder.createBasicType(name, bits, isDecoded ? llvm::dwarf::DW_ATE_float : llvm::dwarf::DW_ATE_unsigned);
        } else if (auto array = asArray(type)) {
            // a fixed array is its elements one after another, counted from 0
            llvm::DIType* element = typeOf(array->element);
            auto subscripts = builder.getOrCreateArray({builder.getOrCreateSubrange(0, static_cast<int64_t>(array->size))});
            made = builder.createArrayType(array->size * element->getSizeInBits(), 0, element, subscripts);
        } else if (type->kind == SemaTypeKind::Primitive) {
            switch (static_cast<PrimitiveSemaType*>(type)->primitive) {
                case PrimitiveKind::Bool:
                    made = builder.createBasicType(name, 8, llvm::dwarf::DW_ATE_boolean);
                    break;
                case PrimitiveKind::Char:
                    made = builder.createBasicType(name, 8, llvm::dwarf::DW_ATE_unsigned_char);
                    break;
                case PrimitiveKind::Rune:
                    made = builder.createBasicType(name, 32, llvm::dwarf::DW_ATE_UTF);
                    break;
                // CodeView has no fixed-point types, so there a boolf is its byte
                case PrimitiveKind::Boolf:
                    if (options.isCodeView) {
                        made = builder.createBasicType(name, 8, llvm::dwarf::DW_ATE_unsigned);
                    } else {
                        made = builder.createRationalFixedPointType(name, 8, 0, llvm::dwarf::DW_ATE_unsigned_fixed, llvm::DINode::FlagZero, llvm::APInt(32, 1), llvm::APInt(32, 255));
                    }
                    break;
                case PrimitiveKind::Void:
                case PrimitiveKind::Never:
                    break;
            }
        }
        return types[type] = made;
    }

    // a symbol's debug type, const unless it's mut, the way the language treats it
    llvm::DIType* typeOf(const Symbol& symbol) {
        llvm::DIType* type = typeOf(symbol.type);
        return symbol.isMutable() ? type : builder.createQualifiedType(llvm::dwarf::DW_TAG_const_type, type);
    }

    // a function's debug type, its result then its inputs, where a null result is void, and only everything has types, so a line table's functions all share the empty one
    llvm::DISubroutineType* typeOf(SemaType* returns, const std::vector<SemaType*>& params) {
        std::vector<llvm::Metadata*> elements;
        if (options.level == DebugLevel::Full) {
            elements.push_back(returns ? typeOf(returns) : nullptr);
            for (SemaType* param : params) elements.push_back(typeOf(param));
        }
        return builder.createSubroutineType(builder.getOrCreateTypeArray(elements));
    }

    // puts the gdb printers' source in the binary's .debug_gdb_scripts section, as an inline Python script, which gdb runs when it loads the binary from a path its auto-load safe-path allows, and llvm.used keeps the section from being dropped
    void embedPrinters() {
        std::string entry = "\x04" + std::string("cherimoya-printers\n") + gdbPrinters + '\0';
        auto* contents = llvm::ConstantDataArray::getString(module.getContext(), entry, false);
        auto* section = new llvm::GlobalVariable(module, contents->getType(), true, llvm::GlobalValue::InternalLinkage, contents, "cheri.gdb.printers");
        section->setSection(".debug_gdb_scripts");
        section->setAlignment(llvm::Align(1));
        llvm::appendToUsed(module, {section});
    }
public:
    DebugInfo(llvm::Module& m, std::string_view source, DebugOptions o) : module(m), builder(m), options(std::move(o)), text(source) {
        lineStarts.push_back(0);
        for (size_t i = 0; i < text.size(); i++) {
            if (text[i] == '\n') lineStarts.push_back(i + 1);
        }
        // the file is named as it was given, in the directory the build ran in, so a debugger finds the source either way
        file = builder.createFile(options.path, std::filesystem::current_path().string());
        // C is the language whose expressions gdb parses best, and cherimoya's simple ones read the same, and both levels are LLVM's full debug info, since its line tables alone leave out functions, which a C program's symbols name anyway but cherimoya's don't, so a line table here still names every function, and leaves out only types, variables, and scopes
        unit = builder.createCompileUnit(llvm::dwarf::DW_LANG_C11, file, "cheri", options.isOptimized, "", 0, "", llvm::DICompileUnit::FullDebug);
        module.addModuleFlag(llvm::Module::Warning, "Debug Info Version", llvm::DEBUG_METADATA_VERSION);
        if (options.isCodeView) {
            module.addModuleFlag(llvm::Module::Warning, "CodeView", 1);
        } else {
            module.addModuleFlag(llvm::Module::Warning, "Dwarf Version", 5);
        }
    }

    // a span's start as a location in the current scope
    llvm::DILocation* location(Span span) {
        auto [line, col] = lineCol(span.start);
        return llvm::DILocation::get(module.getContext(), line, col, scopes.back());
    }

    // starts a function's debug info: its subprogram, named as the source names it, outer.inner for a nested function, with the symbol as its linkage name, where an artificial one, like the function that runs the top level, is one nobody wrote
    void beginFunction(llvm::Function* f, const std::string& name, SemaType* returns, const std::vector<SemaType*>& params, Span span, bool isArtificial) {
        unsigned line = lineCol(span.start).first;
        auto flags = isArtificial ? llvm::DINode::FlagArtificial : llvm::DINode::FlagPrototyped;
        auto spFlags = llvm::DISubprogram::SPFlagDefinition | (options.isOptimized ? llvm::DISubprogram::SPFlagOptimized : llvm::DISubprogram::SPFlagZero);
        auto* subprogram = builder.createFunction(file, name, "", file, line, typeOf(returns, params), line, flags, spFlags);
        f->setSubprogram(subprogram);
        scopes = {subprogram};
    }

    // finishes a function's debug info, so nothing after it lands in its scope
    void endFunction() {
        builder.finalizeSubprogram(llvm::cast<llvm::DISubprogram>(scopes.front()));
        scopes.clear();
    }

    // opens a lexical block for a scope, which only everything has, so a debugger shows each scope's variables only inside it
    void enterScope(Span span) {
        if (options.level != DebugLevel::Full) return;
        auto [line, col] = lineCol(span.start);
        scopes.push_back(builder.createLexicalBlock(scopes.back(), file, line, col));
    }
    void leaveScope() {
        if (options.level == DebugLevel::Full) scopes.pop_back();
    }

    // describes a binding's or an input's stack slot, where input is the input's number from 1, or 0 for a binding, which goes in the current scope while an input goes in its function's
    void declareVariable(llvm::AllocaInst* slot, const Symbol& symbol, unsigned input, llvm::BasicBlock* block) {
        if (options.level != DebugLevel::Full) return;
        unsigned line = lineCol(symbol.span.start).first;
        llvm::DILocalVariable* variable = input
            ? builder.createParameterVariable(scopes.front(), symbol.name, input, file, line, typeOf(symbol), true)
            : builder.createAutoVariable(scopes.back(), symbol.name, file, line, typeOf(symbol), true);
        builder.insertDeclare(slot, variable, builder.createExpression(), location(symbol.span), block);
    }

    // describes a top-level binding's global, named as the source names it, with the symbol as its linkage name
    void declareGlobal(llvm::GlobalVariable* global, const Symbol& symbol) {
        if (options.level != DebugLevel::Full) return;
        unsigned line = lineCol(symbol.span.start).first;
        global->addDebugInfo(builder.createGlobalVariableExpression(unit, symbol.name, "", file, line, typeOf(symbol), true));
    }

    // resolves everything the builder left open, once every function is emitted, and embeds the printers where gdb can find them
    void finish() {
        if (options.level == DebugLevel::Full && options.isELF) embedPrinters();
        builder.finalize();
    }
};
