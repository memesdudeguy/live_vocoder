@echo off
setlocal
rem QEMU user networking exposes the host folder as \\10.0.2.4\qemu
set "SRC=\\10.0.2.4\qemu\LiveVocoder-Setup.exe"
set "DEST=%TEMP%\LiveVocoder-Setup.exe"
echo Copying LiveVocoder-Setup.exe from host share...
copy /Y "%SRC%" "%DEST%" >nul
if errorlevel 1 (
  echo Could not read %SRC%
  echo Open Explorer to \\10.0.2.4\qemu and run LiveVocoder-Setup.exe from there.
  pause
  exit /b 1
)
echo Starting installer...
start "" "%DEST%"
