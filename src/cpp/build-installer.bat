@echo off
setlocal
REM Build Inno Setup installer. Packages dist-windows-cross (preferred) or dist-windows.
REM Full app: embeddable Python + *.py — run bundle-dist-windows-cross-python.sh into dist-windows-cross.
cd /d "%~dp0"
if not exist "dist-installer" mkdir "dist-installer"

set "PORTABLE=dist-windows-cross"
if not exist "%PORTABLE%\LiveVocoder.exe" set "PORTABLE=dist-windows"
if not exist "%PORTABLE%\LiveVocoder.exe" (
  echo Missing dist-windows-cross\LiveVocoder.exe or dist-windows\LiveVocoder.exe. >&2
  echo Build: build-cross-windows.sh or MSYS2 cmake + bundle-mingw.sh >&2
  exit /b 1
)
if not exist "%PORTABLE%\python\python.exe" (
  echo WARNING: No %PORTABLE%\python\python.exe — installer will not include bundled Python GUI. >&2
  echo For a full installer run: bash bundle-dist-windows-cross-python.sh  ^(from cpp/^) >&2
)

set "ISCC="
if exist "%ProgramFiles(x86)%\Inno Setup 6\ISCC.exe" set "ISCC=%ProgramFiles(x86)%\Inno Setup 6\ISCC.exe"
if not defined ISCC if exist "%ProgramFiles%\Inno Setup 6\ISCC.exe" set "ISCC=%ProgramFiles%\Inno Setup 6\ISCC.exe"
where ISCC >nul 2>&1 && for /f "delims=" %%i in ('where ISCC') do set "ISCC=%%i"

if not defined ISCC (
  echo Inno Setup 6 not found. Install from https://jrsoftware.org/isdl.php >&2
  echo Expected: "%ProgramFiles(x86)%\Inno Setup 6\ISCC.exe" >&2
  exit /b 1
)

REM SetupIconFile=LiveVocoder.ico — generate if absent (committed ico skips Python)
if not exist "%~dp0installer\LiveVocoder.ico" (
  python "%~dp0installer\gen_livevocoder_ico.py" 2>nul
  if errorlevel 1 py -3 "%~dp0installer\gen_livevocoder_ico.py" 2>nul
)
if not exist "%~dp0installer\LiveVocoder.ico" (
  echo Missing installer\LiveVocoder.ico — run: python installer\gen_livevocoder_ico.py >&2
  exit /b 1
)

"%ISCC%" "%~dp0installer\LiveVocoder.iss"
if errorlevel 1 exit /b 1
echo Installer: dist-installer\LiveVocoder_Setup_*.exe
endlocal
