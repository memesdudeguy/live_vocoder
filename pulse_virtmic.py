"""
PulseAudio / PipeWire-Pulse: null sink + remapped source = virtual microphone for other apps.

This process plays to the null sink (**PULSE_SINK**). The app must open a **Pulse-backed**
PortAudio output (e.g. ALSA device ``pulse``); raw ``hw:*`` ignores ``PULSE_SINK``.
A **module-remap-source** (or **module-virtual-source** fallback on PipeWire) exposes
``<sink_name>_mic`` as a capture device with **Audio/Source/Virtual** metadata so
WirePlumber, Steam, native apps, and browser portals can list it like a real mic.

**Isolation:** ``move-sink-input`` / mute helpers only touch **this process’s** playback streams.
Loopback unload only removes modules that route **from this app’s monitor** (see
:func:`unload_loopbacks_for_pulse_source`). ``PULSE_SINK`` applies to **this Python process
only**, not OpenMod/OBS/etc. Use env ``LIVE_VOCODER_PULSE_SINK`` (and optional
``LIVE_VOCODER_PULSE_DESCRIPTION``) for a unique sink name if ``live_vocoder`` would clash.

Requires: pactl (pipewire-pulse or pulseaudio on Arch); **pipewire-alsa** recommended on Linux.
"""
from __future__ import annotations

import os
import re
import shutil
import subprocess
import sys
import time
from dataclasses import dataclass


def pactl_available() -> bool:
    return shutil.which("pactl") is not None


def _existing_sinks_matching_live_vocoder() -> list[str]:
    """
    Sinks whose name contains ``live_vocoder`` (case-insensitive), sorted for stable choice.

    Used when ``LIVE_VOCODER_PULSE_SINK`` is unset so a WirePlumber/user-created
    ``live_vocoder2`` (etc.) is reused instead of defaulting to ``live_vocoder`` and missing
    the remapped monitor / ``*_mic`` path.
    """
    p = _run(["pactl", "list", "short", "sinks"])
    if p.returncode != 0 or not (p.stdout or "").strip():
        return []
    out: list[str] = []
    for line in p.stdout.strip().splitlines():
        parts = line.split("\t")
        if len(parts) >= 2:
            n = parts[1].strip()
            if "live_vocoder" in n.lower():
                out.append(n)
    return sorted(set(out))


def format_live_vocoder_sink_discovery_hint() -> str:
    """
    Human-readable hint when GTK shows no virtual input: list existing ``*live_vocoder*`` sinks
    or suggest creating the default null sink.
    """
    if not pactl_available():
        return ""
    cands = _existing_sinks_matching_live_vocoder()
    if cands:
        return (
            "PipeWire sinks matching *live_vocoder*: "
            + ", ".join(f"`{x}`" for x in cands)
            + ". If this session uses the wrong sink, set LIVE_VOCODER_PULSE_SINK to one of these and reopen the app."
        )
    return (
        "No *live_vocoder* sink in `pactl list short sinks`. Close and reopen the window (GTK recreates it), or run:\n"
        "  pactl load-module module-null-sink sink_name=live_vocoder rate=48000 channels=2"
    )


def resolved_pulse_sink_identity(
    *,
    default_sink: str = "live_vocoder",
    default_desc: str = "LiveVocoder",
) -> tuple[str, str]:
    """
    Logical null-sink name + UI description (env overrides for multi-app / multi-instance).

    - ``LIVE_VOCODER_PULSE_SINK`` — sink name (default ``live_vocoder``). Use a **distinct** name
      (e.g. ``live_vocoder_music``) so this app does not share a sink with another virtual mic.
    - ``LIVE_VOCODER_PULSE_DESCRIPTION`` — label in KDE/pavucontrol (default ``LiveVocoder``).

    If the env sink name is **unset** and a sink matching ``*live_vocoder*`` already exists
    (e.g. ``live_vocoder2``), that sink is used so GTK/virtual-mic setup targets the same
    PipeWire node you configured.
    """
    raw = os.environ.get("LIVE_VOCODER_PULSE_SINK", "").strip()
    desc_raw = os.environ.get("LIVE_VOCODER_PULSE_DESCRIPTION", "").strip()
    if not raw:
        cands = _existing_sinks_matching_live_vocoder()
        if default_sink in cands:
            name = default_sink
        elif cands:
            name = cands[0]
        else:
            name = default_sink
    else:
        name = "".join(c if (c.isalnum() or c in "_-") else "_" for c in raw).strip("_")
        if not name:
            name = default_sink
    desc = (desc_raw or default_desc).replace("'", "")
    if not desc:
        desc = default_desc
    return (name, desc)


def _pipewire_pulse_server() -> bool:
    """True if pactl talks to PipeWire's pulse compatibility layer (not legacy PulseAudio)."""
    proc = _run(["pactl", "info"])
    if proc.returncode != 0 or not proc.stdout:
        return False
    return "pipewire" in proc.stdout.lower()


def _run(args: list[str], check: bool = False) -> subprocess.CompletedProcess[str]:
    return subprocess.run(
        args,
        capture_output=True,
        text=True,
        check=check,
    )


def _pw_safe_suffix(name: str) -> str:
    """PipeWire node.name: alphanumeric + underscore."""
    return "".join(c if c.isalnum() else "_" for c in name).strip("_") or "sink"


def sink_exists(sink_name: str) -> bool:
    p = _run(["pactl", "list", "short", "sinks"])
    if p.returncode != 0:
        return False
    for line in p.stdout.strip().splitlines():
        parts = line.split("\t")
        if len(parts) >= 2 and parts[1] == sink_name:
            return True
    return False


def source_exists(source_name: str) -> bool:
    p = _run(["pactl", "list", "short", "sources"])
    if p.returncode != 0:
        return False
    for line in p.stdout.strip().splitlines():
        parts = line.split("\t")
        if len(parts) >= 2 and parts[1] == source_name:
            return True
    return False


