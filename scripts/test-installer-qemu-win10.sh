#!/usr/bin/env bash
# Run a Windows 10 or **Windows 11** VM in QEMU to test LiveVocoder-Setup-Windows.exe on real Windows.
#
# Windows 11 needs TPM 2.0 in the VM: install **swtpm** (Arch: sudo pacman -S swtpm). This script
# starts swtpm automatically when the ISO filename looks like Win11 or when a marker file exists
# beside the disk (see LV_QEMU_TPM below).
#
# The installer directory is exposed to the guest as SMB:  \\10.0.2.4\qemu
# (QEMU user networking). Install Samba so QEMU can spawn smbd:
#   sudo pacman -S --needed samba
#
# Packages (Arch example):
#   sudo pacman -S --needed qemu-system-x86 qemu-img edk2-ovmf samba swtpm
#   Guest audio needs a QEMU audio driver (minimal qemu may only list wav/none):
#   sudo pacman -S --needed qemu-audio-pipewire   # or qemu-audio-pa / qemu-audio-alsa / qemu-audio-sdl
#   Some distros expose the binary as qemu-system-x86_64, qemu-system-x86, or qemu-kvm.
#
# Example ISOs:
#   export WIN10_ISO=/home/memesdudeguy/Downloads/Win10_22H2_English_x64v1.iso
#   # Windows 11 also works (needs swtpm — auto-enabled when the filename contains Win11):
#   # export WIN10_ISO=/home/memesdudeguy/Downloads/Win11_25H2_English_x64_v2.iso
#   (WIN11_ISO is accepted as an alias for WIN10_ISO.)
#
# Run from **repo root** (directory that contains `scripts/` and `cpp/`), e.g. live_vocoder/:
#   ./scripts/test-installer-qemu-win10.sh …
#   ./run-win10-qemu-test.sh …
#   ./cpp/run-win10-qemu-test.sh …
# From **cpp/dist-installer/** only: ../run-win10-qemu-test.sh …  (not ../.. — that is wrong from repo root)
#
# First-time Windows install (empty disk):
#   ./scripts/test-installer-qemu-win10.sh --install
#
# After Windows is installed (boot from disk, share has the setup .exe):
#   ./scripts/test-installer-qemu-win10.sh
#
# Args:
#   [path/to/LiveVocoder-Setup-Windows.exe]   (default: <repo>/cpp/dist-installer/LiveVocoder-Setup-Windows.exe)
# Env:
#   WIN10_ISO / WIN11_ISO   path to Windows 10/11 installer ISO (required for --install if disk empty)
#   WIN10_DISK         qcow2 path (default: ~/.cache/livevocoder-qemu/win10-test.qcow2)
#   LV_QEMU_TPM          1 = force TPM (swtpm); 0 = never; unset = auto (Win11 / *.win11 marker)
#   LV_QEMU_SECBOOT      1 = force OVMF Secure Boot firmware; 0 = never; unset = auto (Win11 / TPM path)
#   WIN10_DISK_GB      virtual disk size when created (default: 64)
#   LV_QEMU_RAM        MB RAM (default: 8192)
#   LV_QEMU_CPUS       vCPUs (default: 4)
#   LV_QEMU_CPU        passed to -cpu (default: host + Hyper-V enlightenments for snappier Windows guests)
#   LV_QEMU_DISK_FAST  1 (default) = qcow2 cache=writeback,discard=unmap on NVMe; 0 = plain drive line
#   LV_QEMU_DISK_AIO   native (default) = lower qcow2 latency on Linux; threads = omit aio=native if unstable
#   LV_QEMU_MISC_FAST  1 (default) = -rtc driftfix=slew + kvm-pit lost_tick_policy=discard (snappier Windows timers)
#   LV_QEMU_FAST_PRESET 1 = set RAM=12288 CPUS=6 AUDIO=0 for a quicker interactive test (override with env below)
#   LV_QEMU_DEBUG      1 = write QEMU trace to $LV_QEMU_CACHE/qemu-debug-*.log (-d guest_errors)
#   LV_QEMU_MON        1 = QEMU monitor on telnet 127.0.0.1:5555 (commands: help, info status, log snapshot)
#   LV_QEMU_DISPLAY    gtk | sdl | none | full gtk string, e.g. gtk,zoom-to-fit=on
#                        default: gtk desktop window when DISPLAY or WAYLAND set, else sdl
#   LV_QEMU_GTK_OPTS   extra gtk display options (default: zoom-to-fit=on)
#   LV_QEMU_FULLSCREEN  set to 1 to add full-screen=on to the gtk display
#   LV_QEMU_AUDIO       1 (default) = guest sound; 0 = no -audiodev / sound card
#   LV_QEMU_AUDIODEV    pipewire | pa | alsa | sdl — backend for -audiodev (default: auto from qemu -audiodev help)
#   LV_QEMU_AUDIO_ID    chardev id (default: lvqa0); must match id= in LV_QEMU_AUDIODEV if you pass a full spec
#                       (auto backend: QEMU -audiodev help is cached in $LV_QEMU_CACHE/qemu-audiodev-help.cache)
#   LV_QEMU_RESET_OVMF_VARS  set to 1 to overwrite NVRAM from distro template (fixes stale NVMe boot "Not Found")
#   LV_QEMU_ALLOW_EMPTY_DISK  set to 1 to boot a nearly empty qcow2 without an ISO (default: refuse)
#   LV_QEMU_NO_AUTO_ISO      set to 1 to disable auto DVD boot when disk is empty but WIN10_ISO is set
#   LV_QEMU_SKIP_AUTO_OVMF_RESET  set to 1 to skip NVRAM refresh on empty-disk+ISO boot (stale Boot0002 can block DVD)
#   VIRTIO_ISO         optional path to virtio-win.iso (Fedora) for extra drivers
#
# If -display gtk fails, install the UI driver:  sudo pacman -S qemu-ui-gtk
#
# If OVMF says NVMe boot "Not Found" → PXE: often the qcow2 is still empty (Windows never installed). Check:
#   qemu-img info ~/.cache/livevocoder-qemu/win10-test.qcow2   # "disk size" tiny vs 64G virtual = empty
# Install once:  WIN10_ISO=/path/to/Win11.iso ./scripts/test-installer-qemu-win10.sh --install
# Stale NVRAM only:  LV_QEMU_RESET_OVMF_VARS=1 ./scripts/test-installer-qemu-win10.sh
#
# One-shot “make it feel faster” (host must have RAM/CPUs to spare):
#   LV_QEMU_FAST_PRESET=1 ./scripts/test-installer-qemu-win10.sh
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
INSTALLER="${LV_INSTALLER:-$ROOT/cpp/dist-installer/LiveVocoder-Setup-Windows.exe}"
CACHE="${LV_QEMU_CACHE:-$HOME/.cache/livevocoder-qemu}"
DISK="${WIN10_DISK:-$CACHE/win10-test.qcow2}"
DISK_GB="${WIN10_DISK_GB:-64}"
case "${LV_QEMU_FAST_PRESET:-0}" in
  1|true|yes)
    RAM_MB="${LV_QEMU_RAM:-12288}"
    CPUS="${LV_QEMU_CPUS:-6}"
    export LV_QEMU_AUDIO="${LV_QEMU_AUDIO:-0}"
    echo "test-installer-qemu-win10: LV_QEMU_FAST_PRESET=1 → RAM=${RAM_MB}M CPUs=${CPUS} LV_QEMU_AUDIO=${LV_QEMU_AUDIO:-0}" >&2
    ;;
  *)
    RAM_MB="${LV_QEMU_RAM:-8192}"
    CPUS="${LV_QEMU_CPUS:-4}"
    ;;
