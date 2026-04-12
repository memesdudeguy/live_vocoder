; Inno Setup 6 — packages the portable folder (LiveVocoder.exe + DLLs + optional embeddable Python).
; Icon: LiveVocoder.ico from assets/app-icon.png (or .webp) via python3 installer/gen_livevocoder_ico.py.
;
; Source directory (first match wins):
;   ..\dist-windows-cross  — Linux cross-build + bundle-dist-windows-cross-python.sh (full app)
;   ..\dist-windows        — MSYS2 bundle-mingw.sh (exe + DLLs; add Python bundle for full installer)
;
; Compile: cpp\build-installer.bat  |  ./build-installer.sh (Linux: Wine + Inno in WINEPREFIX)
; Output: cpp\dist-installer\LiveVocoder_Setup_<version>.exe
;
; C++-only (no Python): LiveVocoderCppMinimal.iss + build-installer-minimal.bat / build-installer-minimal.sh
; https://jrsoftware.org/isinfo.php

#define MyAppName "LiveVocoder"
#define MyAppVersion "7.0"
#define MyAppPublisher "memesdudeguy"
#define MyAppExeName "LiveVocoder.exe"

#ifexist "..\dist-windows-cross\LiveVocoder.exe"
#define PortableRoot "..\dist-windows-cross"
#else
#ifexist "..\dist-windows\LiveVocoder.exe"
#define PortableRoot "..\dist-windows"
#else
#define PortableRoot "..\dist-windows-cross"
#endif
#endif

[Setup]
AppId=com.live_vocoder.LiveVocoder.x64
AppName={#MyAppName}
AppVersion={#MyAppVersion}
AppPublisher={#MyAppPublisher}
AppVerName={#MyAppName} {#MyAppVersion}
DefaultDirName={autopf}\{#MyAppName}
DefaultGroupName={#MyAppName}
OutputDir=..\dist-installer
OutputBaseFilename=LiveVocoder_Setup_{#MyAppVersion}
SetupIconFile=LiveVocoder.ico
UninstallDisplayIcon={app}\LiveVocoder.ico
VersionInfoVersion={#MyAppVersion}.0.0
VersionInfoProductVersion={#MyAppVersion}
VersionInfoTextVersion={#MyAppVersion}
VersionInfoProductName={#MyAppName}
VersionInfoCompany={#MyAppPublisher}
VersionInfoDescription={#MyAppName} {#MyAppVersion} Setup
Compression=lzma2/ultra64
SolidCompression=yes
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
DisableProgramGroupPage=yes
PrivilegesRequiredOverridesAllowed=dialog
WizardStyle=modern
; Windows 7 SP1+ only (matches C++ build baseline). XP/Vista unsupported.
MinVersion=6.1sp1

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"

[Tasks]
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"; Flags: unchecked
Name: "pipdeps"; Description: "Install Python packages now (network; run once after install)"; GroupDescription: "First-time setup:"; Flags: unchecked

[Files]
Source: "LiveVocoder.ico"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#PortableRoot}\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs; \
  Excludes: "**\*.log,*.log,cpp_gui_launch_log.txt,.wine_deps_installed,LiveVocoder.ico"

[Icons]
Name: "{group}\{#MyAppName}"; Filename: "{app}\{#MyAppExeName}"; WorkingDir: "{app}"; IconFilename: "{app}\LiveVocoder.ico"; Comment: "{#MyAppName}"
Name: "{group}\Web GUI (browser)"; Filename: "{app}\run_web_gui.bat"; WorkingDir: "{app}"; IconFilename: "{app}\LiveVocoder.ico"; Comment: "Gradio web UI"
Name: "{group}\GTK GUI"; Filename: "{app}\run_gtk_gui.bat"; WorkingDir: "{app}"; IconFilename: "{app}\LiveVocoder.ico"; Comment: "Native GTK window (needs PyGObject + GTK4)"
Name: "{group}\Install Python dependencies"; Filename: "{app}\install_python_deps.bat"; WorkingDir: "{app}"; Comment: "pip install -r requirements.txt into bundled Python"
Name: "{group}\Run via batch (any flags)"; Filename: "{app}\run_live_vocoder.bat"; WorkingDir: "{app}"; IconFilename: "{app}\LiveVocoder.ico"; Comment: "python live_vocoder.py %*"
Name: "{group}\{cm:UninstallProgram,{#MyAppName}}"; Filename: "{uninstallexe}"
Name: "{commondesktop}\{#MyAppName}"; Filename: "{app}\{#MyAppExeName}"; WorkingDir: "{app}"; IconFilename: "{app}\LiveVocoder.ico"; Tasks: desktopicon

[Run]
Filename: "{app}\install_python_deps.bat"; WorkingDir: "{app}"; StatusMsg: "Installing Python packages (pip)…"; Flags: shellexec waituntilterminated skipifdoesntexist; Tasks: pipdeps
Filename: "{app}\{#MyAppExeName}"; WorkingDir: "{app}"; Description: "Launch {#MyAppName}"; Flags: nowait postinstall skipifsilent skipifdoesntexist

[UninstallDelete]
Type: files; Name: "{app}\python_last_run.log"
Type: files; Name: "{app}\cpp_gui_launch_log.txt"
Type: files; Name: "{app}\install_python_deps_last.log"
Type: files; Name: "{app}\.wine_deps_installed"
