#include "doctest.h"

#include "kr32/asm/object_assembler.h"
#include "kr32/container/container.h"
#include "kr32/link/linker.h"

#include <algorithm>
#include <cstdint>
#include <string_view>
#include <vector>

using namespace kr32;

namespace {

container::Image assemble(std::string_view source) {
    container::Image image;
    assembler::Error error;
    REQUIRE_MESSAGE(assembler::assemble_object(source, image, error), error.message);
    return image;
}

std::uint32_t read32(const container::Section& section, std::uint32_t offset) {
    return static_cast<std::uint32_t>(section.data[offset]) |
           (static_cast<std::uint32_t>(section.data[offset + 1]) << 8) |
           (static_cast<std::uint32_t>(section.data[offset + 2]) << 16) |
           (static_cast<std::uint32_t>(section.data[offset + 3]) << 24);
}

const container::Section* find_section(const container::Image& image, const std::string& name) {
    for (const container::Section& section : image.sections) {
        if (section.name == name) {
            return &section;
        }
    }
    return nullptr;
}

const container::Symbol* find_symbol(const container::Image& image, const std::string& name) {
    for (const container::Symbol& symbol : image.symbols) {
        if (symbol.name == name) {
            return &symbol;
        }
    }
    return nullptr;
}

} // namespace