esac
QEMU_CPU="${LV_QEMU_CPU:-host,hv_relaxed,hv_spinlocks=0x1fff,hv_vapic,hv_time}"
DISK_NVME_EXTRA=""
case "${LV_QEMU_DISK_FAST:-1}" in
  0|false|no) DISK_NVME_EXTRA="" ;;
  *) DISK_NVME_EXTRA=",cache=writeback,discard=unmap" ;;
esac
# aio=native needs cache.direct=on (QEMU rejects native AIO with writeback-only).
DISK_AIO_EXTRA=""
case "${LV_QEMU_DISK_AIO:-native}" in
  threads) ;;
  *) DISK_AIO_EXTRA=",cache.direct=on,aio=native" ;;
esac

# Alias: either variable can point at the Microsoft ISO.
if [[ -z "${WIN10_ISO:-}" && -n "${WIN11_ISO:-}" ]]; then
  WIN10_ISO="$WIN11_ISO"
fi

die() { echo "test-installer-qemu-win10: $*" >&2; exit 1; }

iso_suggests_win11() {
  case "$(basename "${1:-}")" in *Win11*|*win11*|*WIN11*) return 0 ;; *) return 1 ;; esac
}

want_tpm() {
  case "${LV_QEMU_TPM:-}" in
    0|false|no) return 1 ;;
    1|true|yes) return 0 ;;
  esac
  [[ -f "${DISK}.win11" ]] && return 0
  [[ -n "${WIN10_ISO:-}" && -f "$WIN10_ISO" ]] && iso_suggests_win11 "$WIN10_ISO" && return 0
  return 1
}

