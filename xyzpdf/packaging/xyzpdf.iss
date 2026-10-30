; The GitHub installer: XYZPDF-<version>-setup.exe.
;
; Built by packaging/build-installer.ps1, which stages the program with
; build-package.ps1 -StageOnly (the same payload the MSIX carries: Qt's unused
; 29 MB stripped, the C runtime and the QPDF runtime copied in) and passes the
; four defines below. Compiling this file by hand without them is an error on
; purpose: a version typed here would be a second place the number lives.
;
; Per-user, no administrator: %LOCALAPPDATA%\Programs\XYZPDF, HKCU only. Every
; registry entry the program itself writes (the ProgID, 打开方式, the right-click
; menu, the preview pane pointer under the ProgID) is written by the program,
; through `xyzpdf.exe --register-shell` -- the same code the settings box runs --
; and removed by `--unregister-shell` before the files go. What this script
; writes itself is only what has to sit beside a file it installed: the COM class
; of the preview handler, whose InprocServer32 is the DLL's path.
;
; Not registered here: the thumbnail provider. It implements IInitializeWithFile,
; and Windows runs such a provider inside explorer.exe (it has to opt out of the
; isolated thumbnail process to get a path) -- Qt and PDFium loaded into Explorer.
; The MSIX hosts it in DllHost instead. Until it takes a stream, the installed
; build has no thumbnails. The Explorer command (xyzpdfshell.dll) is not
; registered either: Windows 11's first-level menu takes it only from a package,
; and the classic verb the program writes covers the same thing.

#ifndef AppVersion
  #error AppVersion is passed by build-installer.ps1
#endif
#ifndef StageDir
  #error StageDir is passed by build-installer.ps1
#endif
#ifndef OutputDir
  #error OutputDir is passed by build-installer.ps1
#endif
#ifndef PreviewClsid
  #error PreviewClsid is passed by build-installer.ps1 (read from AppxManifest.xml)
#endif

