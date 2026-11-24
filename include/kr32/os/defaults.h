#pragma once

#include "kr32/os/embedded_os.h"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

namespace kr32::os {

struct ImagePaths {
    std::string kernel;
    std::string bios;
    bool embedded = false;
};

namespace detail {

inline std::string environment(const char* name) {
#if defined(_MSC_VER)
    char* value = nullptr;
    std::size_t size = 0;
    if (_dupenv_s(&value, &size, name) != 0 || value == nullptr) {
        return {};
    }
    std::string result(value);
    std::free(value);
    return result;
#else
    const char* value = std::getenv(name);
    return value == nullptr ? std::string() : std::string(value);
#endif
}

inline const char* platform_directory() {
#if defined(_WIN32)
    return "windows";
#elif defined(__APPLE__)
    return "macos";
#else
    return "linux";
#endif
}

inline bool is_file(const std::filesystem::path& path) {
    std::error_code error;
    return std::filesystem::is_regular_file(path, error);
}

inline std::filesystem::path executable_directory(std::string_view argv0) {
    std::error_code error;
    if (!argv0.empty()) {
        const std::filesystem::path executable(argv0);
        if (executable.has_parent_path()) {
            return executable.parent_path();
        }
    }
    const std::filesystem::path current = std::filesystem::current_path(error);
    return error ? std::filesystem::path(".") : current;
}

inline void add_candidate(std::vector<std::filesystem::path>& candidates,
                          std::filesystem::path path) {
    if (path.empty()) return;
    for (const std::filesystem::path& existing : candidates) {
        if (existing == path) return;
    }
    candidates.push_back(std::move(path));
}

inline std::string find_existing(
    const std::vector<std::filesystem::path>& candidates) {
    for (const std::filesystem::path& path : candidates) {
        if (is_file(path)) return path.string();
    }
    return {};
}

} // namespace detail

// Resolves an explicit OS image, or searches the conventional development and
// installation locations. BIOS defaults to a sibling matching the kernel's
// format, or to the embedded image pair when no files are available.
inline bool resolve(std::string_view argv0,
                    const std::string& explicit_kernel,
                    const std::string& explicit_bios,
                    ImagePaths& paths,
                    std::string& error,
                    bool force_embedded = false) {
    paths = {};
    error.clear();
    const std::filesystem::path executable = detail::executable_directory(argv0);
    const std::filesystem::path current = std::filesystem::current_path();
    const bool embedded_available = embedded_os_available();

    if (force_embedded) {
        if (!explicit_kernel.empty() || !explicit_bios.empty()) {
            error = "--embedded cannot be combined with --os or --bios";
            return false;
        }
        if (!embedded_available) {
            error = "this kr32 build has no embedded KR32OS images";
            return false;
        }
        paths.kernel = "<embedded>";
        paths.bios = "<embedded>";
        paths.embedded = true;
        return true;
    }

    if (!explicit_kernel.empty()) {
        if (!detail::is_file(explicit_kernel)) {
            error = "OS image not found: " + explicit_kernel;
            return false;
        }
        paths.kernel = explicit_kernel;
    } else {
        std::vector<std::filesystem::path> candidates;
        detail::add_candidate(candidates, detail::environment("KR32OS_BIN"));
        detail::add_candidate(candidates, executable / "kr32os.bin");
        detail::add_candidate(candidates, current / "kr32os" / "build" /
                                              detail::platform_directory() /
                                              "kernel.k32");
        detail::add_candidate(candidates, current / "kr32os" / "build" /
                                              detail::platform_directory() /
                                              "kernel.bin");
        detail::add_candidate(candidates, current / "kr32os" / "build" / "kernel.bin");
        detail::add_candidate(candidates, current / "kr32os" / "build" / "kr32os.bin");
        detail::add_candidate(candidates, executable / ".." / "share" / "kr32" / "kr32os.bin");
        paths.kernel = detail::find_existing(candidates);
        if (paths.kernel.empty()) {
            if (embedded_available && explicit_bios.empty()) {
                paths.kernel = "<embedded>";
                paths.bios = "<embedded>";
                paths.embedded = true;
                return true;
            }
            error = "cannot find a default KR32OS image; pass --os <kernel.bin>, "
                    "set KR32OS_BIN, or use --embedded";
            return false;
        }
    }

    if (!explicit_bios.empty()) {
        if (!detail::is_file(explicit_bios)) {
            error = "OS BIOS not found: " + explicit_bios;
            return false;
        }
        paths.bios = explicit_bios;
        return true;
    }

    std::vector<std::filesystem::path> bios_candidates;
    detail::add_candidate(bios_candidates, detail::environment("KR32OS_BIOS"));
    const std::filesystem::path kernel_path(paths.kernel);
    // The format is a property of the file, not of its name: a container starts
    // with the KRO magic, a flat image does not. Reading four bytes decides it
    // the same way for any name, including one this code has never seen.
    const auto starts_with_kro_magic = [](const std::filesystem::path& path) {
        std::ifstream stream(path, std::ios::binary);
        if (!stream) return false;
        char magic[4] = {0, 0, 0, 0};
        stream.read(magic, sizeof(magic));
        return stream.gcount() == static_cast<std::streamsize>(sizeof(magic)) &&
               magic[0] == 'K' && magic[1] == '3' && magic[2] == '2' && magic[3] == '\0';
    };
    // With no kernel file to read -- the embedded OS -- there is no header to
    // decide from, and the name is the only information there is.
    const bool kernel_is_container =
        detail::is_file(kernel_path) ? starts_with_kro_magic(kernel_path)
                                     : kernel_path.extension() == ".k32";
    detail::add_candidate(
        bios_candidates,
        kernel_path.parent_path() /
            (kernel_is_container ? "bios.k32" : "bios.bin"));
    detail::add_candidate(
        bios_candidates,
        kernel_path.parent_path() /
            (kernel_is_container ? "bios.bin" : "bios.k32"));
    detail::add_candidate(bios_candidates, executable / "bios.bin");
    detail::add_candidate(bios_candidates, current / "kr32os" / "build" /
                                               detail::platform_directory() /
                                               "bios.k32");
    detail::add_candidate(bios_candidates, current / "kr32os" / "build" /
                                               detail::platform_directory() /
                                               "bios.bin");
    detail::add_candidate(bios_candidates, current / "kr32os" / "build" / "bios.bin");
    detail::add_candidate(bios_candidates, executable / ".." / "share" / "kr32" / "bios.bin");
    paths.bios = detail::find_existing(bios_candidates);
    if (paths.bios.empty()) {
        error = "cannot find the KR32OS BIOS; pass --bios <bios.bin>, set "
                "KR32OS_BIOS, or use --embedded";
        return false;
    }
    return true;
}

} // namespace kr32::os
