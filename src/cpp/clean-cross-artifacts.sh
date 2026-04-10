#!/usr/bin/env bash
# Remove regenerable MinGW cross-compile outputs (safe to delete; rebuild with
# ./build-cross-windows.sh — it runs DLL + Python bundles unless LV_BUNDLE_PYTHON=0).
set -euo pipefail
ROOT="$(cd "$(dirname "$0")" && pwd)"
rm -rf "$ROOT/dist-windows-cross" "$ROOT/build-mingw-cross" "$ROOT/dist-installer" \
  "$ROOT/dist-windows-installer-minimal"
echo "Removed: dist-windows-cross, build-mingw-cross, dist-installer, dist-windows-installer-minimal"
