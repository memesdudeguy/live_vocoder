@echo off
setlocal EnableExtensions EnableDelayedExpansion
rem Loop until carrier ffmpeg->f32->load succeeds (or max tries). For Windows VM / QEMU SMB testing.
rem Env (optional): LV_VM_F32_MAX_TRIES=0 means unlimited (default 0). LV_VM_F32_RETRY_SEC=seconds between tries (default 15).
set "HERE=%~dp0"
cd /d "%HERE%"
if not defined LV_VM_F32_MAX_TRIES set "LV_VM_F32_MAX_TRIES=0"
if not defined LV_VM_F32_RETRY_SEC set "LV_VM_F32_RETRY_SEC=15"
set /a N=0
:loop
set /a N+=1
echo [%date% %time%] carrier-pipeline-test attempt !N! ...
call "%HERE%Test-CarrierF32-VM-Once.bat"
if errorlevel 1 goto failed
echo.
echo ========== SUCCESS: carrier .f32 pipeline OK ==========
exit /b 0
:failed
if not "%LV_VM_F32_MAX_TRIES%"=="0" (
  if !N! geq %LV_VM_F32_MAX_TRIES% (
    echo Giving up after %LV_VM_F32_MAX_TRIES% attempts.
    exit /b 1
  )
)
echo Retrying in %LV_VM_F32_RETRY_SEC%s...  Ctrl+C to stop
timeout /t %LV_VM_F32_RETRY_SEC% /nobreak >nul
goto loop
