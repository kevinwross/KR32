#include "kr32/cli/cli.h"

#include "kr32/asm/assembler.h"
#include "kr32/core/machine.h"
#include "kr32/dis/disassembler.h"
#include "kr32/version.h"

#include <cctype>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <string>
#include <string_view>
#include <vector>

#if defined(KR32_HAS_UI)
#include "kr32/ui/app.h"
#endif

namespace kr32::cli {
namespace {

constexpr std::uint32_t kDefaultBase = 0x0001'0000;
constexpr std::uint64_t kDefaultMaxSteps = 1'000'000;

std::string lower(std::string_view text) {
    std::string result(text);
    for (char& c : result) {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    return result;
}

bool read_file(const std::string& path, std::vector<std::uint8_t>& bytes, std::string& error) {
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        error = "cannot open " + path;
        return false;
    }
    bytes.assign(std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>());
    return true;
}

bool write_file(const std::string& path, const std::vector<std::uint8_t>& bytes, std::string& error) {
    std::ofstream output(path, std::ios::binary);
    if (!output) {
        error = "cannot write " + path;
        return false;
    }
    output.write(reinterpret_cast<const char*>(bytes.data()),
                 static_cast<std::streamsize>(bytes.size()));
    if (!output) {
        error = "write failed: " + path;
        return false;
    }
    return true;
}

bool write_text(const std::string& path, const std::string& text, std::string& error) {
    std::ofstream output(path, std::ios::binary);
    if (!output) {
        error = "cannot write " + path;
        return false;
    }
    output.write(text.data(), static_cast<std::streamsize>(text.size()));
    if (!output) {
        error = "write failed: " + path;
        return false;
    }
    return true;
}

bool parse_u32(std::string_view text, std::uint32_t& value) {
    const std::string copy(text);
    char* end = nullptr;
    const unsigned long long parsed = std::strtoull(copy.c_str(), &end, 0);
    if (end == copy.c_str() || *end != '\0' || parsed > 0xFFFF'FFFFULL) {
        return false;
    }
    value = static_cast<std::uint32_t>(parsed);
    return true;
}

bool is_assembly(const std::string& path) {
    const std::size_t dot = path.find_last_of('.');
    if (dot == std::string::npos) {
        return false;
    }
    const std::string extension = lower(path.substr(dot + 1));
    return extension == "asm" || extension == "s";
}

bool load_program(const std::string& path,
                  std::uint32_t base,
                  std::vector<std::uint8_t>& bytes,
                  std::string& error) {
    if (is_assembly(path)) {
        std::vector<std::uint8_t> source_bytes;
        if (!read_file(path, source_bytes, error)) {
            return false;
        }
        const std::string source(source_bytes.begin(), source_bytes.end());
        assembler::Result assembled;
        assembler::Error assembly_error;
        if (!assembler::assemble(source, assembler::Options{base}, assembled, assembly_error)) {
            error = path + ":" + std::to_string(assembly_error.line) + ": " +
                    assembly_error.message;
            return false;
        }
        bytes = std::move(assembled.bytes);
        return true;
    }
    return read_file(path, bytes, error);
}

std::string fault_name(core::FaultKind kind) {
    switch (kind) {
    case core::FaultKind::InvalidOpcode: return "invalid-opcode";
    case core::FaultKind::DivideByZero: return "divide-by-zero";
    case core::FaultKind::Breakpoint: return "breakpoint";
    case core::FaultKind::PageFault: return "page-fault";
    case core::FaultKind::Alignment: return "alignment";
    case core::FaultKind::GeneralProtection: return "general-protection";
    case core::FaultKind::DoubleFault: return "double-fault";
    case core::FaultKind::SoftwareInterrupt: return "software-interrupt";
    case core::FaultKind::Limit: return "step-limit";
    case core::FaultKind::None: return "none";
    }
    return "unknown";
}

void print_machine(const core::Machine& machine) {
    std::printf(
        "R0=%u R1=%u R2=%u R3=%u R4=%u R5=%u R6=%u R7=%u\n"
        "R8=%u R9=%u R10=%u R11=%u R12=%u R13=%u R14=%u R15=%u\n",
        machine.reg(0), machine.reg(1), machine.reg(2), machine.reg(3),
        machine.reg(4), machine.reg(5), machine.reg(6), machine.reg(7),
        machine.reg(8), machine.reg(9), machine.reg(10), machine.reg(11),
        machine.reg(12), machine.reg(13), machine.reg(14), machine.reg(15));
    std::printf("PC=0x%08X SP=0x%08X FLAGS=0x%08X ticks=%llu\n",
                machine.pc(), machine.sp(), machine.flags(),
                static_cast<unsigned long long>(machine.ticks()));
}

void print_version() {
    std::printf("kr32 %s (ISA %s, container %s, M2b machine)\n",
                kr32::kVersion, kr32::kIsaVersion, kr32::kContainerVersion);
}

void print_help() {
    std::printf(
        "Usage: kr32 <command> [options]\n"
        "\n"
        "Commands:\n"
        "  version              print the version tuple\n"
        "  help                 print this help\n"
        "  as <file.asm>        assemble to a raw image (default a.bin)\n"
        "  dis <image>          disassemble a raw image\n"
        "  vm <file>            assemble/load and run an image\n"
        "  run <file>           alias for vm\n"
        "  ld <files>           link (M3)\n"
        "  build <file>         compile/assemble/link (M3)\n"
        "  cc <file>            compile C (M4)\n"
        "  cxx <file>           compile C++ (M9)\n"
        "  size <file>          section sizes (M3)\n"
        "  debug <file>         dump debug information (M5)\n"
        "  dap <file>           Debug Adapter Protocol server (M7)\n"
        "  shell                interactive shell (M7)\n"
#if defined(KR32_HAS_UI)
        "  gui                  open the windowed debugger (M6)\n"
        "  --smoke-ui           initialize and tear down the GUI platform\n"
#else
        "  gui                  not built (KR32_BUILD_UI=OFF)\n"
        "  --smoke-ui           not built (KR32_BUILD_UI=OFF)\n"
#endif
        "\n"
        "Options for as:  -o <path> --base <addr>\n"
        "Options for dis: --base <addr> --start <addr> --max-words <n>\n"
        "Options for vm:  --base <addr> --max-steps <n> --ram-size <n> --trace --stats\n");
}

void print_as_usage() {
    std::printf("Usage: kr32 as <file.asm> [-o <image>] [--base <addr>] [--listing <path>]\n");
}

void print_dis_usage() {
    std::printf("Usage: kr32 dis <image> [--base <addr>] [--start <addr>] [--max-words <n>]\n");
}

void print_vm_usage() {
    std::printf("Usage: kr32 vm <file> [--bios <rom>] [--base <addr>] [--max-steps <n>] [--ram-size <n>] [--trace] [--stats]\n");
}

int not_implemented(std::string_view command) {
    std::fprintf(stderr, "kr32: '%.*s' is not implemented yet (M2b machine).\n",
                 static_cast<int>(command.size()), command.data());
    return 2;
}

int command_as(int argc, char** argv) {
    std::string source;
    std::string output = "a.bin";
    std::string listing;
    std::uint32_t base = kDefaultBase;

    for (int index = 2; index < argc; ++index) {
        const std::string_view argument(argv[index]);
        if (argument == "--help" || argument == "-h") {
            print_as_usage();
            return 0;
        }
        if (argument == "-o") {
            if (index + 1 >= argc) {
                std::fprintf(stderr, "kr32 as: -o needs a path\n");
                return 2;
            }
            output = argv[++index];
            continue;
        }
        if (argument == "--listing") {
            if (index + 1 >= argc) {
                std::fprintf(stderr, "kr32 as: --listing needs a path\n");
                return 2;
            }
            listing = argv[++index];
            continue;
        }
        if (argument == "--base") {
            if (index + 1 >= argc || !parse_u32(argv[++index], base)) {
                std::fprintf(stderr, "kr32 as: invalid --base\n");
                return 2;
            }
            continue;
        }
        if (argument.rfind("--base=", 0) == 0) {
            if (!parse_u32(argument.substr(7), base)) {
                std::fprintf(stderr, "kr32 as: invalid --base\n");
                return 2;
            }
            continue;
        }
        if (!source.empty()) {
            std::fprintf(stderr, "kr32 as: unexpected argument '%.*s'\n",
                         static_cast<int>(argument.size()), argument.data());
            return 2;
        }
        source = std::string(argument);
    }

    if (source.empty()) {
        print_as_usage();
        return 2;
    }

    std::vector<std::uint8_t> bytes;
    std::string error;
    if (!read_file(source, bytes, error)) {
        std::fprintf(stderr, "kr32 as: %s\n", error.c_str());
        return 1;
    }
    const std::string text(bytes.begin(), bytes.end());
    assembler::Result result;
    assembler::Error assembly_error;
    if (!assembler::assemble(text, assembler::Options{base}, result, assembly_error)) {
        std::fprintf(stderr, "%s:%d: %s\n", source.c_str(), assembly_error.line,
                     assembly_error.message.c_str());
        return 1;
    }
    if (!write_file(output, result.bytes, error)) {
        std::fprintf(stderr, "kr32 as: %s\n", error.c_str());
        return 1;
    }
    if (!listing.empty()) {
        disassembler::Options options;
        options.base = result.base;
        for (const assembler::Result::Symbol& symbol : result.symbols) {
            options.symbols.emplace_back(symbol.name, symbol.address);
        }
        if (!write_text(listing, disassembler::disassemble(result.bytes, options), error)) {
            std::fprintf(stderr, "kr32 as: %s\n", error.c_str());
            return 1;
        }
    }
    std::printf("wrote %zu bytes at 0x%08X to %s\n", result.bytes.size(), base,
                output.c_str());
    return 0;
}

int command_dis(int argc, char** argv) {
    std::string image;
    std::uint32_t base = kDefaultBase;
    std::uint32_t start = 0;
    std::uint32_t max_words = 0;

    for (int index = 2; index < argc; ++index) {
        const std::string_view argument(argv[index]);
        if (argument == "--help" || argument == "-h") {
            print_dis_usage();
            return 0;
        }
        if (argument == "--base" || argument == "--start" || argument == "--max-words") {
            if (index + 1 >= argc) {
                std::fprintf(stderr, "kr32 dis: %.*s needs a value\n",
                             static_cast<int>(argument.size()), argument.data());
                return 2;
            }
            std::uint32_t value = 0;
            if (!parse_u32(argv[++index], value)) {
                std::fprintf(stderr, "kr32 dis: invalid value for %.*s\n",
                             static_cast<int>(argument.size()), argument.data());
                return 2;
            }
            if (argument == "--base") base = value;
            if (argument == "--start") start = value;
            if (argument == "--max-words") max_words = value;
            continue;
        }
        if (!image.empty()) {
            std::fprintf(stderr, "kr32 dis: unexpected argument '%.*s'\n",
                         static_cast<int>(argument.size()), argument.data());
            return 2;
        }
        image = std::string(argument);
    }

    if (image.empty()) {
        print_dis_usage();
        return 2;
    }

    std::vector<std::uint8_t> bytes;
    std::string error;
    if (!read_file(image, bytes, error)) {
        std::fprintf(stderr, "kr32 dis: %s\n", error.c_str());
        return 1;
    }

    disassembler::Options options;
    options.base = base;
    options.start = start;
    options.max_words = max_words;
    std::printf("%s", disassembler::disassemble(bytes, options).c_str());
    return 0;
}

int command_vm(int argc, char** argv) {
    std::string program;
    std::string bios;
    std::uint32_t base = kDefaultBase;
    std::uint32_t ram_size = core::kDefaultRamSize;
    std::uint64_t max_steps = kDefaultMaxSteps;
    bool trace = false;
    bool stats = false;

    for (int index = 2; index < argc; ++index) {
        const std::string_view argument(argv[index]);
        if (argument == "--help" || argument == "-h") {
            print_vm_usage();
            return 0;
        }
        if (argument == "--trace") {
            trace = true;
            continue;
        }
        if (argument == "--stats") {
            stats = true;
            continue;
        }
        if (argument == "--bios") {
            if (index + 1 >= argc) {
                std::fprintf(stderr, "kr32 vm: --bios needs a path\n");
                return 2;
            }
            bios = argv[++index];
            continue;
        }
        if (argument == "--base" || argument == "--ram-size" || argument == "--max-steps") {
            if (index + 1 >= argc) {
                std::fprintf(stderr, "kr32 vm: %.*s needs a value\n",
                             static_cast<int>(argument.size()), argument.data());
                return 2;
            }
            std::uint32_t value = 0;
            if (!parse_u32(argv[++index], value)) {
                std::fprintf(stderr, "kr32 vm: invalid value for %.*s\n",
                             static_cast<int>(argument.size()), argument.data());
                return 2;
            }
            if (argument == "--base") base = value;
            if (argument == "--ram-size") ram_size = value;
            if (argument == "--max-steps") max_steps = value;
            continue;
        }
        if (!program.empty()) {
            std::fprintf(stderr, "kr32 vm: unexpected argument '%.*s'\n",
                         static_cast<int>(argument.size()), argument.data());
            return 2;
        }
        program = std::string(argument);
    }

    if (program.empty()) {
        print_vm_usage();
        return 2;
    }

    std::vector<std::uint8_t> image;
    std::string error;
    if (!load_program(program, base, image, error)) {
        std::fprintf(stderr, "kr32 vm: %s\n", error.c_str());
        return 1;
    }

    core::Machine machine(core::Machine::Options{ram_size});
    if (!machine.load_image(base, image)) {
        std::fprintf(stderr, "kr32 vm: image does not fit at 0x%08X\n", base);
        return 1;
    }
    if (!bios.empty()) {
        std::vector<std::uint8_t> rom;
        if (!read_file(bios, rom, error)) {
            std::fprintf(stderr, "kr32 vm: %s\n", error.c_str());
            return 1;
        }
        if (!machine.load_image(core::kRomBase, rom)) {
            std::fprintf(stderr, "kr32 vm: BIOS does not fit in ROM\n");
            return 1;
        }
        machine.reset();
    } else {
        machine.reset(base);
    }

    bool halted = false;
    for (std::uint64_t step = 0; step < max_steps; ++step) {
        if (trace) {
            std::uint32_t word = 0;
            if (machine.read32(machine.pc(), word)) {
                std::printf("0x%08X: 0x%08X\n", machine.pc(), word);
            }
        }
        const core::StepResult result = machine.step();
        if (result.kind == core::StepResult::Kind::Retired) {
            continue;
        }
        if (result.kind == core::StepResult::Kind::Halted) {
            halted = true;
            break;
        }
        std::printf("FAULT %s PC=0x%08X address=0x%08X code=0x%X\n",
                    fault_name(result.fault.kind).c_str(),
                    machine.pc(), result.fault.address, result.fault.code);
        print_machine(machine);
        return 1;
    }

    if (!halted) {
        std::printf("LIMIT after %llu steps\n",
                    static_cast<unsigned long long>(max_steps));
        print_machine(machine);
        return 1;
    }

    print_machine(machine);
    const int exit_code = static_cast<int>(machine.reg(0) & 0xFFU);
    std::printf("HALTED exit=%d\n", exit_code);
    if (stats) {
        std::printf("ticks=%llu\n", static_cast<unsigned long long>(machine.ticks()));
    }
    return exit_code;
}

} // namespace

int run(int argc, char** argv) {
    if (argc <= 1) {
#if defined(KR32_HAS_UI)
        return kr32::ui::run(argc, argv);
#else
        print_help();
        return 0;
#endif
    }

    const std::string_view command(argv[1]);

    if (command == "version" || command == "--version") {
        print_version();
        return 0;
    }
    if (command == "help" || command == "--help" || command == "-h") {
        print_help();
        return 0;
    }

#if defined(KR32_HAS_UI)
    if (command == "gui") {
        return kr32::ui::run(argc - 1, argv + 1);
    }
    if (command == "--smoke-ui") {
        const bool ok = kr32::ui::smoke_test();
        std::printf("ui-smoke: %s\n", ok ? "ok" : "failed");
        return ok ? 0 : 1;
    }
#else
    if (command == "gui" || command == "--smoke-ui") {
        std::fprintf(stderr, "kr32: the GUI was not built (KR32_BUILD_UI=OFF).\n");
        return 2;
    }
#endif

    if (command == "as") {
        return command_as(argc, argv);
    }
    if (command == "dis") {
        return command_dis(argc, argv);
    }
    if (command == "vm" || command == "run") {
        return command_vm(argc, argv);
    }

    for (const std::string_view candidate :
         {"ld", "build", "cc", "cxx", "size", "debug", "dap", "shell"}) {
        if (command == candidate) {
            return not_implemented(command);
        }
    }

    std::fprintf(stderr, "kr32: unknown command '%.*s'.\n",
                 static_cast<int>(command.size()), command.data());
    print_help();
    return 2;
}

} // namespace kr32::cli
