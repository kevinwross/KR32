#include "kr32/cli/cli.h"

#include "kr32/asm/assembler.h"
#include "kr32/asm/object_assembler.h"
#include "kr32/cc/cc.h"
#include "kr32/container/container.h"
#include "kr32/core/machine.h"
#include "kr32/debug/debug.h"
#include "kr32/dis/disassembler.h"
#include "kr32/link/linker.h"
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

bool is_container(const std::string& path) {
    const std::size_t dot = path.find_last_of('.');
    return dot != std::string::npos && lower(path.substr(dot + 1)) == "k32";
}

bool is_c_source(const std::string& path) {
    const std::size_t dot = path.find_last_of('.');
    return dot != std::string::npos && lower(path.substr(dot + 1)) == "c";
}

std::string fault_name(core::FaultKind kind);
void print_machine(const core::Machine& machine);

bool read_container_file(const std::string& path,
                         container::Image& image,
                         std::string& error) {
    std::vector<std::uint8_t> bytes;
    if (!read_file(path, bytes, error)) {
        return false;
    }
    container::Error container_error;
    if (!container::read(bytes, image, container_error)) {
        error = path + ": " + container_error.message;
        return false;
    }
    return true;
}

bool load_executable(core::Machine& machine,
                     const container::Image& image,
                     std::string& error) {
    if (image.type != container::ImageType::Executable) {
        error = "input is a relocatable object, not an executable";
        return false;
    }
    for (const container::Section& section : image.sections) {
        if ((section.flags & container::Alloc) == 0 ||
            section.type == container::SectionType::Bss) {
            continue;
        }
        if (!machine.load_image(section.address, section.data)) {
            error = "section does not fit: " + section.name;
            return false;
        }
    }
    machine.reset(image.entry);
    return true;
}

void write_map(const linker::Map& map, std::string& text) {
    text.clear();
    text += "SECTIONS\n";
    for (const linker::Map::Section& section : map.sections) {
        char buffer[128];
        std::snprintf(buffer, sizeof(buffer), "  %-16s 0x%08X 0x%08X\n",
                      section.name.c_str(), section.address, section.size);
        text += buffer;
    }
    text += "SYMBOLS\n";
    for (const linker::Map::Symbol& symbol : map.symbols) {
        char buffer[160];
        std::snprintf(buffer, sizeof(buffer), "  %-24s 0x%08X %s\n",
                      symbol.name.c_str(), symbol.address,
                      symbol.bind == container::SymbolBind::Global ? "GLOBAL"
                      : symbol.bind == container::SymbolBind::Weak ? "WEAK"
                                                                   : "LOCAL");
        text += buffer;
    }
}

