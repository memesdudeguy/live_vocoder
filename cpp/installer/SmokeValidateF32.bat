@echo off
setlocal
rem Run from install folder or QEMU share: validates smoke_carrier.f32 loads (no audio).
set "D=%~dp0"
if not exist "%D%smoke_carrier.f32" (
  echo Missing "%D%smoke_carrier.f32" — rebuild installer payload ^(bundle-installer-minimal.sh^).
  exit /b 2
)
if not exist "%D%LiveVocoder.exe" (
  echo Missing "%D%LiveVocoder.exe"
  exit /b 2
)
"%D%LiveVocoder.exe" --validate-carrier "%D%smoke_carrier.f32" 48000
exit /b %ERRORLEVEL%
