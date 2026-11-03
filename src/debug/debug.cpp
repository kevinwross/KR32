#include "kr32/debug/debug.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <map>
#include <sstream>
#include <string>
#include <vector>

#include "kr32/isa/encoding.h"

namespace kr32::debug {
namespace {

std::string trim(std::string value) {
    const std::size_t first = value.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) {
        return {};
    }
    const std::size_t last = value.find_last_not_of(" \t\r\n");
    return value.substr(first, last - first + 1);
}

bool resolve_symbol(const std::map<std::string, std::uint32_t>& symbols,
                    const std::string& name,
                    std::uint32_t& value) {
    const auto found = symbols.find(name);
    if (found == symbols.end()) {
        return false;
    }
    value = found->second;
    return true;
}

} // namespace

bool parse_info(const container::Image& image, Info& info, std::string& error) {
    info = {};
    std::map<std::string, std::uint32_t> symbols;
    for (const container::Symbol& symbol : image.symbols) {
        symbols[symbol.name] = symbol.value;
    }

    const container::Section* debug_section = nullptr;
    for (const container::Section& section : image.sections) {
        if (section.type == container::SectionType::Debug || section.name == ".debug_info") {
            debug_section = &section;
            break;
        }
    }
    if (debug_section == nullptr || debug_section->data.empty()) {
        error = "image has no .debug_info section";
        return false;
    }

    const std::string text(debug_section->data.begin(), debug_section->data.end());
    std::size_t position = 0;
    while (position < text.size()) {
        const std::size_t end = text.find('\0', position);
        const std::string record =
            text.substr(position, end == std::string::npos ? std::string::npos : end - position);
        position = end == std::string::npos ? text.size() : end + 1;
        const std::string trimmed = trim(record);
        if (trimmed.empty()) {
            continue;
        }
        std::istringstream stream(trimmed);
        char kind = 0;
        stream >> kind;
        if (kind == 'S') {
            Source source;
            stream >> source.id;
            std::string path;
            std::getline(stream, path);
            source.path = trim(path);
            info.sources.push_back(std::move(source));
        } else if (kind == 'L') {
            std::string label;
            Line line;
            stream >> label >> line.source >> line.line >> line.column;
            if (!resolve_symbol(symbols, label, line.address)) {
                error = "debug line references unknown symbol: " + label;
                return false;
            }
            info.lines.push_back(line);
        } else if (kind == 'F') {
            Function function;
            std::string start_label;
            std::string end_label;
            stream >> function.name >> start_label >> end_label >> function.frame >>
                function.result_type;
            if (!resolve_symbol(symbols, start_label, function.start) ||
                !resolve_symbol(symbols, end_label, function.end)) {
                error = "debug function references unknown label: " + function.name;
                return false;
            }
            info.functions.push_back(std::move(function));
        } else if (kind == 'V') {
            Variable variable;
            stream >> variable.function >> variable.name >> variable.offset >> variable.type >>
                variable.size;
            info.variables.push_back(std::move(variable));
        } else if (kind == 'G') {
            Global global;
            std::string label;
            stream >> global.name >> label >> global.type >> global.size;
            if (!resolve_symbol(symbols, label, global.address)) {
                error = "debug global references unknown symbol: " + label;
                return false;
            }
            info.globals.push_back(std::move(global));
        } else if (kind == 'T') {
            TypeInfo type;
            stream >> type.name >> type.size;
            info.types.push_back(std::move(type));
        } else if (kind == 'C') {
            ClassInfo klass;
            stream >> klass.name >> klass.size >> klass.base;
            info.classes.push_back(std::move(klass));
        } else if (kind == 'M') {
            std::string class_name;
            Member member;
            stream >> class_name >> member.name >> member.offset >> member.type >>
                member.size;
            for (ClassInfo& klass : info.classes) {
                if (klass.name == class_name) {
                    klass.members.push_back(std::move(member));
                    break;
                }
            }
        }
    }

    std::sort(info.lines.begin(), info.lines.end(), [](const Line& left, const Line& right) {
        return left.address < right.address;
    });
    std::sort(info.functions.begin(), info.functions.end(),
              [](const Function& left, const Function& right) {
                  return left.start < right.start;
              });
    return true;
}

Session::Session() : machine_(core::Machine::Options{core::kDefaultRamSize}) {}

bool Session::load(const container::Image& image, std::string& error) {
    for (const container::Section& section : image.sections) {
        if ((section.flags & container::Alloc) == 0 ||
            section.type == container::SectionType::Bss) {
            continue;
        }
        if (!machine_.load_image(section.address, section.data)) {
            error = "failed to load section " + section.name;
            return false;
        }
    }
    machine_.reset(image.entry);
    machine_.set_syscall_handler([this](core::Machine& current, std::uint32_t vector) {
        if (vector == 33) {
            console_.push_back(static_cast<char>(current.reg(0) & 0xFFU));
            return true;
        }
        if (vector == 32) {
            current.halt(current.reg(0));
            return true;
        }
        return false;
    });
    console_.clear();
    if (!parse_info(image, info_, error)) {
        return false;
    }
    breakpoints_.clear();
    return true;
}

