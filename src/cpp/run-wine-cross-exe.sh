#!/usr/bin/env bash
# Run cross-built LiveVocoder.exe under Wine. Args are passed to the .exe.
# Uses wine64 when available, otherwise wine (Arch often has only "wine" — that is correct).
# Non-.f32 carriers need ffmpeg.exe beside LiveVocoder.exe (see build-cross-windows.sh / README.txt).
# Example:  ./run-wine-cross-exe.sh --minimal-cpp Z:/home/you/song.f32
set -euo pipefail
ROOT="$(cd "$(dirname "$0")" && pwd)"
DIR="$ROOT/dist-windows-cross"

EXE="$DIR/LiveVocoder.exe"
if [[ ! -f "$EXE" ]]; then
  echo "Missing $EXE — build and bundle first." >&2
  exit 1
fi
cd "$DIR"
_pulse_env() {
  exec env \
    "PULSE_PROP_application.name=${LIVE_VOCODER_PULSE_APP_NAME:-Live Vocoder}" \
    "PULSE_PROP_media.name=${LIVE_VOCODER_PULSE_MEDIA_NAME:-Live Vocoder}" \
    "PULSE_PROP_application.icon_name=${LIVE_VOCODER_PULSE_ICON_NAME:-audio-input-microphone}" \
    "$@"
}
if command -v wine64 >/dev/null 2>&1; then
  _pulse_env wine64 "./LiveVocoder.exe" "$@"
fi
_pulse_env wine "./LiveVocoder.exe" "$@"
