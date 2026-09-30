param([string]$BuildDirectory = (Join-Path $PSScriptRoot '../build'))
$ErrorActionPreference = 'Stop'
# Run only on disposable CI machines: this installs in the standard VST3 directory.
if ($env:GITHUB_ACTIONS -ne 'true') { throw 'Installer smoke tests require a disposable GitHub Actions runner.' }
$installer = [IO.Path]::GetFullPath((Join-Path $BuildDirectory 'releases/Sineweave-Windows-x64-Setup.exe'))
$installedBundle = Join-Path $env:CommonProgramFiles 'VST3/Sineweave.vst3'
$appDirectory = Join-Path $env:ProgramFiles 'Sineweave'
$sourceBundle = [IO.Path]::GetFullPath((Join-Path $BuildDirectory 'Sineweave_artefacts/Release/VST3/Sineweave.vst3'))
$sentinel = Join-Path $env:CommonProgramFiles 'VST3/sineweave-ci-unrelated.txt'
Set-Content -LiteralPath $sentinel -Value 'Preserve unrelated plugins'
try {
    foreach ($attempt in 1..2) {
        $process = Start-Process -FilePath $installer -ArgumentList '/VERYSILENT', '/SUPPRESSMSGBOXES', '/NORESTART', '/SP-' -WindowStyle Hidden -Wait -PassThru
        if ($process.ExitCode -notin @(0, 3010)) { throw "Installation failed: $($process.ExitCode)" }
        foreach ($file in Get-ChildItem -LiteralPath $sourceBundle -Recurse -File) {
            $relative = $file.FullName.Substring($sourceBundle.Length).TrimStart('\', '/')
            $installed = Join-Path $installedBundle $relative
            if (!(Test-Path -LiteralPath $installed) -or (Get-FileHash -LiteralPath $installed).Hash -ne (Get-FileHash -LiteralPath $file.FullName).Hash) {
                throw "Missing or changed bundle file: $relative"
            }
        }
        foreach ($entry in @('LICENSE', 'README.md', 'THIRD_PARTY_NOTICES.md', 'licenses', 'docs')) {
            if (!(Test-Path -LiteralPath (Join-Path $appDirectory $entry))) { throw "Missing installed documentation: $entry" }
        }
    }
    $process = Start-Process -FilePath (Join-Path $appDirectory 'unins000.exe') -ArgumentList '/VERYSILENT', '/SUPPRESSMSGBOXES', '/NORESTART' -WindowStyle Hidden -Wait -PassThru
    if ($process.ExitCode -ne 0) { throw "Uninstallation failed: $($process.ExitCode)" }
    if (Test-Path -LiteralPath (Join-Path $installedBundle 'Contents/x86_64-win/Sineweave.vst3')) { throw 'Plugin remains installed.' }
    if (!(Test-Path -LiteralPath $sentinel)) { throw 'Uninstaller removed an unrelated file.' }
    if (!(Test-Path -LiteralPath (Join-Path $env:windir 'System32/vcruntime140.dll'))) { throw 'Shared VC++ runtime is missing.' }
} finally {
    Remove-Item -LiteralPath $sentinel -ErrorAction SilentlyContinue
}
