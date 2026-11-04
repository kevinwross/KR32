#Requires -Version 5.1
<#
Build KR32OS. The default tool path is the debug host build; pass -Kr32 to
select another kr32 executable.
#>
param(
    [string]$Kr32 = ""
)

$ErrorActionPreference = 'Stop'
$here = Split-Path -Parent $MyInvocation.MyCommand.Path
$root = Split-Path -Parent $here
$build = Join-Path $here 'build'

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
if (-not $Kr32) {
    $command = Get-Command kr32 -ErrorAction SilentlyContinue
    if ($command -ne $null) {
        $Kr32 = $command.Source
    }
}
if (-not $Kr32 -or -not (Test-Path -LiteralPath $Kr32)) {
    throw 'kr32 was not found; pass -Kr32 <path> or set KR32.'
}
$Kr32 = (Resolve-Path -LiteralPath $Kr32).Path
$build = Join-Path $here 'build'
New-Item -ItemType Directory -Force -Path $build | Out-Null

function Invoke-Kr32 {
    param([string[]]$Arguments)
    & $Kr32 @Arguments
    if ($LASTEXITCODE -ne 0) {
        throw "kr32 $($Arguments -join ' ') failed with exit code $LASTEXITCODE"
    }
}

$bios = Join-Path $build 'bios.bin'
$kernelAssembly = Join-Path $build 'kernel.s'
$kernelObject = Join-Path $build 'kernel.o'
$consoleAssembly = Join-Path $build 'console.s'
$consoleObject = Join-Path $build 'console.o'
$shellAssembly = Join-Path $build 'shell.s'
$shellObject = Join-Path $build 'shell.o'
$timerAssembly = Join-Path $build 'timer.s'
$timerObject = Join-Path $build 'timer.o'
$trapsAssembly = Join-Path $build 'traps.s'
$trapsObject = Join-Path $build 'traps.o'
$trapStubsObject = Join-Path $build 'trap_stubs.o'
$entryObject = Join-Path $build 'entry.o'
$kernel = Join-Path $build 'kernel.bin'

Invoke-Kr32 -Arguments @('as', (Join-Path $here 'bios\entry.S'), '--base', '0xFFFF0000', '-o', $bios)
Invoke-Kr32 -Arguments @('cc', (Join-Path $here 'kernel\main.c'), '-S', '-o', $kernelAssembly)
Invoke-Kr32 -Arguments @('as', $kernelAssembly, '--object', '-o', $kernelObject)
Invoke-Kr32 -Arguments @('cc', (Join-Path $here 'kernel\console.c'), '-S', '-o', $consoleAssembly)
Invoke-Kr32 -Arguments @('as', $consoleAssembly, '--object', '-o', $consoleObject)
Invoke-Kr32 -Arguments @('cc', (Join-Path $here 'kernel\shell.c'), '-S', '-o', $shellAssembly)
Invoke-Kr32 -Arguments @('as', $shellAssembly, '--object', '-o', $shellObject)
Invoke-Kr32 -Arguments @('cc', (Join-Path $here 'kernel\timer.c'), '-S', '-o', $timerAssembly)
Invoke-Kr32 -Arguments @('as', $timerAssembly, '--object', '-o', $timerObject)
Invoke-Kr32 -Arguments @('cc', (Join-Path $here 'kernel\traps.c'), '-S', '-o', $trapsAssembly)
Invoke-Kr32 -Arguments @('as', $trapsAssembly, '--object', '-o', $trapsObject)
Invoke-Kr32 -Arguments @('as', (Join-Path $here 'kernel\traps.S'), '--object', '-o', $trapStubsObject)
Invoke-Kr32 -Arguments @('as', (Join-Path $here 'boot\entry.S'), '--object', '-o', $entryObject)
Invoke-Kr32 -Arguments @('ld', $entryObject, $kernelObject, $consoleObject, $shellObject,
                        $timerObject, $trapsObject, $trapStubsObject,
                        '--flat', '--base', '0x00020000', '-o', $kernel)

Write-Host "KR32OS M2 built:"
Write-Host "  $bios"
Write-Host "  $kernel"