def ensure_null_sink(sink_name: str, description: str) -> bool:
    """Create a null sink if missing. Returns False if pactl failed."""
    if not pactl_available():
        return False
    if sink_exists(sink_name):
        return True
    d = description.replace("'", "")
    sn = _pw_safe_suffix(sink_name)
    # Null sink must use **Audio/Sink** (not Audio/Sink/Virtual): on PipeWire 1.6+,
    # module-null-sink with media.class=Audio/Sink/Virtual loads a module id but **no sink**
    # appears in `pactl list short sinks`, so routing breaks. The remapped *mic* source
    # still uses Audio/Source/Virtual in ensure_virt_mic_input_source.
    sink_mc = "Audio/Sink"
    props = (
        f"device.description='{d}' node.description='{d}' "
        f"node.name=audio_sink_{sn} "
        f"media.class={sink_mc}"
    )
    # Match LiveVocoder PortAudio (48 kHz stereo). Default null-sink is often 44100 → heavy resampling → muffled audio.
    args = [
        "pactl",
        "load-module",
        "module-null-sink",
        f"sink_name={sink_name}",
        "rate=48000",
        "channels=2",
        f"sink_properties={props}",
    ]
    proc = _run(args)
    if sink_exists(sink_name):
        return True
    # Module id returned but no sink (stuck state on some PipeWire builds) — unload and retry minimal props.
    out = (proc.stdout or "").strip()
    if out.isdigit():
        unload_module(int(out))
    proc2 = _run(
        [
            "pactl",
            "load-module",
            "module-null-sink",
            f"sink_name={sink_name}",
            "rate=48000",
            "channels=2",
            f"sink_properties=device.description='{d}'",
        ]
    )
    ok = sink_exists(sink_name)
    if not ok:
        out2 = (proc2.stdout or "").strip()
        if out2.isdigit():
            unload_module(int(out2))
    return ok


def monitor_source_name(sink_name: str) -> str:
    return f"{sink_name}.monitor"


def virtual_mic_source_name(sink_name: str) -> str:
    """Pulse source name for the remapped virtual mic (input in other apps)."""
    return f"{sink_name}_mic"


def status_hint_missing_virt_mic_source(description: str, sink_name: str) -> str:
    """Short line for session status when the remapped ``*_mic`` source is absent from ``pactl``."""
    d = description.replace("'", "")
    return (
        f"**Pulse:** `{virtual_mic_source_name(sink_name)}` not listed — in pavucontrol (All devices) use "
        f"**Monitor of {d}** or **`{monitor_source_name(sink_name)}`**; Flatpak apps need PipeWire mic access."
    )


def _sink_names_equivalent(a: str, b: str) -> bool:
    """True if Pulse sink/source logical names refer to the same null sink (handles @n vs name)."""
    if not a or not b:
        return False
    if a == b:
        return True
    na = _norm_sink_token(a)
    nb = _norm_sink_token(b)
    if na == nb:
        return True
    if len(na) > 2 and len(nb) > 2 and (na in nb or nb in na):
        return True
    return False


def discover_monitor_source_name(sink_name: str) -> str:
    """
    Logical Pulse name for the null sink’s monitor. Almost always ``{sink}.monitor``;
    resolve via ``pactl list sources`` when the short name is missing (some PipeWire builds).
    """
    expected = monitor_source_name(sink_name)
    if not pactl_available():
        return expected
    if source_exists(expected):
        return expected
    p = _run(["pactl", "list", "sources"])
    if p.returncode != 0 or not (p.stdout or "").strip():
        return expected
    idx_to_name = _parse_short_sink_index_to_name()
    text = p.stdout
    pos = 0
    while True:
        start = text.find("Source #", pos)
        if start == -1:
            break
        end = text.find("Source #", start + 1)
        block = text[start:] if end == -1 else text[start:end]
        mo = re.search(r"(?:^|\n)\s*Monitor of Sink:\s*(\S+)", block)
        if mo:
            ref = mo.group(1).strip().strip("\"'")
            resolved = idx_to_name.get(int(ref), ref) if ref.isdigit() else ref
            if _sink_names_equivalent(str(resolved), sink_name):
                nm = re.search(r"(?:^|\n)\s*Name:\s*(\S+)", block)
                if nm:
                    return nm.group(1).strip()
        pos = end if end != -1 else len(text)
    return expected


def _virt_mic_source_properties_string(
    *,
    label: str,
    node_slug: str,
    pipewire: bool,
    media_class_override: str | None = None,
) -> str:
    """
    Metadata for WirePlumber / xdg-desktop-portal / Steam / browsers.

    - PipeWire: ``Audio/Source/Virtual`` is the usual class for software mics (not monitors).
    - Some desktops (e.g. KDE simple device lists) hide ``/Virtual``; pass ``media_class_override='Audio/Source'`` as fallback.
    - ``device.form-factor=microphone`` improves listings in some pickers.
    """
    safe_label = label.replace("'", "")
    nick = safe_label[:64] if safe_label else "VirtualMic"
    if media_class_override is not None:
        media_class = media_class_override
    else:
        media_class = "Audio/Source/Virtual" if pipewire else "Audio/Source"
    return (
        f"device.description='{safe_label}' node.description='{safe_label}' "
        f"node.nick='{nick}' "
        f"node.name=audio_source_{node_slug} "
        f"media.class={media_class} "
        "device.form-factor=microphone "
        "device.icon-name=audio-input-microphone"
    )


def ensure_virt_mic_input_source(
    sink_name: str,
    description: str,
) -> tuple[int | None, str]:
    """
    Remap the null-sink monitor to a dedicated source (PipeWire: **Audio/Source/Virtual**)
    so it appears as a microphone in recording lists, Steam voice, and portal pickers.
    Returns (module_id_or_none, human hint for status messages).
    """
    src = virtual_mic_source_name(sink_name)
    if source_exists(src):
        d = description.replace("'", "")
        label = f"{d} Virtual Mic"
        return None, f"Mic input **{label}** (`{src}`) already present."

    # Null sink’s monitor can take a moment to appear in `pactl` after module-null-sink loads.
    time.sleep(0.06)
    mon = discover_monitor_source_name(sink_name)
    d = description.replace("'", "")
    label = f"{d} Virtual Mic"
    sn = _pw_safe_suffix(src)
    pipewire = _pipewire_pulse_server()
    props_full = _virt_mic_source_properties_string(label=label, node_slug=sn, pipewire=pipewire)
    props_plain_class = _virt_mic_source_properties_string(
        label=label,
        node_slug=sn,
        pipewire=pipewire,
        media_class_override="Audio/Source",
    )
    safe_l = label.replace("'", "")
    props_min = f"device.description='{safe_l}' node.description='{safe_l}'"

    attempts: list[tuple[str, str]] = [
        ("module-remap-source", props_full),
        ("module-remap-source", props_min),
    ]
    if pipewire:
        attempts.extend(
            [
                ("module-virtual-source", props_full),
                ("module-virtual-source", props_min),
                # KDE / some UIs omit Audio/Source/Virtual from the main input list
                ("module-remap-source", props_plain_class),
                ("module-virtual-source", props_plain_class),
            ]
        )

    last_err = ""
    for mod_name, props in attempts:
        p = _run(
            [
                "pactl",
                "load-module",
                mod_name,
                f"master={mon}",
                f"source_name={src}",
                f"source_properties={props}",
            ]
        )
        err = (p.stderr or p.stdout or "").strip()
        if p.returncode != 0:
            last_err = f"{mod_name}: {err or p.returncode}"
            continue
        raw = (p.stdout or "").strip()
        try:
            mid = int(raw)
        except ValueError:
            last_err = f"{mod_name}: expected numeric module id, got {raw!r}"
            continue
        time.sleep(0.08)
        if source_exists(src):
            tag = ""
            if mod_name != "module-remap-source" or props != props_full:
                tag = f" ({mod_name})"
            return mid, f"Mic input **{label}** (`{src}`){tag}."
        unload_module(mid)
        last_err = f"{mod_name}: module {mid} loaded but `{src}` missing from `pactl list short sources`"

    return None, (
        f"Virtual mic source failed ({last_err}). "
        f"Master tried: `{mon}`. {status_hint_missing_virt_mic_source(description, sink_name)}"
    )


