#include "kr32/vm/host.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <system_error>
#include <utility>
#include <vector>

namespace kr32::vm {
namespace {

constexpr std::uint32_t kErrorIo = 0xFFFF'FFFFU;
constexpr std::uint32_t kErrorInvalid = 0xFFFF'FFFCU;
constexpr std::uint32_t kErrorNoSpace = 0xFFFF'FFFBU;

bool starts_with_path(const std::string& candidate, const std::string& root) {
    if (candidate == root) {
        return true;
    }
    if (candidate.size() <= root.size()) {
        return false;
    }
    if (candidate.compare(0, root.size(), root) != 0) {
        return false;
    }
    const char separator = candidate[root.size()];
    return separator == '/' || separator == '\\';
}

} // namespace

Host::Host(HostOptions options) : options_(std::move(options)) {
    input_ = options_.input;
    std::error_code error;
    std::filesystem::path root = std::filesystem::absolute(options_.fs_root, error);
    if (error) {
        root = std::filesystem::path(options_.fs_root);
    }
    root = std::filesystem::weakly_canonical(root, error);
    if (error) {
        root = root.lexically_normal();
    }
    std::filesystem::create_directories(root, error);
    root_ = root.string();
}

std::string Host::resolve(const std::string& path, bool write, bool& ok) {
    ok = false;
    last_error_.clear();
    if (path.empty() || path.find('\0') != std::string::npos) {
        last_error_ = "empty or invalid path";
        return {};
    }
    const std::filesystem::path relative(path);
    if (relative.is_absolute()) {
        last_error_ = "absolute paths are outside the sandbox";
        return {};
    }
    for (const auto& component : relative) {
        if (component == "..") {
            last_error_ = "parent traversal is outside the sandbox";
            return {};
        }
    }
    if (write && options_.read_only) {
        last_error_ = "sandbox is read-only";
        return {};
    }
    std::error_code error;
    std::filesystem::path candidate = std::filesystem::path(root_) / relative;
    candidate = std::filesystem::weakly_canonical(candidate, error);
    if (error) {
        candidate = candidate.lexically_normal();
    }
    const std::string result = candidate.string();
    if (!starts_with_path(result, root_)) {
        last_error_ = "path escapes the sandbox root";
        return {};
    }
    ok = true;
    return result;
}

int Host::open(const std::string& path, std::uint32_t flags, bool& ok) {
    ok = false;
    const bool write = (flags & 3U) != 0U;
    std::string resolved = resolve(path, write, ok);
    if (!ok) {
        return -1;
    }
    std::error_code error;
    const bool exists = std::filesystem::exists(resolved, error);
    if (!exists && (flags & 4U) == 0U) {
        last_error_ = "file does not exist";
        return -1;
    }
    std::ios::openmode mode = std::ios::binary;
    const std::uint32_t access = flags & 3U;
    if (!exists || (flags & 8U) != 0U) {
        mode |= std::ios::trunc;
    }
    if (access == 0U) {
        mode |= std::ios::in;
    } else if (access == 1U) {
        mode |= std::ios::out;
        if (exists && (flags & 8U) == 0U && (flags & 16U) == 0U) {
            mode |= std::ios::in;
        }
    } else {
        mode |= std::ios::in | std::ios::out;
    }
    if ((flags & 16U) != 0U) {
        mode |= std::ios::app;
    }
    File file;
    file.stream.open(resolved, mode);
    if (!file.stream.is_open() && write) {
        std::ofstream create(resolved, std::ios::binary | std::ios::trunc);
        if (create) {
            create.close();
            file.stream.clear();
            file.stream.open(resolved, mode);
        }
    }
    if (!file.stream.is_open()) {
        last_error_ = "cannot open file: " + resolved;
        return -1;
    }
    file.path = std::move(resolved);
    file.readable = access != 1U;
    file.writable = access != 0U;
    const int descriptor = next_fd_++;
    files_.emplace(descriptor, std::move(file));
    ok = true;
    return descriptor;
}

bool Host::read_guest_string(core::Machine& machine,
                             std::uint32_t address,
                             std::string& value) {
    value.clear();
    for (std::uint32_t offset = 0; offset < 4096; ++offset) {
        std::uint8_t byte = 0;
        if (!machine.read8(address + offset, byte)) {
            return false;
        }
        if (byte == 0) {
            return true;
        }
        value.push_back(static_cast<char>(byte));
    }
    return false;
}

bool Host::read(std::uint32_t descriptor,
                core::Machine& machine,
                std::uint32_t address,
                std::uint32_t count,
                std::uint32_t& result) {
    const auto found = files_.find(static_cast<int>(descriptor));
    if (found == files_.end() || !found->second.readable) {
        result = kErrorInvalid;
        return false;
    }
    std::vector<char> buffer(count);
    found->second.stream.read(buffer.data(), static_cast<std::streamsize>(count));
    const std::streamsize bytes = found->second.stream.gcount();
    for (std::streamsize index = 0; index < bytes; ++index) {
        if (!machine.write8(address + static_cast<std::uint32_t>(index),
                            static_cast<std::uint8_t>(buffer[static_cast<std::size_t>(index)]))) {
            result = kErrorIo;
            return false;
        }
    }
    result = static_cast<std::uint32_t>(bytes);
    return true;
}

bool Host::write(std::uint32_t descriptor,
                 core::Machine& machine,
                 std::uint32_t address,
                 std::uint32_t count,
                 std::uint32_t& result) {
    const auto found = files_.find(static_cast<int>(descriptor));
    if (found == files_.end() || !found->second.writable) {
        result = kErrorInvalid;
        return false;
    }
    if (options_.quota != 0 && bytes_written_ + count > options_.quota) {
        result = kErrorNoSpace;
        return false;
    }
    std::vector<char> buffer(count);
    for (std::uint32_t index = 0; index < count; ++index) {
        std::uint8_t byte = 0;
        if (!machine.read8(address + index, byte)) {
            result = kErrorIo;
            return false;
        }
        buffer[index] = static_cast<char>(byte);
    }
    found->second.stream.write(buffer.data(), static_cast<std::streamsize>(count));
    if (!found->second.stream) {
        result = kErrorIo;
        return false;
    }
    bytes_written_ += count;
    result = count;
    return true;
}

bool Host::seek(std::uint32_t descriptor,
                std::int32_t offset,
                std::uint32_t whence,
                std::uint32_t& result) {
    const auto found = files_.find(static_cast<int>(descriptor));
    if (found == files_.end()) {
        result = kErrorInvalid;
        return false;
    }
    std::ios::seekdir direction = std::ios::beg;
    if (whence == 1) {
        direction = std::ios::cur;
    } else if (whence == 2) {
        direction = std::ios::end;
    }
    found->second.stream.clear();
    found->second.stream.seekg(offset, direction);
    found->second.stream.seekp(offset, direction);
    std::streampos position = found->second.stream.tellg();
    if (position < 0) {
        position = found->second.stream.tellp();
    }
    if (position < 0) {
        result = kErrorIo;
        return false;
    }
    result = static_cast<std::uint32_t>(position);
    return true;
}

std::uint32_t Host::random_value() {
    random_state_ ^= random_state_ << 13U;
    random_state_ ^= random_state_ >> 17U;
    random_state_ ^= random_state_ << 5U;
    return random_state_;
}

std::uint32_t Host::argument_count() const {
    return static_cast<std::uint32_t>(options_.args.size());
}

void Host::install(core::Machine& machine) {
    machine.set_syscall_handler([this](core::Machine& current, std::uint32_t vector) {
        return handle(current, vector);
    });
}

bool Host::handle(core::Machine& machine, std::uint32_t vector) {
    switch (vector) {
    case syscall::Exit:
        machine.halt(machine.reg(0));
        return true;
    case syscall::PutChar: {
        const char character = static_cast<char>(machine.reg(0) & 0xFFU);
        console_.push_back(character);
        if (options_.echo_console) {
            std::putchar(character);
            std::fflush(stdout);
        }
        return true;
    }
    case syscall::GetChar: {
        if (input_.empty()) {
            machine.set_reg(0, kErrorIo);
        } else {
            machine.set_reg(0, static_cast<std::uint32_t>(static_cast<unsigned char>(input_.front())));
            input_.erase(input_.begin());
        }
        return true;
    }
    case syscall::Open: {
        std::string path;
        if (!read_guest_string(machine, machine.reg(0), path)) {
            last_error_ = "invalid guest path pointer";
            machine.set_reg(0, kErrorInvalid);
            return true;
        }
        bool ok = false;
        const int descriptor = open(path, machine.reg(1), ok);
        machine.set_reg(0, ok ? static_cast<std::uint32_t>(descriptor) : kErrorIo);
        return true;
    }
    case syscall::Close: {
        const auto found = files_.find(static_cast<int>(machine.reg(0)));
        if (found == files_.end()) {
            machine.set_reg(0, kErrorInvalid);
        } else {
            found->second.stream.close();
            files_.erase(found);
            machine.set_reg(0, 0);
        }
        return true;
    }
    case syscall::Read: {
        std::uint32_t result = 0;
        (void)read(machine.reg(0), machine, machine.reg(1), machine.reg(2), result);
        machine.set_reg(0, result);
        return true;
    }
    case syscall::Write: {
        std::uint32_t result = 0;
        (void)write(machine.reg(0), machine, machine.reg(1), machine.reg(2), result);
        machine.set_reg(0, result);
        return true;
    }
    case syscall::Seek: {
        std::uint32_t result = 0;
        (void)seek(machine.reg(0), static_cast<std::int32_t>(machine.reg(1)), machine.reg(2),
                   result);
        machine.set_reg(0, result);
        return true;
    }
    case syscall::Time: {
        const auto now = std::chrono::system_clock::now().time_since_epoch();
        machine.set_reg(0, static_cast<std::uint32_t>(
                               std::chrono::duration_cast<std::chrono::seconds>(now).count() &
                               0xFFFF'FFFFU));
        return true;
    }
    case syscall::Random:
        machine.set_reg(0, random_value());
        return true;
    case syscall::Argc:
        machine.set_reg(0, argument_count());
        return true;
    case syscall::Arg: {
        const std::uint32_t index = machine.reg(0);
        const std::uint32_t address = machine.reg(1);
        const std::uint32_t size = machine.reg(2);
        if (index >= options_.args.size() || size == 0) {
            machine.set_reg(0, kErrorInvalid);
            return true;
        }
        const std::string& argument = options_.args[index];
        const std::uint32_t count =
            std::min<std::uint32_t>(size - 1U, static_cast<std::uint32_t>(argument.size()));
        for (std::uint32_t offset = 0; offset < count; ++offset) {
            (void)machine.write8(address + offset, static_cast<std::uint8_t>(argument[offset]));
        }
        (void)machine.write8(address + count, 0);
        machine.set_reg(0, count);
        return true;
    }
    case 46: // thread yield
        machine.set_reg(0, 0);
        return true;
    case 50: // thread self
        machine.set_reg(0, 1);
        return true;
    default:
        last_error_ = "unsupported syscall";
        machine.set_reg(0, kErrorInvalid);
        return true;
    }
}

} // namespace kr32::vm
