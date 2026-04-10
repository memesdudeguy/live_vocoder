#!/usr/bin/env bash
# Same as scripts/run-windows-exe-qemu-gtk.sh — run from repo root.
set -euo pipefail
exec "$(cd "$(dirname "$0")" && pwd)/scripts/run-windows-exe-qemu-gtk.sh" "$@"
