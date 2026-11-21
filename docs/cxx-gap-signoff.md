# C++ corpus sign-off — differences accepted as permanent

**Status:** **accepted** — signed by kevinwross on
2026-11-20T22:18:58-08:00 (plan decision 10). This is the written sign-off that
stage B's decision 1 requires; it does not itself authorise the deletion.
**Toolchain:** KRCompiler `47fa4e5` (4.12), `krc::toolchain` on KR32.
**List:** every entry below is accepted as permanent; none is pending.
**Corpus:** KR32's six guest C++ examples. All six compile and link with krc;
five run to completion, and `FileOpen.cpp` is interactive (it opens a host file
dialog). No example is rejected, so the gaps below are outside KR32's current
corpus but are visible to external users.

Each entry gives the construct, the real diagnostic, a control that compiles,
and the consequence of accepting it permanently.

## 1. `throw <scalar>` — deliberate

```cpp
try { throw 1; } catch (...) { return 1; }   // rejected
```

```text
gap2.cpp:1:20: error: throw requires a class type
```

**Control:** throwing and catching a class object compiles (`struct E { int
code; }; throw e;`).

**Why permanent:** the `.cpp_except`/unwinder design carries a class object with
its typeinfo; a scalar has neither. This is a design decision, not a defect
(ADR 0026 in KRCompiler). Accepting it means scalar exceptions are not part of
the krc C++ dialect.

## 2. `new T[n]` with a runtime bound

```cpp
int* p = new int[n];   // rejected when n is not a constant
```

```text
gap1.cpp:1:42: error: variable length arrays are not supported
```

**Control:** `new int[4]` compiles, and so do constant-bound local arrays.

**Why permanent:** the bound is a runtime value, so this is a codegen gap as
well as a front-end one. Accepting it means variable-length `new[]` is not
available; use a fixed bound or the C allocation APIs.

## 3. Zero-argument functional cast `T()`

```cpp
int total = int();   // rejected
```

```text
gap3.cpp:1:26: error: expected expression
gap3.cpp:1:29: error: expected ';' after declaration
```

The same gap is reported for dependent `T()` in a template.

**Control:** a named value, or a constructor with an argument, compiles.

**Why permanent:** accepting it means `T()` value-initialisation is not part of
the dialect; write a named zero value or `T{}` where the type allows it.

## 4. `S s = S();` for a plain aggregate

```cpp
struct S { int v; };
int main() { S s = S(); return s.v; }   // rejected at 47fa4e5
```

```text
gap3-control.cpp:2:14: error: constructor-initialized class has no constructor
```

**Correction to the dependency's documentation:** KRCompiler's
`docs/cxx-front-end-gaps.md` records this shape as a compiling control for gap
3. At `47fa4e5` it is rejected. Treat it as part of gap 3 rather than a separate
construct, and accept that aggregate value-initialisation through the functional
cast is unavailable.

## 5. `std::thread` / `std::thread::id` in the guest thread header

`include/kr32/thread.hpp` compiles under the built-in front end but not under
krc:

```text
C:/Users/kevin/source/repos/KR32/include/kr32/thread.hpp:262:24:
error: expected ';' after declaration
    inline std::thread::id get_id() { return (std::thread::id)kr32::thread_self(); }
```

**Control:** `kr32::thread_create`, `thread_join`, `thread_yield`,
`thread_exit`, `thread_self` and `sleep_for` are plain functions and are
available.

**Why permanent (proposed):** the krc C++ front end does not support the
`std::thread` surface. Accepting it means guest C++ uses the `kr32::thread_*`
functions, not `std::thread`; `include/kr32/thread.hpp` should not be offered as
a krc header until this is implemented.

## 6. `<fstream>` is absent from the installed sysroot

```cpp
#include <fstream>   // header not found
```

```text
debug-test.cpp:1:2: error: header not found: 'fstream'
```

**Control:** `<stdio.h>` and the C `FILE`/libc stream functions are available;
the C++ `<iostream>`/`<fstream>` pair is not in the installed sysroot.

**Why permanent (proposed):** accepting it means file streams in guest C++ go
through the C stream API, not `<fstream>`. If the owner does not accept it, this
is a KRCompiler sysroot item rather than a KR32 change.

## Debug-information differences (not source-language gaps)

These were accepted in stage A and are recorded here so the sign-off is
complete:

- krc emits no `P` (promoted-local) records, so a krc image shows a frame slot
  where KR32's compiler shows `register: Rn`. The debugger handles both.
- Inherited members are displayed with their base prefix (`base.root_`) rather
  than as `root_`.
- A function's entry line maps to its first statement, not its declaration
  line.

These are debugger-display differences, not C++ acceptance differences.

## Outside this sign-off

The migration also exposed C/runtime differences in krc's libc: `argv` is not
populated by the krc runtime, float `printf` output differs, and several
`stdio.h` buffering macros (`_IOFBF`, `_IOLBF`, `_IONBF`) are absent. They are
not C++ front-end gaps and are not claimed as permanent here; they are recorded
so the owner can decide whether they are release blockers or a separate
runtime-compatibility item.

## Sign-off

By signing, the owner accepts the six C++ differences above (and the
debug-information display differences) as permanent for the krc toolchain, and
confirms stage B's decision 1 is satisfied.

- **Accepted by:** kevinwross
- **Signed:** 2026-11-20T22:18:58-08:00
- **Decision:** plan §8 decision 10 (satisfies decision 9's second condition)
- **List status:** accepted; the six gaps and the debug-information differences
  are permanent, not pending.
