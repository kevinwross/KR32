#pragma once

#include <cstdint>
#include <string>

#include "kr32/core/machine.h"

namespace kr32::os {

inline constexpr std::uint32_t kBootInfoAddress = 0x0003'F000;
inline constexpr std::uint32_t kBootInfoMagic = 0x4232334B; // "K32B"
inline constexpr std::uint32_t kBootInfoVersion = 1;

struct BootInfo {
    std::uint32_t magic;
    std::uint32_t version;
    std::uint32_t size;
    std::uint32_t ram_base;
    std::uint32_t ram_size;
    std::uint32_t kernel_base;
    std::uint32_t kernel_size;
    std::uint32_t kernel_entry;
    std::uint32_t framebuffer_base;
    std::uint32_t framebuffer_size;
    std::uint32_t framebuffer_stride;
    std::uint32_t framebuffer_format;
    std::uint32_t initrd_base;
    std::uint32_t initrd_size;
};

static_assert(sizeof(BootInfo) == 14 * sizeof(std::uint32_t));

inline bool write_boot_info(core::Machine& machine,
                            const BootInfo& info,
                            std::string& error) {
    const std::uint32_t* words = reinterpret_cast<const std::uint32_t*>(&info);
    for (std::uint32_t index = 0; index < 14; ++index) {
        if (!machine.write_physical32(kBootInfoAddress + index * 4U, words[index])) {
            error = "cannot write BootInfo";
            return false;
        }
    }
    machine.set_reg(2, kBootInfoAddress);
    return true;
}

} // namespace kr32::os
