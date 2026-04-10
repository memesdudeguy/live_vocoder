#!/usr/bin/env bash
# Run a **Windows 10/11** VM in QEMU with the **GTK** display to install and run **LiveVocoder.exe**
# (native Windows — SMB share \\10.0.2.4\qemu with your setup .exe).
#
# This is **not** the Linux Python GTK VM — that is scripts/run-gtk-linux-qemu.sh (if present).
#
# Usage:
#   ./scripts/run-windows-exe-qemu-gtk.sh
#   ./scripts/run-windows-exe-qemu-gtk.sh /path/to/LiveVocoder-Setup.exe
#   LV_INSTALLER=/path/to/setup.exe ./scripts/run-windows-exe-qemu-gtk.sh
#
# Requires: qemu + OVMF + samba (smbd) — see scripts/test-installer-qemu-win10.sh header.
# If -display gtk fails: install qemu-ui-gtk (Arch).
#
set -euo pipefail
REPO="$(cd "$(dirname "$0")/.." && pwd)"
export LV_QEMU_DISPLAY="${LV_QEMU_DISPLAY:-gtk,zoom-to-fit=on}"
if [[ "${1:-}" == -* ]]; then
  exec "$REPO/scripts/test-installer-qemu-win10.sh" "$@"
fi
if [[ -n "${1:-}" ]]; then
  export LV_INSTALLER="$(cd "$(dirname "$1")" && pwd)/$(basename "$1")"
  shift
fi
exec "$REPO/scripts/test-installer-qemu-win10.sh" "$@"
