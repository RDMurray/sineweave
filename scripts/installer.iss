; Inputs are supplied by package.ps1. Keep AppId stable across all builds.
[Setup]
AppId={{41AC3B35-E5BB-45F5-9F5D-399ED6B16790}
AppName=Sineweave
AppVersion={#AppVersion}
AppVerName=Sineweave {#AppVersion} ({#CommitSha})
AppPublisher=RDMurray
AppPublisherURL=https://github.com/RDMurray/sineweave
DefaultDirName={autopf}\Sineweave
DisableDirPage=yes
DisableProgramGroupPage=yes
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
PrivilegesRequired=admin
MinVersion=10.0
OutputDir={#OutputDir}
OutputBaseFilename=Sineweave-Windows-x64-Setup
Compression=lzma2
SolidCompression=yes
WizardStyle=modern
VersionInfoVersion={#AppVersion}.0
VersionInfoDescription=Sineweave Windows x64 VST3 ({#CommitSha})
UninstallDisplayName=Sineweave VST3
CloseApplications=yes
RestartApplications=no

[Files]
Source: "{#StageDir}\Sineweave.vst3\*"; DestDir: "{commoncf64}\VST3\Sineweave.vst3"; Flags: ignoreversion recursesubdirs createallsubdirs
Source: "{#StageDir}\LICENSE"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#StageDir}\README.md"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#StageDir}\THIRD_PARTY_NOTICES.md"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#StageDir}\licenses\*"; DestDir: "{app}\licenses"; Flags: ignoreversion recursesubdirs createallsubdirs
Source: "{#StageDir}\docs\*"; DestDir: "{app}\docs"; Flags: ignoreversion recursesubdirs createallsubdirs
Source: "{#RedistPath}"; Flags: dontcopy

[Code]
var
  RuntimeRestart: Boolean;

function RuntimeNeeded: Boolean;
var
  Major, Minor, Build, Revision: Cardinal;
begin
  Result := True;
  if RegQueryDWordValue(HKLM64, 'SOFTWARE\Microsoft\VisualStudio\14.0\VC\Runtimes\x64', 'Major', Major) and
     RegQueryDWordValue(HKLM64, 'SOFTWARE\Microsoft\VisualStudio\14.0\VC\Runtimes\x64', 'Minor', Minor) and
     RegQueryDWordValue(HKLM64, 'SOFTWARE\Microsoft\VisualStudio\14.0\VC\Runtimes\x64', 'Bld', Build) and
     RegQueryDWordValue(HKLM64, 'SOFTWARE\Microsoft\VisualStudio\14.0\VC\Runtimes\x64', 'Rbld', Revision) then
    Result := ComparePackedVersion(PackVersionComponents(Major, Minor, Build, Revision),
      PackVersionComponents({#RedistMajor}, {#RedistMinor}, {#RedistBuild}, {#RedistRevision})) < 0;
end;

function PrepareToInstall(var NeedsRestart: Boolean): String;
var
  ExitCode: Integer;
begin
  Result := '';
  if RuntimeNeeded then begin
    ExtractTemporaryFile('vc_redist.x64.exe');
    if not Exec(ExpandConstant('{tmp}\vc_redist.x64.exe'), '/install /quiet /norestart', '', SW_HIDE,
      ewWaitUntilTerminated, ExitCode) then
      Result := 'Unable to start the Microsoft Visual C++ runtime installer.'
    else if ExitCode = 3010 then
      RuntimeRestart := True
    else if ExitCode <> 0 then
      Result := 'Microsoft Visual C++ runtime installation failed (code ' + IntToStr(ExitCode) + ').';
  end;
end;

function NeedRestart: Boolean;
begin
  Result := RuntimeRestart;
end;
