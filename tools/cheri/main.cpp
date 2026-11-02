#include <algorithm>
#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <string_view>
#include <thread>
#ifdef _WIN32
#define NOMINMAX
#include <fcntl.h>
#include <io.h>
#include <process.h>
#include <windows.h>
#else
#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

#include "parser.hpp"
#include "codegen.hpp"
#include "server.hpp"

#include <llvm/IR/Verifier.h>
#include <llvm/IR/LegacyPassManager.h>
#include <llvm/Bitcode/BitcodeWriter.h>
#include <llvm/MC/TargetRegistry.h>
#include <llvm/Target/TargetMachine.h>
#include <llvm/Target/TargetOptions.h>
#include <llvm/TargetParser/Host.h>
#include <llvm/Support/FileSystem.h>
#include <llvm/Support/Program.h>
#include <llvm/Support/raw_ostream.h>
#include <llvm/Support/TargetSelect.h>
#include <llvm/Passes/PassBuilder.h>

// the few things that differ between platforms, so the rest of the driver reads the same everywhere
namespace platform {
#ifdef _WIN32
    // Windows names executables .exe and objects .obj, and has no cc, but clang links there both with MSVC and under MSYS2
    constexpr const char* exe = ".exe";
    constexpr const char* obj = ".obj";
    constexpr const char* linker = "clang";
    // the C libraries the runtime needs beyond its own, where Windows' C runtime already has the math functions
    constexpr const char* runtimeLibraries = "";
    inline int processId() { return _getpid(); }
    // Windows hands std::system the program's exit code directly, and a crash comes back as an NTSTATUS like 0xC0000005, which is passed on as it is
    inline int exitCode(int status) { return status; }
    // quotes an argument for cmd, so a path with spaces in it stays one argument
    inline std::string quote(const std::string& arg) {
        std::string quoted = "\"";
        for (char c : arg) quoted += c == '"' ? std::string("\\\"") : std::string(1, c);
        return quoted + "\"";
    }
    // cmd strips the outermost pair of quotes from a command line that starts with one, so the whole command is wrapped in one more pair
    inline std::string shell(const std::string& command) { return "\"" + command + "\""; }
    // the language server's messages count bytes, so stdin and stdout can't turn \n into \r\n on the way through
    inline void binaryStdio() {
        _setmode(_fileno(stdin), _O_BINARY);
        _setmode(_fileno(stdout), _O_BINARY);
    }
    // stops a test that ran out of time
    inline void kill(const llvm::sys::ProcessInfo& process) { TerminateProcess(process.Process, 1); }
#else
    constexpr const char* exe = "";
    constexpr const char* obj = ".o";
    constexpr const char* linker = "cc";
    // the math functions the runtime's FP8 conversions use live in libm on Unix
    constexpr const char* runtimeLibraries = " -lm";
    inline int processId() { return getpid(); }
    // a Unix shell reports a program killed by a signal as 128 plus the signal, so cheri does too
    inline int exitCode(int status) {
        if (WIFEXITED(status)) return WEXITSTATUS(status);
        if (WIFSIGNALED(status)) return 128 + WTERMSIG(status);
        return 1;
    }
    // quotes an argument for the shell, so a path with spaces or quotes in it stays one argument
    inline std::string quote(const std::string& arg) {
        std::string quoted = "'";
        for (char c : arg) quoted += c == '\'' ? std::string("'\\''") : std::string(1, c);
        return quoted + "'";
    }
    // the shell takes a command line as it is
    inline std::string shell(const std::string& command) { return command; }
    // stdin and stdout pass bytes through unchanged already
    inline void binaryStdio() {}
    // stops a test that ran out of time
    inline void kill(const llvm::sys::ProcessInfo& process) { ::kill(process.Pid, SIGKILL); }
#endif
}

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
    std::string linker = platform::linker;
    bool isStatic = false;
    std::vector<std::string> linkArgs;
    // whether check rewrites the file with its fixes first
    bool isFixing = false;
    // how much debug info the build carries, none unless -g or -gline-tables-only asks for it
    DebugLevel debug = DebugLevel::None;
    // which tests cheri test runs: the ones whose names hold the filter, or only the one named it exactly
    std::string filter;
    bool isExact = false;
};

