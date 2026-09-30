param(
    [string]$BuildDirectory = (Join-Path $PSScriptRoot '../build'),
    [string]$IsccPath = 'C:/Program Files (x86)/Inno Setup 6/ISCC.exe'
)
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
if (!(Test-Path -LiteralPath $IsccPath)) { throw 'Install Inno Setup 6 or supply -IsccPath.' }
$redist = Join-Path $buildRoot 'vc_redist.x64.exe'
Invoke-WebRequest 'https://aka.ms/vs/17/release/vc_redist.x64.exe' -OutFile $redist
$signature = Get-AuthenticodeSignature -LiteralPath $redist
if ($signature.Status -ne 'Valid' -or $signature.SignerCertificate.Subject -notmatch 'O=Microsoft Corporation,') {
    throw 'The VC++ redistributable does not have a valid Microsoft signature.'
}
$runtimeVersion = (Get-Item -LiteralPath $redist).VersionInfo
$versionLine = $cache | Where-Object { $_ -match '^CMAKE_PROJECT_VERSION:STATIC=' }
$version = ($versionLine -split '=', 2)[1]
$commitSha = (& git -C $projectRoot rev-parse HEAD).Trim()
if ($LASTEXITCODE -ne 0) { throw 'Cannot determine source commit.' }
& $IsccPath "/DAppVersion=$version" "/DCommitSha=$commitSha" "/DStageDir=$stage" "/DOutputDir=$releaseRoot" "/DRedistPath=$redist" `
    "/DRedistMajor=$($runtimeVersion.FileMajorPart)" "/DRedistMinor=$($runtimeVersion.FileMinorPart)" `
    "/DRedistBuild=$($runtimeVersion.FileBuildPart)" "/DRedistRevision=$($runtimeVersion.FilePrivatePart)" `
    (Join-Path $PSScriptRoot 'installer.iss')
if ($LASTEXITCODE -ne 0) { throw 'Installer compilation failed.' }
$installer = Join-Path $releaseRoot 'Sineweave-Windows-x64-Setup.exe'
$checksums = foreach ($asset in @($installer, $archive)) {
    $hash = Get-FileHash -LiteralPath $asset -Algorithm SHA256
    "$($hash.Hash.ToLowerInvariant())  $([IO.Path]::GetFileName($asset))"
}
$checksums | Set-Content -LiteralPath (Join-Path $releaseRoot 'SHA256SUMS.txt') -Encoding ascii
Write-Output $archive
Write-Output $installer
$global:LASTEXITCODE = 0
