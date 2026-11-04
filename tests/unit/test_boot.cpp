#include "doctest.h"

#include "kr32/asm/assembler.h"
#include "kr32/core/devices.h"
#include "kr32/core/machine.h"
#include "kr32/os/bootinfo.h"
#include "kr32/os/kernel_meta.h"

#include <cstdint>
#include <filesystem>
#include <fstream>
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

TEST_CASE("BootInfo is written at the fixed page and R2 points to it") {
    kr32::core::Machine machine(
        kr32::core::Machine::Options{1U * 1024U * 1024U, 0, true});
    kr32::os::BootInfo info{};
    info.magic = kr32::os::kBootInfoMagic;
    info.version = kr32::os::kBootInfoVersion;
    info.size = sizeof(kr32::os::BootInfo);
    info.ram_base = kr32::core::kRamBase;
    info.ram_size = 1U * 1024U * 1024U;
    info.kernel_base = 0x0002'0000;
    info.kernel_size = 0x100;
    info.kernel_entry = 0x0002'0000;
    info.framebuffer_base = kr32::core::kFramebufferBase;
    info.framebuffer_size = kr32::core::kFramebufferSize;
    info.framebuffer_stride = kr32::core::kFramebufferStride;
    info.framebuffer_format = 1;

    std::string error;
    REQUIRE_MESSAGE(kr32::os::write_boot_info(machine, info, error), error);
    CHECK(machine.reg(2) == kr32::os::kBootInfoAddress);
    std::uint32_t magic = 0;
    REQUIRE(machine.read_physical32(kr32::os::kBootInfoAddress, magic));
    CHECK(magic == kr32::os::kBootInfoMagic);
    std::uint32_t version = 0;
    REQUIRE(machine.read_physical32(kr32::os::kBootInfoAddress + 4, version));
    CHECK(version == kr32::os::kBootInfoVersion);
    std::uint32_t size = 0;
    REQUIRE(machine.read_physical32(kr32::os::kBootInfoAddress + 8, size));
    CHECK(size == sizeof(kr32::os::BootInfo));
}

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

TEST_CASE("kernel.meta validates checksum and initrd range") {
    const auto directory =
        std::filesystem::current_path() / "kr32-kernel-meta-test";
    std::filesystem::create_directories(directory);
    const auto kernel_path = directory / "kernel.bin";
    const auto meta_path = directory / "kernel.meta";
    const std::vector<std::uint8_t> image = {1, 2, 3, 4, 5, 6, 7, 8};
    {
        std::ofstream output(kernel_path, std::ios::binary);
        output.write(reinterpret_cast<const char*>(image.data()),
                     static_cast<std::streamsize>(image.size()));
    }

    auto write_meta = [&](std::uint32_t magic, std::uint32_t checksum,
                          std::uint32_t offset, std::uint32_t size) {
        const std::uint32_t words[6] = {
            magic, 1, 0x0002'0000, offset, size, checksum};
        std::ofstream output(meta_path, std::ios::binary);
        for (std::uint32_t word : words) {
            const std::uint8_t bytes[4] = {
                static_cast<std::uint8_t>(word),
                static_cast<std::uint8_t>(word >> 8U),
                static_cast<std::uint8_t>(word >> 16U),
                static_cast<std::uint8_t>(word >> 24U)};
            output.write(reinterpret_cast<const char*>(bytes), 4);
        }
    };

    const std::uint32_t checksum = kr32::os::crc32(image.data(), image.size());
    write_meta(kr32::os::kKernelMetaMagic, checksum, 2, 4);
    kr32::os::KernelMeta meta;
    std::string error;
    CHECK(kr32::os::load_kernel_meta(kernel_path.string(), meta, error));
    CHECK(meta.initrd_offset == 2);
    CHECK(meta.initrd_size == 4);

    write_meta(kr32::os::kKernelMetaMagic, checksum ^ 1U, 2, 4);
    CHECK_FALSE(kr32::os::load_kernel_meta(kernel_path.string(), meta, error));
    CHECK(error == "kernel.meta checksum mismatch");

    write_meta(kr32::os::kKernelMetaMagic, checksum, 4, 8);
    CHECK_FALSE(kr32::os::load_kernel_meta(kernel_path.string(), meta, error));
    CHECK(error == "kernel.meta initrd range is invalid");
    std::filesystem::remove_all(directory);
}
