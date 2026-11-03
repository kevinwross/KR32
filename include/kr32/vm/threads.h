#pragma once

#include <cstdint>
#include <map>
#include <string>
#include <vector>

#include "kr32/container/container.h"
#include "kr32/core/cluster.h"
#include "kr32/core/machine.h"
#include "kr32/vm/host.h"

namespace kr32::vm {

// Stage-1 thread/futex services over the cooperative Cluster. One guest thread
// runs on each core; the host advances one core turn at a time.
namespace thread_syscall {
inline constexpr std::uint32_t Create = 44;
inline constexpr std::uint32_t Join = 45;
inline constexpr std::uint32_t Yield = 46;
inline constexpr std::uint32_t Exit = 47;
inline constexpr std::uint32_t FutexWait = 48;
inline constexpr std::uint32_t FutexWake = 49;
inline constexpr std::uint32_t Self = 50;
} // namespace thread_syscall

inline constexpr std::uint32_t kThreadError = 0xFFFF'FFFFU;
inline constexpr std::uint32_t kThreadWouldBlock = 1U;

// TCB offsets fixed by the M9.10 thread runtime decision (R11 = TP).
namespace tcb {
inline constexpr std::uint32_t Self = 0;
inline constexpr std::uint32_t Id = 4;
inline constexpr std::uint32_t StackBase = 8;
inline constexpr std::uint32_t StackLimit = 12;
inline constexpr std::uint32_t Tsp = 16;
inline constexpr std::uint32_t FprSave = 20;
inline constexpr std::uint32_t CurrentException = 24;
inline constexpr std::uint32_t Reserved = 28;
inline constexpr std::uint32_t Tls = 32;
inline constexpr std::uint32_t HeaderSize = 160;
inline constexpr std::uint32_t FprSaveSize = 128;
inline constexpr std::uint32_t BlockSize = HeaderSize + FprSaveSize;
} // namespace tcb

struct ThreadContext {
    std::uint32_t regs[16] = {};
    std::uint32_t sp = 0;
    std::uint32_t pc = 0;
    std::uint32_t flags = 0;
    std::uint32_t tsp = 0;
    std::uint64_t fpr[16] = {};
    bool valid = false;
};

struct ClusterHostOptions {
    std::uint32_t cores = 2;
    std::uint32_t ram_size = 1U * 1024U * 1024U;
    std::uint32_t stack_size = 64U * 1024U;
};

class ClusterHost {
public:
    explicit ClusterHost(ClusterHostOptions options = {});

    [[nodiscard]] core::Cluster& cluster() { return cluster_; }
    [[nodiscard]] const core::Cluster& cluster() const { return cluster_; }

    void install();
    [[nodiscard]] bool load_image(const container::Image& image,
                                  std::string& error);

    // Start the primary thread on core 0.
    void start(std::uint32_t entry, std::uint32_t stack_top = 0);

    // Create a guest thread on a free core. Returns a 1-based handle.
    [[nodiscard]] std::uint32_t create(std::uint32_t entry,
                                       std::uint32_t arg = 0,
                                       std::uint32_t stack_size = 0);
    [[nodiscard]] bool join(std::uint32_t handle, std::uint32_t& exit_code);

    [[nodiscard]] bool futex_wait(std::uint32_t handle,
                                  std::uint32_t address,
                                  std::uint32_t expected);
    [[nodiscard]] std::uint32_t futex_wake(std::uint32_t address,
                                           std::uint32_t count);

    [[nodiscard]] core::StepResult step();
    [[nodiscard]] bool running(std::uint32_t handle) const;

    // Save/restore the full guest context (GPRs, SP/PC/FLAGS, FPRs, TSP).
    [[nodiscard]] bool save_context(std::uint32_t handle);
    [[nodiscard]] bool restore_context(std::uint32_t handle);

    // Guest syscall dispatch for the thread/futex vector range.
    [[nodiscard]] bool handle(core::Machine& machine, std::uint32_t vector);

private:
    enum class State { Free, Running, Waiting, Done, Faulted };
    struct Thread {
        std::uint32_t handle = 0;
        State state = State::Free;
        std::uint32_t exit_code = 0;
        std::uint32_t wait_address = 0;
        std::uint32_t stack_top = 0;
        std::uint32_t stack_limit = 0;
        std::uint32_t tcb = 0;
        ThreadContext context;
    };

    [[nodiscard]] std::uint32_t handle_for(const core::Machine& machine) const;
    [[nodiscard]] Thread* thread_for(std::uint32_t handle);
    [[nodiscard]] const Thread* thread_for(std::uint32_t handle) const;
    [[nodiscard]] std::uint32_t setup_thread(std::uint32_t core,
                                              std::uint32_t entry,
                                              std::uint32_t arg,
                                              std::uint32_t requested);
    [[nodiscard]] bool has_running_thread() const;
    [[nodiscard]] bool has_running_thread_except(std::uint32_t skip_core) const;
    [[nodiscard]] core::StepResult step_except(std::uint32_t skip_core);
    [[nodiscard]] std::uint32_t join_from_guest(std::uint32_t handle,
                                                std::uint32_t current_core);

    ClusterHostOptions options_;
    core::Cluster cluster_;
    std::uint32_t scheduler_index_ = 0;
    bool servicing_join_ = false;
    std::vector<Thread> threads_;
    std::vector<std::uint8_t> tls_template_;
    std::map<std::uint32_t, std::vector<std::uint32_t>> futex_waiters_;
};

} // namespace kr32::vm
