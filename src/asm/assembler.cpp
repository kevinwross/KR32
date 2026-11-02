#include "kr32/asm/assembler.h"

#include "kr32/isa/encoding.h"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstdlib>
#include <map>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace kr32::assembler {
namespace {

struct Expr {
    bool symbolic = false;
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
    int index = -1;
    bool indexed = false;
    bool pc_relative = false;
    bool uses_symbol = false;
    Expr displacement;
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

bool is_identifier_start(char c) {
    return std::isalpha(static_cast<unsigned char>(c)) != 0 || c == '_' || c == '.';
}

bool is_identifier_char(char c) {
    return std::isalnum(static_cast<unsigned char>(c)) != 0 || c == '_' || c == '.';
}

std::string strip_comment(std::string_view text) {
    std::string result;
    result.reserve(text.size());
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

bool parse_control_register(std::string_view text, int& reg) {
    const std::string value = trim(text);
    if (value.size() < 3 || (value[0] != 'c' && value[0] != 'C') ||
        (value[1] != 'r' && value[1] != 'R')) {
        return false;
    }
    std::size_t index = 2;
    while (index < value.size() && std::isdigit(static_cast<unsigned char>(value[index])) != 0) {
        ++index;
    }
    if (index != value.size() || index == 2) {
        return false;
    }
    const int number = std::atoi(value.c_str() + 2);
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

bool take_register(std::string_view text, int& reg, std::string& rest) {
    std::string value = trim(text);
    if (value.empty() || (value[0] != 'r' && value[0] != 'R')) {
        return false;
    }
    std::size_t end = 1;
    while (end < value.size() && std::isdigit(static_cast<unsigned char>(value[end])) != 0) {
        ++end;
    }
    if (end == 1) {
        return false;
    }
    if (end < value.size() && is_identifier_char(value[end])) {
        return false;
    }
    if (!parse_register(std::string_view(value).substr(0, end), reg)) {
        return false;
    }
    rest = trim(std::string_view(value).substr(end));
    return true;
}

bool has_identifier(std::string_view text) {
    std::size_t index = 0;
    while (index < text.size()) {
        const char c = text[index];
        if (std::isdigit(static_cast<unsigned char>(c)) != 0) {
            ++index;
            while (index < text.size() && std::isalnum(static_cast<unsigned char>(text[index])) != 0) {
                ++index;
            }
            continue;
        }
        if (is_identifier_start(c)) {
            const std::size_t begin = index;
            ++index;
            while (index < text.size() && is_identifier_char(text[index])) {
                ++index;
            }
            const std::string token = lower(text.substr(begin, index - begin));
            if (token.size() >= 2 && token[0] == 'r' &&
                std::all_of(token.begin() + 1, token.end(), [](char ch) {
                    return std::isdigit(static_cast<unsigned char>(ch)) != 0;
                })) {
                continue;
            }
            if (token == "sp" || token == "pc") {
                continue;
            }
            return true;
        }
        ++index;
    }
    return false;
}

bool eval_expr(std::string_view raw,
               const std::map<std::string, std::uint32_t>& symbols,
               bool allow_symbols,
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
    std::int64_t value = 0;
    bool symbolic = false;
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
            value += static_cast<std::int64_t>(sign) * parsed;
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
            const auto found = symbols.find(name);
            if (found != symbols.end()) {
                value += static_cast<std::int64_t>(sign) * static_cast<std::int64_t>(found->second);
            } else if (allow_symbols) {
                symbolic = true;
            } else {
                error = {"undefined symbol: " + name, line};
                return false;
            }
            sign = 1;
            continue;
        }

        error = {"unexpected character in expression: " + std::string(1, text[index]), line};
        return false;
    }

    result.symbolic = symbolic;
    result.value = value;
    return true;
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
            error = {"unterminated escape in string", line};
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

bool take_plus_minus(std::string_view text, int& sign, std::string& rest) {
    std::string value = trim(text);
    if (value.empty()) {
        return false;
    }
    if (value.front() == '+') {
        sign = 1;
        rest = trim(std::string_view(value).substr(1));
        return true;
    }
    if (value.front() == '-') {
        sign = -1;
        rest = trim(std::string_view(value).substr(1));
        return true;
    }
    return false;
}

bool parse_memory(std::string_view raw,
                  const std::map<std::string, std::uint32_t>& symbols,
                  bool allow_symbols,
                  std::uint32_t address,
                  MemoryOperand& memory,
                  Error& error,
                  int line) {
    (void)address;
    memory = {};
    std::string text = trim(raw);
    if (text.size() < 2 || text.front() != '[' || text.back() != ']') {
        error = {"expected a memory operand in brackets", line};
        return false;
    }
    text = trim(std::string_view(text).substr(1, text.size() - 2));
    const std::string lowered = lower(text);

    auto starts_with_word = [&](std::string_view word) {
        if (lowered.size() < word.size()) {
            return false;
        }
        if (lowered.compare(0, word.size(), word.data(), word.size()) != 0) {
            return false;
        }
        return lowered.size() == word.size() || !is_identifier_char(lowered[word.size()]);
    };

    if (starts_with_word("pc")) {
        memory.mode = 6;
        memory.pc_relative = true;
        std::string rest = trim(std::string_view(text).substr(2));
        if (rest.empty()) {
            memory.displacement = {};
            memory.uses_symbol = false;
            return true;
        }
        int sign = 1;
        std::string after_sign;
        if (take_plus_minus(rest, sign, after_sign)) {
            rest = after_sign;
            if (sign < 0) {
                rest = "-" + rest;
            }
        }
        if (!eval_expr(rest, symbols, allow_symbols, memory.displacement, error, line)) {
            return false;
        }
        memory.uses_symbol = has_identifier(rest);
        return true;
    }

    if (starts_with_word("sp")) {
        memory.mode = 4;
        std::string rest = trim(std::string_view(text).substr(2));
        if (rest.empty()) {
            memory.displacement = {};
            memory.uses_symbol = false;
            return true;
        }
        int sign = 1;
        std::string after_sign;
        if (take_plus_minus(rest, sign, after_sign)) {
            rest = after_sign;
            if (sign < 0) {
                rest = "-" + rest;
            }
        }
        if (!eval_expr(rest, symbols, allow_symbols, memory.displacement, error, line)) {
            return false;
        }
        memory.uses_symbol = has_identifier(rest);
        return true;
    }

    int first_reg = 0;
    std::string rest;
    if (take_register(text, first_reg, rest)) {
        memory.base = first_reg;
        if (rest.empty()) {
            memory.mode = 2;
            memory.displacement = {};
            memory.uses_symbol = false;
            return true;
        }

        int sign = 1;
        std::string after_sign;
        if (!take_plus_minus(rest, sign, after_sign)) {
            error = {"expected + or - in memory operand", line};
            return false;
        }

        int second_reg = 0;
        std::string after_second;
        if (take_register(after_sign, second_reg, after_second)) {
            memory.index = second_reg;
            memory.indexed = true;
            memory.mode = 3;
            if (trim(after_second).empty()) {
                memory.displacement = {};
                memory.uses_symbol = false;
                return true;
            }
            int disp_sign = 1;
            std::string disp_text;
            if (!take_plus_minus(after_second, disp_sign, disp_text)) {
                error = {"expected + or - after the index register", line};
                return false;
            }
            if (disp_sign < 0) {
                disp_text = "-" + disp_text;
            }
            if (!eval_expr(disp_text, symbols, allow_symbols, memory.displacement, error, line)) {
                return false;
            }
            memory.uses_symbol = has_identifier(disp_text);
            return true;
        }

        memory.mode = 3;
        std::string disp_text = after_sign;
        if (sign < 0) {
            disp_text = "-" + disp_text;
        }
        if (!eval_expr(disp_text, symbols, allow_symbols, memory.displacement, error, line)) {
            return false;
        }
        memory.uses_symbol = has_identifier(disp_text);
        return true;
    }

    memory.mode = 5;
    if (!eval_expr(text, symbols, allow_symbols, memory.displacement, error, line)) {
        return false;
    }
    memory.uses_symbol = has_identifier(text);
    return true;
}

class Assembler {
public:
    Assembler(const Options& options, Error& error)
        : options_(options), error_(error) {}

    bool run(std::string_view source, Result& result) {
        if (!parse_source(source)) {
            return false;
        }
        if (!pass1()) {
            return false;
        }
        if (!pass2(result)) {
            return false;
        }
        return true;
    }

private:
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
                (end == std::string::npos)
                    ? std::string_view(text).substr(begin)
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

    bool define_symbol(const std::string& name, std::uint32_t value, int line) {
        const auto inserted = symbols_.emplace(name, value);
        if (!inserted.second) {
            return fail(line, "duplicate symbol: " + name);
        }
        return true;
    }

    bool define_label(const std::string& name, std::uint32_t value, int line) {
        if (!define_symbol(name, value, line)) {
            return false;
        }
        labels_.push_back({name, value});
        return true;
    }

    bool pass1() {
        address_ = options_.base;
        for (const Statement& statement : statements_) {
            if (!statement.label.empty()) {
                if (!define_label(statement.label, address_, statement.line)) {
                    return false;
                }
            }
            if (statement.mnemonic.empty()) {
                continue;
            }
            if (statement.mnemonic.front() == '.') {
                if (!pass1_directive(statement)) {
                    return false;
                }
            } else {
                const std::uint32_t size = instruction_size(statement);
                if (size == 0) {
                    return false;
                }
                if (!advance(size, statement.line)) {
                    return false;
                }
            }
        }
        return true;
    }

    bool advance(std::uint32_t amount, int line) {
        if (amount > 0xFFFF'FFFFU - address_) {
            return fail(line, "address overflow");
        }
        address_ += amount;
        return true;
    }

    bool pass1_directive(const Statement& statement) {
        const std::string& name = statement.mnemonic;
        if (name == ".text" || name == ".data" || name == ".global" ||
            name == ".extern" || name == ".file" || name == ".type" ||
            name == ".size") {
            return true;
        }
        if (name == ".equ" || name == ".set") {
            if (statement.operands.size() != 2) {
                return fail(statement.line, name + " expects a name and a value");
            }
            Expr value;
            if (!eval_expr(statement.operands[1], symbols_, false, value, error_, statement.line)) {
                return false;
            }
            if (value.symbolic) {
                return fail(statement.line, "symbolic .equ value");
            }
            return define_symbol(statement.operands[0], static_cast<std::uint32_t>(value.value),
                                 statement.line);
        }
        if (name == ".org") {
            if (statement.operands.size() != 1) {
                return fail(statement.line, ".org expects one address");
            }
            Expr value;
            if (!eval_expr(statement.operands[0], symbols_, false, value, error_, statement.line)) {
                return false;
            }
            if (value.value < static_cast<std::int64_t>(options_.base) ||
                value.value < static_cast<std::int64_t>(address_) ||
                value.value > 0xFFFF'FFFFLL) {
                return fail(statement.line, ".org must move forward within the image");
            }
            address_ = static_cast<std::uint32_t>(value.value);
            return true;
        }
        if (name == ".align" || name == ".balign" || name == ".p2align") {
            if (statement.operands.size() != 1) {
                return fail(statement.line, name + " expects one alignment");
            }
            Expr value;
            if (!eval_expr(statement.operands[0], symbols_, false, value, error_, statement.line)) {
                return false;
            }
            if (value.value <= 0 || value.value > 0x1000'0000LL) {
                return fail(statement.line, "invalid alignment");
            }
            const std::uint32_t alignment = static_cast<std::uint32_t>(value.value);
            const std::uint32_t pad = (alignment - (address_ % alignment)) % alignment;
            return advance(pad, statement.line);
        }
        if (name == ".space" || name == ".zero") {
            if (statement.operands.size() != 1) {
                return fail(statement.line, name + " expects one size");
            }
            Expr value;
            if (!eval_expr(statement.operands[0], symbols_, false, value, error_, statement.line)) {
                return false;
            }
            if (value.value < 0 || value.value > 0xFFFF'FFFFLL) {
                return fail(statement.line, "invalid .space size");
            }
            return advance(static_cast<std::uint32_t>(value.value), statement.line);
        }
        if (name == ".byte") {
            return advance(static_cast<std::uint32_t>(statement.operands.size()), statement.line);
        }
        if (name == ".word") {
            return advance(static_cast<std::uint32_t>(statement.operands.size() * 4U), statement.line);
        }
        if (name == ".asciz" || name == ".ascii") {
            std::uint32_t total = 0;
            for (const std::string& operand : statement.operands) {
                std::vector<std::uint8_t> bytes;
                if (!parse_string(operand, bytes, error_, statement.line)) {
                    return false;
                }
                total += static_cast<std::uint32_t>(bytes.size() + (name == ".asciz" ? 1U : 0U));
            }
            return advance(total, statement.line);
        }
        return fail(statement.line, "unsupported directive: " + name);
    }

    std::uint32_t instruction_size(const Statement& statement) {
        isa::Instruction instruction;
        if (!build_instruction(statement, address_, true, instruction)) {
            return 0;
        }
        return instruction.extension ? 8U : 4U;
    }

    bool set_payload(const Expr& expression,
                     bool force_extension,
                     isa::Instruction& instruction,
                     int line) {
        if (force_extension) {
            instruction.extension = true;
            instruction.extension_word = static_cast<std::uint32_t>(expression.value);
            return true;
        }
        if (expression.value < -1024 || expression.value > 1023) {
            return fail(line, "immediate does not fit in 11 bits");
        }
        instruction.immediate = static_cast<std::int16_t>(expression.value);
        return true;
    }

    bool needs_extension(const Expr& expression, std::string_view text) {
        return expression.symbolic || has_identifier(text) ||
               expression.value < -1024 || expression.value > 1023;
    }

    bool build_instruction(const Statement& statement,
                           std::uint32_t address,
                           bool allow_symbols,
                           isa::Instruction& instruction) {
        instruction = {};
        const std::string& mnemonic = statement.mnemonic;
        const std::vector<std::string>& operands = statement.operands;

        auto fail_operands = [&](std::string_view expected) {
            return fail(statement.line, std::string(mnemonic) + " expects " + std::string(expected));
        };
        auto parse_immediate = [&](const std::string& text, Expr& expression) {
            std::string value = trim(text);
            if (!value.empty() && value.front() == '#') {
                value = trim(std::string_view(value).substr(1));
            }
            return eval_expr(value, symbols_, allow_symbols, expression, error_, statement.line);
        };
        auto parse_memory_operand = [&](const std::string& text, MemoryOperand& memory) {
            return parse_memory(text, symbols_, allow_symbols, address, memory, error_, statement.line);
        };
        auto set_destination = [&](int reg) {
            instruction.rd = static_cast<std::uint8_t>(reg);
        };
        auto set_source = [&](int reg) {
            instruction.rs1 = static_cast<std::uint8_t>(reg);
        };

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
                instruction.literal = false;
                return true;
            }
            Expr value;
            if (!parse_immediate(operands[1], value)) {
                return false;
            }
            instruction.literal = true;
            return set_payload(value, needs_extension(value, operands[1]), instruction,
                               statement.line);
        }

        if (mnemonic == "load" || mnemonic == "load8u" || mnemonic == "load8s" ||
            mnemonic == "load16u" || mnemonic == "load16s" || mnemonic == "lea") {
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
            if (mnemonic == "lea" && memory.indexed) {
                return fail(statement.line, "LEA does not support a base+index operand");
            }
            set_destination(destination);
            if (mnemonic == "load") instruction.opcode = memory.indexed ? 0x10 : 0x01;
            if (mnemonic == "load8u") instruction.opcode = memory.indexed ? 0x12 : 0x03;
            if (mnemonic == "load8s") instruction.opcode = memory.indexed ? 0x13 : 0x04;
            if (mnemonic == "load16u") instruction.opcode = memory.indexed ? 0x14 : 0x05;
            if (mnemonic == "load16s") instruction.opcode = memory.indexed ? 0x15 : 0x06;
            if (mnemonic == "lea") instruction.opcode = 0x09;
            if (memory.base >= 0) {
                set_source(memory.base);
            }
            instruction.field = static_cast<std::uint8_t>(memory.indexed ? memory.index : memory.mode);
            instruction.literal = true;
            Expr displacement = memory.displacement;
            if (memory.pc_relative && memory.uses_symbol) {
                displacement.value -= static_cast<std::int64_t>(address);
            }
            return set_payload(displacement,
                               memory.uses_symbol || displacement.value < -1024 ||
                                   displacement.value > 1023,
                               instruction, statement.line);
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
            if (mnemonic == "store") instruction.opcode = memory.indexed ? 0x11 : 0x02;
            if (mnemonic == "store8") instruction.opcode = memory.indexed ? 0x16 : 0x07;
            if (mnemonic == "store16") instruction.opcode = memory.indexed ? 0x17 : 0x08;
            if (memory.base >= 0) {
                set_source(memory.base);
            }
            instruction.field = static_cast<std::uint8_t>(memory.indexed ? memory.index : memory.mode);
            instruction.literal = true;
            Expr displacement = memory.displacement;
            if (memory.pc_relative && memory.uses_symbol) {
                displacement.value -= static_cast<std::int64_t>(address);
            }
            return set_payload(displacement,
                               memory.uses_symbol || displacement.value < -1024 ||
                                   displacement.value > 1023,
                               instruction, statement.line);
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
            if (memory.indexed) {
                return fail(statement.line, "LL/SC do not support a base+index operand");
            }
            set_destination(source);
            if (memory.base >= 0) {
                set_source(memory.base);
            }
            instruction.opcode = (mnemonic == "ll") ? 0x46 : 0x47;
            instruction.field = static_cast<std::uint8_t>(memory.mode);
            instruction.literal = true;
            Expr displacement = memory.displacement;
            if (memory.pc_relative && memory.uses_symbol) {
                displacement.value -= static_cast<std::int64_t>(address);
            }
            return set_payload(displacement,
                               memory.uses_symbol || displacement.value < -1024 ||
                                   displacement.value > 1023,
                               instruction, statement.line);
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
            int second = 0;
            if (parse_register(operands[1], second)) {
                instruction.field = static_cast<std::uint8_t>(second);
                instruction.literal = false;
                return true;
            }
            Expr value;
            if (!parse_immediate(operands[1], value)) {
                return false;
            }
            instruction.literal = true;
            return set_payload(value, needs_extension(value, operands[1]), instruction,
                               statement.line);
        }

        if (mnemonic == "not" || mnemonic == "neg") {
            if (operands.size() != 2) {
                return fail_operands("Rd, Rs");
            }
            int destination = 0;
            int source = 0;
            if (!parse_register(operands[0], destination) ||
                !parse_register(operands[1], source)) {
                return fail(statement.line, "expected two registers");
            }
            set_destination(destination);
            set_source(source);
            instruction.opcode = (mnemonic == "not") ? 0x2D : 0x2E;
            instruction.literal = false;
            return true;
        }

        if (mnemonic == "slt" || mnemonic == "sltu" || mnemonic == "add" ||
            mnemonic == "sub" || mnemonic == "mul" || mnemonic == "mulh" ||
            mnemonic == "mulhu" || mnemonic == "div" || mnemonic == "divs" ||
            mnemonic == "mod" || mnemonic == "mods" || mnemonic == "and" ||
            mnemonic == "or" || mnemonic == "xor" || mnemonic == "shl" ||
            mnemonic == "shr" || mnemonic == "sar" || mnemonic == "rol" ||
            mnemonic == "ror") {
            if (operands.size() != 3) {
                return fail_operands("Rd, Rs1, Rs2 or Rd, Rs1, #imm");
            }
            int destination = 0;
            int source = 0;
            if (!parse_register(operands[0], destination) ||
                !parse_register(operands[1], source)) {
                return fail(statement.line, "expected Rd and Rs1 registers");
            }
            set_destination(destination);
            set_source(source);

            static const std::map<std::string, std::uint32_t> opcodes = {
                {"add", 0x20}, {"sub", 0x21}, {"mul", 0x22}, {"mulh", 0x23},
                {"mulhu", 0x24}, {"div", 0x25}, {"divs", 0x26}, {"mod", 0x27},
                {"mods", 0x28}, {"and", 0x2A}, {"or", 0x2B}, {"xor", 0x2C},
                {"slt", 0x18}, {"sltu", 0x19}, {"shl", 0x30}, {"shr", 0x31},
                {"sar", 0x32}, {"rol", 0x33}, {"ror", 0x34},
            };
            instruction.opcode = static_cast<std::uint8_t>(opcodes.at(mnemonic));

            int second = 0;
            if (parse_register(operands[2], second)) {
                instruction.field = static_cast<std::uint8_t>(second);
                instruction.literal = false;
                return true;
            }
            Expr value;
            if (!parse_immediate(operands[2], value)) {
                return false;
            }
            instruction.literal = true;
            return set_payload(value, needs_extension(value, operands[2]), instruction,
                               statement.line);
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
                if (compare) {
                    int destination = 0;
                    int left = 0;
                    int right = 0;
                    if (!parse_register(operands[0], destination) ||
                        !parse_fpr(operands[1], left) ||
                        !parse_fpr(operands[2], right)) {
                        return fail(statement.line, "expected Rd, Fs1, Fs2");
                    }
                    instruction.rd = static_cast<std::uint8_t>(destination);
                    instruction.rs1 = static_cast<std::uint8_t>(left);
                    instruction.field = static_cast<std::uint8_t>(right);
                } else {
                    int destination = 0;
                    int left = 0;
                    int right = 0;
                    if (!parse_fpr(operands[0], destination) ||
                        !parse_fpr(operands[1], left) ||
                        (operands.size() == 3 && !parse_fpr(operands[2], right))) {
                        return fail(statement.line, "expected Fd, Fs1, Fs2");
                    }
                    instruction.rd = static_cast<std::uint8_t>(destination);
                    instruction.rs1 = static_cast<std::uint8_t>(left);
                    instruction.field = static_cast<std::uint8_t>(right);
                }
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
                instruction.opcode = 0x60;
                instruction.immediate = static_cast<std::int16_t>(conversion->second);
                const std::uint32_t function = conversion->second;
                const bool destination_gpr =
                    function == 0x022 || function == 0x023 ||
                    function == 0x026 || function == 0x027 || function == 0x031;
                const bool source_gpr =
                    function == 0x024 || function == 0x025 ||
                    function == 0x028 || function == 0x029 || function == 0x030;
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
                instruction.rd = static_cast<std::uint8_t>(destination);
                instruction.rs1 = static_cast<std::uint8_t>(source);
                return true;
            }
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
            if (memory.indexed) {
                return fail(statement.line, "FLOAD/FSTORE do not support base+index");
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
                               memory.uses_symbol || memory.displacement.value < -1024 ||
                                   memory.displacement.value > 1023,
                               instruction, statement.line);
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
            if (memory.indexed) {
                return fail(statement.line, "MLOAD/MSTORE do not support base+index");
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
                if (!eval_expr(stride_text, symbols_, allow_symbols, stride, error_,
                               statement.line)) {
                    return false;
                }
                if (memory.displacement.value != 0 || memory.uses_symbol) {
                    return fail(statement.line,
                                "strided MLOAD/MSTORE requires a zero displacement");
                }
                instruction.extension = true;
                instruction.extension_word = static_cast<std::uint32_t>(stride.value);
                instruction.immediate = 0;
                return true;
            }
            return set_payload(memory.displacement,
                               memory.uses_symbol || memory.displacement.value < -1024 ||
                                   memory.displacement.value > 1023,
                               instruction, statement.line);
        }

