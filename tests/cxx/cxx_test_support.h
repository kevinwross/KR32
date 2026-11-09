#pragma once
#include "doctest.h"

#include "cxx/codegen.h"
#include "kr32/asm/object_assembler.h"
#include "kr32/cc/cc.h"
#include "kr32/container/container.h"
#include "kr32/core/machine.h"
#include "kr32/cxx/cxx.h"
#include "kr32/debug/debug.h"
#include "kr32/link/linker.h"
#include "kr32/vm/threads.h"
#include "kr32/vm/threads.h"

#include <cstdint>
#include <cstdlib>
#include <string>
#include <string_view>
#include <vector>

using namespace kr32;

namespace {

struct ProgramResult {
    std::string output;
    std::uint32_t result = 0;
    bool ok = false;
    std::string diagnostics;
};

ProgramResult run_assemblies(const std::vector<std::string>& user_assemblies) {
    ProgramResult program;
    std::vector<container::Image> objects;
    assembler::Error assembly_error;
    for (const std::string& assembly : user_assemblies) {
        container::Image object;
        if (!assembler::assemble_object(assembly, object, assembly_error)) {
            program.diagnostics = assembly_error.message;
            return program;
        }
        objects.push_back(std::move(object));
    }
    container::Image runtime;
    if (!assembler::assemble_object(cc::runtime_assembly(), runtime, assembly_error)) {
        program.diagnostics = assembly_error.message;
        return program;
    }
    objects.push_back(std::move(runtime));

    container::Image image;
    linker::Map map;
    linker::Error link_error;
    linker::Options link_options;
    link_options.stack_top = 0x0011'0000;
    link_options.stack_limit = 0x0001'0000;
    if (!linker::link(objects, link_options, image, map, link_error)) {
        program.diagnostics = link_error.message;
        return program;
    }

    core::Machine machine(core::Machine::Options{1U * 1024U * 1024U, 0, true});
    for (const container::Section& section : image.sections) {
        if ((section.flags & container::Alloc) == 0 ||
            section.type == container::SectionType::Bss) {
            continue;
        }
        if (!machine.load_image(section.address, section.data)) {
            program.diagnostics = "failed to load image";
            return program;
        }
    }
    machine.reset(image.entry);
    machine.set_tsp(machine.ram_base() + 512U * 1024U);
    machine.set_reg(11, machine.ram_base() + machine.ram_size() - 1024U);
    for (const container::Section& section : image.sections) {
        if (section.name == ".tls") {
            for (std::size_t index = 0; index < section.data.size(); ++index) {
                (void)machine.write8(
                    machine.reg(11) + 32U + static_cast<std::uint32_t>(index),
                    section.data[index]);
            }
        }
    }
    machine.set_syscall_handler([&](core::Machine& current, std::uint32_t vector) {
        if (vector == 33) {
            program.output.push_back(static_cast<char>(current.reg(0) & 0xFFU));
            return true;
        }
        if (vector == 32) {
            current.halt(current.reg(0));
            return true;
        }
        if (vector == 42) {
            current.set_reg(0, 0);
            return true;
        }
        if (vector == 43) {
            current.set_reg(0, 0xFFFF'FFFCU);
            return true;
        }
        if (vector >= 44 && vector <= 50) {
            current.set_reg(0, 0);
            return true;
        }
        return false;
    });

    for (int step = 0; step < 1000000; ++step) {
        const core::StepResult step_result = machine.step();
        if (step_result.kind == core::StepResult::Kind::Halted) {
            program.result = machine.exit_code_set() ? machine.exit_code()
                                                     : machine.reg(0);
            program.ok = true;
            return program;
        }
        if (step_result.kind == core::StepResult::Kind::Raised) {
            program.diagnostics = "machine fault";
            return program;
        }
    }
    program.diagnostics = "step limit";
    return program;
}

bool build_cluster_image(std::string_view source, container::Image& image,
                         std::string& error) {
    const cxx::Result compiled = cxx::compile(source);
    if (!compiled.ok) {
        error = "compile failed";
        return false;
    }
    container::Image object;
    assembler::Error assembly_error;
    if (!assembler::assemble_object(compiled.assembly, object, assembly_error)) {
        error = assembly_error.message;
        return false;
    }
    container::Image runtime;
    if (!assembler::assemble_object(cc::runtime_assembly(), runtime,
                                    assembly_error)) {
        error = assembly_error.message;
        return false;
    }
    std::vector<container::Image> objects;
    objects.push_back(std::move(object));
    objects.push_back(std::move(runtime));
    linker::Map map;
    linker::Error link_error;
    linker::Options options;
    options.stack_top = 0x0011'0000;
    options.stack_limit = 0x0001'0000;
    if (!linker::link(objects, options, image, map, link_error)) {
        error = link_error.message;
        return false;
    }
    return true;
}

ProgramResult compile_and_run(std::string_view source, bool optimize = false) {
    cxx::Options options;
    options.optimize = optimize;
    const cxx::Result compiled = cxx::compile(source, options);
    if (!compiled.ok) {
        ProgramResult failed;
        for (const cxx::Diagnostic& diagnostic : compiled.diagnostics) {
            failed.diagnostics += std::to_string(diagnostic.line) + ":" +
                                  std::to_string(diagnostic.column) + ": " +
                                  diagnostic.message + "\n";
        }
        return failed;
    }
    return run_assemblies({compiled.assembly});
}

ProgramResult compile_and_run_pair(std::string_view first, std::string_view second) {
    const cxx::Result first_result = cxx::compile(first);
    const cxx::Result second_result = cxx::compile(second);
    if (!first_result.ok || !second_result.ok) {
        ProgramResult failed;
        for (const cxx::Result* result : {&first_result, &second_result}) {
            for (const cxx::Diagnostic& diagnostic : result->diagnostics) {
                failed.diagnostics += std::to_string(diagnostic.line) + ":" +
                                      std::to_string(diagnostic.column) + ": " +
                                      diagnostic.message + "\n";
            }
        }
        return failed;
    }
    return run_assemblies({first_result.assembly, second_result.assembly});
}

ProgramResult compile_and_run_many(const std::vector<std::string>& sources) {
    std::vector<std::string> assemblies;
    ProgramResult failed;
    for (const std::string& source : sources) {
        const cxx::Result compiled = cxx::compile(source);
        if (!compiled.ok) {
            for (const cxx::Diagnostic& diagnostic : compiled.diagnostics) {
                failed.diagnostics += std::to_string(diagnostic.line) + ":" +
                                      std::to_string(diagnostic.column) + ": " +
                                      diagnostic.message + "\n";
            }
            return failed;
        }
        assemblies.push_back(compiled.assembly);
    }
    return run_assemblies(assemblies);
}

} // namespace
