@echo off
setlocal
REM Build LiveVocoder.exe (one-file) with PyInstaller. Run from repo root: live_vocoder\
cd /d "%~dp0.."

set "PY=py -3"
where py >nul 2>&1 || set "PY=python"

%PY% -m pip install -q -r requirements.txt pyinstaller
%PY% -m PyInstaller packaging\LiveVocoder.spec --clean --noconfirm

echo.
echo Output: dist\LiveVocoder.exe
echo Default double-click: web UI. CLI: LiveVocoder.exe --audio-info
echo Install ffmpeg and add to PATH for MP3 carriers.
echo Linux + Wine: copy LiveVocoder.exe to dist/ then ./run-wine.sh
endlocal
