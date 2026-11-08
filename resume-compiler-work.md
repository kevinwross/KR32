# Resume notes: C/C++ compiler work

**Updated:** 2026-11-07 19:08 America/Los_Angeles
**Repository:** `C:\Users\kevin\source\repos\KR32`
**Last feature commit:** `46bab01` (`cxx: convert scalars in returns and comparisons`).
**Goal:** “Continue to improve the C and C++ compilers to support the system”
**Goal status:** the harness goal is still marked `paused`. The user requested
resume, but `update_goal action=resume` is rejected for the model; the user must
use the UI resume control. Work can continue manually in the meantime.

## Last verified state

- Windows Debug CTest: **43/43 passed**
- Linux GCC CTest (WSL): **43/43 passed**
- Unit suite: **420 test cases, 7,077 assertions**, all passing
- Working tree clean except pre-existing untracked `node_modules/`,
  `package.json`, and `package-lock.json` (not part of compiler work; leave
  untracked)

Most recent compiler feature, round 69:

```cpp
class Flag {
public:
    int value;
    Flag(int input) : value(input) {}
    operator bool() const { return value != 0; }
};

bool truth(Flag flag) { return flag; }   // conversion in a return
Flag zero(0);
if (zero == 0) { ... }                  // conversion in equality comparison
```

Round 68 added:

```cpp
Number number(7);
int converted = number;   // copy initialization
take(number);             // scalar function argument
```

Changes in `src/cxx/codegen.cpp`:

- `conversion_cost` accepts class → scalar via a conversion operator
- `gen_scalar_conversion` invokes the matching conversion operator, with an
  `operator bool` fallback for integer targets
- Copy initialization, scalar/FP/long-long arguments, scalar returns, and
  equality comparisons use it

Round 67 added conversion operators themselves:

- Conditions (`if (flag)`)
- Explicit casts (`(bool)flag`, `(int)number`)
- Conversion-operator mangling (`operator bool` → `operator_cast_bool`)

## Recent rounds already committed

Approximate order, newest first:

- `46bab01` — cxx: convert scalars in returns and comparisons
- `e0ca3b2` — build: embed KR32OS images in kr32 (BIOS/kernel are now
  embedded by default, with platform-separated OS build outputs)
- `136c246` — cxx: convert scalars in initialization and arguments
- `9f47292` — cxx: support conversion operators
- `9f0665d` — cc: support common compiler builtins
- `c14069d` — cc: support bit-fields
- `50fe845` — cc: support variadic macros and comma expressions
- `2bf397a` — cc: support anonymous struct and union members
- `e0ac2ac` / `d56408d` — compound literals, then correct struct-parameter copy semantics
- `e3939a9` — standard predefined macros (`__STDC__`, `__DATE__`, etc.)
- `1c4f0c9` — `__LINE__` / `__FILE__`
- `55d9691` — `__func__`
- `52861be` — overloaded `operator->`
- `d36795a` — dereference as lvalue (`(*this) += value`)
- `5f3e4f6` — `std::string::append(const string&)`, `swap`
- `6d2b08c` — `std::string::at`, one-argument `substr`
- `93568ab` — `strnlen`, `strndup`
- `c11e7b4` — `starts_with`, `ends_with`, `find_first_of`, `find_last_of`
- `48b887d` — `std::string::insert`, `erase`
- `1fd9789` — string + C-string overloads
- `9e19a85` — disambiguate parenthesized comparisons (`!(a < b)`)
- `39ebd7a` — string relational operators
- `e44f71c` — string self search/compare overloads
- `485007d` — self types inside namespaces
- `d70a820` — `std::string` self append
- `115e66a` — aggregate initializers from calls
- `77ae715` — struct assignment
- `034848a` — function-pointer array calls
- `b0a7502` — stack-passed fixed C arguments

## Key compiler architecture reminders

### C compiler: `src/cc/cc.cpp`

- Single-pass: parsing and codegen are interleaved.
- Synthetic headers are emitted from `preprocess` / directives in the same file.
- Runtime libc is `src/cc/libc.c`, compiled by this C compiler and appended by
  `runtime_assembly()`.
- C structs/unions carry `Type::members` with offsets. Recent additions:
  - `Member::promoted` for anonymous struct/union members
  - `Type::bit_width`, `bit_offset`, `is_bitfield`
- Struct/union parameters are passed as object addresses and **copied into
  callee locals**, preserving C by-value semantics. Do not remove that copy.
- Function-pointer ABI: pointer in `R0`; indirect call uses `R6`; first 4 args in
  `R0`–`R3`; extras on stack at
  `R7 + __FRAME_<name>__ + 8 + (index-4)*8`.
- Aggregate assignment/initialization copies bytes; source in `R0`, destination
  in `R1`.
- `gen_expression()` = assignment precedence. `gen_comma_expression()` adds the
  comma operator only where commas are legal (currently parenthesized
  expressions). Do not make `gen_expression()` consume commas or function
  arguments break.
- `__extension__` is handled at `gen_expression()` top so statements still see
  lvalues.
- `__func__`, `__LINE__`, `__FILE__`, `__DATE__`, `__TIME__`,
  `__STDC__`, `__STDC_HOSTED__`, `__STDC_VERSION__` are handled directly in the
  primary parser.
- `compile_time_stamp` uses `localtime_s` on Windows and `localtime` elsewhere;
  `local_storage` is `[[maybe_unused]]` to keep Linux `-Werror` happy.

### C++ compiler: `src/cxx/{parser,codegen,preprocessor}.cpp`

- `preprocessor.cpp` builds synthetic headers, including `<iostream>`,
  `<fstream>`, `<string>`, and C headers.
