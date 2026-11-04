#include "kr32/ui/help.h"

#include <algorithm>
#include <cctype>
#include <iterator>
#include <string>
#include <vector>

namespace kr32::ui {
namespace {

std::string trim_copy(std::string_view text) {
    std::size_t begin = 0;
    while (begin < text.size() &&
           std::isspace(static_cast<unsigned char>(text[begin])) != 0) {
        ++begin;
    }
    std::size_t end = text.size();
    while (end > begin &&
           std::isspace(static_cast<unsigned char>(text[end - 1])) != 0) {
        --end;
    }
    return std::string(text.substr(begin, end - begin));
}

bool is_register(std::string_view operand) {
    if (operand == "SP" || operand == "PC" || operand == "TSP" ||
        operand == "FLAGS") {
        return true;
    }
    if (operand.size() >= 2 && (operand[0] == 'R' || operand[0] == 'F' ||
                                operand[0] == 'T')) {
        return std::all_of(operand.begin() + 1, operand.end(), [](char c) {
            return std::isdigit(static_cast<unsigned char>(c)) != 0;
        });
    }
    return operand.rfind("CR", 0) == 0 && operand.size() > 2 &&
           std::all_of(operand.begin() + 2, operand.end(), [](char c) {
               return std::isdigit(static_cast<unsigned char>(c)) != 0;
           });
}

bool is_branch(std::string_view mnemonic) {
    static const char* names[] = {"JMP", "CALL", "JZ", "JNZ", "JCS", "JNC",
                                  "JMI", "JPL", "JVS", "JVC", "JHI", "JLS",
                                  "JGE", "JLT", "JGT", "JLE"};
    return std::any_of(std::begin(names), std::end(names),
                       [&](const char* name) { return mnemonic == name; });
}

std::string describe_operand(const std::string& raw, const std::string& mnemonic) {
    const std::string operand = trim_copy(raw);
    if (operand.empty()) return {};
    if (operand.front() == '#') {
        if (mnemonic == "SYSCALL" || mnemonic == "INT") {
            return "software vector number (immediate)";
        }
        return "immediate value encoded in the instruction";
    }
    if (operand.front() == '[' && operand.back() == ']') {
        const std::string inner =
            trim_copy(std::string_view(operand).substr(1, operand.size() - 2));
        if (is_register(inner)) {
            return "register indirect: the register holds the address";
        }
        const std::size_t sign = inner.find_first_of("+-");
        if (sign != std::string::npos &&
            is_register(trim_copy(std::string_view(inner).substr(0, sign)))) {
            return "base register plus displacement";
        }
        return "absolute memory address";
    }
    if (operand == "SP" || operand == "PC" || operand == "TSP" ||
        operand == "FLAGS") {
        return "special register (direct)";
    }
    if (operand.rfind("CR", 0) == 0) {
        return "control register (direct)";
    }
    if (operand.size() >= 2 &&
        (operand[0] == 'F' || operand[0] == 'T')) {
        return operand[0] == 'F' ? "floating-point register (direct)"
                                 : "tile register (direct)";
    }
    if (is_register(operand)) {
        if (is_branch(mnemonic)) {
            return "register target: PC is loaded from this register";
        }
        return "general-purpose register (direct)";
    }
    if (operand.rfind("0x", 0) == 0 ||
        (operand.front() >= '0' && operand.front() <= '9')) {
        if (is_branch(mnemonic)) {
            return "branch target address";
        }
        return "absolute constant";
    }
    return "other operand";
}

const char* description_for(const std::string& mnemonic) {
    struct Entry {
        const char* name;
        const char* text;
    };
    static const Entry entries[] = {
        {".word", "Raw 32-bit data word; this is not an executable instruction."},
        {"MOV", "Copy a register or immediate into the destination register."},
        {"LOAD", "Load a 32-bit word from memory into a register."},
        {"STORE", "Store a 32-bit register value to memory."},
        {"LOAD8U", "Load an unsigned byte from memory."},
        {"LOAD8S", "Load a signed byte and sign-extend it."},
        {"LOAD16U", "Load an unsigned 16-bit halfword."},
        {"LOAD16S", "Load a signed 16-bit halfword and sign-extend it."},
        {"STORE8", "Store the low byte of a register to memory."},
        {"STORE16", "Store the low 16 bits of a register to memory."},
        {"LEA", "Compute a memory operand's effective address into a register."},
        {"LOADX", "Load a 32-bit word using the extended addressing form."},
        {"STOREX", "Store a 32-bit word using the extended addressing form."},
        {"LOAD8UX", "Load an unsigned byte using extended addressing."},
        {"LOAD8SX", "Load a signed byte using extended addressing."},
        {"LOAD16UX", "Load an unsigned halfword using extended addressing."},
        {"LOAD16SX", "Load a signed halfword using extended addressing."},
        {"STORE8X", "Store a byte using extended addressing."},
        {"STORE16X", "Store a halfword using extended addressing."},
        {"SLT", "Set the destination to 1 when signed Rs1 is less than the source."},
        {"SLTU", "Set the destination to 1 when unsigned Rs1 is less than the source."},
        {"ADD", "Add two values and set Z, N, C, and V."},
        {"SUB", "Subtract and set Z, N, C, and V; C means no borrow."},
        {"MUL", "Multiply, keeping the low 32 bits and setting Z/N."},
        {"MULH", "Signed multiply, keeping the high 32 bits."},
        {"MULHU", "Unsigned multiply, keeping the high 32 bits."},
        {"DIV", "Unsigned divide; division by zero raises #DIV0."},
        {"DIVS", "Signed divide; division by zero raises #DIV0."},
        {"MOD", "Unsigned remainder; division by zero raises #DIV0."},
        {"MODS", "Signed remainder; division by zero raises #DIV0."},
        {"CMP", "Subtract to set flags without writing a destination."},
        {"AND", "Bitwise AND."},
        {"OR", "Bitwise OR."},
        {"XOR", "Bitwise exclusive OR."},
        {"NOT", "Bitwise complement."},
        {"NEG", "Two's-complement negate."},
        {"SHL", "Logical shift left."},
        {"SHR", "Logical shift right."},
        {"SAR", "Arithmetic shift right, preserving the sign."},
        {"JMP", "Jump to an absolute target or register target."},
        {"JZ", "Jump when Z = 1 (equal/zero)."},
        {"JNZ", "Jump when Z = 0 (not equal/nonzero)."},
        {"JCS", "Jump when carry is set."},
        {"JNC", "Jump when carry is clear."},
        {"JMI", "Jump when negative is set."},
        {"JPL", "Jump when negative is clear."},
        {"JVS", "Jump when signed overflow is set."},
        {"JVC", "Jump when signed overflow is clear."},
        {"JHI", "Jump when unsigned higher (C = 1 and Z = 0)."},
        {"JLS", "Jump when unsigned lower or same."},
        {"JGE", "Jump when signed greater or equal (N = V)."},
        {"JLT", "Jump when signed less than (N != V)."},
        {"JGT", "Jump when signed greater than."},
        {"JLE", "Jump when signed less or equal."},
        {"CALL", "Push the return address and jump to the target."},
        {"RET", "Pop the return address from the stack into PC."},
        {"PUSH", "Push a register; SP decreases by 4."},
        {"POP", "Pop into a register; SP increases by 4."},
        {"PUSHR", "Push R1..R15; R0 is preserved as the ABI return register."},
        {"POPR", "Pop R15..R1; R0 is preserved as the ABI return register."},
        {"PUSHFP", "Push F0..F15 as 128 bytes of FP context."},
        {"POPFP", "Pop F15..F0 as 128 bytes of FP context."},
        {"ADJSP", "Add an immediate to SP."},
        {"INT", "Raise a software interrupt; vector 32..255."},
        {"SYSCALL", "Enter the host-service ABI with a vector."},
        {"IRET", "Return from an interrupt and restore the saved frame."},
        {"HALT", "Stop the core."},
        {"BRK", "Raise a breakpoint exception."},
        {"NOP", "No operation."},
        {"LL", "Load-linked: load a word and set this core's reservation."},
        {"SC", "Store-conditional: store when the reservation still holds."},
        {"FENCE", "Full memory barrier; no-op with sequential consistency."},
        {"RDCR", "Read a control register (kernel only)."},
        {"WRCR", "Write a control register (kernel only)."},
        {"RDSP", "Read the transform stack pointer into a register."},
        {"WRSP", "Write the transform stack pointer."},
        {"FLOAD", "Load a scalar FP value from memory."},
        {"FSTORE", "Store a scalar FP value to memory."},
        {"MLOAD", "Load a 4x4 tile from memory."},
        {"MSTORE", "Store a 4x4 tile to memory."},
        {"VLOAD", "Load a vector into row 0 of a tile."},
        {"VSTORE", "Store the vector in row 0 of a tile."},
        {"FADD", "Floating-point add; .S or .D selects precision."},
        {"FSUB", "Floating-point subtract."},
        {"FMUL", "Floating-point multiply."},
        {"FDIV", "Floating-point divide."},
        {"FSQRT", "Floating-point square root."},
        {"FMIN", "Floating-point minimum."},
        {"FMAX", "Floating-point maximum."},
        {"FABS", "Floating-point absolute value."},
        {"FNEG", "Floating-point negate."},
        {"FEQ", "Floating-point equality test; writes an integer 0/1."},
        {"FLT", "Floating-point less-than test; writes 0/1."},
        {"FLE", "Floating-point less-or-equal test; writes 0/1."},
        {"FCVT.S.D", "Convert double to single precision."},
        {"FCVT.D.S", "Convert single to double precision."},
        {"FCVT.W.S", "Convert single to signed integer."},
        {"FCVT.WU.S", "Convert single to unsigned integer."},
        {"FCVT.S.W", "Convert signed integer to single."},
        {"FCVT.S.WU", "Convert unsigned integer to single."},
        {"FCVT.W.D", "Convert double to signed integer."},
        {"FCVT.WU.D", "Convert double to unsigned integer."},
        {"FCVT.D.W", "Convert signed integer to double."},
        {"FCVT.D.WU", "Convert unsigned integer to double."},
        {"FMV.W.X", "Move a 32-bit GPR pattern into an FP register."},
        {"FMV.X.W", "Move an FP register's low word into a GPR."},
        {"MMUL", "4x4 tile matrix multiply."},
        {"MMAC", "4x4 tile multiply-accumulate."},
        {"MVEC", "Multiply a tile by a vector in row 0."},
        {"MTRANS", "Transpose a 4x4 tile."},
        {"VDOT", "Dot product of two tile row vectors."},
        {"MPUSH", "Push a tile onto the transform stack."},
        {"MPOP", "Pop a tile from the transform stack."},
        {"MDROP", "Drop the top transform-stack tile."},
    };
    for (const Entry& entry : entries) {
        if (mnemonic == entry.name) return entry.text;
    }
    return nullptr;
}

} // namespace

std::string instruction_help(std::string_view mnemonic) {
    std::string name = trim_copy(mnemonic);
    const std::size_t space = name.find(' ');
    if (space != std::string::npos) name.resize(space);
    if (const char* text = description_for(name)) return text;
    const std::size_t dot = name.find('.');
    if (dot != std::string::npos) {
        if (const char* text = description_for(name.substr(0, dot))) return text;
    }
    return "No description is available for this instruction yet.";
}

std::string addressing_help(std::string_view instruction, std::string_view mnemonic) {
    const std::size_t space = instruction.find(' ');
    if (space == std::string_view::npos) {
        return "This instruction has no operands.";
    }
    const std::string operands(instruction.substr(space + 1));
    const std::string name = trim_copy(mnemonic);
    std::string result;
    std::size_t begin = 0;
    int depth = 0;
    bool first = true;
    while (begin <= operands.size()) {
        std::size_t end = begin;
        while (end < operands.size()) {
            if (operands[end] == '[') ++depth;
            if (operands[end] == ']') --depth;
            if (operands[end] == ',' && depth == 0) break;
            ++end;
        }
        const std::string piece = trim_copy(std::string_view(operands).substr(
            begin, end - begin));
        if (!piece.empty()) {
            if (!first) result += "; ";
            result += piece + ": " + describe_operand(piece, name);
            first = false;
        }
        if (end >= operands.size()) break;
        begin = end + 1;
    }
    return first ? "This instruction has no operands." : result;
}

} // namespace kr32::ui
