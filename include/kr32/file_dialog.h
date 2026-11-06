#pragma once

#include <cstdint>
#include <string>

namespace kr32 {

enum class FileDialogKind {
    Open,
    Save,
};

enum class FileDialogStatus {
    Selected,
    Canceled,
    Unavailable,
    Error,
};

struct FileDialogOptions {
    FileDialogKind kind = FileDialogKind::Open;
    std::string title;
    std::string filter;          // Semicolon-separated patterns, e.g. *.k32;*.c
    std::string initial_path;
    std::string default_extension;
    bool must_exist = true;
    bool overwrite_prompt = true;
};

struct FileDialogResult {
    FileDialogStatus status = FileDialogStatus::Canceled;
    std::string path;
    std::string error;
};

// Guest ABI for the host-only file-dialog syscall.
namespace file_dialog {
inline constexpr std::uint32_t Vector = 51;
inline constexpr std::uint32_t Open = 0;
inline constexpr std::uint32_t Save = 1;
inline constexpr std::uint32_t MustExist = 1U << 0;
inline constexpr std::uint32_t OverwritePrompt = 1U << 1;
inline constexpr std::uint32_t Canceled = 0xFFFF'FFFFU;     // -1
inline constexpr std::uint32_t Unavailable = 0xFFFF'FFFEU;  // -2
inline constexpr std::uint32_t Error = 0xFFFF'FFFBU;        // -5
inline constexpr std::uint32_t Invalid = 0xFFFF'FFEAU;      // -22
inline constexpr std::uint32_t NoSpace = 0xFFFF'FFE4U;      // -28
inline constexpr std::uint32_t Fault = 0xFFFF'FFF2U;        // -14
} // namespace file_dialog

} // namespace kr32