# Windows 11 setup requires "Secure Boot capable" firmware — use OVMF_CODE.secboot*.fd (MS keys).
use_ovmf_secboot() {
  case "${LV_QEMU_SECBOOT:-}" in
    0|false|no) return 1 ;;
    1|true|yes) return 0 ;;
  esac
  want_tpm && return 0
  [[ -f "${DISK}.win11" ]] && return 0
  [[ -n "${WIN10_ISO:-}" && -f "$WIN10_ISO" ]] && iso_suggests_win11 "$WIN10_ISO" && return 0
  return 1
}

DO_INSTALL=false
POS=()
for a in "$@"; do
  case "$a" in
    --install|-i) DO_INSTALL=true ;;
    *) POS+=("$a") ;;
  esac
done
if [[ ${#POS[@]} -ge 1 && -f "${POS[0]}" ]]; then
  INSTALLER="${POS[0]}"
elif [[ ${#POS[@]} -ge 1 && "${POS[0]}" == *.exe ]]; then
  INSTALLER="${POS[0]}"
fi

pick_qemu_system_x86() {
  local c
  for c in qemu-system-x86_64 qemu-system-x86 qemu-kvm; do
    if command -v "$c" >/dev/null 2>&1; then
      printf '%s\n' "$c"
      return 0
    fi
  done
  return 1
}
QEMU_BIN=""
QEMU_BIN="$(pick_qemu_system_x86)" || true
[[ -n "$QEMU_BIN" ]] || die "install QEMU (Arch: sudo pacman -S qemu-system-x86 qemu-img edk2-ovmf — or qemu-desktop)"
command -v qemu-img >/dev/null || die "install qemu-img"

[[ -f "$INSTALLER" ]] || die "installer not found: $INSTALLER"
INSTALLER_DIR="$(cd "$(dirname "$INSTALLER")" && pwd)"

if ! command -v smbd >/dev/null 2>&1; then
  echo "warning: smbd not found — QEMU's -netdev user,smb=... may fail. Install samba (e.g. sudo pacman -S samba)." >&2
fi

mkdir -p "$CACHE"

# Arch edk2-ovmf: standard CODE.4m vs CODE.secboot.4m (Windows 11 installer checks Secure Boot support).
OVMF_CODE=""
OVMF_VARS_SRC=""
_ovmf_pairs=()
if use_ovmf_secboot; then
  _ovmf_pairs+=(
    "/usr/share/edk2/x64/OVMF_CODE.secboot.4m.fd:/usr/share/edk2/x64/OVMF_VARS.4m.fd"
    "/usr/share/edk2-ovmf/x64/OVMF_CODE.secboot.4m.fd:/usr/share/edk2-ovmf/x64/OVMF_VARS.4m.fd"
  )
fi
_ovmf_pairs+=(
  "/usr/share/edk2/x64/OVMF_CODE.4m.fd:/usr/share/edk2/x64/OVMF_VARS.4m.fd"
  "/usr/share/edk2-ovmf/x64/OVMF_CODE.4m.fd:/usr/share/edk2-ovmf/x64/OVMF_VARS.4m.fd"
  "/usr/share/edk2-ovmf/x64/OVMF_CODE.fd:/usr/share/edk2-ovmf/x64/OVMF_VARS.fd"
)
for _pair in "${_ovmf_pairs[@]}"; do
  IFS=: read -r _c _v <<<"$_pair"
  if [[ -f "$_c" && -f "$_v" ]]; then
    OVMF_CODE="$_c"
    OVMF_VARS_SRC="$_v"
    break
  fi
done
[[ -n "$OVMF_CODE" && -n "$OVMF_VARS_SRC" ]] || die "install edk2-ovmf (UEFI for Windows) — no OVMF_CODE/OVMF_VARS found under /usr/share/edk2 or /usr/share/edk2-ovmf"
if use_ovmf_secboot && [[ "$OVMF_CODE" != *secboot* ]]; then
  die "Windows 11 needs Secure Boot UEFI: OVMF_CODE.secboot.4m.fd missing (upgrade edk2-ovmf; expected under /usr/share/edk2/x64)"
fi

# Separate NVRAM for Secure Boot vs non-SB firmware (do not mix varstores).
if use_ovmf_secboot && [[ "$OVMF_CODE" == *secboot* ]]; then
  OVMF_VARS_DISK="${DISK%.qcow2}_OVMF_VARS.secboot.fd"
else
  OVMF_VARS_DISK="${DISK%.qcow2}_OVMF_VARS.fd"
fi
if [[ ! -f "$OVMF_VARS_DISK" ]]; then
  cp -f "$OVMF_VARS_SRC" "$OVMF_VARS_DISK"
fi
case "${LV_QEMU_RESET_OVMF_VARS:-}" in
  1|true|yes)
    cp -f "$OVMF_VARS_SRC" "$OVMF_VARS_DISK"
    echo "LV_QEMU_RESET_OVMF_VARS: rewrote $(basename "$OVMF_VARS_DISK") from template (stale boot entries cleared)." >&2
    ;;
esac
if [[ "$OVMF_CODE" == *secboot* ]]; then
  echo "OVMF: Secure Boot firmware $(basename "$OVMF_CODE") + NVRAM $(basename "$OVMF_VARS_DISK")" >&2
fi

if [[ ! -f "$DISK" ]]; then
  if ! $DO_INSTALL; then
    die "VM disk missing: $DISK — run once with --install and WIN10_ISO=/path/to/win10.iso"
  fi
  [[ -n "${WIN10_ISO:-}" && -f "$WIN10_ISO" ]] || die "set WIN10_ISO (or WIN11_ISO) to your Windows x64 .iso for first install"
  echo "Creating ${DISK_GB}G disk $DISK ..." >&2
  qemu-img create -f qcow2 "$DISK" "${DISK_GB}G"
  if [[ -n "${WIN10_ISO:-}" && -f "$WIN10_ISO" ]] && iso_suggests_win11 "$WIN10_ISO"; then
    : >"${DISK}.win11"
  fi
fi
if $DO_INSTALL && [[ -n "${WIN10_ISO:-}" && -f "$WIN10_ISO" ]] && iso_suggests_win11 "$WIN10_ISO"; then
  : >"${DISK}.win11"
fi

# Host-side qcow2 allocation (detect "empty" disk without probing the guest).
_DISK_ACTUAL_BYTES=""
_EMPTY_DISK=0
if [[ -f "$DISK" ]] && _json="$(qemu-img info -U --output=json "$DISK" 2>/dev/null)"; then
  _DISK_ACTUAL_BYTES="$(grep -o '"actual-size"[[:space:]]*:[[:space:]]*[0-9][0-9]*' <<<"$_json" | head -1 | grep -o '[0-9][0-9]*$')"
  if [[ -n "$_DISK_ACTUAL_BYTES" && "$_DISK_ACTUAL_BYTES" =~ ^[0-9]+$ && $_DISK_ACTUAL_BYTES -lt 10485760 ]]; then
    _EMPTY_DISK=1
  fi
fi

# Empty disk + no installer media → NVMe Boot0002 / PXE. Allow: ISO in WIN10_ISO (auto DVD-first), --install, or ALLOW_EMPTY.
_BOOT_ISO_FIRST=false
if $DO_INSTALL; then
  _BOOT_ISO_FIRST=true
elif [[ $_EMPTY_DISK -eq 1 && -n "${WIN10_ISO:-}" && -f "$WIN10_ISO" ]]; then
  case "${LV_QEMU_NO_AUTO_ISO:-}" in 1|true|yes) ;; *)
    _BOOT_ISO_FIRST=true
    echo "test-installer-qemu-win10: empty disk (~$((_DISK_ACTUAL_BYTES / 1024)) KiB on host) — booting WIN10_ISO from DVD first (no --install needed)." >&2
    ;;
  esac
fi

if $_BOOT_ISO_FIRST && [[ -n "${WIN10_ISO:-}" && -f "$WIN10_ISO" ]] && iso_suggests_win11 "$WIN10_ISO"; then
  : >"${DISK}.win11"
fi

if ! $DO_INSTALL && [[ -f "$DISK" ]] && [[ $_EMPTY_DISK -eq 1 ]]; then
  _auto_iso_ok=0
  if $_BOOT_ISO_FIRST; then _auto_iso_ok=1; fi
  if [[ $_auto_iso_ok -eq 0 ]]; then
    case "${LV_QEMU_ALLOW_EMPTY_DISK:-}" in 1|true|yes) ;; *)
      echo "test-installer-qemu-win10: VM disk $DISK has almost no data on host (~$((_DISK_ACTUAL_BYTES / 1024)) KiB allocated) — Windows is not installed." >&2
      echo "  Set WIN10_ISO=/path/to.iso (DVD boots automatically), or run:  WIN10_ISO=... \"$0\" --install" >&2
      echo "  Or:  LV_QEMU_ALLOW_EMPTY_DISK=1  (expect NVMe \"Not Found\" / PXE until an OS exists.)" >&2
      exit 1
    ;;
    esac
  fi
fi

# OVMF can keep an old Boot0002 (NVMe) ahead of the DVD in BootOrder; empty disk → NVMe "Not Found" → PXE despite bootindex.
if $_BOOT_ISO_FIRST && [[ $_EMPTY_DISK -eq 1 ]]; then
  case "${LV_QEMU_SKIP_AUTO_OVMF_RESET:-}" in 1|true|yes) ;; *)
    cp -f "$OVMF_VARS_SRC" "$OVMF_VARS_DISK"
    echo "test-installer-qemu-win10: refreshed OVMF NVRAM for empty-disk ISO boot (clear stale NVMe-first entries)." >&2
    ;;
  esac
