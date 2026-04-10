@echo off
setlocal
REM Builds RunLiveVocoder.exe — same role as run.bat (venv + web UI).
REM Run from repo root. Copy RunLiveVocoder.exe next to live_vocoder.py on another PC
REM (Python 3.10+ on PATH needed once to create .venv).
cd /d "%~dp0.."

set "PY=py -3"
where py >nul 2>&1 || set "PY=python"

%PY% -m pip install -q pyinstaller
%PY% -m PyInstaller packaging\PortableLauncher.spec --clean --noconfirm

echo.
if exist "dist\RunLiveVocoder.exe" (
  echo Built: dist\RunLiveVocoder.exe
  for %%F in ("dist\RunLiveVocoder.exe") do echo Full path: %%~fF
) else (
  echo ERROR: dist\RunLiveVocoder.exe not found — build must run on Windows.
)
echo The .exe may sit in dist\; it walks up folders to find live_vocoder.py.
echo No Windows PC? GitHub: Actions ^> "Build Windows EXE" ^> download artifact RunLiveVocoder-exe.
echo Linux/macOS + Wine: ./run-wine.sh
endlocal
