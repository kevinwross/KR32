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
## 2026-11-20T11:16:22-08:00 — adopt-krc @ 1de7012

- Host: BitWhiz
- Range: 7a61479..1de7012, 3 commits (consume the installed krc sysroot and
  runtime, with the matching UI changes; two are documentation/log commits)
- Docs-only: no
- Configure: linux-arm64-gcc — ok, 1 s
- Build: linux-arm64-gcc — ok, 11 s (--parallel 3)
- Test: linux-arm64-gcc — 98/98 passed, 118 s
- Skipped: the `KR32_TOOLCHAIN=krc` configuration. It now expects an installed
  krc package and sysroot, neither of which is present on this host; the
  default `builtin` configuration is what was built and tested.
- Next: nothing pending
## 2026-11-20T14:39:22-08:00 — adopt-krc @ abc5093

- Host: BitWhiz
- Range: 1de7012..abc5093, 5 commits (stage-B migration of the tests and
  guest headers; two of them are documentation)
- Docs-only: no
- Configure: linux-arm64-gcc — **FAILED**, 0 s:
  `CMake Error at CMakeLists.txt:30 (find_package): Could not find a package
  configuration file provided by "krc-toolchain"`
- Build: not run
- Test: not run
- Skipped: the krc-toolchain install. Stage B now requires the package whenever
  `KR32_BUILD_TESTS` is ON (`CMakeLists.txt:23-27`), and this host does not have
  it. The tester role does not install tools.
- Next: BLOCKED until krc-toolchain is installed on the Pi or the tests stop
  requiring it
## 2026-11-20T22:29:19-08:00 — adopt-krc @ ecba6ec (release 0.2.0)

- Host: BitWhiz
- Range: abc5093..ecba6ec, 5 commits (0.2.0 release prep, LICENSE, version
  bump to 0.2.0, install/packaging rules; the rest are documentation)
- Docs-only: no
- Configure: linux-arm64-gcc — **FAILED**, 0 s:
  `CMake Error at CMakeLists.txt (find_package): Could not find a package
  configuration file provided by "krc-toolchain"`
- Build: not run
- Test: not run
- Skipped: the krc-toolchain install. The release did not change the
  requirement: the package is still required whenever `KR32_BUILD_TESTS` is ON,
  and this host does not have it. The tester role does not install tools.
- Next: BLOCKED. ARM64 cannot verify the 0.2.0 release until krc-toolchain is
  installed on the Pi or the tests stop requiring it.
## 2026-11-20T22:44:29-08:00 — adopt-krc @ 3a406b9 (stage B)

- Host: BitWhiz
- Range: ecba6ec..3a406b9, 2 commits (stage B landed: the built-in C/C++
  front end, assembler, linker and libc are removed, about 44k lines)
- Docs-only: no
- Configure: linux-arm64-gcc — **FAILED**, 0 s:
  `CMake Error at CMakeLists.txt:13 (find_package): Could not find a package
  configuration file provided by "krc-toolchain"`
- Build: not run
- Test: not run
- Skipped: the krc-toolchain install. `find_package(krc-toolchain CONFIG
  REQUIRED)` is unconditional now and the repository has no in-tree fallback,
  so nothing builds here without the package. The tester role does not install
  tools.
- Next: BLOCKED. ARM64 cannot build at all until krc-toolchain (KRCompiler
  4.12+, KRC_BUILD_TOOLS=ON, exporting KRC_SYSROOT and KRC_KR32_RUNTIME_DIR) is
  provisioned on the Pi and CMAKE_PREFIX_PATH points at it.