// prints every command and option
static void usage(std::ostream& out) {
    out << "usage: cheri <command> <file.moya> [options]\n"
           "\n"
           "commands:\n"
           "  build                 check the program and build it, an executable by default\n"
           "  check                 check the program without building it\n"
           "  check --fix           apply every fix that's the only one for its diagnostic, then check what's left\n"
           "  run                   build the program, run it, and exit with its exit code\n"
           "  test <path> [name]    run the tests in a .toya file, or in every one under a directory, or only those whose names hold name\n"
           "  test ... --exact      run only the test named exactly name\n"
           "  lsp                   run the language server, which an editor starts and talks to over stdin and stdout\n"
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
           "debugging:\n"
           "  -g                    write debug info for a debugger: lines, functions, variables, types, and scopes\n"
           "  -gline-tables-only    write only which source line each instruction came from, for backtraces\n"
           "\n"
           "target:\n"
           "  --target <triple>     the machine to build for, this one by default\n"
           "  --cpu <name>          the cpu to tune for, generic by default, or native for this machine's\n"
           "  --features <list>     cpu features to turn on or off, like +avx2,-sse4.1\n"
           "\n"
           "linking:\n"
           "  --linker <program>    the program that links, cc by default, or clang on Windows\n"
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
    if (options.command != "build" && options.command != "check" && options.command != "run" && options.command != "test") {
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
        } else if (arg == "-g") {
            options.debug = DebugLevel::Full;
        } else if (arg == "-gline-tables-only") {
            options.debug = DebugLevel::Lines;
        } else if (arg == "--fix") {
            options.isFixing = true;
        } else if (arg == "--exact") {
            options.isExact = true;
        } else if (arg.size() > 1 && arg[0] == '-') {
            fail("unknown option '" + std::string(arg) + "'");
            return false;
        } else if (options.input.empty()) {
            options.input = arg;
        } else if (options.command == "test" && options.filter.empty()) {
            options.filter = arg;
        } else {
            fail("only one file can be built at a time, but got '" + options.input + "' and '" + std::string(arg) + "'");
            return false;
        }
    }
    if (options.input.empty()) {
        fail("'" + options.command + "' needs a file");
        return false;
    }
    if (options.isFixing && options.command != "check") {
        fail("'--fix' only goes with 'check', since it rewrites the file instead of building it");
        return false;
    }
    // run and test build temporary executables and remove them once they've run, so there's nothing for -o or --emit to name
    if ((options.command == "run" || options.command == "test") && (!options.output.empty() || options.emit != Emit::Exe)) {
        fail("'" + options.command + "' builds and runs a temporary executable, so it takes no '-o' or '--emit'");
        return false;
    }
    if (options.isExact && options.command != "test") {
        fail("'--exact' only goes with 'test', since it picks which tests run");
        return false;
    }
    // a .toya file holds tests, which only test runs, and test only runs those
    bool isTests = std::filesystem::path(options.input).extension() == ".toya";
    if (isTests && (options.command == "build" || options.command == "run")) {
        fail("a .toya file holds tests, run it with 'cheri test'");
        return false;
    }
    if (options.command == "test" && !isTests && !std::filesystem::is_directory(options.input)) {
        fail("'test' runs a .toya file or every one under a directory, but got '" + options.input + "'");
        return false;
    }
    return true;
}