def warm_pulse_virt_mic_devices(
    sink_name: str, description: str,
) -> tuple[bool, str, str]:
    """
    Create the null sink and remapped virtual mic **without** opening an audio stream
    (when the GTK window opens). Lets **{sink_name}_mic** appear in system input lists
    and Discord/OBS pickers before **Start**. On window close, :func:`teardown_pulse_virt_mic_full`
    tears this down so the next launch recreates a clean pipeline.

    Returns ``(ok, message, capture_kind)`` where ``capture_kind`` is ``\"remap\"`` if
    ``{sink}_mic`` exists, ``\"monitor\"`` if only the null-sink monitor is available
    (remap failed but OBS/Discord can use **Monitor of …**), or ``\"none\"`` on failure.
    """
    if not pactl_available():
        return False, "pactl not found (install pipewire-pulse).", "none"
    if not ensure_null_sink(sink_name, description):
        return (
            False,
            f"Could not create Pulse null sink {sink_name!r}. "
            f"Try: `pactl load-module module-null-sink sink_name={sink_name} rate=48000 channels=2`",
            "none",
        )
    apply_pulse_sink_env(sink_name)
    _mid, msg = ensure_virt_mic_input_source(sink_name, description)
    mic = virtual_mic_source_name(sink_name)
    if source_exists(mic):
        return True, msg, "remap"
    mon = discover_monitor_source_name(sink_name)
    if source_exists(mon):
        return (
            True,
            (
                f"{msg} Use **`{mon}`** (monitor of sink `{sink_name}`) as the recording device — "
                f"same audio as **`{mic}`** would provide if remap worked."
            ),
            "monitor",
        )
    return False, msg, "none"


def get_default_sink_name() -> str | None:
    p = _run(["pactl", "get-default-sink"])
    if p.returncode != 0:
        return None
    s = p.stdout.strip()
    return s or None


def get_default_source_name() -> str | None:
    p = _run(["pactl", "get-default-source"])
    if p.returncode != 0:
        return None
    s = p.stdout.strip()
    return s or None


def load_loopback_monitor_to_sink(
    monitor_source: str,
    sink_name: str,
    latency_msec: int = 25,
) -> tuple[int | None, str]:
    """Route monitor (what we play to virt sink) to a real sink so you can hear it too.

    Returns ``(module_id_or_none, error_or_empty)``; on failure ``error`` is truncated pactl output.
    """
    p = _run(
        [
            "pactl",
            "load-module",
            "module-loopback",
            f"source={monitor_source}",
            f"sink={sink_name}",
            f"latency_msec={latency_msec}",
        ]
    )
    if p.returncode != 0:
        err = (p.stderr or p.stdout or "").strip()
        return None, (err or f"pactl exit {p.returncode}")[:200]
    try:
        return int((p.stdout or "").strip()), ""
    except ValueError:
        raw = ((p.stdout or "").strip())[:80]
        return None, f"expected module id, got {raw!r}"[:200]


def find_existing_loopback_module_id_for_monitor_source(monitor_source: str) -> int | None:
    """
    Return ``module-loopback`` id that already routes ``monitor_source``, if any.

    Prevents stacking duplicate loopbacks on every **Start** (which glitches / cuts out audio on
    PipeWire when the monitor path is recreated repeatedly).
    """
    if not pactl_available():
        return None
    p = _run(["pactl", "list", "short", "modules"])
    if p.returncode != 0 or not (p.stdout or "").strip():
        return None
    needle_src = f"source={monitor_source}"
    needle_m = f"master={monitor_source}"
    for line in p.stdout.strip().splitlines():
        ll = line.lower()
        if "loopback" not in ll:
            continue
        compact = re.sub(r"\s+", "", line)
        if needle_src in compact or needle_m in compact:
            try:
                return int(line.split("\t", 1)[0].strip())
            except (ValueError, IndexError):
                continue
    return None


def load_loopback_monitor_to_default_with_fallback(
    monitor_source: str,
    *,
    latency_msec: int = 25,
) -> tuple[int | None, str]:
    """
    Load ``module-loopback`` from ``monitor_source`` to the user's default output.

    Reuses an existing loopback for this monitor when present (see
    :func:`find_existing_loopback_module_id_for_monitor_source`).

    Tries ``pactl get-default-sink`` first, then ``sink=@DEFAULT_SINK@`` if the named load fails
    (PipeWire-Pulse often accepts the latter when a concrete name fails).

    Returns ``(module_id_or_none, combined_error_or_empty_on_success)``.
    """
    existing = find_existing_loopback_module_id_for_monitor_source(monitor_source)
    if existing is not None:
        return existing, ""
    default = get_default_sink_name()
    errs: list[str] = []
    if default:
        mid, err = load_loopback_monitor_to_sink(
            monitor_source, default, latency_msec
        )
        if mid is not None:
            return mid, ""
        errs.append(err or "unknown error")
    mid2, err2 = load_loopback_monitor_to_sink(
        monitor_source, "@DEFAULT_SINK@", latency_msec
    )
    if mid2 is not None:
        return mid2, ""
    errs.append(f"@DEFAULT_SINK@: {err2 or 'unknown error'}")
    return None, "; ".join(errs)[:400]


def unload_module(module_id: int) -> bool:
    p = _run(["pactl", "unload-module", str(module_id)])
    if p.returncode != 0:
        err = (p.stderr or p.stdout or "").strip()
        print(
            f"[live_vocoder] pactl unload-module {module_id} failed: {err or p.returncode}",
            file=sys.stderr,
        )
        return False
    return True