fi

SWTPM_PID=""
cleanup_swtpm() {
  if [[ -n "${SWTPM_PID:-}" ]] && kill -0 "$SWTPM_PID" 2>/dev/null; then
    kill "$SWTPM_PID" 2>/dev/null || true
    wait "$SWTPM_PID" 2>/dev/null || true
  fi
}
trap cleanup_swtpm EXIT

QEMU_NET="user,id=net0,smb=$INSTALLER_DIR"
# e1000e: works in Windows Setup without virtio drivers.
QEMU_ARGS=(
  -enable-kvm
  -machine "q35,accel=kvm,kernel-irqchip=on"
  -cpu "$QEMU_CPU"
  -m "$RAM_MB"
  -smp "cpus=$CPUS,cores=$CPUS,threads=1,sockets=1"
  -netdev "$QEMU_NET"
  # Fixed PCI addrs: OVMF boot entries use Pci(0x3,0x0) for our NVMe root port — slot=5 broke that (Not Found).
  # e1000e must not share dev 3; 04.0 is a typical free slot on q35 root bus.
  -device "e1000e,netdev=net0,bootindex=10,addr=04.0"
  -drive "if=pflash,format=raw,unit=0,readonly=on,file=$OVMF_CODE"
  -drive "if=pflash,format=raw,unit=1,file=$OVMF_VARS_DISK"
)

