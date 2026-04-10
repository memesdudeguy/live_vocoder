#!/usr/bin/env bash
# Browser (Gradio) UI under Wine — from dist-windows-cross/: ./run_web_gui.sh
DIR="$(cd "$(dirname "$0")" && pwd)"
cd "$DIR"
export LIVE_VOCODER_GUI=web
if command -v wine64 >/dev/null 2>&1; then
  exec wine64 ./LiveVocoder.exe "$@"
fi
if command -v wine >/dev/null 2>&1; then
  exec wine ./LiveVocoder.exe "$@"
fi
echo "Need wine64 or wine on PATH." >&2
exit 1
