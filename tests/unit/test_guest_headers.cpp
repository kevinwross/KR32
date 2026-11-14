#include "doctest.h"

#include "kr32/cc/cc.h"
#include "kr32/cxx/cxx.h"

#include <string>

using namespace kr32;

TEST_CASE("the c front end declares the guest headers") {
    const cc::Result result = cc::compile(
        "#include <kr32/fb.h>\n"
        "#include <kr32/display.h>\n"
        "#include <kr32/gfx.h>\n"
        "#include <kr32/con.h>\n"
        "#include <kr32/input.h>\n"
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
        "    return (int)created;\n"
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
