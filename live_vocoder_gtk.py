#!/usr/bin/env python3
"""
GTK 4 GUI for the live vocoder (VoiceMod-inspired layout: dark theme, cards, quick presets).

System packages (Arch):
  sudo pacman -S python-gobject gtk4

Optional pip (often still needs system gtk4):  pip install PyGObject

PipeWire: ``live_vocoder.py --gtk-gui`` on Linux (with ``pactl``) routes vocoded audio to the null sink
for **Discord / OBS**. Use **Hear me (monitor)** for sidetone (vocoded audio in your headphones via PipeWire
when Virtual mic is on); leave it **OFF** for Discord-only (silent local). ``--no-hear-local`` starts with
monitor off. See ``LIVE_VOCODER_PORTAUDIO_OUTPUT`` in ``pulse_portaudio.py``. Optional
``LIVE_VOCODER_PULSE_SINK`` / ``LIVE_VOCODER_PULSE_DESCRIPTION`` give a **distinct** null sink so
this app does not share ``live_vocoder`` with another virtual mic.

On Linux with Virtual mic enabled, the app **registers the null sink and ``*_mic`` input when the window opens**
(so **System Settings → Sound → Input** and Discord can list **… Virtual Mic** before you press **Start**).

The main stack is in a **scrollable** area (vertical + horizontal when needed) so the layout fits small
displays on Linux, macOS, and Windows. The header and carrier card use a shared **inverted / neon mic** mark
(SVG via Gdk.Texture, with emoji fallback if SVG load fails).

The window includes a **Pulse / virtual mic** block (bottom) that refreshes while open: ``pactl``, PortAudio
device index, ``PULSE_SINK``, and whether the null sink / ``*_mic`` source exist. Use **Status** (top) for
carrier load, ffmpeg, and stream messages when something goes wrong.
"""
from __future__ import annotations

import os
import subprocess
import sys
from pathlib import Path

from audio_backend import format_audio_backend_report
from carrier_library import ensure_carrier_library_dir, list_carrier_files
from vocoder_session import VocoderSession


def _full_pipeline_diag_text(sess: VocoderSession, *, virt_ui_on: bool) -> str:
    audio = format_audio_backend_report(
        preferred_input=int(sess.in_dev),
        preferred_output=int(sess.out_dev),
    )
    if sys.platform.startswith("linux"):
        return f"{audio}\n\n{_pulse_pipeline_diag_text(sess, virt_ui_on=virt_ui_on)}"
    return (
        f"{audio}\n\n"
        "── Virtual microphone (Windows / macOS) ──\n"
        "Enable **Virtual mic** and use **VB-Audio Cable** (Windows) or **BlackHole** / **Loopback** (macOS); "
        "match the cable **output** in DEVICE HINTS if PortAudio does not auto-detect."
    )


def _pulse_pipeline_diag_text(sess: VocoderSession, *, virt_ui_on: bool) -> str:
    """Live snapshot for PipeWire / PortAudio / null-sink (updated periodically in the GTK window)."""
    if not sys.platform.startswith("linux"):
        return ""
    lines: list[str] = []
    try:
        from pulse_portaudio import resolve_stream_device_for_pulse_virt_mic
        from pulse_virtmic import (
            discover_monitor_source_name,
            format_live_vocoder_sink_discovery_hint,
            get_default_source_name,
            pactl_available,
            sink_exists,
            source_exists,
            virtual_mic_source_name,
        )
    except ImportError as e:
        return f"── Pulse / virtual mic ──\n(import error: {e})"

    if not pactl_available():
        lines.append("pactl: NOT FOUND (install pipewire-pulse)")
    else:
        lines.append("pactl: ok")
    env_out = os.environ.get("LIVE_VOCODER_PORTAUDIO_OUTPUT", "").strip()
    if env_out:
        lines.append(f"LIVE_VOCODER_PORTAUDIO_OUTPUT={env_out!r}")
    else:
        lines.append("LIVE_VOCODER_PORTAUDIO_OUTPUT: (unset — ./run.sh sets pulse/pipewire on Linux)")
    env_sn = os.environ.get("LIVE_VOCODER_PULSE_SINK", "").strip()
    if env_sn:
        lines.append(f"LIVE_VOCODER_PULSE_SINK={env_sn!r} (distinct sink; avoids clashing with other VMs)")
    lines.append(f"Virtual mic (checkbox): {'on' if virt_ui_on else 'off'}")
    if not virt_ui_on:
        lines.append("Turn **Virtual mic (Pulse)** ON, then **Start**, for Discord/OBS.")
        return "\n".join(lines)
    pvm = resolve_stream_device_for_pulse_virt_mic()
    if pvm is None:
        lines.append("PortAudio stream dev: NONE matching pulse/pipewire — set LIVE_VOCODER_PORTAUDIO_OUTPUT")
    else:
        try:
            import sounddevice as sd

            di = sd.query_devices(pvm)
            dn = di.get("name", "?")
            inch = int(di.get("max_input_channels") or 0)
        except Exception:
            dn = "?"
            inch = -1
        low = str(dn).lower()
        if "pulse" not in low and "pipewire" not in low and low != "default":
            lines.append(
                f"PortAudio → [{pvm}] {dn!r} in_ch={inch} ⚠ not pulse/pipewire — PULSE_SINK may be ignored"
            )
        else:
            lines.append(
                f"PortAudio → [{pvm}] {dn!r} (duplex in_ch={inch}; mic = PipeWire default source)"
            )
    sn = sess.pulse_sink_name
    lines.append(f"PULSE_SINK={sn!r} (env set when GTK opens)")
    if pactl_available():
        lines.append(f"Sink {sn!r}: {'yes' if sink_exists(sn) else 'no (created when window opens / Start)'}")
        mic = virtual_mic_source_name(sn)
        mon = discover_monitor_source_name(sn)
        has_mic = source_exists(mic)
        has_mon = source_exists(mon)
        lines.append(
            f"Source {mic!r} (remapped virtual mic): "
            f"{'yes' if has_mic else 'no'}"
        )
        lines.append(
            f"Source {mon!r} (null-sink monitor): "
            f"{'yes' if has_mon else 'no'}"
        )
        if has_mic or has_mon:
            lines.append(
                "**Virtual mic / capture:** OK — pick `*_mic` or the **monitor** source above in Discord/OBS / System Settings (not *Default*)."
            )
        else:
            lines.append(
                "**Virtual mic / capture:** not detected for sink "
                f"`{sn}` (no remapped `*_mic` and no `{mon}` in `pactl`). "
                "Other apps will not hear the vocoder until you fix this."
            )
            hint = format_live_vocoder_sink_discovery_hint()
            if hint:
                lines.append(hint)
            if not sink_exists(sn):
                lines.append(
                    f"Sink `{sn}` is missing — virtual mic setup did not leave a null sink. "
                    "Toggle **Virtual mic (Pulse)** off then on, or restart the app."
                )
            else:
                lines.append(
                    f"Sink `{sn}` exists but no `*_mic` / monitor in `pactl` — run `pactl list short sources` and look "
                    "for **Monitor of** that sink; if the session sink name is wrong, set LIVE_VOCODER_PULSE_SINK and reopen."
                )
        dsrc = get_default_source_name() or ""
        if dsrc:
            lines.append(f"Default recording source: {dsrc!r}")
        if (has_mic or has_mon) and dsrc and "live_vocoder" not in dsrc.lower():
            lines.append(
                "Default mic is still your **hardware** — apps on “default” ignore Live Vocoder. "
                f"Select **{sess.pulse_sink_description}** / `{mic}` or **`{mon}`** in **System Settings → Sound → Input** "
                "or in Discord/OBS."
            )
        lines.append(
            "The virtual mic carries audio **only while Start is running** (vocoder feeding the null sink)."
        )
        obs_pick = (
            f"**{sess.pulse_sink_description} Virtual Mic** (`{mic}`)"
            if has_mic
            else f"**Monitor of {sess.pulse_sink_description}** / `{mon}`"
        )
        lines.append(
            f"**OBS:** Add **Audio Input Capture** (or PipeWire/Pulse device) → pick "
            f"{obs_pick}, not *Default* or your headset."
        )
        lines.append(
            "**Browser sites** (e.g. online voice recorders): in the mic permission or site settings, "
            f"choose {obs_pick} (or **All devices** → monitor). Leaving **Default** records your normal hardware mic."
        )
    lines.append(
        "If carrier load or streaming fails, read **Status** above (ffmpeg decode errors, PortAudio xruns, etc.)."
    )
    return "── Pulse / virtual mic ──\n" + "\n".join(lines)

