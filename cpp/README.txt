C++ executable (SDL2 GUI) + optional Python app
==============================================

**Optional:** edit the same C++ sources under ``../LiveVocoder/`` (minimal tree), then
``LiveVocoder/sync-to-cpp.sh`` to refresh ``cpp/src`` before Windows bundles / CI.

CMake on Linux / macOS — use this layout
-----------------------------------------
  cd cpp
  cmake -B build .
  cmake --build build
  cmake --build build --target inno_installer   # prints help only; see below

Do not run ``cmake -B build .`` from inside ``cpp/build`` (that nests a wrong build tree).

**Inno Setup .exe:** Installers are built **only on Windows** with ISCC (or Wine + Inno on Linux).
On Linux, ``cmake --build … --target inno_installer`` does not write under ``dist-installer/``.
Full portable tree: ``LiveVocoder_Setup_*.exe`` (``installer/LiveVocoder.iss``).
**Minimal C++ only** (exe + DLLs, no Python): ``LiveVocoder_Cpp_Setup_*.exe`` via
``build-installer-minimal.sh`` / ``build-installer-minimal.bat`` and ``installer/LiveVocoderCppMinimal.iss``.
Download artifact **LiveVocoder-setup** from GitHub Actions, or read ``cpp/installer/README-LINUX.txt``.

The built binary is always named ``LiveVocoder.exe`` (under ``cpp/build/`` or your CMake output dir). On Windows / MinGW it is a PE; on Linux/macOS it is still a native executable with an ``.exe`` suffix for consistency. Use the MinGW cross build under ``dist-windows-cross/`` when you need a Windows PE for real Windows or Wine.

- **Default** (no arguments): **SDL2 + PortAudio** vocoder GUI (C++ only — no GTK/Tk/web from this binary).
- **Automation / headless SDL:** ``LIVE_VOCODER_SDL_SKIP_STARTUP_MODALS=1`` skips the welcome and font-warning message boxes (still shows error dialogs). Pair with ``SDL_VIDEODRIVER=dummy`` for CI smoke tests. Linux CMake build copies ``fonts/DejaVuSans.ttf`` next to ``LiveVocoder.exe`` when found under ``/usr/share/fonts``.
- **``<audio|.f32>``** as the only argument, or **``--sdl-gui``** [optional path]: same SDL GUI (non-.f32 needs **ffmpeg** on PATH).
- **``--minimal-cpp <audio|.f32> [sr]``**: headless C++ band vocoder (ffmpeg converts to mono f32le at ``sr``, default 48000).

GTK / web / Tk: run ``python3 live_vocoder.py …`` from the repo (separate from the C++ exe).

Carriers: raw ``.f32`` is mono little-endian float32 at the vocoder sample rate; any other format is converted with ``ffmpeg -f f32le -ac 1 -ar 48000`` (SDL saves the result under ``Documents/LiveVocoderCarriers``). On each SDL launch, that folder (and ``LiveVocoderCarriers`` next to the exe when it differs) is scanned: known audio files are converted to sibling ``.f32`` if the ``.f32`` is missing or older.

Optional CMake: ``-DLIVE_VOCODER_CPP_SDL_GUI=OFF`` for a build with only ``--minimal-cpp`` (no SDL2).

Dependencies (Linux): build-essential cmake pkg-config libfftw3-dev portaudio19-dev
  libsdl2-dev libsdl2-ttf-dev, and **ffmpeg** for non-.f32 carriers

**Virtual microphone (Discord, etc.):** Live Vocoder does not register a kernel driver. Use a **null or virtual sink** (PipeWire/Pulse: e.g. KDE’s “Null Output” / ``Test:Null``), route this app’s **playback** to that sink, and in Discord set input to **Monitor of** that sink (or a remapped ``*_mic`` source).

**Native Linux C++ (SDL):** On startup the app runs **``pactl``** (unless ``LIVE_VOCODER_AUTO_VIRT_MIC=0``) to ensure a **``live_vocoder*``** null sink and a remapped **``*_mic``** input exist — same idea as Python GTK. If ``PULSE_SINK`` is unset, it is set to that sink when it already exists (or after creation). You can still set **``LIVE_VOCODER_PULSE_SINK``** / **``LIVE_VOCODER_PULSE_DESCRIPTION``** explicitly. The SDL **status** line (and stderr for ``--minimal-cpp``) summarizes **``pactl``** (sink / ``*_mic`` / ``*.monitor``). If ``LIVE_VOCODER_PA_OUTPUT`` is unset, **``LIVE_VOCODER_PULSE_SINK``** is also used as a **substring** match on output device names.

