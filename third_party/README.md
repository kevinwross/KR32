# Third-party dependencies

This directory holds small vendored headers and local SDK installs. The SDK
install directories are gitignored; the install scripts and this note are
committed.

## SDL2 (Windows)

- Version: **2.32.10**, `SDL2-devel-2.32.10-VC.zip`.
- Location: `third_party/SDL2` (gitignored).
- Install:

  ```powershell
  powershell -ExecutionPolicy Bypass -File scripts/install-sdl2.ps1
  ```

- CMake: `find_package(SDL2 CONFIG REQUIRED)` and link `SDL2::SDL2`. Define
  `SDL_MAIN_HANDLED` when the application supplies a normal `main`; otherwise
  the Windows SDL2main path expects `SDL_main`.
- Headers are flat in this VC package: use `#include <SDL.h>`. The CMake target
  adds the include directory; on Linux the same include works because
  pkg-config adds `-I/usr/include/SDL2`.
- License: zlib. See `third_party/SDL2/LICENSE.txt`.
- The provenance manifest does not attest this tree: `provenance.json` names it
  under `ignore`, because a fetched SDK has no checked-in origin to record.
- The DLL must be next to the executable at run time; the CMake build will copy
  it in a post-build step.

## SDL2 (Linux / ARM64)

Use the system package instead of this directory:

```bash
sudo apt install libsdl2-dev pkg-config
```

## doctest

Planned: a pinned `third_party/doctest.h` (MIT), vendored when the test harness
lands. Record the exact version here when it is added.
