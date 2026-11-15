#include "kr32/asm/optimizer.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdint>
#include <cstdlib>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <vector>

namespace kr32::assembler {
namespace {

std::string trim(std::string_view text) {
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

bool is_label(std::string_view line) {
    const std::string text = trim(line);
    return !text.empty() && text.back() == ':';
}

bool parse_register(std::string_view text, int& reg) {
    const std::string value = trim(text);
    if (value.size() < 2 || (value[0] != 'R' && value[0] != 'r')) {
        return false;
    }
    char* end = nullptr;
    const long number = std::strtol(value.c_str() + 1, &end, 10);
    if (end == nullptr || *end != '\0' || number < 0 || number > 15) {
        return false;
    }
    reg = static_cast<int>(number);
    return true;
}

bool parse_register_address(std::string_view text, int& reg) {
    const std::string value = trim(text);
    if (value.size() < 3 || value.front() != '[' || value.back() != ']') {
        return false;
    }
    std::string inside = value.substr(1, value.size() - 2);
    inside.erase(std::remove_if(inside.begin(), inside.end(),
                                [](unsigned char character) {
                                    return std::isspace(character) != 0;
                                }),
                 inside.end());
    const std::size_t separator = inside.find_first_of("+-");
    if (separator != std::string::npos) {
        inside.resize(separator);
    }
    return parse_register(inside, reg);
}

bool parse_register_offset(std::string_view text, int& reg, int& offset) {
    const std::string value = trim(text);
    if (value.size() < 3 || value.front() != '[' || value.back() != ']') {
        return false;
    }
    std::string inside = value.substr(1, value.size() - 2);
    inside.erase(std::remove_if(inside.begin(), inside.end(),
                                [](unsigned char character) {
                                    return std::isspace(character) != 0;
                                }),
                 inside.end());
    offset = 0;
    const std::size_t separator = inside.find_first_of("+-");
    if (separator == std::string::npos) {
        return parse_register(inside, reg);
    }
    if (!parse_register(inside.substr(0, separator), reg)) {
        return false;
    }
    const int sign = inside[separator] == '-' ? -1 : 1;
    const std::string number = inside.substr(separator + 1);
    if (number.empty()) {
        return false;
    }
    char* end = nullptr;
    const long parsed = std::strtol(number.c_str(), &end, 10);
    if (end == nullptr || *end != '\0') {
        return false;
    }
    offset = sign * static_cast<int>(parsed);
    return true;
}

bool parse_frame_slot(std::string_view text, int& slot) {
    const std::string value = trim(text);
    if (value.size() < 4 || value.front() != '[' || value.back() != ']') {
        return false;
    }
    std::string inside = value.substr(1, value.size() - 2);
    inside.erase(std::remove_if(inside.begin(), inside.end(),
                                [](unsigned char character) {
                                    return std::isspace(character) != 0;
                                }),
                 inside.end());
    if (inside == "R7") {
        slot = 0;
        return true;
    }
    if (inside.rfind("R7+", 0) != 0) {
        return false;
    }
    const char* start = inside.c_str() + 3;
    char* end = nullptr;
    const long number = std::strtol(start, &end, 10);
    if (end == nullptr || *end != '\0') {
        return false;
    }
    slot = static_cast<int>(number);
    return true;
}

struct Instruction {
    std::string mnemonic;
    std::vector<std::string> operands;
};

bool parse_instruction(std::string_view line, Instruction& instruction) {
    const std::string text = trim(line);
    if (text.empty() || text.front() == '.' || text.back() == ':') {
        return false;
    }
    const std::size_t space = text.find_first_of(" \t");
    if (space == std::string::npos) {
        instruction.mnemonic = text;
        instruction.operands.clear();
        return true;
    }
    instruction.mnemonic = text.substr(0, space);
    for (char& character : instruction.mnemonic) {
        character = static_cast<char>(
            std::toupper(static_cast<unsigned char>(character)));
    }
    std::string rest = text.substr(space);
    instruction.operands.clear();
    std::size_t begin = 0;
    while (begin <= rest.size()) {
        const std::size_t comma = rest.find(',', begin);
        instruction.operands.push_back(trim(rest.substr(
            begin, comma == std::string::npos ? std::string::npos
                                               : comma - begin)));
        if (comma == std::string::npos) {
            break;
        }
        begin = comma + 1;
    }
    return true;
}

std::string format_instruction(const Instruction& instruction) {
    std::string text = "    " + instruction.mnemonic;
    for (std::size_t index = 0; index < instruction.operands.size(); ++index) {
        text += index == 0 ? " " : ", ";
        text += instruction.operands[index];
    }
    return text;
}

bool is_branch(std::string_view mnemonic) {
    return !mnemonic.empty() && mnemonic.front() == 'J';
}

bool is_call(std::string_view mnemonic) {
    return mnemonic == "CALL" || mnemonic == "SYSCALL";
}

// RETW is the compiler's wide return: same encoding as RET, but the optimizer
// keeps R1 live for it because a 64-bit result uses both words.
bool is_return_mnemonic(std::string_view mnemonic) {
    return mnemonic == "RET" || mnemonic == "RETW";
}

bool writes_first_operand(const Instruction& instruction) {
    if (instruction.operands.empty()) {
        return false;
    }
    if (instruction.mnemonic == "CMP" || instruction.mnemonic == "STORE" ||
        instruction.mnemonic == "STORE8" || instruction.mnemonic == "STORE16" ||
        instruction.mnemonic == "PUSH" || instruction.mnemonic == "FENCE" ||
        instruction.mnemonic == "NOP" ||
        is_return_mnemonic(instruction.mnemonic) ||
        instruction.mnemonic == "HALT" || is_call(instruction.mnemonic) ||
        is_branch(instruction.mnemonic)) {
        return false;
    }
    int reg = 0;
    return parse_register(instruction.operands[0], reg);
}

void remove_register(std::map<int, std::string>& slots, int reg) {
    for (auto iterator = slots.begin(); iterator != slots.end();) {
        int source = 0;
        if (parse_register(iterator->second, source) && source == reg) {
            iterator = slots.erase(iterator);
        } else {
            ++iterator;
        }
    }
}

struct RegisterReadCounts {
    std::array<int, 16> reads{};
};

void count_reads(const Instruction& instruction, RegisterReadCounts& counts) {
    if (is_call(instruction.mnemonic)) {
        for (const std::string& operand : instruction.operands) {
            int reg = 0;
            if (parse_register(operand, reg)) {
                ++counts.reads[static_cast<std::size_t>(reg)];
            }
        }
        for (int reg = 0; reg < 4; ++reg) {
            ++counts.reads[static_cast<std::size_t>(reg)];
        }
        return;
    }
    if (instruction.mnemonic == "RETW") {
        ++counts.reads[0];
        ++counts.reads[1];
        return;
    }
    if (instruction.mnemonic == "RET") {
        ++counts.reads[0];
        return;
    }
    const std::size_t first =
        writes_first_operand(instruction) ? 1 : 0;
    for (std::size_t index = first; index < instruction.operands.size(); ++index) {
        int reg = 0;
        if (parse_register(instruction.operands[index], reg)) {
            ++counts.reads[static_cast<std::size_t>(reg)];
        }
    }
}

int remove_forwarded_loads(std::vector<std::string>& lines, int& rewritten) {
    int removed = 0;
    std::map<int, std::string> slots;
    const auto clear = [&]() { slots.clear(); };
    for (std::string& line : lines) {
        Instruction instruction;
        if (!parse_instruction(line, instruction)) {
            clear();
            continue;
        }
        if (is_call(instruction.mnemonic) || is_branch(instruction.mnemonic)) {
            clear();
            continue;
        }
        if (instruction.mnemonic == "STORE" &&
            instruction.operands.size() == 2) {
            int source = 0;
            int slot = 0;
            if (parse_register(instruction.operands[0], source) &&
                parse_frame_slot(instruction.operands[1], slot)) {
                slots[slot] = instruction.operands[0];
            } else {
                clear();
            }
            continue;
        }
        if (instruction.mnemonic == "STORE8" ||
            instruction.mnemonic == "STORE16" ||
            (instruction.mnemonic == "STORE" &&
             instruction.operands.size() != 2)) {
            clear();
            continue;
        }
        if (instruction.mnemonic == "LOAD" &&
            instruction.operands.size() == 2) {
            int destination = 0;
            int slot = 0;
            if (parse_register(instruction.operands[0], destination) &&
                parse_frame_slot(instruction.operands[1], slot)) {
                const auto found = slots.find(slot);
                if (found != slots.end()) {
                    int source = 0;
                    if (parse_register(found->second, source) &&
                        source == destination) {
                        line.clear();
                        ++removed;
                    } else if (parse_register(found->second, source)) {
                        line = "    MOV " + instruction.operands[0] + ", " +
                               found->second;
                        ++rewritten;
                    }
                }
                remove_register(slots, destination);
            }
            int loaded = 0;
            if (parse_register(instruction.operands[0], loaded)) {
                remove_register(slots, loaded);
            }
            continue;
        }
        int destination = 0;
        if (writes_first_operand(instruction) &&
            parse_register(instruction.operands[0], destination)) {
            remove_register(slots, destination);
        }
    }
    return removed;
}
int fold_immediate_consumers(std::vector<std::string>& lines) {
    std::vector<RegisterReadCounts> function_counts;
    std::vector<int> function_of_line(lines.size(), 0);
    int function = -1;
    for (std::size_t index = 0; index < lines.size(); ++index) {
        if (trim(lines[index]) == ".text") {
            ++function;
            function_counts.emplace_back();
        }
        if (function < 0) {
            function = 0;
            function_counts.emplace_back();
        }
        function_of_line[index] = function;
        Instruction instruction;
        if (parse_instruction(lines[index], instruction)) {
            count_reads(instruction,
                        function_counts[static_cast<std::size_t>(function)]);
        }
    }

    int removed = 0;
    struct Immediate {
        long long value = 0;
        std::size_t definition = 0;
        int owner = 0;
    };
    std::map<int, Immediate> immediates;
    for (std::size_t index = 0; index < lines.size(); ++index) {
        if (trim(lines[index]) == ".text") {
            immediates.clear();
            continue;
        }
        Instruction instruction;
        if (!parse_instruction(lines[index], instruction)) {
            if (is_label(lines[index])) {
                immediates.clear();
            }
            continue;
        }
        if (is_branch(instruction.mnemonic) || is_call(instruction.mnemonic)) {
            immediates.clear();
            continue;
        }

        const auto consume = [&](std::size_t operand, const std::string& reg) {
            int reg_number = 0;
            if (!parse_register(reg, reg_number)) {
                return false;
            }
            const auto found = immediates.find(reg_number);
            if (found == immediates.end()) {
                return false;
            }
            instruction.operands[operand] =
                "#" + std::to_string(found->second.value);
            const std::size_t definition = found->second.definition;
            const RegisterReadCounts& counts =
                function_counts[static_cast<std::size_t>(
                    function_of_line[index])];
            if (found->second.owner == reg_number &&
                counts.reads[static_cast<std::size_t>(reg_number)] == 1 &&
                definition < lines.size() && !lines[definition].empty()) {
                lines[definition].clear();
                ++removed;
            }
            immediates.erase(found);
            return true;
        };

        bool rewritten = false;
        if (instruction.mnemonic == "CMP" &&
            instruction.operands.size() == 2) {
            rewritten = consume(1, instruction.operands[1]);
        } else if ((instruction.mnemonic == "ADD" ||
                    instruction.mnemonic == "SUB" ||
                    instruction.mnemonic == "AND" ||
                    instruction.mnemonic == "OR" ||
                    instruction.mnemonic == "XOR" ||
                    instruction.mnemonic == "SHL" ||
                    instruction.mnemonic == "SHR" ||
                    instruction.mnemonic == "SAR") &&
                   instruction.operands.size() == 3) {
            rewritten = consume(2, instruction.operands[2]);
        }
        if (rewritten) {
            lines[index] = format_instruction(instruction);
        }

        if (instruction.mnemonic == "MOV" &&
            instruction.operands.size() == 2) {
            int destination = 0;
            if (parse_register(instruction.operands[0], destination)) {
                const std::string& source = instruction.operands[1];
                if (!source.empty() && source.front() == '#') {
                    const long long value =
                        std::strtoll(source.c_str() + 1, nullptr, 0);
                    immediates[destination] = {value, index, destination};
                    continue;
                }
                int source_register = 0;
                if (parse_register(source, source_register)) {
                    const auto found = immediates.find(source_register);
                    if (found != immediates.end()) {
                        immediates[destination] = found->second;
                    } else {
                        immediates.erase(destination);
                    }
                }
                continue;
            }
        }
        if (writes_first_operand(instruction)) {
            int destination = 0;
            if (parse_register(instruction.operands[0], destination)) {
                immediates.erase(destination);
            }
        }
    }
    return removed;
}

bool register_dead_after(const std::vector<std::string>& lines,
                         std::size_t start, int reg) {
    for (std::size_t index = start; index < lines.size(); ++index) {
        if (trim(lines[index]) == ".text") {
            return true;
        }
        Instruction instruction;
        if (!parse_instruction(lines[index], instruction)) {
            if (is_label(lines[index])) {
                return false;
            }
            continue;
        }
        if (is_branch(instruction.mnemonic)) {
            return false;
        }
        if (is_call(instruction.mnemonic)) {
            return false;
        }
        const std::size_t first =
            writes_first_operand(instruction) ? 1 : 0;
        for (std::size_t operand = first;
             operand < instruction.operands.size(); ++operand) {
            int used = 0;
            if ((parse_register(instruction.operands[operand], used) ||
                 parse_register_address(instruction.operands[operand], used)) &&
                used == reg) {
                return false;
            }
        }
        int destination = 0;
        if (writes_first_operand(instruction) &&
            parse_register(instruction.operands[0], destination) &&
            destination == reg) {
            return true;
        }
    }
    return true;
}

bool is_conditional_jump(const std::string& mnemonic) {
    return mnemonic == "JZ" || mnemonic == "JNZ" ||
           mnemonic == "JLT" || mnemonic == "JLE" ||
           mnemonic == "JGT" || mnemonic == "JGE" ||
           mnemonic == "JCS" || mnemonic == "JNC" ||
           mnemonic == "JLS" || mnemonic == "JHI";
}

using RegisterMask = std::uint16_t;

RegisterMask bit_for_register(int reg) {
    return static_cast<RegisterMask>(1U << reg);
}

void add_operand_register(const std::string& operand, RegisterMask& mask) {
    int reg = 0;
    if (parse_register(operand, reg) ||
        parse_register_address(operand, reg)) {
        mask = static_cast<RegisterMask>(mask | bit_for_register(reg));
    }
}

void instruction_uses_defs(const Instruction& instruction,
                           RegisterMask& uses, RegisterMask& defs) {
    uses = 0;
    defs = 0;
    // A jump to a named target leaves the function: the target reads the
    // argument registers, so they are live even though this block has no
    // successor. Without this, a copy into R0..R3 before a tail call is
    // deleted as dead and the target receives a stale argument.
    if (instruction.mnemonic == "JMP" && instruction.operands.size() == 1 &&
        !instruction.operands[0].empty() &&
        instruction.operands[0][0] != '.') {
        for (int reg = 0; reg < 4; ++reg) {
            uses = static_cast<RegisterMask>(uses | bit_for_register(reg));
        }
        return;
    }
    if (instruction.mnemonic == "RETW") {
        uses = bit_for_register(0) | bit_for_register(1);
        return;
    }
    if (instruction.mnemonic == "RET") {
        uses = bit_for_register(0);
        return;
    }
    if (is_call(instruction.mnemonic)) {
        for (const std::string& operand : instruction.operands) {
            add_operand_register(operand, uses);
        }
        // A call may carry its integer argument count as "#N"; without it the
        // callee might read any of R0..R3.
        int argument_registers = 4;
        if (instruction.mnemonic == "CALL" &&
            instruction.operands.size() >= 2 &&
            instruction.operands[1].size() > 1 &&
            instruction.operands[1][0] == '#') {
            try {
                argument_registers =
                    std::stoi(instruction.operands[1].substr(1));
            } catch (...) {
                argument_registers = 4;
            }
            if (argument_registers < 0) {
                argument_registers = 0;
            }
            if (argument_registers > 4) {
                argument_registers = 4;
            }
        }
        for (int reg = 0; reg < argument_registers; ++reg) {
            uses = static_cast<RegisterMask>(
                uses | bit_for_register(reg));
        }
        for (int reg = 0; reg < 7; ++reg) {
            defs = static_cast<RegisterMask>(
                defs | bit_for_register(reg));
        }
        for (int reg = 12; reg < 16; ++reg) {
            defs = static_cast<RegisterMask>(
                defs | bit_for_register(reg));
        }
        return;
    }
    const std::size_t first =
        writes_first_operand(instruction) ? 1 : 0;
    if (first == 1) {
        int destination = 0;
        if (parse_register(instruction.operands[0], destination)) {
            defs = bit_for_register(destination);
        }
    }
    for (std::size_t index = first;
         index < instruction.operands.size(); ++index) {
        add_operand_register(instruction.operands[index], uses);
    }
}

void frame_slot_effects(const Instruction& instruction,
                        std::set<int>& uses, std::set<int>& defs) {
    if (instruction.operands.size() != 2) {
        return;
    }
    int slot = 0;
    if (!parse_frame_slot(instruction.operands[1], slot)) {
        return;
    }
    const bool full_load = instruction.mnemonic == "LOAD";
    const bool partial_load = instruction.mnemonic == "LOAD8S" ||
                              instruction.mnemonic == "LOAD8U" ||
                              instruction.mnemonic == "LOAD16S" ||
                              instruction.mnemonic == "LOAD16U";
    const bool fp_load = instruction.mnemonic == "FLOAD.W" ||
                         instruction.mnemonic == "FLOAD.D";
    const bool full_store = instruction.mnemonic == "STORE";
    const bool partial_store = instruction.mnemonic == "STORE8" ||
                               instruction.mnemonic == "STORE16";
    const bool fp_store = instruction.mnemonic == "FSTORE.W" ||
                          instruction.mnemonic == "FSTORE.D";
    if (full_load || partial_load || fp_load) {
        uses.insert(slot);
    } else if (full_store) {
        defs.insert(slot);
    } else if (partial_store || fp_store) {
        uses.insert(slot);
        defs.insert(slot);
    }
}

struct CfgBlock {
    std::size_t begin = 0;
    std::size_t end = 0;
    std::vector<std::size_t> successors;
    RegisterMask uses = 0;
    RegisterMask defs = 0;
    RegisterMask live_in = 0;
    RegisterMask live_out = 0;
    std::set<int> slot_uses;
    std::set<int> slot_defs;
};

struct CfgFunction {
    std::size_t begin = 0;
    std::size_t end = 0;
    std::vector<CfgBlock> blocks;
    std::map<std::string, std::size_t> labels;
};

bool is_function_label(std::string_view line) {
    const std::string text = trim(line);
    return !text.empty() && text.back() == ':' && text.front() != '.';
}

std::vector<CfgFunction> build_cfgs(const std::vector<std::string>& lines) {
    std::vector<std::pair<std::size_t, std::size_t>> ranges;
    for (std::size_t index = 0; index < lines.size(); ++index) {
        if (!is_function_label(lines[index])) {
            continue;
        }
        if (!ranges.empty()) {
            ranges.back().second = index;
        }
        ranges.emplace_back(index, lines.size());
    }
    if (ranges.empty()) {
        ranges.emplace_back(0, lines.size());
    } else if (ranges.front().first != 0) {
        ranges.insert(ranges.begin(), {0, ranges.front().first});
    }

    std::vector<CfgFunction> functions;
    for (const auto& [function_begin, function_end] : ranges) {
        if (function_begin >= function_end) {
            continue;
        }
        CfgFunction function;
        function.begin = function_begin;
        function.end = function_end;

        std::vector<std::size_t> starts{function_begin};
        bool previous_terminated = false;
        for (std::size_t index = function_begin; index < function_end;
             ++index) {
            const std::string text = trim(lines[index]);
            if (text.empty()) {
                continue;
            }
            if (index != function_begin &&
                (is_label(text) || previous_terminated)) {
                if (starts.back() != index) {
                    starts.push_back(index);
                }
            }
            Instruction instruction;
            if (parse_instruction(text, instruction)) {
                previous_terminated =
                    is_branch(instruction.mnemonic) ||
                    is_return_mnemonic(instruction.mnemonic) ||
                    instruction.mnemonic == "HALT";
            } else {
                previous_terminated = false;
            }
        }

        for (std::size_t block_index = 0; block_index < starts.size();
             ++block_index) {
            CfgBlock block;
            block.begin = starts[block_index];
            block.end = block_index + 1 < starts.size()
                            ? starts[block_index + 1]
                            : function_end;
            function.blocks.push_back(block);
        }
        for (std::size_t block_index = 0;
             block_index < function.blocks.size(); ++block_index) {
            CfgBlock& block = function.blocks[block_index];
            for (std::size_t index = block.begin; index < block.end;
                 ++index) {
                const std::string text = trim(lines[index]);
                if (is_label(text)) {
                    function.labels[text.substr(0, text.size() - 1)] =
                        block_index;
                    continue;
                }
                Instruction instruction;
                if (!parse_instruction(text, instruction)) {
                    continue;
                }
                RegisterMask uses = 0;
                RegisterMask defs = 0;
                instruction_uses_defs(instruction, uses, defs);
                // A block's use set is the reads that happen before a
                // definition inside the block; a later read is supplied by
                // the definition, not by the predecessors.
                block.uses = static_cast<RegisterMask>(
                    block.uses |
                    (uses & static_cast<RegisterMask>(~block.defs)));
                block.defs = static_cast<RegisterMask>(block.defs | defs);
                frame_slot_effects(instruction, block.slot_uses,
                                   block.slot_defs);
            }
        }
        for (std::size_t block_index = 0;
             block_index < function.blocks.size(); ++block_index) {
            CfgBlock& block = function.blocks[block_index];
            Instruction last;
            bool has_last = false;
            for (std::size_t index = block.end; index > block.begin;
                 --index) {
                if (parse_instruction(lines[index - 1], last)) {
                    has_last = true;
                    break;
                }
            }
            const auto add_target = [&](const std::string& label) {
                const auto found = function.labels.find(trim(label));
                if (found != function.labels.end()) {
                    if (std::find(block.successors.begin(),
                                  block.successors.end(),
                                  found->second) ==
                        block.successors.end()) {
                        block.successors.push_back(found->second);
                    }
                }
            };
            const std::size_t fallthrough = block_index + 1;
            if (has_last && is_branch(last.mnemonic)) {
                if (!last.operands.empty()) {
                    add_target(last.operands[0]);
                }
                if (last.mnemonic != "JMP" &&
                    fallthrough < function.blocks.size()) {
                    block.successors.push_back(fallthrough);
                }
            } else if (!has_last ||
                       (!is_return_mnemonic(last.mnemonic) &&
                        last.mnemonic != "HALT")) {
                if (fallthrough < function.blocks.size()) {
                    block.successors.push_back(fallthrough);
                }
            }
        }
        functions.push_back(std::move(function));
    }
    return functions;
}

struct LivenessInfo {
    std::vector<RegisterMask> live_after;
};

LivenessInfo compute_register_liveness(
    const std::vector<std::string>& lines) {
    LivenessInfo info;
    info.live_after.assign(lines.size(), 0);
    const std::vector<CfgFunction> functions = build_cfgs(lines);
    for (const CfgFunction& function : functions) {
        std::vector<CfgBlock> blocks = function.blocks;
        bool changed = true;
        int rounds = 0;
        while (changed && rounds++ < 256) {
            changed = false;
            for (std::size_t block_index = blocks.size();
                 block_index > 0; --block_index) {
                CfgBlock& block = blocks[block_index - 1];
                RegisterMask live_out = 0;
                for (const std::size_t successor : block.successors) {
                    if (successor < blocks.size()) {
                        live_out = static_cast<RegisterMask>(
                            live_out | blocks[successor].live_in);
                    }
                }
                const RegisterMask live_in = static_cast<RegisterMask>(
                    block.uses |
                    (live_out & static_cast<RegisterMask>(~block.defs)));
                if (live_in != block.live_in ||
                    live_out != block.live_out) {
                    block.live_in = live_in;
                    block.live_out = live_out;
                    changed = true;
                }
            }
        }
        for (const CfgBlock& block : blocks) {
            RegisterMask live = block.live_out;
            for (std::size_t index = block.end; index > block.begin;
                 --index) {
                info.live_after[index - 1] = live;
                Instruction instruction;
                if (!parse_instruction(lines[index - 1], instruction)) {
                    continue;
                }
                RegisterMask uses = 0;
                RegisterMask defs = 0;
                instruction_uses_defs(instruction, uses, defs);
                live = static_cast<RegisterMask>(
                    uses | (live & static_cast<RegisterMask>(~defs)));
            }
        }
    }
    return info;
}

bool register_live_after(const LivenessInfo& info, std::size_t line,
                         int reg) {
    if (line >= info.live_after.size() || reg < 0 || reg > 15) {
        return false;
    }
    return (info.live_after[line] & bit_for_register(reg)) != 0;
}

bool replace_register_operand(std::string& operand, int from, int to) {
    int reg = 0;
    if (parse_register(operand, reg)) {
        if (reg != from) {
            return false;
        }
        operand = "R" + std::to_string(to);
        return true;
    }
    if (operand.size() < 3 || operand.front() != '[' ||
        operand.back() != ']') {
        return false;
    }
    if (!parse_register_address(operand, reg) || reg != from) {
        return false;
    }
    const std::string original = "R" + std::to_string(from);
    const std::string replacement = "R" + std::to_string(to);
    const std::size_t position = operand.find(original);
    if (position == std::string::npos) {
        return false;
    }
    operand.replace(position, original.size(), replacement);
    return true;
}

int coalesce_block_moves(std::vector<std::string>& lines,
                         const LivenessInfo& liveness,
                         const CfgBlock& block) {
    int removed = 0;
    for (std::size_t index = block.begin; index < block.end; ++index) {
        Instruction move;
        if (!parse_instruction(lines[index], move) ||
            move.mnemonic != "MOV" || move.operands.size() != 2) {
            continue;
        }
        int destination = 0;
        int source = 0;
        if (!parse_register(move.operands[0], destination) ||
            !parse_register(move.operands[1], source) ||
            destination == source || destination == 0 ||
            destination == 7 || destination == 11 ||
            source == 7 || source == 11) {
            continue;
        }
        if (block.end == 0 ||
            register_live_after(liveness, block.end - 1, destination)) {
            continue;
        }
        bool removable = true;
        for (std::size_t next = index + 1; next < block.end; ++next) {
            Instruction instruction;
            if (!parse_instruction(lines[next], instruction)) {
                continue;
            }
            if (is_call(instruction.mnemonic)) {
                removable = false;
                break;
            }
            if (is_return_mnemonic(instruction.mnemonic)) {
                if (destination == 1) {
                    removable = false;
                }
                break;
            }
            if (is_branch(instruction.mnemonic)) {
                break;
            }
            const bool writes_first = writes_first_operand(instruction);
            int first_destination = 0;
            if (writes_first &&
                parse_register(instruction.operands[0],
                               first_destination)) {
                if (first_destination == source) {
                    removable = false;
                    break;
                }
            }
            const std::size_t first_read = writes_first ? 1 : 0;
            bool changed = false;
            for (std::size_t operand = first_read;
                 operand < instruction.operands.size(); ++operand) {
                if (replace_register_operand(instruction.operands[operand],
                                             destination, source)) {
                    changed = true;
                }
            }
            if (changed) {
                lines[next] = format_instruction(instruction);
            }
            if (writes_first && first_destination == destination) {
                break;
            }
        }
        if (removable) {
            lines[index].clear();
            ++removed;
        }
    }
    return removed;
}

int coalesce_moves(std::vector<std::string>& lines) {
    const LivenessInfo liveness = compute_register_liveness(lines);
    const std::vector<CfgFunction> functions = build_cfgs(lines);
    int removed = 0;
    for (const CfgFunction& function : functions) {
        for (const CfgBlock& block : function.blocks) {
            removed += coalesce_block_moves(lines, liveness, block);
        }
    }
    return removed;
}

bool operand_reads_register(const std::string& operand, int reg) {
    int found = -1;
    if (parse_register(operand, found) ||
        parse_register_address(operand, found)) {
        return found == reg;
    }
    return false;
}

bool next_consumer(std::string_view load_line, int destination, int source,
                   std::size_t consumer_index, const LivenessInfo& liveness,
                   Instruction& consumer) {
    if (!parse_instruction(load_line, consumer)) {
        return false;
    }
    (void)load_line;
    if (is_return_mnemonic(consumer.mnemonic) || consumer.mnemonic == "HALT" ||
        is_branch(consumer.mnemonic) || is_call(consumer.mnemonic)) {
        return false;
    }
    const bool writes_first = writes_first_operand(consumer);
    const std::size_t first_read = writes_first ? 1 : 0;
    bool reads_destination = false;
    for (std::size_t operand = first_read;
         operand < consumer.operands.size(); ++operand) {
        if (operand_reads_register(consumer.operands[operand], destination)) {
            reads_destination = true;
            break;
        }
    }
    if (!reads_destination) {
        return false;
    }
    int written = -1;
    if (writes_first &&
        !parse_register(consumer.operands[0], written)) {
        return false;
    }
    if (written != destination &&
        register_live_after(liveness, consumer_index, destination)) {
        return false;
    }
    bool changed = false;
    for (std::size_t operand = first_read;
         operand < consumer.operands.size(); ++operand) {
        if (replace_register_operand(consumer.operands[operand], destination,
                                     source)) {
            changed = true;
        }
    }
    return changed;
}

struct PromotionInsertion {
    std::size_t after = 0;
    int reg = 0;
    int slot = 0;
};

bool register_dead_ignoring_line_labels(const std::vector<std::string>& lines,
                                        const std::set<std::string>& branch_targets,
                                        std::size_t start, int reg);

// LOAD Rd, [R7 + slot] where Rd is overwritten before it is read. Frame slots
// are ordinary memory, so dropping the load has no device side effect.
int remove_dead_frame_loads(std::vector<std::string>& lines) {
    std::set<std::string> branch_targets;
    for (const std::string& line : lines) {
        Instruction instruction;
        if (parse_instruction(line, instruction) &&
            is_branch(instruction.mnemonic) && !instruction.operands.empty()) {
            branch_targets.insert(trim(instruction.operands[0]));
        }
    }
    int removed = 0;
    for (std::size_t index = 0; index + 1 < lines.size(); ++index) {
        Instruction load;
        if (!parse_instruction(lines[index], load) ||
            load.mnemonic != "LOAD" || load.operands.size() != 2) {
            continue;
        }
        int destination = 0;
        int slot = 0;
        if (!parse_register(load.operands[0], destination) ||
            destination == 7 || destination == 11 ||
            !parse_frame_slot(load.operands[1], slot)) {
            continue;
        }
        if (!register_dead_ignoring_line_labels(lines, branch_targets,
                                                index + 1, destination)) {
            continue;
        }
        lines[index].clear();
        ++removed;
    }
    return removed;
}

// Cache frequently loaded slots in the callee-saved registers R8-R10 for
// functions that call out. A callee-saved register survives a call, which a
// frame slot costs two accesses per crossing to match. The saved registers live
// in a 16-byte area so SP keeps its 16-byte alignment at CALL.
int promote_call_surviving_scalars(std::vector<std::string>& lines,
                                   int& rewritten) {
    struct Edits {
        std::map<std::size_t, std::string> replace;
        std::map<std::size_t, std::vector<std::string>> insert;
    };
    Edits edits;
    for (const CfgFunction& function : build_cfgs(lines)) {
        bool has_call = false;
        bool has_exception_path = false;
        std::size_t prologue_after = 0;
        for (std::size_t index = function.begin; index < function.end;
             ++index) {
            if (lines[index].find(".Lcs_") != std::string::npos) {
                has_exception_path = true;
            }
            Instruction instruction;
            if (!parse_instruction(lines[index], instruction)) {
                continue;
            }
            if (is_call(instruction.mnemonic)) {
                has_call = true;
            }
            if (instruction.mnemonic == "LEA" &&
                instruction.operands.size() == 2 &&
                trim(instruction.operands[1]) == "[SP]") {
                int reg = 0;
                if (parse_register(instruction.operands[0], reg) && reg == 7 &&
                    prologue_after == 0) {
                    prologue_after = index + 1;
                }
            }
        }
        if (!has_call || has_exception_path || prologue_after == 0) {
            continue;
        }
        bool used[16] = {};
        for (std::size_t index = function.begin; index < function.end;
             ++index) {
            Instruction instruction;
            if (!parse_instruction(lines[index], instruction)) {
                continue;
            }
            for (const std::string& operand : instruction.operands) {
                int reg = -1;
                if (parse_register(operand, reg) ||
                    parse_register_address(operand, reg)) {
                    if (reg >= 0 && reg < 16) {
                        used[reg] = true;
                    }
                }
            }
        }
        std::vector<int> registers;
        for (const int candidate : {8, 9, 10}) {
            if (!used[candidate]) {
                registers.push_back(candidate);
            }
        }
        if (registers.empty()) {
            continue;
        }
        struct Access {
            int loads = 0;
            int stores = 0;
            bool blocked = false;
            bool saw_access = false;
            bool first_is_store = false;
        };
        std::map<int, Access> slots;
        for (std::size_t index = function.begin; index < function.end;
             ++index) {
            Instruction instruction;
            if (!parse_instruction(lines[index], instruction) ||
                instruction.operands.size() != 2) {
                continue;
            }
            int slot = 0;
            if (!parse_frame_slot(instruction.operands[1], slot)) {
                continue;
            }
            Access& access = slots[slot];
            if (instruction.mnemonic == "LEA") {
                access.blocked = true;
                continue;
            }
            const bool load = instruction.mnemonic == "LOAD";
            const bool store = instruction.mnemonic == "STORE";
            if (!load && !store) {
                // A narrow or floating-point access to the same slot blocks it.
                access.blocked = true;
                continue;
            }
            if (!access.saw_access) {
                access.saw_access = true;
                access.first_is_store = store;
            }
            if (load) {
                ++access.loads;
            } else {
                ++access.stores;
            }
        }
        std::vector<std::pair<int, int>> chosen;  // loads, slot
        for (const auto& entry : slots) {
            const Access& access = entry.second;
            if (access.blocked || !access.saw_access || !access.first_is_store ||
                access.loads < 3 || access.stores < 1) {
                continue;
            }
            chosen.emplace_back(access.loads, entry.first);
        }
        std::sort(chosen.begin(), chosen.end(),
                  [](const std::pair<int, int>& left,
                     const std::pair<int, int>& right) {
                      return left.first > right.first;
                  });
        std::map<int, int> promoted;  // slot -> register
        for (std::size_t index = 0;
             index < chosen.size() && index < registers.size(); ++index) {
            promoted[chosen[index].second] = registers[index];
        }
        if (promoted.empty()) {
            continue;
        }
        for (std::size_t index = function.begin; index < function.end;
             ++index) {
            Instruction instruction;
            if (!parse_instruction(lines[index], instruction) ||
                instruction.operands.size() != 2) {
                continue;
            }
            if (instruction.mnemonic != "LOAD" &&
                instruction.mnemonic != "STORE") {
                continue;
            }
            int slot = 0;
            if (!parse_frame_slot(instruction.operands[1], slot)) {
                continue;
            }
            const auto found = promoted.find(slot);
            if (found == promoted.end()) {
                continue;
            }
            const std::string cached = "R" + std::to_string(found->second);
            Instruction move;
            move.mnemonic = "MOV";
            if (instruction.mnemonic == "LOAD") {
                move.operands.push_back(instruction.operands[0]);
                move.operands.push_back(cached);
            } else {
                move.operands.push_back(cached);
                move.operands.push_back(instruction.operands[0]);
            }
            edits.replace[index] = format_instruction(move);
            ++rewritten;
        }
        std::vector<std::string> save;
        save.push_back("    ADJSP #-16");
        std::vector<std::string> restore;
        for (std::size_t index = 0; index < registers.size(); ++index) {
            if (index >= promoted.size()) {
                break;
            }
            const std::string reg = "R" + std::to_string(registers[index]);
            save.push_back("    STORE " + reg + ", [SP + " +
                           std::to_string(index * 4) + "]");
            restore.push_back("    LOAD " + reg + ", [SP + " +
                              std::to_string(index * 4) + "]");
        }
        restore.push_back("    ADJSP #16");
        edits.insert[prologue_after] = save;
        for (std::size_t index = function.begin; index < function.end;
             ++index) {
            Instruction instruction;
            if (!parse_instruction(lines[index], instruction)) {
                continue;
            }
            if (instruction.mnemonic != "POP" ||
                instruction.operands.size() != 1 ||
                instruction.operands[0] != "R7") {
                continue;
            }
            // The frame release can be several ADJSP steps (a stack argument
            // area, then the frame). The save area sits directly below the
            // frame, so restore before the first of them.
            std::size_t insert_at = index;
            std::size_t scan = index;
            while (scan > function.begin) {
                --scan;
                if (lines[scan].empty()) {
                    continue;
                }
                Instruction before;
                if (!parse_instruction(lines[scan], before) ||
                    before.mnemonic != "ADJSP" || before.operands.size() != 1 ||
                    before.operands[0].size() < 2 ||
                    before.operands[0][0] != '#') {
                    break;
                }
                long long amount = 0;
                try {
                    amount = std::stoll(before.operands[0].substr(1));
                } catch (...) {
                    break;
                }
                if (amount <= 0) {
                    break;
                }
                insert_at = scan;
            }
            edits.insert[insert_at] = restore;
        }
    }
    if (edits.replace.empty()) {
        return 0;
    }
    std::vector<std::string> output;
    output.reserve(lines.size() + edits.insert.size() * 4);
    for (std::size_t index = 0; index < lines.size(); ++index) {
        const auto insert = edits.insert.find(index);
        if (insert != edits.insert.end()) {
            for (const std::string& extra : insert->second) {
                output.push_back(extra);
            }
        }
        const auto replace = edits.replace.find(index);
        if (replace != edits.replace.end()) {
            output.push_back(replace->second);
        } else {
            output.push_back(lines[index]);
        }
    }
    lines = std::move(output);
    return 0;
}

int promote_leaf_scalars(std::vector<std::string>& lines) {
    const LivenessInfo liveness = compute_register_liveness(lines);
    std::vector<PromotionInsertion> insertions;
    int removed = 0;
    for (const CfgFunction& function : build_cfgs(lines)) {
        bool has_call = false;
        bool has_exception_path = false;
        for (std::size_t index = function.begin; index < function.end;
             ++index) {
            if (lines[index].find(".Lcs_") != std::string::npos) {
                has_exception_path = true;
            }
            Instruction instruction;
            if (parse_instruction(lines[index], instruction) &&
                is_call(instruction.mnemonic)) {
                has_call = true;
            }
        }
        if (has_call || has_exception_path) {
            continue;
        }

        std::size_t prologue_end = function.end;
        bool saw_push_frame = false;
        for (std::size_t index = function.begin; index < function.end;
             ++index) {
            Instruction instruction;
            if (!parse_instruction(lines[index], instruction)) {
                continue;
            }
            if (!saw_push_frame) {
                int reg = 0;
                if (instruction.mnemonic != "PUSH" ||
                    instruction.operands.size() != 1 ||
                    !parse_register(instruction.operands[0], reg) ||
                    reg != 7) {
                    break;
                }
                saw_push_frame = true;
                continue;
            }
            if (instruction.mnemonic == "ADJSP") {
                continue;
            }
            if (instruction.mnemonic == "LEA" &&
                instruction.operands.size() == 2 &&
                trim(instruction.operands[1]) == "[SP]") {
                int reg = 0;
                if (parse_register(instruction.operands[0], reg) && reg == 7) {
                    prologue_end = index + 1;
                }
            }
            break;
        }
        if (!saw_push_frame || prologue_end == function.end) {
            continue;
        }

        bool used_register[16] = {};
        std::map<int, int> load_counts;
        std::map<int, int> store_counts;
        std::set<int> blocked_slots;
        for (std::size_t index = function.begin; index < function.end;
             ++index) {
            Instruction instruction;
            if (!parse_instruction(lines[index], instruction)) {
                continue;
            }
            for (const std::string& operand : instruction.operands) {
                int reg = -1;
                if (parse_register(operand, reg) ||
                    parse_register_address(operand, reg)) {
                    used_register[reg] = true;
                }
            }
            if (instruction.operands.size() != 2) {
                continue;
            }
            int slot = 0;
            if (!parse_frame_slot(instruction.operands[1], slot)) {
                continue;
            }
            if (instruction.mnemonic == "LEA") {
                blocked_slots.insert(slot);
            } else if (instruction.mnemonic == "LOAD") {
                ++load_counts[slot];
            } else if (instruction.mnemonic == "STORE") {
                int source = 0;
                if (!parse_register(instruction.operands[0], source)) {
                    blocked_slots.insert(slot);
                } else {
                    ++store_counts[slot];
                }
            } else {
                blocked_slots.insert(slot);
            }
        }

        std::vector<int> free_registers;
        for (int reg = 12; reg < 16; ++reg) {
            if (!used_register[reg]) {
                free_registers.push_back(reg);
            }
        }
        if (free_registers.empty()) {
            continue;
        }

        std::vector<std::pair<int, int>> candidates;
        for (const auto& [slot, count] : load_counts) {
            const int stores = store_counts[slot];
            if (count >= 1 && count >= stores &&
                blocked_slots.find(slot) == blocked_slots.end()) {
                candidates.emplace_back(count, slot);
            }
        }
        std::sort(candidates.begin(), candidates.end(),
                  [](const auto& left, const auto& right) {
                      if (left.first != right.first) {
                          return left.first > right.first;
                      }
                      return left.second < right.second;
                  });

        std::size_t free_index = 0;
        for (const auto& candidate : candidates) {
            if (free_index >= free_registers.size()) {
                break;
            }
            const int slot = candidate.second;
            const int reg = free_registers[free_index];

            std::vector<std::size_t> removable_loads;
            for (std::size_t index = function.begin; index < function.end;
                 ++index) {
                Instruction load;
                if (!parse_instruction(lines[index], load) ||
                    load.mnemonic != "LOAD" ||
                    load.operands.size() != 2) {
                    continue;
                }
                int loaded_slot = 0;
                int destination = 0;
                if (!parse_frame_slot(load.operands[1], loaded_slot) ||
                    loaded_slot != slot ||
                    !parse_register(load.operands[0], destination)) {
                    continue;
                }
                std::size_t consumer_index = index + 1;
                while (consumer_index < function.end &&
                       lines[consumer_index].empty()) {
                    ++consumer_index;
                }
                if (consumer_index >= function.end) {
                    continue;
                }
                Instruction consumer;
                if (next_consumer(lines[consumer_index], destination, reg,
                                  consumer_index, liveness, consumer)) {
                    removable_loads.push_back(index);
                }
            }


            for (std::size_t index = function.begin; index < function.end;
                 ++index) {
                Instruction instruction;
                if (!parse_instruction(lines[index], instruction) ||
                    instruction.operands.size() != 2) {
                    continue;
                }
                int accessed = 0;
                if (!parse_frame_slot(instruction.operands[1], accessed) ||
                    accessed != slot) {
                    continue;
                }
                if (instruction.mnemonic == "STORE") {
                    int source = 0;
                    if (parse_register(instruction.operands[0], source)) {
                        lines[index] = "    MOV R" +
                                       std::to_string(reg) + ", R" +
                                       std::to_string(source);
                    }
                    continue;
                }
                if (instruction.mnemonic != "LOAD") {
                    continue;
                }
                int destination = 0;
                if (!parse_register(instruction.operands[0], destination)) {
                    continue;
                }
                std::size_t consumer_index = index + 1;
                while (consumer_index < function.end &&
                       lines[consumer_index].empty()) {
                    ++consumer_index;
                }
                Instruction consumer;
                if (consumer_index < function.end &&
                    next_consumer(lines[consumer_index], destination, reg,
                                  consumer_index, liveness, consumer)) {
                    lines[consumer_index] = format_instruction(consumer);
                    lines[index].clear();
                    ++removed;
                } else {
                    lines[index] = "    MOV R" +
                                   std::to_string(destination) + ", R" +
                                   std::to_string(reg);
                }
            }
            insertions.push_back({prologue_end, reg, slot});
            ++free_index;
        }
    }
    if (insertions.empty()) {
        return removed;
    }

    std::sort(insertions.begin(), insertions.end(),
              [](const PromotionInsertion& left,
                 const PromotionInsertion& right) {
                  if (left.after != right.after) {
                      return left.after < right.after;
                  }
                  return left.reg < right.reg;
              });
    std::vector<std::string> output;
    output.reserve(lines.size() + insertions.size());
    std::size_t insertion_index = 0;
    for (std::size_t index = 0; index < lines.size(); ++index) {
        output.push_back(lines[index]);
        while (insertion_index < insertions.size() &&
               insertions[insertion_index].after == index + 1) {
            const PromotionInsertion& insertion =
                insertions[insertion_index++];
            output.push_back("    LOAD R" +
                             std::to_string(insertion.reg) + ", [R7 + " +
                             std::to_string(insertion.slot) + "]");
        }
    }
    lines = std::move(output);
    return removed;
}

int fold_pointer_load_offsets(std::vector<std::string>& lines) {
    int removed = 0;
    for (std::size_t index = 0; index + 1 < lines.size(); ++index) {
        Instruction adjust;
        if (!parse_instruction(lines[index], adjust) ||
            (adjust.mnemonic != "ADD" && adjust.mnemonic != "SUB") ||
            adjust.operands.size() != 3) {
            continue;
        }
        int destination = 0;
        int left = 0;
        int value = 0;
        if (!parse_register(adjust.operands[0], destination) ||
            !parse_register(adjust.operands[1], left) ||
            left != destination || destination == 7 || destination == 11 ||
            adjust.operands[2].empty() ||
            adjust.operands[2].front() != '#') {
            continue;
        }
        try {
            value = std::stoi(adjust.operands[2].substr(1));
        } catch (...) {
            continue;
        }
        if (adjust.mnemonic == "SUB") {
            value = -value;
        }
        std::size_t next = index + 1;
        while (next < lines.size() && lines[next].empty()) {
            ++next;
        }
        if (next >= lines.size()) {
            continue;
        }
        Instruction consumer;
        if (!parse_instruction(lines[next], consumer) ||
            consumer.operands.size() != 2) {
            continue;
        }
        const bool load = consumer.mnemonic == "LOAD" ||
                          consumer.mnemonic == "LOAD8S" ||
                          consumer.mnemonic == "LOAD8U" ||
                          consumer.mnemonic == "LOAD16S" ||
                          consumer.mnemonic == "LOAD16U";
        if (!load) {
            continue;
        }
        int loaded = 0;
        int address = 0;
        int offset = 0;
        if (!parse_register(consumer.operands[0], loaded) ||
            loaded != destination ||
            !parse_register_offset(consumer.operands[1], address, offset) ||
            address != destination || offset != 0) {
            continue;
        }
        std::size_t after = next + 1;
        while (after < lines.size() && lines[after].empty()) {
            ++after;
        }
        if (after < lines.size()) {
            Instruction following;
            if (parse_instruction(lines[after], following) &&
                is_conditional_jump(following.mnemonic)) {
                continue;
            }
        }
        consumer.operands[1] =
            "[" + adjust.operands[1] +
            (value >= 0 ? " + " : " - ") +
            std::to_string(value >= 0 ? value : -value) + "]";
        lines[next] = format_instruction(consumer);
        lines[index].clear();
        ++removed;
    }
    return removed;
}

bool parse_symbol_address(std::string_view text, std::string& symbol) {
    const std::string value = trim(text);
    if (value.size() < 3 || value.front() != '[' || value.back() != ']') {
        return false;
    }
    const std::string inside =
        trim(value.substr(1, value.size() - 2));
    if (inside.empty() || inside.find_first_of("+-* /") != std::string::npos) {
        return false;
    }
    int reg = 0;
    if (parse_register(inside, reg)) {
        return false;
    }
    symbol = inside;
    return true;
}

// STORE Rx, [R7 + slot] immediately followed by LOAD Rx, [R7 + slot] is a
// round trip through memory that leaves Rx unchanged. Labels may sit between
// the two; a CFG pass cannot remove these when the function has an exception
// path, but adjacency makes it safe locally.
int remove_adjacent_slot_roundtrips(std::vector<std::string>& lines) {
    int removed = 0;
    for (std::size_t index = 0; index + 1 < lines.size(); ++index) {
        Instruction store;
        if (!parse_instruction(lines[index], store) ||
            store.mnemonic != "STORE" || store.operands.size() != 2) {
            continue;
        }
        int stored_register = 0;
        int stored_slot = 0;
        if (!parse_register(store.operands[0], stored_register) ||
            !parse_frame_slot(store.operands[1], stored_slot)) {
            continue;
        }
        std::size_t next = index + 1;
        while (next < lines.size() &&
               (lines[next].empty() || is_label(lines[next]))) {
            ++next;
        }
        if (next >= lines.size()) {
            continue;
        }
        Instruction load;
        if (!parse_instruction(lines[next], load) ||
            load.mnemonic != "LOAD" || load.operands.size() != 2) {
            continue;
        }
        int loaded_register = 0;
        int loaded_slot = 0;
        if (!parse_register(load.operands[0], loaded_register) ||
            !parse_frame_slot(load.operands[1], loaded_slot) ||
            loaded_register != stored_register ||
            loaded_slot != stored_slot) {
            continue;
        }
        lines[next].clear();
        ++removed;
    }
    return removed;
}
int fold_symbol_address_loads(std::vector<std::string>& lines) {
    int removed = 0;
    for (std::size_t index = 0; index + 1 < lines.size(); ++index) {
        Instruction address;
        if (!parse_instruction(lines[index], address) ||
            address.mnemonic != "LEA" || address.operands.size() != 2) {
            continue;
        }
        int destination = 0;
        std::string symbol;
        if (!parse_register(address.operands[0], destination) ||
            !parse_symbol_address(address.operands[1], symbol)) {
            continue;
        }
        std::size_t next = index + 1;
        while (next < lines.size() && lines[next].empty()) {
            ++next;
        }
        if (next >= lines.size()) {
            continue;
        }
        Instruction load;
        if (!parse_instruction(lines[next], load) ||
            load.operands.size() != 2) {
            continue;
        }
        const bool loads_value = load.mnemonic == "LOAD" ||
                                 load.mnemonic == "LOAD8S" ||
                                 load.mnemonic == "LOAD8U" ||
                                 load.mnemonic == "LOAD16S" ||
                                 load.mnemonic == "LOAD16U";
        if (!loads_value) {
            continue;
        }
        int loaded = 0;
        int address_register = 0;
        if (!parse_register(load.operands[0], loaded) ||
            loaded != destination ||
            !parse_register_address(load.operands[1], address_register) ||
            address_register != destination) {
            continue;
        }
        load.operands[1] = "[" + symbol + "]";
        lines[next] = format_instruction(load);
        lines[index].clear();
        ++removed;
    }
    return removed;
}

// "[symbol]" or "[symbol + n]" to its parts.
bool parse_symbol_offset(std::string_view text, std::string& symbol, int& offset) {
    const std::string value = trim(text);
    if (value.size() < 3 || value.front() != '[' || value.back() != ']') {
        return false;
    }
    const std::string inside = trim(value.substr(1, value.size() - 2));
    if (inside.empty()) {
        return false;
    }
    int reg = 0;
    if (parse_register(inside, reg)) {
        return false;
    }
    const std::size_t plus = inside.find(" + ");
    if (plus == std::string::npos) {
        if (inside.find_first_of("+-* /") != std::string::npos) {
            return false;
        }
        symbol = inside;
        offset = 0;
        return true;
    }
    const std::string base = trim(inside.substr(0, plus));
    const std::string number = trim(inside.substr(plus + 3));
    if (base.empty() || number.empty() ||
        base.find_first_of("+-* /") != std::string::npos) {
        return false;
    }
    int parsed = 0;
    try {
        parsed = std::stoi(number);
    } catch (...) {
        return false;
    }
    symbol = base;
    offset = parsed;
    return true;
}

bool instruction_touches_register(const Instruction& instruction, int reg) {
    RegisterMask uses = 0;
    RegisterMask defs = 0;
    instruction_uses_defs(instruction, uses, defs);
    const RegisterMask bit = bit_for_register(reg);
    return ((uses | defs) & bit) != 0;
}

// Finds the store that consumes `reg` after the LEA at `start`, skipping
// instructions that do not touch it. Labels that can be entered by a branch
// stop the walk; other labels are line markers and are skipped.
bool find_store_through(const std::vector<std::string>& lines,
                        const std::set<std::string>& branch_targets,
                        std::size_t start, int reg, std::size_t& consumer) {
    std::size_t cursor = start;
    while (cursor < lines.size()) {
        Instruction instruction;
        if (!parse_instruction(lines[cursor], instruction)) {
            const std::string trimmed = trim(lines[cursor]);
            if (trimmed.empty()) {
                ++cursor;
                continue;
            }
            if (is_label(lines[cursor]) &&
                branch_targets.find(trimmed.substr(0, trimmed.size() - 1)) ==
                    branch_targets.end()) {
                ++cursor;
                continue;
            }
            return false;
        }
        if (is_branch(instruction.mnemonic) || is_call(instruction.mnemonic)) {
            return false;
        }
        if (instruction_touches_register(instruction, reg)) {
            break;
        }
        ++cursor;
    }
    if (cursor >= lines.size()) {
        return false;
    }
    Instruction store;
    if (!parse_instruction(lines[cursor], store)) {
        return false;
    }
    if (store.mnemonic != "STORE" && store.mnemonic != "STORE8" &&
        store.mnemonic != "STORE16") {
        return false;
    }
    if (store.operands.size() != 2) {
        return false;
    }
    int address = 0;
    int offset = 0;
    if (!parse_register_offset(store.operands[1], address, offset) ||
        address != reg) {
        return false;
    }
    consumer = cursor;
    return true;
}

// True when `reg` is rewritten before it is read. Labels that no branch can
// enter are line markers and are skipped; a branch target is a real entry.
bool register_dead_ignoring_line_labels(
    const std::vector<std::string>& lines,
    const std::set<std::string>& branch_targets, std::size_t start, int reg) {
    const RegisterMask bit = bit_for_register(reg);
    for (std::size_t index = start; index < lines.size(); ++index) {
        Instruction instruction;
        if (!parse_instruction(lines[index], instruction)) {
            const std::string trimmed = trim(lines[index]);
            if (trimmed.empty()) {
                continue;
            }
            if (is_label(lines[index]) &&
                branch_targets.find(trimmed.substr(0, trimmed.size() - 1)) ==
                    branch_targets.end()) {
                continue;
            }
            return false;
        }
        if (is_branch(instruction.mnemonic) || is_call(instruction.mnemonic)) {
            return false;
        }
        RegisterMask uses = 0;
        RegisterMask defs = 0;
        instruction_uses_defs(instruction, uses, defs);
        if ((uses & bit) != 0) {
            return false;
        }
        if ((defs & bit) != 0) {
            return true;
        }
    }
    return true;
}

// LEA Rd, [symbol + n] ; LOAD/STORE x, [Rd]  ->  LOAD/STORE x, [symbol + n]
// The absolute mode carries the symbol and its displacement, so the address
// never has to be materialised in a register.
// LEA Rd, [symbol + n] where Rd is only ever read as a memory address: every
// use can name the symbol directly, so the register and the LEA go away.
int promote_symbol_bases(std::vector<std::string>& lines) {
    const std::vector<CfgFunction> functions = build_cfgs(lines);
    int removed = 0;
    for (std::size_t index = 0; index < lines.size(); ++index) {
        Instruction address;
        if (!parse_instruction(lines[index], address) ||
            address.mnemonic != "LEA" || address.operands.size() != 2) {
            continue;
        }
        int reg = 0;
        std::string symbol;
        int base_offset = 0;
        if (!parse_register(address.operands[0], reg) || reg == 7 || reg == 11 ||
            !parse_symbol_offset(address.operands[1], symbol, base_offset)) {
            continue;
        }
        const CfgFunction* function = nullptr;
        for (const CfgFunction& candidate : functions) {
            if (index >= candidate.begin && index < candidate.end) {
                function = &candidate;
                break;
            }
        }
        if (function == nullptr) {
            continue;
        }
        const RegisterMask bit = bit_for_register(reg);
        std::vector<std::size_t> uses;
        bool ok = true;
        for (std::size_t scan = function->begin; scan < function->end; ++scan) {
            if (scan == index) {
                continue;
            }
            Instruction instruction;
            if (!parse_instruction(lines[scan], instruction)) {
                continue;
            }
            RegisterMask used = 0;
            RegisterMask defined = 0;
            instruction_uses_defs(instruction, used, defined);
            if ((defined & bit) != 0) {
                ok = false;
                break;
            }
            if ((used & bit) == 0) {
                continue;
            }
            const bool memory_instruction =
                instruction.mnemonic.rfind("LOAD", 0) == 0 ||
                instruction.mnemonic.rfind("STORE", 0) == 0;
            bool memory_read = false;
            bool plain_read = false;
            for (const std::string& operand : instruction.operands) {
                int address_reg = 0;
                int offset = 0;
                int plain = 0;
                if (parse_register(operand, plain) && plain == reg) {
                    plain_read = true;
                }
                if (memory_instruction &&
                    parse_register_offset(operand, address_reg, offset) &&
                    address_reg == reg) {
                    memory_read = true;
                }
            }
            if (!memory_read || plain_read) {
                ok = false;
                break;
            }
            uses.push_back(scan);
        }
        if (!ok || uses.empty()) {
            continue;
        }
        for (const std::size_t use : uses) {
            Instruction instruction;
            if (!parse_instruction(lines[use], instruction)) {
                continue;
            }
            for (std::string& operand : instruction.operands) {
                int address_reg = 0;
                int offset = 0;
                if (!parse_register_offset(operand, address_reg, offset) ||
                    address_reg != reg) {
                    continue;
                }
                const int combined = base_offset + offset;
                operand = combined == 0
                              ? "[" + symbol + "]"
                              : "[" + symbol + " + " +
                                    std::to_string(combined) + "]";
            }
            lines[use] = format_instruction(instruction);
        }
        lines[index].clear();
        ++removed;
    }
    return removed;
}
int use_absolute_symbol_addresses(std::vector<std::string>& lines) {
    const LivenessInfo liveness = compute_register_liveness(lines);
    std::set<std::string> branch_targets;
    for (const std::string& line : lines) {
        Instruction instruction;
        if (!parse_instruction(line, instruction) ||
            !is_branch(instruction.mnemonic) || instruction.operands.empty()) {
            continue;
        }
        branch_targets.insert(trim(instruction.operands[0]));
    }
    int removed = 0;
    for (std::size_t index = 0; index + 1 < lines.size(); ++index) {
        Instruction address;
        if (!parse_instruction(lines[index], address) ||
            address.mnemonic != "LEA" || address.operands.size() != 2) {
            continue;
        }
        int address_register = 0;
        std::string symbol;
        int displacement = 0;
        if (!parse_register(address.operands[0], address_register) ||
            address_register == 7 || address_register == 11 ||
            !parse_symbol_offset(address.operands[1], symbol, displacement)) {
            continue;
        }
        std::size_t consumer = 0;
        bool found = false;
        std::size_t cursor = index + 1;
        while (cursor < lines.size()) {
            Instruction instruction;
            if (!parse_instruction(lines[cursor], instruction)) {
                const std::string trimmed = trim(lines[cursor]);
                if (trimmed.empty()) {
                    ++cursor;
                    continue;
                }
                if (is_label(lines[cursor]) &&
                    branch_targets.find(trimmed.substr(0, trimmed.size() - 1)) ==
                        branch_targets.end()) {
                    ++cursor;
                    continue;
                }
                break;
            }
            if (is_branch(instruction.mnemonic) ||
                is_call(instruction.mnemonic)) {
                break;
            }
            if (instruction_touches_register(instruction, address_register)) {
                consumer = cursor;
                found = true;
                break;
            }
            ++cursor;
        }
        if (!found) {
            continue;
        }
        Instruction memory;
        if (!parse_instruction(lines[consumer], memory) ||
            memory.operands.size() != 2) {
            continue;
        }
        const bool is_load =
            memory.mnemonic == "LOAD" || memory.mnemonic == "LOAD8U" ||
            memory.mnemonic == "LOAD8S" || memory.mnemonic == "LOAD16U" ||
            memory.mnemonic == "LOAD16S";
        const bool is_store = memory.mnemonic == "STORE" ||
                              memory.mnemonic == "STORE8" ||
                              memory.mnemonic == "STORE16";
        if (!is_load && !is_store) {
            continue;
        }
        int accessed = 0;
        int offset = 0;
        if (!parse_register_offset(memory.operands[1], accessed, offset) ||
            accessed != address_register || offset != 0) {
            continue;
        }
        if (register_live_after(liveness, consumer, address_register)) {
            continue;
        }
        memory.operands[1] =
            displacement == 0
                ? "[" + symbol + "]"
                : "[" + symbol + " + " + std::to_string(displacement) + "]";
        lines[consumer] = format_instruction(memory);
        lines[index].clear();
        ++removed;
    }
    return removed;
}

// Keeps one base pointer in a register and reaches the following stores with a
// displacement: LEA R1, [s + a] ; ... ; STORE ; LEA R1, [s + b] ; STORE
// becomes LEA R1, [s + a] ; ... ; STORE ; STORE [R1 + b].
int reuse_symbol_bases(std::vector<std::string>& lines) {
    std::set<std::string> branch_targets;
    for (const std::string& line : lines) {
        Instruction instruction;
        if (!parse_instruction(line, instruction) ||
            !is_branch(instruction.mnemonic) || instruction.operands.empty()) {
            continue;
        }
        branch_targets.insert(trim(instruction.operands[0]));
    }
    int removed = 0;
    for (std::size_t index = 0; index + 1 < lines.size(); ++index) {
        Instruction base;
        if (!parse_instruction(lines[index], base) ||
            base.mnemonic != "LEA" || base.operands.size() != 2) {
            continue;
        }
        int base_register = 0;
        std::string symbol;
        int base_offset = 0;
        if (!parse_register(base.operands[0], base_register) ||
            base_register == 7 || base_register == 11 ||
            !parse_symbol_offset(base.operands[1], symbol, base_offset)) {
            continue;
        }
        std::size_t consumer = 0;
        if (!find_store_through(lines, branch_targets, index + 1, base_register,
                                consumer)) {
            continue;
        }
        std::size_t cursor = consumer + 1;
        while (true) {
            std::size_t scan = cursor;
            bool blocked = false;
            while (scan < lines.size()) {
                Instruction instruction;
                if (!parse_instruction(lines[scan], instruction)) {
                    const std::string trimmed = trim(lines[scan]);
                    if (trimmed.empty()) {
                        ++scan;
                        continue;
                    }
                    if (is_label(lines[scan]) &&
                        branch_targets.find(trimmed.substr(0, trimmed.size() - 1)) ==
                            branch_targets.end()) {
                        ++scan;
                        continue;
                    }
                    blocked = true;
                    break;
                }
                if (is_branch(instruction.mnemonic) ||
                    is_call(instruction.mnemonic)) {
                    blocked = true;
                    break;
                }
                if (instruction_touches_register(instruction, base_register)) {
                    break;
                }
                ++scan;
            }
            if (blocked || scan >= lines.size()) {
                break;
            }
            Instruction next_lea;
            if (!parse_instruction(lines[scan], next_lea) ||
                next_lea.mnemonic != "LEA" || next_lea.operands.size() != 2) {
                break;
            }
            int next_register = 0;
            std::string next_symbol;
            int next_offset = 0;
            if (!parse_register(next_lea.operands[0], next_register) ||
                next_register != base_register ||
                !parse_symbol_offset(next_lea.operands[1], next_symbol,
                                     next_offset) ||
                next_symbol != symbol) {
                break;
            }
            std::size_t next_consumer = 0;
            if (!find_store_through(lines, branch_targets, scan + 1,
                                    base_register, next_consumer)) {
                break;
            }
            Instruction store;
            parse_instruction(lines[next_consumer], store);
            const int displacement = next_offset - base_offset;
            store.operands[1] =
                "[R" + std::to_string(base_register) +
                (displacement >= 0 ? " + " : " - ") +
                std::to_string(displacement >= 0 ? displacement : -displacement) +
                "]";
            lines[next_consumer] = format_instruction(store);
            lines[scan].clear();
            ++removed;
            cursor = next_consumer + 1;
        }
    }
    return removed;
}

// LEA Rd, [symbol] ; ADD Rd, Rd, #n  ->  LEA Rd, [symbol + n]
int fold_symbol_offsets(std::vector<std::string>& lines) {
    int removed = 0;
    for (std::size_t index = 0; index + 1 < lines.size(); ++index) {
        Instruction address;
        if (!parse_instruction(lines[index], address) ||
            address.mnemonic != "LEA" || address.operands.size() != 2) {
            continue;
        }
        int destination = 0;
        std::string symbol;
        if (!parse_register(address.operands[0], destination) ||
            !parse_symbol_address(address.operands[1], symbol)) {
            continue;
        }
        std::size_t next = index + 1;
        while (next < lines.size() && lines[next].empty()) {
            ++next;
        }
        if (next >= lines.size()) {
            continue;
        }
        Instruction adjust;
        if (!parse_instruction(lines[next], adjust) ||
            adjust.mnemonic != "ADD" || adjust.operands.size() != 3) {
            continue;
        }
        int adjusted = 0;
        int left = 0;
        if (!parse_register(adjust.operands[0], adjusted) ||
            adjusted != destination ||
            !parse_register(adjust.operands[1], left) || left != destination) {
            continue;
        }
        if (adjust.operands[2].empty() || adjust.operands[2].front() != '#') {
            continue;
        }
        int value = 0;
        try {
            value = std::stoi(adjust.operands[2].substr(1));
        } catch (...) {
            continue;
        }
        if (value < 0) {
            continue;
        }
        address.operands[1] =
            "[" + symbol + " + " + std::to_string(value) + "]";
        lines[index] = format_instruction(address);
        lines[next].clear();
        ++removed;
    }
    return removed;
}

// MOV Ry, Rd where Rd is dead afterwards: retarget the LEA or ADD that
// MOV Rd, #v ; NEG Rd, Rd  ->  MOV Rd, #-v
// MOV Rd, #v ; NOT Rd, Rd  ->  MOV Rd, #~v
int fold_unary_constants(std::vector<std::string>& lines) {
    int removed = 0;
    for (std::size_t index = 0; index + 1 < lines.size(); ++index) {
        Instruction move;
        if (!parse_instruction(lines[index], move) || move.mnemonic != "MOV" ||
            move.operands.size() != 2) {
            continue;
        }
        int destination = 0;
        if (!parse_register(move.operands[0], destination) ||
            move.operands[1].empty() || move.operands[1].front() != '#') {
            continue;
        }
        int value = 0;
        try {
            value = std::stoi(move.operands[1].substr(1));
        } catch (...) {
            continue;
        }
        std::size_t next = index + 1;
        while (next < lines.size() && lines[next].empty()) {
            ++next;
        }
        if (next >= lines.size()) {
            continue;
        }
        Instruction unary;
        if (!parse_instruction(lines[next], unary) ||
            (unary.mnemonic != "NEG" && unary.mnemonic != "NOT") ||
            unary.operands.size() != 2) {
            continue;
        }
        int target = 0;
        int source = 0;
        if (!parse_register(unary.operands[0], target) ||
            !parse_register(unary.operands[1], source) ||
            target != destination || source != destination) {
            continue;
        }
        value = unary.mnemonic == "NEG" ? -value : ~value;
        move.operands[1] = "#" + std::to_string(value);
        lines[index] = format_instruction(move);
        lines[next].clear();
        ++removed;
    }
    return removed;
}

// produced Rd to write Ry instead, and drop the copy.
// MOV Rd, Rs where the producer computes an address in Rs and the consumer
// immediately reads and clobbers Rs: move the copy into both ends so the
// address survives in Rd and the extra move disappears.
int fold_clobbered_address_copies(std::vector<std::string>& lines) {
    int removed = 0;
    for (std::size_t index = 1; index + 1 < lines.size(); ++index) {
        Instruction move;
        if (!parse_instruction(lines[index], move) || move.mnemonic != "MOV" ||
            move.operands.size() != 2) {
            continue;
        }
        int destination = 0;
        int source = 0;
        if (!parse_register(move.operands[0], destination) ||
            !parse_register(move.operands[1], source) ||
            destination == source || destination == 7 || destination == 11 ||
            source == 7 || source == 11) {
            continue;
        }
        std::size_t producer = index;
        bool found = false;
        while (producer > 0) {
            --producer;
            if (lines[producer].empty()) {
                continue;
            }
            found = !is_label(lines[producer]);
            break;
        }
        if (!found) {
            continue;
        }
        Instruction produced;
        if (!parse_instruction(lines[producer], produced) ||
            (produced.mnemonic != "LEA" && produced.mnemonic != "ADD" &&
             produced.mnemonic != "SUB") ||
            produced.operands.empty()) {
            continue;
        }
        int target = 0;
        if (!parse_register(produced.operands[0], target) || target != source) {
            continue;
        }
        std::size_t consumer = index + 1;
        while (consumer < lines.size() && lines[consumer].empty()) {
            ++consumer;
        }
        if (consumer >= lines.size()) {
            continue;
        }
        Instruction instruction;
        if (!parse_instruction(lines[consumer], instruction)) {
            continue;
        }
        RegisterMask uses = 0;
        RegisterMask defs = 0;
        instruction_uses_defs(instruction, uses, defs);
        const RegisterMask source_bit = bit_for_register(source);
        const RegisterMask destination_bit = bit_for_register(destination);
        if ((uses & source_bit) == 0 || (defs & source_bit) == 0) {
            continue;
        }
        if ((uses & destination_bit) != 0 ||
            (defs & destination_bit) != 0) {
            continue;
        }
        const std::size_t first =
            writes_first_operand(instruction) ? 1 : 0;
        bool replaced = false;
        for (std::size_t operand = first;
             operand < instruction.operands.size(); ++operand) {
            if (replace_register_operand(instruction.operands[operand],
                                         source, destination)) {
                replaced = true;
            }
        }
        if (!replaced) {
            continue;
        }
        produced.operands[0] = "R" + std::to_string(destination);
        lines[producer] = format_instruction(produced);
        lines[consumer] = format_instruction(instruction);
        lines[index].clear();
        ++removed;
    }
    return removed;
}
int fold_address_moves(std::vector<std::string>& lines) {
    int removed = 0;
    for (std::size_t index = 1; index < lines.size(); ++index) {
        Instruction move;
        if (!parse_instruction(lines[index], move) ||
            move.mnemonic != "MOV" || move.operands.size() != 2) {
            continue;
        }
        int destination = 0;
        int source = 0;
        if (!parse_register(move.operands[0], destination) ||
            !parse_register(move.operands[1], source) ||
            destination == source || destination == 7 || destination == 11 ||
            source == 7 || source == 11) {
            continue;
        }
        if (!register_dead_after(lines, index + 1, source)) {
            continue;
        }
        std::size_t producer = index;
        bool found = false;
        while (producer > 0) {
            --producer;
            if (lines[producer].empty()) {
                continue;
            }
            found = !is_label(lines[producer]);
            break;
        }
        if (!found) {
            continue;
        }
        Instruction produced;
        if (!parse_instruction(lines[producer], produced)) {
            continue;
        }
        const bool address_instruction =
            produced.mnemonic == "LEA" || produced.mnemonic == "ADD" ||
            produced.mnemonic == "SUB" || produced.mnemonic == "LOAD" ||
            produced.mnemonic == "LOAD8S" || produced.mnemonic == "LOAD8U" ||
            produced.mnemonic == "LOAD16S" || produced.mnemonic == "LOAD16U";
        if (!address_instruction || produced.operands.empty()) {
            continue;
        }
        int target = 0;
        if (!parse_register(produced.operands[0], target) || target != source) {
            continue;
        }
        produced.operands[0] = "R" + std::to_string(destination);
        lines[producer] = format_instruction(produced);
        lines[index].clear();
        ++removed;
    }
    return removed;
}

int fold_symbol_address_stores(std::vector<std::string>& lines) {
    int removed = 0;
    for (std::size_t index = 0; index + 1 < lines.size(); ++index) {
        Instruction address;
        if (!parse_instruction(lines[index], address) ||
            address.mnemonic != "LEA" || address.operands.size() != 2) {
            continue;
        }
        int destination = 0;
        std::string symbol;
        if (!parse_register(address.operands[0], destination) ||
            destination == 7 || destination == 11 ||
            !parse_symbol_address(address.operands[1], symbol)) {
            continue;
        }
        std::size_t next = index + 1;
        while (next < lines.size() && lines[next].empty()) {
            ++next;
        }
        if (next >= lines.size()) {
            continue;
        }
        Instruction store;
        if (!parse_instruction(lines[next], store) ||
            (store.mnemonic != "STORE" &&
             store.mnemonic != "STORE8" &&
             store.mnemonic != "STORE16") ||
            store.operands.size() != 2) {
            continue;
        }
        int source = 0;
        int address_register = 0;
        if (!parse_register(store.operands[0], source) ||
            source == destination ||
            !parse_register_address(store.operands[1], address_register) ||
            address_register != destination) {
            continue;
        }
        std::size_t after = next + 1;
        while (after < lines.size() && lines[after].empty()) {
            ++after;
        }
        if (after >= lines.size()) {
            continue;
        }
        Instruction overwrite;
        int overwritten = 0;
        if (!parse_instruction(lines[after], overwrite) ||
            !writes_first_operand(overwrite) ||
            !parse_register(overwrite.operands[0], overwritten) ||
            overwritten != destination) {
            continue;
        }
        store.operands[1] = "[" + symbol + "]";
        lines[next] = format_instruction(store);
        lines[index].clear();
        ++removed;
    }
    return removed;
}

int fold_pointer_store_offsets(std::vector<std::string>& lines) {
    int removed = 0;
    for (std::size_t index = 0; index + 1 < lines.size(); ++index) {
        Instruction adjust;
        if (!parse_instruction(lines[index], adjust) ||
            (adjust.mnemonic != "ADD" && adjust.mnemonic != "SUB") ||
            adjust.operands.size() != 3) {
            continue;
        }
        int destination = 0;
        int left = 0;
        int value = 0;
        if (!parse_register(adjust.operands[0], destination) ||
            !parse_register(adjust.operands[1], left) ||
            left != destination || destination == 7 || destination == 11 ||
            adjust.operands[2].empty() ||
            adjust.operands[2].front() != '#') {
            continue;
        }
        try {
            value = std::stoi(adjust.operands[2].substr(1));
        } catch (...) {
            continue;
        }
        if (adjust.mnemonic == "SUB") {
            value = -value;
        }
        std::size_t next = index + 1;
        while (next < lines.size() && lines[next].empty()) {
            ++next;
        }
        if (next >= lines.size()) {
            continue;
        }
        Instruction consumer;
        if (!parse_instruction(lines[next], consumer) ||
            (consumer.mnemonic != "STORE" &&
             consumer.mnemonic != "STORE8" &&
             consumer.mnemonic != "STORE16") ||
            consumer.operands.size() != 2) {
            continue;
        }
        int source = 0;
        int address = 0;
        int offset = 0;
        if (!parse_register(consumer.operands[0], source) ||
            source == destination ||
            !parse_register_offset(consumer.operands[1], address, offset) ||
            address != destination || offset != 0) {
            continue;
        }
        std::size_t after = next + 1;
        while (after < lines.size() && lines[after].empty()) {
            ++after;
        }
        if (after >= lines.size()) {
            continue;
        }
        Instruction overwrite;
        int overwritten = 0;
        if (!parse_instruction(lines[after], overwrite) ||
            !writes_first_operand(overwrite) ||
            !parse_register(overwrite.operands[0], overwritten) ||
            overwritten != destination) {
            continue;
        }
        consumer.operands[1] =
            "[" + adjust.operands[1] +
            (value >= 0 ? " + " : " - ") +
            std::to_string(value >= 0 ? value : -value) + "]";
        lines[next] = format_instruction(consumer);
        lines[index].clear();
        ++removed;
    }
    return removed;
}

int fold_frame_address_arithmetic(std::vector<std::string>& lines) {
    int removed = 0;
    for (std::size_t index = 0; index + 1 < lines.size(); ++index) {
        Instruction address;
        if (!parse_instruction(lines[index], address) ||
            address.mnemonic != "LEA" || address.operands.size() != 2) {
            continue;
        }
        int destination = 0;
        int slot = 0;
        if (!parse_register(address.operands[0], destination) ||
            !parse_frame_slot(address.operands[1], slot)) {
            continue;
        }
        std::size_t next = index + 1;
        while (next < lines.size() && lines[next].empty()) {
            ++next;
        }
        if (next >= lines.size()) {
            continue;
        }
        Instruction adjust;
        if (!parse_instruction(lines[next], adjust) ||
            (adjust.mnemonic != "ADD" && adjust.mnemonic != "SUB") ||
            adjust.operands.size() != 3) {
            continue;
        }
        int left = 0;
        int right = 0;
        int value = 0;
        if (!parse_register(adjust.operands[0], left) ||
            !parse_register(adjust.operands[1], right) ||
            left != destination || right != destination ||
            adjust.operands[2].empty() ||
            adjust.operands[2].front() != '#') {
            continue;
        }
        try {
            value = std::stoi(adjust.operands[2].substr(1));
        } catch (...) {
            continue;
        }
        if (adjust.mnemonic == "SUB") {
            value = -value;
        }
        address.operands[1] =
            "[R7 + " + std::to_string(slot + value) + "]";
        lines[index] = format_instruction(address);
        lines[next].clear();
        ++removed;
    }
    return removed;
}

int fold_frame_addresses(std::vector<std::string>& lines) {
    int removed = 0;
    for (std::size_t index = 0; index + 1 < lines.size(); ++index) {
        Instruction address;
        if (!parse_instruction(lines[index], address) ||
            address.mnemonic != "LEA" || address.operands.size() != 2) {
            continue;
        }
        int address_register = 0;
        int slot = 0;
        if (!parse_register(address.operands[0], address_register) ||
            !parse_frame_slot(address.operands[1], slot)) {
            continue;
        }
        std::size_t next = index + 1;
        while (next < lines.size() && lines[next].empty()) {
            ++next;
        }
        Instruction consumer;
        if (next >= lines.size() ||
            !parse_instruction(lines[next], consumer) ||
            consumer.operands.size() != 2) {
            continue;
        }
        if (consumer.mnemonic == "LOAD") {
            int destination = 0;
            int loaded_address = 0;
            int offset = 0;
            if (!parse_register(consumer.operands[0], destination) ||
                !parse_register_offset(consumer.operands[1], loaded_address,
                                       offset) ||
                loaded_address != address_register) {
                continue;
            }
            consumer.operands[1] =
                "[R7 + " + std::to_string(slot + offset) + "]";
            lines[next] = format_instruction(consumer);
            if (destination == address_register ||
                register_dead_after(lines, next + 1, address_register)) {
                lines[index].clear();
                ++removed;
            }
        } else if (consumer.mnemonic == "STORE") {
            int stored_address = 0;
            int stored_source = 0;
            int offset = 0;
            if (!parse_register_offset(consumer.operands[1], stored_address,
                                       offset) ||
                stored_address != address_register ||
                (parse_register(consumer.operands[0], stored_source) &&
                 stored_source == address_register)) {
                continue;
            }
            consumer.operands[1] =
                "[R7 + " + std::to_string(slot + offset) + "]";
            lines[next] = format_instruction(consumer);
        }
    }
    return removed;
}

std::string inverse_condition(const std::string& mnemonic) {
    if (mnemonic == "JZ") return "JNZ";
    if (mnemonic == "JNZ") return "JZ";
    if (mnemonic == "JLT") return "JGE";
    if (mnemonic == "JLE") return "JGT";
    if (mnemonic == "JGT") return "JLE";
    if (mnemonic == "JGE") return "JLT";
    if (mnemonic == "JNC") return "JCS";
    if (mnemonic == "JCS") return "JNC";
    if (mnemonic == "JLS") return "JHI";
    if (mnemonic == "JHI") return "JLS";
    return {};
}

int fold_comparison_values(std::vector<std::string>& lines) {
    int removed = 0;
    for (std::size_t index = 0; index + 6 < lines.size(); ++index) {
        Instruction compare;
        Instruction branch;
        if (!parse_instruction(lines[index], compare) ||
            compare.mnemonic != "CMP" || compare.operands.size() != 2 ||
            !parse_instruction(lines[index + 1], branch) ||
            branch.operands.size() != 1) {
            continue;
        }
        const std::string& jump = branch.mnemonic;
        const bool signed_less = jump == "JLT" || jump == "JLE" ||
                                 jump == "JGT" || jump == "JGE";
        const bool unsigned_less = jump == "JNC" || jump == "JLS" ||
                                   jump == "JHI" || jump == "JCS";
        if (!signed_less && !unsigned_less) {
            continue;
        }
        const std::string true_label = trim(branch.operands[0]);
        if (trim(lines[index + 2]) != "MOV R0, #0") {
            continue;
        }
        Instruction skip;
        if (!parse_instruction(lines[index + 3], skip) ||
            skip.mnemonic != "JMP" || skip.operands.size() != 1) {
            continue;
        }
        const std::string end_label = trim(skip.operands[0]);
        if (trim(lines[index + 4]) != true_label + ":" ||
            trim(lines[index + 5]) != "MOV R0, #1" ||
            trim(lines[index + 6]) != end_label + ":") {
            continue;
        }
        std::size_t after = index + 7;
        while (after < lines.size() && lines[after].empty()) {
            ++after;
        }
        if (after < lines.size()) {
            Instruction following;
            if (parse_instruction(lines[after], following) &&
                is_conditional_jump(following.mnemonic)) {
                continue;
            }
        }
        const std::string mnemonic = unsigned_less ? "SLTU" : "SLT";
        std::string result;
        if (jump == "JLT" || jump == "JNC" || jump == "JGE" ||
            jump == "JCS") {
            result = "    " + mnemonic + " R0, " + compare.operands[0] +
                     ", " + compare.operands[1];
        } else {
            int right = 0;
            if (!parse_register(compare.operands[1], right)) {
                continue;
            }
            result = "    " + mnemonic + " R0, " + compare.operands[1] +
                     ", " + compare.operands[0];
        }
        lines[index] = result;
        if (jump == "JLE" || jump == "JGT" || jump == "JGE" ||
            jump == "JLS" || jump == "JHI" || jump == "JCS") {
            lines[index + 1] = "    XOR R0, R0, #1";
        } else {
            lines[index + 1].clear();
        }
        for (std::size_t clear = index + 2; clear <= index + 6; ++clear) {
            lines[clear].clear();
        }
        removed += 5;
    }
    return removed;
}

int fold_compare_materialization_branches(std::vector<std::string>& lines) {
    int removed = 0;
    for (std::size_t index = 0; index + 2 < lines.size(); ++index) {
        Instruction first;
        if (!parse_instruction(lines[index], first) ||
            first.operands.size() < 3) {
            continue;
        }
        const bool less =
            first.mnemonic == "SLT" || first.mnemonic == "SLTU";
        const bool equality = first.mnemonic == "XOR";
        if (!less && !equality) {
            continue;
        }
        int destination = 0;
        if (!parse_register(first.operands[0], destination) ||
            destination != 0) {
            continue;
        }
        std::size_t compare_index = index + 1;
        bool inverted = false;
        if (less) {
            Instruction maybe_invert;
            if (parse_instruction(lines[compare_index], maybe_invert) &&
                maybe_invert.mnemonic == "XOR" &&
                maybe_invert.operands.size() == 2 &&
                trim(maybe_invert.operands[0]) == "R0" &&
                trim(maybe_invert.operands[1]) == "#1") {
                inverted = true;
                ++compare_index;
            }
        } else {
            Instruction maybe_one;
            if (!parse_instruction(lines[compare_index], maybe_one) ||
                maybe_one.mnemonic != "SLTU" ||
                maybe_one.operands.size() != 3 ||
                trim(maybe_one.operands[0]) != "R0" ||
                !parse_register(maybe_one.operands[1], destination) ||
                destination != 0 ||
                trim(maybe_one.operands[2]) != "#1") {
                continue;
            }
            ++compare_index;
        }
        if (compare_index + 1 >= lines.size()) {
            continue;
        }
        Instruction compare;
        if (!parse_instruction(lines[compare_index], compare) ||
            compare.mnemonic != "CMP" || compare.operands.size() != 2 ||
            trim(compare.operands[0]) != "R0" ||
            trim(compare.operands[1]) != "#0") {
            continue;
        }
        Instruction branch;
        if (!parse_instruction(lines[compare_index + 1], branch) ||
            branch.operands.size() != 1 ||
            (branch.mnemonic != "JZ" && branch.mnemonic != "JNZ")) {
            continue;
        }
        const bool branch_true = branch.mnemonic == "JNZ";
        const bool unsigned_compare = first.mnemonic == "SLTU";
        std::string replacement;
        if (equality) {
            replacement = branch_true ? "JZ" : "JNZ";
        } else if ((less && !inverted) || (inverted && !branch_true)) {
            replacement = branch_true ? (unsigned_compare ? "JNC" : "JLT")
                                      : (unsigned_compare ? "JCS" : "JGE");
        } else {
            replacement = branch_true ? (unsigned_compare ? "JCS" : "JGE")
                                      : (unsigned_compare ? "JNC" : "JLT");
        }
        if (equality) {
            lines[index] = "    CMP " + first.operands[1] + ", " +
                           first.operands[2];
        } else {
            lines[index] = "    CMP " + first.operands[1] + ", " +
                           first.operands[2];
        }
        for (std::size_t clear = index + 1; clear <= compare_index;
             ++clear) {
            lines[clear].clear();
        }
        lines[compare_index + 1] =
            "    " + replacement + " " + trim(branch.operands[0]);
        removed += static_cast<int>(compare_index - index);
    }
    return removed;
}

int fold_boolean_branches(std::vector<std::string>& lines) {
    int removed = 0;
    for (std::size_t index = 0; index + 8 < lines.size(); ++index) {
        Instruction compare;
        Instruction branch;
        if (!parse_instruction(lines[index], compare) ||
            compare.mnemonic != "CMP" ||
            !parse_instruction(lines[index + 1], branch) ||
            branch.operands.size() != 1) {
            continue;
        }
        const std::string inverse = inverse_condition(branch.mnemonic);
        if (inverse.empty()) {
            continue;
        }
        const std::string true_label = trim(branch.operands[0]);
        if (true_label.rfind(".Ltrue_", 0) != 0 ||
            trim(lines[index + 2]) != "MOV R0, #0") {
            continue;
        }
        Instruction skip;
        if (!parse_instruction(lines[index + 3], skip) ||
            skip.mnemonic != "JMP" || skip.operands.size() != 1) {
            continue;
        }
        const std::string end_label = trim(skip.operands[0]);
        if (end_label.rfind(".Lcmpend_", 0) != 0 ||
            trim(lines[index + 4]) != true_label + ":" ||
            trim(lines[index + 5]) != "MOV R0, #1" ||
            trim(lines[index + 6]) != end_label + ":") {
            continue;
        }
        Instruction test;
        Instruction false_branch;
        if (!parse_instruction(lines[index + 7], test) ||
            trim(lines[index + 7]) != "CMP R0, #0" ||
            !parse_instruction(lines[index + 8], false_branch) ||
            false_branch.mnemonic != "JZ" ||
            false_branch.operands.size() != 1) {
            continue;
        }
        lines[index + 1] =
            "    " + inverse + " " + trim(false_branch.operands[0]);
        for (std::size_t clear = index + 2; clear <= index + 8; ++clear) {
            lines[clear].clear();
        }
        removed += 7;
    }
    return removed;
}

int remove_dead_frame_stores(std::vector<std::string>& lines) {
    int removed = 0;
    for (const CfgFunction& function : build_cfgs(lines)) {
        bool has_exception_path = false;
        for (std::size_t index = function.begin; index < function.end;
             ++index) {
            if (lines[index].find(".Lcs_") != std::string::npos) {
                has_exception_path = true;
                break;
            }
        }
        if (has_exception_path) {
            continue;
        }

        std::set<int> escaped;
        for (std::size_t index = function.begin; index < function.end;
             ++index) {
            Instruction instruction;
            if (!parse_instruction(lines[index], instruction) ||
                instruction.mnemonic != "LEA" ||
                instruction.operands.size() != 2) {
                continue;
            }
            int slot = 0;
            if (parse_frame_slot(instruction.operands[1], slot)) {
                escaped.insert(slot);
            }
        }

        const std::vector<CfgBlock>& blocks = function.blocks;
        std::vector<std::set<int>> live_in(blocks.size());
        std::vector<std::set<int>> live_out(blocks.size());
        bool changed = true;
        int rounds = 0;
        while (changed && rounds++ < 256) {
            changed = false;
            for (std::size_t block_index = blocks.size();
                 block_index > 0; --block_index) {
                const CfgBlock& block = blocks[block_index - 1];
                std::set<int> out;
                for (const std::size_t successor : block.successors) {
                    if (successor < live_in.size()) {
                        out.insert(live_in[successor].begin(),
                                   live_in[successor].end());
                    }
                }
                std::set<int> in = block.slot_uses;
                for (const int slot : out) {
                    if (block.slot_defs.find(slot) ==
                        block.slot_defs.end()) {
                        in.insert(slot);
                    }
                }
                if (in != live_in[block_index - 1] ||
                    out != live_out[block_index - 1]) {
                    live_in[block_index - 1] = std::move(in);
                    live_out[block_index - 1] = std::move(out);
                    changed = true;
                }
            }
        }

        for (std::size_t block_index = 0;
             block_index < blocks.size(); ++block_index) {
            const CfgBlock& block = blocks[block_index];
            std::set<int> live = live_out[block_index];
            for (std::size_t index = block.end; index > block.begin;
                 --index) {
                Instruction instruction;
                if (!parse_instruction(lines[index - 1], instruction)) {
                    continue;
                }
                int slot = 0;
                const bool full_store =
                    instruction.mnemonic == "STORE" &&
                    instruction.operands.size() == 2 &&
                    parse_frame_slot(instruction.operands[1], slot);
                // A store at or after an escaped slot may be a field of the
                // escaped block, so its address is reachable through that
                // pointer even though no direct load names it.
                bool after_escape = false;
                for (const int base : escaped) {
                    if (slot >= base) {
                        after_escape = true;
                        break;
                    }
                }
                if (full_store && !after_escape &&
                    live.find(slot) == live.end()) {
                    lines[index - 1].clear();
                    ++removed;
                }
                std::set<int> uses;
                std::set<int> defs;
                frame_slot_effects(instruction, uses, defs);
                for (const int defined : defs) {
                    live.erase(defined);
                }
                live.insert(uses.begin(), uses.end());
            }
        }
    }
    return removed;
}

int remove_overwritten_moves(std::vector<std::string>& lines) {
    int removed = 0;
    for (std::size_t index = 0; index + 1 < lines.size(); ++index) {
        Instruction move;
        if (!parse_instruction(lines[index], move) ||
            move.mnemonic != "MOV" || move.operands.size() != 2) {
            continue;
        }
        int destination = 0;
        if (!parse_register(move.operands[0], destination)) {
            continue;
        }
        std::size_t next = index + 1;
        while (next < lines.size() && lines[next].empty()) {
            ++next;
        }
        Instruction following;
        if (next >= lines.size() ||
            !parse_instruction(lines[next], following) ||
            !writes_first_operand(following) ||
            following.operands.empty()) {
            continue;
        }
        int next_destination = 0;
        if (!parse_register(following.operands[0], next_destination) ||
            next_destination != destination) {
            continue;
        }
        bool read_before_write = false;
        for (std::size_t operand = 1; operand < following.operands.size();
             ++operand) {
            int reg = 0;
            if (parse_register(following.operands[operand], reg) &&
                reg == destination) {
                read_before_write = true;
                break;
            }
        }
        if (!read_before_write) {
            lines[index].clear();
            ++removed;
        }
    }
    return removed;
}

int remove_dead_moves(std::vector<std::string>& lines) {
    const LivenessInfo liveness = compute_register_liveness(lines);
    int removed = 0;
    for (std::size_t index = 0; index < lines.size(); ++index) {
        Instruction instruction;
        if (!parse_instruction(lines[index], instruction) ||
            instruction.mnemonic != "MOV" ||
            instruction.operands.size() != 2) {
            continue;
        }
        int destination = 0;
        if (!parse_register(instruction.operands[0], destination) ||
            destination == 7 || destination == 11) {
            continue;
        }
        if (!register_live_after(liveness, index, destination)) {
            lines[index].clear();
            ++removed;
        }
    }
    return removed;
}

int remove_redundant_jumps(std::vector<std::string>& lines) {
    int removed = 0;
    for (std::size_t index = 0; index < lines.size(); ++index) {
        Instruction instruction;
        if (!parse_instruction(lines[index], instruction) ||
            instruction.mnemonic != "JMP" ||
            instruction.operands.size() != 1) {
            continue;
        }
        std::size_t next = index + 1;
        while (next < lines.size()) {
            const std::string text = trim(lines[next]);
            if (text.empty()) {
                ++next;
                continue;
            }
            if (text == instruction.operands[0] + ":") {
                lines[index].clear();
                ++removed;
                break;
            }
            if (!is_label(text)) {
                break;
            }
            ++next;
        }
    }
    return removed;
}

bool is_terminator(const std::string& mnemonic) {
    return is_return_mnemonic(mnemonic) || mnemonic == "JMP" ||
           mnemonic == "HALT";
}

int fold_load_copy_overwrites(std::vector<std::string>& lines) {
    int removed = 0;
    for (std::size_t index = 0; index + 2 < lines.size(); ++index) {
        Instruction load;
        if (!parse_instruction(lines[index], load) ||
            load.mnemonic != "LOAD" || load.operands.size() != 2) {
            continue;
        }
        int source = 0;
        if (!parse_register(load.operands[0], source)) {
            continue;
        }
        std::size_t copy_index = index + 1;
        while (copy_index < lines.size() && lines[copy_index].empty()) {
            ++copy_index;
        }
        if (copy_index >= lines.size()) {
            continue;
        }
        Instruction copy;
        if (!parse_instruction(lines[copy_index], copy) ||
            copy.mnemonic != "MOV" || copy.operands.size() != 2) {
            continue;
        }
        int destination = 0;
        int copied = 0;
        if (!parse_register(copy.operands[0], destination) ||
            !parse_register(copy.operands[1], copied) ||
            copied != source || destination == source) {
            continue;
        }
        std::size_t next = copy_index + 1;
        while (next < lines.size() && lines[next].empty()) {
            ++next;
        }
        if (next >= lines.size()) {
            continue;
        }
        Instruction overwrite;
        int overwritten = 0;
        if (!parse_instruction(lines[next], overwrite) ||
            !writes_first_operand(overwrite) ||
            overwrite.operands.empty() ||
            !parse_register(overwrite.operands[0], overwritten) ||
            overwritten != source) {
            continue;
        }
        load.operands[0] = copy.operands[0];
        lines[index] = format_instruction(load);
        lines[copy_index].clear();
        ++removed;
    }
    return removed;
}

int fold_add_immediate_chain(std::vector<std::string>& lines) {
    int removed = 0;
    for (std::size_t index = 0; index + 1 < lines.size(); ++index) {
        Instruction first;
        if (!parse_instruction(lines[index], first) ||
            (first.mnemonic != "ADD" && first.mnemonic != "SUB") ||
            first.operands.size() != 3) {
            continue;
        }
        int destination = 0;
        int left = 0;
        if (!parse_register(first.operands[0], destination) ||
            !parse_register(first.operands[1], left) ||
            left != destination || first.operands[2].empty() ||
            first.operands[2].front() != '#') {
            continue;
        }
        int value = 0;
        try {
            value = std::stoi(first.operands[2].substr(1));
        } catch (...) {
            continue;
        }
        if (first.mnemonic == "SUB") {
            value = -value;
        }
        std::size_t next = index + 1;
        while (next < lines.size() && lines[next].empty()) {
            ++next;
        }
        if (next >= lines.size()) {
            continue;
        }
        Instruction second;
        if (!parse_instruction(lines[next], second) ||
            (second.mnemonic != "ADD" && second.mnemonic != "SUB") ||
            second.operands.size() != 3) {
            continue;
        }
        int second_destination = 0;
        int second_left = 0;
        if (!parse_register(second.operands[0], second_destination) ||
            !parse_register(second.operands[1], second_left) ||
            second_destination != destination ||
            second_left != destination || second.operands[2].empty() ||
            second.operands[2].front() != '#') {
            continue;
        }
        int second_value = 0;
        try {
            second_value = std::stoi(second.operands[2].substr(1));
        } catch (...) {
            continue;
        }
        if (second.mnemonic == "SUB") {
            second_value = -second_value;
        }
        std::size_t after = next + 1;
        while (after < lines.size() && lines[after].empty()) {
            ++after;
        }
        if (after < lines.size()) {
            Instruction following;
            if (parse_instruction(lines[after], following) &&
                is_conditional_jump(following.mnemonic)) {
                continue;
            }
        }
        const int total = value + second_value;
        first.mnemonic = total < 0 ? "SUB" : "ADD";
        first.operands[2] = "#" + std::to_string(total < 0 ? -total : total);
        lines[index] = format_instruction(first);
        lines[next].clear();
        ++removed;
    }
    return removed;
}

int remove_unreachable_after_terminators(std::vector<std::string>& lines) {
    int removed = 0;
    bool unreachable = false;
    for (std::string& line : lines) {
        const std::string value = trim(line);
        if (value.empty()) {
            continue;
        }
        const bool label = value.back() == ':';
        const bool directive = value.front() == '.';
        if (unreachable) {
            if (label || directive) {
                unreachable = false;
            } else {
                line.clear();
                ++removed;
                continue;
            }
        }
        Instruction instruction;
        if (parse_instruction(line, instruction) &&
            is_terminator(instruction.mnemonic)) {
            unreachable = true;
        }
    }
    return removed;
}

int remove_repeated_frame_loads(std::vector<std::string>& lines) {
    int removed = 0;
    std::map<int, std::string> loaded;
    const auto clear = [&]() { loaded.clear(); };
    for (std::string& line : lines) {
        Instruction instruction;
        if (!parse_instruction(line, instruction)) {
            clear();
            continue;
        }
        if (is_call(instruction.mnemonic) ||
            instruction.mnemonic == "JMP") {
            clear();
            continue;
        }
        if (instruction.mnemonic == "STORE" ||
            instruction.mnemonic == "STORE8" ||
            instruction.mnemonic == "STORE16" ||
            instruction.mnemonic == "FSTORE.W" ||
            instruction.mnemonic == "FSTORE.D") {
            clear();
            continue;
        }
        if (instruction.mnemonic == "LOAD" &&
            instruction.operands.size() == 2) {
            int destination = 0;
            int slot = 0;
            if (parse_register(instruction.operands[0], destination) &&
                parse_frame_slot(instruction.operands[1], slot)) {
                const auto found = loaded.find(slot);
                if (found != loaded.end() &&
                    found->second == instruction.operands[0]) {
                    line.clear();
                    ++removed;
                    continue;
                }
                const std::string destination_text =
                    instruction.operands[0];
                for (auto entry = loaded.begin(); entry != loaded.end();) {
                    if (entry->second == destination_text) {
                        entry = loaded.erase(entry);
                    } else {
                        ++entry;
                    }
                }
                loaded[slot] = destination_text;
                continue;
            }
        }
        if (writes_first_operand(instruction) &&
            !instruction.operands.empty()) {
            const std::string& destination = instruction.operands[0];
            for (auto entry = loaded.begin(); entry != loaded.end();) {
                if (entry->second == destination) {
                    entry = loaded.erase(entry);
                } else {
                    ++entry;
                }
            }
        }
    }
    return removed;
}

struct TailReplacement {
    std::size_t call = 0;
    std::size_t jump = 0;
    std::string adjust;
    std::string target;
};

int fold_tail_calls(std::vector<std::string>& lines, int& rewritten) {
    std::map<std::string, std::size_t> labels;
    for (std::size_t index = 0; index < lines.size(); ++index) {
        if (is_label(lines[index])) {
            const std::string text = trim(lines[index]);
            labels[text.substr(0, text.size() - 1)] = index;
        }
    }
    std::vector<TailReplacement> replacements;
    for (std::size_t index = 0; index < lines.size(); ++index) {
        Instruction call;
        if (!parse_instruction(lines[index], call) ||
            call.mnemonic != "CALL" || call.operands.empty() ||
            (call.operands.size() > 1 &&
             (call.operands[1].empty() || call.operands[1][0] != '#'))) {
            continue;
        }
        int indirect = 0;
        if (parse_register(call.operands[0], indirect)) {
            continue;
        }
        std::size_t jump_index = index + 1;
        while (jump_index < lines.size() && lines[jump_index].empty()) {
            ++jump_index;
        }
        Instruction jump;
        if (jump_index >= lines.size() ||
            !parse_instruction(lines[jump_index], jump) ||
            jump.mnemonic != "JMP" || jump.operands.size() != 1) {
            continue;
        }
        const std::string return_label = trim(jump.operands[0]);
        if (return_label.rfind(".Lreturn_", 0) != 0) {
            continue;
        }
        const auto label = labels.find(return_label);
        if (label == labels.end()) {
            continue;
        }
        std::size_t epilogue = label->second + 1;
        while (epilogue < lines.size() && lines[epilogue].empty()) {
            ++epilogue;
        }
        std::string adjust;
        if (epilogue < lines.size()) {
            Instruction instruction;
            if (parse_instruction(lines[epilogue], instruction) &&
                instruction.mnemonic == "ADJSP" &&
                instruction.operands.size() == 1) {
                adjust = format_instruction(instruction);
                ++epilogue;
                while (epilogue < lines.size() &&
                       lines[epilogue].empty()) {
                    ++epilogue;
                }
            }
        }
        Instruction pop;
        if (epilogue >= lines.size() ||
            !parse_instruction(lines[epilogue], pop) ||
            pop.mnemonic != "POP" || pop.operands.size() != 1 ||
            trim(pop.operands[0]) != "R7") {
            continue;
        }
        std::size_t ret_index = epilogue + 1;
        while (ret_index < lines.size() && lines[ret_index].empty()) {
            ++ret_index;
        }
        Instruction ret;
        if (ret_index >= lines.size() ||
            !parse_instruction(lines[ret_index], ret) ||
            !is_return_mnemonic(ret.mnemonic)) {
            continue;
        }
        replacements.push_back({index, jump_index, adjust,
                                trim(call.operands[0])});
    }
    if (replacements.empty()) {
        return 0;
    }
    std::vector<std::string> output;
    output.reserve(lines.size() + replacements.size() * 2);
    std::size_t replacement = 0;
    for (std::size_t index = 0; index < lines.size(); ++index) {
        if (replacement < replacements.size() &&
            replacements[replacement].call == index) {
            const TailReplacement& item = replacements[replacement];
            if (!item.adjust.empty()) {
                output.push_back(item.adjust);
            }
            output.push_back("    POP R7");
            output.push_back("    JMP " + item.target);
            ++rewritten;
            index = item.jump;
            ++replacement;
            continue;
        }
        output.push_back(lines[index]);
    }
    lines = std::move(output);
    return 0;
}

bool is_symbol_character(char character) {
    return std::isalnum(static_cast<unsigned char>(character)) != 0 ||
           character == '_' || character == '$' || character == '.';
}

bool contains_symbol(std::string_view line, std::string_view symbol) {
    if (symbol.empty() || symbol.size() > line.size()) {
        return false;
    }
    std::size_t position = line.find(symbol);
    while (position != std::string_view::npos) {
        const bool left_ok =
            position == 0 ||
            !is_symbol_character(line[position - 1]);
        const std::size_t after = position + symbol.size();
        const bool right_ok =
            after >= line.size() || !is_symbol_character(line[after]);
        if (left_ok && right_ok) {
            return true;
        }
        position = line.find(symbol, position + 1);
    }
    return false;
}

int remove_dead_functions(std::vector<std::string>& lines) {
    std::size_t debug_start = lines.size();
    std::set<std::string> external;
    for (std::size_t index = 0; index < lines.size(); ++index) {
        const std::string text = trim(lines[index]);
        if (text == ".debug_info") {
            debug_start = index;
        } else if (text.rfind(".global ", 0) == 0) {
            external.insert(trim(text.substr(8)));
        } else if (text.rfind(".weak ", 0) == 0) {
            external.insert(trim(text.substr(6)));
        }
    }

    struct FunctionRange {
        std::string name;
        std::size_t begin = 0;
        std::size_t end = 0;
        bool local = false;
    };
    std::vector<FunctionRange> functions;
    for (std::size_t index = 0; index < debug_start; ++index) {
        if (!is_function_label(lines[index])) {
            continue;
        }
        std::string name = trim(lines[index]);
        name.pop_back();
        if (!functions.empty()) {
            functions.back().end = index;
        }
        FunctionRange range;
        range.name = name;
        range.begin = index;
        range.end = debug_start;
        range.local = external.find(name) == external.end();
        functions.push_back(std::move(range));
    }
    if (functions.empty()) {
        return 0;
    }

    std::map<std::string, std::size_t> local_index;
    for (std::size_t index = 0; index < functions.size(); ++index) {
        if (functions[index].local) {
            local_index[functions[index].name] = index;
        }
    }
    if (local_index.empty()) {
        return 0;
    }

    std::vector<std::set<std::size_t>> edges(functions.size());
    std::set<std::size_t> roots;
    for (std::size_t line_index = 0; line_index < debug_start;
         ++line_index) {
        std::size_t owner = functions.size();
        for (std::size_t index = 0; index < functions.size(); ++index) {
            if (line_index >= functions[index].begin &&
                line_index < functions[index].end) {
                owner = index;
                break;
            }
        }
        for (const auto& entry : local_index) {
            if (line_index == functions[entry.second].begin ||
                !contains_symbol(lines[line_index], entry.first)) {
                continue;
            }
            if (owner < functions.size() && functions[owner].local) {
                edges[owner].insert(entry.second);
            } else {
                roots.insert(entry.second);
            }
        }
    }

    std::vector<bool> reachable(functions.size(), false);
    std::vector<std::size_t> work(roots.begin(), roots.end());
    for (const std::size_t root : roots) {
        reachable[root] = true;
    }
    while (!work.empty()) {
        const std::size_t current = work.back();
        work.pop_back();
        for (const std::size_t next : edges[current]) {
            if (!reachable[next]) {
                reachable[next] = true;
                work.push_back(next);
            }
        }
    }

    std::set<std::string> removed;
    int removed_lines = 0;
    for (std::size_t index = 0; index < functions.size(); ++index) {
        if (!functions[index].local || reachable[index]) {
            continue;
        }
        removed.insert(functions[index].name);
        std::size_t preserve = functions[index].end;
        while (preserve > functions[index].begin) {
            const std::string text = trim(lines[preserve - 1]);
            if (text.rfind(".text", 0) == 0 ||
                text.rfind(".global ", 0) == 0 ||
                text.rfind(".weak ", 0) == 0 ||
                text.rfind(".section ", 0) == 0) {
                --preserve;
            } else {
                break;
            }
        }
        for (std::size_t line = functions[index].begin; line < preserve;
             ++line) {
            if (!lines[line].empty()) {
                lines[line].clear();
                ++removed_lines;
            }
        }
    }
    if (removed.empty()) {
        return 0;
    }

    bool remove_members = false;
    for (std::size_t index = debug_start; index < lines.size(); ++index) {
        const std::string text = trim(lines[index]);
        if (text.rfind(".asciz \"F ", 0) == 0) {
            remove_members = false;
            for (const std::string& name : removed) {
                if (contains_symbol(text, name)) {
                    remove_members = true;
                    break;
                }
            }
            if (remove_members) {
                lines[index].clear();
            }
            continue;
        }
        if (remove_members &&
            (text.rfind(".asciz \"V ", 0) == 0 ||
             text.rfind(".asciz \"L ", 0) == 0)) {
            lines[index].clear();
        }
    }
    return removed_lines;
}

using FrameAvailability = std::map<int, int>;

void invalidate_register(FrameAvailability& available, int reg) {
    for (auto entry = available.begin(); entry != available.end();) {
        if (entry->second == reg) {
            entry = available.erase(entry);
        } else {
            ++entry;
        }
    }
}

void transfer_frame_availability(FrameAvailability& available,
                                 const Instruction& instruction) {

    if (is_call(instruction.mnemonic)) {
        available.clear();
        return;
    }
    if (instruction.mnemonic == "STORE" &&
        instruction.operands.size() == 2) {
        int source = 0;
        int slot = 0;
        if (parse_register(instruction.operands[0], source) &&
            parse_frame_slot(instruction.operands[1], slot)) {
            available[slot] = source;

        } else {

            available.clear();
        }
        return;
    }
    if (instruction.mnemonic == "STORE8" ||
        instruction.mnemonic == "STORE16" ||
        instruction.mnemonic == "FSTORE.W" ||
        instruction.mnemonic == "FSTORE.D" ||
        instruction.mnemonic == "SC") {
        available.clear();
        return;
    }
    if (instruction.mnemonic == "LOAD" &&
        instruction.operands.size() == 2) {
        int destination = 0;
        int slot = 0;
        if (parse_register(instruction.operands[0], destination) &&
            parse_frame_slot(instruction.operands[1], slot) &&
            available.find(slot) != available.end() &&
            available[slot] == destination) {
            return;
        }
    }
    if (writes_first_operand(instruction) &&
        !instruction.operands.empty()) {
        int destination = 0;
        if (parse_register(instruction.operands[0], destination)) {
            invalidate_register(available, destination);
        }
    }
}

FrameAvailability intersect_availability(
    const std::vector<FrameAvailability>& inputs) {
    if (inputs.empty()) {
        return {};
    }
    FrameAvailability result = inputs.front();
    for (std::size_t index = 1; index < inputs.size(); ++index) {
        for (auto entry = result.begin(); entry != result.end();) {
            const auto found = inputs[index].find(entry->first);
            if (found == inputs[index].end() ||
                found->second != entry->second) {
                entry = result.erase(entry);
            } else {
                ++entry;
            }
        }
    }
    return result;
}

int forward_cfg_frame_loads(std::vector<std::string>& lines,
                               int& rewritten) {
    int removed = 0;
    for (const CfgFunction& function : build_cfgs(lines)) {
        bool has_exception_path = false;
        for (std::size_t index = function.begin; index < function.end;
             ++index) {
            if (lines[index].find(".Lcs_") != std::string::npos) {
                has_exception_path = true;
                break;
            }
        }
        if (has_exception_path || function.blocks.empty()) {
            continue;
        }
        const std::size_t block_count = function.blocks.size();
        std::vector<std::vector<std::size_t>> predecessors(block_count);
        for (std::size_t block = 0; block < block_count; ++block) {
            for (const std::size_t successor :
                 function.blocks[block].successors) {
                if (successor < block_count) {
                    predecessors[successor].push_back(block);
                }
            }
        }
        std::vector<FrameAvailability> live_in(block_count);
        std::vector<FrameAvailability> live_out(block_count);
        bool changed = true;
        int rounds = 0;
        while (changed && rounds++ < 256) {
            changed = false;
            for (std::size_t block = 0; block < block_count; ++block) {
                std::vector<FrameAvailability> inputs;
                for (const std::size_t predecessor :
                     predecessors[block]) {
                    inputs.push_back(live_out[predecessor]);
                }
                FrameAvailability in = intersect_availability(inputs);
                FrameAvailability out = in;
                for (std::size_t index = function.blocks[block].begin;
                     index < function.blocks[block].end; ++index) {
                    Instruction instruction;
                    if (parse_instruction(lines[index], instruction)) {
                        transfer_frame_availability(out, instruction);
                    }
                }
                if (in != live_in[block] || out != live_out[block]) {
                    live_in[block] = std::move(in);
                    live_out[block] = std::move(out);
                    changed = true;
                }
            }
        }
        for (std::size_t block = 0; block < block_count; ++block) {
            FrameAvailability available = live_in[block];


            for (std::size_t index = function.blocks[block].begin;
                 index < function.blocks[block].end; ++index) {
                Instruction instruction;
                if (!parse_instruction(lines[index], instruction)) {
                    continue;
                }
                if (instruction.mnemonic == "LOAD" &&
                    instruction.operands.size() == 2) {
                    int destination = 0;
                    int slot = 0;

                    if (parse_register(instruction.operands[0],
                                       destination) &&
                        destination != 7 && destination != 11 &&
                        parse_frame_slot(instruction.operands[1], slot) &&
                        available.find(slot) != available.end()) {
                        const int source = available[slot];
                        if (source == destination) {
                            lines[index].clear();
                            ++removed;
                            continue;
                        }
                        if (source != 7 && source != 11) {
                            // The value already sits in a register: serve the
                            // load from it instead of memory.
                            lines[index] = "    MOV R" +
                                           std::to_string(destination) + ", R" +
                                           std::to_string(source);
                            ++rewritten;
                            Instruction move;
                            move.mnemonic = "MOV";
                            move.operands.push_back(
                                "R" + std::to_string(destination));
                            move.operands.push_back("R" +
                                                    std::to_string(source));
                            transfer_frame_availability(available, move);
                            continue;
                        }
                    }
                }
                transfer_frame_availability(available, instruction);
            }
        }
    }
    return removed;
}

// "[SP]" or "[SP + n]" to its displacement; SP is not in the R0..R15 syntax.
bool parse_stack_address(std::string_view text, int& offset) {
    const std::string value = trim(text);
    if (value.size() < 4 || value.front() != '[' || value.back() != ']') {
        return false;
    }
    std::string inside = value.substr(1, value.size() - 2);
    inside.erase(std::remove_if(inside.begin(), inside.end(),
                                [](unsigned char character) {
                                    return std::isspace(character) != 0;
                                }),
                 inside.end());
    if (inside.rfind("SP", 0) != 0) {
        return false;
    }
    const std::string rest = inside.substr(2);
    if (rest.empty()) {
        offset = 0;
        return true;
    }
    if (rest.front() != '+' && rest.front() != '-') {
        return false;
    }
    const int sign = rest.front() == '-' ? -1 : 1;
    const std::string number = rest.substr(1);
    if (number.empty()) {
        return false;
    }
    char* end = nullptr;
    const long parsed = std::strtol(number.c_str(), &end, 10);
    if (end == nullptr || *end != '\0') {
        return false;
    }
    offset = sign * static_cast<int>(parsed);
    return true;
}

// Pushes and pops a single 32-bit value through [SP]. When the span between
// them touches neither SP nor a free scratch register, the value can stay in
// that register instead: `ADJSP #-4 ; STORE Rs, [SP] ... LOAD Rd, [SP] ;
// ADJSP #4` becomes `MOV Rc, Rs ... MOV Rd, Rc`.
int forward_stack_temporaries(std::vector<std::string>& lines) {
    const LivenessInfo liveness = compute_register_liveness(lines);
    int removed = 0;
    for (std::size_t index = 0; index + 2 < lines.size(); ++index) {
        Instruction adjust;
        if (!parse_instruction(lines[index], adjust) ||
            adjust.mnemonic != "ADJSP" || adjust.operands.size() != 1 ||
            trim(adjust.operands[0]) != "#-4") {
            continue;
        }
        std::size_t store_index = index + 1;
        while (store_index < lines.size() && lines[store_index].empty()) {
            ++store_index;
        }
        Instruction store;
        if (store_index >= lines.size() ||
            !parse_instruction(lines[store_index], store) ||
            store.mnemonic != "STORE" || store.operands.size() != 2) {
            continue;
        }
        int source = 0;
        if (!parse_register(store.operands[0], source)) {
            continue;
        }
        int offset = 0;
        if (!parse_stack_address(store.operands[1], offset) || offset != 0) {
            continue;
        }
        // Find the matching reload and make sure the span leaves SP alone.
        std::size_t load_index = store_index + 1;
        bool span_ok = true;
        RegisterMask span_uses = 0;
        RegisterMask span_defs = 0;
        while (load_index < lines.size()) {
            Instruction instruction;
            if (!parse_instruction(lines[load_index], instruction)) {
                const std::string trimmed = trim(lines[load_index]);
                if (trimmed.empty()) {
                    ++load_index;
                    continue;
                }
                span_ok = false;
                break;
            }
            const bool touches_stack =
                instruction.mnemonic == "ADJSP" ||
                instruction.mnemonic == "PUSH" ||
                instruction.mnemonic == "POP" ||
                instruction.mnemonic == "CALL" ||
                instruction.mnemonic == "SYSCALL" ||
                is_branch(instruction.mnemonic);
            if (touches_stack) {
                span_ok = false;
                break;
            }
            int destination = 0;
            const bool is_reload =
                instruction.mnemonic == "LOAD" &&
                instruction.operands.size() == 2 &&
                parse_register(instruction.operands[0], destination) &&
                parse_stack_address(instruction.operands[1], offset) &&
                offset == 0;
            if (is_reload) {
                break;
            }
            bool mentions_stack = false;
            for (const std::string& operand : instruction.operands) {
                if (operand.find("[SP") != std::string::npos) {
                    mentions_stack = true;
                    break;
                }
            }
            if (mentions_stack) {
                span_ok = false;
                break;
            }
            RegisterMask uses = 0;
            RegisterMask defs = 0;
            instruction_uses_defs(instruction, uses, defs);
            span_uses = static_cast<RegisterMask>(span_uses | uses);
            span_defs = static_cast<RegisterMask>(span_defs | defs);
            ++load_index;
        }
        if (!span_ok || load_index >= lines.size()) {
            continue;
        }
        Instruction reload;
        if (!parse_instruction(lines[load_index], reload) ||
            reload.mnemonic != "LOAD" || reload.operands.size() != 2) {
            continue;
        }
        int reload_target = 0;
        if (!parse_register(reload.operands[0], reload_target)) {
            continue;
        }
        std::size_t after = load_index + 1;
        while (after < lines.size() && lines[after].empty()) {
            ++after;
        }
        if (after >= lines.size()) {
            continue;
        }
        Instruction restore;
        if (!parse_instruction(lines[after], restore) ||
            restore.mnemonic != "ADJSP" || restore.operands.size() != 1 ||
            trim(restore.operands[0]) != "#4") {
            continue;
        }
        // Pick a caller-saved scratch register the span does not touch and
        // that is dead once the value has been reloaded.
        int chosen = -1;
        for (int candidate = 2; candidate <= 6; ++candidate) {
            const RegisterMask bit = bit_for_register(candidate);
            if ((span_uses & bit) != 0 || (span_defs & bit) != 0) {
                continue;
            }
            if (candidate == source || candidate == reload_target) {
                continue;
            }
            if (register_live_after(liveness, after, candidate)) {
                continue;
            }
            chosen = candidate;
            break;
        }
        if (chosen < 0) {
            continue;
        }
        lines[index].clear();
        lines[store_index] = format_instruction(
            Instruction{"MOV", {"R" + std::to_string(chosen),
                                 "R" + std::to_string(source)}});
        lines[load_index] = format_instruction(
            Instruction{"MOV", {"R" + std::to_string(reload_target),
                                 "R" + std::to_string(chosen)}});
        lines[after].clear();
        ++removed;
    }
    return removed;
}

// AND/SHL/SAR that truncate a value to a byte or halfword are dead work when
// the consumer stores only that many bits.
int fold_narrowing_stores(std::vector<std::string>& lines) {
    int removed = 0;
    for (std::size_t index = 0; index < lines.size(); ++index) {
        Instruction store;
        if (!parse_instruction(lines[index], store) ||
            (store.mnemonic != "STORE8" && store.mnemonic != "STORE16") ||
            store.operands.size() != 2) {
            continue;
        }
        int value = 0;
        if (!parse_register(store.operands[0], value)) {
            continue;
        }
        const bool narrow = store.mnemonic == "STORE8";
        std::size_t cursor = index;
        int chain = 0;
        while (cursor > 0) {
            std::size_t previous = cursor - 1;
            while (previous > 0 && lines[previous].empty()) {
                --previous;
            }
            Instruction step;
            if (!parse_instruction(lines[previous], step) ||
                step.operands.size() != 3) {
                break;
            }
            int destination = 0;
            int source = 0;
            if (!parse_register(step.operands[0], destination) ||
                !parse_register(step.operands[1], source) ||
                destination != value || source != value) {
                break;
            }
            if (step.operands[2].empty() || step.operands[2].front() != '#') {
                break;
            }
            int amount = 0;
            try {
                amount = std::stoi(step.operands[2].substr(1));
            } catch (...) {
                break;
            }
            const int width = narrow ? 24 : 16;
            const int mask = narrow ? 0xFF : 0xFFFF;
            const bool fits =
                (step.mnemonic == "AND" && (amount & mask) == mask) ||
                ((step.mnemonic == "SHL" || step.mnemonic == "SAR" ||
                  step.mnemonic == "SHR") &&
                 amount == width);
            if (!fits) {
                break;
            }
            ++chain;
            cursor = previous;
        }
        if (chain == 0) {
            continue;
        }
        for (std::size_t line = cursor; line < index; ++line) {
            lines[line].clear();
        }
        removed += chain;
    }
    return removed;
}

// MOV Rd, #imm ; MOV Re, Rd  ->  MOV Re, #imm when Rd is dead afterwards.
int fold_literal_copies(std::vector<std::string>& lines) {
    const LivenessInfo liveness = compute_register_liveness(lines);
    int removed = 0;
    for (std::size_t index = 0; index + 1 < lines.size(); ++index) {
        Instruction move;
        if (!parse_instruction(lines[index], move) || move.mnemonic != "MOV" ||
            move.operands.size() != 2) {
            continue;
        }
        int destination = 0;
        if (!parse_register(move.operands[0], destination) ||
            destination == 7 || destination == 11 ||
            move.operands[1].empty() || move.operands[1][0] != '#') {
            continue;
        }
        std::size_t next = index + 1;
        while (next < lines.size() && lines[next].empty()) {
            ++next;
        }
        if (next >= lines.size()) {
            continue;
        }
        Instruction copy;
        if (!parse_instruction(lines[next], copy) || copy.mnemonic != "MOV" ||
            copy.operands.size() != 2) {
            continue;
        }
        int target = 0;
        int source = 0;
        if (!parse_register(copy.operands[0], target) ||
            !parse_register(copy.operands[1], source) ||
            source != destination || target == destination || target == 7 ||
            target == 11) {
            continue;
        }
        if (register_live_after(liveness, next, destination)) {
            continue;
        }
        copy.operands[1] = move.operands[1];
        lines[next] = format_instruction(copy);
        lines[index].clear();
        ++removed;
    }
    return removed;
}
// MOV Rd, Rs followed by the first reader of Rd: fold the copy into the
// consumer's source operands and drop the move. Reads of Rd in the consumer
// see the value the copy produced, whether or not the consumer also writes Rd.
int fold_copy_consumers(std::vector<std::string>& lines) {
    const LivenessInfo liveness = compute_register_liveness(lines);
    int removed = 0;
    for (std::size_t index = 0; index + 1 < lines.size(); ++index) {
        Instruction move;
        if (!parse_instruction(lines[index], move) || move.mnemonic != "MOV" ||
            move.operands.size() != 2) {
            continue;
        }
        int destination = 0;
        int source = 0;
        if (!parse_register(move.operands[0], destination) ||
            !parse_register(move.operands[1], source) ||
            destination == source || destination == 7 || destination == 11 ||
            source == 7 || source == 11) {
            continue;
        }
        const RegisterMask bit = bit_for_register(destination);
        const RegisterMask source_bit = bit_for_register(source);
        std::size_t consumer = 0;
        bool found = false;
        bool consumer_writes = false;
        for (std::size_t scan = index + 1; scan < lines.size(); ++scan) {
            Instruction instruction;
            if (!parse_instruction(lines[scan], instruction)) {
                if (trim(lines[scan]).empty()) {
                    continue;
                }
                break;
            }
            if (is_branch(instruction.mnemonic) ||
                is_call(instruction.mnemonic) ||
                is_return_mnemonic(instruction.mnemonic)) {
                break;
            }
            RegisterMask uses = 0;
            RegisterMask defs = 0;
            instruction_uses_defs(instruction, uses, defs);
            if ((uses & bit) == 0) {
                if ((defs & bit) != 0) {
                    break;
                }
                if ((defs & source_bit) != 0) {
                    break;
                }
                continue;
            }
            consumer = scan;
            consumer_writes = (defs & bit) != 0;
            found = true;
            break;
        }
        if (!found) {
            continue;
        }
        if (!consumer_writes &&
            register_live_after(liveness, consumer, destination)) {
            continue;
        }
        Instruction instruction;
        if (!parse_instruction(lines[consumer], instruction)) {
            continue;
        }
        const std::size_t first =
            writes_first_operand(instruction) ? 1 : 0;
        bool replaced = false;
        for (std::size_t operand = first;
             operand < instruction.operands.size(); ++operand) {
            if (replace_register_operand(instruction.operands[operand],
                                         destination, source)) {
                replaced = true;
            }
        }
        if (!replaced) {
            continue;
        }
        lines[consumer] = format_instruction(instruction);
        lines[index].clear();
        ++removed;
    }
    return removed;
}
int fold_push_pop_pairs(std::vector<std::string>& lines, int& rewritten) {
    int removed = 0;
    for (std::size_t index = 0; index < lines.size(); ++index) {
        Instruction push;
        if (!parse_instruction(lines[index], push) ||
            push.mnemonic != "PUSH" || push.operands.size() != 1) {
            continue;
        }
        int source = 0;
        if (!parse_register(push.operands[0], source)) {
            continue;
        }
        int depth = 1;
        bool invalid = false;
        std::size_t pop_index = index + 1;
        for (; pop_index < lines.size(); ++pop_index) {
            const std::string text = trim(lines[pop_index]);
            if (text.empty()) {
                continue;
            }
            if (is_label(text)) {
                invalid = true;
                break;
            }
            Instruction instruction;
            if (!parse_instruction(text, instruction)) {
                invalid = true;
                break;
            }
            if (instruction.mnemonic == "PUSH") {
                ++depth;
                continue;
            }
            if (instruction.mnemonic == "POP") {
                --depth;
                if (depth == 0) {
                    break;
                }
                continue;
            }
            if (is_call(instruction.mnemonic) ||
                is_branch(instruction.mnemonic) ||
                is_return_mnemonic(instruction.mnemonic) ||
                instruction.mnemonic == "HALT" ||
                instruction.mnemonic == "ADJSP") {
                invalid = true;
                break;
            }
            for (const std::string& operand : instruction.operands) {
                if (operand.find("SP") != std::string::npos) {
                    invalid = true;
                    break;
                }
            }
            if (invalid) {
                break;
            }
        }
        if (invalid || pop_index >= lines.size()) {
            continue;
        }
        Instruction pop;
        if (!parse_instruction(lines[pop_index], pop) ||
            pop.mnemonic != "POP" || pop.operands.size() != 1) {
            continue;
        }
        int destination = 0;
        if (!parse_register(pop.operands[0], destination) ||
            destination == 7) {
            continue;
        }
        bool destination_conflict = false;
        for (std::size_t scan = index + 1; scan < pop_index; ++scan) {
            Instruction instruction;
            if (!parse_instruction(lines[scan], instruction)) {
                continue;
            }
            if (writes_first_operand(instruction) &&
                !instruction.operands.empty()) {
                int written = 0;
                if (parse_register(instruction.operands[0], written) &&
                    written == destination) {
                    destination_conflict = true;
                    break;
                }
            }
            const std::size_t first_read =
                writes_first_operand(instruction) ? 1 : 0;
            for (std::size_t operand = first_read;
                 operand < instruction.operands.size(); ++operand) {
                int read = 0;
                if ((parse_register(instruction.operands[operand], read) ||
                     parse_register_address(instruction.operands[operand],
                                            read)) &&
                    read == destination) {
                    destination_conflict = true;
                    break;
                }
            }
            if (destination_conflict) {
                break;
            }
        }
        if (destination_conflict) {
            continue;
        }
        if (destination == source) {
            lines[index].clear();
            lines[pop_index].clear();
            removed += 2;
        } else {
            lines[index] = "    MOV R" + std::to_string(destination) +
                           ", R" + std::to_string(source);
            lines[pop_index].clear();
            ++removed;
            ++rewritten;
        }
    }
    return removed;
}

int strength_reduce_immediates(std::vector<std::string>& lines,
                               int& rewritten) {
    int changed = 0;
    for (std::size_t index = 0; index < lines.size(); ++index) {
        Instruction instruction;
        if (!parse_instruction(lines[index], instruction) ||
            instruction.operands.size() != 3) {
            continue;
        }
        const bool multiply = instruction.mnemonic == "MUL";
        const bool divide = instruction.mnemonic == "DIV";
        const bool modulo = instruction.mnemonic == "MOD";
        if (!multiply && !divide && !modulo) {
            continue;
        }
        int destination = 0;
        int source = 0;
        if (!parse_register(instruction.operands[0], destination) ||
            !parse_register(instruction.operands[1], source) ||
            instruction.operands[2].empty() ||
            instruction.operands[2].front() != '#') {
            continue;
        }
        long long value = 0;
        try {
            value = std::stoll(instruction.operands[2].substr(1));
        } catch (...) {
            continue;
        }
        if (value < 0) {
            continue;
        }
        std::size_t after = index + 1;
        while (after < lines.size() && lines[after].empty()) {
            ++after;
        }
        if (after < lines.size()) {
            Instruction following;
            if (parse_instruction(lines[after], following) &&
                is_conditional_jump(following.mnemonic)) {
                continue;
            }
        }
        if (value == 0) {
            if (multiply || modulo) {
                lines[index] = "    MOV R" +
                               std::to_string(destination) + ", #0";
                ++rewritten;
                ++changed;
            }
            continue;
        }
        const bool power_of_two =
            (value & (value - 1)) == 0 && value <= 0x80000000LL;
        if (!power_of_two) {
            continue;
        }
        int shift = 0;
        long long remaining = value;
        while (remaining > 1) {
            remaining >>= 1;
            ++shift;
        }
        const std::string destination_name =
            "R" + std::to_string(destination);
        const std::string source_name = "R" + std::to_string(source);
        if (multiply) {
            if (value == 1) {
                lines[index] = "    MOV " + destination_name + ", " +
                               source_name;
            } else {
                lines[index] = "    SHL " + destination_name + ", " +
                               source_name + ", #" + std::to_string(shift);
            }
            ++rewritten;
            ++changed;
        } else if (divide) {
            if (value == 1) {
                lines[index] = "    MOV " + destination_name + ", " +
                               source_name;
            } else {
                lines[index] = "    SHR " + destination_name + ", " +
                               source_name + ", #" + std::to_string(shift);
            }
            ++rewritten;
            ++changed;
        } else {
            lines[index] = "    AND " + destination_name + ", " +
                           source_name + ", #" +
                           std::to_string(value - 1);
            ++rewritten;
            ++changed;
        }
    }
    return changed;
}

// PUSH R7 ; LEA R7, [SP] ... POP R7 with no other use of R7: the function has
// no frame, so the prologue and epilogue are dead.
int remove_unused_frame_prologues(std::vector<std::string>& lines) {
    const std::vector<CfgFunction> functions = build_cfgs(lines);
    int removed = 0;
    for (const CfgFunction& function : functions) {
        std::size_t cursor = function.begin;
        while (cursor < function.end) {
            Instruction instruction;
            if (!parse_instruction(lines[cursor], instruction)) {
                ++cursor;
                continue;
            }
            break;
        }
        if (cursor >= function.end) {
            continue;
        }
        Instruction push;
        if (!parse_instruction(lines[cursor], push) ||
            push.mnemonic != "PUSH" || push.operands.size() != 1 ||
            push.operands[0] != "R7") {
            continue;
        }
        const std::size_t push_line = cursor;
        std::size_t next = push_line + 1;
        while (next < function.end && lines[next].empty()) {
            ++next;
        }
        // The frame is allocated before the frame pointer is set up.
        std::size_t frame_line = function.end;
        int frame_bytes = 0;
        if (next < function.end) {
            Instruction allocate;
            if (parse_instruction(lines[next], allocate) &&
                allocate.mnemonic == "ADJSP" && allocate.operands.size() == 1 &&
                allocate.operands[0].size() > 1 &&
                allocate.operands[0][0] == '#') {
                try {
                    frame_bytes = std::stoi(allocate.operands[0].substr(1));
                } catch (...) {
                    frame_bytes = 0;
                }
                if (frame_bytes < 0) {
                    frame_line = next;
                    ++next;
                    while (next < function.end && lines[next].empty()) {
                        ++next;
                    }
                } else {
                    frame_bytes = 0;
                }
            }
        }
        if (next >= function.end) {
            continue;
        }
        Instruction setup;
        if (!parse_instruction(lines[next], setup) || setup.mnemonic != "LEA" ||
            setup.operands.size() != 2 || setup.operands[0] != "R7" ||
            trim(setup.operands[1]) != "[SP]") {
            continue;
        }
        const std::size_t setup_line = next;
        int pops = 0;
        bool used = false;
        std::size_t pop_line = function.end;
        for (std::size_t scan = setup_line + 1; scan < function.end; ++scan) {
            Instruction instruction;
            if (!parse_instruction(lines[scan], instruction)) {
                continue;
            }
            if (instruction.mnemonic == "POP" &&
                instruction.operands.size() == 1 &&
                instruction.operands[0] == "R7") {
                ++pops;
                pop_line = scan;
                continue;
            }
            if (instruction_touches_register(instruction, 7)) {
                used = true;
                break;
            }
        }
        if (used || pops != 1) {
            continue;
        }
        // The frame release immediately before the pop, when it matches.
        std::size_t release_line = function.end;
        if (frame_line != function.end) {
            std::size_t before = pop_line;
            while (before > function.begin && lines[before - 1].empty()) {
                --before;
            }
            if (before > function.begin && before - 1 >= function.begin) {
                Instruction release;
                if (parse_instruction(lines[before - 1], release) &&
                    release.mnemonic == "ADJSP" &&
                    release.operands.size() == 1 &&
                    release.operands[0] == "#" +
                                             std::to_string(-frame_bytes)) {
                    release_line = before - 1;
                }
            }
        }
        lines[push_line].clear();
        lines[setup_line].clear();
        lines[pop_line].clear();
        removed += 3;
        if (release_line != function.end) {
            lines[frame_line].clear();
            lines[release_line].clear();
            removed += 2;
        }
    }
    return removed;
}
int remove_zero_frame_adjustments(std::vector<std::string>& lines) {
    int removed = 0;
    for (std::string& line : lines) {
        const std::string value = trim(line);
        if (value == "ADJSP #-0" || value == "ADJSP #0") {
            line.clear();
            ++removed;
        }
    }
    return removed;
}

int remove_noop_moves(std::vector<std::string>& lines) {
    int removed = 0;
    for (std::string& line : lines) {
        Instruction instruction;
        if (!parse_instruction(line, instruction) ||
            instruction.mnemonic != "MOV" ||
            instruction.operands.size() != 2) {
            continue;
        }
        int left = 0;
        int right = 0;
        if (parse_register(instruction.operands[0], left) &&
            parse_register(instruction.operands[1], right) && left == right) {
            line.clear();
            ++removed;
        }
    }
    return removed;
}

std::string join_lines(const std::vector<std::string>& lines) {
    std::string result;
    for (const std::string& line : lines) {
        if (line.empty()) {
            continue;
        }
        result += line;
        result += '\n';
    }
    return result;
}

std::vector<std::string> split_lines(std::string_view assembly) {
    std::vector<std::string> lines;
    std::size_t begin = 0;
    while (begin <= assembly.size()) {
        const std::size_t end = assembly.find('\n', begin);
        lines.emplace_back(assembly.substr(
            begin, end == std::string_view::npos ? std::string_view::npos
                                                 : end - begin));
        if (end == std::string_view::npos) {
            break;
        }
        begin = end + 1;
    }
    while (!lines.empty() && lines.back().empty()) {
        lines.pop_back();
    }
    return lines;
}

} // namespace

OptimizeStats optimize_assembly(std::string& assembly) {
    OptimizeStats stats;
    std::vector<std::string> lines = split_lines(assembly);
    stats.removed += remove_noop_moves(lines);
    stats.removed += remove_adjacent_slot_roundtrips(lines);
    stats.removed += fold_symbol_address_loads(lines);
    stats.removed += fold_symbol_offsets(lines);
    stats.removed += fold_unary_constants(lines);
    stats.removed += fold_symbol_address_stores(lines);
    stats.removed += fold_pointer_load_offsets(lines);
    stats.removed += fold_pointer_store_offsets(lines);
    stats.removed += remove_repeated_frame_loads(lines);
    fold_tail_calls(lines, stats.rewritten);
    stats.removed += fold_load_copy_overwrites(lines);
    int add_chain = 0;
    do {
        add_chain = fold_add_immediate_chain(lines);
        stats.removed += add_chain;
    } while (add_chain > 0);
    stats.removed += remove_unreachable_after_terminators(lines);
    stats.removed += remove_zero_frame_adjustments(lines);
    stats.removed += remove_unused_frame_prologues(lines);
    stats.removed += fold_frame_address_arithmetic(lines);
    stats.removed += fold_frame_addresses(lines);
    stats.removed += remove_forwarded_loads(lines, stats.rewritten);
    stats.removed += fold_immediate_consumers(lines);
    stats.removed += fold_comparison_values(lines);
    stats.removed += fold_compare_materialization_branches(lines);
    stats.removed += fold_boolean_branches(lines);
    stats.removed += forward_cfg_frame_loads(lines, stats.rewritten);
    stats.removed += remove_dead_frame_stores(lines);
    stats.removed += promote_leaf_scalars(lines);
    stats.removed += promote_call_surviving_scalars(lines, stats.rewritten);
    stats.removed += remove_dead_frame_loads(lines);
    stats.removed += fold_push_pop_pairs(lines, stats.rewritten);
    stats.removed += forward_stack_temporaries(lines);
    stats.removed += fold_narrowing_stores(lines);
    stats.removed += fold_copy_consumers(lines);
    stats.removed += fold_literal_copies(lines);
    stats.removed += fold_clobbered_address_copies(lines);
    stats.removed += fold_address_moves(lines);
    stats.removed += use_absolute_symbol_addresses(lines);
    stats.removed += promote_symbol_bases(lines);
    stats.removed += reuse_symbol_bases(lines);
    stats.removed += strength_reduce_immediates(lines, stats.rewritten);
    stats.removed += coalesce_moves(lines);
    stats.removed += fold_narrowing_stores(lines);
    stats.removed += fold_copy_consumers(lines);
    stats.removed += fold_address_moves(lines);
    stats.removed += remove_dead_moves(lines);
    stats.removed += remove_overwritten_moves(lines);
    // The folds above can expose frame traffic that the first round could not
    // see, so repeat the address and frame clean-up on the folded code.
    stats.removed += fold_clobbered_address_copies(lines);
    stats.removed += fold_address_moves(lines);

    stats.removed += use_absolute_symbol_addresses(lines);
    stats.removed += promote_symbol_bases(lines);
    stats.removed += forward_cfg_frame_loads(lines, stats.rewritten);
    stats.removed += remove_dead_frame_loads(lines);
    stats.removed += fold_push_pop_pairs(lines, stats.rewritten);
    stats.removed += remove_dead_frame_stores(lines);

    stats.removed += remove_zero_frame_adjustments(lines);
    stats.removed += remove_unused_frame_prologues(lines);
    stats.removed += fold_copy_consumers(lines);
    stats.removed += coalesce_moves(lines);
    stats.removed += fold_address_moves(lines);
    stats.removed += remove_dead_moves(lines);
    stats.removed += use_absolute_symbol_addresses(lines);
    stats.removed += promote_symbol_bases(lines);
    stats.removed += fold_address_moves(lines);
    stats.removed += remove_dead_moves(lines);
    stats.removed += remove_dead_frame_loads(lines);
    stats.removed += remove_dead_frame_stores(lines);
    stats.removed += remove_redundant_jumps(lines);
    stats.removed += remove_dead_functions(lines);
    for (std::string& line : lines) {
        Instruction instruction;
        if (parse_instruction(line, instruction) &&
            instruction.mnemonic == "RETW") {
            instruction.mnemonic = "RET";
            line = format_instruction(instruction);
            continue;
        }
        if (parse_instruction(line, instruction) &&
            instruction.mnemonic == "CALL" &&
            instruction.operands.size() > 1 &&
            instruction.operands[1].size() > 1 &&
            instruction.operands[1][0] == '#') {
            instruction.operands.pop_back();
            line = format_instruction(instruction);
        }
    }
    assembly = join_lines(lines);
    return stats;
}

} // namespace kr32::assembler
