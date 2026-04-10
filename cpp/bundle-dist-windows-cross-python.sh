#!/usr/bin/env bash
# Add Windows embeddable CPython into cpp/dist-windows-cross/ next to LiveVocoder.exe.
# Run after: ./build-cross-windows.sh && ./bundle-cross-dlls.sh
#
# Produces:
#   dist-windows-cross/python/     — official embeddable zip from python.org
#   dist-windows-cross/*.py        — app + GUI modules (skips test_*, run_portable.py)
#   dist-windows-cross/requirements.txt
#   dist-windows-cross/run_live_vocoder.bat — PYTHONPATH + python live_vocoder.py %*
#   dist-windows-cross/install_python_deps.bat — get-pip + pip (Windows only; exits under Wine)
#   dist-windows-cross/install_python_deps.sh  — same via wine64/wine; logs to install_python_deps_last.log
#
# Env:
#   LV_WIN_PY_VERSION=3.12.8   (full x.y.z matching a python.org embed release)
#
# Needs: curl or wget, unzip. Network for downloads.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")" && pwd)"
REPO="$(cd "$ROOT/.." && pwd)"
DIST="$ROOT/dist-windows-cross"
PY_VER="${LV_WIN_PY_VERSION:-3.12.8}"
ZIP="python-${PY_VER}-embed-amd64.zip"
URL="https://www.python.org/ftp/python/${PY_VER}/${ZIP}"

if [[ ! -f "${DIST}/LiveVocoder.exe" ]]; then
  echo "Missing ${DIST}/LiveVocoder.exe — run ./build-cross-windows.sh (and ./bundle-cross-dlls.sh) first." >&2
  exit 1
fi

dl() {
  local out="$1"
  local u="$2"
  if command -v curl >/dev/null 2>&1; then
    curl -fsSL "$u" -o "$out"
  elif command -v wget >/dev/null 2>&1; then
    wget -q -O "$out" "$u"
  else
    echo "Need curl or wget to download Python." >&2
    exit 1
  fi
}

TMPZIP="$(mktemp)"
trap 'rm -f "$TMPZIP"' EXIT
echo "Downloading $URL ..."
dl "$TMPZIP" "$URL"

rm -rf "${DIST}/python"
mkdir -p "${DIST}/python"
unzip -q -o "$TMPZIP" -d "${DIST}/python"
# Installer catalog only; not used when running from a folder (~0.5 MB).
rm -f "${DIST}/python/python.cat"

