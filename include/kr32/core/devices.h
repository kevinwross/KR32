#pragma once

#include <array>
#include <cstdint>
#include <deque>
#include <functional>
#include <span>
#include <string>
#include <string_view>
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
inline constexpr std::uint32_t kFramebufferControlBase = kDeviceBase + 0x4000;
inline constexpr std::uint32_t kSerialBase = kDeviceBase + 0x5000;
inline constexpr std::uint32_t kKeyboardBase = kDeviceBase + 0x6000;
// Reserved for the pointer device (display.md 8.5). No device owns this page
// yet; accesses fault until one does.
inline constexpr std::uint32_t kPointerBase = kDeviceBase + 0x7000;

// Dedicated 32 MiB display aperture below the MMIO window.
inline constexpr std::uint32_t kFramebufferBase = 0xFC00'0000;
inline constexpr std::uint32_t kFramebufferSize = 0x0200'0000;
inline constexpr std::uint32_t kFramebufferWidth = 1920;
inline constexpr std::uint32_t kFramebufferHeight = 1080;
inline constexpr std::uint32_t kFramebufferStride = kFramebufferWidth * 4;
inline constexpr std::uint32_t kFramebufferSurfaceBytes =
    kFramebufferWidth * kFramebufferHeight * 4;

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

class SerialDevice final : public Device {
public:
    static constexpr std::uint32_t Data = 0x00;
    static constexpr std::uint32_t Status = 0x04;
    static constexpr std::uint32_t Peek = 0x08;
    static constexpr std::uint32_t RxReady = 1U << 0;
    static constexpr std::uint32_t TxReady = 1U << 1;
    static constexpr std::uint32_t Overrun = 1U << 2;
    static constexpr std::size_t Capacity = 4096;

    [[nodiscard]] bool read(std::uint32_t offset, std::uint32_t& value) override;
    [[nodiscard]] bool write(std::uint32_t offset, std::uint32_t value) override;
    void reset() override;

    [[nodiscard]] bool push_input(std::uint8_t byte);
    void push_input(std::string_view text);
    [[nodiscard]] std::size_t available() const { return rx_.size(); }
    [[nodiscard]] bool peek(std::uint8_t& byte) const;
    [[nodiscard]] std::string take_output();
    [[nodiscard]] const std::string& output() const { return output_; }
    [[nodiscard]] bool overrun() const { return overrun_; }

private:
    std::deque<std::uint8_t> rx_;
    std::string output_;
    bool overrun_ = false;
};

class KeyboardDevice final : public Device {
public:
    static constexpr std::uint32_t Data = 0x00;
    static constexpr std::uint32_t Status = 0x04;
    static constexpr std::uint32_t Control = 0x08;
    static constexpr std::uint32_t Irq = 0x0C;

    static constexpr std::uint32_t StatusAvailable = 1U << 0;
    static constexpr std::uint32_t StatusOverrun = 1U << 1;
    static constexpr std::size_t Capacity = 64;

    // ASCII in bits 0..7, modifiers 8..15, scancode 16..23, type 24..31.
    static constexpr std::uint32_t pack_event(std::uint8_t ascii,
                                              std::uint8_t modifiers,
                                              std::uint8_t scancode,
                                              std::uint8_t type) {
        return static_cast<std::uint32_t>(ascii) |
               (static_cast<std::uint32_t>(modifiers) << 8) |
               (static_cast<std::uint32_t>(scancode) << 16) |
               (static_cast<std::uint32_t>(type) << 24);
    }

    bool read(std::uint32_t offset, std::uint32_t& value) override;
    bool write(std::uint32_t offset, std::uint32_t value) override;
    void reset() override;

    // Drops the event and sets OVERRUN when the FIFO is full.
    [[nodiscard]] bool push_event(std::uint32_t word);
    void set_irq_handler(std::function<void(std::uint32_t)> handler) {
        irq_handler_ = std::move(handler);
    }
    [[nodiscard]] std::size_t size() const { return fifo_.size(); }
    [[nodiscard]] bool overrun() const { return overrun_; }

private:
    void update_irq();

    std::deque<std::uint32_t> fifo_;
    bool overrun_ = false;
    bool irq_enabled_ = false;
    std::uint32_t irq_line_ = 0;
    std::function<void(std::uint32_t)> irq_handler_;
};

