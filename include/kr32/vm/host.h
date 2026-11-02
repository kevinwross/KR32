#pragma once

#include <cstdint>
#include <fstream>
#include <map>
#include <string>
#include <vector>

#include "kr32/core/machine.h"

namespace kr32::vm {

// Guest-visible SYSCALL ABI. All calls use R0..R3 and return in R0.
//   32 exit(code)                 33 putchar(char)
//   34 getchar() -> int           35 open(path, flags) -> fd
//   36 close(fd)                  37 read(fd, buffer, count)
//   38 write(fd, buffer, count)   39 seek(fd, offset, whence)
//   40 time() -> seconds           41 random() -> uint32
//   42 argc()                      43 arg(index, buffer, size)
namespace syscall {
inline constexpr std::uint32_t Exit = 32;
inline constexpr std::uint32_t PutChar = 33;
inline constexpr std::uint32_t GetChar = 34;
inline constexpr std::uint32_t Open = 35;
inline constexpr std::uint32_t Close = 36;
inline constexpr std::uint32_t Read = 37;
inline constexpr std::uint32_t Write = 38;
inline constexpr std::uint32_t Seek = 39;
inline constexpr std::uint32_t Time = 40;
inline constexpr std::uint32_t Random = 41;
inline constexpr std::uint32_t Argc = 42;
inline constexpr std::uint32_t Arg = 43;
} // namespace syscall

struct HostOptions {
    std::string fs_root = ".";
    bool read_only = false;
    std::uint64_t quota = 1024U * 1024U;
    std::vector<std::string> args;
    std::string input;
    bool echo_console = true;
};

class Host {
public:
    explicit Host(HostOptions options = {});

    void install(core::Machine& machine);
    [[nodiscard]] bool handle(core::Machine& machine, std::uint32_t vector);

    [[nodiscard]] const std::string& console() const { return console_; }
    void clear_console() { console_.clear(); }
    void set_input(std::string input) { input_ = std::move(input); }
    [[nodiscard]] const std::string& last_error() const { return last_error_; }

private:
    struct File {
        std::fstream stream;
        std::string path;
        bool readable = false;
        bool writable = false;
    };

    [[nodiscard]] std::string resolve(const std::string& path, bool write, bool& ok);
    [[nodiscard]] int open(const std::string& path, std::uint32_t flags, bool& ok);
    [[nodiscard]] bool read(std::uint32_t fd,
                            core::Machine& machine,
                            std::uint32_t address,
                            std::uint32_t count,
                            std::uint32_t& result);
    [[nodiscard]] bool write(std::uint32_t fd,
                             core::Machine& machine,
                             std::uint32_t address,
                             std::uint32_t count,
                             std::uint32_t& result);
    [[nodiscard]] bool seek(std::uint32_t fd, std::int32_t offset, std::uint32_t whence,
                            std::uint32_t& result);
    [[nodiscard]] bool read_guest_string(core::Machine& machine,
                                         std::uint32_t address,
                                         std::string& value);
    [[nodiscard]] std::uint32_t random_value();
    [[nodiscard]] std::uint32_t argument_count() const;

    HostOptions options_;
    std::string root_;
    std::map<int, File> files_;
    int next_fd_ = 3;
    std::string console_;
    std::string input_;
    std::string last_error_;
    std::uint32_t random_state_ = 0x4B523332U;
    std::uint64_t bytes_written_ = 0;
};

} // namespace kr32::vm
