#include "kr32/container/container.h"

#include <algorithm>
#include <cstring>
#include <iterator>
#include <map>
#include <utility>

namespace kr32::container {
namespace {

constexpr std::uint8_t kMagic[4] = {'K', '3', '2', 0};
constexpr std::uint16_t kVersion = 1;
constexpr std::uint16_t kHeaderSize = 64;
constexpr std::size_t kSectionEntrySize = 32;
constexpr std::size_t kSymbolEntrySize = 20;
constexpr std::size_t kRelocationEntrySize = 20;

void put16(std::vector<std::uint8_t>& bytes, std::size_t offset, std::uint16_t value) {
    bytes[offset] = static_cast<std::uint8_t>(value & 0xFFU);
    bytes[offset + 1] = static_cast<std::uint8_t>((value >> 8) & 0xFFU);
}

void put32(std::vector<std::uint8_t>& bytes, std::size_t offset, std::uint32_t value) {
    bytes[offset] = static_cast<std::uint8_t>(value & 0xFFU);
    bytes[offset + 1] = static_cast<std::uint8_t>((value >> 8) & 0xFFU);
    bytes[offset + 2] = static_cast<std::uint8_t>((value >> 16) & 0xFFU);
    bytes[offset + 3] = static_cast<std::uint8_t>((value >> 24) & 0xFFU);
}

std::uint16_t get16(const std::vector<std::uint8_t>& bytes, std::size_t offset) {
    return static_cast<std::uint16_t>(bytes[offset]) |
           static_cast<std::uint16_t>(bytes[offset + 1] << 8);
}

std::uint32_t get32(const std::vector<std::uint8_t>& bytes, std::size_t offset) {
    return static_cast<std::uint32_t>(bytes[offset]) |
           (static_cast<std::uint32_t>(bytes[offset + 1]) << 8) |
           (static_cast<std::uint32_t>(bytes[offset + 2]) << 16) |
           (static_cast<std::uint32_t>(bytes[offset + 3]) << 24);
}

bool add_string(std::vector<std::uint8_t>& strings,
                std::map<std::string, std::uint32_t>& offsets,
                const std::string& name,
                std::uint32_t& offset,
                Error& error) {
    if (name.empty()) {
        offset = 0;
        return true;
    }
    const auto found = offsets.find(name);
    if (found != offsets.end()) {
        offset = found->second;
        return true;
    }
    if (strings.size() > 0xFFFF'FFFFU - name.size() - 1) {
        error = {"string table overflow"};
        return false;
    }
    offset = static_cast<std::uint32_t>(strings.size());
    strings.insert(strings.end(), name.begin(), name.end());
    strings.push_back(0);
    offsets.emplace(name, offset);
    return true;
}

const std::string* string_at(const std::vector<std::uint8_t>& strings,
                             std::size_t offset,
                             std::string& storage,
                             Error& error) {
    if (offset >= strings.size()) {
        error = {"string offset out of range"};
        return nullptr;
    }
    std::size_t end = offset;
    while (end < strings.size() && strings[end] != 0) {
        ++end;
    }
    if (end >= strings.size()) {
        error = {"unterminated string"};
        return nullptr;
    }
    storage.assign(reinterpret_cast<const char*>(strings.data() + offset), end - offset);
    return &storage;
}

} // namespace

std::uint32_t crc32(const std::uint8_t* data, std::size_t size) {
    std::uint32_t crc = 0xFFFF'FFFFU;
    for (std::size_t index = 0; index < size; ++index) {
        crc ^= data[index];
        for (int bit = 0; bit < 8; ++bit) {
            const std::uint32_t mask = static_cast<std::uint32_t>(-(static_cast<int>(crc & 1U)));
            crc = (crc >> 1) ^ (0xEDB8'8320U & mask);
        }
    }
    return ~crc;
}

bool write(const Image& image, std::vector<std::uint8_t>& bytes, Error& error) {
    error = {};
    bytes.clear();

    if (image.sections.size() > 0xFFFF'FFFFU || image.symbols.size() > 0xFFFF'FFFFU ||
        image.relocations.size() > 0xFFFF'FFFFU) {
        error = {"too many container entries"};
        return false;
    }

    std::vector<std::uint8_t> strings(1, 0);
    std::map<std::string, std::uint32_t> string_offsets;
    std::vector<std::uint32_t> section_name_offsets(image.sections.size());
    std::vector<std::uint32_t> symbol_name_offsets(image.symbols.size());
    std::vector<std::uint32_t> relocation_name_offsets(image.relocations.size());

    for (std::size_t index = 0; index < image.sections.size(); ++index) {
        const Section& section = image.sections[index];
        if (section.flags > 0xFFFFU) {
            error = {"section flags out of range: " + section.name};
            return false;
        }
        if (section.type != SectionType::Bss && section.data.size() != section.size &&
            section.size != 0) {
            error = {"section size mismatch: " + section.name};
            return false;
        }
        if (!add_string(strings, string_offsets, section.name, section_name_offsets[index], error)) {
            return false;
        }
    }
    for (std::size_t index = 0; index < image.symbols.size(); ++index) {
        const Symbol& symbol = image.symbols[index];
        if (symbol.section != kUndefinedSection && symbol.section >= image.sections.size()) {
            error = {"symbol section out of range: " + symbol.name};
            return false;
        }
        if (!add_string(strings, string_offsets, symbol.name, symbol_name_offsets[index], error)) {
            return false;
        }
    }
    for (std::size_t index = 0; index < image.relocations.size(); ++index) {
        const Relocation& relocation = image.relocations[index];
        if (relocation.section >= image.sections.size()) {
            error = {"relocation section out of range"};
            return false;
        }
        if (!add_string(strings, string_offsets, relocation.symbol,
                        relocation_name_offsets[index], error)) {
            return false;
        }
    }

    const std::size_t table_size = kHeaderSize +
                                   image.sections.size() * kSectionEntrySize +
                                   image.symbols.size() * kSymbolEntrySize +
                                   image.relocations.size() * kRelocationEntrySize;
    const std::size_t strings_offset = table_size;
    auto align_up = [](std::size_t value, std::size_t alignment) {
        if (alignment <= 1) {
            return value;
        }
        return ((value + alignment - 1U) / alignment) * alignment;
    };
    std::size_t cursor = align_up(strings_offset + strings.size(), 16);

    std::vector<std::uint32_t> data_offsets(image.sections.size(), 0);
    for (std::size_t index = 0; index < image.sections.size(); ++index) {
        const Section& section = image.sections[index];
        if (section.type == SectionType::Bss) {
            continue;
        }
        const std::uint32_t alignment = section.alignment == 0 ? 1 : section.alignment;
        cursor = align_up(cursor, alignment);
        data_offsets[index] = static_cast<std::uint32_t>(cursor);
        cursor += section.data.size();
    }

    if (cursor > 0xFFFF'FFFFU) {
        error = {"container too large"};
        return false;
    }

    bytes.assign(cursor, 0);
    std::copy(kMagic, kMagic + 4, bytes.begin());
    put16(bytes, 4, kVersion);
    put16(bytes, 6, static_cast<std::uint16_t>(image.type));
    put16(bytes, 8, kHeaderSize);
    put16(bytes, 10, 0);
    put32(bytes, 12, static_cast<std::uint32_t>(image.sections.size()));
    put32(bytes, 16, static_cast<std::uint32_t>(image.symbols.size()));
    put32(bytes, 20, static_cast<std::uint32_t>(image.relocations.size()));
    put32(bytes, 24, image.entry);
    put32(bytes, 28, image.base);
    put32(bytes, 32, static_cast<std::uint32_t>(strings_offset));
    put32(bytes, 36, static_cast<std::uint32_t>(strings.size()));
    put32(bytes, 40, static_cast<std::uint32_t>(bytes.size()));
    put32(bytes, 44, 0);
    std::fill(bytes.begin() + 48, bytes.begin() + 64, std::uint8_t{0});

    std::size_t entry = kHeaderSize;
    for (std::size_t index = 0; index < image.sections.size(); ++index) {
        const Section& section = image.sections[index];
        put32(bytes, entry + 0, section_name_offsets[index]);
        put16(bytes, entry + 4, static_cast<std::uint16_t>(section.type));
        put16(bytes, entry + 6, static_cast<std::uint16_t>(section.flags));
        put32(bytes, entry + 8, section.address);
        put32(bytes, entry + 12, section.type == SectionType::Bss ? 0 : data_offsets[index]);
        put32(bytes, entry + 16,
              section.type == SectionType::Bss
                  ? (section.size == 0 ? static_cast<std::uint32_t>(section.data.size())
                                       : section.size)
                  : static_cast<std::uint32_t>(section.data.size()));
        put32(bytes, entry + 20, section.alignment == 0 ? 1 : section.alignment);
        std::fill(bytes.begin() + static_cast<std::ptrdiff_t>(entry + 24),
                  bytes.begin() + static_cast<std::ptrdiff_t>(entry + 32),
                  std::uint8_t{0});
        entry += kSectionEntrySize;
    }

    for (std::size_t index = 0; index < image.symbols.size(); ++index) {
        const Symbol& symbol = image.symbols[index];
        put32(bytes, entry + 0, symbol_name_offsets[index]);
        put32(bytes, entry + 4, symbol.value);
        put32(bytes, entry + 8, symbol.size);
        put16(bytes, entry + 12, symbol.section);
        bytes[entry + 14] = static_cast<std::uint8_t>(symbol.bind);
        bytes[entry + 15] = 0;
        put32(bytes, entry + 16, 0);
        entry += kSymbolEntrySize;
    }

    for (std::size_t index = 0; index < image.relocations.size(); ++index) {
        const Relocation& relocation = image.relocations[index];
        put32(bytes, entry + 0, relocation.section);
        put32(bytes, entry + 4, relocation.offset);
        put32(bytes, entry + 8, relocation_name_offsets[index]);
        put32(bytes, entry + 12, relocation.addend);
        put16(bytes, entry + 16, static_cast<std::uint16_t>(relocation.type));
        put16(bytes, entry + 18, 0);
        entry += kRelocationEntrySize;
    }

    std::copy(strings.begin(), strings.end(), bytes.begin() + static_cast<std::ptrdiff_t>(strings_offset));
    for (std::size_t index = 0; index < image.sections.size(); ++index) {
        if (image.sections[index].type == SectionType::Bss) {
            continue;
        }
        std::copy(image.sections[index].data.begin(), image.sections[index].data.end(),
                  bytes.begin() + static_cast<std::ptrdiff_t>(data_offsets[index]));
    }

    const std::uint32_t checksum = crc32(bytes.data() + kHeaderSize, bytes.size() - kHeaderSize);
    put32(bytes, 44, checksum);
    return true;
}

bool read(const std::vector<std::uint8_t>& bytes, Image& image, Error& error) {
    error = {};
    image = {};

    if (bytes.size() < kHeaderSize) {
        error = {"container is shorter than its header"};
        return false;
    }
    if (!std::equal(std::begin(kMagic), std::end(kMagic), bytes.begin())) {
        error = {"bad container magic"};
        return false;
    }
    if (get16(bytes, 4) != kVersion) {
        error = {"unsupported container version"};
        return false;
    }
    if (get16(bytes, 8) != kHeaderSize) {
        error = {"unsupported header size"};
        return false;
    }
    const std::uint32_t file_size = get32(bytes, 40);
    if (file_size != bytes.size()) {
        error = {"container file size mismatch"};
        return false;
    }
    const std::uint32_t section_count = get32(bytes, 12);
    const std::uint32_t symbol_count = get32(bytes, 16);
    const std::uint32_t relocation_count = get32(bytes, 20);
    const std::size_t table_size = kHeaderSize +
                                   static_cast<std::size_t>(section_count) * kSectionEntrySize +
                                   static_cast<std::size_t>(symbol_count) * kSymbolEntrySize +
                                   static_cast<std::size_t>(relocation_count) * kRelocationEntrySize;
    if (table_size > bytes.size()) {
        error = {"container tables exceed the file"};
        return false;
    }
    const std::uint32_t strings_offset = get32(bytes, 32);
    const std::uint32_t strings_size = get32(bytes, 36);
    if (strings_offset > bytes.size() || strings_size > bytes.size() - strings_offset) {
        error = {"container string table out of range"};
        return false;
    }
    const std::vector<std::uint8_t> strings(bytes.begin() + strings_offset,
                                            bytes.begin() + strings_offset + strings_size);
    const std::uint32_t checksum = get32(bytes, 44);
    if (crc32(bytes.data() + kHeaderSize, bytes.size() - kHeaderSize) != checksum) {
        error = {"container checksum mismatch"};
        return false;
    }

    image.type = static_cast<ImageType>(get16(bytes, 6));
    image.entry = get32(bytes, 24);
    image.base = get32(bytes, 28);

    std::size_t entry = kHeaderSize;
    image.sections.reserve(section_count);
    for (std::uint32_t index = 0; index < section_count; ++index) {
        Section section;
        std::string storage;
        const std::string* name = string_at(strings, get32(bytes, entry + 0), storage, error);
        if (name == nullptr) {
            return false;
        }
        section.name = *name;
        section.type = static_cast<SectionType>(get16(bytes, entry + 4));
        section.flags = get16(bytes, entry + 6);
        section.address = get32(bytes, entry + 8);
        const std::uint32_t data_offset = get32(bytes, entry + 12);
        const std::uint32_t size = get32(bytes, entry + 16);
        section.alignment = get32(bytes, entry + 20);
        section.size = size;
        if (section.type != SectionType::Bss) {
            if (data_offset > bytes.size() || size > bytes.size() - data_offset) {
                error = {"section data out of range: " + section.name};
                return false;
            }
            section.data.assign(bytes.begin() + data_offset, bytes.begin() + data_offset + size);
        }
        image.sections.push_back(std::move(section));
        entry += kSectionEntrySize;
    }

    image.symbols.reserve(symbol_count);
    for (std::uint32_t index = 0; index < symbol_count; ++index) {
        Symbol symbol;
        std::string storage;
        const std::string* name = string_at(strings, get32(bytes, entry + 0), storage, error);
        if (name == nullptr) {
            return false;
        }
        symbol.name = *name;
        symbol.value = get32(bytes, entry + 4);
        symbol.size = get32(bytes, entry + 8);
        symbol.section = get16(bytes, entry + 12);
        symbol.bind = static_cast<SymbolBind>(bytes[entry + 14]);
        if (symbol.section != kUndefinedSection && symbol.section >= section_count) {
            error = {"symbol section out of range: " + symbol.name};
            return false;
        }
        image.symbols.push_back(std::move(symbol));
        entry += kSymbolEntrySize;
    }

    image.relocations.reserve(relocation_count);
    for (std::uint32_t index = 0; index < relocation_count; ++index) {
        Relocation relocation;
        std::string storage;
        const std::string* name = string_at(strings, get32(bytes, entry + 8), storage, error);
        if (name == nullptr) {
            return false;
        }
        relocation.section = get32(bytes, entry + 0);
        relocation.offset = get32(bytes, entry + 4);
        relocation.symbol = *name;
        relocation.addend = get32(bytes, entry + 12);
        relocation.type = static_cast<RelocationType>(get16(bytes, entry + 16));
        if (relocation.section >= section_count) {
            error = {"relocation section out of range"};
            return false;
        }
        image.relocations.push_back(std::move(relocation));
        entry += kRelocationEntrySize;
    }

    return true;
}

} // namespace kr32::container
