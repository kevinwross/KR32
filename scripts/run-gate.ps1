# Runs the build and test gate with a job count sized for the machine.
#
#   pwsh -File scripts/run-gate.ps1 [-Preset windows-msvc-debug] [-NoTests]
#
# CMake test presets cannot compute a processor count, so the number lives here:
# an Arm64 machine runs four jobs, and an x64 machine runs 80% of its logical
# processors. The same rule is in scripts/run-gate.sh. A preset's own
# execution.jobs is only a fallback for a bare `ctest --preset`.
param(
    [string]$Preset = "windows-msvc-debug",
    [switch]$NoTests
)
# J5: ask the doctor first, so a missing tool fails with a sentence naming the
# tool and the repository instead of a build error. The doctor lives in the
# compiler tree, the same place the shell one does.
$doctor = Join-Path (Split-Path -Parent $PSScriptRoot) '../krcompiler/scripts/doctor.ps1'
if (Test-Path $doctor) {
    & pwsh -NoProfile -File $doctor -Repo kr32
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
} else {
    Write-Warning "gate: no doctor at '$doctor'; continuing without one"
}

# NUMBER_OF_PROCESSORS is set by Windows and by CI runners, so read it rather
# than a WMI query; the runtime count is the fallback when it is absent.
$architecture = $env:PROCESSOR_ARCHITECTURE
$cores = 0
if ($env:NUMBER_OF_PROCESSORS) {
    $cores = [int]$env:NUMBER_OF_PROCESSORS
}
if ($cores -lt 1) {
    $cores = [Environment]::ProcessorCount
}
if ($architecture -eq "ARM64") {
    $jobs = 4
} else {
    $jobs = [Math]::Max(1, [Math]::Floor($cores * 0.8))
}
# The sanitizer gate is CPU-bound and has a 600 s per-test watchdog: eight jobs
# oversubscribe a small machine and its codegen buckets cross it. Cap that preset
# regardless of the core count.
if ($Preset -match "asan" -and $jobs -gt 4) {
    Write-Host ("gate: {0} is the sanitizer gate, capping {1} -> 4 jobs" -f $Preset, $jobs)
    $jobs = 4
}
Write-Host ("gate: {0} on {1}, {2} logical processors -> {3} jobs" -f $Preset, $architecture, $cores, $jobs)

$cmake = "C:\Program Files\Microsoft Visual Studio\18\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
$ctest = "C:\Program Files\Microsoft Visual Studio\18\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\ctest.exe"
if (-not (Test-Path $cmake)) {
    $cmake = "cmake"
    $ctest = "ctest"
}

$clock = [System.Diagnostics.Stopwatch]::StartNew()
& $cmake --build --preset $Preset -j $jobs
if ($LASTEXITCODE -ne 0) {
    Write-Host "gate: build failed"
    exit $LASTEXITCODE
}
Write-Host ("gate: build {0:0.0}s" -f $clock.Elapsed.TotalSeconds)
if ($NoTests) { exit 0 }

$clock.Restart()
& $ctest --preset $Preset -j $jobs
$code = $LASTEXITCODE
Write-Host ("gate: tests {0:0.0}s" -f $clock.Elapsed.TotalSeconds)
exit $code