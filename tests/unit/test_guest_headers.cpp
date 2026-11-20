#include "doctest.h"

#include <fstream>
#include <iterator>
#include <string>
#include <string_view>

namespace {

std::string read_header(const char* name) {
    const std::string path =
        std::string(KR32_SOURCE_DIR) + "/include/kr32/" + name;
    std::ifstream input(path, std::ios::binary);
    INFO(name);
    REQUIRE_MESSAGE(input, "cannot open " << path);
    return std::string((std::istreambuf_iterator<char>(input)),
                       std::istreambuf_iterator<char>());
}

} // namespace

TEST_CASE("the guest headers are tracked files with their declarations") {
    struct Entry {
        const char* file;
        std::string_view declaration;
    };
    const Entry entries[] = {
        {"fb.h", "int fb_probe(void);"},
        {"display.h", "int sys_display_present(void);"},
        {"gfx.h", "gfx_surface *gfx_create(int width, int height);"},
        {"cons.h", "void con_puts(const char *text);"},
        {"input.h", "int input_poll(kr32_event *event);"},
        {"sys.h", "int sys_write(int descriptor, const void *buffer, int length);"},
    };
    for (const Entry& entry : entries) {
        const std::string contents = read_header(entry.file);
        CHECK(contents.find(entry.declaration) != std::string::npos);
    }
}
