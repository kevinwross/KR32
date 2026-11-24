# Builds KR32OS with the KRCompiler toolchain: no kr32 executable is involved.
#
#   powershell -ExecutionPolicy Bypass -File kr32os/build.ps1 [-Krc <krc.exe>] [-Out <dir>] [-Opt -O1]
#
# The guest C is compiled by `krc -c`, every image is linked and flattened by
# `krc image`, and the guest assembly is handed to the same command, which
# assembles a non-object input. The old script drove `kr32 cc`, `kr32 as` and
# `kr32 ld`; those verbs no longer exist, which is why this file exists.
#
# Windows writes to kr32os/build/windows; scripts/update-os.ps1 copies the two
# container images into src/resources/os/windows and relinks kr32.
param(
    [string]$Krc = "",
    [string]$Out = "",
    [string]$Opt = ""
)

$ErrorActionPreference = 'Stop'
$here = Split-Path -Parent $MyInvocation.MyCommand.Path

if (-not $Out) { $Out = Join-Path $here 'build\windows' }
$build = $Out
New-Item -ItemType Directory -Force -Path $build | Out-Null

if (-not $Opt) { $Opt = $env:KR32OS_OPT }
if (-not $Opt) { $Opt = '-O1' }

if (-not $Krc) { $Krc = $env:KRC }
if (-not $Krc) {
    $candidates = @(
        (Join-Path $here '..\build\windows-msvc\Debug\krc.exe'),
        'C:\Users\kevin\krc-debug\bin\krc.exe',
        'C:\Users\kevin\krc\bin\krc.exe'
    )
    foreach ($candidate in $candidates) {
        if (Test-Path -LiteralPath $candidate) { $Krc = $candidate; break }
    }
}
if (-not $Krc) {
    $command = Get-Command krc -ErrorAction SilentlyContinue
    if ($command) { $Krc = $command.Source }
}
if (-not $Krc -or -not (Test-Path -LiteralPath $Krc)) {
    throw 'krc was not found; pass -Krc <path> or set KRC'
}
$Krc = (Resolve-Path -LiteralPath $Krc).Path

$biosBase = '0xFFFF0000'
$kernelBase = '0x00020000'
$userBase = '0x00400000'
$includes = @(
    # The guest headers live in the repository's include tree, which is where
    # <kr32/sys.h> and the rest of the OS ABI are declared.
    (Join-Path $here '..\include'),
    (Join-Path $here 'include'),
    (Join-Path $here 'kernel'),
    (Join-Path $here 'libc'),
    (Join-Path $here 'user')
)

function Invoke-Krc {
    param([string[]]$Arguments)
    & $Krc @Arguments
    if ($LASTEXITCODE -ne 0) {
        throw "krc $($Arguments -join ' ') failed with exit code $LASTEXITCODE"
    }
}

function Compile-Guest([string]$Source, [string]$Output) {
    $arguments = @('-c', '--target=kr32', $Opt)
    foreach ($include in $includes) { $arguments += @('-I', $include) }
    $arguments += @($Source, '-o', $Output)
    Invoke-Krc -Arguments $arguments
}

function New-Image([string]$Output, [string]$Base, [string[]]$Inputs, [switch]$Flat,
                   [string]$Map = '', [string]$Sym = '', [string]$Entry = '') {
    $arguments = @('image', '--target=kr32', "--base=$Base")
    if ($Entry) { $arguments += "--entry=$Entry" }
    if ($Flat) { $arguments += '--flat' }
    if ($Map) { $arguments += "--map=$Map" }
    if ($Sym) { $arguments += "--sym=$Sym" }
    $arguments += $Inputs
    $arguments += @('-o', $Output)
    Invoke-Krc -Arguments $arguments
}

# --- kernel C sources -------------------------------------------------------
$kernelNames = @('main', 'console', 'fb', 'shell', 'timer', 'traps', 'user', 'memory',
                 'process', 'syscall', 'gfx', 'input')
$kernelObjects = @()
foreach ($name in $kernelNames) {
    $object = Join-Path $build "kernel_$name.kro"
    Compile-Guest (Join-Path $here "kernel\$name.c") $object
    $kernelObjects += $object
}

