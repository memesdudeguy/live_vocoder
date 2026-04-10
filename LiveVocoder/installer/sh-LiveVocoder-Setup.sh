#!/bin/sh
set -eu
# Embedded prefix after POSIX sh is active (must match LiveVocoderCppMinimal.iss SetupEmbeddedShPrefix): default /bin/sh
_EMBED_SH_PREFIX="${LIVE_VOCODER_EMBED_SH:-/bin/sh}"
# POSIX sh — run LiveVocoder-Setup.exe under Wine. Keep this script next to LiveVocoder-Setup.exe.
# Desktop Exec= should use that prefix, e.g. /bin/sh "/path/to/sh-LiveVocoder-Setup.sh"
# Override: LIVE_VOCODER_EMBED_SH=/usr/bin/sh
# Usage: ${_EMBED_SH_PREFIX} ./sh-LiveVocoder-Setup.sh   or   ./sh-LiveVocoder-Setup.sh (shebang)
DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
SETUP="$DIR/LiveVocoder-Setup.exe"
if [ ! -f "$SETUP" ]; then
  echo "Missing: $SETUP" >&2
  echo "Place sh-LiveVocoder-Setup.sh in the same folder as LiveVocoder-Setup.exe." >&2
  echo "Run with embedded sh prefix: ${_EMBED_SH_PREFIX} \"$DIR/sh-LiveVocoder-Setup.sh\"" >&2
  exit 1
fi
export WINEPREFIX="${WINEPREFIX:-$HOME/.wine}"
# Host PipeWire/Pulse: tear down LiveVocoder virtual stack (loopbacks → mic → sinks) so Wine/Setup does not
# inherit duplicate devices or a stale live_vocoder.monitor → speakers route.
if command -v pactl >/dev/null 2>&1; then
  PATH="/usr/bin:/bin:${PATH}"
  export PATH
  pactl list modules short 2>/dev/null | awk '$2 ~ /loopback/ && index($0,"live_vocoder.monitor"){print $1}' | while read -r id; do
    if [ -n "$id" ]; then
      pactl unload-module "$id" 2>/dev/null || true
    fi
  done || true
  pactl list modules short 2>/dev/null | awk '($2=="module-remap-source"||$2=="module-virtual-source")&&(/live_vocoder_mic/||/LiveVocoderVirtualMic/){print $1}' | while read -r id; do
    if [ -n "$id" ]; then
      pactl unload-module "$id" 2>/dev/null || true
    fi
  done || true
  pactl list modules short 2>/dev/null | awk '$2=="module-null-sink"&&(/live_vocoder/||/LiveVocoder/){print $1}' | while read -r id; do
    if [ -n "$id" ]; then
      pactl unload-module "$id" 2>/dev/null || true
    fi
  done || true
fi
# Headless example: .../sh-LiveVocoder-Setup.sh /VERYSILENT /NORESTART /SUPPRESSMSGBOXES /CURRENTUSER /NOCLOSEAPPLICATIONS
# Inno cannot replace LiveVocoder.exe while it is still running (Windows DeleteFile error 5 → install aborts).
if command -v wine >/dev/null 2>&1; then
  wine taskkill /IM LiveVocoder.exe /F 2>/dev/null || true
  sleep 0.5
fi
exec wine "$SETUP" "$@"