PortAudio often sees ALSA names; list and pick output by substring::

  LIVE_VOCODER_PA_LIST_DEVICES=1 ./LiveVocoder.exe
  LIVE_VOCODER_PA_OUTPUT=Null ./LiveVocoder.exe
  LIVE_VOCODER_PULSE_SINK=live_vocoder ./LiveVocoder.exe
  LIVE_VOCODER_PA_INPUT=HyperX ./LiveVocoder.exe   # optional mic match

Use ``LIVE_VOCODER_PA_OUTPUT_INDEX=N`` / ``LIVE_VOCODER_PA_INPUT_INDEX=N`` if you prefer indices from the list. Output device must expose **at least 2 playback channels** (stereo).

**Wine .exe + OBS on Linux:** The MinGW ``LiveVocoder.exe`` under Wine sends audio to the **host default** Pulse/PipeWire sink unless you override it. If OBS uses **``live_vocoder*_mic``**, that source only carries audio when something plays into the matching **null sink** — **Wine does not do that automatically**. Fix one of:
  - Start Wine/CrossOver with **``PULSE_SINK=live_vocoder2``** (match your sink name from ``pactl list short sinks``), **or**
  - **pavucontrol → Playback:** while LiveVocoder runs, move its stream to the **LiveVocoder** null sink, **or**
  - Add OBS **Application Audio Capture (PipeWire)** and pick the **Wine** stream (not ``*_mic``).
Native **ELF** ``cpp/build/LiveVocoder.exe`` sets ``PULSE_SINK`` / null sink for you — use that if you want ``*_mic`` without manual routing.

To capture vocoder audio in OBS:

- **PipeWire (recommended):** Add **Application Audio Capture (PipeWire)** (OBS 28+), start Live Vocoder under Wine, then choose the **Wine / LiveVocoder** stream in the source properties. **To hear yourself (monitoring):** in the mixer, **Advanced Audio Properties** for that source → **Audio Monitoring** = **Monitor and Output** (or **Monitor Only**). **Settings → Audio → Advanced → Monitoring Device** must be your real headphones/speakers (not “Default” if that is wrong). In **pavucontrol → Playback**, confirm the **OBS** stream is unmuted and routed to the device you wear.
- Alternatively use **Helvum** / **qpwgraph** to wire Wine’s output node into a dedicated sink and record **Monitor of** that sink via **Audio Input Capture**.
- **PulseAudio-style UI:** Open **pavucontrol** (or KDE Plasma’s **Audio Volume** → **Applications**) while the vocoder is playing, move the **Wine** playback stream to a **Null Output** or other sink, then in OBS add **Audio Input Capture** and select **Monitor of** that sink (e.g. “Monitor of Test:Null”).

**Easier than Wine for streaming:** Run the **native Linux** binary ``cpp/build/LiveVocoder.exe`` (it is an ELF file with an ``.exe`` name). It shows up as a normal PipeWire/Pulse application, and **Application Audio Capture** can target it directly.

**Note:** ``LIVE_VOCODER_PA_LIST_DEVICES`` / ``LIVE_VOCODER_PA_OUTPUT`` list **Windows** device names *inside* Wine (WASAPI/DirectSound), not Linux’s OBS device names — they help on real Windows more than for OBS routing on Linux.

