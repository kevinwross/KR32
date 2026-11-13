#include "kr32/platform/window.h"

#include <SDL.h>

#include <algorithm>
#include <cstdio>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#include <commdlg.h>
#else
#include <sys/wait.h>
#endif

namespace kr32::platform {
namespace {

int map_key(SDL_Keycode key) {
    switch (key) {
    case SDLK_ESCAPE: return key::Escape;
    case SDLK_TAB: return key::Tab;
    case SDLK_F2: return key::F2;
    case SDLK_F5: return key::F5;
    case SDLK_F6: return key::F6;
    case SDLK_F9: return key::F9;
    case SDLK_F10: return key::F10;
    case SDLK_F11: return key::F11;
    case SDLK_LEFT: return key::Left;
    case SDLK_RIGHT: return key::Right;
    case SDLK_UP: return key::Up;
    case SDLK_DOWN: return key::Down;
    case SDLK_RETURN:
    case SDLK_KP_ENTER: return key::Enter;
    case SDLK_BACKSPACE: return key::Backspace;
    case SDLK_KP_0: return '0';
    case SDLK_KP_1: return '1';
    case SDLK_KP_2: return '2';
    case SDLK_KP_3: return '3';
    case SDLK_KP_4: return '4';
    case SDLK_KP_5: return '5';
    case SDLK_KP_6: return '6';
    case SDLK_KP_7: return '7';
    case SDLK_KP_8: return '8';
    case SDLK_KP_9: return '9';
    default: break;
    }
    if (key >= 32 && key < 127) {
        return static_cast<int>(key);
    }
    return 0;
}

std::uint8_t kr32_scancode(SDL_Scancode code) {
    if (code >= SDL_SCANCODE_A && code <= SDL_SCANCODE_Z) {
        return static_cast<std::uint8_t>('A' + (code - SDL_SCANCODE_A));
    }
    if (code >= SDL_SCANCODE_1 && code <= SDL_SCANCODE_9) {
        return static_cast<std::uint8_t>('1' + (code - SDL_SCANCODE_1));
    }
    switch (code) {
    case SDL_SCANCODE_0: return '0';
    case SDL_SCANCODE_SPACE: return 0x20;
    case SDL_SCANCODE_MINUS: return '-';
    case SDL_SCANCODE_EQUALS: return '=';
    case SDL_SCANCODE_LEFTBRACKET: return '[';
    case SDL_SCANCODE_RIGHTBRACKET: return ']';
    case SDL_SCANCODE_BACKSLASH: return '\\';
    case SDL_SCANCODE_SEMICOLON: return ';';
    case SDL_SCANCODE_APOSTROPHE: return '\'';
    case SDL_SCANCODE_GRAVE: return '`';
    case SDL_SCANCODE_COMMA: return ',';
    case SDL_SCANCODE_PERIOD: return '.';
    case SDL_SCANCODE_SLASH: return '/';
    case SDL_SCANCODE_RETURN:
    case SDL_SCANCODE_KP_ENTER: return 0x80;
    case SDL_SCANCODE_ESCAPE: return 0x81;
    case SDL_SCANCODE_BACKSPACE: return 0x82;
    case SDL_SCANCODE_TAB: return 0x83;
    case SDL_SCANCODE_DELETE: return 0x84;
    case SDL_SCANCODE_INSERT: return 0x85;
    case SDL_SCANCODE_HOME: return 0x86;
    case SDL_SCANCODE_END: return 0x87;
    case SDL_SCANCODE_PAGEUP: return 0x88;
    case SDL_SCANCODE_PAGEDOWN: return 0x89;
    case SDL_SCANCODE_UP: return 0x8A;
    case SDL_SCANCODE_DOWN: return 0x8B;
    case SDL_SCANCODE_LEFT: return 0x8C;
    case SDL_SCANCODE_RIGHT: return 0x8D;
    case SDL_SCANCODE_F1: return 0x90;
    case SDL_SCANCODE_F2: return 0x91;
    case SDL_SCANCODE_F3: return 0x92;
    case SDL_SCANCODE_F4: return 0x93;
    case SDL_SCANCODE_F5: return 0x94;
    case SDL_SCANCODE_F6: return 0x95;
    case SDL_SCANCODE_F7: return 0x96;
    case SDL_SCANCODE_F8: return 0x97;
    case SDL_SCANCODE_F9: return 0x98;
    case SDL_SCANCODE_F10: return 0x99;
    case SDL_SCANCODE_F11: return 0x9A;
    case SDL_SCANCODE_F12: return 0x9B;
    case SDL_SCANCODE_LSHIFT: return 0xA0;
    case SDL_SCANCODE_RSHIFT: return 0xA1;
    case SDL_SCANCODE_LCTRL: return 0xA2;
    case SDL_SCANCODE_RCTRL: return 0xA3;
    case SDL_SCANCODE_LALT: return 0xA4;
    case SDL_SCANCODE_RALT: return 0xA5;
    case SDL_SCANCODE_CAPSLOCK: return 0xA6;
    default: return 0;
    }
}

int modifier_bits(SDL_Keymod mod) {
    int bits = 0;
    if ((mod & KMOD_SHIFT) != 0) bits |= 1;
    if ((mod & KMOD_CTRL) != 0) bits |= 2;
    if ((mod & KMOD_ALT) != 0) bits |= 4;
    if ((mod & KMOD_CAPS) != 0) bits |= 8;
    return bits;
}

} // namespace

std::uint32_t event_word(const Event& event) {
    const std::uint32_t ascii = static_cast<std::uint32_t>(event.ascii) & 0xFFU;
    const std::uint32_t modifiers = static_cast<std::uint32_t>(event.modifiers) & 0xFFU;
    const std::uint32_t scancode = static_cast<std::uint32_t>(event.scancode) & 0xFFU;
    const std::uint32_t type = event.kind == Event::Kind::KeyUp ? 1U : 0U;
    return ascii | (modifiers << 8) | (scancode << 16) | (type << 24);
}

Window::~Window() {
    destroy();
}

bool Window::create(const std::string& title, int width, int height) {
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS) != 0) {
        std::fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
        return false;
    }
    width_ = width;
    height_ = height;
    window_ = SDL_CreateWindow(title.c_str(), SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                               width, height, SDL_WINDOW_RESIZABLE);
    if (window_ == nullptr) {
        std::fprintf(stderr, "SDL_CreateWindow failed: %s\n", SDL_GetError());
        return false;
    }
    renderer_ = SDL_CreateRenderer(static_cast<SDL_Window*>(window_), -1,
                                   SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (renderer_ == nullptr) {
        renderer_ = SDL_CreateRenderer(static_cast<SDL_Window*>(window_), -1, SDL_RENDERER_SOFTWARE);
    }
    if (renderer_ == nullptr) {
        std::fprintf(stderr, "SDL_CreateRenderer failed: %s\n", SDL_GetError());
        return false;
    }
    texture_ = SDL_CreateTexture(static_cast<SDL_Renderer*>(renderer_),
                                 SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, width,
                                 height);
    if (texture_ == nullptr) {
        std::fprintf(stderr, "SDL_CreateTexture failed: %s\n", SDL_GetError());
        return false;
    }
    SDL_SetWindowMinimumSize(static_cast<SDL_Window*>(window_), 800, 520);
    return true;
}

