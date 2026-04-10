Live Vocoder — Wine host installer (Linux)
==========================================

This package is the same C++ SDL app as the Windows build, but the filename marks it for
installing under Wine on Linux (PipeWire / Pulse helpers run during setup).

Run from a terminal:

  wine LiveVocoder-Setup-Wine.exe

Silent install (optional):

  wine LiveVocoder-Setup-Wine.exe /VERYSILENT /CURRENTUSER /SUPPRESSMSGBOXES /NORESTART /CLOSEAPPLICATIONS

After install, use the “Live Vocoder” desktop entry created under ~/.local/share/applications
(launch script sets up the virtual sink + mic; see README.txt in the install folder).

Carrier conversion needs Windows ffmpeg.exe next to LiveVocoder.exe (this installer places it).
Do not set LIVE_VOCODER_FFMPEG to Linux /usr/bin/ffmpeg — it is ignored under Wine on purpose.

For bare metal Windows, use LiveVocoder-Setup-Windows.exe instead.