shopt -s nullglob
PTS=("${DIST}/python"/python*._pth)
if [[ ${#PTS[@]} -ne 1 ]]; then
  echo "Expected exactly one python*._pth under ${DIST}/python, got ${#PTS[@]}" >&2
  exit 1
fi
PTH="${PTS[0]}"
# Enable site-packages and add parent dir (where live_vocoder.py lives).
sed -i 's/^#import site/import site/' "$PTH"
if ! grep -qx '\.\.' "$PTH"; then
  printf '\n..\n' >>"$PTH"
fi

dl "${DIST}/python/get-pip.py" "https://bootstrap.pypa.io/get-pip.py"

# App + GUI sources (flat next to exe; run_live_vocoder.bat sets PYTHONPATH = this folder).
echo "Copying Python modules (GUI, session, Pulse helpers, …) into $DIST/"
for f in "$REPO"/*.py; do
  [[ -f "$f" ]] || continue
  base="$(basename "$f")"
  [[ "$base" == test_*.py ]] && continue
  [[ "$base" == run_portable.py ]] && continue
  cp -f "$f" "$DIST/"
done
if [[ -f "$REPO/requirements.txt" ]]; then
  cp -f "$REPO/requirements.txt" "$DIST/"
else
  echo "warning: no requirements.txt at repo root" >&2
fi

for _launcher in install_python_deps.sh run_web_gui.sh run_gtk_gui.sh run_web_gui.bat run_gtk_gui.bat; do
  if [[ -f "${ROOT}/${_launcher}" ]]; then
    cp -f "${ROOT}/${_launcher}" "${DIST}/${_launcher}"
  fi
done
if [[ -f "${REPO}/run-mac.command" ]]; then
  cp -f "${REPO}/run-mac.command" "${DIST}/run-mac.command"
  chmod +x "${DIST}/run-mac.command"
fi
chmod +x "${DIST}/install_python_deps.sh" "${DIST}/run_web_gui.sh" "${DIST}/run_gtk_gui.sh" 2>/dev/null || true

cat >"${DIST}/run_live_vocoder.bat" <<'BAT'
@echo off
setlocal
set "HERE=%~dp0"
set "PATH=%HERE%python;%PATH%"
set "PYTHONPATH=%HERE%"
"%HERE%python\python.exe" "%HERE%live_vocoder.py" %*
endlocal
BAT

cat >"${DIST}/install_python_deps.bat" <<'BAT'
@echo off
setlocal
set "HERE=%~dp0"
REM Wine ships winepath.exe in System32; real Windows does not — use .sh from Linux instead.
if exist "%SystemRoot%\system32\winepath.exe" (
  echo.
  echo Detected Wine: this batch file is not used for pip here.
  echo From Linux, in the same folder as this file, run:
  echo   ./install_python_deps.sh
  echo Or:  bash install_python_deps.sh
  echo Optional: WINE=wine ./install_python_deps.sh
  exit /b 1
)
cd /d "%HERE%python"
if not exist "python.exe" (
  echo Missing python\python.exe >&2
  exit /b 1
)
python.exe get-pip.py
python.exe -m pip install --upgrade pip
python.exe -m pip install -r "%HERE%requirements.txt"
echo.
echo Done. Run: run_live_vocoder.bat --web-gui   or   --gtk-gui   or   --gui
endlocal
BAT

cat >"${DIST}/README_PORTABLE.txt" <<EOF
Portable folder (MinGW C++ exe + Windows embeddable Python)
============================================================

LiveVocoder.exe        — With no real CLI args: same PATH/PYTHONPATH as run_live_vocoder.bat; output in python_last_run.log.
                         Under Wine: if install_python_deps.sh exists and .wine_deps_installed is missing, runs that script first
                         (host bash → wine pip). Skip: LIVE_VOCODER_SKIP_WINE_AUTO_DEPS=1. Force reinstall: LIVE_VOCODER_WINE_DEPS_ALWAYS=1
                         (LIVE_VOCODER_WINE_DEPS_ALWAYS=1 = every launch; else delete .wine_deps_installed to retry once).
                         With no args, Wine runs host uname -s: Darwin → run-mac.command; Linux → run.sh --gtk-gui (if present).
                         Env LIVE_VOCODER_GUI=gtk|tk|web. --minimal-cpp = C++ only.
python/                — Official Python ${PY_VER} embeddable (amd64) from python.org.

Python app (GTK / web / Tk / CLI):
  1) Install pip packages once (network):
     Windows:  install_python_deps.bat
     Linux+Wine (this folder):  ./install_python_deps.sh   (uses wine64/wine + python\\python.exe)
  2) Web UI: double-click LiveVocoder.exe or run_web_gui.bat / ./run_web_gui.sh (Wine).
     GTK UI: run_gtk_gui.bat / ./run_gtk_gui.sh  OR  run_live_vocoder.bat --gtk-gui
     Env: LIVE_VOCODER_GUI=web|gtk|tk for LiveVocoder.exe
  App .py files sit next to the .exe (run_portable.py omitted — Linux helper).

Wine on Linux (avoid "Invalid handle" / "Bad format")
-----------------------------------------------------
Use wine64 for this 64-bit MinGW exe (plain wine may use the 32-bit loader and fail).

  cd cpp/dist-windows-cross
  wine64 ./LiveVocoder.exe --minimal-cpp Z:/home/you/song.f32

Map a real .f32 path: Wine sees Linux files as Z:\\home\\...  Use a real file, not a placeholder.

From repo root, ./run-wine.sh prefers wine64 for LiveVocoder.exe.

Windows embeddable Python under Wine is fragile (pip/deps, GUI). Prefer native host UI: Linux ./run.sh --gtk-gui;
  macOS ./run-mac.command (or double-click run-mac.command in Finder). If you test under Wine anyway, read python_last_run.log after a failure.

Re-run bundle on Linux after changing LV_WIN_PY_VERSION:
  LV_WIN_PY_VERSION=3.12.8 ./bundle-dist-windows-cross-python.sh

Windows installer (Inno Setup 6, on a PC with ISCC):
  From cpp/: build-installer.bat after this bundle — produces dist-installer/LiveVocoder_Setup_*.exe
EOF

echo "Bundled embeddable Python ${PY_VER} into ${DIST}/python"
echo "Copied app .py files and requirements.txt; see ${DIST}/README_PORTABLE.txt"
