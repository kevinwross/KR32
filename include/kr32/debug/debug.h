#pragma once

#include <cstdint>
#include <functional>
#include <set>
#include <string>
#include <vector>

#include "kr32/container/container.h"
#include "kr32/core/machine.h"

namespace kr32::debug {

struct Source {
    int id = 0;
    std::string path;
};

struct Line {
    std::uint32_t address = 0;
    int source = 0;
    int line = 0;
    int column = 0;
};

struct Function {
    std::string name;
    std::uint32_t start = 0;
    std::uint32_t end = 0;
    std::uint32_t frame = 0;
    std::string result_type;
};

struct Variable {
    std::string function;
    std::string name;
    std::uint32_t offset = 0;
    std::string type;
    std::uint32_t size = 0;
};

struct TypeInfo {
    std::string name;
    std::uint32_t size = 0;
};

struct Member {
    std::string name;
    std::uint32_t offset = 0;
    std::string type;
    std::uint32_t size = 0;
};

struct ClassInfo {
    std::string name;
    std::uint32_t size = 0;
    std::string base;
    std::vector<Member> members;
};

struct Global {
    std::string name;
    std::uint32_t address = 0;
    std::string type;
    std::uint32_t size = 0;
};

struct Info {
    std::vector<Source> sources;
    std::vector<Line> lines;
    std::vector<Function> functions;
    std::vector<Variable> variables;
    std::vector<Global> globals;
    std::vector<TypeInfo> types;
    std::vector<ClassInfo> classes;
};

struct Frame {
    std::string function;
    std::uint32_t pc = 0;
    std::uint32_t frame_pointer = 0;
};

struct ThreadInfo {
    std::uint32_t id = 0;
    std::string name;
    std::uint32_t pc = 0;
    std::uint32_t sp = 0;
    std::uint32_t frame_pointer = 0;
    bool current = false;
};

[[nodiscard]] bool parse_info(const container::Image& image, Info& info, std::string& error);

class Session {
public:
    Session();

    [[nodiscard]] bool load(const container::Image& image, std::string& error);
    [[nodiscard]] bool load_raw(const std::vector<std::uint8_t>& kernel,
                                std::uint32_t kernel_base,
                                const std::vector<std::uint8_t>& bios,
                                std::string& error);

    [[nodiscard]] core::Machine& machine() { return machine_; }
    [[nodiscard]] const core::Machine& machine() const { return machine_; }
    [[nodiscard]] std::size_t core_count() const { return 1; }
    [[nodiscard]] core::Machine& core(std::size_t index) {
        (void)index;
        return machine_;
    }
    [[nodiscard]] const core::Machine& core(std::size_t index) const {
        (void)index;
        return machine_;
    }
    [[nodiscard]] const Info& info() const { return info_; }

    void set_threads(std::vector<ThreadInfo> threads);
    void set_thread_provider(std::function<std::vector<ThreadInfo>()> provider);
    void refresh_threads();
    [[nodiscard]] const std::vector<ThreadInfo>& threads() const { return threads_; }
    void select_thread(std::uint32_t id);
    [[nodiscard]] std::uint32_t selected_thread() const { return selected_thread_; }

    void set_arguments(std::vector<std::string> arguments) {
        arguments_ = std::move(arguments);
    }
    [[nodiscard]] const std::vector<std::string>& arguments() const {
        return arguments_;
    }
    void set_program_name(std::string name) {
        program_name_ = std::move(name);
    }

    void set_breakpoint(std::uint32_t address);
    void clear_breakpoint(std::uint32_t address);
    void clear_breakpoints();
    [[nodiscard]] bool at_breakpoint() const;
    [[nodiscard]] const std::set<std::uint32_t>& breakpoints() const { return breakpoints_; }

    [[nodiscard]] core::StepResult step();
    [[nodiscard]] core::StepResult step_line_in();
    [[nodiscard]] core::StepResult step_line_over();
    [[nodiscard]] core::StepResult step_line_out();
    [[nodiscard]] core::StepResult run(std::uint64_t max_steps = 1'000'000);
    [[nodiscard]] core::StepResult step_over();
    [[nodiscard]] core::StepResult step_out();

    [[nodiscard]] const Line* line_at(std::uint32_t pc) const;
    [[nodiscard]] const Function* function_at(std::uint32_t pc) const;
    [[nodiscard]] std::vector<Frame> call_stack() const;

    [[nodiscard]] bool read_variable(const std::string& name,
                                     std::uint32_t& value,
                                     std::string& error) const;
    [[nodiscard]] bool read_global(const std::string& name,
                                   std::uint32_t& value,
                                   std::string& error) const;

    [[nodiscard]] bool read_memory(std::uint32_t address,
                                   std::uint32_t size,
                                   std::uint32_t& value) const;
    [[nodiscard]] const std::string& console() const { return console_; }
    void clear_console() { console_.clear(); }

    [[nodiscard]] std::string dump() const;

private:
    void install_syscall_handler();
    [[nodiscard]] bool read_value(std::uint32_t address,
                                  std::uint32_t size,
                                  std::uint32_t& value) const;
    [[nodiscard]] std::uint32_t return_address() const;

    core::Machine machine_;
    Info info_;
    std::set<std::uint32_t> breakpoints_;
    std::vector<ThreadInfo> threads_;
    std::function<std::vector<ThreadInfo>()> thread_provider_;
    std::uint32_t selected_thread_ = 0;
    std::vector<std::string> arguments_;
    std::string program_name_ = "program";
    std::string console_;
};

} // namespace kr32::debug