void Window::destroy() {
    if (surface_texture_ != nullptr) {
        SDL_DestroyTexture(static_cast<SDL_Texture*>(surface_texture_));
        surface_texture_ = nullptr;
    }
    if (texture_ != nullptr) {
        SDL_DestroyTexture(static_cast<SDL_Texture*>(texture_));
        texture_ = nullptr;
    }
    if (renderer_ != nullptr) {
        SDL_DestroyRenderer(static_cast<SDL_Renderer*>(renderer_));
        renderer_ = nullptr;
    }
    if (window_ != nullptr) {
        SDL_DestroyWindow(static_cast<SDL_Window*>(window_));
        window_ = nullptr;
    }
    SDL_Quit();
}

bool Window::poll(Event& event) {
    event = Event{};
    SDL_Event sdl_event{};
    while (SDL_PollEvent(&sdl_event) != 0) {
        switch (sdl_event.type) {
        case SDL_QUIT:
            event.kind = Event::Kind::Quit;
            return true;
        case SDL_WINDOWEVENT:
            if (sdl_event.window.event == SDL_WINDOWEVENT_RESIZED ||
                sdl_event.window.event == SDL_WINDOWEVENT_SIZE_CHANGED) {
                event.kind = Event::Kind::Resize;
                event.x = sdl_event.window.data1;
                event.y = sdl_event.window.data2;
                return true;
            }
            break;
        case SDL_KEYDOWN:
        case SDL_KEYUP: {
            const bool down = sdl_event.type == SDL_KEYDOWN;
            event.kind = down ? Event::Kind::KeyDown : Event::Kind::KeyUp;
            event.key = map_key(sdl_event.key.keysym.sym);
            event.shift = (sdl_event.key.keysym.mod & KMOD_SHIFT) != 0;
            event.control = (sdl_event.key.keysym.mod & KMOD_CTRL) != 0;
            event.scancode = kr32_scancode(sdl_event.key.keysym.scancode);
            event.modifiers = modifier_bits(static_cast<SDL_Keymod>(SDL_GetModState()));
            const int symbol = static_cast<int>(sdl_event.key.keysym.sym);
            event.ascii = (down && symbol >= 32 && symbol < 127) ? symbol : 0;
            return true;
        }
        case SDL_MOUSEBUTTONDOWN:
            event.kind = Event::Kind::MouseDown;
            event.button = sdl_event.button.button;
            event.x = sdl_event.button.x;
            event.y = sdl_event.button.y;
            return true;
        case SDL_MOUSEBUTTONUP:
            event.kind = Event::Kind::MouseUp;
            event.x = sdl_event.button.x;
            event.y = sdl_event.button.y;
            return true;
        case SDL_MOUSEMOTION:
            event.kind = Event::Kind::MouseMove;
            event.x = sdl_event.motion.x;
            event.y = sdl_event.motion.y;
            return true;
        case SDL_MOUSEWHEEL: {
            int mouse_x = 0;
            int mouse_y = 0;
            SDL_GetMouseState(&mouse_x, &mouse_y);
            event.kind = Event::Kind::Wheel;
            event.wheel = sdl_event.wheel.y;
            event.x = mouse_x;
            event.y = mouse_y;
            return true;
        }
        default:
            break;
        }
    }
    return false;
}

