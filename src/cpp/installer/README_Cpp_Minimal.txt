Live Vocoder — C++ SDL build (minimal installer)
==================================================

Made by memesdudeguy.

Release 6.0 — ``build-installer-minimal.sh`` / ``.bat`` compile **one** minimal installer:

- ``LiveVocoder-Setup.exe`` — use on **native Windows** or with **Wine** on Linux (Wine-only extras apply at install-time when the installer detects Wine).

On GitHub Actions, workflow “Build C++ Windows EXE” uploads it as artifact ``LiveVocoder-Cpp-setup-v6.0``.
Pushing a tag matching ``v*`` attaches it plus ``LiveVocoder_Setup_6.0.exe`` (full installer) to the Release.

What you get
------------
LiveVocoder.exe, SDL2/PortAudio/FFTW runtime DLLs, optional DejaVu font under fonts\,
and **ffmpeg.exe** (required — the minimal installer always ships it next to the .exe).
This build is the SDL GUI only (no Python/GTK/web stack). **Wine on Linux** is supported:
use ``sh-LiveVocoder-Setup.sh`` / the generated ``.desktop``, or run the .exe under Wine;
the app uses Windows ffmpeg.exe and host PipeWire via the installer’s Wine-only registry entries.
**Native Windows** can auto-route playback to VB-Audio Virtual Cable (CABLE Input) when installed; **under Wine**
that heuristic is off—use ``PULSE_SINK`` / null-sink routing on the Linux host instead.