# Windows inbox NVMe driver — no extra storage driver during setup.
QEMU_ARGS+=(-drive "file=$DISK,if=none,id=nvme0,format=qcow2${DISK_NVME_EXTRA}${DISK_AIO_EXTRA}")
case "${LV_QEMU_MISC_FAST:-1}" in
  0|false|no) ;;
  *)
    QEMU_ARGS+=(-global "kvm-pit.lost_tick_policy=discard")
    QEMU_ARGS+=(-rtc "base=localtime,driftfix=slew")
    ;;
esac
# NVMe behind a dedicated root port at bus 0 dev 3 fn 0 matches OVMF paths like …/Pci(0x3,0x0)/Pci(0x0,0x0)/NVMe…
QEMU_ARGS+=(-device "pcie-root-port,id=lv_rp_nvme,bus=pcie.0,addr=03.0")
# QEMU ≥6.1 auto-generates a non-zero namespace EUI; OVMF Boot0002 often stores NVMe(…,00-00-…-00).
# Controller + nvme-ns with eui64=0 matches that path (avoids BdsDxe "Not Found" → PXE).

DISP="${LV_QEMU_DISPLAY:-}"
if [[ -z "$DISP" ]]; then
  if [[ -n "${DISPLAY:-}" || -n "${WAYLAND_DISPLAY:-}" ]]; then
    DISP=gtk
  else
    DISP=sdl
  fi