def _loopback_module_connects_source(block: str, source_name: str) -> bool:
    """
    True if a ``pactl list modules`` block is a loopback whose **input** is ``source_name``.

    Strict matching avoids unloading other apps' virtual-mic monitor loopbacks.
    """
    if not re.search(r"module-loopback|libpipewire-module-loopback", block, re.I):
        return False
    esc = re.escape(source_name)
    if re.search(rf"(?i)source\s*=\s*[\"']?{esc}[\"']?(?:\s|,|$|\n)", block):
        return True
    if re.search(rf"(?i)master\s*=\s*[\"']?{esc}[\"']?(?:\s|,|$|\n)", block):
        return True
    if source_name not in block:
        return False
    for m in re.finditer(re.escape(source_name), block):
        s, e = m.span()
        before = block[s - 1] if s > 0 else " "
        after = block[e] if e < len(block) else " "
        if (before.isalnum() or before == "_") or (after.isalnum() or after == "_"):
            continue
        return True
    return False


def unload_loopbacks_for_pulse_source(source_name: str) -> int:
    """
    Unload ``module-loopback`` instances that route **from** ``source_name`` only.

    Does not unload loopbacks for other monitors (other virtual mics). Uses
    :func:`_loopback_module_connects_source` so we never tear down unrelated modules just because
    the word ``loopback`` appears in a long property string.
    """
    p = _run(["pactl", "list", "modules"])
    if p.returncode != 0 or not (p.stdout or "").strip():
        return 0
    text = p.stdout
    to_unload: list[int] = []
    pos = 0
    while True:
        start = text.find("Module #", pos)
        if start == -1:
            break
        end = text.find("Module #", start + 1)
        block = text[start:] if end == -1 else text[start:end]
        m = re.match(r"Module #(\d+)", block)
        if not m:
            pos = start + 8
            continue
        mid = int(m.group(1))
        if _loopback_module_connects_source(block, source_name):
            to_unload.append(mid)
        pos = end if end != -1 else len(text)
    n = 0
    for mid in to_unload:
        if unload_module(mid):
            n += 1
    return n


def find_sink_input_by_owner_module(owner_module_id: int) -> int | None:
    """
    Find the sink-input index created by ``module-loopback`` (Owner Module / module id).
    Used to mute only local speaker playback without tearing down the loopback.
    """
    proc = _run(["pactl", "list", "sink-inputs"])
    if proc.returncode != 0:
        return None
    text = proc.stdout or ""
    current: int | None = None
    for line in text.splitlines():
        s = line.strip()
        if s.startswith("Sink Input #"):
            try:
                current = int(s.replace("Sink Input #", "").strip().split()[0])
            except (ValueError, IndexError):
                current = None
            continue
        if current is None:
            continue
        if not (s.startswith("Owner Module:") or s.startswith("module:")):
            continue
        try:
            mid = int(s.split(":", 1)[1].strip())
        except (ValueError, IndexError):
            continue
        if mid == owner_module_id:
            return current
    return None


def discover_loopback_sink_input(module_id: int, *, retries: int = 20, delay_sec: float = 0.03) -> int | None:
    """Wait for PipeWire to register the loopback sink-input (race after load-module)."""
    for _ in range(retries):
        si = find_sink_input_by_owner_module(module_id)
        if si is not None:
            return si
        time.sleep(delay_sec)
    return None


def set_sink_input_mute(sink_input_id: int, muted: bool) -> bool:
    """Mute/unmute one capture → playback path (local speakers only; null sink unchanged)."""
    p = _run(
        [
            "pactl",
            "set-sink-input-mute",
            str(sink_input_id),
            "1" if muted else "0",
        ]
    )
    if p.returncode != 0:
        err = (p.stderr or p.stdout or "").strip()
        print(
            f"[live_vocoder] pactl set-sink-input-mute {sink_input_id} failed: {err or p.returncode}",
            file=sys.stderr,
        )
        return False
    return True


def set_sink_input_volume_percent(sink_input_id: int, percent: str) -> bool:
    """Sink-input volume, e.g. ``0%`` / ``100%`` (fallback when mute property is ignored)."""
    p = _run(["pactl", "set-sink-input-volume", str(sink_input_id), percent])
    return p.returncode == 0


def set_sink_input_suspend(sink_input_id: int, suspend: bool) -> bool:
    """Pause/resume a sink-input (PipeWire often respects this when mute is ignored)."""
    p = _run(
        [
            "pactl",
            "suspend-sink-input",
            str(sink_input_id),
            "1" if suspend else "0",
        ]
    )
    return p.returncode == 0


def apply_pulse_sink_env(sink_name: str) -> None:
    """
    Route **this process’s** Pulse/ALSA clients to ``sink_name`` via ``PULSE_SINK``.

    Child processes inherit the env var; **other apps** (Discord, OpenMod, OBS, …) are unchanged.
    """
    os.environ["PULSE_SINK"] = sink_name
    # So `pactl list sink-inputs` / pavucontrol show “Live Vocoder”, not only “python”.
    prop = os.environ.get("PULSE_PROP", "")
    if "application.name=" not in prop:
        extra = "application.name=Live Vocoder"
        os.environ["PULSE_PROP"] = f"{prop} {extra}".strip() if prop else extra


def _parse_short_sink_index_to_name() -> dict[int, str]:
    """Map numeric sink index from `pactl list short sinks` to sink name."""
    p = _run(["pactl", "list", "short", "sinks"])
    if p.returncode != 0 or not (p.stdout or "").strip():
        return {}
    out: dict[int, str] = {}
    for line in p.stdout.strip().splitlines():
        parts = line.split("\t")
        if len(parts) >= 2:
            try:
                out[int(parts[0].strip())] = parts[1].strip()
            except ValueError:
                continue
    return out


def _sink_input_block_looks_like_pulse_loopback(block: str) -> bool:
    """Monitor→speakers loopback: must not be moved to the null sink or mistaken for PortAudio."""
    if re.search(r"media\.name\s*=\s*\"[^\"]*[Ll]oopback", block):
        return True
    if re.search(r"node\.name\s*=\s*\"[^\"]*[Ll]oopback", block):
        return True
    if re.search(r"application\.name\s*=\s*\"[^\"]*[Ll]oopback", block):
        return True
    return False


def _sink_input_block_is_our_playback(block: str, pid: int) -> bool:
    """True if this sink-input is likely Live Vocoder’s PortAudio stream (not module-loopback)."""
    # Monitor → speakers loopback must stay on the default sink; never move it to live_vocoder.
    if _sink_input_block_looks_like_pulse_loopback(block):
        return False
    m = re.search(r"application\.process\.id\s*=\s*\"?(\d+)\"?", block)
    if m and int(m.group(1)) == pid:
        return True
    # ALSA pulse often omits process.id; PULSE_PROP sets this when the client connects.
    if re.search(r"application\.name\s*=\s*\"Live Vocoder\"", block):
        return True
    if re.search(r"application\.name\s*=\s*'Live Vocoder'", block):
        return True
    # Common for PortAudio → ALSA “pulse”: name is literally “ALSA plug-in [python…]”.
    if re.search(r"application\.name\s*=\s*\"ALSA plug-in[^\"]*python", block, re.I):
        return True
    # PipeWire: binary is python3 / python3.14 with ALSA plug-in client name (no “python” substring).
    mb = re.search(r"application\.process\.binary\s*=\s*\"([^\"]+)\"", block)
    if mb:
        binname = mb.group(1).strip().split("/")[-1]
        if re.match(r"^python\d*(\.\d+)?$", binname, re.I):
            if re.search(r"application\.name\s*=\s*\"ALSA plug-in", block, re.I):
                return True
    return False


