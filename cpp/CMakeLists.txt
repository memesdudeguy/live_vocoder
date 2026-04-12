cmake_minimum_required(VERSION 3.16)
project(live_vocoder_cpp LANGUAGES CXX)

find_program(LIVE_VOCODER_PYTHON3 NAMES python3 python)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# C++ binary uses SDL2 + PortAudio only (no embedded Python / GTK / Tk / web from this exe).
option(LIVE_VOCODER_CPP_SDL_GUI "SDL2 + PortAudio vocoder GUI" ON)
option(LIVE_VOCODER_CPP_QT_GUI "Build LiveVocoderQt (Qt6 Widgets + level graph; optional second binary)" OFF)

find_package(PkgConfig REQUIRED)
pkg_check_modules(FFTW REQUIRED fftw3)
pkg_check_modules(PORTAUDIO REQUIRED portaudio-2.0)

add_executable(live_vocoder_cpp
    src/main.cpp
    src/gui_audio_engine.cpp
    src/vocoder.cpp
    src/carrier_convert.cpp
    src/pa_duplex.cpp
    src/linux_pulse_env.cpp
)

if(LIVE_VOCODER_CPP_SDL_GUI)
    pkg_check_modules(SDL2 REQUIRED sdl2)
    pkg_check_modules(SDL2_TTF REQUIRED SDL2_ttf)
    target_sources(live_vocoder_cpp PRIVATE src/sdl_gui.cpp)
    target_compile_definitions(live_vocoder_cpp PRIVATE LIVE_VOCODER_HAS_SDL2)
    target_include_directories(live_vocoder_cpp PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/third_party")
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/assets/app-icon.png")
        add_custom_command(TARGET live_vocoder_cpp POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${CMAKE_CURRENT_SOURCE_DIR}/assets/app-icon.png"
                "$<TARGET_FILE_DIR:live_vocoder_cpp>/app-icon.png"
            COMMENT "Copy app-icon.png next to executable for SDL window icon")
    endif()
    # Linux: ship DejaVu next to the binary so SDL TTF works without relying on distro paths alone.
    if(UNIX AND NOT APPLE AND NOT WIN32)
        set(_LV_DEJAVU_FONT "")
        if(EXISTS "/usr/share/fonts/TTF/DejaVuSans.ttf")
            set(_LV_DEJAVU_FONT "/usr/share/fonts/TTF/DejaVuSans.ttf")
        elseif(EXISTS "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf")
            set(_LV_DEJAVU_FONT "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf")
        endif()
        if(_LV_DEJAVU_FONT)
            add_custom_command(TARGET live_vocoder_cpp POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:live_vocoder_cpp>/fonts"
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    "${_LV_DEJAVU_FONT}"
                    "$<TARGET_FILE_DIR:live_vocoder_cpp>/fonts/DejaVuSans.ttf"
                COMMENT "Copy DejaVuSans.ttf for SDL GUI (fonts/ next to executable)")
        else()
            message(STATUS "SDL2: DejaVuSans.ttf not found under /usr/share/fonts — install fonts-dejavu or add fonts/ manually")
        endif()
    endif()
    if(WIN32)
        target_sources(live_vocoder_cpp PRIVATE src/sdl_win32_entry.cpp)
    else()
        target_sources(live_vocoder_cpp PRIVATE src/default_entry.cpp)
    endif()
    message(STATUS "SDL2 GUI: ON")
else()
    target_sources(live_vocoder_cpp PRIVATE src/default_entry.cpp)
    message(STATUS "SDL2 GUI: OFF")
endif()

target_include_directories(live_vocoder_cpp PRIVATE
    ${FFTW_INCLUDE_DIRS}
    ${PORTAUDIO_INCLUDE_DIRS}
    src
)

target_link_libraries(live_vocoder_cpp PRIVATE
    ${FFTW_LIBRARIES}
    ${PORTAUDIO_LIBRARIES}
)

if(LIVE_VOCODER_CPP_SDL_GUI)
    target_include_directories(live_vocoder_cpp PRIVATE ${SDL2_INCLUDE_DIRS} ${SDL2_TTF_INCLUDE_DIRS})
    # Windows: link SDL2main (sdl_win32_entry.cpp → SDL_main). Else: drop SDL2main (normal main in default_entry.cpp).
    if(WIN32)
        target_link_libraries(live_vocoder_cpp PRIVATE ${SDL2_LIBRARIES} ${SDL2_TTF_LIBRARIES})
    else()
        set(_lv_sdl2_libs "")
        foreach(_lv_x IN LISTS SDL2_LIBRARIES)
            if(_lv_x MATCHES "SDL2main")
                continue()
            endif()
            list(APPEND _lv_sdl2_libs "${_lv_x}")
        endforeach()
        target_link_libraries(live_vocoder_cpp PRIVATE ${_lv_sdl2_libs} ${SDL2_TTF_LIBRARIES})
    endif()
    target_compile_options(live_vocoder_cpp PRIVATE ${SDL2_CFLAGS_OTHER} ${SDL2_TTF_CFLAGS_OTHER})
endif()

# Output file is always LiveVocoder.exe (Windows PE or native ELF — same name for docs / scripts).
set_target_properties(live_vocoder_cpp PROPERTIES OUTPUT_NAME "LiveVocoder")
if(NOT WIN32)
    set_target_properties(live_vocoder_cpp PROPERTIES SUFFIX ".exe")
endif()
if(CMAKE_SYSTEM_NAME STREQUAL "Windows")
    target_sources(live_vocoder_cpp PRIVATE src/win_default_virt_mic.cpp src/pa_win32_monitor_out.cpp
        src/update_check_win.cpp)
    target_link_libraries(live_vocoder_cpp PRIVATE shell32 ole32 propsys oleaut32 winhttp)
    # GUI subsystem: no console window on Windows or under Wine/CrossOver (MinGW: -mwindows).
    if(LIVE_VOCODER_CPP_SDL_GUI)
        if(MSVC)
            target_link_options(live_vocoder_cpp PRIVATE "/SUBSYSTEM:WINDOWS")
        elseif(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
            target_link_options(live_vocoder_cpp PRIVATE "-mwindows")
        endif()
    endif()
    set(_lv_rc "${CMAKE_CURRENT_SOURCE_DIR}/installer/live_vocoder.rc")
    set(_lv_rc_inc "${CMAKE_CURRENT_SOURCE_DIR}/installer")
    target_sources(live_vocoder_cpp PRIVATE "${_lv_rc}")
    # windres/rc resolve ICON paths from the compile cwd; -I makes "LiveVocoder.ico" in live_vocoder.rc find the file.
    if(MINGW OR CMAKE_CXX_COMPILER_ID MATCHES "GNU")
        set_source_files_properties("${_lv_rc}" PROPERTIES COMPILE_FLAGS "-I${_lv_rc_inc}")
    elseif(MSVC)
        set_source_files_properties("${_lv_rc}" PROPERTIES COMPILE_FLAGS "/I${_lv_rc_inc}")
    endif()
    # Regenerate embedded-EXE icon before windres compiles live_vocoder.rc (needs installer/LiveVocoder.ico).
    if(LIVE_VOCODER_PYTHON3 AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/assets/app-icon.png")
        add_custom_command(
            TARGET live_vocoder_cpp
            PRE_BUILD
            COMMAND "${LIVE_VOCODER_PYTHON3}" "${CMAKE_CURRENT_SOURCE_DIR}/installer/gen_livevocoder_ico.py"
            WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
            COMMENT "Regenerate installer/LiveVocoder.ico from assets/app-icon.png"
            VERBATIM)
    endif()
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/installer/LiveVocoder.ico")
        add_custom_command(
            TARGET live_vocoder_cpp
            POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CMAKE_CURRENT_SOURCE_DIR}/installer/LiveVocoder.ico"
                    "$<TARGET_FILE_DIR:live_vocoder_cpp>/LiveVocoder.ico"
            COMMENT "Copy LiveVocoder.ico next to executable (shortcuts / shell)")
    endif()
endif()

target_compile_options(live_vocoder_cpp PRIVATE ${FFTW_CFLAGS_OTHER} ${PORTAUDIO_CFLAGS_OTHER})

if(LIVE_VOCODER_CPP_QT_GUI)
    find_package(Qt6 REQUIRED COMPONENTS Core Widgets)
    set(CMAKE_AUTOMOC ON)
    add_executable(live_vocoder_qt
        src/qt_main.cpp
        src/qt_gui.cpp
        src/gui_audio_engine.cpp
        src/vocoder.cpp
        src/carrier_convert.cpp
        src/pa_duplex.cpp
        src/linux_pulse_env.cpp
    )
    target_include_directories(live_vocoder_qt PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/src"
        "${CMAKE_CURRENT_SOURCE_DIR}/third_party"
        ${FFTW_INCLUDE_DIRS}
        ${PORTAUDIO_INCLUDE_DIRS}
    )
    target_link_libraries(live_vocoder_qt PRIVATE
        Qt6::Core
        Qt6::Widgets
        ${FFTW_LIBRARIES}
        ${PORTAUDIO_LIBRARIES}
    )
    if(CMAKE_SYSTEM_NAME STREQUAL "Windows")
        target_sources(live_vocoder_qt PRIVATE src/win_default_virt_mic.cpp src/pa_win32_monitor_out.cpp)
        target_link_libraries(live_vocoder_qt PRIVATE shell32 ole32 propsys oleaut32)
    endif()
    target_compile_options(live_vocoder_qt PRIVATE ${FFTW_CFLAGS_OTHER} ${PORTAUDIO_CFLAGS_OTHER})
    set_target_properties(live_vocoder_qt PROPERTIES OUTPUT_NAME "LiveVocoderQt")
    if(NOT WIN32)
        set_target_properties(live_vocoder_qt PROPERTIES SUFFIX ".exe")
    endif()
    message(STATUS "Qt6 GUI: ON (target live_vocoder_qt)")
endif()

# Inno Setup → dist-installer/LiveVocoderCpp_Setup_*.exe (CMake *host* Windows only).
# When cross-compiling Linux→Windows, WIN32 is true for the *target* but the host is not Windows;
# use CMAKE_HOST_WIN32 so we skip ISCC here (and avoid $ENV{ProgramFiles(x86)} — invalid in CMake).
# Needs cpp/dist-windows-cross/ or cpp/dist-windows/ populated (see installer/LiveVocoder.iss).
if(CMAKE_HOST_WIN32)
    find_program(INNO_ISCC ISCC
        HINTS
            "$ENV{ProgramFiles}/Inno Setup 6"
            "C:/Program Files (x86)/Inno Setup 6"
            "C:/Program Files/Inno Setup 6"
        DOC "Inno Setup 6 command-line compiler")
    if(INNO_ISCC)
        add_custom_target(inno_installer
            COMMAND "${INNO_ISCC}" "${CMAKE_CURRENT_SOURCE_DIR}/installer/LiveVocoder.iss"
            WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/installer"
            COMMENT "ISCC: LiveVocoder.iss → dist-installer/*.exe"
            VERBATIM)
        message(STATUS "Inno Setup: ISCC found — run: cmake --build . --target inno_installer")
    else()
        message(STATUS "Inno Setup: ISCC not found (install Inno Setup 6 for target inno_installer)")
        add_custom_target(inno_installer
            COMMAND ${CMAKE_COMMAND} -E echo "Install Inno Setup 6, then re-run cmake so ISCC is detected."
            COMMAND ${CMAKE_COMMAND} -E false
            COMMENT "inno_installer unavailable (ISCC missing)")
    endif()
else()
    # Linux/macOS: optional Wine + Inno Setup 6 inside WINEPREFIX (same as build-installer.sh).
    set(_LV_WINE_DRIVE_C "")
    if(DEFINED ENV{WINEPREFIX} AND NOT "$ENV{WINEPREFIX}" STREQUAL "")
        set(_LV_WINE_DRIVE_C "$ENV{WINEPREFIX}/drive_c")
    elseif(DEFINED ENV{HOME} AND NOT "$ENV{HOME}" STREQUAL "")
        set(_LV_WINE_DRIVE_C "$ENV{HOME}/.wine/drive_c")
    endif()
    set(_LV_INNO_ISCC "")
    if(NOT _LV_WINE_DRIVE_C STREQUAL "")
        foreach(_lv_sub
                "Program Files (x86)/Inno Setup 6/ISCC.exe"
                "Program Files/Inno Setup 6/ISCC.exe")
            if(_LV_INNO_ISCC STREQUAL "" AND EXISTS "${_LV_WINE_DRIVE_C}/${_lv_sub}")
                set(_LV_INNO_ISCC "${_LV_WINE_DRIVE_C}/${_lv_sub}")
            endif()
        endforeach()
    endif()
    find_program(LV_WINE_EXECUTABLE wine)
    if(LV_WINE_EXECUTABLE AND NOT _LV_INNO_ISCC STREQUAL "")
        add_custom_target(inno_installer
            COMMAND "${LV_WINE_EXECUTABLE}" "${_LV_INNO_ISCC}"
                    "${CMAKE_CURRENT_SOURCE_DIR}/installer/LiveVocoder.iss"
            WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/installer"
            COMMENT "Wine+ISCC: LiveVocoder.iss → dist-installer/*.exe"
            VERBATIM)
        message(STATUS "Inno Setup: Wine ISCC found — run: cmake --build . --target inno_installer")
    else()
        add_custom_target(inno_installer
            COMMAND ${CMAKE_COMMAND} -E echo ""
            COMMAND ${CMAKE_COMMAND} -E echo "=== inno_installer: no native ISCC and no Wine+Inno found ==="
            COMMAND ${CMAKE_COMMAND} -E echo "Install Inno Setup 6 in your WINEPREFIX, or run: cpp/build-installer.sh"
            COMMAND ${CMAKE_COMMAND} -E echo "CI artifacts: workflow Build C++ Windows EXE, artifact LiveVocoder-setup"
            COMMAND ${CMAKE_COMMAND} -E echo "See cpp/installer/README-LINUX.txt"
            COMMAND ${CMAKE_COMMAND} -E echo ""
            COMMENT "inno_installer needs Windows ISCC or Wine + Inno Setup 6")
    endif()
endif()
