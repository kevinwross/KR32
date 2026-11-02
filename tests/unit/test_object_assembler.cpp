#include "doctest.h"

#include "kr32/asm/object_assembler.h"

#include <cstdint>
#include <string_view>

using namespace kr32;

namespace {

container::Image assemble(std::string_view source) {
    container::Image image;
    assembler::Error error;
    REQUIRE_MESSAGE(assembler::assemble_object(source, image, error), error.message);
    return image;
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

TEST_CASE("object assembler emits sections, symbols, and relocations") {
    const container::Image image = assemble(
        ".text\n"
        ".global _start\n"
        ".extern foo\n"
        "_start:\n"
        "    CALL foo\n"
        "    HALT\n"
        ".data\n"
        "value:\n"
        "    .word 0x12345678\n"
        ".rodata\n"
        "message:\n"
        "    .asciz \"hi\"\n"
        ".bss\n"
        "buffer:\n"
        "    .space 16\n");

    const container::Section* text = find_section(image, ".text");
    REQUIRE(text != nullptr);
    CHECK(text->type == container::SectionType::Text);
    CHECK(text->size == 12);

    const container::Section* data = find_section(image, ".data");
    REQUIRE(data != nullptr);
    CHECK(data->size == 4);
    CHECK(data->data[0] == 0x78);

    const container::Section* rodata = find_section(image, ".rodata");
    REQUIRE(rodata != nullptr);
    CHECK(rodata->data.size() == 3);

    const container::Section* bss = find_section(image, ".bss");
    REQUIRE(bss != nullptr);
    CHECK(bss->size == 16);

    const container::Symbol* start = find_symbol(image, "_start");
    REQUIRE(start != nullptr);
    CHECK(start->section == 0);
    CHECK(start->value == 0);
    CHECK(start->bind == container::SymbolBind::Global);

    const container::Symbol* foo = find_symbol(image, "foo");
    REQUIRE(foo != nullptr);
    CHECK(foo->section == container::kUndefinedSection);
    CHECK(foo->bind == container::SymbolBind::Global);

    REQUIRE(image.relocations.size() == 1);
    CHECK(image.relocations[0].section == 0);
    CHECK(image.relocations[0].offset == 4);
    CHECK(image.relocations[0].symbol == "foo");
    CHECK(image.relocations[0].type == container::RelocationType::Abs32);
}

TEST_CASE("object assembler relocates data references") {
    const container::Image image = assemble(
        ".text\n"
        ".global _start\n"
        "_start:\n"
        "    MOV R1, #value\n"
        "    HALT\n"
        ".data\n"
        "value:\n"
        "    .word value\n");
    REQUIRE(image.relocations.size() == 2);
    CHECK(image.relocations[0].symbol == "value");
    CHECK(image.relocations[1].symbol == "value");
    CHECK(image.relocations[1].addend == 0);
}

TEST_CASE("object assembler rejects duplicate labels and undefined directives") {
    container::Image image;
    assembler::Error error;
    CHECK_FALSE(assembler::assemble_object(
        "start:\nstart:\nHALT\n", image, error));
    CHECK(error.line == 2);

    CHECK_FALSE(assembler::assemble_object(
        ".unknown 1\n", image, error));
    CHECK(error.line == 1);
}
