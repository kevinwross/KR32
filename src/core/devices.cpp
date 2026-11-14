#include "kr32/core/devices.h"

#include <algorithm>
#include <utility>

#include "kr32/core/font10x20.h"
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

namespace {

constexpr std::uint32_t kTerminalPalette[16] = {
    0x00101820U, 0x00A03030U, 0x0030A030U, 0x00A0A030U,
    0x003040A0U, 0x00A030A0U, 0x0030A0A0U, 0x00E6EDF3U,
    0x00506070U, 0x00F06060U, 0x0060F060U, 0x00F0F060U,
    0x006080F0U, 0x00F060F0U, 0x0060F0F0U, 0x00FFFFFFU,
};

} // namespace

TerminalDevice::TerminalDevice() {
    reset();
}

void TerminalDevice::attach(FramebufferDevice* framebuffer) {
    framebuffer_ = framebuffer;
    columns_ = 0;
    rows_ = 0;
    sync_geometry();
}

void TerminalDevice::reset() {
    cursor_x_ = 0;
    cursor_y_ = 0;
    cursor_drawn_x_ = -1;
    cursor_drawn_y_ = -1;
    control_ = ControlCursor;
    color_ = 0x07;
    scrollback_head_ = 0;
    scrollback_count_ = 0;
    columns_ = 0;
    rows_ = 0;
    sync_geometry();
    if (framebuffer_ == nullptr) {
        columns_ = 80;
        rows_ = 30;
        chars_.assign(static_cast<std::size_t>(columns_) * rows_, ' ');
        attributes_.assign(static_cast<std::size_t>(columns_) * rows_,
                           static_cast<std::uint8_t>(color_));
    }
}

void TerminalDevice::sync_geometry() {
    int columns = 80;
    int rows = 30;
    if (framebuffer_ != nullptr) {
        const std::uint32_t width = framebuffer_->width();
        const std::uint32_t height = framebuffer_->height();
        if (width >= kTerminalCellWidth && height >= kTerminalCellHeight) {
            columns = static_cast<int>(width / kTerminalCellWidth);
            rows = static_cast<int>(height / kTerminalCellHeight);
        }
    }
    columns = std::clamp(columns, 1, 200);
    rows = std::clamp(rows, 1, 100);
    if (columns == columns_ && rows == rows_) {
        return;
    }
    columns_ = columns;
    rows_ = rows;
    chars_.assign(static_cast<std::size_t>(columns_) * rows_, ' ');
    attributes_.assign(static_cast<std::size_t>(columns_) * rows_,
                       static_cast<std::uint8_t>(color_));
    scrollback_.assign(ScrollbackLines * static_cast<std::size_t>(columns_), ' ');
    scrollback_head_ = 0;
    scrollback_count_ = 0;
    cursor_x_ = 0;
    cursor_y_ = 0;
    cursor_drawn_x_ = -1;
    cursor_drawn_y_ = -1;
    render_all();
}

bool TerminalDevice::read(std::uint32_t offset, std::uint32_t& value) {
    switch (offset) {
    case Status:
        value = StatusReady | (static_cast<std::uint32_t>(columns_) << 8) |
                (static_cast<std::uint32_t>(rows_) << 16);
        return true;
    case Control: value = control_; return true;
    case Color: value = color_; return true;
    case Id: value = IdValue; return true;
    case Data:
    case Command:
    default:
        value = 0;
        return true;
    }
}

bool TerminalDevice::write(std::uint32_t offset, std::uint32_t value) {
    switch (offset) {
    case Data:
        putc(static_cast<char>(value & 0xFF));
        return true;
    case Control: {
        const std::uint32_t next = value & (ControlCursor | ControlHidden);
        const std::uint32_t previous = control_;
        // Remove the cursor while the terminal is still visible, so hiding does
        // not leave the block painted on the surface.
        if ((next & ControlHidden) != 0 && (previous & ControlHidden) == 0) {
            erase_cursor();
        }
        control_ = next;
        if ((previous & ControlHidden) != 0 && (control_ & ControlHidden) == 0) {
            cursor_drawn_x_ = -1;
            cursor_drawn_y_ = -1;
            render_all();
            draw_cursor();
        } else if ((previous & ControlCursor) != 0 && (control_ & ControlCursor) == 0) {
            erase_cursor();
        } else if ((previous & ControlCursor) == 0 && (control_ & ControlCursor) != 0) {
            draw_cursor();
        }
        return true;
    }
    case Command: {
        const std::uint32_t command = value & 0xFF;
        if (command == CommandClear) {
            clear();
        } else if (command == CommandReset) {
            control_ = ControlCursor;
            color_ = 0x07;
            sync_geometry();
            clear();
        }
        return true;
    }
    case Color:
        color_ = value & 0xFF;
        return true;
    default:
        return true;
    }
}

