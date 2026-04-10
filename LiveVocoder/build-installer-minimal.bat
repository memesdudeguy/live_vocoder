@echo off
REM Windows CMD only — on Linux use:  ./build-installer-minimal.sh
setlocal
cd /d "%~dp0"
if not exist "dist-installer" mkdir "dist-installer"

call "%~dp0bundle-installer-minimal.bat"
if errorlevel 1 exit /b 1

if not exist "%~dp0installer\LiveVocoder.ico" (
  python "%~dp0installer\gen_livevocoder_ico.py" 2>nul
  if errorlevel 1 py -3 "%~dp0installer\gen_livevocoder_ico.py" 2>nul
)
if not exist "%~dp0installer\LiveVocoder.ico" (
  echo Missing installer\LiveVocoder.ico — run: python installer\gen_livevocoder_ico.py >&2
  exit /b 1
)

set "ISCC="
if exist "%ProgramFiles(x86)%\Inno Setup 6\ISCC.exe" set "ISCC=%ProgramFiles(x86)%\Inno Setup 6\ISCC.exe"
if not defined ISCC if exist "%ProgramFiles%\Inno Setup 6\ISCC.exe" set "ISCC=%ProgramFiles%\Inno Setup 6\ISCC.exe"
where ISCC >nul 2>&1 && for /f "delims=" %%i in ('where ISCC') do set "ISCC=%%i"

if not defined ISCC (
  echo Inno Setup 6 not found. https://jrsoftware.org/isdl.php >&2
  exit /b 1
)

"%ISCC%" "%~dp0installer\LiveVocoderCppMinimal.iss"
if errorlevel 1 exit /b 1
echo Installer: dist-installer\LiveVocoder_Cpp_Setup_*.exe
endlocal