void Session::set_threads(std::vector<ThreadInfo> threads) {
    threads_ = std::move(threads);
    if (threads_.empty()) {
        selected_thread_ = 0;
        return;
    }
    selected_thread_ = threads_.front().id;
    for (const ThreadInfo& thread : threads_) {
        if (thread.current) {
            selected_thread_ = thread.id;
        }
    }
}

void Session::set_thread_provider(
    std::function<std::vector<ThreadInfo>()> provider) {
    thread_provider_ = std::move(provider);
}

void Session::refresh_threads() {
    if (thread_provider_) {
        set_threads(thread_provider_());
    }
}

void Session::select_thread(std::uint32_t id) {
    for (const ThreadInfo& thread : threads_) {
        if (thread.id == id) {
            selected_thread_ = id;
            return;
        }
    }
}

void Session::set_breakpoint(std::uint32_t address) {
    breakpoints_.insert(address);
}

void Session::clear_breakpoint(std::uint32_t address) {
    breakpoints_.erase(address);
}

void Session::clear_breakpoints() {
    breakpoints_.clear();
}

bool Session::at_breakpoint() const {
    return breakpoints_.find(machine_.pc()) != breakpoints_.end();
}

core::StepResult Session::step() {
    return machine_.step();
}

core::StepResult Session::step_line_in() {
    const Line* start = line_at(machine_.pc());
    if (start == nullptr) {
        return machine_.step();
    }
    core::StepResult result;
    for (int guard = 0; guard < 1'000'000; ++guard) {
        result = machine_.step();
        if (result.kind != core::StepResult::Kind::Retired) {
            return result;
        }
        const Line* current = line_at(machine_.pc());
        if (current != nullptr &&
            (current->line != start->line || current->source != start->source)) {
            return result;
        }
    }
    return result;
}

core::StepResult Session::step_line_over() {
    const Line* start = line_at(machine_.pc());
    if (start == nullptr) {
        return machine_.step();
    }
    std::uint32_t target = 0;
    bool found = false;
    const Function* function = function_at(machine_.pc());
    for (const Line& line : info_.lines) {
        if (line.address <= machine_.pc()) continue;
        if (function != nullptr && function_at(line.address) != function) continue;
        if (line.line == start->line && line.source == start->source) continue;
        target = line.address;
        found = true;
        break;
    }
    if (!found) {
        return machine_.step();
    }
    const bool existing = breakpoints_.find(target) != breakpoints_.end();
    if (!existing) {
        set_breakpoint(target);
    }
    core::StepResult result = run(200000);
    if (!existing) {
        clear_breakpoint(target);
    }
    return result;
}

core::StepResult Session::step_line_out() {
    if (function_at(machine_.pc()) == nullptr) {
        return machine_.step();
    }
    const core::StepResult result = step_out();
    if (result.kind != core::StepResult::Kind::Retired) {
        return result;
    }
    if (function_at(machine_.pc()) == nullptr) {
        return result;
    }
    return step_line_in();
}

core::StepResult Session::run(std::uint64_t max_steps) {
    core::StepResult result;
    const std::uint32_t initial_pc = machine_.pc();
    bool skipped_initial = breakpoints_.find(initial_pc) == breakpoints_.end();
    for (std::uint64_t step_index = 0; step_index < max_steps; ++step_index) {
        if (at_breakpoint()) {
            if (!skipped_initial) {
                skipped_initial = true;
                result = machine_.step();
                if (result.kind == core::StepResult::Kind::Halted ||
                    result.kind == core::StepResult::Kind::Raised) {
                    return result;
                }
                continue;
            }
            return result;
        }
        result = machine_.step();
        if (result.kind == core::StepResult::Kind::Halted ||
            result.kind == core::StepResult::Kind::Raised) {
            return result;
        }
    }
    return result;
}

core::StepResult Session::step_over() {
    const std::uint32_t pc = machine_.pc();
    std::uint32_t words[2] = {0, 0};
    if (!machine_.read32(pc, words[0])) {
        return machine_.step();
    }
    if (!machine_.read32(pc + 4, words[1])) {
        words[1] = 0;
    }
    isa::Instruction instruction;
    if (!isa::decode(words, 2, instruction) || instruction.opcode != 0x3AU) {
        return machine_.step();
    }
    const std::uint32_t return_pc = pc + (instruction.extension ? 8U : 4U);
    core::StepResult result = machine_.step();
    if (result.kind != core::StepResult::Kind::Retired) {
        return result;
    }
    for (int guard = 0; guard < 1'000'000 && machine_.pc() != return_pc; ++guard) {
        result = machine_.step();
        if (result.kind == core::StepResult::Kind::Halted ||
            result.kind == core::StepResult::Kind::Raised) {
            return result;
        }
    }
    return result;
}