char TerminalDevice::cell(int column, int row) const {
    if (column < 0 || column >= columns_ || row < 0 || row >= rows_) {
        return ' ';
    }
    return chars_[static_cast<std::size_t>(row) * columns_ + column];
}

std::uint8_t TerminalDevice::attribute(int column, int row) const {
    if (column < 0 || column >= columns_ || row < 0 || row >= rows_) {
        return 0;
    }
    return attributes_[static_cast<std::size_t>(row) * columns_ + column];
}

void TerminalDevice::clear() {
    chars_.assign(static_cast<std::size_t>(columns_) * rows_, ' ');
    attributes_.assign(static_cast<std::size_t>(columns_) * rows_,
                       static_cast<std::uint8_t>(color_));
    cursor_x_ = 0;
    cursor_y_ = 0;
    cursor_drawn_x_ = -1;
    cursor_drawn_y_ = -1;
    render_all();
    draw_cursor();
}

void TerminalDevice::newline() {
    cursor_x_ = 0;
    if (cursor_y_ + 1 >= rows_) {
        cursor_y_ = rows_ - 1;
        scroll_up();
    } else {
        ++cursor_y_;
    }
}

void TerminalDevice::scroll_up() {
    erase_cursor();
    if (columns_ > 0 && rows_ > 0) {
        std::copy_n(chars_.begin(), columns_,
                    scrollback_.begin() + static_cast<std::ptrdiff_t>(scrollback_head_) *
                                              columns_);
        scrollback_head_ = (scrollback_head_ + 1) % static_cast<int>(ScrollbackLines);
        if (scrollback_count_ < static_cast<int>(ScrollbackLines)) {
            ++scrollback_count_;
        }
    }
    for (int row = 1; row < rows_; ++row) {
        std::copy_n(chars_.begin() + static_cast<std::ptrdiff_t>(row) * columns_,
                    columns_,
                    chars_.begin() + static_cast<std::ptrdiff_t>(row - 1) * columns_);
        std::copy_n(attributes_.begin() + static_cast<std::ptrdiff_t>(row) * columns_,
                    columns_,
                    attributes_.begin() +
                        static_cast<std::ptrdiff_t>(row - 1) * columns_);
    }
    std::fill_n(chars_.begin() + static_cast<std::ptrdiff_t>(rows_ - 1) * columns_,
                columns_, ' ');
    std::fill_n(attributes_.begin() +
                    static_cast<std::ptrdiff_t>(rows_ - 1) * columns_,
                columns_, static_cast<std::uint8_t>(color_));
    cursor_drawn_x_ = -1;
    cursor_drawn_y_ = -1;
    render_all();
}

void TerminalDevice::putc(char character) {
    if (character == '\n') {
        erase_cursor();
        newline();
        draw_cursor();
        return;
    }
    if (character == '\r') {
        erase_cursor();
        cursor_x_ = 0;
        draw_cursor();
        return;
    }
    if (character == '\b') {
        erase_cursor();
        if (cursor_x_ > 0) {
            --cursor_x_;
        } else if (cursor_y_ > 0) {
            --cursor_y_;
            cursor_x_ = columns_ - 1;
        }
        draw_cursor();
        return;
    }
    if (character == '\t') {
        erase_cursor();
        const int next = (cursor_x_ / 8 + 1) * 8;
        if (next >= columns_) {
            cursor_x_ = 0;
            newline();
        } else {
            cursor_x_ = next;
        }
        draw_cursor();
        return;
    }
    if (character == '\f') {
        clear();
        return;
    }
    if (character < 0x20 || character > 0x7E) {
        return;
    }
    erase_cursor();
    write_cell(cursor_x_, cursor_y_, character);
    ++cursor_x_;
    if (cursor_x_ >= columns_) {
        newline();
    }
    draw_cursor();
}

void TerminalDevice::write_cell(int column, int row, char character) {
    if (column < 0 || column >= columns_ || row < 0 || row >= rows_) {
        return;
    }
    chars_[static_cast<std::size_t>(row) * columns_ + column] = character;
    attributes_[static_cast<std::size_t>(row) * columns_ + column] =
        static_cast<std::uint8_t>((color_ & 0x0FU) | ((color_ >> 4) << 4));
    render_cell(column, row);
}