def _sink_input_belongs_to_process_for_routing(
    block: str,
    pid: int,
    pulse_client_ids: set[str],
) -> bool:
    """
    True if this sink-input should be treated as our app’s playback for move / mute / detection.
    Uses property heuristics and, when available, ``Client:`` → ``pactl list clients`` (PipeWire).
    """
    if _sink_input_block_looks_like_pulse_loopback(block):
        return False
    if _sink_input_block_is_our_playback(block, pid):
        return True
    mc = re.search(r"^\s*Client:\s*(\S+)", block, re.MULTILINE)
    if mc and pulse_client_ids and mc.group(1) in pulse_client_ids:
        return True
    return False


def _sink_input_likely_python_client_on_virt_sink(block: str, pid: int) -> bool:
    """
    Strict match failed, but this sink-input is on our null sink: treat a python* client as ours
    unless it is clearly a loopback (PipeWire sometimes omits application.process.id on first listing).
    """
    if re.search(r"media\.name\s*=\s*\"[^\"]*[Ll]oopback", block):
        return False
    if re.search(r"node\.name\s*=\s*\"[^\"]*[Ll]oopback", block):
        return False
    if re.search(r"application\.name\s*=\s*\"[^\"]*[Ll]oopback", block):
        return False
    m = re.search(r"application\.process\.id\s*=\s*\"?(\d+)\"?", block)
    if m and int(m.group(1)) == pid:
        return True
    mb = re.search(r"application\.process\.binary\s*=\s*\"([^\"]+)\"", block)
    if mb:
        binname = mb.group(1).strip().split("/")[-1]
        if re.match(r"^python\d*(\.\d+)?$", binname, re.I):
            return True
    return False


def _sink_input_current_sink_name(block: str, sink_index_to_name: dict[int, str]) -> str | None:
    m = re.search(r"^\s*Sink:\s*(\S+)", block, re.MULTILINE)
    if not m:
        return None
    ref = m.group(1).strip()
    if ref.isdigit():
        return sink_index_to_name.get(int(ref), ref)
    return ref


def _norm_sink_token(s: str | None) -> str:
    if not s:
        return ""
    return s.strip().lstrip("@").strip()


def _sink_name_is_virt(sink_resolved: str, virt_sink_name: str) -> bool:
    """True if ``sink_resolved`` is the null sink that feeds the virtual mic."""
    a = _norm_sink_token(sink_resolved)
    b = _norm_sink_token(virt_sink_name)
    if not a or not b:
        return False
    return a == b or a.endswith(b) or b.endswith(a) or b in a


def _sink_input_routes_virt_monitor_to_non_virt_sink(
    block: str, virt_sink_name: str, sink_basename: str,
) -> bool:
    """
    True if this sink-input plays to a sink other than ``virt_sink_name`` and the stream is tied
    to this app's null-sink monitor (PipeWire may omit ``module-loopback`` in the driver line).
    """
    sink_index_to_name = _parse_short_sink_index_to_name()
    cur = _sink_input_current_sink_name(block, sink_index_to_name)
    if cur is None:
        return False
    if _sink_name_is_virt(cur, virt_sink_name):
        return False
    mon = monitor_source_name(sink_basename)
    bl = block.lower()
    if mon in block:
        return True
    if sink_basename in block and "monitor" in bl:
        return True
    if re.search(r"media\.name\s*=\s*[\"'][^\"']*[Ll]oopback", block):
        if sink_basename in block or mon in block:
            return True
    if "pipewire" in bl and "loopback" in bl and (sink_basename in block or mon in block):
        return True
    return False


def _sink_input_block_is_monitor_loopback(
    block: str, monitor_full: str, sink_basename: str
) -> bool:
    """
    True if this sink-input is a Pulse/PipeWire loopback playing the null-sink monitor.

    Stray loopbacks (not tracked in ``VirtMicSetup``) still reach headphones; unloading
    ``module-loopback`` by id sometimes misses duplicates or differently named modules.

    PipeWire often includes ``monitor_full`` in the block; some builds only show a
    ``Loopback … Monitor of …`` style ``media.name`` plus ``sink_basename``.
    """
    bl = block.lower()
    loopish = (
        "module-loopback" in bl
        or re.search(r"media\.name\s*=\s*[\"'][^\"']*[Ll]oopback", block)
        or "loopback from" in bl
        or "loopback of" in bl
    )
    if not loopish:
        return False
    if monitor_full in block:
        return True
    if sink_basename in block and "monitor" in bl:
        return True
    return False


def mute_monitor_loopback_sink_inputs(sink_name: str, muted: bool) -> int:
    """
    Mute/unmute **all** sink-inputs that loop ``sink_name.monitor`` to another sink.

    Used when local speaker monitor should be off: tears down tracked modules plus any
    duplicate/stray loopback paths PipeWire still shows as active sink-inputs.
    """
    if not pactl_available():
        return 0
    mon = monitor_source_name(sink_name)
    p = _run(["pactl", "list", "sink-inputs"])
    if p.returncode != 0 or not (p.stdout or "").strip():
        return 0
    text = p.stdout
    n = 0
    pos = 0
    while True:
        start = text.find("Sink Input #", pos)
        if start == -1:
            break
        end = text.find("Sink Input #", start + 1)
        block = text[start:] if end == -1 else text[start:end]
        m = re.match(r"Sink Input #(\d+)", block)
        if not m:
            pos = start + 12
            continue
        si_id = m.group(1)
        is_lb = _sink_input_block_is_monitor_loopback(block, mon, sink_name)
        is_route = _sink_input_routes_virt_monitor_to_non_virt_sink(
            block, sink_name, sink_name
        )
        if not is_lb and not is_route:
            pos = end if end != -1 else len(text)
            continue
        sid = int(si_id)
        if muted:
            changed = False
            if set_sink_input_mute(sid, True):
                changed = True
            if set_sink_input_volume_percent(sid, "0%"):
                changed = True
            if set_sink_input_suspend(sid, True):
                changed = True
            if changed:
                n += 1
        else:
            set_sink_input_suspend(sid, False)
            if set_sink_input_mute(sid, False):
                n += 1
            set_sink_input_volume_percent(sid, "100%")
        pos = end if end != -1 else len(text)
    return n


