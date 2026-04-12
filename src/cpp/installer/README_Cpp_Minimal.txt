Live Vocoder — C++ SDL build (minimal installer)
==================================================

Made by memesdudeguy.

Release 6.0 — ``build-installer-minimal.sh`` / ``.bat`` compile **one** minimal installer:

- ``LiveVocoder-Setup.exe`` — use on **native Windows** or with **Wine** on Linux (Wine-only extras apply at install-time when the installer detects Wine).
  **ISCC** writes it to ``cpp/dist-installer/LiveVocoder-Setup.exe`` (relative to the repo). On the wizard **Finish**
  page (non-silent), the full path of the ``.exe`` you ran is shown, plus paths under ``extras\`` for
  ``VBCABLE_Setup_x64.exe`` and **``VBCABLE_ControlPanel.exe``** (VB-Audio’s own UI for debug).

On GitHub Actions, workflow “Build C++ Windows EXE” uploads artifact ``LiveVocoder-Cpp-setup-v6.0`` with
``LiveVocoder-Setup.exe`` plus **Linux helpers** ``sh-LiveVocoder-Setup.sh`` and ``check-wine-livevocoder-host.sh``
(keep them next to the ``.exe``; run ``/bin/sh ./sh-LiveVocoder-Setup.sh`` to use ``~/.wine-livevocoder``, auto-``wineboot``,
and a **wine32** check — avoids a broken ``~/.wine`` blocking setup).
Pushing a tag matching ``v*`` attaches those files plus ``LiveVocoder_Setup_6.0.exe`` (full installer) to the Release.

What you get
------------
LiveVocoder.exe, SDL2/PortAudio/FFTW runtime DLLs, optional DejaVu font under fonts\,
and **ffmpeg.exe** (required — the minimal installer always ships it next to the .exe).
This build is the SDL GUI only (no Python/GTK/web stack). **Wine on Linux** is supported:
use ``sh-LiveVocoder-Setup.sh`` / the generated ``.desktop``, or run the .exe under Wine;
the app uses Windows ffmpeg.exe and host PipeWire via the installer’s Wine-only registry entries.
**Wine prerequisite (Debian/Ubuntu):** install **``wine32``** after ``dpkg --add-architecture i386``, or you may get
``kernel32.dll`` / ``c0000135`` errors — see **``README_Wine.txt``** (from the installer) or repo **``scripts/check-wine-livevocoder-host.sh``**.
**Native Windows** can auto-route playback to VB-Audio Virtual Cable (CABLE Input) when installed; **under Wine**
that heuristic is off—use ``PULSE_SINK`` / null-sink routing on the Linux host instead.

**VB-Cable in the setup:** GitHub Actions downloads the official ``VBCABLE_Driver_Pack45.zip`` and bundles
**all** files from that zip (``.inf``, ``.cat``, ``.sys``, and ``VBCABLE_Setup_x64.exe``) under
``{app}\extras\``. The VB-Audio setup **must** sit next to those files; the installer alone is not enough.
On **native Windows**, VB-Cable **installs automatically** after the app files (no “opt-in” checkbox). The wizard
has an **optional unchecked** task **“Skip VB-Audio Virtual Cable”** only if you must not run the driver installer.
Silent installs run VB-Cable by default; use ``/MERGETASKS=skipvbcable`` to skip it.
For local builds, extract the zip into ``cpp/installer/third_party/vbcable/`` (see ``third_party/README.txt``)
before ``bundle-installer-minimal.sh``. VB-Audio is donationware.
During setup, Live Vocoder **copies** the VB pack from ``{app}\extras`` into a **session temp folder** and runs
``VBCABLE_Setup_x64.exe`` from there (VB-Audio often errors if the driver setup is launched directly under
``Program Files\Live Vocoder\extras``). The full pack remains under ``extras`` for manual installs.
The bundled ``VBCABLE_Setup_x64.exe`` runs **with no silent flags** during a normal GUI setup (full VB-Audio wizard —
most reliable for driver install in VMs). **``/SILENT``** / **``/VERYSILENT``** Live Vocoder installs still pass
``-i -h -H -n`` to VB-Cable so the wizard does not block automation. **``SW_SHOW``** (not hidden).
**``PrivilegesRequired=admin``** means setup is elevated after the first UAC, so VB-Cable uses **``Exec``**
(no second ``runas`` hop). **``/CURRENTUSER``** (Wine / per-user) drops admin and VB uses **``ShellExec('runas', …)``**.
Windows may still show **driver trust** / SmartScreen. **Note:** after ``runas``, exit code is often ``-1``.

**If either installer or Windows says restart, reboot the PC (or VM) before testing OBS / Live Vocoder** — the
virtual cable endpoints often do not show up in PortAudio until after a reboot.

**If OBS has no “CABLE Input / Output”:** Approve **Windows Security** driver prompts during install. If you
used **Skip**, or install failed, run ``VBCABLE_Setup_x64.exe`` from ``C:\Program Files\Live Vocoder\extras\``
(leave the other VB-Audio files in that folder). Reboot if asked, then **restart OBS**. Check **Device Manager**
for **VB-Audio Virtual Cable**.

**VB-Audio control panel (debug):** The driver pack includes ``VBCABLE_ControlPanel.exe`` in the same
``extras`` folder (also linked from **Start → Live Vocoder → VB-Audio Virtual Cable (Control Panel)** after install).
Use it to check **levels**, **mute**, and basic cable state when OBS or PortAudio routing looks wrong — it is not a
full graph like Linux PipeWire, but it is the official VB-Audio tool for this driver.

