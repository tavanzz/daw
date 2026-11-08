; The Windows installer (Inno Setup 6). The release workflow compiles it:
;
;   ISCC /DAppVersion=0.8.0 /DSourceDir=<folder with DAW.exe> /DOutputDir=<folder>
;        /DIconFile=<icon.ico> packaging\windows\DAW.iss
;
; It asks whether to install for the current user only (no administrator rights, the
; default; updates then need none either) or for everyone (Program Files); the update
; installer passes /CURRENTUSER or /ALLUSERS to keep the same. Uninstalling leaves the
; user's settings, logs and projects alone.

#ifndef AppVersion
  #error Pass /DAppVersion=x.y.z
#endif
#ifndef SourceDir
  #error Pass /DSourceDir=<the folder with DAW.exe>
#endif
#ifndef OutputDir
  #define OutputDir "."
#endif

#define AppName "DAW"
#define AppExe "DAW.exe"
#define Publisher "tavanzz"
#define Repository "https://github.com/tavanzz/daw"

[Setup]
; Never change the AppId: Windows knows the installed app by it, and updates replace it.
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
Source: "{#SourceDir}\Licences\*"; DestDir: "{app}\Licences"; Flags: ignoreversion recursesubdirs createallsubdirs

[InstallDelete]
; Licences of components a newer version no longer has.
Type: filesandordirs; Name: "{app}\Licences"

[Icons]
Name: "{autoprograms}\{#AppName}"; Filename: "{app}\{#AppExe}"
Name: "{autodesktop}\{#AppName}"; Filename: "{app}\{#AppExe}"; Tasks: desktopicon

[Registry]
; Project files open with the app (HKA: the current user's or everyone's, as installed).
Root: HKA; Subkey: "Software\Classes\.dawproject\OpenWithProgids"; ValueType: string; ValueName: "DAW.Project"; ValueData: ""; Flags: uninsdeletevalue
Root: HKA; Subkey: "Software\Classes\DAW.Project"; ValueType: string; ValueName: ""; ValueData: "{#AppName} Project"; Flags: uninsdeletekey
Root: HKA; Subkey: "Software\Classes\DAW.Project\DefaultIcon"; ValueType: string; ValueName: ""; ValueData: "{app}\{#AppExe},0"
Root: HKA; Subkey: "Software\Classes\DAW.Project\shell\open\command"; ValueType: string; ValueName: ""; ValueData: """{app}\{#AppExe}"" ""%1"""
Root: HKA; Subkey: "Software\Classes\Applications\{#AppExe}\SupportedTypes"; ValueType: string; ValueName: ".dawproject"; ValueData: ""; Flags: uninsdeletekey

[Run]
Filename: "{app}\{#AppExe}"; Description: "{cm:LaunchProgram,{#AppName}}"; Flags: nowait postinstall skipifsilent
