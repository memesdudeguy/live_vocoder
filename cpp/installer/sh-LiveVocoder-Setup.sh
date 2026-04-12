#!/bin/sh
set -eu
# Embedded prefix after POSIX sh is active (must match LiveVocoderCppMinimal.iss SetupEmbeddedShPrefix): default /bin/sh
_EMBED_SH_PREFIX="${LIVE_VOCODER_EMBED_SH:-/bin/sh}"
# POSIX sh — run the minimal Inno installer under Wine. Keep this script next to the .exe.
# Picks (in order): LiveVocoder-Setup.exe, then legacy -Wine/-Windows names if present
# Desktop Exec= should use that prefix, e.g. /bin/sh "/path/to/sh-LiveVocoder-Setup.sh"
# Override: LIVE_VOCODER_EMBED_SH=/usr/bin/sh
# Wine prefix: defaults to ~/.wine-livevocoder (avoids a broken ~/.wine → kernel32 c0000135).
# Override: LIVE_VOCODER_WINEPREFIX=$HOME/.wine  or  export WINEPREFIX=... before running.
# Usage: ${_EMBED_SH_PREFIX} ./sh-LiveVocoder-Setup.sh   or   ./sh-LiveVocoder-Setup.sh (shebang)
DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
SETUP=""
for _cand in "$DIR/LiveVocoder-Setup.exe" "$DIR/LiveVocoder-Setup-Wine.exe" "$DIR/LiveVocoder-Setup-Windows.exe"; do
  if [ -f "$_cand" ]; then
    SETUP="$_cand"
    break
  fi
done
if [ -z "$SETUP" ]; then
  echo "Missing installer .exe next to this script (expected LiveVocoder-Setup.exe)." >&2
  echo "Run with embedded sh prefix: ${_EMBED_SH_PREFIX} \"$DIR/sh-LiveVocoder-Setup.sh\"" >&2
  exit 1
fi
# Dedicated default prefix so a corrupt global ~/.wine does not block setup (c0000135 / kernel32.dll).
# Resolve before host checks so wine probes use this prefix, not a broken ~/.wine.
if [ -n "${LIVE_VOCODER_WINEPREFIX-}" ]; then
  export WINEPREFIX="$LIVE_VOCODER_WINEPREFIX"
elif [ -n "${WINEPREFIX-}" ]; then
  export WINEPREFIX
else
  export WINEPREFIX="${HOME}/.wine-livevocoder"
fi
# Create prefix automatically (equivalent to: WINEARCH=win64 wineboot -u).
if [ ! -f "$WINEPREFIX/system.reg" ]; then
  echo "Live Vocoder: initializing Wine prefix at $WINEPREFIX ..." >&2
  mkdir -p "$WINEPREFIX"
  WINEARCH="${WINEARCH:-win64}"
  export WINEARCH
  wineboot -u >/dev/null 2>&1 || wineboot -u
fi
# Debian/Ubuntu: wine32 + multiarch — without it, wine often fails with kernel32.dll c0000135.
_CHECK="$DIR/check-wine-livevocoder-host.sh"
if [ -f "$_CHECK" ]; then
  /bin/sh "$_CHECK" || exit $?
else
  if command -v dpkg >/dev/null 2>&1 && command -v apt-get >/dev/null 2>&1; then
    if ! dpkg -l 2>/dev/null | grep -qE '^ii\s+wine32(:i386)?\s'; then
      echo "Wine: wine32 may be missing (common cause of kernel32.dll / c0000135)." >&2
      echo "  sudo dpkg --add-architecture i386 && sudo apt-get update" >&2
      echo "  sudo apt-get install -y wine wine64 wine32" >&2
      echo "  rm -rf \"\$WINEPREFIX\" && WINEARCH=win64 wineboot -u   # or rm -rf ~/.wine" >&2
    fi
  fi
  if ! WINEDEBUG=-all wine64 cmd /c exit 0 2>/dev/null && ! WINEDEBUG=-all wine cmd /c exit 0 2>/dev/null; then
    echo "Wine failed a quick probe. Install wine32 (see above) or place check-wine-livevocoder-host.sh next to this script." >&2
    exit 1
  fi
fi
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
