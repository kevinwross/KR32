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
    bool try_lock() {
        int expected = 0;
        return __atomic_compare_exchange_n(&state, &expected, 1, 0, 0, 0);
    }
    void unlock() {
        __atomic_store_n(&state, 0, 0);
        futex_wake(&state, 1);
    }
};

struct recursive_mutex {
    int state;
    int owner;
    int count;
    recursive_mutex() {
        state = 0;
        owner = 0;
        count = 0;
    }
    void lock() {
        const int self = thread_self();
        if (owner == self) {
            count = count + 1;
            return;
        }
        int expected = 0;
        while (!__atomic_compare_exchange_n(&state, &expected, 1, 0, 0, 0)) {
            expected = 0;
            futex_wait(&state, 1);
        }
        owner = self;
        count = 1;
    }
    bool try_lock() {
        const int self = thread_self();
        if (owner == self) {
            count = count + 1;
            return true;
        }
        int expected = 0;
        if (!__atomic_compare_exchange_n(&state, &expected, 1, 0, 0, 0)) {
            return false;
        }
        owner = self;
        count = 1;
        return true;
    }
    void unlock() {
        if (owner != thread_self()) {
            return;
        }
        count = count - 1;
        if (count == 0) {
            owner = 0;
            __atomic_store_n(&state, 0, 0);
            futex_wake(&state, 1);
        }
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
    thread() { handle = 0; }
    thread(int entry, int arg) { handle = thread_create(entry, arg, 0); }
    thread(void* entry, int arg) {
        handle = thread_create((int)entry, arg, 0);
    }
    bool joinable() { return handle != 0; }
    static int hardware_concurrency() { return 1; }
    void join() {
        if (handle != 0) {
            thread_join(handle);
            handle = 0;
        }
    }
    void detach() { handle = 0; }
    void yield() { thread_yield(); }
};

namespace this_thread {
inline void yield() { thread_yield(); }
inline int get_id() { return thread_self(); }
} // namespace this_thread

} // namespace kr32

namespace std {
typedef kr32::atomic_int atomic_int;
typedef kr32::condition_variable condition_variable;
typedef kr32::mutex mutex;
typedef kr32::recursive_mutex recursive_mutex;
struct thread {
    int handle;
    thread() { handle = 0; }
    thread(int entry, int arg) { handle = kr32::thread_create(entry, arg, 0); }
    thread(void* entry, int arg) { handle = kr32::thread_create((int)entry, arg, 0); }
    bool joinable() { return handle != 0; }
    static int hardware_concurrency() { return 1; }
    void join() {
        if (handle != 0) {
            kr32::thread_join(handle);
            handle = 0;
        }
    }
    void detach() { handle = 0; }
    void yield() { kr32::thread_yield(); }
};
namespace this_thread {
inline void yield() { kr32::thread_yield(); }
inline int get_id() { return kr32::thread_self(); }
} // namespace this_thread
} // namespace std
