Live Vocoder — Wine on macOS (virtual audio)
==============================================

The Windows build runs under **Wine** on macOS. **VB-Audio Virtual Cable does not work here**: it is a
Windows kernel driver. In the installer, use the **“Skip VB-Audio Virtual Cable”** task if offered, or
ignore VB-Cable setup errors under Wine.

Use macOS virtual audio instead
-------------------------------
1. Install **BlackHole 2ch** (free): https://existential.audio/blackhole/  
   (Commercial alternatives: **Loopback**, **Soundflower**-successors, etc.)

2. Open **Audio MIDI Setup** (Spotlight). Click **+** → **Create Multi-Output Device**. Enable your
**built-in speakers/headphones** and **BlackHole 2ch**. Check **Drift Correction** for the built-in device
if you hear crackle.

3. Right-click the Multi-Output Device → **Use This Device For Sound Output**. Now system audio (and
Wine’s default output) goes to **both** speakers and BlackHole.

4. In **OBS** (or another app), choose **BlackHole 2ch** as the microphone / desktop audio capture source
to pick up what Live Vocoder plays.

Pick devices inside Live Vocoder (optional)
-------------------------------------------
Wine exposes CoreAudio devices to PortAudio with names that often include **“Core Audio”** or the
macOS device name.

- Run once with: ``LIVE_VOCODER_PA_LIST_DEVICES=1`` (see README next to the app) and note the output index or name.
- Then set, for example:  
  ``LIVE_VOCODER_PA_OUTPUT=BlackHole``  
  ``LIVE_VOCODER_PA_INPUT=Built-in``  
  (substring match; adjust for your mic name.)

Wine on macOS
-------------
- Install Wine via **Homebrew** (e.g. ``brew install --cask wine-stable``) or the upstream **Winehq** macOS
  packages. You do **not** need Linux-style **wine32** / i386 multiarch (that is a Debian/Ubuntu issue).

- The **live-vocoder-wine-launch.sh** script skips PipeWire/pactl setup on Darwin (that stack is for Linux
  hosts). Launching ``wine LiveVocoder.exe`` directly is fine if you set routing in Audio MIDI Setup.

- **.desktop** files under ``~/.local/share/applications`` are mainly for Linux; on macOS run the app from
  Terminal, Automator, or a small shell alias pointing at ``live-vocoder-wine-launch.sh`` or ``wine``.