void Window::present(const ui::Canvas& canvas) {
    if (renderer_ == nullptr) {
        return;
    }
    if (texture_ == nullptr || width_ != canvas.width() || height_ != canvas.height()) {
        if (texture_ != nullptr) {
            SDL_DestroyTexture(static_cast<SDL_Texture*>(texture_));
            texture_ = nullptr;
        }
        width_ = canvas.width();
        height_ = canvas.height();
        texture_ = SDL_CreateTexture(static_cast<SDL_Renderer*>(renderer_),
                                     SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING,
                                     width_, height_);
        if (texture_ == nullptr) {
            return;
        }
    }
    SDL_UpdateTexture(static_cast<SDL_Texture*>(texture_), nullptr, canvas.pixels().data(),
                      canvas.width() * static_cast<int>(sizeof(std::uint32_t)));
    SDL_RenderClear(static_cast<SDL_Renderer*>(renderer_));
    SDL_RenderCopy(static_cast<SDL_Renderer*>(renderer_), static_cast<SDL_Texture*>(texture_),
                   nullptr, nullptr);
    SDL_RenderPresent(static_cast<SDL_Renderer*>(renderer_));
}

void Window::present_surface(const std::uint8_t* pixels, int width, int height,
                             int stride, int scale) {
    if (renderer_ == nullptr || window_ == nullptr || pixels == nullptr) {
        return;
    }
    if (width <= 0 || height <= 0 || scale <= 0 || stride < width * 4) {
        return;
    }
    if (surface_texture_ == nullptr || surface_width_ != width ||
        surface_height_ != height) {
        if (surface_texture_ != nullptr) {
            SDL_DestroyTexture(static_cast<SDL_Texture*>(surface_texture_));
            surface_texture_ = nullptr;
        }
        surface_width_ = width;
        surface_height_ = height;
        surface_texture_ = SDL_CreateTexture(
            static_cast<SDL_Renderer*>(renderer_), SDL_PIXELFORMAT_ARGB8888,
            SDL_TEXTUREACCESS_STREAMING, width, height);
        if (surface_texture_ == nullptr) {
            return;
        }
        SDL_SetTextureBlendMode(static_cast<SDL_Texture*>(surface_texture_),
                                SDL_BLENDMODE_NONE);
    }
    if (SDL_UpdateTexture(static_cast<SDL_Texture*>(surface_texture_), nullptr,
                          pixels, stride) != 0) {
        return;
    }
    int output_width = 0;
    int output_height = 0;
    SDL_GetRendererOutputSize(static_cast<SDL_Renderer*>(renderer_),
                              &output_width, &output_height);
    const SDL_Rect destination{(output_width - width * scale) / 2,
                               (output_height - height * scale) / 2,
                               width * scale, height * scale};
    SDL_RenderClear(static_cast<SDL_Renderer*>(renderer_));
    SDL_RenderCopy(static_cast<SDL_Renderer*>(renderer_),
                   static_cast<SDL_Texture*>(surface_texture_), nullptr,
                   &destination);
    SDL_RenderPresent(static_cast<SDL_Renderer*>(renderer_));
}

