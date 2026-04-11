VB-Audio Virtual Cable (optional installer bundle)
=================================================

To ship **virtual microphone + virtual playback** support in `LiveVocoder-Setup.exe`, download the
official x64 installer from VB-Audio and place it here **exactly** as:

  VBCABLE_Setup_x64.exe

Source: https://vb-audio.com/Cable/  (donateware; follow their license for redistribution).

Then run `./bundle-installer-minimal.sh` (or `./build-installer-minimal.sh`). The setup wizard will
offer a checked task to run this installer after the app files are installed (UAC / driver prompts
are normal).

Without this file, the Live Vocoder installer still builds; native Windows users can install
VB-Cable manually for the same audio routing (see README_Cpp_Minimal.txt).
