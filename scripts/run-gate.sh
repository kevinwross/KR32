#!/usr/bin/env bash
# Runs the build and test gate with a job count sized for the machine.
#
#   bash scripts/run-gate.sh [preset]
#
# Same rule as scripts/run-gate.ps1: an Arm64 machine runs four jobs, an x64
# machine 80% of its logical processors.
set -e
preset="${1:-linux-gcc}"
case "$(uname -m)" in
    aarch64|arm64) jobs=4 ;;
    *) jobs=$(( $(nproc) * 8 / 10 )); [ "$jobs" -lt 1 ] && jobs=1 ;;
esac
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
start=$(date +%s)
cmake --build --preset "${preset}" -j "${jobs}"
echo "gate: build $(( $(date +%s) - start ))s"
start=$(date +%s)
ctest --preset "${preset}" -j "${jobs}"
echo "gate: tests $(( $(date +%s) - start ))s"