# --- user programs ----------------------------------------------------------
$userCrt = Join-Path $here 'libc\crt0.S'
$userSyscall = Join-Path $here 'libc\syscall.S'
$userGfx = Join-Path $build 'user_gfx.kro'
Compile-Guest (Join-Path $here 'libc\gfx.c') $userGfx

$userPrograms = [ordered]@{
    'init'     = 'init'
    'fault'    = 'fault'
    'wx'       = 'wx'
    'gfxdemo'  = 'gfxdemo'
    'gfxcheck' = 'gfxcheck'
    'shutdown' = 'shutdown'
    'readline' = 'readline'
}
$userImages = [ordered]@{}
foreach ($entry in $userPrograms.GetEnumerator()) {
    $object = Join-Path $build "user_$($entry.Key).kro"
    Compile-Guest (Join-Path $here "user\$($entry.Value).c") $object
    $image = Join-Path $build "$($entry.Key).bin"
    New-Image -Output $image -Base $userBase -Flat -Inputs @($userCrt, $object, $userSyscall, $userGfx)
    $userImages[$entry.Key] = $image
}

# --- initrd -----------------------------------------------------------------
$initrd = Join-Path $build 'initrd.bin'
$initrdAssembly = Join-Path $build 'initrd.s'
$initrdMagic = [uint32]0x4932334B
$initrdVersion = [uint32]1
$initrdHeaderSize = 12
$initrdEntrySize = 44
$initrdNameSize = 32
$initrdEntryOffsetOffset = 32
$initrdEntryOffsetSize = 36
$initrdEntryOffsetEntry = 40
$userCodeBase = [uint32]$userBase
$initrdNames = @($userImages.Keys)
$initrdEntryCount = $initrdNames.Count

$payloads = @()
foreach ($name in $initrdNames) { $payloads += , [System.IO.File]::ReadAllBytes($userImages[$name]) }
$payloadOffset = $initrdHeaderSize + $initrdEntryCount * $initrdEntrySize
$total = $payloadOffset
foreach ($payload in $payloads) { $total += $payload.Length }
$initrdBytes = New-Object byte[] $total
[System.Array]::Copy([System.BitConverter]::GetBytes($initrdMagic), 0, $initrdBytes, 0, 4)
[System.Array]::Copy([System.BitConverter]::GetBytes($initrdVersion), 0, $initrdBytes, 4, 4)
[System.Array]::Copy([System.BitConverter]::GetBytes([uint32]$initrdEntryCount), 0, $initrdBytes, 8, 4)
$offset = $payloadOffset
for ($index = 0; $index -lt $initrdEntryCount; ++$index) {
    $entryOffset = $initrdHeaderSize + $index * $initrdEntrySize
    $nameBytes = [System.Text.Encoding]::ASCII.GetBytes($initrdNames[$index])
    [System.Array]::Copy($nameBytes, 0, $initrdBytes, $entryOffset, $nameBytes.Length)
    [System.Array]::Copy([System.BitConverter]::GetBytes([uint32]$offset), 0, $initrdBytes, $entryOffset + $initrdEntryOffsetOffset, 4)
    [System.Array]::Copy([System.BitConverter]::GetBytes([uint32]$payloads[$index].Length), 0, $initrdBytes, $entryOffset + $initrdEntryOffsetSize, 4)
    [System.Array]::Copy([System.BitConverter]::GetBytes($userCodeBase), 0, $initrdBytes, $entryOffset + $initrdEntryOffsetEntry, 4)
    [System.Array]::Copy($payloads[$index], 0, $initrdBytes, $offset, $payloads[$index].Length)
    $offset += $payloads[$index].Length
}
[System.IO.File]::WriteAllBytes($initrd, $initrdBytes)

