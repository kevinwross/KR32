#include "kr32/cli/cli.h"

#include "kr32/container/container.h"
#include "kr32/core/devices.h"
#include "kr32/core/machine.h"
#include "kr32/dap/dap.h"
#include "kr32/debug/debug.h"
#include "kr32/dis/disassembler.h"
#include "kr32/os/bootinfo.h"
#include "kr32/os/kernel_meta.h"
#include "kr32/os/defaults.h"
#include "kr32/version.h"
#include "kr32/vm/host.h"

#ifdef KR32_HAS_UI
#include "kr32/platform/window.h"
#endif

#include <algorithm>
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

#ifdef KR32_HAS_UI
vm::FileDialogCallback platform_file_dialog() {
    return [](const kr32::FileDialogOptions& options) {
        return platform::show_file_dialog(options);
    };
}
#endif

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

bool load_program(const std::string& path,
                  std::uint32_t base,
                  std::vector<std::uint8_t>& bytes,
                  std::string& error) {
    (void)base;
    return read_file(path, bytes, error);
}

bool is_container(const std::string& path) {
    const std::size_t dot = path.find_last_of('.');
    return dot != std::string::npos && lower(path.substr(dot + 1)) == "k32";
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
    container::LoadedImage loaded;
    if (!container::load_into(machine, image, loaded, error)) {
        return false;
    }
    machine.reset(image.entry);
    if (!loaded.tls_template.empty()) {
        const std::uint32_t tcb =
            machine.ram_base() + machine.ram_size() - 1024U;
        for (std::size_t index = 0; index < loaded.tls_template.size(); ++index) {
            (void)machine.write8(
                tcb + 32U + static_cast<std::uint32_t>(index),
                loaded.tls_template[index]);
        }
        machine.set_reg(11, tcb);
    }
    return true;
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
        "  dis <image>          disassemble a raw image\n"
        "  vm <file>            load and run a .k32 image\n"
        "  run <file>           alias for vm\n"
        "  os [kernel.k32]      boot KR32OS with the default BIOS\n"
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
        "Options for dis: --base <addr> --start <addr> --max-words <n>\n"
        "Options for vm:  --base <addr> --max-steps <n> --ram-size <n> --trace --stats "
        "--fs-root <dir> --read-only --quota <bytes> --arg <value> --input <text>\n"
        "Options for os:  --embedded --bios <rom> --max-steps <n> --ram-size <n> --trace --stats "
        "--fs-root <dir> --read-only --quota <bytes> --arg <value> --input <text>\n");
}

void print_dis_usage() {
    std::printf("Usage: kr32 dis <image> [--base <addr>] [--start <addr>] [--max-words <n>]\n");
}

