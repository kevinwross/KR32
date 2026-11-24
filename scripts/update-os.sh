#!/usr/bin/env bash
# Rebuilds KR32OS with the KRCompiler toolchain, copies the two container images
# into the host resources, and relinks kr32 with them embedded.
#
# This is the explicit "update the BIOS/kernel" step. Normal kr32 builds only
# consume the checked-in .kr32 files under src/resources/os, and the ctest row
# kr32_os_images_match_source fails when they no longer match a rebuild.
#
#   KRC=/path/to/krc bash scripts/update-os.sh [preset]
#
# The old version of this script drove `kr32 cc`, `kr32 as` and `kr32 ld` through
# kr32os/build.sh; those verbs no longer exist, so the compiler is the input now.
set -euo pipefail

preset="${1:-linux-gcc}"
here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
root="$(dirname "$here")"

if [[ -z "${KRC:-}" ]]; then
    for candidate in \
        "$root/../krcompiler/build/linux-gcc-debug/host/bin/Debug/krc" \
        "$HOME/krc/bin/krc"; do
        if [[ -x "$candidate" ]]; then
            KRC="$candidate"
            break
        fi
    done
fi
if [[ -z "${KRC:-}" ]] && command -v krc >/dev/null 2>&1; then
    KRC="$(command -v krc)"
fi
if [[ -z "${KRC:-}" || ! -x "$KRC" ]]; then
    echo "krc was not found; set KRC" >&2
    exit 2
fi

cmake --build --preset "$preset" --target kr32 >/dev/null

KRC="$KRC" bash "$root/kr32os/build.sh"

source="$root/kr32os/build/linux"
destination="$root/src/resources/os/linux"
mkdir -p "$destination"
cp "$source/bios.kr32" "$destination/bios.kr32"
cp "$source/kernel.kr32" "$destination/kernel.kr32"

cmake --build --preset "$preset" --target kr32 >/dev/null

echo "updated $destination from $source"
