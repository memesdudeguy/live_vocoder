#!/usr/bin/env bash
# Same as cpp/run-win10-qemu-test.sh — run from **repo root** (live_vocoder/).
# Default installer: cpp/dist-installer/LiveVocoder-Setup-Windows.exe
set -euo pipefail
ROOT="$(cd "$(dirname "$0")" && pwd)"
export LV_INSTALLER="${LV_INSTALLER:-$ROOT/cpp/dist-installer/LiveVocoder-Setup-Windows.exe}"
exec "$ROOT/scripts/test-installer-qemu-win10.sh" "$@"