$initrdText = New-Object System.Text.StringBuilder
[void]$initrdText.AppendLine('.section .rodata')
[void]$initrdText.AppendLine('.global __initrd_start')
[void]$initrdText.AppendLine('__initrd_start:')
for ($index = 0; $index -lt $initrdBytes.Length; $index += 16) {
    $count = [Math]::Min(16, $initrdBytes.Length - $index)
    $parts = @()
    for ($step = 0; $step -lt $count; ++$step) {
        $parts += ('0x{0:X2}' -f $initrdBytes[$index + $step])
    }
    [void]$initrdText.AppendLine('.byte ' + ($parts -join ', '))
}
[void]$initrdText.AppendLine('.global __initrd_end')
[void]$initrdText.AppendLine('__initrd_end:')
Set-Content -LiteralPath $initrdAssembly -Value $initrdText.ToString() -NoNewline -Encoding ascii

# --- bios -------------------------------------------------------------------
$biosEntry = Join-Path $here 'bios\entry.S'
$bios = Join-Path $build 'bios.bin'
$biosK32 = Join-Path $build 'bios.kr32'
$biosSym = Join-Path $build 'bios.sym'
New-Image -Output $bios -Base $biosBase -Flat -Inputs @($biosEntry) -Sym $biosSym -Entry bios_entry
New-Image -Output $biosK32 -Base $biosBase -Inputs @($biosEntry) -Entry bios_entry

# --- kernel -----------------------------------------------------------------
$kernelInputs = @(Join-Path $here 'boot\entry.S') + $kernelObjects + @(
    (Join-Path $here 'kernel\traps-asm.S'),
    (Join-Path $here 'kernel\fast.S'),
    (Join-Path $here 'kernel\user-asm.S'),
    $initrdAssembly
)
$kernel = Join-Path $build 'kernel.bin'
$kernelK32 = Join-Path $build 'kernel.kr32'
$kernelMap = Join-Path $build 'kernel.map'
$kernelSym = Join-Path $build 'kernel.sym'
New-Image -Output $kernel -Base $kernelBase -Flat -Inputs $kernelInputs -Map $kernelMap -Sym $kernelSym
New-Image -Output $kernelK32 -Base $kernelBase -Inputs $kernelInputs

# --- kernel.meta ------------------------------------------------------------
$kernelMeta = Join-Path $build 'kernel.meta'
$mapLines = Get-Content -LiteralPath $kernelMap
$startLine = $mapLines | Where-Object { $_ -match '^\s*__initrd_start\s+(0x[0-9A-Fa-f]+)' } | Select-Object -First 1
$endLine = $mapLines | Where-Object { $_ -match '^\s*__initrd_end\s+(0x[0-9A-Fa-f]+)' } | Select-Object -First 1
if ($startLine -and $endLine) {
    [void]($startLine -match '0x([0-9A-Fa-f]+)')
    $initrdStart = [Convert]::ToUInt32($Matches[1], 16)
    [void]($endLine -match '0x([0-9A-Fa-f]+)')
    $initrdEnd = [Convert]::ToUInt32($Matches[1], 16)
    $imageBytes = [System.IO.File]::ReadAllBytes($kernel)
    [uint32]$crc = [uint32]::MaxValue
    foreach ($byte in $imageBytes) {
        $crc = $crc -bxor [uint32]$byte
        for ($bit = 0; $bit -lt 8; ++$bit) {
            if (($crc -band 1) -ne 0) {
                $crc = [uint32](($crc -shr 1) -bxor [uint32]3988292384)
            } else {
                $crc = [uint32]($crc -shr 1)
            }
        }
    }
    $crc = [uint32](-bnot $crc)
    $metaBytes = New-Object byte[] 24
    $metaWords = @(
        [uint32]0x4D32334B,
        [uint32]1,
        [uint32]0x00020000,
        [uint32]($initrdStart - 0x00020000),
        [uint32]($initrdEnd - $initrdStart),
        [uint32]$crc
    )
    for ($index = 0; $index -lt 6; ++$index) {
        [System.Array]::Copy([System.BitConverter]::GetBytes($metaWords[$index]), 0,
                             $metaBytes, $index * 4, 4)
    }
    [System.IO.File]::WriteAllBytes($kernelMeta, $metaBytes)
} else {
    Write-Warning 'kernel.meta not written: the map does not name __initrd_start and __initrd_end'
}

Write-Host 'KR32OS M4 built with the KRCompiler toolchain:'
Write-Host "  $bios"
Write-Host "  $biosK32"
Write-Host "  $kernel"
Write-Host "  $kernelK32"