void TerminalDevice::render_cell(int column, int row, bool inverse) {
    if (framebuffer_ == nullptr || hidden() || !framebuffer_->enabled()) {
        return;
    }
    if (column < 0 || column >= columns_ || row < 0 || row >= rows_) {
        return;
    }
    const std::size_t index = static_cast<std::size_t>(row) * columns_ + column;
    const unsigned char character = static_cast<unsigned char>(chars_[index]);
    int glyph = 0;
    if (character >= 0x20 && character <= 0x7E) {
        glyph = character - kFont10x20First;
    } else if (character != ' ') {
        glyph = '?' - kFont10x20First;
    }
    const std::uint8_t attribute_value = attributes_[index];
    std::uint32_t foreground = kTerminalPalette[attribute_value & 0x0FU];
    std::uint32_t background = kTerminalPalette[(attribute_value >> 4) & 0x0FU];
    if (inverse) {
        std::swap(foreground, background);
    }
    const int origin_x = column * static_cast<int>(kTerminalCellWidth);
    const int origin_y = row * static_cast<int>(kTerminalCellHeight);
    const std::uint16_t* rows = &kFont10x20[glyph * kFont10x20Height];
    for (int line = 0; line < kFont10x20Height; ++line) {
        const std::uint16_t bits = rows[line];
        for (int pixel = 0; pixel < kFont10x20Width; ++pixel) {
            const bool set = (bits & (1U << (kFont10x20Width - 1 - pixel))) != 0;
            paint(origin_x + pixel, origin_y + line, set ? foreground : background);
        }
    }
}

void TerminalDevice::render_all() {
    if (framebuffer_ == nullptr || hidden() || !framebuffer_->enabled()) {
        return;
    }
    const std::uint32_t background = kTerminalPalette[(color_ >> 4) & 0x0FU];
    const int width = static_cast<int>(framebuffer_->width());
    const int height = static_cast<int>(framebuffer_->height());
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            paint(x, y, background);
        }
    }
    for (int row = 0; row < rows_; ++row) {
        for (int column = 0; column < columns_; ++column) {
            render_cell(column, row);
        }
    }
    cursor_drawn_x_ = -1;
    cursor_drawn_y_ = -1;
}

void TerminalDevice::draw_cursor() {
    if (hidden() || (control_ & ControlCursor) == 0) {
        return;
    }
    render_cell(cursor_x_, cursor_y_, true);
    cursor_drawn_x_ = cursor_x_;
    cursor_drawn_y_ = cursor_y_;
}

void TerminalDevice::erase_cursor() {
    if (cursor_drawn_x_ < 0 || cursor_drawn_y_ < 0) {
        return;
    }
    render_cell(cursor_drawn_x_, cursor_drawn_y_);
    cursor_drawn_x_ = -1;
    cursor_drawn_y_ = -1;
}

void TerminalDevice::paint(int x, int y, std::uint32_t color) {
    if (framebuffer_ == nullptr) {
        return;
    }
    const std::uint32_t width = framebuffer_->width();
    const std::uint32_t height = framebuffer_->height();
    if (x < 0 || y < 0 || static_cast<std::uint32_t>(x) >= width ||
        static_cast<std::uint32_t>(y) >= height) {
        return;
    }
    std::vector<std::uint8_t>& vram = framebuffer_->vram();
    const std::size_t offset = framebuffer_->back_offset() +
                               static_cast<std::size_t>(y) * framebuffer_->stride() +
                               static_cast<std::size_t>(x) * 4U;
    if (offset + 4U > vram.size()) {
        return;
    }
    vram[offset] = static_cast<std::uint8_t>(color & 0xFFU);
    vram[offset + 1] = static_cast<std::uint8_t>((color >> 8) & 0xFFU);
    vram[offset + 2] = static_cast<std::uint8_t>((color >> 16) & 0xFFU);
    vram[offset + 3] = 0;
}

GraphicsDevice::GraphicsDevice() {
    reset();
}

void GraphicsDevice::attach(FramebufferDevice* framebuffer) {
    framebuffer_ = framebuffer;
}

