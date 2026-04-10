@echo off
setlocal
rem QEMU user networking exposes the host folder as \\10.0.2.4\qemu
set "SRC=\\10.0.2.4\qemu\LiveVocoder-Setup-Windows.exe"
set "DEST=%TEMP%\LiveVocoder-Setup-Windows.exe"
echo Copying LiveVocoder-Setup-Windows.exe from host share...
copy /Y "%SRC%" "%DEST%" >nul
if errorlevel 1 (
  echo Could not read %SRC%
  echo Open Explorer to \\10.0.2.4\qemu and run LiveVocoder-Setup-Windows.exe from there.
  pause
  exit /b 1
)
echo Starting installer...
start "" "%DEST%"
