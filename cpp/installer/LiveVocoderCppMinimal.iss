; Inno Setup 6 — C++ SDL only (from cpp/). Stage: ../bundle-installer-minimal.sh
; Single output for native Windows and Wine on Linux:
;   ISCC LiveVocoderCppMinimal.iss  →  ..\dist-installer\LiveVocoder-Setup.exe
; Wine-only files/registry use Check: IsRunningUnderWine (install-time), not a second compile.
; https://jrsoftware.org/isinfo.php

#define MyAppName "Live Vocoder"
#define MyAppVersion "6.0"
#define MyAppPublisher "memesdudeguy"
#define MyAppExeName "LiveVocoder.exe"
#define MyOutputBase "LiveVocoder-Setup"
#define MyFlavorSuffix " (minimal C++)"
; Linux: embedded shell paths (not bare "sh"/"bash" from PATH) for .desktop and host helpers.
#define SetupEmbeddedShPrefix "/bin/sh"
#define SetupEmbeddedBashPrefix "/bin/bash"
#define MinimalRoot "..\dist-windows-installer-minimal"

[Setup]
AppId=com.live_vocoder.LiveVocoder.cpp.sdl.x64
AppName={#MyAppName}
AppVersion={#MyAppVersion}
AppPublisher={#MyAppPublisher}
AppVerName={#MyAppName} {#MyAppVersion}{#MyFlavorSuffix}
AppCopyright=Copyright (C) {#MyAppPublisher}
DefaultDirName={autopf}\{#MyAppName}
DefaultGroupName={#MyAppName}
OutputDir=..\dist-installer
OutputBaseFilename={#MyOutputBase}
SetupIconFile=LiveVocoder.ico
; WizardSmallImageFile must be a valid .bmp for many Inno builds; .ico here triggers "Bitmap image is not valid."
UninstallDisplayIcon={app}\LiveVocoder.ico
; File properties → Details tab in Explorer (and consistent version resource on Setup.exe).
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
; commandline: /CURRENTUSER or /ALLUSERS without a GUI prompt (needed for /VERYSILENT under Wine).
PrivilegesRequiredOverridesAllowed=commandline
UsedUserAreasWarning=no
; Match SDL GUI palette (sdl_gui.cpp: kBgTop / kCardFill) + Inno 6 dark modern wizard.
WizardStyle=modern dark hidebevels includetitlebar
WizardBackColor=#181b26
WizardImageBackColor=#0b0e14
WizardSmallImageBackColor=#0b0e14
CloseApplications=yes
RestartApplications=no

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"

[Tasks]
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"; Flags: unchecked
#ifexist "..\dist-windows-installer-minimal\extras\VBCABLE_Setup_x64.exe"
Name: "vbcable"; Description: "Install VB-Audio Virtual Cable (virtual mic + CABLE devices for Discord/OBS; UAC / driver prompts)"; GroupDescription: "Windows native audio:"; Flags: checkedonce; Check: not IsRunningUnderWine
#endif

[Files]
Source: "LiveVocoder.ico"; DestDir: "{app}"; Flags: ignoreversion
; App + carrier converter: both must land in the same directory ({app}) so LiveVocoder.exe finds sibling ffmpeg.exe.
Source: "{#MinimalRoot}\LiveVocoder.exe"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#MinimalRoot}\ffmpeg.exe"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#MinimalRoot}\*.dll"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#MinimalRoot}\app-icon.png"; DestDir: "{app}"; Flags: ignoreversion skipifsourcedoesntexist
Source: "{#MinimalRoot}\fonts\DejaVuSans.ttf"; DestDir: "{app}\fonts"; Flags: ignoreversion skipifsourcedoesntexist
Source: "{#MinimalRoot}\README_Cpp_Minimal.txt"; DestDir: "{app}"; DestName: "README.txt"; Flags: ignoreversion skipifsourcedoesntexist
Source: "{#MinimalRoot}\Run-from-QEMU-share.bat"; DestDir: "{app}"; Flags: ignoreversion skipifsourcedoesntexist
Source: "{#MinimalRoot}\smoke_carrier.f32"; DestDir: "{app}"; Flags: ignoreversion skipifsourcedoesntexist
Source: "{#MinimalRoot}\SmokeValidateF32.bat"; DestDir: "{app}"; Flags: ignoreversion skipifsourcedoesntexist
#ifexist "..\dist-windows-installer-minimal\extras\VBCABLE_Setup_x64.exe"
Source: "{#MinimalRoot}\extras\VBCABLE_Setup_x64.exe"; DestDir: "{app}\extras"; Flags: ignoreversion
#endif
; Per-user carrier library (matches SDL: %USERPROFILE%\Documents\LiveVocoderCarriers). Not removed on uninstall.
Source: "CarriersFolderReadme.txt"; DestDir: "{userdocs}\LiveVocoderCarriers"; DestName: "README.txt"; Flags: ignoreversion uninsneveruninstall
; Linux host notes — only when the installer itself runs under Wine (skipped on native Windows).
Source: "README_Wine_Installer.txt"; DestDir: "{app}"; DestName: "README_Wine.txt"; Flags: ignoreversion skipifsourcedoesntexist; Check: IsRunningUnderWine
; Linux host helper for running this Windows installer under Wine only — not used on real Windows.
Source: "sh-LiveVocoder-Setup.sh"; DestDir: "{app}"; Flags: ignoreversion skipifsourcedoesntexist; Check: IsRunningUnderWine

[Registry]
; Pulse sink defaults are for Linux audio when the app runs under Wine — skip on native Windows (PortAudio/WASAPI).
; Reinstall overwrites values (no createvalueifdoesntexist) so a bad manual edit gets fixed on upgrade.
Root: HKCU; Subkey: "Environment"; ValueType: string; ValueName: "LIVE_VOCODER_PULSE_SINK"; ValueData: "live_vocoder"; Flags: uninsdeletevalue; Check: IsRunningUnderWine
Root: HKCU; Subkey: "Environment"; ValueType: string; ValueName: "PULSE_SINK"; ValueData: "live_vocoder"; Flags: uninsdeletevalue; Check: IsRunningUnderWine
; Windows “compatibility mode” shim data — Wine reads AppCompatFlags\Layers for many apps; native Windows installs skip this (Check: IsRunningUnderWine is false there).
Root: HKCU; Subkey: "Software\Microsoft\Windows NT\CurrentVersion\AppCompatFlags\Layers"; ValueType: string; ValueName: "{app}\{#MyAppExeName}"; ValueData: "~ WIN10RTM"; Flags: uninsdeletevalue; Check: IsRunningUnderWine

[Icons]
; Under Wine, shortcuts to LiveVocoder.exe skip PipeWire setup — use the host .desktop from InstallWineLauncherScript instead.
Name: "{group}\{#MyAppName}"; Filename: "{app}\{#MyAppExeName}"; WorkingDir: "{app}"; IconFilename: "{app}\LiveVocoder.ico"; Comment: "{#MyAppName} (SDL2)"; Check: not IsRunningUnderWine
Name: "{group}\{cm:UninstallProgram,{#MyAppName}}"; Filename: "{uninstallexe}"
Name: "{commondesktop}\{#MyAppName}"; Filename: "{app}\{#MyAppExeName}"; WorkingDir: "{app}"; IconFilename: "{app}\LiveVocoder.ico"; Tasks: desktopicon; Check: not IsRunningUnderWine

[Run]
#ifexist "..\dist-windows-installer-minimal\extras\VBCABLE_Setup_x64.exe"
; VB-Audio setup: -i install, -h silent UI, -H extra silent, -n no reboot prompt (driver/UAC may still appear).
Filename: "{app}\extras\VBCABLE_Setup_x64.exe"; Parameters: "-i -h -H -n"; StatusMsg: "Installing VB-Audio Virtual Cable (silent)..."; Description: "Install VB-Audio Virtual Cable"; Flags: waituntilterminated postinstall skipifdoesntexist; Tasks: vbcable; Check: not IsRunningUnderWine
#endif
Filename: "{app}\{#MyAppExeName}"; WorkingDir: "{app}"; Description: "Launch {#MyAppName}"; Flags: nowait postinstall skipifsilent skipifdoesntexist; Check: not IsRunningUnderWine

[UninstallDelete]
Type: files; Name: "{app}\cpp_gui_launch_log.txt"

[Code]
{ Wine host integration uses Z:\usr\bin\bash (Wine maps Z: to /. Do not use GetProcAddress(ntdll,wine_get_version)
  here: it can access-violate under Wine during ssPostInstall while the C++ app still uses that check at runtime. }
function FindHostSh: String;
begin
  if FileExists('Z:\usr\bin\sh') then
    Result := 'Z:\usr\bin\sh'
  else if FileExists('Z:\bin\sh') then
    Result := 'Z:\bin\sh'
  else
    Result := '';
end;

function FindHostBash: String;
begin
  if FileExists('Z:\usr\bin\bash') then
    Result := 'Z:\usr\bin\bash'
  else if FileExists('Z:\bin\bash') then
    Result := 'Z:\bin\bash'
  else
    Result := '';
end;

function IsRunningUnderWine: Boolean;
begin
  Result := (FindHostBash <> '');
end;

procedure WineRunHostHelper(const HelperBody: String);
var
  BashExe, TmpWin: String;
  ResultCode: Integer;
begin
  BashExe := FindHostBash;
  if BashExe = '' then
    Exit;
  TmpWin := 'Z:\tmp\lv_wine_helper.sh';
  SaveStringToFile(TmpWin, '#!/bin/bash' + #10 + HelperBody + #10, False);
  Exec(BashExe, '-c "chmod +x /tmp/lv_wine_helper.sh && exec bash /tmp/lv_wine_helper.sh"',
       '', SW_HIDE, ewWaitUntilTerminated, ResultCode);
end;

procedure SetupPipeWireVirtualMic;
begin
  WineRunHostHelper(
    'export PATH=/usr/bin:/bin:$PATH' + #10 +
    'if [ -n "$WINE_HOST_XDG_RUNTIME_DIR" ]; then export XDG_RUNTIME_DIR="$WINE_HOST_XDG_RUNTIME_DIR"; fi' + #10 +
    'command -v pactl >/dev/null 2>&1 || exit 0' + #10 +
    'if ! pactl list short sinks 2>/dev/null | grep -q live_vocoder; then' + #10 +
    '  pactl load-module module-null-sink sink_name=live_vocoder rate=48000 channels=2 sink_properties="device.description=LiveVocoder node.description=LiveVocoder media.class=Audio/Sink" 2>/dev/null || true' + #10 +
    '  sleep 0.25' + #10 +
    'fi' + #10 +
    'for _w in 1 2 3 4 5 6 7 8 9 10 11 12 13 14 15 16 17 18 19 20; do' + #10 +
    '  pactl list short sources 2>/dev/null | grep -q live_vocoder.monitor && break' + #10 +
    '  sleep 0.1' + #10 +
    'done' + #10 +
    'if ! pactl list short sources 2>/dev/null | grep -q live_vocoder_mic; then' + #10 +
    '  if pactl list short sources 2>/dev/null | grep -q live_vocoder.monitor; then' + #10 +
    '    for _m in 1 2 3 4 5; do' + #10 +
    '      pactl load-module module-remap-source master=live_vocoder.monitor source_name=live_vocoder_mic source_properties="device.description=LiveVocoderVirtualMic node.description=LiveVocoderVirtualMic media.class=Audio/Source/Virtual device.form-factor=microphone" 2>/dev/null || true' + #10 +
    '      pactl list short sources 2>/dev/null | grep -q live_vocoder_mic && break' + #10 +
    '      sleep 0.15' + #10 +
    '    done' + #10 +
    '  fi' + #10 +
    'fi'
  );
end;

procedure InstallWineLauncherScript;
var
  AppDirWin: String;
begin
  AppDirWin := ExpandConstant('{app}');
  WineRunHostHelper(
    'export PATH=/usr/bin:/bin:$PATH' + #10 +
    'WP="${WINEPREFIX:-$HOME/.wine}"' + #10 +
    'AD=$(WINEPREFIX="$WP" winepath -u "' + AppDirWin + '" 2>/dev/null || true)' + #10 +
    'if [ -z "$AD" ] || [ ! -f "$AD/LiveVocoder.exe" ]; then' + #10 +
    '  AD=""' + #10 +
    '  _u="$(whoami)"' + #10 +
    '  for try in "$WP/drive_c/users/$_u/AppData/Local/Programs/Live Vocoder" "$WP/drive_c/Program Files/Live Vocoder" "$WP/drive_c/Program Files (x86)/Live Vocoder"; do' + #10 +
    '    if [ -f "$try/LiveVocoder.exe" ]; then AD="$try"; break; fi' + #10 +
    '  done' + #10 +
    'fi' + #10 +
    'if [ -z "$AD" ] || [ ! -f "$AD/LiveVocoder.exe" ]; then echo "LiveVocoder: could not resolve Linux path for this Wine prefix (per-user installs use AppData/Local/Programs). Reinstall or run from repo scripts." >&2; exit 1; fi' + #10 +
    'SH="$AD/live-vocoder-wine-launch.sh"' + #10 +
    'DT="$AD/LiveVocoder_Wine.desktop"' + #10 +
    'EXE="$AD/LiveVocoder.exe"' + #10 +
    '' + #10 +
    'cat > "$SH" << ''SHEOF''' + #10 +
    '#!/usr/bin/env bash' + #10 +
    'set -euo pipefail' + #10 +
    'SINK_NAME="live_vocoder"' + #10 +
    'MIC_NAME="${SINK_NAME}_mic"' + #10 +
    'MON_NAME="${SINK_NAME}.monitor"' + #10 +
    'SCRIPT_DIR="$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)"' + #10 +
    'EXE="$SCRIPT_DIR/LiveVocoder.exe"' + #10 +
    'if [ ! -f "$EXE" ]; then echo "LiveVocoder.exe not found: $EXE" >&2; exit 1; fi' + #10 +
    'if command -v pactl >/dev/null 2>&1; then' + #10 +
    '  # Purge all LiveVocoder PipeWire modules (duplicates + canonical), then recreate one stack below.' + #10 +
    '  pactl list modules short 2>/dev/null | awk ''$2 ~ /loopback/ && index($0,"live_vocoder.monitor"){print $1}'' | while read -r id; do [ -n "$id" ] && pactl unload-module "$id" 2>/dev/null || true; done' + #10 +
    '  pactl list modules short 2>/dev/null | awk ''($2=="module-remap-source"||$2=="module-virtual-source")&&(/live_vocoder_mic/||/LiveVocoderVirtualMic/){print $1}'' | while read -r id; do [ -n "$id" ] && pactl unload-module "$id" 2>/dev/null || true; done' + #10 +
    '  pactl list modules short 2>/dev/null | awk ''$2=="module-null-sink"&&(/live_vocoder/||/LiveVocoder/){print $1}'' | while read -r id; do [ -n "$id" ] && pactl unload-module "$id" 2>/dev/null || true; done' + #10 +
    '  sleep 0.12' + #10 +
    '  if ! pactl list short sinks 2>/dev/null | grep -q "$SINK_NAME"; then' + #10 +
    '    pactl load-module module-null-sink sink_name="$SINK_NAME" rate=48000 channels=2 sink_properties="device.description=LiveVocoder node.description=LiveVocoder media.class=Audio/Sink" 2>/dev/null || true' + #10 +
    '    sleep 0.25' + #10 +
    '  fi' + #10 +
    '  for _w2 in 1 2 3 4 5 6 7 8 9 10 11 12 13 14 15 16 17 18 19 20; do' + #10 +
    '    pactl list short sources 2>/dev/null | grep -q "$MON_NAME" && break' + #10 +
    '    sleep 0.1' + #10 +
    '  done' + #10 +
    '  if ! pactl list short sources 2>/dev/null | grep -q "$MIC_NAME"; then' + #10 +
    '    if pactl list short sources 2>/dev/null | grep -q "$MON_NAME"; then' + #10 +
    '      for _m2 in 1 2 3 4 5; do' + #10 +
    '        pactl load-module module-remap-source master="$MON_NAME" source_name="$MIC_NAME" source_properties="device.description=LiveVocoderVirtualMic node.description=LiveVocoderVirtualMic media.class=Audio/Source/Virtual device.form-factor=microphone" 2>/dev/null || true' + #10 +
    '        pactl list short sources 2>/dev/null | grep -q "$MIC_NAME" && break' + #10 +
    '        sleep 0.15' + #10 +
    '      done' + #10 +
    '    fi' + #10 +
    '  fi' + #10 +
    'fi' + #10 +
    '# Pulse wants dotted keys (application.name); bash cannot export those names - pass them with env(1).' + #10 +
    '_LV_APP="${LIVE_VOCODER_PULSE_APP_NAME:-Live Vocoder}"' + #10 +
    '_LV_MEDIA="${LIVE_VOCODER_PULSE_MEDIA_NAME:-Live Vocoder}"' + #10 +
    '_LV_ICON="${LIVE_VOCODER_PULSE_ICON_NAME:-audio-input-microphone}"' + #10 +
    '# Skip SDL welcome / font-info modals under Wine (set LIVE_VOCODER_SDL_SKIP_STARTUP_MODALS=0 to show them).' + #10 +
    'env PULSE_SINK="$SINK_NAME" LIVE_VOCODER_PULSE_SINK="$SINK_NAME" \' + #10 +
    '  WINE_HOST_XDG_RUNTIME_DIR="${XDG_RUNTIME_DIR:-/run/user/$(id -u)}" \' + #10 +
    '  LIVE_VOCODER_SDL_SKIP_STARTUP_MODALS="${LIVE_VOCODER_SDL_SKIP_STARTUP_MODALS:-1}" \' + #10 +
    '  "PULSE_PROP_application.name=$_LV_APP" \' + #10 +
    '  "PULSE_PROP_media.name=$_LV_MEDIA" \' + #10 +
    '  "PULSE_PROP_application.icon_name=$_LV_ICON" \' + #10 +
    '  wine "$EXE" "$@" &' + #10 +
    '_WINE_PID=$!' + #10 +
    '(' + #10 +
    '  sleep 3' + #10 +
    '  for _i in $(seq 1 40); do' + #10 +
    '    _SI=""' + #10 +
    '    _CUR=""' + #10 +
    '    _sid=""' + #10 +
    '    while IFS= read -r _line; do' + #10 +
    '      case "$_line" in' + #10 +
    '        "Sink Input #"*) _sid="${_line#Sink Input #}"; _sid="${_sid%%[!0-9]*}" ;;' + #10 +
    '        *"Sink: "*) [ -n "$_sid" ] && _CUR="${_line##*Sink: }" ;;' + #10 +
    '        *"application.name ="*)' + #10 +
    '          if [ -n "$_sid" ]; then' + #10 +
    '            _nl="${_line,,}"' + #10 +
    '            if [[ "$_nl" == *"livevocoder"* || "$_nl" == *"live vocoder"* || "$_nl" == *"portaudio"* || "$_nl" == *"[audio stream"* ]]; then' + #10 +
    '              _SI="$_sid"' + #10 +
    '              break' + #10 +
    '            fi' + #10 +
    '          fi' + #10 +
    '          ;;' + #10 +
    '      esac' + #10 +
    '    done < <(pactl list sink-inputs 2>/dev/null)' + #10 +
    '    if [ -n "$_SI" ]; then' + #10 +
    '      _TGT=""' + #10 +
    '      while IFS=$''\t'' read -r _id _nm _rest; do' + #10 +
    '        [ "$_nm" = "$SINK_NAME" ] && _TGT="$_id" && break' + #10 +
    '      done < <(pactl list short sinks 2>/dev/null)' + #10 +
    '      if [ -n "$_TGT" ] && [ "$_CUR" != "$_TGT" ]; then' + #10 +
    '        pactl move-sink-input "$_SI" "$SINK_NAME" 2>/dev/null && break' + #10 +
    '      else' + #10 +
    '        break' + #10 +
    '      fi' + #10 +
    '    fi' + #10 +
    '    sleep 0.3' + #10 +
    '  done' + #10 +
    ') &' + #10 +
    '_MOVER_PID=$!' + #10 +
    'wait "$_WINE_PID" 2>/dev/null || true' + #10 +
    'if [ -n "${_MOVER_PID:-}" ]; then kill "$_MOVER_PID" 2>/dev/null || true; wait "$_MOVER_PID" 2>/dev/null || true; fi' + #10 +
    'SHEOF' + #10 +
    'chmod +x "$SH"' + #10 +
    '' + #10 +
    'cat > "$DT" << DTEOF' + #10 +
    '[Desktop Entry]' + #10 +
    'Version=1.0' + #10 +
    'Type=Application' + #10 +
    'Name=Live Vocoder' + #10 +
    'Comment=Real-time vocoder (Wine + PipeWire virtual mic)' + #10 +
    'Exec=' + ExpandConstant('{#SetupEmbeddedBashPrefix}') + ' "' + '$SH' + '" %f' + #10 +
    'Icon=audio-input-microphone' + #10 +
    'Categories=AudioVideo;Audio;Mixer;' + #10 +
    'MimeType=audio/x-wav;audio/mpeg;audio/flac;audio/ogg;' + #10 +
    'DTEOF' + #10 +
    'chmod +x "$DT"' + #10 +
    'mkdir -p ~/.local/share/applications' + #10 +
    'cp -f "$DT" ~/.local/share/applications/LiveVocoder_Wine.desktop'
  );
end;

procedure WineLaunchApp;
var
  BashExe: String;
  ResultCode: Integer;
  Entry: String;
  AppDirWin: String;
begin
  BashExe := FindHostBash;
  if BashExe = '' then
    Exit;
  AppDirWin := ExpandConstant('{app}');
  Entry := '#!/bin/bash' + #10 +
    'export PATH=/usr/bin:/bin:$PATH' + #10 +
    'WP="${WINEPREFIX:-$HOME/.wine}"' + #10 +
    'AD=$(WINEPREFIX="$WP" winepath -u "' + AppDirWin + '" 2>/dev/null || true)' + #10 +
    'if [ -z "$AD" ] || [ ! -f "$AD/LiveVocoder.exe" ]; then' + #10 +
    '  AD=""' + #10 +
    '  _u="$(whoami)"' + #10 +
    '  for try in "$WP/drive_c/users/$_u/AppData/Local/Programs/Live Vocoder" "$WP/drive_c/Program Files/Live Vocoder" "$WP/drive_c/Program Files (x86)/Live Vocoder"; do' + #10 +
    '    if [ -f "$try/LiveVocoder.exe" ]; then AD="$try"; break; fi' + #10 +
    '  done' + #10 +
    'fi' + #10 +
    'if [ -z "$AD" ] || [ ! -f "$AD/live-vocoder-wine-launch.sh" ]; then echo "LiveVocoder: host launch script missing; rerun the Wine installer (non-silent) once." >&2; exit 1; fi' + #10 +
    'exec ' + ExpandConstant('{#SetupEmbeddedBashPrefix}') + ' "$AD/live-vocoder-wine-launch.sh"' + #10;
  SaveStringToFile('Z:\tmp\lv_wine_launch_entry.sh', Entry, False);
  Exec(BashExe, '-c "chmod +x /tmp/lv_wine_launch_entry.sh && exec bash /tmp/lv_wine_launch_entry.sh"',
       '', SW_HIDE, ewNoWait, ResultCode);
end;

procedure CurStepChanged(CurStep: TSetupStep);
begin
  if CurStep = ssPostInstall then
  begin
    if IsRunningUnderWine then
    begin
      SetupPipeWireVirtualMic;
      InstallWineLauncherScript;
    end;
  end;
end;

procedure CurPageChanged(CurPageID: Integer);
begin
  { Touching WizardForm during /SILENT or /VERYSILENT can surface the wizard on Wine. }
  if CurPageID = wpFinished then
  begin
    if IsRunningUnderWine and (not WizardSilent) then
      WizardForm.RunList.Visible := False;
  end;
end;

function ShouldSkipPage(PageID: Integer): Boolean;
begin
  { Silent installs should exit without a Finish page (otherwise automation hangs under Wine). }
  Result := (PageID = wpFinished) and WizardSilent;
end;

function NextButtonClick(CurPageID: Integer): Boolean;
begin
  Result := True;
  if CurPageID = wpFinished then
  begin
    { Do not auto-launch when unattended; same flags hide the Finish page via ShouldSkipPage. }
    if IsRunningUnderWine and (not WizardSilent) then
      WineLaunchApp;
  end;
end;
