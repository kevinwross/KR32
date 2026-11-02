#pragma once

#include <cstddef>
#include <cstdint>

namespace kr32::isa {

inline constexpr unsigned kOpcodeShift = 25;
inline constexpr unsigned kRdShift = 21;
inline constexpr unsigned kRs1Shift = 17;
inline constexpr unsigned kFieldShift = 13;
inline constexpr unsigned kExtensionShift = 12;
inline constexpr unsigned kLiteralShift = 11;

inline constexpr std::uint32_t kInlineMask = 0x7FF;
inline constexpr std::uint32_t kOpcodeMask = 0x7F;
inline constexpr std::uint32_t kRegisterMask = 0xF;
inline constexpr std::uint8_t kRegisterCount = 16;

// The M0 representation of one instruction word. The fields are the base
// encoding from docs/isa-v1.md; semantics arrive with the M1 core.
struct Instruction {
    std::uint8_t opcode = 0;
    std::uint8_t rd = 0;
    std::uint8_t rs1 = 0;
    std::uint8_t field = 0;
    bool extension = false;
    bool literal = false;
    std::int16_t immediate = 0;
    std::uint32_t extension_word = 0;
};

struct Encoded {
    std::uint32_t words[2] = {0, 0};
    std::uint8_t count = 0;
};

// Encodes one instruction. Returns false for an out-of-range opcode, register,
// or an inline immediate that does not fit in 11 bits.
[[nodiscard]] bool encode(const Instruction& instruction, Encoded& out);

// Decodes one or two words. `count` must be 1 for a 4-byte instruction and 2
// when the base word has E set.
[[nodiscard]] bool decode(const std::uint32_t* words, std::size_t count, Instruction& out);

} // namespace kr32::isa
