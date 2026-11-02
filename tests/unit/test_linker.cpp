#include "doctest.h"

#include "kr32/asm/object_assembler.h"
#include "kr32/container/container.h"
#include "kr32/link/linker.h"

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