[Setup]
; Never change this: it is how an upgrade finds the copy it replaces, and how
; Windows lists the program under 已安装的应用.
AppId={{6F02474A-0B25-4D66-9CD2-188EE91B17E3}
AppName=XYZPDF
AppVersion={#AppVersion}
AppVerName=XYZPDF {#AppVersion}
AppPublisher=allgoodhi
AppPublisherURL=https://github.com/allgoodhi/XYZPDF
AppSupportURL=https://github.com/allgoodhi/XYZPDF/issues
AppUpdatesURL=https://github.com/allgoodhi/XYZPDF/releases
VersionInfoVersion={#AppVersion}
VersionInfoProductName=XYZPDF
VersionInfoDescription=XYZPDF Setup
PrivilegesRequired=lowest
DefaultDirName={autopf}\XYZPDF
DisableProgramGroupPage=yes
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
; The same floor as the MSIX manifest's MinVersion.
MinVersion=10.0.17763
OutputDir={#OutputDir}
OutputBaseFilename=XYZPDF-{#AppVersion}-setup
SetupIconFile=app.ico
UninstallDisplayIcon={app}\bin\xyzpdf.exe
UninstallDisplayName=XYZPDF
LicenseFile=..\..\LICENSE
Compression=lzma2/max
SolidCompression=yes
WizardStyle=modern
; A running XYZPDF holds its DLLs open; Restart Manager asks it to close rather
; than leaving the upgrade half-copied.
CloseApplications=yes
RestartApplications=no
ChangesAssociations=yes
ShowLanguageDialog=auto

[Languages]
; ChineseSimplified.isl is Inno Setup's user-contributed translation (Files/Languages/
; Unofficial in jrsoftware/issrc at the is-6_7_4 tag), committed beside this file
; because the official set does not include it.
Name: "chinesesimplified"; MessagesFile: "ChineseSimplified.isl"
Name: "english"; MessagesFile: "compiler:Default.isl"

[CustomMessages]
chinesesimplified.TaskShell=在「打开方式」和右键菜单里加入 XYZPDF
english.TaskShell=Add XYZPDF to "Open with" and the right-click menu
chinesesimplified.TaskPreview=在资源管理器的预览窗格里预览 PDF（XYZPDF 是 PDF 默认程序时生效）
english.TaskPreview=Preview PDFs in Explorer's preview pane (when XYZPDF opens PDFs by default)
chinesesimplified.GroupShell=资源管理器：
english.GroupShell=File Explorer:
chinesesimplified.StatusShell=正在登记文件关联……
english.StatusShell=Registering the file association...

[Tasks]
Name: "shell"; Description: "{cm:TaskShell}"; GroupDescription: "{cm:GroupShell}"
Name: "shell\preview"; Description: "{cm:TaskPreview}"; GroupDescription: "{cm:GroupShell}"
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"; Flags: unchecked

[Files]
Source: "{#StageDir}\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs

[Icons]
Name: "{autoprograms}\XYZPDF"; Filename: "{app}\bin\xyzpdf.exe"
Name: "{autodesktop}\XYZPDF"; Filename: "{app}\bin\xyzpdf.exe"; Tasks: desktopicon

[Registry]
; The preview handler's class. Loaded by prevhost.exe (that AppID is Windows' own
; 64-bit preview host), never by Explorer itself. The program points the ProgID
; at it; this only makes the class exist.
Root: HKCU; Subkey: "Software\Classes\CLSID\{{{#PreviewClsid}}"; ValueType: string; ValueData: "XYZPDF"; Flags: uninsdeletekey; Tasks: shell\preview
Root: HKCU; Subkey: "Software\Classes\CLSID\{{{#PreviewClsid}}"; ValueType: string; ValueName: "AppID"; ValueData: "{{6D2B5079-2F0B-48DD-AB7F-97CEC514D30B}"; Tasks: shell\preview
Root: HKCU; Subkey: "Software\Classes\CLSID\{{{#PreviewClsid}}\InprocServer32"; ValueType: string; ValueData: "{app}\bin\xyzpdfpreview.dll"; Tasks: shell\preview
Root: HKCU; Subkey: "Software\Classes\CLSID\{{{#PreviewClsid}}\InprocServer32"; ValueType: string; ValueName: "ThreadingModel"; ValueData: "Apartment"; Tasks: shell\preview
Root: HKCU; Subkey: "Software\Microsoft\Windows\CurrentVersion\PreviewHandlers"; ValueType: string; ValueName: "{{{#PreviewClsid}}"; ValueData: "XYZPDF"; Flags: uninsdeletevalue; Tasks: shell\preview
; An upgrade with the box cleared takes the class away again: Inno leaves the
; entries of a task that is no longer selected where they were.
Root: HKCU; Subkey: "Software\Classes\CLSID\{{{#PreviewClsid}}"; ValueType: none; Flags: deletekey; Tasks: not shell\preview
Root: HKCU; Subkey: "Software\Microsoft\Windows\CurrentVersion\PreviewHandlers"; ValueType: none; ValueName: "{{{#PreviewClsid}}"; Flags: deletevalue; Tasks: not shell\preview

[Run]
Filename: "{app}\bin\xyzpdf.exe"; Parameters: "--register-shell"; StatusMsg: "{cm:StatusShell}"; Flags: runhidden waituntilterminated; Tasks: shell
Filename: "{app}\bin\xyzpdf.exe"; Parameters: "--unregister-shell"; Flags: runhidden waituntilterminated; Tasks: not shell
Filename: "{app}\bin\xyzpdf.exe"; Description: "{cm:LaunchProgram,XYZPDF}"; Flags: nowait postinstall skipifsilent

[UninstallRun]
; Before the files go, while the program that knows its own keys is still there.
Filename: "{app}\bin\xyzpdf.exe"; Parameters: "--unregister-shell"; Flags: runhidden waituntilterminated; RunOnceId: "UnregisterShell"