**VB-Cable in the setup:** GitHub Actions downloads the official ``VBCABLE_Driver_Pack45.zip`` and bundles
``VBCABLE_Setup_x64.exe`` into release ``LiveVocoder-Setup.exe`` (checked wizard task on native Windows).
For local builds, place ``VBCABLE_Setup_x64.exe`` in ``cpp/installer/third_party/`` (extract from that zip
or from https://vb-audio.com/Cable/ ) before ``bundle-installer-minimal.sh``. VB-Audio is donationware.
UAC/driver prompts when running the bundled setup are normal.


Smoke test (.f32 loads without starting audio)
----------------------------------------------
- **Windows / QEMU VM:** in the install folder (or SMB share), double-click **SmokeValidateF32.bat**, or run:
  ``LiveVocoder.exe --validate-carrier "%ProgramFiles%\Live Vocoder\smoke_carrier.f32" 48000``
  Exit **0** means the carrier file and vocoder initialized (same load path as the GUI).
- **Linux dev tree:** ``./scripts/vm-wine-validate-f32-loop.sh`` (uses native ``cpp/build/LiveVocoder.exe`` when present; retries until success or max tries).

Quick start (Windows)
---------------------
1. Run ``LiveVocoder-Setup.exe`` and finish the wizard (or unzip/copy the whole folder).
2. Start Live Vocoder from the Start menu or run LiveVocoder.exe in:
   C:\Program Files\Live Vocoder\
3. Pick a carrier (see below), choose Vocode or Clean mic, then press Start.
4. Use Monitor on/off to hear output on your speakers when your routing allows it.


Carriers (voice source)
-----------------------
The app stores carriers here (created automatically):
  <Your Documents folder>\LiveVocoderCarriers\
That is the real Windows "Documents" path (including OneDrive moves). A short README.txt
is also placed there by the installer.

Ways to load a carrier:
- Drag and drop a WAV, MP3, FLAC, or other common audio file onto the window. The app
  converts it to mono 48 kHz .f32 in LiveVocoderCarriers (needs ffmpeg).
- Click "Library..." and choose an .f32 or convertible file already in that folder.
- Put a mono 48 kHz raw float32 file named something.f32 in LiveVocoderCarriers.

ffmpeg (required for non-.f32 audio)
------------------------------------
The installer normally places ffmpeg.exe next to LiveVocoder.exe. If conversion fails:

OneDrive / cloud folders: the source file must be fully downloaded (not "online-only").
Open it once in File Explorer, or use "Always keep on this device", then try again in the app.

If conversion fails:
- Ensure ffmpeg.exe sits in the same folder as LiveVocoder.exe, or
- Add a Windows ffmpeg install to your PATH, or
- Set environment variable LIVE_VOCODER_FFMPEG to the full path of ffmpeg.exe (**native Windows only**; the app
  ignores this variable under Wine — use Windows ``ffmpeg.exe`` next to ``LiveVocoder.exe``).
- **MP3 with embedded cover art** used to trip ffmpeg (“no suitable output format”); the app now passes **-vn**
  so only audio goes to the .f32 file.
- **OneDrive / Documents:** when possible the app **copies the source into %TEMP%**, runs ffmpeg on short ASCII paths,
  then moves the .f32 into LiveVocoderCarriers (helps cloud folders and long Unicode paths).

Pre-converted .f32 carriers do not need ffmpeg.


Troubleshooting carrier / ffmpeg
---------------------------------
**Native Windows**
- If the error mentions **Wine** but you are on real Windows, you are on an **old LiveVocoder.exe** — reinstall from this **6.0** setup so you get CreateProcess ffmpeg + fixed messages.
- Confirm both files exist: ``dir "C:\Program Files\Live Vocoder\LiveVocoder.exe" "C:\Program Files\Live Vocoder\ffmpeg.exe"``
- OneDrive: source audio must be **fully downloaded** (not cloud-only).
- Set ``LIVE_VOCODER_FFMPEG`` (native Windows only; ignored under Wine) to a **full Windows path** to a known-good
  ffmpeg.exe (e.g. gyan.dev / BtbN builds) and restart the app. Unicode paths are read from the user environment.

**Wine on Linux**
- Use **Windows ffmpeg.exe** next to the app (the installer provides it). Do not point Wine at ``/usr/bin/ffmpeg``.
- **6.0** runs that ffmpeg with the same **CreateProcess** path as real Windows (working directory next to ``ffmpeg.exe``, stderr captured). If conversion still fails, the dialog should show ffmpeg’s own error text — not an empty log.
- Prefer ``live-vocoder-wine-launch.sh`` or your distro’s launcher so ``PULSE_SINK`` / null sinks are set before ``wine``.
- Carrier paths under ``Z:\`` are normal; the app keeps the Wine ffmpeg command path.
- **QEMU test VM:** the host shares ``dist-installer`` as ``\\10.0.2.4\qemu``. Run ``LiveVocoder-Setup.exe`` (see ``Run-from-QEMU-share.bat``), install, then run **LiveVocoder.exe** from the Start menu or ``Program Files``.
- **Linux host:** ``wine LiveVocoder-Setup.exe``, or ``sh-LiveVocoder-Setup.sh`` / the generated ``.desktop`` next to the installer.


Main controls
-------------
Header
  Start  — open the mic + (in Vocode mode) run the vocoder on the chosen carrier.
  Stop   — stop streaming.
  Quit   — exit the app.
  ?      — in-app help summary.

Voice card
  Vocode    — needs a carrier; your mic modulates the carrier spectrum.
  Clean mic — dry microphone pass-through (no carrier).
  Monitor on/off — hear output on the playback device when routing allows it.

Quick sound (presets and sliders)
  Preset chips (Clean, Radio, Deep, Studio) set a baseline wet level and presence.
  Clarity — modulation presence (dB).
  Reverb — wet reverb send.

Meters (when streaming)
  Mic in / Output — rough level indicators. F9 sends a short test beep on the output
  path while streaming (useful for virtual cables / null sinks).


Audio devices (Windows)
-----------------------
PortAudio opens the default capture/playback devices unless you override with environment
variables (then restart the app):

  LIVE_VOCODER_PA_INPUT   substring of the mic device name, or
  LIVE_VOCODER_PA_INPUT_INDEX   zero-based input index
  LIVE_VOCODER_PA_OUTPUT  substring of the output device name, or
  LIVE_VOCODER_PA_OUTPUT_INDEX  zero-based output index
  LIVE_VOCODER_WIN_DEFAULT_VIRT_MIC  set to ``0`` to skip setting Windows default recording device to VB-Audio
  CABLE Output (default: on when VB-Cable is installed; use with ``LIVE_VOCODER_DISABLE_VB_CABLE=1`` to leave defaults alone)

List names once:
  set LIVE_VOCODER_PA_LIST_DEVICES=1
  then run LiveVocoder.exe from a Command Prompt and read the console output.

If fonts\DejaVuSans.ttf is missing, the UI still uses Segoe UI / Arial where available.


Linux (native build)
--------------------
Same SDL app; audio routing often uses PipeWire/Pulse. See the main project README for
PULSE_SINK, null sinks, and LIVE_VOCODER_PA_* usage. LIVE_VOCODER_PA_LIST_DEVICES=1
still lists devices on stderr.


Wine on Linux
-------------
After install, prefer launching with:
  live-vocoder-wine-launch.sh
(in the install folder as seen from Linux) so PipeWire null sinks and PULSE_SINK are
set before wine runs the .exe.

The Windows .exe detects Wine and can run host pactl via the Wine Z: drive mapping.
Override bash: LIVE_VOCODER_WINE_BASH. Disable auto virtual mic: LIVE_VOCODER_AUTO_VIRT_MIC=0.

Under Wine you must use Windows ffmpeg.exe (bundled next to the .exe), not Linux /usr/bin/ffmpeg
inside the Wine process.


Other environment variables
---------------------------
  LIVE_VOCODER_START_CARRIER   path to a carrier file to load at startup
  LIVE_VOCODER_FFMPEG          full path to ffmpeg.exe (native Windows only; ignored under Wine)
  LIVE_VOCODER_SDL_SKIP_STARTUP_MODALS  set to 1 to skip non-error SDL message boxes at startup


Larger bundle
-------------
The full Python/GTK/web Live Vocoder installer is a separate, larger package from the
same project if you need that stack.