def mute_playback_sink_inputs_not_on_virt_sink(
    virt_sink_name: str,
    muted: bool,
    pid: int | None = None,
) -> int:
    """
    If this process has **more than one** playback sink-input and at least one is on
    ``virt_sink_name``, mute/unmute the others. Some PipeWire setups duplicate the ALSA client so
    headphones still get a stream after the VM path is fixed — muting the duplicate silences local
    output without touching the null-sink stream.

    Does **not** mute when only a single sink-input exists on a non-virt sink (that case needs
    ``move-sink-input`` only; muting would silence the virtual mic too).
    """
    if not pactl_available():
        return 0
    pid = int(os.getpid() if pid is None else pid)
    pulse_cids = _pactl_client_ids_for_pid(pid)
    sink_index_to_name = _parse_short_sink_index_to_name()
    p = _run(["pactl", "list", "sink-inputs"])
    if p.returncode != 0 or not (p.stdout or "").strip():
        return 0
    text = p.stdout
    ours: list[tuple[str, str]] = []
    pos = 0
    while True:
        start = text.find("Sink Input #", pos)
        if start == -1:
            break
        end = text.find("Sink Input #", start + 1)
        block = text[start:] if end == -1 else text[start:end]
        m = re.match(r"Sink Input #(\d+)", block)
        if not m:
            pos = start + 12
            continue
        si_id = m.group(1)
        if not _sink_input_belongs_to_process_for_routing(block, pid, pulse_cids):
            pos = end if end != -1 else len(text)
            continue
        cur = _sink_input_current_sink_name(block, sink_index_to_name)
        if cur is None:
            pos = end if end != -1 else len(text)
            continue
        ours.append((si_id, cur))
        pos = end if end != -1 else len(text)

    on_v = [t for t in ours if _sink_name_is_virt(t[1], virt_sink_name)]
    off_v = [t for t in ours if not _sink_name_is_virt(t[1], virt_sink_name)]
    if len(on_v) < 1 or len(off_v) < 1:
        return 0
    n = 0
    for si_id, _ in off_v:
        sid = int(si_id)
        if muted:
            if set_sink_input_mute(sid, True) or set_sink_input_volume_percent(sid, "0%"):
                n += 1
        else:
            set_sink_input_mute(sid, False)
            set_sink_input_volume_percent(sid, "100%")
    return n


def _sink_targets_for_move(sink_name: str) -> list[str]:
    """Try logical name, then numeric index from ``list short sinks`` (some PipeWire builds need it)."""
    out: list[str] = [sink_name]
    p = _run(["pactl", "list", "short", "sinks"])
    if p.returncode != 0 or not (p.stdout or "").strip():
        return out
    for line in p.stdout.strip().splitlines():
        parts = line.split("\t")
        if len(parts) >= 2 and parts[1].strip() == sink_name:
            idx = parts[0].strip()
            if idx.isdigit() and idx not in out:
                out.append(idx)
            break
    return out


def move_sink_inputs_for_pid_to_sink(sink_name: str, pid: int | None = None) -> int:
    """
    Force this process's playback stream(s) onto ``sink_name`` (tries sink name then numeric index).

    Prefer :func:`iter_move_to_virt_sink` after mute toggles; WirePlumber may need several passes.
    """
    if not pactl_available():
        return 0
    pid = int(os.getpid() if pid is None else pid)
    pulse_cids = _pactl_client_ids_for_pid(pid)
    sink_index_to_name = _parse_short_sink_index_to_name()
    targets = _sink_targets_for_move(sink_name)
    p = _run(["pactl", "list", "sink-inputs"])
    if p.returncode != 0 or not (p.stdout or "").strip():
        return 0
    text = p.stdout
    moved = 0
    pos = 0
    while True:
        start = text.find("Sink Input #", pos)
        if start == -1:
            break
        end = text.find("Sink Input #", start + 1)
        block = text[start:] if end == -1 else text[start:end]
        m = re.match(r"Sink Input #(\d+)", block)
        if not m:
            pos = start + 12
            continue
        si_id = m.group(1)
        cur = _sink_input_current_sink_name(block, sink_index_to_name)
        if cur is not None and _sink_name_is_virt(cur, sink_name):
            pos = end if end != -1 else len(text)
            continue
        if not _sink_input_belongs_to_process_for_routing(block, pid, pulse_cids):
            pos = end if end != -1 else len(text)
            continue
        ok = False
        last_err = ""
        for tgt in targets:
            mv = _run(["pactl", "move-sink-input", si_id, tgt])
            if mv.returncode == 0:
                ok = True
                break
            err = (mv.stderr or mv.stdout or "").strip()
            last_err = err or str(mv.returncode)
        if ok:
            moved += 1
        else:
            print(
                f"[live_vocoder] pactl move-sink-input {si_id} → {sink_name!r} failed: {last_err}",
                file=sys.stderr,
            )
        pos = end if end != -1 else len(text)
    return moved


def iter_move_to_virt_sink(
    sink_name: str,
    pid: int | None = None,
    *,
    max_rounds: int = 6,
) -> int:
    """Repeated moves until nothing left to move (WirePlumber sometimes needs a few passes).

    Fewer rounds / shorter sleeps than before: each ``move-sink-input`` reconnects the stream and
    can cause audible dropouts if we hammer PipeWire while audio is running.
    """
    total = 0
    for _ in range(max_rounds):
        n = move_sink_inputs_for_pid_to_sink(sink_name, pid)
        total += n
        if n == 0:
            break
        time.sleep(0.025)
    return total


def _pactl_client_ids_for_pid(pid: int) -> set[str]:
    """
    Pulse client index numbers whose ``application.process.id`` matches ``pid``.
    PipeWire often omits per-stream ``application.name`` on sink-inputs; the client record is reliable.
    """
    p = _run(["pactl", "list", "clients"])
    if p.returncode != 0 or not (p.stdout or "").strip():
        return set()
    text = p.stdout
    out: set[str] = set()
    pos = 0
    while True:
        start = text.find("Client #", pos)
        if start == -1:
            break
        end = text.find("Client #", start + 1)
        block = text[start:] if end == -1 else text[start:end]
        m = re.match(r"Client #(\d+)", block)
        if not m:
            pos = start + 8
            continue
        cid = m.group(1)
        m2 = re.search(r"application\.process\.id\s*=\s*\"(\d+)\"", block)
        if m2 and int(m2.group(1)) == pid:
            out.add(cid)
        pos = end if end != -1 else len(text)
    return out


