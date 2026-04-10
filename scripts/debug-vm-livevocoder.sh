#!/usr/bin/env bash
# Start the Win10/11 QEMU test VM with QEMU debug log + monitor (see test-installer-qemu-win10.sh).
#
# Usage (from repo root):
#   ./scripts/debug-vm-livevocoder.sh
#   LV_INSTALLER=/path/to/LiveVocoder-Setup-Windows.exe ./scripts/debug-vm-livevocoder.sh
#
# Host:
#   - Tail QEMU log:  ls -t ~/.cache/livevocoder-qemu/qemu-debug-*.log | head -1 | xargs tail -f
#   - QEMU monitor:   telnet 127.0.0.1 5555
#
# Guest (CMD as Admin optional):
#   cd /d "C:\Program Files\Live Vocoder"
#   set LIVE_VOCODER_PA_LIST_DEVICES=1
#   LiveVocoder.exe
#   rem Or capture errors:
#   LiveVocoder.exe 2> "%TEMP%\lv-stderr.txt"
#
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
export LV_QEMU_DEBUG="${LV_QEMU_DEBUG:-1}"
export LV_QEMU_MON="${LV_QEMU_MON:-1}"
exec "$ROOT/scripts/test-installer-qemu-win10.sh" "$@"
