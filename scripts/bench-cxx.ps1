# Measures optimization with the machine's own tick counter rather than with an
# instruction count: a fold that removes instructions can cost ticks, and the
# reverse, so the ratio the compiler is judged on has to come from a run.
#
#   pwsh -File scripts/bench-cxx.ps1 [-Kr32 <path>]
param(
    [string]$Kr32 = ""
)

$root = Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)
if (-not $Kr32) {
    $release = Join-Path $root "build/windows-msvc/Release/kr32.exe"
    $debug = Join-Path $root "build/windows-msvc/Debug/kr32.exe"
    $Kr32 = if (Test-Path $release) { $release } else { $debug }
}
if (-not (Test-Path $Kr32)) { throw "kr32 not found: $Kr32" }

$sources = @(
    "examples/balanced-tree.cpp",
    "examples/matrix.cpp",
    "examples/hello.cpp",
    "examples/binarytree.c"

)

$rows = @()
foreach ($relative in $sources) {
    $source = Join-Path $root $relative
    if (-not (Test-Path $source)) { Write-Warning "missing $relative"; continue }
    $name = [System.IO.Path]::GetFileNameWithoutExtension($relative)
    $ticks = @{}
    foreach ($level in @("-O0", "-O1")) {
        $scratch = Join-Path $root "build/bench"
        New-Item -ItemType Directory -Force -Path $scratch | Out-Null
        $image = Join-Path $scratch ($name + $level + ".k32")
        $output = (& $Kr32 build $source $level -o $image --run 2>&1) -join "`n"
        $match = [regex]::Match($output, "ticks=(\d+)")
        if ($match.Success) {
            $ticks[$level] = [int64]$match.Groups[1].Value
        } else {
            Write-Warning ("no ticks for {0}{1}: {2}" -f $name, $level, $output)
        }
        Remove-Item $image -ErrorAction SilentlyContinue
    }
    if ($ticks.ContainsKey("-O0") -and $ticks.ContainsKey("-O1") -and $ticks["-O0"] -gt 0) {
        $rows += [pscustomobject]@{
            Source  = $name
            TicksO0 = $ticks["-O0"]
            TicksO1 = $ticks["-O1"]
            Speedup = [math]::Round($ticks["-O0"] / [double]$ticks["-O1"], 2)
        }
    }
}
$rows | Format-Table -AutoSize