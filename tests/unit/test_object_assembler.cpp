#include "doctest.h"

#include "kr32/asm/object_assembler.h"

#include <cstdint>
#include <string_view>
#include <vector>

using namespace kr32;

namespace {

std::uint32_t word_at(const std::vector<std::uint8_t>& bytes, std::size_t offset) {
    return static_cast<std::uint32_t>(bytes[offset]) |
           (static_cast<std::uint32_t>(bytes[offset + 1]) << 8) |
           (static_cast<std::uint32_t>(bytes[offset + 2]) << 16) |
           (static_cast<std::uint32_t>(bytes[offset + 3]) << 24);
}

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

TEST_CASE("object assembler emits float and double data and FP instructions") {
    const container::Image image = assemble(
        ".text\n"
        ".global _start\n"
        "_start:\n"
        "    FADD.S F1, F2, F3\n"
        ".data\n"
        "fvalue:\n"
        "    .float 1.5\n"
        ".align 8\n"
        "dvalue:\n"
        "    .double 2.5\n");

    const container::Section* text = find_section(image, ".text");
    REQUIRE(text != nullptr);
    REQUIRE(text->data.size() == 4);
    CHECK(word_at(text->data, 0) == 0xC024'6000);

    const container::Section* data = find_section(image, ".data");
    REQUIRE(data != nullptr);
    CHECK(data->data.size() == 16);
    CHECK(word_at(data->data, 0) == 0x3FC0'0000);
    CHECK(word_at(data->data, 4) == 0);
    CHECK(word_at(data->data, 8) == 0x0000'0000);
    CHECK(word_at(data->data, 12) == 0x4004'0000);
}

TEST_CASE("object assembler emits tile and transform-stack instructions") {
    const container::Image image = assemble(
        ".text\n"
        "_start:\n"
        "    MMUL T1, T2, T3\n"
        "    VDOT F0, T1, T2\n"
        "    MPUSH T1\n"
        "    MPOP T2\n"
        "    MDROP\n"
        "    MLOAD T1, [R0]\n"
        "    VLOAD T2, [R1]\n");
    const container::Section* text = find_section(image, ".text");
    REQUIRE(text != nullptr);
    REQUIRE(text->data.size() == 7 * 4);
    CHECK(word_at(text->data, 0) == 0xC224'6000);
    CHECK(word_at(text->data, 4) == 0xC202'4004);
    CHECK(word_at(text->data, 8) == 0xC220'0005);
    CHECK(word_at(text->data, 12) == 0xC240'0006);
    CHECK(word_at(text->data, 16) == 0xC200'0007);
    CHECK((word_at(text->data, 20) >> 25) == 0x52);
    CHECK((word_at(text->data, 24) >> 25) == 0x52);
}

TEST_CASE("object assembler emits atomics") {
    const container::Image image = assemble(
        ".text\n"
        "_start:\n"
        "    LL R1, [R2]\n"
        "    SC R1, [R2]\n"
        "    FENCE\n");
    const container::Section* text = find_section(image, ".text");
    REQUIRE(text != nullptr);
    REQUIRE(text->data.size() == 3 * 4);
    CHECK(word_at(text->data, 0) == 0x8C24'4800);
    CHECK(word_at(text->data, 4) == 0x8E24'4800);
    CHECK(word_at(text->data, 8) == 0x9000'0000);
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

TEST_CASE("object assembler emits source line records for assembly") {
    const container::Image image = assemble(
        ".file \"kr32os/bios/entry.S\"\n"
        ".text\n"
        ".global entry\n"
        "entry:\n"
        "    MOV R0, #1\n"
        "    HALT\n");
    const container::Section* debug = find_section(image, ".debug_info");
    REQUIRE(debug != nullptr);
    const std::string text(debug->data.begin(), debug->data.end());
    CHECK(text.find("kr32os/bios/entry.S") != std::string::npos);
    CHECK(text.find("L .Lsource_") != std::string::npos);
    bool has_source_symbol = false;
    for (const container::Symbol& symbol : image.symbols) {
        has_source_symbol =
            has_source_symbol || symbol.name.rfind(".Lsource_", 0) == 0;
    }
    CHECK(has_source_symbol);
}

TEST_CASE("object assembler encodes bulk stack operations") {
    const container::Image image = assemble(
        "PUSHR\n"
        "POPR\n"
        "PUSHFP\n"
        "POPFP\n");
    const container::Section* text = find_section(image, ".text");
    REQUIRE(text != nullptr);
    REQUIRE(text->data.size() == 4 * 4);
    CHECK(word_at(text->data, 0) == 0x9A000000U);
    CHECK(word_at(text->data, 4) == 0x9C000000U);
    CHECK(word_at(text->data, 8) == 0x9E000000U);
    CHECK(word_at(text->data, 12) == 0x14000000U);
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
