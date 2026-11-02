#include "kr32/cli/cli.h"

#include "kr32/version.h"

#include <cstdio>
#include <cstring>
#include <string_view>

#if defined(KR32_HAS_UI)
#include "kr32/ui/app.h"
#endif

namespace kr32::cli {
namespace {

void print_version() {
    std::printf("kr32 %s (ISA %s, container %s, M0 scaffold)\n",
                kr32::kVersion, kr32::kIsaVersion, kr32::kContainerVersion);
}

void print_help() {
    std::printf(
        "Usage: kr32 <command> [options]\n"
        "\n"
        "Commands:\n"
        "  version          print the version tuple\n"
        "  help             print this help\n"
        "  as <file>        assemble (M1)\n"
        "  ld <files>       link (M3)\n"
        "  build <file>     compile/assemble/link (M3)\n"
        "  cc <file>        compile C (M4)\n"
        "  cxx <file>       compile C++ (M9)\n"
        "  vm <file>        run an image (M1)\n"
        "  run <file>       alias for vm\n"
        "  dis <file>       disassemble (M1)\n"
        "  size <file>      section sizes (M3)\n"
        "  debug <file>     dump debug information (M5)\n"
        "  dap <file>       Debug Adapter Protocol server (M7)\n"
        "  shell            interactive shell (M7)\n"
#if defined(KR32_HAS_UI)
        "  gui              open the windowed debugger (M6)\n"
        "  --smoke-ui       initialize and tear down the GUI platform\n"
#else
        "  gui              not built (KR32_BUILD_UI=OFF)\n"
        "  --smoke-ui       not built (KR32_BUILD_UI=OFF)\n"
#endif
        "  version, help\n");
}

int not_implemented(std::string_view command) {
    std::fprintf(stderr, "kr32: '%s' is not implemented yet (M0 scaffold).\n",
                 std::string_view(command).data());
    return 2;
}

} // namespace

int run(int argc, char** argv) {
    if (argc <= 1) {
#if defined(KR32_HAS_UI)
        return kr32::ui::run(argc, argv);
#else
        print_help();
        return 0;
#endif
    }

    const std::string_view command(argv[1]);

    if (command == "version" || command == "--version") {
        print_version();
        return 0;
    }
    if (command == "help" || command == "--help" || command == "-h") {
        print_help();
        return 0;
    }

#if defined(KR32_HAS_UI)
    if (command == "gui") {
        return kr32::ui::run(argc - 1, argv + 1);
    }
    if (command == "--smoke-ui") {
        const bool ok = kr32::ui::smoke_test();
        std::printf("ui-smoke: %s\n", ok ? "ok" : "failed");
        return ok ? 0 : 1;
    }
#else
    if (command == "gui" || command == "--smoke-ui") {
        std::fprintf(stderr, "kr32: the GUI was not built (KR32_BUILD_UI=OFF).\n");
        return 2;
    }
#endif

    for (const std::string_view candidate :
         {"as", "ld", "build", "cc", "cxx", "vm", "run", "dis", "size",
          "debug", "dap", "shell"}) {
        if (command == candidate) {
            return not_implemented(command);
        }
    }

    std::fprintf(stderr, "kr32: unknown command '%.*s'.\n",
                 static_cast<int>(command.size()), command.data());
    print_help();
    return 2;
}

} // namespace kr32::cli
