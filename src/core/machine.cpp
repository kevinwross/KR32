#include "kr32/core/machine.h"

#include <algorithm>
#include <cstdint>
#include <iterator>
#include <limits>

namespace kr32::core {
namespace {

constexpr std::uint32_t kWordMask = 0xFFFF'FFFFU;

} // namespace

Machine::Machine()
    : Machine(Options{}) {}

Machine::Machine(Options options)
    : rom_(kRomSize, 0) {
    if (options.ram_size == 0) {
        options.ram_size = kDefaultRamSize;
    }
    const std::uint32_t max_ram = kRomBase - kRamBase;
    options.ram_size = std::min(options.ram_size, max_ram);
    ram_.assign(options.ram_size, 0);
    reset();
}

void Machine::reset(std::uint32_t pc, std::uint32_t sp) {
    std::fill(std::begin(regs_), std::end(regs_), 0U);
    pc_ = pc;
    sp_ = (sp == 0) ? stack_top() : sp;
    flags_ = kFlagK;
    ticks_ = 0;
    halted_ = false;
    fault_ = {};
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

    if (begin >= kRamBase && end <= static_cast<std::uint64_t>(kRamBase) + ram_.size()) {
        std::copy(data, data + size, ram_.begin() + static_cast<std::ptrdiff_t>(begin - kRamBase));
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

bool Machine::read8(std::uint32_t address, std::uint8_t& value) const {
    const std::uint64_t wide = address;
    if (wide >= kRamBase && wide < static_cast<std::uint64_t>(kRamBase) + ram_.size()) {
        value = ram_[address - kRamBase];
        return true;
    }
    if (wide >= kRomBase && wide < static_cast<std::uint64_t>(kRomBase) + rom_.size()) {
        value = rom_[address - kRomBase];
        return true;
    }
    return false;
}

bool Machine::write8(std::uint32_t address, std::uint8_t value) {
    const std::uint64_t wide = address;
    if (wide >= kRamBase && wide < static_cast<std::uint64_t>(kRamBase) + ram_.size()) {
        ram_[address - kRamBase] = value;
        return true;
    }
    return false;
}

bool Machine::read16(std::uint32_t address, std::uint16_t& value) const {
    std::uint8_t lo = 0;
    std::uint8_t hi = 0;
    if (!read8(address, lo) || !read8(address + 1, hi)) {
        return false;
    }
    value = static_cast<std::uint16_t>(static_cast<std::uint16_t>(lo) |
                                       (static_cast<std::uint16_t>(hi) << 8));
    return true;
}

bool Machine::write16(std::uint32_t address, std::uint16_t value) {
    return write8(address, static_cast<std::uint8_t>(value & 0xFFU)) &&
           write8(address + 1, static_cast<std::uint8_t>((value >> 8) & 0xFFU));
}

bool Machine::read32(std::uint32_t address, std::uint32_t& value) const {
    std::uint8_t b0 = 0;
    std::uint8_t b1 = 0;
    std::uint8_t b2 = 0;
    std::uint8_t b3 = 0;
    if (!read8(address, b0) || !read8(address + 1, b1) ||
        !read8(address + 2, b2) || !read8(address + 3, b3)) {
        return false;
    }
    value = static_cast<std::uint32_t>(b0) |
            (static_cast<std::uint32_t>(b1) << 8) |
            (static_cast<std::uint32_t>(b2) << 16) |
            (static_cast<std::uint32_t>(b3) << 24);
    return true;
}

bool Machine::write32(std::uint32_t address, std::uint32_t value) {
    return write8(address, static_cast<std::uint8_t>(value & 0xFFU)) &&
           write8(address + 1, static_cast<std::uint8_t>((value >> 8) & 0xFFU)) &&
           write8(address + 2, static_cast<std::uint8_t>((value >> 16) & 0xFFU)) &&
           write8(address + 3, static_cast<std::uint8_t>((value >> 24) & 0xFFU));
}

std::uint32_t Machine::reg(std::size_t index) const {
    return index < 16 ? regs_[index] : 0;
}

void Machine::set_reg(std::size_t index, std::uint32_t value) {
    if (index < 16) {
        regs_[index] = value;
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

StepResult Machine::raise(FaultKind kind,
                          std::uint32_t vector,
                          std::uint32_t address,
                          std::uint32_t code) {
    fault_ = {kind, vector, address, code};
    return {StepResult::Kind::Raised, fault_};
}

StepResult Machine::step() {
    if (halted_) {
        return {StepResult::Kind::Halted, {}};
    }
    if ((pc_ & 3U) != 0) {
        return raise(FaultKind::Alignment, kVectorAlignment, pc_);
    }

    const std::uint32_t instruction_pc = pc_;
    std::uint32_t first = 0;
    if (!read32(pc_, first)) {
        return raise(FaultKind::GeneralProtection, kVectorGeneralProtection, pc_);
    }

    const bool has_extension = ((first >> 12) & 1U) != 0;
    std::uint32_t words[2] = {first, 0};
    std::size_t count = 1;
    if (has_extension) {
        if (!read32(pc_ + 4, words[1])) {
            return raise(FaultKind::GeneralProtection, kVectorGeneralProtection, pc_);
        }
        count = 2;
    }

    isa::Instruction instruction{};
    if (!isa::decode(words, count, instruction)) {
        return raise(FaultKind::InvalidOpcode, kVectorInvalidOpcode, instruction_pc);
    }

    const std::uint32_t next_pc = instruction_pc + (has_extension ? 8U : 4U);
    StepResult result = execute(instruction, instruction_pc, next_pc);
    if (result.kind == StepResult::Kind::Retired) {
        ++ticks_;
    } else if (result.kind == StepResult::Kind::Halted) {
        halted_ = true;
        ++ticks_;
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
    auto load_memory = [&](std::uint32_t address, std::uint8_t& value) {
        return read8(address, value);
    };
    auto store_memory = [&](std::uint32_t address, std::uint8_t value) {
        return write8(address, value);
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
        if (!read32(address, value)) {
            return fail_gp(address);
        }
        regs_[rd] = value;
        return finish_next();
    }

    case 0x02: { // STORE
        std::uint32_t address = 0;
        if (!effective_address(instruction, instruction_pc, address)) {
            return fail_ud();
        }
        if (!write32(address, regs_[rd])) {
            return fail_gp(address);
        }
        return finish_next();
    }

    case 0x03: { // LOAD8U
        std::uint32_t address = 0;
        if (!effective_address(instruction, instruction_pc, address)) {
            return fail_ud();
        }
        std::uint8_t value = 0;
        if (!load_memory(address, value)) {
            return fail_gp(address);
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
        if (!load_memory(address, value)) {
            return fail_gp(address);
        }
        regs_[rd] = static_cast<std::uint32_t>(static_cast<std::int32_t>(static_cast<std::int8_t>(value)));
        return finish_next();
    }

    case 0x05: { // LOAD16U
        std::uint32_t address = 0;
        if (!effective_address(instruction, instruction_pc, address)) {
            return fail_ud();
        }
        std::uint16_t value = 0;
        if (!read16(address, value)) {
            return fail_gp(address);
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
        if (!read16(address, value)) {
            return fail_gp(address);
        }
        regs_[rd] = static_cast<std::uint32_t>(static_cast<std::int32_t>(static_cast<std::int16_t>(value)));
        return finish_next();
    }

    case 0x07: { // STORE8
        std::uint32_t address = 0;
        if (!effective_address(instruction, instruction_pc, address)) {
            return fail_ud();
        }
        if (!store_memory(address, static_cast<std::uint8_t>(regs_[rd] & 0xFFU))) {
            return fail_gp(address);
        }
        return finish_next();
    }

    case 0x08: { // STORE16
        std::uint32_t address = 0;
        if (!effective_address(instruction, instruction_pc, address)) {
            return fail_ud();
        }
        if (!write16(address, static_cast<std::uint16_t>(regs_[rd] & 0xFFFFU))) {
            return fail_gp(address);
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
        if (opcode == 0x10 || opcode == 0x12 || opcode == 0x13 ||
            opcode == 0x14 || opcode == 0x15) {
            if (opcode == 0x10) {
                std::uint32_t value = 0;
                if (!read32(address, value)) {
                    return fail_gp(address);
                }
                regs_[rd] = value;
            } else if (opcode == 0x12) {
                std::uint8_t value = 0;
                if (!load_memory(address, value)) {
                    return fail_gp(address);
                }
                regs_[rd] = value;
            } else if (opcode == 0x13) {
                std::uint8_t value = 0;
                if (!load_memory(address, value)) {
                    return fail_gp(address);
                }
                regs_[rd] = static_cast<std::uint32_t>(
                    static_cast<std::int32_t>(static_cast<std::int8_t>(value)));
            } else if (opcode == 0x14) {
                std::uint16_t value = 0;
                if (!read16(address, value)) {
                    return fail_gp(address);
                }
                regs_[rd] = value;
            } else {
                std::uint16_t value = 0;
                if (!read16(address, value)) {
                    return fail_gp(address);
                }
                regs_[rd] = static_cast<std::uint32_t>(
                    static_cast<std::int32_t>(static_cast<std::int16_t>(value)));
            }
        } else if (opcode == 0x11) {
            if (!write32(address, regs_[rd])) {
                return fail_gp(address);
            }
        } else if (opcode == 0x16) {
            if (!store_memory(address, static_cast<std::uint8_t>(regs_[rd] & 0xFFU))) {
                return fail_gp(address);
            }
        } else {
            if (!write16(address, static_cast<std::uint16_t>(regs_[rd] & 0xFFFFU))) {
                return fail_gp(address);
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

    case 0x23: { // MULH (signed high)
        const std::int64_t product =
            static_cast<std::int64_t>(static_cast<std::int32_t>(regs_[rs1])) *
            static_cast<std::int64_t>(static_cast<std::int32_t>(right));
        const std::uint32_t result = static_cast<std::uint32_t>(static_cast<std::uint64_t>(product) >> 32);
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
        if ((sp_ & 3U) != 0) {
            return fail_align(sp_);
        }
        if (sp_ < 4 || !write32(sp_ - 4, next_pc)) {
            return fail_gp(sp_ - 4);
        }
        sp_ -= 4;
        return finish(target);
    }

    case 0x3B: { // RET
        if ((sp_ & 3U) != 0) {
            return fail_align(sp_);
        }
        std::uint32_t target = 0;
        if (!read32(sp_, target)) {
            return fail_gp(sp_);
        }
        if ((target & 3U) != 0) {
            return fail_align(target);
        }
        sp_ += 4;
        return finish(target);
    }

    case 0x3C: { // PUSH
        if ((sp_ & 3U) != 0) {
            return fail_align(sp_);
        }
        if (sp_ < 4 || !write32(sp_ - 4, regs_[rd])) {
            return fail_gp(sp_ - 4);
        }
        sp_ -= 4;
        return finish_next();
    }

    case 0x3D: { // POP
        if ((sp_ & 3U) != 0) {
            return fail_align(sp_);
        }
        std::uint32_t value = 0;
        if (!read32(sp_, value)) {
            return fail_gp(sp_);
        }
        sp_ += 4;
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

    case 0x43: // HALT
        pc_ = next_pc;
        return {StepResult::Kind::Halted, {}};

    case 0x44: // BRK
        pc_ = next_pc;
        return raise(FaultKind::Breakpoint, kVectorBreakpoint, instruction_pc);

    case 0x45: // NOP
        return finish_next();

    default:
        return fail_ud();
    }
}

StepResult Machine::run(std::uint64_t max_steps) {
    for (std::uint64_t index = 0; index < max_steps; ++index) {
        StepResult result = step();
        if (result.kind != StepResult::Kind::Retired) {
            return result;
        }
    }
    fault_ = {FaultKind::Limit, 0, pc_, 0};
    return {StepResult::Kind::Raised, fault_};
}

} // namespace kr32::core
