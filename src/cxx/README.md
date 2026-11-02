# kr32_cxx

The KR32 C++ toolchain module. The design contract for M9 is
[`docs/cpp-plan.md`](../../docs/cpp-plan.md); milestone status stays in
[`implementation.md`](../../implementation.md).

The module is intentionally independent of the GUI/SDL layer. It currently
provides:

- `include/kr32/cxx/cxx.h`: the compile API.
- `src/cxx/cxx.cpp`: the front-end implementation stub.
- `src/cxx/tool/main.cpp`: the `kr32-cxx` standalone driver used for development
  and tests.

Build the standalone driver with the normal CMake presets; it is emitted as
`kr32-cxx` next to `kr32`.
