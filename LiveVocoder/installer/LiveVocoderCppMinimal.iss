; Inno Setup 6 — C++ SDL only (from LiveVocoder/). Stage: ../bundle-installer-minimal.sh
; Compile (from LiveVocoder/): ./build-installer-minimal.sh  |  build-installer-minimal.bat
; Output: ..\dist-installer\LiveVocoder_Cpp_Setup_<version>.exe
; https://jrsoftware.org/isinfo.php

#define MyAppName "Live Vocoder"
#define MyAppVersion "0.3.0"
#define MyAppPublisher "live_vocoder"
#define MyAppExeName "LiveVocoder.exe"
; Linux: embedded shell paths (not bare "sh"/"bash" from PATH) for .desktop and host helpers.
#define SetupEmbeddedShPrefix "/bin/sh"
#define SetupEmbeddedBashPrefix "/bin/bash"
#define MinimalRoot "..\dist-windows-installer-minimal"

[Setup]
AppId=com.live_vocoder.LiveVocoder.cpp.sdl.x64
AppName={#MyAppName}
AppVersion={#MyAppVersion}
AppPublisher={#MyAppPublisher}
AppVerName={#MyAppName} {#MyAppVersion} (C++)
AppCopyright=Copyright (C) {#MyAppPublisher}
DefaultDirName={autopf}\{#MyAppName}
DefaultGroupName={#MyAppName}
OutputDir=..\dist-installer
OutputBaseFilename=LiveVocoder-Setup
SetupIconFile=LiveVocoder.ico
UninstallDisplayIcon={app}\LiveVocoder.ico
Compression=lzma2/ultra64
SolidCompression=yes
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
DisableProgramGroupPage=yes
PrivilegesRequiredOverridesAllowed=dialog
UsedUserAreasWarning=no
WizardStyle=modern
; Replace in-use LiveVocoder.exe on upgrade (otherwise DeleteFile code 5 → abort, especially with /SUPPRESSMSGBOXES).
CloseApplications=yes
RestartApplications=no

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"

[Tasks]
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"; Flags: unchecked

[Files]
Source: "LiveVocoder.ico"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#MinimalRoot}\LiveVocoder.exe"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#MinimalRoot}\*.dll"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#MinimalRoot}\app-icon.png"; DestDir: "{app}"; Flags: ignoreversion skipifsourcedoesntexist
Source: "{#MinimalRoot}\fonts\DejaVuSans.ttf"; DestDir: "{app}\fonts"; Flags: ignoreversion skipifsourcedoesntexist
Source: "{#MinimalRoot}\ffmpeg.exe"; DestDir: "{app}"; Flags: ignoreversion skipifsourcedoesntexist
Source: "{#MinimalRoot}\README_Cpp_Minimal.txt"; DestDir: "{app}"; DestName: "README.txt"; Flags: ignoreversion skipifsourcedoesntexist
; Wine/Linux host: invoke with {#SetupEmbeddedShPrefix} (not PATH "sh") — see script header.
Source: "sh-LiveVocoder-Setup.sh"; DestDir: "{app}"; Flags: ignoreversion skipifsourcedoesntexist

[Registry]
Root: HKCU; Subkey: "Environment"; ValueType: string; ValueName: "LIVE_VOCODER_PULSE_SINK"; ValueData: "live_vocoder"; Flags: uninsdeletevalue createvalueifdoesntexist
Root: HKCU; Subkey: "Environment"; ValueType: string; ValueName: "PULSE_SINK"; ValueData: "live_vocoder"; Flags: uninsdeletevalue createvalueifdoesntexist

[Icons]
Name: "{group}\{#MyAppName}"; Filename: "{app}\{#MyAppExeName}"; WorkingDir: "{app}"; IconFilename: "{app}\LiveVocoder.ico"; Comment: "{#MyAppName} (SDL2)"
Name: "{group}\{cm:UninstallProgram,{#MyAppName}}"; Filename: "{uninstallexe}"
Name: "{commondesktop}\{#MyAppName}"; Filename: "{app}\{#MyAppExeName}"; WorkingDir: "{app}"; IconFilename: "{app}\LiveVocoder.ico"; Tasks: desktopicon

[Run]
Filename: "{app}\{#MyAppExeName}"; WorkingDir: "{app}"; Description: "Launch {#MyAppName}"; Flags: nowait postinstall skipifsilent skipifdoesntexist; Check: not IsRunningUnderWine

[UninstallDelete]
Type: files; Name: "{app}\cpp_gui_launch_log.txt"

[Code]

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

function FindHostShell: String;
begin
  Result := FindHostSh;
  if Result = '' then
    Result := FindHostBash;
end;

function IsRunningUnderWine: Boolean;
begin
  Result := (FindHostShell <> '');
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
begin
  WineRunHostHelper(
    'WP="${WINEPREFIX:-$HOME/.wine}"' + #10 +
    'AD="$WP/drive_c/Program Files/Live Vocoder"' + #10 +
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
    'WP="${WINEPREFIX:-$HOME/.wine}"' + #10 +
    'EXE="$WP/drive_c/Program Files/Live Vocoder/LiveVocoder.exe"' + #10 +
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
    'export PULSE_SINK="$SINK_NAME"' + #10 +
    'export LIVE_VOCODER_PULSE_SINK="$SINK_NAME"' + #10 +
    'export WINE_HOST_XDG_RUNTIME_DIR="${XDG_RUNTIME_DIR:-/run/user/$(id -u)}"' + #10 +
    'wine "$EXE" "$@" &' + #10 +
    '_WINE_PID=$!' + #10 +
    '(' + #10 +
    '  sleep 3' + #10 +
    '  for _i in $(seq 1 40); do' + #10 +
    '    _SI="" _CUR="" _sid=""' + #10 +
    '    while IFS= read -r _line; do' + #10 +
    '      case "$_line" in' + #10 +
    '        "Sink Input #"*) _sid="${_line#Sink Input #}"; _sid="${_sid%%[!0-9]*}" ;;' + #10 +
    '        *"Sink: "*) [ -n "$_sid" ] && _CUR="${_line##*Sink: }" ;;' + #10 +
    '        *"application.name ="*)' + #10 +
    '          if [ -n "$_sid" ]; then' + #10 +
    '            _nl="${_line,,}"' + #10 +
    '            if [[ "$_nl" == *"livevocoder"* || "$_nl" == *"live vocoder"* ]]; then' + #10 +
    '              _SI="$_sid"' + #10 +
    '              break' + #10 +
    '            fi' + #10 +
    '          fi' + #10 +
    '          ;;' + #10 +
    '      esac' + #10 +
    '    done < <(pactl list sink-inputs 2>/dev/null)' + #10 +
    '    if [ -n "$_SI" ]; then' + #10 +
    '      _TGT=$(pactl list short sinks 2>/dev/null | while IFS="$(printf ''\t'')" read -r _id _nm _rest; do' + #10 +
    '        [ "$_nm" = "$SINK_NAME" ] && echo "$_id" && break; done)' + #10 +
    '      if [ -n "$_TGT" ] && [ "$_CUR" != "$_TGT" ]; then' + #10 +
    '        pactl move-sink-input "$_SI" "$SINK_NAME" 2>/dev/null && break' + #10 +
    '      else break; fi' + #10 +
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
begin
  BashExe := FindHostBash;
  if BashExe = '' then
    Exit;
  Exec(BashExe, '-c "WP=${WINEPREFIX:-$HOME/.wine}; exec ' + ExpandConstant('{#SetupEmbeddedBashPrefix}') +
       ' \"$WP/drive_c/Program Files/Live Vocoder/live-vocoder-wine-launch.sh\""',
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
  if CurPageID = wpFinished then
  begin
    if IsRunningUnderWine then
      WizardForm.RunList.Visible := False;
  end;
end;

function ShouldSkipPage(PageID: Integer): Boolean;
begin
  Result := False;
end;

function NextButtonClick(CurPageID: Integer): Boolean;
begin
  Result := True;
  if CurPageID = wpFinished then
  begin
    if IsRunningUnderWine then
      WineLaunchApp;
  end;
end;