**Can’t hear yourself on Monitor (Windows + cable):** Live Vocoder opens **one** PortAudio playback device. If that
device is **CABLE Input**, all processed audio goes **into the virtual cable** (good for OBS). It does **not**
also play to your headset — there is no built-in “split” to real speakers. Use **Settings → Sound → CABLE Output →
Listen to this device** and pick your headphones/speakers, **or** run with ``LIVE_VOCODER_DISABLE_VB_CABLE=1`` and
``LIVE_VOCODER_PA_OUTPUT`` set to your physical output (then you lose automatic cable routing for OBS unless you
add another path). **VB-Cable control panel** can help confirm the cable isn’t muted.

**Virtual mic / OBS has no sound:** Press **Start** in Live Vocoder (streaming must be running). In OBS add **Audio
Input Capture** → **CABLE Output** (not CABLE Input). Signal flows **Live Vocoder → CABLE Input → cable → CABLE
Output → OBS**. If meters in Live Vocoder stay flat, fix the **real microphone** in Windows (Privacy / Sound input)
or ``LIVE_VOCODER_PA_INPUT``. If Live Vocoder shows output level but OBS is silent, OBS is almost certainly listening
to the wrong device.

**If setup says “DeleteFile failed; code 5” for ``LiveVocoder.exe``:** Quit **Live Vocoder** (and anything using
that folder), then choose **Try again**. The installer can also finish after a reboot if the file was locked.


Smoke test (.f32 loads without starting audio)
----------------------------------------------
- **Windows / QEMU VM:** in the install folder (or SMB share), double-click **SmokeValidateF32.bat**, or run:
  ``LiveVocoder.exe --validate-carrier "%ProgramFiles%\Live Vocoder\smoke_carrier.f32" 48000``
  Exit **0** means the carrier file and vocoder initialized (same load path as the GUI).
- **Linux dev tree:** ``./scripts/vm-wine-validate-f32-loop.sh`` (uses native ``cpp/build/LiveVocoder.exe`` when present; retries until success or max tries).

Quick start (Windows)
---------------------
1. **Close Live Vocoder** before upgrading or reinstalling (avoids “access denied” when replacing
   ``LiveVocoder.exe``). Run ``LiveVocoder-Setup.exe`` and finish the wizard. Do **not** check
   **Skip VB-Audio Virtual Cable** unless you intend to skip the driver. **Reboot** if the Live Vocoder or
   VB-Audio installer (or Windows) asks — then test OBS / Live Vocoder.
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
- **Linux host:** prefer ``sh-LiveVocoder-Setup.sh`` / the generated ``.desktop`` next to the installer; or ``wine LiveVocoder-Setup.exe`` if your ``WINEPREFIX`` is already healthy.


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
**VB-Cable has two different names:** **CABLE Input** is a *playback* endpoint (Live Vocoder sends processed
audio there via PortAudio). **CABLE Output** is a *recording* endpoint (OBS/Discord use it as the microphone).
The status bar may also say **Windows recording default → CABLE Output** — that is only “what Windows uses as
the default mic for other apps,” not the same as PortAudio’s chosen capture device inside Live Vocoder.

PortAudio opens the default capture/playback devices unless you override with environment
variables (then restart the app):

  LIVE_VOCODER_PA_INPUT   substring of the mic device name, or
  LIVE_VOCODER_PA_INPUT_INDEX   zero-based input index
  LIVE_VOCODER_PA_OUTPUT  substring of the output device name, or
  LIVE_VOCODER_PA_OUTPUT_INDEX  zero-based output index
  LIVE_VOCODER_WIN_DEFAULT_VIRT_MIC  set to ``1`` to set Windows **default recording** to VB-Audio **CABLE Output**
  (so OBS/Discord can use **Default** mic). **Omit** or ``0`` to **leave your real mic** as the system default.
  LIVE_VOCODER_WIN_MONITOR_DEVICE  optional **substring** of the **speaker** device name for Monitor preview on Windows
  when playback is the virtual cable (e.g. ``Speakers`` or ``Headphones``).

List names once:
  set LIVE_VOCODER_PA_LIST_DEVICES=1
  then run LiveVocoder.exe from a Command Prompt and read the console output.

If PortAudio reports **“Illegal combination of I/O devices”**, the mic and playback were on different
Windows backends (e.g. **MME** vs **WASAPI**). Current builds auto-pick **VB-Cable on the same API as the
mic**, or a **mic on the same API as CABLE Input**; you can still force a matching pair with
``LIVE_VOCODER_PA_INPUT`` / ``LIVE_VOCODER_PA_OUTPUT`` substrings from the same API column in the device list.

**VB-Cable duplex** uses **low** PortAudio suggested latency by default (tight monitoring; avoids multi‑second
delays some hosts saw with high buffers). If **Pull loss** climbs in VB-Audio’s control panel, set
``LIVE_VOCODER_PA_HIGH_LATENCY=1`` or ``LIVE_VOCODER_VB_HIGH_LATENCY=1`` before starting the app (wider
buffers, more delay). ``LIVE_VOCODER_PA_LOW_LATENCY=1`` and ``LIVE_VOCODER_LIVE_MONITORING=1`` still force
low latency explicitly.

**Lower monitoring delay (live feel):** set ``LIVE_VOCODER_LIVE_MONITORING=1`` before starting the app. That
uses a **256-sample** PortAudio hop (~5.3 ms @ 48 kHz instead of ~10.7 ms) with VB-Cable (may increase **Pull
loss** — use ``LIVE_VOCODER_PA_HIGH_LATENCY=1`` or raise VB buffer if needed). Finer control:
``LIVE_VOCODER_HOP`` = ``64``, ``128``, ``256``, or ``512`` (must divide 2048). Also lower **Latency (smp)**
in **VB-Audio Virtual Cable** control panel if it is very high (e.g. 7168).

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
