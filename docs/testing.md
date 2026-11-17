# Testing workflow

The full suite used to spend most of its wall time in one `unit` CTest entry.
CTest now registers one entry per test source file using doctest's
`--source-file` filter. The two largest suites are split into eight buckets
each, so `ctest -j` uses the machine instead of running one long process:

- `unit/test_cc_1.cpp` … `test_cc_8.cpp`
- `cxx/test_cxx_codegen_1.cpp` … `test_cxx_codegen_8.cpp`

The `check` and `check-unit` targets query the local processor count with
CMake's `ProcessorCount` and pass it as `ctest -j`. On the 24-logical-processor
development machine, both platforms complete in well under a minute.

Measured wall time:

| Slice | Windows Debug (24-way) | Linux GCC (24-way) |
| --- | --- | --- |
| `unit_test_cc_*` | 10-42 s per bucket | 2-6 s per bucket |
| `unit_test_cxx_codegen_*` | 15-48 s per bucket | 2-5 s per bucket |
| `unit_test_debug`, `unit_test_host` | 15-18 s each | 5-7 s each |
| everything else | under 10 s | under 5 s |
| **whole `check` target** | **~49 s** | **~12 s** |

Before the split, the monolithic `unit` entry alone took 4-5 minutes. Full
parallel CTest now runs on every normal change, but focused filters are still
faster while iterating.

## Fast edit loop

Build only the test executable; it is the only target needed for compiler
unit tests:

```powershell
cmake --build --preset windows-msvc-debug --target kr32_tests
```

Run the cases that changed by name substring:

```powershell
.\build\windows-msvc\tests\Debug\kr32_tests.exe --test-case="*string prefix*"
```

Or run an affected slice:

```powershell
ctest --test-dir .\build\windows-msvc -C Debug -R "unit_test_cc_" --output-on-failure -j8
ctest --test-dir .\build\windows-msvc -C Debug -R "unit_test_cxx_codegen_" --output-on-failure -j8
```

Linux uses the same names:

```bash
cmake --build build/linux-gcc -j8 --target kr32_tests
ctest --test-dir build/linux-gcc -R "unit_test_cc_" --output-on-failure -j8
```

## Full parallel suite

```powershell
cmake --build --preset windows-msvc-debug --target check
```

The target depends on `kr32` and `kr32_tests`, passes the active build
configuration, and runs the whole CTest tree in parallel at the detected CPU
count. Equivalent direct command:

```powershell
ctest --test-dir .\build\windows-msvc -C Debug -j24 --output-on-failure
```

Every test preset runs four jobs, so `ctest --preset <preset>` is parallel
without a `-j` argument. Four is what the CI runners and the Arm64 gate
provide; the sanitizer gate needs it, because eight jobs oversubscribe a
four-core machine and its CPU-heavy codegen buckets cross the 600 s watchdog.
Local runs can pass a larger `-j` explicitly; the timings below were taken
with `-j8` on a 24-core machine. Measured on an otherwise idle machine, all 96
tests:

| Platform | build (incremental) | tests | total |
| --- | ---: | ---: | ---: |
| Windows | 2.3 s | 220.3 s | **222.6 s** |
| WSL | 0.8 s | 86.5 s | **87.3 s** |

Before the two changes below the same suite took 388 s of test time on WSL and
383 s on Windows, so this is 4.5x and 1.7x.

A clean build of the whole tree, measured in a separate directory so the working
build was untouched, is **160 s** on Windows (4.4 s configure plus 159.9 s
compile). A compiler cache would only pay off against that figure: nothing
installed on this machine provides one (`ccache` and `sccache` are both absent
from `PATH` on Windows and in WSL), and an incremental gate build is already
2.3 s, so the cache would not touch the case this gate actually runs.

The OS boot/user tests no longer build the images themselves. `kr32os_images` is a
CTest `FIXTURES_SETUP` test that runs `kr32os/build.ps1` once, and every test that
boots the OS declares `FIXTURES_REQUIRED kr32os_images` and passes
`-DSKIP_BUILD=1`, so the build happens once per gate instead of once per test.
They also no longer hold a `RESOURCE_LOCK`: they only read the tree the fixture
wrote.

The difference is large because a full KR32OS build was most of each test's cost:
`run_os_shutdown` went from 65 s to 3.4 s on Windows, and the CPU time of the
label covering the OS tests from 203 to 59 seconds.

`check-unit` runs only the doctest slices:

```powershell
cmake --build --preset windows-msvc-debug --target check-unit
```

## Bounded parts

The suite is also grouped into eleven labelled parts, so one invocation can be
bounded instead of running the whole tree. Every part finishes in under three
minutes on the current development host (Hukilau, measured 2026-11-15 with
`ctest -L <part> -j 2`):

