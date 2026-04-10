Inno Setup (Windows Setup.exe) from LiveVocoder/
===============================================

1) Cross-build the PE and DLLs (Linux + MinGW):
     ./build-cross-windows.sh
   Or reuse ../cpp/dist-windows-cross/ if you built there.

2) Stage payload + compile installer:
     ./build-installer-minimal.sh
   Needs Inno Setup 6 (ISCC.exe), or Wine with Inno under ~/.wine.

Output: ../dist-installer/LiveVocoder_Cpp_Setup_0.3.0.exe

On Windows only: build-installer-minimal.bat (after you have dist-windows-cross
from MSYS2 or copied from CI).

See also ../cpp/installer/README-LINUX.txt for CI artifacts.
