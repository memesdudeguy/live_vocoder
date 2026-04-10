#!/usr/bin/env bash
# Reference copy: Inno Setup embeds the same logic in LiveVocoderCppMinimal.iss (InstallWineLauncherScript).
# Install path:  <Wine prefix>/drive_c/Program Files/Live Vocoder/live-vocoder-wine-launch.sh
set -euo pipefail
SINK_NAME="live_vocoder"
MIC_NAME="${SINK_NAME}_mic"
MON_NAME="${SINK_NAME}.monitor"
SCRIPT_DIR="$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)"
EXE="$SCRIPT_DIR/LiveVocoder.exe"
if [ ! -f "$EXE" ]; then echo "LiveVocoder.exe not found: $EXE" >&2; exit 1; fi
if command -v pactl >/dev/null 2>&1; then
  pactl list modules short 2>/dev/null | awk '$2 ~ /loopback/ && index($0,"live_vocoder.monitor"){print $1}' | while read -r id; do [ -n "$id" ] && pactl unload-module "$id" 2>/dev/null || true; done
  pactl list modules short 2>/dev/null | awk '($2=="module-remap-source"||$2=="module-virtual-source")&&(/live_vocoder_mic/||/LiveVocoderVirtualMic/){print $1}' | while read -r id; do [ -n "$id" ] && pactl unload-module "$id" 2>/dev/null || true; done
  pactl list modules short 2>/dev/null | awk '$2=="module-null-sink"&&(/live_vocoder/||/LiveVocoder/){print $1}' | while read -r id; do [ -n "$id" ] && pactl unload-module "$id" 2>/dev/null || true; done
  sleep 0.12
  if ! pactl list short sinks 2>/dev/null | grep -q "$SINK_NAME"; then
    pactl load-module module-null-sink sink_name="$SINK_NAME" rate=48000 channels=2 sink_properties="device.description=LiveVocoder node.description=LiveVocoder media.class=Audio/Sink" 2>/dev/null || true
    sleep 0.25
  fi
  for _w2 in 1 2 3 4 5 6 7 8 9 10 11 12 13 14 15 16 17 18 19 20; do
    pactl list short sources 2>/dev/null | grep -q "$MON_NAME" && break
    sleep 0.1
  done
  if ! pactl list short sources 2>/dev/null | grep -q "$MIC_NAME"; then
    if pactl list short sources 2>/dev/null | grep -q "$MON_NAME"; then
      for _m2 in 1 2 3 4 5; do
        pactl load-module module-remap-source master="$MON_NAME" source_name="$MIC_NAME" source_properties="device.description=LiveVocoderVirtualMic node.description=LiveVocoderVirtualMic media.class=Audio/Source/Virtual device.form-factor=microphone" 2>/dev/null || true
        pactl list short sources 2>/dev/null | grep -q "$MIC_NAME" && break
        sleep 0.15
      done
    fi
  fi
fi
# Pulse dotted keys cannot be bash export names - pass via env(1) only (never export PULSE_PROP_* with dots).
_LV_APP="${LIVE_VOCODER_PULSE_APP_NAME:-Live Vocoder}"
_LV_MEDIA="${LIVE_VOCODER_PULSE_MEDIA_NAME:-Live Vocoder}"
_LV_ICON="${LIVE_VOCODER_PULSE_ICON_NAME:-audio-input-microphone}"
env PULSE_SINK="$SINK_NAME" LIVE_VOCODER_PULSE_SINK="$SINK_NAME" \
  WINE_HOST_XDG_RUNTIME_DIR="${XDG_RUNTIME_DIR:-/run/user/$(id -u)}" \
  LIVE_VOCODER_SDL_SKIP_STARTUP_MODALS="${LIVE_VOCODER_SDL_SKIP_STARTUP_MODALS:-1}" \
  "PULSE_PROP_application.name=$_LV_APP" \
  "PULSE_PROP_media.name=$_LV_MEDIA" \
  "PULSE_PROP_application.icon_name=$_LV_ICON" \
  wine "$EXE" "$@" &
_WINE_PID=$!
(
  sleep 3
  for _i in $(seq 1 40); do
    _SI=""
    _CUR=""
    _sid=""
    while IFS= read -r _line; do
      case "$_line" in
        "Sink Input #"*) _sid="${_line#Sink Input #}"; _sid="${_sid%%[!0-9]*}" ;;
        *"Sink: "*) [ -n "${_sid:-}" ] && _CUR="${_line##*Sink: }" ;;
        *"application.name ="*)
          if [ -n "${_sid:-}" ]; then
            _nl="${_line,,}"
            if [[ "$_nl" == *"livevocoder"* || "$_nl" == *"live vocoder"* ]]; then
              _SI="$_sid"
              break
            fi
          fi
          ;;
      esac
    done < <(pactl list sink-inputs 2>/dev/null)
    if [ -n "$_SI" ]; then
      _TGT=""
      while IFS=$'\t' read -r _id _nm _rest; do
        if [ "$_nm" = "$SINK_NAME" ]; then _TGT="$_id"; break; fi
      done < <(pactl list short sinks 2>/dev/null)
      if [ -n "$_TGT" ] && [ "$_CUR" != "$_TGT" ]; then
        pactl move-sink-input "$_SI" "$SINK_NAME" 2>/dev/null && break
      else
        break
      fi
    fi
    sleep 0.3
  done
) &
_MOVER_PID=$!
wait "$_WINE_PID" 2>/dev/null || true
if [ -n "${_MOVER_PID:-}" ]; then kill "$_MOVER_PID" 2>/dev/null || true; wait "$_MOVER_PID" 2>/dev/null || true; fi
