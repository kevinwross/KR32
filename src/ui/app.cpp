#include "kr32/ui/app.h"

#include "kr32/platform/window.h"

#include <cstdio>

namespace kr32::ui {

bool smoke_test() {
    return kr32::platform::smoke_test();
}

int run(int argc, char** argv) {
    (void)argc;
    (void)argv;
    std::fprintf(stderr, "kr32: the windowed debugger is not implemented yet (M0 scaffold).\n");
    return 2;
}

} // namespace kr32::ui
