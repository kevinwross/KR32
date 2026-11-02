#pragma once

namespace kr32::platform {

// Initializes the platform layer and immediately shuts it down. It exists so
// M0 can prove the SDL2 dependency, include path, and link step without
// committing to a window design yet.
[[nodiscard]] bool smoke_test();

} // namespace kr32::platform
