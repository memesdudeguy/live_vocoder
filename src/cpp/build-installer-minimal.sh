#!/usr/bin/env bash
# Inno Setup — minimal C++-only package (LiveVocoderCppMinimal.iss).
set -euo pipefail
ROOT="$(cd "$(dirname "$0")" && pwd)"
cd "$ROOT"
mkdir -p "${ROOT}/dist-installer"

bash "${ROOT}/bundle-installer-minimal.sh"

if ! python3 "${ROOT}/installer/gen_livevocoder_ico.py"; then
  echo "Failed to build installer/LiveVocoder.ico (need python3 + Pillow or ffmpeg; assets/app-icon.webp)." >&2
  exit 1
fi
if [[ ! -f "${ROOT}/installer/LiveVocoder.ico" ]]; then
  echo "Missing installer/LiveVocoder.ico" >&2
  exit 1
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
  "$iscc" "${ROOT}/installer/LiveVocoderCppMinimal.iss"
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
    (cd "${ROOT}/installer" && wine "$_wiscc" LiveVocoderCppMinimal.iss)
  else
    echo "ISCC.exe not found. Install Inno Setup 6 (64-bit → \"Program Files\", 32-bit → \"Program Files (x86)\") under Wine, or use Windows/Git Bash. See installer/README-LINUX.txt" >&2
    exit 1
  fi
fi

echo "Installer output:"
ls -1 "${ROOT}/dist-installer"/LiveVocoder-Setup.exe 2>/dev/null || true

_setup_exe=""
if [[ -f "${ROOT}/dist-installer/LiveVocoder-Setup.exe" ]]; then
  _setup_exe="${ROOT}/dist-installer/LiveVocoder-Setup.exe"
else
  _setup_exe="$(ls -1t "${ROOT}/dist-installer"/LiveVocoder_Cpp_Setup_*.exe 2>/dev/null | head -1)"
fi
if [[ -n "$_setup_exe" ]]; then
  cp -f "${ROOT}/installer/LiveVocoder.ico" "${ROOT}/dist-installer/LiveVocoder.ico"
  _ic_abs="$(readlink -f "${ROOT}/dist-installer/LiveVocoder.ico")"
  _ex_abs="$(readlink -f "$_setup_exe")"
  _desk="${ROOT}/dist-installer/LiveVocoder_Cpp_Setup_Wine.desktop"
  {
    echo "[Desktop Entry]"
    echo "Version=1.0"
    echo "Type=Application"
    echo "Name=Live Vocoder C++ Setup (minimal)"
    echo "Comment=Windows installer (run with Wine)"
    echo "Exec=wine \"${_ex_abs}\""
    echo "Icon=${_ic_abs}"
    echo "Categories=AudioVideo;Audio;Mixer;"
  } >"$_desk"
  chmod 755 "$_desk"
  echo "Linux launcher: $_desk" >&2
fi

cp -f "${ROOT}/installer/sh-LiveVocoder-Setup.sh" "${ROOT}/dist-installer/sh-LiveVocoder-Setup.sh"
chmod +x "${ROOT}/dist-installer/sh-LiveVocoder-Setup.sh"