# Inverted / high-contrast mic mark for GTK (light–neon on dark); used in header + carrier card.
# SVG is loaded via Gdk.Texture (needs GTK built with SVG support; falls back to emoji).
_MIC_SVG_MARK = """<?xml version="1.0" encoding="UTF-8"?>
<svg xmlns="http://www.w3.org/2000/svg" width="256" height="256" viewBox="0 0 256 256">
  <defs>
    <linearGradient id="vmic" x1="0" y1="0" x2="1" y2="1">
      <stop offset="0" stop-color="#d4fff7"/>
      <stop offset="0.45" stop-color="#7cf8ff"/>
      <stop offset="1" stop-color="#ffa6f3"/>
    </linearGradient>
    <linearGradient id="vmicdim" x1="0" y1="1" x2="1" y2="0">
      <stop offset="0" stop-color="#6af0e8"/>
      <stop offset="1" stop-color="#c86bff"/>
    </linearGradient>
  </defs>
  <rect width="256" height="256" rx="56" fill="#0a0812"/>
  <ellipse cx="128" cy="88" rx="52" ry="62" fill="url(#vmic)" opacity="0.95"/>
  <ellipse cx="128" cy="88" rx="36" ry="44" fill="#12081a" opacity="0.35"/>
  <rect x="108" y="142" width="40" height="56" rx="12" fill="url(#vmicdim)"/>
  <path d="M72 168 Q128 228 184 168" fill="none" stroke="url(#vmic)" stroke-width="14"
        stroke-linecap="round"/>
  <rect x="112" y="214" width="32" height="18" rx="6" fill="#d4fff7"/>
</svg>"""


def _mic_texture_from_svg() -> "Gdk.Texture | None":
    try:
        from gi.repository import Gdk, GLib

        return Gdk.Texture.new_from_bytes(GLib.Bytes.new(_MIC_SVG_MARK.encode("utf-8")))
    except Exception:
        return None


def _app_brand_icon_paths() -> list[Path]:
    """cpp/assets/app-icon.webp or .png (same artwork as Windows .ico / SDL)."""
    cpp_assets = Path(__file__).resolve().parent / "cpp" / "assets"
    return [cpp_assets / "app-icon.webp", cpp_assets / "app-icon.png"]


def _brand_texture() -> "Gdk.Texture | None":
    try:
        from gi.repository import Gdk
    except Exception:
        return _mic_texture_from_svg()
    for p in _app_brand_icon_paths():
        if not p.is_file():
            continue
        try:
            return Gdk.Texture.new_from_filename(str(p))
        except Exception:
            continue
    return _mic_texture_from_svg()


def _mic_image_widget(pixel_size: int):
    """Gtk.Image from app icon or mic texture, or emoji fallback."""
    from gi.repository import Gtk

    tex = _brand_texture()
    if tex is not None:
        img = Gtk.Image.new_from_paintable(tex)
        img.set_pixel_size(pixel_size)
        img.set_tooltip_text("Live Vocoder")
        return img
    lab = Gtk.Label(label="🎙️")
    lab.add_css_class("vm-brand")
    return lab


