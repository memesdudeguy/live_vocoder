@echo off
setlocal
rem One-shot: sine WAV -> LiveVocoder --carrier-pipeline-test (ffmpeg + load .f32, no mic/speakers).
rem Run from the folder that contains LiveVocoder.exe and ffmpeg.exe (QEMU \\10.0.2.4\qemu share or Program Files).
set "HERE=%~dp0"
cd /d "%HERE%"
if not exist "LiveVocoder.exe" (
  echo ERROR: LiveVocoder.exe not found in %HERE% >&2
  exit /b 2
)
if not exist "ffmpeg.exe" (
  echo ERROR: ffmpeg.exe not found in %HERE% >&2
  exit /b 2
)
set "WAV=%TEMP%\lvoc_vm_carrier_sine.wav"
"%HERE%ffmpeg.exe" -y -hide_banner -loglevel error -f lavfi -i sine=frequency=440:duration=1 -ac 1 -ar 48000 "%WAV%"
if errorlevel 1 (
  echo ERROR: ffmpeg could not write test WAV >&2
  exit /b 3
)
"%HERE%LiveVocoder.exe" --carrier-pipeline-test "%WAV%" 48000
set "RC=%errorlevel%"
del /f /q "%WAV%" 2>nul
exit /b %RC%
