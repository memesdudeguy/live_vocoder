Live Vocoder — C++ snapshot (no Python app)
============================================

cpp/
  CMake-based native GUI (SDL2 and optional Qt6). See cpp/README.txt for dependencies,
  PipeWire notes, and PortAudio env vars.

scripts/
  Shell helpers copied from the full repo (device listing, Wine, virt-mic smoke tests).
  The full repo’s run.sh uses Python; this snapshot does not include it.

Build (Linux)
-------------
  cd cpp
  cmake -B build .
  cmake --build build
  ./build/LiveVocoder.exe

Windows installer / cross-build: see cpp/README.txt and cpp/installer/README-LINUX.txt.

Icon: installer/LiveVocoder.ico is included. Regenerating it from assets requires Python
(omitted here); edit assets and use the upstream gen_livevocoder_ico.py if needed.
