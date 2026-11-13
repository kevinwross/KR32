#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace kr32::core {
class Machine;
}

namespace kr32::container {

enum class ImageType : std::uint16_t {
    Object = 0,
    Executable = 1,
};

enum class SectionType : std::uint16_t {
    Other = 0,
    Text = 1,
    Rodata = 2,
    Data = 3,
    Bss = 4,
    Debug = 5,
};

enum SectionFlags : std::uint32_t {
    Alloc = 1U << 0,
    Write = 1U << 1,
    Execute = 1U << 2,
    Comdat = 1U << 3, // reserved for C++ COMDAT groups
};

enum class SymbolBind : std::uint8_t {
    Local = 0,
    Global = 1,
    Weak = 2,
};

enum class RelocationType : std::uint16_t {
    Abs32 = 1,
    PcRel32 = 2,
    Abs16 = 3,
};

inline constexpr std::uint16_t kUndefinedSection = 0xFFFFU;

struct Section {
    std::string name;
    SectionType type = SectionType::Other;
    std::uint32_t flags = 0;
    std::uint32_t alignment = 1;
    std::uint32_t address = 0;
    std::vector<std::uint8_t> data;
    std::uint32_t size = 0;
};

struct Symbol {
    std::string name;
    std::uint32_t value = 0;
    std::uint32_t size = 0;
    std::uint16_t section = kUndefinedSection;
    SymbolBind bind = SymbolBind::Local;
};

struct Relocation {
    std::uint32_t section = 0;
    std::uint32_t offset = 0;
    std::string symbol;
    std::uint32_t addend = 0;
    RelocationType type = RelocationType::Abs32;
};

struct Image {
    ImageType type = ImageType::Object;
    std::uint32_t entry = 0;
    std::uint32_t base = 0;
    std::vector<Section> sections;
    std::vector<Symbol> symbols;
    std::vector<Relocation> relocations;
};

struct Error {
    std::string message;
};

// `.tls` is a template, not a loadable region: the loader captures it and the
// caller installs it into the thread control block.
struct LoadedImage {
    std::vector<std::uint8_t> tls_template;
};

// Loads every allocated section of `image` into `machine` and zero-fills each
// `.bss` range. Returns false with `error` set when a section does not fit.
[[nodiscard]] bool load_into(core::Machine& machine, const Image& image,
                             LoadedImage& loaded, std::string& error);

[[nodiscard]] std::uint32_t crc32(const std::uint8_t* data, std::size_t size);

[[nodiscard]] bool write(const Image& image, std::vector<std::uint8_t>& bytes, Error& error);
[[nodiscard]] bool read(const std::vector<std::uint8_t>& bytes, Image& image, Error& error);

} // namespace kr32::container
