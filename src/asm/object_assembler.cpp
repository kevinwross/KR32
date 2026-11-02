#include "kr32/asm/object_assembler.h"

#include "kr32/isa/encoding.h"

#include <algorithm>
#include <bit>
#include <cctype>
#include <cstdint>
#include <cstdlib>
#include <map>
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace kr32::assembler {
namespace {

struct Expr {
    bool symbolic = false;
    std::string symbol;
    std::int64_t addend = 0;
    std::int64_t value = 0;
};

struct Statement {
    int line = 0;
    std::string label;
    std::string mnemonic;
    std::vector<std::string> operands;
};

struct MemoryOperand {
    int mode = 0;
    int base = -1;
    Expr displacement;
};

struct Label {
    std::uint32_t section = 0;
    std::uint32_t offset = 0;
    bool global = false;
    bool weak = false;
};

std::string trim(std::string_view text) {
    std::size_t begin = 0;
    while (begin < text.size() && std::isspace(static_cast<unsigned char>(text[begin])) != 0) {
        ++begin;
    }
    std::size_t end = text.size();
    while (end > begin && std::isspace(static_cast<unsigned char>(text[end - 1])) != 0) {
        --end;
    }
    return std::string(text.substr(begin, end - begin));
}

std::string lower(std::string_view text) {
    std::string result(text);
    std::transform(result.begin(), result.end(), result.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return result;
}

bool parse_fp_literal(std::string_view raw, double& value, Error& error, int line) {
    std::string text = trim(raw);
    if (!text.empty() && (text.back() == 'f' || text.back() == 'F' ||
                          text.back() == 'l' || text.back() == 'L')) {
        text.pop_back();
    }
    char* end = nullptr;
    value = std::strtod(text.c_str(), &end);
    if (end == nullptr || *end != '\0') {
        error = {"invalid floating-point literal: " + std::string(raw), line};
        return false;
    }
    return true;
}

bool is_identifier_start(char c) {
    return std::isalpha(static_cast<unsigned char>(c)) != 0 || c == '_' || c == '.';
}

bool is_identifier_char(char c) {
    return std::isalnum(static_cast<unsigned char>(c)) != 0 || c == '_' || c == '.';
}

std::string strip_comment(std::string_view text) {
    std::string result;
    bool in_string = false;
    bool escaped = false;
    for (char c : text) {
        if (in_string) {
            result.push_back(c);
            if (escaped) {
                escaped = false;
            } else if (c == '\\') {
                escaped = true;
            } else if (c == '"') {
                in_string = false;
            }
        } else if (c == '"') {
            in_string = true;
            result.push_back(c);
        } else if (c == ';') {
            break;
        } else {
            result.push_back(c);
        }
    }
    return result;
}

std::vector<std::string> split_operands(std::string_view text) {
    std::vector<std::string> operands;
    std::string current;
    bool in_string = false;
    bool escaped = false;
    int brackets = 0;
    for (char c : text) {
        if (in_string) {
            current.push_back(c);
            if (escaped) {
                escaped = false;
            } else if (c == '\\') {
                escaped = true;
            } else if (c == '"') {
                in_string = false;
            }
            continue;
        }
        if (c == '"') {
            in_string = true;
            current.push_back(c);
        } else if (c == '[') {
            ++brackets;
            current.push_back(c);
        } else if (c == ']') {
            --brackets;
            current.push_back(c);
        } else if (c == ',' && brackets == 0) {
            operands.push_back(trim(current));
            current.clear();
        } else {
            current.push_back(c);
        }
    }
    if (!current.empty() || !text.empty()) {
        operands.push_back(trim(current));
    }
    return operands;
}

bool parse_statement(std::string_view raw, int line, Statement& statement) {
    statement = {};
    statement.line = line;
    std::string text = trim(strip_comment(raw));
    if (text.empty()) {
        return true;
    }
    if (is_identifier_start(text.front())) {
        std::size_t end = 1;
        while (end < text.size() && is_identifier_char(text[end])) {
            ++end;
        }
        if (end < text.size() && text[end] == ':') {
            statement.label = text.substr(0, end);
            text = trim(std::string_view(text).substr(end + 1));
        }
    }
    if (text.empty()) {
        return true;
    }
    std::size_t split = 0;
    while (split < text.size() && std::isspace(static_cast<unsigned char>(text[split])) == 0) {
        ++split;
    }
    statement.mnemonic = lower(std::string_view(text).substr(0, split));
    statement.operands = split_operands(trim(std::string_view(text).substr(split)));
    return true;
}

bool parse_register(std::string_view text, int& reg) {
    const std::string value = trim(text);
    if (value.size() < 2 || (value[0] != 'r' && value[0] != 'R')) {
        return false;
    }
    std::size_t index = 1;
    while (index < value.size() && std::isdigit(static_cast<unsigned char>(value[index])) != 0) {
        ++index;
    }
    if (index != value.size() || index == 1) {
        return false;
    }
    const int number = std::atoi(value.c_str() + 1);
    if (number < 0 || number >= 16) {
        return false;
    }
    reg = number;
    return true;
}

bool parse_fpr(std::string_view text, int& reg) {
    const std::string value = trim(text);
    if (value.size() < 2 || (value[0] != 'f' && value[0] != 'F')) {
        return false;
    }
    std::size_t index = 1;
    while (index < value.size() && std::isdigit(static_cast<unsigned char>(value[index])) != 0) {
        ++index;
    }
    if (index != value.size() || index == 1) {
        return false;
    }
    const int number = std::atoi(value.c_str() + 1);
    if (number < 0 || number >= 16) {
        return false;
    }
    reg = number;
    return true;
}

bool parse_tile(std::string_view text, int& reg) {
    const std::string value = trim(text);
    if (value.size() < 2 || (value[0] != 't' && value[0] != 'T')) {
        return false;
    }
    std::size_t index = 1;
    while (index < value.size() && std::isdigit(static_cast<unsigned char>(value[index])) != 0) {
        ++index;
    }
    if (index != value.size() || index == 1) {
        return false;
    }
    const int number = std::atoi(value.c_str() + 1);
    if (number < 0 || number >= 16) {
        return false;
    }
    reg = number;
    return true;
}

bool eval_expr(std::string_view raw,
               const std::map<std::string, std::int64_t>& constants,
               Expr& result,
               Error& error,
               int line) {
    result = {};
    const std::string text = trim(raw);
    if (text.empty()) {
        error = {"empty expression", line};
        return false;
    }
    std::size_t index = 0;
    int sign = 1;
    while (index < text.size()) {
        while (index < text.size() && std::isspace(static_cast<unsigned char>(text[index])) != 0) {
            ++index;
        }
        if (index >= text.size()) {
            break;
        }
        if (text[index] == '+') {
            sign = 1;
            ++index;
            continue;
        }
        if (text[index] == '-') {
            sign = -1;
            ++index;
            continue;
        }
        if (std::isdigit(static_cast<unsigned char>(text[index])) != 0) {
            const std::size_t begin = index;
            if (text[index] == '0' && index + 1 < text.size() &&
                (text[index + 1] == 'x' || text[index + 1] == 'X')) {
                index += 2;
                while (index < text.size() &&
                       std::isxdigit(static_cast<unsigned char>(text[index])) != 0) {
                    ++index;
                }
            } else {
                ++index;
                while (index < text.size() &&
                       std::isdigit(static_cast<unsigned char>(text[index])) != 0) {
                    ++index;
                }
            }
            const std::string number(text.substr(begin, index - begin));
            char* end = nullptr;
            const long long parsed = std::strtoll(number.c_str(), &end, 0);
            if (end == nullptr || *end != '\0') {
                error = {"invalid number: " + number, line};
                return false;
            }
            if (result.symbolic) {
                result.addend += static_cast<std::int64_t>(sign) * parsed;
            } else {
                result.value += static_cast<std::int64_t>(sign) * parsed;
            }
            sign = 1;
            continue;
        }
        if (is_identifier_start(text[index])) {
            const std::size_t begin = index;
            ++index;
            while (index < text.size() && is_identifier_char(text[index])) {
                ++index;
            }
            const std::string name(text.substr(begin, index - begin));
            const auto constant = constants.find(name);
            if (constant != constants.end()) {
                if (result.symbolic) {
                    result.addend += static_cast<std::int64_t>(sign) * constant->second;
                } else {
                    result.value += static_cast<std::int64_t>(sign) * constant->second;
                }
            } else {
                if (result.symbolic) {
                    error = {"expression has more than one symbol", line};
                    return false;
                }
                if (sign < 0) {
                    error = {"negative symbol references are not supported", line};
                    return false;
                }
                result.symbolic = true;
                result.symbol = name;
                result.addend = result.value;
                result.value = 0;
            }
            sign = 1;
            continue;
        }
        error = {"unexpected character in expression", line};
        return false;
    }
    return true;
}

bool parse_memory(std::string_view raw,
                  const std::map<std::string, std::int64_t>& constants,
                  MemoryOperand& memory,
                  Error& error,
                  int line) {
    memory = {};
    std::string text = trim(raw);
    if (text.size() < 2 || text.front() != '[' || text.back() != ']') {
        error = {"expected a memory operand", line};
        return false;
    }
    text = trim(std::string_view(text).substr(1, text.size() - 2));
    const std::string lowered = lower(text);
    auto starts_with = [&](std::string_view word) {
        return lowered.size() >= word.size() &&
               lowered.compare(0, word.size(), word.data(), word.size()) == 0 &&
               (lowered.size() == word.size() || !is_identifier_char(lowered[word.size()]));
    };
    if (starts_with("sp")) {
        memory.mode = 4;
        std::string rest = trim(std::string_view(text).substr(2));
        if (!rest.empty()) {
            if (rest.front() == '+') {
                rest = trim(std::string_view(rest).substr(1));
            }
            if (!eval_expr(rest, constants, memory.displacement, error, line)) {
                return false;
            }
        }
        return true;
    }
    if (starts_with("pc")) {
        memory.mode = 6;
        std::string rest = trim(std::string_view(text).substr(2));
        if (!rest.empty()) {
            if (rest.front() == '+') {
                rest = trim(std::string_view(rest).substr(1));
            }
            if (!eval_expr(rest, constants, memory.displacement, error, line)) {
                return false;
            }
        }
        return true;
    }
    int base = 0;
    if (parse_register(text.substr(0, text.find_first_of(" +-")), base)) {
        memory.base = base;
        const std::size_t sign = text.find_first_of("+-", text.find_first_not_of(" \t"));
        if (sign == std::string::npos) {
            memory.mode = 2;
            return true;
        }
        memory.mode = 3;
        std::string rest = trim(std::string_view(text).substr(sign));
        if (!rest.empty() && rest.front() == '+') {
            rest = trim(std::string_view(rest).substr(1));
        }
        if (!eval_expr(rest, constants, memory.displacement, error, line)) {
            return false;
        }
        return true;
    }
    memory.mode = 5;
    return eval_expr(text, constants, memory.displacement, error, line);
}

bool parse_string(std::string_view raw, std::vector<std::uint8_t>& bytes, Error& error, int line) {
    const std::string text = trim(raw);
    if (text.size() < 2 || text.front() != '"' || text.back() != '"') {
        error = {"expected a quoted string", line};
        return false;
    }
    bytes.clear();
    for (std::size_t index = 1; index + 1 < text.size(); ++index) {
        char c = text[index];
        if (c != '\\') {
            bytes.push_back(static_cast<std::uint8_t>(c));
            continue;
        }
        if (index + 1 >= text.size() - 1) {
            error = {"unterminated escape", line};
            return false;
        }
        const char escaped = text[++index];
        switch (escaped) {
        case 'n': bytes.push_back('\n'); break;
        case 'r': bytes.push_back('\r'); break;
        case 't': bytes.push_back('\t'); break;
        case '0': bytes.push_back('\0'); break;
        case '\\': bytes.push_back('\\'); break;
        case '"': bytes.push_back('"'); break;
        default: bytes.push_back(static_cast<std::uint8_t>(escaped)); break;
        }
    }
    return true;
}

class ObjectAssembler {
public:
    ObjectAssembler(Error& error) : error_(error) {
        add_section(".text", container::SectionType::Text,
                    container::Alloc | container::Execute);
        add_section(".rodata", container::SectionType::Rodata, container::Alloc);
        add_section(".cpp_except", container::SectionType::Rodata,
                    container::Alloc);
        add_section(".data", container::SectionType::Data,
                    container::Alloc | container::Write);
        add_section(".bss", container::SectionType::Bss,
                    container::Alloc | container::Write);
        add_section(".debug_info", container::SectionType::Debug, 0);
    }

    bool run(std::string_view source, container::Image& image) {
        if (!parse_source(source)) {
            return false;
        }
        if (!pass1()) {
            return false;
        }
        if (!pass2(image)) {
            return false;
        }
        return true;
    }

private:
    void add_section(std::string name, container::SectionType type, std::uint32_t flags) {
        container::Section section;
        section.name = std::move(name);
        section.type = type;
        section.flags = flags;
        section.alignment = 4;
        sections_.push_back(std::move(section));
    }

    bool fail(int line, std::string message) {
        error_ = {std::move(message), line};
        return false;
    }

    bool parse_source(std::string_view source) {
        std::string text(source);
        std::size_t begin = 0;
        int line = 1;
        while (begin <= text.size()) {
            const std::size_t end = text.find('\n', begin);
            const std::string_view raw =
                end == std::string::npos ? std::string_view(text).substr(begin)
                                         : std::string_view(text).substr(begin, end - begin);
            Statement statement;
            if (!parse_statement(raw, line, statement)) {
                return false;
            }
            if (!statement.label.empty() || !statement.mnemonic.empty()) {
                statements_.push_back(std::move(statement));
            }
            if (end == std::string::npos) {
                break;
            }
            begin = end + 1;
            ++line;
        }
        return true;
    }

    std::uint32_t offset() const {
        return sections_[current_].type == container::SectionType::Bss
                   ? sections_[current_].size
                   : static_cast<std::uint32_t>(sections_[current_].data.size());
    }

    bool advance(std::uint32_t amount, int line) {
        if (amount > 0xFFFF'FFFFU - offset()) {
            return fail(line, "section too large");
        }
        if (sections_[current_].type == container::SectionType::Bss) {
            sections_[current_].size += amount;
        } else {
            sections_[current_].data.resize(sections_[current_].data.size() + amount, 0);
        }
        return true;
    }

    bool align_section(std::uint32_t alignment, int line) {
        if (alignment == 0 || (alignment & (alignment - 1U)) != 0) {
            return fail(line, "alignment must be a power of two");
        }
        const std::uint32_t pad = (alignment - (offset() % alignment)) % alignment;
        return advance(pad, line);
    }

    bool define_label(const std::string& name, int line) {
        if (labels_.find(name) != labels_.end()) {
            return fail(line, "duplicate label: " + name);
        }
        labels_[name] = {static_cast<std::uint32_t>(current_), offset(),
                         globals_.find(name) != globals_.end()};
        return true;
    }

    bool pass1() {
        current_ = 0;
        for (const Statement& statement : statements_) {
            if (!statement.label.empty() && !define_label(statement.label, statement.line)) {
                return false;
            }
            if (statement.mnemonic.empty()) {
                continue;
            }
            if (statement.mnemonic.front() == '.') {
                if (!pass1_directive(statement)) {
                    return false;
                }
            } else {
                isa::Instruction instruction;
                std::string relocation;
                std::int64_t addend = 0;
                if (!build_instruction(statement, instruction, relocation, addend)) {
                    return false;
                }
                if (!advance(instruction.extension ? 8U : 4U, statement.line)) {
                    return false;
                }
            }
        }
        return true;
    }

    bool pass1_directive(const Statement& statement) {
        const std::string& name = statement.mnemonic;
        if (name == ".text" || name == ".rodata" || name == ".cpp_except" ||
            name == ".data" || name == ".bss" || name == ".debug_info") {
            return switch_section(name, statement.line);
        }
        if (name == ".global" || name == ".globl" || name == ".weak") {
            if (statement.operands.size() != 1) {
                return fail(statement.line, ".global/.weak expects one name");
            }
            globals_.insert(statement.operands[0]);
            if (name == ".weak") {
                weak_symbols_.insert(statement.operands[0]);
            }
            externs_.erase(statement.operands[0]);
            return true;
        }
        if (name == ".extern") {
            if (statement.operands.size() != 1) {
                return fail(statement.line, ".extern expects one name");
            }
            externs_.insert(statement.operands[0]);
            return true;
        }
        if (name == ".equ" || name == ".set") {
            if (statement.operands.size() != 2) {
                return fail(statement.line, ".equ expects a name and a value");
            }
            Expr value;
            if (!eval_expr(statement.operands[1], constants_, value, error_, statement.line)) {
                return false;
            }
            if (value.symbolic) {
                return fail(statement.line, "symbolic .equ value");
            }
            constants_[statement.operands[0]] = value.value;
            return true;
        }
        if (name == ".word") {
            return advance(static_cast<std::uint32_t>(statement.operands.size() * 4U),
                           statement.line);
        }
        if (name == ".float") {
            return advance(static_cast<std::uint32_t>(statement.operands.size() * 4U),
                           statement.line);
        }
        if (name == ".double") {
            return advance(static_cast<std::uint32_t>(statement.operands.size() * 8U),
                           statement.line);
        }
        if (name == ".byte") {
            return advance(static_cast<std::uint32_t>(statement.operands.size()),
                           statement.line);
        }
        if (name == ".space" || name == ".zero") {
            if (statement.operands.size() != 1) {
                return fail(statement.line, ".space expects one size");
            }
            Expr value;
            if (!eval_expr(statement.operands[0], constants_, value, error_, statement.line) ||
                value.symbolic || value.value < 0 || value.value > 0xFFFF'FFFFLL) {
                return fail(statement.line, "invalid .space size");
            }
            return advance(static_cast<std::uint32_t>(value.value), statement.line);
        }
        if (name == ".align" || name == ".balign") {
            if (statement.operands.size() != 1) {
                return fail(statement.line, ".align expects one value");
            }
            Expr value;
            if (!eval_expr(statement.operands[0], constants_, value, error_, statement.line) ||
                value.symbolic) {
                return false;
            }
            return align_section(static_cast<std::uint32_t>(value.value), statement.line);
        }
        if (name == ".asciz" || name == ".ascii" || name == ".string") {
            std::uint32_t total = 0;
            for (const std::string& operand : statement.operands) {
                std::vector<std::uint8_t> bytes;
                if (!parse_string(operand, bytes, error_, statement.line)) {
                    return false;
                }
                total += static_cast<std::uint32_t>(bytes.size() +
                                                    (name == ".ascii" ? 0U : 1U));
            }
            return advance(total, statement.line);
        }
        if (name == ".org") {
            if (statement.operands.size() != 1) {
                return fail(statement.line, ".org expects one offset");
            }
            Expr value;
            if (!eval_expr(statement.operands[0], constants_, value, error_, statement.line) ||
                value.symbolic || value.value < 0) {
                return false;
            }
            const std::uint32_t target = static_cast<std::uint32_t>(value.value);
            if (target < offset()) {
                return fail(statement.line, ".org cannot move backwards");
            }
            return advance(target - offset(), statement.line);
        }
        return fail(statement.line, "unsupported directive: " + name);
    }

    bool switch_section(const std::string& name, int line) {
        for (std::size_t index = 0; index < sections_.size(); ++index) {
            if (sections_[index].name == name) {
                current_ = index;
                return true;
            }
        }
        return fail(line, "unknown section: " + name);
    }

    bool set_payload(const Expr& expression,
                     bool force_extension,
                     isa::Instruction& instruction,
                     std::string& relocation,
                     std::int64_t& addend,
                     int line) {
        if (force_extension) {
            instruction.extension = true;
            instruction.extension_word = 0;
            if (expression.symbolic) {
                relocation = expression.symbol;
                addend = expression.addend;
            } else {
                instruction.extension_word = static_cast<std::uint32_t>(expression.value);
            }
            return true;
        }
        if (expression.value < -1024 || expression.value > 1023) {
            return fail(line, "immediate does not fit in 11 bits");
        }
        instruction.immediate = static_cast<std::int16_t>(expression.value);
        return true;
    }

    bool build_instruction(const Statement& statement,
                           isa::Instruction& instruction,
                           std::string& relocation,
                           std::int64_t& addend) {
        instruction = {};
        relocation.clear();
        addend = 0;
        const std::string& mnemonic = statement.mnemonic;
        const std::vector<std::string>& operands = statement.operands;
        auto fail_operands = [&](std::string_view expected) {
            return fail(statement.line, mnemonic + " expects " + std::string(expected));
        };
        auto parse_immediate = [&](const std::string& text, Expr& expression) {
            std::string value = trim(text);
            if (!value.empty() && value.front() == '#') {
                value = trim(std::string_view(value).substr(1));
            }
            return eval_expr(value, constants_, expression, error_, statement.line);
        };
        auto parse_memory_operand = [&](const std::string& text, MemoryOperand& memory) {
            return parse_memory(text, constants_, memory, error_, statement.line);
        };
        auto set_destination = [&](int reg) { instruction.rd = static_cast<std::uint8_t>(reg); };
        auto set_source = [&](int reg) { instruction.rs1 = static_cast<std::uint8_t>(reg); };

        if (mnemonic == "mov") {
            if (operands.size() != 2) {
                return fail_operands("Rd, Rs or Rd, #imm");
            }
            int destination = 0;
            if (!parse_register(operands[0], destination)) {
                return fail(statement.line, "expected a destination register");
            }
            set_destination(destination);
            instruction.opcode = 0x00;
            int source = 0;
            if (parse_register(operands[1], source)) {
                set_source(source);
                return true;
            }
            Expr value;
            if (!parse_immediate(operands[1], value)) {
                return false;
            }
            instruction.literal = true;
            return set_payload(value, value.symbolic || value.value < -1024 || value.value > 1023,
                               instruction, relocation, addend, statement.line);
        }

        {
            static const std::map<std::string, std::uint32_t> alu = {
                {"add", 0x20}, {"sub", 0x21}, {"and", 0x2A}, {"or", 0x2B},
                {"xor", 0x2C}, {"slt", 0x18}, {"sltu", 0x19}, {"mul", 0x22},
                {"div", 0x25}, {"mod", 0x27}, {"shl", 0x30}, {"shr", 0x31},
                {"sar", 0x32},
            };
            const auto operation = alu.find(mnemonic);
            if (operation != alu.end()) {
                if (operands.size() != 3) {
                    return fail_operands("Rd, Rs1, Rs2 or Rd, Rs1, #imm");
                }
                int destination = 0;
                int source = 0;
                if (!parse_register(operands[0], destination) ||
                    !parse_register(operands[1], source)) {
                    return fail(statement.line, "expected registers");
                }
                set_destination(destination);
                set_source(source);
                instruction.opcode = static_cast<std::uint8_t>(operation->second);
                int right = 0;
                if (parse_register(operands[2], right)) {
                    instruction.field = static_cast<std::uint8_t>(right);
                    return true;
                }
                Expr value;
                if (!parse_immediate(operands[2], value)) {
                    return false;
                }
                instruction.literal = true;
                return set_payload(value, value.symbolic || value.value < -1024 ||
                                             value.value > 1023,
                                   instruction, relocation, addend, statement.line);
            }
        }

        if (mnemonic == "cmp") {
            if (operands.size() != 2) {
                return fail_operands("Rs1, Rs2 or Rs1, #imm");
            }
            int source = 0;
            if (!parse_register(operands[0], source)) {
                return fail(statement.line, "expected a source register");
            }
            set_source(source);
            instruction.opcode = 0x29;
            int right = 0;
            if (parse_register(operands[1], right)) {
                instruction.field = static_cast<std::uint8_t>(right);
                return true;
            }
            Expr value;
            if (!parse_immediate(operands[1], value)) {
                return false;
            }
            instruction.literal = true;
            return set_payload(value, value.symbolic || value.value < -1024 ||
                                         value.value > 1023,
                               instruction, relocation, addend, statement.line);
        }

        if (mnemonic == "load" || mnemonic == "lea" || mnemonic == "load8u" ||
            mnemonic == "load8s" || mnemonic == "load16u" || mnemonic == "load16s") {
            if (operands.size() != 2) {
                return fail_operands("Rd, [addr]");
            }
            int destination = 0;
            if (!parse_register(operands[0], destination)) {
                return fail(statement.line, "expected a destination register");
            }
            MemoryOperand memory;
            if (!parse_memory_operand(operands[1], memory)) {
                return false;
            }
            set_destination(destination);
            instruction.opcode = mnemonic == "load" ? 0x01
                                 : mnemonic == "load8u" ? 0x03
                                 : mnemonic == "load8s" ? 0x04
                                 : mnemonic == "load16u" ? 0x05
                                 : mnemonic == "load16s" ? 0x06
                                 : 0x09;
            if (memory.base >= 0) {
                set_source(memory.base);
            }
            instruction.field = static_cast<std::uint8_t>(memory.mode);
            instruction.literal = true;
            return set_payload(memory.displacement,
                               memory.displacement.symbolic ||
                                   memory.displacement.value < -1024 ||
                                   memory.displacement.value > 1023,
                               instruction, relocation, addend, statement.line);
        }

        if (mnemonic == "store" || mnemonic == "store8" || mnemonic == "store16") {
            if (operands.size() != 2) {
                return fail_operands("Rs, [addr]");
            }
            int source = 0;
            if (!parse_register(operands[0], source)) {
                return fail(statement.line, "expected a source register");
            }
            MemoryOperand memory;
            if (!parse_memory_operand(operands[1], memory)) {
                return false;
            }
            set_destination(source);
            instruction.opcode = mnemonic == "store" ? 0x02
                                 : mnemonic == "store8" ? 0x07
                                 : 0x08;
            if (memory.base >= 0) {
                set_source(memory.base);
            }
            instruction.field = static_cast<std::uint8_t>(memory.mode);
            instruction.literal = true;
            return set_payload(memory.displacement,
                               memory.displacement.symbolic ||
                                   memory.displacement.value < -1024 ||
                                   memory.displacement.value > 1023,
                               instruction, relocation, addend, statement.line);
        }

        if (mnemonic == "jmp" || mnemonic == "call") {
            if (operands.size() != 1) {
                return fail_operands("target");
            }
            instruction.opcode = mnemonic == "jmp" ? 0x38 : 0x3A;
            int target = 0;
            if (parse_register(operands[0], target)) {
                set_source(target);
                return true;
            }
            Expr value;
            if (!parse_immediate(operands[0], value)) {
                return false;
            }
            instruction.literal = true;
            return set_payload(value, true, instruction, relocation, addend, statement.line);
        }

        {
            static const std::map<std::string, std::uint32_t> conditions = {
                {"jz", 0}, {"jne", 1}, {"jnz", 1}, {"jcs", 2}, {"jnc", 3},
                {"jmi", 4}, {"jpl", 5}, {"jvs", 6}, {"jvc", 7}, {"jhi", 8},
                {"jls", 9}, {"jge", 10}, {"jlt", 11}, {"jgt", 12}, {"jle", 13},
            };
            const auto condition = conditions.find(mnemonic);
            if (condition != conditions.end()) {
                if (operands.size() != 1) {
                    return fail_operands("target");
                }
                instruction.opcode = 0x39;
                instruction.field = static_cast<std::uint8_t>(condition->second);
                int target = 0;
                if (parse_register(operands[0], target)) {
                    set_source(target);
                    return true;
                }
                Expr value;
                if (!parse_immediate(operands[0], value)) {
                    return false;
                }
                instruction.literal = true;
                return set_payload(value, true, instruction, relocation, addend,
                                   statement.line);
            }
        }

        if (mnemonic == "push" || mnemonic == "pop") {
            if (operands.size() != 1) {
                return fail_operands("Rs");
            }
            int reg = 0;
            if (!parse_register(operands[0], reg)) {
                return fail(statement.line, "expected a register");
            }
            set_destination(reg);
            instruction.opcode = mnemonic == "push" ? 0x3C : 0x3D;
            return true;
        }

        if (mnemonic == "adjsp") {
            if (operands.size() != 1) {
                return fail_operands("#imm");
            }
            Expr value;
            if (!parse_immediate(operands[0], value)) {
                return false;
            }
            instruction.opcode = 0x3E;
            instruction.literal = true;
            return set_payload(value, value.symbolic || value.value < -1024 ||
                                         value.value > 1023,
                               instruction, relocation, addend, statement.line);
        }

        if (mnemonic == "setsp") {
            if (operands.size() != 1) {
                return fail_operands("Rs");
            }
            int source = 0;
            if (!parse_register(operands[0], source)) {
                return fail(statement.line, "expected a source register");
            }
            instruction.opcode = 0x3F;
            set_source(source);
            return true;
        }

        if (mnemonic == "not" || mnemonic == "neg") {
            if (operands.size() != 2) {
                return fail_operands("Rd, Rs");
            }
            int destination = 0;
            int source = 0;
            if (!parse_register(operands[0], destination) ||
                !parse_register(operands[1], source)) {
                return fail(statement.line, "expected registers");
            }
            set_destination(destination);
            set_source(source);
            instruction.opcode = mnemonic == "not" ? 0x2D : 0x2E;
            return true;
        }

        if (mnemonic == "int" || mnemonic == "syscall") {
            if (operands.size() != 1) {
                return fail_operands("#vector");
            }
            Expr value;
            if (!parse_immediate(operands[0], value)) {
                return false;
            }
            instruction.opcode = mnemonic == "int" ? 0x40 : 0x41;
            instruction.literal = true;
            return set_payload(value, value.symbolic || value.value < -1024 ||
                                         value.value > 1023,
                               instruction, relocation, addend, statement.line);
        }

        if (mnemonic == "ll" || mnemonic == "sc") {
            if (operands.size() != 2) {
                return fail_operands("Rd, [addr]");
            }
            int source = 0;
            if (!parse_register(operands[0], source)) {
                return fail(statement.line, "expected a register");
            }
            MemoryOperand memory;
            if (!parse_memory_operand(operands[1], memory)) {
                return false;
            }
            set_destination(source);
            if (memory.base >= 0) {
                set_source(memory.base);
            }
            instruction.opcode = (mnemonic == "ll") ? 0x46 : 0x47;
            instruction.field = static_cast<std::uint8_t>(memory.mode);
            instruction.literal = true;
            return set_payload(memory.displacement,
                               memory.displacement.symbolic ||
                                   memory.displacement.value < -1024 ||
                                   memory.displacement.value > 1023,
                               instruction, relocation, addend, statement.line);
        }

        if (mnemonic == "nop" || mnemonic == "ret" || mnemonic == "halt" ||
            mnemonic == "brk" || mnemonic == "fence") {
            if (!operands.empty()) {
                return fail_operands("no operands");
            }
            if (mnemonic == "nop") instruction.opcode = 0x45;
            if (mnemonic == "ret") instruction.opcode = 0x3B;
            if (mnemonic == "halt") instruction.opcode = 0x43;
            if (mnemonic == "brk") instruction.opcode = 0x44;
            if (mnemonic == "fence") instruction.opcode = 0x48;
            return true;
        }

        if (mnemonic == "fload.w" || mnemonic == "fload.d" ||
            mnemonic == "fstore.w" || mnemonic == "fstore.d") {
            if (operands.size() != 2) {
                return fail_operands("Fd, [addr]");
            }
            int fpr = 0;
            if (!parse_fpr(operands[0], fpr)) {
                return fail(statement.line, "expected an FP register");
            }
            MemoryOperand memory;
            if (!parse_memory_operand(operands[1], memory)) {
                return false;
            }
            instruction.opcode = (mnemonic.rfind("fload", 0) == 0) ? 0x50 : 0x51;
            instruction.rd = static_cast<std::uint8_t>(fpr);
            instruction.literal = mnemonic.size() >= 2 &&
                                  mnemonic.substr(mnemonic.size() - 2) == ".d";
            if (memory.base >= 0) {
                set_source(memory.base);
            }
            instruction.field = static_cast<std::uint8_t>(memory.mode);
            return set_payload(memory.displacement,
                               memory.displacement.symbolic ||
                                   memory.displacement.value < -1024 ||
                                   memory.displacement.value > 1023,
                               instruction, relocation, addend, statement.line);
        }

        {
            static const std::map<std::string, std::uint32_t> fp_arithmetic = {
                {"fadd", 0x000}, {"fsub", 0x001}, {"fmul", 0x002},
                {"fdiv", 0x003}, {"fsqrt", 0x004}, {"fmin", 0x005},
                {"fmax", 0x006}, {"fabs", 0x007}, {"fneg", 0x008},
                {"feq", 0x009}, {"flt", 0x00A}, {"fle", 0x00B},
            };
            std::string fp_name = mnemonic;
            bool is_double = false;
            if (fp_name.size() > 2 && fp_name.substr(fp_name.size() - 2) == ".d") {
                is_double = true;
                fp_name.resize(fp_name.size() - 2);
            } else if (fp_name.size() > 2 && fp_name.substr(fp_name.size() - 2) == ".s") {
                fp_name.resize(fp_name.size() - 2);
            }
            const auto fp_operation = fp_arithmetic.find(fp_name);
            if (fp_operation != fp_arithmetic.end()) {
                const bool compare = fp_operation->second >= 0x009;
                const bool unary = fp_operation->second == 0x004 ||
                                   fp_operation->second == 0x007 ||
                                   fp_operation->second == 0x008;
                const std::size_t expected = compare ? 3U : (unary ? 2U : 3U);
                if (operands.size() != expected) {
                    return fail_operands("the FP operand count");
                }
                instruction.opcode = 0x60;
                instruction.immediate = static_cast<std::int16_t>(
                    fp_operation->second | (is_double ? 0x100U : 0U));
                int destination = 0;
                int left = 0;
                int right = 0;
                if (compare) {
                    if (!parse_register(operands[0], destination) ||
                        !parse_fpr(operands[1], left) ||
                        !parse_fpr(operands[2], right)) {
                        return fail(statement.line, "expected Rd, Fs1, Fs2");
                    }
                    instruction.rd = static_cast<std::uint8_t>(destination);
                } else {
                    if (!parse_fpr(operands[0], destination) ||
                        !parse_fpr(operands[1], left) ||
                        (operands.size() == 3 && !parse_fpr(operands[2], right))) {
                        return fail(statement.line, "expected Fd, Fs1, Fs2");
                    }
                    instruction.rd = static_cast<std::uint8_t>(destination);
                }
                instruction.rs1 = static_cast<std::uint8_t>(left);
                instruction.field = static_cast<std::uint8_t>(right);
                return true;
            }
        }

        {
            static const std::map<std::string, std::uint32_t> conversions = {
                {"fcvt.s.d", 0x020}, {"fcvt.d.s", 0x021},
                {"fcvt.w.s", 0x022}, {"fcvt.wu.s", 0x023},
                {"fcvt.s.w", 0x024}, {"fcvt.s.wu", 0x025},
                {"fcvt.w.d", 0x026}, {"fcvt.wu.d", 0x027},
                {"fcvt.d.w", 0x028}, {"fcvt.d.wu", 0x029},
                {"fmv.w.x", 0x030}, {"fmv.x.w", 0x031},
            };
            const auto conversion = conversions.find(mnemonic);
            if (conversion != conversions.end()) {
                if (operands.size() != 2) {
                    return fail_operands("destination, source");
                }
                const std::uint32_t function = conversion->second;
                const bool destination_gpr = function == 0x022 || function == 0x023 ||
                                             function == 0x026 || function == 0x027 ||
                                             function == 0x031;
                const bool source_gpr = function == 0x024 || function == 0x025 ||
                                        function == 0x028 || function == 0x029 ||
                                        function == 0x030;
                int destination = 0;
                int source = 0;
                if (destination_gpr) {
                    if (!parse_register(operands[0], destination) ||
                        !parse_fpr(operands[1], source)) {
                        return fail(statement.line, "expected Rd, Fs");
                    }
                } else if (source_gpr) {
                    if (!parse_fpr(operands[0], destination) ||
                        !parse_register(operands[1], source)) {
                        return fail(statement.line, "expected Fd, Rs");
                    }
                } else {
                    if (!parse_fpr(operands[0], destination) ||
                        !parse_fpr(operands[1], source)) {
                        return fail(statement.line, "expected Fd, Fs");
                    }
                }
                instruction.opcode = 0x60;
                instruction.immediate = static_cast<std::int16_t>(function);
                instruction.rd = static_cast<std::uint8_t>(destination);
                instruction.rs1 = static_cast<std::uint8_t>(source);
                return true;
            }
        }

        {
            static const std::map<std::string, std::uint32_t> matrix_ops = {
                {"mmul", 0x000}, {"mmac", 0x001}, {"mvec", 0x002},
                {"mtrans", 0x003}, {"vdot", 0x004}, {"mpush", 0x005},
                {"mpop", 0x006}, {"mdrop", 0x007},
            };
            const auto operation = matrix_ops.find(mnemonic);
            if (operation != matrix_ops.end()) {
                instruction.opcode = 0x61;
                instruction.immediate = static_cast<std::int16_t>(operation->second);
                if (mnemonic == "mdrop") {
                    if (!operands.empty()) {
                        return fail_operands("no operands");
                    }
                    return true;
                }
                if (mnemonic == "mpush" || mnemonic == "mpop") {
                    if (operands.size() != 1) {
                        return fail_operands("Td");
                    }
                    int tile = 0;
                    if (!parse_tile(operands[0], tile)) {
                        return fail(statement.line, "expected a tile register");
                    }
                    instruction.rd = static_cast<std::uint8_t>(tile);
                    return true;
                }
                if (operands.size() < 2 || operands.size() > 3) {
                    return fail_operands("Td, Ta, Tb");
                }
                int destination = 0;
                int left = 0;
                int right = 0;
                if (mnemonic == "vdot") {
                    if (!parse_fpr(operands[0], destination) ||
                        !parse_tile(operands[1], left) ||
                        !parse_tile(operands[2], right)) {
                        return fail(statement.line, "expected Fd, Ta, Tb");
                    }
                } else {
                    if (!parse_tile(operands[0], destination) ||
                        !parse_tile(operands[1], left) ||
                        (operands.size() == 3 && !parse_tile(operands[2], right))) {
                        return fail(statement.line, "expected Td, Ta, Tb");
                    }
                }
                instruction.rd = static_cast<std::uint8_t>(destination);
                instruction.rs1 = static_cast<std::uint8_t>(left);
                instruction.field = static_cast<std::uint8_t>(right);
                return true;
            }
        }

        if (mnemonic == "mload" || mnemonic == "mstore" ||
            mnemonic == "vload" || mnemonic == "vstore") {
            if (operands.size() < 2 || operands.size() > 3) {
                return fail_operands("Td, [addr], optional #stride");
            }
            int tile = 0;
            if (!parse_tile(operands[0], tile)) {
                return fail(statement.line, "expected a tile register");
            }
            MemoryOperand memory;
            if (!parse_memory_operand(operands[1], memory)) {
                return false;
            }
            const bool vector = mnemonic == "vload" || mnemonic == "vstore";
            instruction.opcode = (mnemonic == "mload" || mnemonic == "vload") ? 0x52 : 0x53;
            instruction.rd = static_cast<std::uint8_t>(tile);
            instruction.literal = vector;
            if (memory.base >= 0) {
                set_source(memory.base);
            }
            instruction.field = static_cast<std::uint8_t>(memory.mode);
            if (operands.size() == 3) {
                if (vector) {
                    return fail(statement.line, "strided VLOAD/VSTORE is reserved");
                }
                Expr stride;
                std::string stride_text = trim(operands[2]);
                if (!stride_text.empty() && stride_text.front() == '#') {
                    stride_text = trim(std::string_view(stride_text).substr(1));
                }
                if (!eval_expr(stride_text, constants_, stride, error_, statement.line)) {
                    return false;
                }
                if (memory.displacement.value != 0 || memory.displacement.symbolic) {
                    return fail(statement.line,
                                "strided MLOAD/MSTORE requires a zero displacement");
                }
                instruction.extension = true;
                instruction.extension_word = static_cast<std::uint32_t>(stride.value);
                instruction.immediate = 0;
                return true;
            }
            return set_payload(memory.displacement,
                               memory.displacement.symbolic ||
                                   memory.displacement.value < -1024 ||
                                   memory.displacement.value > 1023,
                               instruction, relocation, addend, statement.line);
        }

        return fail(statement.line, "unsupported instruction: " + mnemonic);
    }

    bool emit8(std::uint8_t value) {
        if (sections_[current_].type == container::SectionType::Bss) {
            sections_[current_].size += 1;
        } else {
            sections_[current_].data.push_back(value);
        }
        return true;
    }

    bool emit32(std::uint32_t value) {
        emit8(static_cast<std::uint8_t>(value & 0xFFU));
        emit8(static_cast<std::uint8_t>((value >> 8) & 0xFFU));
        emit8(static_cast<std::uint8_t>((value >> 16) & 0xFFU));
        emit8(static_cast<std::uint8_t>((value >> 24) & 0xFFU));
        return true;
    }

    bool pass2(container::Image& image) {
        for (container::Section& section : sections_) {
            section.data.clear();
            section.size = 0;
        }
        image_relocations_.clear();
        current_ = 0;
        for (const Statement& statement : statements_) {
            if (statement.mnemonic.empty()) {
                continue;
            }
            if (statement.mnemonic.front() == '.') {
                if (!pass2_directive(statement)) {
                    return false;
                }
                continue;
            }
            isa::Instruction instruction;
            std::string relocation;
            std::int64_t addend = 0;
            if (!build_instruction(statement, instruction, relocation, addend)) {
                return false;
            }
            isa::Encoded encoded;
            if (!isa::encode(instruction, encoded)) {
                return fail(statement.line, "cannot encode instruction");
            }
            const std::uint32_t instruction_offset = offset();
            for (std::uint8_t index = 0; index < encoded.count; ++index) {
                emit32(encoded.words[index]);
            }
            if (!relocation.empty()) {
                if (!instruction.extension) {
                    return fail(statement.line, "relocation requires an extension word");
                }
                image.relocations.push_back(
                    {static_cast<std::uint32_t>(current_), instruction_offset + 4U, relocation,
                     static_cast<std::uint32_t>(addend), container::RelocationType::Abs32});
            }
        }

        image.type = container::ImageType::Object;
        image.base = 0;
        image.entry = 0;
        image.sections = sections_;
        image.relocations.insert(image.relocations.end(), image_relocations_.begin(),
                                 image_relocations_.end());
        for (auto& [name, label] : labels_) {
            if (globals_.find(name) != globals_.end()) {
                label.global = true;
            }
        }
        for (container::Section& section : image.sections) {
            if (section.type != container::SectionType::Bss) {
                section.size = static_cast<std::uint32_t>(section.data.size());
            }
        }

        for (const auto& [name, label] : labels_) {
            container::SymbolBind bind = container::SymbolBind::Local;
            if (label.global) {
                bind = weak_symbols_.find(name) != weak_symbols_.end()
                           ? container::SymbolBind::Weak
                           : container::SymbolBind::Global;
            }
            image.symbols.push_back({name, label.offset, 0,
                                     static_cast<std::uint16_t>(label.section), bind});
        }
        for (const std::string& name : externs_) {
            if (labels_.find(name) == labels_.end()) {
                image.symbols.push_back(
                    {name, 0, 0, container::kUndefinedSection, container::SymbolBind::Global});
            }
        }
        return true;
    }

    bool pass2_directive(const Statement& statement) {
        const std::string& name = statement.mnemonic;
        if (name == ".text" || name == ".rodata" || name == ".cpp_except" ||
            name == ".data" || name == ".bss" || name == ".debug_info") {
            return switch_section(name, statement.line);
        }
        if (name == ".global" || name == ".globl" || name == ".weak" ||
            name == ".extern" || name == ".equ" || name == ".set") {
            return true;
        }
        if (name == ".word") {
            for (const std::string& operand : statement.operands) {
                Expr value;
                if (!eval_expr(operand, constants_, value, error_, statement.line)) {
                    return false;
                }
                const std::uint32_t relocation_offset = offset();
                emit32(value.symbolic ? 0U : static_cast<std::uint32_t>(value.value));
                if (value.symbolic) {
                    image_relocations_.push_back(
                        {static_cast<std::uint32_t>(current_), relocation_offset, value.symbol,
                         static_cast<std::uint32_t>(value.addend),
                         container::RelocationType::Abs32});
                }
            }
            return true;
        }
        if (name == ".float") {
            for (const std::string& operand : statement.operands) {
                double value = 0.0;
                if (!parse_fp_literal(operand, value, error_, statement.line)) {
                    return false;
                }
                emit32(std::bit_cast<std::uint32_t>(static_cast<float>(value)));
            }
            return true;
        }
        if (name == ".double") {
            for (const std::string& operand : statement.operands) {
                double value = 0.0;
                if (!parse_fp_literal(operand, value, error_, statement.line)) {
                    return false;
                }
                const std::uint64_t bits = std::bit_cast<std::uint64_t>(value);
                emit32(static_cast<std::uint32_t>(bits & 0xFFFFFFFFU));
                emit32(static_cast<std::uint32_t>(bits >> 32));
            }
            return true;
        }
        if (name == ".byte") {
            for (const std::string& operand : statement.operands) {
                Expr value;
                if (!eval_expr(operand, constants_, value, error_, statement.line) ||
                    value.symbolic) {
                    return fail(statement.line, "byte relocation is not supported");
                }
                emit8(static_cast<std::uint8_t>(value.value & 0xFF));
            }
            return true;
        }
        if (name == ".space" || name == ".zero") {
            Expr value;
            if (!eval_expr(statement.operands[0], constants_, value, error_, statement.line)) {
                return false;
            }
            return advance(static_cast<std::uint32_t>(value.value), statement.line);
        }
        if (name == ".align" || name == ".balign") {
            Expr value;
            if (!eval_expr(statement.operands[0], constants_, value, error_, statement.line)) {
                return false;
            }
            return align_section(static_cast<std::uint32_t>(value.value), statement.line);
        }
        if (name == ".asciz" || name == ".ascii" || name == ".string") {
            for (const std::string& operand : statement.operands) {
                std::vector<std::uint8_t> bytes;
                if (!parse_string(operand, bytes, error_, statement.line)) {
                    return false;
                }
                for (std::uint8_t byte : bytes) {
                    emit8(byte);
                }
                if (name != ".ascii") {
                    emit8(0);
                }
            }
            return true;
        }
        if (name == ".org") {
            Expr value;
            if (!eval_expr(statement.operands[0], constants_, value, error_, statement.line)) {
                return false;
            }
            return advance(static_cast<std::uint32_t>(value.value) - offset(), statement.line);
        }
        return fail(statement.line, "unsupported directive: " + name);
    }

    Error& error_;
    std::vector<Statement> statements_;
    std::vector<container::Section> sections_;
    std::size_t current_ = 0;
    std::map<std::string, std::int64_t> constants_;
    std::map<std::string, Label> labels_;
    std::set<std::string> globals_;
    std::set<std::string> weak_symbols_;
    std::set<std::string> externs_;
    std::vector<container::Relocation> image_relocations_;
};

} // namespace

bool assemble_object(std::string_view source,
                     container::Image& image,
                     Error& error) {
    error = {};
    image = {};
    ObjectAssembler assembler(error);
    if (!assembler.run(source, image)) {
        return false;
    }
    return true;
}

} // namespace kr32::assembler
