# adopt-krc log — stage A of the krc toolchain adoption

Append-only. Every entry begins with the exact `iso` string from `local-datetime`,
called immediately before that entry was written. Entries are never rewritten; a
correction is a new entry. The plan is
`krdbg/docs/requests/kr32-adopt-krc-toolchain.md`; the brief is
`krdbg/docs/requests/brief-kr32.md`. The pinned dependency is KRCompiler
`f936155` on `adopt-krc`.

## 2026-11-20T09:35:32-08:00

**Changed:** nothing in the repository. Stage 3.1, the debug spike: a krc-built
`.k32` was opened in the current GUI and driven through a breakpoint, a local, a
call stack and line stepping. The image was built outside the tree with a
throwaway facade host in `build/spike/` (ignored), not by a KR32 code path.

**Pinned dependency:** KRCompiler `f936155b4423d86df21190024e1f0350a534a318`
on `adopt-krc`, built from a detached worktree at
`C:\Users\kevin\source\repos\.krc-pin-f936155` and installed to
`C:\Users\kevin\source\repos\.krc-install-f936155` with
`cmake --install ... --config Debug`. The facade host linked `krc::toolchain`
through `find_package(krc-toolchain CONFIG REQUIRED)`.

**Spike source (C, line 10 is the `printf`):**

```c
#include <stdio.h>

int add(int a, int b) {
    int sum = a + b;
    return sum;
}

int main(void) {
    int total = add(20, 22);
    printf("total=%d\n", total);
    return total;
}
```

**Commands and real results (Windows, Debug):**

- Facade host: `compile` (`debug=true`, `optimize=1`, `base=0x10000`,
  `entry=_start`) → 3301 bytes of assembly and 527 bytes of `.debug_info`;
  `assemble`; `link` with krc's staged KR32 runtime → `wrote 1279213 bytes,
  entry 0x2f564 base 0x10000`.
- `kr32 vm spike-krc.k32` → `total=42`, `HALTED exit=42`.
- `kr32 debug spike-krc.k32` → `source 0 build\spike\spike.c`, `function add
  0x10000-0x10060 frame=40`, `function main 0x1006c-0x100d0 frame=32`, followed
  by the libc functions. KR32's existing `debug::parse_info` reads the krc
  bridge unchanged.
- `kr32 gui spike-krc.k32 --headless --screenshot gui-krc.ppm` →
  `ui-render width=1440 height=900 hash=1DEAF1DFB37CBEA9`, exit 0. The rendered
  frame shows `SOURCE build\spike\spike.c` with lines 1–12, line 9 highlighted,
  `Locals (1) total = 0x00000000`, and `#0 main 00010078` under CALL STACK.
- `kr32 dap spike-krc.k32` with a scripted session
  (initialize, launch, `setBreakpoints` line 10, configurationDone, continue,
  stackTrace, scopes, variables, disconnect) →
  `"line":10,"verified":true`, stop `"reason":"breakpoint"`,
  `"stackFrames":[{"column":5,"instructionPointerReference":"65692","line":10,
  "name":"main","source":{"name":"source","path":"build\\spike\\spike.c"}}]`,
  `"name":"total","type":"i32","value":"0x0000002A"`.
- A driver on the real `DebuggerUI` (`load`, then `step_line_in`,
  `step_line_over`, `step_line_out`) printed:

```text
load   pc=00010078 line=9 function=main frames=1 total=0x00000000
in     pc=00010034 line=4 function=add frames=2
over   pc=00010058 line=5 function=add frames=2
out    pc=00010090 line=9 function=main frames=1 total=0x00000000
```

  A second run with two `step_line_over` calls moved 9 → 10 (total
  `0x0000002A`) → 11. Source lines, locals, call stack and line stepping all
  work in the current GUI. **3.1 is a GO.**

**Decision — the runtime linked by the krc path is krc's staged KR32 runtime, not
`cc::runtime_assembly()`.** The plan's risk table says to keep KR32's runtime and
link it with krc objects. That route does not exist:

1. krc's assembler rejects KR32's runtime assembly spelling: `unsupported
   directive .RODATA` (also `.bss`, `.space`, `.asciz`).
2. After those five spellings are rewritten to the krc set, it rejects an
   instruction: `unsupported instruction 'LL'` (KR32's libc atomics use load-
   linked), and more would follow.
