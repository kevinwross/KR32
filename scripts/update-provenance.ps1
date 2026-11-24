# Writes third_party/provenance.json: what each checked-in artefact is, where it
# came from, and the hash the repository expects.
#
# Run it after adding or replacing a file under third_party/, then commit the
# manifest with the file. The check row compares the tree against the manifest, so
# an artefact replaced without re-running this script fails the gate.
#
#   pwsh -File scripts/update-provenance.ps1
param(
    [string]$Root = (Split-Path -Parent $PSScriptRoot)
)

$ErrorActionPreference = 'Stop'
$directory = 'third_party'

# Where each file came from. A file with no entry here is recorded as unknown and
# the check still verifies its hash; the source line is what a reader needs most,
# so it is worth naming.
$sources = @{
    'third_party/doctest.h'                 = 'doctest v2.4.11, https://github.com/doctest/doctest (MIT)'
    'third_party/doctest.LICENSE.txt'       = 'doctest v2.4.11, LICENSE (MIT)'
    'third_party/stb/stb_truetype.h'        = 'stb_truetype v1.26, https://github.com/nothings/stb (public domain)'
    'third_party/fonts/OFL-Inter.txt'       = 'Inter, SIL Open Font License 1.1'
    'third_party/fonts/OFL-JetBrainsMono.txt' = 'JetBrains Mono, SIL Open Font License 1.1'
}

$entries = @()
Get-ChildItem (Join-Path $Root $directory) -Recurse -File |
    Sort-Object FullName |
    ForEach-Object {
        $relative = $_.FullName.Substring($Root.Length + 1).Replace('\', '/')
        $sha = (Get-FileHash $_.FullName -Algorithm SHA256).Hash.ToLowerInvariant()
        $source = $sources[$relative]
        if (-not $source) { $source = 'recorded in this repository; origin not written down' }
        $entries += [ordered]@{ path = $relative; sha256 = $sha; source = $source }
    }

$manifest = [ordered]@{
    directory   = $directory
    directories = @($directory)
    entries     = $entries
}
$json = $manifest | ConvertTo-Json -Depth 6
Set-Content -Path (Join-Path $Root "$directory/provenance.json") -Value $json -Encoding utf8
Write-Host "wrote $directory/provenance.json with $($entries.Count) artefacts"
