#!/usr/bin/env bash
# Stage only files needed for the C++ SDL installer (no Python).
# Picks Windows PE binaries into dist-windows-installer-minimal/x64/ (required).
# Optional native ARM64: set LV_MINIMAL_ARM64_DIR to a folder with LiveVocoder.exe + *.dll + ffmpeg.exe
#   → also populates dist-windows-installer-minimal/arm64/ (Inno picks arch at install time).
#
# x64 source resolution (first match):
#   1) LV_MINIMAL_X64_DIR — folder containing PE LiveVocoder.exe + DLLs
#   2) LV_MINIMAL_EXE — explicit path to PE executable (DLLs from its directory)
#   3) build/LiveVocoder.exe — if PE (MinGW)
#   4) dist-windows-cross/ then dist-windows/
#
# Output: dist-windows-installer-minimal/ with x64/ (and optionally arm64/) plus shared extras/fonts/README.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")" && pwd)"

is_windows_pe_exe() {
  local f="$1"
  [[ -f "$f" ]] || return 1
  if [[ "${LV_SKIP_PE_CHECK:-}" == "1" ]]; then
    return 0
  fi
  case "$(file -b "$f" 2>/dev/null)" in
    PE32*) return 0 ;;
    *"PE32"*) return 0 ;;
    *"MS Windows"*) return 0 ;;
    *) return 1 ;;
  esac
}

X64_SRC=""
if [[ -n "${LV_MINIMAL_X64_DIR:-}" ]]; then
  _xdir="$(cd "${LV_MINIMAL_X64_DIR}" && pwd)"
  if ! is_windows_pe_exe "${_xdir}/LiveVocoder.exe"; then
    echo "LV_MINIMAL_X64_DIR must contain a PE LiveVocoder.exe, got:" >&2
    file "${_xdir}/LiveVocoder.exe" >&2 || true
    exit 1
  fi
  X64_SRC="${_xdir}"
  echo "Minimal bundle: using LV_MINIMAL_X64_DIR → ${X64_SRC}" >&2
elif [[ -n "${LV_MINIMAL_EXE:-}" ]]; then
  if ! is_windows_pe_exe "${LV_MINIMAL_EXE}"; then
    echo "LV_MINIMAL_EXE must be a Windows PE executable, got:" >&2
    file "${LV_MINIMAL_EXE}" >&2 || true
    exit 1
  fi
  X64_SRC="$(cd "$(dirname "${LV_MINIMAL_EXE}")" && pwd)"
  echo "Minimal bundle: using LV_MINIMAL_EXE → ${LV_MINIMAL_EXE}" >&2
elif is_windows_pe_exe "${ROOT}/build/LiveVocoder.exe"; then
  X64_SRC="${ROOT}/build"
  echo "Minimal bundle: using ${X64_SRC}/LiveVocoder.exe (Windows PE)" >&2
elif [[ -f "${ROOT}/dist-windows-cross/LiveVocoder.exe" ]] && is_windows_pe_exe "${ROOT}/dist-windows-cross/LiveVocoder.exe"; then
  X64_SRC="${ROOT}/dist-windows-cross"
  echo "Minimal bundle: using ${X64_SRC}/LiveVocoder.exe" >&2
elif [[ -f "${ROOT}/dist-windows/LiveVocoder.exe" ]] && is_windows_pe_exe "${ROOT}/dist-windows/LiveVocoder.exe"; then
  X64_SRC="${ROOT}/dist-windows"
  echo "Minimal bundle: using ${X64_SRC}/LiveVocoder.exe" >&2
else
  if [[ -f "${ROOT}/build/LiveVocoder.exe" ]]; then
    echo "${ROOT}/build/LiveVocoder.exe exists but is not a Windows PE binary (native Linux build is ELF)." >&2
    echo "The Inno installer must package the MinGW/cross build. For example:" >&2
    echo "  cd \"${ROOT}\" && LV_BUNDLE_PYTHON=0 ./build-cross-windows.sh" >&2
    echo "Or set LV_MINIMAL_EXE / LV_MINIMAL_X64_DIR to a PE folder." >&2
  else
    echo "Missing Windows LiveVocoder.exe — build with MinGW cross, e.g.:" >&2
    echo "  LV_BUNDLE_PYTHON=0 ./build-cross-windows.sh" >&2
    echo "Or place a PE build at ${ROOT}/build/LiveVocoder.exe" >&2
  fi
  exit 1
