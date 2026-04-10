#!/usr/bin/env bash
# Run inside MSYS2 MINGW64 after cmake --build. Copies exe + MinGW DLLs for Windows / Wine.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")" && pwd)"
EXE="$ROOT/build/LiveVocoder.exe"
OUT="$ROOT/dist-windows"
if [[ ! -f "$EXE" ]]; then
  echo "missing $EXE" >&2
  exit 1
fi
mkdir -p "$OUT"
cp -f "$EXE" "$OUT/"
if [[ -f "$ROOT/installer/LiveVocoder.ico" ]]; then
  cp -f "$ROOT/installer/LiveVocoder.ico" "$OUT/"
fi
if [[ -f "$ROOT/assets/app-icon.png" ]]; then
  cp -f "$ROOT/assets/app-icon.png" "$OUT/"
fi
cd "$OUT"
# Dependencies from the MinGW prefix (skip Windows system DLLs)
while read -r dll; do
  [[ -f "$dll" ]] || continue
  case "$(basename "$dll" | tr '[:upper:]' '[:lower:]')" in
    kernel32.dll|user32.dll|msvcrt.dll) continue ;;
  esac
  cp -f "$dll" .
done < <(ldd "$EXE" 2>/dev/null | sed -n 's/.*=> \(.*\) (.*/\1/p' | sort -u)
echo "Bundled to $OUT"
ls -la "$OUT"
