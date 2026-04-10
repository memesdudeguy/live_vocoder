#!/usr/bin/env bash
# Download Windows ffmpeg.exe (BtbN GPL build) into dist-windows-cross/ next to LiveVocoder.exe.
# Required for MP3/FLAC/… carriers when running the MinGW .exe under Wine or on Windows
# (Linux /usr/bin/ffmpeg is not usable from the PE).
#
# Usage: from cpp/:  ./fetch-windows-ffmpeg.sh
# Needs: curl, unzip. ~200 MiB download.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")" && pwd)"
OUT="${ROOT}/dist-windows-cross"
ZIP="${TMPDIR:-/tmp}/live-vocoder-ffmpeg-win64.zip"
URL="https://github.com/BtbN/FFmpeg-Builds/releases/download/latest/ffmpeg-master-latest-win64-gpl.zip"
INZIP="ffmpeg-master-latest-win64-gpl/bin/ffmpeg.exe"

mkdir -p "$OUT"
echo "Downloading $URL ..."
curl -fsSL -o "$ZIP" "$URL"
echo "Extracting ffmpeg.exe → $OUT/"
unzip -o -j "$ZIP" "$INZIP" -d "$OUT"
ls -la "$OUT/ffmpeg.exe"
echo "Done. Run: cd $OUT && wine ./LiveVocoder.exe"
