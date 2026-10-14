; SPDX-License-Identifier: GPL-2.0-or-later
; Blendity's Windows installer (task 0076), for Inno Setup 6: a per-user install (no administrator needed) into
; %LOCALAPPDATA%\Programs\Blendity, Start menu and optional desktop shortcuts, .scene files opening in Blendity, and an
; uninstaller in Settings > Apps. Built by `build.bat installer` or the release workflow:
;   ISCC /DAppVersion=0.10.0 /DDist=..\..\dist\windows /DIcons=<dir with blendity.ico> packaging\windows\blendity.iss
#ifndef AppVersion
  #define AppVersion "0.0.0"
#endif
#ifndef Dist
  #define Dist "..\..\dist\windows"
#endif
#ifndef Icons
  #define Icons "..\..\build\icons"
#endif

[Setup]
AppId={{6B0E3C1A-7D2F-4B8E-9C61-B1E2D0A5F347}
AppName=Blendity
AppVersion={#AppVersion}
AppPublisher=Emiliano Manalo
AppPublisherURL=https://github.com/EmilianoManaloIV/blendity
; {autopf}: %LOCALAPPDATA%\Programs for one user, Program Files when installing for everyone.
DefaultDirName={autopf}\Blendity
DefaultGroupName=Blendity
DisableProgramGroupPage=yes
PrivilegesRequired=lowest
PrivilegesRequiredOverridesAllowed=dialog
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
LicenseFile=..\..\LICENSE
OutputDir=..\..\dist\installers
OutputBaseFilename=Blendity-Setup-{#AppVersion}
SetupIconFile={#Icons}\blendity.ico
UninstallDisplayIcon={app}\blendity.ico
Compression=lzma2/max
SolidCompression=yes
WizardStyle=modern
ChangesAssociations=yes

[Tasks]
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"
Name: "sceneassoc"; Description: "Open .scene files with Blendity"; GroupDescription: "File types:"

[Files]
Source: "{#Dist}\Blendity.exe"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#Dist}\*.dll"; DestDir: "{app}"; Flags: ignoreversion skipifsourcedoesntexist
Source: "{#Dist}\datafiles\*"; DestDir: "{app}\datafiles"; Flags: ignoreversion recursesubdirs createallsubdirs skipifsourcedoesntexist
Source: "{#Dist}\licenses\*"; DestDir: "{app}\licenses"; Flags: ignoreversion recursesubdirs createallsubdirs skipifsourcedoesntexist
Source: "..\..\licenses\*"; DestDir: "{app}\licenses"; Flags: ignoreversion recursesubdirs createallsubdirs
Source: "..\..\LICENSE"; DestDir: "{app}"; Flags: ignoreversion
Source: "..\..\docs\MANUAL.md"; DestDir: "{app}\docs"; Flags: ignoreversion
Source: "{#Icons}\blendity.ico"; DestDir: "{app}"; Flags: ignoreversion

[Icons]
Name: "{autoprograms}\Blendity"; Filename: "{app}\Blendity.exe"; IconFilename: "{app}\blendity.ico"
Name: "{autodesktop}\Blendity"; Filename: "{app}\Blendity.exe"; IconFilename: "{app}\blendity.ico"; Tasks: desktopicon

[Registry]
; Per user (HKCU\Software\Classes), so no administrator is needed; removed with the uninstaller.
Root: HKA; Subkey: "Software\Classes\.scene"; ValueType: string; ValueName: ""; ValueData: "Blendity.Scene"; Flags: uninsdeletevalue uninsdeletekeyifempty; Tasks: sceneassoc
Root: HKA; Subkey: "Software\Classes\Blendity.Scene"; ValueType: string; ValueName: ""; ValueData: "Blendity Scene"; Flags: uninsdeletekey; Tasks: sceneassoc
Root: HKA; Subkey: "Software\Classes\Blendity.Scene\DefaultIcon"; ValueType: string; ValueName: ""; ValueData: "{app}\blendity.ico"; Tasks: sceneassoc
Root: HKA; Subkey: "Software\Classes\Blendity.Scene\shell\open\command"; ValueType: string; ValueName: ""; ValueData: """{app}\Blendity.exe"" ""%1"""; Tasks: sceneassoc

[Run]
Filename: "{app}\Blendity.exe"; Description: "{cm:LaunchProgram,Blendity}"; Flags: nowait postinstall skipifsilent
