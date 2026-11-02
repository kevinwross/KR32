#include "kr32/vm/threads.h"

#include <algorithm>
#include <utility>

namespace kr32::vm {

ClusterHost::ClusterHost(ClusterHostOptions options)
    : options_(std::move(options)),
      cluster_(std::max(1U, options_.cores),
               core::Machine::Options{options_.ram_size}) {
    threads_.resize(cluster_.size());
    for (std::size_t index = 0; index < threads_.size(); ++index) {
        threads_[index].handle = static_cast<std::uint32_t>(index) + 1;
    }
}

void ClusterHost::install() {
    for (std::uint32_t index = 0; index < cluster_.size(); ++index) {
        cluster_.core(index).set_syscall_handler(
            [this](core::Machine& machine, std::uint32_t vector) {
                return handle(machine, vector);
            });
    }
}

bool ClusterHost::load_image(const container::Image& image, std::string& error) {
    for (const container::Section& section : image.sections) {
        if (section.name == ".tls") {
            tls_template_ = section.data;
            continue;
        }
        if ((section.flags & container::Alloc) == 0 ||
            section.type == container::SectionType::Bss) {
            continue;
        }
        if (!cluster_.core(0).load_image(section.address, section.data)) {
            error = "failed to load image section";
            return false;
        }
    }
    return true;
}

void ClusterHost::start(std::uint32_t entry, std::uint32_t stack_top) {
    if (stack_top != 0) {
        core::Machine& machine = cluster_.core(0);
        machine.reset(entry, stack_top);
        threads_[0].state = State::Running;
        threads_[0].stack_top = stack_top;
        threads_[0].stack_limit = cluster_.core(0).ram_base();
        threads_[0].tcb = 0;
        return;
    }
    (void)setup_thread(0, entry, 0, options_.stack_size);
}

std::uint32_t ClusterHost::create(std::uint32_t entry, std::uint32_t arg,
                                  std::uint32_t stack_size) {
    for (std::uint32_t core = 1; core < cluster_.size(); ++core) {
        Thread& thread = threads_[core];
        if (thread.state != State::Free) {
            continue;
        }
        (void)setup_thread(core, entry, arg,
                           stack_size == 0 ? options_.stack_size : stack_size);
        return thread.handle;
    }
    return kThreadError;
}

std::uint32_t ClusterHost::setup_thread(std::uint32_t core,
                                        std::uint32_t entry,
                                        std::uint32_t arg,
                                        std::uint32_t requested) {
    Thread& thread = threads_[core];
    const std::uint32_t minimum = 4096;
    const std::uint32_t size = std::max(minimum, (requested + 15U) & ~15U);
    const std::uint32_t region_base =
        cluster_.core(0).ram_base() + cluster_.core(0).ram_size() -
        (core + 1U) * size;
    const std::uint32_t region_end = region_base + size;
    const std::uint32_t tcb = (region_end - 320U) & ~15U;
    const std::uint32_t stack_top = (tcb - 16U) & ~15U;
    const std::uint32_t stack_limit = (region_base + 512U) & ~15U;
    const std::uint32_t tsp = (region_base + 1024U) & ~63U;

    core::Machine& machine = cluster_.core(core);
    machine.reset(entry, stack_top);
    machine.set_reg(0, arg);
    machine.set_reg(11, tcb);
    machine.set_tsp(tsp);

    (void)machine.write32(tcb + tcb::Self, tcb);
    (void)machine.write32(tcb + tcb::Id, thread.handle);
    (void)machine.write32(tcb + tcb::StackBase, stack_top);
    (void)machine.write32(tcb + tcb::StackLimit, stack_limit);
    (void)machine.write32(tcb + tcb::Tsp, tsp);
    (void)machine.write32(tcb + tcb::FprSave, tcb + tcb::HeaderSize);
    (void)machine.write32(tcb + tcb::CurrentException, 0);
    (void)machine.write32(tcb + tcb::Reserved, 0);
    for (std::size_t index = tcb::Tls; index < tls_template_.size(); ++index) {
        (void)machine.write8(tcb + static_cast<std::uint32_t>(index),
                             tls_template_[index]);
    }

    thread.state = State::Running;
    thread.exit_code = 0;
    thread.stack_top = stack_top;
    thread.stack_limit = stack_limit;
    thread.tcb = tcb;
    thread.wait_address = 0;
    thread.context = {};
    return tcb;
}

bool ClusterHost::join(std::uint32_t handle, std::uint32_t& exit_code) {
    Thread* thread = thread_for(handle);
    if (thread == nullptr || thread->state == State::Free) {
        return false;
    }
    while (thread->state != State::Done && thread->state != State::Faulted) {
        if (!has_running_thread()) {
            return false;
        }
        (void)step();
    }
    exit_code = thread->exit_code;
    return true;
}

bool ClusterHost::futex_wait(std::uint32_t handle, std::uint32_t address,
                             std::uint32_t expected) {
    Thread* thread = thread_for(handle);
    if (thread == nullptr || thread->state != State::Running) {
        return false;
    }
    std::uint32_t value = 0;
    if (!cluster_.core(handle - 1).read32(address, value)) {
        return false;
    }
    if (value != expected) {
        return false;
    }
    thread->state = State::Waiting;
    thread->wait_address = address;
    futex_waiters_[address].push_back(handle);
    return true;
}

std::uint32_t ClusterHost::futex_wake(std::uint32_t address,
                                      std::uint32_t count) {
    const auto found = futex_waiters_.find(address);
    if (found == futex_waiters_.end()) {
        return 0;
    }
    std::uint32_t woken = 0;
    auto& waiters = found->second;
    while (!waiters.empty() && woken < count) {
        const std::uint32_t handle = waiters.front();
        waiters.erase(waiters.begin());
        if (Thread* thread = thread_for(handle)) {
            if (thread->state == State::Waiting) {
                thread->state = State::Running;
                thread->wait_address = 0;
                ++woken;
            }
        }
    }
    if (waiters.empty()) {
        futex_waiters_.erase(found);
    }
    return woken;
}

core::StepResult ClusterHost::step() {
    for (std::uint32_t attempt = 0; attempt < cluster_.size(); ++attempt) {
        const std::uint32_t core =
            (scheduler_index_++) % static_cast<std::uint32_t>(cluster_.size());
        Thread& thread = threads_[core];
        if (thread.state != State::Running) {
            continue;
        }
        core::Machine& machine = cluster_.core(core);
        const std::uint64_t before = machine.write_generation();
        core::StepResult result = machine.step();
        if (machine.write_generation() != before) {
            for (std::uint32_t other = 0; other < cluster_.size(); ++other) {
                if (other != core) {
                    cluster_.core(other).invalidate_reservation();
                }
            }
        }
        if (result.kind == core::StepResult::Kind::Halted) {
            thread.state = State::Done;
            thread.exit_code =
                machine.exit_code_set() ? machine.exit_code() : machine.reg(0);
        } else if (result.kind == core::StepResult::Kind::Raised) {
            thread.state = State::Faulted;
            thread.exit_code = kThreadError;
        }
        return result;
    }
    return {};
}

core::StepResult ClusterHost::step_except(std::uint32_t skip_core) {
    for (std::uint32_t attempt = 0; attempt < cluster_.size(); ++attempt) {
        const std::uint32_t core =
            (scheduler_index_++) % static_cast<std::uint32_t>(cluster_.size());
        if (core == skip_core) {
            continue;
        }
        Thread& thread = threads_[core];
        if (thread.state != State::Running) {
            continue;
        }
        core::Machine& machine = cluster_.core(core);
        const std::uint64_t before = machine.write_generation();
        core::StepResult result = machine.step();
        if (machine.write_generation() != before) {
            for (std::uint32_t other = 0; other < cluster_.size(); ++other) {
                if (other != core) {
                    cluster_.core(other).invalidate_reservation();
                }
            }
        }
        if (result.kind == core::StepResult::Kind::Halted) {
            thread.state = State::Done;
            thread.exit_code =
                machine.exit_code_set() ? machine.exit_code() : machine.reg(0);
        } else if (result.kind == core::StepResult::Kind::Raised) {
            thread.state = State::Faulted;
            thread.exit_code = kThreadError;
        }
        return result;
    }
    return {};
}

std::uint32_t ClusterHost::join_from_guest(std::uint32_t handle,
                                           std::uint32_t current_core) {
    Thread* thread = thread_for(handle);
    if (thread == nullptr || thread->state == State::Free ||
        handle == current_core + 1 || servicing_join_) {
        return kThreadError;
    }
    servicing_join_ = true;
    while (thread->state != State::Done && thread->state != State::Faulted) {
        if (!has_running_thread_except(current_core)) {
            servicing_join_ = false;
            return kThreadError;
        }
        (void)step_except(current_core);
    }
    servicing_join_ = false;
    return thread->exit_code;
}

bool ClusterHost::running(std::uint32_t handle) const {
    const Thread* thread = thread_for(handle);
    return thread != nullptr && thread->state == State::Running;
}

bool ClusterHost::save_context(std::uint32_t handle) {
    Thread* thread = thread_for(handle);
    if (thread == nullptr || thread->state == State::Free) {
        return false;
    }
    core::Machine& machine = cluster_.core(handle - 1);
    for (std::size_t index = 0; index < 16; ++index) {
        thread->context.regs[index] = machine.reg(index);
        thread->context.fpr[index] = machine.fpr(index);
    }
    thread->context.sp = machine.sp();
    thread->context.pc = machine.pc();
    thread->context.flags = machine.flags();
    thread->context.tsp = machine.tsp();
    thread->context.valid = true;
    if (thread->tcb != 0) {
        (void)machine.write32(thread->tcb + tcb::Tsp, thread->context.tsp);
        for (std::size_t index = 0; index < 16; ++index) {
            const std::uint32_t address =
                thread->tcb + tcb::HeaderSize +
                static_cast<std::uint32_t>(index * 8U);
            (void)machine.write32(
                address, static_cast<std::uint32_t>(thread->context.fpr[index]));
            (void)machine.write32(
                address + 4U,
                static_cast<std::uint32_t>(thread->context.fpr[index] >> 32));
        }
    }
    return true;
}

bool ClusterHost::restore_context(std::uint32_t handle) {
    Thread* thread = thread_for(handle);
    if (thread == nullptr || !thread->context.valid) {
        return false;
    }
    core::Machine& machine = cluster_.core(handle - 1);
    for (std::size_t index = 0; index < 16; ++index) {
        machine.set_reg(index, thread->context.regs[index]);
        machine.set_fpr(index, thread->context.fpr[index]);
    }
    machine.set_sp(thread->context.sp);
    machine.set_pc(thread->context.pc);
    machine.set_flags(thread->context.flags);
    machine.set_tsp(thread->context.tsp);
    if (thread->tcb != 0) {
        (void)machine.write32(thread->tcb + tcb::Tsp, thread->context.tsp);
    }
    return true;
}

bool ClusterHost::handle(core::Machine& machine, std::uint32_t vector) {
    const std::uint32_t handle = handle_for(machine);
    Thread* thread = thread_for(handle);
    switch (vector) {
    case syscall::Exit:
        if (thread != nullptr) {
            thread->state = State::Done;
            thread->exit_code = machine.reg(0);
        }
        machine.halt(machine.reg(0));
        return true;
    case thread_syscall::Create: {
        const std::uint32_t created =
            create(machine.reg(0), machine.reg(1), machine.reg(2));
        machine.set_reg(0, created);
        return true;
    }
    case thread_syscall::Join:
        machine.set_reg(0, join_from_guest(machine.reg(0), machine.core_id()));
        return true;
    case thread_syscall::Yield:
        machine.set_reg(0, 0);
        return true;
    case thread_syscall::Exit:
        if (thread != nullptr) {
            thread->state = State::Done;
            thread->exit_code = machine.reg(0);
        }
        machine.halt(machine.reg(0));
        return true;
    case thread_syscall::FutexWait: {
        const bool blocked =
            thread != nullptr && futex_wait(handle, machine.reg(0), machine.reg(1));
        machine.set_reg(0, blocked ? 0U : kThreadWouldBlock);
        return true;
    }
    case thread_syscall::FutexWake:
        machine.set_reg(0, futex_wake(machine.reg(0), machine.reg(1)));
        return true;
    case thread_syscall::Self:
        machine.set_reg(0, handle);
        return true;
    default:
        return false;
    }
}

std::uint32_t ClusterHost::handle_for(const core::Machine& machine) const {
    const std::uint32_t core = machine.core_id();
    if (core >= threads_.size()) {
        return kThreadError;
    }
    return threads_[core].handle;
}

ClusterHost::Thread* ClusterHost::thread_for(std::uint32_t handle) {
    if (handle == 0 || handle > threads_.size()) {
        return nullptr;
    }
    return &threads_[handle - 1];
}

const ClusterHost::Thread* ClusterHost::thread_for(std::uint32_t handle) const {
    if (handle == 0 || handle > threads_.size()) {
        return nullptr;
    }
    return &threads_[handle - 1];
}

bool ClusterHost::has_running_thread_except(std::uint32_t skip_core) const {
    for (std::uint32_t core = 0; core < threads_.size(); ++core) {
        if (core == skip_core) {
            continue;
        }
        if (threads_[core].state == State::Running) {
            return true;
        }
    }
    return false;
}

bool ClusterHost::has_running_thread() const {
    for (const Thread& thread : threads_) {
        if (thread.state == State::Running) {
            return true;
        }
    }
    return false;
}

} // namespace kr32::vm
