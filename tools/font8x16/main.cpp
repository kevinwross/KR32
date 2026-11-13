// Generates kr32os/kernel/font8x16.c from the JetBrains Mono Regular face that
// is embedded in src/resources/embedded_fonts.cpp, so the kernel font matches
// the debugger's without shipping a second copy of the face.
//
// Usage: kr32_font8x16 <embedded_fonts.cpp> <out.c> [em_pixels] [baseline]
//        kr32_font8x16 <embedded_fonts.cpp> --sample <out.ppm> <width> <height> <em> <baseline>
//
// The --sample mode renders an 800x600 console mock-up with the requested cell
// size so a font size can be judged before it is adopted.

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#define STB_TRUETYPE_IMPLEMENTATION
#include "stb_truetype.h"

namespace {

constexpr int kFirst = 0x20;
constexpr int kLast = 0x7E;
constexpr int kWidth = 8;
constexpr int kHeight = 16;

constexpr int kScreenWidth = 800;
constexpr int kScreenHeight = 600;

const unsigned int kPalette[16] = {
    0x00101820U, 0x00A03030U, 0x0030A030U, 0x00A0A030U,
    0x003040A0U, 0x00A030A0U, 0x0030A0A0U, 0x00E6EDF3U,
    0x00506070U, 0x00F06060U, 0x0060F060U, 0x00F0F060U,
    0x006080F0U, 0x00F060F0U, 0x0060F0F0U, 0x00FFFFFFU,
};

std::vector<unsigned char> extract_face(const std::string& text) {
    const std::string marker = "kJetBrainsRegular";
    const std::size_t name = text.find(marker);
    if (name == std::string::npos) {
        return {};
    }
    const std::size_t brace = text.find('{', name);
    const std::size_t end = text.find('}', brace);
    if (brace == std::string::npos || end == std::string::npos) {
        return {};
    }
    std::vector<unsigned char> face;
    for (std::size_t index = brace; index + 4 < end; ++index) {
        if (text[index] == '0' && (text[index + 1] == 'x' || text[index + 1] == 'X')) {
            face.push_back(static_cast<unsigned char>(
                std::strtoul(text.substr(index + 2, 2).c_str(), nullptr, 16)));
            index += 3;
        }
    }
    return face;
}

// Rasterises one code point into `height` rows of `width` bits; bit 15 is the
// leftmost pixel.
void rasterize(const stbtt_fontinfo& font, float scale, int code, int width,
               int height, int baseline, std::vector<unsigned short>& rows) {
    rows.assign(static_cast<std::size_t>(height), 0);
    int glyph_width = 0;
    int glyph_height = 0;
    int x_offset = 0;
    int y_offset = 0;
    unsigned char* bitmap = stbtt_GetCodepointBitmap(
        &font, scale, scale, code, &glyph_width, &glyph_height, &x_offset,
        &y_offset);
    for (int y = 0; y < glyph_height; ++y) {
        const int target_y = baseline + y_offset + y;
        if (target_y < 0 || target_y >= height) {
            continue;
        }
        for (int x = 0; x < glyph_width; ++x) {
            const int target_x = x_offset + x;
            if (target_x < 0 || target_x >= width) {
                continue;
            }
            if (bitmap[y * glyph_width + x] >= 128) {
                rows[static_cast<std::size_t>(target_y)] |=
                    static_cast<unsigned short>(0x8000U >> target_x);
            }
        }
    }
    stbtt_FreeBitmap(bitmap, nullptr);
}

bool write_ppm(const std::string& path,
               const std::vector<unsigned int>& pixels) {
    std::ofstream out(path, std::ios::binary);
    if (!out) {
        return false;
    }
    out << "P6\n" << kScreenWidth << " " << kScreenHeight << "\n255\n";
    for (unsigned int color : pixels) {
        const char bytes[3] = {static_cast<char>((color >> 16) & 0xFFU),
                               static_cast<char>((color >> 8) & 0xFFU),
                               static_cast<char>(color & 0xFFU)};
        out.write(bytes, 3);
    }
    return static_cast<bool>(out);
}

void blit_glyph(std::vector<unsigned int>& pixels,
                const std::vector<unsigned short>& rows, int origin_x,
                int origin_y, int width, int height, unsigned int foreground,
                unsigned int background) {
    for (int y = 0; y < height; ++y) {
        const int screen_y = origin_y + y;
        if (screen_y < 0 || screen_y >= kScreenHeight) {
            continue;
        }
        for (int x = 0; x < width; ++x) {
            const int screen_x = origin_x + x;
            if (screen_x < 0 || screen_x >= kScreenWidth) {
                continue;
            }
            const bool set =
                (rows[static_cast<std::size_t>(y)] & (0x8000U >> x)) != 0;
            pixels[static_cast<std::size_t>(screen_y) * kScreenWidth +
                   static_cast<std::size_t>(screen_x)] = set ? foreground
                                                             : background;
        }
    }
}

int run_sample(const stbtt_fontinfo& font, float scale, const std::string& path,
               int width, int height, int baseline) {
    const int columns = kScreenWidth / width;
    const int rows = kScreenHeight / height;
    const unsigned int foreground = kPalette[7];
    const unsigned int background = kPalette[0];

    const std::vector<std::string> lines = {
        "KR32OS M4",
        "init: hello from user mode",
        "init: pid 1",
        "init exited 0",
        "> help",
        "help echo clear mem ps ls cat run exit",
        "> ls",
        "init.bin  fault.bin  wx.bin",
        "> cat readme.txt",
        "KR32 is a 32-bit teaching computer.",
        "The quick brown fox jumps over the lazy dog 0123456789",
        "ABCDEFGHIJKLMNOPQRSTUVWXYZ",
        "abcdefghijklmnopqrstuvwxyz",
        "!\"#$%&'()*+,-./:;<=>?@[\\]^_`{|}~",
        "> ",
    };

    std::vector<unsigned int> pixels(
        static_cast<std::size_t>(kScreenWidth) * kScreenHeight, background);
    std::vector<unsigned short> rows_bits;
    for (std::size_t index = 0; index < lines.size(); ++index) {
        const int row = static_cast<int>(index);
        if (row >= rows) {
            break;
        }
        const std::string& line = lines[index];
        for (std::size_t column = 0; column < line.size(); ++column) {
            if (static_cast<int>(column) >= columns) {
                break;
            }
            rasterize(font, scale, static_cast<unsigned char>(line[column]),
                      width, height, baseline, rows_bits);
            blit_glyph(pixels, rows_bits, static_cast<int>(column) * width,
                       row * height, width, height, foreground, background);
        }
    }
    // Cursor block after the last prompt.
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            const int screen_x = 2 * width + x;
            const int screen_y = 14 * height + y;
            if (screen_x < kScreenWidth && screen_y < kScreenHeight) {
                pixels[static_cast<std::size_t>(screen_y) * kScreenWidth +
                       static_cast<std::size_t>(screen_x)] = foreground;
            }
        }
    }

    if (!write_ppm(path, pixels)) {
        std::fprintf(stderr, "cannot write %s\n", path.c_str());
        return 1;
    }
    std::printf("sample %dx%d cells, grid %dx%d -> %s\n", width, height, columns,
                rows, path.c_str());
    return 0;
}

} // namespace