TEST_CASE("the linker resolves a two-object call and reports a map") {
    const container::Image start = assemble(
        ".text\n"
        ".global _start\n"
        ".extern foo\n"
        "_start:\n"
        "    CALL foo\n"
        "    HALT\n");
    const container::Image function = assemble(
        ".text\n"
        ".global foo\n"
        "foo:\n"
        "    MOV R0, #7\n"
        "    RET\n");

    container::Image image;
    linker::Map map;
    linker::Error error;
    REQUIRE_MESSAGE(linker::link({start, function}, linker::Options{}, image, map, error),
                    error.message);

    CHECK(image.type == container::ImageType::Executable);
    CHECK(image.entry == 0x0001'0000U);
    const container::Section* text = find_section(image, ".text");
    REQUIRE(text != nullptr);
    CHECK(text->address == 0x0001'0000U);
    CHECK(text->size == 20);
    CHECK(read32(*text, 4) == 0x0001'000CU);

    const container::Symbol* entry = find_symbol(image, "_start");
    REQUIRE(entry != nullptr);
    CHECK(entry->value == 0x0001'0000U);
    const container::Symbol* function_symbol = find_symbol(image, "foo");
    REQUIRE(function_symbol != nullptr);
    CHECK(function_symbol->value == 0x0001'000CU);

    CHECK(map.sections.size() >= 1);
    bool found_start = false;
    bool found_function = false;
    for (const linker::Map::Symbol& symbol : map.symbols) {
        if (symbol.name == "_start") found_start = true;
        if (symbol.name == "foo") found_function = true;
    }
    CHECK(found_start);
    CHECK(found_function);
}

TEST_CASE("the linker reports undefined and duplicate global symbols") {
    const container::Image unresolved = assemble(
        ".text\n"
        ".global _start\n"
        "_start:\n"
        "    CALL missing\n"
        "    HALT\n");
    container::Image image;
    linker::Map map;
    linker::Error error;
    CHECK_FALSE(linker::link({unresolved}, linker::Options{}, image, map, error));
    CHECK(error.message.find("undefined") != std::string::npos);

    const container::Image first = assemble(
        ".text\n"
        ".global _start\n"
        "_start:\n"
        "    HALT\n");
    const container::Image duplicate = assemble(
        ".text\n"
        ".global _start\n"
        "_start:\n"
        "    HALT\n");
    CHECK_FALSE(linker::link({first, duplicate}, linker::Options{}, image, map, error));
    CHECK(error.message.find("duplicate") != std::string::npos);
}

TEST_CASE("the linker can strip local symbols") {
    const container::Image object = assemble(
        ".text\n"
        ".global _start\n"
        "_start:\n"
        "    HALT\n"
        "helper:\n"
        "    NOP\n");
    container::Image image;
    linker::Map map;
    linker::Error error;
    linker::Options options;
    options.strip = true;
    REQUIRE_MESSAGE(linker::link({object}, options, image, map, error), error.message);
    CHECK(find_symbol(image, "_start") != nullptr);
    CHECK(find_symbol(image, "helper") == nullptr);
    CHECK(find_symbol(image, "__heap_start") != nullptr);
    CHECK(find_symbol(image, "__heap_end") != nullptr);
}

TEST_CASE("comdat data and bss sections keep one copy") {
    const container::Image first = assemble(
        ".text\n"
        ".global _start\n"
        "_start:\n"
        "    HALT\n"
        ".section .data.shared\n"
        ".weak value\n"
        "value:\n"
        "    .word 7\n"
        ".section .bss.shared\n"
        ".weak scratch\n"
        "scratch:\n"
        "    .space 4\n");
    const container::Image second = assemble(
        ".section .data.shared\n"
        ".weak value\n"
        "value:\n"
        "    .word 9\n"
        ".section .bss.shared\n"
        ".weak scratch\n"
        "scratch:\n"
        "    .space 4\n");

    container::Image image;
    linker::Map map;
    linker::Error error;
    REQUIRE_MESSAGE(linker::link({first, second}, linker::Options{}, image, map, error),
                    error.message);

    const container::Section* data = find_section(image, ".data.shared");
    REQUIRE(data != nullptr);
    CHECK(data->size == 4);
    const container::Section* bss = find_section(image, ".bss.shared");
    REQUIRE(bss != nullptr);
    CHECK(bss->size == 4);
}

TEST_CASE("comdat sections keep one copy across objects") {
    const container::Image first = assemble(
        ".text\n"
        ".global _start\n"
        "_start:\n"
        "    HALT\n"
        ".section .text.shared\n"
        ".weak shared\n"
        "shared:\n"
        "    MOV R0, #1\n"
        "    RET\n");
    const container::Image second = assemble(
        ".section .text.shared\n"
        ".weak shared\n"
        "shared:\n"
        "    MOV R0, #2\n"
        "    RET\n");

    container::Image image;
    linker::Map map;
    linker::Error error;
    REQUIRE_MESSAGE(linker::link({first, second}, linker::Options{}, image, map, error),
                    error.message);

    const container::Section* shared = find_section(image, ".text.shared");
    REQUIRE(shared != nullptr);
    CHECK(shared->size == 8);
    CHECK((shared->flags & container::Comdat) != 0);
}

TEST_CASE("flat output starts at the requested base and zero-fills BSS") {
    const container::Image object = assemble(
        ".text\n"
        ".global _start\n"
        "_start:\n"
        "    MOV R0, #7\n"
        "    HALT\n"
        ".data\n"
        ".global value\n"
        "value:\n"
        "    .word 0x55667788\n"
        ".bss\n"
        ".global scratch\n"
        "scratch:\n"
        "    .space 8\n");
    container::Image executable;
    linker::Map map;
    linker::Error link_error;
    linker::Options options;
    options.base = 0x0002'0000;
    REQUIRE_MESSAGE(linker::link({object}, options, executable, map, link_error),
                    link_error.message);

    const container::Section* text = find_section(executable, ".text");
    const container::Section* data = find_section(executable, ".data");
    const container::Section* bss = find_section(executable, ".bss");
    REQUIRE(text != nullptr);
    REQUIRE(data != nullptr);
    REQUIRE(bss != nullptr);

    std::vector<std::uint8_t> flat;
    linker::Error flat_error;
    REQUIRE_MESSAGE(linker::flatten(executable, options.base, flat, flat_error),
                    flat_error.message);
    CHECK(text->address == 0x0002'0000U);
    CHECK(flat.size() == bss->address + bss->size - options.base);
    CHECK(std::equal(text->data.begin(), text->data.end(), flat.begin()));
    CHECK(std::equal(data->data.begin(), data->data.end(),
                     flat.begin() + (data->address - options.base)));
    for (std::uint32_t offset = 0; offset < bss->size; ++offset) {
        CHECK(flat[bss->address - options.base + offset] == 0);
    }

    std::vector<std::uint8_t> rejected;
    linker::Error reject_error;
    CHECK_FALSE(linker::flatten(executable, options.base + 4, rejected, reject_error));
    CHECK(reject_error.message.find("below the flat base") != std::string::npos);
}

TEST_CASE("flat output rejects an entry point outside the image") {
    container::Image executable;
    executable.entry = 0x3000;
    container::Section section;
    section.name = ".text";
    section.type = container::SectionType::Text;
    section.flags = container::Alloc;
    section.address = 0x2000;
    section.size = 4;
    section.data = {1, 2, 3, 4};
    executable.sections.push_back(section);

    std::vector<std::uint8_t> flat;
    linker::Error error;
    CHECK_FALSE(linker::flatten(executable, 0x2000, flat, error));
    CHECK(error.message.find("entry point") != std::string::npos);
}

TEST_CASE("a linked executable round-trips through the container") {
    const container::Image object = assemble(
        ".text\n"
        ".global _start\n"
        "_start:\n"
        "    MOV R0, #7\n"
        "    HALT\n");
    container::Image executable;
    linker::Map map;
    linker::Error link_error;
    REQUIRE(linker::link({object}, linker::Options{}, executable, map, link_error));

    std::vector<std::uint8_t> bytes;
    container::Error container_error;
    REQUIRE_MESSAGE(container::write(executable, bytes, container_error),
                    container_error.message);
    container::Image decoded;
    REQUIRE_MESSAGE(container::read(bytes, decoded, container_error),
                    container_error.message);
    CHECK(decoded.type == container::ImageType::Executable);
    CHECK(decoded.entry == executable.entry);
    CHECK(decoded.sections.size() == executable.sections.size());
}
TEST_CASE("the linker aligns sections that hold eight-byte data") {
    // One 4-byte instruction leaves .text 4-mod-8, so a section alignment of 4
    // would place the double constant at a 4-byte boundary.
    const container::Image object = assemble(
        ".text\n"
        ".global main\n"
        "main:\n"
        "    RET\n"
        ".rodata\n"
        ".global rconst\n"
        "rconst:\n"
        "    .double 2.0\n"
        ".data\n"
        ".global gdouble\n"
        "gdouble:\n"
        "    .double 3.0\n"
        ".bss\n"
        ".global gzero\n"
        "gzero:\n"
        "    .space 8\n");

    container::Image linked;
    linker::Map map;
    linker::Error error;
    const std::vector<container::Image> inputs{object};
    REQUIRE_MESSAGE(
        linker::link(inputs, linker::Options{}, linked, map, error),
        error.message);

    for (const char* name : {"rconst", "gdouble", "gzero"}) {
        const container::Symbol* symbol = find_symbol(linked, name);
        REQUIRE(symbol != nullptr);
        INFO(name);
        CHECK((symbol->value % 8U) == 0U);
    }
}