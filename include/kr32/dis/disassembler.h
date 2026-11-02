#pragma once

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace kr32::disassembler {

struct Options {
    std::uint32_t base = 0x0001'0000;
    std::uint32_t start = 0;
    std::uint32_t max_words = 0;
    std::vector<std::pair<std::string, std::uint32_t>> symbols;
};

[[nodiscard]] std::string disassemble(const std::vector<std::uint8_t>& bytes,
                                      const Options& options = {});

} // namespace kr32::disassembler