Windows .exe (same PE runs on Windows and under Wine)
-------------------------------------------------------
  Cross-compile from Linux (MinGW-w64): from ``cpp/`` run
    ./build-cross-windows.sh
  That builds the .exe, runs ``bundle-cross-dlls.sh``, then by default
  ``bundle-dist-windows-cross-python.sh`` (embeddable Python + repo ``*.py``; needs network).
  Offline / C++-only:  LV_BUNDLE_PYTHON=0 ./build-cross-windows.sh
  You can still run ``./bundle-cross-dlls.sh`` or ``./bundle-dist-windows-cross-python.sh`` alone if needed.
  Arch: ``pacman -S mingw-w64-gcc mingw-w64-binutils`` then AUR
  ``yay -S mingw-w64-fftw mingw-w64-portaudio mingw-w64-pkg-config`` (those three
  are not in official repos). See ``build-cross-windows.sh`` header; Fedora differs.
  Output: ``dist-windows-cross/`` — exe, MinGW DLLs, ``python/``, ``*.py``, batch helpers.
  Clean cross outputs:  ./clean-cross-artifacts.sh   (removes dist-windows-cross, build-mingw-cross, dist-installer)
  Python zip version:  ``LV_WIN_PY_VERSION=3.12.8`` (default) when bundling.

  **Wine (Linux):** ``./run-wine-cross-exe.sh`` or ``cd dist-windows-cross && wine ./LiveVocoder.exe``.
  Many distros ship only ``wine`` (no ``wine64`` binary); that is fine for 64-bit PEs.
  **ffmpeg:** The Windows exe runs **ffmpeg.exe**, not Linux ``/usr/bin/ffmpeg``. If ``build-cross-windows.sh``
  found ``${MINGW_ROOT}/bin/ffmpeg.exe``, it is copied next to ``LiveVocoder.exe``. Otherwise run
  ``./fetch-windows-ffmpeg.sh`` (downloads BtbN win64 GPL build into ``dist-windows-cross/``), place any
  Windows **ffmpeg.exe** beside ``LiveVocoder.exe``, or set ``LIVE_VOCODER_FFMPEG`` to its full path.
  **Raw .f32** carriers work without ffmpeg.

  **No black console (Wine / Windows):** The SDL GUI ``.exe`` is linked as a **Windows GUI** app
  (MinGW ``-mwindows``, MSVC ``/SUBSYSTEM:WINDOWS``), so Wine and CrossOver do not spawn the extra
  console window. The headless ``--minimal-cpp`` mode has no console either; use redirection or a
  terminal wrapper if you need ``stderr`` on Windows.

  **Audio on Windows:** There is no compile-time sound driver. At **runtime**,
  PortAudio asks Windows for the **default host API** (usually **WASAPI**) and
  **default input/output** devices — the same defaults as the Sound control panel.
  ``--minimal-cpp`` prints those names when the stream starts.

  Local (on Windows): MSYS2 MINGW64 +  cpp/build-mingw.bat  (or cmake + bash bundle-mingw.sh)
  CI:    GitHub Actions → "Build C++ Windows EXE" → artifacts:
           * LiveVocoder-windows — portable folder under ``dist-windows-cross/`` (``LiveVocoder.exe`` + DLLs + embeddable Python + ``*.py``)
           * LiveVocoder-setup — Inno Setup installer ``LiveVocoder_Setup_*.exe`` (installs that tree to Program Files)
  Inno (local): after build-mingw.bat, install Inno Setup 6 from https://jrsoftware.org/isinfo.php
  then either:
    * cpp\build-installer.bat
    * bash cpp/build-installer.sh   (Git Bash / MSYS2)
    * cmake --build cpp/build --target inno_installer   (Windows: if CMake found ISCC)
  On Linux/macOS: use forward slashes, e.g.  cd cpp/build  not  cd cpp\\build
  The Inno compiler is Windows-only; on Linux use the CI artifact LiveVocoder-setup.
  Script: cpp/installer/LiveVocoder.iss → cpp/dist-installer/LiveVocoder_Setup_*.exe
  Minimal: cpp/build-installer-minimal.sh → LiveVocoder_Cpp_Setup_*.exe (stage ``dist-windows-installer-minimal/`` first)

  Wine:  use the ``dist-windows-cross/`` zip (exe + DLLs + optional ``python/``), then e.g.
           ``cd cpp/dist-windows-cross && wine64 ./LiveVocoder.exe``
         or ``./run-wine.sh`` from the repo. For C++-only testing, ``--minimal-cpp`` needs no Python tree.

Optional future work: libsndfile or miniaudio for WAV/MP3, PipeWire via C API,
match every Python knob inside the C++ engine (reverb, phase mode, …).
