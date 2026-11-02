#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "kr32/container/container.h"

namespace kr32::linker {

struct Options {
    std::uint32_t base = 0x0001'0000;
    std::uint32_t stack_top = 0x0801'0000;
    std::uint32_t stack_limit = 0x0001'0000;
    bool strip = false;
};

struct Map {
    struct Section {
        std::string name;
        std::uint32_t address = 0;
        std::uint32_t size = 0;
    };
    struct Symbol {
        std::string name;
        std::uint32_t address = 0;
        container::SymbolBind bind = container::SymbolBind::Local;
    };
    std::vector<Section> sections;
    std::vector<Symbol> symbols;
};

struct Error {
    std::string message;
};

[[nodiscard]] bool link(const std::vector<container::Image>& objects,
                        const Options& options,
                        container::Image& image,
                        Map& map,
                        Error& error);

} // namespace kr32::linker
