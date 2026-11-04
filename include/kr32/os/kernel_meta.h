#pragma once

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

namespace kr32::os {

inline constexpr std::uint32_t kKernelMetaMagic = 0x4D32334B; // "K32M"
inline constexpr std::uint32_t kKernelMetaVersion = 1;

struct KernelMeta {
    std::uint32_t magic = 0;
    std::uint32_t version = 0;
    std::uint32_t entry = 0;
    std::uint32_t initrd_offset = 0;
    std::uint32_t initrd_size = 0;
    std::uint32_t checksum = 0;
};

static_assert(sizeof(KernelMeta) == 6 * sizeof(std::uint32_t));

inline std::uint32_t crc32(const std::uint8_t* data, std::size_t size) {
    std::uint32_t crc = 0xFFFF'FFFFU;
    for (std::size_t index = 0; index < size; ++index) {
        crc ^= data[index];
        for (int bit = 0; bit < 8; ++bit) {
            const std::uint32_t mask = 0U - (crc & 1U);
            crc = (crc >> 1U) ^ (0xEDB8'8320U & mask);
        }
    }
    return ~crc;
}

// Loads <kernel-stem>.meta when present. A missing sidecar is not an error for
// pre-M4 kernels; a malformed or stale sidecar is.
inline bool load_kernel_meta(const std::string& kernel_path,
                             KernelMeta& meta,
                             std::string& error) {
    std::filesystem::path meta_path(kernel_path);
    meta_path.replace_extension(".meta");
    error.clear();
    if (!std::filesystem::exists(meta_path)) {
        return false;
    }
    std::ifstream input(meta_path, std::ios::binary);
    if (!input) {
        error = "cannot read " + meta_path.string();
        return false;
    }
    std::uint8_t raw[sizeof(KernelMeta)];
    input.read(reinterpret_cast<char*>(raw), sizeof(raw));
    if (input.gcount() != static_cast<std::streamsize>(sizeof(raw))) {
        error = "short kernel.meta";
        return false;
    }
    KernelMeta parsed{};
    std::uint32_t* words = reinterpret_cast<std::uint32_t*>(&parsed);
    for (std::size_t index = 0; index < sizeof(KernelMeta) / 4; ++index) {
        words[index] = static_cast<std::uint32_t>(raw[index * 4]) |
                       (static_cast<std::uint32_t>(raw[index * 4 + 1]) << 8U) |
                       (static_cast<std::uint32_t>(raw[index * 4 + 2]) << 16U) |
                       (static_cast<std::uint32_t>(raw[index * 4 + 3]) << 24U);
    }
    if (parsed.magic != kKernelMetaMagic ||
        parsed.version != kKernelMetaVersion) {
        error = "invalid kernel.meta header";
        return false;
    }
    std::ifstream kernel(kernel_path, std::ios::binary);
    if (!kernel) {
        error = "cannot read kernel";
        return false;
    }
    std::vector<std::uint8_t> image(
        (std::istreambuf_iterator<char>(kernel)), std::istreambuf_iterator<char>());
    if (parsed.checksum != crc32(image.data(), image.size())) {
        error = "kernel.meta checksum mismatch";
        return false;
    }
    if (parsed.initrd_size != 0 &&
        (parsed.initrd_offset > image.size() ||
         parsed.initrd_size > image.size() - parsed.initrd_offset)) {
        error = "kernel.meta initrd range is invalid";
        return false;
    }
    meta = parsed;
    return true;
}

} // namespace kr32::os