// rewrites a file with every preferred fix, the only fix its diagnostic has, then checks it again, since one fix can uncover another, until nothing's left to fix, where a fix whose edits touch one already chosen waits for the next round, and says how many it applied
static void fixFile(const std::string& path, Role role) {
    size_t applied = 0;
    for (int round = 0; round < 10; round++) {
        SourceManager manager;
        manager.loadFile(path);
        DiagnosticEngine engine(manager, nullptr);
        Lexer lexer(engine);
        auto tokens = lexer.lex();
        Parser parser(tokens, engine);
        auto program = parser.parseProgram();
        Sema sema(engine);
        sema.check(program, role);
        std::vector<Edit> edits;
        size_t fixed = 0;
        for (auto& diagnostic : engine.all()) {
            if (diagnostic.fixes.size() != 1) continue;
            auto& fix = diagnostic.fixes[0];
            bool touches = false;
            for (auto& edit : fix.edits) {
                for (auto& chosen : edits) touches = touches || (edit.span.start <= chosen.span.end && chosen.span.start <= edit.span.end);
            }
            if (touches) continue;
            edits.insert(edits.end(), fix.edits.begin(), fix.edits.end());
            fixed++;
        }
        if (fixed == 0) break;
        std::ofstream(path, std::ios::binary) << applyEdits(std::string(manager.text()), edits);
        applied += fixed;
    }
    std::cerr << "cheri: applied " << applied << (applied == 1 ? " fix" : " fixes") << " to '" << path << "'" << std::endl;
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

// one file through the front end, which owns the text every later stage points into: lexed, parsed, and checked as the role says, with its diagnostics printed
struct Checked {
    SourceManager manager;
    std::unique_ptr<DiagnosticEngine> engine;
    std::vector<std::unique_ptr<Stmt>> program;
    std::unique_ptr<Sema> sema;
    bool isRead = false;

    Checked(const std::string& path, Role role) {
        manager.loadFile(path);
        if (manager.text() == "") return;
        isRead = true;
        engine = std::make_unique<DiagnosticEngine>(manager);
        Lexer lexer(*engine);
        auto tokens = lexer.lex();
        program = Parser(tokens, *engine).parseProgram();
        sema = std::make_unique<Sema>(*engine);
        sema->check(program, role);
    }
};

// a module lowered from a checked file, verified, and optimized for the machine it's built for, or null after reporting why it couldn't be
struct Built {
    std::unique_ptr<Codegen> codegen;
    std::unique_ptr<llvm::Module> module;
    std::unique_ptr<llvm::TargetMachine> machine;
};
static std::optional<Built> buildModule(const Options& options, Checked& checked, const std::string& stem, const std::vector<Test>& tests = {}) {
    // the machine to build for, which decides the debug format, CodeView for Windows' MSVC debuggers and DWARF everywhere else
    llvm::Triple triple(options.triple.empty() ? llvm::sys::getDefaultTargetTriple() : options.triple);
    DebugOptions debugOptions{options.debug, options.input, options.level != '0', triple.isWindowsMSVCEnvironment(), triple.isOSBinFormatELF()};
    Built built;
    // the module is named after the file, so prog.moya defines cheri.prog.start
    built.codegen = std::make_unique<Codegen>(stem, checked.manager, debugOptions);
    built.module = built.codegen->generate(checked.program, tests);
    // broken IR is a codegen bug, so it's printed along with what's wrong with it before anything else touches it
    if (llvm::verifyModule(*built.module, &llvm::errs())) {
        llvm::errs() << *built.module << '\n';
        return std::nullopt;
    }
    // sets up the machine to build for, which the optimizer and every output after it depend on, with every target available for cross-compiling
    llvm::InitializeAllTargetInfos();
    llvm::InitializeAllTargets();
    llvm::InitializeAllTargetMCs();
    llvm::InitializeAllAsmPrinters();
    std::string error;
    const llvm::Target* target = llvm::TargetRegistry::lookupTarget(triple, error);
    if (!target) {
        llvm::errs() << "cheri: " << error << '\n';
        return std::nullopt;
    }
    std::string cpu = options.cpu == "native" ? llvm::sys::getHostCPUName().str() : options.cpu;
    // position-independent code links into the PIE executables cc makes by default, and a static executable doesn't need it, and the backend optimizes as hard as the pipeline does
    llvm::TargetOptions targetOptions;
    llvm::CodeGenOptLevel backendLevel = options.level == '0' ? llvm::CodeGenOptLevel::None
        : options.level == '1' ? llvm::CodeGenOptLevel::Less
        : options.level == '3' ? llvm::CodeGenOptLevel::Aggressive
        : llvm::CodeGenOptLevel::Default;
    built.machine.reset(target->createTargetMachine(triple, cpu, options.features, targetOptions, options.isStatic ? llvm::Reloc::Static : llvm::Reloc::PIC_, std::nullopt, backendLevel));
    built.module->setTargetTriple(triple);
    built.module->setDataLayout(built.machine->createDataLayout());
    optimize(*built.module, built.machine.get(), options.level);
    return built;
}

// writes a built module as an object next to the executable, as a temporary that's removed once it's linked, then links it with the runtime and an entry point, a program's, whose C main calls cheri_init and cheri_start, or a test binary's, and the linker brings in the C runtime that calls main, where every argument is quoted so paths with spaces stay whole
static bool linkExecutable(const Options& options, Built& built, const std::string& output, const char* entry) {
    std::string object = output + platform::obj;
    if (!writeMachineCode(*built.module, *built.machine, object, llvm::CodeGenFileType::ObjectFile)) {
        std::filesystem::remove(object);
        return false;
    }
    std::string command = platform::quote(options.linker) + " " + platform::quote(object) + " " + platform::quote(entry) + " " + platform::quote(CHERI_RUNTIME) + platform::runtimeLibraries + " -o " + platform::quote(output);
    if (options.isStatic) command += " -static";
    // the linker writes a CodeView build's PDB only when it's asked to, and -g changes nothing for one that keeps DWARF in the executable
    if (options.debug != DebugLevel::None) command += " -g";
    for (auto& arg : options.linkArgs) command += " " + platform::quote(arg);
    int linked = std::system(platform::shell(command).c_str());
    std::filesystem::remove(object);
    return linked == 0;
}

// where a temporary executable goes, in the temporary directory, named so two runs at once don't collide
static std::string temporaryExecutable(const std::string& stem) {
    return (std::filesystem::temp_directory_path() / (stem + "-" + std::to_string(platform::processId()) + platform::exe)).string();
}

// what happened to one test: it passed, it failed with what it printed, or it ran out of time
struct Outcome {
    enum Kind { Passed, Failed, TimedOut } kind;
    std::string detail;
};

// runs one test in a process of its own, the test binary given its number, and judges it: a test passes by finishing, or, when it's expected to trap, by trapping with that text in the message, which the runtime prints to stderr before it aborts, where a test with a timeout is polled and killed once it runs past it, since LLVM's own timeout is one alarm for the whole process, which tests running at once can't share
static Outcome runTest(const std::string& binary, size_t index, const Test& test) {
    std::string log = binary + "." + std::to_string(index) + ".stderr";
    std::string number = std::to_string(index);
    std::optional<llvm::StringRef> redirects[] = {llvm::StringRef(""), llvm::StringRef(""), llvm::StringRef(log)};
    std::string error;
    llvm::sys::ProcessInfo process = llvm::sys::ExecuteNoWait(binary, {binary, number}, std::nullopt, redirects, 0, &error);
    if (process.Pid == llvm::sys::ProcessInfo::InvalidPid) return {Outcome::Failed, "couldn't run: " + error};
    auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(test.timeout.value_or(0));
    llvm::sys::ProcessInfo finished;
    bool isTimedOut = false;
    while (true) {
        finished = llvm::sys::Wait(process, test.timeout ? std::optional<unsigned>(0) : std::nullopt, &error, nullptr, true);
        if (finished.Pid != 0) break;
        if (std::chrono::steady_clock::now() >= deadline) {
            platform::kill(process);
            llvm::sys::Wait(process, std::nullopt, &error);
            isTimedOut = true;
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    int status = finished.ReturnCode;
    std::ifstream file(log);
    std::string printed((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    file.close();
    std::filesystem::remove(log);
    while (!printed.empty() && printed.back() == '\n') printed.pop_back();
    if (isTimedOut) return {Outcome::TimedOut, ""};
    if (test.traps) {
        if (status != 0 && printed.find(*test.traps) != std::string::npos) return {Outcome::Passed, "trapped as expected"};
        if (status == 0) return {Outcome::Failed, "finished, but it should have trapped with '" + *test.traps + "'"};
        return {Outcome::Failed, printed.empty() ? "crashed without a trap, but it should have trapped with '" + *test.traps + "'" : printed};
    }
    if (status == 0) return {Outcome::Passed, ""};
    if (!printed.empty()) return {Outcome::Failed, printed};
    return {Outcome::Failed, status == -2 ? "crashed" : status == -1 ? "couldn't run: " + error : "exited with " + std::to_string(status)};
}

// the passed and failed counts across every file cheri test runs, and the files that didn't build
struct Tally {
    size_t passed = 0;
    size_t failed = 0;
    size_t unbuilt = 0;
};

// checks, builds, and runs one file's tests, each picked by the filter, as many at once as there are cores, then prints each one's outcome in the order they're written, under the file's name
static void testFile(const Options& options, const std::string& path, Tally& tally) {
    Checked checked(path, Role::Tests);
    if (!checked.isRead) {
        std::cerr << "cheri: '" << path << "' is empty or can't be read" << std::endl;
        tally.unbuilt++;
        return;
    }
    if (checked.engine->hadErrors()) {
        tally.unbuilt++;
        return;
    }
    std::vector<Test> picked;
    for (auto& test : checked.sema->allTests()) {
        bool isPicked = options.filter.empty() || (options.isExact ? test.name == options.filter : test.name.find(options.filter) != std::string::npos);
        if (isPicked) picked.push_back(test);
    }
    if (picked.empty()) return;
    // the runner numbers every test the file has, so a picked test is run by its number among them all
    auto& every = checked.sema->allTests();
    std::string stem = std::filesystem::path(path).stem().string();
    auto built = buildModule(options, checked, stem, every);
    std::string binary = temporaryExecutable(stem + "-test");
    if (!built || !linkExecutable(options, *built, binary, CHERI_TEST)) {
        tally.unbuilt++;
        return;
    }
    std::vector<Outcome> outcomes(picked.size());
    std::atomic<size_t> next{0};
    auto work = [&] {
        for (size_t i = next++; i < picked.size(); i = next++) {
            size_t index = std::find_if(every.begin(), every.end(), [&](const Test& t) { return t.fn == picked[i].fn; }) - every.begin();
            outcomes[i] = runTest(binary, index, picked[i]);
        }
    };
    std::vector<std::thread> workers;
    size_t count = std::max<size_t>(1, std::min<size_t>(std::thread::hardware_concurrency(), picked.size()));
    for (size_t i = 0; i < count; i++) workers.emplace_back(work);
    for (auto& worker : workers) worker.join();
    std::filesystem::remove(binary);
    // each line is the outcome, padded to one column, then the test's name, and what it printed goes under it, indented to the name
    std::cout << path << '\n';
    for (size_t i = 0; i < picked.size(); i++) {
        auto& outcome = outcomes[i];
        const char* label = outcome.kind == Outcome::Passed ? "ok       " : outcome.kind == Outcome::Failed ? "FAIL     " : "TIMEOUT  ";
        std::cout << "  " << label << picked[i].name;
        if (outcome.kind == Outcome::TimedOut) std::cout << " (after " << *picked[i].timeout << "s)";
        if (outcome.kind == Outcome::Passed && !outcome.detail.empty()) std::cout << " (" << outcome.detail << ")";
        std::cout << '\n';
        if (outcome.kind == Outcome::Failed) {
            std::string detail = outcome.detail;
            size_t from = 0;
            while (from <= detail.size()) {
                size_t end = detail.find('\n', from);
                if (end == std::string::npos) end = detail.size();
                std::cout << "           " << detail.substr(from, end - from) << '\n';
                from = end + 1;
            }
        }
        (outcome.kind == Outcome::Passed ? tally.passed : tally.failed)++;
    }
}

// cheri test runs every .toya file under a directory, or one file, and prints how many tests passed and failed in all, failing when any test did or any file didn't build
static int runTests(const Options& options) {
    std::vector<std::string> files;
    if (std::filesystem::is_directory(options.input)) {
        for (auto& entry : std::filesystem::recursive_directory_iterator(options.input)) {
            if (entry.is_regular_file() && entry.path().extension() == ".toya") files.push_back(entry.path().string());
        }
        std::sort(files.begin(), files.end());
    } else {
        files.push_back(options.input);
    }
    Tally tally;
    for (auto& file : files) testFile(options, file, tally);
    std::cout << '\n' << tally.passed << " passed, " << tally.failed << " failed";
    if (tally.unbuilt) std::cout << ", " << tally.unbuilt << (tally.unbuilt == 1 ? " file" : " files") << " didn't build";
    std::cout << std::endl;
    return tally.failed || tally.unbuilt ? 1 : 0;
}

// cheri <command> <file> checks a program and builds it: lex, parse, check, lower to LLVM, optimize, then write the stage that was asked for, or link an executable with the runtime, while cheri test runs a file's tests and cheri lsp serves an editor instead
int main(int argc, char** argv) {
    if (argc >= 2) {
        std::string_view first = argv[1];
        if (first == "help" || first == "-h" || first == "--help") {
            usage(std::cout);
            return 0;
        }
        // the language server takes no file and no options, since the editor sends it every document it needs
        if (first == "lsp") {
            platform::binaryStdio();
            return Server(std::cin, std::cout).run();
        }
    }
    if (argc < 2) {
        usage(std::cerr);
        return 1;
    }
    Options options;
    if (!parseOptions(argc, argv, options)) return 1;
    if (options.command == "test") return runTests(options);

    // a .toya file is checked as tests, and anything else as a program, which needs a start
    Role role = std::filesystem::path(options.input).extension() == ".toya" ? Role::Tests : Role::Program;
    if (options.isFixing) fixFile(options.input, role);
    // runs the front end, and stops before codegen if anything reported an error, since codegen only handles checked programs
    Checked checked(options.input, role);
    if (!checked.isRead) {
        std::cerr << "cheri: '" << options.input << "' is empty or can't be read" << std::endl;
        return 1;
    }
    if (checked.engine->hadErrors()) return 1;
    if (options.command == "check") return 0;

    std::string stem = std::filesystem::path(options.input).stem().string();
    auto built = buildModule(options, checked, stem);
    if (!built) return 1;

    // the stages before an executable are written as asked, named after the file with the stage's extension unless -o says otherwise
    auto outputPath = [&](const std::string& extension) { return options.output.empty() ? stem + extension : options.output; };
    switch (options.emit) {
        case Emit::LLVM: {
            // IR goes to the terminal unless it has somewhere else to go, since it's most often read right away
            if (options.output.empty()) {
                llvm::outs() << *built->module;
                return 0;
            }
            auto out = openOutput(options.output, llvm::sys::fs::OF_Text);
            if (!out) return 1;
            *out << *built->module;
            return 0;
        }
        case Emit::Bitcode: {
            auto out = openOutput(outputPath(".bc"), llvm::sys::fs::OF_None);
            if (!out) return 1;
            llvm::WriteBitcodeToFile(*built->module, *out);
            return 0;
        }
        case Emit::Asm:
            return writeMachineCode(*built->module, *built->machine, outputPath(".s"), llvm::CodeGenFileType::AssemblyFile) ? 0 : 1;
        case Emit::Obj:
            return writeMachineCode(*built->module, *built->machine, outputPath(platform::obj), llvm::CodeGenFileType::ObjectFile) ? 0 : 1;
        case Emit::Exe:
            break;
    }

    // an executable that's only run goes in the temporary directory
    std::string output = options.command == "run" ? temporaryExecutable(stem) : options.output.empty() ? stem + platform::exe : options.output;
    if (!linkExecutable(options, *built, output, CHERI_START)) return 1;
    if (options.command == "build") return 0;

    // runs the program and exits the way it did
    int status = std::system(platform::shell(platform::quote(output)).c_str());
    std::filesystem::remove(output);
    return platform::exitCode(status);
}
