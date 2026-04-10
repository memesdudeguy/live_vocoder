"""
Cross-platform virtual microphone routing for the live vocoder.

1. **Linux/BSD (and WSL with PipeWire):** PulseAudio / PipeWire null sink + remapped
   source (see ``pulse_virtmic``) when ``pactl`` is available.
2. **Fallback (Windows / macOS / Linux without Pulse):** Play through a **virtual
   audio cable** output (VB-Audio Cable, BlackHole, etc.) so other apps record from
   the cable's other end.

On Linux + PipeWire, pick **… Virtual Mic** in Chromium/Firefox when the site asks for a
microphone (PipeWire portal). **Steam:** Settings → Voice → same device name.
If a **Flatpak** app’s mic list is empty, allow PipeWire socket access (see Flatpak docs)
or use the non-Flatpak build.

For **tab-only** capture without a system device, the web UI **Browser mic** stream still
works via getUserMedia in that tab only.
"""
from __future__ import annotations

import os
import platform

import sounddevice as sd

from pulse_portaudio import PULSE_VIRT_MIC_TROUBLESHOOT
from pulse_virtmic import VirtMicSetup, pactl_available, setup_virt_mic, teardown_virt_mic

__all__ = [
    "find_virtual_cable_output",
    "setup_virt_mic_unified",
    "teardown_unified_virt_mic",
]


def _dedupe_preserve(seq: list[str]) -> list[str]:
    seen: set[str] = set()
    out: list[str] = []
    for s in seq:
        if not s or s in seen:
            continue
        seen.add(s)
        out.append(s)
    return out


def find_virtual_cable_output(hint: str | None = None) -> tuple[int | None, str]:
    """
    Find a PortAudio output device likely to be a virtual cable / loopback sink.

    Returns (device_index_or_none, device_name_or_empty).
    """
    hints: list[str] = []
    if hint and str(hint).strip():
        hints.append(str(hint).strip().lower())
    system = platform.system().lower()
    if system == "windows":
        hints.extend(
            [
                "cable input",
                "vb-audio virtual cable",
                "vb-audio",
                "virtual cable",
            ]
        )
    elif system == "darwin":
        hints.extend(
            [
                "blackhole 2ch",
                "blackhole 16ch",
                "blackhole",
                "loopback",
                "soundflower",
            ]
        )
    elif system == "linux":
        # ALSA `snd-aloop` / similar — no PipeWire null sink, but a cable-style output name.
        hints.extend(
            [
                "hw:loopback",
                "loopback",
                "aloop",
                "snd_aloop",
            ]
        )

    hints = _dedupe_preserve(hints)

    try:
        devices = sd.query_devices()
    except Exception as e:
        return None, f"(query_devices failed: {e})"

    for i, d in enumerate(devices):
        if int(d.get("max_output_channels") or 0) < 1:
            continue
        name = (d.get("name") or "").lower()
        for h in hints:
            if h in name:
                return i, str(d.get("name") or "")

    return None, ""


def setup_virt_mic_unified(
    *,
    sink_name: str,
    description: str,
    hear_on_default_sink: bool,
    cable_name_hint: str | None = None,
) -> tuple[bool, VirtMicSetup | None, int | None, str]:
    """
    Try Pulse/PipeWire virtual mic first; otherwise a virtual cable output device.

    Returns:
        (ok, pulse_setup_or_none, portaudio_output_index_or_none, message)

    When ``pulse_setup_or_none`` is set, play with ``outdev=None`` and ``PULSE_SINK``
    (already applied). When ``portaudio_output_index_or_none`` is set, play only to
    that device (no Pulse).
    """
    cable_hint = cable_name_hint or os.environ.get("LIVE_VOCODER_VIRT_DEVICE", "").strip()
    cable_hint = cable_hint or None

    pulse_fail: str | None = None
    if pactl_available():
        ok, psetup, msg = setup_virt_mic(
            sink_name=sink_name,
            description=description,
            hear_on_default_sink=hear_on_default_sink,
        )
        if ok:
            return True, psetup, None, msg
        pulse_fail = msg
    elif platform.system() == "Linux":
        pulse_fail = (
            "**`pactl` not found** (nothing provides the PipeWire-Pulse CLI on your PATH). "
            "Install **pipewire-pulse** (Arch: `sudo pacman -S pipewire-pulse`; "
            "Debian/Ubuntu: `sudo apt install pipewire-pulse`; Fedora: `sudo dnf install pipewire-pulseaudio` "
            "or `pulseaudio-utils`). Then run the app from the same graphical session as your desktop "
            "(SSH without PipeWire’s user socket will not work). "
            "If Python is in a container/Flatpak, use a host venv or ensure `pactl` is on `PATH`."
        )

    idx, devname = find_virtual_cable_output(cable_hint)
    if idx is not None:
        plat = platform.system()
        tip = (
            "In Discord/OBS, choose the **other end** of the cable as the microphone "
            "(e.g. **CABLE Output** on Windows, **BlackHole** on macOS)."
        )
        if plat == "Linux":
            tip += " Linux ALSA loopback: record from the **capture** side of `snd-aloop`."
        return (
            True,
            None,
            idx,
            f"Virtual mic ({plat}): sending audio to output **[{idx}] {devname}**. {tip}",
        )

    plat = platform.system()
    cable_part = (
        f"No virtual cable output matched in PortAudio"
        f"{f' (hint: {cable_hint!r})' if cable_hint else ''}. "
        "List devices with `python -m sounddevice`. Windows: **VB-Audio Virtual Cable**. "
        "macOS: **BlackHole 2ch**. Linux without Pulse: **snd-aloop** (`sudo modprobe snd-aloop`) "
        "or set **LIVE_VOCODER_VIRT_DEVICE** / **--virt-mic-device** to a substring of the output name."
    )
    parts = [f"Virtual mic unavailable on {plat}."]
    if pulse_fail:
        parts.append(pulse_fail)
    parts.append(cable_part)
    parts.append(PULSE_VIRT_MIC_TROUBLESHOOT)
    return False, None, None, " ".join(parts)


def teardown_unified_virt_mic(
    setup: VirtMicSetup | None,
    *,
    unload_remapped_mic: bool = True,
) -> None:
    """Unload Pulse modules; pass ``unload_remapped_mic=False`` when stopping a stream but keeping the virtual mic."""
    teardown_virt_mic(setup, unload_remapped_mic=unload_remapped_mic)
