#include "doctest.h"

#include "kr32/cc/cc.h"
#include "kr32/cc/guest_headers.h"
#include "kr32/cxx/cxx.h"

#include <fstream>
#include <iterator>
#include <string>
#include <string_view>

using namespace kr32;

TEST_CASE("the guest header files carry the injected declarations") {
    // The built-in front ends are filesystem-free and inject these bodies; krc
    // reads include/kr32/*.h. `con.h` is generated into the build tree because
    // its name is a reserved DOS device, so the repository cannot track it. One
    // body, two carriers: this test is what stops them drifting apart.
    struct Entry {
        const char* file;
        std::string_view text;
        bool generated;
    };
    const Entry entries[] = {
        {"fb.h", cc::kGuestFbHeader, false},
        {"display.h", cc::kGuestDisplayHeader, false},
        {"gfx.h", cc::kGuestGfxHeader, false},
        {"con.h", cc::kGuestConHeader, true},
        {"input.h", cc::kGuestInputHeader, false},
        {"sys.h", cc::kGuestSysHeader, false},
    };
    for (const Entry& entry : entries) {
        const std::string directory =
            entry.generated
                ? std::string(KR32_GENERATED_GUEST_INCLUDE_DIR) + "/kr32"
                : std::string(KR32_SOURCE_DIR) + "/include/kr32";
        const std::string path = directory + "/" + entry.file;
        std::ifstream input(path, std::ios::binary);
        INFO(entry.file);
        REQUIRE_MESSAGE(input, "cannot open " << path);
        const std::string contents((std::istreambuf_iterator<char>(input)),
                                   std::istreambuf_iterator<char>());
        CHECK(contents.find(entry.text) != std::string::npos);
    }
}

TEST_CASE("the c front end declares the guest headers") {
    const cc::Result result = cc::compile(
        "#include <kr32/fb.h>\n"
        "#include <kr32/display.h>\n"
        "#include <kr32/gfx.h>\n"
        "#include <kr32/con.h>\n"
        "#include <kr32/input.h>\n"
        "#include <kr32/sys.h>\n"
        "int main(void) {\n"
        "    fb_info info;\n"
        "    info.width = 800;\n"
        "    gfx_surface surface;\n"
        "    surface.offset = 0;\n"
        "    surface.clip_x = 0;\n"
        "    gfx_surface *created = gfx_create(16, 16);\n"
        "    gfx_fill_rect(created, 0, 0, 4, 4, 0x00FF0000U);\n"
        "    gfx_line(created, 0, 0, 3, 3, 0x0000FF00U);\n"
        "    gfx_text(created, 0, 0, \"hi\", 0x00FFFFFFU, 0);\n"
        "    con_puts(\"hello\");\n"
        "    kr32_event event;\n"
        "    (void)input_poll(&event);\n"
        "    (void)fb_info_get(&info);\n"
        "    (void)sys_getpid();\n"
        "    return (int)created + KR32_STDOUT;\n"
        "}\n");
    std::string detail = "compile failed";
    if (!result.diagnostics.empty()) {
        detail = result.diagnostics.front().message;
    }
    CHECK_MESSAGE(result.ok, detail);
    CHECK(result.assembly.find("gfx_fill_rect") != std::string::npos);
    CHECK(result.assembly.find("gfx_line") != std::string::npos);
    CHECK(result.assembly.find("gfx_text") != std::string::npos);
}

TEST_CASE("the c front end accepts a repeated guest header") {
    const cc::Result result = cc::compile(
        "#include <kr32/gfx.h>\n"
        "#include <kr32/gfx.h>\n"
        "int main(void) { gfx_surface *s = gfx_create(4, 4); return (int)s; }\n");
    std::string detail = "compile failed";
    if (!result.diagnostics.empty()) {
        detail = result.diagnostics.front().message;
    }
    CHECK_MESSAGE(result.ok, detail);
}

TEST_CASE("the c front end warns on an unknown header and keeps compiling") {
    const cc::Result result = cc::compile(
        "#include <kr32/no_such_header.h>\n"
        "int main(void) { return 7; }\n");
    CHECK(result.ok);
    bool warned = false;
    for (const cc::Diagnostic& diagnostic : result.diagnostics) {
        warned = warned || diagnostic.message.find("unknown header") != std::string::npos;
    }
    CHECK(warned);
}

TEST_CASE("the c++ front end declares the guest headers") {
    const cxx::Result result = cxx::compile(
        "#include <kr32/gfx.h>\n"
        "#include <kr32/con.h>\n"
        "int main() {\n"
        "    gfx_surface *surface = gfx_create(16, 16);\n"
        "    gfx_fill_rect(surface, 0, 0, 4, 4, 0x00FF0000U);\n"
        "    gfx_text(surface, 0, 0, \"hi\", 0x00FFFFFFU, 0);\n"
        "    con_puts(\"hello\");\n"
        "    return (int)surface;\n"
        "}\n");
    std::string detail = "compile failed";
    if (!result.diagnostics.empty()) {
        detail = result.diagnostics.front().message;
    }
    CHECK_MESSAGE(result.ok, detail);
    CHECK(result.assembly.find("gfx_fill_rect") != std::string::npos);
}

TEST_CASE("the c++ front end still rejects an unknown header") {
    const cxx::Result result = cxx::compile(
        "#include <kr32/no_such_header.h>\n"
        "int main() { return 0; }\n");
    CHECK_FALSE(result.ok);
}
