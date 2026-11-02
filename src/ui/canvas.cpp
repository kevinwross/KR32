#include "kr32/ui/canvas.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <string>

namespace kr32::ui {
namespace {

using Glyph = std::array<std::uint8_t, 7>;

Glyph make_glyph(std::string_view spec) {
    Glyph glyph{};
    std::size_t row = 0;
    std::size_t column = 0;
    for (char c : spec) {
        if (c == '/') {
            ++row;
            column = 0;
            continue;
        }
        if (row >= glyph.size() || column >= 5) {
            continue;
        }
        if (c == '1') {
            glyph[row] = static_cast<std::uint8_t>(glyph[row] | (1U << (4 - column)));
        }
        ++column;
    }
    return glyph;
}

Glyph glyph_for(char input) {
    char c = input;
    if (c >= 'a' && c <= 'z') {
        c = static_cast<char>(c - 'a' + 'A');
    }
    switch (c) {
    case ' ': return make_glyph("00000/00000/00000/00000/00000/00000/00000");
    case 'A': return make_glyph("01110/10001/10001/11111/10001/10001/10001");
    case 'B': return make_glyph("11110/10001/10001/11110/10001/10001/11110");
    case 'C': return make_glyph("01111/10000/10000/10000/10000/10000/01111");
    case 'D': return make_glyph("11110/10001/10001/10001/10001/10001/11110");
    case 'E': return make_glyph("11111/10000/10000/11110/10000/10000/11111");
    case 'F': return make_glyph("11111/10000/10000/11110/10000/10000/10000");
    case 'G': return make_glyph("01111/10000/10000/10111/10001/10001/01111");
    case 'H': return make_glyph("10001/10001/10001/11111/10001/10001/10001");
    case 'I': return make_glyph("11111/00100/00100/00100/00100/00100/11111");
    case 'J': return make_glyph("00111/00010/00010/00010/00010/10010/01100");
    case 'K': return make_glyph("10001/10010/10100/11000/10100/10010/10001");
    case 'L': return make_glyph("10000/10000/10000/10000/10000/10000/11111");
    case 'M': return make_glyph("10001/11011/10101/10101/10001/10001/10001");
    case 'N': return make_glyph("10001/11001/10101/10011/10001/10001/10001");
    case 'O': return make_glyph("01110/10001/10001/10001/10001/10001/01110");
    case 'P': return make_glyph("11110/10001/10001/11110/10000/10000/10000");
    case 'Q': return make_glyph("01110/10001/10001/10001/10101/10010/01101");
    case 'R': return make_glyph("11110/10001/10001/11110/10100/10010/10001");
    case 'S': return make_glyph("01111/10000/10000/01110/00001/00001/11110");
    case 'T': return make_glyph("11111/00100/00100/00100/00100/00100/00100");
    case 'U': return make_glyph("10001/10001/10001/10001/10001/10001/01110");
    case 'V': return make_glyph("10001/10001/10001/10001/10001/01010/00100");
    case 'W': return make_glyph("10001/10001/10001/10101/10101/11011/10001");
    case 'X': return make_glyph("10001/10001/01010/00100/01010/10001/10001");
    case 'Y': return make_glyph("10001/10001/01010/00100/00100/00100/00100");
    case 'Z': return make_glyph("11111/00001/00010/00100/01000/10000/11111");
    case '0': return make_glyph("01110/10011/10101/10101/11001/10001/01110");
    case '1': return make_glyph("00100/01100/00100/00100/00100/00100/01110");
    case '2': return make_glyph("01110/10001/00001/00010/00100/01000/11111");
    case '3': return make_glyph("11110/00001/00001/01110/00001/00001/11110");
    case '4': return make_glyph("00010/00110/01010/10010/11111/00010/00010");
    case '5': return make_glyph("11111/10000/11110/00001/00001/10001/01110");
    case '6': return make_glyph("00110/01000/10000/11110/10001/10001/01110");
    case '7': return make_glyph("11111/00001/00010/00100/01000/01000/01000");
    case '8': return make_glyph("01110/10001/10001/01110/10001/10001/01110");
    case '9': return make_glyph("01110/10001/10001/01111/00001/00010/01100");
    case '.': return make_glyph("00000/00000/00000/00000/00000/00100/00100");
    case ',': return make_glyph("00000/00000/00000/00000/00000/00100/01000");
    case ':': return make_glyph("00000/00100/00100/00000/00100/00100/00000");
    case ';': return make_glyph("00000/00100/00100/00000/00100/01000/00000");
    case '-': return make_glyph("00000/00000/00000/11111/00000/00000/00000");
    case '_': return make_glyph("00000/00000/00000/00000/00000/00000/11111");
    case '/': return make_glyph("00001/00010/00010/00100/01000/01000/10000");
    case '\\': return make_glyph("10000/01000/01000/00100/00010/00010/00001");
    case '(': return make_glyph("00010/00100/01000/01000/01000/00100/00010");
    case ')': return make_glyph("01000/00100/00010/00010/00010/00100/01000");
    case '[': return make_glyph("01110/01000/01000/01000/01000/01000/01110");
    case ']': return make_glyph("01110/00010/00010/00010/00010/00010/01110");
    case '{': return make_glyph("00110/01000/01000/10000/01000/01000/00110");
    case '}': return make_glyph("01100/00010/00010/00001/00010/00010/01100");
    case '<': return make_glyph("00010/00100/01000/10000/01000/00100/00010");
    case '>': return make_glyph("01000/00100/00010/00001/00010/00100/01000");
    case '=': return make_glyph("00000/00000/11111/00000/11111/00000/00000");
    case '+': return make_glyph("00000/00100/00100/11111/00100/00100/00000");
    case '*': return make_glyph("00000/10101/01110/11111/01110/10101/00000");
    case '%': return make_glyph("11001/11010/00010/00100/01000/01011/10011");
    case '!': return make_glyph("00100/00100/00100/00100/00100/00000/00100");
    case '?': return make_glyph("01110/10001/00001/00010/00100/00000/00100");
    case '#': return make_glyph("01010/01010/11111/01010/11111/01010/01010");
    case '\'': return make_glyph("00100/00100/00000/00000/00000/00000/00000");
    case '"': return make_glyph("01010/01010/00000/00000/00000/00000/00000");
    case '|': return make_glyph("00100/00100/00100/00100/00100/00100/00100");
    case '&': return make_glyph("01100/10010/10100/01000/10101/10010/01101");
    case '@': return make_glyph("01110/10001/10111/10101/10111/10000/01110");
    case '$': return make_glyph("00100/01111/10100/01110/00101/11110/00100");
    case '^': return make_glyph("00100/01010/10001/00000/00000/00000/00000");
    case '~': return make_glyph("00000/00000/01001/10110/00000/00000/00000");
    default: return make_glyph("11111/10001/10001/10001/10001/10001/11111");
    }
}

} // namespace

Canvas::Canvas(int width, int height)
    : width_(std::max(0, width)), height_(std::max(0, height)),
      pixels_(static_cast<std::size_t>(width_) * static_cast<std::size_t>(height_), 0xFF101418U) {}

std::uint32_t Canvas::pack(Color color) {
    return (static_cast<std::uint32_t>(color.a) << 24) |
           (static_cast<std::uint32_t>(color.r) << 16) |
           (static_cast<std::uint32_t>(color.g) << 8) | static_cast<std::uint32_t>(color.b);
}

void Canvas::clear(Color color) {
    std::fill(pixels_.begin(), pixels_.end(), pack(color));
}

void Canvas::pixel(int x, int y, Color color) {
    if (x < 0 || y < 0 || x >= width_ || y >= height_) {
        return;
    }
    pixels_[static_cast<std::size_t>(y) * static_cast<std::size_t>(width_) +
            static_cast<std::size_t>(x)] = pack(color);
}

std::uint32_t Canvas::pixel_at(int x, int y) const {
    if (x < 0 || y < 0 || x >= width_ || y >= height_) {
        return 0;
    }
    return pixels_[static_cast<std::size_t>(y) * static_cast<std::size_t>(width_) +
                  static_cast<std::size_t>(x)];
}

void Canvas::fill(Rect rect, Color color) {
    const int left = std::max(0, rect.x);
    const int top = std::max(0, rect.y);
    const int right = std::min(width_, rect.x + rect.width);
    const int bottom = std::min(height_, rect.y + rect.height);
    for (int y = top; y < bottom; ++y) {
        for (int x = left; x < right; ++x) {
            pixel(x, y, color);
        }
    }
}

void Canvas::frame(Rect rect, Color color, int thickness) {
    for (int i = 0; i < thickness; ++i) {
        fill(Rect{rect.x + i, rect.y + i, rect.width - 2 * i, 1}, color);
        fill(Rect{rect.x + i, rect.y + rect.height - 1 - i, rect.width - 2 * i, 1}, color);
        fill(Rect{rect.x + i, rect.y + i, 1, rect.height - 2 * i}, color);
        fill(Rect{rect.x + rect.width - 1 - i, rect.y + i, 1, rect.height - 2 * i}, color);
    }
}

void Canvas::line(int x0, int y0, int x1, int y1, Color color) {
    int dx = std::abs(x1 - x0);
    int dy = -std::abs(y1 - y0);
    const int sx = x0 < x1 ? 1 : -1;
    const int sy = y0 < y1 ? 1 : -1;
    int error = dx + dy;
    while (true) {
        pixel(x0, y0, color);
        if (x0 == x1 && y0 == y1) {
            break;
        }
        const int twice = 2 * error;
        if (twice >= dy) {
            error += dy;
            x0 += sx;
        }
        if (twice <= dx) {
            error += dx;
            y0 += sy;
        }
    }
}

int Canvas::text_width(std::string_view value, int scale) {
    return static_cast<int>(value.size()) * 6 * scale;
}

int Canvas::text_height(int scale) {
    return 7 * scale;
}

void Canvas::text(int x, int y, std::string_view value, Color color, int scale) {
    if (scale < 1) {
        scale = 1;
    }
    int cursor = x;
    for (char c : value) {
        if (c == '\n') {
            cursor = x;
            y += 8 * scale;
            continue;
        }
        if (c == '\t') {
            cursor += 4 * 6 * scale;
            continue;
        }
        const Glyph glyph = glyph_for(c);
        for (std::size_t row = 0; row < glyph.size(); ++row) {
            for (int column = 0; column < 5; ++column) {
                if ((glyph[row] & (1U << (4 - column))) == 0) {
                    continue;
                }
                fill(Rect{cursor + column * scale, y + static_cast<int>(row) * scale, scale,
                          scale},
                     color);
            }
        }
        cursor += 6 * scale;
    }
}

std::uint64_t Canvas::hash() const {
    std::uint64_t value = 1469598103934665603ULL;
    for (std::uint32_t pixel_value : pixels_) {
        value ^= pixel_value;
        value *= 1099511628211ULL;
    }
    return value;
}

bool Canvas::write_ppm(const std::string& path) const {
    std::ofstream output(path, std::ios::binary);
    if (!output) {
        return false;
    }
    output << "P6\n" << width_ << " " << height_ << "\n255\n";
    for (std::uint32_t value : pixels_) {
        const std::array<char, 3> rgb{
            static_cast<char>((value >> 16) & 0xFF),
            static_cast<char>((value >> 8) & 0xFF),
            static_cast<char>(value & 0xFF),
        };
        output.write(rgb.data(), static_cast<std::streamsize>(rgb.size()));
    }
    return static_cast<bool>(output);
}

} // namespace kr32::ui
