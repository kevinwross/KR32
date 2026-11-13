#pragma once

namespace kr32::ui {

// The M0 placeholder for the windowed debugger.
[[nodiscard]] int run(int argc, char** argv);

// The display-only window and the headless screen shot.
[[nodiscard]] int run_display(int argc, char** argv);

// Initializes and tears down the platform layer. Used by `kr32 --smoke-ui`.
[[nodiscard]] bool smoke_test();

} // namespace kr32::ui
