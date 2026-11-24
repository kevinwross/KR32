#Requires -Version 5.1
<#
Rebuilds KR32OS with the KRCompiler toolchain, copies the two container images
into the host resources, and relinks kr32 with them embedded.

This is the explicit "update the BIOS/kernel" step. Normal kr32 builds only
consume the checked-in .kr32 files under src/resources/os, and the ctest row
kr32_os_images_match_source fails when they no longer match a rebuild.

The old version of this script drove `kr32 cc`, `kr32 as` and `kr32 ld` through
kr32os/build.ps1; those verbs no longer exist, so the compiler is the input now.
#>
param(
    [string]$Preset = "windows-msvc-debug",
    [string]$Krc = "",
    [string]$CMake = "",
    [switch]$SkipBuild,
    [switch]$SkipRelink
)

$ErrorActionPreference = 'Stop'
$here = Split-Path -Parent $MyInvocation.MyCommand.Path
$root = Split-Path -Parent $here

if (-not $CMake) {
    $command = Get-Command cmake -ErrorAction SilentlyContinue
    if ($command -ne $null) {
        $CMake = $command.Source
    }
    if (-not $CMake) {
        $candidate = 'C:\Program Files\Microsoft Visual Studio\18\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe'
        if (Test-Path -LiteralPath $candidate) { $CMake = $candidate }
    }
}

if (-not $SkipBuild) {
    if (-not $CMake) {
        throw 'cmake was not found; pass -CMake <path> or use -SkipBuild.'
    }
    & $CMake --build --preset $Preset --target kr32
    if ($LASTEXITCODE -ne 0) {
        throw "cmake --build --target kr32 failed with exit code $LASTEXITCODE"
    }
}

if (-not $Krc) { $Krc = $env:KRC }
if (-not $Krc) {
    $candidates = @(
        (Join-Path $root '..\krcompiler\build\windows-msvc-debug\host\bin\Debug\krc.exe'),
        'C:\Users\kevin\krc-debug\bin\krc.exe',
        'C:\Users\kevin\krc\bin\krc.exe'
    )
    foreach ($candidate in $candidates) {
        if (Test-Path -LiteralPath $candidate) { $Krc = $candidate; break }
    }
}
if (-not $Krc -or -not (Test-Path -LiteralPath $Krc)) {
    throw 'krc was not found; pass -Krc <path> or set KRC.'
}
$Krc = (Resolve-Path -LiteralPath $Krc).Path

& (Join-Path $root 'kr32os\build.ps1') -Krc $Krc
if ($LASTEXITCODE -ne 0) {
    throw "kr32os/build.ps1 failed with exit code $LASTEXITCODE"
}

$source = Join-Path $root 'kr32os\build\windows'
$destination = Join-Path $root 'src\resources\os\windows'
New-Item -ItemType Directory -Force -Path $destination | Out-Null
Copy-Item (Join-Path $source 'bios.kr32') (Join-Path $destination 'bios.kr32') -Force
Copy-Item (Join-Path $source 'kernel.kr32') (Join-Path $destination 'kernel.kr32') -Force

if (-not $SkipRelink) {
    if (-not $CMake) {
        throw 'cmake was not found; pass -CMake <path> or use -SkipRelink.'
    }
    & $CMake --build --preset $Preset --target kr32
    if ($LASTEXITCODE -ne 0) {
        throw "cmake --build --target kr32 failed with exit code $LASTEXITCODE"
    }
}

Write-Host "updated $destination from $source"
