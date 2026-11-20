// Builds a test image from one source with the installed krc package. The test
// suite uses this instead of `kr32 build`, so the C/C++ examples still have an
// image to run while KR32's built-in compiler is being retired.

#include <cstdint>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "krc/toolchain.h"

namespace {

bool read_text(const std::string& path, std::string& text) {
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        return false;
    }
    text.assign(std::istreambuf_iterator<char>(input),
                std::istreambuf_iterator<char>());
    return true;
}

bool read_bytes(const std::string& path, std::vector<std::uint8_t>& bytes) {
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        return false;
    }
    bytes.assign(std::istreambuf_iterator<char>(input),
                 std::istreambuf_iterator<char>());
    return true;
}

} // namespace

int main(int argc, char** argv) {
    std::string source_path;
    std::string output_path;
    bool cxx = false;
    for (int index = 1; index < argc; ++index) {
        const std::string_view argument = argv[index];
        if (argument == "--cxx") {
            cxx = true;
        } else if (argument == "--source" && index + 1 < argc) {
            source_path = argv[++index];
        } else if (argument == "--output" && index + 1 < argc) {
            output_path = argv[++index];
        } else {
            std::fprintf(stderr,
                         "usage: kr32_krc_image --source <file> --output <file> "
                         "[--cxx]\n");
            return 2;
        }
    }
    if (source_path.empty() || output_path.empty()) {
        std::fprintf(stderr, "kr32_krc_image: source and output are required\n");
        return 2;
    }

    std::string source;
    if (!read_text(source_path, source)) {
        std::fprintf(stderr, "kr32_krc_image: cannot read %s\n", source_path.c_str());
        return 1;
    }

    krc::toolchain::Options options;
    options.target = krc::toolchain::Target::Kr32;
    options.cxx = cxx;
    options.debug = true;
    options.optimize = 1;
    options.base = 0x10000;
    options.entry = "_start";
    options.include_paths = {std::string(KRC_SYSROOT) + "/include",
                             std::string(KRC_SYSROOT) + "/kr32/include",
                             KR32_GUEST_INCLUDE_DIR};

    const krc::toolchain::Source unit{source_path, source};
    const krc::toolchain::CompileResult compiled =
        krc::toolchain::compile(unit, options);
    if (!compiled.ok) {
        std::fprintf(stderr, "%s", krc::toolchain::format_diagnostics(
                                      compiled.diagnostics).c_str());
        std::fprintf(stderr, "%s\n", compiled.error.c_str());
        return 1;
    }
    const krc::toolchain::AssembleResult assembled =
        krc::toolchain::assemble(compiled.assembly, compiled.debug_info, options);
    if (!assembled.ok) {
        std::fprintf(stderr, "%s", krc::toolchain::format_diagnostics(
                                      assembled.diagnostics).c_str());
        return 1;
    }

    std::vector<krc::toolchain::Object> objects{
        {"user", assembled.object}};
    std::vector<std::uint8_t> libc_bytes;
    if (!read_bytes(std::string(KRC_KR32_RUNTIME_DIR) + "/libkrcrt.kro",
                    libc_bytes)) {
        std::fprintf(stderr, "kr32_krc_image: cannot read libkrcrt.kro\n");
        return 1;
    }
    objects.push_back({"libkrcrt", std::move(libc_bytes)});
    for (const char* file : {"/fileio.s", "/eh.s", "/crt0.s"}) {
        std::vector<std::uint8_t> bytes;
        if (!read_bytes(std::string(KRC_KR32_RUNTIME_DIR) + file, bytes)) {
            std::fprintf(stderr, "kr32_krc_image: cannot read %s\n", file);
            return 1;
        }
        const std::string text(bytes.begin(), bytes.end());
        const krc::toolchain::AssembleResult runtime =
            krc::toolchain::assemble(text, options);
        if (!runtime.ok) {
            std::fprintf(stderr, "%s", krc::toolchain::format_diagnostics(
                                          runtime.diagnostics).c_str());
            return 1;
        }
        objects.push_back({file, runtime.object});
    }

    const krc::toolchain::LinkResult linked =
        krc::toolchain::link(objects, options);
    if (!linked.ok) {
        std::fprintf(stderr, "%s", krc::toolchain::format_diagnostics(
                                      linked.diagnostics).c_str());
        std::fprintf(stderr, "%s\n", linked.error.c_str());
        return 1;
    }
    std::ofstream output(output_path, std::ios::binary);
    output.write(reinterpret_cast<const char*>(linked.image.data()),
                 static_cast<std::streamsize>(linked.image.size()));
    if (!output) {
        std::fprintf(stderr, "kr32_krc_image: cannot write %s\n", output_path.c_str());
        return 1;
    }
    return 0;
}
