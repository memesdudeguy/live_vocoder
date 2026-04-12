#!/usr/bin/env bash
# Run on Linux before: wine LiveVocoder-Setup.exe
# Detects incomplete Wine (missing wine32 / WOW64) that causes:
#   wine: could not load kernel32.dll, status c0000135
#   wine32 is missing, you should install it (multiarch / i386)
set -euo pipefail

have_cmd() { command -v "$1" >/dev/null 2>&1; }

echo "Live Vocoder — Wine host check"
echo "================================"

if ! have_cmd wine && ! have_cmd wine64; then
  echo "ERROR: No 'wine' or 'wine64' in PATH. Install Wine for your distro." >&2
  exit 1
fi

WINE_VER="$(wine --version 2>/dev/null || true)"
echo "Wine: ${WINE_VER:-unknown}"

# Debian / Ubuntu — wine32:i386 is often required for a working WOW64 prefix
if have_cmd dpkg && have_cmd apt-get; then
  if ! dpkg -l 2>/dev/null | grep -qE '^ii\s+wine32(:i386)?\s'; then
    echo ""
    echo "WARNING: package 'wine32' does not appear installed."
    echo "Without 32-bit Wine, you may see:"
    echo "  - wine: could not load kernel32.dll, status c0000135"
    echo "  - experimental wow64 / ntdll.dll errors"
    echo ""
    echo "Fix (run as root):"
    echo "  sudo dpkg --add-architecture i386"
    echo "  sudo apt-get update"
    echo "  sudo apt-get install -y wine wine64 wine32"
    echo ""
    echo "Then reset a broken prefix (removes ~/.wine):"
    echo "  rm -rf ~/.wine"
    echo "  WINEARCH=win64 wineboot -u"
    echo ""
  else
    echo "OK: wine32 appears installed (dpkg)."
  fi
fi

# Fedora-style
if have_cmd rpm && ! have_cmd dpkg; then
  if ! rpm -q wine-core wine-core-i686 >/dev/null 2>&1; then
    echo "Fedora/RHEL: ensure both 64- and 32-bit Wine parts are installed, e.g.:"
    echo "  sudo dnf install wine wine-core wine-core-i686"
    echo ""
  fi
fi

echo "Quick prefix probe (errors here often mean reinstall wine32 + rm -rf ~/.wine):"
if WINEDEBUG=-all wine64 cmd /c exit 0 2>/dev/null || WINEDEBUG=-all wine cmd /c exit 0 2>/dev/null; then
  echo "OK: wine could start a minimal Windows process."
  exit 0
fi

echo "ERROR: wine failed to run 'cmd'. Install wine32 (see above) and recreate ~/.wine." >&2
exit 1
