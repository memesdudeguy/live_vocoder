@echo off
setlocal
REM Build both Windows .exe targets (same binaries work on Windows and under Wine).
REM Run from repo root (live_vocoder\).
cd /d "%~dp0.."
call packaging\build-exe.bat
call packaging\build-launcher-exe.bat
echo.
echo dist\LiveVocoder.exe       — full app (Wine-friendly: no extra Python project files)
echo dist\RunLiveVocoder.exe    — launcher (needs sources / venv next to or above exe)
endlocal
