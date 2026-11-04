#include "kr32/core/machine.h"

#include "kr32/core/devices.h"

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstdint>
#include <iterator>
#include <limits>
#include <utility>

namespace kr32::core {
namespace {

constexpr std::uint32_t kWordMask = 0xFFFF'FFFFU;
constexpr std::uint32_t kInterruptMask = 0x0000'FFFFU;
constexpr std::uint32_t kCanonicalF32 = 0x7FC0'0000U;
constexpr std::uint64_t kCanonicalF64 = 0x7FF8'0000'0000'0000ULL;

float float_from_bits(std::uint32_t bits) {
    return std::bit_cast<float>(bits);
}

std::uint32_t float_to_bits(float value) {
    return std::bit_cast<std::uint32_t>(value);
}

double double_from_bits(std::uint64_t bits) {
    return std::bit_cast<double>(bits);
}

std::uint64_t double_to_bits(double value) {
    return std::bit_cast<std::uint64_t>(value);
}

std::uint32_t canonical_float_bits(float value) {
    return std::isnan(value) ? kCanonicalF32 : float_to_bits(value);
}

std::uint64_t canonical_double_bits(double value) {
    return std::isnan(value) ? kCanonicalF64 : double_to_bits(value);
}

std::int32_t saturate_i32(double value) {
    if (std::isnan(value) || value >= 2147483647.0) {
        return std::numeric_limits<std::int32_t>::max();
    }
    if (value <= -2147483648.0) {
        return std::numeric_limits<std::int32_t>::min();
    }
    return static_cast<std::int32_t>(value);
}

std::uint32_t saturate_u32(double value) {
    if (std::isnan(value) || value >= 4294967295.0) {
        return std::numeric_limits<std::uint32_t>::max();
    }
    if (value <= 0.0) {
        return 0;
    }
    return static_cast<std::uint32_t>(value);
}

} // namespace

Machine::Machine()
    : Machine(Options{}) {}

Machine::Machine(Options options)
    : rom_(kRomSize, 0),
      core_id_(options.core_id),
      exceptions_enabled_(options.exceptions_enabled),
      syscall_handler_(std::move(options.syscall_handler)) {
    if (options.ram_size == 0) {
        options.ram_size = kDefaultRamSize;
    }
    const std::uint32_t max_ram = kRomBase - kRamBase;
    options.ram_size = std::min(options.ram_size, max_ram);
    if (options.shared_ram != nullptr) {
        ram_ = std::move(options.shared_ram);
    } else {
        ram_ = std::make_shared<std::vector<std::uint8_t>>(options.ram_size, std::uint8_t{0});
    }
    reset();
}

void Machine::reset() {
    std::fill(std::begin(regs_), std::end(regs_), 0U);
    std::fill(std::begin(fpr_), std::end(fpr_), 0U);
    for (auto& tile : tiles_) {
        std::fill(std::begin(tile), std::end(tile), 0.0F);
    }
    pc_ = kRomBase;
    sp_ = stack_top();
    ksp_ = sp_;
    flags_ = kFlagK;
    ticks_ = 0;
    halted_ = false;
    fault_ = {};
    vtbr_ = 0;
    esr_ = 0;
    ptbr_ = 0;
    far_ = 0;
    mctl_ = 0;
    imr_ = 0;
    irq_ = 0;
    tsp_ = 0;
    reservation_valid_ = false;
    reservation_address_ = 0;
    exit_code_set_ = false;
    exit_code_ = 0;

    std::uint32_t boot_pc = 0;
    if (read_physical32(kRomBase, boot_pc)) {
        pc_ = boot_pc;
    }
}

void Machine::reset(std::uint32_t pc, std::uint32_t sp) {
    std::fill(std::begin(regs_), std::end(regs_), 0U);
    std::fill(std::begin(fpr_), std::end(fpr_), 0U);
    for (auto& tile : tiles_) {
        std::fill(std::begin(tile), std::end(tile), 0.0F);
    }
    pc_ = pc;
    sp_ = (sp == 0) ? stack_top() : sp;
    ksp_ = sp_;
    flags_ = kFlagK;
    ticks_ = 0;
    halted_ = false;
    fault_ = {};
    vtbr_ = 0;
    esr_ = 0;
    ptbr_ = 0;
    far_ = 0;
    mctl_ = 0;
    imr_ = 0;
    irq_ = 0;
    tsp_ = 0;
    reservation_valid_ = false;
    reservation_address_ = 0;
    exit_code_set_ = false;
    exit_code_ = 0;
}

bool Machine::setup_arguments(const std::vector<std::string>& arguments,
                              std::string_view program_name,
                              std::string& error) {
    std::vector<std::string> all;
    all.reserve(arguments.size() + 1);
    all.emplace_back(program_name.empty() ? "program" : std::string(program_name));
    all.insert(all.end(), arguments.begin(), arguments.end());

    const std::uint32_t count = static_cast<std::uint32_t>(all.size());
    std::uint64_t bytes = static_cast<std::uint64_t>(count + 1) * 4U;
    for (const std::string& argument : all) {
        bytes += static_cast<std::uint64_t>(argument.size()) + 1U;
    }
    bytes = (bytes + 15U) & ~std::uint64_t{15};
    if (sp_ < kRamBase + 4096U ||
        sp_ - kRamBase < bytes + 4096U) {
        error = "not enough stack space for argc/argv";
        return false;
    }

    const std::uint32_t base =
        (sp_ - static_cast<std::uint32_t>(bytes)) & ~15U;
    const std::uint32_t strings = base + (count + 1U) * 4U;
    std::uint32_t cursor = strings;
    for (std::uint32_t index = 0; index < count; ++index) {
        const std::string& argument = all[index];
        if (!write32(base + index * 4U, cursor)) {
            error = "cannot write argv pointer";
            return false;
        }
        for (char ch : argument) {
            if (!write8(cursor++, static_cast<std::uint8_t>(ch))) {
                error = "cannot write argv string";
                return false;
            }
        }
        if (!write8(cursor++, 0)) {
            error = "cannot write argv terminator";
            return false;
        }
    }
    if (!write32(base + count * 4U, 0)) {
        error = "cannot write argv null terminator";
        return false;
    }
    sp_ = base;
    ksp_ = base;
    regs_[0] = count;
    regs_[1] = base;
    return true;
}

bool Machine::load_image(std::uint32_t address,
                         const std::uint8_t* data,
                         std::size_t size) {
    if (size == 0) {
        return true;
    }
    if (data == nullptr) {
        return false;
    }

    const std::uint64_t begin = address;
    const std::uint64_t end = begin + size;

    if (begin >= kRamBase && end <= static_cast<std::uint64_t>(kRamBase) + ram_->size()) {
        std::copy(data, data + size, ram_->begin() + static_cast<std::ptrdiff_t>(begin - kRamBase));
        return true;
    }
    if (begin >= kRomBase && end <= static_cast<std::uint64_t>(kRomBase) + rom_.size()) {
        std::copy(data, data + size, rom_.begin() + static_cast<std::ptrdiff_t>(begin - kRomBase));
        return true;
    }
    return false;
}

bool Machine::load_image(std::uint32_t address,
                         const std::vector<std::uint8_t>& data) {
    return load_image(address, data.data(), data.size());
}

void Machine::attach_devices(DeviceBus* devices, bool tick_devices) {
    devices_ = devices;
    tick_devices_ = tick_devices;
    if (devices_ != nullptr) {
        devices_->attach_machine(*this);
        if (!devices_->has_irq_handler()) {
            devices_->set_irq_handler([this](std::uint32_t line) { raise_interrupt(line); });
        }
    }
}

void Machine::invalidate_reservation() {
    reservation_valid_ = false;
    reservation_address_ = 0;
}

bool Machine::read_physical8(std::uint32_t address, std::uint8_t& value) const {
    const std::uint64_t wide = address;
    if (devices_ != nullptr && devices_->contains(address)) {
        std::uint32_t word = 0;
        if (!devices_->read32(address & ~3U, word)) {
            return false;
        }
        value = static_cast<std::uint8_t>((word >> ((address & 3U) * 8U)) & 0xFFU);
        return true;
    }
    if (wide >= kRamBase && wide < static_cast<std::uint64_t>(kRamBase) + ram_->size()) {
        value = (*ram_)[address - kRamBase];
        return true;
    }
    if (wide >= kRomBase && wide < static_cast<std::uint64_t>(kRomBase) + rom_.size()) {
        value = rom_[address - kRomBase];
        return true;
    }
    return false;
}

bool Machine::write_physical8(std::uint32_t address, std::uint8_t value) {
    const std::uint64_t wide = address;
    if (devices_ != nullptr && devices_->contains(address)) {
        std::uint32_t word = 0;
        if (!devices_->read32(address & ~3U, word)) {
            return false;
        }
        const std::uint32_t shift = (address & 3U) * 8U;
        word = (word & ~(0xFFU << shift)) | (static_cast<std::uint32_t>(value) << shift);
        return devices_->write32(address & ~3U, word);
    }
    if (wide >= kRamBase && wide < static_cast<std::uint64_t>(kRamBase) + ram_->size()) {
        if (reservation_valid_ && (address & ~3U) == reservation_address_) {
            reservation_valid_ = false;
        }
        (*ram_)[address - kRamBase] = value;
        ++write_generation_;
        return true;
    }
    return false;
}

bool Machine::read_physical16(std::uint32_t address, std::uint16_t& value) const {
    std::uint8_t lo = 0;
    std::uint8_t hi = 0;
    if (!read_physical8(address, lo) || !read_physical8(address + 1, hi)) {
        return false;
    }
    value = static_cast<std::uint16_t>(static_cast<std::uint16_t>(lo) |
                                       (static_cast<std::uint16_t>(hi) << 8));
    return true;
}

bool Machine::write_physical16(std::uint32_t address, std::uint16_t value) {
    return write_physical8(address, static_cast<std::uint8_t>(value & 0xFFU)) &&
           write_physical8(address + 1, static_cast<std::uint8_t>((value >> 8) & 0xFFU));
}

bool Machine::read_physical32(std::uint32_t address, std::uint32_t& value) const {
    if (devices_ != nullptr && devices_->contains(address)) {
        return devices_->read32(address, value);
    }
    std::uint8_t b0 = 0;
    std::uint8_t b1 = 0;
    std::uint8_t b2 = 0;
    std::uint8_t b3 = 0;
    if (!read_physical8(address, b0) || !read_physical8(address + 1, b1) ||
        !read_physical8(address + 2, b2) || !read_physical8(address + 3, b3)) {
        return false;
    }
    value = static_cast<std::uint32_t>(b0) |
            (static_cast<std::uint32_t>(b1) << 8) |
            (static_cast<std::uint32_t>(b2) << 16) |
            (static_cast<std::uint32_t>(b3) << 24);
    return true;
}

bool Machine::write_physical32(std::uint32_t address, std::uint32_t value) {
    if (devices_ != nullptr && devices_->contains(address)) {
        return devices_->write32(address, value);
    }
    return write_physical8(address, static_cast<std::uint8_t>(value & 0xFFU)) &&
           write_physical8(address + 1, static_cast<std::uint8_t>((value >> 8) & 0xFFU)) &&
           write_physical8(address + 2, static_cast<std::uint8_t>((value >> 16) & 0xFFU)) &&
           write_physical8(address + 3, static_cast<std::uint8_t>((value >> 24) & 0xFFU));
}

Fault Machine::memory_fault(FaultKind kind,
                            std::uint32_t vector,
                            std::uint32_t address,
                            std::uint32_t code) const {
    return {kind, vector, address, code};
}

bool Machine::translate(std::uint32_t virtual_address,
                        Access access,
                        std::uint32_t& physical_address,
                        Fault& fault) {
    fault = {};
    const bool user = !kernel_mode();
    const std::uint32_t code =
        (access == Access::Store ? 2U : 0U) |
        (user ? 4U : 0U) |
        (access == Access::Fetch ? 8U : 0U);
    auto page_fault = [&](std::uint32_t fault_code) {
        far_ = virtual_address;
        fault = memory_fault(FaultKind::PageFault, kVectorPageFault,
                             virtual_address, fault_code);
        return false;
    };

    if (!paging_enabled()) {
        if (virtual_address < kGuardBase + kGuardSize) {
            fault = memory_fault(FaultKind::GeneralProtection, kVectorGeneralProtection,
                                 virtual_address, 0);
            return false;
        }
        if (virtual_address >= kRamBase &&
            static_cast<std::uint64_t>(virtual_address) <
                static_cast<std::uint64_t>(kRamBase) + ram_->size()) {
            physical_address = virtual_address;
            return true;
        }
        if (virtual_address >= kRomBase &&
            static_cast<std::uint64_t>(virtual_address) <
                static_cast<std::uint64_t>(kRomBase) + rom_.size()) {
            if (access == Access::Store) {
                fault = memory_fault(FaultKind::GeneralProtection, kVectorGeneralProtection,
                                     virtual_address, 0);
                return false;
            }
            physical_address = virtual_address;
            return true;
        }
        fault = memory_fault(FaultKind::GeneralProtection, kVectorGeneralProtection,
                             virtual_address, 0);
        return false;
    }

    if ((ptbr_ & kPageOffsetMask) != 0) {
        return page_fault(code);
    }

    const std::uint32_t vpn = virtual_address >> 12;
    const std::uint32_t directory_address = ptbr_ + ((vpn >> 10) * 4U);
    std::uint32_t directory_entry = 0;
    if (!read_physical32(directory_address, directory_entry)) {
        return page_fault(code);
    }
    if ((directory_entry & kPteValid) == 0) {
        return page_fault(code);
    }
    if ((directory_entry & kPtePageSize) != 0) {
        return page_fault(code | 1U);
    }
    if (user && (directory_entry & kPteUser) == 0) {
        return page_fault(code | 1U);
    }

    const std::uint32_t leaf_address =
        (directory_entry & kPageFrameMask) + ((vpn & 0x3FFU) * 4U);
    std::uint32_t leaf_entry = 0;
    if (!read_physical32(leaf_address, leaf_entry)) {
        return page_fault(code);
    }
    if ((leaf_entry & kPteValid) == 0) {
        return page_fault(code);
    }
    if ((leaf_entry & kPtePageSize) != 0) {
        return page_fault(code | 1U);
    }
    if (user && (leaf_entry & kPteUser) == 0) {
        return page_fault(code | 1U);
    }
    if (access == Access::Fetch && (leaf_entry & kPteExecutable) == 0) {
        return page_fault(code | 1U);
    }
    if (access == Access::Store && (leaf_entry & kPteWritable) == 0) {
        return page_fault(code | 1U);
    }
    if (access == Access::Store && kernel_mode() &&
        (mctl_ & kMctlWriteProtect) != 0 && (leaf_entry & kPteWritable) == 0) {
        return page_fault(code | 1U);
    }

    directory_entry |= kPteAccessed;
    leaf_entry |= kPteAccessed;
    if (access == Access::Store) {
        leaf_entry |= kPteDirty;
    }
    if (!write_physical32(directory_address, directory_entry) ||
        !write_physical32(leaf_address, leaf_entry)) {
        fault = memory_fault(FaultKind::GeneralProtection, kVectorGeneralProtection,
                             virtual_address, 0);
        return false;
    }

    physical_address = (leaf_entry & kPageFrameMask) | (virtual_address & kPageOffsetMask);
    const std::uint64_t wide = physical_address;
    if (wide >= kRamBase && wide < static_cast<std::uint64_t>(kRamBase) + ram_->size()) {
        return true;
    }
    if (wide >= kRomBase && wide < static_cast<std::uint64_t>(kRomBase) + rom_.size()) {
        if (access == Access::Store) {
            fault = memory_fault(FaultKind::GeneralProtection, kVectorGeneralProtection,
                                 virtual_address, 0);
            return false;
        }
        return true;
    }
    if (devices_ != nullptr && devices_->contains(physical_address)) {
        return true;
    }

    return page_fault(code | 1U);
}

bool Machine::read_virtual8(std::uint32_t address,
                            Access access,
                            std::uint8_t& value,
                            Fault& fault) {
    std::uint32_t physical = 0;
    if (!translate(address, access, physical, fault)) {
        return false;
    }
    if (!read_physical8(physical, value)) {
        fault = memory_fault(FaultKind::GeneralProtection, kVectorGeneralProtection, address, 0);
        return false;
    }
    return true;
}

bool Machine::read_virtual16(std::uint32_t address,
                             Access access,
                             std::uint16_t& value,
                             Fault& fault) {
    if ((address & kPageOffsetMask) <= kPageSize - 2U) {
        std::uint32_t physical = 0;
        if (!translate(address, access, physical, fault)) {
            return false;
        }
        if (!read_physical16(physical, value)) {
            fault = memory_fault(FaultKind::GeneralProtection, kVectorGeneralProtection,
                                 address, 0);
            return false;
        }
        return true;
    }
    std::uint8_t lo = 0;
    std::uint8_t hi = 0;
    if (!read_virtual8(address, access, lo, fault) ||
        !read_virtual8(address + 1, access, hi, fault)) {
        return false;
    }
    value = static_cast<std::uint16_t>(static_cast<std::uint16_t>(lo) |
                                       (static_cast<std::uint16_t>(hi) << 8));
    return true;
}

bool Machine::read_virtual32(std::uint32_t address,
                             Access access,
                             std::uint32_t& value,
                             Fault& fault) {
    if ((address & kPageOffsetMask) <= kPageSize - 4U) {
        std::uint32_t physical = 0;
        if (!translate(address, access, physical, fault)) {
            return false;
        }
        if (!read_physical32(physical, value)) {
            fault = memory_fault(FaultKind::GeneralProtection, kVectorGeneralProtection,
                                 address, 0);
            return false;
        }
        return true;
    }
    std::uint8_t b0 = 0;
    std::uint8_t b1 = 0;
    std::uint8_t b2 = 0;
    std::uint8_t b3 = 0;
    if (!read_virtual8(address, access, b0, fault) ||
        !read_virtual8(address + 1, access, b1, fault) ||
        !read_virtual8(address + 2, access, b2, fault) ||
        !read_virtual8(address + 3, access, b3, fault)) {
        return false;
    }
    value = static_cast<std::uint32_t>(b0) |
            (static_cast<std::uint32_t>(b1) << 8) |
            (static_cast<std::uint32_t>(b2) << 16) |
            (static_cast<std::uint32_t>(b3) << 24);
    return true;
}

bool Machine::write_virtual8(std::uint32_t address,
                             std::uint8_t value,
                             Fault& fault) {
    std::uint32_t physical = 0;
    if (!translate(address, Access::Store, physical, fault)) {
        return false;
    }
    if (!write_physical8(physical, value)) {
        fault = memory_fault(FaultKind::GeneralProtection, kVectorGeneralProtection, address, 0);
        return false;
    }
    return true;
}

bool Machine::write_virtual16(std::uint32_t address,
                              std::uint16_t value,
                              Fault& fault) {
    if ((address & kPageOffsetMask) <= kPageSize - 2U) {
        std::uint32_t physical = 0;
        if (!translate(address, Access::Store, physical, fault)) {
            return false;
        }
        if (!write_physical16(physical, value)) {
            fault = memory_fault(FaultKind::GeneralProtection, kVectorGeneralProtection,
                                 address, 0);
            return false;
        }
        return true;
    }
    return write_virtual8(address, static_cast<std::uint8_t>(value & 0xFFU), fault) &&
           write_virtual8(address + 1, static_cast<std::uint8_t>((value >> 8) & 0xFFU), fault);
}

bool Machine::write_virtual32(std::uint32_t address,
                              std::uint32_t value,
                              Fault& fault) {
    if ((address & kPageOffsetMask) <= kPageSize - 4U) {
        std::uint32_t physical = 0;
        if (!translate(address, Access::Store, physical, fault)) {
            return false;
        }
        if (!write_physical32(physical, value)) {
            fault = memory_fault(FaultKind::GeneralProtection, kVectorGeneralProtection,
                                 address, 0);
            return false;
        }
        return true;
    }
    return write_virtual8(address, static_cast<std::uint8_t>(value & 0xFFU), fault) &&
           write_virtual8(address + 1, static_cast<std::uint8_t>((value >> 8) & 0xFFU), fault) &&
           write_virtual8(address + 2, static_cast<std::uint8_t>((value >> 16) & 0xFFU), fault) &&
           write_virtual8(address + 3, static_cast<std::uint8_t>((value >> 24) & 0xFFU), fault);
}

bool Machine::read8(std::uint32_t address, std::uint8_t& value) const {
    Machine* self = const_cast<Machine*>(this);
    Fault fault;
    return self->read_virtual8(address, Access::Load, value, fault);
}

bool Machine::write8(std::uint32_t address, std::uint8_t value) {
    Fault fault;
    return write_virtual8(address, value, fault);
}

bool Machine::read16(std::uint32_t address, std::uint16_t& value) const {
    Machine* self = const_cast<Machine*>(this);
    Fault fault;
    return self->read_virtual16(address, Access::Load, value, fault);
}

bool Machine::write16(std::uint32_t address, std::uint16_t value) {
    Fault fault;
    return write_virtual16(address, value, fault);
}

bool Machine::read32(std::uint32_t address, std::uint32_t& value) const {
    Machine* self = const_cast<Machine*>(this);
    Fault fault;
    return self->read_virtual32(address, Access::Load, value, fault);
}

bool Machine::write32(std::uint32_t address, std::uint32_t value) {
    Fault fault;
    return write_virtual32(address, value, fault);
}

std::uint32_t Machine::reg(std::size_t index) const {
    return index < 16 ? regs_[index] : 0;
}

void Machine::raise_interrupt(std::uint32_t line) {
    if (line < 16) {
        irq_ |= (1U << line);
    }
}

void Machine::set_syscall_handler(SyscallHandler handler) {
    syscall_handler_ = std::move(handler);
}

void Machine::halt(std::uint32_t exit_code) {
    halted_ = true;
    exit_code_set_ = true;
    exit_code_ = exit_code;
}

void Machine::set_reg(std::size_t index, std::uint32_t value) {
    if (index < 16) {
        regs_[index] = value;
    }
}

std::uint64_t Machine::fpr(std::size_t index) const {
    return index < 16 ? fpr_[index] : 0;
}

void Machine::set_fpr(std::size_t index, std::uint64_t value) {
    if (index < 16) {
        fpr_[index] = value;
    }
}

float Machine::tile_value(std::size_t tile, std::size_t index) const {
    if (tile >= 16 || index >= 16) {
        return 0.0F;
    }
    return tiles_[tile][index];
}

void Machine::set_tile_value(std::size_t tile, std::size_t index, float value) {
    if (tile < 16 && index < 16) {
        tiles_[tile][index] = value;
    }
}

std::uint32_t Machine::payload(const isa::Instruction& instruction) const {
    if (instruction.extension) {
        return instruction.extension_word;
    }
    return static_cast<std::uint32_t>(static_cast<std::int32_t>(instruction.immediate));
}

bool Machine::effective_address(const isa::Instruction& instruction,
                                std::uint32_t instruction_pc,
                                std::uint32_t& address) const {
    const std::uint32_t displacement = payload(instruction);
    switch (instruction.field) {
    case 2:
        address = regs_[instruction.rs1];
        return true;
    case 3:
        address = regs_[instruction.rs1] + displacement;
        return true;
    case 4:
        address = sp_ + displacement;
        return true;
    case 5:
        address = displacement;
        return true;
    case 6:
        address = instruction_pc + displacement;
        return true;
    default:
        return false;
    }
}

bool Machine::indexed_address(const isa::Instruction& instruction,
                              std::uint32_t& address) const {
    address = regs_[instruction.rs1] + regs_[instruction.field] + payload(instruction);
    return true;
}

bool Machine::condition_holds(std::uint32_t condition) const {
    const bool z = flag(kFlagZ);
    const bool n = flag(kFlagN);
    const bool c = flag(kFlagC);
    const bool v = flag(kFlagV);

    switch (condition) {
    case 0: return z;                  // EQ
    case 1: return !z;                 // NE
    case 2: return c;                  // CS
    case 3: return !c;                 // CC
    case 4: return n;                  // MI
    case 5: return !n;                 // PL
    case 6: return v;                  // VS
    case 7: return !v;                 // VC
    case 8: return c && !z;            // HI
    case 9: return !c || z;            // LS
    case 10: return n == v;            // GE
    case 11: return n != v;            // LT
    case 12: return !z && (n == v);    // GT
    case 13: return z || (n != v);     // LE
    case 14: return true;              // AL
    default: return false;             // NV
    }
}

void Machine::set_flag(std::uint32_t bit, bool value) {
    if (value) {
        flags_ |= bit;
    } else {
        flags_ &= ~bit;
    }
}

void Machine::set_zn(std::uint32_t result) {
    set_flag(kFlagZ, result == 0);
    set_flag(kFlagN, (result & 0x8000'0000U) != 0);
}

void Machine::set_add_flags(std::uint32_t left, std::uint32_t right, std::uint32_t result) {
    set_zn(result);
    set_flag(kFlagC, result < left);
    set_flag(kFlagV, ((left ^ result) & (right ^ result) & 0x8000'0000U) != 0);
}

void Machine::set_sub_flags(std::uint32_t left, std::uint32_t right, std::uint32_t result) {
    set_zn(result);
    set_flag(kFlagC, left >= right);
    set_flag(kFlagV, ((left ^ right) & (left ^ result) & 0x8000'0000U) != 0);
}

bool Machine::read_control_register(std::uint32_t number, std::uint32_t& value) const {
    switch (number) {
    case 0: value = flag(kFlagI) ? 1U : 0U; return true;
    case 1: value = imr_; return true;
    case 2: value = irq_; return true;
    case 3: value = vtbr_; return true;
    case 4: value = kernel_mode() ? 1U : 0U; return true;
    case 5: value = esr_; return true;
    case 6: value = ksp_; return true;
    case 7: value = 0; return true;
    case 8: value = ptbr_; return true;
    case 9: value = far_; return true;
    case 10: value = mctl_; return true;
    case 11: value = core_id_; return true;
    default: return false;
    }
}

bool Machine::write_control_register(std::uint32_t number, std::uint32_t value) {
    switch (number) {
    case 0:
        set_flag(kFlagI, (value & 1U) != 0);
        return true;
    case 1:
        imr_ = value & kInterruptMask;
        return true;
    case 2:
        irq_ = value & kInterruptMask;
        return true;
    case 3:
        if ((value & 3U) != 0) {
            return false;
        }
        vtbr_ = value;
        return true;
    case 4:
    case 7:
    case 11:
        return false;
    case 5:
        esr_ = value;
        return true;
    case 6:
        ksp_ = value;
        return true;
    case 8:
        if ((value & kPageOffsetMask) != 0) {
            return false;
        }
        ptbr_ = value;
        return true;
    case 9:
        far_ = value;
        return true;
    case 10:
        if ((value & ~(kMctlPaging | kMctlWriteProtect)) != 0) {
            return false;
        }
        mctl_ = value;
        return true;
    default:
        return false;
    }
}

StepResult Machine::raise(FaultKind kind,
                          std::uint32_t vector,
                          std::uint32_t address,
                          std::uint32_t code) {
    fault_ = {kind, vector, address, code};
    return {StepResult::Kind::Raised, fault_};
}

bool Machine::push32(std::uint32_t value) {
    if ((sp_ & 3U) != 0 || sp_ < 4) {
        return false;
    }
    Fault fault;
    if (!write_virtual32(sp_ - 4, value, fault)) {
        return false;
    }
    sp_ -= 4;
    return true;
}

bool Machine::pop32(std::uint32_t& value) {
    if ((sp_ & 3U) != 0) {
        return false;
    }
    Fault fault;
    if (!read_virtual32(sp_, Access::Load, value, fault)) {
        return false;
    }
    sp_ += 4;
    return true;
}

bool Machine::deliver(std::uint32_t vector,
                      std::uint32_t code,
                      std::uint32_t victim_pc) {
    if (vector > 255 || (vtbr_ & 3U) != 0) {
        return false;
    }

    const std::uint32_t old_sp = sp_;
    const std::uint32_t old_flags = flags_;
    const std::uint32_t old_pc = pc_;
    const bool user = !kernel_mode();
    const std::uint32_t saved_sp = sp_;
    const std::uint32_t saved_flags = flags_;

    if (user) {
        if ((ksp_ & 3U) != 0) {
            return false;
        }
        sp_ = ksp_;
    }

    flags_ |= kFlagK;
    if (!push32(code) || !push32(saved_sp) || !push32(saved_flags) ||
        !push32(victim_pc)) {
        sp_ = old_sp;
        flags_ = old_flags;
        pc_ = old_pc;
        return false;
    }

    flags_ &= ~kFlagI;
    Fault fetch_fault;
    std::uint32_t handler = 0;
    if (!read_virtual32(vtbr_ + vector * 4U, Access::Load, handler, fetch_fault) ||
        (handler & 3U) != 0) {
        sp_ = old_sp;
        flags_ = old_flags;
        pc_ = old_pc;
        return false;
    }

    pc_ = handler;
    esr_ = vector;
    reservation_valid_ = false;
    return true;
}

StepResult Machine::handle_fault(Fault fault, std::uint32_t victim_pc) {
    fault_ = fault;
    if (!exceptions_enabled_) {
        return {StepResult::Kind::Raised, fault_};
    }

    if (deliver(fault.vector, fault.code, victim_pc)) {
        return {StepResult::Kind::FaultDelivered, fault_};
    }

    Fault double_fault{FaultKind::DoubleFault, kVectorDoubleFault, victim_pc, 0};
    if (deliver(kVectorDoubleFault, 0, victim_pc)) {
        fault_ = double_fault;
        return {StepResult::Kind::FaultDelivered, fault_};
    }

    halted_ = true;
    fault_ = double_fault;
    return {StepResult::Kind::Halted, fault_};
}

StepResult Machine::step() {
    if (halted_) {
        return {StepResult::Kind::Halted, {}};
    }

    const std::uint32_t pending = irq_ & ~imr_ & kInterruptMask;
    if (flag(kFlagI) && pending != 0) {
        std::uint32_t line = 0;
        while (((pending >> line) & 1U) == 0) {
            ++line;
        }
        irq_ &= ~(1U << line);
        const std::uint32_t vector = 16U + line;
        if (deliver(vector, 0, pc_)) {
            fault_ = {FaultKind::SoftwareInterrupt, vector, pc_, 0};
            return {StepResult::Kind::FaultDelivered, fault_};
        }
        return handle_fault({FaultKind::DoubleFault, kVectorDoubleFault, pc_, 0}, pc_);
    }

    if ((pc_ & 3U) != 0) {
        return handle_fault({FaultKind::Alignment, kVectorAlignment, pc_, 0}, pc_);
    }

    const std::uint32_t instruction_pc = pc_;
    std::uint32_t first = 0;
    Fault fetch_fault;
    if (!read_virtual32(pc_, Access::Fetch, first, fetch_fault)) {
        return handle_fault(fetch_fault, instruction_pc);
    }

    const bool has_extension = ((first >> 12) & 1U) != 0;
    std::uint32_t words[2] = {first, 0};
    std::size_t count = 1;
    if (has_extension) {
        if (!read_virtual32(pc_ + 4, Access::Fetch, words[1], fetch_fault)) {
            return handle_fault(fetch_fault, instruction_pc);
        }
        count = 2;
    }

    isa::Instruction instruction{};
    if (!isa::decode(words, count, instruction)) {
        return handle_fault({FaultKind::InvalidOpcode, kVectorInvalidOpcode, instruction_pc, 0},
                            instruction_pc);
    }

    const std::uint32_t next_pc = instruction_pc + (has_extension ? 8U : 4U);
    StepResult result = execute(instruction, instruction_pc, next_pc);
    if (result.kind == StepResult::Kind::Retired) {
        ++ticks_;
    } else if (result.kind == StepResult::Kind::Halted) {
        halted_ = true;
        ++ticks_;
    } else if (result.kind == StepResult::Kind::Raised) {
        return handle_fault(fault_, pc_);
    }
    if (devices_ != nullptr && tick_devices_) {
        devices_->tick();
    }
    return result;
}

StepResult Machine::execute(const isa::Instruction& instruction,
                            std::uint32_t instruction_pc,
                            std::uint32_t next_pc) {
    const std::uint8_t opcode = instruction.opcode;
    const std::uint8_t rd = instruction.rd;
    const std::uint8_t rs1 = instruction.rs1;
    const std::uint32_t right = instruction.literal ? payload(instruction) : regs_[instruction.field];

    auto fail_ud = [&]() {
        return raise(FaultKind::InvalidOpcode, kVectorInvalidOpcode, instruction_pc);
    };
    auto fail_gp = [&](std::uint32_t address) {
        return raise(FaultKind::GeneralProtection, kVectorGeneralProtection, address);
    };
    auto fail_align = [&](std::uint32_t address) {
        return raise(FaultKind::Alignment, kVectorAlignment, address);
    };
    auto fail_fault = [&](const Fault& fault) {
        return raise(fault.kind, fault.vector, fault.address, fault.code);
    };
    auto finish = [&](std::uint32_t new_pc) {
        pc_ = new_pc;
        return StepResult{StepResult::Kind::Retired, {}};
    };
    auto finish_next = [&]() {
        return finish(next_pc);
    };
    auto logical_flags = [&](std::uint32_t value) {
        set_zn(value);
        set_flag(kFlagC, false);
        set_flag(kFlagV, false);
    };
    auto load32 = [&](std::uint32_t address, std::uint32_t& value, Fault& fault) {
        return read_virtual32(address, Access::Load, value, fault);
    };
    auto load16 = [&](std::uint32_t address, std::uint16_t& value, Fault& fault) {
        return read_virtual16(address, Access::Load, value, fault);
    };
    auto load8 = [&](std::uint32_t address, std::uint8_t& value, Fault& fault) {
        return read_virtual8(address, Access::Load, value, fault);
    };
    auto store32 = [&](std::uint32_t address, std::uint32_t value, Fault& fault) {
        return write_virtual32(address, value, fault);
    };

    switch (opcode) {
    case 0x00: // MOV
        regs_[rd] = instruction.literal ? payload(instruction) : regs_[rs1];
        return finish_next();

    case 0x01: { // LOAD
        std::uint32_t address = 0;
        if (!effective_address(instruction, instruction_pc, address)) {
            return fail_ud();
        }
        std::uint32_t value = 0;
        Fault fault;
        if (!load32(address, value, fault)) {
            return fail_fault(fault);
        }
        regs_[rd] = value;
        return finish_next();
    }

    case 0x02: { // STORE
        std::uint32_t address = 0;
        if (!effective_address(instruction, instruction_pc, address)) {
            return fail_ud();
        }
        Fault fault;
        if (!store32(address, regs_[rd], fault)) {
            return fail_fault(fault);
        }
        return finish_next();
    }

    case 0x03: { // LOAD8U
        std::uint32_t address = 0;
        if (!effective_address(instruction, instruction_pc, address)) {
            return fail_ud();
        }
        std::uint8_t value = 0;
        Fault fault;
        if (!load8(address, value, fault)) {
            return fail_fault(fault);
        }
        regs_[rd] = value;
        return finish_next();
    }

    case 0x04: { // LOAD8S
        std::uint32_t address = 0;
        if (!effective_address(instruction, instruction_pc, address)) {
            return fail_ud();
        }
        std::uint8_t value = 0;
        Fault fault;
        if (!load8(address, value, fault)) {
            return fail_fault(fault);
        }
        regs_[rd] = static_cast<std::uint32_t>(
            static_cast<std::int32_t>(static_cast<std::int8_t>(value)));
        return finish_next();
    }

    case 0x05: { // LOAD16U
        std::uint32_t address = 0;
        if (!effective_address(instruction, instruction_pc, address)) {
            return fail_ud();
        }
        std::uint16_t value = 0;
        Fault fault;
        if (!load16(address, value, fault)) {
            return fail_fault(fault);
        }
        regs_[rd] = value;
        return finish_next();
    }

    case 0x06: { // LOAD16S
        std::uint32_t address = 0;
        if (!effective_address(instruction, instruction_pc, address)) {
            return fail_ud();
        }
        std::uint16_t value = 0;
        Fault fault;
        if (!load16(address, value, fault)) {
            return fail_fault(fault);
        }
        regs_[rd] = static_cast<std::uint32_t>(
            static_cast<std::int32_t>(static_cast<std::int16_t>(value)));
        return finish_next();
    }

    case 0x07: { // STORE8
        std::uint32_t address = 0;
        if (!effective_address(instruction, instruction_pc, address)) {
            return fail_ud();
        }
        Fault fault;
        if (!write_virtual8(address, static_cast<std::uint8_t>(regs_[rd] & 0xFFU), fault)) {
            return fail_fault(fault);
        }
        return finish_next();
    }

    case 0x08: { // STORE16
        std::uint32_t address = 0;
        if (!effective_address(instruction, instruction_pc, address)) {
            return fail_ud();
        }
        Fault fault;
        if (!write_virtual16(address, static_cast<std::uint16_t>(regs_[rd] & 0xFFFFU), fault)) {
            return fail_fault(fault);
        }
        return finish_next();
    }

    case 0x09: { // LEA
        std::uint32_t address = 0;
        if (!effective_address(instruction, instruction_pc, address)) {
            return fail_ud();
        }
        regs_[rd] = address;
        return finish_next();
    }

    case 0x10: // LOADX
    case 0x11: // STOREX
    case 0x12: // LOAD8UX
    case 0x13: // LOAD8SX
    case 0x14: // LOAD16UX
    case 0x15: // LOAD16SX
    case 0x16: // STORE8X
    case 0x17: { // STORE16X
        std::uint32_t address = 0;
        if (!indexed_address(instruction, address)) {
            return fail_ud();
        }
        Fault fault;
        if (opcode == 0x10) {
            std::uint32_t value = 0;
            if (!load32(address, value, fault)) {
                return fail_fault(fault);
            }
            regs_[rd] = value;
        } else if (opcode == 0x12) {
            std::uint8_t value = 0;
            if (!load8(address, value, fault)) {
                return fail_fault(fault);
            }
            regs_[rd] = value;
        } else if (opcode == 0x13) {
            std::uint8_t value = 0;
            if (!load8(address, value, fault)) {
                return fail_fault(fault);
            }
            regs_[rd] = static_cast<std::uint32_t>(
                static_cast<std::int32_t>(static_cast<std::int8_t>(value)));
        } else if (opcode == 0x14) {
            std::uint16_t value = 0;
            if (!load16(address, value, fault)) {
                return fail_fault(fault);
            }
            regs_[rd] = value;
        } else if (opcode == 0x15) {
            std::uint16_t value = 0;
            if (!load16(address, value, fault)) {
                return fail_fault(fault);
            }
            regs_[rd] = static_cast<std::uint32_t>(
                static_cast<std::int32_t>(static_cast<std::int16_t>(value)));
        } else if (opcode == 0x11) {
            if (!store32(address, regs_[rd], fault)) {
                return fail_fault(fault);
            }
        } else if (opcode == 0x16) {
            if (!write_virtual8(address, static_cast<std::uint8_t>(regs_[rd] & 0xFFU), fault)) {
                return fail_fault(fault);
            }
        } else {
            if (!write_virtual16(address, static_cast<std::uint16_t>(regs_[rd] & 0xFFFFU), fault)) {
                return fail_fault(fault);
            }
        }
        return finish_next();
    }

    case 0x18: { // SLT
        const std::uint32_t value =
            (static_cast<std::int32_t>(regs_[rs1]) < static_cast<std::int32_t>(right)) ? 1U : 0U;
        regs_[rd] = value;
        logical_flags(value);
        return finish_next();
    }

    case 0x19: { // SLTU
        const std::uint32_t value = (regs_[rs1] < right) ? 1U : 0U;
        regs_[rd] = value;
        logical_flags(value);
        return finish_next();
    }

    case 0x20: { // ADD
        const std::uint32_t left = regs_[rs1];
        const std::uint32_t result = left + right;
        regs_[rd] = result;
        set_add_flags(left, right, result);
        return finish_next();
    }

    case 0x21: { // SUB
        const std::uint32_t left = regs_[rs1];
        const std::uint32_t result = left - right;
        regs_[rd] = result;
        set_sub_flags(left, right, result);
        return finish_next();
    }

    case 0x22: { // MUL
        const std::uint64_t product =
            static_cast<std::uint64_t>(regs_[rs1]) * static_cast<std::uint64_t>(right);
        const std::uint32_t result = static_cast<std::uint32_t>(product & kWordMask);
        regs_[rd] = result;
        set_zn(result);
        set_flag(kFlagC, false);
        set_flag(kFlagV, (product >> 32) != 0);
        return finish_next();
    }

    case 0x23: { // MULH
        const std::int64_t product =
            static_cast<std::int64_t>(static_cast<std::int32_t>(regs_[rs1])) *
            static_cast<std::int64_t>(static_cast<std::int32_t>(right));
        const std::uint32_t result =
            static_cast<std::uint32_t>(static_cast<std::uint64_t>(product) >> 32);
        regs_[rd] = result;
        set_zn(result);
        set_flag(kFlagC, false);
        set_flag(kFlagV, false);
        return finish_next();
    }

    case 0x24: { // MULHU
        const std::uint64_t product =
            static_cast<std::uint64_t>(regs_[rs1]) * static_cast<std::uint64_t>(right);
        const std::uint32_t result = static_cast<std::uint32_t>(product >> 32);
        regs_[rd] = result;
        set_zn(result);
        set_flag(kFlagC, false);
        set_flag(kFlagV, false);
        return finish_next();
    }

    case 0x25: { // DIV
        if (right == 0) {
            return raise(FaultKind::DivideByZero, kVectorDivideByZero, instruction_pc);
        }
        const std::uint32_t result = regs_[rs1] / right;
        regs_[rd] = result;
        set_zn(result);
        set_flag(kFlagC, false);
        set_flag(kFlagV, false);
        return finish_next();
    }

    case 0x26: { // DIVS
        if (right == 0) {
            return raise(FaultKind::DivideByZero, kVectorDivideByZero, instruction_pc);
        }
        const std::int32_t left = static_cast<std::int32_t>(regs_[rs1]);
        const std::int32_t divisor = static_cast<std::int32_t>(right);
        std::int32_t result = 0;
        if (left == std::numeric_limits<std::int32_t>::min() && divisor == -1) {
            result = std::numeric_limits<std::int32_t>::min();
        } else {
            result = left / divisor;
        }
        regs_[rd] = static_cast<std::uint32_t>(result);
        set_zn(regs_[rd]);
        set_flag(kFlagC, false);
        set_flag(kFlagV, false);
        return finish_next();
    }

    case 0x27: { // MOD
        if (right == 0) {
            return raise(FaultKind::DivideByZero, kVectorDivideByZero, instruction_pc);
        }
        const std::uint32_t result = regs_[rs1] % right;
        regs_[rd] = result;
        set_zn(result);
        set_flag(kFlagC, false);
        set_flag(kFlagV, false);
        return finish_next();
    }

    case 0x28: { // MODS
        if (right == 0) {
            return raise(FaultKind::DivideByZero, kVectorDivideByZero, instruction_pc);
        }
        const std::int32_t left = static_cast<std::int32_t>(regs_[rs1]);
        const std::int32_t divisor = static_cast<std::int32_t>(right);
        std::int32_t result = 0;
        if (left == std::numeric_limits<std::int32_t>::min() && divisor == -1) {
            result = 0;
        } else {
            result = left % divisor;
        }
        regs_[rd] = static_cast<std::uint32_t>(result);
        set_zn(regs_[rd]);
        set_flag(kFlagC, false);
        set_flag(kFlagV, false);
        return finish_next();
    }

    case 0x29: { // CMP
        const std::uint32_t left = regs_[rs1];
        const std::uint32_t result = left - right;
        set_sub_flags(left, right, result);
        return finish_next();
    }

    case 0x2A: { // AND
        const std::uint32_t result = regs_[rs1] & right;
        regs_[rd] = result;
        logical_flags(result);
        return finish_next();
    }

    case 0x2B: { // OR
        const std::uint32_t result = regs_[rs1] | right;
        regs_[rd] = result;
        logical_flags(result);
        return finish_next();
    }

    case 0x2C: { // XOR
        const std::uint32_t result = regs_[rs1] ^ right;
        regs_[rd] = result;
        logical_flags(result);
        return finish_next();
    }

    case 0x2D: { // NOT
        const std::uint32_t result = ~regs_[rs1];
        regs_[rd] = result;
        logical_flags(result);
        return finish_next();
    }

    case 0x2E: { // NEG
        const std::uint32_t left = 0;
        const std::uint32_t value = regs_[rs1];
        const std::uint32_t result = left - value;
        regs_[rd] = result;
        set_sub_flags(left, value, result);
        return finish_next();
    }

    case 0x30: // SHL
    case 0x31: // SHR
    case 0x32: // SAR
    case 0x33: // ROL
    case 0x34: { // ROR
        const std::uint32_t value = regs_[rs1];
        const std::uint32_t count = right & 31U;
        if (count == 0) {
            regs_[rd] = value;
            set_zn(value);
            set_flag(kFlagV, false);
            return finish_next();
        }

        std::uint32_t result = value;
        bool carry = false;
        switch (opcode) {
        case 0x30:
            carry = ((value >> (32U - count)) & 1U) != 0;
            result = value << count;
            break;
        case 0x31:
            carry = ((value >> (count - 1U)) & 1U) != 0;
            result = value >> count;
            break;
        case 0x32:
            carry = ((value >> (count - 1U)) & 1U) != 0;
            if ((value & 0x8000'0000U) != 0) {
                result = (value >> count) | (~0U << (32U - count));
            } else {
                result = value >> count;
            }
            break;
        case 0x33:
            carry = ((value >> (32U - count)) & 1U) != 0;
            result = (value << count) | (value >> (32U - count));
            break;
        default:
            carry = ((value >> (count - 1U)) & 1U) != 0;
            result = (value >> count) | (value << (32U - count));
            break;
        }

        regs_[rd] = result;
        set_zn(result);
        set_flag(kFlagC, carry);
        set_flag(kFlagV, false);
        return finish_next();
    }

    case 0x38: { // JMP
        const std::uint32_t target = instruction.literal ? payload(instruction) : regs_[rs1];
        if ((target & 3U) != 0) {
            return fail_align(target);
        }
        return finish(target);
    }

    case 0x39: { // JCOND
        const std::uint32_t target = instruction.literal ? payload(instruction) : regs_[rs1];
        if (condition_holds(instruction.field)) {
            if ((target & 3U) != 0) {
                return fail_align(target);
            }
            return finish(target);
        }
        return finish_next();
    }

    case 0x3A: { // CALL
        const std::uint32_t target = instruction.literal ? payload(instruction) : regs_[rs1];
        if ((target & 3U) != 0) {
            return fail_align(target);
        }
        if (!push32(next_pc)) {
            return fail_gp(sp_);
        }
        return finish(target);
    }

    case 0x3B: { // RET
        if ((sp_ & 3U) != 0) {
            return fail_align(sp_);
        }
        std::uint32_t target = 0;
        Fault fault;
        if (!read_virtual32(sp_, Access::Load, target, fault)) {
            return fail_fault(fault);
        }
        if ((target & 3U) != 0) {
            return fail_align(target);
        }
        sp_ += 4;
        return finish(target);
    }

    case 0x3C: { // PUSH
        if (!push32(regs_[rd])) {
            return fail_gp(sp_);
        }
        return finish_next();
    }

    case 0x3D: { // POP
        std::uint32_t value = 0;
        if (!pop32(value)) {
            return fail_gp(sp_);
        }
        regs_[rd] = value;
        return finish_next();
    }

    case 0x3E: { // ADJSP
        const std::uint32_t delta = payload(instruction);
        const std::uint32_t new_sp = sp_ + delta;
        if ((new_sp & 3U) != 0) {
            return fail_align(new_sp);
        }
        sp_ = new_sp;
        return finish_next();
    }

    case 0x3F: { // SETSP (unwinder stack restore)
        const std::uint32_t new_sp = reg(instruction.rs1);
        if ((new_sp & 3U) != 0) {
            return fail_align(new_sp);
        }
        sp_ = new_sp;
        return finish_next();
    }

    case 0x40: // INT
    case 0x41: { // SYSCALL
        const std::uint32_t vector = payload(instruction);
        if (vector < 32 || vector > 255) {
            return fail_gp(instruction_pc);
        }
        if (syscall_handler_ && syscall_handler_(*this, vector)) {
            return finish_next();
        }
        pc_ = next_pc;
        return raise(FaultKind::SoftwareInterrupt, vector, instruction_pc, 0);
    }

    case 0x42: { // IRET
        if (!kernel_mode()) {
            return fail_gp(instruction_pc);
        }
        Fault fault;
        std::uint32_t new_pc = 0;
        std::uint32_t new_flags = 0;
        std::uint32_t new_sp = 0;
        std::uint32_t error = 0;
        if (!read_virtual32(sp_, Access::Load, new_pc, fault) ||
            !read_virtual32(sp_ + 4U, Access::Load, new_flags, fault) ||
            !read_virtual32(sp_ + 8U, Access::Load, new_sp, fault) ||
            !read_virtual32(sp_ + 12U, Access::Load, error, fault)) {
            return fail_fault(fault);
        }
        if ((new_pc & 3U) != 0) {
            return fail_align(new_pc);
        }
        pc_ = new_pc;
        flags_ = new_flags;
        sp_ = new_sp;
        (void)error;
        return {StepResult::Kind::Retired, {}};
    }

    case 0x43: // HALT
        if (!kernel_mode()) {
            return fail_gp(instruction_pc);
        }
        pc_ = next_pc;
        return {StepResult::Kind::Halted, {}};

    case 0x44: // BRK
        pc_ = next_pc;
        return raise(FaultKind::Breakpoint, kVectorBreakpoint, instruction_pc);

    case 0x45: // NOP
        return finish_next();

    case 0x46: { // LL
        std::uint32_t address = 0;
        if (!effective_address(instruction, instruction_pc, address)) {
            return fail_ud();
        }
        if ((address & 3U) != 0) {
            return fail_align(address);
        }
        std::uint32_t value = 0;
        Fault fault;
        if (!load32(address, value, fault)) {
            return fail_fault(fault);
        }
        std::uint32_t physical = 0;
        if (!translate(address, Access::Load, physical, fault)) {
            return fail_fault(fault);
        }
        if (physical < kRamBase ||
            physical >= static_cast<std::uint32_t>(kRamBase + ram_->size())) {
            return fail_gp(address);
        }
        regs_[rd] = value;
        reservation_valid_ = true;
        reservation_address_ = physical & ~3U;
        return finish_next();
    }

    case 0x47: { // SC
        std::uint32_t address = 0;
        if (!effective_address(instruction, instruction_pc, address)) {
            return fail_ud();
        }
        if ((address & 3U) != 0) {
            return fail_align(address);
        }
        std::uint32_t physical = 0;
        Fault fault;
        if (!translate(address, Access::Store, physical, fault)) {
            return fail_fault(fault);
        }
        if (physical < kRamBase ||
            physical >= static_cast<std::uint32_t>(kRamBase + ram_->size())) {
            return fail_gp(address);
        }
        const bool success =
            reservation_valid_ && reservation_address_ == (physical & ~3U);
        if (success && !write_physical32(physical, regs_[rd])) {
            return fail_gp(address);
        }
        reservation_valid_ = false;
        set_flag(kFlagZ, success);
        return finish_next();
    }

    case 0x48: // FENCE
        return finish_next();

    case 0x49: { // RDCR
        if (!kernel_mode()) {
            return fail_gp(instruction_pc);
        }
        std::uint32_t value = 0;
        if (!read_control_register(instruction.field, value)) {
            return fail_gp(instruction_pc);
        }
        regs_[rd] = value;
        return finish_next();
    }

    case 0x4A: { // WRCR
        if (!kernel_mode()) {
            return fail_gp(instruction_pc);
        }
        if (!write_control_register(instruction.field, regs_[rd])) {
            return fail_gp(instruction_pc);
        }
        return finish_next();
    }

    case 0x4B: // RDSP
        regs_[rd] = tsp_;
        return finish_next();

    case 0x4C: // WRSP
        tsp_ = regs_[rd];
        return finish_next();

    case 0x50: // FLOAD
    case 0x51: { // FSTORE
        std::uint32_t address = 0;
        if (!effective_address(instruction, instruction_pc, address)) {
            return fail_ud();
        }
        Fault fault;
        if (opcode == 0x50) {
            if (instruction.literal) {
                std::uint32_t low = 0;
                std::uint32_t high = 0;
                if (!read_virtual32(address, Access::Load, low, fault) ||
                    !read_virtual32(address + 4U, Access::Load, high, fault)) {
                    return fail_fault(fault);
                }
                fpr_[rd] = static_cast<std::uint64_t>(low) |
                           (static_cast<std::uint64_t>(high) << 32);
            } else {
                std::uint32_t low = 0;
                if (!read_virtual32(address, Access::Load, low, fault)) {
                    return fail_fault(fault);
                }
                fpr_[rd] = low;
            }
        } else {
            if (instruction.literal) {
                if (!write_virtual32(address, static_cast<std::uint32_t>(fpr_[rd] & kWordMask),
                                     fault) ||
                    !write_virtual32(address + 4U,
                                     static_cast<std::uint32_t>(fpr_[rd] >> 32), fault)) {
                    return fail_fault(fault);
                }
            } else {
                if (!write_virtual32(address,
                                     static_cast<std::uint32_t>(fpr_[rd] & kWordMask), fault)) {
                    return fail_fault(fault);
                }
            }
        }
        return finish_next();
    }

    case 0x52: // MLOAD
    case 0x53: // MSTORE
    case 0x60: // FP escape
    case 0x61: // M escape
        return (opcode == 0x60) ? execute_fp(instruction, instruction_pc, next_pc)
                                : execute_matrix(instruction, instruction_pc, next_pc);

    case 0x62: { // byte/bit escape
        const std::uint32_t encoded = static_cast<std::uint32_t>(instruction.immediate);
        const std::uint32_t function = (encoded >> 5) & 0x7U;
        const std::uint32_t bit_shift = encoded & 0x1FU;
        const std::uint32_t source = regs_[rs1];
        std::uint32_t result = 0;
        if (function == 0x000) {
            result = (source >> 24) | ((source >> 8) & 0x0000FF00U) |
                     ((source << 8) & 0x00FF0000U) | (source << 24);
        } else if (function == 0x001) {
            const std::uint32_t width = static_cast<std::uint32_t>(instruction.field) + 1U;
            const std::uint32_t shift = bit_shift & 31U;
            const std::uint32_t mask = width >= 32U ? 0xFFFF'FFFFU : ((1U << width) - 1U);
            result = (source >> shift) & mask;
        } else if (function == 0x002) {
            const std::uint32_t width = static_cast<std::uint32_t>(instruction.field) + 1U;
            const std::uint32_t shift = bit_shift & 31U;
            const std::uint32_t mask = width >= 32U ? 0xFFFF'FFFFU : ((1U << width) - 1U);
            result = (regs_[rd] & ~(mask << shift)) | ((source & mask) << shift);
        } else if (function == 0x003) {
            if (source == 0) {
                result = 32;
            } else {
                while ((source & (0x8000'0000U >> result)) == 0) {
                    ++result;
                }
            }
        } else if (function == 0x004) {
            if (source == 0) {
                result = 32;
            } else {
                while ((source & (1U << result)) == 0) {
                    ++result;
                }
            }
        } else if (function == 0x005) {
            std::uint32_t value = source;
            while (value != 0) {
                result += value & 1U;
                value >>= 1;
            }
        } else {
            return fail_ud();
        }
        regs_[rd] = result;
        set_flag(kFlagZ, result == 0);
        set_flag(kFlagN, (result & 0x8000'0000U) != 0);
        set_flag(kFlagC, false);
        set_flag(kFlagV, false);
        return finish_next();
    }

    default:
        return fail_ud();
    }
}

StepResult Machine::execute_fp(const isa::Instruction& instruction,
                               std::uint32_t instruction_pc,
                               std::uint32_t next_pc) {
    const std::uint32_t function = static_cast<std::uint32_t>(instruction.immediate) & 0x7FFU;
    const bool is_double = (function & 0x100U) != 0;
    const std::uint32_t base = function & 0xFFU;
    const std::uint8_t fd = instruction.rd;
    const std::uint8_t fs1 = instruction.rs1;
    const std::uint8_t fs2 = instruction.field;

    auto fail_ud = [&]() {
        return raise(FaultKind::InvalidOpcode, kVectorInvalidOpcode, instruction_pc);
    };
    auto get_float = [&](std::uint8_t index) {
        return float_from_bits(static_cast<std::uint32_t>(fpr_[index] & kWordMask));
    };
    auto get_double = [&](std::uint8_t index) {
        return double_from_bits(fpr_[index]);
    };
    auto put_float = [&](std::uint8_t index, float value) {
        fpr_[index] = canonical_float_bits(value);
    };
    auto put_double = [&](std::uint8_t index, double value) {
        fpr_[index] = canonical_double_bits(value);
    };
    auto finish_next = [&]() {
        pc_ = next_pc;
        return StepResult{StepResult::Kind::Retired, {}};
    };

    switch (base) {
    case 0x00: // FADD
    case 0x01: // FSUB
    case 0x02: // FMUL
    case 0x03: // FDIV
    case 0x05: // FMIN
    case 0x06: { // FMAX
        if (is_double) {
            const double left = get_double(fs1);
            const double right = get_double(fs2);
            double result = 0.0;
            if (base == 0x00) result = left + right;
            if (base == 0x01) result = left - right;
            if (base == 0x02) result = left * right;
            if (base == 0x03) result = left / right;
            if (base == 0x05) {
                if (std::isnan(left) && std::isnan(right)) {
                    result = double_from_bits(kCanonicalF64);
                } else if (std::isnan(left)) {
                    result = right;
                } else if (std::isnan(right)) {
                    result = left;
                } else {
                    result = std::fmin(left, right);
                }
            }
            if (base == 0x06) {
                if (std::isnan(left) && std::isnan(right)) {
                    result = double_from_bits(kCanonicalF64);
                } else if (std::isnan(left)) {
                    result = right;
                } else if (std::isnan(right)) {
                    result = left;
                } else {
                    result = std::fmax(left, right);
                }
            }
            put_double(fd, result);
        } else {
            const float left = get_float(fs1);
            const float right = get_float(fs2);
            float result = 0.0F;
            if (base == 0x00) result = left + right;
            if (base == 0x01) result = left - right;
            if (base == 0x02) result = left * right;
            if (base == 0x03) result = left / right;
            if (base == 0x05) {
                if (std::isnan(left) && std::isnan(right)) {
                    result = float_from_bits(kCanonicalF32);
                } else if (std::isnan(left)) {
                    result = right;
                } else if (std::isnan(right)) {
                    result = left;
                } else {
                    result = std::fmin(left, right);
                }
            }
            if (base == 0x06) {
                if (std::isnan(left) && std::isnan(right)) {
                    result = float_from_bits(kCanonicalF32);
                } else if (std::isnan(left)) {
                    result = right;
                } else if (std::isnan(right)) {
                    result = left;
                } else {
                    result = std::fmax(left, right);
                }
            }
            put_float(fd, result);
        }
        return finish_next();
    }

    case 0x04: { // FSQRT
        if (is_double) {
            put_double(fd, std::sqrt(get_double(fs1)));
        } else {
            put_float(fd, std::sqrt(get_float(fs1)));
        }
        return finish_next();
    }

    case 0x07: // FABS
    case 0x08: { // FNEG
        if (is_double) {
            std::uint64_t bits = fpr_[fs1];
            if (base == 0x07) {
                bits &= ~(1ULL << 63);
            } else {
                bits ^= (1ULL << 63);
            }
            fpr_[fd] = bits;
        } else {
            std::uint32_t bits = static_cast<std::uint32_t>(fpr_[fs1] & kWordMask);
            if (base == 0x07) {
                bits &= 0x7FFF'FFFFU;
            } else {
                bits ^= 0x8000'0000U;
            }
            fpr_[fd] = bits;
        }
        return finish_next();
    }

    case 0x09: // FEQ
    case 0x0A: // FLT
    case 0x0B: { // FLE
        bool result = false;
        if (is_double) {
            const double left = get_double(fs1);
            const double right = get_double(fs2);
            if (base == 0x09) result = left == right;
            if (base == 0x0A) result = left < right;
            if (base == 0x0B) result = left <= right;
        } else {
            const float left = get_float(fs1);
            const float right = get_float(fs2);
            if (base == 0x09) result = left == right;
            if (base == 0x0A) result = left < right;
            if (base == 0x0B) result = left <= right;
        }
        regs_[fd] = result ? 1U : 0U;
        set_flag(kFlagZ, regs_[fd] == 0);
        set_flag(kFlagN, false);
        set_flag(kFlagC, false);
        set_flag(kFlagV, false);
        return finish_next();
    }

    case 0x20: // FCVT.S.D
        put_float(fd, static_cast<float>(get_double(fs1)));
        return finish_next();

    case 0x21: // FCVT.D.S
        put_double(fd, static_cast<double>(get_float(fs1)));
        return finish_next();

    case 0x22: // FCVT.W.S
        regs_[fd] = static_cast<std::uint32_t>(saturate_i32(get_float(fs1)));
        return finish_next();

    case 0x23: // FCVT.WU.S
        regs_[fd] = saturate_u32(get_float(fs1));
        return finish_next();

    case 0x24: // FCVT.S.W
        put_float(fd, static_cast<float>(static_cast<std::int32_t>(regs_[fs1])));
        return finish_next();

    case 0x25: // FCVT.S.WU
        put_float(fd, static_cast<float>(regs_[fs1]));
        return finish_next();

    case 0x26: // FCVT.W.D
        regs_[fd] = static_cast<std::uint32_t>(saturate_i32(get_double(fs1)));
        return finish_next();

    case 0x27: // FCVT.WU.D
        regs_[fd] = saturate_u32(get_double(fs1));
        return finish_next();

    case 0x28: // FCVT.D.W
        put_double(fd, static_cast<double>(static_cast<std::int32_t>(regs_[fs1])));
        return finish_next();

    case 0x29: // FCVT.D.WU
        put_double(fd, static_cast<double>(regs_[fs1]));
        return finish_next();

    case 0x30: // FMV.W.X
        fpr_[fd] = static_cast<std::uint64_t>(regs_[fs1] & kWordMask);
        return finish_next();

    case 0x31: // FMV.X.W
        regs_[fd] = static_cast<std::uint32_t>(fpr_[fs1] & kWordMask);
        return finish_next();

    default:
        return fail_ud();
    }
}

StepResult Machine::execute_matrix(const isa::Instruction& instruction,
                                   std::uint32_t instruction_pc,
                                   std::uint32_t next_pc) {
    const std::uint32_t function = static_cast<std::uint32_t>(instruction.immediate) & 0x7FFU;
    const std::uint8_t fd = instruction.rd;
    const std::uint8_t ta = instruction.rs1;
    const std::uint8_t tb = instruction.field;

    auto fail_ud = [&]() {
        return raise(FaultKind::InvalidOpcode, kVectorInvalidOpcode, instruction_pc);
    };
    auto fail_fault = [&](const Fault& fault) {
        return raise(fault.kind, fault.vector, fault.address, fault.code);
    };
    auto finish_next = [&]() {
        pc_ = next_pc;
        return StepResult{StepResult::Kind::Retired, {}};
    };
    auto read_float = [&](std::uint32_t address, float& value, Fault& fault) {
        std::uint32_t bits = 0;
        if (!read_virtual32(address, Access::Load, bits, fault)) {
            return false;
        }
        value = float_from_bits(bits);
        return true;
    };
    auto write_float = [&](std::uint32_t address, float value, Fault& fault) {
        return write_virtual32(address, float_to_bits(value), fault);
    };
    auto write_result = [&](const float result[16]) {
        for (std::size_t index = 0; index < 16; ++index) {
            tiles_[fd][index] = result[index];
        }
    };

    if (instruction.opcode == 0x52 || instruction.opcode == 0x53) {
        const bool vector = instruction.literal;
        const bool strided = instruction.extension;
        if (vector && strided) {
            return fail_ud();
        }
        if (strided && instruction.immediate != 0) {
            return fail_ud();
        }
        std::uint32_t address = 0;
        if (!effective_address(instruction, instruction_pc, address)) {
            return fail_ud();
        }
        const std::uint32_t stride =
            strided ? instruction.extension_word : 16U;
        if ((address & 3U) != 0 || (strided && (stride & 3U) != 0)) {
            return raise(FaultKind::Alignment, kVectorAlignment, address);
        }

        const std::size_t rows = vector ? 1U : 4U;
        if (instruction.opcode == 0x52) {
            float result[16] = {};
            Fault fault;
            for (std::size_t row = 0; row < rows; ++row) {
                const std::uint32_t row_address =
                    address + static_cast<std::uint32_t>(row) * (strided ? stride : 16U);
                for (std::size_t column = 0; column < 4; ++column) {
                    if (!read_float(row_address + static_cast<std::uint32_t>(column * 4U),
                                    result[row * 4U + column], fault)) {
                        return fail_fault(fault);
                    }
                }
            }
            write_result(result);
        } else {
            Fault fault;
            for (std::size_t row = 0; row < rows; ++row) {
                const std::uint32_t row_address =
                    address + static_cast<std::uint32_t>(row) * (strided ? stride : 16U);
                for (std::size_t column = 0; column < 4; ++column) {
                    if (!write_float(row_address + static_cast<std::uint32_t>(column * 4U),
                                     tiles_[fd][row * 4U + column], fault)) {
                        return fail_fault(fault);
                    }
                }
            }
        }
        return finish_next();
    }

    switch (function) {
    case 0x000: // MMUL
    case 0x001: { // MMAC
        float result[16] = {};
        for (std::size_t row = 0; row < 4; ++row) {
            for (std::size_t column = 0; column < 4; ++column) {
                float sum = function == 0x001 ? tiles_[fd][row * 4U + column] : 0.0F;
                for (std::size_t k = 0; k < 4; ++k) {
                    sum = std::fma(tiles_[ta][row * 4U + k],
                                   tiles_[tb][k * 4U + column], sum);
                }
                result[row * 4U + column] = float_from_bits(canonical_float_bits(sum));
            }
        }
        write_result(result);
        return finish_next();
    }

    case 0x002: { // MVEC
        float result[16] = {};
        for (std::size_t row = 0; row < 4; ++row) {
            float sum = 0.0F;
            for (std::size_t k = 0; k < 4; ++k) {
                sum = std::fma(tiles_[ta][row * 4U + k], tiles_[tb][k * 4U], sum);
            }
            result[row * 4U] = float_from_bits(canonical_float_bits(sum));
        }
        write_result(result);
        return finish_next();
    }

    case 0x003: { // MTRANS
        float result[16] = {};
        for (std::size_t row = 0; row < 4; ++row) {
            for (std::size_t column = 0; column < 4; ++column) {
                result[column * 4U + row] = tiles_[ta][row * 4U + column];
            }
        }
        write_result(result);
        return finish_next();
    }

    case 0x004: { // VDOT
        float sum = 0.0F;
        for (std::size_t index = 0; index < 4; ++index) {
            sum = std::fma(tiles_[ta][index], tiles_[tb][index], sum);
        }
        fpr_[fd] = canonical_float_bits(sum);
        return finish_next();
    }

    case 0x005: { // MPUSH
        if ((tsp_ & 63U) != 0) {
            return raise(FaultKind::Alignment, kVectorAlignment, tsp_);
        }
        if (tsp_ < 64) {
            return raise(FaultKind::GeneralProtection, kVectorGeneralProtection, tsp_);
        }
        const std::uint32_t new_tsp = tsp_ - 64;
        Fault fault;
        for (std::size_t index = 0; index < 16; ++index) {
            if (!write_float(new_tsp + static_cast<std::uint32_t>(index * 4U),
                             tiles_[fd][index], fault)) {
                return fail_fault(fault);
            }
        }
        tsp_ = new_tsp;
        return finish_next();
    }

    case 0x006: { // MPOP
        if ((tsp_ & 63U) != 0) {
            return raise(FaultKind::Alignment, kVectorAlignment, tsp_);
        }
        Fault fault;
        float result[16] = {};
        for (std::size_t index = 0; index < 16; ++index) {
            if (!read_float(tsp_ + static_cast<std::uint32_t>(index * 4U),
                            result[index], fault)) {
                return fail_fault(fault);
            }
        }
        write_result(result);
        tsp_ += 64;
        return finish_next();
    }

    case 0x007: // MDROP
        if ((tsp_ & 63U) != 0) {
            return raise(FaultKind::Alignment, kVectorAlignment, tsp_);
        }
        tsp_ += 64;
        return finish_next();

    default:
        return fail_ud();
    }
}

StepResult Machine::run(std::uint64_t max_steps) {
    for (std::uint64_t index = 0; index < max_steps; ++index) {
        StepResult result = step();
        if (result.kind == StepResult::Kind::FaultDelivered) {
            continue;
        }
        if (result.kind != StepResult::Kind::Retired) {
            return result;
        }
    }
    fault_ = {FaultKind::Limit, 0, pc_, 0};
    return {StepResult::Kind::Raised, fault_};
}

} // namespace kr32::core