int run_machine(core::Machine& machine,
                std::uint64_t max_steps,
                bool trace,
                bool stats) {
    machine.set_syscall_handler([](core::Machine& current, std::uint32_t vector) {
        if (vector == 33) {
            std::putchar(static_cast<int>(current.reg(0) & 0xFFU));
            return true;
        }
        if (vector == 32) {
            current.halt(current.reg(0));
            return true;
        }
        return false;
    });

    bool halted = false;
    for (std::uint64_t step = 0; step < max_steps; ++step) {
        if (trace) {
            std::uint32_t word = 0;
            if (machine.read32(machine.pc(), word)) {
                std::printf("0x%08X: 0x%08X\n", machine.pc(), word);
            }
        }
        const core::StepResult result = machine.step();
        if (result.kind == core::StepResult::Kind::Retired ||
            result.kind == core::StepResult::Kind::FaultDelivered) {
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
    const int exit_code = machine.exit_code_set()
                              ? static_cast<int>(machine.exit_code() & 0xFFU)
                              : static_cast<int>(machine.reg(0) & 0xFFU);
    std::printf("HALTED exit=%d\n", exit_code);
    if (stats) {
        std::printf("ticks=%llu\n", static_cast<unsigned long long>(machine.ticks()));
    }
    return exit_code;
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
    std::printf("kr32 %s (ISA %s, container %s, M4 C bootstrap)\n",
                kr32::kVersion, kr32::kIsaVersion, kr32::kContainerVersion);
}

void print_help() {
    std::printf(
        "Usage: kr32 <command> [options]\n"
        "\n"
        "Commands:\n"
        "  version              print the version tuple\n"
        "  help                 print this help\n"
        "  as <file.asm>        assemble a raw image or .k32 object\n"
        "  dis <image>          disassemble a raw image\n"
        "  vm <file>            assemble/load and run an image\n"
        "  run <file>           alias for vm\n"
        "  ld <files>           link .k32 objects into an executable\n"
        "  build <file>         assemble, link, and optionally run\n"
        "  cc <file>            compile C (M4)\n"
        "  cxx <file>           compile C++ (M9)\n"
        "  size <file>          print .k32 section sizes\n"
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
        "Options for as:  -o <path> --object --base <addr> --listing <path>\n"
        "Options for ld:  -o <image.k32> --map <path> --base <addr> --strip\n"
        "Options for build: -o <image.k32> --map <path> --base <addr> --run --strip\n"
        "Options for dis: --base <addr> --start <addr> --max-words <n>\n"
        "Options for vm:  --base <addr> --max-steps <n> --ram-size <n> --trace --stats\n");
}

void print_as_usage() {
    std::printf("Usage: kr32 as <file.asm> [--object] [-o <path>] [--base <addr>] [--listing <path>]\n");
}

void print_dis_usage() {
    std::printf("Usage: kr32 dis <image> [--base <addr>] [--start <addr>] [--max-words <n>]\n");
}

void print_vm_usage() {
    std::printf("Usage: kr32 vm <file> [--bios <rom>] [--base <addr>] [--max-steps <n>] [--ram-size <n>] [--trace] [--stats]\n");
}

int not_implemented(std::string_view command) {
    std::fprintf(stderr, "kr32: '%.*s' is not implemented yet (M4 C bootstrap).\n",
                 static_cast<int>(command.size()), command.data());
    return 2;
}

int command_as(int argc, char** argv) {
    std::string source;
    std::string output = "a.bin";
    std::string listing;
    std::uint32_t base = kDefaultBase;
    bool object = false;

    for (int index = 2; index < argc; ++index) {
        const std::string_view argument(argv[index]);
        if (argument == "--help" || argument == "-h") {
            print_as_usage();
            return 0;
        }
        if (argument == "--object") {
            object = true;
            continue;
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
    if (object) {
        if (output == "a.bin") {
            output = "a.k32";
        }
        container::Image object_image;
        assembler::Error object_error;
        if (!assembler::assemble_object(text, object_image, object_error)) {
            std::fprintf(stderr, "%s:%d: %s\n", source.c_str(), object_error.line,
                         object_error.message.c_str());
            return 1;
        }
        std::vector<std::uint8_t> container_bytes;
        container::Error container_error;
        if (!container::write(object_image, container_bytes, container_error)) {
            std::fprintf(stderr, "kr32 as: %s\n", container_error.message.c_str());
            return 1;
        }
        if (!write_file(output, container_bytes, error)) {
            std::fprintf(stderr, "kr32 as: %s\n", error.c_str());
            return 1;
        }
        std::printf("wrote %zu-byte object to %s\n", container_bytes.size(), output.c_str());
        return 0;
    }
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

void print_ld_usage() {
    std::printf("Usage: kr32 ld <objects...> [-o <image.k32>] [--map <path>] [--base <addr>] [--strip]\n");
}

void print_build_usage() {
    std::printf("Usage: kr32 build <file.s> [-o <image.k32>] [--map <path>] [--base <addr>] [--run] [--strip]\n");
}

int command_ld(int argc, char** argv) {
    std::string output = "a.k32";
    std::string map_path;
    linker::Options options;
    bool strip = false;
    std::vector<std::string> inputs;

    for (int index = 2; index < argc; ++index) {
        const std::string_view argument(argv[index]);
        if (argument == "--help" || argument == "-h") {
            print_ld_usage();
            return 0;
        }
        if (argument == "-o") {
            if (index + 1 >= argc) {
                std::fprintf(stderr, "kr32 ld: -o needs a path\n");
                return 2;
            }
            output = argv[++index];
            continue;
        }
        if (argument == "--map") {
            if (index + 1 >= argc) {
                std::fprintf(stderr, "kr32 ld: --map needs a path\n");
                return 2;
            }
            map_path = argv[++index];
            continue;
        }
        if (argument == "--strip") {
            strip = true;
            continue;
        }
        if (argument == "--base" || argument == "--stack-top" || argument == "--stack-limit") {
            if (index + 1 >= argc) {
                std::fprintf(stderr, "kr32 ld: %.*s needs a value\n",
                             static_cast<int>(argument.size()), argument.data());
                return 2;
            }
            std::uint32_t value = 0;
            if (!parse_u32(argv[++index], value)) {
                std::fprintf(stderr, "kr32 ld: invalid value\n");
                return 2;
            }
            if (argument == "--base") options.base = value;
            if (argument == "--stack-top") options.stack_top = value;
            if (argument == "--stack-limit") options.stack_limit = value;
            continue;
        }
        inputs.emplace_back(argument);
    }

    if (inputs.empty()) {
        print_ld_usage();
        return 2;
    }
    options.strip = strip;

    std::vector<container::Image> objects;
    std::string error;
    for (const std::string& input : inputs) {
        container::Image object;
        if (!read_container_file(input, object, error)) {
            std::fprintf(stderr, "kr32 ld: %s\n", error.c_str());
            return 1;
        }
        if (object.type != container::ImageType::Object) {
            std::fprintf(stderr, "kr32 ld: %s is not an object\n", input.c_str());
            return 1;
        }
        objects.push_back(std::move(object));
    }

    container::Image image;
    linker::Map map;
    linker::Error link_error;
    if (!linker::link(objects, options, image, map, link_error)) {
        std::fprintf(stderr, "kr32 ld: %s\n", link_error.message.c_str());
        return 1;
    }

    std::vector<std::uint8_t> bytes;
    container::Error container_error;
    if (!container::write(image, bytes, container_error)) {
        std::fprintf(stderr, "kr32 ld: %s\n", container_error.message.c_str());
        return 1;
    }
    if (!write_file(output, bytes, error)) {
        std::fprintf(stderr, "kr32 ld: %s\n", error.c_str());
        return 1;
    }
    if (!map_path.empty()) {
        std::string text;
        write_map(map, text);
        if (!write_text(map_path, text, error)) {
            std::fprintf(stderr, "kr32 ld: %s\n", error.c_str());
            return 1;
        }
    }
    std::printf("linked %zu objects to %s (entry 0x%08X)\n", objects.size(),
                output.c_str(), image.entry);
    return 0;
}

int command_size(int argc, char** argv) {
    if (argc != 3) {
        std::printf("Usage: kr32 size <image.k32>\n");
        return 2;
    }
    container::Image image;
    std::string error;
    if (!read_container_file(argv[2], image, error)) {
        std::fprintf(stderr, "kr32 size: %s\n", error.c_str());
        return 1;
    }
    std::printf("%-16s %10s %10s %10s\n", "SECTION", "ADDRESS", "SIZE", "ALIGN");
    for (const container::Section& section : image.sections) {
        std::printf("%-16s 0x%08X 0x%08X %10u\n", section.name.c_str(), section.address,
                    section.size, section.alignment);
    }
    std::printf("entry=0x%08X base=0x%08X type=%s\n", image.entry, image.base,
                image.type == container::ImageType::Executable ? "executable" : "object");
    return 0;
}

int command_build(int argc, char** argv) {
    std::string source;
    std::string output = "a.k32";
    std::string map_path;
    linker::Options options;
    bool run = false;
    bool strip = false;

    for (int index = 2; index < argc; ++index) {
        const std::string_view argument(argv[index]);
        if (argument == "--help" || argument == "-h") {
            print_build_usage();
            return 0;
        }
        if (argument == "--run") {
            run = true;
            continue;
        }
        if (argument == "--strip") {
            strip = true;
            continue;
        }
        if (argument == "-o" || argument == "--map") {
            if (index + 1 >= argc) {
                std::fprintf(stderr, "kr32 build: %.*s needs a path\n",
                             static_cast<int>(argument.size()), argument.data());
                return 2;
            }
            if (argument == "-o") output = argv[++index];
            if (argument == "--map") map_path = argv[++index];
            continue;
        }
        if (argument == "--base") {
            if (index + 1 >= argc || !parse_u32(argv[++index], options.base)) {
                std::fprintf(stderr, "kr32 build: invalid --base\n");
                return 2;
            }
            continue;
        }
        if (!source.empty()) {
            std::fprintf(stderr, "kr32 build: unexpected argument\n");
            return 2;
        }
        source = std::string(argument);
    }

    if (source.empty()) {
        print_build_usage();
        return 2;
    }
    options.strip = strip;

    std::vector<std::uint8_t> source_bytes;
    std::string error;
    if (!read_file(source, source_bytes, error)) {
        std::fprintf(stderr, "kr32 build: %s\n", error.c_str());
        return 1;
    }
    const std::string text(source_bytes.begin(), source_bytes.end());
    std::vector<container::Image> objects;
    assembler::Error assembly_error;
    if (is_c_source(source)) {
        const cc::Result compiled = cc::compile(text, source);
        if (!compiled.ok) {
            for (const cc::Diagnostic& diagnostic : compiled.diagnostics) {
                std::fprintf(stderr, "%s:%d:%d: %s\n", source.c_str(), diagnostic.line,
                             diagnostic.column, diagnostic.message.c_str());
            }
            return 1;
        }
        container::Image user;
        if (!assembler::assemble_object(compiled.assembly, user, assembly_error)) {
            std::fprintf(stderr, "kr32 build: %s:%d: %s\n", source.c_str(),
                         assembly_error.line, assembly_error.message.c_str());
            return 1;
        }
        objects.push_back(std::move(user));
        container::Image runtime;
        if (!assembler::assemble_object(cc::runtime_assembly(), runtime, assembly_error)) {
            std::fprintf(stderr, "kr32 build: runtime: %s\n",
                         assembly_error.message.c_str());
            return 1;
        }
        objects.push_back(std::move(runtime));
    } else {
        container::Image object;
        if (!assembler::assemble_object(text, object, assembly_error)) {
            std::fprintf(stderr, "%s:%d: %s\n", source.c_str(), assembly_error.line,
                         assembly_error.message.c_str());
            return 1;
        }
        objects.push_back(std::move(object));
    }

    container::Image image;
    linker::Map map;
    linker::Error link_error;
    if (!linker::link(objects, options, image, map, link_error)) {
        std::fprintf(stderr, "kr32 build: %s\n", link_error.message.c_str());
        return 1;
    }

    std::vector<std::uint8_t> bytes;
    container::Error container_error;
    if (!container::write(image, bytes, container_error)) {
        std::fprintf(stderr, "kr32 build: %s\n", container_error.message.c_str());
        return 1;
    }
    if (!write_file(output, bytes, error)) {
        std::fprintf(stderr, "kr32 build: %s\n", error.c_str());
        return 1;
    }
    if (!map_path.empty()) {
        std::string map_text;
        write_map(map, map_text);
        if (!write_text(map_path, map_text, error)) {
            std::fprintf(stderr, "kr32 build: %s\n", error.c_str());
            return 1;
        }
    }
    std::printf("built %s (entry 0x%08X)\n", output.c_str(), image.entry);

    if (!run) {
        return 0;
    }

    core::Machine machine(core::Machine::Options{core::kDefaultRamSize});
    if (!load_executable(machine, image, error)) {
        std::fprintf(stderr, "kr32 build: %s\n", error.c_str());
        return 1;
    }
    return run_machine(machine, kDefaultMaxSteps, false, false);
}

void print_cc_usage() {
    std::printf("Usage: kr32 cc <file.c> [-S] [-o <path>]\n");
}

int command_cc(int argc, char** argv) {
    std::string source;
    std::string output;
    bool assembly_only = false;

    for (int index = 2; index < argc; ++index) {
        const std::string_view argument(argv[index]);
        if (argument == "--help" || argument == "-h") {
            print_cc_usage();
            return 0;
        }
        if (argument == "-S") {
            assembly_only = true;
            continue;
        }
        if (argument == "-o") {
            if (index + 1 >= argc) {
                std::fprintf(stderr, "kr32 cc: -o needs a path\n");
                return 2;
            }
            output = argv[++index];
            continue;
        }
        if (!source.empty()) {
            std::fprintf(stderr, "kr32 cc: unexpected argument\n");
            return 2;
        }
        source = std::string(argument);
    }

    if (source.empty()) {
        print_cc_usage();
        return 2;
    }
    if (output.empty()) {
        output = assembly_only ? "a.s" : "a.o";
    }

    std::vector<std::uint8_t> bytes;
    std::string error;
    if (!read_file(source, bytes, error)) {
        std::fprintf(stderr, "kr32 cc: %s\n", error.c_str());
        return 1;
    }
    const std::string text(bytes.begin(), bytes.end());
    const cc::Result compiled = cc::compile(text, source);
    if (!compiled.ok) {
        for (const cc::Diagnostic& diagnostic : compiled.diagnostics) {
            std::fprintf(stderr, "%s:%d:%d: %s\n", source.c_str(), diagnostic.line,
                         diagnostic.column, diagnostic.message.c_str());
        }
        return 1;
    }

    if (assembly_only) {
        if (!write_text(output, compiled.assembly, error)) {
            std::fprintf(stderr, "kr32 cc: %s\n", error.c_str());
            return 1;
        }
        std::printf("wrote assembly to %s\n", output.c_str());
        return 0;
    }

    container::Image object;
    assembler::Error assembly_error;
    if (!assembler::assemble_object(compiled.assembly, object, assembly_error)) {
        std::fprintf(stderr, "kr32 cc: %s:%d: %s\n", source.c_str(), assembly_error.line,
                     assembly_error.message.c_str());
        return 1;
    }
    std::vector<std::uint8_t> object_bytes;
    container::Error container_error;
    if (!container::write(object, object_bytes, container_error)) {
        std::fprintf(stderr, "kr32 cc: %s\n", container_error.message.c_str());
        return 1;
    }
    if (!write_file(output, object_bytes, error)) {
        std::fprintf(stderr, "kr32 cc: %s\n", error.c_str());
        return 1;
    }
    std::printf("wrote object to %s\n", output.c_str());
    return 0;
}

int command_debug(int argc, char** argv) {
    if (argc != 3) {
        std::printf("Usage: kr32 debug <image.k32>\n");
        return 2;
    }
    container::Image image;
    std::string error;
    if (!read_container_file(argv[2], image, error)) {
        std::fprintf(stderr, "kr32 debug: %s\n", error.c_str());
        return 1;
    }
    if (image.type != container::ImageType::Executable) {
        std::fprintf(stderr, "kr32 debug: input is a relocatable object, not an executable\n");
        return 1;
    }
    debug::Session session;
    if (!session.load(image, error)) {
        std::fprintf(stderr, "kr32 debug: %s\n", error.c_str());
        return 1;
    }
    std::printf("%s", session.dump().c_str());
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

    core::Machine machine(core::Machine::Options{ram_size});
    std::string error;
    if (is_container(program)) {
        container::Image executable;
        if (!read_container_file(program, executable, error)) {
            std::fprintf(stderr, "kr32 vm: %s\n", error.c_str());
            return 1;
        }
        if (!load_executable(machine, executable, error)) {
            std::fprintf(stderr, "kr32 vm: %s\n", error.c_str());
            return 1;
        }
    } else {
        std::vector<std::uint8_t> flat;
        if (!load_program(program, base, flat, error)) {
            std::fprintf(stderr, "kr32 vm: %s\n", error.c_str());
            return 1;
        }
        if (!machine.load_image(base, flat)) {
            std::fprintf(stderr, "kr32 vm: image does not fit at 0x%08X\n", base);
            return 1;
        }
        machine.reset(base);
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
    }

    return run_machine(machine, max_steps, trace, stats);
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
    if (command == "ld") {
        return command_ld(argc, argv);
    }
    if (command == "build") {
        return command_build(argc, argv);
    }
    if (command == "size") {
        return command_size(argc, argv);
    }
    if (command == "cc") {
        return command_cc(argc, argv);
    }
    if (command == "debug") {
        return command_debug(argc, argv);
    }

    for (const std::string_view candidate :
         {"cxx", "dap", "shell"}) {
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
