#!/usr/bin/env bash
# Stage only files needed for the C++ SDL installer (no Python).
# Picks a Windows PE LiveVocoder.exe (required for Inno):
#   1) LV_MINIMAL_EXE — explicit path to PE executable (DLLs from its directory)
#   2) build/LiveVocoder.exe — if it is PE (MinGW -B build); skipped if it is Linux ELF (native make setup)
#   3) dist-windows-cross/ then dist-windows/
#
# Output: dist-windows-installer-minimal/
set -euo pipefail
ROOT="$(cd "$(dirname "$0")" && pwd)"

is_windows_pe_exe() {
  local f="$1"
  [[ -f "$f" ]] || return 1
  case "$(file -b "$f" 2>/dev/null)" in
    PE32*) return 0 ;;
    *"PE32"*) return 0 ;;
    *"MS Windows"*) return 0 ;;
    *) return 1 ;;
  esac
}

SRC=""
if [[ -n "${LV_MINIMAL_EXE:-}" ]]; then
  if ! is_windows_pe_exe "${LV_MINIMAL_EXE}"; then
    echo "LV_MINIMAL_EXE must be a Windows PE executable, got:" >&2
    file "${LV_MINIMAL_EXE}" >&2 || true
    exit 1
  fi
  SRC="$(cd "$(dirname "${LV_MINIMAL_EXE}")" && pwd)"
  echo "Minimal bundle: using LV_MINIMAL_EXE → ${LV_MINIMAL_EXE}" >&2
elif is_windows_pe_exe "${ROOT}/build/LiveVocoder.exe"; then
  SRC="${ROOT}/build"
  echo "Minimal bundle: using ${SRC}/LiveVocoder.exe (Windows PE)" >&2
elif [[ -f "${ROOT}/dist-windows-cross/LiveVocoder.exe" ]] && is_windows_pe_exe "${ROOT}/dist-windows-cross/LiveVocoder.exe"; then
  SRC="${ROOT}/dist-windows-cross"
  echo "Minimal bundle: using ${SRC}/LiveVocoder.exe" >&2
elif [[ -f "${ROOT}/dist-windows/LiveVocoder.exe" ]] && is_windows_pe_exe "${ROOT}/dist-windows/LiveVocoder.exe"; then
  SRC="${ROOT}/dist-windows"
  echo "Minimal bundle: using ${SRC}/LiveVocoder.exe" >&2
else
  if [[ -f "${ROOT}/build/LiveVocoder.exe" ]]; then
    echo "${ROOT}/build/LiveVocoder.exe exists but is not a Windows PE binary (native Linux build is ELF)." >&2
    echo "The Inno installer must package the MinGW/cross build. For example:" >&2
    echo "  cd \"${ROOT}\" && LV_BUNDLE_PYTHON=0 ./build-cross-windows.sh" >&2
    echo "Or set LV_MINIMAL_EXE to a PE LiveVocoder.exe path." >&2
  else
    echo "Missing Windows LiveVocoder.exe — build with MinGW cross, e.g.:" >&2
    echo "  LV_BUNDLE_PYTHON=0 ./build-cross-windows.sh" >&2
    echo "Or place a PE build at ${ROOT}/build/LiveVocoder.exe" >&2
  fi
  exit 1
fi

DST="${ROOT}/dist-windows-installer-minimal"
mkdir -p "${DST}/fonts"
rm -f "${DST}"/*.dll 2>/dev/null || true

if [[ -n "${LV_MINIMAL_EXE:-}" ]]; then
  cp -f "${LV_MINIMAL_EXE}" "${DST}/LiveVocoder.exe"
else
  cp -f "${SRC}/LiveVocoder.exe" "${DST}/"
fi

shopt -s nullglob
_dlls=("${SRC}"/*.dll)
if ((${#_dlls[@]} == 0)); then
  echo "warning: no *.dll next to LiveVocoder.exe — run ./bundle-cross-dlls.sh (MinGW cross) or MSYS2 bundle." >&2
else
  for f in "${_dlls[@]}"; do
    cp -f "$f" "${DST}/"
  done
fi

if [[ -f "${ROOT}/installer/LiveVocoder.ico" ]]; then
  cp -f "${ROOT}/installer/LiveVocoder.ico" "${DST}/"
fi
if [[ -f "${SRC}/app-icon.png" ]]; then
  cp -f "${SRC}/app-icon.png" "${DST}/"
elif [[ -f "${ROOT}/assets/app-icon.png" ]]; then
  cp -f "${ROOT}/assets/app-icon.png" "${DST}/"
fi
if [[ -f "${SRC}/fonts/DejaVuSans.ttf" ]]; then
  cp -f "${SRC}/fonts/DejaVuSans.ttf" "${DST}/fonts/"
fi
if [[ -f "${SRC}/ffmpeg.exe" ]]; then
  cp -f "${SRC}/ffmpeg.exe" "${DST}/"
fi

if [[ ! -f "${DST}/ffmpeg.exe" ]]; then
  echo "bundle-installer-minimal: ffmpeg.exe is required next to LiveVocoder.exe for the Windows installer (carrier conversion)." >&2
  echo "  Copy ffmpeg.exe into ${SRC}/ or install MinGW ffmpeg and re-bundle. On Arch cross-build, place ffmpeg.exe in dist-windows-cross/." >&2
  exit 1
fi

if [[ -f "${ROOT}/installer/Run-from-QEMU-share.bat" ]]; then
  cp -f "${ROOT}/installer/Run-from-QEMU-share.bat" "${DST}/"
fi
for _bat in Test-CarrierF32-VM-Once.bat Test-CarrierF32-VM-UntilOk.bat; do
  if [[ -f "${ROOT}/installer/${_bat}" ]]; then
    cp -f "${ROOT}/installer/${_bat}" "${DST}/"
  fi
done
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
