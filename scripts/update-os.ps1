#Requires -Version 5.1
<#
Rebuild KR32OS with the current kr32 executable, copy the .k32 images into the
host resources, and relink kr32 with the new embedded images.

This is the explicit "update the BIOS/kernel" step. Normal kr32 builds only
consume the checked-in .k32 files under src/resources/os.
#>
param(
    [string]$Preset = "windows-msvc-debug",
    [string]$Kr32 = "",
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

if (-not $Kr32) {
    $Kr32 = $env:KR32
}
if (-not $Kr32) {
    $candidates = @(
        (Join-Path $root 'build\windows-msvc\Debug\kr32.exe'),
        (Join-Path $root 'build\windows-msvc\Release\kr32.exe'),
        (Join-Path $root 'build\windows-ninja\kr32.exe')
    )
    foreach ($candidate in $candidates) {
        if (Test-Path -LiteralPath $candidate) {
            $Kr32 = $candidate
            break
        }
    }
}
if (-not $Kr32 -or -not (Test-Path -LiteralPath $Kr32)) {
    throw 'kr32 was not found; pass -Kr32 <path> or set KR32.'
}
$Kr32 = (Resolve-Path -LiteralPath $Kr32).Path

& (Join-Path $root 'kr32os\build.ps1') -Kr32 $Kr32
if ($LASTEXITCODE -ne 0) {
    throw "kr32os/build.ps1 failed with exit code $LASTEXITCODE"
}

$source = Join-Path $root 'kr32os\build\windows'
$destination = Join-Path $root 'src\resources\os\windows'
New-Item -ItemType Directory -Force -Path $destination | Out-Null
Copy-Item (Join-Path $source 'bios.k32') (Join-Path $destination 'bios.k32') -Force
Copy-Item (Join-Path $source 'kernel.k32') (Join-Path $destination 'kernel.k32') -Force

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
