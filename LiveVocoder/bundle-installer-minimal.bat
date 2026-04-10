@echo off
setlocal EnableDelayedExpansion
cd /d "%~dp0"
set "SRC=dist-windows-cross"
if not exist "%SRC%\LiveVocoder.exe" set "SRC=..\cpp\dist-windows-cross"
if not exist "%SRC%\LiveVocoder.exe" set "SRC=dist-windows"
if not exist "%SRC%\LiveVocoder.exe" (
  echo Missing LiveVocoder.exe — run build-cross-windows.sh here or build in cpp\. >&2
  exit /b 1
)
if not exist "dist-windows-installer-minimal" mkdir "dist-windows-installer-minimal"
if not exist "dist-windows-installer-minimal\fonts" mkdir "dist-windows-installer-minimal\fonts"
del /q "dist-windows-installer-minimal\*.dll" 2>nul
copy /Y "%SRC%\LiveVocoder.exe" "dist-windows-installer-minimal\" >nul
for %%f in ("%SRC%\*.dll") do copy /Y "%%f" "dist-windows-installer-minimal\" >nul
if exist "installer\LiveVocoder.ico" copy /Y "installer\LiveVocoder.ico" "dist-windows-installer-minimal\" >nul
if exist "%SRC%\app-icon.png" (
  copy /Y "%SRC%\app-icon.png" "dist-windows-installer-minimal\" >nul
) else if exist "assets\app-icon.png" (
  copy /Y "assets\app-icon.png" "dist-windows-installer-minimal\" >nul
)
if exist "%SRC%\fonts\DejaVuSans.ttf" copy /Y "%SRC%\fonts\DejaVuSans.ttf" "dist-windows-installer-minimal\fonts\" >nul
if exist "%SRC%\ffmpeg.exe" copy /Y "%SRC%\ffmpeg.exe" "dist-windows-installer-minimal\" >nul

set "README=dist-windows-installer-minimal\README_Cpp_Minimal.txt"
> "%README%" echo Live Vocoder — C++ SDL build ^(this installer^)
>>"%README%" echo.
>>"%README%" echo Included: LiveVocoder.exe, runtime DLLs, optional DejaVu font and ffmpeg.

echo Staged: dist-windows-installer-minimal\
dir /b "dist-windows-installer-minimal"
endlocal
