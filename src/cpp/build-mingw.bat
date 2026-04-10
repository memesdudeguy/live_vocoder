@echo off
setlocal
REM Build LiveVocoder.exe with MSYS2 MINGW64 (install: pacman -S mingw-w64-x86_64-{gcc,cmake,ninja,pkg-config,fftw,portaudio})
REM Run this from cmd after adding C:\msys64\mingw64\bin to PATH, or run the commands inside MSYS2 MinGW64 shell.

where cmake >nul 2>&1 || (
  echo Add MSYS2 MinGW64 to PATH, e.g. set PATH=C:\msys64\mingw64\bin;%%PATH%%
  exit /b 1
)

cd /d "%~dp0"
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release .
cmake --build build
if errorlevel 1 exit /b 1

bash bundle-mingw.sh
if errorlevel 1 (
  echo If bash is missing, in MSYS2 MinGW64 run:  bash cpp/bundle-mingw.sh
  exit /b 1
)
echo Output folder: dist-windows\  ^(exe + DLLs; copy entire folder for Wine / minimal C++^)
echo Full GUI bundle: bash bundle-dist-windows-cross-python.sh  ^(fills dist-windows-cross\ — use for Inno^)
echo Optional: build-installer.bat  ^(Inno Setup 6 — dist-installer\LiveVocoderCpp_Setup_*.exe^)
endlocal
