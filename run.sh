#!/usr/bin/env bash
# Always run with project venv. For Windows use run.bat; for macOS use run-mac.command.
#
# Optional: unique Pulse null sink so Live Vocoder doesn’t clash with another virtual mic:
#   export LIVE_VOCODER_PULSE_SINK=live_vocoder_me
#   export LIVE_VOCODER_PULSE_DESCRIPTION=LiveVocoderMe
#
# Virtual mic needs PortAudio to use PipeWire’s ALSA plugin (not raw hw:). List devices with:
#   ./list_sounddevices.sh
# (Do not use system `python -m sounddevice` — it usually has no sounddevice.) Then:
#   export LIVE_VOCODER_PORTAUDIO_OUTPUT=7
# (use the index from YOUR ./list_sounddevices.sh list — often **pulse** or **pipewire**)
# Arch: sudo pacman -S pipewire-alsa pipewire-pulse
#      systemctl --user restart pipewire pipewire-pulse
#
# Auto-pick PortAudio output for PipeWire (so virtual mic works without manual export):
# On Linux, if LIVE_VOCODER_PORTAUDIO_OUTPUT is unset, we set it to a **duplex** **pulse** / **pipewire**
# / **default** ALSA device when available (mic + playback in one stream). Disable: LIVE_VOCODER_NO_AUTO_PULSE_OUT=1
set -euo pipefail
ROOT="$(cd "$(dirname "$0")" && pwd)"
cd "$ROOT"
if [[ ! -x .venv/bin/python ]]; then
  echo "Creating .venv and installing deps…" >&2
  python3 -m venv .venv
  .venv/bin/pip install -r requirements.txt
fi
# Same logic as pulse_portaudio.resolve_stream_device_for_pulse_virt_mic (duplex pulse/pipewire preferred).
if [[ "$(uname -s)" == Linux ]] && [[ -z "${LIVE_VOCODER_PORTAUDIO_OUTPUT:-}" ]] && [[ -z "${LIVE_VOCODER_NO_AUTO_PULSE_OUT:-}" ]]; then
  _auto_out="$(.venv/bin/python -c "
from pulse_portaudio import resolve_stream_device_for_pulse_virt_mic
i = resolve_stream_device_for_pulse_virt_mic()
print(i if i is not None else '', end='')
" 2>/dev/null || true)"
  if [[ -n "${_auto_out}" ]]; then
    export LIVE_VOCODER_PORTAUDIO_OUTPUT="${_auto_out}"
  fi
fi
exec .venv/bin/python live_vocoder.py "$@"
