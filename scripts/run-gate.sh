#!/usr/bin/env bash
# Runs the build and test gate with a job count sized for the machine.
#
#   bash scripts/run-gate.sh [preset]
#
# Same rule as scripts/run-gate.ps1: an Arm64 machine runs four jobs, an x64
# machine 80% of its logical processors.
set -e
preset="${1:-linux-gcc-debug}"

# J5: ask the doctor first, so a missing tool fails before the first compile with
# a sentence naming the tool and the repository.
doctor_tool="${KRC_DOCTOR_TOOL:-$(cd "$(dirname "$0")/.." && pwd)/../krcompiler/scripts/doctor.sh}"
if [ -f "$doctor_tool" ]; then
    sh "$doctor_tool" kr32 --need=krc,krld
else
    echo "gate: no doctor at '$doctor_tool' (set KRC_DOCTOR_TOOL); continuing without one" >&2
fi

# I2: build in the native mirror, never on the mount. A /mnt source carries mount
# paths in its debug data and its timestamps are too coarse for incremental
# builds. The re-executed gate runs on a native path, so it cannot loop.
in_place=0
for argument in "$@"; do
    if [ "$argument" = "--in-place" ]; then in_place=1; fi
done
root="$(cd "$(dirname "$0")/.." && pwd)"
case "$root" in
    /mnt/*)
        if [ "$in_place" = "1" ]; then
            echo "gate: building on $root (--in-place): debug paths will carry the mount" >&2
        else
            repo="$(basename "$root")"
            mirror_tool="${KRC_MIRROR_TOOL:-$root/../krcompiler/scripts/kr-mirror.sh}"
            if [ ! -f "$mirror_tool" ]; then
                echo "gate: $root is a /mnt path and no mirror tool was found at '$mirror_tool'; set KRC_MIRROR_TOOL or pass --in-place" >&2
                exit 2
            fi
            bash "$mirror_tool" "$repo" --wait 600 >&2
            mirror="$HOME/mirrors/$repo"
            echo "gate: source was $root; building from the mirror at $mirror" >&2
            exec bash "$mirror/scripts/run-gate.sh" "$@"
        fi
        ;;
esac
# NUMBER_OF_PROCESSORS is set by Windows and by CI runners; nproc is the fallback.
cores="${NUMBER_OF_PROCESSORS:-$(nproc)}"
# A share handed down by the family gate is used below, clamped by the x86_64 cap.
if [ -n "${FAMILY_JOBS:-}" ]; then
    echo "gate: ${FAMILY_JOBS} jobs from the family gate"
fi
# GCC 15 in the WSL image intermittently segfaults while garbage-collecting, so
# x86_64 stays at two jobs whatever the core count; an ARM64 host takes the count.
if [ "$(uname -m)" = "x86_64" ]; then
    jobs="${FAMILY_JOBS:-2}"
    [ "$jobs" -gt 2 ] && jobs=2
else
    jobs="${FAMILY_JOBS:-$cores}"
fi
# The sanitizer gate is CPU-bound with a 600 s per-test watchdog, so it keeps the
# four-job cap whatever the core count.
case "${preset}" in
    *asan*)
        if [ "${jobs}" -gt 4 ]; then
            echo "gate: ${preset} is the sanitizer gate, capping ${jobs} -> 4 jobs"
            jobs=4
        fi
        ;;
esac
echo "gate: ${preset} on $(uname -m), $(nproc) logical processors -> ${jobs} jobs"
# A fresh mirror has no build directory, and the gate used to fail with "not a
# directory" rather than configuring one. Configure only when it is missing, so an
# existing tree keeps its cache and an empty one is created.
# Configure when the tree holds no build.ninja, not when the directory is absent:
# a configure that failed halfway leaves the directory behind, and the next run
# then built a tree that was never generated.
if [ ! -f "build/${preset}/build.ninja" ]; then
    cmake --preset "${preset}"
fi
start=$(date +%s)
cmake --build --preset "${preset}" -j "${jobs}"
echo "gate: build $(( $(date +%s) - start ))s"
start=$(date +%s)
ctest --preset "${preset}" -j "${jobs}"
echo "gate: tests $(( $(date +%s) - start ))s"