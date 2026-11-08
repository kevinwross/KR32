#pragma once

#include <cstdint>
#include <span>
#include <string_view>

namespace kr32::os {

struct EmbeddedOs {
    std::span<const std::uint8_t> bios;
    std::span<const std::uint8_t> kernel;
    std::uint32_t bios_crc = 0;
    std::uint32_t kernel_crc = 0;
    std::string_view build_id;
};

[[nodiscard]] const EmbeddedOs& embedded_os();

[[nodiscard]] bool embedded_os_available();

} // namespace kr32::os
