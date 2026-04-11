VB-Audio Virtual Cable (bundled into LiveVocoder-Setup.exe)
============================================================

**GitHub Actions** (`.github/workflows/build-cpp-windows-exe.yml`) downloads the official
`VBCABLE_Driver_Pack45.zip` from https://download.vb-audio.com/Download_CABLE/ , extracts
`VBCABLE_Setup_x64.exe`, and places it here before `bundle-installer-minimal.sh` runs. Inno Setup
then includes it under `{app}\extras\` with a checked wizard task on native Windows (see
`LiveVocoderCppMinimal.iss`). VB-Audio software is **donationware** — see https://vb-audio.com/Cable/

**Local / Wine builds:** copy the same file here manually if you want the VB-Cable step in your
`LiveVocoder-Setup.exe`:

  VBCABLE_Setup_x64.exe

Then run `./bundle-installer-minimal.sh` (or `./build-installer-minimal.sh`).

Without this file locally, the minimal installer still builds but omits the VB-Cable task until you
add the exe or use CI.
