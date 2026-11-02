#pragma once

// Minimal M9.10 thread/TLS/atomic/synchronization surface. The compiler
// injects the same declarations for `#include <kr32/thread.hpp>`.

namespace kr32 {

inline int thread_create(int entry, int arg, int stack_size = 0) {
    return __builtin_kr32_syscall(44, entry, arg, stack_size);
}
inline int thread_join(int handle) {
    return __builtin_kr32_syscall(45, handle, 0, 0);
}
inline void thread_yield() {
    (void)__builtin_kr32_syscall(46, 0, 0, 0);
}
inline void thread_exit(int code) {
    (void)__builtin_kr32_syscall(47, code, 0, 0);
}
inline int thread_self() {
    return __builtin_kr32_syscall(50, 0, 0, 0);
}
inline int futex_wait(int* address, int expected) {
    return __builtin_kr32_syscall(48, (int)address, expected, 0);
}
inline int futex_wake(int* address, int count) {
    return __builtin_kr32_syscall(49, (int)address, count, 0);
}

struct atomic_int {
    int value;
    atomic_int() { value = 0; }
    explicit atomic_int(int initial) { value = initial; }
    int load() { return __atomic_load_n(&value, 0); }
    void store(int next) { __atomic_store_n(&value, next, 0); }
    int fetch_add(int delta) { return __atomic_fetch_add(&value, delta, 0); }
    bool compare_exchange(int* expected, int desired) {
        return __atomic_compare_exchange_n(&value, expected, desired, 0, 0, 0);
    }
};

struct mutex {
    int state;
    mutex() { state = 0; }
    void lock() {
        int expected = 0;
        while (!__atomic_compare_exchange_n(&state, &expected, 1, 0, 0, 0)) {
            expected = 0;
            futex_wait(&state, 1);
        }
    }
    void unlock() {
        __atomic_store_n(&state, 0, 0);
        futex_wake(&state, 1);
    }
};

struct condition_variable {
    int sequence;
    condition_variable() { sequence = 0; }
    void wait(mutex& guard) {
        const int observed = sequence;
        guard.unlock();
        futex_wait(&sequence, observed);
        guard.lock();
    }
    void notify_one() {
        __atomic_fetch_add(&sequence, 1, 0);
        futex_wake(&sequence, 1);
    }
    void notify_all() {
        __atomic_fetch_add(&sequence, 1, 0);
        futex_wake(&sequence, 2147483647);
    }
};

struct thread {
    int handle;
    thread(int entry, int arg) { handle = thread_create(entry, arg, 0); }
    int join() { return thread_join(handle); }
    void yield() { thread_yield(); }
};

} // namespace kr32
