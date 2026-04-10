#!/usr/bin/env bash
# Build a simple .deb (no dh_python). Run from repo root:  ./packaging/build-deb.sh
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"

VERSION="$(sed -n 's/^version = "\([^"]*\)".*/\1/p' "$ROOT/pyproject.toml" | head -1)"
if [[ -z "$VERSION" ]]; then
  echo "Could not read version from pyproject.toml" >&2
  exit 1
fi
PKG="live-vocoder_${VERSION}_all"
STAGE="$(mktemp -d)"
trap 'rm -rf "$STAGE"' EXIT
DEST="$STAGE/$PKG"

mkdir -p "$DEST/usr/share/live-vocoder"
mkdir -p "$DEST/usr/bin"

shopt -s nullglob
for f in "$ROOT"/*.py; do
  base="$(basename "$f")"
  case "$base" in
    test_*.py) continue ;;
    vocoder_session.py.bak) continue ;;
  esac
  cp "$f" "$DEST/usr/share/live-vocoder/"
done

cat > "$DEST/usr/bin/live-vocoder" <<'WRAP'
#!/bin/sh
export PYTHONPATH="/usr/share/live-vocoder${PYTHONPATH:+:$PYTHONPATH}"
exec python3 /usr/share/live-vocoder/live_vocoder.py "$@"
WRAP
chmod 755 "$DEST/usr/bin/live-vocoder"

mkdir -p "$DEST/DEBIAN"
cat > "$DEST/DEBIAN/control" <<EOF
Package: live-vocoder
Version: $VERSION
Section: sound
Priority: optional
Architecture: all
Depends: python3 (>= 3.10), python3-numpy, python3-scipy, python3-sounddevice, ffmpeg
Recommends: python3-gi, gir1.2-gtk-4.0, python3-gi-cairo, python3-gradio, pipewire-pulse | pulseaudio
Suggests: python3-tk, pavucontrol
Maintainer: Live Vocoder <packaging@local>
Description: Live microphone vocoder (GTK / web / CLI)
 PipeWire/Pulse virtual mic on Linux; optional Gradio web UI (--web-gui); ffmpeg for carriers.
EOF

OUTPUT="${ROOT}/dist"
mkdir -p "$OUTPUT"
dpkg-deb --root-owner-group --build "$DEST" "$OUTPUT/${PKG}.deb"
echo "Wrote $OUTPUT/${PKG}.deb"
