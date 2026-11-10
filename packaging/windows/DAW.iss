; The Windows installer (Inno Setup 6). The release workflow compiles it:
;
;   ISCC /DAppVersion=0.8.0 /DSourceDir=<folder with MICINSTATION.exe> /DOutputDir=<folder>
;        /DIconFile=<icon.ico> packaging\windows\DAW.iss
;
; It asks whether to install for the current user only (no administrator rights, the
; default; updates then need none either) or for everyone (Program Files); the update
; installer passes /CURRENTUSER or /ALLUSERS to keep the same. Uninstalling leaves the
; user's settings, logs and projects alone.
;
; Up to v0.8.0-rc.4 the app was called DAW: this installer replaces such an installation
; in its folder (same AppId), and takes away what the old name left (shortcuts, the
; scanner, the file types). The old app runs it to update itself and then starts DAW.exe:
; so, that time only, a copy of the new program is left as DAW.exe, which starts
; MICINSTATION.exe (services::UpdateInstaller::handoverFromPreviousName()); the next
; update removes it.

#ifndef AppVersion
  #error Pass /DAppVersion=x.y.z
#endif
#ifndef SourceDir
  #error Pass /DSourceDir=<the folder with MICINSTATION.exe>
#endif
#ifndef OutputDir
  #define OutputDir "."
#endif

#define AppName "MICINSTATION"
#define AppExe "MICINSTATION.exe"
#define PreviousName "DAW"
#define PreviousExe "DAW.exe"
#define Publisher "tavanzz"
#define Repository "https://github.com/tavanzz/daw"

