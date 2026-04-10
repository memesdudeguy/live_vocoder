#!/usr/bin/env bash
# Boot a Debian cloud image in QEMU, cloud-init installs Wine, downloads the
# Inno Setup .exe from the host (10.0.2.2), runs a silent install, writes logs.
#
# Requires: qemu-system-x86_64, qemu-img, curl/wget, python3 (venv + pycdlib).
# On Arch:   sudo pacman -S --needed qemu-system-x86 qemu-img edk2-ovmf curl python
#            Binary may be qemu-system-x86_64, qemu-system-x86, or qemu-kvm.
#
# For a **Windows 10** VM (real Windows, not Wine) use:
#   ./scripts/test-installer-qemu-win10.sh
#
# Usage:
#   ./scripts/test-installer-qemu.sh [path/to/LiveVocoder-Setup-Wine.exe]
# Env:
#   LV_QEMU_PORT=9876   host HTTP port for the guest to fetch the installer
#   LV_QEMU_RAM=6144    MB RAM
#   LV_QEMU_CPUS=4
#   LV_QEMU_DISPLAY=sdl|gtk|none   (default: gtk if DISPLAY set, else none + serial)
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
INSTALLER="${1:-$ROOT/cpp/dist-installer/LiveVocoder-Setup-Wine.exe}"
INST_BN="$(basename "$INSTALLER")"
PORT="${LV_QEMU_PORT:-9876}"
RAM_MB="${LV_QEMU_RAM:-6144}"
CPUS="${LV_QEMU_CPUS:-4}"
CACHE="${LV_QEMU_CACHE:-$HOME/.cache/livevocoder-qemu}"
WORKDIR="${LV_QEMU_WORK:-/tmp/livevocoder-qemu-$$}"
HTTP_PID=""
SERVER_LOG=""

die() { echo "test-installer-qemu: $*" >&2; exit 1; }

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
command -v curl >/dev/null || command -v wget >/dev/null || die "install curl or wget"
command -v python3 >/dev/null || die "install python3"

[[ -f "$INSTALLER" ]] || die "installer not found: $INSTALLER"

mkdir -p "$CACHE" "$WORKDIR"
cleanup() {
  if [[ -n "$HTTP_PID" ]] && kill -0 "$HTTP_PID" 2>/dev/null; then
    kill "$HTTP_PID" 2>/dev/null || true
    wait "$HTTP_PID" 2>/dev/null || true
  fi
  if [[ -n "${SERVER_LOG:-}" && -f "$SERVER_LOG" ]]; then
    echo "--- host http log (tail) ---" >&2
    tail -20 "$SERVER_LOG" >&2 || true
  fi
}
trap cleanup EXIT

BASE_QCOW="$CACHE/debian-12-generic-amd64.qcow2"
BASE_URL="https://cloud.debian.org/images/cloud/bookworm/latest/debian-12-generic-amd64.qcow2"
if [[ ! -f "$BASE_QCOW" ]]; then
  echo "Downloading Debian 12 generic cloud image (~500MB) to $BASE_QCOW ..." >&2
  if command -v curl >/dev/null; then
    curl -fL --retry 3 -o "$BASE_QCOW.tmp" "$BASE_URL" && mv "$BASE_QCOW.tmp" "$BASE_QCOW"
  else
    wget -O "$BASE_QCOW.tmp" "$BASE_URL" && mv "$BASE_QCOW.tmp" "$BASE_QCOW"
  fi
fi

OVERLAY="$WORKDIR/vm.qcow2"
rm -f "$OVERLAY"
qemu-img create -f qcow2 -F qcow2 -b "$BASE_QCOW" "$OVERLAY" 32G >/dev/null

SEED_ISO="$WORKDIR/cidata.iso"
VENV="$CACHE/qemu-test-venv"
if [[ ! -x "$VENV/bin/python" ]]; then
  python3 -m venv "$VENV"
  "$VENV/bin/pip" install -q pycdlib
fi

USER_DATA="$WORKDIR/user-data"
META_DATA="$WORKDIR/meta-data"
cat >"$META_DATA" <<EOF
instance-id: livevocoder-qemu-$(date +%s)
local-hostname: lv-qemu
EOF

# cloud-init: install wine, fetch installer from host slirp address, silent Inno.
cat >"$USER_DATA" <<EOF
#cloud-config
hostname: lv-qemu
manage_etc_hosts: localhost

# Debian cloud images ship a 'debian' user; enable console login + sudo.
chpasswd:
  list: |
    debian:debian
  expire: false
ssh_pwauth: true

package_update: true
package_upgrade: true
packages:
  - wine64
  - wine
  - wget
  - ca-certificates

