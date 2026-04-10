#!/usr/bin/env bash
# List PortAudio devices using the project venv (system `python` often has no sounddevice).
set -euo pipefail
ROOT="$(cd "$(dirname "$0")" && pwd)"
cd "$ROOT"
if [[ ! -x .venv/bin/python ]]; then
  echo "No .venv — run ./run.sh once or: python3 -m venv .venv && .venv/bin/pip install -r requirements.txt" >&2
  exit 1
fi
.venv/bin/python -m sounddevice "$@"
echo ""
.venv/bin/python -c "
from pulse_portaudio import resolve_stream_device_for_pulse_virt_mic
i = resolve_stream_device_for_pulse_virt_mic()
if i is not None:
    print('---')
    print(f'Live Vocoder auto-pick (duplex preferred, same as ./run.sh): LIVE_VOCODER_PORTAUDIO_OUTPUT={i}')
    print('If virtual mic is silent, export that line then ./run.sh --gtk-gui')
else:
    print('---')
    print('Could not auto-pick pulse/pipewire duplex — choose an index with input+output above.')
" 2>/dev/null || true
