#include "doctest.h"

#include "kr32/asm/assembler.h"
#include "kr32/core/machine.h"

#include <cstdint>
#include <string_view>
#include <vector>

namespace {

constexpr std::uint32_t kUserCode = 0x0002'0000;
constexpr std::uint32_t kUserStack = 0x0002'1000;
constexpr std::uint32_t kHandler = 0x0003'0000;
constexpr std::uint32_t kPageDirectory = 0x0004'0000;
constexpr std::uint32_t kLowLeaf = 0x0004'1000;
constexpr std::uint32_t kHighLeaf = 0x0004'2000;
constexpr std::uint32_t kVectorTable = 0x0005'0000;

std::vector<std::uint8_t> assemble_at(std::string_view source, std::uint32_t base) {
    kr32::assembler::Result result;
    kr32::assembler::Error error;
    REQUIRE_MESSAGE(kr32::assembler::assemble(
                        source, kr32::assembler::Options{base}, result, error),
                    error.message);
    return result.bytes;
}

void map_identity(kr32::core::Machine& machine,
                  std::uint32_t virtual_address,
                  std::uint32_t flags) {
    const std::uint32_t vpn = virtual_address >> 12;
    const std::uint32_t directory_index = vpn >> 10;
    const std::uint32_t leaf = directory_index == 0 ? kLowLeaf : kHighLeaf;
    const std::uint32_t directory_entry = leaf | kr32::core::kPteValid |
                                          (directory_index == 0 ? kr32::core::kPteUser : 0U);
    REQUIRE(machine.write_physical32(kPageDirectory + directory_index * 4U,
                                     directory_entry));
    REQUIRE(machine.write_physical32(leaf + (vpn & 0x3FFU) * 4U,
                                     (virtual_address & kr32::core::kPageFrameMask) | flags));
}

} // namespace

TEST_CASE("BIOS boots through paging into user code and a syscall") {
    kr32::core::Machine machine(
        kr32::core::Machine::Options{1U * 1024U * 1024U, 0, true});

    map_identity(machine, kUserCode,
                 kr32::core::kPteValid | kr32::core::kPteWritable |
                     kr32::core::kPteExecutable | kr32::core::kPteUser);
    map_identity(machine, kUserStack,
                 kr32::core::kPteValid | kr32::core::kPteWritable |
                     kr32::core::kPteUser);
    map_identity(machine, kHandler,
                 kr32::core::kPteValid | kr32::core::kPteWritable |
                     kr32::core::kPteExecutable);
    map_identity(machine, kVectorTable,
                 kr32::core::kPteValid | kr32::core::kPteWritable);
    map_identity(machine, 0x0010'F000,
                 kr32::core::kPteValid | kr32::core::kPteWritable);
    map_identity(machine, 0x000F'F000,
                 kr32::core::kPteValid | kr32::core::kPteWritable);
    map_identity(machine, kr32::core::kRomBase,
                 kr32::core::kPteValid | kr32::core::kPteExecutable);

    REQUIRE(machine.write_physical32(kVectorTable + 32U * 4U, kHandler));

    const std::vector<std::uint8_t> user = assemble_at(
        "MOV R0, #7\n"
        "SYSCALL #32\n"
        "MOV R0, #9\n"
        "HALT\n",
        kUserCode);
    REQUIRE(machine.load_image(kUserCode, user));

    const std::vector<std::uint8_t> handler = assemble_at("HALT\n", kHandler);
    REQUIRE(machine.load_image(kHandler, handler));

    const std::vector<std::uint8_t> bios = assemble_at(
        ".word bios_entry\n"
        "bios_entry:\n"
        "    MOV R1, #0x00040000\n"
        "    WRCR CR8, R1\n"
        "    MOV R1, #0x00050000\n"
        "    WRCR CR3, R1\n"
        "    MOV R1, #0x00100000\n"
        "    WRCR CR6, R1\n"
        "    ADJSP #-16\n"
        "    MOV R1, #0\n"
        "    STORE R1, [SP + 12]\n"
        "    MOV R1, #0x00021F00\n"
        "    STORE R1, [SP + 8]\n"
        "    MOV R1, #0\n"
        "    STORE R1, [SP + 4]\n"
        "    MOV R1, #0x00020000\n"
        "    STORE R1, [SP]\n"
        "    MOV R1, #1\n"
        "    WRCR CR10, R1\n"
        "    IRET\n",
        kr32::core::kRomBase);
    REQUIRE(machine.load_image(kr32::core::kRomBase, bios));

    machine.reset();
    CHECK(machine.pc() == kr32::core::kRomBase + 4U);

    bool halted = false;
    for (int step = 0; step < 1000; ++step) {
        const auto result = machine.step();
        if (result.kind == kr32::core::StepResult::Kind::Halted) {
            halted = true;
            break;
        }
        REQUIRE_MESSAGE(result.kind != kr32::core::StepResult::Kind::Raised,
                        "unexpected raised fault");
    }

    CHECK(halted);
    CHECK(machine.reg(0) == 7);
    CHECK(machine.paging_enabled());
}
