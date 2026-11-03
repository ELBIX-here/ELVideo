; Inno Setup 6 script. Built by scripts\package.ps1, which passes:
;   /DAppVersion=0.1.0  /DSourceDir=<dist\ELVideo>  /DOutputDir=<dist>

#ifndef AppVersion
  #error Pass /DAppVersion=x.y.z (use scripts\package.ps1)
#endif
#ifndef SourceDir
  #define SourceDir "..\dist\ELVideo"
#endif
#ifndef OutputDir
  #define OutputDir "..\dist"
#endif

[Setup]
AppId={{6C1F3E0A-5B7D-4B8E-9E2A-3D6F1C0B7A42}
AppName=ELVideo
AppVersion={#AppVersion}
AppVerName=ELVideo {#AppVersion}
AppPublisher=ELVideo
VersionInfoVersion={#AppVersion}
; Per-user install by default (no admin prompt); the dialog offers all users.
PrivilegesRequired=lowest
PrivilegesRequiredOverridesAllowed=dialog
DefaultDirName={autopf}\ELVideo
DisableProgramGroupPage=yes
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
; Windows 10 2004: WDA_EXCLUDEFROMCAPTURE and borderless-capture era APIs.
MinVersion=10.0.19041
OutputDir={#OutputDir}
OutputBaseFilename=ELVideo-{#AppVersion}-setup
SetupIconFile=..\app\resources\elvideo.ico
UninstallDisplayIcon={app}\ELVideo.exe
UninstallDisplayName=ELVideo
Compression=lzma2/max
SolidCompression=yes
WizardStyle=modern
; The app holds this mutex while running; setup and uninstall ask to close it.
AppMutex=Local\ELVideo.SingleInstance
CloseApplications=yes

[Languages]
Name: "korean"; MessagesFile: "compiler:Languages\Korean.isl"
Name: "english"; MessagesFile: "compiler:Default.isl"

[Tasks]
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"; Flags: unchecked

[Files]
Source: "{#SourceDir}\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs

[Icons]
Name: "{autoprograms}\ELVideo"; Filename: "{app}\ELVideo.exe"
Name: "{autodesktop}\ELVideo"; Filename: "{app}\ELVideo.exe"; Tasks: desktopicon

[Run]
Filename: "{app}\ELVideo.exe"; Description: "{cm:LaunchProgram,ELVideo}"; Flags: nowait postinstall skipifsilent

; Settings (%APPDATA%\ELVideo), logs and crash dumps (%LOCALAPPDATA%\ELVideo) and
; recordings are left in place on uninstall.