fi

DST="${ROOT}/dist-windows-installer-minimal"
mkdir -p "${DST}/x64" "${DST}/fonts" "${DST}/extras"
rm -f "${DST}/x64"/*.dll 2>/dev/null || true
rm -rf "${DST}/arm64"
mkdir -p "${DST}/arm64"
rm -f "${DST}/arm64"/*.dll 2>/dev/null || true

copy_arch_tree() {
  local src="$1"
  local dest="$2"
  if [[ -n "${LV_MINIMAL_EXE:-}" ]] && [[ "${src}" == "${X64_SRC}" ]]; then
    cp -f "${LV_MINIMAL_EXE}" "${dest}/LiveVocoder.exe"
  else
    cp -f "${src}/LiveVocoder.exe" "${dest}/"
  fi
  shopt -s nullglob
  local dlls=("${src}"/*.dll)
  if ((${#dlls[@]} == 0)); then
    echo "warning: no *.dll next to LiveVocoder.exe in ${src} — run ./bundle-cross-dlls.sh (MinGW cross) or MSYS2 bundle." >&2
  else
    for f in "${dlls[@]}"; do
      cp -f "$f" "${dest}/"
    done
  fi
  shopt -u nullglob
}

copy_arch_tree "${X64_SRC}" "${DST}/x64"

ARM_SRC=""
if [[ -n "${LV_MINIMAL_ARM64_DIR:-}" ]]; then
  ARM_SRC="$(cd "${LV_MINIMAL_ARM64_DIR}" && pwd)"
  if ! is_windows_pe_exe "${ARM_SRC}/LiveVocoder.exe"; then
    echo "LV_MINIMAL_ARM64_DIR must contain a PE LiveVocoder.exe" >&2
    exit 1
  fi
  copy_arch_tree "${ARM_SRC}" "${DST}/arm64"
  echo "Minimal bundle: also staging arm64 from LV_MINIMAL_ARM64_DIR → ${ARM_SRC}" >&2
else
  rmdir "${DST}/arm64" 2>/dev/null || true
fi

if [[ -f "${ROOT}/installer/LiveVocoder.ico" ]]; then
  cp -f "${ROOT}/installer/LiveVocoder.ico" "${DST}/"
fi
# VB-Audio's zip contains .inf/.cat/.sys next to VBCABLE_Setup_x64.exe; copying only the .exe breaks driver install.
_vb_pack="${ROOT}/installer/third_party/vbcable"
_vb_legacy="${ROOT}/installer/third_party/VBCABLE_Setup_x64.exe"
if [[ -f "${_vb_pack}/VBCABLE_Setup_x64.exe" ]]; then
  mkdir -p "${DST}/extras"
  cp -f "${_vb_pack}/"* "${DST}/extras/"
  echo "bundle-installer-minimal: bundled VB-Audio driver pack → ${DST}/extras/ ($(ls -1 "${DST}/extras" | wc -l) files)" >&2
elif [[ -f "${_vb_legacy}" ]]; then
  cp -f "${_vb_legacy}" "${DST}/extras/"
  echo "bundle-installer-minimal: warning: only ${_vb_legacy} — extract the full VBCABLE_Driver_Pack*.zip into installer/third_party/vbcable/ so .inf files ship with the exe (see third_party/README.txt)." >&2
else
  echo "bundle-installer-minimal: optional VB-Cable not found — add installer/third_party/vbcable/ (full zip contents) or legacy VBCABLE_Setup_x64.exe in third_party/ (see README.txt)." >&2
fi
if [[ -f "${X64_SRC}/app-icon.png" ]]; then
  cp -f "${X64_SRC}/app-icon.png" "${DST}/"
elif [[ -f "${ROOT}/assets/app-icon.png" ]]; then
  cp -f "${ROOT}/assets/app-icon.png" "${DST}/"
fi
if [[ -f "${X64_SRC}/fonts/DejaVuSans.ttf" ]]; then
  cp -f "${X64_SRC}/fonts/DejaVuSans.ttf" "${DST}/fonts/"
elif [[ -n "${ARM_SRC}" ]] && [[ -f "${ARM_SRC}/fonts/DejaVuSans.ttf" ]]; then
  cp -f "${ARM_SRC}/fonts/DejaVuSans.ttf" "${DST}/fonts/"
fi
if [[ -f "${X64_SRC}/ffmpeg.exe" ]]; then
  cp -f "${X64_SRC}/ffmpeg.exe" "${DST}/x64/"
fi

if [[ ! -f "${DST}/x64/ffmpeg.exe" ]]; then
  echo "bundle-installer-minimal: ffmpeg.exe is required in x64/ for the Windows installer (carrier conversion)." >&2
  echo "  Copy ffmpeg.exe into ${X64_SRC}/ or install MinGW ffmpeg and re-bundle." >&2
  exit 1
fi

if [[ -n "${ARM_SRC}" ]]; then
  if [[ -f "${ARM_SRC}/ffmpeg.exe" ]]; then
    cp -f "${ARM_SRC}/ffmpeg.exe" "${DST}/arm64/"
  fi
  if [[ ! -f "${DST}/arm64/ffmpeg.exe" ]]; then
    echo "bundle-installer-minimal: arm64 payload requires ffmpeg.exe next to LiveVocoder.exe in LV_MINIMAL_ARM64_DIR." >&2
    exit 1
  fi
fi

if [[ -f "${ROOT}/installer/Run-from-QEMU-share.bat" ]]; then
  cp -f "${ROOT}/installer/Run-from-QEMU-share.bat" "${DST}/"
fi
if [[ -f "${ROOT}/installer/SmokeValidateF32.bat" ]]; then
  cp -f "${ROOT}/installer/SmokeValidateF32.bat" "${DST}/"
fi
_PY="${PYTHON_CMD:-python3}"
if ! "${_PY}" "${ROOT}/installer/gen_smoke_carrier_f32.py" "${DST}/smoke_carrier.f32"; then
  echo "bundle-installer-minimal: gen_smoke_carrier_f32.py failed (set PYTHON_CMD if python3 is not on PATH)." >&2
  exit 1
fi
if [[ -f "${ROOT}/../scripts/check-wine-livevocoder-host.sh" ]]; then
  cp -f "${ROOT}/../scripts/check-wine-livevocoder-host.sh" "${DST}/check-wine-livevocoder-host.sh"
  chmod +x "${DST}/check-wine-livevocoder-host.sh"
fi
if [[ -f "${ROOT}/installer/sh-LiveVocoder-Setup.sh" ]]; then
  cp -f "${ROOT}/installer/sh-LiveVocoder-Setup.sh" "${DST}/sh-LiveVocoder-Setup.sh"
  chmod +x "${DST}/sh-LiveVocoder-Setup.sh"
fi
if [[ -f "${ROOT}/installer/README_Cpp_Minimal.txt" ]]; then
  cp -f "${ROOT}/installer/README_Cpp_Minimal.txt" "${DST}/README_Cpp_Minimal.txt"
else
  cat >"${DST}/README_Cpp_Minimal.txt" <<'EOF'
Live Vocoder — C++ SDL build (this installer)

Included: LiveVocoder.exe, MinGW/Pulse/SDL runtime DLLs, optional DejaVu font and ffmpeg.

• Carriers folder: Windows Documents\LiveVocoderCarriers (shell path; same as setup’s userdocs; created by setup and on first run).
  A README.txt is placed there during install. Drop audio onto the app window to convert to .f32 here.
• Carrier files: non-.f32 formats need ffmpeg.exe next to the app or ffmpeg on PATH.
• Text UI: Windows uses Segoe UI / Arial if fonts\DejaVuSans.ttf is missing.

Windows — audio devices
  Choose input/output with LIVE_VOCODER_PA_INPUT / LIVE_VOCODER_PA_OUTPUT (name substring) or *_INDEX.
  LIVE_VOCODER_PA_LIST_DEVICES=1 lists PortAudio device names. Optional virtual cables are not required.

Linux / PipeWire: see the main project README (PULSE_SINK, null sink, *_mic).

Wine on Linux: on startup the Windows .exe detects Wine (ntdll wine_get_version) and runs host pactl (via Z:\\usr\\bin\\bash) to
  ensure the live_vocoder* null sink + *_mic and sets PULSE_SINK. Prefer live-vocoder-wine-launch.sh after install.
  Override bash path: LIVE_VOCODER_WINE_BASH. Disable auto setup: LIVE_VOCODER_AUTO_VIRT_MIC=0.

Full Python/GTK/web bundle: use the standard LiveVocoder_Setup_*.exe (larger) from the same project.
EOF
fi

echo "Minimal installer payload: ${DST}/"
ls -la "${DST}"
