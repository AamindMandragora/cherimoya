#include <filesystem>
#include <iostream>
#include <memory>
#include <string_view>
#include <sys/wait.h>
#include <unistd.h>

#include "parser.hpp"
#include "codegen.hpp"

#include <llvm/IR/Verifier.h>
#include <llvm/IR/LegacyPassManager.h>
#include <llvm/Bitcode/BitcodeWriter.h>
#include <llvm/MC/TargetRegistry.h>
#include <llvm/Target/TargetMachine.h>
#include <llvm/Target/TargetOptions.h>
#include <llvm/TargetParser/Host.h>
#include <llvm/Support/FileSystem.h>
#include <llvm/Support/raw_ostream.h>
#include <llvm/Support/TargetSelect.h>
#include <llvm/Passes/PassBuilder.h>

// what a build produces: a linked executable, or the program at one of the stages before it
enum class Emit { Exe, Obj, Asm, LLVM, Bitcode };

// everything the command line can set, holding the default whenever an option isn't given
struct Options {
    std::string command;
    std::string input;
    std::string output;
    Emit emit = Emit::Exe;
    // the optimization level as written after -O: 0, 1, 2, 3, s, or z
    char level = '3';
    // the machine to build for, where an empty triple is the one cheri runs on and the cpu native is this machine's own
    std::string triple;
    std::string cpu = "generic";
    std::string features;
    // how to link: the program that links, whether the executable is static, and the arguments passed straight to it
    std::string linker = "cc";
    bool isStatic = false;
    std::vector<std::string> linkArgs;
};

// prints every command and option
static void usage(std::ostream& out) {
    out << "usage: cheri <command> <file.moya> [options]\n"
           "\n"
           "commands:\n"
           "  build                 check the program and build it, an executable by default\n"
           "  check                 check the program without building it\n"
           "  run                   build the program, run it, and exit with its exit code\n"
           "  help                  print this message\n"
           "\n"
           "output:\n"
           "  -o <path>             where the output goes, named after the file by default\n"
           "  --emit <kind>         what to produce: exe, obj, asm, llvm, or bc\n"
           "  -c                    the same as --emit obj\n"
           "  -S                    the same as --emit asm\n"
           "  --emit-llvm           the same as --emit llvm, printed unless -o is given\n"
           "\n"
           "optimization:\n"
           "  -O0 -O1 -O2 -O3       how hard to optimize, -O3 by default, where -O0 leaves the IR as codegen wrote it\n"
           "  -Os -Oz               optimize for size, or for size above everything else\n"
           "\n"
           "target:\n"
           "  --target <triple>     the machine to build for, this one by default\n"
           "  --cpu <name>          the cpu to tune for, generic by default, or native for this machine's\n"
           "  --features <list>     cpu features to turn on or off, like +avx2,-sse4.1\n"
           "\n"
           "linking:\n"
           "  --linker <program>    the program that links, cc by default\n"
           "  --static              link a static executable\n"
           "  -L<dir> -l<lib>       search a directory for libraries, and link one\n"
           "  --link-arg <arg>      pass an argument straight to the linker\n";
}

// reports a bad command line and points at the help
static int fail(const std::string& message) {
    std::cerr << "cheri: " << message << ", see 'cheri help'" << std::endl;
    return 1;
}

// reads the command line into options, where the command comes first and the file can go before, between, or after the options, returning false after reporting what was wrong with it
static bool parseOptions(int argc, char** argv, Options& options) {
    options.command = argv[1];
    if (options.command != "build" && options.command != "check" && options.command != "run") {
        fail("unknown command '" + options.command + "'");
        return false;
    }
    for (int i = 2; i < argc; i++) {
        std::string_view arg = argv[i];
        // takes the value after an option that needs one, or reports that it's missing
        auto value = [&](std::string& into) {
            if (i + 1 >= argc) {
                fail("'" + std::string(arg) + "' needs a value");
                return false;
            }
            into = argv[++i];
            return true;
        };
        if (arg == "-o") {
            if (!value(options.output)) return false;
        } else if (arg == "--emit") {
            std::string kind;
            if (!value(kind)) return false;
            if (kind == "exe") options.emit = Emit::Exe;
            else if (kind == "obj") options.emit = Emit::Obj;
            else if (kind == "asm") options.emit = Emit::Asm;
            else if (kind == "llvm") options.emit = Emit::LLVM;
            else if (kind == "bc") options.emit = Emit::Bitcode;
            else {
                fail("unknown kind to emit '" + kind + "'");
                return false;
            }
        } else if (arg == "-c") {
            options.emit = Emit::Obj;
        } else if (arg == "-S") {
            options.emit = Emit::Asm;
        } else if (arg == "--emit-llvm") {
            options.emit = Emit::LLVM;
        } else if (arg.size() == 3 && arg.substr(0, 2) == "-O" && std::string_view("0123sz").find(arg[2]) != std::string_view::npos) {
            options.level = arg[2];
        } else if (arg == "--target") {
            if (!value(options.triple)) return false;
        } else if (arg == "--cpu") {
            if (!value(options.cpu)) return false;
        } else if (arg == "--features") {
            if (!value(options.features)) return false;
        } else if (arg == "--linker") {
            if (!value(options.linker)) return false;
        } else if (arg == "--static") {
            options.isStatic = true;
        } else if (arg.size() > 2 && (arg.substr(0, 2) == "-L" || arg.substr(0, 2) == "-l")) {
            options.linkArgs.push_back(std::string(arg));
        } else if (arg == "--link-arg") {
            std::string link;
            if (!value(link)) return false;
            options.linkArgs.push_back(link);
        } else if (arg.size() > 1 && arg[0] == '-') {
            fail("unknown option '" + std::string(arg) + "'");
            return false;
        } else if (options.input.empty()) {
            options.input = arg;
        } else {
            fail("only one file can be built at a time, but got '" + options.input + "' and '" + std::string(arg) + "'");
            return false;
        }
    }
    if (options.input.empty()) {
        fail("'" + options.command + "' needs a file");
        return false;
    }
    // run builds a temporary executable and removes it once it's run, so there's nothing for -o or --emit to name
    if (options.command == "run" && (!options.output.empty() || options.emit != Emit::Exe)) {
        fail("'run' builds and runs a temporary executable, so it takes no '-o' or '--emit'");
        return false;
    }
    return true;
}

