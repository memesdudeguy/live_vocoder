# MinGW-w64 cross-compile: Linux host -> Windows x86_64 PE (.exe)
# Use with:  cmake -DCMAKE_TOOLCHAIN_FILE=cmake/mingw-w64-cross.cmake -DMINGW_ROOT=...
#
# Arch: pacman -S mingw-w64-gcc mingw-w64-binutils
#       yay -S mingw-w64-fftw mingw-w64-portaudio mingw-w64-pkg-config   (AUR)
# Typical root:    /usr/x86_64-w64-mingw32

if(NOT DEFINED MINGW_ROOT OR MINGW_ROOT STREQUAL "")
    set(MINGW_ROOT "/usr/x86_64-w64-mingw32")
endif()

set(CMAKE_SYSTEM_NAME Windows)
set(CMAKE_SYSTEM_PROCESSOR AMD64)

set(CMAKE_C_COMPILER x86_64-w64-mingw32-gcc)
set(CMAKE_CXX_COMPILER x86_64-w64-mingw32-g++)
set(CMAKE_RC_COMPILER x86_64-w64-mingw32-windres)
set(CMAKE_FIND_ROOT_PATH "${MINGW_ROOT}")
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)

# Prefer MinGW headers/libs; without this, plain pkg-config may return host /usr/include fftw3.
set(ENV{PKG_CONFIG_LIBDIR} "${MINGW_ROOT}/lib/pkgconfig")
set(ENV{PKG_CONFIG_PATH} "")
set(ENV{PKG_CONFIG_SYSROOT_DIR} "${MINGW_ROOT}")