def list_sink_input_ids_for_client_ids(
    client_ids: set[str],
    *,
    exclude_loopback: bool = True,
) -> list[str]:
    """Sink-input IDs whose ``Client:`` line references one of ``client_ids``."""
    if not client_ids or not pactl_available():
        return []
    p = _run(["pactl", "list", "sink-inputs"])
    if p.returncode != 0 or not (p.stdout or "").strip():
        return []
    text = p.stdout
    out: list[str] = []
    pos = 0
    while True:
        start = text.find("Sink Input #", pos)
        if start == -1:
            break
        end = text.find("Sink Input #", start + 1)
        block = text[start:] if end == -1 else text[start:end]
        m = re.match(r"Sink Input #(\d+)", block)
        if not m:
            pos = start + 12
            continue
        if exclude_loopback and _sink_input_block_looks_like_pulse_loopback(block):
            pos = end if end != -1 else len(text)
            continue
        mc = re.search(r"^\s*Client:\s*(\S+)", block, re.MULTILINE)
        if mc and mc.group(1) in client_ids:
            out.append(m.group(1))
        pos = end if end != -1 else len(text)
    return out


def list_live_vocoder_sink_input_ids(pid: int | None = None) -> list[str]:
    """
    Sink-input IDs that look like this app’s Pulse playback (any sink), or [] if none.
    Used to detect when PortAudio never registered a stream with PipeWire (wrong ALSA device).
    """
    if not pactl_available():
        return []
    pid = int(os.getpid() if pid is None else pid)
    pulse_cids = _pactl_client_ids_for_pid(pid)
    p = _run(["pactl", "list", "sink-inputs"])
    if p.returncode != 0 or not (p.stdout or "").strip():
        return []
    text = p.stdout
    out: list[str] = []
    pos = 0
    while True:
        start = text.find("Sink Input #", pos)
        if start == -1:
            break
        end = text.find("Sink Input #", start + 1)
        block = text[start:] if end == -1 else text[start:end]
        m = re.match(r"Sink Input #(\d+)", block)
        if not m:
            pos = start + 12
            continue
        if _sink_input_belongs_to_process_for_routing(block, pid, pulse_cids):
            out.append(m.group(1))
        pos = end if end != -1 else len(text)
    return out


def format_sink_inputs_on_virt_sink_report(sink_name: str = "live_vocoder") -> str:
    """
    Human-readable lines for **any** sink-input currently routed to the null sink (any PID).

    Used by shell diagnostics when the virtual mic is silent: if this list is empty, PortAudio
    is not feeding ``PULSE_SINK`` (wrong ALSA device or app not **Start**ed).
    """
    if not pactl_available():
        return "pactl not available."
    sink_index_to_name = _parse_short_sink_index_to_name()
    p = _run(["pactl", "list", "sink-inputs"])
    if p.returncode != 0 or not (p.stdout or "").strip():
        return "pactl list sink-inputs failed."
    text = p.stdout
    lines: list[str] = []
    pos = 0
    while True:
        start = text.find("Sink Input #", pos)
        if start == -1:
            break
        end = text.find("Sink Input #", start + 1)
        block = text[start:] if end == -1 else text[start:end]
        m = re.match(r"Sink Input #(\d+)", block)
        if not m:
            pos = start + 12
            continue
        cur = _sink_input_current_sink_name(block, sink_index_to_name)
        if cur is None or not _sink_name_is_virt(cur, sink_name):
            pos = end if end != -1 else len(text)
            continue
        sid = m.group(1)
        an = re.search(r"application\.name\s*=\s*\"([^\"]*)\"", block)
        app = an.group(1).strip() if an else "?"
        bn = re.search(r"application\.process\.binary\s*=\s*\"([^\"]*)\"", block)
        binname = bn.group(1).strip() if bn else "?"
        lines.append(f"  sink-input #{sid} → sink `{cur}` — app={app!r} binary={binname!r}")
        pos = end if end != -1 else len(text)
    if not lines:
        return (
            f"No sink-inputs on `{sink_name}` (nothing is playing into the null sink).\n"
            "Fix: In Live Vocoder press **Start** with a carrier loaded; GTK **Status** must show Streaming…\n"
            "If it does but this stays empty, set **LIVE_VOCODER_PORTAUDIO_OUTPUT** to a **pulse** or "
            "**pipewire** index from `python -m sounddevice` (not a raw hw: device)."
        )
    hdr = f"Sink-inputs feeding `{sink_name}` ({len(lines)}):"
    return hdr + "\n" + "\n".join(lines)


def list_our_sink_inputs_on_named_sink(sink_name: str, pid: int | None = None) -> list[str]:
    """
    Sink-input IDs that play to ``sink_name`` (the null sink) and belong to this process.
    Stronger signal after ``iter_move_to_virt_sink`` than global ``list_live_vocoder_sink_input_ids``
    when PipeWire omits some client properties on streams still on the default sink.
    """
    if not pactl_available():
        return []
    pid = int(os.getpid() if pid is None else pid)
    pulse_cids = _pactl_client_ids_for_pid(pid)
    sink_index_to_name = _parse_short_sink_index_to_name()
    p = _run(["pactl", "list", "sink-inputs"])
    if p.returncode != 0 or not (p.stdout or "").strip():
        return []
    text = p.stdout
    out: list[str] = []
    pos = 0
    while True:
        start = text.find("Sink Input #", pos)
        if start == -1:
            break
        end = text.find("Sink Input #", start + 1)
        block = text[start:] if end == -1 else text[start:end]
        m = re.match(r"Sink Input #(\d+)", block)
        if not m:
            pos = start + 12
            continue
        cur = _sink_input_current_sink_name(block, sink_index_to_name)
        if cur is None or not _sink_name_is_virt(cur, sink_name):
            pos = end if end != -1 else len(text)
            continue
        if _sink_input_belongs_to_process_for_routing(block, pid, pulse_cids) or _sink_input_likely_python_client_on_virt_sink(
            block, pid
        ):
            out.append(m.group(1))
        pos = end if end != -1 else len(text)
    return out


