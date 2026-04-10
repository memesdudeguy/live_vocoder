#!/usr/bin/env bash
# Run test-installer-qemu-win10.sh with LV_INSTALLER defaulting to this tree's setup .exe.
# From repo root:  ./run-win10-qemu-test.sh  or  ./cpp/run-win10-qemu-test.sh
# From dist-installer:  ../run-win10-qemu-test.sh
set -euo pipefail
REPO="$(cd "$(dirname "$0")/.." && pwd)"
export LV_INSTALLER="${LV_INSTALLER:-$REPO/cpp/dist-installer/LiveVocoder-Setup.exe}"
exec "$REPO/scripts/test-installer-qemu-win10.sh" "$@"
