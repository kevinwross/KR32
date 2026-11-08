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

The OS boot/user tests write the same `kr32os/build` tree, so they share a CTest
`RESOURCE_LOCK` and run one at a time even under `-j`.

`check-unit` runs only the doctest slices:

```powershell
cmake --build --preset windows-msvc-debug --target check-unit
```

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
