#!/usr/bin/env bash
# Stage dist-windows-installer-minimal/ for Inno (exe + DLLs, no Python).
# Prefers ./dist-windows-cross; else ../cpp/dist-windows-cross; else ./dist-windows
set -euo pipefail
ROOT="$(cd "$(dirname "$0")" && pwd)"
SRC="${ROOT}/dist-windows-cross"
if [[ ! -f "${SRC}/LiveVocoder.exe" ]]; then
  SRC="${ROOT}/../cpp/dist-windows-cross"
fi
if [[ ! -f "${SRC}/LiveVocoder.exe" ]]; then
  SRC="${ROOT}/dist-windows"
fi
if [[ ! -f "${SRC}/LiveVocoder.exe" ]]; then
  echo "Missing LiveVocoder.exe — run ./build-cross-windows.sh here or ../cpp/build-cross-windows.sh" >&2
  exit 1
fi

DST="${ROOT}/dist-windows-installer-minimal"
mkdir -p "${DST}/fonts"
rm -f "${DST}"/*.dll 2>/dev/null || true
cp -f "${SRC}/LiveVocoder.exe" "${DST}/"

shopt -s nullglob
_dlls=("${SRC}"/*.dll)
if ((${#_dlls[@]} == 0)); then
  echo "warning: no *.dll — run ./bundle-cross-dlls.sh after cross-build." >&2
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

cat >"${DST}/README_Cpp_Minimal.txt" <<'EOF'
Live Vocoder — C++ SDL build (this installer)

Included: LiveVocoder.exe, MinGW/Pulse/SDL runtime DLLs, optional DejaVu font and ffmpeg.

• Carrier files: non-.f32 formats need ffmpeg.exe next to the app or ffmpeg on PATH.
• Text UI: Windows uses Segoe UI / Arial if fonts\DejaVuSans.ttf is missing.

Windows — audio devices
  Choose input/output with LIVE_VOCODER_PA_INPUT / LIVE_VOCODER_PA_OUTPUT (name substring) or *_INDEX.
  LIVE_VOCODER_PA_LIST_DEVICES=1 lists PortAudio device names. Optional virtual cables are not required.

Wine on Linux (PipeWire)
  On startup the app runs pactl via the host shell (needs Z:\\usr\\bin\\bash and pactl on the Linux side)
  to create the live_vocoder* null sink + *_mic and sets PULSE_SINK for Wine’s Pulse client — same idea as
  the native Linux ELF. If bash is elsewhere: LIVE_VOCODER_WINE_BASH=/path/to/bash. Disable: LIVE_VOCODER_AUTO_VIRT_MIC=0.

Linux — POSIX sh wrapper for the Windows installer (same folder as LiveVocoder-Setup.exe):
  /bin/sh ./sh-LiveVocoder-Setup.sh
  (SetupEmbeddedShPrefix in LiveVocoderCppMinimal.iss = /bin/sh; build-installer-minimal.sh EMBED_SH; optional LIVE_VOCODER_EMBED_SH;
  WINEPREFIX defaults to ~/.wine; also under Program Files\\Live Vocoder\\)

Full Python/GTK bundle: build from ../cpp/ (LiveVocoder_Setup_*.exe).
EOF

echo "Minimal installer payload: ${DST}/"
ls -la "${DST}"