| Part | Contents | Tests | Measured |
| --- | --- | ---: | ---: |
| `part01` | `unit_test_cxx_codegen_2b`, `unit_test_optimizer` | 2 | 141 s |
| `part02` | `unit_test_cxx_codegen_2c`, `encoding`, `golden_vectors`, `version`, `matrix`, `boot`, `fp` | 7 | 81 s |
| `part03` | `unit_test_cxx_codegen_2`, `cc_1` | 2 | 90 s |
| `part04` | `unit_test_cxx_codegen_4`, `debug`, `disassembler` | 3 | 70 s |
| `part05` | `unit_test_cxx_codegen_3`, `cxx_codegen_1`, `host` | 3 | 76 s |
| `part06` | `unit_test_cxx_codegen_5`, `cxx_codegen_8`, `cxx_codegen_7`, `cc_8` | 4 | 96 s |
| `part07` | `unit_test_cxx_codegen_6`, `cc_2`, `cc_6`, `cc_3`, `threads` | 5 | 55 s |
| `part08` | the light unit buckets (`cc_4`, `cc_5`, `cc_7`, `ui`, `fast`, `machine`, ...) | 19 | 30 s |
| `part09` | the light integration tests (`build_*`, `run_c*`, `run_cxx_*`, `cli_*`, `ui_*`) | 41 | 22 s |
| `part10` | `os_console_screen`, `run_os_boot`, `run_os_embedded`, `os_gfx_check` | 4 | 101 s |
| `part11` | `run_os_user`, `run_os_fault_timer`, `run_os_shell`, `run_os_shutdown`, `run_os_gfx_dismiss`, `os_gfx_demo` | 6 | 167 s |

`part10` and `part11` boot the OS in several configurations. The shell-driven
runs end with `exit 0` and assert the host exit status, so they stop at the halt
rather than at the step budget. The two graphics programs wait for a key that the
harness cannot send, so `RunGfxProgram.cmake` samples them at 5,000,000 steps,
which is past the roughly 3,000,000 that draw the frame.

```powershell
$ctest = "C:\Program Files\Microsoft Visual Studio\18\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\ctest.exe"
foreach ($part in 1..11) {
    $label = "part{0:D2}" -f $part
    & $ctest --preset windows-msvc-debug -L $label -j 2
}
```

The assignment lives at the end of `tests/CMakeLists.txt` as
`KR32_TEST_PART_ROWS`, one row per part, `label|test|test|...`. Every test must
appear in exactly one row; a test missing from every row is left out of all
parts, which is why the rows are checked against `ctest -N` whenever they
change. The part labels are appended to `unit` and `integration`, so the
existing `-L` and `-E` filters keep working.

A row may also name a test that a configuration does not build — the UI suites
when `KR32_BUILD_UI` is off, for instance. Those rows are skipped rather than
aborting the configure step, and the configure output names each one:

```text
-- part part09: 'ui_headless' is not defined in this configuration
```

Read that list after a configure that turns an option off. The same message
appears for a test that was renamed without updating its row, which is the way a
test silently drops out of every part.

`test_cxx_codegen_2.cpp` accounts for most of the suite's wall time, so it is
split into `test_cxx_codegen_2.cpp`, `_2b.cpp`, and `_2c.cpp` (about 80 s, 110 s,
and 104 s). ctest registers one entry per source file, so splitting a bucket
needs no other change.

## Running Windows and WSL together

The two platforms use separate build and OS output trees, so they can run at the
same time. With 24 logical processors, start both with a bounded width:

```powershell
Start-Job {
    ctest --test-dir .\build\windows-msvc -C Debug -j12 --output-on-failure
}
wsl.exe -e bash -lc 'cd /mnt/c/Users/kevin/source/repos/KR32 && ctest --test-dir build/linux-gcc -j12 --output-on-failure'
Receive-Job -Wait -AutoRemoveJob
```

On this machine the combined wall time is roughly the Windows side alone.

## Cadence

1. **Per edit:** build `kr32_tests`, run a name-filtered case (seconds).
2. **Per logical change:** run the affected slice, or `check` if the change
   touches shared compiler/codegen code.
3. **Before commit / once per round batch:** run `check` on Windows and the
   Linux CTest suite, concurrently or sequentially.
4. **Rare:** run serially only when diagnosing ordering or shared-resource
   failures.

## Adding tests

Add a new `tests/**/test_*.cpp` file to `KR32_TEST_SOURCES` in
`tests/CMakeLists.txt`; the per-source-file loop registers it automatically.
Keep a file under roughly a minute of runtime, or split it into numbered
buckets with a shared support header.

Then add the new test name to exactly one `KR32_TEST_PART_ROWS` row at the end
of `tests/CMakeLists.txt`. Choose the part by the measured runtime, keeping the
part total under three minutes; re-measure the affected part and update the
table above.

## Display and console

| Test | What it covers |
| --- | --- |
| `display_screen_shot` | Builds `examples/color-bars.asm`, captures the committed frame twice with `kr32 display --screen-shot`, and requires byte-identical PPMs |
| `ui_headless_os` | Renders the debugger with an OS image so the `SCREEN` tab and its "no committed frame" path are exercised without booting |

The OS tests build KR32OS and run it with `--max-steps 12000000` (`24000000`
for `run_os_fault_timer`). The kernel's console redraw is the reason: a
full-screen fill or scroll is hundreds of thousands of pixel stores at roughly
14 instructions each, so boot alone takes about 7M steps. Lower the budgets
once the redraw is faster.
