#pragma once

#include <cstdlib>
#include <filesystem>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

namespace kr32::os {

struct ImagePaths {
    std::string kernel;
    std::string bios;
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
// installation locations. BIOS defaults to the kernel's sibling bios.bin.
inline bool resolve(std::string_view argv0,
                    const std::string& explicit_kernel,
                    const std::string& explicit_bios,
                    ImagePaths& paths,
                    std::string& error) {
    paths = {};
    error.clear();
    const std::filesystem::path executable = detail::executable_directory(argv0);
    const std::filesystem::path current = std::filesystem::current_path();

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
        detail::add_candidate(candidates, current / "kr32os" / "build" / "kernel.bin");
        detail::add_candidate(candidates, current / "kr32os" / "build" / "kr32os.bin");
        detail::add_candidate(candidates, executable / ".." / "share" / "kr32" / "kr32os.bin");
        paths.kernel = detail::find_existing(candidates);
        if (paths.kernel.empty()) {
            error = "cannot find a default KR32OS image; pass --os <kernel.bin> "
                    "or set KR32OS_BIN";
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
    detail::add_candidate(
        bios_candidates,
        std::filesystem::path(paths.kernel).parent_path() / "bios.bin");
    detail::add_candidate(bios_candidates, executable / "bios.bin");
    detail::add_candidate(bios_candidates, current / "kr32os" / "build" / "bios.bin");
    detail::add_candidate(bios_candidates, executable / ".." / "share" / "kr32" / "bios.bin");
    paths.bios = detail::find_existing(bios_candidates);
    if (paths.bios.empty()) {
        error = "cannot find the KR32OS BIOS; pass --bios <bios.bin> or set KR32OS_BIOS";
        return false;
    }
    return true;
}

} // namespace kr32::os
