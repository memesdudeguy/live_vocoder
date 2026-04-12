VB-Audio Virtual Cable (bundled into LiveVocoder-Setup.exe)
============================================================

**GitHub Actions** (`.github/workflows/build-cpp-windows-exe.yml`) downloads the official
`VBCABLE_Driver_Pack45.zip` from https://download.vb-audio.com/Download_CABLE/ , extracts **all**
files into `third_party/vbcable/` before `bundle-installer-minimal.sh` runs. The VB-Cable setup
**must** sit next to its `.inf`, `.cat`, and `.sys` files — copying only `VBCABLE_Setup_x64.exe`
causes “Missing .inf file or Driver package corrupted” when the installer runs from
`{app}\extras\`. Inno then packs the whole folder under `{app}\extras\`. VB-Audio software is
**donationware** — see https://vb-audio.com/Cable/

**Local / Wine builds:** extract the official ZIP to a folder named:

  installer/third_party/vbcable/

(all files from the zip in that directory, same level as `VBCABLE_Setup_x64.exe`)

**Legacy:** a lone `third_party/VBCABLE_Setup_x64.exe` still bundles, but the build script warns
that driver install may fail until you add the full pack.

Then run `./bundle-installer-minimal.sh` (or `./build-installer-minimal.sh`). The pack also ships
`VBCABLE_ControlPanel.exe` — documented in `README_Cpp_Minimal.txt` for Windows routing debug.

Without `vbcable/VBCABLE_Setup_x64.exe` (or the legacy exe), the minimal installer still builds but
omits the VB-Cable task until you add files or use CI.
