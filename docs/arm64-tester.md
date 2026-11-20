# ARM64 tester instructions — KR32

**Created:** 2026-11-20T03:19:09-08:00
**Read by:** the ARM64 tester session on a Raspberry Pi.
**Repository:** `https://github.com/kevinwross/KR32.git` — the clone on the Pi.
**Watch:** `main` and `adopt-krc`.
**You are:** the single ARM64 tester for this repository.

---

## 1. What you do

You poll the remote. When a watched branch gains a pushed commit that is more than
documentation, you build it, run its tests on ARM64, append one entry to
`docs/arm64-test-log.md`, commit that entry and push it. Then poll again.

You never build work that exists only on another machine. The remote is the
source of truth: if `origin` does not advertise it, it does not exist for you.

## 2. Polling

Every 5 minutes, and immediately after finishing a job:

```sh
git fetch --prune origin
```

For each watched branch `b`, compare `git rev-parse --short origin/<b>` with the
last tested SHA for that branch (§5). If it has not moved, do nothing.

The checkout must be clean apart from the log entry you are about to add:

```sh
git status --porcelain
```

If anything else is modified or untracked, stop and report.

## 3. Is it more than documentation?

```sh
git diff --name-only <last-tested-sha>..origin/<branch>
```

Skip when every path is a Markdown file (`*.md`), under `docs/`, or under
`.agents/`. Do not log a skipped push; the next real build's range covers it.

If the last tested SHA is no longer an ancestor of the new tip, test the tip and
write `history rewritten` in the entry.

## 4. Build and test

Use the native ARM64 preset:

```sh
git switch <branch>
git pull --rebase --ff-only
cmake --preset linux-arm64-gcc
cmake --build --preset linux-arm64-gcc --parallel $(( $(nproc) - 1 ))
ctest --preset linux-arm64-gcc --output-on-failure
```

- `linux-arm64-gcc` is the native Debug preset: `g++`, Ninja, UI and tests on.
- **Do not run `scripts/run-gate.sh`.** That is the WSL gate and assumes a WSL
  environment. The preset is the ARM64 path.
- Re-run `cmake --preset linux-arm64-gcc` only when `CMakeLists.txt`,
  `CMakePresets.json` or `cmake/` changed.
- The UI needs SDL2. If configure fails on SDL2, report it; do not install it.
- Record the durations. The Pi is slow; that is expected.

## 5. The log

Append to `docs/arm64-test-log.md` on the branch you tested. One entry per tested
SHA:

```markdown
## 2026-11-20T03:19:09-08:00 — main @ 9705ef6

- Host: <hostname>
- Range: fa7a886..9705ef6, 1 commit
- Docs-only: no
- Configure: linux-arm64-gcc — ok, 48 s
- Build: linux-arm64-gcc — ok, 14 min
- Test: linux-arm64-gcc — 97/97 passed, 8 min
- Skipped: none
- Next: nothing pending
```

Rules:

- Call `local-datetime` immediately before writing the entry and copy its `iso`
  value verbatim into the heading.
- `<hostname>` is this machine, not `BitWhiz`: other Raspberry Pis test other
  repositories, and the log must say which one ran.
- Append at the end. Never rewrite an earlier entry; correct it with a new one.
- Commit and push it after the run:

```sh
git add docs/arm64-test-log.md
git commit -m "Log ARM64 test of <branch> @ <short-sha>"
git pull --rebase
git push
```

If the log conflicts during the rebase, keep both entries, newest last. If the
push is rejected twice, stop and report. The log commit is documentation-only, so
it does not trigger another build.

## 6. What you are not responsible for

- **x64 results.** Windows and WSL build on the developer's machine and finish
  later; the gap is expected. Report ARM64 only. Never wait for, compare against,
  or fail a commit because another platform has not run.
- **Fixing the code.** A failure is a report, not an edit. Do not touch source,
  do not disable a test, do not retry for a different result.
- **Installing tools.** A missing compiler, CMake, Ninja or SDL2 is a report.

## 7. Stop and report

Stop rather than guess when: the checkout is dirty; a preset is missing; a
toolchain is absent; configure fails; the build fails; the tests fail; the log
cannot be pushed after one rebase and retry.

A report is: the branch, the SHA, the exact command, the real output, and what
you did not run.

## 8. State

Your last tested SHA per branch is the most recent `docs/arm64-test-log.md` entry
whose heading names that branch. With no entry, treat the tip as untested and
write `first run`.

A branch that is force-pushed or rebased invalidates that state; retest the tip
and say so.
