#Requires -Version 5.1
<#
Installs the pinned SDL2 Visual C++ development package into third_party/SDL2.
The extracted SDK is gitignored; this script is the reproducible install step
for Windows machines. Linux uses the system libsdl2-dev package instead.
#>
[CmdletBinding()]
param(
    [string]$Version = '2.32.10',
    [string]$Destination = (Join-Path (Split-Path -Parent $PSScriptRoot) 'third_party\SDL2')
)

$ErrorActionPreference = 'Stop'

$header = Join-Path $Destination 'include\SDL.h'
if (Test-Path -LiteralPath $header) {
    Write-Host "SDL2 is already installed at $Destination"
    Write-Host "Set SDL2_DIR to: $(Join-Path $Destination 'cmake')"
    exit 0
}

$url = "https://github.com/libsdl-org/SDL/releases/download/release-$Version/SDL2-devel-$Version-VC.zip"
$zip = Join-Path $env:TEMP "SDL2-devel-$Version-VC.zip"

Write-Host "Downloading $url"
if (-not (Test-Path -LiteralPath $zip)) {
    & curl.exe -L --fail --retry 3 -o $zip $url
    if ($LASTEXITCODE -ne 0) {
        throw "Download failed with exit code $LASTEXITCODE"
    }
}

New-Item -ItemType Directory -Force -Path $Destination | Out-Null
Write-Host "Extracting to $Destination"
& tar.exe -xf $zip -C $Destination --strip-components=1
if ($LASTEXITCODE -ne 0) {
    throw "Extraction failed with exit code $LASTEXITCODE"
}

if (-not (Test-Path -LiteralPath $header)) {
    throw "SDL2 header not found after extraction: $header"
}

Write-Host "SDL2 $Version installed."
Write-Host "Set SDL2_DIR to: $(Join-Path $Destination 'cmake')"
