#pragma once

#include <array>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace kr32::core {

class Machine;

// MMIO window from the plan. Each device owns a 4 KiB page inside it.
inline constexpr std::uint32_t kDeviceBase = 0xFFF0'0000;
inline constexpr std::uint32_t kDeviceWindow = 0x000F'0000;
inline constexpr std::uint32_t kTimerBase = kDeviceBase;
inline constexpr std::uint32_t kBlockBase = kDeviceBase + 0x1000;
inline constexpr std::uint32_t kConsoleBase = kDeviceBase + 0x2000;
inline constexpr std::uint32_t kIrqRouterBase = kDeviceBase + 0x3000;

class Device {
public:
    virtual ~Device() = default;
    virtual bool read(std::uint32_t offset, std::uint32_t& value) = 0;
    virtual bool write(std::uint32_t offset, std::uint32_t value) = 0;
    virtual void tick() {}
    virtual void reset() {}
};

class TimerDevice final : public Device {
public:
    static constexpr std::uint32_t Control = 0x00;
    static constexpr std::uint32_t Reload = 0x04;
    static constexpr std::uint32_t Count = 0x08;
    static constexpr std::uint32_t Status = 0x0C;
    static constexpr std::uint32_t Irq = 0x10;

    bool read(std::uint32_t offset, std::uint32_t& value) override;
    bool write(std::uint32_t offset, std::uint32_t value) override;
    void tick() override;
    void reset() override;

    [[nodiscard]] bool pending() const { return pending_; }
    void set_irq_handler(std::function<void(std::uint32_t)> handler) {
        irq_handler_ = std::move(handler);
    }

private:
    bool enabled_ = false;
    bool periodic_ = false;
    bool irq_enabled_ = false;
    std::uint32_t reload_ = 1;
    std::uint32_t count_ = 1;
    std::uint32_t irq_line_ = 0;
    bool pending_ = false;
    std::function<void(std::uint32_t)> irq_handler_;
};

class BlockDevice final : public Device {
public:
    static constexpr std::uint32_t Address = 0x00;
    static constexpr std::uint32_t Count = 0x04;
    static constexpr std::uint32_t Command = 0x08;
    static constexpr std::uint32_t Status = 0x0C;
    static constexpr std::uint32_t Irq = 0x10;
    static constexpr std::uint32_t Data = 0x14;

    explicit BlockDevice(std::size_t size = 4096);

    void attach_machine(Machine& machine) { machine_ = &machine; }
    bool read(std::uint32_t offset, std::uint32_t& value) override;
    bool write(std::uint32_t offset, std::uint32_t value) override;
    void reset() override;

    void set_irq_handler(std::function<void(std::uint32_t)> handler) {
        irq_handler_ = std::move(handler);
    }
    [[nodiscard]] const std::vector<std::uint8_t>& storage() const { return storage_; }
    [[nodiscard]] std::uint32_t status() const { return status_; }

private:
    void start_dma();

    std::vector<std::uint8_t> storage_;
    Machine* machine_ = nullptr;
    std::uint32_t address_ = 0;
    std::uint32_t count_ = 0;
    std::uint32_t command_ = 0;
    std::uint32_t status_ = 0;
    std::uint32_t irq_line_ = 0;
    bool irq_enabled_ = false;
    std::function<void(std::uint32_t)> irq_handler_;
};

class ConsoleDevice final : public Device {
public:
    static constexpr std::uint32_t Data = 0x00;
    static constexpr std::uint32_t Status = 0x04;

    bool read(std::uint32_t offset, std::uint32_t& value) override;
    bool write(std::uint32_t offset, std::uint32_t value) override;
    void reset() override;

    [[nodiscard]] const std::string& output() const { return output_; }
    void clear() { output_.clear(); }

private:
    std::string output_;
};

class IrqRouter {
public:
    IrqRouter();
    [[nodiscard]] bool read(std::uint32_t offset, std::uint32_t& value) const;
    [[nodiscard]] bool write(std::uint32_t offset, std::uint32_t value);
    [[nodiscard]] std::uint32_t route(std::uint32_t line) const;
    void reset();

private:
    std::array<std::uint32_t, 16> routes_{};
};

class DeviceBus {
public:
    DeviceBus();

    void attach_machine(Machine& machine);
    void set_irq_handler(std::function<void(std::uint32_t)> handler);
    [[nodiscard]] bool has_irq_handler() const { return static_cast<bool>(irq_handler_); }
    [[nodiscard]] bool contains(std::uint32_t address) const;
    [[nodiscard]] bool read32(std::uint32_t address, std::uint32_t& value);
    [[nodiscard]] bool write32(std::uint32_t address, std::uint32_t value);

    void tick();
    void reset();

    [[nodiscard]] TimerDevice& timer() { return timer_; }
    [[nodiscard]] BlockDevice& block() { return block_; }
    [[nodiscard]] ConsoleDevice& console() { return console_; }
    [[nodiscard]] IrqRouter& irq_router() { return irq_router_; }

    [[nodiscard]] std::uint32_t raise_count() const { return raise_count_; }

private:
    void raise(std::uint32_t line);

    TimerDevice timer_;
    BlockDevice block_;
    ConsoleDevice console_;
    IrqRouter irq_router_;
    std::function<void(std::uint32_t)> irq_handler_;
    std::uint32_t raise_count_ = 0;
};

} // namespace kr32::core