3. KR32's object container and krc's KRO object are different formats; krc's
   linker rejects a KR32 object with `KRO magic mismatch`.
4. Compiling KR32's `src/cc/libc.c` with krc instead fails on
   `__atomic_compare_exchange_n`, `__atomic_store_n`, `__builtin_heap_start`,
   `__builtin_heap_end`, `__builtin_sqrt`, and a stricter `feof` redefinition.
5. Stage B deletes `src/asm` and `src/link`, so a final image assembled by KR32's
   assembler cannot be the adopted path.

The krc path therefore assembles and links the runtime the facade is documented
to need: `<sysroot>/kr32/lib/crt0.s` (staged copy), `<sysroot>/kr32/lib/libkrcrt.kro`,
and `fileio.s`/`eh.s` from `targets/kr32/runtime`, in that order after the user
object. Guest code built through krc uses krc's libc rather than KR32's
`cc::libc.c`; this is the deviation the plan's risk row did not anticipate, and
it is recorded here for the programme owner before stage B.

**Next step:** 3.2, the C++ corpus.

**Blocker:** none for 3.1. The runtime-engine choice above is a plan deviation
that needs a decision before stage B; it does not block stage A.

## 2026-11-20T09:35:50-08:00

**Changed:** nothing in the repository. Stage 3.2, the C++ corpus: KR32's six
guest C++ examples were compiled and linked with the pinned krc, then run under
`kr32 vm`, and the constructs in KRCompiler's `docs/cxx-front-end-gaps.md` were
reproduced separately to show the boundary of each open gap.

**Corpus and real results** (`krc --target=kr32 -c -O1 -I include --sysroot
<stage>`, then `krld <user.kro> libkrcrt.kro fileio.s eh.s crt0.s`):

| Example | Compile | Link | Run |
|---|---|---|---|
| `examples/balanced-tree.cpp` | ok | ok | sorted order (30) printed |
| `examples/cxx_libc.cpp` | ok | ok | `C++ via C libc: shared libc`, exit 0 |
| `examples/cxx_tls.cpp` | ok | ok | exit 42 |
| `examples/FileOpen.cpp` | ok | ok | not run: opens the host file dialog |
| `examples/hello.cpp` | ok | ok | exit 42 |
| `examples/matrix.cpp` | ok | ok | exit 42, so every tile-intrinsic check passed |

`matrix.cpp` is the interesting one: krc lowers the `kr32::matrix_mul`,
`transform`, `transpose` and tile calls in `include/kr32/matrix.hpp` the same way
KR32's C++ front end does, and the program's own value checks (translation,
scale-then-translate, aliasing, `new`/`delete`) all returned 42. No example was
rejected, so **no corpus failure maps to a documented gap**.

**Gap reproduction** (`build/spike/gaps/`, not committed):

| Construct | krc diagnostic |
|---|---|
| `new int[n]` with runtime `n` | `variable length arrays are not supported` (line 1, column 42) |
| `throw 1` | `throw requires a class type` (line 1, column 20) — deliberate |
| `int total = int();` | `expected expression` (line 1, column 26) plus `expected ';' after declaration` |
| control: `new int[4]` | compiles |
| control: throw/catch a one-field class | compiles |
| control: `S s = S();` for a plain struct | rejected at this pin: `constructor-initialized class has no constructor` |

The last row is a correction to `docs/cxx-front-end-gaps.md`, which records that
control as compiling; it is a documentation drift in the dependency, not a KR32
change, and it narrows gap 3 slightly rather than opening a new one.

**KR32 corpus gap not covered by KRCompiler's list:** KR32's guest headers
(`<kr32/fb.h>`, `<kr32/gfx.h>`, `<kr32/con.h>`, `<kr32/input.h>`, `<kr32/sys.h>`)
exist only as the injected string constants in
`include/kr32/cc/guest_headers.h`; they are not files, so krc's preprocessor
cannot find them. No `examples/*.cpp` uses them, so the stage-A corpus passes,
but a GUI-compiled program that includes one will fail under
`KR32_TOOLCHAIN=krc` until those headers have on-disk copies. Recorded as a named
stage-B gap.

**Next step:** 3.3–3.6, the CMake option and the two compile branches.

**Blocker:** none. 3.2 does not exercise any open krc gap; the documented subset
is not also KR32's subset, because KR32's examples are inside the supported set.
