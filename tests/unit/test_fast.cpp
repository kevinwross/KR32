#include "doctest.h"

#include "kr32/asm/object_assembler.h"
#include "kr32/cc/cc.h"
#include "kr32/container/container.h"
#include "kr32/core/machine.h"
#include "kr32/link/linker.h"

#include <cstdint>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#ifndef KR32_SOURCE_DIR
#error "KR32_SOURCE_DIR must point at the repository root"
#endif

using namespace kr32;

namespace {

constexpr const char* kDriver = R"(void fast_WordFill(int cntWords, void *mem, unsigned int fillValue);
void fast_ByteFill(int cntBytes, void *mem, unsigned char fillValue);
static unsigned char buffer[64];
int main(void) {
    int i;
    for (i = 0; i < 64; ++i) { buffer[i] = 0; }
    fast_WordFill(4, buffer, 0x11223344);
    if (buffer[0] != 0x44) return 1;
    if (buffer[1] != 0x33) return 2;
    if (buffer[2] != 0x22) return 3;
    if (buffer[15] != 0x11) return 4;
    if (buffer[16] != 0x00) return 5;
    for (i = 0; i < 64; ++i) { buffer[i] = 0; }
    fast_ByteFill(10, buffer, 0xAB);
    for (i = 0; i < 10; ++i) { if (buffer[i] != 0xAB) return 6; }
    if (buffer[10] != 0x00) return 7;
    fast_ByteFill(3, buffer, 0x5A);
    if (buffer[2] != 0x5A) return 8;
    if (buffer[3] != 0xAB) return 9;
    fast_ByteFill(0, buffer, 0xCD);
    if (buffer[0] != 0x5A) return 10;
    for (i = 0; i < 64; ++i) { buffer[i] = 0; }
    fast_ByteFill(6, buffer + 1, 0x5A);
    if (buffer[0] != 0x00) return 11;
    for (i = 1; i <= 6; ++i) { if (buffer[i] != 0x5A) return 12; }
    if (buffer[7] != 0x00) return 13;
    for (i = 0; i < 64; ++i) { buffer[i] = 0; }
    fast_ByteFill(9, buffer + 3, 0xC3);
    if (buffer[2] != 0x00) return 14;
    for (i = 3; i < 12; ++i) { if (buffer[i] != 0xC3) return 15; }
    if (buffer[12] != 0x00) return 16;
    fast_WordFill(0, buffer, 0xFFFF);
    return 0;
}
)";

std::string read_text(const std::string& path) {
    std::ifstream input(path, std::ios::binary);
    std::ostringstream text;
    text << input.rdbuf();
    return text.str();
}

container::Image assemble(std::string_view text) {
    container::Image object;
    assembler::Error error;
    const bool ok = assembler::assemble_object(text, object, error);
    INFO(error.message);
    REQUIRE(ok);
    return object;
}

} // namespace

TEST_CASE("kernel fast fills match their contracts") {
    const std::string source =
        read_text(std::string(KR32_SOURCE_DIR) + "/kr32os/kernel/fast.S");
    REQUIRE_MESSAGE(!source.empty(), "cannot read kr32os/kernel/fast.S");

    const cc::Result compiled = cc::compile(kDriver, "<fast-test>");
    const std::string diagnostic =
        compiled.diagnostics.empty() ? "compile failed"
                                     : compiled.diagnostics.front().message;
    INFO(diagnostic);
    REQUIRE(compiled.ok);

    const container::Image user = assemble(compiled.assembly);
    const container::Image fast = assemble(source);
    const container::Image runtime = assemble(cc::runtime_assembly());

    container::Image image;
    linker::Map map;
    linker::Error link_error;
    linker::Options options;
    options.stack_top = 0x0011'0000;
    options.stack_limit = 0x0001'0000;
    REQUIRE_MESSAGE(linker::link({user, fast, runtime}, options, image, map, link_error),
                    link_error.message);

    core::Machine machine(core::Machine::Options{1U * 1024U * 1024U, 0, true});
    for (const container::Section& section : image.sections) {
        if ((section.flags & container::Alloc) == 0 ||
            section.type == container::SectionType::Bss) {
            continue;
        }
        REQUIRE(machine.load_image(section.address, section.data));
    }
    machine.reset(image.entry);
    machine.set_reg(11, machine.ram_base() + machine.ram_size() - 1024U);
    machine.set_syscall_handler([&](core::Machine& current, std::uint32_t vector) {
        if (vector == 32) {
            current.halt(current.reg(0));
            return true;
        }
        if (vector == 33) {
            current.set_reg(0, 0);
            return true;
        }
        return false;
    });

    bool halted = false;
    for (int step = 0; step < 200000; ++step) {
        const core::StepResult result = machine.step();
        if (result.kind == core::StepResult::Kind::Halted) {
            halted = true;
            break;
        }
        if (result.kind == core::StepResult::Kind::Raised) {
            break;
        }
    }
    REQUIRE(halted);
    CHECK(machine.reg(0) == 0U);
}
