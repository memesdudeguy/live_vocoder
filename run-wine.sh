#!/usr/bin/env bash
# Run Windows .exe builds under Wine, or fall back to native Linux / Python.
# Same .exe files work on real Windows and under Wine (download from GitHub Actions if needed).
#
# Priority when wine is available:
#   1) dist/LiveVocoder.exe     — full Python app (Gradio bundle)
#   2) dist/RunLiveVocoder.exe  — Python launcher
#   3) cpp/dist-windows/LiveVocoder.exe — CI / MSYS2 bundle (+ DLLs in same folder)
#   4) cpp/dist-windows-cross/LiveVocoder.exe — Linux cross-compile bundle (+ DLLs)
#   5) dist/RunLiveVocoder      — Linux PyInstaller launcher
#   6) python3 run_portable.py
#
# Wine + Documents (same folder as GTK / Dolphin: LiveVocoderCarriers):
#   - Creates ~/Documents/LiveVocoderCarriers
#   - For each $WINEPREFIX/drive_c/users/<name>/, ensures Documents and "My Documents"
#     exist (Wine often has no Documents until first use — the old script skipped those users).
#   - Symlinks both LiveVocoderCarriers and LiveVocoder → that host folder (SDL + legacy).
#
# Dolphin "Open with Wine" does NOT run this script — either launch via ./run-wine.sh or run:
#   ./run-wine.sh --link-wine-documents
#
# Dolphin "Open with Wine" often breaks MinGW exes (wrong cwd → "Invalid handle").
# 64-bit PE: use wine64 if installed, otherwise plain wine (common on Arch — there is no wine64 binary):
#   cd cpp/dist-windows-cross && wine ./LiveVocoder.exe
#   ./cpp/run-wine-cross-exe.sh
#
# Portable cpp .exe defaults to browser UI. For GTK under Wine:
#   LIVE_VOCODER_GUI=gtk ./run-wine.sh
# Or from dist-windows-cross: ./run_gtk_gui.sh  |  ./run_web_gui.sh
# Native Linux (no Wine): ./run.sh --gtk-gui  (or --web-gui for browser)
#
# CI: "Build Windows EXE" (Python) · "Build C++ Windows EXE" (native vocoder)
#
# OBS / streaming (Linux): Wine does not feed live_vocoder*_mic by default. Either:
#   PULSE_SINK=live_vocoder2 wine ./LiveVocoder.exe   # route Wine audio to your null sink → *_mic works
# or pavucontrol → Playback → move CrossOver/Wine stream to the LiveVocoder null sink.
# Or OBS "Application Audio Capture (PipeWire)" on the Wine stream. Native ./cpp/build/LiveVocoder.exe (ELF)
# sets up null sink + PULSE_SINK automatically.

set -euo pipefail
ROOT="$(cd "$(dirname "$0")" && pwd)"
cd "$ROOT"

# PipeWire / qpwgraph: Pulse expects dotted keys (PULSE_PROP_application.name). Bash cannot export
# names with dots; pass them via env(1) so streams are not labeled "LiveVocoder.exe [audio stream…]".
lv_exec_wine() {
  exec env \
    "PULSE_PROP_application.name=${LIVE_VOCODER_PULSE_APP_NAME:-Live Vocoder}" \
    "PULSE_PROP_media.name=${LIVE_VOCODER_PULSE_MEDIA_NAME:-Live Vocoder}" \
    "PULSE_PROP_application.icon_name=${LIVE_VOCODER_PULSE_ICON_NAME:-audio-input-microphone}" \
    "$@"
}

# Host folder = Python carrier_library + SDL dropped .f32 (GTK and C++ agree).
link_wine_documents_livevocoder() {
  local host="${HOME}/Documents/LiveVocoderCarriers"
  mkdir -p "$host"
  local wp="${WINEPREFIX:-$HOME/.wine}"
  if [[ ! -d "$wp/drive_c/users" ]]; then
    echo "run-wine.sh: no Wine users dir: ${wp}/drive_c/users (run winecfg or wineboot once)." >&2
    return 0
  fi
  local udir bn docs_root name linkdir
  for udir in "$wp"/drive_c/users/*; do
    [[ -d "$udir" ]] || continue
    bn="$(basename "$udir")"
    [[ "$bn" == Public ]] && continue
    for docs_root in "$udir/Documents" "$udir/My Documents"; do
      mkdir -p "$docs_root"
      for name in LiveVocoderCarriers LiveVocoder; do
        linkdir="${docs_root}/${name}"
        if [[ -e "$linkdir" && ! -L "$linkdir" ]]; then
          echo "run-wine.sh: skip ${linkdir} (exists and is not a symlink)." >&2
          continue
        fi
        ln -sfn "$host" "$linkdir"
      done
    done
  done
}

if [[ "${1:-}" == "--link-wine-documents" ]]; then
  link_wine_documents_livevocoder
  echo "Wine profile(s) linked to: ${HOME}/Documents/LiveVocoderCarriers" >&2
  exit 0
fi

WIN_FULL="$ROOT/dist/LiveVocoder.exe"
WIN_LAUNCHER="$ROOT/dist/RunLiveVocoder.exe"
WIN_CPP_DIRS=(
  "$ROOT/cpp/dist-windows"
  "$ROOT/cpp/dist-windows-cross"
)
LIN_LAUNCHER="$ROOT/dist/RunLiveVocoder"

if command -v wine >/dev/null 2>&1 && [[ -f "$WIN_FULL" ]]; then
  link_wine_documents_livevocoder
  lv_exec_wine wine "$WIN_FULL" "$@"
fi

if command -v wine >/dev/null 2>&1 && [[ -f "$WIN_LAUNCHER" ]]; then
  link_wine_documents_livevocoder
  lv_exec_wine wine "$WIN_LAUNCHER" "$@"
fi

if command -v wine >/dev/null 2>&1; then
  for _cppdir in "${WIN_CPP_DIRS[@]}"; do
    for _name in LiveVocoder.exe; do
      _exe="$_cppdir/$_name"
      if [[ -f "$_exe" ]]; then
        cd "$_cppdir"
        link_wine_documents_livevocoder
        if command -v wine64 >/dev/null 2>&1; then
          lv_exec_wine wine64 "./$_name" "$@"
        fi
        lv_exec_wine wine "./$_name" "$@"
      fi
    done
  done
fi

if [[ -f "$LIN_LAUNCHER" ]] && [[ -x "$LIN_LAUNCHER" ]]; then
  exec "$LIN_LAUNCHER" "$@"
fi

if command -v python3 >/dev/null 2>&1; then
  exec python3 "$ROOT/run_portable.py" "$@"
fi

echo "Install python3, wine, and put LiveVocoder.exe or RunLiveVocoder.exe in dist/ (from CI or Windows build)." >&2
exit 1
