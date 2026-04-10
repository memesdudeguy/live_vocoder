#!/usr/bin/env bash
# Double-click in Finder (may need: chmod +x run-mac.command).
set -euo pipefail
ROOT="$(cd "$(dirname "$0")" && pwd)"
cd "$ROOT"

if [[ ! -x .venv/bin/python ]]; then
  echo "Creating .venv and installing dependencies…"
  python3 -m venv .venv
  .venv/bin/pip install -U pip
  .venv/bin/pip install -r requirements.txt
fi

# GTK 4 + PyGObject: brew install pygobject3 gtk4 (names vary by macOS/Homebrew).
if .venv/bin/python -c "import gi; gi.require_version('Gtk', '4.0'); from gi.repository import Gtk" 2>/dev/null; then
  exec .venv/bin/python live_vocoder.py --gtk-gui "$@"
else
  echo "[live_vocoder] GTK 4 not available — opening web UI. For native UI: brew install pygobject3 gtk4, then pip install PyGObject pycairo" >&2
  exec .venv/bin/python live_vocoder.py --web-gui "$@"
fi
