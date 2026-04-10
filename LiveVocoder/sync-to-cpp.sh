#!/usr/bin/env bash
# Copy sources from this tree into ../cpp/ (preserves installer & bundle scripts under cpp/).
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
CPP="$(cd "$HERE/.." && pwd)/cpp"
if [[ ! -d "$CPP/src" ]]; then
  echo "Expected ../cpp/src — run from repo live_vocoder/LiveVocoder" >&2
  exit 1
fi
rsync -a --delete "$HERE/src/" "$CPP/src/"
rsync -a "$HERE/third_party/" "$CPP/third_party/"
rsync -a "$HERE/cmake/" "$CPP/cmake/"
[[ -f "$HERE/assets/app-icon.png" ]] && cp -f "$HERE/assets/app-icon.png" "$CPP/assets/"
[[ -f "$HERE/assets/app-icon.webp" ]] && cp -f "$HERE/assets/app-icon.webp" "$CPP/assets/"
# Icon helpers (optional)
for f in live_vocoder.rc gen_livevocoder_ico.py LiveVocoder.ico LiveVocoderCppMinimal.iss; do
  [[ -f "$HERE/installer/$f" ]] && cp -f "$HERE/installer/$f" "$CPP/installer/"
done
# Optional: keep cpp installer scripts aligned with this tree
for f in bundle-installer-minimal.sh build-installer-minimal.sh; do
  [[ -f "$HERE/$f" ]] && cp -f "$HERE/$f" "$CPP/$f"
done
echo "Synced → $CPP (src, third_party, cmake, assets, installer peers)"
echo "If you changed CMakeLists.txt here:  cp LiveVocoder/CMakeLists.txt cpp/   (merge inno block manually if needed)"
