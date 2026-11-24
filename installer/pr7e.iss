; Paragon PR7-E installer (Inno Setup 6).
;   iscc /DAppVersion=0.1.0 installer\pr7e.iss      (after pyinstaller pr7e.spec has made dist\PR7-E)
;
; Per-user (no admin rights) into %LOCALAPPDATA%\Programs\PR7-E, with a Start menu entry; ticked by default,
; "Open in PR7-E" on Explorer's folder menu; unticked by default, "Use PR7-E for Folders" (double-clicked folders
; and Win+E open PR7-E). PR7-E registers both itself (--register-menu, --default-folders on), so installer, main
; menu and command line share shell.py; --unregister on uninstall takes everything away and folders open in File
; Explorer again. Bookmarks, stars and settings (HKCU\Software\Paragon\PR7-E) stay after an uninstall.

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
Name: "defaultfolders"; Description: "Use PR7-E for folders (double-clicked folders and Win+E open PR7-E)"; GroupDescription: "Explorer:"; Flags: unchecked
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
Filename: "{app}\PR7-E.exe"; Parameters: "--unregister-menu"; Flags: runhidden waituntilterminated; Tasks: not explorermenu
; Only ever turned on here: turning it off is the main menu's tick box (or uninstalling), so a reinstall never undoes
; a choice made in PR7-E.
Filename: "{app}\PR7-E.exe"; Parameters: "--default-folders on"; Flags: runhidden waituntilterminated; Tasks: defaultfolders
Filename: "{app}\PR7-E.exe"; Description: "{cm:LaunchProgram,Paragon PR7-E}"; Flags: nowait postinstall skipifsilent
; Updates (Check for Updates runs this installer with /SILENT /relaunch=1): start PR7-E again when done.
Filename: "{app}\PR7-E.exe"; Flags: nowait runasoriginaluser; Check: Relaunch

[UninstallRun]
Filename: "{app}\PR7-E.exe"; Parameters: "--unregister"; Flags: runhidden waituntilterminated; RunOnceId: "UnregisterMenu"

[Code]
// True only when the updater asked for it (/relaunch=1): normal and CI installs behave as before.
function Relaunch: Boolean;
begin
  Result := ExpandConstant('{param:relaunch|0}') = '1';
end;
