@echo off
REM GTK 4 UI — requires PyGObject/GTK in python\ (Windows or Wine).
setlocal
cd /d "%~dp0"
set "LIVE_VOCODER_GUI=gtk"
"%~dp0LiveVocoder.exe"
endlocal