class FramebufferDevice {
public:
    static constexpr std::uint32_t Control = 0x00;
    static constexpr std::uint32_t Width = 0x04;
    static constexpr std::uint32_t Height = 0x08;
    static constexpr std::uint32_t Stride = 0x0C;
    static constexpr std::uint32_t Format = 0x10;
    static constexpr std::uint32_t Front = 0x14;
    static constexpr std::uint32_t Back = 0x18;
    static constexpr std::uint32_t Status = 0x1C;
    static constexpr std::uint32_t Flip = 0x20;
    static constexpr std::uint32_t Frame = 0x24;
    static constexpr std::uint32_t Id = 0x28;

    // Compared as a 32-bit word, never as the byte string "FBR1": on a
    // little-endian machine the bytes in memory read 31 52 42 46 ("1RBF").
    static constexpr std::uint32_t IdValue = 0x4642'5231U;
    static constexpr std::uint32_t StatusEnabled = 1U << 0;
    static constexpr std::uint32_t StatusModeOk = 1U << 1;
    static constexpr std::uint32_t StatusFlipError = 1U << 2;

    // Snapshot of the last successful FLIP. Offsets are into VRAM.
    struct CommittedFrame {
        bool enabled = false;
        std::uint32_t offset = 0;
        std::uint32_t width = kFramebufferWidth;
        std::uint32_t height = kFramebufferHeight;
        std::uint32_t stride = kFramebufferStride;
        std::uint32_t format = 1;
        std::uint32_t number = 0;
    };

    FramebufferDevice();

    // The control page never faults: unknown offsets read zero and ignore
    // writes, and an out-of-range write leaves the register unchanged.
    [[nodiscard]] bool read_control(std::uint32_t offset, std::uint32_t& value) const;
    [[nodiscard]] bool write_control(std::uint32_t offset, std::uint32_t value);
    [[nodiscard]] bool read_pixel(std::uint32_t offset, std::uint32_t& value) const;
    [[nodiscard]] bool write_pixel(std::uint32_t offset, std::uint32_t value);
    void reset();

    // Commits the current register bundle: the surface that was BACK becomes
    // the displayed frame, FRONT and BACK swap, and FRAME increments. A
    // rejected bundle sets STATUS.FLIP_ERROR and touches nothing else.
    [[nodiscard]] bool flip();

    [[nodiscard]] const CommittedFrame& frame() const { return frame_; }
    [[nodiscard]] std::span<const std::uint8_t> frame_pixels() const;
    [[nodiscard]] std::uint32_t status() const;

    [[nodiscard]] const std::vector<std::uint8_t>& vram() const { return vram_; }
    [[nodiscard]] std::vector<std::uint8_t>& vram() { return vram_; }
    [[nodiscard]] std::uint32_t width() const { return width_; }
    [[nodiscard]] std::uint32_t height() const { return height_; }
    [[nodiscard]] std::uint32_t stride() const { return stride_; }
    [[nodiscard]] bool enabled() const { return (control_ & 1U) != 0; }
    [[nodiscard]] std::uint32_t frame_number() const { return frame_.number; }
    [[nodiscard]] bool flip_error() const { return flip_error_; }

    // Test seam for the 2^32 frame-counter wrap.
    void set_frame_number(std::uint32_t value) { frame_.number = value; }

private:
    [[nodiscard]] bool mode_ok() const;

    std::vector<std::uint8_t> vram_;
    std::uint32_t control_ = 0;
    std::uint32_t width_ = kFramebufferWidth;
    std::uint32_t height_ = kFramebufferHeight;
    std::uint32_t stride_ = kFramebufferStride;
    std::uint32_t format_ = 1;
    std::uint32_t front_ = 0;
    std::uint32_t back_ = kFramebufferSurfaceBytes;
    bool flip_error_ = false;
    CommittedFrame frame_{};
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
    [[nodiscard]] SerialDevice& serial() { return serial_; }
    [[nodiscard]] const SerialDevice& serial() const { return serial_; }
    [[nodiscard]] KeyboardDevice& keyboard() { return keyboard_; }
    [[nodiscard]] const KeyboardDevice& keyboard() const { return keyboard_; }
    [[nodiscard]] FramebufferDevice& framebuffer() { return framebuffer_; }
    [[nodiscard]] const FramebufferDevice& framebuffer() const { return framebuffer_; }
    [[nodiscard]] IrqRouter& irq_router() { return irq_router_; }

    [[nodiscard]] std::uint32_t raise_count() const { return raise_count_; }

private:
    void raise(std::uint32_t line);

    TimerDevice timer_;
    BlockDevice block_;
    ConsoleDevice console_;
    SerialDevice serial_;
    KeyboardDevice keyboard_;
    FramebufferDevice framebuffer_;
    IrqRouter irq_router_;
    std::function<void(std::uint32_t)> irq_handler_;
    std::uint32_t raise_count_ = 0;
};

} // namespace kr32::core