void GraphicsDevice::reset() {
    arguments_.fill(0);
    command_ = 0;
    clip_x_ = 0;
    clip_y_ = 0;
    clip_width_ = 0;
    clip_height_ = 0;
    foreground_ = 0x00FF'FFFFU;
    background_ = 0;
    command_count_ = 0;
    error_ = false;
}

bool GraphicsDevice::read(std::uint32_t offset, std::uint32_t& value) {
    if (offset >= ArgumentBase && offset < ArgumentBase + 8U * 4U) {
        value = arguments_[(offset - ArgumentBase) / 4U];
        return true;
    }
    switch (offset) {
    case CommandRegister: value = command_; return true;
    case StatusRegister: value = error_ ? StatusError : 0U; return true;
    case IdRegister: value = IdValue; return true;
    default: value = 0; return true;
    }
}

bool GraphicsDevice::write(std::uint32_t offset, std::uint32_t value) {
    if (offset >= ArgumentBase && offset < ArgumentBase + 8U * 4U) {
        arguments_[(offset - ArgumentBase) / 4U] = value;
        return true;
    }
    switch (offset) {
    case CommandRegister:
        command_ = value;
        return true;
    case Doorbell:
        if ((value & 1U) != 0) {
            execute();
        }
        return true;
    case StatusRegister:
        if ((value & StatusError) != 0) {
            error_ = false;
        }
        return true;
    default:
        return true;
    }
}

void GraphicsDevice::execute() {
    ++command_count_;
    const auto op = static_cast<Op>(command_);
    switch (op) {
    case Op::SetClip: {
        if (framebuffer_ == nullptr) {
            error_ = true;
            return;
        }
        const std::uint32_t width = framebuffer_->width();
        const std::uint32_t height = framebuffer_->height();
        const std::uint32_t x = std::min(arguments_[0], width);
        const std::uint32_t y = std::min(arguments_[1], height);
        clip_x_ = x;
        clip_y_ = y;
        clip_width_ = std::min(arguments_[2], width - x);
        clip_height_ = std::min(arguments_[3], height - y);
        return;
    }
    case Op::SetColor:
        foreground_ = arguments_[0] & 0x00FF'FFFFU;
        background_ = arguments_[1] & 0x00FF'FFFFU;
        return;
    case Op::FillRect:
        fill_rect(arguments_[0], arguments_[1], arguments_[2], arguments_[3],
                  arguments_[4]);
        return;
    case Op::CopyRect:
        copy_rect(arguments_[0], arguments_[1], arguments_[2], arguments_[3],
                  arguments_[4]);
        return;
    case Op::DrawLine:
        draw_line(static_cast<int>(arguments_[0]), static_cast<int>(arguments_[1]),
                  static_cast<int>(arguments_[2]), static_cast<int>(arguments_[3]),
                  arguments_[4]);
        return;
    case Op::DrawText:
        draw_text(arguments_[0], arguments_[1], arguments_[2], arguments_[3],
                  arguments_[4], arguments_[5]);
        return;
    case Op::Flip:
        if (framebuffer_ == nullptr || !framebuffer_->flip()) {
            error_ = true;
        }
        return;
    default:
        error_ = true;
        return;
    }
}

void GraphicsDevice::effective_clip(std::uint32_t& x, std::uint32_t& y,
                                    std::uint32_t& width,
                                    std::uint32_t& height) const {
    if (framebuffer_ == nullptr) {
        x = 0;
        y = 0;
        width = 0;
        height = 0;
        return;
    }
    if (clip_width_ == 0 || clip_height_ == 0) {
        x = 0;
        y = 0;
        width = framebuffer_->width();
        height = framebuffer_->height();
        return;
    }
    x = clip_x_;
    y = clip_y_;
    width = clip_width_;
    height = clip_height_;
}

