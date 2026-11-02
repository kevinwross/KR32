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
    };

    [[nodiscard]] std::uint32_t handle_for(const core::Machine& machine) const;
    [[nodiscard]] Thread* thread_for(std::uint32_t handle);
    [[nodiscard]] const Thread* thread_for(std::uint32_t handle) const;
    [[nodiscard]] std::uint32_t allocate_stack(std::uint32_t core,
                                               std::uint32_t requested);
    [[nodiscard]] bool has_running_thread() const;

    ClusterHostOptions options_;
    core::Cluster cluster_;
    std::uint32_t scheduler_index_ = 0;
    std::vector<Thread> threads_;
    std::map<std::uint32_t, std::vector<std::uint32_t>> futex_waiters_;
};

} // namespace kr32::vm
