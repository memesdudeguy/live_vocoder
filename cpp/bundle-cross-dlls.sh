#!/usr/bin/env bash
# Copy PE DLL dependencies for a MinGW-built LiveVocoder.exe (Linux cross-build).
# Walks transitive imports (exe → libportaudio → libssp, libgcc → libwinpthread, …) so Wine
# and portable folders get every DLL from MINGW_ROOT/bin.
#
# Requires: x86_64-w64-mingw32-objdump, MinGW DLLs under MINGW_ROOT/bin
set -euo pipefail
ROOT="$(cd "$(dirname "$0")" && pwd)"
EXE="${ROOT}/dist-windows-cross/LiveVocoder.exe"
OUT="${ROOT}/dist-windows-cross"

if [[ ! -f "$EXE" ]]; then
  echo "Missing $EXE — run ./build-cross-windows.sh first." >&2
  exit 1
fi

detect_root() {
  if [[ -n "${MINGW_ROOT:-}" ]]; then
    echo "$MINGW_ROOT"
    return
  fi
  for d in /usr/x86_64-w64-mingw32 /usr/x86_64-w64-mingw32/sys-root/mingw; do
    [[ -d "$d/bin" ]] || continue
    echo "$d"
    return
  done
  echo ""
}

MINGW_ROOT="$(detect_root)"
if [[ -z "$MINGW_ROOT" ]]; then
  echo "Set MINGW_ROOT to your MinGW sysroot (directory containing bin/*.dll)." >&2
  exit 1
fi

BINDIR="${MINGW_ROOT}/bin"
if [[ ! -d "$BINDIR" ]]; then
  echo "No bin/ under MINGW_ROOT=$MINGW_ROOT" >&2
  exit 1
fi

OBJDUMP=x86_64-w64-mingw32-objdump
if ! command -v "$OBJDUMP" >/dev/null 2>&1; then
  echo "Need $OBJDUMP (mingw-w64-binutils)" >&2
  exit 1
fi

# Provided by Windows / Wine — not in MinGW bin (or must not be bundled).
should_skip() {
  local dl="${1,,}"
  case "$dl" in
    kernel32.dll|kernelbase.dll|user32.dll|gdi32.dll|shell32.dll|advapi32.dll|msvcrt.dll|ucrtbase.dll|ole32.dll|oleaut32.dll|uuid.dll|winmm.dll|ws2_32.dll|ntdll.dll|imm32.dll|crypt32.dll|rpcrt4.dll|setupapi.dll|iphlpapi.dll|dbghelp.dll|version.dll|shlwapi.dll|comdlg32.dll|comctl32.dll|secur32.dll|uxtheme.dll|dwmapi.dll|dnsapi.dll|netapi32.dll|userenv.dll|winspool.drv|psapi.dll|dsound.dll|powrprof.dll|hid.dll|cfgmgr32.dll|devobj.dll|bcrypt.dll|ncrypt.dll|cryptbase.dll|msasn1.dll|wintrust.dll|profapi.dll|mpr.dll|winhttp.dll|mswsock.dll|dhcpcsvc.dll|dhcpcsvc6.dll|sxs.dll|combase.dll|windows.storage.dll|shcore.dll|msvcp_win.dll|usp10.dll)
      return 0
      ;;
  esac
  [[ "$dl" == api-ms-win-* ]] || [[ "$dl" == ext-ms-win-* ]] && return 0
  return 1
}

resolve_src() {
  local name="$1"
  local low="${name,,}"
  if [[ -f "${BINDIR}/${name}" ]]; then
    echo "${BINDIR}/${name}"
    return 0
  fi
  if [[ -f "${BINDIR}/${low}" ]]; then
    echo "${BINDIR}/${low}"
    return 0
  fi
  return 1
}

declare -A scanned
queue=("$EXE")
missing=0

while ((${#queue[@]} > 0)); do
  pe="${queue[0]}"
  queue=("${queue[@]:1}")
  [[ -n "${scanned[$pe]:-}" ]] && continue
  scanned[$pe]=1

  mapfile -t deps < <("$OBJDUMP" -p "$pe" 2>/dev/null | sed -n 's/.*DLL Name: //p' | sort -u)
  for dll in "${deps[@]}"; do
    [[ -z "$dll" ]] && continue
    if should_skip "$dll"; then
      continue
    fi
    src=""
    if ! src="$(resolve_src "$dll")"; then
      echo "warning: $dll (needed by $(basename "$pe")) not in $BINDIR" >&2
      missing=1
      continue
    fi
    base="$(basename "$src")"
    dest="${OUT}/${base}"
    if [[ ! -f "$dest" ]]; then
      cp -f "$src" "$dest"
      echo "copied $base"
    fi
    # Always enqueue so we scan transitive deps even if this DLL was copied by an older script.
    queue+=("$dest")
  done
done

echo "Bundle in: $OUT"
exit "$missing"
