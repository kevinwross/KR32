#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace kr32::ui {

struct Color {
    std::uint8_t r = 0;
    std::uint8_t g = 0;
    std::uint8_t b = 0;
    std::uint8_t a = 255;
};

struct Rect {
    int x = 0;
    int y = 0;
    int width = 0;
    int height = 0;

    [[nodiscard]] bool contains(int px, int py) const {
        return px >= x && py >= y && px < x + width && py < y + height;
    }
};

// Portable software canvas. Pixels are packed as 0xAARRGGBB, matching SDL's
// ARGB8888 texture format.
class Canvas {
public:
    Canvas(int width, int height);

    void clear(Color color);
    void fill(Rect rect, Color color);
    void frame(Rect rect, Color color, int thickness = 1);
    void pixel(int x, int y, Color color);
    void line(int x0, int y0, int x1, int y1, Color color);

    void text(int x, int y, std::string_view value, Color color, int scale = 1);
    [[nodiscard]] static int text_width(std::string_view value, int scale = 1);
    [[nodiscard]] static int text_height(int scale = 1);

    [[nodiscard]] int width() const { return width_; }
    [[nodiscard]] int height() const { return height_; }
    [[nodiscard]] const std::vector<std::uint32_t>& pixels() const { return pixels_; }
    [[nodiscard]] std::uint32_t pixel_at(int x, int y) const;
    [[nodiscard]] std::uint64_t hash() const;

    // Writes a PPM (P6) image without any external image dependency.
    [[nodiscard]] bool write_ppm(const std::string& path) const;

private:
    [[nodiscard]] static std::uint32_t pack(Color color);
    int width_ = 0;
    int height_ = 0;
    std::vector<std::uint32_t> pixels_;
};

} // namespace kr32::ui
