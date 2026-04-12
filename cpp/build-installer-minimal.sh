#!/usr/bin/env bash
# Inno Setup — minimal C++-only package (LiveVocoderCppMinimal.iss).
set -euo pipefail
ROOT="$(cd "$(dirname "$0")" && pwd)"
cd "$ROOT"
mkdir -p "${ROOT}/dist-installer"

bash "${ROOT}/bundle-installer-minimal.sh"

if [[ ! -f "${ROOT}/dist-windows-installer-minimal/x64/LiveVocoder.exe" ]]; then
  echo "build-installer-minimal: missing dist-windows-installer-minimal/x64/LiveVocoder.exe (run bundle-installer-minimal.sh)." >&2
  exit 1
fi
if [[ ! -f "${ROOT}/dist-windows-installer-minimal/x64/ffmpeg.exe" ]]; then
  echo "build-installer-minimal: missing dist-windows-installer-minimal/x64/ffmpeg.exe (required for carrier conversion on Windows)." >&2
  exit 1
fi

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

# Inno lives under the Wine prefix where it was installed. If WINEPREFIX points elsewhere (e.g. a throwaway
# prefix from a prior test), fall back to $HOME/.wine so ISCC is still found.
_wine_iscc=""
_inno_wineprefix=""
_prev_wp=""
for _wp in "${WINEPREFIX:-$HOME/.wine}" "$HOME/.wine"; do
  [[ -z "$_wp" ]] && continue
  [[ "$_wp" == "$_prev_wp" ]] && continue
  _prev_wp="$_wp"
  for _sub in \
    "Program Files (x86)/Inno Setup 6/ISCC.exe" \
    "Program Files/Inno Setup 6/ISCC.exe"; do
    if [[ -f "$_wp/drive_c/$_sub" ]]; then
      _wine_iscc="$_wp/drive_c/$_sub"
      _inno_wineprefix="$_wp"
      break 2
    fi
  done
done

run_iscc() {
  local extra=("$@")
  if [[ -n "$iscc" ]]; then
    "$iscc" "${extra[@]}" "${ROOT}/installer/LiveVocoderCppMinimal.iss"
  elif [[ -n "$_wine_iscc" ]] && command -v wine >/dev/null 2>&1; then
    echo "Using Wine (WINEPREFIX=${_inno_wineprefix}): $_wine_iscc ${extra[*]}" >&2
    (cd "${ROOT}/installer" && WINEPREFIX="${_inno_wineprefix}" wine "$_wine_iscc" "${extra[@]}" LiveVocoderCppMinimal.iss)
  else
    echo "ISCC.exe not found. Install Inno Setup 6 (64-bit → \"Program Files\", 32-bit → \"Program Files (x86)\") under Wine, or use Windows/Git Bash. See installer/README-LINUX.txt" >&2
    return 1
  fi
}

if [[ -n "$iscc" ]]; then
  run_iscc
elif [[ -n "$_wine_iscc" ]]; then
  run_iscc
else
  echo "ISCC.exe not found. Install Inno Setup 6 (64-bit → \"Program Files\", 32-bit → \"Program Files (x86)\") under Wine, or use Windows/Git Bash. See installer/README-LINUX.txt" >&2
  exit 1
fi

echo "Installer output:"
ls -1 "${ROOT}/dist-installer/LiveVocoder-Setup.exe" 2>/dev/null || true

_setup_exe=""
if [[ -f "${ROOT}/dist-installer/LiveVocoder-Setup.exe" ]]; then
  _setup_exe="${ROOT}/dist-installer/LiveVocoder-Setup.exe"
else
  _setup_exe="$(ls -1t "${ROOT}/dist-installer"/LiveVocoder_Cpp_Setup_*.exe 2>/dev/null | head -1)"
fi
cp -f "${ROOT}/installer/sh-LiveVocoder-Setup.sh" "${ROOT}/dist-installer/sh-LiveVocoder-Setup.sh"
chmod +x "${ROOT}/dist-installer/sh-LiveVocoder-Setup.sh"
if [[ -f "${ROOT}/../scripts/check-wine-livevocoder-host.sh" ]]; then
  cp -f "${ROOT}/../scripts/check-wine-livevocoder-host.sh" "${ROOT}/dist-installer/check-wine-livevocoder-host.sh"
  chmod +x "${ROOT}/dist-installer/check-wine-livevocoder-host.sh"
fi
if [[ -n "$_setup_exe" ]]; then
  cp -f "${ROOT}/installer/LiveVocoder.ico" "${ROOT}/dist-installer/LiveVocoder.ico"
  _ic_abs="$(readlink -f "${ROOT}/dist-installer/LiveVocoder.ico")"
  _ex_abs="$(readlink -f "$_setup_exe")"
  _sh_abs="$(readlink -f "${ROOT}/dist-installer/sh-LiveVocoder-Setup.sh")"
  _desk="${ROOT}/dist-installer/LiveVocoder_Cpp_Setup_Wine.desktop"
  {
    echo "[Desktop Entry]"
    echo "Version=1.0"
    echo "Type=Application"
    echo "Name=Live Vocoder C++ Setup (Wine)"
    echo "Comment=Minimal installer — Wine prefix ~/.wine-livevocoder, wineboot if new, wine32 check"
    echo "Exec=/bin/sh \"${_sh_abs}\""
    echo "Icon=${_ic_abs}"
    echo "Categories=AudioVideo;Audio;Mixer;"
  } >"$_desk"
  chmod 755 "$_desk"
  echo "Linux launcher (Wine installer): $_desk" >&2
fi