# Quick effect presets: (wet, presence_db, reverb_mix, reverb_room)
_VOICE_PRESETS: dict[str, tuple[float, float, float, float]] = {
    "clean": (1.0, 4.0, 0.0, 0.45),
    "radio": (1.2, 6.5, 0.08, 0.35),
    "deep": (1.35, 2.0, 0.18, 0.55),
    "studio": (1.15, 5.0, 0.12, 0.4),
}


def _vm_css() -> str:
    return """
    window.vm-root {
      background: linear-gradient(165deg, #0c0e14 0%, #12151f 45%, #0a0c12 100%);
    }
    .vm-brand {
      font-weight: 800;
      font-size: 1.35rem;
      letter-spacing: -0.03em;
      color: #f4f5f8;
    }
    .vm-sub {
      font-size: 0.8rem;
      color: #8b92a8;
      margin-top: 2px;
    }
    .vm-card {
      background: rgba(22, 26, 38, 0.92);
      border: 1px solid rgba(124, 92, 220, 0.22);
      border-radius: 14px;
      padding: 16px 18px;
    }
    .vm-section-label {
      font-size: 0.72rem;
      font-weight: 700;
      letter-spacing: 0.12em;
      color: #9b6dff;
      margin-bottom: 10px;
    }
    .vm-mute-foot {
      font-size: 0.75rem;
      color: #6d7388;
    }
    .vm-preset flowboxchild {
      padding: 0;
    }
    button.vm-preset-btn {
      padding: 8px 14px;
      border-radius: 999px;
      font-weight: 600;
      background: rgba(40, 44, 58, 0.95);
      border: 1px solid rgba(124, 92, 220, 0.35);
      color: #e8e9ef;
    }
    button.vm-preset-btn:hover {
      background: rgba(90, 60, 180, 0.45);
      border-color: rgba(168, 130, 255, 0.55);
    }
    /* GTK 4: avoid negative GtkGizmo min-size warnings from SpinButton internals on some themes */
    spinbutton {
      min-width: 80px;
      min-height: 28px;
    }
    scrolledwindow.vm-scroll {
      background: transparent;
    }
    scrolledwindow.vm-scroll undershoot.top,
    scrolledwindow.vm-scroll undershoot.bottom {
      background: linear-gradient(to bottom, rgba(12, 14, 20, 0.5), transparent);
    }
    """


def _install_gtk_gizmo_warning_filter() -> None:
    """Suppress harmless GTK 4 warnings: GtkGizmo slider reported min width/height -2."""
    try:
        from gi.repository import GLib

        def _handler(domain: str, level: int, message: str, udata) -> None:
            if (
                message
                and "GtkGizmo" in message
                and "reported min" in message
                and "sizes must be" in message
            ):
                return
            GLib.log_default_handler(domain, level, message, udata)

        GLib.log_set_handler(
            "Gtk",
            GLib.LogLevelFlags.LEVEL_WARNING,
            _handler,
            None,
        )
    except (AttributeError, TypeError, RuntimeError):
        pass


def _apply_css(widget) -> None:
    import gi

    gi.require_version("Gtk", "4.0")
    from gi.repository import Gtk

    prov = Gtk.CssProvider()
    prov.load_from_data(_vm_css().encode("utf-8"))
    Gtk.StyleContext.add_provider_for_display(
        widget.get_display(),
        prov,
        Gtk.STYLE_PROVIDER_PRIORITY_APPLICATION,
    )


