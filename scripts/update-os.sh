#!/usr/bin/env bash
# Rebuild KR32OS with the current kr32 executable, copy the .k32 images into the
# host resources, and relink kr32 with the new embedded images.
#
# This is the explicit "update the BIOS/kernel" step. Normal kr32 builds only
# consume the checked-in .k32 files under src/resources/os.
set -euo pipefail

here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
root="$(cd "$here/.." && pwd)"
preset="${PRESET:-linux-gcc}"
kr32="${KR32:-$root/build/linux-gcc/kr32}"
cmake_bin="${CMAKE:-cmake}"

skip_build=0
skip_relink=0
for argument in "$@"; do
    case "$argument" in
        --skip-build) skip_build=1 ;;
        --skip-relink) skip_relink=1 ;;
        *) echo "update-os.sh: unknown argument $argument" >&2; exit 2 ;;
    esac
done

if [ "$skip_build" -eq 0 ]; then
    "$cmake_bin" --build "$root/build/$preset" --target kr32
fi

if [ ! -x "$kr32" ]; then
    echo "update-os.sh: kr32 not found at $kr32 (set KR32 or build first)" >&2
    exit 1
fi

KR32="$kr32" bash "$root/kr32os/build.sh"

mkdir -p "$root/src/resources/os/linux"
cp "$root/kr32os/build/linux/bios.k32" "$root/src/resources/os/linux/bios.k32"
cp "$root/kr32os/build/linux/kernel.k32" "$root/src/resources/os/linux/kernel.k32"

if [ "$skip_relink" -eq 0 ]; then
    "$cmake_bin" --build "$root/build/$preset" --target kr32
fi

echo "updated $root/src/resources/os/linux from $root/kr32os/build/linux"
