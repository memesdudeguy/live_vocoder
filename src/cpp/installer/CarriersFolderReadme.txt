Live Vocoder — carrier audio folder
===================================

This folder lives under your Windows Documents directory (the shell “My Documents”
path, including OneDrive or other relocations). It is created by the installer and
by the app on first run if missing.

Drag and drop
  Drop WAV, MP3, FLAC, or other supported files onto the Live Vocoder window.
  The app converts them (needs ffmpeg.exe next to LiveVocoder.exe or on PATH) and
  saves mono 48 kHz float32 files here as .f32.

Library
  Use "Library…" in the app to pick a carrier already in this folder.

Pre-converted carriers
  You can copy mono 48 kHz raw float32 .f32 files here, or drop them on the window.

See README.txt next to LiveVocoder.exe for audio device hints (PortAudio / Pulse).
