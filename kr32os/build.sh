#!/usr/bin/env bash
set -euo pipefail

here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
root="$(dirname "$here")"
build="$here/build"

if [[ -z "${KR32:-}" ]]; then
    for candidate in \
        "$root/build/linux-gcc/kr32" \
        "$root/build/linux-clang/kr32" \
        "$here/build/kr32"; do
        if [[ -x "$candidate" ]]; then
            KR32="$candidate"
            break
        fi
    done
fi
if [[ -z "${KR32:-}" ]] && command -v kr32 >/dev/null 2>&1; then
    KR32="$(command -v kr32)"
fi
if [[ -z "${KR32:-}" || ! -x "$KR32" ]]; then
    echo "kr32 was not found; set KR32 or pass a host build path" >&2
    exit 2
fi

mkdir -p "$build"
"$KR32" as "$here/bios/entry.S" --base 0xFFFF0000 -o "$build/bios.bin"
"$KR32" cc "$here/kernel/main.c" -S -o "$build/kernel.s"
"$KR32" as "$build/kernel.s" --object -o "$build/kernel.o"
"$KR32" as "$here/boot/entry.S" --object -o "$build/entry.o"
"$KR32" ld "$build/entry.o" "$build/kernel.o" --flat --base 0x00020000 -o "$build/kernel.bin"

echo "KR32OS M0 built:"
echo "  $build/bios.bin"
echo "  $build/kernel.bin"