void print_vm_usage() {
    std::printf("Usage: kr32 vm <file> [--bios <rom>] [--os [kernel.bin]] [--embedded] [--base <addr>] "
                "[--max-steps <n>] [--ram-size <n>] [--trace] [--stats] "
                "[--fs-root <dir>] [--read-only] [--quota <bytes>] [--arg <value>] "
                "[--input <text>]\n");
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
    bool embedded_images = false;
    std::string serial_input;
    vm::HostOptions host_options;
#ifdef KR32_HAS_UI
    host_options.file_dialog = platform_file_dialog();
#endif

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
        if (argument == "--embedded") {
            embedded_images = true;
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

    if (embedded_images && !os_mode) {
        std::fprintf(stderr, "kr32 vm: --embedded requires --os\n");
        return 2;
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
        if (!os::resolve(argv[0], os_path, bios, paths, resolve_error,
                         embedded_images)) {
            std::fprintf(stderr, "kr32 os: %s\n", resolve_error.c_str());
            return 1;
        }
        std::vector<std::uint8_t> kernel;
        std::vector<std::uint8_t> rom;
        std::string error;
        if (paths.embedded) {
            const os::EmbeddedOs& images = os::embedded_os();
            kernel.assign(images.kernel.begin(), images.kernel.end());
            rom.assign(images.bios.begin(), images.bios.end());
        } else {
            if (!read_file(paths.kernel, kernel, error)) {
                std::fprintf(stderr, "kr32 os: %s\n", error.c_str());
                return 1;
            }
            if (!read_file(paths.bios, rom, error)) {
                std::fprintf(stderr, "kr32 os: %s\n", error.c_str());
                return 1;
            }
        }
        core::Machine machine(core::Machine::Options{ram_size});
        const auto is_container = [](const std::vector<std::uint8_t>& bytes) {
            return bytes.size() >= 4 && bytes[0] == 'K' && bytes[1] == '3' &&
                   bytes[2] == '2' && bytes[3] == 0;
        };
        const bool kernel_container = is_container(kernel);
        const bool bios_container = is_container(rom);
        std::uint32_t kernel_size = static_cast<std::uint32_t>(kernel.size());
        if (kernel_container || bios_container) {
            if (!kernel_container || !bios_container) {
                std::fprintf(stderr, "kr32 os: kernel and BIOS must both be .k32 "
                                     "or both be raw .bin\n");
                return 1;
            }
            container::Image kernel_image;
            container::Image bios_image;
            container::Error container_error;
            if (!container::read(kernel, kernel_image, container_error) ||
                !container::read(rom, bios_image, container_error)) {
                std::fprintf(stderr, "kr32 os: %s\n", container_error.message.c_str());
                return 1;
            }
            const auto load_sections = [&](const container::Image& image) {
                for (const container::Section& section : image.sections) {
                    if ((section.flags & container::Alloc) == 0) continue;
                    if (section.type == container::SectionType::Bss) {
                        for (std::uint32_t offset = 0; offset < section.size; ++offset) {
                            if (!machine.write8(section.address + offset, 0)) {
                                return false;
                            }
                        }
                    } else if (!section.data.empty() &&
                               !machine.load_image(section.address, section.data)) {
                        return false;
                    }
                }
                return true;
            };
            if (!load_sections(kernel_image) || !load_sections(bios_image)) {
                std::fprintf(stderr, "kr32 os: .k32 sections do not fit\n");
                return 1;
            }
            kernel_size = 0;
            for (const container::Section& section : kernel_image.sections) {
                if ((section.flags & container::Alloc) != 0) {
                    kernel_size = std::max(kernel_size, section.address + section.size -
                                                        0x0002'0000U);
                }
            }
        } else {
            if (!machine.load_image(0x0002'0000, kernel)) {
                std::fprintf(stderr, "kr32 os: kernel does not fit at 0x00020000\n");
                return 1;
            }
            if (!machine.load_image(core::kRomBase, rom)) {
                std::fprintf(stderr, "kr32 os: BIOS does not fit in ROM\n");
                return 1;
            }
        }
        machine.reset();
        os::BootInfo boot{};
        boot.magic = os::kBootInfoMagic;
        boot.version = os::kBootInfoVersion;
        boot.size = sizeof(os::BootInfo);
        boot.ram_base = core::kRamBase;
        boot.ram_size = ram_size;
        boot.kernel_base = 0x0002'0000;
        boot.kernel_size = kernel_size;
        boot.kernel_entry = 0x0002'0000;
        boot.framebuffer_base = core::kFramebufferBase;
        boot.framebuffer_size = core::kFramebufferSize;
        boot.framebuffer_stride = core::kFramebufferStride;
        boot.framebuffer_format = 1;
        os::KernelMeta meta{};
        std::string meta_error;
        if (!kernel_container && os::load_kernel_meta(paths.kernel, meta, meta_error)) {
            boot.initrd_base = 0x0002'0000U + meta.initrd_offset;
            boot.initrd_size = meta.initrd_size;
        } else if (!kernel_container && !meta_error.empty()) {
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
        if (paths.embedded) {
            std::printf("os: embedded kernel (%.*s)\nbios: embedded (%.*s)\n",
                        static_cast<int>(os::embedded_os().build_id.size()),
                        os::embedded_os().build_id.data(),
                        static_cast<int>(os::embedded_os().build_id.size()),
                        os::embedded_os().build_id.data());
        } else {
            std::printf("os: %s\nbios: %s\n", paths.kernel.c_str(), paths.bios.c_str());
        }
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
                "commands: debug, dis, run, size, vm, version, history, clear, "
                "exit\n");
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
        if (mode == "dis") result = command_dis(static_cast<int>(raw.size()), raw.data());
        if (mode == "size") result = command_size(static_cast<int>(raw.size()), raw.data());
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
    if (command == "display") {
        return kr32::ui::run_display(argc - 1, argv + 1);
    }
    if (command == "--smoke-ui") {
        const bool ok = kr32::ui::smoke_test();
        std::printf("ui-smoke: %s\n", ok ? "ok" : "failed");
        return ok ? 0 : 1;
    }
#else
    if (command == "gui" || command == "display" || command == "--smoke-ui") {
        std::fprintf(stderr, "kr32: the GUI was not built (KR32_BUILD_UI=OFF).\n");
        return 2;
    }
#endif

    if (command == "dis") {
        return command_dis(argc, argv);
    }
    if (command == "vm" || command == "run") {
        return command_vm(argc, argv);
    }
    if (command == "os") {
        return command_os(argc, argv);
    }
    if (command == "size") {
        return command_size(argc, argv);
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
