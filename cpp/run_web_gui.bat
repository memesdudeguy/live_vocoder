@echo off
REM Browser UI (Gradio) — explicit web (same as default LiveVocoder.exe).
setlocal
cd /d "%~dp0"
set "LIVE_VOCODER_GUI=web"
"%~dp0LiveVocoder.exe"
endlocal
