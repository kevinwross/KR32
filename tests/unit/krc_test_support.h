#pragma once

// Test-side image builder. The test suite no longer compiles through KR32's
// built-in front ends or the `kr32 build` command; it builds the few images it
// needs through the installed krc package, exactly as the GUI does.

#include <cstdint>
#include <fstream>
#include <iterator>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "krc/toolchain.h"
#include "kr32/container/container.h"

namespace kr32::test {

struct BuiltImage {
    bool ok = false;
    std::string error;
    container::Image image;
};

inline bool read_file_bytes(const std::string& path, std::vector<std::uint8_t>& bytes) {
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        return false;
    }
    bytes.assign(std::istreambuf_iterator<char>(input),
                 std::istreambuf_iterator<char>());
    return true;
}

inline BuiltImage build_krc_image(const std::vector<krc::toolchain::Source>& units,
                                  bool cxx = false) {
    BuiltImage result;
    const std::string sysroot = KR32_TEST_KRC_SYSROOT;
    const std::string runtime = KR32_TEST_KRC_RUNTIME_DIR;

    krc::toolchain::Options options;
    options.target = krc::toolchain::Target::Kr32;
    options.cxx = cxx;
    options.debug = true;
    options.optimize = 1;
    options.base = 0x10000;
    options.entry = "_start";
    options.include_paths = {sysroot + "/include", sysroot + "/kr32/include",
                             std::string(KR32_SOURCE_DIR) + "/include"};

    std::vector<krc::toolchain::Object> objects;
    for (const krc::toolchain::Source& unit : units) {
        const krc::toolchain::CompileResult compiled =
            krc::toolchain::compile(unit, options);
        if (!compiled.ok) {
            result.error = krc::toolchain::format_diagnostics(
                               compiled.diagnostics) +
                           compiled.error;
            return result;
        }
        const krc::toolchain::AssembleResult assembled =
            krc::toolchain::assemble(compiled.assembly, compiled.debug_info,
                                     options);
        if (!assembled.ok) {
            result.error = krc::toolchain::format_diagnostics(
                               assembled.diagnostics) +
                           assembled.error;
            return result;
        }
        objects.push_back(
            krc::toolchain::Object{unit.name, assembled.object});
    }

    std::vector<std::uint8_t> libc_bytes;
    if (!read_file_bytes(runtime + "/libkrcrt.kro", libc_bytes)) {
        result.error = "cannot read " + runtime + "/libkrcrt.kro";
        return result;
    }
    objects.push_back(krc::toolchain::Object{"libkrcrt", std::move(libc_bytes)});
    for (const char* file : {"/fileio.s", "/eh.s", "/crt0.s"}) {
        std::vector<std::uint8_t> bytes;
        if (!read_file_bytes(runtime + file, bytes)) {
            result.error = "cannot read " + runtime + file;
            return result;
        }
        const std::string text(bytes.begin(), bytes.end());
        const krc::toolchain::AssembleResult assembled =
            krc::toolchain::assemble(text, options);
        if (!assembled.ok) {
            result.error = "runtime " + std::string(file) + ": " +
                           krc::toolchain::format_diagnostics(
                               assembled.diagnostics) +
                           assembled.error;
            return result;
        }
        objects.push_back(krc::toolchain::Object{file, assembled.object});
    }

    const krc::toolchain::LinkResult linked =
        krc::toolchain::link(objects, options);
    if (!linked.ok) {
        result.error =
            krc::toolchain::format_diagnostics(linked.diagnostics) + linked.error;
        return result;
    }
    container::Error container_error;
    if (!container::read(linked.image, result.image, container_error)) {
        result.error = container_error.message;
        return result;
    }
    result.ok = true;
    return result;
}

inline BuiltImage build_krc_image(std::string_view source, std::string name,
                                  bool cxx = false) {
    std::vector<krc::toolchain::Source> units;
    units.push_back(krc::toolchain::Source{std::move(name), source});
    return build_krc_image(units, cxx);
}

} // namespace kr32::test
