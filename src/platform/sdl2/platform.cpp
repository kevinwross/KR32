#include "kr32/platform/window.h"

#include <SDL.h>

#include <cstdio>

namespace kr32::platform {
namespace {

int map_key(SDL_Keycode key) {
    switch (key) {
    case SDLK_ESCAPE: return key::Escape;
    case SDLK_TAB: return key::Tab;
    case SDLK_F5: return key::F5;
    case SDLK_F9: return key::F9;
    case SDLK_F10: return key::F10;
    case SDLK_F11: return key::F11;
    case SDLK_LEFT: return key::Left;
    case SDLK_RIGHT: return key::Right;
    case SDLK_UP: return key::Up;
    case SDLK_DOWN: return key::Down;
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
        case SDL_MOUSEWHEEL:
            event.kind = Event::Kind::Wheel;
            event.wheel = sdl_event.wheel.y;
            return true;
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

} // namespace kr32::platform
