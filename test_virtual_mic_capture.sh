#!/usr/bin/env bash
# Quick check: default mic vs Live Vocoder, playback into null sink, and capture from live_vocoder_mic.
# Run while Live Vocoder shows **Streaming…** (Start + carrier loaded).
set -euo pipefail
MIC="${1:-live_vocoder_mic}"
SCRIPT_DIR=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
cd "$SCRIPT_DIR"
PYTHON=python3
if [[ -x "$SCRIPT_DIR/.venv/bin/python" ]]; then
  PYTHON="$SCRIPT_DIR/.venv/bin/python"
fi

echo "=== Live Vocoder virtual mic capture test ==="
echo "Default recording source: $(pactl get-default-source 2>/dev/null || echo '(pactl failed)')"
echo ""
if ! pactl list short sources | grep -qF "$MIC"; then
  echo "FAIL: no source matching '$MIC' in \`pactl list short sources\`."
  echo "Open the GTK app with Virtual mic ON first."
  exit 1
fi
echo "OK: source '$MIC' exists."
echo ""

echo "--- Playback into null sink (any app) ---"
$PYTHON -c "from pulse_virtmic import format_sink_inputs_on_virt_sink_report; print(format_sink_inputs_on_virt_sink_report())"
echo ""

if ! pactl get-default-source 2>/dev/null | grep -qi live_vocoder; then
  echo "NOTE: default source is NOT Live Vocoder — Discord/KDE 'default' still uses your headset."
  echo "      Pick **LiveVocoder** / **${MIC}** explicitly in Discord or System Settings → Sound → Input."
  echo ""
fi

if ! command -v parec >/dev/null 2>&1; then
  echo "Install pulseaudio-utils (parec) for the level sample, or watch Input level in pavucontrol."
  exit 0
fi

REC_ERR=$(mktemp)
trap 'rm -f "$REC_ERR"' EXIT

echo "Recording 2s from '$MIC' (mono s16le 48k)…"
set +e
timeout 2 parec -d "$MIC" --format=s16le --rate=48000 --channels=1 >"/tmp/lv_mic_test.$$" 2>"$REC_ERR"
PEC=$?
set -e
N=$(wc -c <"/tmp/lv_mic_test.$$" || echo 0)
rm -f "/tmp/lv_mic_test.$$"

if [[ "$N" -lt 1000 ]]; then
  echo "Bytes captured: $N (parec exit $PEC)"
  if [[ -s "$REC_ERR" ]]; then
    echo "parec stderr:"
    sed 's/^/  /' "$REC_ERR"
  fi
  echo ""
  echo "Trying stereo capture (some PipeWire nodes are 2ch)…"
  set +e
  timeout 2 parec -d "$MIC" --format=s16le --rate=48000 --channels=2 >"/tmp/lv_mic_test2.$$" 2>"$REC_ERR"
  N2=$(wc -c <"/tmp/lv_mic_test2.$$" || echo 0)
  rm -f "/tmp/lv_mic_test2.$$"
  set -e
  echo "Bytes captured (2ch): $N2"
  if [[ "$N2" -ge 1000 ]]; then
    echo "OK: audio present (use 2ch device in your app if needed)."
    exit 0
  fi
  echo ""
  echo "FAIL: no PCM from '$MIC'."
  echo "Most often: nothing is playing into sink **live_vocoder** (see block above), or the app is not **Started**."
  echo ""
  echo "PortAudio index (use project venv — NOT system python):"
  echo "  cd \"$SCRIPT_DIR\" && ./list_sounddevices.sh"
  echo "Find an OUTPUT device whose name contains **pulse** or **pipewire**, note its index [N], then:"
  echo "  export LIVE_VOCODER_PORTAUDIO_OUTPUT=N"
  echo "  ./run.sh --gtk-gui"
  echo "(Replace N with an index from YOUR ./list_sounddevices.sh — often pulse=7 or pipewire=6 on PipeWire.)"
  exit 1
fi

echo "Bytes captured: $N"
echo "OK: audio is present on the virtual mic source."