void GraphicsDevice::fill_rect(std::uint32_t x, std::uint32_t y,
                               std::uint32_t width, std::uint32_t height,
                               std::uint32_t color) {
    if (framebuffer_ == nullptr || !framebuffer_->enabled()) {
        error_ = true;
        return;
    }
    const std::uint32_t surface_width = framebuffer_->width();
    const std::uint32_t surface_height = framebuffer_->height();
    std::uint32_t clip_x = 0;
    std::uint32_t clip_y = 0;
    std::uint32_t clip_width = 0;
    std::uint32_t clip_height = 0;
    effective_clip(clip_x, clip_y, clip_width, clip_height);
    const std::uint32_t left = std::max(x, clip_x);
    const std::uint32_t top = std::max(y, clip_y);
    const std::uint32_t right = std::min(x + width, std::min(clip_x + clip_width, surface_width));
    const std::uint32_t bottom = std::min(y + height, std::min(clip_y + clip_height, surface_height));
    if (left >= right || top >= bottom) {
        return;
    }
    std::vector<std::uint8_t>& vram = framebuffer_->vram();
    for (std::uint32_t row = top; row < bottom; ++row) {
        for (std::uint32_t column = left; column < right; ++column) {
            const std::size_t offset = framebuffer_->back_offset() +
                                       static_cast<std::size_t>(row) * framebuffer_->stride() +
                                       static_cast<std::size_t>(column) * 4U;
            if (offset + 4U > vram.size()) {
                error_ = true;
                return;
            }
            vram[offset] = static_cast<std::uint8_t>(color & 0xFFU);
            vram[offset + 1] = static_cast<std::uint8_t>((color >> 8) & 0xFFU);
            vram[offset + 2] = static_cast<std::uint8_t>((color >> 16) & 0xFFU);
            vram[offset + 3] = 0;
        }
    }
}

void GraphicsDevice::copy_rect(std::uint32_t source, std::uint32_t dx,
                               std::uint32_t dy, std::uint32_t width,
                               std::uint32_t height) {
    if (framebuffer_ == nullptr || !framebuffer_->enabled()) {
        error_ = true;
        return;
    }
    const std::uint32_t stride = framebuffer_->stride();
    std::uint32_t clip_x = 0;
    std::uint32_t clip_y = 0;
    std::uint32_t clip_width = 0;
    std::uint32_t clip_height = 0;
    effective_clip(clip_x, clip_y, clip_width, clip_height);
    const std::uint32_t left = std::max(dx, clip_x);
    const std::uint32_t top = std::max(dy, clip_y);
    const std::uint32_t right = std::min(dx + width, clip_x + clip_width);
    const std::uint32_t bottom = std::min(dy + height, clip_y + clip_height);
    if (left >= right || top >= bottom) {
        return;
    }
    const std::uint64_t needed = static_cast<std::uint64_t>(source) +
                                 static_cast<std::uint64_t>(bottom - dy) * stride;
    if (needed > framebuffer_->vram().size()) {
        error_ = true;
        return;
    }
    std::vector<std::uint8_t> scratch(static_cast<std::size_t>(right - left) * 4U);
    for (std::uint32_t row = top; row < bottom; ++row) {
        const std::size_t from = source + static_cast<std::size_t>(row - dy) * stride +
                                 static_cast<std::size_t>(left - dx) * 4U;
        const std::size_t to = framebuffer_->back_offset() +
                               static_cast<std::size_t>(row) * stride +
                               static_cast<std::size_t>(left) * 4U;
        if (from + scratch.size() > framebuffer_->vram().size() ||
            to + scratch.size() > framebuffer_->vram().size()) {
            error_ = true;
            return;
        }
        std::copy_n(framebuffer_->vram().begin() + static_cast<std::ptrdiff_t>(from),
                    scratch.size(), scratch.begin());
        std::copy(scratch.begin(), scratch.end(),
                  framebuffer_->vram().begin() + static_cast<std::ptrdiff_t>(to));
    }
}

void GraphicsDevice::draw_line(int x0, int y0, int x1, int y1,
                               std::uint32_t color) {
    if (framebuffer_ == nullptr || !framebuffer_->enabled()) {
        error_ = true;
        return;
    }
    std::uint32_t clip_x = 0;
    std::uint32_t clip_y = 0;
    std::uint32_t clip_width = 0;
    std::uint32_t clip_height = 0;
    effective_clip(clip_x, clip_y, clip_width, clip_height);
    const int dx = std::abs(x1 - x0);
    const int dy = -std::abs(y1 - y0);
    const int sx = x0 < x1 ? 1 : -1;
    const int sy = y0 < y1 ? 1 : -1;
    int error = dx + dy;
    while (true) {
        if (x0 >= static_cast<int>(clip_x) &&
            y0 >= static_cast<int>(clip_y) &&
            static_cast<std::uint32_t>(x0) < clip_x + clip_width &&
            static_cast<std::uint32_t>(y0) < clip_y + clip_height) {
            const std::size_t offset = framebuffer_->back_offset() +
                                       static_cast<std::size_t>(y0) * framebuffer_->stride() +
                                       static_cast<std::size_t>(x0) * 4U;
            if (offset + 4U > framebuffer_->vram().size()) {
                error_ = true;
                return;
            }
            framebuffer_->vram()[offset] = static_cast<std::uint8_t>(color & 0xFFU);
            framebuffer_->vram()[offset + 1] = static_cast<std::uint8_t>((color >> 8) & 0xFFU);
            framebuffer_->vram()[offset + 2] = static_cast<std::uint8_t>((color >> 16) & 0xFFU);
            framebuffer_->vram()[offset + 3] = 0;
        }
        if (x0 == x1 && y0 == y1) {
            break;
        }
        const int doubled = 2 * error;
        if (doubled >= dy) {
            error += dy;
            x0 += sx;
        }
        if (doubled <= dx) {
            error += dx;
            y0 += sy;
        }
    }
}

