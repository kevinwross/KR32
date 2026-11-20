// Materialises the guest <kr32/con.h> into the build tree.
//
// The name is a reserved DOS device name. A repository cannot track it: Git
// refuses to add it on Windows with core.protectNTFS on, and even with that off
// `git checkout` cannot create the path. The declarations therefore live in
// include/kr32/cc/guest_headers.h -- the same body the built-in front ends
// inject -- and this host tool writes the file a toolchain that reads include
// paths (krc) needs. On Windows the destination is opened through the
// extended-length prefix, the only spelling that creates the name.

#include <cstdio>
#include <fstream>
#include <string>

#include "kr32/cc/guest_headers.h"

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace {

#if defined(_WIN32)
std::wstring widen(const std::string& text) {
    const int length =
        MultiByteToWideChar(CP_UTF8, 0, text.c_str(), -1, nullptr, 0);
    if (length <= 0) {
        return {};
    }
    std::wstring wide(static_cast<std::size_t>(length), L'\0');
    if (MultiByteToWideChar(CP_UTF8, 0, text.c_str(), -1, wide.data(), length) <=
        0) {
        return {};
    }
    wide.resize(static_cast<std::size_t>(length - 1));
    return wide;
}
#endif

bool write_file(const std::string& path, const std::string& contents) {
#if defined(_WIN32)
    std::string extended = path;
    for (char& character : extended) {
        if (character == '/') {
            character = '\\';
        }
    }
    if (extended.rfind("\\\\?\\", 0) == 0) {
        // Already prefixed.
    } else if (extended.size() >= 2 && extended[1] == ':') {
        extended = "\\\\?\\" + extended;
    } else {
        return false;
    }
    const std::wstring wide = widen(extended);
    if (wide.empty()) {
        return false;
    }
    const HANDLE file =
        CreateFileW(wide.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                    FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        return false;
    }
    DWORD written = 0;
    const bool ok =
        WriteFile(file, contents.data(), static_cast<DWORD>(contents.size()),
                  &written, nullptr) != 0 &&
        written == contents.size();
    CloseHandle(file);
    return ok;
#else
    std::ofstream output(path, std::ios::binary);
    if (!output) {
        return false;
    }
    output.write(contents.data(), static_cast<std::streamsize>(contents.size()));
    return static_cast<bool>(output);
#endif
}

} // namespace

int main(int argc, char** argv) {
    if (argc != 2) {
        std::fprintf(stderr, "usage: kr32_guest_headers <path-to-con.h>\n");
        return 2;
    }
    const std::string contents =
        "#pragma once\n\n" + std::string(kr32::cc::kGuestConHeader);
    if (!write_file(argv[1], contents)) {
        std::fprintf(stderr, "kr32_guest_headers: cannot write %s\n", argv[1]);
        return 1;
    }
    return 0;
}
