#pragma once

#include <cstdint>
#include <functional>
#include <set>
#include <string>
#include <vector>

#include "kr32/container/container.h"
#include "kr32/core/machine.h"
#include "kr32/file_dialog.h"
#include "kr32/vm/host.h"

namespace kr32::debug {

using FileDialogCallback =
    std::function<FileDialogResult(const FileDialogOptions&)>;

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

// A promoted local lives in a register over a PC range instead of in its frame
// slot: `P <function> <name> <register> <start_label> <end_label>`.
struct Location {
    std::string function;
    std::string name;
    int reg = 0;
    bool fp = false;
    std::uint32_t start = 0;
    std::uint32_t end = 0;
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
    std::vector<Location> locations;
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

// The members of a class type, including the ones it inherits. A derived class
// often has none of its own: `EngineerTree : BalancedTree` stores `root_` and
// `count_` at the base's offsets, so a reader that looks only at the derived
// record shows an aggregate with no fields. Bases are visited first, which is the
// order they occupy in the object, and the walk stops after eight levels so a
// malformed table cannot recurse for ever.
[[nodiscard]] std::vector<Member> class_members(const Info& info,
                                               const std::string& type);

// What a pointer type points at, or an empty string when the type is not a
// pointer. The front ends spell a pointer three ways - `char*` from C++, and
// `char_*` or `char *` from C - so a reader that strips only one of them treats
// the others as an aggregate and shows nothing.
[[nodiscard]] std::string pointee_type(const std::string& type);

class Session {
public:
    Session();

    [[nodiscard]] bool load(const container::Image& image, std::string& error);
    [[nodiscard]] bool load_os(const container::Image& kernel,
                              const container::Image& bios,
                              std::string& error);
    [[nodiscard]] bool load_raw(const std::vector<std::uint8_t>& kernel,
                                std::uint32_t kernel_base,
                                const std::vector<std::uint8_t>& bios,
                                std::string& error);
    [[nodiscard]] bool set_debug_info(const container::Image& image,
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
    void set_input(std::string input) {
        input_ = std::move(input);
    }
    void set_file_dialog(FileDialogCallback callback) {
        host_.set_file_dialog(std::move(callback));
    }

    void set_breakpoint(std::uint32_t address, bool enabled = true);
    void set_breakpoint_enabled(std::uint32_t address, bool enabled);
    void clear_breakpoint(std::uint32_t address);
    void clear_breakpoints();
    [[nodiscard]] bool at_breakpoint() const;
    [[nodiscard]] bool breakpoint_enabled(std::uint32_t address) const;
    [[nodiscard]] const std::set<std::uint32_t>& breakpoints() const { return breakpoints_; }
    [[nodiscard]] const std::set<std::uint32_t>& disabled_breakpoints() const {
        return disabled_breakpoints_;
    }

    [[nodiscard]] core::StepResult step();
    [[nodiscard]] core::StepResult step_line_in();
    [[nodiscard]] core::StepResult step_line_over();
    [[nodiscard]] core::StepResult step_line_out();
    // max_steps == 0 means no instruction limit.
    [[nodiscard]] core::StepResult run(std::uint64_t max_steps = 1'000'000);
    [[nodiscard]] core::StepResult step_over();
    [[nodiscard]] core::StepResult step_out();

    [[nodiscard]] const Line* line_at(std::uint32_t pc) const;
    [[nodiscard]] const Function* function_at(std::uint32_t pc) const;
    // The register location holding `name` at `pc`, or nullptr when the variable
    // is in its frame slot.
    [[nodiscard]] const Location* location_at(const std::string& function,
                                              const std::string& name,
                                              std::uint32_t pc) const;
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
    [[nodiscard]] const std::string& host_error() const { return host_.last_error(); }
    void clear_console() { console_.clear(); }

    [[nodiscard]] std::string dump() const;

private:
    void install_syscall_handler();
    [[nodiscard]] bool read_value(std::uint32_t address,
                                  std::uint32_t size,
                                  std::uint32_t& value) const;
    [[nodiscard]] std::uint32_t return_address() const;

    core::Machine machine_;
    vm::Host host_;
    Info info_;
    std::set<std::uint32_t> breakpoints_;
    std::set<std::uint32_t> disabled_breakpoints_;
    std::vector<ThreadInfo> threads_;
    std::function<std::vector<ThreadInfo>()> thread_provider_;
    std::uint32_t selected_thread_ = 0;
    std::vector<std::string> arguments_;
    std::string program_name_ = "program";
    std::string console_;
    std::string input_;
};

} // namespace kr32::debug
