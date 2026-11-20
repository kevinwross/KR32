# ARM64 test log

## 2026-11-20T03:29:39-08:00 — adopt-krc @ ac21873

- Host: BitWhiz
- Range: first run (main 22eb0be..9705ef6, 3 commits; adopt-krc adds the
  docs-only ac21873)
- Docs-only: no — the gate covers the three main commits; ac21873 itself only
  adds `docs/arm64-tester.md`
- Configure: linux-arm64-gcc — ok, 1 s
- Build: linux-arm64-gcc — ok, 20 s (--parallel 3)
- Test: linux-arm64-gcc — 97/97 passed, 155 s
- Skipped: none
- Next: nothing pending

## 2026-11-20T09:51:07-08:00 — adopt-krc @ adaef4f

- Host: BitWhiz
- Range: 0e98ae6..adaef4f, 3 commits (krc toolchain behind `KR32_TOOLCHAIN`,
  built-in listing writer guard, stage-A krc record)
- Docs-only: no
- Configure: linux-arm64-gcc — ok, 0 s
- Build: linux-arm64-gcc — ok, 9 s (--parallel 3)
- Test: linux-arm64-gcc — 98/98 passed, 94 s
- Skipped: the `KR32_TOOLCHAIN=krc` configuration. It needs an external
  `krc-toolchain` package plus a staged sysroot and runtime directory, none of
  which are present on this host; the default `builtin` configuration is what
  was built and tested.
- Next: nothing pending

## 2026-11-20T10:12:21-08:00 — adopt-krc @ 1a69a9f

- Host: BitWhiz
- Range: adaef4f..1a69a9f, 4 commits (guest headers shipped as files plus the
  generator tool; two of the commits are documentation only)
- Docs-only: no
- Configure: linux-arm64-gcc — ok, 1 s
- Build: linux-arm64-gcc — ok, 73 s (--parallel 3)
- Test: linux-arm64-gcc — 98/98 passed, 110 s
- Skipped: the `KR32_TOOLCHAIN=krc` configuration, which still needs the
  external `krc-toolchain` package and a staged sysroot/runtime.
- Next: nothing pending
## 2026-11-20T10:21:31-08:00 — adopt-krc @ 7a61479

- Host: BitWhiz
- Range: 1a69a9f..7a61479, 2 commits (krc external-input inventory in
  `CMakeLists.txt`; documentation)
- Docs-only: no — `CMakeLists.txt` changed, but only inside the
  `KR32_TOOLCHAIN=krc` branch, so the compiled `builtin` tree is unchanged
- Configure: linux-arm64-gcc — ok, 0 s
- Build: linux-arm64-gcc — ok, 0 s (ninja: no work to do; CMake-only change)
- Test: linux-arm64-gcc — 98/98 passed, 128 s
- Skipped: the `KR32_TOOLCHAIN=krc` configuration, which still needs the
  external `krc-toolchain` package and a staged sysroot/runtime.
- Next: nothing pending
