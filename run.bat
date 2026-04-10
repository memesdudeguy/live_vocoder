@echo off
setlocal
REM Or use dist\RunLiveVocoder.exe (build: packaging\build-launcher-exe.bat). Linux: ./run-wine.sh
cd /d "%~dp0"
set "PY=py -3"
where py >nul 2>&1 || set "PY=python"

if not exist ".venv\Scripts\python.exe" (
  echo Creating .venv and installing dependencies...
  %PY% -m venv .venv || exit /b 1
  call .venv\Scripts\activate.bat
  python -m pip install -U pip
  pip install -r requirements.txt || exit /b 1
) else (
  call .venv\Scripts\activate.bat
)

REM Web UI works everywhere; install VB-Audio Cable / BlackHole and use --virt-mic for a "virtual mic".
python live_vocoder.py --web-gui %*
