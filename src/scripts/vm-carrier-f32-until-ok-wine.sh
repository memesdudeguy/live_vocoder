#!/usr/bin/env bash
# Retry LiveVocoder --carrier-pipeline-test under Wine until it succeeds (same PE build as the Windows VM).
#
# Usage (from repo root):
#   ./scripts/vm-carrier-f32-until-ok-wine.sh
#   LV_PORTABLE=cpp/dist-windows-cross LV_MAX_TRIES=50 ./scripts/vm-carrier-f32-until-ok-wine.sh
#
# In a Windows QEMU VM (SMB \\10.0.2.4\qemu): open CMD in the shared folder and run
#   Test-CarrierF32-VM-UntilOk.bat
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
PORTABLE="${LV_PORTABLE:-$ROOT/cpp/dist-windows-cross}"
EXE="$PORTABLE/LiveVocoder.exe"
MAX="${LV_MAX_TRIES:-0}"
SLEEP="${LV_RETRY_SEC:-15}"
WAV="${LV_TEST_WAV:-}"

if [[ ! -f "$EXE" ]]; then
  echo "vm-carrier-f32-until-ok-wine: missing $EXE (build cross first: cpp/build-cross-windows.sh)" >&2
  exit 1
fi
if ! command -v wine >/dev/null 2>&1; then
  echo "vm-carrier-f32-until-ok-wine: wine not found" >&2
  exit 1
fi

if [[ -z "$WAV" ]]; then
  WAV="$(mktemp --suffix=.wav 2>/dev/null || mktemp /tmp/lvoc_test_XXXXXX.wav)"
  cleanup() { rm -f "$WAV" 2>/dev/null || true; }
  trap cleanup EXIT
  if command -v ffmpeg >/dev/null 2>&1; then
    ffmpeg -y -hide_banner -loglevel error -f lavfi -i sine=frequency=440:duration=1 -ac 1 -ar 48000 "$WAV"
  else
    echo "vm-carrier-f32-until-ok-wine: set LV_TEST_WAV to a .wav/.mp3 or install host ffmpeg to synthesize a tone" >&2
    exit 1
  fi
fi

[[ -f "$WAV" ]] || { echo "test audio not found: $WAV" >&2; exit 1; }

n=0
while true; do
  n=$((n + 1))
  echo "[$n] WINEDEBUG=-all wine $EXE --carrier-pipeline-test $(basename "$WAV") 48000" >&2
  if (cd "$PORTABLE" && WINEDEBUG=-all wine "$EXE" --carrier-pipeline-test "$WAV" 48000); then
    echo "vm-carrier-f32-until-ok-wine: SUCCESS after $n attempt(s)" >&2
    exit 0
  fi
  if [[ "$MAX" != "0" && "$n" -ge "$MAX" ]]; then
    echo "vm-carrier-f32-until-ok-wine: giving up after $MAX attempts" >&2
    exit 1
  fi
  echo "vm-carrier-f32-until-ok-wine: retry in ${SLEEP}s (Ctrl+C to stop)" >&2
  sleep "$SLEEP"
done