int main(int argc, char** argv) {
    if (argc < 3) {
        std::fprintf(stderr,
                     "usage: kr32_font8x16 <embedded_fonts.cpp> <out.c> [em] [baseline]\n"
                     "       kr32_font8x16 <embedded_fonts.cpp> --sample <out.ppm> "
                     "<width> <height> <em> <baseline>\n");
        return 2;
    }
    std::ifstream input(argv[1]);
    if (!input) {
        std::fprintf(stderr, "cannot open %s\n", argv[1]);
        return 1;
    }
    const std::string text((std::istreambuf_iterator<char>(input)),
                           std::istreambuf_iterator<char>());
    const std::vector<unsigned char> face = extract_face(text);
    if (face.empty()) {
        std::fprintf(stderr, "no JetBrains Mono bytes found in %s\n", argv[1]);
        return 1;
    }

    stbtt_fontinfo font{};
    if (stbtt_InitFont(&font, face.data(), 0) == 0) {
        std::fprintf(stderr, "not a TrueType face\n");
        return 1;
    }

    if (std::strcmp(argv[2], "--sample") == 0) {
        if (argc < 8) {
            std::fprintf(stderr,
                         "usage: kr32_font8x16 <face.cpp> --sample <out.ppm> "
                         "<width> <height> <em> <baseline>\n");
            return 2;
        }
        const int width = std::atoi(argv[4]);
        const int height = std::atoi(argv[5]);
        const float em = static_cast<float>(std::atof(argv[6]));
        const int baseline = std::atoi(argv[7]);
        const float scale = stbtt_ScaleForMappingEmToPixels(&font, em);
        return run_sample(font, scale, argv[3], width, height, baseline);
    }

    const float em = argc > 3 ? static_cast<float>(std::atof(argv[3])) : 13.0f;
    const int baseline = argc > 4 ? std::atoi(argv[4]) : 13;
    const float scale = stbtt_ScaleForMappingEmToPixels(&font, em);

    std::ofstream out(argv[2], std::ios::binary);
    if (!out) {
        std::fprintf(stderr, "cannot write %s\n", argv[2]);
        return 1;
    }
    out << "/* Generated by tools/font8x16 from JetBrains Mono Regular (SIL OFL\n"
           "   1.1). See third_party/fonts/OFL-JetBrainsMono.txt. Do not edit. */\n\n"
           "const unsigned char kFont8x16[1520] = {\n";
    std::vector<unsigned short> rows;
    for (int code = kFirst; code <= kLast; ++code) {
        rasterize(font, scale, code, kWidth, kHeight, baseline, rows);
        out << "    ";
        for (int row = 0; row < kHeight; ++row) {
            char byte[8];
            std::snprintf(byte, sizeof(byte), "0x%02X",
                          static_cast<unsigned>(rows[static_cast<std::size_t>(row)] >> 8));
            out << byte;
            if (!(row == kHeight - 1 && code == kLast)) {
                out << ", ";
            }
        }
        out << " /* " << static_cast<char>(code) << " */\n";
    }
    out << "};\n";
    return out ? 0 : 1;
}
