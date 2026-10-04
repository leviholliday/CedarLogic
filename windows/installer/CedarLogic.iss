; CedarLogic for Windows (native): its installer, made with Inno Setup 6 by
; CI (.github/workflows/windows-native.yml) from the folder the zip is made
; of. For this user only, so no administrator is needed: CedarLogic goes in
; %LOCALAPPDATA%\Programs\CedarLogic, with a Start menu entry (and one on
; the desktop if asked), .cdl files opening in it, and an uninstaller in
; Settings > Apps. The app's own updates keep working there: the folder is
; the user's, and they replace the files beside the exe as in the zip.
;
;   ISCC /DAppVersion=0.1.0 /DArch=x64 windows\installer\CedarLogic.iss
;
; Arch is x64 or ARM64. AppFiles, the folder with CedarLogic.exe and res,
; is package\CedarLogic at the repo's root unless given (where CI makes the
; zip from); the setup is made at the repo's root, as
; CedarLogic-Windows-Setup-<Arch>.exe. The pictures come from
; make-images.swift.
;
; The names here are the ones the app uses when a copy run from the zip adds
; itself to Start (windows/App/Integration.cpp), so this takes those over,
; and InstallDir tells the app it was installed.

#ifndef AppVersion
  #define AppVersion "0.1.0"
#endif
#ifndef Arch
  #define Arch "x64"
#endif
#ifndef AppFiles
  #define AppFiles AddBackslash(SourcePath) + "..\..\package\CedarLogic"
#endif
#ifndef SetupDir
  #define SetupDir AddBackslash(SourcePath) + "..\.."
#endif

; Which Windows each runs on: ARM64 on Windows on ARM; x64 on x64 PCs, and
; on Windows 11 on ARM too, which runs x64 apps (Inno Setup 6.3 and later
; can say so; before, x64 means x64 PCs only).
#if Arch == "ARM64"
  #define ArchId "arm64"
#elif Arch == "x64"
  #if VER >= EncodeVer(6, 3, 0, 0)
    #define ArchId "x64compatible"
  #else
    #define ArchId "x64"
  #endif
#else
  #error Arch must be x64 or ARM64
#endif

#define AppExe "CedarLogic.exe"
#define ProgId "CedarLogic.Circuit"

[Setup]
; The same for every build, so a new one installs over the last. (The app
; finds its Settings > Apps entry by it, to keep the version there current
; after an update from inside the app: Integration.cpp.)
AppId={{59AB891F-9536-471C-BAE2-13C34308A7C3}
AppName=CedarLogic
AppVersion={#AppVersion}
AppVerName=CedarLogic {#AppVersion}
AppPublisher=Cedarville University
AppPublisherURL=https://cedarlogic.netlify.app
AppSupportURL=https://cedarlogic.netlify.app
AppUpdatesURL=https://github.com/leviholliday/CedarLogic/releases
VersionInfoDescription=CedarLogic Setup
VersionInfoProductName=CedarLogic
; This user only: nothing needs an administrator.
PrivilegesRequired=lowest
DefaultDirName={localappdata}\Programs\CedarLogic
DisableDirPage=yes
DisableProgramGroupPage=yes
DisableReadyPage=yes
ArchitecturesAllowed={#ArchId}
ArchitecturesInstallIn64BitMode={#ArchId}
MinVersion=10.0
; .cdl files open with it; Explorer hears of the change.
ChangesAssociations=yes
; While CedarLogic runs, setup and the uninstaller ask for it to be closed
; first (its windows ask about unsaved work). The app holds this mutex.
AppMutex=CedarLogic.Native
UninstallDisplayIcon={app}\{#AppExe}
UninstallDisplayName=CedarLogic
SetupIconFile=..\..\res\icon.ico
WizardStyle=modern
WizardImageFile=WizardLarge-100.bmp,WizardLarge-150.bmp,WizardLarge-200.bmp
WizardSmallImageFile=WizardSmall-100.bmp,WizardSmall-125.bmp,WizardSmall-150.bmp,WizardSmall-175.bmp,WizardSmall-200.bmp
OutputDir={#SetupDir}
OutputBaseFilename=CedarLogic-Windows-Setup-{#Arch}
Compression=lzma2/max
SolidCompression=yes
SetupLogging=yes

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"

[Tasks]
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"; Flags: unchecked

; Over an older install: its res goes first (a file a newer build dropped
; doesn't linger), with what an update in the app left behind.
[InstallDelete]
Type: filesandordirs; Name: "{app}\res"
Type: filesandordirs; Name: "{app}\res.new"
Type: filesandordirs; Name: "{app}\res.old"
Type: files; Name: "{app}\{#AppExe}.old"

[Files]
Source: "{#AppFiles}\{#AppExe}"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#AppFiles}\res\*"; DestDir: "{app}\res"; Flags: ignoreversion recursesubdirs createallsubdirs
Source: "{#AppFiles}\LICENSE.txt"; DestDir: "{app}"; Flags: ignoreversion

[Icons]
Name: "{autoprograms}\CedarLogic"; Filename: "{app}\{#AppExe}"; WorkingDir: "{app}"; Comment: "Build and simulate digital logic circuits"
Name: "{autodesktop}\CedarLogic"; Filename: "{app}\{#AppExe}"; WorkingDir: "{app}"; Comment: "Build and simulate digital logic circuits"; Tasks: desktopicon

; .cdl files: this user's own (HKCU\Software\Classes), with the page icon
; that's the exe's second, and opened in CedarLogic -- in the one already
; running when there is one (the app hands them over).
[Registry]
Root: HKCU; Subkey: "Software\Classes\.cdl"; ValueType: string; ValueName: ""; ValueData: "{#ProgId}"; Flags: uninsdeletevalue uninsdeletekeyifempty
Root: HKCU; Subkey: "Software\Classes\.cdl\OpenWithProgids"; ValueType: string; ValueName: "{#ProgId}"; ValueData: ""; Flags: uninsdeletevalue uninsdeletekeyifempty
Root: HKCU; Subkey: "Software\Classes\{#ProgId}"; ValueType: string; ValueName: ""; ValueData: "CedarLogic Circuit"; Flags: uninsdeletekey
Root: HKCU; Subkey: "Software\Classes\{#ProgId}\DefaultIcon"; ValueType: string; ValueName: ""; ValueData: "{app}\{#AppExe},1"
Root: HKCU; Subkey: "Software\Classes\{#ProgId}\shell\open\command"; ValueType: string; ValueName: ""; ValueData: """{app}\{#AppExe}"" ""%1"""
Root: HKCU; Subkey: "Software\CedarLogic"; Flags: uninsdeletekeyifempty
Root: HKCU; Subkey: "Software\CedarLogic\Native"; ValueType: string; ValueName: "InstallDir"; ValueData: "{app}"; Flags: uninsdeletekey

[Run]
Filename: "{app}\{#AppExe}"; Description: "{cm:LaunchProgram,CedarLogic}"; Flags: nowait postinstall skipifsilent

; What the app's updates and the install above may leave beside it. Your
; Circuits and the settings (in %APPDATA%\CedarLogic) stay.
[UninstallDelete]
Type: filesandordirs; Name: "{app}\res"
Type: filesandordirs; Name: "{app}\res.new"
Type: filesandordirs; Name: "{app}\res.old"
Type: files; Name: "{app}\{#AppExe}.old"
