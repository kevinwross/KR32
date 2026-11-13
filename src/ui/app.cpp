#include "kr32/ui/app.h"

#include "kr32/container/container.h"
#include "kr32/core/devices.h"
#include "kr32/core/machine.h"
#include "kr32/os/bootinfo.h"
#include "kr32/os/defaults.h"
#include "kr32/platform/window.h"
#include "kr32/ui/debugger_ui.h"

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <memory>
#include <span>
#include <string>
#include <thread>
#include <vector>

namespace kr32::ui {
namespace {
constexpr int kInitialWidth = 1440;
constexpr int kInitialHeight = 900;

// Debugger shortcuts always win, even while the guest owns the keyboard.
bool is_debugger_key(int key) {
    return key == kr32::platform::key::Escape || key == kr32::platform::key::F5 ||
           key == kr32::platform::key::F6 || key == kr32::platform::key::F9 ||
           key == kr32::platform::key::F10 || key == kr32::platform::key::F11;
}
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

int run(int argc, char** argv) {    std::string image_path;
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
                if (debugger.screen_focused() && !is_debugger_key(event.key)) {
                    debugger.guest_key(kr32::platform::event_word(event));
                } else {
                    (void)debugger.key(event.key, event.shift, event.control);
                }
                break;
            case kr32::platform::Event::Kind::KeyUp:
                if (debugger.screen_focused()) {
                    debugger.guest_key(kr32::platform::event_word(event));
                }
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

namespace {

bool read_bytes(const std::string& path, std::vector<std::uint8_t>& bytes,
                std::string& error) {
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        error = "cannot open " + path;
        return false;
    }
    bytes.assign(std::istreambuf_iterator<char>(input),
                 std::istreambuf_iterator<char>());
    return true;
}

bool read_container(const std::string& path, container::Image& image,
                    std::string& error) {
    std::vector<std::uint8_t> bytes;
    if (!read_bytes(path, bytes, error)) {
        return false;
    }
    container::Error container_error;
    if (!container::read(bytes, image, container_error)) {
        error = path + ": " + container_error.message;
        return false;
    }
    return true;
}

bool load_executable(core::Machine& machine, const container::Image& image,
                     std::uint32_t stack_pointer, std::string& error) {
    if (image.type != container::ImageType::Executable) {
        error = "input is a relocatable object, not an executable";
        return false;
    }
    container::LoadedImage loaded;
    if (!container::load_into(machine, image, loaded, error)) {
        return false;
    }
    machine.reset(image.entry, stack_pointer);
    if (!loaded.tls_template.empty()) {
        const std::uint32_t tcb =
            machine.ram_base() + machine.ram_size() - 1024U;
        for (std::size_t index = 0; index < loaded.tls_template.size(); ++index) {
            (void)machine.write8(tcb + 32U + static_cast<std::uint32_t>(index),
                                 loaded.tls_template[index]);
        }
        machine.set_reg(11, tcb);
    }
    return true;
}

bool load_os_machine(core::Machine& machine, const os::ImagePaths& paths,
                     std::string& error) {
    std::vector<std::uint8_t> kernel_bytes;
    std::vector<std::uint8_t> bios_bytes;
    if (paths.embedded) {
        const kr32::os::EmbeddedOs& images = kr32::os::embedded_os();
        kernel_bytes.assign(images.kernel.begin(), images.kernel.end());
        bios_bytes.assign(images.bios.begin(), images.bios.end());
    } else if (!read_bytes(paths.kernel, kernel_bytes, error) ||
               !read_bytes(paths.bios, bios_bytes, error)) {
        return false;
    }
    container::Image kernel;
    container::Image bios;
    container::Error container_error;
    if (!container::read(kernel_bytes, kernel, container_error)) {
        error = "kernel: " + container_error.message;
        return false;
    }
    if (!container::read(bios_bytes, bios, container_error)) {
        error = "bios: " + container_error.message;
        return false;
    }
    container::LoadedImage loaded;
    if (!container::load_into(machine, kernel, loaded, error) ||
        !container::load_into(machine, bios, loaded, error)) {
        return false;
    }

    std::uint32_t kernel_size = 0;
    for (const container::Section& section : kernel.sections) {
        if ((section.flags & container::Alloc) == 0) {
            continue;
        }
        const std::uint32_t end = section.address + section.size;
        if (end > 0x0002'0000U && end - 0x0002'0000U > kernel_size) {
            kernel_size = end - 0x0002'0000U;
        }
    }

    machine.reset();
    os::BootInfo boot{};
    boot.magic = os::kBootInfoMagic;
    boot.version = os::kBootInfoVersion;
    boot.size = sizeof(os::BootInfo);
    boot.ram_base = core::kRamBase;
    boot.ram_size = core::kDefaultRamSize;
    boot.kernel_base = 0x0002'0000U;
    boot.kernel_size = kernel_size;
    boot.kernel_entry = 0x0002'0000U;
    boot.framebuffer_base = core::kFramebufferBase;
    boot.framebuffer_size = core::kFramebufferSize;
    boot.framebuffer_stride = core::kFramebufferStride;
    boot.framebuffer_format = 1;
    std::string boot_error;
    if (!os::write_boot_info(machine, boot, boot_error)) {
        error = boot_error;
        return false;
    }
    return true;
}

// A standalone display guest is linked flat and has no MMU setup of its own,
// so the host maps the low 4 MiB (code and stack), the first 4 MiB of the
// aperture, and the MMIO window. KR32OS images build their own tables.
void install_display_map(core::Machine& machine) {
    constexpr std::uint32_t kTables = 0x0030'0000;
    constexpr std::uint32_t kLowLeaf = kTables + 0x1000;
    constexpr std::uint32_t kVramLeaf = kTables + 0x2000;
    constexpr std::uint32_t kMmioLeaf = kTables + 0x3000;
    constexpr std::uint32_t kFlags =
        core::kPteValid | core::kPteWritable | core::kPteExecutable;

    for (std::uint32_t index = 0; index < 1024U; ++index) {
        (void)machine.write_physical32(kLowLeaf + index * 4U,
                                       index * 0x1000U | kFlags);
        (void)machine.write_physical32(kVramLeaf + index * 4U,
                                       core::kFramebufferBase + index * 0x1000U |
                                           (core::kPteValid | core::kPteWritable));
        (void)machine.write_physical32(kMmioLeaf + index * 4U,
                                       0xFFC0'0000U + index * 0x1000U |
                                           (core::kPteValid | core::kPteWritable));
    }
    (void)machine.write_physical32(kTables, kLowLeaf | core::kPteValid);
    (void)machine.write_physical32(kTables + 1008U * 4U, kVramLeaf | core::kPteValid);
    (void)machine.write_physical32(kTables + 1023U * 4U, kMmioLeaf | core::kPteValid);
    (void)machine.write_control_register(8, kTables);
    (void)machine.write_control_register(10, core::kMctlPaging);
}

bool write_screen_shot(const std::string& path,
                       const core::FramebufferDevice& framebuffer,
                       std::string& error) {
    const core::FramebufferDevice::CommittedFrame frame = framebuffer.frame();
    const std::span<const std::uint8_t> pixels = framebuffer.frame_pixels();
    if (frame.width == 0 || frame.height == 0 || pixels.empty()) {
        error = "the guest has not committed a frame";
        return false;
    }
    std::ofstream output(path, std::ios::binary);
    if (!output) {
        error = "cannot write " + path;
        return false;
    }
    output << "P6\n" << frame.width << " " << frame.height << "\n255\n";
    std::vector<char> row(static_cast<std::size_t>(frame.width) * 3U);
    for (std::uint32_t y = 0; y < frame.height; ++y) {
        const std::uint8_t* source =
            pixels.data() + static_cast<std::size_t>(y) * frame.stride;
        for (std::uint32_t x = 0; x < frame.width; ++x) {
            row[x * 3U + 0U] = static_cast<char>(source[x * 4U + 2U]);
            row[x * 3U + 1U] = static_cast<char>(source[x * 4U + 1U]);
            row[x * 3U + 2U] = static_cast<char>(source[x * 4U + 0U]);
        }
        output.write(row.data(), static_cast<std::streamsize>(row.size()));
    }
    if (!output) {
        error = "failed to write " + path;
        return false;
    }
    return true;
}

} // namespace

int run_display(int argc, char** argv) {
    std::string image_path;
    std::string shot_path;
    std::string os_path;
    std::string bios_path;
    bool os_mode = false;
    bool embedded = false;
    std::uint64_t step_limit = 50'000'000ULL;

    for (int index = 1; index < argc; ++index) {
        const std::string argument = argv[index];
        if (argument == "--os") {
            os_mode = true;
            if (index + 1 < argc && argv[index + 1][0] != '-') {
                os_path = argv[++index];
            }
            continue;
        }
        if (argument == "--embedded") {
            embedded = true;
            continue;
        }
        if (argument == "--bios") {
            if (index + 1 >= argc) {
                std::fprintf(stderr, "kr32 display: --bios needs a path\n");
                return 2;
            }
            bios_path = argv[++index];
            continue;
        }
        if (argument == "--screen-shot") {
            if (index + 1 >= argc) {
                std::fprintf(stderr, "kr32 display: --screen-shot needs a path\n");
                return 2;
            }
            shot_path = argv[++index];
            continue;
        }
        if (argument == "--steps") {
            if (index + 1 >= argc) {
                std::fprintf(stderr, "kr32 display: --steps needs a count\n");
                return 2;
            }
            step_limit = std::strtoull(argv[++index], nullptr, 10);
            continue;
        }
        if (argument == "--help" || argument == "-h") {
            std::printf(
                "Usage: kr32 display <image.k32> [--screen-shot out.ppm] [--steps N]\n"
                "       kr32 display --os [kernel.bin] [--bios bios.bin] [--embedded]\n"
                "                         [--screen-shot out.ppm]\n"
                "Keys: Esc quits, F2 cycles 1x/2x/3x zoom.\n");
            return 0;
        }
        if (!argument.empty() && argument[0] == '-') {
            std::fprintf(stderr, "kr32 display: unknown option %s\n", argument.c_str());
            return 2;
        }
        image_path = argument;
    }

    if (os_mode && !image_path.empty()) {
        std::fprintf(stderr, "kr32 display: use either --os or an image path\n");
        return 2;
    }
    if (!os_mode && image_path.empty()) {
        std::fprintf(stderr, "kr32 display: expected an image or --os\n");
        return 2;
    }
    if (embedded && !os_mode) {
        std::fprintf(stderr, "kr32 display: --embedded requires --os\n");
        return 2;
    }

    core::Machine machine(core::Machine::Options{core::kDefaultRamSize});
    core::DeviceBus devices;
    devices.attach_machine(machine);
    machine.attach_devices(&devices);

    std::string error;
    if (os_mode) {
        os::ImagePaths paths;
        if (!os::resolve(argv[0], os_path, bios_path, paths, error, embedded)) {
            std::fprintf(stderr, "kr32 display: %s\n", error.c_str());
            return 1;
        }
        if (!load_os_machine(machine, paths, error)) {
            std::fprintf(stderr, "kr32 display: %s\n", error.c_str());
            return 1;
        }
    } else {
        container::Image image;
        // The flat map covers the low 4 MiB, so put the stack inside it.
        if (!read_container(image_path, image, error) ||
            !load_executable(machine, image, 0x0020'0000U, error)) {
            std::fprintf(stderr, "kr32 display: %s\n", error.c_str());
            return 1;
        }
        install_display_map(machine);
    }

    if (!shot_path.empty()) {
        for (std::uint64_t step = 0; step < step_limit && !machine.halted(); ++step) {
            (void)machine.step();
        }
        if (!write_screen_shot(shot_path, devices.framebuffer(), error)) {
            std::fprintf(stderr, "kr32 display: %s\n", error.c_str());
            return 1;
        }
        return 0;
    }

    kr32::platform::Window window;
    if (!window.create("KR32 Display", 800, 600)) {
        return 1;
    }

    int scale = 1;
    std::uint32_t last_frame = 0;
    bool presented = false;
    bool running = true;
    while (running) {
        for (int slice = 0; slice < 100000 && !machine.halted(); ++slice) {
            (void)machine.step();
        }
        kr32::platform::Event event;
        while (window.poll(event)) {
            if (event.kind == kr32::platform::Event::Kind::Quit) {
                running = false;
            } else if (event.kind == kr32::platform::Event::Kind::KeyDown) {
                if (event.key == kr32::platform::key::Escape) {
                    running = false;
                } else if (event.key == kr32::platform::key::F2) {
                    scale = scale % 3 + 1;
                    presented = false;
                } else {
                    (void)devices.keyboard().push_event(
                        kr32::platform::event_word(event));
                }
            } else if (event.kind == kr32::platform::Event::Kind::KeyUp) {
                (void)devices.keyboard().push_event(
                    kr32::platform::event_word(event));
            }
        }
        const core::FramebufferDevice& framebuffer = devices.framebuffer();
        if (framebuffer.frame_number() != 0 &&
            (!presented || framebuffer.frame_number() != last_frame)) {
            const std::span<const std::uint8_t> pixels = framebuffer.frame_pixels();
            if (!pixels.empty()) {
                window.present_surface(
                    pixels.data(), static_cast<int>(framebuffer.frame().width),
                    static_cast<int>(framebuffer.frame().height),
                    static_cast<int>(framebuffer.frame().stride), scale);
            }
            last_frame = framebuffer.frame_number();
            presented = true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(16));
    }
    return 0;
}

} // namespace kr32::ui
