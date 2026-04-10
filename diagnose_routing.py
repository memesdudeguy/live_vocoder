#!/usr/bin/env python3
"""
Pulse/PipeWire + PortAudio routing checklist (monitor path, virtual mic, defaults).

Used by ``live_vocoder.py --diagnose-routing`` and the GTK **Full routing report** button.
"""
from __future__ import annotations

import os
import sys


def format_diagnosis_report(sink_name: str, description: str) -> str:
    """
    Human-readable report for the given null-sink name and UI description.

    Does not load/unload modules or move streams — read-only ``pactl`` / device queries.
    """
    lines: list[str] = [
        "=== Live Vocoder — monitor & routing ===",
        "",
        "Virtual mic path: PortAudio (pulse/pipewire) → PULSE_SINK → null sink → "
        f"{sink_name}_mic (or Monitor of {description}).",
        f"Local monitor: module-loopback from {sink_name}.monitor → default output (when toggles ON in GTK).",
        "",
    ]

    try:
        from pulse_portaudio import (
            PULSE_VIRT_MIC_TROUBLESHOOT,
            resolve_stream_device_for_pulse_virt_mic,
        )
        from pulse_virtmic import (
            get_default_sink_name,
            get_default_source_name,
            monitor_source_name,
            pactl_available,
            sink_exists,
            source_exists,
            virtual_mic_source_name,
        )
    except ImportError as e:
        return "\n".join(lines + [f"(import error: {e})"])

    mon = monitor_source_name(sink_name)
    mic = virtual_mic_source_name(sink_name)

    lines.append("— Environment (this shell / process) —")
    for key in (
        "LIVE_VOCODER_PORTAUDIO_OUTPUT",
        "LIVE_VOCODER_PULSE_SINK",
        "LIVE_VOCODER_PULSE_DESCRIPTION",
        "LIVE_VOCODER_NO_AUTO_PULSE_OUT",
        "LIVE_VOCODER_SKIP_PULSE_PLAYBACK_CHECK",
    ):
        v = os.environ.get(key, "").strip()
        lines.append(f"  {key}={'(unset)' if not v else repr(v)}")
    lines.append(f"  PULSE_SINK (if set here)={os.environ.get('PULSE_SINK', '(unset)')!r}")
    lines.append("")

    if not pactl_available():
        lines.append("pactl: NOT on PATH — install pipewire-pulse / pulseaudio-utils.")
        lines.append(PULSE_VIRT_MIC_TROUBLESHOOT)
        return "\n".join(lines)

    lines.append("pactl: ok")
    ds = get_default_sink_name()
    dsrc = get_default_source_name()
    lines.append(f"  Default output sink: {ds or '(none)'}")
    lines.append(f"  Default input source: {dsrc or '(none)'}")
    if dsrc and sink_name not in dsrc.lower() and "livevocoder" not in dsrc.lower():
        lines.append(
            "  → Default mic is not the Live Vocoder device — expected for capture; "
            "the app records PipeWire **default source** (unmute it in System Settings)."
        )
    lines.append("")

    lines.append(f"— Null sink `{sink_name}` ({description!r}) —")
    lines.append(f"  Sink exists: {'yes' if sink_exists(sink_name) else 'no (created when GTK opens or on Start)'}")
    lines.append(f"  Source `{mic}`: {'yes' if source_exists(mic) else 'no (use Monitor / fix remap)'}")
    lines.append(f"  Monitor `{mon}`: {'yes' if source_exists(mon) else 'no'}")
    lines.append("")

    lines.append("— PortAudio (duplex pulse/pipewire for this app) —")
    try:
        import sounddevice as sd

        idx = resolve_stream_device_for_pulse_virt_mic()
        if idx is None:
            lines.append("  No suitable pulse/pipewire duplex device — see list_sounddevices.sh")
        else:
            di = sd.query_devices(idx)
            n = di.get("name", "?")
            inch = int(di.get("max_input_channels") or 0)
            outch = int(di.get("max_output_channels") or 0)
            lines.append(f"  Index [{idx}] {n!r}  in_ch={inch} out_ch={outch}")
    except Exception as e:
        lines.append(f"  (sounddevice error: {e})")
    lines.append("")

    lines.append("— GTK monitor toggles (reference) —")
    lines.append("  • Virtual mic (Pulse): sends vocoder to null sink for Discord/OBS.")
    lines.append("  • Monitor on my speakers/headphones: allow loopback to default output.")
    lines.append("  • Hear vocoded… / Hear dry mic: what the loopback carries.")
    lines.append("  If monitor is silent: unmute **Input** and **Output** in system Sound; mic must not be 0%.")
    lines.append("")
    lines.append(PULSE_VIRT_MIC_TROUBLESHOOT)
    return "\n".join(lines)


def run_diagnose(sink_name: str, description: str) -> int:
    """Print :func:`format_diagnosis_report` to stdout; return 0."""
    print(format_diagnosis_report(sink_name, description))
    return 0


def main(argv: list[str] | None = None) -> int:
    """``python diagnose_routing.py`` — uses env / defaults for sink name."""
    from pulse_virtmic import resolved_pulse_sink_identity

    sn, sd = resolved_pulse_sink_identity()
    return run_diagnose(sn, sd)


if __name__ == "__main__":
    sys.exit(main())
