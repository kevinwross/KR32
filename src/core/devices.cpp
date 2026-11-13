#include "kr32/core/devices.h"

#include <algorithm>
#include <utility>

#include "kr32/core/machine.h"

namespace kr32::core {

bool TimerDevice::read(std::uint32_t offset, std::uint32_t& value) {
    switch (offset) {
    case Control:
        value = (enabled_ ? 1U : 0U) | (periodic_ ? 2U : 0U) | (irq_enabled_ ? 4U : 0U);
        return true;
    case Reload: value = reload_; return true;
    case Count: value = count_; return true;
    case Status: value = pending_ ? 1U : 0U; return true;
    case Irq: value = irq_line_; return true;
    default: return false;
    }
}

bool TimerDevice::write(std::uint32_t offset, std::uint32_t value) {
    switch (offset) {
    case Control: {
        enabled_ = (value & 1U) != 0;
        periodic_ = (value & 2U) != 0;
        irq_enabled_ = (value & 4U) != 0;
        if (enabled_) {
            count_ = reload_;
        }
        return true;
    }
    case Reload:
        reload_ = std::max(1U, value);
        if (!enabled_) count_ = reload_;
        return true;
    case Count:
        count_ = value;
        return true;
    case Status:
        if ((value & 1U) != 0) pending_ = false;
        return true;
    case Irq:
        irq_line_ = value & 15U;
        return true;
    default: return false;
    }
}

void TimerDevice::tick() {
    if (!enabled_) return;
    if (count_ > 0) {
        --count_;
    }
    if (count_ != 0) return;
    pending_ = true;
    if (irq_enabled_ && irq_handler_) {
        irq_handler_(irq_line_);
    }
    if (periodic_) {
        count_ = reload_;
    } else {
        enabled_ = false;
    }
}

void TimerDevice::reset() {
    enabled_ = false;
    periodic_ = false;
    irq_enabled_ = false;
    reload_ = 1;
    count_ = 1;
    irq_line_ = 0;
    pending_ = false;
}

BlockDevice::BlockDevice(std::size_t size) : storage_(std::max<std::size_t>(size, 16)) {
    for (std::size_t index = 0; index < storage_.size(); ++index) {
        storage_[index] = static_cast<std::uint8_t>(index & 0xFFU);
    }
}

bool BlockDevice::read(std::uint32_t offset, std::uint32_t& value) {
    switch (offset) {
    case Address: value = address_; return true;
    case Count: value = count_; return true;
    case Command: value = command_; return true;
    case Status: value = status_; return true;
    case Irq: value = irq_line_; return true;
    case Data:
        value = storage_.empty() ? 0U : storage_.front();
        return true;
    default: return false;
    }
}

bool BlockDevice::write(std::uint32_t offset, std::uint32_t value) {
    switch (offset) {
    case Address: address_ = value; return true;
    case Count: count_ = value; return true;
    case Command:
        command_ = value;
        irq_enabled_ = (value & 4U) != 0;
        if ((value & 1U) != 0) {
            start_dma();
        }
        return true;
    case Status:
        status_ &= ~(value & 1U);
        return true;
    case Irq: irq_line_ = value & 15U; return true;
    case Data:
        if (!storage_.empty()) storage_.front() = static_cast<std::uint8_t>(value & 0xFFU);
        return true;
    default: return false;
    }
}

void BlockDevice::start_dma() {
    status_ = 1U << 1;
    const bool to_guest = (command_ & 2U) == 0;
    if (machine_ == nullptr || count_ > storage_.size()) {
        status_ = (1U << 1) | (1U << 2);
        return;
    }
    bool ok = true;
    for (std::uint32_t index = 0; index < count_; ++index) {
        if (to_guest) {
            ok = machine_->write_physical8(address_ + index, storage_[index]);
        } else {
            std::uint8_t value = 0;
            ok = machine_->read_physical8(address_ + index, value);
            if (ok) storage_[index] = value;
        }
        if (!ok) break;
    }
    status_ = ok ? 1U : ((1U << 1) | (1U << 2));
    if (ok && irq_enabled_ && irq_handler_) {
        irq_handler_(irq_line_);
    }
}

void BlockDevice::reset() {
    address_ = 0;
    count_ = 0;
    command_ = 0;
    status_ = 0;
    irq_line_ = 0;
    irq_enabled_ = false;
}

bool ConsoleDevice::read(std::uint32_t offset, std::uint32_t& value) {
    switch (offset) {
    case Data:
        value = output_.empty() ? 0U : static_cast<unsigned char>(output_.back());
        return true;
    case Status:
        value = 1;
        return true;
    default: return false;
    }
}

bool ConsoleDevice::write(std::uint32_t offset, std::uint32_t value) {
    if (offset == Data) {
        output_.push_back(static_cast<char>(value & 0xFFU));
        return true;
    }
    if (offset == Status) {
        return true;
    }
    return false;
}

void ConsoleDevice::reset() {
    output_.clear();
}

bool SerialDevice::read(std::uint32_t offset, std::uint32_t& value) {
    switch (offset) {
    case Data:
        if (rx_.empty()) {
            value = 0;
            return true;
        }
        value = rx_.front();
        rx_.pop_front();
        return true;
    case Status:
        value = (rx_.empty() ? 0U : RxReady) | TxReady |
                (overrun_ ? Overrun : 0U);
        return true;
    case Peek:
        value = rx_.empty() ? 0xFFFF'FFFFU : rx_.front();
        return true;
    default:
        return false;
    }
}

bool SerialDevice::write(std::uint32_t offset, std::uint32_t value) {
    switch (offset) {
    case Data:
        output_.push_back(static_cast<char>(value & 0xFFU));
        return true;
    case Status:
        if ((value & Overrun) != 0) {
            overrun_ = false;
        }
        return true;
    default:
        return false;
    }
}

void SerialDevice::reset() {
    rx_.clear();
    output_.clear();
    overrun_ = false;
}

bool SerialDevice::push_input(std::uint8_t byte) {
    if (rx_.size() >= Capacity) {
        overrun_ = true;
        return false;
    }
    rx_.push_back(byte);
    return true;
}

void SerialDevice::push_input(std::string_view text) {
    for (char ch : text) {
        (void)push_input(static_cast<std::uint8_t>(ch));
    }
}

bool SerialDevice::peek(std::uint8_t& byte) const {
    if (rx_.empty()) return false;
    byte = rx_.front();
    return true;
}

std::string SerialDevice::take_output() {
    std::string result;
    result.swap(output_);
    return result;
}

FramebufferDevice::FramebufferDevice()
    : vram_(kFramebufferSize, std::uint8_t{0}) {}

bool FramebufferDevice::read_control(std::uint32_t offset,
                                     std::uint32_t& value) const {
    switch (offset) {
    case Control:
        value = control_;
        return true;
    case Width:
        value = width_;
        return true;
    case Height:
        value = height_;
        return true;
    case Stride:
        value = stride_;
        return true;
    case Format:
        value = format_;
        return true;
    case Front:
        value = front_;
        return true;
    case Back:
        value = back_;
        return true;
    case Status:
        value = status();
        return true;
    case Flip:
        value = 0;
        return true;
    case Frame:
        value = frame_.number;
        return true;
    case Id:
        value = IdValue;
        return true;
    default:
        value = 0;
        return true;
    }
}

bool FramebufferDevice::write_control(std::uint32_t offset, std::uint32_t value) {
    switch (offset) {
    case Control:
        control_ = value & 1U;
        break;
    case Width:
        if (value >= 1U && value <= kFramebufferWidth) {
            width_ = value;
            stride_ = value * 4U;
        }
        break;
    case Height:
        if (value >= 1U && value <= kFramebufferHeight) {
            height_ = value;
        }
        break;
    case Stride:
        if (value >= width_ * 4U && value <= kFramebufferStride && (value & 3U) == 0) {
            stride_ = value;
        }
        break;
    case Format:
        if (value == 1U) {
            format_ = value;
        }
        break;
    case Front:
    case Back:
        if ((value & 3U) == 0 && value < kFramebufferSize) {
            if (offset == Front) {
                front_ = value;
            } else {
                back_ = value;
            }
        }
        break;
    case Status:
        if ((value & StatusFlipError) != 0) {
            flip_error_ = false;
        }
        break;
    case Flip:
        if ((value & 1U) != 0) {
            (void)flip();
        }
        break;
    default:
        break;
    }
    return true;
}

bool FramebufferDevice::read_pixel(std::uint32_t offset,
                                   std::uint32_t& value) const {
    if (offset > vram_.size() || vram_.size() - offset < 4) {
        return false;
    }
    value = static_cast<std::uint32_t>(vram_[offset]) |
            (static_cast<std::uint32_t>(vram_[offset + 1]) << 8) |
            (static_cast<std::uint32_t>(vram_[offset + 2]) << 16) |
            (static_cast<std::uint32_t>(vram_[offset + 3]) << 24);
    return true;
}

bool FramebufferDevice::write_pixel(std::uint32_t offset, std::uint32_t value) {
    if (offset > vram_.size() || vram_.size() - offset < 4) {
        return false;
    }
    vram_[offset] = static_cast<std::uint8_t>(value & 0xFFU);
    vram_[offset + 1] = static_cast<std::uint8_t>((value >> 8) & 0xFFU);
    vram_[offset + 2] = static_cast<std::uint8_t>((value >> 16) & 0xFFU);
    vram_[offset + 3] = static_cast<std::uint8_t>((value >> 24) & 0xFFU);
    return true;
}

void FramebufferDevice::reset() {
    std::fill(vram_.begin(), vram_.end(), std::uint8_t{0});
    control_ = 0;
    width_ = kFramebufferWidth;
    height_ = kFramebufferHeight;
    stride_ = kFramebufferStride;
    format_ = 1;
    front_ = 0;
    back_ = kFramebufferSurfaceBytes;
    flip_error_ = false;
    frame_ = CommittedFrame{};
}

bool FramebufferDevice::mode_ok() const {
    if (width_ < 1U || width_ > kFramebufferWidth) return false;
    if (height_ < 1U || height_ > kFramebufferHeight) return false;
    if (format_ != 1U) return false;
    if (stride_ < width_ * 4U || stride_ > kFramebufferStride) return false;
    if ((stride_ & 3U) != 0) return false;
    if ((front_ & 3U) != 0 || (back_ & 3U) != 0) return false;

    const std::uint64_t bytes =
        static_cast<std::uint64_t>(height_) * static_cast<std::uint64_t>(stride_);
    return static_cast<std::uint64_t>(front_) + bytes <= kFramebufferSize &&
           static_cast<std::uint64_t>(back_) + bytes <= kFramebufferSize;
}

bool FramebufferDevice::flip() {
    if (!mode_ok()) {
        flip_error_ = true;
        return false;
    }
    frame_ = CommittedFrame{(control_ & 1U) != 0, back_, width_, height_, stride_,
                            format_, frame_.number + 1U};
    std::swap(front_, back_);
    flip_error_ = false;
    return true;
}

std::uint32_t FramebufferDevice::status() const {
    std::uint32_t value = 0;
    if (frame_.enabled) value |= StatusEnabled;
    if (mode_ok()) value |= StatusModeOk;
    if (flip_error_) value |= StatusFlipError;
    return value;
}

std::span<const std::uint8_t> FramebufferDevice::frame_pixels() const {
    const std::size_t offset = frame_.offset;
    const std::size_t length =
        static_cast<std::size_t>(frame_.height) * frame_.stride;
    if (offset > vram_.size() || vram_.size() - offset < length) {
        return {};
    }
    return std::span<const std::uint8_t>(vram_.data() + offset, length);
}

bool KeyboardDevice::read(std::uint32_t offset, std::uint32_t& value) {
    switch (offset) {
    case Data:
        if (fifo_.empty()) {
            value = 0;
            return true;
        }
        value = fifo_.front();
        fifo_.pop_front();
        return true;
    case Status: {
        std::uint32_t status = 0;
        if (!fifo_.empty()) status |= StatusAvailable;
        if (overrun_) status |= StatusOverrun;
        status |= static_cast<std::uint32_t>(fifo_.size() & 0xFFU) << 8;
        value = status;
        return true;
    }
    case Control:
        value = irq_enabled_ ? 1U : 0U;
        return true;
    case Irq:
        value = irq_line_;
        return true;
    default:
        value = 0;
        return true;
    }
}

bool KeyboardDevice::write(std::uint32_t offset, std::uint32_t value) {
    switch (offset) {
    case Status:
        if ((value & StatusOverrun) != 0) {
            overrun_ = false;
        }
        break;
    case Control:
        irq_enabled_ = (value & 1U) != 0;
        update_irq();
        break;
    case Irq:
        if (value <= 15U) {
            irq_line_ = value;
        }
        break;
    default:
        break;
    }
    return true;
}

bool KeyboardDevice::push_event(std::uint32_t word) {
    if (fifo_.size() >= Capacity) {
        overrun_ = true;
        return false;
    }
    fifo_.push_back(word);
    update_irq();
    return true;
}

void KeyboardDevice::reset() {
    fifo_.clear();
    overrun_ = false;
    irq_enabled_ = false;
    irq_line_ = 0;
}

void KeyboardDevice::update_irq() {
    if (irq_enabled_ && !fifo_.empty() && irq_handler_) {
        irq_handler_(irq_line_);
    }
}

IrqRouter::IrqRouter() {
    reset();
}

void IrqRouter::reset() {
    routes_.fill(1U);
}

bool IrqRouter::read(std::uint32_t offset, std::uint32_t& value) const {
    if ((offset & 3U) != 0 || offset / 4 >= routes_.size()) return false;
    value = routes_[offset / 4];
    return true;
}

bool IrqRouter::write(std::uint32_t offset, std::uint32_t value) {
    if ((offset & 3U) != 0 || offset / 4 >= routes_.size()) return false;
    routes_[offset / 4] = value;
    return true;
}

std::uint32_t IrqRouter::route(std::uint32_t line) const {
    return line < routes_.size() ? routes_[line] : 0;
}

DeviceBus::DeviceBus() {
    timer_.set_irq_handler([this](std::uint32_t line) { raise(line); });
    block_.set_irq_handler([this](std::uint32_t line) { raise(line); });
    keyboard_.set_irq_handler([this](std::uint32_t line) { raise(line); });
}

void DeviceBus::attach_machine(Machine& machine) {
    block_.attach_machine(machine);
}

void DeviceBus::set_irq_handler(std::function<void(std::uint32_t)> handler) {
    irq_handler_ = std::move(handler);
}

bool DeviceBus::contains(std::uint32_t address) const {
    if (address >= kFramebufferBase && address < kFramebufferBase + kFramebufferSize) {
        return true;
    }
    return address >= kDeviceBase && address < kDeviceBase + kDeviceWindow;
}

bool DeviceBus::read32(std::uint32_t address, std::uint32_t& value) {
    if (address >= kFramebufferBase && address < kFramebufferBase + kFramebufferSize) {
        return framebuffer_.read_pixel(address - kFramebufferBase, value);
    }
    if (!contains(address)) return false;
    if (address >= kTimerBase && address < kTimerBase + 0x1000) {
        return timer_.read(address - kTimerBase, value);
    }
    if (address >= kBlockBase && address < kBlockBase + 0x1000) {
        return block_.read(address - kBlockBase, value);
    }
    if (address >= kConsoleBase && address < kConsoleBase + 0x1000) {
        return console_.read(address - kConsoleBase, value);
    }
    if (address >= kIrqRouterBase && address < kIrqRouterBase + 0x1000) {
        return irq_router_.read(address - kIrqRouterBase, value);
    }
    if (address >= kSerialBase && address < kSerialBase + 0x1000) {
        return serial_.read(address - kSerialBase, value);
    }
    if (address >= kKeyboardBase && address < kKeyboardBase + 0x1000) {
        return keyboard_.read(address - kKeyboardBase, value);
    }
    if (address >= kFramebufferControlBase && address < kFramebufferControlBase + 0x1000) {
        return framebuffer_.read_control(address - kFramebufferControlBase, value);
    }
    return false;
}

bool DeviceBus::write32(std::uint32_t address, std::uint32_t value) {
    if (address >= kFramebufferBase && address < kFramebufferBase + kFramebufferSize) {
        return framebuffer_.write_pixel(address - kFramebufferBase, value);
    }
    if (!contains(address)) return false;
    if (address >= kTimerBase && address < kTimerBase + 0x1000) {
        return timer_.write(address - kTimerBase, value);
    }
    if (address >= kBlockBase && address < kBlockBase + 0x1000) {
        return block_.write(address - kBlockBase, value);
    }
    if (address >= kConsoleBase && address < kConsoleBase + 0x1000) {
        return console_.write(address - kConsoleBase, value);
    }
    if (address >= kIrqRouterBase && address < kIrqRouterBase + 0x1000) {
        return irq_router_.write(address - kIrqRouterBase, value);
    }
    if (address >= kSerialBase && address < kSerialBase + 0x1000) {
        return serial_.write(address - kSerialBase, value);
    }
    if (address >= kKeyboardBase && address < kKeyboardBase + 0x1000) {
        return keyboard_.write(address - kKeyboardBase, value);
    }
    if (address >= kFramebufferControlBase && address < kFramebufferControlBase + 0x1000) {
        return framebuffer_.write_control(address - kFramebufferControlBase, value);
    }
    return false;
}

void DeviceBus::tick() {
    timer_.tick();
}

void DeviceBus::reset() {
    timer_.reset();
    block_.reset();
    console_.reset();
    serial_.reset();
    keyboard_.reset();
    framebuffer_.reset();
    irq_router_.reset();
    raise_count_ = 0;
}

void DeviceBus::raise(std::uint32_t line) {
    ++raise_count_;
    if (irq_handler_) {
        irq_handler_(line);
    }
}

} // namespace kr32::core
