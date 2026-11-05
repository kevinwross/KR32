#include "kr32/platform/window.h"

#include <SDL.h>

#include <cstdio>
#include <string>
#include <utility>

#ifdef _WIN32
#include <windows.h>
#include <commdlg.h>
#endif

namespace kr32::platform {
namespace {

int map_key(SDL_Keycode key) {
    switch (key) {
    case SDLK_ESCAPE: return key::Escape;
    case SDLK_TAB: return key::Tab;
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

} // namespace

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
            event.kind = Event::Kind::KeyDown;
            event.key = map_key(sdl_event.key.keysym.sym);
            event.shift = (sdl_event.key.keysym.mod & KMOD_SHIFT) != 0;
            event.control = (sdl_event.key.keysym.mod & KMOD_CTRL) != 0;
            return true;
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

bool open_file_dialog(std::string& path) {
#ifdef _WIN32
    wchar_t file[32768] = L"";
    OPENFILENAMEW dialog{};
    dialog.lStructSize = sizeof(dialog);
    dialog.hwndOwner = GetActiveWindow();
    dialog.lpstrFilter =
        L"KR32 sources and images (*.k32;*.c;*.cpp;*.cc;*.cxx)\0"
        L"*.k32;*.c;*.cpp;*.cc;*.cxx\0All files (*.*)\0*.*\0";
    dialog.lpstrFile = file;
    dialog.nMaxFile = 32768;
    dialog.lpstrDefExt = L"k32";
    dialog.Flags = OFN_EXPLORER | OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST |
                   OFN_NOCHANGEDIR;
    if (GetOpenFileNameW(&dialog) == 0) {
        return false;
    }
    const int length = WideCharToMultiByte(CP_UTF8, 0, file, -1, nullptr, 0,
                                           nullptr, nullptr);
    if (length <= 1) {
        return false;
    }
    std::string converted(static_cast<std::size_t>(length - 1), '\0');
    WideCharToMultiByte(CP_UTF8, 0, file, -1, converted.data(), length,
                        nullptr, nullptr);
    path = std::move(converted);
    return true;
#else
    const char* commands[] = {
        "zenity --file-selection --title='Load KR32 source or image' "
        "--file-filter='KR32 sources and images | *.k32 *.c *.cpp *.cc *.cxx' "
        "--file-filter='All files | *' 2>/dev/null",
        "kdialog --getopenfilename . 'KR32 sources and images (*.k32 *.c *.cpp *.cc *.cxx)|All files (*)' 2>/dev/null",
    };
    for (const char* command : commands) {
        FILE* pipe = popen(command, "r");
        if (pipe == nullptr) {
            continue;
        }
        std::string result;
        char buffer[4096];
        while (std::fgets(buffer, sizeof(buffer), pipe) != nullptr) {
            result += buffer;
        }
        const int status = pclose(pipe);
        if (status != 0) {
            continue;
        }
        while (!result.empty() &&
               (result.back() == '\n' || result.back() == '\r')) {
            result.pop_back();
        }
        if (!result.empty()) {
            path = std::move(result);
            return true;
        }
    }
    return false;
#endif
}

} // namespace kr32::platform
