#pragma once

#include <cstdint>
#include <string>

#include "kr32/file_dialog.h"
#include "kr32/ui/canvas.h"

namespace kr32::platform {

namespace key {
inline constexpr int Escape = 256;
inline constexpr int Tab = 257;
inline constexpr int F5 = 258;
inline constexpr int F6 = 268;
inline constexpr int F2 = 269;
inline constexpr int F9 = 259;inline constexpr int F10 = 260;
inline constexpr int F11 = 261;
inline constexpr int Left = 262;
inline constexpr int Right = 263;
inline constexpr int Up = 264;
inline constexpr int Down = 265;
inline constexpr int Enter = 266;
inline constexpr int Backspace = 267;
} // namespace key

struct Event {
    enum class Kind { None, Quit, Resize, KeyDown, KeyUp, MouseDown, MouseUp, MouseMove, Wheel };
    Kind kind = Kind::None;
    int key = 0;
    int button = 0;
    bool shift = false;
    bool control = false;
    int x = 0;
    int y = 0;
    int wheel = 0;
    int scancode = 0;  // KR32 scancode
    int ascii = 0;     // 0 when the key has no character
    int modifiers = 0; // bit 0 shift, 1 control, 2 alt, 3 caps
};

// Packs a key event into the keyboard device word: ASCII in bits 0..7,
// modifiers 8..15, scancode 16..23, type 24..31 (0 down, 1 up).
[[nodiscard]] std::uint32_t event_word(const Event& event);

class Window {
public:
    Window() = default;
    ~Window();
    Window(const Window&) = delete;
    Window& operator=(const Window&) = delete;

    [[nodiscard]] bool create(const std::string& title, int width, int height);
    void destroy();
    [[nodiscard]] bool poll(Event& event);
    void present(const ui::Canvas& canvas);
    // Presents a raw XRGB8888 surface (the guest framebuffer) with integer
    // zoom, centered. `stride` is in bytes and may exceed width * 4.
    void present_surface(const std::uint8_t* pixels, int width, int height,
                         int stride, int scale);
    [[nodiscard]] bool valid() const { return window_ != nullptr; }

private:
    void* window_ = nullptr;
    void* renderer_ = nullptr;
    void* texture_ = nullptr;
    void* surface_texture_ = nullptr;
    int width_ = 0;
    int height_ = 0;
    int surface_width_ = 0;
    int surface_height_ = 0;
};

// Initializes and tears down the SDL2 platform. Used by `kr32 --smoke-ui`.
[[nodiscard]] bool smoke_test();

// Shows a native open/save file dialog. Returns Selected, Canceled,
// Unavailable (no dialog backend), or Error.
[[nodiscard]] FileDialogResult show_file_dialog(const FileDialogOptions& options);

// Backward-compatible open dialog helper.
[[nodiscard]] bool open_file_dialog(std::string& path);

// Returns the host clipboard as UTF-8 text, or an empty string.
[[nodiscard]] std::string clipboard_text();

} // namespace kr32::platform
