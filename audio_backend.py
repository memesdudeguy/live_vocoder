"""
Detect the audio stack PortAudio sees (host APIs, default devices) and OS session hints (PipeWire vs Pulse on Linux).
"""
from __future__ import annotations

import platform
import re
import shutil
import subprocess
import sys
from typing import Any


def _pactl_pulse_server_hint() -> str | None:
    if not shutil.which("pactl"):
        return None
    try:
        proc = subprocess.run(
            ["pactl", "info"],
            capture_output=True,
            text=True,
            timeout=6,
        )
    except (OSError, subprocess.TimeoutExpired):
        return None
    if proc.returncode != 0 or not proc.stdout:
        return None
    for line in proc.stdout.splitlines():
        if re.match(r"^Server Name\s*:", line, re.I):
            return line.split(":", 1)[-1].strip()
    return None


def _classify_pulse_server(server_line: str) -> str:
    low = server_line.lower()
    if "pipewire" in low:
        return "PipeWire (pulse compatibility / pipewire-pulse)"
    if "pulseaudio" in low:
        return "PulseAudio"
    return "Pulse-compatible server"


def format_audio_backend_report(
    *,
    preferred_input: int | None = None,
    preferred_output: int | None = None,
) -> str:
    """
    Human-readable report: OS kernel/userland, PortAudio version, host APIs (WASAPI/MME/ALSA/…),
    default I/O devices and which host API each uses.
    """
    lines: list[str] = ["── Audio stack (PortAudio / sounddevice) ──"]
    uname = platform.release()
    lines.append(f"OS: {sys.platform} · kernel/release: {uname!r} · machine: {platform.machine()}")

    pulse_line = _pactl_pulse_server_hint()
    if pulse_line:
        lines.append(f"Session: {_classify_pulse_server(pulse_line)} (`pactl`: {pulse_line})")
    elif sys.platform.startswith("linux"):
        lines.append("Session: no `pactl` in PATH (install pipewire-pulse or pulseaudio-utils for routing tools)")
    elif sys.platform == "win32":
        lines.append(
            "Session: Windows audio stack → PortAudio usually exposes **MME**, **DirectSound**, **WASAPI** "
            "(and **ASIO** if a driver provides it). Prefer **WASAPI** for lowest latency when listed."
        )
    elif sys.platform == "darwin":
        lines.append("Session: macOS Core Audio → PortAudio **Core Audio** host API")

    try:
        import sounddevice as sd
    except Exception as e:
        lines.append(f"sounddevice: not importable ({e})")
        return "\n".join(lines)

    try:
        ver = sd.get_portaudio_version()
        if isinstance(ver, tuple) and len(ver) >= 2:
            lines.append(f"PortAudio: {ver[1]} (numeric {ver[0]})")
        else:
            lines.append(f"PortAudio: {ver!r}")
    except Exception as e:
        lines.append(f"PortAudio version: (unknown: {e})")

    try:
        apis = sd.query_hostapis()
    except Exception as e:
        lines.append(f"Host APIs: query failed ({e})")
        return "\n".join(lines)

    lines.append("Host APIs (what your build of PortAudio compiled in):")
    for i, api in enumerate(apis):
        name = api.get("name", "?")
        devs = api.get("devices", [])
        lines.append(f"  [{i}] {name} — device indices: {devs!r}")

    def _one_dev(label: str, idx: int | None) -> None:
        if idx is None:
            return
        try:
            d: dict[str, Any] = sd.query_devices(idx)  # type: ignore[assignment]
        except Exception as e:
            lines.append(f"{label}: device {idx} ({e})")
            return
        ha_i = int(d.get("hostapi", -1))
        ha_name = apis[ha_i]["name"] if 0 <= ha_i < len(apis) else "?"
        ch_in = int(d.get("max_input_channels") or 0)
        ch_out = int(d.get("max_output_channels") or 0)
        nm = d.get("name", "?")
        lines.append(
            f"{label}: [{idx}] {nm!r} · host API [{ha_i}] {ha_name!r} · max_in={ch_in} max_out={ch_out}"
        )

    try:
        d_in, d_out = sd.default.device
    except Exception:
        d_in, d_out = None, None

    lines.append("PortAudio defaults (used if you do not pass --input-device / --output-device):")
    _one_dev("  default input", int(d_in) if d_in is not None else None)
    _one_dev("  default output", int(d_out) if d_out is not None else None)

    if preferred_input is not None:
        _one_dev("  app input (this session)", preferred_input)
    if preferred_output is not None:
        _one_dev("  app output (this session)", preferred_output)

    ff = shutil.which("ffmpeg")
    if ff:
        lines.append(f"ffmpeg: {ff} (decodes MP3/FLAC/… carriers; WAV does not need ffmpeg)")
    else:
        lines.append(
            "ffmpeg: **not in PATH** — install `ffmpeg` for MP3/FLAC/etc. carriers (WAV-only works without it)."
        )

    lines.append(
        "Driver/vendor hints: device **names** above often show the chip or driver "
        "(e.g. Realtek, USB Audio, Focusrite). The **host API** line is the actual audio driver path PortAudio uses."
    )
    return "\n".join(lines)