// quotes an argument for the shell, so a path with spaces or quotes in it stays one argument
static std::string quote(const std::string& arg) {
    std::string quoted = "'";
    for (char c : arg) quoted += c == '\'' ? std::string("'\\''") : std::string(1, c);
    return quoted + "'";
}

// opens a file to write an output to, or reports why it can't and gives null
static std::unique_ptr<llvm::raw_fd_ostream> openOutput(const std::string& path, llvm::sys::fs::OpenFlags flags) {
    std::error_code ec;
    auto out = std::make_unique<llvm::raw_fd_ostream>(path, ec, flags);
    if (!ec) return out;
    llvm::errs() << "cheri: can't write '" << path << "': " << ec.message() << '\n';
    return nullptr;
}

// runs LLVM's standard pipeline at the chosen level, which promotes every stack slot to registers and phis early on, where -O0 runs nothing
static void optimize(llvm::Module& module, llvm::TargetMachine* machine, char level) {
    if (level == '0') return;
    llvm::OptimizationLevel chosen = level == '1' ? llvm::OptimizationLevel::O1
        : level == '2' ? llvm::OptimizationLevel::O2
        : level == 's' ? llvm::OptimizationLevel::Os
        : level == 'z' ? llvm::OptimizationLevel::Oz
        : llvm::OptimizationLevel::O3;
    // each kind of pass has an analysis manager that caches what passes learn about the code, and the proxies let them share it
    llvm::LoopAnalysisManager loops;
    llvm::FunctionAnalysisManager functions;
    llvm::CGSCCAnalysisManager calls;
    llvm::ModuleAnalysisManager modules;
    llvm::PassBuilder builder(machine);
    builder.registerModuleAnalyses(modules);
    builder.registerCGSCCAnalyses(calls);
    builder.registerFunctionAnalyses(functions);
    builder.registerLoopAnalyses(loops);
    builder.crossRegisterProxies(loops, functions, calls, modules);
    llvm::ModulePassManager pipeline = builder.buildPerModuleDefaultPipeline(chosen);
    pipeline.run(module, modules);
}

// writes the module as machine code, an object file or assembly, returning false after reporting what went wrong
static bool writeMachineCode(llvm::Module& module, llvm::TargetMachine& machine, const std::string& path, llvm::CodeGenFileType type) {
    auto out = openOutput(path, type == llvm::CodeGenFileType::AssemblyFile ? llvm::sys::fs::OF_Text : llvm::sys::fs::OF_None);
    if (!out) return false;
    llvm::legacy::PassManager passes;
    if (machine.addPassesToEmitFile(passes, *out, nullptr, type)) {
        llvm::errs() << "cheri: the target can't emit this kind of file\n";
        return false;
    }
    passes.run(module);
    out->flush();
    return true;
}

