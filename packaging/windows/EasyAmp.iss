; Inno Setup script for EasyAmp (Windows installer).
; Built in CI: iscc /DAppVersion=<x.y.z> EasyAmp.iss
; Paths are relative to this .iss file (packaging/windows).

#ifndef AppVersion
  #define AppVersion "0.0.0"
#endif

[Setup]
AppName=EasyAmp
AppVersion={#AppVersion}
AppPublisher=VonHoltenCodes
AppPublisherURL=https://github.com/VonHoltenCodes/EasyAmp
DefaultDirName={autopf}\EasyAmp
DefaultGroupName=EasyAmp
UninstallDisplayIcon={app}\EasyAmp.exe
OutputDir=.
OutputBaseFilename=EasyAmp-Setup-x64
Compression=lzma2
SolidCompression=yes
ArchitecturesAllowed=x64
ArchitecturesInstallIn64BitMode=x64
WizardStyle=modern
DisableProgramGroupPage=yes
; In-place upgrades: AppId defaults to AppName ("EasyAmp") and DefaultDirName
; is constant, so a new installer upgrades the existing install in place —
; the splash tells users they can install right over their current version.
; CloseApplications lets Setup shut a running EasyAmp so the .exe can be
; replaced instead of erroring on a locked file.
CloseApplications=yes
RestartApplications=no

[Files]
Source: "dist\EasyAmp\*"; DestDir: "{app}"; Flags: recursesubdirs createallsubdirs

[Icons]
Name: "{group}\EasyAmp"; Filename: "{app}\EasyAmp.exe"
Name: "{group}\Uninstall EasyAmp"; Filename: "{uninstallexe}"
Name: "{autodesktop}\EasyAmp"; Filename: "{app}\EasyAmp.exe"; Tasks: desktopicon

[Tasks]
Name: "desktopicon"; Description: "Create a &desktop shortcut"; GroupDescription: "Additional icons:"; Flags: unchecked

[Run]
Filename: "{app}\EasyAmp.exe"; Description: "Launch EasyAmp"; Flags: nowait postinstall skipifsilent