fi
# GTK "desktop mode": zoom guest into the window (good for Win11). Allow full LV_QEMU_DISPLAY=gtk,foo,bar.
QEMU_DISPLAY_SPEC=""
if [[ "$DISP" == "gtk" ]]; then
  _gopts="${LV_QEMU_GTK_OPTS:-zoom-to-fit=on}"
  if [[ "${LV_QEMU_FULLSCREEN:-0}" == "1" || "${LV_QEMU_FULLSCREEN:-}" == "yes" ]]; then
    _gopts="${_gopts},full-screen=on"
  fi
  QEMU_DISPLAY_SPEC="gtk,${_gopts}"
  QEMU_ARGS+=(-display "$QEMU_DISPLAY_SPEC")
elif [[ "$DISP" == gtk,* ]]; then
  QEMU_DISPLAY_SPEC="$DISP"
  QEMU_ARGS+=(-display "$DISP")
else
  QEMU_DISPLAY_SPEC="$DISP"
  QEMU_ARGS+=(-display "$DISP")
fi
QEMU_ARGS+=(-usb -device "usb-tablet")

# Keep PCI order stable: NVMe/TPM must stay before ich9-intel-hda or OVMF boot entries break
# ("failed to load ... NVMe ... Not Found"). Audio goes after disks/CDs.

if want_tpm; then
  command -v swtpm >/dev/null || die "Windows 11 (or LV_QEMU_TPM=1) needs swtpm — e.g. sudo pacman -S swtpm"
  TPM_ST="${DISK%.qcow2}_swtpm"
  mkdir -p "$TPM_ST"
  SWTPM_SOCK="$TPM_ST/swtpm-sock"
  rm -f "$SWTPM_SOCK"
  swtpm socket --tpm2 --tpmstate "dir=$TPM_ST" --ctrl "type=unixio,path=$SWTPM_SOCK" --log "file=$TPM_ST/swtpm.log" &
  SWTPM_PID=$!
  _swtpm_i=0
  while [[ ! -S "$SWTPM_SOCK" && $_swtpm_i -lt 200 ]]; do
    sleep 0.02
    ((_swtpm_i++)) || true
  done
  [[ -S "$SWTPM_SOCK" ]] || die "swtpm did not create $SWTPM_SOCK (see $TPM_ST/swtpm.log)"
  QEMU_ARGS+=(-chardev "socket,id=chrtpm,path=$SWTPM_SOCK")
  QEMU_ARGS+=(-tpmdev "emulator,id=tpm0,chardev=chrtpm")
  QEMU_ARGS+=(-device "tpm-crb,tpmdev=tpm0")
  echo "TPM: swtpm on $SWTPM_SOCK (state in $TPM_ST)" >&2
