#!/usr/bin/env bash
# Build Inno Setup installer: native ISCC (Windows / Git Bash / MSYS2), or Wine + Inno Setup 6 in WINEPREFIX.
#
# Packages dist-windows-cross/ if present, else dist-windows/.
# Full GUI bundle: run bundle-dist-windows-cross-python.sh (embeddable Python + *.py).
set -euo pipefail
ROOT="$(cd "$(dirname "$0")" && pwd)"
cd "$ROOT"
mkdir -p "$ROOT/dist-installer"

PORTABLE="dist-windows-cross"
if [[ ! -f "$PORTABLE/LiveVocoder.exe" ]]; then
  PORTABLE="dist-windows"
fi
if [[ ! -f "$PORTABLE/LiveVocoder.exe" ]]; then
  echo "Missing dist-windows-cross/LiveVocoder.exe or dist-windows/LiveVocoder.exe." >&2
  exit 1
fi

# Inno SetupIconFile=LiveVocoder.ico (same folder as .iss)
if ! python3 "$ROOT/installer/gen_livevocoder_ico.py"; then
  echo "Failed to build installer/LiveVocoder.ico (need python3 + Pillow or ffmpeg; assets/app-icon.webp)." >&2
  exit 1
fi
if [[ ! -f "$PORTABLE/python/python.exe" ]]; then
  echo "WARNING: No $PORTABLE/python/python.exe — installer omits bundled Python. Run bundle-dist-windows-cross-python.sh for full app." >&2
fi

iscc=""
for d in \
  "/c/Program Files (x86)/Inno Setup 6" \
  "/c/Program Files/Inno Setup 6"; do
  if [[ -x "$d/ISCC.exe" ]]; then
    iscc="$d/ISCC.exe"
    break
  fi
done

if [[ -z "$iscc" ]] && command -v iscc.exe >/dev/null 2>&1; then
  iscc="$(command -v iscc.exe)"
fi

if [[ -n "$iscc" ]]; then
  "$iscc" "$ROOT/installer/LiveVocoder.iss"
else
  _wp="${WINEPREFIX:-$HOME/.wine}"
  _wiscc=""
  for _sub in \
    "Program Files (x86)/Inno Setup 6/ISCC.exe" \
    "Program Files/Inno Setup 6/ISCC.exe"; do
    if [[ -f "$_wp/drive_c/$_sub" ]]; then
      _wiscc="$_wp/drive_c/$_sub"
      break
    fi
  done
  if [[ -n "$_wiscc" ]] && command -v wine >/dev/null 2>&1; then
    echo "Using Wine: $_wiscc" >&2
    (cd "$ROOT/installer" && wine "$_wiscc" LiveVocoder.iss)
  else
    echo "ISCC.exe not found (Windows paths or PATH), and no Wine Inno under:" >&2
    echo "  $_wp/drive_c/Program Files (x86)/Inno Setup 6/  or  .../Program Files/..." >&2
    echo "Install Inno Setup 6: https://jrsoftware.org/isinfo.php" >&2
    exit 1
  fi
fi

echo "Installer output:"
ls -1 "$ROOT/dist-installer"/LiveVocoder_Setup_*.exe 2>/dev/null || true

# Dolphin / many Linux file managers do not show embedded Windows icons on .exe files.
# Ship a .desktop next to the installer so KDE can show LiveVocoder.ico when you use the launcher.
_setup_exe="$(ls -1t "$ROOT/dist-installer"/LiveVocoder_Setup_*.exe 2>/dev/null | head -1)"
if [[ -n "$_setup_exe" ]]; then
  cp -f "$ROOT/installer/LiveVocoder.ico" "$ROOT/dist-installer/LiveVocoder.ico"
  _ic_abs="$(readlink -f "$ROOT/dist-installer/LiveVocoder.ico")"
  _ex_abs="$(readlink -f "$_setup_exe")"
  _desk="$ROOT/dist-installer/LiveVocoder_Setup_Wine.desktop"
  {
    echo "[Desktop Entry]"
    echo "Version=1.0"
    echo "Type=Application"
    echo "Name=Live Vocoder Setup"
    echo "Comment=Windows installer (run with Wine)"
    echo "Exec=wine \"${_ex_abs}\""
    echo "Icon=${_ic_abs}"
    echo "Categories=AudioVideo;Audio;Mixer;"
  } >"$_desk"
  chmod 755 "$_desk"
  echo "Linux launcher (custom icon in KDE): $_desk" >&2
fi
