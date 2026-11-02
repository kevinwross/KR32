#include "kr32/link/linker.h"

#include <algorithm>
#include <cstdint>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace kr32::linker {
namespace {

std::uint32_t align_up(std::uint32_t value, std::uint32_t alignment) {
    if (alignment <= 1) {
        return value;
    }
    return ((value + alignment - 1U) / alignment) * alignment;
}

int section_rank(const container::Section& section) {
    if (section.name == ".text") return 0;
    if (section.name == ".rodata") return 1;
    if (section.name == ".data") return 2;
    if (section.name == ".bss") return 3;
    if (section.type == container::SectionType::Debug) return 5;
    return 4;
}

void put32(container::Section& section, std::uint32_t offset, std::uint32_t value) {
    section.data[offset] = static_cast<std::uint8_t>(value & 0xFFU);
    section.data[offset + 1] = static_cast<std::uint8_t>((value >> 8) & 0xFFU);
    section.data[offset + 2] = static_cast<std::uint8_t>((value >> 16) & 0xFFU);
    section.data[offset + 3] = static_cast<std::uint8_t>((value >> 24) & 0xFFU);
}

void put16(container::Section& section, std::uint32_t offset, std::uint16_t value) {
    section.data[offset] = static_cast<std::uint8_t>(value & 0xFFU);
    section.data[offset + 1] = static_cast<std::uint8_t>((value >> 8) & 0xFFU);
}

} // namespace