        if (mnemonic == "jmp" || mnemonic == "call") {            if (operands.size() != 1) {
                return fail_operands("target");
            }
            instruction.opcode = (mnemonic == "jmp") ? 0x38 : 0x3A;
            int target = 0;
            if (parse_register(operands[0], target)) {
                set_source(target);
                instruction.literal = false;
                return true;
            }
            Expr value;
            if (!parse_immediate(operands[0], value)) {
                return false;
            }
            instruction.literal = true;
            return set_payload(value, true, instruction, statement.line);
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
                    instruction.literal = false;
                    return true;
                }
                Expr value;
                if (!parse_immediate(operands[0], value)) {
                    return false;
                }
                instruction.literal = true;
                return set_payload(value, true, instruction, statement.line);
            }
        }

        if (mnemonic == "rdcr" || mnemonic == "wrcr") {
            if (operands.size() != 2) {
                return fail_operands("Rd, CRn or CRn, Rs");
            }
            int control = 0;
            int reg = 0;
            if (mnemonic == "rdcr") {
                if (!parse_register(operands[0], reg) ||
                    !parse_control_register(operands[1], control)) {
                    return fail(statement.line, "expected Rd, CRn");
                }
                set_destination(reg);
                instruction.opcode = 0x49;
            } else {
                if (!parse_control_register(operands[0], control) ||
                    !parse_register(operands[1], reg)) {
                    return fail(statement.line, "expected CRn, Rs");
                }
                set_destination(reg);
                instruction.opcode = 0x4A;
            }
            instruction.field = static_cast<std::uint8_t>(control);
            return true;
        }

        if (mnemonic == "rdsp" || mnemonic == "wrsp") {
            if (operands.size() != 1) {
                return fail_operands("Rd");
            }
            int reg = 0;
            if (!parse_register(operands[0], reg)) {
                return fail(statement.line, "expected a register");
            }
            set_destination(reg);
            instruction.opcode = (mnemonic == "rdsp") ? 0x4B : 0x4C;
            return true;
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
            instruction.opcode = (mnemonic == "push") ? 0x3C : 0x3D;
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
            return set_payload(value, needs_extension(value, operands[0]), instruction,
                               statement.line);
        }

        if (mnemonic == "int" || mnemonic == "syscall") {
            if (operands.size() != 1) {
                return fail_operands("#vector");
            }
            Expr value;
            if (!parse_immediate(operands[0], value)) {
                return false;
            }
            instruction.opcode = (mnemonic == "int") ? 0x40 : 0x41;
            instruction.literal = true;
            return set_payload(value, needs_extension(value, operands[0]), instruction,
                               statement.line);
        }

        if (mnemonic == "nop" || mnemonic == "brk" || mnemonic == "halt" ||
            mnemonic == "ret" || mnemonic == "iret" || mnemonic == "fence") {
            if (!operands.empty()) {
                return fail_operands("no operands");
            }
            if (mnemonic == "nop") instruction.opcode = 0x45;
            if (mnemonic == "brk") instruction.opcode = 0x44;
            if (mnemonic == "halt") instruction.opcode = 0x43;
            if (mnemonic == "ret") instruction.opcode = 0x3B;
            if (mnemonic == "iret") instruction.opcode = 0x42;
            if (mnemonic == "fence") instruction.opcode = 0x48;
            return true;
        }

        return fail(statement.line, "unsupported instruction: " + mnemonic);
    }

    bool pass2(Result& result) {
        address_ = options_.base;
        bytes_.clear();
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
            if (!build_instruction(statement, address_, false, instruction)) {
                return false;
            }
            isa::Encoded encoded;
            if (!isa::encode(instruction, encoded)) {
                return fail(statement.line, "cannot encode instruction");
            }
            const std::uint32_t expected = instruction.extension ? 8U : 4U;
            if (encoded.count * 4U != expected) {
                return fail(statement.line, "internal encoding size mismatch");
            }
            for (std::uint8_t index = 0; index < encoded.count; ++index) {
                emit32(encoded.words[index]);
            }
        }
        result.base = options_.base;
        result.bytes = bytes_;
        result.symbols = labels_;
        std::sort(result.symbols.begin(), result.symbols.end(),
                  [](const Result::Symbol& left, const Result::Symbol& right) {
                      if (left.address != right.address) {
                          return left.address < right.address;
                      }
                      return left.name < right.name;
                  });
        return true;
    }

    bool pass2_directive(const Statement& statement) {
        const std::string& name = statement.mnemonic;
        if (name == ".text" || name == ".data" || name == ".global" ||
            name == ".extern" || name == ".file" || name == ".type" ||
            name == ".size" || name == ".equ" || name == ".set") {
            return true;
        }
        if (name == ".org") {
            Expr value;
            if (!eval_expr(statement.operands[0], symbols_, false, value, error_, statement.line)) {
                return false;
            }
            return pad_to(static_cast<std::uint32_t>(value.value), statement.line);
        }
        if (name == ".align" || name == ".balign" || name == ".p2align") {
            Expr value;
            if (!eval_expr(statement.operands[0], symbols_, false, value, error_, statement.line)) {
                return false;
            }
            const std::uint32_t alignment = static_cast<std::uint32_t>(value.value);
            const std::uint32_t pad = (alignment - (address_ % alignment)) % alignment;
            return pad_to(address_ + pad, statement.line);
        }
        if (name == ".space" || name == ".zero") {
            Expr value;
            if (!eval_expr(statement.operands[0], symbols_, false, value, error_, statement.line)) {
                return false;
            }
            return pad_to(address_ + static_cast<std::uint32_t>(value.value), statement.line);
        }
        if (name == ".byte") {
            for (const std::string& operand : statement.operands) {
                Expr value;
                if (!eval_expr(operand, symbols_, false, value, error_, statement.line)) {
                    return false;
                }
                emit8(static_cast<std::uint8_t>(value.value & 0xFF));
            }
            return true;
        }
        if (name == ".word") {
            for (const std::string& operand : statement.operands) {
                Expr value;
                if (!eval_expr(operand, symbols_, false, value, error_, statement.line)) {
                    return false;
                }
                emit32(static_cast<std::uint32_t>(value.value));
            }
            return true;
        }
        if (name == ".asciz" || name == ".ascii") {
            for (const std::string& operand : statement.operands) {
                std::vector<std::uint8_t> bytes;
                if (!parse_string(operand, bytes, error_, statement.line)) {
                    return false;
                }
                for (std::uint8_t byte : bytes) {
                    emit8(byte);
                }
                if (name == ".asciz") {
                    emit8(0);
                }
            }
            return true;
        }
        return fail(statement.line, "unsupported directive: " + name);
    }

    bool pad_to(std::uint32_t target, int line) {
        if (target < options_.base || target < address_) {
            return fail(line, "cannot move the image backwards");
        }
        const std::uint64_t needed = static_cast<std::uint64_t>(target) - options_.base;
        if (needed > 0xFFFF'FFFFULL) {
            return fail(line, "image too large");
        }
        if (bytes_.size() < needed) {
            bytes_.resize(static_cast<std::size_t>(needed), 0);
        }
        address_ = target;
        return true;
    }

    void emit8(std::uint8_t value) {
        const std::size_t offset = address_ - options_.base;
        if (bytes_.size() <= offset) {
            bytes_.resize(offset + 1, 0);
        }
        bytes_[offset] = value;
        ++address_;
    }

    void emit32(std::uint32_t value) {
        emit8(static_cast<std::uint8_t>(value & 0xFFU));
        emit8(static_cast<std::uint8_t>((value >> 8) & 0xFFU));
        emit8(static_cast<std::uint8_t>((value >> 16) & 0xFFU));
        emit8(static_cast<std::uint8_t>((value >> 24) & 0xFFU));
    }

    Options options_;
    Error& error_;
    std::vector<Statement> statements_;
    std::map<std::string, std::uint32_t> symbols_;
    std::vector<Result::Symbol> labels_;
    std::uint32_t address_ = 0;
    std::vector<std::uint8_t> bytes_;
};

} // namespace

bool assemble(std::string_view source,
              const Options& options,
              Result& result,
              Error& error) {
    error = {};
    result = {};
    Assembler assembler(options, error);
    return assembler.run(source, result);
}

} // namespace kr32::assembler
