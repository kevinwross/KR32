#include "kr32/cli/cli.h"

#include <cstdio>
#include <exception>

int main(int argc, char** argv) {
    try {
        return kr32::cli::run(argc, argv);
    } catch (const std::exception& error) {
        std::fprintf(stderr, "kr32: %s\n", error.what());
        return 1;
    } catch (...) {
        std::fprintf(stderr, "kr32: unknown fatal error\n");
        return 1;
    }
}