- `std::string` is a compact heap-backed subset:
  - fields `char* data_`, `int size_`
  - self-type parameters sometimes need `const std::string&` qualification
    inside `namespace std`; a previous compiler bug was fixed, so unqualified
    self types should work now.
- Conversion operators:
  - Parser names them `"operator bool"`, `"operator int"`, etc.
  - `mangle_component` turns `"operator <type>"` into a space-free
    `operator_cast_<chars>` symbol.
  - `gen_condition` uses `operator bool`/`operator int`.
  - Explicit casts use `gen_scalar_conversion`.
  - Copy init and scalar args use `gen_scalar_conversion`.
- `operator->` is supported through `find_member_operator(..., "operator->")`
  plus `gen_zero_argument_member_call`.
- `gen_zero_argument_member_call` is defined after some callers but inside the
  class; call order is fine.
- `same_type` still compares named types by raw name. Member/operator matching
  also falls back to `class_of` identity, which fixed namespace self-types.
- Parser disambiguation:
  - `looks_like_template_arguments` stops at unmatched `)`/operators.
  - `looks_like_type_inside_parentheses` only treats `ident <` as a template
    when a matching `>` appears before `)`.

## Current known gaps / good next targets

1. **More implicit conversion contexts**
   - arithmetic/operands involving class conversions
   - `0 == flag` and relational comparisons (`flag < 5`)
   - `return`/argument conversions to class constructors
   - conversion to `std::string`? (would need constructor/conversion interplay)

2. **C `_Generic`**
   - Currently unsupported; `_Generic(value, int: ..., default: ...)` parses
     `int` as an expression and errors.
   - Hard in single-pass compiler; would need branch parsing/rollback and
     compile-time type selection.

3. **More C features**
   - `#error` / `#warning`
   - `__attribute__((...))` skipping and packed/aligned layout
   - `_Generic`
   - `#line`
   - designated initializer edge cases
   - `typeof`
   - `_Atomic` / `<stdatomic.h>`
   - richer initializer/declarator combinations

4. **C++ library surface**
   - `std::vector` / `std::algorithm` style helpers
   - more `std::string` (e.g. `find` with position, `replace` with string,
     `operator<<` for string streams)
   - full iostreams/locales/facets remain out of scope
   - standard `<utility>`/`<algorithm>` pieces commonly expected by guest code

5. **C++ language**
   - default arguments if not already complete
   - more template/overload resolution cases
   - `operator->*`
   - conversion operators returning references/pointers
   - `mutable`, `constexpr`, richer class semantics

6. **Known small quirks**
   - `__FILE__` uses the compiler’s provided source name, which may be a full
     path.
   - `__builtin_constant_p` currently always yields `0`.
   - `__builtin_unreachable()` is a no-op.
   - `FLT_MAX`/`DBL_MAX` presence in `<float.h>` should be rechecked before
     relying on them.
   - `std::string` is a subset, not full STL; do not assume `std::vector` exists.

## Build and test commands

Windows (from repo root):

```powershell
& 'C:\Program Files\Microsoft Visual Studio\18\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe' --build --preset windows-msvc-debug
& 'C:\Program Files\Microsoft Visual Studio\18\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\ctest.exe' --test-dir .\build\windows-msvc -C Debug --output-on-failure
.\build\windows-msvc\tests\Debug\kr32_tests.exe
```

Linux/WSL:

```powershell
wsl.exe -e bash -lc 'cd /mnt/c/Users/kevin/source/repos/KR32 && cmake --build build/linux-gcc -j2 && ctest --test-dir build/linux-gcc --output-on-failure'
```

Manual guest compile/run:

```powershell
.\build\windows-msvc\Debug\kr32.exe build .\build\scratch.c -o .\build\scratch.k32 --run
.\build\windows-msvc\Debug\kr32.exe build .\build\scratch.cpp -o .\build\scratch.k32 --run
```

Targeted doctest:

```powershell
.\build\windows-msvc\tests\Debug\kr32_tests.exe --test-case="C supports bit*"
.\build\windows-msvc\tests\Debug\kr32_tests.exe --test-case="conversion operators*"
```

Important:

- Build `kr32_tests` explicitly if you only built `kr32` and need the test exe.
- Do not kill a user-hosted `kr32.exe` GUI; a running exe can cause `LNK1168`.
- Linux builds are `-Werror`; unused variables and switch exhaustiveness matter.
- The thread test’s counter symbol lookup must stay exact/mangled; substring
  matching previously broke on libc’s `tmpfile_counter`.

## Resume checklist

1. `get_goal`; if active-but-disarmed, `update_goal` action `resume`.
2. `git status` and `git log --oneline -5`; confirm HEAD is `136c246` or later.
3. Rebuild `kr32_tests` and run the unit suite, then Windows/Linux CTest if the
   tree changed.
4. Pick the next gap above, write a focused failing test first, implement,
   then run targeted tests, full unit suite, and both CTests.
5. Commit with a narrow message and update `implementation.md`’s timestamp and
   log entry.

## Files changed most often

- `src/cc/cc.cpp` — C compiler, synthetic headers, parser/codegen
- `src/cc/libc.c` — guest libc implementation
- `src/cxx/parser.cpp` — C++ parsing/disambiguation
- `src/cxx/codegen.cpp` — C++ codegen, classes, overloads, conversions
- `src/cxx/preprocessor.cpp` — synthetic C++ headers, `<string>`, streams
- `tests/unit/test_cc.cpp` — C compiler/runtime tests
- `tests/cxx/test_cxx_codegen.cpp` — C++ compiler/runtime tests
- `docs/libc.md` — supported surface documentation
- `implementation.md` — chronological implementation log
