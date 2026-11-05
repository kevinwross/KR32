#pragma once

#include <string>

#include "kr32/ui/canvas.h"

namespace kr32::platform {

namespace key {
inline constexpr int Escape = 256;
inline constexpr int Tab = 257;
inline constexpr int F5 = 258;
inline constexpr int F6 = 268;
inline constexpr int F9 = 259;
inline constexpr int F10 = 260;
inline constexpr int F11 = 261;
inline constexpr int Left = 262;
inline constexpr int Right = 263;
inline constexpr int Up = 264;
inline constexpr int Down = 265;
inline constexpr int Enter = 266;
inline constexpr int Backspace = 267;
} // namespace key

struct Event {
    enum class Kind { None, Quit, Resize, KeyDown, MouseDown, MouseUp, MouseMove, Wheel };
    Kind kind = Kind::None;
    int key = 0;
    int button = 0;
    bool shift = false;
    bool control = false;
    int x = 0;
    int y = 0;
    int wheel = 0;
};

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
    [[nodiscard]] bool valid() const { return window_ != nullptr; }

private:
    void* window_ = nullptr;
    void* renderer_ = nullptr;
    void* texture_ = nullptr;
    int width_ = 0;
    int height_ = 0;
};

// Initializes and tears down the SDL2 platform. Used by `kr32 --smoke-ui`.
[[nodiscard]] bool smoke_test();

// Shows the platform file picker. Returns false when canceled or unavailable.
[[nodiscard]] bool open_file_dialog(std::string& path);

// Returns the host clipboard as UTF-8 text, or an empty string.
[[nodiscard]] std::string clipboard_text();

} // namespace kr32::platform
