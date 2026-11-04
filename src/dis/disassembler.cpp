#include "kr32/dis/disassembler.h"

#include "kr32/isa/encoding.h"

#include <cstdint>
#include <iomanip>
#include <sstream>
#include <string>
#include <vector>

namespace kr32::disassembler {
namespace {

using SymbolList = std::vector<std::pair<std::string, std::uint32_t>>;

std::uint32_t read32(const std::vector<std::uint8_t>& bytes, std::size_t offset) {
    return static_cast<std::uint32_t>(bytes[offset]) |
           (static_cast<std::uint32_t>(bytes[offset + 1]) << 8) |
           (static_cast<std::uint32_t>(bytes[offset + 2]) << 16) |
           (static_cast<std::uint32_t>(bytes[offset + 3]) << 24);
}

std::string reg(std::uint32_t index) {
    return "R" + std::to_string(index);
}

std::string freg(std::uint32_t index) {
    return "F" + std::to_string(index);
}

std::string treg(std::uint32_t index) {
    return "T" + std::to_string(index);
}

std::uint32_t payload(const isa::Instruction& instruction) {
    if (instruction.extension) {
        return instruction.extension_word;
    }
    return static_cast<std::uint32_t>(static_cast<std::int32_t>(instruction.immediate));
}

std::string value_text(const isa::Instruction& instruction, bool prefix_hash) {
    std::ostringstream out;
    if (prefix_hash) {
        out << '#';
    }
    if (instruction.extension) {
        out << "0x" << std::hex << std::setw(8) << std::setfill('0')
            << instruction.extension_word;
    } else {
        out << std::dec << instruction.immediate;
    }
    return out.str();
}

std::string displacement_text(std::uint32_t displacement) {
    std::ostringstream out;
    out << "0x" << std::hex << std::setw(8) << std::setfill('0') << displacement;
    return out.str();
}

std::string memory_text(const isa::Instruction& instruction, bool indexed) {
    std::ostringstream out;
    if (indexed) {
        out << "[R" << static_cast<unsigned>(instruction.rs1)
            << " + R" << static_cast<unsigned>(instruction.field);
        if (instruction.extension) {
            out << " + " << displacement_text(instruction.extension_word);
        } else if (instruction.immediate > 0) {
            out << " + " << instruction.immediate;
        } else if (instruction.immediate < 0) {
            out << " - " << -instruction.immediate;
        }
        out << ']';
        return out.str();
    }

    switch (instruction.field) {
    case 2:
        out << "[R" << static_cast<unsigned>(instruction.rs1) << ']';
        break;
    case 3:
        out << "[R" << static_cast<unsigned>(instruction.rs1);
        if (instruction.extension) {
            out << " + " << displacement_text(instruction.extension_word);
        } else if (instruction.immediate > 0) {
            out << " + " << instruction.immediate;
        } else if (instruction.immediate < 0) {
            out << " - " << -instruction.immediate;
        }
        out << ']';
        break;
    case 4:
        out << "[SP";
        if (instruction.extension) {
            out << " + " << displacement_text(instruction.extension_word);
        } else if (instruction.immediate > 0) {
            out << " + " << instruction.immediate;
        } else if (instruction.immediate < 0) {
            out << " - " << -instruction.immediate;
        }
        out << ']';
        break;
    case 5:
        out << '[';
        if (instruction.extension) {
            out << displacement_text(instruction.extension_word);
        } else {
            out << "0x" << std::hex << std::setw(8) << std::setfill('0')
                << static_cast<std::uint32_t>(instruction.immediate);
        }
        out << ']';
        break;
    case 6:
        out << "[PC";
        if (instruction.extension) {
            out << " + " << displacement_text(instruction.extension_word);
        } else if (instruction.immediate > 0) {
            out << " + " << instruction.immediate;
        } else if (instruction.immediate < 0) {
            out << " - " << -instruction.immediate;
        }
        out << ']';
        break;
    default:
        out << "<invalid-mode>";
        break;
    }
    return out.str();
}

std::string target_text(const isa::Instruction& instruction, const SymbolList& symbols) {
    if (instruction.literal) {
        const std::uint32_t target = payload(instruction);
        for (const auto& symbol : symbols) {
            if (symbol.second == target) {
                return symbol.first;
            }
        }
        return value_text(instruction, false);
    }
    return reg(instruction.rs1);
}

std::string condition_mnemonic(std::uint32_t condition) {
    static const char* names[] = {
        "JZ", "JNZ", "JCS", "JNC", "JMI", "JPL", "JVS", "JVC",
        "JHI", "JLS", "JGE", "JLT", "JGT", "JLE", "JMP", "J??",
    };
    return condition < 16 ? names[condition] : "J??";
}

std::string format_instruction(const isa::Instruction& instruction,
                               std::uint32_t address,
                               const SymbolList& symbols) {
    (void)address;
    std::ostringstream out;
    const std::uint32_t opcode = instruction.opcode;

    switch (opcode) {
    case 0x00:
        out << "MOV " << reg(instruction.rd) << ", "
            << (instruction.literal ? value_text(instruction, true) : reg(instruction.rs1));
        break;
    case 0x01:
    case 0x03:
    case 0x04:
    case 0x05:
    case 0x06:
    case 0x09: {
        const char* name = "LOAD";
        if (opcode == 0x03) name = "LOAD8U";
        if (opcode == 0x04) name = "LOAD8S";
        if (opcode == 0x05) name = "LOAD16U";
        if (opcode == 0x06) name = "LOAD16S";
        if (opcode == 0x09) name = "LEA";
        out << name << ' ' << reg(instruction.rd) << ", "
            << memory_text(instruction, false);
        break;
    }
    case 0x02:
    case 0x07:
    case 0x08: {
        const char* name = "STORE";
        if (opcode == 0x07) name = "STORE8";
        if (opcode == 0x08) name = "STORE16";
        out << name << ' ' << reg(instruction.rd) << ", "
            << memory_text(instruction, false);
        break;
    }
    case 0x10:
        out << "LOADX " << reg(instruction.rd) << ", " << memory_text(instruction, true);
        break;
    case 0x11:
        out << "STOREX " << reg(instruction.rd) << ", " << memory_text(instruction, true);
        break;
    case 0x12:
        out << "LOAD8UX " << reg(instruction.rd) << ", " << memory_text(instruction, true);
        break;
    case 0x13:
        out << "LOAD8SX " << reg(instruction.rd) << ", " << memory_text(instruction, true);
        break;
    case 0x14:
        out << "LOAD16UX " << reg(instruction.rd) << ", " << memory_text(instruction, true);
        break;
    case 0x15:
        out << "LOAD16SX " << reg(instruction.rd) << ", " << memory_text(instruction, true);
        break;
    case 0x16:
        out << "STORE8X " << reg(instruction.rd) << ", " << memory_text(instruction, true);
        break;
    case 0x17:
        out << "STORE16X " << reg(instruction.rd) << ", " << memory_text(instruction, true);
        break;
    case 0x18:
        out << "SLT " << reg(instruction.rd) << ", " << reg(instruction.rs1) << ", "
            << (instruction.literal ? value_text(instruction, true)
                                   : reg(instruction.field));
        break;
    case 0x19:
        out << "SLTU " << reg(instruction.rd) << ", " << reg(instruction.rs1) << ", "
            << (instruction.literal ? value_text(instruction, true)
                                   : reg(instruction.field));
        break;
    case 0x20: case 0x21: case 0x22: case 0x23: case 0x24: case 0x25:
    case 0x26: case 0x27: case 0x28: case 0x2A: case 0x2B: case 0x2C:
    case 0x30: case 0x31: case 0x32: case 0x33: case 0x34: {
        static const char* names[] = {
            "ADD", "SUB", "MUL", "MULH", "MULHU", "DIV", "DIVS", "MOD",
            "MODS", "CMP", "AND", "OR", "XOR", "SHL", "SHR", "SAR", "ROL",
            "ROR",
        };
        const char* name = "???";
        if (opcode == 0x20) name = names[0];
        if (opcode == 0x21) name = names[1];
        if (opcode == 0x22) name = names[2];
        if (opcode == 0x23) name = names[3];
        if (opcode == 0x24) name = names[4];
        if (opcode == 0x25) name = names[5];
        if (opcode == 0x26) name = names[6];
        if (opcode == 0x27) name = names[7];
        if (opcode == 0x28) name = names[8];
        if (opcode == 0x2A) name = names[10];
        if (opcode == 0x2B) name = names[11];
        if (opcode == 0x2C) name = names[12];
        if (opcode == 0x30) name = names[13];
        if (opcode == 0x31) name = names[14];
        if (opcode == 0x32) name = names[15];
        if (opcode == 0x33) name = names[16];
        if (opcode == 0x34) name = names[17];
        out << name << ' ' << reg(instruction.rd) << ", " << reg(instruction.rs1) << ", "
            << (instruction.literal ? value_text(instruction, true)
                                   : reg(instruction.field));
        break;
    }
    case 0x29:
        out << "CMP " << reg(instruction.rs1) << ", "
            << (instruction.literal ? value_text(instruction, true)
                                   : reg(instruction.field));
        break;
    case 0x2D:
        out << "NOT " << reg(instruction.rd) << ", " << reg(instruction.rs1);
        break;
    case 0x2E:
        out << "NEG " << reg(instruction.rd) << ", " << reg(instruction.rs1);
        break;
    case 0x38:
        out << "JMP " << target_text(instruction, symbols);
        break;
    case 0x39:
        out << condition_mnemonic(instruction.field) << ' '
            << target_text(instruction, symbols);
        break;
    case 0x3A:
        out << "CALL " << target_text(instruction, symbols);
        break;
    case 0x3B:
        out << "RET";
        break;
    case 0x3C:
        out << "PUSH " << reg(instruction.rd);
        break;
    case 0x3D:
        out << "POP " << reg(instruction.rd);
        break;
    case 0x3E:
        out << "ADJSP " << value_text(instruction, true);
        break;
    case 0x3F:
        out << "SETSP " << reg(instruction.rs1);
        break;
    case 0x40:
        out << "INT " << value_text(instruction, true);
        break;
    case 0x41:
        out << "SYSCALL " << value_text(instruction, true);
        break;
    case 0x42:
        out << "IRET";
        break;
    case 0x43:
        out << "HALT";
        break;
    case 0x44:
        out << "BRK";
        break;
    case 0x45:
        out << "NOP";
        break;
    case 0x46:
        out << "LL " << reg(instruction.rd) << ", " << memory_text(instruction, false);
        break;
    case 0x47:
        out << "SC " << reg(instruction.rd) << ", " << memory_text(instruction, false);
        break;
    case 0x48:
        out << "FENCE";
        break;
    case 0x49:
        out << "RDCR " << reg(instruction.rd) << ", CR" << static_cast<unsigned>(instruction.field);
        break;
    case 0x4A:
        out << "WRCR CR" << static_cast<unsigned>(instruction.field) << ", "
            << reg(instruction.rd);
        break;
    case 0x4B:
        out << "RDSP " << reg(instruction.rd);
        break;
    case 0x4C:
        out << "WRSP " << reg(instruction.rd);
        break;

    case 0x50:
    case 0x51: {
        const bool wide = instruction.literal;
        out << (opcode == 0x50 ? "FLOAD" : "FSTORE")
            << (wide ? ".D " : ".W ")
            << freg(instruction.rd) << ", " << memory_text(instruction, false);
        break;
    }

    case 0x52:
    case 0x53: {
        const bool vector = instruction.literal;
        const char* name = "MLOAD";
        if (opcode == 0x52 && vector) name = "VLOAD";
        if (opcode == 0x53) name = "MSTORE";
        if (opcode == 0x53 && vector) name = "VSTORE";
        out << name << ' ' << treg(instruction.rd) << ", "
            << memory_text(instruction, false);
        if (instruction.extension) {
            out << ", #0x" << std::hex << std::setw(8) << std::setfill('0')
                << instruction.extension_word;
        }
        break;
    }

    case 0x62: {
        const std::uint32_t encoded =
            static_cast<std::uint32_t>(instruction.immediate);
        const std::uint32_t function = (encoded >> 5) & 0x7U;
        const std::uint32_t shift = encoded & 0x1FU;
        if (function == 0x000) {
            out << "BSWAP " << reg(instruction.rd) << ", " << reg(instruction.rs1);
        } else if (function == 0x001) {
            out << "BFX " << reg(instruction.rd) << ", " << reg(instruction.rs1)
                << ", #" << shift << ", #"
                << (static_cast<std::uint32_t>(instruction.field) + 1U);
        } else if (function == 0x002) {
            out << "BFI " << reg(instruction.rd) << ", " << reg(instruction.rs1)
                << ", #" << shift << ", #"
                << (static_cast<std::uint32_t>(instruction.field) + 1U);
        } else if (function == 0x003) {
            out << "CLZ " << reg(instruction.rd) << ", " << reg(instruction.rs1);
        } else if (function == 0x004) {
            out << "CTZ " << reg(instruction.rd) << ", " << reg(instruction.rs1);
        } else if (function == 0x005) {
            out << "POPCNT " << reg(instruction.rd) << ", " << reg(instruction.rs1);
        } else {
            out << "BITOP?";
        }
        break;
    }

    case 0x60: {
        const std::uint32_t function =
            static_cast<std::uint32_t>(instruction.immediate) & 0x7FFU;
        const bool wide = (function & 0x100U) != 0;
        const std::uint32_t base = function & 0xFFU;
        const char* suffix = wide ? ".D" : ".S";
        static const char* arithmetic[] = {
            "FADD", "FSUB", "FMUL", "FDIV", "FSQRT", "FMIN", "FMAX",
            "FABS", "FNEG", "FEQ", "FLT", "FLE",
        };
        if (base <= 0x00B) {
            const bool compare = base >= 0x009;
            const bool unary = base == 0x004 || base == 0x007 || base == 0x008;
            out << arithmetic[base] << suffix << ' ';
            if (compare) {
                out << reg(instruction.rd) << ", ";
            } else {
                out << freg(instruction.rd) << ", ";
            }
            out << freg(instruction.rs1);
            if (!unary) {
                out << ", " << freg(instruction.field);
            }
        } else if (base == 0x020) {
            out << "FCVT.S.D " << freg(instruction.rd) << ", " << freg(instruction.rs1);
        } else if (base == 0x021) {
            out << "FCVT.D.S " << freg(instruction.rd) << ", " << freg(instruction.rs1);
        } else if (base == 0x022) {
            out << "FCVT.W.S " << reg(instruction.rd) << ", " << freg(instruction.rs1);
        } else if (base == 0x023) {
            out << "FCVT.WU.S " << reg(instruction.rd) << ", " << freg(instruction.rs1);
        } else if (base == 0x024) {
            out << "FCVT.S.W " << freg(instruction.rd) << ", " << reg(instruction.rs1);
        } else if (base == 0x025) {
            out << "FCVT.S.WU " << freg(instruction.rd) << ", " << reg(instruction.rs1);
        } else if (base == 0x026) {
            out << "FCVT.W.D " << reg(instruction.rd) << ", " << freg(instruction.rs1);
        } else if (base == 0x027) {
            out << "FCVT.WU.D " << reg(instruction.rd) << ", " << freg(instruction.rs1);
        } else if (base == 0x028) {
            out << "FCVT.D.W " << freg(instruction.rd) << ", " << reg(instruction.rs1);
        } else if (base == 0x029) {
            out << "FCVT.D.WU " << freg(instruction.rd) << ", " << reg(instruction.rs1);
        } else if (base == 0x030) {
            out << "FMV.W.X " << freg(instruction.rd) << ", " << reg(instruction.rs1);
        } else if (base == 0x031) {
            out << "FMV.X.W " << reg(instruction.rd) << ", " << freg(instruction.rs1);
        } else {
            out << ".word";
        }
        break;
    }

    case 0x61: {
        const std::uint32_t function =
            static_cast<std::uint32_t>(instruction.immediate) & 0x7FFU;
        switch (function) {
        case 0x000:
            out << "MMUL " << treg(instruction.rd) << ", " << treg(instruction.rs1)
                << ", " << treg(instruction.field);
            break;
        case 0x001:
            out << "MMAC " << treg(instruction.rd) << ", " << treg(instruction.rs1)
                << ", " << treg(instruction.field);
            break;
        case 0x002:
            out << "MVEC " << treg(instruction.rd) << ", " << treg(instruction.rs1)
                << ", " << treg(instruction.field);
            break;
        case 0x003:
            out << "MTRANS " << treg(instruction.rd) << ", " << treg(instruction.rs1);
            break;
        case 0x004:
            out << "VDOT " << freg(instruction.rd) << ", " << treg(instruction.rs1)
                << ", " << treg(instruction.field);
            break;
        case 0x005:
            out << "MPUSH " << treg(instruction.rd);
            break;
        case 0x006:
            out << "MPOP " << treg(instruction.rd);
            break;
        case 0x007:
            out << "MDROP";
            break;
        default:
            out << ".word";
            break;
        }
        break;
    }

    default:
        out << ".word";
        break;
    }
    return out.str();
}

} // namespace

std::string disassemble(const std::vector<std::uint8_t>& bytes, const Options& options) {
    std::ostringstream out;
    const std::uint32_t base = options.base;
    std::uint32_t start = options.start == 0 ? base : options.start;
    if (start < base) {
        start = base;
    }
    std::size_t offset = static_cast<std::size_t>(start - base);
    std::uint32_t words = 0;

    while (offset + 4 <= bytes.size()) {
        if (options.max_words != 0 && words >= options.max_words) {
            break;
        }
        const std::uint32_t address = base + static_cast<std::uint32_t>(offset);
        const std::uint32_t first = read32(bytes, offset);
        const bool has_extension = ((first >> 12) & 1U) != 0;
        std::uint32_t encoded[2] = {first, 0};
        std::size_t count = 1;
        if (has_extension && offset + 8 <= bytes.size()) {
            encoded[1] = read32(bytes, offset + 4);
            count = 2;
        }

        isa::Instruction instruction{};
        const bool decoded = isa::decode(encoded, count, instruction);
        std::ostringstream line;
        line << std::hex << std::setw(8) << std::setfill('0') << address << ": ";
        line << std::setw(8) << first;
        if (count == 2) {
            line << ' ' << std::setw(8) << encoded[1];
        } else {
            line << "         ";
        }
        line << "  ";

        if (decoded && count == 2 && instruction.extension) {
            std::string rendered = format_instruction(instruction, address, options.symbols);
            if (rendered == ".word") {
                std::ostringstream value;
                value << ".word 0x" << std::hex << std::setw(8) << std::setfill('0')
                      << first;
                rendered = value.str();
            }
            line << rendered;
        } else if (decoded && !instruction.extension) {
            std::string rendered = format_instruction(instruction, address, options.symbols);
            if (rendered == ".word") {
                std::ostringstream value;
                value << ".word 0x" << std::hex << std::setw(8) << std::setfill('0')
                      << first;
                rendered = value.str();
            }
            line << rendered;
        } else {
            line << ".word 0x" << std::setw(8) << first;
        }
        out << line.str() << '\n';

        offset += count * 4;
        ++words;
    }

    return out.str();
}

} // namespace kr32::disassembler
