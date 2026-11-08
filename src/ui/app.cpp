#include "kr32/ui/app.h"

#include "kr32/os/defaults.h"
#include "kr32/platform/window.h"
#include "kr32/ui/debugger_ui.h"

#include <chrono>
#include <cstdio>
#include <memory>
#include <string>
#include <thread>

namespace kr32::ui {
namespace {
constexpr int kInitialWidth = 1440;
constexpr int kInitialHeight = 900;
}

bool smoke_test() {
    return kr32::platform::smoke_test();
}

bool load_os_images(DebuggerUI& debugger, const kr32::os::ImagePaths& paths,
                    std::string& error) {
    if (!paths.embedded) {
        return debugger.load_os(paths.kernel, paths.bios, error);
    }
    const kr32::os::EmbeddedOs& images = kr32::os::embedded_os();
    std::vector<std::uint8_t> kernel(images.kernel.begin(), images.kernel.end());
    std::vector<std::uint8_t> bios(images.bios.begin(), images.bios.end());
    return debugger.load_os_memory(kernel, bios, "<embedded>", "<embedded>",
                                   error);
}

int run(int argc, char** argv) {
    std::string image_path;
    std::string ppm_path;
    std::string os_path;
    std::string bios_path;
    bool os_mode = false;
    bool embedded_images = false;
    bool headless = false;
    for (int index = 1; index < argc; ++index) {
        const std::string argument = argv[index];
        if (argument == "--headless") {
            headless = true;
            continue;
        }
        if (argument == "--os") {
            os_mode = true;
            if (index + 1 < argc && argv[index + 1][0] != '-') {
                os_path = argv[++index];
            }
            continue;
        }
        if (argument == "--embedded") {
            embedded_images = true;
            continue;
        }
        if (argument == "--bios") {
            if (index + 1 >= argc) {
                std::fprintf(stderr, "kr32 gui: --bios needs a path\n");
                return 2;
            }
            bios_path = argv[++index];
            continue;
        }
        if (argument == "--screenshot") {
            if (index + 1 < argc) {
                ppm_path = argv[++index];
            }
            continue;
        }
        if (argument == "--help" || argument == "-h") {
            std::printf("Usage: kr32 gui <image.k32> [--headless] [--screenshot out.ppm]\n"
                        "       kr32 gui --os [kernel.bin] [--bios bios.bin] "
                        "[--embedded] [--headless] [--screenshot out.ppm]\n");
            return 0;
        }
        if (!argument.empty() && argument[0] == '-') {
            std::fprintf(stderr, "kr32 gui: unknown option %s\n", argument.c_str());
            return 2;
        }
        image_path = argument;
    }
    if (os_mode && !image_path.empty()) {
        std::fprintf(stderr, "kr32 gui: use either --os or an image path\n");
        return 2;
    }
    if (embedded_images && !os_mode) {
        std::fprintf(stderr, "kr32 gui: --embedded requires --os\n");
        return 2;
    }
    os::ImagePaths paths;
    if (os_mode) {
        std::string error;
        if (!os::resolve(argv[0], os_path, bios_path, paths, error,
                         embedded_images)) {
            std::fprintf(stderr, "kr32 gui: %s\n", error.c_str());
            return 1;
        }
    }

    if (headless) {
        if (!os_mode && image_path.empty()) {
            std::fprintf(stderr, "kr32 gui: --headless needs an image path\n");
            return 2;
        }
        DebuggerUI debugger;
        std::string error;
        const bool loaded =
            os_mode ? load_os_images(debugger, paths, error)
                    : debugger.load(image_path, error);
        if (!loaded) {
            std::fprintf(stderr, "kr32 gui: %s\n", error.c_str());
            return 1;
        }
        return debugger.headless(ppm_path) ? 0 : 1;
    }

    kr32::platform::Window window;
    if (!window.create("KR32 Debugger", kInitialWidth, kInitialHeight)) {
        return 1;
    }

    DebuggerUI debugger;
    if (os_mode) {
        std::string error;
        if (!load_os_images(debugger, paths, error)) {
            std::fprintf(stderr, "kr32 gui: %s\n", error.c_str());
            debugger.show_error(error);
        }
    } else if (!image_path.empty()) {
        std::string error;
        if (!debugger.load(image_path, error)) {
            std::fprintf(stderr, "kr32 gui: %s\n", error.c_str());
            debugger.show_error(error);
        }
    }

    auto canvas = std::make_unique<Canvas>(kInitialWidth, kInitialHeight);
    bool running = true;
    while (running) {
        kr32::platform::Event event;
        while (window.poll(event)) {
            switch (event.kind) {
            case kr32::platform::Event::Kind::Quit:
                running = false;
                break;
            case kr32::platform::Event::Kind::Resize:
                if (event.x >= 800 && event.y >= 520) {
                    canvas = std::make_unique<Canvas>(event.x, event.y);
                }
                break;
            case kr32::platform::Event::Kind::KeyDown:
                (void)debugger.key(event.key, event.shift, event.control);
                break;
            case kr32::platform::Event::Kind::MouseDown:
                if (event.button == 3) {
                    (void)debugger.context_menu(event.x, event.y, canvas->width(), canvas->height());
                } else {
                    (void)debugger.click(event.x, event.y, canvas->width(), canvas->height());
                }
                break;
            case kr32::platform::Event::Kind::MouseMove:
                debugger.hover(event.x, event.y, canvas->width(), canvas->height());
                break;
            case kr32::platform::Event::Kind::MouseUp:
                debugger.mouse_up();
                break;
            case kr32::platform::Event::Kind::Wheel:
                (void)debugger.wheel(event.x, event.y, canvas->width(), canvas->height(),
                                     event.wheel);
                break;
            case kr32::platform::Event::Kind::None:
                break;
            }
            if (debugger.quit_requested()) {
                running = false;
            }
        }
        debugger.update();
        debugger.render(*canvas);
        window.present(*canvas);
        std::this_thread::sleep_for(std::chrono::milliseconds(16));
    }
    return 0;
}

} // namespace kr32::ui