def run_gtk_gui(
    initial_carrier: Path | None = None,
    sr: int = 48000,
    input_device: int | None = None,
    output_device: int | None = None,
    use_pulse_virt_mic: bool = False,
    pulse_sink_name: str | None = None,
    pulse_sink_description: str | None = None,
    virt_mic_hear_loopback: bool = False,
    mute_speakers: bool = True,
    mute_vocoded: bool = True,
    virt_mic_device_hint: str | None = None,
    reverb_mix: float = 0.0,
    reverb_room: float = 0.45,
) -> int:
    try:
        import gi

        gi.require_version("Gtk", "4.0")
        from gi.repository import Gio, GLib, Gtk, Pango
    except (ImportError, ValueError) as e:
        print(
            "GTK 4 GUI needs PyGObject + GTK 4.\n"
            "  Arch:  sudo pacman -S python-gobject gtk4\n"
            f"  ({e})\n",
            file=sys.stderr,
        )
        raise

    _install_gtk_gizmo_warning_filter()

    if sys.platform.startswith("linux") and use_pulse_virt_mic:
        from pulse_virtmic import apply_pulse_sink_env

        apply_pulse_sink_env(pulse_sink_name)

    session = VocoderSession(
        sr=sr,
        input_device=input_device,
        output_device=output_device,
        use_pulse_virt_mic=use_pulse_virt_mic,
        pulse_sink_name=pulse_sink_name,
        pulse_sink_description=pulse_sink_description,
        virt_mic_hear_loopback=virt_mic_hear_loopback,
        mute_speakers=mute_speakers,
        mute_vocoded=mute_vocoded,
        virt_mic_device_hint=virt_mic_device_hint,
        reverb_mix=reverb_mix,
        reverb_room=reverb_room,
    )
    session.try_default_carrier(initial_carrier)

    if sys.platform.startswith("linux") and session.use_pulse_virt_mic:
        from pulse_virtmic import virtual_mic_source_name, warm_pulse_virt_mic_devices

        ok_warm, warm_msg, cap_kind = warm_pulse_virt_mic_devices(
            session.pulse_sink_name, session.pulse_sink_description
        )
        mic_n = virtual_mic_source_name(session.pulse_sink_name)
        if ok_warm:
            if cap_kind == "remap":
                pulse_tail = (
                    f"**Pulse:** Virtual mic **{session.pulse_sink_description} Virtual Mic** "
                    f"(`{mic_n}`) is registered for this session. "
                    "Closing the window **removes** the null sink and mic from PipeWire; reopening **recreates** them. "
                    "Check **System Settings → Sound → Input** (or `pavucontrol`) if the device is missing."
                )
            else:
                pulse_tail = (
                    f"**Pulse:** Null sink `{session.pulse_sink_name}` is ready. {warm_msg} "
                    "Closing the window **removes** the null sink from PipeWire."
                )
        else:
            pulse_tail = warm_msg
        session.last_status = f"{session.last_status}\n{pulse_tail}".strip()

    class VocoderWindow(Gtk.ApplicationWindow):
        def __init__(self, app: Gtk.Application, sess: VocoderSession) -> None:
            super().__init__(application=app, title="Live Vocoder")
            self.add_css_class("vm-root")
            self.set_default_size(640, 720)
            self.set_size_request(360, 420)
            self.session = sess
            self._status_timer: int = 0

            _apply_css(self)

            header = Gtk.HeaderBar()
            title_box = Gtk.Box(orientation=Gtk.Orientation.VERTICAL, spacing=0)
            t1 = Gtk.Label(label="Live Vocoder")
            t1.add_css_class("vm-brand")
            t1.set_xalign(0)
            t2 = Gtk.Label(
                label=(
                    "Linux: virtual mic + Hear me (monitor) default ON — unmute mic/output in System Settings if silent"
                    if sys.platform.startswith("linux")
                    else "Carrier vocoder · pick a voice source, then Start"
                )
            )
            t2.add_css_class("vm-sub")
            t2.set_xalign(0)
            title_box.append(t1)
            title_box.append(t2)
            header.set_title_widget(title_box)

            mic_header = _mic_image_widget(36)
            mic_header.set_margin_end(10)
            header.pack_start(mic_header)

            self.btn_start = Gtk.Button(label="Start")
            self.btn_start.add_css_class("suggested-action")
            self.btn_start.set_tooltip_text("Begin live vocoding (mic → carrier spectrum)")
            self.btn_start.connect("clicked", self._on_start)
            header.pack_end(self.btn_start)

            self.btn_stop = Gtk.Button(label="Stop")
            self.btn_stop.add_css_class("destructive-action")
            self.btn_stop.connect("clicked", self._on_stop)
            header.pack_end(self.btn_stop)

            self.set_titlebar(header)

            root = Gtk.Box(orientation=Gtk.Orientation.VERTICAL, spacing=0)
            self.set_child(root)

            scroll = Gtk.ScrolledWindow()
            scroll.add_css_class("vm-scroll")
            scroll.set_policy(Gtk.PolicyType.AUTOMATIC, Gtk.PolicyType.AUTOMATIC)
            scroll.set_vexpand(True)
            scroll.set_hexpand(True)
            # GTK 4.2+: natural size so tall content scrolls inside a shorter window (Linux/macOS/Win GTK).
            for _setter in ("set_propagate_natural_height", "set_propagate_natural_width"):
                fn = getattr(scroll, _setter, None)
                if callable(fn):
                    fn(True)
            root.append(scroll)

            outer = Gtk.Box(orientation=Gtk.Orientation.VERTICAL, spacing=14)
            outer.set_margin_start(16)
            outer.set_margin_end(16)
            outer.set_margin_top(14)
            outer.set_margin_bottom(14)
            scroll.set_child(outer)

            # —— Voice (carrier) card ——
            voice_card = Gtk.Box(orientation=Gtk.Orientation.VERTICAL, spacing=12)
            voice_card.add_css_class("vm-card")
            sec_v = Gtk.Label(label="VOICE SOURCE")
            sec_v.add_css_class("vm-section-label")
            sec_v.set_xalign(0)
            voice_card.append(sec_v)

            hero = Gtk.Box(orientation=Gtk.Orientation.HORIZONTAL, spacing=14)
            hero.append(_mic_image_widget(48))
            path_col = Gtk.Box(orientation=Gtk.Orientation.VERTICAL, spacing=6)
            path_col.set_hexpand(True)
            self.path_entry = Gtk.Entry()
            self.path_entry.set_placeholder_text("Carrier audio (mp3, wav, flac…)")
            self.path_entry.set_text(sess.path_hint or "")
            path_col.append(self.path_entry)
            self.carrier_hint = Gtk.Label(
                label="Pick a loop or pad; your mic shapes the spectrum.",
                xalign=0,
            )
            self.carrier_hint.add_css_class("dim-label")
            path_col.append(self.carrier_hint)
            hero.append(path_col)
            voice_card.append(hero)

            row_btns = Gtk.Box(orientation=Gtk.Orientation.HORIZONTAL, spacing=8)
            btn_browse = Gtk.Button(label="Browse…")
            btn_browse.connect("clicked", self._on_browse)
            row_btns.append(btn_browse)
            btn_load = Gtk.Button(label="Load & apply")
            btn_load.add_css_class("suggested-action")
            btn_load.connect("clicked", self._on_load_apply)
            row_btns.append(btn_load)
            voice_card.append(row_btns)

            lib_dir = ensure_carrier_library_dir()
            lib_l = Gtk.Label(
                label=f"Carriers in Documents · {lib_dir.name}",
                xalign=0,
            )
            lib_l.add_css_class("vm-section-label")
            lib_l.set_margin_top(8)
            voice_card.append(lib_l)
            lib_sub = Gtk.Label(
                label=str(lib_dir),
                xalign=0,
                wrap=True,
            )
            lib_sub.add_css_class("dim-label")
            voice_card.append(lib_sub)

            self._carrier_dropdown_suppress = False
            self._carrier_paths: list[Path] = []
            self._carrier_model = Gtk.StringList.new(None)
            self.carrier_dropdown = Gtk.DropDown.new(self._carrier_model, None)
            self.carrier_dropdown.set_hexpand(True)
            self.carrier_dropdown.set_tooltip_text(
                "Pick a file from ~/Documents/LiveVocoderCarriers (put MP3/WAV/FLAC here)"
            )
            self.carrier_dropdown.connect("notify::selected", self._on_carrier_dropdown_selected)

            lib_row = Gtk.Box(orientation=Gtk.Orientation.HORIZONTAL, spacing=8)
            lib_row.append(self.carrier_dropdown)
            btn_refresh_lib = Gtk.Button(label="Refresh")
            btn_refresh_lib.set_tooltip_text("Rescan the carrier folder")
            btn_refresh_lib.connect("clicked", self._on_refresh_carrier_list)
            lib_row.append(btn_refresh_lib)
            btn_open_lib = Gtk.Button(label="Open folder")
            btn_open_lib.set_tooltip_text("Open the carrier folder in the file manager")
            btn_open_lib.connect("clicked", self._on_open_carrier_folder)
            lib_row.append(btn_open_lib)
            voice_card.append(lib_row)
            self._refill_carrier_dropdown(select_path=sess.path_hint or None)

            # Preset chips (effect macros)
            preset_row = Gtk.Box(orientation=Gtk.Orientation.VERTICAL, spacing=8)
            pr_l = Gtk.Label(label="QUICK SOUND")
            pr_l.add_css_class("vm-section-label")
            pr_l.set_margin_top(4)
            pr_l.set_xalign(0)
            preset_row.append(pr_l)
            flow = Gtk.FlowBox()
            flow.set_selection_mode(Gtk.SelectionMode.NONE)
            flow.set_column_spacing(8)
            flow.set_row_spacing(8)
            flow.add_css_class("vm-preset")
            for key, name in (
                ("clean", "Clean"),
                ("radio", "Radio"),
                ("deep", "Deep"),
                ("studio", "Studio"),
            ):
                b = Gtk.Button(label=name)
                b.add_css_class("vm-preset-btn")
                b.connect("clicked", self._on_preset, key)
                flow.append(b)
            preset_row.append(flow)
            voice_card.append(preset_row)

            outer.append(voice_card)

            # —— Routing / monitoring card ——
            route_card = Gtk.Box(orientation=Gtk.Orientation.VERTICAL, spacing=10)
            route_card.add_css_class("vm-card")
            sec_r = Gtk.Label(label="MONITOR & OUTPUT")
            sec_r.add_css_class("vm-section-label")
            sec_r.set_xalign(0)
            route_card.append(sec_r)

            self.cb_virt = Gtk.CheckButton()
            self.cb_virt.set_active(sess.use_pulse_virt_mic)
            self.cb_virt.connect("toggled", self._on_virt_toggle)
            route_card.append(
                self._toggle_row(
                    "Virtual mic (Pulse)",
                    f"After **Start**, choose **{sess.pulse_sink_description}** / **{sess.pulse_sink_name}_mic** "
                    f"in Discord/OBS (not “default”, if that is still your headset). "
                    f"In KDE **Sound → Input**, select it as default if you want system-wide. "
                    f"Stays listed after **Stop** until you close this window.",
                    self.cb_virt,
                )
            )

            self.cb_hear_monitor = Gtk.CheckButton()
            self.cb_hear_monitor.set_active(not sess.mute_speakers)
            self.cb_hear_monitor.connect("toggled", self._on_hear_monitor_toggle)
            route_card.append(
                self._toggle_row(
                    "Hear me (monitor)",
                    "Hear **vocoded** audio in your speakers/headphones while streaming. With Virtual mic on, "
                    "PipeWire copies the null-sink output to your default playback device. **OFF** = silent "
                    "headphones but Discord/OBS still get the virtual mic. Raw-mic-only sidetone is not "
                    "available from this toggle.",
                    self.cb_hear_monitor,
                )
            )

            outer.append(route_card)

            # —— Advanced / cable ——
            adv_card = Gtk.Box(orientation=Gtk.Orientation.VERTICAL, spacing=10)
            adv_card.add_css_class("vm-card")
            sec_a = Gtk.Label(label="DEVICE HINTS")
            sec_a.add_css_class("vm-section-label")
            sec_a.set_xalign(0)
            adv_card.append(sec_a)
            adv_card.append(Gtk.Label(label="Cable / device name contains (Windows/macOS fallback)", xalign=0))
            self.virt_dev_entry = Gtk.Entry()
            self.virt_dev_entry.set_text(sess.virt_mic_device_hint or "")
            self.virt_dev_entry.connect("changed", self._on_virt_dev_changed)
            adv_card.append(self.virt_dev_entry)

            hp_hint = Gtk.Label(
                label=(
                    "Apps use **one** capture device: **… Virtual Mic** (`…_mic`) or **Monitor of …**. "
                    "Typical Discord: Virtual mic **ON**, **Hear me (monitor)** **OFF**. "
                    "If you hear nothing locally: turn **Hear me (monitor)** **ON**, and check system **Input** "
                    "(default mic unmuted) and **Output** (tray not muted)."
                ),
                xalign=0,
                wrap=True,
            )
            hp_hint.add_css_class("dim-label")
            hp_hint.set_margin_top(4)
            adv_card.append(hp_hint)
            outer.append(adv_card)

            # —— Mixer / effects ——
            mix_card = Gtk.Box(orientation=Gtk.Orientation.VERTICAL, spacing=12)
            mix_card.add_css_class("vm-card")
            sec_m = Gtk.Label(label="MIXER & EFFECTS")
            sec_m.add_css_class("vm-section-label")
            sec_m.set_xalign(0)
            mix_card.append(sec_m)

            self.cb_clean_mic = Gtk.CheckButton()
            self.cb_clean_mic.set_active(sess.clean_mic)
            self.cb_clean_mic.connect("toggled", self._on_clean_mic_toggle)
            mix_card.append(
                self._toggle_row(
                    "Clean mic",
                    "Bypass the vocoder and reverb: send **dry microphone** to your outputs and virtual mic. "
                    "A carrier file must still be loaded so **Start** can run.",
                    self.cb_clean_mic,
                )
            )

            adj_gain = Gtk.Adjustment(
                value=sess.gain,
                lower=0.3,
                upper=8.0,
                step_increment=0.05,
                page_increment=0.5,
            )
            adj_gain.connect("value-changed", self._on_gain)
            mix_card.append(self._slider_block("Output gain", adj_gain, 2))

            adj_wet = Gtk.Adjustment(
                value=sess.wet_level,
                lower=0.0,
                upper=2.0,
                step_increment=0.02,
                page_increment=0.1,
            )
            adj_wet.connect("value-changed", self._on_wet)
            self.adj_wet = adj_wet
            mix_card.append(self._slider_block("Vocoder wet (voice in mix)", adj_wet, 2))

            adj_pr = Gtk.Adjustment(
                value=sess.presence_db,
                lower=0.0,
                upper=10.0,
                step_increment=0.1,
                page_increment=1.0,
            )
            adj_pr.connect("value-changed", self._on_presence)
            self.adj_pr = adj_pr
            mix_card.append(self._slider_block("Clarity / presence (dB)", adj_pr, 1))

            adj_rev = Gtk.Adjustment(
                value=sess.reverb_mix,
                lower=0.0,
                upper=1.0,
                step_increment=0.02,
                page_increment=0.1,
            )
            adj_rev.connect("value-changed", self._on_reverb_mix)
            self.adj_reverb_mix = adj_rev
            mix_card.append(self._slider_block("Reverb wet", adj_rev, 2))

            adj_rm = Gtk.Adjustment(
                value=sess.reverb_room,
                lower=0.0,
                upper=1.0,
                step_increment=0.02,
                page_increment=0.1,
            )
            adj_rm.connect("value-changed", self._on_reverb_room)
            self.adj_reverb_room = adj_rm
            mix_card.append(self._slider_block("Reverb room / decay", adj_rm, 2))

            outer.append(mix_card)

            self.status_label = Gtk.Label(label=sess.display_status(), xalign=0, wrap=True)
            self.status_label.set_wrap_mode(Pango.WrapMode.WORD_CHAR)
            self.status_label.set_selectable(True)
            self.status_label.add_css_class("vm-mute-foot")
            outer.append(self.status_label)

            dev = Gtk.Label(
                label=f"In [{sess.in_dev}] · Out [{sess.out_dev}] · {sr} Hz",
                xalign=0,
            )
            dev.add_css_class("dim-label")
            outer.append(dev)

            self.diag_label = Gtk.Label(
                label=_full_pipeline_diag_text(sess, virt_ui_on=sess.use_pulse_virt_mic),
                xalign=0,
                wrap=True,
            )
            self.diag_label.set_wrap_mode(Pango.WrapMode.WORD_CHAR)
            self.diag_label.set_selectable(True)
            self.diag_label.add_css_class("vm-mute-foot")
            outer.append(self.diag_label)

            btn_routing = Gtk.Button(label="Full routing report…")
            btn_routing.set_tooltip_text(
                "Read-only checklist: defaults, null sink, PortAudio index, monitor path (same as "
                "./run.sh --diagnose-routing)."
            )
            btn_routing.connect("clicked", self._on_routing_report)
            btn_routing.set_halign(Gtk.Align.START)
            outer.append(btn_routing)

            self._diag_tick = 0

            self.connect("close-request", self._on_close_request)
            self._status_timer = GLib.timeout_add(200, self._tick_status)

        def _pulse_local_monitor_wanted(
            self,
            *,
            virt: bool | None = None,
            hear_monitor: bool | None = None,
        ) -> bool:
            """PipeWire: null-sink monitor → default sink loopback (Virtual mic + Hear me both ON)."""
            v = self.cb_virt.get_active() if virt is None else virt
            h = (
                self.cb_hear_monitor.get_active()
                if hear_monitor is None
                else hear_monitor
            )
            return bool(v and h)

        def _refill_carrier_dropdown(self, *, select_path: str | None = None) -> None:
            ensure_carrier_library_dir()
            self._carrier_paths = list_carrier_files()
            self._carrier_dropdown_suppress = True
            while self._carrier_model.get_n_items() > 0:
                self._carrier_model.remove(self._carrier_model.get_n_items() - 1)
            if not self._carrier_paths:
                self._carrier_model.append("(no files — add MP3/WAV/FLAC to the folder)")
                self.carrier_dropdown.set_sensitive(False)
                if self._carrier_model.get_n_items() > 0:
                    self.carrier_dropdown.set_selected(0)
                self._carrier_dropdown_suppress = False
                return
            self.carrier_dropdown.set_sensitive(True)
            for p in self._carrier_paths:
                self._carrier_model.append(p.name)
            sel = 0
            if select_path:
                try:
                    want = Path(select_path).expanduser().resolve()
                    for i, cp in enumerate(self._carrier_paths):
                        try:
                            if cp.resolve() == want:
                                sel = i
                                break
                        except OSError:
                            continue
                except OSError:
                    pass
            self.carrier_dropdown.set_selected(sel)
            self._carrier_dropdown_suppress = False

        def _on_carrier_dropdown_selected(self, dd: Gtk.DropDown, _pspec) -> None:
            if self._carrier_dropdown_suppress:
                return
            if not self._carrier_paths:
                return
            idx = int(dd.get_selected())
            if idx < 0 or idx >= len(self._carrier_paths):
                return
            p = self._carrier_paths[idx]
            self.path_entry.set_text(str(p))
            self.session.load_from_path(str(p), err_to_status=True)
            self.status_label.set_label(self.session.display_status())

        def _on_refresh_carrier_list(self, _btn: Gtk.Button) -> None:
            self._refill_carrier_dropdown(
                select_path=self.path_entry.get_text().strip() or None
            )

        def _on_open_carrier_folder(self, _btn: Gtk.Button) -> None:
            d = ensure_carrier_library_dir()
            try:
                if sys.platform == "win32":
                    os.startfile(str(d))  # type: ignore[attr-defined]
                elif sys.platform == "darwin":
                    subprocess.Popen(["open", str(d)], start_new_session=True)
                else:
                    subprocess.Popen(["xdg-open", str(d)], start_new_session=True)
            except Exception as e:
                self.session.last_status = f"Could not open folder: {e}"
                self.status_label.set_label(self.session.display_status())

        def _on_clean_mic_toggle(self, btn: Gtk.CheckButton) -> None:
            self.session.set_clean_mic(btn.get_active())
            self.status_label.set_label(self.session.display_status())

        def _toggle_row(self, title: str, subtitle: str, toggle: Gtk.CheckButton) -> Gtk.CheckButton:
            """Put labels inside the CheckButton so clicking the title or description toggles it (GTK 4)."""
            from gi.repository import Gtk as _Gtk
            from gi.repository import Pango

            text = _Gtk.Box(orientation=_Gtk.Orientation.VERTICAL, spacing=2)
            text.set_hexpand(True)
            lt = _Gtk.Label(label=title, xalign=0)
            lt.set_hexpand(True)
            st = _Gtk.Label(label=subtitle, xalign=0)
            st.add_css_class("dim-label")
            st.set_hexpand(True)
            st.set_wrap(True)
            st.set_wrap_mode(Pango.WrapMode.WORD_CHAR)
            text.append(lt)
            text.append(st)
            toggle.set_child(text)
            toggle.set_halign(_Gtk.Align.FILL)
            toggle.set_hexpand(True)
            toggle.set_margin_top(4)
            toggle.set_margin_bottom(4)
            return toggle

        def _slider_block(
            self,
            title: str,
            adj: Gtk.Adjustment,
            digits: int,
        ) -> Gtk.Box:
            """Numeric row using ``Gtk.SpinButton`` — avoids Gtk.Scale GtkGizmo min-size warnings on GTK 4."""
            from gi.repository import Gtk as _Gtk

            box = _Gtk.Box(orientation=_Gtk.Orientation.VERTICAL, spacing=4)
            row = _Gtk.Box(orientation=_Gtk.Orientation.HORIZONTAL, spacing=12)
            lab = _Gtk.Label(label=title, xalign=0)
            lab.set_hexpand(True)
            lab.set_valign(_Gtk.Align.CENTER)
            spin = _Gtk.SpinButton()
            spin.set_adjustment(adj)
            spin.set_digits(digits)
            spin.set_numeric(True)
            spin.set_increments(adj.get_step_increment(), adj.get_page_increment())
            spin.set_valign(_Gtk.Align.CENTER)
            spin.set_width_chars(8)
            row.append(lab)
            row.append(spin)
            box.append(row)
            return box

        def _refresh_pulse_diag(self) -> None:
            self.diag_label.set_label(
                _full_pipeline_diag_text(
                    self.session,
                    virt_ui_on=self.cb_virt.get_active(),
                )
            )

        def _tick_status(self) -> bool:
            self.status_label.set_label(self.session.display_status())
            self._diag_tick += 1
            if self._diag_tick % 5 == 0:
                self._refresh_pulse_diag()
            return True

        def _on_routing_report(self, _btn: Gtk.Button) -> None:
            from diagnose_routing import format_diagnosis_report

            body = format_diagnosis_report(
                self.session.pulse_sink_name,
                self.session.pulse_sink_description,
            )
            win = Gtk.Window(
                transient_for=self,
                modal=True,
                title="Pulse / PortAudio routing",
            )
            win.set_default_size(560, 440)
            vbox = Gtk.Box(orientation=Gtk.Orientation.VERTICAL, spacing=8)
            vbox.set_margin_top(12)
            vbox.set_margin_bottom(12)
            vbox.set_margin_start(12)
            vbox.set_margin_end(12)
            tv = Gtk.TextView()
            tv.set_monospace(True)
            tv.get_buffer().set_text(body)
            tv.set_editable(False)
            tv.set_wrap_mode(Gtk.WrapMode.WORD_CHAR)
            sw = Gtk.ScrolledWindow()
            sw.set_policy(Gtk.PolicyType.AUTOMATIC, Gtk.PolicyType.AUTOMATIC)
            sw.set_vexpand(True)
            sw.set_hexpand(True)
            sw.set_child(tv)
            btn_close = Gtk.Button(label="Close")
            btn_close.connect("clicked", lambda *_w: win.close())
            vbox.append(sw)
            vbox.append(btn_close)
            win.set_child(vbox)
            win.present()

        def _on_close_request(self, *_args) -> bool:
            if self._status_timer:
                GLib.source_remove(self._status_timer)
                self._status_timer = 0
            self.session.shutdown()
            return False

        def _sync_from_widgets(self) -> None:
            mon = self.cb_hear_monitor.get_active()
            self.session.set_hear_self(False)
            self.session.set_hear_vocoded(True)
            self.session.set_clean_mic(self.cb_clean_mic.get_active())
            self.session.use_pulse_virt_mic = self.cb_virt.get_active()
            self.session.set_mute_speakers(not mon)
            self.session.set_mute_vocoded(not mon)
            self.session.set_virt_mic_hear_loopback(self._pulse_local_monitor_wanted())
            self.session.set_virt_mic_device_hint(self.virt_dev_entry.get_text())
            self.session.set_reverb_mix(self.adj_reverb_mix.get_value())
            self.session.set_reverb_room(self.adj_reverb_room.get_value())

        def _on_preset(self, _btn: Gtk.Button, key: str) -> None:
            if key not in _VOICE_PRESETS:
                return
            wet, pr, rmix, rroom = _VOICE_PRESETS[key]
            self.adj_wet.set_value(wet)
            self.adj_pr.set_value(pr)
            self.adj_reverb_mix.set_value(rmix)
            self.adj_reverb_room.set_value(rroom)
            self.session.set_wet(wet)
            self.session.set_presence(pr)
            self.session.set_reverb_mix(rmix)
            self.session.set_reverb_room(rroom)
            self.status_label.set_label(self.session.display_status())

        def _on_browse(self, _btn: Gtk.Button) -> None:
            from gi.repository import Gtk as _Gtk

            dlg = _Gtk.FileChooserNative(
                title="Carrier audio",
                transient_for=self,
                action=_Gtk.FileChooserAction.OPEN,
                accept_label="_Open",
                cancel_label="_Cancel",
            )
            filt = _Gtk.FileFilter()
            filt.set_name("Audio")
            for pat in ("*.mp3", "*.wav", "*.flac", "*.ogg", "*.m4a", "*.aac"):
                filt.add_pattern(pat)
            dlg.add_filter(filt)
            dlg.connect("response", self._on_file_response)
            dlg.show()

        def _on_file_response(self, dlg, response: int) -> None:
            from gi.repository import Gtk as _Gtk

            if response == _Gtk.ResponseType.ACCEPT:
                g = dlg.get_file()
                if g:
                    p = g.get_path()
                    if p:
                        self.path_entry.set_text(p)
                        self.session.load_from_path(p)
                        self._refill_carrier_dropdown(select_path=p)
                        self.status_label.set_label(self.session.display_status())
            dlg.destroy()

        def _on_load_apply(self, _btn: Gtk.Button) -> None:
            self.session.load_from_path(self.path_entry.get_text())
            self.session.apply_carrier_live()
            self._refill_carrier_dropdown(
                select_path=self.path_entry.get_text().strip() or None
            )
            self.status_label.set_label(self.session.display_status())

        def _on_start(self, _btn: Gtk.Button) -> None:
            self._sync_from_widgets()
            if self.session.carrier_arr is None:
                self.session.load_from_path(self.path_entry.get_text())
            if self.session.carrier_arr is None:
                self.status_label.set_label("Load a valid carrier file first.")
                return
            self.session.start()
            self.status_label.set_label(self.session.display_status())
            self._refresh_pulse_diag()

        def _on_stop(self, _btn: Gtk.Button) -> None:
            self.session.stop()
            self.status_label.set_label(self.session.display_status())
            self._refresh_pulse_diag()

        def _on_virt_toggle(self, _btn: Gtk.CheckButton) -> None:
            state = _btn.get_active()
            if self.session.is_streaming() and state != self.session._stream_started_with_virt_mic:
                _btn.handler_block_by_func(self._on_virt_toggle)
                try:
                    _btn.set_active(self.session._stream_started_with_virt_mic)
                finally:
                    _btn.handler_unblock_by_func(self._on_virt_toggle)
                self.session.last_status = (
                    "Stop streaming first, then change Virtual mic (Pulse), then Start again."
                )
                self.status_label.set_label(self.session.display_status())
                return
            self.session.use_pulse_virt_mic = state
            if sys.platform.startswith("linux") and state:
                from pulse_virtmic import apply_pulse_sink_env

                apply_pulse_sink_env(self.session.pulse_sink_name)
            mon = self.cb_hear_monitor.get_active()
            self.session.set_hear_self(False)
            self.session.set_mute_speakers(not mon)
            self.session.set_mute_vocoded(not mon)
            self.session.set_virt_mic_hear_loopback(
                self._pulse_local_monitor_wanted(virt=state)
            )
            self._refresh_pulse_diag()

        def _on_hear_monitor_toggle(self, _btn: Gtk.CheckButton) -> None:
            on = _btn.get_active()
            self.session.set_hear_self(False)
            self.session.set_hear_vocoded(True)
            self.session.set_mute_speakers(not on)
            self.session.set_mute_vocoded(not on)
            self.session.set_virt_mic_hear_loopback(self._pulse_local_monitor_wanted())

        def _on_virt_dev_changed(self, entry: Gtk.Entry) -> None:
            self.session.set_virt_mic_device_hint(entry.get_text())

        def _on_gain(self, adj: Gtk.Adjustment) -> None:
            self.session.set_gain(adj.get_value())

        def _on_wet(self, adj: Gtk.Adjustment) -> None:
            self.session.set_wet(adj.get_value())

        def _on_presence(self, adj: Gtk.Adjustment) -> None:
            self.session.set_presence(adj.get_value())

        def _on_reverb_mix(self, adj: Gtk.Adjustment) -> None:
            self.session.set_reverb_mix(adj.get_value())

        def _on_reverb_room(self, adj: Gtk.Adjustment) -> None:
            self.session.set_reverb_room(adj.get_value())

    class VocoderApp(Gtk.Application):
        def __init__(self) -> None:
            super().__init__(
                application_id="local.live_vocoder.gtk",
                flags=Gio.ApplicationFlags.DEFAULT_FLAGS,
            )
            self._sess = session

        def do_activate(self) -> None:
            win = self.props.active_window
            if win is None:
                win = VocoderWindow(self, self._sess)
            win.present()

    app = VocoderApp()
    return app.run(None)


if __name__ == "__main__":
    raise SystemExit(run_gtk_gui())
