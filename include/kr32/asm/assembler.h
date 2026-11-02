#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace kr32::assembler {

struct Options {
    std::uint32_t base = 0x0001'0000;
};

struct Result {
    std::uint32_t base = 0;
    std::vector<std::uint8_t> bytes;
    struct Symbol {
        std::string name;
        std::uint32_t address = 0;
    };
    std::vector<Symbol> symbols;
};

struct Error {
    std::string message;
    int line = 0;
};

[[nodiscard]] bool assemble(std::string_view source,
                            const Options& options,
                            Result& result,
                            Error& error);

} // namespace kr32::assembler