fi

# OVMF/UEFI often ignores -boot order=d when NVMe is present; use per-device bootindex (lower = earlier).
if $_BOOT_ISO_FIRST; then
  [[ -n "${WIN10_ISO:-}" && -f "$WIN10_ISO" ]] || die "WIN10_ISO (or WIN11_ISO) must point to a Windows .iso"
  QEMU_ARGS+=(-drive "file=$WIN10_ISO,if=none,id=lv_win_iso,media=cdrom,readonly=on")
  QEMU_ARGS+=(-device "ide-cd,drive=lv_win_iso,bootindex=1")
  QEMU_ARGS+=(-device "nvme,id=lv_nvme,serial=livevocoder,bus=lv_rp_nvme")
  QEMU_ARGS+=(-device "nvme-ns,drive=nvme0,bus=lv_nvme,nsid=1,eui64=0,bootindex=2")
  QEMU_ARGS+=(-boot "menu=on,strict=on")
  if $DO_INSTALL; then
    echo "Starting Windows **installer** (DVD bootindex before NVMe). If it still skips the ISO, press ESC at TianoCore → Boot Manager → DVD." >&2
  else
    echo "Starting from **installer ISO** (empty disk). After Windows is installed, run without WIN10_ISO or use normal boot from NVMe." >&2
  fi
else
  QEMU_ARGS+=(-device "nvme,id=lv_nvme,serial=livevocoder,bus=lv_rp_nvme")
  QEMU_ARGS+=(-device "nvme-ns,drive=nvme0,bus=lv_nvme,nsid=1,eui64=0,bootindex=1")
  QEMU_ARGS+=(-boot "order=c,menu=on")
  echo "Starting Windows from disk. In the guest, open:  \\\\10.0.2.4\\qemu  → run LiveVocoder-Setup-Windows.exe" >&2
  echo "If NVMe boot \"Not Found\" / PXE: LV_QEMU_RESET_OVMF_VARS=1 once, or ESC → Boot Manager → Windows Boot Manager." >&2
fi

if [[ -n "${VIRTIO_ISO:-}" && -f "$VIRTIO_ISO" ]]; then
  QEMU_ARGS+=(-drive "file=$VIRTIO_ISO,media=cdrom,readonly=on")
  echo "Attached virtio-win ISO as an extra CD (optional drivers)." >&2
fi

# Guest audio (after NVMe/TPM/CD so UEFI NVMe device paths stay valid).
AUDIO_SUMMARY="off"
case "${LV_QEMU_AUDIO:-1}" in
  0|false|no|none|off) LV_QEMU_AUDIO=0 ;;
  *) LV_QEMU_AUDIO=1 ;;
