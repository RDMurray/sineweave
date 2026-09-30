param([string]$BuildDirectory = (Join-Path $PSScriptRoot '../build'))
$ErrorActionPreference = 'Stop'
$projectRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$buildRoot = [IO.Path]::GetFullPath($BuildDirectory)
$bundle = Join-Path $buildRoot 'Sineweave_artefacts/Release/VST3/Sineweave.vst3'
if (!(Test-Path -LiteralPath $bundle)) { throw 'Build the Release VST3 first.' }
$releaseRoot = Join-Path $buildRoot 'releases'
New-Item -ItemType Directory -Path $releaseRoot -Force | Out-Null
$stage = Join-Path $releaseRoot ('staging-' + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $stage | Out-Null
Copy-Item -LiteralPath $bundle -Destination $stage -Recurse
foreach ($entry in @('LICENSE', 'README.md', 'THIRD_PARTY_NOTICES.md', 'licenses', 'docs')) {
    Copy-Item -LiteralPath (Join-Path $projectRoot $entry) -Destination $stage -Recurse
}
# Include corresponding project and dependency source with the binary candidate.
$source = Join-Path $stage 'Source'
New-Item -ItemType Directory -Path $source | Out-Null
foreach ($entry in @('CMakeLists.txt', 'CMakePresets.json', '.clang-format', '.gitignore', '.github',
    'src', 'tests', 'cmake', 'scripts', 'docs', 'LICENSE', 'README.md', 'THIRD_PARTY_NOTICES.md', 'licenses')) {
    Copy-Item -LiteralPath (Join-Path $projectRoot $entry) -Destination $source -Recurse
}
$cache = Get-Content -LiteralPath (Join-Path $buildRoot 'CMakeCache.txt')
foreach ($name in @('JUCE', 'LORIS')) {
    $local = $cache | Where-Object { $_ -match "^SINEWEAVE_${name}_PATH:PATH=(.+)$" }
    if ($local) { $dependency = ($local -split '=', 2)[1] }
    else { $dependency = Join-Path $buildRoot ('_deps/' + $name.ToLower() + '-src') }
    if (!(Test-Path -LiteralPath $dependency)) { throw "Missing $name source for packaging." }
    & robocopy $dependency (Join-Path $source "third_party/$name") /E /XD .git /NFL /NDL /NJH /NJS /NP | Out-Null
    if ($LASTEXITCODE -gt 7) { throw "Failed to copy $name source." }
}
$archive = Join-Path $releaseRoot 'Sineweave-Windows-x64-VST3.zip'
Compress-Archive -Path (Join-Path $stage '*') -DestinationPath $archive -Force
Write-Output $archive
$global:LASTEXITCODE = 0
