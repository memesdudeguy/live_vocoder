#!/usr/bin/env bash
# Retry until --validate-carrier succeeds (loads .f32 + constructs vocoder; no audio / SDL).
#
# On Linux dev hosts, the script prefers the **native ELF** build (cpp/build/LiveVocoder.exe) — same
# carrier + vocoder code path as Windows. For a **Windows PE** binary use LV_TEST_EXE=.../LiveVocoder.exe
# with Wine (may fail on some Wine setups; use a real Windows VM + SmokeValidateF32.bat instead).
#
#   ./scripts/vm-wine-validate-f32-loop.sh
#   LV_VALIDATE_MAX_TRIES=60 LV_VALIDATE_RETRY_SEC=5 ./scripts/vm-wine-validate-f32-loop.sh
#
# Windows VM (after install or from SMB share folder):
#   SmokeValidateF32.bat
#   or: LiveVocoder.exe --validate-carrier "%ProgramFiles%\Live Vocoder\smoke_carrier.f32" 48000
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
MAX="${LV_VALIDATE_MAX_TRIES:-40}"
SLEEP="${LV_VALIDATE_RETRY_SEC:-4}"
TMP_F32="${TMPDIR:-/tmp}/lv_smoke_$$.f32"

cleanup() { rm -f "$TMP_F32"; }
trap cleanup EXIT

[[ -f "$ROOT/cpp/installer/gen_smoke_carrier_f32.py" ]] || {
  echo "missing gen_smoke_carrier_f32.py" >&2
  exit 1
}
python3 "$ROOT/cpp/installer/gen_smoke_carrier_f32.py" "$TMP_F32"

pick_exe() {
  if [[ -n "${LV_TEST_EXE:-}" ]]; then
    printf '%s\n' "$LV_TEST_EXE"
    return
  fi
  local elf="$ROOT/cpp/build/LiveVocoder.exe"
  if [[ -f "$elf" ]] && file -b "$elf" | grep -qi ELF; then
    printf '%s\n' "$elf"
    return
  fi
  local pe="$ROOT/cpp/build-mingw-cross/LiveVocoder.exe"
  if [[ -f "$pe" ]]; then
    printf '%s\n' "$pe"
    return
  fi
  printf '%s\n' "$ROOT/cpp/dist-windows-cross/LiveVocoder.exe"
}

EXE="$(pick_exe)"
[[ -f "$EXE" ]] || {
  echo "missing $EXE — run: cd $ROOT/cpp && cmake --build build && cmake --build build-mingw-cross" >&2
  exit 1
}

run_validate() {
  if file -b "$EXE" | grep -qi ELF; then
    "$EXE" --validate-carrier "$TMP_F32" 48000
    return $?
  fi
  command -v wine >/dev/null || {
    echo "wine not found; on Linux build the ELF target (cmake --build cpp/build) or install wine for PE." >&2
    return 1
  }
  local exedir winpath
  exedir="$(cd "$(dirname "$EXE")" && pwd)"
  winpath="$(WINEPREFIX="${WINEPREFIX:-$HOME/.wine}" wine winepath -w "$TMP_F32" 2>/dev/null)" || true
  [[ -n "$winpath" ]] || winpath="$TMP_F32"
  (cd "$exedir" && wine "$(basename "$EXE")" --validate-carrier "$winpath" 48000)
}

export WINEDEBUG="${WINEDEBUG:--all}"
attempt=0
ec=1
while [[ $attempt -lt $MAX ]]; do
  attempt=$((attempt + 1))
  echo "=== validate-carrier attempt $attempt / $MAX ($EXE) ===" >&2
  set +e
  run_validate
  ec=$?
  set -e
  if [[ $ec -eq 0 ]]; then
    echo "OK: .f32 loaded and vocoder constructed." >&2
    break
  fi
  echo "exit $ec — retry in ${SLEEP}s (rebuild cpp/build or cpp/build-mingw-cross; Windows VM: run SmokeValidateF32.bat)." >&2
  sleep "$SLEEP"
done
exit "$ec"
