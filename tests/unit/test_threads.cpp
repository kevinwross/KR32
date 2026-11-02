#include "doctest.h"

#include "kr32/asm/object_assembler.h"
#include "kr32/cc/cc.h"
#include "kr32/container/container.h"
#include "kr32/link/linker.h"
#include "kr32/vm/threads.h"

#include <cstdint>
#include <string>

using namespace kr32;

namespace {

struct Program {
    container::Image image;
    std::uint32_t entry = 0;
};

Program build_worker(std::string_view source, std::string_view entry_name) {
    const cc::Result compiled = cc::compile(source, "thread-test.c");
    const std::string diagnostic =
        compiled.diagnostics.empty() ? "compile failed"
                                     : compiled.diagnostics.front().message;
    INFO(diagnostic);
    REQUIRE(compiled.ok);
    container::Image user;
    assembler::Error assembly_error;
    REQUIRE_MESSAGE(assembler::assemble_object(compiled.assembly, user,
                                               assembly_error),
                    assembly_error.message.c_str());
    container::Image runtime;
    REQUIRE_MESSAGE(assembler::assemble_object(cc::runtime_assembly(), runtime,
                                               assembly_error),
                    assembly_error.message.c_str());
    Program program;
    linker::Map map;
    linker::Error link_error;
    REQUIRE_MESSAGE(linker::link({user, runtime}, linker::Options{}, program.image,
                                 map, link_error),
                    link_error.message.c_str());
    for (const container::Symbol& symbol : program.image.symbols) {
        if (symbol.name == entry_name &&
            symbol.section != container::kUndefinedSection) {
            program.entry = symbol.value;
            break;
        }
    }
    REQUIRE(program.entry != 0);
    return program;
}

} // namespace

TEST_CASE("cluster host creates and joins a worker thread") {
    const Program program = build_worker(
        "void worker(int value) { exit(value); }\n"
        "int main() { return 0; }\n",
        "worker");
    vm::ClusterHost host({2, 1U * 1024U * 1024U, 64U * 1024U});
    host.install();
    std::string error;
    REQUIRE(host.load_image(program.image, error));
    const std::uint32_t handle = host.create(program.entry, 42);
    REQUIRE(handle != vm::kThreadError);
    CHECK(host.cluster().core(handle - 1).reg(0) == 42);
    CHECK(host.cluster().core(handle - 1).pc() == program.entry);
    std::uint32_t exit_code = 0;
    REQUIRE(host.join(handle, exit_code));
    CHECK(host.cluster().core(handle - 1).halted());
    CHECK(host.cluster().core(handle - 1).exit_code());
    CHECK(exit_code == 42);
}

TEST_CASE("thread_create syscall starts a thread and thread_self identifies it") {
    const Program program = build_worker(
        "void worker(int value) { exit(value); }\n"
        "int main() { return 0; }\n",
        "worker");
    vm::ClusterHost host({2, 1U * 1024U * 1024U, 64U * 1024U});
    host.install();
    std::string error;
    REQUIRE(host.load_image(program.image, error));
    host.start(0);
    core::Machine& primary = host.cluster().core(0);
    primary.set_reg(0, program.entry);
    primary.set_reg(1, 42);
    primary.set_reg(2, 0);
    REQUIRE(host.handle(primary, vm::thread_syscall::Create));
    const std::uint32_t created = primary.reg(0);
    REQUIRE(created != vm::kThreadError);
    primary.set_reg(0, 0);
    REQUIRE(host.handle(primary, vm::thread_syscall::Exit));
    std::uint32_t exit_code = 0;
    REQUIRE(host.join(created, exit_code));
    CHECK(exit_code == 42);
}

TEST_CASE("futex wait blocks a thread until futex wake") {
    const Program program = build_worker(
        "void worker(int value) { exit(value); }\n"
        "int main() { return 0; }\n",
        "worker");
    vm::ClusterHost host({2, 1U * 1024U * 1024U, 64U * 1024U});
    host.install();
    std::string error;
    REQUIRE(host.load_image(program.image, error));
    const std::uint32_t handle = host.create(program.entry, 42);
    REQUIRE(handle != vm::kThreadError);
    const std::uint32_t address = host.cluster().core(0).ram_base() + 0x1000U;
    REQUIRE(host.cluster().core(0).write32(address, 0));
    REQUIRE(host.futex_wait(handle, address, 0));
    CHECK_FALSE(host.running(handle));
    CHECK(host.futex_wake(address, 1) == 1);
    CHECK(host.running(handle));
    std::uint32_t exit_code = 0;
    REQUIRE(host.join(handle, exit_code));
    CHECK(exit_code == 42);
}

