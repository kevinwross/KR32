#include "kr32/cli/cli.h"

#include "kr32/asm/assembler.h"
#include "kr32/asm/object_assembler.h"
#include "kr32/cc/cc.h"
#include "kr32/container/container.h"
#include "kr32/core/devices.h"
#include "kr32/core/machine.h"
#include "kr32/cxx/cxx.h"
#include "kr32/dap/dap.h"
#include "kr32/debug/debug.h"
#include "kr32/dis/disassembler.h"
#include "kr32/link/linker.h"
#include "kr32/os/bootinfo.h"
#include "kr32/os/kernel_meta.h"
#include "kr32/os/defaults.h"
#include "kr32/version.h"
#include "kr32/vm/host.h"

#include <cctype>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#include <string_view>
#include <vector>

#if defined(_WIN32)
#include <io.h>
#else
#include <unistd.h>
#endif

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

bool is_cxx_source(const std::string& path) {
    const std::size_t dot = path.find_last_of('.');
    if (dot == std::string::npos) {
        return false;
    }
    const std::string extension = lower(path.substr(dot + 1));
    return extension == "cpp" || extension == "cc" || extension == "cxx";
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
    std::vector<std::uint8_t> tls_template;
    for (const container::Section& section : image.sections) {
        if (section.name == ".tls") {
            tls_template = section.data;
            continue;
        }
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
    if (!tls_template.empty()) {
        const std::uint32_t tcb =
            machine.ram_base() + machine.ram_size() - 1024U;
        for (std::size_t index = 0; index < tls_template.size(); ++index) {
            (void)machine.write8(
                tcb + 32U + static_cast<std::uint32_t>(index),
                tls_template[index]);
        }
        machine.set_reg(11, tcb);
    }
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
                bool stats,
                const vm::HostOptions& host_options = {},
                bool setup_arguments = true,
                core::DeviceBus* devices = nullptr) {
    if (setup_arguments) {
        std::string argument_error;
        if (!machine.setup_arguments(host_options.args, "program", argument_error)) {
            std::printf("ARGV %s\n", argument_error.c_str());
            return 1;
        }
    }
    vm::Host host(host_options);
    host.install(machine);

    bool halted = false;
    for (std::uint64_t step = 0; step < max_steps; ++step) {
        if (trace) {
            std::uint32_t word = 0;
            if (machine.read32(machine.pc(), word)) {
                std::printf("0x%08X: 0x%08X\n", machine.pc(), word);
            }
        }
        const core::StepResult result = machine.step();
        if (devices != nullptr) {
            const std::string serial_output = devices->serial().take_output();
            if (!serial_output.empty()) {
                std::fwrite(serial_output.data(), 1, serial_output.size(), stdout);
                std::fflush(stdout);
            }
        }
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
    std::printf("kr32 %s (ISA %s, container %s, M7 host services)\n",
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
        "  os [kernel.bin]      boot KR32OS with the default BIOS\n"
        "  ld <files>           link .k32 objects into an executable\n"
        "  build <file>         assemble, link, and optionally run\n"
        "  cc <file>            compile C (M4)\n"
        "  cxx <file>           compile C++ (M9)\n"
        "  size <file>          print .k32 section sizes\n"
        "  debug <file>         dump debug information (M5)\n"
        "  dap <file>           Debug Adapter Protocol server (stdio or --port)\n"
        "  shell                interactive command shell\n"
#if defined(KR32_HAS_UI)
        "  gui                  open the windowed debugger (M6)\n"
        "  --smoke-ui           initialize and tear down the GUI platform\n"
#else
        "  gui                  not built (KR32_BUILD_UI=OFF)\n"
        "  --smoke-ui           not built (KR32_BUILD_UI=OFF)\n"
#endif
        "\n"
        "Options for as:  -o <path> --object --base <addr> --listing <path>\n"
        "Options for ld:  -o <image.k32> --map <path> --base <addr> --flat --strip\n"
        "Options for build: -o <image.k32> --map <path> --base <addr> --run --strip "
        "--fs-root <dir> --read-only --quota <bytes> --arg <value>\n"
        "Options for dis: --base <addr> --start <addr> --max-words <n>\n"
        "Options for vm:  --base <addr> --max-steps <n> --ram-size <n> --trace --stats "
        "--fs-root <dir> --read-only --quota <bytes> --arg <value> --input <text>\n"
        "Options for os:  --bios <rom> --max-steps <n> --ram-size <n> --trace --stats "
        "--fs-root <dir> --read-only --quota <bytes> --arg <value> --input <text>\n");
}

void print_as_usage() {
    std::printf("Usage: kr32 as <file.asm> [--object] [-o <path>] [--base <addr>] [--listing <path>]\n");
}

void print_dis_usage() {
    std::printf("Usage: kr32 dis <image> [--base <addr>] [--start <addr>] [--max-words <n>]\n");
}

void print_vm_usage() {
    std::printf("Usage: kr32 vm <file> [--bios <rom>] [--os [kernel.bin]] [--base <addr>] "
                "[--max-steps <n>] [--ram-size <n>] [--trace] [--stats] "
                "[--fs-root <dir>] [--read-only] [--quota <bytes>] [--arg <value>] "
                "[--input <text>]\n");
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
    std::printf("Usage: kr32 ld <objects...> [-o <image.k32>] [--map <path>] [--base <addr>] "
                "[--flat] [--strip]\n");
}

void print_build_usage() {
    std::printf("Usage: kr32 build <file.s> [-o <image.k32>] [--map <path>] [--base <addr>] "
                "[--run] [--strip] [--fs-root <dir>] [--read-only] [--quota <bytes>] "
                "[--arg <value>] [--input <text>]\n");
}

int command_ld(int argc, char** argv) {
    std::string output = "a.k32";
    std::string map_path;
    linker::Options options;
    bool strip = false;
    bool flat = false;
    bool output_set = false;
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
            output_set = true;
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
        if (argument == "--flat") {
            flat = true;
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
    if (flat && !output_set) {
        output = "a.bin";
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
    if (flat) {
        linker::Error flat_error;
        if (!linker::flatten(image, options.base, bytes, flat_error)) {
            std::fprintf(stderr, "kr32 ld: %s\n", flat_error.message.c_str());
            return 1;
        }
    } else {
        container::Error container_error;
        if (!container::write(image, bytes, container_error)) {
            std::fprintf(stderr, "kr32 ld: %s\n", container_error.message.c_str());
            return 1;
        }
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
    std::printf("linked %zu objects to %s (entry 0x%08X%s)\n", objects.size(),
                output.c_str(), image.entry, flat ? ", flat" : "");
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
    vm::HostOptions host_options;

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
        if (argument == "--read-only") {
            host_options.read_only = true;
            continue;
        }
        if (argument == "--fs-root" || argument == "--quota" || argument == "--arg" ||
            argument == "--input") {
            if (index + 1 >= argc) {
                std::fprintf(stderr, "kr32 build: %.*s needs a value\n",
                             static_cast<int>(argument.size()), argument.data());
                return 2;
            }
            const std::string value = argv[++index];
            if (argument == "--fs-root") host_options.fs_root = value;
            if (argument == "--input") host_options.input = value;
            if (argument == "--arg") host_options.args.push_back(value);
            if (argument == "--quota") {
                std::uint32_t quota = 0;
                if (!parse_u32(value, quota)) {
                    std::fprintf(stderr, "kr32 build: invalid --quota\n");
                    return 2;
                }
                host_options.quota = quota;
            }
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
    } else if (is_cxx_source(source)) {
        cxx::Options cxx_options;
        cxx_options.source_name = source;
        const cxx::Result compiled = cxx::compile(text, cxx_options);
        if (!compiled.ok) {
            for (const cxx::Diagnostic& diagnostic : compiled.diagnostics) {
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
    return run_machine(machine, kDefaultMaxSteps, false, false, host_options);
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

void print_cxx_usage() {
    std::printf("Usage: kr32 cxx <file.cpp> [-S] [-o <path>]\n");
}

int command_cxx(int argc, char** argv) {
    std::string source;
    std::string output;
    bool assembly_only = false;
    for (int index = 2; index < argc; ++index) {
        const std::string_view argument(argv[index]);
        if (argument == "--help" || argument == "-h") {
            print_cxx_usage();
            return 0;
        }
        if (argument == "-S") {
            assembly_only = true;
            continue;
        }
        if (argument == "-o") {
            if (index + 1 >= argc) {
                std::fprintf(stderr, "kr32 cxx: -o needs a path\n");
                return 2;
            }
            output = argv[++index];
            continue;
        }
        if (!source.empty()) {
            std::fprintf(stderr, "kr32 cxx: unexpected argument\n");
            return 2;
        }
        source = std::string(argument);
    }
    if (source.empty()) {
        print_cxx_usage();
        return 2;
    }
    if (output.empty()) {
        output = assembly_only ? "a.s" : "a.o";
    }
    std::vector<std::uint8_t> bytes;
    std::string error;
    if (!read_file(source, bytes, error)) {
        std::fprintf(stderr, "kr32 cxx: %s\n", error.c_str());
        return 1;
    }
    const std::string text(bytes.begin(), bytes.end());
    cxx::Options options;
    options.source_name = source;
    const cxx::Result compiled = cxx::compile(text, options);
    if (!compiled.ok) {
        for (const cxx::Diagnostic& diagnostic : compiled.diagnostics) {
            std::fprintf(stderr, "%s:%d:%d: %s\n", source.c_str(), diagnostic.line,
                         diagnostic.column, diagnostic.message.c_str());
        }
        return 1;
    }
    if (assembly_only) {
        if (!write_text(output, compiled.assembly, error)) {
            std::fprintf(stderr, "kr32 cxx: %s\n", error.c_str());
            return 1;
        }
        std::printf("wrote assembly to %s\n", output.c_str());
        return 0;
    }
    container::Image object;
    assembler::Error assembly_error;
    if (!assembler::assemble_object(compiled.assembly, object, assembly_error)) {
        std::fprintf(stderr, "kr32 cxx: %s:%d: %s\n", source.c_str(), assembly_error.line,
                     assembly_error.message.c_str());
        return 1;
    }
    std::vector<std::uint8_t> object_bytes;
    container::Error container_error;
    if (!container::write(object, object_bytes, container_error)) {
        std::fprintf(stderr, "kr32 cxx: %s\n", container_error.message.c_str());
        return 1;
    }
    if (!write_file(output, object_bytes, error)) {
        std::fprintf(stderr, "kr32 cxx: %s\n", error.c_str());
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

int command_dap(int argc, char** argv) {
    dap::Options options;
    for (int index = 2; index < argc; ++index) {
        const std::string_view argument(argv[index]);
        if (argument == "--help" || argument == "-h") {
            std::printf("Usage: kr32 dap [<image.k32>] [--port <n>] [--stdio]\n");
            return 0;
        }
        if (argument == "--stdio") {
            options.stdio = true;
            options.port = 0;
            continue;
        }
        if (argument == "--port") {
            if (index + 1 >= argc) {
                std::fprintf(stderr, "kr32 dap: --port needs a value\n");
                return 2;
            }
            std::uint32_t port = 0;
            if (!parse_u32(argv[++index], port) || port == 0 || port > 65535) {
                std::fprintf(stderr, "kr32 dap: invalid port\n");
                return 2;
            }
            options.port = static_cast<int>(port);
            options.stdio = false;
            continue;
        }
        if (!options.image_path.empty()) {
            std::fprintf(stderr, "kr32 dap: unexpected argument\n");
            return 2;
        }
        options.image_path = std::string(argument);
    }
    return dap::run(options);
}

int command_vm(int argc, char** argv) {
    std::string program;
    std::string bios;
    std::string os_path;
    std::uint32_t base = kDefaultBase;
    std::uint32_t ram_size = core::kDefaultRamSize;
    std::uint64_t max_steps = kDefaultMaxSteps;
    bool trace = false;
    bool stats = false;
    bool os_mode = false;
    std::string serial_input;
    vm::HostOptions host_options;

    for (int index = 2; index < argc; ++index) {
        const std::string_view argument(argv[index]);
        if (argument == "--help" || argument == "-h") {
            print_vm_usage();
            return 0;
        }
        if (argument == "--os") {
            os_mode = true;
            if (index + 1 < argc && argv[index + 1][0] != '-') {
                os_path = argv[++index];
            }
            continue;
        }
        if (argument == "--trace") {
            trace = true;
            continue;
        }
        if (argument == "--stats") {
            stats = true;
            continue;
        }
        if (argument == "--read-only") {
            host_options.read_only = true;
            continue;
        }
        if (argument == "--fs-root" || argument == "--quota" || argument == "--arg" ||
            argument == "--input" || argument == "--serial-input") {
            if (index + 1 >= argc) {
                std::fprintf(stderr, "kr32 vm: %.*s needs a value\n",
                             static_cast<int>(argument.size()), argument.data());
                return 2;
            }
            const std::string value = argv[++index];
            if (argument == "--fs-root") host_options.fs_root = value;
            if (argument == "--input") host_options.input = value;
            if (argument == "--serial-input") serial_input = value;
            if (argument == "--arg") host_options.args.push_back(value);
            if (argument == "--quota") {
                std::uint32_t quota = 0;
                if (!parse_u32(value, quota)) {
                    std::fprintf(stderr, "kr32 vm: invalid --quota\n");
                    return 2;
                }
                host_options.quota = quota;
            }
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

    if (os_mode) {
        if (os_path.empty() && !program.empty()) {
            os_path = program;
            program.clear();
        }
        if (!program.empty()) {
            std::fprintf(stderr, "kr32 os: unexpected argument '%s'\n", program.c_str());
            return 2;
        }
        os::ImagePaths paths;
        std::string resolve_error;
        if (!os::resolve(argv[0], os_path, bios, paths, resolve_error)) {
            std::fprintf(stderr, "kr32 os: %s\n", resolve_error.c_str());
            return 1;
        }
        std::vector<std::uint8_t> kernel;
        std::vector<std::uint8_t> rom;
        std::string error;
        if (!read_file(paths.kernel, kernel, error)) {
            std::fprintf(stderr, "kr32 os: %s\n", error.c_str());
            return 1;
        }
        if (!read_file(paths.bios, rom, error)) {
            std::fprintf(stderr, "kr32 os: %s\n", error.c_str());
            return 1;
        }
        core::Machine machine(core::Machine::Options{ram_size});
        if (!machine.load_image(0x0002'0000, kernel)) {
            std::fprintf(stderr, "kr32 os: kernel does not fit at 0x00020000\n");
            return 1;
        }
        if (!machine.load_image(core::kRomBase, rom)) {
            std::fprintf(stderr, "kr32 os: BIOS does not fit in ROM\n");
            return 1;
        }
        machine.reset();
        os::BootInfo boot{};
        boot.magic = os::kBootInfoMagic;
        boot.version = os::kBootInfoVersion;
        boot.size = sizeof(os::BootInfo);
        boot.ram_base = core::kRamBase;
        boot.ram_size = ram_size;
        boot.kernel_base = 0x0002'0000;
        boot.kernel_size = static_cast<std::uint32_t>(kernel.size());
        boot.kernel_entry = 0x0002'0000;
        boot.framebuffer_base = core::kFramebufferBase;
        boot.framebuffer_size = core::kFramebufferSize;
        boot.framebuffer_stride = core::kFramebufferStride;
        boot.framebuffer_format = 1;
        os::KernelMeta meta{};
        std::string meta_error;
        if (os::load_kernel_meta(paths.kernel, meta, meta_error)) {
            boot.initrd_base = 0x0002'0000U + meta.initrd_offset;
            boot.initrd_size = meta.initrd_size;
        } else if (!meta_error.empty()) {
            std::fprintf(stderr, "kr32 os: %s\n", meta_error.c_str());
            return 1;
        }
        std::string boot_error;
        if (!os::write_boot_info(machine, boot, boot_error)) {
            std::fprintf(stderr, "kr32 os: %s\n", boot_error.c_str());
            return 1;
        }
        core::DeviceBus devices;
        devices.attach_machine(machine);
        machine.attach_devices(&devices);
        devices.serial().push_input(serial_input);
        std::printf("os: %s\nbios: %s\n", paths.kernel.c_str(), paths.bios.c_str());
        return run_machine(machine, max_steps, trace, stats, host_options, true,
                           &devices);
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

    return run_machine(machine, max_steps, trace, stats, host_options,
                       bios.empty());
}

int command_os(int argc, char** argv) {
    std::vector<std::string> arguments{"kr32", "vm", "--os"};
    for (int index = 2; index < argc; ++index) {
        arguments.emplace_back(argv[index]);
    }
    std::vector<char*> raw;
    raw.reserve(arguments.size());
    for (std::string& argument : arguments) {
        raw.push_back(argument.data());
    }
    return command_vm(static_cast<int>(raw.size()), raw.data());
}

std::vector<std::string> tokenize_shell(const std::string& line) {
    std::vector<std::string> tokens;
    std::string current;
    bool in_single = false;
    bool in_double = false;
    for (std::size_t index = 0; index < line.size(); ++index) {
        const char c = line[index];
        if (in_single) {
            if (c == '\'') {
                in_single = false;
            } else {
                current.push_back(c);
            }
            continue;
        }
        if (in_double) {
            if (c == '"') {
                in_double = false;
            } else if (c == '\\' && index + 1 < line.size()) {
                current.push_back(line[++index]);
            } else {
                current.push_back(c);
            }
            continue;
        }
        if (c == '\'') {
            in_single = true;
        } else if (c == '"') {
            in_double = true;
        } else if (std::isspace(static_cast<unsigned char>(c)) != 0) {
            if (!current.empty()) {
                tokens.push_back(current);
                current.clear();
            }
        } else {
            current.push_back(c);
        }
    }
    if (!current.empty()) {
        tokens.push_back(current);
    }
    return tokens;
}

bool shell_interactive() {
#if defined(_WIN32)
    return _isatty(_fileno(stdin)) != 0;
#else
    return isatty(fileno(stdin)) != 0;
#endif
}

int command_shell(int argc, char** argv) {
    (void)argc;
    (void)argv;
    std::vector<std::string> history;
    const bool interactive = shell_interactive();
    std::printf("KR32 shell. Type 'help' for commands, 'exit' to leave.\n");
    std::string line;
    while (true) {
        if (interactive) {
            std::printf("kr32> ");
            std::fflush(stdout);
        }
        if (!std::getline(std::cin, line)) {
            break;
        }
        const std::vector<std::string> tokens = tokenize_shell(line);
        if (tokens.empty()) {
            continue;
        }
        history.push_back(line);
        const std::string& command = tokens.front();
        if (command == "exit" || command == "quit") {
            break;
        }
        if (command == "help") {
            std::printf(
                "commands: as, build, cc, debug, dis, ld, run, size, vm, version, "
                "history, clear, exit\n");
            continue;
        }
        if (command == "history") {
            for (std::size_t index = 0; index < history.size(); ++index) {
                std::printf("%3zu  %s\n", index + 1, history[index].c_str());
            }
            continue;
        }
        if (command == "clear") {
            std::printf("\x1b[2J\x1b[H");
            continue;
        }
        if (command == "version") {
            print_version();
            continue;
        }

        std::vector<std::string> arguments;
        arguments.push_back("kr32");
        if (command == "run") {
            arguments.push_back("vm");
        } else {
            arguments.push_back(command);
        }
        arguments.insert(arguments.end(), tokens.begin() + 1, tokens.end());
        std::vector<char*> raw;
        raw.reserve(arguments.size());
        for (std::string& argument : arguments) {
            raw.push_back(argument.data());
        }
        int result = 2;
        const std::string& mode = arguments[1];
        if (mode == "as") result = command_as(static_cast<int>(raw.size()), raw.data());
        if (mode == "dis") result = command_dis(static_cast<int>(raw.size()), raw.data());
        if (mode == "ld") result = command_ld(static_cast<int>(raw.size()), raw.data());
        if (mode == "size") result = command_size(static_cast<int>(raw.size()), raw.data());
        if (mode == "build") result = command_build(static_cast<int>(raw.size()), raw.data());
        if (mode == "cc") result = command_cc(static_cast<int>(raw.size()), raw.data());
        if (mode == "debug") result = command_debug(static_cast<int>(raw.size()), raw.data());
        if (mode == "vm") result = command_vm(static_cast<int>(raw.size()), raw.data());
        if (result != 0) {
            std::printf("[exit %d]\n", result);
        }
    }
    return 0;
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
    if (command == "os") {
        return command_os(argc, argv);
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
    if (command == "cxx") {
        return command_cxx(argc, argv);
    }
    if (command == "debug") {
        return command_debug(argc, argv);
    }
    if (command == "shell") {
        return command_shell(argc, argv);
    }
    if (command == "dap") {
        return command_dap(argc, argv);
    }

    std::fprintf(stderr, "kr32: unknown command '%.*s'.\n",
                 static_cast<int>(command.size()), command.data());
    print_help();
    return 2;
}

} // namespace kr32::cli
