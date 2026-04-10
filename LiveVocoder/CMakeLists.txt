# Standalone build (sources live in this folder). For Inno installers use ../cpp/ + its installer/.
cmake_minimum_required(VERSION 3.16)
project(LiveVocoder LANGUAGES CXX)

find_program(LIVE_VOCODER_PYTHON3 NAMES python3 python)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

option(LIVE_VOCODER_CPP_SDL_GUI "SDL2 + PortAudio vocoder GUI" ON)

find_package(PkgConfig REQUIRED)
pkg_check_modules(FFTW REQUIRED fftw3)
pkg_check_modules(PORTAUDIO REQUIRED portaudio-2.0)

add_executable(live_vocoder_cpp
    src/main.cpp
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

set_target_properties(live_vocoder_cpp PROPERTIES OUTPUT_NAME "LiveVocoder")
if(NOT WIN32)
    set_target_properties(live_vocoder_cpp PROPERTIES SUFFIX ".exe")
endif()

if(CMAKE_SYSTEM_NAME STREQUAL "Windows")
    if(LIVE_VOCODER_CPP_SDL_GUI)
        if(MSVC)
            target_link_options(live_vocoder_cpp PRIVATE "/SUBSYSTEM:WINDOWS")
        elseif(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
            target_link_options(live_vocoder_cpp PRIVATE "-mwindows")
        endif()
    endif()
    target_sources(live_vocoder_cpp PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/installer/live_vocoder.rc")
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