TEST_CASE("futex wait returns immediately when the value does not match") {
    vm::ClusterHost host({2, 1U * 1024U * 1024U, 64U * 1024U});
    host.install();
    host.start(0);
    core::Machine& primary = host.cluster().core(0);
    const std::uint32_t address = primary.ram_base() + 0x1000U;
    REQUIRE(primary.write32(address, 7));
    primary.set_reg(0, address);
    primary.set_reg(1, 0);
    REQUIRE(host.handle(primary, vm::thread_syscall::FutexWait));
    CHECK(primary.reg(0) == vm::kThreadWouldBlock);
}

TEST_CASE("guest TCB exposes the approved R11-relative layout") {
    const Program program = build_worker(
        "void worker(int value) { exit(value); }\n"
        "int main() { return 0; }\n",
        "worker");
    vm::ClusterHost host({2, 1U * 1024U * 1024U, 64U * 1024U});
    host.install();
    std::string error;
    REQUIRE(host.load_image(program.image, error));
    const std::uint32_t handle = host.create(program.entry, 42);
    REQUIRE(handle != vm::kThreadError);
    core::Machine& machine = host.cluster().core(handle - 1);
    const std::uint32_t tcb = machine.reg(11);
    REQUIRE(tcb != 0);
    std::uint32_t self = 0;
    std::uint32_t id = 0;
    std::uint32_t stack_base = 0;
    std::uint32_t stack_limit = 0;
    std::uint32_t fpr_save = 0;
    REQUIRE(machine.read32(tcb + vm::tcb::Self, self));
    REQUIRE(machine.read32(tcb + vm::tcb::Id, id));
    REQUIRE(machine.read32(tcb + vm::tcb::StackBase, stack_base));
    REQUIRE(machine.read32(tcb + vm::tcb::StackLimit, stack_limit));
    REQUIRE(machine.read32(tcb + vm::tcb::FprSave, fpr_save));
    CHECK(self == tcb);
    CHECK(id == handle);
    CHECK(stack_base == machine.sp());
    CHECK(stack_limit < stack_base);
    CHECK(fpr_save == tcb + vm::tcb::HeaderSize);
}

TEST_CASE("thread context saves and restores GPRs, FPRs, SP, PC, and TSP") {
    const Program program = build_worker(
        "void worker(int value) { exit(value); }\n"
        "int main() { return 0; }\n",
        "worker");
    vm::ClusterHost host({2, 1U * 1024U * 1024U, 64U * 1024U});
    host.install();
    std::string error;
    REQUIRE(host.load_image(program.image, error));
    const std::uint32_t handle = host.create(program.entry, 42);
    REQUIRE(handle != vm::kThreadError);
    core::Machine& machine = host.cluster().core(handle - 1);
    machine.set_reg(3, 0x1234);
    machine.set_fpr(2, 0x4009'21FB'5444'2D18ULL);
    machine.set_tsp(0x2000);
    REQUIRE(host.save_context(handle));
    machine.set_reg(3, 0);
    machine.set_fpr(2, 0);
    machine.set_tsp(0);
    REQUIRE(host.restore_context(handle));
    CHECK(machine.reg(3) == 0x1234);
    CHECK(machine.fpr(2) == 0x4009'21FB'5444'2D18ULL);
    CHECK(machine.tsp() == 0x2000);
    CHECK(machine.pc() == program.entry);
}

TEST_CASE("guest thread_join blocks until the target thread exits") {
    const Program program = build_worker(
        "void worker(int value) { exit(value); }\n"
        "int main() { return 0; }\n",
        "worker");
    vm::ClusterHost host({2, 1U * 1024U * 1024U, 64U * 1024U});
    host.install();
    std::string error;
    REQUIRE(host.load_image(program.image, error));
    host.start(0);
    core::Machine& primary = host.cluster().core(0);
    const std::uint32_t created = host.create(program.entry, 42);
    REQUIRE(created != vm::kThreadError);
    primary.set_reg(0, created);
    REQUIRE(host.handle(primary, vm::thread_syscall::Join));
    CHECK(primary.reg(0) == 42);
}

TEST_CASE("thread_self reports the primary thread handle") {
    vm::ClusterHost host({2, 1U * 1024U * 1024U, 64U * 1024U});
    host.install();
    host.start(0);
    core::Machine& primary = host.cluster().core(0);
    REQUIRE(host.handle(primary, vm::thread_syscall::Self));
    CHECK(primary.reg(0) == 1);
}
