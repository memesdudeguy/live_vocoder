#!/bin/sh
# Run a downloaded LiveVocoder-Setup*.exe under Wine with an isolated prefix (~/.wine-livevocoder).
# Use this when: wine Setup.exe → "could not load kernel32.dll, status c0000135"
#
#   curl -sLO https://raw.githubusercontent.com/memesdudeguy/live_vocoder/main/scripts/wine-run-livevocoder-setup.sh
#   chmod +x wine-run-livevocoder-setup.sh
#   ./wine-run-livevocoder-setup.sh '/home/virt/Downloads/LiveVocoder-Setup(4).exe'
#
# Debian/Ubuntu: you still need wine32 (i386) — script prints apt lines if it looks missing.
set -eu

EXE="${1:?Usage: $0 /path/to/LiveVocoder-Setup.exe [extra args passed to the installer...]}"
shift

if [ ! -f "$EXE" ]; then
  echo "Not a file: $EXE" >&2
  exit 1
fi

if ! command -v wine >/dev/null 2>&1 && ! command -v wine64 >/dev/null 2>&1; then
  echo "ERROR: wine is not installed." >&2
  exit 1
fi

if command -v dpkg >/dev/null 2>&1 && command -v apt-get >/dev/null 2>&1; then
  if ! dpkg -l 2>/dev/null | grep -qE '^ii\s+wine32(:i386)?\s'; then
    echo "" >&2
    echo "wine32 does not appear installed — this often causes kernel32.dll / c0000135." >&2
    echo "Fix (run as root):" >&2
    echo "  sudo dpkg --add-architecture i386" >&2
    echo "  sudo apt-get update" >&2
    echo "  sudo apt-get install -y wine wine64 wine32" >&2
    echo "" >&2
  fi
fi

export WINEPREFIX="${WINEPREFIX:-$HOME/.wine-livevocoder}"

if [ ! -f "$WINEPREFIX/system.reg" ]; then
  echo "Initializing Wine prefix: $WINEPREFIX" >&2
  mkdir -p "$WINEPREFIX"
  WINEARCH="${WINEARCH:-win64}"
  export WINEARCH
  wineboot -u
fi

if ! WINEDEBUG=-all wine64 cmd /c exit 0 2>/dev/null && ! WINEDEBUG=-all wine cmd /c exit 0 2>/dev/null; then
  echo "ERROR: Wine still cannot run a minimal Windows process." >&2
  echo "Install wine32 (see above), then remove the prefix and retry:" >&2
  echo "  rm -rf \"$WINEPREFIX\"" >&2
  exit 1
fi

exec wine "$EXE" "$@"