esac
if [[ "$LV_QEMU_AUDIO" == "1" ]]; then
  AUDIO_ID="${LV_QEMU_AUDIO_ID:-lvqa0}"
  _ad_backend="${LV_QEMU_AUDIODEV:-}"
  if [[ -z "$_ad_backend" ]]; then
    _qemu_abs="$(command -v "$QEMU_BIN")"
    _qemu_mtime="$(stat -c %Y "$_qemu_abs" 2>/dev/null || echo 0)"
    _ad_cache="$CACHE/qemu-audiodev-help.cache"
    _help=""
    if [[ -f "$_ad_cache" ]]; then
      IFS=$'\t' read -r _c_path _c_mtime < <(head -1 "$_ad_cache" || true) || true
      if [[ "$_c_path" == "$_qemu_abs" && "$_c_mtime" == "$_qemu_mtime" ]]; then
        _help="$(tail -n +3 "$_ad_cache" 2>/dev/null || true)"
      fi
    fi
    if [[ -z "$_help" ]]; then
      _help="$("$QEMU_BIN" -audiodev help 2>&1 || true)"
      printf '%s\t%s\n\n%s\n' "$_qemu_abs" "$_qemu_mtime" "$_help" > "${_ad_cache}.new"
      mv -f "${_ad_cache}.new" "$_ad_cache"
    fi
    # qemu -audiodev help lists driver names one per line (format varies by build).
    if grep -qE '^pipewire($|[[:space:]])' <<<"$_help"; then
      _ad_backend=pipewire
    elif grep -qE '^pa($|[[:space:]])' <<<"$_help"; then
      _ad_backend=pa
    elif grep -qE '^alsa($|[[:space:]])' <<<"$_help"; then
      _ad_backend=alsa
    elif grep -qE '^sdl($|[[:space:]])' <<<"$_help"; then
      _ad_backend=sdl
    else
      _ad_backend=""
    fi
  fi
  if [[ -n "$_ad_backend" ]]; then
    if [[ "$_ad_backend" == *","* ]]; then
      QEMU_ARGS+=(-audiodev "$_ad_backend")
      if [[ "$_ad_backend" != *"id=$AUDIO_ID"* ]]; then
        die "LV_QEMU_AUDIODEV must include id=$AUDIO_ID (or set LV_QEMU_AUDIO_ID to match your id=...)"
      fi
    else
      QEMU_ARGS+=(-audiodev "${_ad_backend},id=${AUDIO_ID}")
    fi
    QEMU_ARGS+=(-device ich9-intel-hda)
    QEMU_ARGS+=(-device "hda-duplex,audiodev=${AUDIO_ID}")
    AUDIO_SUMMARY="${_ad_backend%%,*}+ich9-hda"
  else
    echo "warning: QEMU has no usable audiodev (only none/wav?) — install e.g. qemu-audio-pipewire or qemu-audio-pa (Arch), or set LV_QEMU_AUDIO=0." >&2
  fi
fi

case "${LV_QEMU_DEBUG:-0}" in
  1|true|yes)
    _qlog="$CACHE/qemu-debug-$(date +%Y%m%d-%H%M%S).log"
    QEMU_ARGS+=(-D "$_qlog")
    QEMU_ARGS+=(-d guest_errors)
    echo "LV_QEMU_DEBUG: QEMU log file: $_qlog" >&2
    echo "  In the guest (CMD): LiveVocoder.exe 2> \"%TEMP%\\lv-stderr.txt\"" >&2
    echo "  List audio devices: set LIVE_VOCODER_PA_LIST_DEVICES=1 before starting LiveVocoder.exe" >&2
    ;;
esac
case "${LV_QEMU_MON:-0}" in
  1|true|yes)
    QEMU_ARGS+=(-monitor "telnet:127.0.0.1:5555,server,nowait")
    echo "LV_QEMU_MON: QEMU monitor →  telnet 127.0.0.1 5555   (try: help | info status | log snapshot)" >&2
    ;;
esac

echo "Disk: $DISK  |  -display $QEMU_DISPLAY_SPEC  |  RAM: ${RAM_MB}M  |  smp: ${CPUS}c/1t  |  -cpu ${QEMU_CPU}  |  nvme: format=qcow2${DISK_NVME_EXTRA}${DISK_AIO_EXTRA}  |  misc_fast: ${LV_QEMU_MISC_FAST:-1}  |  audio: $AUDIO_SUMMARY  |  smbd: $(command -v smbd 2>/dev/null || echo none)" >&2
"$QEMU_BIN" "${QEMU_ARGS[@]}"
rc=$?
cleanup_swtpm
trap - EXIT
exit "$rc"
