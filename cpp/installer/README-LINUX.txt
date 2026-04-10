Inno Setup installer (.exe) and Linux
=====================================

The file LiveVocoder_Setup_*.exe (full portable tree) is produced ONLY on Windows by
Inno Setup's ISCC compiler. It is written to ../dist-installer/ next to this folder.

On Linux or macOS:
  - cmake --build ... --target inno_installer  will NOT create any .exe here.
  - Download the installer from GitHub Actions: workflow "Build C++ Windows EXE",
    artifact name: LiveVocoder-setup
  - Or run build-installer.bat on a Windows PC with Inno Setup 6 installed.

Minimal C++ installer (exe + DLLs only, no bundled Python):
  - From cpp/: LV_BUNDLE_PYTHON=0 ./build-cross-windows.sh   then   ./build-installer-minimal.sh
  - Or on Windows: build-installer-minimal.bat (uses bundle-installer-minimal.bat + ISCC)
  - Output: dist-installer/LiveVocoder_Cpp_Setup_<version>.exe
  - Inno script: installer/LiveVocoderCppMinimal.iss

Correct CMake layout (from repo):
  cd /path/to/live_vocoder/cpp
  cmake -B build .
  cmake --build build

Do not run "cmake -B build ." from inside cpp/build — that nests a second build
tree and confuses paths.
