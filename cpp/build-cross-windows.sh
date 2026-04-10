#!/usr/bin/env bash
# Cross-compile LiveVocoder.exe on Linux using MinGW-w64 + pkg-config for the MinGW sysroot.
#
# Arch Linux
#   Toolchain (official repos):
#     sudo pacman -S mingw-w64-gcc mingw-w64-binutils
#   FFTW + PortAudio + SDL2 + SDL2_ttf + MinGW pkg-config wrappers are in the AUR (not pacman extra/community):
#     yay -S mingw-w64-fftw mingw-w64-portaudio mingw-w64-sdl2 mingw-w64-sdl2_ttf mingw-w64-pkg-config
#   (paru -S ... works the same.)
#
# Fedora (similar):
#   sudo dnf install mingw64-gcc-c++ mingw64-fftw mingw64-portaudio mingw64-pkg-config
#   export MINGW_ROOT=/usr/x86_64-w64-mingw32/sys-root/mingw   # if .pc files live there
#
# Then from cpp/:
#   ./build-cross-windows.sh
#
# This script also runs bundle-cross-dlls.sh and, by default, bundle-dist-windows-cross-python.sh
# (embeddable Python + repo *.py into dist-windows-cross; needs curl/wget + network).
# Skip the Python step:  LV_BUNDLE_PYTHON=0 ./build-cross-windows.sh
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
  echo "Could not find MinGW fftw3 / portaudio under /usr/x86_64-w64-mingw32/lib/pkgconfig." >&2
  echo "On Arch, those packages are AUR-only, e.g.:" >&2
  echo "  yay -S mingw-w64-fftw mingw-w64-portaudio mingw-w64-pkg-config" >&2
  echo "Or set MINGW_ROOT to a sysroot that already has lib/pkgconfig/fftw3.pc and portaudio-2.0.pc" >&2
  exit 1
fi

export PKG_CONFIG_LIBDIR="${MINGW_ROOT}/lib/pkgconfig"
export PKG_CONFIG_PATH=""
export PKG_CONFIG_SYSROOT_DIR="${MINGW_ROOT}"

PC_BIN=""
if command -v x86_64-w64-mingw32-pkg-config >/dev/null 2>&1; then
  PC_BIN=x86_64-w64-mingw32-pkg-config
elif command -v mingw64-pkg-config >/dev/null 2>&1; then
  PC_BIN=mingw64-pkg-config
else
  PC_BIN=pkg-config
fi

if ! "$PC_BIN" --exists fftw3 2>/dev/null; then
  echo "pkg-config cannot see mingw fftw3 (PKG_CONFIG_LIBDIR=$PKG_CONFIG_LIBDIR)." >&2
  echo "Arch: install AUR package mingw-w64-fftw (and usually mingw-w64-pkg-config)." >&2
  exit 1
fi
if ! "$PC_BIN" --exists portaudio-2.0 2>/dev/null; then
  echo "pkg-config cannot see mingw portaudio-2.0 (PKG_CONFIG_LIBDIR=$PKG_CONFIG_LIBDIR)." >&2
  echo "Arch: install AUR package mingw-w64-portaudio." >&2
  exit 1
fi
if ! "$PC_BIN" --exists sdl2 2>/dev/null; then
  echo "pkg-config cannot see mingw sdl2 (PKG_CONFIG_LIBDIR=$PKG_CONFIG_LIBDIR)." >&2
  echo "Arch: install AUR package mingw-w64-sdl2 (SDL2 GUI is default for portable LiveVocoder.exe)." >&2
  exit 1
fi
if ! "$PC_BIN" --exists SDL2_ttf 2>/dev/null; then
  echo "pkg-config cannot see mingw SDL2_ttf (needed for SDL GUI text, GTK-style layout)." >&2
  echo "Arch: install AUR package mingw-w64-sdl2_ttf (and mingw-w64-freetype if pulled in)." >&2
  exit 1
fi

BUILD_DIR="${ROOT}/build-mingw-cross"
TOOLCHAIN="${ROOT}/cmake/mingw-w64-cross.cmake"

echo "MINGW_ROOT=$MINGW_ROOT"
echo "Using pkg-config: $PC_BIN"

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
  echo "Bundled MinGW ffmpeg.exe → $OUT/ffmpeg.exe (MP3/FLAC/… carriers under Wine/Windows)."
else
  echo "No ${_FFMPEG_PE} — non-.f32 carriers need ffmpeg.exe next to LiveVocoder.exe or on PATH (see README.txt)."
fi
for _launcher in install_python_deps.sh run_web_gui.sh run_gtk_gui.sh run_web_gui.bat run_gtk_gui.bat; do
  if [[ -f "${ROOT}/${_launcher}" ]]; then
    cp -f "${ROOT}/${_launcher}" "$OUT/"
    case "${_launcher}" in
      *.sh) chmod +x "$OUT/${_launcher}" ;;
    esac
  fi
done
REPO_ROOT="$(cd "$ROOT/.." && pwd)"
if [[ -f "${REPO_ROOT}/run-mac.command" ]]; then
  cp -f "${REPO_ROOT}/run-mac.command" "$OUT/"
  chmod +x "$OUT/run-mac.command"
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
  echo "warning: DejaVuSans.ttf not found on build host — Windows users still get Segoe UI from WINDIR/Fonts" >&2
fi

if [[ "${LV_BUNDLE_PYTHON:-1}" != "0" ]]; then
  echo "==> bundle-dist-windows-cross-python.sh (LV_BUNDLE_PYTHON=0 to skip; needs network)"
  bash "$ROOT/bundle-dist-windows-cross-python.sh"
else
  echo "Skipped embeddable Python bundle. Run ./bundle-dist-windows-cross-python.sh when online."
fi

echo "Done: $OUT/"
