#include "doctest.h"

#include "kr32/container/container.h"
#include "kr32/core/machine.h"

#include <cstdint>
#include <string>
#include <vector>

using namespace kr32::container;

namespace {

Image make_image() {
    Image image;
    image.type = ImageType::Executable;
    image.entry = 0x0001'0000;
    image.base = 0x0001'0000;

    Section text;
    text.name = ".text";
    text.type = SectionType::Text;
    text.flags = Alloc | Execute;
    text.alignment = 4;
    text.address = 0x0001'0000;
    text.data = {0x01, 0x02, 0x03, 0x04};
    text.size = 4;
    image.sections.push_back(text);

    Section rodata;
    rodata.name = ".rodata";
    rodata.type = SectionType::Rodata;
    rodata.flags = Alloc;
    rodata.alignment = 1;
    rodata.address = 0x0001'0004;
    rodata.data = {'h', 'i'};
    rodata.size = 2;
    image.sections.push_back(rodata);

    Section data;
    data.name = ".data";
    data.type = SectionType::Data;
    data.flags = Alloc | Write;
    data.alignment = 4;
    data.address = 0x0001'0008;
    data.data = {0xAA, 0xBB};
    data.size = 2;
    image.sections.push_back(data);

    Section bss;
    bss.name = ".bss";
    bss.type = SectionType::Bss;
    bss.flags = Alloc | Write;
    bss.alignment = 8;
    bss.address = 0x0001'0010;
    bss.size = 16;
    image.sections.push_back(bss);

    Section debug;
    debug.name = ".debug_info";
    debug.type = SectionType::Debug;
    debug.flags = 0;
    debug.alignment = 1;
    debug.address = 0;
    debug.data = {9, 8, 7};
    debug.size = 3;
    image.sections.push_back(debug);

    image.symbols.push_back({"_start", 0, 4, 0, SymbolBind::Global});
    image.symbols.push_back({"local", 4, 0, 0, SymbolBind::Local});
    image.symbols.push_back({"external", 0, 0, kUndefinedSection, SymbolBind::Global});

    image.relocations.push_back({0, 0, "_start", 4, RelocationType::Abs32});
    image.relocations.push_back({1, 0, "external", 0, RelocationType::PcRel32});
    image.relocations.push_back({2, 0, "_start", 2, RelocationType::Abs16});
    return image;
}

} // namespace

