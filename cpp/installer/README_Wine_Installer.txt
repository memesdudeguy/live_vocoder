Live Vocoder — same installer on Linux (Wine) and Windows
==========================================================

``LiveVocoder-Setup.exe`` is one build: use it on **native Windows** or run it with **Wine** on Linux.
When the installer detects Wine, it adds PipeWire/Pulse env defaults, optional AppCompat Layers, the host
``live-vocoder-wine-launch.sh``, ``README_Wine.txt``, and a ``.desktop`` entry.

Debian / Ubuntu — install **32-bit Wine** (required for many WOW64 prefixes)
-----------------------------------------------------------------------------
If you see::

  wine: could not load kernel32.dll, status c0000135
  it looks like wine32 is missing … dpkg --add-architecture i386 … apt-get install wine32

then enable **multiarch** and install **wine32** (not only ``wine64``)::

  sudo dpkg --add-architecture i386
  sudo apt-get update
  sudo apt-get install -y wine wine64 wine32

If Wine already failed once, **remove the broken prefix** and recreate::

  rm -rf ~/.wine
  WINEARCH=win64 wineboot -u

Optional: from the Live Vocoder repo run ``scripts/check-wine-livevocoder-host.sh`` — it prints the same
hints if ``wine32`` is missing.

Run from a terminal:

  wine LiveVocoder-Setup.exe

Silent install (optional):

  wine LiveVocoder-Setup.exe /VERYSILENT /CURRENTUSER /SUPPRESSMSGBOXES /NORESTART /CLOSEAPPLICATIONS

Wine prints many “fixme:” lines to the terminal during setup; that is normal and does not mean the install failed.

After install, use the “Live Vocoder” desktop entry created under ~/.local/share/applications
(launch script sets up the virtual sink + mic; see README.txt in the install folder).

Carrier conversion needs Windows ffmpeg.exe next to LiveVocoder.exe (this installer places it).
Do not set LIVE_VOCODER_FFMPEG to Linux /usr/bin/ffmpeg — it is ignored under Wine on purpose.
