#include "kr32/isa/encoding.h"

namespace kr32::isa {
namespace {

std::int16_t sign_extend_11(std::uint32_t value) {
    value &= kInlineMask;
    if ((value & 0x400U) != 0U) {
        value |= ~kInlineMask;
    }
    return static_cast<std::int16_t>(value);
}

bool valid_register(std::uint8_t value) {
    return value < kRegisterCount;
}

} // namespace

bool encode(const Instruction& instruction, Encoded& out) {
    out = {};

    if ((static_cast<std::uint32_t>(instruction.opcode) & ~kOpcodeMask) != 0U) {
        return false;
    }
    if (!valid_register(instruction.rd) || !valid_register(instruction.rs1) ||
        !valid_register(instruction.field)) {
        return false;
    }

    const std::uint32_t base =
        (static_cast<std::uint32_t>(instruction.opcode) << kOpcodeShift) |
        (static_cast<std::uint32_t>(instruction.rd) << kRdShift) |
        (static_cast<std::uint32_t>(instruction.rs1) << kRs1Shift) |
        (static_cast<std::uint32_t>(instruction.field) << kFieldShift) |
        (instruction.extension ? (1U << kExtensionShift) : 0U) |
        (instruction.literal ? (1U << kLiteralShift) : 0U);

    if (instruction.extension) {
        out.words[0] = base;
        out.words[1] = instruction.extension_word;
        out.count = 2;
        return true;
    }

    if (instruction.immediate < -1024 || instruction.immediate > 1023) {
        return false;
    }

    out.words[0] = base | (static_cast<std::uint32_t>(instruction.immediate) & kInlineMask);
    out.count = 1;
    return true;
}

bool decode(const std::uint32_t* words, std::size_t count, Instruction& out) {
    if (words == nullptr || count == 0 || count > 2) {
        return false;
    }

    const std::uint32_t word = words[0];
    out = {};

    out.opcode = static_cast<std::uint8_t>((word >> kOpcodeShift) & kOpcodeMask);
    out.rd = static_cast<std::uint8_t>((word >> kRdShift) & kRegisterMask);
    out.rs1 = static_cast<std::uint8_t>((word >> kRs1Shift) & kRegisterMask);
    out.field = static_cast<std::uint8_t>((word >> kFieldShift) & kRegisterMask);
    out.extension = ((word >> kExtensionShift) & 1U) != 0U;
    out.literal = ((word >> kLiteralShift) & 1U) != 0U;

    if (out.extension) {
        if (count < 2) {
            return false;
        }
        out.extension_word = words[1];
        out.immediate = 0;
    } else {
        out.immediate = sign_extend_11(word);
    }

    return true;
}

} // namespace kr32::isa
