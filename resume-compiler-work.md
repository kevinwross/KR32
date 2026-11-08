# Resume notes: C/C++ compiler work

**Updated:** 2026-11-08 07:14 America/Los_Angeles
**Repository:** `C:\Users\kevin\source\repos\KR32`
**Last feature commit:** `449a814` (`cxx: add __func__`).
**Goal:** “Continue to improve the C and C++ compilers to support the system”
**Goal status:** active. Rounds 69–125 continued the goal; latest verification is
below.

## Last verified state

- Windows Debug CTest: **43/43 passed**
- Linux GCC CTest (WSL): **43/43 passed**
- Unit suite: **445 test cases, 7,256 assertions**, all passing
- Working tree clean except pre-existing untracked `node_modules/`,
  `package.json`, and `package-lock.json` (not part of compiler work; leave
  untracked)

## Recent feature areas (rounds 69–125)

- Conversion operators now apply to scalar returns, equality/relational
  comparisons, and integer arithmetic, including reversed operands.
- Converting constructors work for copy initialization, scalar and class
  arguments (by value as well as by reference), returns, assignments, and
  C-style class-to-class casts.
- Default arguments cover functions, members, and constructors, including
  global and copy-initialized objects.
- C: `#error`/`#warning`, attribute skipping, `__attribute__((packed/aligned))`,
  `_Alignas`, `_Generic`, `typeof`/`__typeof__`, and `_Static_assert`/
  `static_assert` checks.
- C++ classes: `explicit`, `alignas`/`alignof`, C-style class temporaries,
  range-for over `begin`/`end` ranges and reference loop variables.
- `std::string`: many search/compare/assign/insert/erase/resize overloads,
  `npos`, `data`, `begin`/`end`, deep copy assignment, and repeated-character
  operations.
- C++ ABI: fixed integer arguments beyond `R3` are stack-passed, enabling
  four- and five-argument member calls such as `std::string::replace/compare`.
- Predefined macros in both front ends include `__COUNTER__`; C++ also supports
  `__DATE__`, `__TIME__`, `__FILE__`, `__LINE__`, and `__func__`.
- C alignment/static assertion support covers both `_Alignas` positions and
  message-less `_Static_assert`.

## Earlier conversion-operator foundation (rounds 67–68)

```cpp
class Flag {
public:
    int value;
    Flag(int input) : value(input) {}
    operator bool() const { return value != 0; }
};

bool truth(Flag flag) { return flag; }
Flag zero(0);
if (zero == 0) { ... }
```

- `conversion_cost` accepts class → scalar via a conversion operator
- `gen_scalar_conversion` invokes the matching conversion operator, with an
  `operator bool` fallback for integer targets
- Conditions, explicit casts, copy initialization, scalar/FP/long-long
  arguments, returns, comparisons, and integer arithmetic use it

## Recent rounds already committed

Approximate order, newest first:

- `449a814` — cxx: add __func__
- `a63db6c` — cxx: add date and time macros
- `9e650cb` — cxx: add __COUNTER__
- `b1e8837` — cc: add __COUNTER__
- `d237c94` — cc: allow message-less static assertions
- `b24139f` — cc: accept _Alignas after the type
- `0212d66` — cxx: add string capacity and reserve
- `1a30922` — cxx: convert class assignments through constructors
- `d2ce9ad` — cxx: convert class returns through constructors
- `4179cd3` — cxx: construct strings from prefixes
- `bbeb0da` — cxx: pass by-value class constructor arguments
- `e61cb17` — cxx: convert class arguments through constructors
- `6a77f7f` — cxx: copy-initialize classes through constructors
- `07fc0c6` — cxx: cast between class types
- `c9f3ea7` — docs: refresh compiler resume checkpoint
- `ff90f10` — cxx: add not-of search positions
- `c2b9842` — cxx: compare string subranges
- `e934ecb` — cxx: deep-copy string assignment
- `ee9186b` — cxx: compare string prefixes by count
- `37c43b9` — cxx: pass fixed arguments on the stack
- `0b242e2` — cxx: append string prefixes by count
- `6689d18` — cxx: insert string prefixes by count
- `c83be11` — cxx: append string prefixes by count
- `f0a0a91` — cxx: resize strings with fill characters
- `e9fb414` — cc: support _Alignas declarations
- `e609737` — cxx: iterate strings with begin and end
- `5e85996` — cc: honor aligned struct attributes
- `d478717` — cc: apply trailing packed attributes
- `dd96da5` — cc: honor packed struct attributes
- `ceb23d5` — cxx: add string data accessors
- `6054cea` — cxx: support alignas and alignof
- `53ad2f9` — cxx: add string assign overloads
- `ba4e244` — cxx: support default arguments
- `78719b4` — cxx: construct class temporaries from scalar arguments
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

1. **Remaining conversion contexts**
   - conversion operators returning classes used implicitly
   - conversion operators returning references/pointers
   - further constructor-overload ranking cases

2. **C features**
   - `_Atomic` / `<stdatomic.h>` semantics beyond plain qualifiers
   - `#line` file-name tracking in diagnostics
   - designated initializer edge cases
   - `_Alignas` on parameters (if needed)
   - richer initializer/declarator combinations

3. **C++ library surface**
   - `std::vector` / `std::algorithm` style helpers
   - `std::string` still lacks some niche overloads and stream insertion
   - full iostreams/locales/facets remain out of scope
   - standard `<utility>`/`<algorithm>` pieces commonly expected by guest code

4. **C++ language**
   - range-for with custom iterator classes (pointer iterators work)
   - more template/overload resolution cases
   - `operator->*`
   - richer `constexpr`/`mutable`/class semantics

5. **C++ ABI**
   - FP stack arguments beyond `F7`
   - `long long` and two-register classes beyond the integer register bank
   - stack-argument coverage for aggregates/lambdas

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
