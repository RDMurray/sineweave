param([Parameter(Mandatory)][string]$AssetDirectory)
$ErrorActionPreference = 'Stop'
$repo = $env:GITHUB_REPOSITORY
$sha = $env:GITHUB_SHA
if (!$repo -or !$sha -or $env:GITHUB_REF -ne 'refs/heads/main') { throw 'Releases require a main push in GitHub Actions.' }
$tag = "build-$sha"
$assets = @('Sineweave-Windows-x64-Setup.exe', 'Sineweave-Windows-x64-VST3.zip', 'SHA256SUMS.txt') |
    ForEach-Object { Join-Path $AssetDirectory $_ }
foreach ($asset in $assets) {
    if (!(Test-Path -LiteralPath $asset)) { throw "Missing release asset: $asset" }
}
& gh release view $tag --repo $repo --json id 2>$null | Out-Null
if ($LASTEXITCODE -ne 0) {
    $notesPath = Join-Path $env:RUNNER_TEMP 'sineweave-release.md'
    @"
Windows x64 VST3 build from commit $sha.

Download **Sineweave-Windows-x64-Setup.exe** to install. The installer is unsigned and includes the Microsoft VC++ runtime.
The ZIP contains the portable plugin, documentation, licences, and corresponding project and dependency source.
SHA256SUMS.txt provides checksums for both downloads.
"@ | Set-Content -LiteralPath $notesPath -Encoding utf8
    & gh release create $tag --repo $repo --target $sha --title "Sineweave build $($sha.Substring(0, 7))" --notes-file $notesPath --latest=false --draft
    if ($LASTEXITCODE -ne 0) { throw 'Release creation failed.' }
    & gh release upload $tag @assets --repo $repo
    if ($LASTEXITCODE -ne 0) { throw 'Release asset upload failed.' }
} else {
    # Complete a failed draft run, but preserve assets of published releases.
    $existing = (& gh release view $tag --repo $repo --json isDraft | ConvertFrom-Json)
    if ($LASTEXITCODE -ne 0) { throw 'Release lookup failed.' }
    if ($existing.isDraft) {
        & gh release upload $tag @assets --repo $repo --clobber
        if ($LASTEXITCODE -ne 0) { throw 'Release asset upload failed.' }
    }
}
& gh release edit $tag --repo $repo --draft=false --latest=false
if ($LASTEXITCODE -ne 0) { throw 'Release publication failed.' }
# Serialized publication prevents older builds finishing later from taking latest.
$latestJson = & gh api "repos/$repo/releases/latest" 2>$null
if ($LASTEXITCODE -eq 0) {
    $latest = $latestJson | ConvertFrom-Json
    $comparison = (& gh api "repos/$repo/compare/$($latest.tag_name)...$sha" | ConvertFrom-Json)
    if ($LASTEXITCODE -ne 0) { throw 'Cannot compare latest release to this commit.' }
    $makeLatest = $comparison.status -in @('ahead', 'identical')
} else {
    $makeLatest = $true
}
if ($makeLatest) {
    & gh release edit $tag --repo $repo --latest
    if ($LASTEXITCODE -ne 0) { throw 'Latest release update failed.' }
}
