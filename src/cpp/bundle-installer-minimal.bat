@echo off
setlocal EnableDelayedExpansion
cd /d "%~dp0"
REM Stage minimal Inno payload. Prefer newest native build over stale cross artifacts:
REM   1) LV_MINIMAL_EXE — directory of that PE exe (and its *.dll)
REM   2) build\, build\Release\, build\Debug\ — Ninja single-config or Visual Studio
REM   3) dist-windows\ — after bundle-mingw / native bundle
REM   4) dist-windows-cross\ — Linux cross-build drop-in
set "SRC="
set "LV_EXE="
if not "%LV_MINIMAL_EXE%"=="" if exist "%LV_MINIMAL_EXE%" (
  for %%I in ("%LV_MINIMAL_EXE%") do set "SRC=%%~dpI"
  if "!SRC:~-1!"=="\" set "SRC=!SRC:~0,-1!"
  set "LV_EXE=%LV_MINIMAL_EXE%"
  goto :src_ok
)
if exist "build\LiveVocoder.exe" set "SRC=build"
if defined SRC goto :src_ok
if exist "build\Release\LiveVocoder.exe" set "SRC=build\Release"
if defined SRC goto :src_ok
if exist "build\Debug\LiveVocoder.exe" set "SRC=build\Debug"
if defined SRC goto :src_ok
if exist "dist-windows\LiveVocoder.exe" set "SRC=dist-windows"
if defined SRC goto :src_ok
if exist "dist-windows-cross\LiveVocoder.exe" set "SRC=dist-windows-cross"
if defined SRC goto :src_ok
echo No Windows LiveVocoder.exe found. Build first, e.g.: >&2
echo   cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release . ^&^& cmake --build build >&2
echo   or MSYS2: build-mingw.bat  ^(fills dist-windows^) >&2
echo Or set LV_MINIMAL_EXE to the full path of LiveVocoder.exe. >&2
exit /b 1

:src_ok
if not exist "dist-windows-installer-minimal" mkdir "dist-windows-installer-minimal"
if not exist "dist-windows-installer-minimal\fonts" mkdir "dist-windows-installer-minimal\fonts"
del /q "dist-windows-installer-minimal\*.dll" 2>nul
if defined LV_EXE (
  copy /Y "!LV_EXE!" "dist-windows-installer-minimal\LiveVocoder.exe" >nul
) else (
  copy /Y "%SRC%\LiveVocoder.exe" "dist-windows-installer-minimal\" >nul
)
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
>>"%README%" echo.
>>"%README%" echo • Carriers folder: Windows Documents\LiveVocoderCarriers ^(shell path; installer uses user docs; created by setup and on first run^).
>>"%README%" echo   A README.txt is placed there during install. Drop audio onto the app window to convert to .f32 here.
>>"%README%" echo • Carrier files: non-.f32 formats need ffmpeg.exe next to the app or ffmpeg on PATH.
>>"%README%" echo • Text UI: Windows uses Segoe UI / Arial if fonts\DejaVuSans.ttf is missing.
>>"%README%" echo.
>>"%README%" echo Windows — audio devices
>>"%README%" echo   Choose input/output with LIVE_VOCODER_PA_INPUT / LIVE_VOCODER_PA_OUTPUT ^(name substring^) or *_INDEX.
>>"%README%" echo   LIVE_VOCODER_PA_LIST_DEVICES=1 lists PortAudio device names.

echo Staged from "%SRC%" — dist-windows-installer-minimal\
dir /b "dist-windows-installer-minimal"
endlocal
