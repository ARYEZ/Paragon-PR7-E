; Paragon PR7-E installer (Inno Setup 6).
;   iscc /DAppVersion=0.1.0 installer\pr7e.iss      (after pyinstaller pr7e.spec has made dist\PR7-E)
;
; Per-user (no admin rights) into %LOCALAPPDATA%\Programs\PR7-E, with a Start menu entry and, ticked by default,
; "Open in PR7-E" on Explorer's folder menu (registered by PR7-E itself: --register-menu; --unregister on
; uninstall). Bookmarks, stars and settings (HKCU\Software\Paragon\PR7-E) stay after an uninstall.

#ifndef AppVersion
  #define AppVersion "0.0.0"
#endif

[Setup]
; AppId is permanent: it's how a new version finds and upgrades the installed one. Never change it.
AppId={{6B8E2F14-3C5A-4D97-A1E0-7F29C4B85D36}
AppName=Paragon PR7-E
AppVersion={#AppVersion}
AppVerName=Paragon PR7-E {#AppVersion}
AppPublisher=Paragon
VersionInfoVersion={#AppVersion}
PrivilegesRequired=lowest
DefaultDirName={autopf}\PR7-E
DefaultGroupName=Paragon PR7-E
DisableProgramGroupPage=yes
UsePreviousAppDir=yes
UsePreviousTasks=yes
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
MinVersion=10.0
OutputDir=..\dist
OutputBaseFilename=PR7-E-Setup-{#AppVersion}
SetupIconFile=..\pr7e\icons\app.ico
UninstallDisplayIcon={app}\PR7-E.exe
UninstallDisplayName=Paragon PR7-E
WizardStyle=modern
WizardImageFile=wizard-large-1x.bmp,wizard-large-2x.bmp
WizardSmallImageFile=wizard-small-1x.bmp,wizard-small-2x.bmp
Compression=lzma2/max
SolidCompression=yes
CloseApplications=yes
RestartApplications=no

[Tasks]
Name: "explorermenu"; Description: "Add ""Open in PR7-E"" to Explorer's folder menu"; GroupDescription: "Explorer:"
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"; Flags: unchecked

[Files]
Source: "..\dist\PR7-E\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs

[InstallDelete]
Type: filesandordirs; Name: "{app}\_internal"

[Icons]
Name: "{autoprograms}\Paragon PR7-E"; Filename: "{app}\PR7-E.exe"; Comment: "Files"
Name: "{autodesktop}\Paragon PR7-E"; Filename: "{app}\PR7-E.exe"; Tasks: desktopicon

[Run]
Filename: "{app}\PR7-E.exe"; Parameters: "--register-menu"; Flags: runhidden waituntilterminated; Tasks: explorermenu
Filename: "{app}\PR7-E.exe"; Parameters: "--unregister"; Flags: runhidden waituntilterminated; Tasks: not explorermenu
Filename: "{app}\PR7-E.exe"; Description: "{cm:LaunchProgram,Paragon PR7-E}"; Flags: nowait postinstall skipifsilent

[UninstallRun]
Filename: "{app}\PR7-E.exe"; Parameters: "--unregister"; Flags: runhidden waituntilterminated; RunOnceId: "UnregisterMenu"
