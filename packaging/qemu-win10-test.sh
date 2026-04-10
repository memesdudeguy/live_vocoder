#!/usr/bin/env bash
# Launch Windows 10 in QEMU for testing LiveVocoder (installer + exe).
#
# Prerequisites (Arch Linux examples):
#   sudo pacman -S qemu-system-x86 edk2-ovmf samba   # samba = QEMU user-mode SMB share (Arch package rename)
#
# First-time Windows install: put official Win10 x64 ISO on disk, then e.g.
#   WIN10_ISO=~/Downloads/Win10_22H2.iso ./qemu-win10-test.sh ~/VMs/win10.qcow2
#
# Daily test (existing disk image):
#   ./qemu-win10-test.sh ~/VMs/win10.qcow2
#
# In the guest, open File Explorer → \\10.0.2.4\qemu — shared folder is the
# minimal installer directory (override with LV_SHARE_DIR).

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
LV_LIVE="${REPO_ROOT}/LiveVocoder"
DEFAULT_SHARE="${LV_LIVE}/dist-installer"
LV_SHARE_DIR="${LV_SHARE_DIR:-$DEFAULT_SHARE}"

QEMU="${QEMU:-qemu-system-x86_64}"
if ! command -v "$QEMU" >/dev/null 2>&1; then
  echo "error: $QEMU not found. Install e.g.  sudo pacman -S qemu-system-x86 (or qemu-desktop)" >&2
  exit 1
fi

DISK="${1:-${WIN10_DISK:-}}"
if [[ -z "$DISK" ]]; then
  echo "usage: $0 <path-to-win10.qcow2|raw>" >&2
  echo "   or: WIN10_DISK=... $0" >&2
  exit 1
fi
if [[ ! -f "$DISK" ]]; then
  echo "error: disk image not found: $DISK" >&2
  exit 1
fi

if [[ ! -d "$LV_SHARE_DIR" ]]; then
  echo "error: share dir missing (build installer first): $LV_SHARE_DIR" >&2
  exit 1
fi

# accel
ACCEL="kvm:tcg"
if [[ "${QEMU_NO_KVM:-0}" == "1" ]]; then
  ACCEL="tcg"
fi
CPU_ARGS=(-cpu qemu64)
if [[ "$ACCEL" == kvm* ]] && [[ -r /dev/kvm ]]; then
  CPU_ARGS=(-cpu host)
else
  ACCEL="tcg"
fi

MEM="${QEMU_MEM:-6144}"
SMP="${QEMU_SMP:-4}"

# SMB share into the guest (Windows: \\10.0.2.4\qemu) — needs samba (smbd) on the host
NETDEV_SPEC="user,id=net0"
if command -v smbd >/dev/null 2>&1 || [[ -x /usr/sbin/smbd ]]; then
  NETDEV_SPEC="${NETDEV_SPEC},smb=${LV_SHARE_DIR}"
else
  echo "warning: smbd not found — install samba for \\10.0.2.4\\qemu share (or copy installer via USB/virtio-fs)." >&2
fi

ARGS=(
  -machine "q35,accel=${ACCEL}"
  "${CPU_ARGS[@]}"
  -m "$MEM"
  -smp "$SMP"
  -vga virtio
  -display "${QEMU_DISPLAY:-gtk,zoom-to-fit=on}"
)

# UEFI. Arch 2025+: /usr/share/edk2/x64/OVMF_CODE.4m.fd (older: edk2-ovmf/x64/OVMF_CODE.fd)
OVMF_CODE="${OVMF_CODE:-}"
OVMF_VARS="${OVMF_VARS:-}"
for cand in \
  /usr/share/edk2/x64/OVMF_CODE.4m.fd \
  /usr/share/edk2-ovmf/x64/OVMF_CODE.4m.fd \
  /usr/share/edk2-ovmf/x64/OVMF_CODE.fd \
  /usr/share/OVMF/OVMF_CODE.fd \
  /usr/share/qemu/edk2-x86_64-code.fd; do
  if [[ -r "$cand" ]]; then
    OVMF_CODE="$cand"
    break
  fi
done
if [[ -n "$OVMF_CODE" ]]; then
  VARS_TEMPLATE=""
  for cand in \
    /usr/share/edk2/x64/OVMF_VARS.4m.fd \
    /usr/share/edk2-ovmf/x64/OVMF_VARS.4m.fd \
    /usr/share/edk2-ovmf/x64/OVMF_VARS.fd \
    /usr/share/OVMF/OVMF_VARS.fd \
    /usr/share/qemu/edk2-x86_64-vars.fd; do
    if [[ -r "$cand" ]]; then
      VARS_TEMPLATE="$cand"
      break
    fi
  done
  if [[ -n "${OVMF_VARS:-}" ]] && [[ -r "$OVMF_VARS" ]]; then
    :
  elif [[ -n "$VARS_TEMPLATE" ]]; then
    RUN_OVMF="${XDG_RUNTIME_DIR:-/tmp}/livevocoder-ovmf-vars.$$.fd"
    cp -f "$VARS_TEMPLATE" "$RUN_OVMF"
    OVMF_VARS="$RUN_OVMF"
    trap 'rm -f "$RUN_OVMF" 2>/dev/null' EXIT
  fi
  if [[ -n "${OVMF_VARS:-}" ]] && [[ -r "$OVMF_VARS" ]]; then
    ARGS+=(
      -drive "if=pflash,format=raw,unit=0,file=${OVMF_CODE},readonly=on"
      -drive "if=pflash,format=raw,unit=1,file=${OVMF_VARS}"
    )
  fi
fi

# Main disk (IDE is slow but works without virtio drivers in older images)
FORMAT="${DISK##*.}"
case "$FORMAT" in
  qcow2|raw) ;;
  *) FORMAT=qcow2 ;;
esac
ARGS+=(-drive "file=${DISK},format=${FORMAT},if=virtio,cache=writeback")

# Optional install ISO
if [[ -n "${WIN10_ISO:-}" ]] && [[ -f "${WIN10_ISO}" ]]; then
  ARGS+=(-cdrom "${WIN10_ISO}" -boot "order=dc")
fi

ARGS+=(
  -netdev "$NETDEV_SPEC"
  -device "virtio-net,netdev=net0"
  -usb
  -device "usb-tablet"
)

echo "Disk:     $DISK"
echo "Share:    $LV_SHARE_DIR  →  Windows: \\\\10.0.2.4\\qemu"
echo "QEMU:     ${ARGS[*]}"
exec "$QEMU" "${ARGS[@]}"
