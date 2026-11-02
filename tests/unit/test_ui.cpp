#include "doctest.h"

#include "kr32/ui/canvas.h"

using namespace kr32;

TEST_CASE("the portable canvas renders deterministic frames") {
    ui::Canvas first(320, 200);
    first.clear(ui::Color{10, 20, 30, 255});
    first.fill(ui::Rect{4, 4, 100, 20}, ui::Color{40, 60, 80, 255});
    first.frame(ui::Rect{4, 4, 100, 20}, ui::Color{80, 120, 160, 255});
    first.text(10, 10, "KR32", ui::Color{220, 230, 240, 255});
    first.line(0, 0, 319, 199, ui::Color{90, 100, 110, 255});

    ui::Canvas second(320, 200);
    second.clear(ui::Color{10, 20, 30, 255});
    second.fill(ui::Rect{4, 4, 100, 20}, ui::Color{40, 60, 80, 255});
    second.frame(ui::Rect{4, 4, 100, 20}, ui::Color{80, 120, 160, 255});
    second.text(10, 10, "KR32", ui::Color{220, 230, 240, 255});
    second.line(0, 0, 319, 199, ui::Color{90, 100, 110, 255});

    CHECK(first.hash() == second.hash());
    CHECK(first.hash() != 0);
    CHECK(first.width() == 320);
    CHECK(first.height() == 200);
    CHECK(ui::Canvas::text_width("KR32") > 0);
    CHECK(ui::Canvas::text_height(ui::FontRole::Mono) > 0);
}

TEST_CASE("the portable canvas clips drawing to its bounds") {
    ui::Canvas canvas(16, 16);
    canvas.clear(ui::Color{0, 0, 0, 255});
    canvas.fill(ui::Rect{-10, -10, 100, 100}, ui::Color{255, 255, 255, 255});
    CHECK(canvas.pixel_at(0, 0) == 0xFFFFFFFFU);
    canvas.pixel(-1, -1, ui::Color{255, 0, 0, 255});
    canvas.pixel(16, 16, ui::Color{255, 0, 0, 255});
    CHECK(canvas.pixel_at(0, 0) == 0xFFFFFFFFU);
}
