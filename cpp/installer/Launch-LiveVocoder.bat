@echo off
setlocal
rem After installing from LiveVocoder-Setup.exe (default: Program Files\Live Vocoder)
set "APP=%ProgramFiles%\Live Vocoder\LiveVocoder.exe"
if exist "%APP%" (
  start "" /D "%ProgramFiles%\Live Vocoder" "%APP%"
  exit /b 0
)
set "APP=%LocalAppData%\Programs\Live Vocoder\LiveVocoder.exe"
if exist "%APP%" (
  for %%I in ("%APP%") do start "" /D "%%~dpI" "%APP%"
  exit /b 0
)
echo Live Vocoder is not installed in the usual folders.
echo Install first from \\10.0.2.4\qemu\LiveVocoder-Setup.exe or Run-from-QEMU-share.bat
pause
exit /b 1