bool smoke_test() {
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS) != 0) {
        std::fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
        return false;
    }

    SDL_version version{};
    SDL_GetVersion(&version);
    std::printf("SDL %u.%u.%u\n",
                static_cast<unsigned>(version.major),
                static_cast<unsigned>(version.minor),
                static_cast<unsigned>(version.patch));

    SDL_Quit();
    return version.major == 2;
}

std::string clipboard_text() {
    char* text = SDL_GetClipboardText();
    if (text == nullptr) {
        return {};
    }
    std::string result(text);
    SDL_free(text);
    return result;
}

namespace {

constexpr const char* kDefaultFilter = "*.k32;*.c;*.cpp;*.cc;*.cxx";

#ifdef _WIN32
std::wstring widen(const std::string& text) {
    if (text.empty()) {
        return {};
    }
    const int length =
        MultiByteToWideChar(CP_UTF8, 0, text.c_str(), -1, nullptr, 0);
    if (length <= 0) {
        return {};
    }
    std::wstring converted(static_cast<std::size_t>(length - 1), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, text.c_str(), -1, converted.data(), length);
    return converted;
}

std::string narrow(const std::wstring& text) {
    if (text.empty()) {
        return {};
    }
    const int length =
        WideCharToMultiByte(CP_UTF8, 0, text.c_str(), -1, nullptr, 0, nullptr, nullptr);
    if (length <= 1) {
        return {};
    }
    std::string converted(static_cast<std::size_t>(length - 1), '\0');
    WideCharToMultiByte(CP_UTF8, 0, text.c_str(), -1, converted.data(), length,
                        nullptr, nullptr);
    return converted;
}

std::wstring make_filter(const std::string& patterns) {
    const std::string value = patterns.empty() ? "*.*" : patterns;
    const std::wstring wide = widen(value);
    std::wstring filter = L"Supported files (" + wide + L")";
    filter.push_back(L'\0');
    filter += wide;
    filter.push_back(L'\0');
    filter += L"All files (*.*)";
    filter.push_back(L'\0');
    filter += L"*.*";
    filter.push_back(L'\0');
    filter.push_back(L'\0');
    return filter;
}
#else
std::string shell_quote(const std::string& value) {
    std::string result = "'";
    for (const char character : value) {
        if (character == '\'') {
            result += "'\\''";
        } else {
            result += character;
        }
    }
    result += "'";
    return result;
}

std::string pattern_list(const std::string& patterns) {
    std::string result = patterns.empty() ? "*" : patterns;
    std::replace(result.begin(), result.end(), ';', ' ');
    return result;
}

FileDialogResult run_dialog_command(const std::string& command) {
    FileDialogResult result;
    FILE* pipe = popen(command.c_str(), "r");
    if (pipe == nullptr) {
        result.status = FileDialogStatus::Unavailable;
        result.error = "cannot start dialog process";
        return result;
    }
    std::string output;
    char buffer[4096];
    while (std::fgets(buffer, sizeof(buffer), pipe) != nullptr) {
        output += buffer;
    }
    const int status = pclose(pipe);
    const int code = WIFEXITED(status) ? WEXITSTATUS(status) : -1;
    if (code == 127) {
        result.status = FileDialogStatus::Unavailable;
        return result;
    }
    while (!output.empty() &&
           (output.back() == '\n' || output.back() == '\r')) {
        output.pop_back();
    }
    if (code == 1) {
        result.status = FileDialogStatus::Canceled;
        return result;
    }
    if (code != 0) {
        result.status = FileDialogStatus::Error;
        result.error = "dialog exited with status " + std::to_string(code);
        return result;
    }
    if (output.empty()) {
        result.status = FileDialogStatus::Canceled;
        return result;
    }
    result.status = FileDialogStatus::Selected;
    result.path = std::move(output);
    return result;
}
#endif

} // namespace

