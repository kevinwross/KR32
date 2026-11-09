#include "kr32/asm/optimizer.h"

#include <algorithm>
#include <array>
#include <cctype>
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

bool writes_first_operand(const Instruction& instruction) {
    if (instruction.operands.empty()) {
        return false;
    }
    if (instruction.mnemonic == "CMP" || instruction.mnemonic == "STORE" ||
        instruction.mnemonic == "STORE8" || instruction.mnemonic == "STORE16" ||
        instruction.mnemonic == "PUSH" || instruction.mnemonic == "FENCE" ||
        instruction.mnemonic == "NOP" || instruction.mnemonic == "RET" ||
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
    if (instruction.mnemonic == "RET") {
        ++counts.reads[0];
        ++counts.reads[1];
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
    std::map<int, std::set<int>> used;
    int function = -1;
    for (const std::string& line : lines) {
        if (trim(line) == ".text") {
            ++function;
            used[function];
            continue;
        }
        if (function < 0) {
            function = 0;
            used[function];
        }
        Instruction instruction;
        if (!parse_instruction(line, instruction)) {
            continue;
        }
        const bool reads_memory =
            instruction.mnemonic == "LEA" ||
            instruction.mnemonic == "LOAD" ||
            instruction.mnemonic == "LOAD8U" ||
            instruction.mnemonic == "LOAD8S" ||
            instruction.mnemonic == "LOAD16U" ||
            instruction.mnemonic == "LOAD16S";
        if (reads_memory && instruction.operands.size() >= 2) {
            int slot = 0;
            if (parse_frame_slot(instruction.operands[1], slot)) {
                used[function].insert(slot);
            }
        }
    }

    function = -1;
    for (std::string& line : lines) {
        if (trim(line) == ".text") {
            ++function;
            continue;
        }
        if (function < 0) {
            function = 0;
        }
        Instruction instruction;
        if (!parse_instruction(line, instruction) ||
            instruction.mnemonic != "STORE" ||
            instruction.operands.size() != 2) {
            continue;
        }
        int slot = 0;
        if (parse_frame_slot(instruction.operands[1], slot) &&
            used[function].find(slot) == used[function].end()) {
            line.clear();
            ++removed;
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
    for (std::size_t index = 0; index < lines.size(); ++index) {
        Instruction instruction;
        if (!parse_instruction(lines[index], instruction) ||
            instruction.mnemonic != "MOV" ||
            instruction.operands.size() != 2) {
            continue;
        }
        int destination = 0;
        if (!parse_register(instruction.operands[0], destination) ||
            destination == 0 || destination == 7 || destination == 11) {
            continue;
        }
        const RegisterReadCounts& counts =
            function_counts[static_cast<std::size_t>(function_of_line[index])];
        if (counts.reads[static_cast<std::size_t>(destination)] == 0) {
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
        while (next < lines.size() && trim(lines[next]).empty()) {
            ++next;
        }
        if (next < lines.size() &&
            trim(lines[next]) == instruction.operands[0] + ":") {
            lines[index].clear();
            ++removed;
        }
    }
    return removed;
}

bool is_terminator(const std::string& mnemonic) {
    return mnemonic == "RET" || mnemonic == "JMP" || mnemonic == "HALT";
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
            }
            continue;
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
    stats.removed += fold_symbol_address_loads(lines);
    stats.removed += fold_symbol_address_stores(lines);
    stats.removed += fold_pointer_load_offsets(lines);
    stats.removed += fold_pointer_store_offsets(lines);
    stats.removed += remove_repeated_frame_loads(lines);
    stats.removed += fold_load_copy_overwrites(lines);
    int add_chain = 0;
    do {
        add_chain = fold_add_immediate_chain(lines);
        stats.removed += add_chain;
    } while (add_chain > 0);
    stats.removed += remove_unreachable_after_terminators(lines);
    stats.removed += remove_zero_frame_adjustments(lines);
    stats.removed += fold_frame_address_arithmetic(lines);
    stats.removed += fold_frame_addresses(lines);
    stats.removed += remove_forwarded_loads(lines, stats.rewritten);
    stats.removed += fold_immediate_consumers(lines);
    stats.removed += fold_comparison_values(lines);
    stats.removed += fold_boolean_branches(lines);
    stats.removed += remove_dead_frame_stores(lines);
    stats.removed += remove_dead_moves(lines);
    stats.removed += remove_overwritten_moves(lines);
    stats.removed += remove_redundant_jumps(lines);
    assembly = join_lines(lines);
    return stats;
}

} // namespace kr32::assembler