void GraphicsDevice::draw_text(std::uint32_t x, std::uint32_t y,
                               std::uint32_t address, std::uint32_t length,
                               std::uint32_t fg, std::uint32_t bg) {
    if (framebuffer_ == nullptr || !framebuffer_->enabled()) {
        error_ = true;
        return;
    }
    if (address > framebuffer_->vram().size() ||
        framebuffer_->vram().size() - address < length) {
        error_ = true;
        return;
    }
    std::uint32_t clip_x = 0;
    std::uint32_t clip_y = 0;
    std::uint32_t clip_width = 0;
    std::uint32_t clip_height = 0;
    effective_clip(clip_x, clip_y, clip_width, clip_height);
    const std::uint32_t surface_width = framebuffer_->width();
    const std::uint32_t surface_height = framebuffer_->height();
    for (std::uint32_t index = 0; index < length; ++index) {
        const unsigned char character = framebuffer_->vram()[address + index];
        int glyph = 0;
        if (character >= 0x20 && character <= 0x7E) {
            glyph = character - kFont10x20First;
        } else if (character != ' ') {
            glyph = '?' - kFont10x20First;
        }
        const std::uint32_t origin_x = x + index * kTerminalCellWidth;
        for (int line = 0; line < kFont10x20Height; ++line) {
            const std::uint16_t bits = kFont10x20[glyph * kFont10x20Height + line];
            for (int pixel = 0; pixel < kFont10x20Width; ++pixel) {
                const std::uint32_t px = origin_x + static_cast<std::uint32_t>(pixel);
                const std::uint32_t py = y + static_cast<std::uint32_t>(line);
                if (px < clip_x || py < clip_y || px >= clip_x + clip_width ||
                    py >= clip_y + clip_height || px >= surface_width ||
                    py >= surface_height) {
                    continue;
                }
                const bool set = (bits & (1U << (kFont10x20Width - 1 - pixel))) != 0;
                const std::uint32_t color = set ? fg : bg;
                const std::size_t offset = framebuffer_->back_offset() +
                                           static_cast<std::size_t>(py) * framebuffer_->stride() +
                                           static_cast<std::size_t>(px) * 4U;
                if (offset + 4U > framebuffer_->vram().size()) {
                    error_ = true;
                    return;
                }
                framebuffer_->vram()[offset] = static_cast<std::uint8_t>(color & 0xFFU);
                framebuffer_->vram()[offset + 1] = static_cast<std::uint8_t>((color >> 8) & 0xFFU);
                framebuffer_->vram()[offset + 2] = static_cast<std::uint8_t>((color >> 16) & 0xFFU);
                framebuffer_->vram()[offset + 3] = 0;
            }
        }
    }
}

DeviceBus::DeviceBus() {
    timer_.set_irq_handler([this](std::uint32_t line) { raise(line); });
    block_.set_irq_handler([this](std::uint32_t line) { raise(line); });
    keyboard_.set_irq_handler([this](std::uint32_t line) { raise(line); });
    terminal_.attach(&framebuffer_);
    graphics_.attach(&framebuffer_);
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
    if (address >= kTerminalBase && address < kTerminalBase + 0x1000) {
        return terminal_.read(address - kTerminalBase, value);
    }
    if (address >= kGraphicsBase && address < kGraphicsBase + 0x1000) {
        return graphics_.read(address - kGraphicsBase, value);
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
    if (address >= kTerminalBase && address < kTerminalBase + 0x1000) {
        return terminal_.write(address - kTerminalBase, value);
    }
    if (address >= kGraphicsBase && address < kGraphicsBase + 0x1000) {
        return graphics_.write(address - kGraphicsBase, value);
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
    terminal_.reset();
    graphics_.reset();
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