def wait_for_live_vocoder_playback_sink_input(
    virt_sink_name: str,
    pid: int | None = None,
    *,
    attempts: int = 16,
    delay_s: float = 0.06,
) -> list[str]:
    """
    Wait until ``pactl`` shows this process’s playback (PipeWire often lags the first PortAudio
    callback). Re-runs ``iter_move_to_virt_sink`` periodically so streams that landed on the
    default sink still get moved.
    """
    if not pactl_available():
        return []
    pid = int(os.getpid() if pid is None else pid)
    # One move before waiting; repeating move-sink-input during playback causes cutouts on PipeWire.
    iter_move_to_virt_sink(virt_sink_name, pid)
    mid_retry = max(1, attempts // 2)
    for attempt in range(attempts):
        ids = list_live_vocoder_sink_input_ids(pid)
        if ids:
            return ids
        ids = list_our_sink_inputs_on_named_sink(virt_sink_name, pid)
        if ids:
            return ids
        cids = _pactl_client_ids_for_pid(pid)
        ids = list_sink_input_ids_for_client_ids(cids, exclude_loopback=True)
        if ids:
            return ids
        if attempt == mid_retry:
            iter_move_to_virt_sink(virt_sink_name, pid)
        time.sleep(delay_s)
    return []


@dataclass
class VirtMicSetup:
    sink_name: str
    description: str
    loopback_module_id: int | None
    remap_module_id: int | None = None
    loopback_sink_input_id: int | None = None


def setup_virt_mic(
    sink_name: str = "live_vocoder",
    description: str = "LiveVocoder",
    hear_on_default_sink: bool = True,
) -> tuple[bool, VirtMicSetup | None, str]:
    """
    Returns (ok, setup_or_none, message).
    If ok, call teardown_virt_mic(setup, …) when done; see :func:`teardown_virt_mic` for unload options.
    """
    if not pactl_available():
        return (
            False,
            None,
            "pactl not found. Install pipewire-pulse (Arch: sudo pacman -S pipewire-pulse).",
        )
    if not ensure_null_sink(sink_name, description):
        return (
            False,
            None,
            f"Could not create Pulse null sink '{sink_name}'. Try: pactl load-module module-null-sink sink_name={sink_name} rate=48000 channels=2",
        )
    apply_pulse_sink_env(sink_name)
    remap_id, remap_hint = ensure_virt_mic_input_source(sink_name, description)
    mon = monitor_source_name(sink_name)
    loop_id: int | None = None
    if hear_on_default_sink:
        loop_id, loop_err = load_loopback_monitor_to_default_with_fallback(mon)
        if loop_id is None:
            return (
                True,
                VirtMicSetup(
                    sink_name,
                    description,
                    None,
                    remap_module_id=remap_id,
                    loopback_sink_input_id=None,
                ),
                f"Sink `{sink_name}` ready. {remap_hint} Loopback to speakers failed ({loop_err}) — "
                "you may hear silence locally.",
            )
    sink_in_id: int | None = None
    if loop_id is not None:
        sink_in_id = discover_loopback_sink_input(loop_id)
    parts = [
        remap_hint,
        f"Fallback: **Monitor of {description}** (`{mon}`).",
        f"Output → sink `{sink_name}` (PULSE_SINK).",
    ]
    return (
        True,
        VirtMicSetup(
            sink_name,
            description,
            loop_id,
            remap_module_id=remap_id,
            loopback_sink_input_id=sink_in_id,
        ),
        " ".join(parts),
    )


def teardown_pulse_virt_mic_full(sink_name: str) -> int:
    """
    Tear down **all** Pulse/PipeWire pieces this app uses for ``sink_name`` (app quit / window close).

    Unloads, in order across passes: **module-loopback** from ``{sink}.monitor``,
    **module-remap-source** / **module-virtual-source** for ``{sink}_mic``,
    **module-null-sink** whose argument ``sink_name=`` matches ``sink_name`` exactly (not a prefix of
    another sink). Does not touch other apps' virtual mics.

    Pair with :func:`warm_pulse_virt_mic_devices` when the GTK window opens. Safe to call when no
    modules exist (no-op).
    """
    if not pactl_available():
        return 0
    mic = virtual_mic_source_name(sink_name)
    mon = monitor_source_name(sink_name)
    total = 0
    total += unload_loopbacks_for_pulse_source(mon)
    mute_monitor_loopback_sink_inputs(sink_name, True)
    sn_esc = re.escape(sink_name)
    # sink_name=NAME must not match NAME_foo (boundary after NAME)
    null_re = re.compile(rf"(?i)sink_name\s*=\s*{sn_esc}(?:\s|$|\t)")
    mic_token = f"source_name={mic}"

    for _ in range(24):
        p = _run(["pactl", "list", "short", "modules"])
        if p.returncode != 0 or not (p.stdout or "").strip():
            break
        remap_ids: list[int] = []
        null_ids: list[int] = []
        for line in p.stdout.strip().splitlines():
            parts = line.split("\t", 1)
            if not parts:
                continue
            try:
                mid = int(parts[0].strip())
            except ValueError:
                continue
            rest = parts[1] if len(parts) > 1 else ""
            rl = rest.lower()
            compact = re.sub(r"\s+", "", rest)
            if ("remap-source" in rl or "virtual-source" in rl) and mic_token in compact:
                remap_ids.append(mid)
            if "null-sink" in rl and null_re.search(rest):
                null_ids.append(mid)
        if not remap_ids and not null_ids:
            break
        for mid in sorted(set(remap_ids), reverse=True):
            if unload_module(mid):
                total += 1
        for mid in sorted(set(null_ids), reverse=True):
            if unload_module(mid):
                total += 1
    return total


def teardown_virt_mic(
    setup: VirtMicSetup | None,
    *,
    unload_remapped_mic: bool = True,
) -> None:
    """
    Tear down Pulse modules created for streaming.

    - Always unload **loopback** (monitor → speakers) when ``loopback_module_id`` is set.
    - Unload **module-remap-source** / virtual mic only if ``unload_remapped_mic`` is True.
      After **Stop**, the GUI keeps the null sink + mic so you can **Start** again without
      re-picking the device. On **window close**, :func:`teardown_pulse_virt_mic_full` removes
      the full stack (null sink, remap, loopbacks).
    """
    if setup is None:
        return
    if setup.loopback_module_id is not None:
        unload_module(setup.loopback_module_id)
        setup.loopback_module_id = None
        setup.loopback_sink_input_id = None
    if unload_remapped_mic and setup.remap_module_id is not None:
        unload_module(setup.remap_module_id)
        setup.remap_module_id = None


def apply_speaker_mute_pulse(
    setup: VirtMicSetup,
    *,
    muted: bool,
    restore_loopback: bool = False,
) -> None:
    """
    Muted: mute the loopback sink-input (preferred) or unload module — virtual mic unchanged.
    Unmuted + restore_loopback: unmute sink-input or re-add loopback if it was removed.
    """
    if muted:
        if setup.loopback_sink_input_id is not None:
            set_sink_input_mute(setup.loopback_sink_input_id, True)
        elif setup.loopback_module_id is not None:
            unload_module(setup.loopback_module_id)
            setup.loopback_module_id = None
        return
    if restore_loopback:
        if setup.loopback_sink_input_id is not None:
            set_sink_input_mute(setup.loopback_sink_input_id, False)
        elif setup.loopback_module_id is None:
            mon = monitor_source_name(setup.sink_name)
            mid, _err = load_loopback_monitor_to_default_with_fallback(mon)
            setup.loopback_module_id = mid
            if mid is not None:
                setup.loopback_sink_input_id = discover_loopback_sink_input(mid)
