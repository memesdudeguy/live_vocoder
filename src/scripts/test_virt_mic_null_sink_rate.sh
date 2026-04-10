#!/usr/bin/env bash
# Verify LiveVocoder null sink runs at 48 kHz (matches app). Wrong rate → muffled / metallic *_mic audio.
set -euo pipefail
SN="${1:-live_vocoder2}"
if ! command -v pactl >/dev/null 2>&1; then
  echo "SKIP: pactl not in PATH"
  exit 0
fi
line=$(pactl list short sinks 2>/dev/null | awk -F'\t' -v n="$SN" '$2==n {print; exit}')
if [[ -z "$line" ]]; then
  echo "SKIP: no sink named $SN (create with LiveVocoder or: pactl load-module module-null-sink sink_name=$SN rate=48000 channels=2 …)"
  exit 0
fi
if echo "$line" | grep -qE '48000|48\.0 kHz'; then
  echo "PASS: sink $SN is 48 kHz (short list: ${line:0:120}...)"
  exit 0
fi
echo "FAIL: sink $SN should be 48000 Hz to match LiveVocoder. Short list line:"
echo "$line"
echo "Fix: unload the old null-sink module, then restart the app so it recreates rate=48000 channels=2."
exit 1
