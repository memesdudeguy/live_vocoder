"""
Pick a PortAudio **output** device that talks to PipeWire/Pulse so **PULSE_SINK** works.

If the stream opens a raw ALSA **hw:*,*** device, the environment variable is ignored and
audio never reaches the null sink — other apps then hear silence on the “virtual mic”.

Env:
  ``LIVE_VOCODER_PORTAUDIO_OUTPUT`` — force output device **index** (from ``python -m sounddevice``;
  prefer **pulse** or **pipewire** ALSA device, not ``hw:``).
  ``LIVE_VOCODER_SKIP_PULSE_PLAYBACK_CHECK`` — set to ``1`` to skip the post-start ``pactl``
  sink-input probe (if detection false-alarms on your system).
  ``LIVE_VOCODER_PULSE_SINK`` / ``LIVE_VOCODER_PULSE_DESCRIPTION`` — unique null-sink name + label
  so this app does not share ``live_vocoder`` with another virtual mic (see ``pulse_virtmic``).
  ``LIVE_VOCODER_PULSE_MOVE_SETTLE_SEC`` — seconds to wait after opening the stream before the first
  ``move-sink-input`` (default ``0.05``); increase slightly if audio **cuts out** on **Start**.
  ``LIVE_VOCODER_STREAM_LATENCY`` — ``high`` (default) vs ``low`` for PortAudio; use ``high`` if you
  hear glitches / dropouts.

Linux checklist (Arch examples):

1. ``python -m sounddevice`` — use a **duplex** **pulse** or **pipewire** index when possible, or rely on ``./run.sh``
   (auto-picks duplex on Linux). ``export LIVE_VOCODER_PORTAUDIO_OUTPUT=<index>`` to override (must have input+output for virtual mic).
2. Packages: ``sudo pacman -S pipewire-alsa pipewire-pulse`` (PortAudio → PipeWire + ``pactl``).
3. If sockets are stale: ``systemctl --user restart pipewire pipewire-pulse``.
4. After **Start**: ``pactl list short sources | grep -E 'live_vocoder|Virtual'`` — expect
   ``live_vocoder_mic`` or use **Monitor of LiveVocoder** in Discord/OBS if remap failed.
"""
from __future__ import annotations

import os

# Appended to status lines when Pulse routing or virtual-mic setup fails (keep on one screen line).
PULSE_VIRT_MIC_TROUBLESHOOT = (
    "Troubleshoot: run `python -m sounddevice`, set `export LIVE_VOCODER_PORTAUDIO_OUTPUT=<idx>` "
    "for a **pipewire**/**pulse** output; Arch: `sudo pacman -S pipewire-alsa pipewire-pulse` then "
    "`systemctl --user restart pipewire pipewire-pulse`; after Start run "
    "`pactl list short sources | grep live_vocoder`."
)


def _env_forced_output_index() -> int | None:
    raw = os.environ.get("LIVE_VOCODER_PORTAUDIO_OUTPUT", "").strip()
    if not raw.isdigit():
        return None
    try:
        import sounddevice as sd

        idx = int(raw)
        di = sd.query_devices(idx)
        if int(di.get("max_output_channels") or 0) < 1:
            return None
        return idx
    except Exception:
        return None


def resolve_output_device_for_pulse_virt_mic() -> int | None:
    """
    Pick a PortAudio **output** that talks to Pulse/PipeWire so ``PULSE_SINK`` is honored.

    Many systems expose the plugin as ``pulse`` or ``pipewire``; some use longer names
    (e.g. ``PulseAudio``) — we match exact names first, then substring ``pulse``/``pipewire``.

    Returns ``sounddevice`` output device index, or ``None`` if nothing suitable exists.
    """
    forced = _env_forced_output_index()
    if forced is not None:
        return forced

    try:
        import sounddevice as sd
    except ImportError:
        return None

    outs: list[tuple[int, str, str]] = []
    for i, d in enumerate(sd.query_devices()):
        if int(d.get("max_output_channels") or 0) < 1:
            continue
        name = (d.get("name") or "").strip()
        outs.append((i, name, name.lower()))

    # Prefer **pulse** before **pipewire**: on many Arch/PipeWire-Pulse setups the `pulse` ALSA PCM
    # honors `PULSE_SINK` more reliably for sink-input visibility than `pipewire` alone.
    for want in ("pulse", "pipewire", "default"):
        for i, _name, nl in outs:
            if nl == want:
                return i

    for i, _name, nl in outs:
        if "pulse" in nl or "pipewire" in nl:
            return i

    return None


def resolve_duplex_pulse_device() -> int | None:
    """
    One PortAudio index that supports **both** capture and playback on the Pulse/PipeWire
    ALSA plugin. Using the same PCM for input and output avoids ALSA full-duplex bugs
    (PortAudio asserts on teardown when mic and speakers are different hardware nodes).
    """
    forced = _env_forced_output_index()
    if forced is not None:
        try:
            import sounddevice as sd

            di = sd.query_devices(forced)
            if int(di.get("max_input_channels") or 0) >= 1 and int(di.get("max_output_channels") or 0) >= 1:
                return forced
        except Exception:
            pass

    try:
        import sounddevice as sd
    except ImportError:
        return None

    def first_duplex(name_wanted: str) -> int | None:
        want = name_wanted.lower()
        for i, d in enumerate(sd.query_devices()):
            if int(d.get("max_input_channels") or 0) < 1:
                continue
            if int(d.get("max_output_channels") or 0) < 1:
                continue
            name = (d.get("name") or "").strip().lower()
            if name == want:
                return i
        return None

    for candidate in ("pulse", "pipewire", "default"):
        idx = first_duplex(candidate)
        if idx is not None:
            return idx

    for i, d in enumerate(sd.query_devices()):
        if int(d.get("max_input_channels") or 0) < 1:
            continue
        if int(d.get("max_output_channels") or 0) < 1:
            continue
        nl = (d.get("name") or "").strip().lower()
        if "pulse" in nl or "pipewire" in nl:
            return i
    return None


def resolve_stream_device_for_pulse_virt_mic() -> int | None:
    """
    One PortAudio index for **both** mic capture and vocoder playback into PipeWire.

    Prefer a **duplex** ``pulse`` / ``pipewire`` / ``default`` device so the capture side
    follows the user's default **microphone** in WirePlumber while playback honors
    ``PULSE_SINK``. Picking an output-only ALSA node (or different in/out indices) often
    routes the wrong input or breaks full-duplex.

    If no duplex Pulse entry exists, falls back to :func:`resolve_output_device_for_pulse_virt_mic`
    (playback-only; may fail to open ``sd.Stream`` with input channels — then install
    ``pipewire-alsa`` or pick a duplex device index).
    """
    d = resolve_duplex_pulse_device()
    if d is not None:
        return d
    return resolve_output_device_for_pulse_virt_mic()