FileDialogResult show_file_dialog(const FileDialogOptions& options) {
    FileDialogResult result;
    const std::string patterns =
        options.filter.empty() ? kDefaultFilter : options.filter;
#ifdef _WIN32
    wchar_t file[32768] = L"";
    if (!options.initial_path.empty()) {
        const std::wstring initial = widen(options.initial_path);
        if (!initial.empty()) {
            wcsncpy_s(file, 32768, initial.c_str(), _TRUNCATE);
        }
    }
    OPENFILENAMEW dialog{};
    dialog.lStructSize = sizeof(dialog);
    dialog.hwndOwner = GetActiveWindow();
    const std::wstring filter = make_filter(patterns);
    const std::wstring title = widen(options.title);
    const std::wstring extension = widen(options.default_extension);
    dialog.lpstrFilter = filter.c_str();
    dialog.lpstrFile = file;
    dialog.nMaxFile = 32768;
    dialog.lpstrDefExt = extension.empty() ? nullptr : extension.c_str();
    dialog.lpstrTitle = title.empty() ? nullptr : title.c_str();
    dialog.Flags = OFN_EXPLORER | OFN_NOCHANGEDIR;
    if (options.kind == FileDialogKind::Open) {
        if (options.must_exist) {
            dialog.Flags |= OFN_FILEMUSTEXIST;
        }
        dialog.Flags |= OFN_PATHMUSTEXIST;
    } else {
        dialog.Flags |= OFN_PATHMUSTEXIST;
        if (options.overwrite_prompt) {
            dialog.Flags |= OFN_OVERWRITEPROMPT;
        }
    }
    const BOOL accepted = options.kind == FileDialogKind::Open
                              ? GetOpenFileNameW(&dialog)
                              : GetSaveFileNameW(&dialog);
    if (!accepted) {
        const DWORD error = CommDlgExtendedError();
        if (error == 0) {
            result.status = FileDialogStatus::Canceled;
        } else {
            result.status = FileDialogStatus::Error;
            result.error = "file dialog error " + std::to_string(error);
        }
        return result;
    }
    result.path = narrow(file);
    if (result.path.empty()) {
        result.status = FileDialogStatus::Error;
        result.error = "file dialog returned an empty path";
        return result;
    }
    result.status = FileDialogStatus::Selected;
    return result;
#else
    const std::string patterns_list = pattern_list(patterns);
    const std::string title = options.title.empty() ? "Select File" : options.title;
    const std::string initial =
        options.initial_path.empty() ? "." : options.initial_path;
    std::vector<std::string> commands;
    {
        std::ostringstream command;
        command << "command -v zenity >/dev/null 2>&1 || exit 127; "
                << "zenity --file-selection";
        if (options.kind == FileDialogKind::Save) {
            command << " --save";
            if (options.overwrite_prompt) {
                command << " --confirm-overwrite";
            }
        }
        command << " --title=" << shell_quote(title)
                << " --filename=" << shell_quote(initial)
                << " --file-filter=" << shell_quote("Supported files | " + patterns_list)
                << " --file-filter=" << shell_quote("All files | *")
                << " 2>/dev/null";
        commands.push_back(command.str());
    }
    {
        std::ostringstream command;
        command << "command -v kdialog >/dev/null 2>&1 || exit 127; "
                << "kdialog --title " << shell_quote(title) << " ";
        command << (options.kind == FileDialogKind::Save ? "--getsavefilename "
                                                         : "--getopenfilename ")
                << shell_quote(initial) << " "
                << shell_quote("Supported files (" + patterns_list +
                               ")|All files (*)")
                << " 2>/dev/null";
        commands.push_back(command.str());
    }
    FileDialogResult last;
    for (const std::string& command : commands) {
        FileDialogResult candidate = run_dialog_command(command);
        if (candidate.status == FileDialogStatus::Unavailable) {
            continue;
        }
        if (candidate.status == FileDialogStatus::Selected ||
            candidate.status == FileDialogStatus::Canceled) {
            return candidate;
        }
        last = std::move(candidate);
    }
    if (last.status == FileDialogStatus::Error) {
        return last;
    }
    result.status = FileDialogStatus::Unavailable;
    result.error = "no file dialog backend is available";
    return result;
#endif
}

bool open_file_dialog(std::string& path) {
    FileDialogOptions options;
    options.title = "Load KR32 source or image";
    options.filter = kDefaultFilter;
    options.default_extension = "k32";
    options.must_exist = true;
    const FileDialogResult result = show_file_dialog(options);
    if (result.status != FileDialogStatus::Selected) {
        return false;
    }
    path = result.path;
    return true;
}

} // namespace kr32::platform
