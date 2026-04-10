LiveVocoder — C++ SDL tree (sources + Windows setup)
======================================================

Native (Linux / macOS):

  cmake -B build -S .
  cmake --build build
  ./build/LiveVocoder.exe

Windows PE + minimal Setup.exe (from Linux + MinGW + Inno)
-----------------------------------------------------------
  ./build-cross-windows.sh          # → dist-windows-cross/ (exe + DLLs, no Python)
  ./build-installer-minimal.sh      # → dist-installer/LiveVocoder_Cpp_Setup_*.exe   (Linux / Git Bash)

  On Linux do not run build-installer-minimal.bat — that is for Windows CMD only.
  "Permission denied" on .bat here is normal; use ./build-installer-minimal.sh (Wine + Inno, or ISCC under Git Bash).

  Needs: same MinGW deps as ../cpp/build-cross-windows.sh (see that file’s header).
  Inno Setup 6 (ISCC) on Windows path, or Wine + Inno for ISCC.

If you built only under ../cpp/, bundle-installer-minimal still picks up
../cpp/dist-windows-cross/ automatically.

Windows PC (MSYS2 build + Inno)
-------------------------------
  After you have dist-windows-cross\ with exe + DLLs:
    build-installer-minimal.bat

Sync sources back to packaging tree
------------------------------------
  ./sync-to-cpp.sh

See installer/README-INSTALLER.txt and ../cpp/README.txt for PipeWire, ffmpeg, Wine.