bool link(const std::vector<container::Image>& objects,
          const Options& options,
          container::Image& image,
          Map& map,
          Error& error) {
    error = {};
    image = {};
    map = {};

    if (objects.empty()) {
        error = {"no input objects"};
        return false;
    }

    std::vector<container::Section> output_sections;
    std::map<std::string, std::size_t> section_index;
    std::vector<std::vector<std::size_t>> object_section_output(objects.size());
    std::vector<std::vector<std::uint32_t>> object_section_base(objects.size());

    auto find_section = [&](const std::string& name) -> int {
        const auto found = section_index.find(name);
        return found == section_index.end() ? -1 : static_cast<int>(found->second);
    };

    for (std::size_t object_index = 0; object_index < objects.size(); ++object_index) {
        const container::Image& object = objects[object_index];
        object_section_output[object_index].resize(object.sections.size());
        object_section_base[object_index].resize(object.sections.size());
        for (std::size_t section_number = 0; section_number < object.sections.size();
             ++section_number) {
            const container::Section& input = object.sections[section_number];
            int index = find_section(input.name);
            if (index < 0) {
                index = static_cast<int>(output_sections.size());
                section_index.emplace(input.name, static_cast<std::size_t>(index));
                container::Section created;
                created.name = input.name;
                created.type = input.type;
                created.flags = input.flags;
                created.alignment = input.alignment == 0 ? 1 : input.alignment;
                output_sections.push_back(std::move(created));
            }
            container::Section& output = output_sections[static_cast<std::size_t>(index)];
            output.flags |= input.flags;
            output.alignment = std::max(output.alignment, input.alignment == 0 ? 1 : input.alignment);
            object_section_output[object_index][section_number] =
                static_cast<std::size_t>(index);

            if (input.type == container::SectionType::Bss) {
                output.size = align_up(output.size, output.alignment);
                object_section_base[object_index][section_number] = output.size;
                output.size += input.size == 0 ? static_cast<std::uint32_t>(input.data.size())
                                               : input.size;
            } else {
                output.data.resize(align_up(static_cast<std::uint32_t>(output.data.size()),
                                             output.alignment),
                                   0);
                object_section_base[object_index][section_number] =
                    static_cast<std::uint32_t>(output.data.size());
                output.data.insert(output.data.end(), input.data.begin(), input.data.end());
                output.size = static_cast<std::uint32_t>(output.data.size());
            }
        }
    }

    std::vector<std::size_t> order(output_sections.size());
    for (std::size_t index = 0; index < order.size(); ++index) {
        order[index] = index;
    }
    std::stable_sort(order.begin(), order.end(), [&](std::size_t left, std::size_t right) {
        return section_rank(output_sections[left]) < section_rank(output_sections[right]);
    });

    std::uint32_t cursor = options.base;
    std::uint32_t first_text = 0;
    bool have_text = false;
    for (std::size_t index : order) {
        container::Section& section = output_sections[index];
        if ((section.flags & container::Alloc) == 0) {
            section.address = 0;
            continue;
        }
        cursor = align_up(cursor, section.alignment == 0 ? 1 : section.alignment);
        section.address = cursor;
        cursor += section.size;
        if (section.type == container::SectionType::Text && !have_text) {
            first_text = section.address;
            have_text = true;
        }
    }

    struct Global {
        std::uint32_t address = 0;
        container::SymbolBind bind = container::SymbolBind::Global;
    };
    std::map<std::string, Global> globals;
    std::vector<container::Symbol> output_symbols;

    auto add_global = [&](const std::string& name, std::uint32_t address,
                          container::SymbolBind bind) -> bool {
        const auto found = globals.find(name);
        if (found == globals.end()) {
            globals.emplace(name, Global{address, bind});
            return true;
        }
        if (found->second.bind == container::SymbolBind::Weak &&
            bind == container::SymbolBind::Global) {
            found->second = {address, bind};
            return true;
        }
        if (bind == container::SymbolBind::Weak) {
            return true;
        }
        error = {"duplicate global symbol: " + name};
        return false;
    };

    for (std::size_t object_index = 0; object_index < objects.size(); ++object_index) {
        const container::Image& object = objects[object_index];
        for (const container::Symbol& symbol : object.symbols) {
            if (symbol.section == container::kUndefinedSection) {
                continue;
            }
            if (symbol.section >= object.sections.size()) {
                error = {"symbol section out of range: " + symbol.name};
                return false;
            }
            const std::size_t output_index =
                object_section_output[object_index][symbol.section];
            const std::uint32_t address =
                output_sections[output_index].address +
                object_section_base[object_index][symbol.section] + symbol.value;
            if (symbol.bind == container::SymbolBind::Global ||
                symbol.bind == container::SymbolBind::Weak) {
                if (!add_global(symbol.name, address, symbol.bind)) {
                    return false;
                }
            }
            if (symbol.bind == container::SymbolBind::Global ||
                symbol.bind == container::SymbolBind::Weak || !options.strip) {
                output_symbols.push_back(
                    {symbol.name, address, symbol.size,
                     static_cast<std::uint16_t>(output_index), symbol.bind});
            }
        }
    }

    std::uint32_t heap_start = cursor;
    heap_start = align_up(heap_start, 16);
    std::uint32_t heap_end = options.stack_top > 0x0001'0000U
                                 ? options.stack_top - 0x0001'0000U
                                 : options.stack_top;
    heap_end &= ~0xFU;
    if (heap_end < heap_start) {
        heap_end = heap_start;
    }
    if (!add_global("__heap_start", heap_start, container::SymbolBind::Global)) {
        return false;
    }
    if (!add_global("__heap_end", heap_end, container::SymbolBind::Global)) {
        return false;
    }
    if (!add_global("__stack_top", options.stack_top, container::SymbolBind::Global) ||
        !add_global("__stack_limit", options.stack_limit, container::SymbolBind::Global)) {
        return false;
    }
    output_symbols.push_back({"__heap_start", heap_start, 0, 0,
                              container::SymbolBind::Global});
    output_symbols.push_back({"__heap_end", heap_end, 0, 0,
                              container::SymbolBind::Global});
    output_symbols.push_back({"__stack_top", options.stack_top, 0, 0,
                              container::SymbolBind::Global});
    output_symbols.push_back({"__stack_limit", options.stack_limit, 0, 0,
                              container::SymbolBind::Global});

    const auto start = globals.find("_start");
    image.entry = start != globals.end() ? start->second.address : first_text;

    for (std::size_t object_index = 0; object_index < objects.size(); ++object_index) {
        const container::Image& object = objects[object_index];
        for (const container::Relocation& relocation : object.relocations) {
            if (relocation.section >= object.sections.size()) {
                error = {"relocation section out of range"};
                return false;
            }
            const std::size_t output_index =
                object_section_output[object_index][relocation.section];
            container::Section& section = output_sections[output_index];
            if (section.type == container::SectionType::Bss) {
                error = {"cannot relocate a .bss section"};
                return false;
            }
            const std::uint32_t patch =
                object_section_base[object_index][relocation.section] + relocation.offset;
            if (patch + 4U > section.data.size()) {
                error = {"relocation offset out of range"};
                return false;
            }

            const auto target = globals.find(relocation.symbol);
            std::uint32_t symbol_address = 0;
            if (target != globals.end()) {
                symbol_address = target->second.address;
            } else {
                bool found = false;
                for (const container::Symbol& symbol : object.symbols) {
                    if (symbol.name == relocation.symbol &&
                        symbol.section != container::kUndefinedSection) {
                        symbol_address = output_sections[object_section_output[object_index]
                                                                         [symbol.section]]
                                                 .address +
                                         object_section_base[object_index][symbol.section] +
                                         symbol.value;
                        found = true;
                        break;
                    }
                }
                if (!found) {
                    error = {"undefined symbol: " + relocation.symbol};
                    return false;
                }
            }

            const std::uint32_t place = section.address + patch;
            if (relocation.type == container::RelocationType::Abs32) {
                put32(section, patch, symbol_address + relocation.addend);
            } else if (relocation.type == container::RelocationType::PcRel32) {
                put32(section, patch, symbol_address + relocation.addend - place);
            } else if (relocation.type == container::RelocationType::Abs16) {
                put16(section, patch, static_cast<std::uint16_t>(symbol_address +
                                                                 relocation.addend));
            } else {
                error = {"unsupported relocation type"};
                return false;
            }
        }
    }

    image.type = container::ImageType::Executable;
    image.base = options.base;
    image.sections = std::move(output_sections);
    image.symbols = std::move(output_symbols);
    image.relocations.clear();

    for (const container::Section& section : image.sections) {
        map.sections.push_back({section.name, section.address, section.size});
    }
    std::sort(map.symbols.begin(), map.symbols.end(),
              [](const Map::Symbol& left, const Map::Symbol& right) {
                  return left.address < right.address;
              });
    for (const auto& [name, global] : globals) {
        map.symbols.push_back({name, global.address, global.bind});
    }
    std::sort(map.symbols.begin(), map.symbols.end(),
              [](const Map::Symbol& left, const Map::Symbol& right) {
                  if (left.address != right.address) {
                      return left.address < right.address;
                  }
                  return left.name < right.name;
              });
    return true;
}

} // namespace kr32::linker