TEST_CASE("CRC32 matches the standard check value") {
    const std::string text = "123456789";
    CHECK(crc32(reinterpret_cast<const std::uint8_t*>(text.data()), text.size()) ==
          0xCBF4'3926U);
}

TEST_CASE("container round-trips every section and relocation type") {
    const Image original = make_image();
    std::vector<std::uint8_t> bytes;
    Error error;
    REQUIRE_MESSAGE(write(original, bytes, error), error.message);

    Image decoded;
    REQUIRE_MESSAGE(read(bytes, decoded, error), error.message);
    CHECK(decoded.type == original.type);
    CHECK(decoded.entry == original.entry);
    CHECK(decoded.base == original.base);
    REQUIRE(decoded.sections.size() == original.sections.size());
    for (std::size_t index = 0; index < original.sections.size(); ++index) {
        CHECK(decoded.sections[index].name == original.sections[index].name);
        CHECK(decoded.sections[index].type == original.sections[index].type);
        CHECK(decoded.sections[index].flags == original.sections[index].flags);
        CHECK(decoded.sections[index].alignment == original.sections[index].alignment);
        CHECK(decoded.sections[index].address == original.sections[index].address);
        CHECK(decoded.sections[index].size == original.sections[index].size);
        CHECK(decoded.sections[index].data == original.sections[index].data);
    }

    REQUIRE(decoded.symbols.size() == original.symbols.size());
    for (std::size_t index = 0; index < original.symbols.size(); ++index) {
        CHECK(decoded.symbols[index].name == original.symbols[index].name);
        CHECK(decoded.symbols[index].value == original.symbols[index].value);
        CHECK(decoded.symbols[index].size == original.symbols[index].size);
        CHECK(decoded.symbols[index].section == original.symbols[index].section);
        CHECK(decoded.symbols[index].bind == original.symbols[index].bind);
    }

    REQUIRE(decoded.relocations.size() == original.relocations.size());
    for (std::size_t index = 0; index < original.relocations.size(); ++index) {
        CHECK(decoded.relocations[index].section == original.relocations[index].section);
        CHECK(decoded.relocations[index].offset == original.relocations[index].offset);
        CHECK(decoded.relocations[index].symbol == original.relocations[index].symbol);
        CHECK(decoded.relocations[index].addend == original.relocations[index].addend);
        CHECK(decoded.relocations[index].type == original.relocations[index].type);
    }
}

TEST_CASE("container readers reject corruption, truncation, and bad magic") {
    std::vector<std::uint8_t> bytes;
    Error error;
    REQUIRE(write(make_image(), bytes, error));

    std::vector<std::uint8_t> corrupted = bytes;
    corrupted[corrupted.size() - 1] ^= 0xFF;
    Image image;
    CHECK_FALSE(read(corrupted, image, error));
    CHECK(error.message.find("checksum") != std::string::npos);

    std::vector<std::uint8_t> truncated(bytes.begin(), bytes.end() - 1);
    CHECK_FALSE(read(truncated, image, error));

    std::vector<std::uint8_t> magic = bytes;
    magic[0] = 'X';
    CHECK_FALSE(read(magic, image, error));
}

TEST_CASE("load_into zero-fills .bss over dirty memory") {
    kr32::core::Machine machine;
    const std::uint32_t address = kr32::core::kRamBase + 0x1000;
    const std::uint8_t dirty[] = {0xAA, 0xBB, 0xCC, 0xDD};
    REQUIRE(machine.load_image(address, dirty, sizeof(dirty)));

    Image image;
    image.type = ImageType::Executable;
    image.entry = address;
    Section bss;
    bss.name = ".bss";
    bss.type = SectionType::Bss;
    bss.flags = Alloc | Write;
    bss.address = address;
    bss.size = static_cast<std::uint32_t>(sizeof(dirty));
    image.sections.push_back(bss);

    LoadedImage loaded;
    std::string error;
    REQUIRE(load_into(machine, image, loaded, error));
    CHECK(error.empty());
    for (std::uint32_t offset = 0; offset < sizeof(dirty); ++offset) {
        std::uint8_t value = 0xFF;
        REQUIRE(machine.read8(address + offset, value));
        CHECK(value == 0);
    }
}

TEST_CASE("load_into zero-fills .bss on every load") {
    kr32::core::Machine machine;
    const std::uint32_t address = kr32::core::kRamBase + 0x2000;
    const std::uint8_t dirty[] = {1, 2, 3, 4, 5, 6, 7, 8};

    Image image;
    image.type = ImageType::Executable;
    image.entry = address;
    Section bss;
    bss.name = ".bss";
    bss.type = SectionType::Bss;
    bss.flags = Alloc | Write;
    bss.address = address;
    bss.size = static_cast<std::uint32_t>(sizeof(dirty));
    image.sections.push_back(bss);

    for (int pass = 0; pass < 2; ++pass) {
        REQUIRE(machine.load_image(address, dirty, sizeof(dirty)));
        LoadedImage loaded;
        std::string error;
        REQUIRE(load_into(machine, image, loaded, error));
        for (std::uint32_t offset = 0; offset < sizeof(dirty); ++offset) {
            std::uint8_t value = 0xFF;
            REQUIRE(machine.read8(address + offset, value));
            CHECK(value == 0);
        }
    }
}

TEST_CASE("load_into captures .tls instead of writing it") {
    kr32::core::Machine machine;
    const std::uint32_t address = kr32::core::kRamBase + 0x3000;
    const std::uint8_t dirty[] = {0x55, 0x66};
    REQUIRE(machine.load_image(address, dirty, sizeof(dirty)));

    Image image;
    image.type = ImageType::Executable;
    Section tls;
    tls.name = ".tls";
    tls.type = SectionType::Data;
    tls.flags = Alloc | Write;
    tls.address = address;
    tls.data = {0x11, 0x22};
    tls.size = 2;
    image.sections.push_back(tls);

    LoadedImage loaded;
    std::string error;
    REQUIRE(load_into(machine, image, loaded, error));
    REQUIRE(loaded.tls_template.size() == 2);
    CHECK(loaded.tls_template[0] == 0x11);
    CHECK(loaded.tls_template[1] == 0x22);
    for (std::uint32_t offset = 0; offset < sizeof(dirty); ++offset) {
        std::uint8_t value = 0;
        REQUIRE(machine.read8(address + offset, value));
        CHECK(value == dirty[offset]);
    }
}

TEST_CASE("load_into rejects a .bss section that does not fit") {
    kr32::core::Machine machine(kr32::core::Machine::Options{0x1'0000});
    Image image;
    image.type = ImageType::Executable;
    Section bss;
    bss.name = ".bss";
    bss.type = SectionType::Bss;
    bss.flags = Alloc | Write;
    bss.address = kr32::core::kRamBase + 0xFFFC;
    bss.size = 8;
    image.sections.push_back(bss);

    LoadedImage loaded;
    std::string error;
    CHECK_FALSE(load_into(machine, image, loaded, error));
    CHECK(error.find(".bss") != std::string::npos);
}
