# Testing workflow

The full suite used to spend most of its wall time in one `unit` CTest entry.
CTest now registers one entry per test source file using doctest's
`--source-file` filter. The large C++ codegen file is split into four buckets,
so `ctest -j` can run independent suites concurrently.

Measured wall time with 8-way CTest:

| Slice | Windows Debug | Linux GCC |
| --- | --- | --- |
| `unit_test_cc` | ~80 s | ~22 s |
| four `unit_test_cxx_codegen_*` buckets | 45-65 s each | 8-12 s each |
| `unit_test_debug`, `unit_test_host` | 15-18 s each | 5-6 s each |
| everything else | under 10 s | under 5 s |
| **whole `check` target** | **~82 s** | **~24 s** |

Before the split, the monolithic `unit` entry alone took 4-5 minutes. Full
parallel CTest is now fast enough to run on a normal change, but the focused
loops below are still much faster.

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

Or run a whole compiler slice:

```powershell
ctest --test-dir .\build\windows-msvc -C Debug -R unit_test_cc --output-on-failure
ctest --test-dir .\build\windows-msvc -C Debug -R "unit_test_cxx_codegen_" --output-on-failure -j4
```

Linux uses the same names:

```bash
cmake --build build/linux-gcc -j2 --target kr32_tests
ctest --test-dir build/linux-gcc -R unit_test_cc --output-on-failure
```

## Parallel full suite

```powershell
cmake --build --preset windows-msvc-debug --target check
```

or directly:

```powershell
ctest --test-dir .\build\windows-msvc -C Debug -j8 --output-on-failure
```

`check` depends on `kr32` and `kr32_tests`, passes the build configuration, and
runs the whole CTest tree in parallel.

The OS boot/user tests write the same `kr32os/build` tree, so they share a CTest
`RESOURCE_LOCK` and run one at a time even under `-j`.

`check-unit` runs only the doctest source-file slices:

```powershell
cmake --build --preset windows-msvc-debug --target check-unit
```

## Cadence

1. **Per edit:** build `kr32_tests`, run a name-filtered case (seconds).
2. **Per logical change:** run the affected source-file slice
   (`unit_test_cc`, one `unit_test_cxx_codegen_*` bucket, etc.).
3. **Before commit / once per round batch:** run `check` on Windows or Linux.
   Both platforms in the same round is cheap now but not mandatory for a doc or
   single-header change.
4. **Rare:** run the whole suite serially only when diagnosing ordering or
   shared-resource failures.

## Adding tests

Add a new `tests/**/test_*.cpp` file to `KR32_TEST_SOURCES` in
`tests/CMakeLists.txt`; the per-source-file loop registers it automatically.
Keep the file under roughly a minute of runtime, or split it into numbered
buckets like `test_cxx_codegen_*.cpp` with a shared support header.
