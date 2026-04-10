#!/usr/bin/env bash
# Cross-compile LiveVocoder.exe (MinGW) from this tree — no Python bundle.
# Same toolchain notes as ../cpp/build-cross-windows.sh (Arch AUR mingw-w64-* packages).
#
# From LiveVocoder/:
#   ./build-cross-windows.sh
#   ./build-installer-minimal.sh    # Inno Setup → dist-installer/LiveVocoder_Cpp_Setup_*.exe
#
set -euo pipefail
ROOT="$(cd "$(dirname "$0")" && pwd)"
cd "$ROOT"

if [[ -f "$ROOT/assets/app-icon.webp" ]] || [[ -f "$ROOT/assets/app-icon.png" ]]; then
  if ! python3 "$ROOT/installer/gen_livevocoder_ico.py"; then
    echo "gen_livevocoder_ico.py failed (install pillow or ffmpeg); using committed LiveVocoder.ico if present." >&2
  fi
fi
if [[ ! -f "$ROOT/installer/LiveVocoder.ico" ]]; then
  echo "Missing installer/LiveVocoder.ico — run: python3 installer/gen_livevocoder_ico.py" >&2
  exit 1
fi

detect_root() {
  if [[ -n "${MINGW_ROOT:-}" ]]; then
    echo "$MINGW_ROOT"
    return
  fi
  for d in /usr/x86_64-w64-mingw32 /usr/x86_64-w64-mingw32/sys-root/mingw; do
    if [[ -d "$d/lib/pkgconfig" ]] && [[ -f "$d/lib/pkgconfig/fftw3.pc" || -f "$d/lib/pkgconfig/portaudio-2.0.pc" ]]; then
      echo "$d"
      return
    fi
  done
  echo ""
}

MINGW_ROOT="$(detect_root)"
if [[ -z "$MINGW_ROOT" ]]; then
  echo "Could not find MinGW fftw3 / portaudio pkg-config. See ../cpp/build-cross-windows.sh header." >&2
  exit 1
fi

export PKG_CONFIG_LIBDIR="${MINGW_ROOT}/lib/pkgconfig"
export PKG_CONFIG_PATH=""
export PKG_CONFIG_SYSROOT_DIR="${MINGW_ROOT}"

if command -v x86_64-w64-mingw32-pkg-config >/dev/null 2>&1; then
  PC_BIN=x86_64-w64-mingw32-pkg-config
elif command -v mingw64-pkg-config >/dev/null 2>&1; then
  PC_BIN=mingw64-pkg-config
else
  PC_BIN=pkg-config
fi

for pc in fftw3 portaudio-2.0 sdl2 SDL2_ttf; do
  if ! "$PC_BIN" --exists "$pc" 2>/dev/null; then
    echo "pkg-config missing $pc (PKG_CONFIG_LIBDIR=$PKG_CONFIG_LIBDIR)." >&2
    exit 1
  fi
done

BUILD_DIR="${ROOT}/build-mingw-cross"
TOOLCHAIN="${ROOT}/cmake/mingw-w64-cross.cmake"

echo "MINGW_ROOT=$MINGW_ROOT"
cmake -S "$ROOT" -B "$BUILD_DIR" \
  -DCMAKE_TOOLCHAIN_FILE="$TOOLCHAIN" \
  -DMINGW_ROOT="$MINGW_ROOT" \
  -DCMAKE_BUILD_TYPE=Release

cmake --build "$BUILD_DIR" -j"$(nproc 2>/dev/null || echo 4)"

EXE="${BUILD_DIR}/LiveVocoder.exe"
if [[ ! -f "$EXE" ]]; then
  echo "Build failed: missing $EXE" >&2
  exit 1
fi

OUT="${ROOT}/dist-windows-cross"
mkdir -p "$OUT"
cp -f "$EXE" "$OUT/"
_FFMPEG_PE="${MINGW_ROOT}/bin/ffmpeg.exe"
if [[ -f "$_FFMPEG_PE" ]]; then
  cp -f "$_FFMPEG_PE" "$OUT/ffmpeg.exe"
  echo "Bundled MinGW ffmpeg.exe → $OUT/ffmpeg.exe"
else
  echo "No ${_FFMPEG_PE} — non-.f32 carriers need ffmpeg.exe next to LiveVocoder.exe or on PATH."
fi
if [[ -f "${ROOT}/installer/LiveVocoder.ico" ]]; then
  cp -f "${ROOT}/installer/LiveVocoder.ico" "$OUT/"
fi
if [[ -f "${ROOT}/assets/app-icon.png" ]]; then
  cp -f "${ROOT}/assets/app-icon.png" "$OUT/"
fi
echo "Built: $OUT/LiveVocoder.exe"

echo "==> bundle-cross-dlls.sh"
bash "$ROOT/bundle-cross-dlls.sh"

echo "==> bundle SDL GUI font (DejaVuSans.ttf)"
mkdir -p "$OUT/fonts"
_FONT=""
for _f in /usr/share/fonts/TTF/DejaVuSans.ttf /usr/share/fonts/truetype/dejavu/DejaVuSans.ttf; do
  [[ -f "$_f" ]] && _FONT="$_f" && break
done
if [[ -n "$_FONT" ]]; then
  cp -f "$_FONT" "$OUT/fonts/DejaVuSans.ttf"
  echo "copied fonts/DejaVuSans.ttf"
else
  echo "warning: DejaVuSans.ttf not found on build host" >&2
fi

echo "Done: $OUT/ (use ./build-installer-minimal.sh for Setup.exe)"