// cheri <command> <file> checks a program and builds it: lex, parse, check, lower to LLVM, optimize, then write the stage that was asked for, or link an executable with the runtime
int main(int argc, char** argv) {
    if (argc >= 2) {
        std::string_view first = argv[1];
        if (first == "help" || first == "-h" || first == "--help") {
            usage(std::cout);
            return 0;
        }
    }
    if (argc < 2) {
        usage(std::cerr);
        return 1;
    }
    Options options;
    if (!parseOptions(argc, argv, options)) return 1;

    // loads the source, which every later stage points into
    SourceManager manager;
    manager.loadFile(options.input);
    if (manager.text() == "") {
        std::cerr << "cheri: '" << options.input << "' is empty or can't be read" << std::endl;
        return 1;
    }
    // runs the front end, and stops before codegen if anything reported an error, since codegen only handles checked programs
    DiagnosticEngine engine(manager);
    Lexer lexer(engine);
    auto tokens = lexer.lex();
    Parser parser(tokens, engine);
    auto program = parser.parseProgram();
    Sema sema(engine);
    sema.check(program, true);
    if (engine.hadErrors()) return 1;
    if (options.command == "check") return 0;

    // the module is named after the file, so prog.moya defines cheri.prog.main
    std::string stem = std::filesystem::path(options.input).stem().string();
    Codegen codegen(stem, manager);
    auto module = codegen.generate(program);
    // broken IR is a codegen bug, so it's printed along with what's wrong with it before anything else touches it
    if (llvm::verifyModule(*module, &llvm::errs())) {
        llvm::errs() << *module << '\n';
        return 1;
    }

    // sets up the machine to build for, which the optimizer and every output after it depend on, with every target available for cross-compiling
    llvm::InitializeAllTargetInfos();
    llvm::InitializeAllTargets();
    llvm::InitializeAllTargetMCs();
    llvm::InitializeAllAsmPrinters();
    llvm::Triple triple(options.triple.empty() ? llvm::sys::getDefaultTargetTriple() : options.triple);
    std::string error;
    const llvm::Target* target = llvm::TargetRegistry::lookupTarget(triple, error);
    if (!target) {
        llvm::errs() << "cheri: " << error << '\n';
        return 1;
    }
    std::string cpu = options.cpu == "native" ? llvm::sys::getHostCPUName().str() : options.cpu;
    // position-independent code links into the PIE executables cc makes by default, and a static executable doesn't need it, and the backend optimizes as hard as the pipeline does
    llvm::TargetOptions targetOptions;
    llvm::CodeGenOptLevel backendLevel = options.level == '0' ? llvm::CodeGenOptLevel::None
        : options.level == '1' ? llvm::CodeGenOptLevel::Less
        : options.level == '3' ? llvm::CodeGenOptLevel::Aggressive
        : llvm::CodeGenOptLevel::Default;
    std::unique_ptr<llvm::TargetMachine> machine(target->createTargetMachine(triple, cpu, options.features, targetOptions, options.isStatic ? llvm::Reloc::Static : llvm::Reloc::PIC_, std::nullopt, backendLevel));
    module->setTargetTriple(triple);
    module->setDataLayout(machine->createDataLayout());
    optimize(*module, machine.get(), options.level);

    // the stages before an executable are written as asked, named after the file with the stage's extension unless -o says otherwise
    auto outputPath = [&](const std::string& extension) { return options.output.empty() ? stem + extension : options.output; };
    switch (options.emit) {
        case Emit::LLVM: {
            // IR goes to the terminal unless it has somewhere else to go, since it's most often read right away
            if (options.output.empty()) {
                llvm::outs() << *module;
                return 0;
            }
            auto out = openOutput(options.output, llvm::sys::fs::OF_Text);
            if (!out) return 1;
            *out << *module;
            return 0;
        }
        case Emit::Bitcode: {
            auto out = openOutput(outputPath(".bc"), llvm::sys::fs::OF_None);
            if (!out) return 1;
            llvm::WriteBitcodeToFile(*module, *out);
            return 0;
        }
        case Emit::Asm:
            return writeMachineCode(*module, *machine, outputPath(".s"), llvm::CodeGenFileType::AssemblyFile) ? 0 : 1;
        case Emit::Obj:
            return writeMachineCode(*module, *machine, outputPath(".o"), llvm::CodeGenFileType::ObjectFile) ? 0 : 1;
        case Emit::Exe:
            break;
    }

    // an executable that's only run goes in the temporary directory, named so two runs at once don't collide
    std::string output = options.output;
    if (options.command == "run") {
        output = (std::filesystem::temp_directory_path() / (stem + "-" + std::to_string(getpid()))).string();
    } else if (output.empty()) {
        output = stem;
    }
    // writes the object next to the executable, as a temporary that's removed once it's linked, or once writing it fails
    std::string object = output + ".o";
    if (!writeMachineCode(*module, *machine, object, llvm::CodeGenFileType::ObjectFile)) {
        std::filesystem::remove(object);
        return 1;
    }
    // links the object with the runtime, whose C main calls cheri_init and cheri_main, and the linker brings in the C runtime that calls main, where every argument is quoted so paths with spaces stay whole
    std::string command = quote(options.linker) + " " + quote(object) + " " + quote(CHERI_RUNTIME) + " -o " + quote(output);
    if (options.isStatic) command += " -static";
    for (auto& arg : options.linkArgs) command += " " + quote(arg);
    int linked = std::system(command.c_str());
    std::filesystem::remove(object);
    if (linked != 0) return 1;
    if (options.command == "build") return 0;

    // runs the program and exits the way it did, where a program killed by a signal exits like a shell reports it, 128 plus the signal
    int status = std::system(quote(output).c_str());
    std::filesystem::remove(output);
    if (WIFEXITED(status)) return WEXITSTATUS(status);
    if (WIFSIGNALED(status)) return 128 + WTERMSIG(status);
    return 1;
}
