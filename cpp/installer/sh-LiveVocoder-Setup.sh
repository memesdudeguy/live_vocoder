#!/bin/sh
set -eu
# Embedded prefix after POSIX sh is active (must match LiveVocoderCppMinimal.iss SetupEmbeddedShPrefix): default /bin/sh
_EMBED_SH_PREFIX="${LIVE_VOCODER_EMBED_SH:-/bin/sh}"
# POSIX sh — run the minimal Inno installer under Wine. Keep this script next to the .exe.
# Picks (in order): LiveVocoder-Setup-Wine.exe, LiveVocoder-Setup-Windows.exe, LiveVocoder-Setup.exe
# Desktop Exec= should use that prefix, e.g. /bin/sh "/path/to/sh-LiveVocoder-Setup.sh"
# Override: LIVE_VOCODER_EMBED_SH=/usr/bin/sh
# Usage: ${_EMBED_SH_PREFIX} ./sh-LiveVocoder-Setup.sh   or   ./sh-LiveVocoder-Setup.sh (shebang)
DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
SETUP=""
for _cand in "$DIR/LiveVocoder-Setup-Wine.exe" "$DIR/LiveVocoder-Setup-Windows.exe" "$DIR/LiveVocoder-Setup.exe"; do
  if [ -f "$_cand" ]; then
    SETUP="$_cand"
    break
  fi
done
if [ -z "$SETUP" ]; then
  echo "Missing installer .exe next to this script (expected LiveVocoder-Setup-Wine.exe or LiveVocoder-Setup-Windows.exe)." >&2
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
# Inno cannot replace LiveVocoder.exe while it is still running (Windows DeleteFile error 5 → install aborts).
if command -v wine >/dev/null 2>&1; then
  wine taskkill /IM LiveVocoder.exe /F 2>/dev/null || true
  sleep 0.5
fi
# Unattended (CI / scripts): avoids privilege dialog and Finish page hang under Wine.
# Requires installer built with PrivilegesRequiredOverridesAllowed=commandline.
if [ "${LIVE_VOCODER_SETUP_SILENT:-0}" = 1 ]; then
  exec wine "$SETUP" /VERYSILENT /SUPPRESSMSGBOXES /CURRENTUSER /NORESTART /CLOSEAPPLICATIONS "$@"
fi
exec wine "$SETUP" "$@"