core::StepResult Session::step_out() {
    const Function* function = function_at(machine_.pc());
    if (function == nullptr) {
        return machine_.step();
    }
    const std::uint32_t return_pc = return_address();
    if (return_pc == 0) {
        return machine_.step();
    }
    core::StepResult result;
    for (int guard = 0; guard < 1'000'000 && machine_.pc() != return_pc; ++guard) {
        result = machine_.step();
        if (result.kind == core::StepResult::Kind::Halted ||
            result.kind == core::StepResult::Kind::Raised) {
            return result;
        }
    }
    return result;
}

const Line* Session::line_at(std::uint32_t pc) const {
    const Function* function = function_at(pc);
    if (function == nullptr) {
        return nullptr;
    }
    const auto begin = std::lower_bound(
        info_.lines.begin(), info_.lines.end(), function->start,
        [](const Line& line, std::uint32_t address) { return line.address < address; });
    const auto end = std::lower_bound(
        begin, info_.lines.end(), function->end,
        [](const Line& line, std::uint32_t address) { return line.address < address; });
    const auto next = std::upper_bound(
        begin, end, pc,
        [](std::uint32_t address, const Line& line) { return address < line.address; });
    if (next == begin) {
        return nullptr;
    }
    return &*(next - 1);
}

const Function* Session::function_at(std::uint32_t pc) const {
    for (const Function& function : info_.functions) {
        if (pc >= function.start && pc < function.end) {
            return &function;
        }
    }
    return nullptr;
}

std::vector<Frame> Session::call_stack() const {
    std::vector<Frame> frames;
    std::uint32_t pc = machine_.pc();
    std::uint32_t frame_pointer = machine_.reg(7);
    for (int depth = 0; depth < 64; ++depth) {
        const Function* function = function_at(pc);
        if (function == nullptr) {
            break;
        }
        frames.push_back(Frame{function->name, pc, frame_pointer});
        std::uint32_t saved_frame = 0;
        std::uint32_t return_pc = 0;
        if (!machine_.read32(frame_pointer + function->frame, saved_frame) ||
            !machine_.read32(frame_pointer + function->frame + 4, return_pc)) {
            break;
        }
        if (return_pc == 0) {
            break;
        }
        pc = return_pc;
        frame_pointer = saved_frame;
    }
    return frames;
}

bool Session::read_value(std::uint32_t address,
                         std::uint32_t size,
                         std::uint32_t& value) const {
    if (size <= 1) {
        std::uint8_t byte = 0;
        if (!machine_.read8(address, byte)) {
            return false;
        }
        value = byte;
        return true;
    }
    return machine_.read32(address, value);
}

bool Session::read_variable(const std::string& name,
                            std::uint32_t& value,
                            std::string& error) const {
    const Function* function = function_at(machine_.pc());
    if (function == nullptr) {
        error = "no current function";
        return false;
    }
    for (const Variable& variable : info_.variables) {
        if (variable.function == function->name && variable.name == name) {
            const std::uint32_t address = machine_.reg(7) + variable.offset;
            if (!read_value(address, variable.size, value)) {
                error = "cannot read variable " + name;
                return false;
            }
            return true;
        }
    }
    error = "unknown local variable: " + name;
    return false;
}

bool Session::read_global(const std::string& name,
                          std::uint32_t& value,
                          std::string& error) const {
    for (const Global& global : info_.globals) {
        if (global.name == name) {
            if (!read_value(global.address, global.size, value)) {
                error = "cannot read global " + name;
                return false;
            }
            return true;
        }
    }
    error = "unknown global: " + name;
    return false;
}

bool Session::read_memory(std::uint32_t address,
                          std::uint32_t size,
                          std::uint32_t& value) const {
    return read_value(address, size, value);
}

std::uint32_t Session::return_address() const {
    const Function* function = function_at(machine_.pc());
    if (function == nullptr) {
        return 0;
    }
    std::uint32_t address = 0;
    if (!machine_.read32(machine_.reg(7) + function->frame + 4, address)) {
        return 0;
    }
    return address;
}

std::string Session::dump() const {
    std::ostringstream output;
    for (const Source& source : info_.sources) {
        output << "source " << source.id << " " << source.path << "\n";
    }
    for (const Function& function : info_.functions) {
        output << "function " << function.name << " 0x" << std::hex << function.start << "-0x"
               << function.end << std::dec << " frame=" << function.frame
               << " result=" << function.result_type << "\n";
    }
    for (const Global& global : info_.globals) {
        output << "global " << global.name << " 0x" << std::hex << global.address << std::dec
               << " type=" << global.type << " size=" << global.size << "\n";
    }
    for (const Variable& variable : info_.variables) {
        output << "variable " << variable.function << "." << variable.name
               << " offset=" << variable.offset << " type=" << variable.type
               << " size=" << variable.size << "\n";
    }
    for (const TypeInfo& type : info_.types) {
        output << "type " << type.name << " size=" << type.size << "\n";
    }
    for (const Line& line : info_.lines) {
        output << "line 0x" << std::hex << line.address << std::dec << " " << line.source << ":"
               << line.line << ":" << line.column << "\n";
    }
    return output.str();
}

} // namespace kr32::debug