[Setup]
; Never change the AppId: Windows knows the installed app by it (also under its previous
; name), and updates replace it.
AppId={{6BCA2E0C-45E2-4868-97D7-234D684ADDCB}
AppName={#AppName}
AppVersion={#AppVersion}
AppVerName={#AppName} {#AppVersion}
AppPublisher={#Publisher}
AppPublisherURL={#Repository}
AppSupportURL={#Repository}/issues
AppUpdatesURL={#Repository}/releases
VersionInfoVersion={#Copy(AppVersion, 1, Pos("-", AppVersion + "-") - 1)}
DefaultDirName={autopf}\{#AppName}
DefaultGroupName={#AppName}
DisableProgramGroupPage=yes
PrivilegesRequired=lowest
PrivilegesRequiredOverridesAllowed=dialog commandline
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
MinVersion=10.0
OutputDir={#OutputDir}
OutputBaseFilename={#AppName}-{#AppVersion}-Windows-x64-Setup
#ifdef IconFile
SetupIconFile={#IconFile}
#endif
UninstallDisplayIcon={app}\{#AppExe}
UninstallDisplayName={#AppName}
Compression=lzma2
SolidCompression=yes
WizardStyle=modern
ChangesAssociations=yes
CloseApplications=yes
RestartApplications=no

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"
Name: "italian"; MessagesFile: "compiler:Languages\Italian.isl"

[Tasks]
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"; Flags: unchecked

[Files]
Source: "{#SourceDir}\{#AppExe}"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#SourceDir}\{#AppName} Plugin Scanner.exe"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#SourceDir}\{#AppExe}"; DestDir: "{app}"; DestName: "{#PreviousExe}"; Flags: ignoreversion; Check: UpdatingPreviousName
Source: "{#SourceDir}\Licences\*"; DestDir: "{app}\Licences"; Flags: ignoreversion recursesubdirs createallsubdirs

[InstallDelete]
; Licences of components a newer version no longer has.
Type: filesandordirs; Name: "{app}\Licences"
; What the app's previous name left.
Type: files; Name: "{app}\{#PreviousName} Plugin Scanner.exe"
Type: files; Name: "{app}\{#PreviousExe}"; Check: not UpdatingPreviousName
Type: files; Name: "{autoprograms}\{#PreviousName}.lnk"
Type: files; Name: "{autodesktop}\{#PreviousName}.lnk"

[Icons]
Name: "{autoprograms}\{#AppName}"; Filename: "{app}\{#AppExe}"
Name: "{autodesktop}\{#AppName}"; Filename: "{app}\{#AppExe}"; Tasks: desktopicon

[Registry]
; Project files open with the app (HKA: the current user's or everyone's, as installed):
; .micin, its own, and .dawproject, as it saved them under its previous name, which it
; does not claim (Bitwig's DAWproject format has the same extension).
Root: HKA; Subkey: "Software\Classes\.micin"; ValueType: string; ValueName: ""; ValueData: "{#AppName}.Project"; Flags: uninsdeletevalue
Root: HKA; Subkey: "Software\Classes\.micin\OpenWithProgids"; ValueType: string; ValueName: "{#AppName}.Project"; ValueData: ""; Flags: uninsdeletevalue
Root: HKA; Subkey: "Software\Classes\.dawproject\OpenWithProgids"; ValueType: string; ValueName: "{#AppName}.Project"; ValueData: ""; Flags: uninsdeletevalue
Root: HKA; Subkey: "Software\Classes\{#AppName}.Project"; ValueType: string; ValueName: ""; ValueData: "{#AppName} Project"; Flags: uninsdeletekey
Root: HKA; Subkey: "Software\Classes\{#AppName}.Project\DefaultIcon"; ValueType: string; ValueName: ""; ValueData: "{app}\{#AppExe},0"
Root: HKA; Subkey: "Software\Classes\{#AppName}.Project\shell\open\command"; ValueType: string; ValueName: ""; ValueData: """{app}\{#AppExe}"" ""%1"""
Root: HKA; Subkey: "Software\Classes\Applications\{#AppExe}\SupportedTypes"; ValueType: string; ValueName: ".micin"; ValueData: ""; Flags: uninsdeletekey
Root: HKA; Subkey: "Software\Classes\Applications\{#AppExe}\SupportedTypes"; ValueType: string; ValueName: ".dawproject"; ValueData: ""; Flags: uninsdeletekey
; Links of co-op sessions (micinstation://join?..., micinstation://server?...) open the app,
; which hands them to the copy already running.
Root: HKA; Subkey: "Software\Classes\micinstation"; ValueType: string; ValueName: ""; ValueData: "URL:{#AppName} link"; Flags: uninsdeletekey
Root: HKA; Subkey: "Software\Classes\micinstation"; ValueType: string; ValueName: "URL Protocol"; ValueData: ""
Root: HKA; Subkey: "Software\Classes\micinstation\DefaultIcon"; ValueType: string; ValueName: ""; ValueData: "{app}\{#AppExe},0"
Root: HKA; Subkey: "Software\Classes\micinstation\shell\open\command"; ValueType: string; ValueName: ""; ValueData: """{app}\{#AppExe}"" ""%1"""
; What the app registered under its previous name.
Root: HKA; Subkey: "Software\Classes\.dawproject\OpenWithProgids"; ValueType: none; ValueName: "{#PreviousName}.Project"; Flags: deletevalue
Root: HKA; Subkey: "Software\Classes\{#PreviousName}.Project"; ValueType: none; Flags: deletekey
Root: HKA; Subkey: "Software\Classes\Applications\{#PreviousExe}"; ValueType: none; Flags: deletekey

[Run]
Filename: "{app}\{#AppExe}"; Description: "{cm:LaunchProgram,{#AppName}}"; Flags: nowait postinstall skipifsilent

[Code]
var
  FromPreviousName: Boolean;

// Whether this replaces an installation of the app under its previous name: decided once,
// before anything is installed or deleted.
function UpdatingPreviousName: Boolean;
begin
  Result := FromPreviousName;
end;

procedure CurStepChanged(CurStep: TSetupStep);
begin
  if CurStep = ssInstall then
    FromPreviousName := FileExists(ExpandConstant('{app}\{#PreviousExe}')) and
                        not FileExists(ExpandConstant('{app}\{#AppExe}'));
end;