write_files:
  - path: /usr/local/bin/lv-run-installer.sh
    permissions: '0755'
    content: |
      #!/bin/bash
      set -euxo pipefail
      exec > >(tee /home/debian/lv-installer-run.log) 2>&1
      export WINEPREFIX=/home/debian/.wine
      export HOME=/home/debian
      echo "[lv] fetching installer from host..."
      wget -q -O /tmp/${INST_BN} "http://10.0.2.2:${PORT}/${INST_BN}"
      ls -la /tmp/${INST_BN}
      export DEBIAN_FRONTEND=noninteractive
      export WINEDEBUG=-all
      echo "[lv] running silent Inno under Wine (may take several minutes)..."
      wine64 /tmp/${INST_BN} /VERYSILENT /CURRENTUSER /SUPPRESSMSGBOXES /NORESTART \
        || wine /tmp/${INST_BN} /VERYSILENT /CURRENTUSER /SUPPRESSMSGBOXES /NORESTART \
        || true
      echo "[lv] listing install dir (if any):"
      ls -la "/home/debian/.wine/drive_c/Program Files/Live Vocoder/" 2>/dev/null || true
      ls -la "/home/debian/.wine/drive_c/Program Files/" 2>/dev/null | head -30 || true
      echo "[lv] done"

runcmd:
  - [ sh, -c, "echo 'cloud-init lv-qemu: starting installer test' | tee /dev/ttyS0 /dev/console || true" ]
  - [ /usr/bin/sudo, -u, debian, /bin/bash, /usr/local/bin/lv-run-installer.sh ]
  - [ sh, -c, "echo 'cloud-init lv-qemu: finished' | tee /dev/ttyS0 /dev/console || true" ]
  - [ shutdown, -h, now ]
EOF

"$VENV/bin/python" <<PY
import io, os, pycdlib
iso = pycdlib.PyCdlib()
iso.new(joliet=3, rock_ridge="1.09", vol_ident="cidata")
ud_path = "${USER_DATA}"
md_path = "${META_DATA}"
with open(ud_path, "rb") as f:
    ud = f.read()
with open(md_path, "rb") as f:
    md = f.read()
out = "${SEED_ISO}"
iso.add_fp(io.BytesIO(ud), len(ud), "/USERDATA.;1", rr_name="user-data", joliet_path="/user-data")
iso.add_fp(io.BytesIO(md), len(md), "/METADATA.;1", rr_name="meta-data", joliet_path="/meta-data")
iso.write(out)
iso.close()
print("wrote", out)
PY

INST_DIR="$(cd "$(dirname "$INSTALLER")" && pwd)"
SERVER_LOG="$WORKDIR/http-server.log"
(
  cd "$INST_DIR"
  python3 -m http.server "$PORT" --bind 0.0.0.0
) >>"$SERVER_LOG" 2>&1 &
HTTP_PID=$!
sleep 0.4
kill -0 "$HTTP_PID" || die "failed to start HTTP server on port $PORT"

QEMU_ARGS=(
  -enable-kvm
  -machine "q35,accel=kvm"
  -cpu host
  -m "$RAM_MB"
  -smp "$CPUS"
  -drive "file=$OVERLAY,if=virtio,cache=writeback"
  -drive "file=$SEED_ISO,if=virtio,format=raw"
  -netdev "user,id=n0"
  -device "virtio-net-pci,netdev=n0"
)

OVMF_CODE=""
OVMF_VARS_SRC=""
while IFS=: read -r _c _v; do
  if [[ -f "$_c" && -f "$_v" ]]; then
    OVMF_CODE="$_c"
    OVMF_VARS_SRC="$_v"
    break
  fi
done <<'OVPAIRS'
/usr/share/edk2/x64/OVMF_CODE.4m.fd:/usr/share/edk2/x64/OVMF_VARS.4m.fd
/usr/share/edk2-ovmf/x64/OVMF_CODE.4m.fd:/usr/share/edk2-ovmf/x64/OVMF_VARS.4m.fd
/usr/share/edk2-ovmf/x64/OVMF_CODE.fd:/usr/share/edk2-ovmf/x64/OVMF_VARS.fd
OVPAIRS
[[ -n "$OVMF_CODE" && -n "$OVMF_VARS_SRC" ]] || die "edk2-ovmf required (Arch: sudo pacman -S edk2-ovmf) — no OVMF firmware pair found"
cp -f "$OVMF_VARS_SRC" "$WORKDIR/OVMF_VARS.fd"
QEMU_ARGS+=(
  -drive "if=pflash,format=raw,unit=0,readonly=on,file=$OVMF_CODE"
  -drive "if=pflash,format=raw,unit=1,file=$WORKDIR/OVMF_VARS.fd"
)

DISP="${LV_QEMU_DISPLAY:-}"
if [[ -z "$DISP" ]]; then
  if [[ -n "${DISPLAY:-}" ]]; then
    DISP="gtk"
  else
    DISP="none"
  fi
fi
if [[ "$DISP" == "none" ]]; then
  QEMU_ARGS+=(-nographic)
else
  QEMU_ARGS+=(-display "$DISP")
  QEMU_ARGS+=(-serial stdio)
fi

echo "Starting QEMU (installer served at http://127.0.0.1:$PORT/$INST_BN)..." >&2
echo "Workdir: $WORKDIR  |  Guest user: debian / debian  |  Poweroff after cloud-init (~10–25 min first boot)." >&2
"$QEMU_BIN" "${QEMU_ARGS[@]}"
echo "QEMU exited." >&2
