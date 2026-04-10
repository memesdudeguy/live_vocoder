#!/usr/bin/env bash
# Install pip + requirements.txt into the portable embeddable Windows Python layout
# (dist-windows-cross/) using Wine. Use this when running under Wine/Linux.
#
# On real Windows, use install_python_deps.bat instead (normal pip).
#
# When LiveVocoder.exe spawns this script, the environment often has no PATH;
# we prepend standard dirs and try absolute paths for wine64/wine.
#
# Usage (from the portable folder, same dir as LiveVocoder.exe):
#   ./install_python_deps.sh
# Env:
#   WINE   — wine binary (default: wine64 if found, else wine)

set -euo pipefail

ROOT="$(cd "$(dirname "$0")" && pwd)"
LOG="${ROOT}/install_python_deps_last.log"
PY="${ROOT}/python/python.exe"
GPIP="${ROOT}/python/get-pip.py"
REQ="${ROOT}/requirements.txt"

# Non-interactive bash from Wine's CreateProcess often has PATH="" or minimal.
export PATH="/usr/local/bin:/usr/bin:/bin:${PATH:-}"
export PIP_DISABLE_PIP_VERSION_CHECK=1

rm -f "$LOG"
if ! touch "$LOG" 2>/dev/null; then
  LOG="${TMPDIR:-/tmp}/live_vocoder_install_python_deps.log"
  echo "warning: install dir not writable for log — using $LOG" >&2
  rm -f "$LOG"
  touch "$LOG"
fi

# Log to file and stderr; avoid `exec > >(tee)` (can break under Wine-spawned bash).
_run() {
  set -euo pipefail
  echo "=== install_python_deps.sh $(date -Is) ==="
  echo "PATH=$PATH"
  echo "ROOT=$ROOT"

  if [[ ! -f "$PY" ]]; then
    echo "Missing: $PY — run cpp/bundle-dist-windows-cross-python.sh first." >&2
    exit 1
  fi
  if [[ ! -f "$GPIP" ]]; then
    echo "Missing: $GPIP — re-run the bundle script (downloads get-pip.py)." >&2
    exit 1
  fi

  wine_cmd() {
    if [[ -n "${WINE:-}" ]]; then
      if [[ -x "$WINE" ]]; then
        printf '%s' "$WINE"
        return 0
      fi
      if command -v "$WINE" >/dev/null 2>&1; then
        command -v "$WINE"
        return 0
      fi
    fi
    local c p
    for c in wine64 wine; do
      if command -v "$c" >/dev/null 2>&1; then
        command -v "$c"
        return 0
      fi
    done
    for p in /usr/bin/wine64 /usr/bin/wine; do
      if [[ -x "$p" ]]; then
        printf '%s' "$p"
        return 0
      fi
    done
    return 1
  }

  WINE_BIN="$(wine_cmd)" || {
    echo "Need wine64 or wine (tried PATH, /usr/bin/wine64, /usr/bin/wine). Set WINE=/path/to/wine." >&2
    exit 1
  }

  echo "Using WINE_BIN=$WINE_BIN"
  "$WINE_BIN" --version 2>&1 || true

  # Only use pip --user when the embeddable python dir is not writable (e.g. real Windows
  # Program Files as a normal user). Under Wine, ~/.wine/drive_c/Program Files/... is
  # usually writable from Linux — forcing --user sends packages to Wine APPDATA and often breaks.
  PY_DIR="$(dirname "$PY")"
  USE_USER=0
  if [[ ! -w "$PY_DIR" ]] 2>/dev/null; then
    USE_USER=1
    echo "Python directory not writable — using pip --user."
  fi

  to_win_path() {
    local u="$1"
    local w=""
    if command -v winepath >/dev/null 2>&1; then
      w="$(winepath -w "$u" 2>/dev/null || true)"
    fi
    if [[ -z "$w" || "$w" == "$u" ]]; then
      w="$u"
    fi
    printf '%s' "$w"
  }

  PY_W="$(to_win_path "$PY")"
  GPIP_W="$(to_win_path "$GPIP")"
  REQ_W=""
  if [[ -f "$REQ" ]]; then
    REQ_W="$(to_win_path "$REQ")"
  fi

  if "$WINE_BIN" "$PY_W" -m pip --version >/dev/null 2>&1; then
    echo "pip already present — skipping get-pip.py"
  else
    echo "Running get-pip.py ..."
    if [[ "$USE_USER" -eq 1 ]]; then
      "$WINE_BIN" "$PY_W" "$GPIP_W" --user || exit 1
    else
      "$WINE_BIN" "$PY_W" "$GPIP_W" || exit 1
    fi
  fi

  echo "Upgrading pip ..."
  if [[ "$USE_USER" -eq 1 ]]; then
    "$WINE_BIN" "$PY_W" -m pip install --user --upgrade pip || exit 1
  else
    if ! "$WINE_BIN" "$PY_W" -m pip install --upgrade pip; then
      echo "pip upgrade failed; retrying with --user ..." >&2
      "$WINE_BIN" "$PY_W" -m pip install --user --upgrade pip || exit 1
    fi
  fi

  if [[ -f "$REQ" && -n "$REQ_W" ]]; then
    echo "Installing requirements.txt (host: $REQ) (Wine: $REQ_W) ..."
    if [[ "$USE_USER" -eq 1 ]]; then
      "$WINE_BIN" "$PY_W" -m pip install --user -r "$REQ_W" || exit 1
    else
      if ! "$WINE_BIN" "$PY_W" -m pip install -r "$REQ_W"; then
        echo "pip install failed (permissions?). Retrying with --user ..." >&2
        "$WINE_BIN" "$PY_W" -m pip install --user -r "$REQ_W" || exit 1
      fi
    fi
  else
    echo "warning: missing $REQ" >&2
  fi

  touch "${ROOT}/.wine_deps_installed"
  echo ""
  echo "Done. Log: $LOG"
  echo "Example: cd \"$ROOT\" && $WINE_BIN ./LiveVocoder.exe"
}

set -o pipefail
_run 2>&1 | tee -a "$LOG"
